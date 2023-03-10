/*
 *  Copyright (C) 2012, Samsung Electronics Co. Ltd. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/i2c/mxts.h>
#include <asm/unaligned.h>
#include <linux/firmware.h>
#include <linux/string.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>

//#define GPIO_TOUCH_SCL	19
//#define GPIO_TOUCH_SDA	18

extern int poweroff_charging;

#if CHECK_ANTITOUCH
#define MXT_T61_TIMER_ONESHOT			0
#define MXT_T61_TIMER_REPEAT			1
#define MXT_T61_TIMER_CMD_START		1
#define MXT_T61_TIMER_CMD_STOP		2
#endif


#if ENABLE_TOUCH_KEY
int tsp_keycodes[NUMOFKEYS] = {
	KEY_MENU,
	KEY_BACK,
};
char *tsp_keyname[NUMOFKEYS] = {
	"Menu",
	"Back",
};
static u16 tsp_keystatus;
#endif

int touch_is_pressed;
EXPORT_SYMBOL(touch_is_pressed);

static int calibrate_chip(struct mxt_data *data);
static int mxt_reset(struct mxt_data *data);

#if TOUCH_BOOSTER
static void change_dvfs_lock(struct work_struct *work)
{
	struct mxt_data *data = container_of(work,
				struct mxt_data, work_dvfs_chg.work);
	int ret;
	mutex_lock(&data->dvfs_lock);
	ret = set_freq_limit(DVFS_TOUCH_ID, 998400);
	mutex_unlock(&data->dvfs_lock);

	if (ret < 0)
		printk(KERN_ERR "%s: 1booster stop failed(%d)\n",\
					__func__, __LINE__);
	else
		printk(KERN_INFO "[TSP] %s", __func__);
}

static void set_dvfs_off(struct work_struct *work)
{
	struct mxt_data *data = container_of(work,
				struct mxt_data, work_dvfs_off.work);
	mutex_lock(&data->dvfs_lock);
	set_freq_limit(DVFS_TOUCH_ID, -1);
	data->dvfs_lock_status = false;
	mutex_unlock(&data->dvfs_lock);

}

static void set_dvfs_lock(struct mxt_data *data, uint32_t on)
{
	int ret = 0;

	mutex_lock(&data->dvfs_lock);
	if (on == 0) {
		if (data->dvfs_lock_status) {
			schedule_delayed_work(&data->work_dvfs_off,
				msecs_to_jiffies(TOUCH_BOOSTER_OFF_TIME));
		}
	} else if (on == 1) {
		cancel_delayed_work(&data->work_dvfs_off);
		if (!data->dvfs_lock_status) {
			ret = set_freq_limit(DVFS_TOUCH_ID, 998400);
			if (ret < 0)
				printk(KERN_ERR "%s: cpu lock failed(%d)\n",\
							__func__, ret);

			data->dvfs_lock_status = true;
		}
	} else if (on == 2) {
		cancel_delayed_work(&data->work_dvfs_off);
		schedule_work(&data->work_dvfs_off.work);
	}
	mutex_unlock(&data->dvfs_lock);
}


#endif /* - TOUCH_BOOSTER */


static int mxt_read_mem(struct mxt_data *data, u16 reg, u8 len, void *buf)
{
	int ret = 0, i = 0;
	u16 le_reg = cpu_to_le16(reg);
	struct i2c_msg msg[2] = {
		{
			.addr = data->client->addr,
			.flags = 0,
			.len = 2,
			.buf = (u8 *)&le_reg,
		},
		{
			.addr = data->client->addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = buf,
		},
	};

#if TSP_USE_ATMELDBG
	if (data->atmeldbg.block_access)
		return 0;
#endif

	for (i = 0; i < 3 ; i++) {
		ret = i2c_transfer(data->client->adapter, msg, 2);
		if (ret < 0){
			dev_err(&data->client->dev, "%s fail[%d] address[0x%x]\n", __func__, ret, le_reg);
			udelay(50);
		}else
			break;
	}
	return ret == 2 ? 0 : -EIO;
}

static int mxt_write_mem(struct mxt_data *data,
		u16 reg, u8 len, const u8 *buf)
{
	int ret = 0, i = 0;
	u8 tmp[len + 2];

#if TSP_USE_ATMELDBG
	if (data->atmeldbg.block_access)
		return 0;
#endif

	put_unaligned_le16(cpu_to_le16(reg), tmp);
	memcpy(tmp + 2, buf, len);

	for (i = 0; i < 3 ; i++) {
		ret = i2c_master_send(data->client, tmp, sizeof(tmp));
		if (ret < 0){
			dev_err(&data->client->dev,	"%s %d times write error on address[0x%x,0x%x]\n", __func__, i, tmp[1], tmp[0]);
			udelay(50);
		}else
			break;
	}

	return ret == sizeof(tmp) ? 0 : -EIO;
}

static struct mxt_object *
	mxt_get_object(struct mxt_data *data, u8 type)
{
	struct mxt_object *object;
	int i;

	if (!data->objects)
		return NULL;

	for (i = 0; i < data->info.object_num; i++) {
		object = data->objects + i;
		if (object->type == type)
			return object;
	}

	dev_err(&data->client->dev, "Invalid object type T%d\n",
		type);

	return NULL;
}

static int mxt_read_message(struct mxt_data *data,
				 struct mxt_message *message)
{
	struct mxt_object *object;

	object = mxt_get_object(data, MXT_GEN_MESSAGEPROCESSOR_T5);
	if (!object)
		return -EINVAL;

	return mxt_read_mem(data, object->start_address,
			sizeof(struct mxt_message), message);
}

static int mxt_read_message_reportid(struct mxt_data *data,
	struct mxt_message *message, u8 reportid)
{
	int try = 0;
	int error;
	int fail_count;

	fail_count = data->max_reportid * 2;

	while (++try < fail_count) {
		error = mxt_read_message(data, message);
		if (error)
			return error;

		if (message->reportid == 0xff)
			continue;

		if (message->reportid == reportid)
			return 0;
	}

	return -EINVAL;
}

static int mxt_read_object(struct mxt_data *data,
				u8 type, u8 offset, u8 *val)
{
	struct mxt_object *object;
	int error = 0;

	object = mxt_get_object(data, type);
	if (!object)
		return -EINVAL;

	error = mxt_read_mem(data, object->start_address + offset, 1, val);
	if (error)
		dev_err(&data->client->dev, "Error to read T[%d] offset[%d] val[%d]\n",
			type, offset, *val);
	return error;
}

static int mxt_write_object(struct mxt_data *data,
				 u8 type, u8 offset, u8 val)
{
	struct mxt_object *object;
	int error = 0;
	u16 reg;

	object = mxt_get_object(data, type);
	if (!object)
		return -EINVAL;

	if (offset >= object->size * object->instances) {
		dev_err(&data->client->dev, "Tried to write outside object T%d offset:%d, size:%d\n",
			type, offset, object->size);
		return -EINVAL;
	}
	reg = object->start_address;
	error = mxt_write_mem(data, reg + offset, 1, &val);
	if (error)
		dev_err(&data->client->dev, "Error to write T[%d] offset[%d] val[%d]\n",
			type, offset, val);

	return error;
}

static u32 mxt_make_crc24(u32 crc, u8 byte1, u8 byte2)
{
	static const u32 crcpoly = 0x80001B;
	u32 res;
	u16 data_word;

	data_word = (((u16)byte2) << 8) | byte1;
	res = (crc << 1) ^ (u32)data_word;

	if (res & 0x1000000)
		res ^= crcpoly;

	return res;
}

static int mxt_calculate_infoblock_crc(struct mxt_data *data,
		u32 *crc_pointer)
{
	u32 crc = 0;
	u8 mem[7 + data->info.object_num * 6];
	int ret;
	int i;

	ret = mxt_read_mem(data, 0, sizeof(mem), mem);

	if (ret)
		return ret;

	for (i = 0; i < sizeof(mem) - 1; i += 2)
		crc = mxt_make_crc24(crc, mem[i], mem[i + 1]);

	*crc_pointer = mxt_make_crc24(crc, mem[i], 0) & 0x00FFFFFF;

	return 0;
}

static int mxt_read_info_crc(struct mxt_data *data, u32 *crc_pointer)
{
	u16 crc_address;
	u8 msg[3];
	int ret;

	/* Read Info block CRC address */
	crc_address = MXT_OBJECT_TABLE_START_ADDRESS +
			data->info.object_num * MXT_OBJECT_TABLE_ELEMENT_SIZE;

	ret = mxt_read_mem(data, crc_address, 3, msg);
	if (ret)
		return ret;

	*crc_pointer = msg[0] | (msg[1] << 8) | (msg[2] << 16);

	return 0;
}
static int mxt_read_config_crc(struct mxt_data *data, u32 *crc)
{
	struct device *dev = &data->client->dev;
	struct mxt_message message;
	struct mxt_object *object;
	int error;

	object = mxt_get_object(data, MXT_GEN_COMMANDPROCESSOR_T6);
	if (!object)
		return -EIO;

	/* Try to read the config checksum of the existing cfg */
	mxt_write_object(data, MXT_GEN_COMMANDPROCESSOR_T6,
		MXT_COMMAND_REPORTALL, 1);

	/* Read message from command processor, which only has one report ID */
	error = mxt_read_message_reportid(data, &message, object->max_reportid);
	if (error) {
		dev_err(dev, "Failed to retrieve CRC\n");
		return error;
	}

	/* Bytes 1-3 are the checksum. */
	*crc = message.message[1] | (message.message[2] << 8) |
		(message.message[3] << 16);

	return 0;
}

#if CHECK_ANTITOUCH
void mxt_t61_timer_set(struct mxt_data *data, u8 mode, u8 cmd, u16 msPeriod)					
{
	struct mxt_object *object;															
	int ret = 0;																		
	u16 reg;																			
	u8 buf[5] = {3, 0, 0, 0, 0};

	buf[1] = cmd;
	buf[2] = mode;
	buf[3] = msPeriod & 0xFF;
	buf[4] = (msPeriod >> 8) & 0xFF;

	object = mxt_get_object(data, MXT_SPT_TIMER_T61);									
	reg = object->start_address;
	ret = mxt_write_mem(data, reg+0, 5,(const u8*)&buf);

	pr_info("[TSP] T61 Timer Enabled %d\n", msPeriod);

	
}

void mxt_t8_cal_set(struct mxt_data *data, u8 mstime)
{
//	int ret = 0;
//	u16 size = 0;
//	u16 obj_address = 0;

	if (mstime)
		data->pdata->check_autocal = 1;							
	else
		data->pdata->check_autocal = 0;

	mxt_write_object(data, MXT_GEN_ACQUISITIONCONFIG_T8,4, mstime);

}

static int diff_two_point(s16 x, s16 y, s16 oldx, s16 oldy)
{
	s16 diffx, diffy;
	s16 distance;

	diffx = x-oldx;
	diffy = y-oldy;
	distance = abs(diffx) + abs(diffy);

	return distance;
}

static void mxt_check_coordinate(struct mxt_data *data,
				u8 detect, u8 id,
				s16 x, s16 y)
{
	int i;
	if (detect) {
		data->tcount[id] = 0;
		data->distance[id] = 0;
	} else {
		data->distance[id] = diff_two_point(x, y,
		data->touchbx[id], data->touchby[id]);
		
		if (data->distance[id] < 3) {///0730
			if (data->atch_value >= data->tch_value) {
				data->release_max = 3;
				if (data->tcount[id] < 20000)
					data->tcount[id]++;
			} else if ((data->atch_value +
				data->tch_value) >= 80) {
				data->release_max = 10;
				if (data->tcount[id] < 20000)
					data->tcount[id]++;
			}
		} else
			data->tcount[id] = 0;
	}

	data->touchbx[id] = x;
	data->touchby[id] = y;

	if (id >= data->old_id)
		data->max_id = id;
	else
		data->max_id = data->old_id;

	data->old_id = id;
	
	if (!data->Press_Release_check) {///0730
		if (data->Report_touch_number > 0) {
			for (i = 0;  i <= data->max_id; i++) {
				if (data->tcount[i] > data->release_max) {
						data->Press_cnt = 0;
						data->Release_cnt = 0;
						data->Press_Release_check = 1;
						data->release_max = 3;
						calibrate_chip(data);
						pr_info("[TSP] Recal for Pattern tracking\n");
				}
			}
		}
	 }

}

#endif	/* CHECK_ANTITOUCH */

static int mxt_check_instance(struct mxt_data *data, u8 type)
{
	int i;

	for (i = 0; i < data->info.object_num; i++) {
		if (data->objects[i].type == type)
			return data->objects[i].instances;
	}
	return 0;
}

static int mxt_init_write_config(struct mxt_data *data,
		u8 type, const u8 *cfg)
{
	struct mxt_object *object;
	u8 *temp;
	int ret;

	object = mxt_get_object(data, type);
	if (!object)
		return -EINVAL;

	if ((object->size == 0) || (object->start_address == 0)) {
		dev_err(&data->client->dev,	"%s error T%d\n",
			 __func__, type);
		return -ENODEV;
	}

	ret = mxt_write_mem(data, object->start_address,
			object->size, cfg);
	if (ret) {
		dev_err(&data->client->dev,	"%s write error T%d address[0x%x]\n",
			__func__, type, object->start_address);
		return ret;
	}

	if (mxt_check_instance(data, type)) {
		temp = kzalloc(object->size, GFP_KERNEL);

		if (temp == NULL)
			return -ENOMEM;

		ret |= mxt_write_mem(data, object->start_address + object->size,
			object->size, temp);
		kfree(temp);
	}

	return ret;
}

static int mxt_write_config_from_pdata(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	u8 **tsp_config = (u8 **)data->pdata->config;
	u8 i;
	int ret;

	if (!tsp_config) {
		dev_info(dev, "No cfg data in pdata\n");
		return 0;
	}

	for (i = 0; tsp_config[i][0] != MXT_RESERVED_T255; i++) {
		ret = mxt_init_write_config(data, tsp_config[i][0],
							tsp_config[i] + 1);
		if (ret)
			return ret;
	}
	return ret;
}

#if DUAL_CFG
static int mxt_write_config(struct mxt_fw_info *fw_info)
{
	struct mxt_data *data = fw_info->data;
	struct device *dev = &data->client->dev;
	struct mxt_object *object;
	struct mxt_cfg_data *cfg_data;
	u32 current_crc;
	u8 i, val = 0;
	u16 reg, index;
	int ret;
	u32 cfg_length = data->cfg_len = fw_info->cfg_len / 2 ;

	if (!fw_info->ta_cfg_raw_data && !fw_info->batt_cfg_raw_data) {
		dev_info(dev, "No cfg data in file\n");
		ret = mxt_write_config_from_pdata(data);
		return ret;
	}

	/* Get config CRC from device */
	ret = mxt_read_config_crc(data, &current_crc);
	if (ret)
		return ret;

	/* Check Version information */
	if (fw_info->fw_ver != data->info.version) {
		dev_err(dev, "Warning: version mismatch! %s\n", __func__);
		return 0;
	}
	if (fw_info->build_ver != data->info.build) {
		dev_err(dev, "Warning: build num mismatch! %s\n", __func__);
		return 0;
	}

	/* Check config CRC */
	if (current_crc == fw_info->cfg_crc) {
		dev_info(dev, "Skip writing Config:[CRC 0x%06X]\n",
			current_crc);
		return 0;
	}

	dev_info(dev, "Writing Config:[CRC 0x%06X!=0x%06X]\n",
		current_crc, fw_info->cfg_crc);

	/* Get the address of configuration data */
	data->batt_cfg_raw_data = fw_info->batt_cfg_raw_data;
	data->ta_cfg_raw_data = fw_info->ta_cfg_raw_data =
		fw_info->batt_cfg_raw_data + cfg_length;

	/* Write config info */
	for (index = 0; index < cfg_length;) {
		if (index + sizeof(struct mxt_cfg_data) >= cfg_length) {
			dev_err(dev, "index(%d) of cfg_data exceeded total size(%d)!!\n",
				index + sizeof(struct mxt_cfg_data),
				cfg_length);
			return -EINVAL;
		}

		/* Get the info about each object */
		if (data->charging_mode)
			cfg_data = (struct mxt_cfg_data *)
					(&fw_info->ta_cfg_raw_data[index]);
		else
			cfg_data = (struct mxt_cfg_data *)
					(&fw_info->batt_cfg_raw_data[index]);

		index += sizeof(struct mxt_cfg_data) + cfg_data->size;
		if (index > cfg_length) {
			dev_err(dev, "index(%d) of cfg_data exceeded total size(%d) in T%d object!!\n",
				index, cfg_length, cfg_data->type);
			//return -EINVAL;
		}

		object = mxt_get_object(data, cfg_data->type);
		if (!object) {
			dev_err(dev, "T%d is Invalid object type\n",
				cfg_data->type);
			return -EINVAL;
		}

		/* Check and compare the size, instance of each object */
		if (cfg_data->size > object->size) {
			dev_err(dev, "T%d Object length exceeded!\n",
				cfg_data->type);
			return -EINVAL;
		}
		if (cfg_data->instance >= object->instances) {
			dev_err(dev, "T%d Object instances exceeded!\n",
				cfg_data->type);
			return -EINVAL;
		}

		dev_dbg(dev, "Writing config for obj %d len %d instance %d (%d/%d)\n",
			cfg_data->type, object->size,
			cfg_data->instance, index, cfg_length);

		reg = object->start_address + object->size * cfg_data->instance;

		/* Write register values of each object */
		ret = mxt_write_mem(data, reg, cfg_data->size,
					 cfg_data->register_val);
		if (ret) {
			dev_err(dev, "Write T%d Object failed\n",
				object->type);
			return ret;
		}

		/*
		 * If firmware is upgraded, new bytes may be added to end of
		 * objects. It is generally forward compatible to zero these
		 * bytes - previous behaviour will be retained. However
		 * this does invalidate the CRC and will force a config
		 * download every time until the configuration is updated.
		 */
		if (cfg_data->size < object->size) {
			dev_err(dev, "Warning: zeroing %d byte(s) in T%d\n",
				 object->size - cfg_data->size, cfg_data->type);

			for (i = cfg_data->size + 1; i < object->size; i++) {
				ret = mxt_write_mem(data, reg + i, 1, &val);
				if (ret)
					return ret;
			}
		}
	}
	dev_info(dev, "Updated configuration\n");

	return ret;
}
#else
static int mxt_write_config(struct mxt_fw_info *fw_info)
{
	struct mxt_data *data = fw_info->data;
	struct device *dev = &data->client->dev;
	struct mxt_object *object;
	struct mxt_cfg_data *cfg_data;
	u32 current_crc;
	u8 i, val = 0;
	u16 reg, index;
	int ret;

	if (!fw_info->cfg_raw_data) {
		dev_info(dev, "No cfg data in file\n");
		ret = mxt_write_config_from_pdata(data);
		return ret;
	}

	/* Get config CRC from device */
	ret = mxt_read_config_crc(data, &current_crc);
	if (ret)
		return ret;

	/* Check Version information */
	if (fw_info->fw_ver != data->info.version) {
		dev_err(dev, "Warning: version mismatch! %s\n", __func__);
		return 0;
	}
	if (fw_info->build_ver != data->info.build) {
		dev_err(dev, "Warning: build num mismatch! %s\n", __func__);
		return 0;
	}

	/* Check config CRC */
	if (current_crc == fw_info->cfg_crc) {
		dev_info(dev, "Skip writing Config:[CRC 0x%06X]\n",
			current_crc);
		return 0;
	}

	dev_info(dev, "Writing Config:[CRC 0x%06X!=0x%06X]\n",
		current_crc, fw_info->cfg_crc);

	/* Write config info */
	for (index = 0; index < fw_info->cfg_len;) {

		if (index + sizeof(struct mxt_cfg_data) >= fw_info->cfg_len) {
			dev_err(dev, "index(%d) of cfg_data exceeded total size(%d)!!\n",
				index + sizeof(struct mxt_cfg_data),
				fw_info->cfg_len);
			return -EINVAL;
		}

		/* Get the info about each object */
		cfg_data = (struct mxt_cfg_data *)
					(&fw_info->cfg_raw_data[index]);

		index += sizeof(struct mxt_cfg_data) + cfg_data->size;
		if (index > fw_info->cfg_len) {
			dev_err(dev, "index(%d) of cfg_data exceeded total size(%d) in T%d object!!\n",
				index, fw_info->cfg_len, cfg_data->type);
			return -EINVAL;
		}

		object = mxt_get_object(data, cfg_data->type);
		if (!object) {
			dev_err(dev, "T%d is Invalid object type\n",
				cfg_data->type);
			return -EINVAL;
		}

		/* Check and compare the size, instance of each object */
		if (cfg_data->size > object->size) {
			dev_err(dev, "T%d Object length exceeded!\n",
				cfg_data->type);
			return -EINVAL;
		}
		if (cfg_data->instance >= object->instances) {
			dev_err(dev, "T%d Object instances exceeded!\n",
				cfg_data->type);
			return -EINVAL;
		}

		dev_dbg(dev, "Writing config for obj %d len %d instance %d (%d/%d)\n",
			cfg_data->type, object->size,
			cfg_data->instance, index, fw_info->cfg_len);

		reg = object->start_address + object->size * cfg_data->instance;

		/* Write register values of each object */
		ret = mxt_write_mem(data, reg, cfg_data->size,
					 cfg_data->register_val);
		if (ret) {
			dev_err(dev, "Write T%d Object failed\n",
				object->type);
			return ret;
		}

		/*
		 * If firmware is upgraded, new bytes may be added to end of
		 * objects. It is generally forward compatible to zero these
		 * bytes - previous behaviour will be retained. However
		 * this does invalidate the CRC and will force a config
		 * download every time until the configuration is updated.
		 */
		if (cfg_data->size < object->size) {
			dev_err(dev, "Warning: zeroing %d byte(s) in T%d\n",
				 object->size - cfg_data->size, cfg_data->type);

			for (i = cfg_data->size + 1; i < object->size; i++) {
				ret = mxt_write_mem(data, reg + i, 1, &val);
				if (ret)
					return ret;
			}
		}
	}

	dev_info(dev, "Updated configuration\n");

	return ret;
}
#endif

#if TSP_PATCH
#include "mxts_patch.c"
#endif

/* TODO TEMP_ADONIS: need to inspect below functions */
#if TSP_INFORM_CHARGER

static int set_charger_config(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	struct mxt_object *object;
	struct mxt_cfg_data *cfg_data;
	u8 i, val = 0;
	u16 reg, index;
	int ret;
	
	printk("[TSP] %s, %d\n",__func__, __LINE__);
	
	dev_info(&data->client->dev, "Current state is %s",
		data->charging_mode ? "Charging mode" : "Battery mode");

/* if you need to change configuration depend on chager detection,
 * please insert below line.
 */
	
	dev_dbg(dev, "set_charger_config data->cfg_len = %d\n", data->cfg_len);

	for (index = 0; index < data->cfg_len;) {
		if (index + sizeof(struct mxt_cfg_data) >= data->cfg_len) {
			dev_err(dev, "index(%d) of cfg_data exceeded total size(%d)!!\n",
				index + sizeof(struct mxt_cfg_data),
				data->cfg_len);
			return -EINVAL;
		}

		/* Get the info about each object */
		if (data->charging_mode)
			cfg_data = (struct mxt_cfg_data *)
					(&data->ta_cfg_raw_data[index]);
		else
			cfg_data = (struct mxt_cfg_data *)
					(&data->batt_cfg_raw_data[index]);

		index += sizeof(struct mxt_cfg_data) + cfg_data->size;
		if (index > data->cfg_len) {
			dev_err(dev, "index(%d) of cfg_data exceeded total size(%d) in T%d object!!\n",
				index, data->cfg_len, cfg_data->type);
			return -EINVAL;
		}

		object = mxt_get_object(data, cfg_data->type);
		if (!object) {
			dev_err(dev, "T%d is Invalid object type\n",
				cfg_data->type);
			return -EINVAL;
		}

		/* Check and compare the size, instance of each object */
		if (cfg_data->size > object->size) {
			dev_err(dev, "T%d Object length exceeded!\n",
				cfg_data->type);
			return -EINVAL;
		}
		if (cfg_data->instance >= object->instances) {
			dev_err(dev, "T%d Object instances exceeded!\n",
				cfg_data->type);
			return -EINVAL;
		}

		dev_dbg(dev, "Writing config for obj %d len %d instance %d (%d/%d)\n",
			cfg_data->type, object->size,
			cfg_data->instance, index, data->cfg_len);

		reg = object->start_address + object->size * cfg_data->instance;

		/* Write register values of each object */
		ret = mxt_write_mem(data, reg, cfg_data->size,
					 cfg_data->register_val);
		if (ret) {
			dev_err(dev, "Write T%d Object failed\n",
				object->type);
			return ret;
		}

		/*
		 * If firmware is upgraded, new bytes may be added to end of
		 * objects. It is generally forward compatible to zero these
		 * bytes - previous behaviour will be retained. However
		 * this does invalidate the CRC and will force a config
		 * download every time until the configuration is updated.
		 */
		if (cfg_data->size < object->size) {
			dev_err(dev, "Warning: zeroing %d byte(s) in T%d\n",
				 object->size - cfg_data->size, cfg_data->type);

			for (i = cfg_data->size + 1; i < object->size; i++) {
				ret = mxt_write_mem(data, reg + i, 1, &val);
				if (ret)
					return ret;
			}
		}
	}

#if TSP_PATCH
	if (data->charging_mode){
		if(data->patch.event_cnt)
			mxt_patch_test_event(data, 1); 			
	}
	else{
		if(data->patch.event_cnt)
			mxt_patch_test_event(data, 0); 			
	}
#endif
	calibrate_chip(data);									
	return ret;
}

struct tsp_callbacks *charger_callbacks;
void tsp_charger_infom(bool en)
{
	printk("[TSP] %s, en=%d (%d )\n",__func__, en, __LINE__);
 	if (charger_callbacks && charger_callbacks->inform_charger){
 		charger_callbacks->inform_charger(charger_callbacks, en);
	}
}

static void inform_charger(struct tsp_callbacks *cb,
	bool en)
{
	struct mxt_data *data = container_of(cb,
			struct mxt_data, callbacks);

	printk("[TSP] %s, %d\n",__func__, __LINE__);

	cancel_delayed_work_sync(&data->noti_dwork);
	data->charging_mode = en;
	schedule_delayed_work(&data->noti_dwork, HZ / 5);
}

static void charger_noti_dwork(struct work_struct *work)
{
	struct mxt_data *data =
		container_of(work, struct mxt_data,
		noti_dwork.work);

	printk("[TSP] %s, %d\n",__func__, __LINE__);

	if (!data->mxt_enabled) {
		schedule_delayed_work(&data->noti_dwork, HZ / 5);
		return ;
	}

	dev_info(&data->client->dev, "%s mode\n",
		data->charging_mode ? "charging" : "battery");
	
#if	CHECK_ANTITOUCH
	data->Press_cnt = 0;
	data->Release_cnt = 0;
	data->Press_Release_check = 1;
#endif

	set_charger_config(data);
}

static void inform_charger_init(struct mxt_data *data)
{
	printk("[TSP] %s, %d\n",__func__, __LINE__);

	INIT_DELAYED_WORK(&data->noti_dwork, charger_noti_dwork);
}
#endif

static void mxt_report_input_data(struct mxt_data *data)
{
	int i;
	int count = 0;
	int report_count = 0;
	
	for (i = 0; i < MXT_MAX_FINGER; i++) {
		if (data->fingers[i].state == MXT_STATE_INACTIVE)
			continue;

		input_mt_slot(data->input_dev, i);
		if (data->fingers[i].state == MXT_STATE_RELEASE) {
			input_mt_report_slot_state(data->input_dev,
					MT_TOOL_FINGER, false);
		} else {
			input_mt_report_slot_state(data->input_dev,
					MT_TOOL_FINGER, true);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X,
					data->fingers[i].x);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y,
					data->fingers[i].y);
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR,
					data->fingers[i].w);
			input_report_abs(data->input_dev, ABS_MT_PRESSURE,
					 data->fingers[i].z);
#if TSP_USE_SHAPETOUCH
			/* Currently revision G firmware do not support it */
			if (data->pdata->revision == MXT_REVISION_I) {
				input_report_abs(data->input_dev,
					ABS_MT_COMPONENT,
					data->fingers[i].component);
				input_report_abs(data->input_dev,
					ABS_MT_SUMSIZE, data->sumsize);
			}
#endif
			input_report_key(data->input_dev,
				BTN_TOOL_FINGER, 1);

			if (data->fingers[i].type
				 == MXT_T100_TYPE_HOVERING_FINGER)
				/* hover is reported */
				input_report_key(data->input_dev,
					BTN_TOUCH, 0);
			else
				/* finger or passive stylus are reported */
				input_report_key(data->input_dev,
					BTN_TOUCH, 1);
		}
		report_count++;

#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
		if (data->fingers[i].state == MXT_STATE_PRESS)
			dev_info(&data->client->dev, "[P][%d]: T[%d][%d] X[%d],Y[%d]\n",
				i, data->fingers[i].type,
				data->fingers[i].event,
				data->fingers[i].x, data->fingers[i].y);
#else
		if (data->fingers[i].state == MXT_STATE_PRESS)
			dev_info(&data->client->dev, "[P][%d]: T[%d][%d]\n",
				i, data->fingers[i].type,
				data->fingers[i].event);
#endif
		else if (data->fingers[i].state == MXT_STATE_RELEASE)
			dev_info(&data->client->dev, "[R][%d]: T[%d][%d] M[%d]\n",
				i, data->fingers[i].type,
				data->fingers[i].event,
				data->fingers[i].mcount);


		if (data->fingers[i].state == MXT_STATE_RELEASE) {
			data->fingers[i].state = MXT_STATE_INACTIVE;
			data->fingers[i].mcount = 0;
		} else {
			data->fingers[i].state = MXT_STATE_MOVE;
			count++;
		}
	}

	if (count == 0) {
		input_report_key(data->input_dev, BTN_TOUCH, 0);
		input_report_key(data->input_dev, BTN_TOOL_FINGER, 0);
	}

	if (report_count > 0) {
#if TSP_USE_ATMELDBG
		if (!data->atmeldbg.stop_sync)
#endif
			input_sync(data->input_dev);
	}

	if (count)
		touch_is_pressed = 1;
	else
		touch_is_pressed = 0;

#if (TSP_USE_SHAPETOUCH || TOUCH_BOOSTER)
	/* all fingers are released */
	if (count == 0) {
#if TSP_USE_SHAPETOUCH
		data->sumsize = 0;
#endif
#if TOUCH_BOOSTER
	set_dvfs_lock(data, 2);
	printk("[TSP] %s : dvfs_lock free. (%d)",__func__,__LINE__);
#endif
	}
#endif

	data->finger_mask = 0;
}

static void mxt_release_all_finger(struct mxt_data *data)
{
	int i;
	int count = 0;

	for (i = 0; i < MXT_MAX_FINGER; i++) {
		if (data->fingers[i].state == MXT_STATE_INACTIVE)
			continue;
		data->fingers[i].z = 0;
		data->fingers[i].state = MXT_STATE_RELEASE;
		count++;
	}
	if (count) {
		dev_err(&data->client->dev, "%s\n", __func__);
		mxt_report_input_data(data);
	}
}

#if TSP_HOVER_WORKAROUND
static void mxt_current_calibration(struct mxt_data *data)
{
	dev_info(&data->client->dev, "%s\n", __func__);

	mxt_write_object(data, MXT_SPT_SELFCAPHOVERCTECONFIG_T102, 1, 1);
}
#endif

static int calibrate_chip(struct mxt_data *data)
{
	int ret = 0;
	/* send calibration command to the chip */
	if (data->cal_busy)
		return ret;

	ret = mxt_write_object(data, MXT_GEN_COMMANDPROCESSOR_T6,	MXT_COMMAND_CALIBRATE, 1 );		

	/* set flag for calibration lockup
	recovery if cal command was successful */
	data->cal_busy = 1;
	if (!ret)
		pr_info("[TSP] calibration success!!!\n");
	return ret;
}

#if CHECK_ANTITOUCH
static int mxt_dist_check(struct mxt_data *data)
{
	int i;
	u16 dist_sum = 0;

	for (i = 0; i <= data->max_id; i++) {
		if (data->distance[i] < 3)
			dist_sum++;
		else
			dist_sum = 0;
	}

	for (i = data->max_id + 1; i < MAX_USING_FINGER_NUM; i++)
		data->distance[i] = 0;

	return dist_sum;
}

static void mxt_tch_atch_area_check(struct mxt_data *data,
		int tch_area, int atch_area, int touch_area)
{
	u16 dist_sum = 0;
	unsigned char touch_num;

	touch_num = data->Report_touch_number;
	if (tch_area) {
		/* First Touch After Calibration */
		if (data->pdata->check_timer == 0) {
			data->coin_check = 0;
			mxt_t61_timer_set(data,
					MXT_T61_TIMER_ONESHOT,
					MXT_T61_TIMER_CMD_START,
					1000);
			data->pdata->check_timer = 1;
		}
	}

	if ((tch_area == 0) && (atch_area > 0)) {
		pr_info("[TSP] T57_Abnormal Status, tch=%d, atch=%d\n"
		, data->tch_value
		, data->atch_value);
		calibrate_chip(data);
		return;
	}
	dist_sum = mxt_dist_check(data);
	if (touch_num > 1 && tch_area <= 45) {
		if (touch_num == 2) {
			if (tch_area < atch_area-3) {
				pr_info("[TSP] Two Cal_Bad : tch area < atch_area-3, tch=%d, atch=%d\n"
				, data->tch_value
				, data->atch_value);
				calibrate_chip(data);
			} 
		}
		else if (tch_area <= (touch_num * 4 + 2)) {
			
			if (!data->coin_check) {
				if (dist_sum == (data->max_id + 1)) {
					if (touch_area < T_AREA_LOW_MT) {
						if (data->t_area_l_cnt >= 7) {
							pr_info("[TSP] Multi Cal maybe bad contion : Set autocal = 5, tch=%d, atch=%d\n"
							, data->tch_value
							, data->atch_value);
							mxt_t8_cal_set(data, 5);
							data->coin_check = 1;
							data->t_area_l_cnt = 0;
						} else {
							data->t_area_l_cnt++;
						}

						data->t_area_cnt = 0;
					} else {
						data->t_area_cnt = 0;
						data->t_area_l_cnt = 0;
					}
				}
			}

		} else {
			if (tch_area < atch_area-2) {
				pr_info("[TSP] Multi Cal_Bad : tch area < atch_area-2 , tch=%d, atch=%d\n"
				, data->tch_value
				, data->atch_value);
				calibrate_chip(data);
			}
		}
	} else if (touch_num  > 1 && tch_area > 48) {
		if (tch_area > atch_area) {
				pr_info("[TSP] Multi Cal_Bad : tch area > atch_area  , tch=%d, atch=%d\n"
				, data->tch_value
				, data->atch_value);
			calibrate_chip(data);
		} 

	} else if (touch_num == 1) {
		/* single Touch */
		dist_sum = data->distance[0];
		if ((tch_area < 7) &&
			(atch_area <= 1)) {
			if (!data->coin_check) {
				if (data->distance[0] < 3) {
					if (touch_area < T_AREA_LOW_ST) {
						if (data->t_area_l_cnt >= 7) {
							pr_info("[TSP] Single Floating metal Wakeup suspection :Set autocal = 5, tch=%d, atch=%d\n"
							, data->tch_value
							, data->atch_value);
							mxt_t8_cal_set(data, 5);
							data->coin_check = 1;
							data->t_area_l_cnt = 0;
						} else {
							data->t_area_l_cnt++;
						}
						data->t_area_cnt = 0;

					} else if (touch_area < \
							T_AREA_HIGH_ST) {
						if (data->t_area_cnt >= 7) {
							pr_info("[TSP] Single Floating metal Wakeup suspection :Set autocal = 5, tch=%d, atch=%d\n"
							, data->tch_value
							, data->atch_value);
							mxt_t8_cal_set(data, 5);
							data->coin_check = 1;
							data->t_area_cnt = 0;
						} else {
							data->t_area_cnt++;
						}
						data->t_area_l_cnt = 0;
					} else {
						data->t_area_cnt = 0;
						data->t_area_l_cnt = 0;
					}
				}
			}
		} else if (tch_area > 25) {
			pr_info("[TSP] tch_area > 25, tch=%d, atch=%d\n"
					, data->tch_value, data->atch_value);
			calibrate_chip(data);
		}
	}
}
#endif

static void mxt_treat_T6_object(struct mxt_data *data,
		struct mxt_message *message)
{
	/* Normal mode */
	if (message->message[0] == 0x00) {
		dev_info(&data->client->dev, "Normal mode\n");
        	if (data->cal_busy)	data->cal_busy = 0;			
#if TSP_HOVER_WORKAROUND
/* TODO HOVER : Below commands should be removed.
*/
		if (data->pdata->revision == MXT_REVISION_I
			&& data->cur_cal_status) {
			mxt_current_calibration(data);
			data->cur_cal_status = false;
		}
#endif
	}
	/* I2C checksum error */
	if (message->message[0] & 0x04)
		dev_err(&data->client->dev, "I2C checksum error\n");
	/* Config error */
	if (message->message[0] & 0x08)
		dev_err(&data->client->dev, "Config error\n");
	/* Calibration */
	if (message->message[0] & 0x10){
		dev_info(&data->client->dev, "Calibration is on going !!\n");

#if CHECK_ANTITOUCH
		/* After Calibration */
		data->coin_check = 0;
		mxt_t8_cal_set(data, 0);
		data->pdata->check_antitouch = 1;
		mxt_t61_timer_set(data,
				MXT_T61_TIMER_ONESHOT,
				MXT_T61_TIMER_CMD_STOP, 0);
		data->pdata->check_timer = 0;
		data->pdata->check_calgood = 0;
		data->cal_busy = 1;
		data->finger_area = 0;
#if PALM_CAL///0730
		data->palm_cnt = 0;
#endif

		if (!data->Press_Release_check) {
			pr_info("[TSP] Second Cal check\n");
			data->Press_Release_check = 1;
			data->Press_cnt = 0;
			data->Release_cnt = 0;
			data->release_max = 3;///0730
		}
#endif
    }
	/* Signal error */
	if (message->message[0] & 0x20)
		dev_err(&data->client->dev, "Signal error\n");
	/* Overflow */
	if (message->message[0] & 0x40)
		dev_err(&data->client->dev, "Overflow detected\n");
	/* Reset */
	if (message->message[0] & 0x80) {
		dev_info(&data->client->dev, "Reset is ongoing\n");
#if TSP_INFORM_CHARGER
		if (data->charging_mode)	
			set_charger_config(data);			
#endif

#if	CHECK_ANTITOUCH 
		data->Press_Release_check = 1;
		data->Press_cnt = 0;
		data->Release_cnt = 0;
		data->release_max = 3;
#endif

#if TSP_HOVER_WORKAROUND
/* TODO HOVER : Below commands should be removed.
 * it added just for hover. Current firmware shoud set the acqusition mode
 * with free-run and run current calibration after receive reset command
 * to support hover functionality.
 * it is bug of firmware. and it will be fixed in firmware level.
 */
		if (data->pdata->revision == MXT_REVISION_I) {
			int error = 0;
			u8 value = 0;

			error = mxt_read_object(data,
				MXT_SPT_TOUCHSCREENHOVER_T101, 0, &value);

			if (error) {
				dev_err(&data->client->dev, "Error read hover enable status[%d]\n"
					, error);
			} else {
				if (value)
					data->cur_cal_status = true;
			}
		}
#endif
	}
}

#if ENABLE_TOUCH_KEY
static void mxt_release_all_keys(struct mxt_data *data)
{
	if (tsp_keystatus != TOUCH_KEY_NULL) {
		switch (tsp_keystatus) {
		case TOUCH_KEY_MENU:
			input_report_key(data->input_dev, KEY_MENU,
								KEY_RELEASE);
			break;
		case TOUCH_KEY_BACK:
			input_report_key(data->input_dev, KEY_BACK,
								KEY_RELEASE);
			break;
		default:
			break;
		}
		dev_info(&data->client->dev, "[TSP_KEY] r %s\n",
						tsp_keyname[tsp_keystatus - 1]);
		tsp_keystatus = TOUCH_KEY_NULL;
	}
}


static void mxt_treat_T15_object(struct mxt_data *data,
						struct mxt_message *message)
{
	struct	input_dev *input;
	input = data->input_dev;

	/* single key configuration*/
	if (message->message[MXT_MSG_T15_STATUS] & MXT_MSGB_T15_DETECT) {

		/* defence code, if there is any Pressed key, force release!! */
		if (tsp_keystatus != TOUCH_KEY_NULL)
			mxt_release_all_keys(data);

		switch (message->message[MXT_MSG_T15_KEYSTATE]) {
		case TOUCH_KEY_MENU:
			input_report_key(data->input_dev, KEY_MENU, KEY_PRESS);
			tsp_keystatus = TOUCH_KEY_MENU;
			break;
		case TOUCH_KEY_BACK:
			input_report_key(data->input_dev, KEY_BACK, KEY_PRESS);
			tsp_keystatus = TOUCH_KEY_BACK;
			break;
		default:
			dev_err(&data->client->dev, "[TSP_KEY] abnormal P [%d %d]\n",
				message->message[0], message->message[1]);
			return;
		}

		dev_info(&data->client->dev, "[TSP_KEY] P %s\n",
						tsp_keyname[tsp_keystatus - 1]);
	} else {
		switch (tsp_keystatus) {
		case TOUCH_KEY_MENU:
			input_report_key(data->input_dev, KEY_MENU,
								KEY_RELEASE);
			break;
		case TOUCH_KEY_BACK:
			input_report_key(data->input_dev, KEY_BACK,
								KEY_RELEASE);
			break;
		default:
			dev_err(&data->client->dev, "[TSP_KEY] abnormal R [%d %d]\n",
				message->message[0], message->message[1]);
			return;
		}
		dev_info(&data->client->dev, "[TSP_KEY] R %s\n",
						tsp_keyname[tsp_keystatus - 1]);
		tsp_keystatus = TOUCH_KEY_NULL;
	}
	input_sync(data->input_dev);
	return;
}
#endif

static void mxt_treat_T9_object(struct mxt_data *data,
		struct mxt_message *message)
{
	int id;
	u8 *msg = message->message;

	id = data->reportids[message->reportid].index;

	/* If not a touch event, return */
	if (id >= MXT_MAX_FINGER) {
		dev_err(&data->client->dev, "MAX_FINGER exceeded!\n");
		return;
	}
	if (msg[0] & MXT_RELEASE_MSG_MASK) {
		data->fingers[id].z = 0;
		data->fingers[id].w = msg[4];
		data->fingers[id].state = MXT_STATE_RELEASE;
		
#if 	CHECK_ANTITOUCH
		data->tcount[id] = 0;
		data->distance[id] = 0;
#endif
	
		mxt_report_input_data(data);
	} else if ((msg[0] & MXT_DETECT_MSG_MASK)
		&& (msg[0] & (MXT_PRESS_MSG_MASK | MXT_MOVE_MSG_MASK))) {
		data->fingers[id].x = (msg[1] << 4) | (msg[3] >> 4);
		data->fingers[id].y = (msg[2] << 4) | (msg[3] & 0xF);
		data->fingers[id].w = msg[4];
		data->fingers[id].z = msg[5];
#if TSP_USE_SHAPETOUCH
		data->fingers[id].component = msg[6];
#endif

		if (data->pdata->max_x < 1024)
			data->fingers[id].x = data->fingers[id].x >> 2;
		if (data->pdata->max_y < 1024)
			data->fingers[id].y = data->fingers[id].y >> 2;

		data->finger_mask |= 1U << id;

		if (msg[0] & MXT_PRESS_MSG_MASK) {
			data->fingers[id].state = MXT_STATE_PRESS;
			data->fingers[id].mcount = 0;
			
#if 	CHECK_ANTITOUCH
			mxt_check_coordinate(data, 1, id,
				data->fingers[id].x,
				data->fingers[id].y);			
#endif
			
		} else if (msg[0] & MXT_MOVE_MSG_MASK) {
			data->fingers[id].mcount += 1;
			
#if 	CHECK_ANTITOUCH
			mxt_check_coordinate(data, 0, id,
				data->fingers[id].x,
				data->fingers[id].y);
#endif
		}

#if TOUCH_BOOSTER
	set_dvfs_lock(data, 1);
#endif
	} else if ((msg[0] & MXT_SUPPRESS_MSG_MASK)
		&& (data->fingers[id].state != MXT_STATE_INACTIVE)) {
		data->fingers[id].z = 0;
		data->fingers[id].w = msg[4];
		data->fingers[id].state = MXT_STATE_RELEASE;
		data->finger_mask |= 1U << id;
	} else {
		/* ignore changed amplitude and vector messsage */
		if (!((msg[0] & MXT_DETECT_MSG_MASK)
				&& (msg[0] & MXT_AMPLITUDE_MSG_MASK
				 || msg[0] & MXT_VECTOR_MSG_MASK)))
			dev_err(&data->client->dev, "Unknown state %#02x %#02x\n",
				msg[0], msg[1]);
	}
}

static void mxt_treat_T42_object(struct mxt_data *data,
		struct mxt_message *message)
{
	if (message->message[0] & 0x01) {
		/* Palm Press */
		dev_info(&data->client->dev, "palm touch detected\n");
		touch_is_pressed = 1;
	} else {
		/* Palm release */
		dev_info(&data->client->dev, "palm touch released\n");
		touch_is_pressed = 0;
	}
}

static void mxt_treat_T57_object(struct mxt_data *data,
		struct mxt_message *message)
{
			
#if CHECK_ANTITOUCH
	u16 tch_area = 0; u16 atch_area = 0; u16 touch_area_T57 = 0; u8 i;

	touch_area_T57 = message->message[0] | (message->message[1] << 8);						
	tch_area = message->message[2] | (message->message[3] << 8);
	atch_area = message->message[4] | (message->message[5] << 8);

	data->tch_value  = tch_area;
	data->atch_value = atch_area;
	data->T57_touch = touch_area_T57;
	data->Report_touch_number = 0;	
	
	for (i = 0; i < MXT_MAX_FINGER; i++) {
		if ((data->fingers[i].state != \
			MXT_STATE_INACTIVE) &&
			(data->fingers[i].state != \
			MXT_STATE_RELEASE))
			data->Report_touch_number++;
	}

	if (data->pdata->check_antitouch) {
		mxt_tch_atch_area_check(data,
		tch_area, atch_area, touch_area_T57);
#if  PALM_CAL
		if ((data->Report_touch_number >= 5) && \
			(touch_area_T57 <\
			(data->Report_touch_number * 2) + 2)) {
			if (data->palm_cnt >= 5) {
				data->palm_cnt = 0;
				pr_info("[TSP] Palm Calibration, tch:%d, atch:%d, t57tch:%d\n"
				, tch_area, atch_area
				, touch_area_T57);
				calibrate_chip(data);
			} else {
				data->palm_cnt++;
			}
		} else {
			data->palm_cnt = 0;
		}
#endif
	}

	if (data->pdata->check_calgood == 1) {
		if ((atch_area - tch_area) > 15) {
			if (tch_area < 25) {
				pr_info("[TSP] Cal Not Good1 ,tch:%d, atch:%d, t57tch:%d\n"
				, tch_area, atch_area
				, touch_area_T57);
				calibrate_chip(data);
			}
		}
		if ((tch_area - atch_area) > 48) {
			pr_info("[TSP] Cal Not Good 2 ,tch:%d, atch:%d, t57tch:%d\n"
			, tch_area, atch_area
			, touch_area_T57);
			calibrate_chip(data);
		}
	}
#endif	/* CHECK_ANTITOUCH */

#if TSP_USE_SHAPETOUCH
	data->sumsize = message->message[0] + (message->message[1] << 8);
#endif	/* TSP_USE_SHAPETOUCH */

}
static void mxt_treat_T61_object(struct mxt_data *data,
		struct mxt_message *message)
{

#if  CHECK_ANTITOUCH
//	u8 i;
//	u8 distance_check_cnt = 0;
//	u8 distance_backup[10];

	if ((message->message[0] & 0xa0) == 0xa0) {	
		if (data->pdata->check_calgood == 1) {
			if (data->Press_cnt == \
			data->Release_cnt) {
				if ((data->tch_value == 0)\
				&& (data->atch_value == 0)) {
					if (data->FirstCal_tch == 0\
					&& data->FirstCal_atch == 0) {
						if (data->FirstCal_t57tch\
						== data->T57_touch) {
							if (\
						data->T57_touch == 0\
						|| data->T57_touch > 12) {
								pr_info("[TSP] CalFail_1 SPT_TIMER_T61 Stop 3sec, tch=%d, atch=%d, t57tch=%d\n"
							, data->tch_value
							, data->atch_value
							, data->T57_touch);
							calibrate_chip(data);
				} else {
					data->pdata->check_calgood = 0;
					data->Press_Release_check = 0;
					data->pdata->check_afterCalgood = 1;
					pr_info("[TSP] CalGood SPT_TIMER_T61 Stop 3sec, tch=%d, atch=%d, t57tch=%d\n"
					, data->tch_value
					, data->atch_value
					, data->T57_touch);
					}
				} else {
					data->pdata->check_calgood = 0;
					data->Press_Release_check = 0;
					data->pdata->check_afterCalgood = 1;
					pr_info("[TSP] CalGood SPT_TIMER_T61 Stop 3sec, tch=%d, atch=%d, t57tch=%d\n"
					, data->tch_value
					, data->atch_value
					, data->T57_touch);
					}
				} else {
					data->pdata->check_calgood = 0;
					data->Press_Release_check = 0;
					data->pdata->check_afterCalgood = 1;
					pr_info("[TSP] CalGood SPT_TIMER_T61 Stop 3sec, tch=%d, atch=%d, t57tch=%d\n"
					, data->tch_value
					, data->atch_value
					, data->T57_touch);
					}
				} else {
						calibrate_chip(data);
						pr_info("[TSP] CalFail_2 SPT_TIMER_T61 Stop 3sec, tch=%d, atch=%d, t57tch=%d\n"
						, data->tch_value
						, data->atch_value
						, data->T57_touch);
					}
				} else {
				if (data->atch_value == 0) {
					if (data->finger_area < 35) {									
						calibrate_chip(data);
				pr_info("[TSP] CalFail_3 Press_cnt Fail, tch=%d, atch=%d, t57tch=%d\n"
				, data->tch_value
				, data->atch_value
				, data->T57_touch);
				} else {
				pr_info("[TSP] CalGood Press_cnt Fail, tch=%d, atch=%d, t57tch=%d\n"
				, data->tch_value
				, data->atch_value
				, data->T57_touch);
				data->pdata->check_afterCalgood = 1;
				data->pdata->check_calgood = 0;
				data->Press_Release_check = 0;
				}
			}
			else if (data->atch_value < data->tch_value \
				&& data->Report_touch_number < 4) {
				if (data->Report_touch_number == 2 && \
					data->tch_value > 12 && \
					data->T57_touch >=1) {
					pr_info("[TSP] CalGood Press_two touch, tch=%d, atch=%d, num=%d, t57tch=%d\n"
					, data->tch_value
					, data->atch_value
					, data->Report_touch_number
					, data->T57_touch);
					data->pdata->check_calgood = 0;
					data->Press_Release_check = 0;
					data->pdata->check_afterCalgood = 1;
				} else if (data->Report_touch_number == 3 \
					&&	data->tch_value > 18\
					&& data->T57_touch > 8) {
						pr_info("[TSP] CalGood Press_three touch, tch=%d, atch=%d, num=%d, t57tch=%d\n"
				, data->tch_value, data->atch_value
				, data->Report_touch_number, data->T57_touch);
				data->pdata->check_calgood = 0;
				data->Press_Release_check = 0;
				data->pdata->check_afterCalgood = 1;
				} else {
				calibrate_chip(data);
				pr_info("[TSP] CalFail_4 Press_cnt Fail, tch=%d, atch=%d, num=%d, t57tch=%d\n"
				, data->tch_value, data->atch_value, \
				data->Report_touch_number, \
				data->T57_touch);
				}
				} else {
				calibrate_chip(data);
				pr_info("[TSP] CalFail_5 Press_cnt Fail, tch=%d, atch=%d, num=%d, t57tch=%d\n"
				, data->tch_value, data->atch_value,\
				data->Report_touch_number,\
				data->T57_touch);
				}
				}
		} else if (data->pdata->check_antitouch) {
			if (data->pdata->check_autocal == 1) {
				pr_info("[TSP] Auto cal is on going - 1sec time restart, tch=%d, atch=%d, t57tch=%d\n"
				, data->tch_value, data->atch_value\
				, data->T57_touch);
				data->pdata->check_timer = 0;
				data->coin_check = 0;
				mxt_t8_cal_set(data, 0);
				mxt_t61_timer_set(data,
				MXT_T61_TIMER_ONESHOT,
				MXT_T61_TIMER_CMD_START,
				1000);
			} else {
				data->pdata->check_antitouch = 0;
				data->pdata->check_timer = 0;
				mxt_t8_cal_set(data, 0);
				data->pdata->check_calgood = 1;
				data->coin_check = 0;
				pr_info("[TSP] First Check Good, tch=%d, atch=%d, t57tch=%d\n"
				, data->tch_value, data->atch_value
				, data->T57_touch);
				data->FirstCal_tch = data->tch_value;
				data->FirstCal_atch = data->atch_value;
				data->FirstCal_t57tch = data->T57_touch;
				mxt_t61_timer_set(data,
				MXT_T61_TIMER_ONESHOT,
				MXT_T61_TIMER_CMD_START,
				3000);
		}
	}
		if (!data->Press_Release_check) {
			if (data->pdata->check_afterCalgood) {
				pr_info("[TSP] CalGood 3sec START\n");
				data->pdata->check_afterCalgood = 0;
				mxt_t61_timer_set(data,
				MXT_T61_TIMER_ONESHOT,
				MXT_T61_TIMER_CMD_START,
				5000);
			} else {
				if (data->tch_value < data->atch_value) {
					calibrate_chip(data);
					pr_info("[TSP] CalFail_6 5sec End, tch=%d, atch=%d, num=%d, t57tch=%d\n"
					, data->tch_value, data->atch_value
					, data->Report_touch_number
					, data->T57_touch);
				} else {
					pr_info("[TSP] CalGood 5sec STOP & Final\n");
					mxt_t61_timer_set(data,
					MXT_T61_TIMER_ONESHOT,
					MXT_T61_TIMER_CMD_STOP, 0);
				}
			}

		}

	}
#endif
}
static void mxt_treat_T100_object(struct mxt_data *data,
		struct mxt_message *message)
{
	u8 id, index;
	u8 *msg = message->message;
	u8 touch_type = 0, touch_event = 0, touch_detect = 0;

	index = data->reportids[message->reportid].index;

	/* Treate screen messages */
	if (index < MXT_T100_SCREEN_MESSAGE_NUM_RPT_ID) {
		if (index == MXT_T100_SCREEN_MSG_FIRST_RPT_ID)
			/* TODO: Need to be implemeted after fixed protocol
			 * This messages will indicate TCHAREA, ATCHAREA
			 */
			dev_dbg(&data->client->dev, "SCRSTATUS:[%02X] %02X %04X %04X %04X\n",
				 msg[0], msg[1], (msg[3] << 8) | msg[2],
				 (msg[5] << 8) | msg[4],
				 (msg[7] << 8) | msg[6]);
#if TSP_USE_SHAPETOUCH
			data->sumsize = (msg[3] << 8) | msg[2];
#endif	/* TSP_USE_SHAPETOUCH */
		return;
	}

	/* Treate touch status messages */
	id = index - MXT_T100_SCREEN_MESSAGE_NUM_RPT_ID;
	touch_detect = msg[0] >> MXT_T100_DETECT_MSG_MASK;
	touch_type = (msg[0] & 0x70) >> 4;
	touch_event = msg[0] & 0x0F;

	dev_dbg(&data->client->dev, "TCHSTATUS [%d] : DETECT[%d] TYPE[%d] EVENT[%d] %d,%d,%d,%d,%d\n",
		id, touch_detect, touch_type, touch_event,
		msg[1] | (msg[2] << 8),	msg[3] | (msg[4] << 8),
		msg[5], msg[6], msg[7]);

	switch (touch_type)	{
	case MXT_T100_TYPE_FINGER:
	case MXT_T100_TYPE_PASSIVE_STYLUS:
	case MXT_T100_TYPE_HOVERING_FINGER:
		/* There are no touch on the screen */
		if (!touch_detect) {
			if (touch_event == MXT_T100_EVENT_UP
				|| touch_event == MXT_T100_EVENT_SUPPESS) {

				data->fingers[id].w = 0;
				data->fingers[id].z = 0;
				data->fingers[id].state = MXT_STATE_RELEASE;
				data->fingers[id].type = touch_type;
				data->fingers[id].event = touch_event;

				mxt_report_input_data(data);
			} else {
				dev_err(&data->client->dev, "Untreated Undetectd touch : type[%d], event[%d]\n",
					touch_type, touch_event);
			}
			break;
		}

		/* There are touch on the screen */
		if (touch_event == MXT_T100_EVENT_DOWN
			|| touch_event == MXT_T100_EVENT_UNSUPPRESS
			|| touch_event == MXT_T100_EVENT_MOVE
			|| touch_event == MXT_T100_EVENT_NONE) {

			data->fingers[id].x = msg[1] | (msg[2] << 8);
			data->fingers[id].y = msg[3] | (msg[4] << 8);

			/* AUXDATA[n]'s order is depended on which values are
			 * enabled or not.
			 */
#if TSP_USE_SHAPETOUCH
			data->fingers[id].component = msg[5];
#endif
			data->fingers[id].z = msg[6];
			data->fingers[id].w = msg[7];

			if (touch_type == MXT_T100_TYPE_HOVERING_FINGER) {
				data->fingers[id].w = 0;
				data->fingers[id].z = 0;
			}

			if (touch_event == MXT_T100_EVENT_DOWN
				|| touch_event == MXT_T100_EVENT_UNSUPPRESS) {
				data->fingers[id].state = MXT_STATE_PRESS;
				data->fingers[id].mcount = 0;
			} else {
				data->fingers[id].state = MXT_STATE_MOVE;
				data->fingers[id].mcount += 1;
			}
			data->fingers[id].type = touch_type;
			data->fingers[id].event = touch_event;

			mxt_report_input_data(data);
		} else {
			dev_err(&data->client->dev, "Untreated Detectd touch : type[%d], event[%d]\n",
				touch_type, touch_event);
		}
		break;
	case MXT_T100_TYPE_ACTIVE_STYLUS:
		break;
	}
}

static irqreturn_t mxt_irq_thread(int irq, void *ptr)
{
	struct mxt_data *data = ptr;
	struct mxt_message message;
	struct device *dev = &data->client->dev;
	u8 reportid, type;

	do {
		if (mxt_read_message(data, &message)) {
			dev_err(dev, "Failed to read message\n");
			goto end;
		}

#if TSP_USE_ATMELDBG
		if (data->atmeldbg.display_log) {
			print_hex_dump(KERN_INFO, "MXT MSG:",
				DUMP_PREFIX_NONE, 16, 1,
				&message,
				sizeof(struct mxt_message), false);
		}
#endif
		reportid = message.reportid;

		if (reportid > data->max_reportid)
			goto end;

		type = data->reportids[reportid].type;

		switch (type) {
		case MXT_RESERVED_T0:
			goto end;
			break;
		case MXT_GEN_COMMANDPROCESSOR_T6:
			mxt_treat_T6_object(data, &message);
			break;
		case MXT_TOUCH_MULTITOUCHSCREEN_T9:
			mxt_treat_T9_object(data, &message);
			break;
#if ENABLE_TOUCH_KEY
		case MXT_TOUCH_KEYARRAY_T15:
			mxt_treat_T15_object(data, &message);
			break;
#endif
		case MXT_SPT_SELFTEST_T25:
			dev_err(dev, "Self test fail [0x%x 0x%x 0x%x 0x%x]\n",
				message.message[0], message.message[1],
				message.message[2], message.message[3]);
			mxt_reset(data);	//temp touch lock up
			break;
		case MXT_PROCI_TOUCHSUPPRESSION_T42:
			mxt_treat_T42_object(data, &message);
			break;
		case MXT_PROCI_EXTRATOUCHSCREENDATA_T57:
			mxt_treat_T57_object(data, &message);
			break;
		case MXT_SPT_TIMER_T61:
			mxt_treat_T61_object(data, &message);
			break;
		case MXT_PROCG_NOISESUPPRESSION_T62:
			break;
		case MXT_TOUCH_MULTITOUCHSCREEN_T100:
			mxt_treat_T100_object(data, &message);
			break;

		default:
			dev_dbg(dev, "Untreated Object type[%d]\tmessage[0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x]\n",
				type, message.message[0],
				message.message[1], message.message[2],
				message.message[3], message.message[4],
				message.message[5], message.message[6]);
			break;
		}
#if TSP_PATCH
		mxt_patch_message(data, &message);
#endif
	} while (!data->pdata->read_chg());

	if (data->finger_mask)
		mxt_report_input_data(data);
end:
	return IRQ_HANDLED;
}

static int mxt_get_bootloader_version(struct i2c_client *client, u8 val)
{
	u8 buf[3];

	if (val & MXT_BOOT_EXTENDED_ID) {
		if (i2c_master_recv(client, buf, sizeof(buf)) != sizeof(buf)) {
			dev_err(&client->dev, "%s: i2c recv failed\n",
				 __func__);
			return -EIO;
		}
		dev_info(&client->dev, "Bootloader ID:%d Version:%d",
			 buf[1], buf[2]);
	} else {
		dev_info(&client->dev, "Bootloader ID:%d",
			 val & MXT_BOOT_ID_MASK);
	}
	return 0;
}

static int mxt_check_bootloader(struct i2c_client *client,
				     unsigned int state)
{
	u8 val;

recheck:
	if (i2c_master_recv(client, &val, 1) != 1) {
		dev_err(&client->dev, "%s: i2c recv failed\n", __func__);
		return -EIO;
	}

	switch (state) {
	case MXT_WAITING_BOOTLOAD_CMD:
		if (mxt_get_bootloader_version(client, val))
			return -EIO;
		val &= ~MXT_BOOT_STATUS_MASK;
		break;
	case MXT_WAITING_FRAME_DATA:
	case MXT_APP_CRC_FAIL:
		val &= ~MXT_BOOT_STATUS_MASK;
		break;
	case MXT_FRAME_CRC_PASS:
		if (val == MXT_FRAME_CRC_CHECK)
			goto recheck;
		if (val == MXT_FRAME_CRC_FAIL) {
			dev_err(&client->dev, "Bootloader CRC fail\n");
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	if (val != state) {
		dev_err(&client->dev,
			 "Invalid bootloader mode state 0x%X\n", val);
		return -EINVAL;
	}

	return 0;
}

static int mxt_unlock_bootloader(struct i2c_client *client)
{
	u8 buf[2] = {MXT_UNLOCK_CMD_LSB, MXT_UNLOCK_CMD_MSB};

	if (i2c_master_send(client, buf, 2) != 2) {
		dev_err(&client->dev, "%s: i2c send failed\n", __func__);

		return -EIO;
	}

	return 0;
}

static int mxt_probe_bootloader(struct i2c_client *client)
{
	u8 val;

	if (i2c_master_recv(client, &val, 1) != 1) {
		dev_err(&client->dev, "%s: i2c recv failed\n", __func__);
		return -EIO;
	}

	if (val & (~MXT_BOOT_STATUS_MASK)) {
		if (val & MXT_APP_CRC_FAIL)
			dev_err(&client->dev, "Application CRC failure\n");
		else
			dev_err(&client->dev, "Device in bootloader mode\n");
	} else {
		dev_err(&client->dev, "%s: Unknow status\n", __func__);
		return -EIO;
	}
	return 0;
}

static int mxt_fw_write(struct i2c_client *client,
				const u8 *frame_data, unsigned int frame_size)
{
	if (i2c_master_send(client, frame_data, frame_size) != frame_size) {
		dev_err(&client->dev, "%s: i2c send failed\n", __func__);
		return -EIO;
	}

	return 0;
}

#if DUAL_CFG
int mxt_verify_fw(struct mxt_fw_info *fw_info, const struct firmware *fw)
{
	struct mxt_data *data = fw_info->data;
	struct device *dev = &data->client->dev;
	struct mxt_fw_image *fw_img;

	if (!fw) {
		dev_err(dev, "could not find firmware file\n");
		return -ENOENT;
	}

	fw_img = (struct mxt_fw_image *)fw->data;

	if (le32_to_cpu(fw_img->magic_code) != MXT_FW_MAGIC) {
		/* In case, firmware file only consist of firmware */
		dev_info(dev, "Firmware file only consist of raw firmware\n");
		fw_info->fw_len = fw->size;
		fw_info->fw_raw_data = fw->data;
	} else {
		/*
		 * In case, firmware file consist of header,
		 * configuration, firmware.
		 */
		dev_info(dev, "Firmware file consist of header, configuration, firmware\n");
		fw_info->fw_ver = fw_img->fw_ver;
		fw_info->build_ver = fw_img->build_ver;
		fw_info->hdr_len = le32_to_cpu(fw_img->hdr_len);
		fw_info->cfg_len = le32_to_cpu(fw_img->cfg_len);
		fw_info->fw_len = le32_to_cpu(fw_img->fw_len);
		fw_info->cfg_crc = le32_to_cpu(fw_img->cfg_crc);

		/* Check the firmware file with header */
		if (fw_info->hdr_len != sizeof(struct mxt_fw_image)
			|| fw_info->hdr_len + fw_info->cfg_len
				+ fw_info->fw_len != fw->size) {
#if TSP_PATCH
			struct patch_header* ppheader;		
			u32 ppos = fw_info->hdr_len + fw_info->cfg_len + fw_info->fw_len;		
			ppheader = (struct patch_header*)(fw->data + ppos);									
			if(ppheader->magic == MXT_PATCH_MAGIC){					
				dev_info(dev, "Firmware file has patch size: %d\n", ppheader->size);
				if(ppheader->size){	
					u8* patch=NULL;			
					if(!data->patch.patch){
						kfree(data->patch.patch);
					}
					patch = kzalloc(ppheader->size, GFP_KERNEL);
					memcpy(patch, (u8*)ppheader, ppheader->size);
					data->patch.patch = patch;					
				}
			}
			else
			{
#endif		
			dev_err(dev, "Firmware file is invaild !!hdr size[%d] cfg,fw size[%d,%d] filesize[%d]\n",
				fw_info->hdr_len, fw_info->cfg_len,
				fw_info->fw_len, fw->size);
			return -EINVAL;
#if TSP_PATCH
			}
#endif		
		}

		if (!fw_info->cfg_len) {
			dev_err(dev, "Firmware file dose not include configuration data\n");
			return -EINVAL;
		}
		if (!fw_info->fw_len) {
			dev_err(dev, "Firmware file dose not include raw firmware data\n");
			return -EINVAL;
		}

		/* Get the address of configuration data */
		data->cfg_len = fw_info->cfg_len / 2;
		data->batt_cfg_raw_data = fw_info->batt_cfg_raw_data
			= fw_img->data;
		data->ta_cfg_raw_data = fw_info->ta_cfg_raw_data
			= fw_img->data +  (fw_info->cfg_len / 2) ;

		/* Get the address of firmware data */
		fw_info->fw_raw_data = fw_img->data + fw_info->cfg_len;

#if TSP_SEC_FACTORY
		data->fdata->fw_ver = fw_info->fw_ver;
		data->fdata->build_ver = fw_info->build_ver;
#endif
	}

	return 0;
}
#else
int mxt_verify_fw(struct mxt_fw_info *fw_info, const struct firmware *fw)
{
	struct mxt_data *data = fw_info->data;
	struct device *dev = &data->client->dev;
	struct mxt_fw_image *fw_img;

	if (!fw) {
		dev_err(dev, "could not find firmware file\n");
		return -ENOENT;
	}

	fw_img = (struct mxt_fw_image *)fw->data;

	if (le32_to_cpu(fw_img->magic_code) != MXT_FW_MAGIC) {
		/* In case, firmware file only consist of firmware */
		dev_dbg(dev, "Firmware file only consist of raw firmware\n");
		fw_info->fw_len = fw->size;
		fw_info->fw_raw_data = fw->data;
	} else {
		/*
		 * In case, firmware file consist of header,
		 * configuration, firmware.
		 */
		dev_info(dev, "Firmware file consist of header, configuration, firmware\n");
		fw_info->fw_ver = fw_img->fw_ver;
		fw_info->build_ver = fw_img->build_ver;
		fw_info->hdr_len = le32_to_cpu(fw_img->hdr_len);
		fw_info->cfg_len = le32_to_cpu(fw_img->cfg_len);
		fw_info->fw_len = le32_to_cpu(fw_img->fw_len);
		fw_info->cfg_crc = le32_to_cpu(fw_img->cfg_crc);

		/* Check the firmware file with header */
		if (fw_info->hdr_len != sizeof(struct mxt_fw_image)
			|| fw_info->hdr_len + fw_info->cfg_len
				+ fw_info->fw_len != fw->size) {
			dev_err(dev, "Firmware file is invaild !!hdr size[%d] cfg,fw size[%d,%d] filesize[%d]\n",
				fw_info->hdr_len, fw_info->cfg_len,
				fw_info->fw_len, fw->size);
			return -EINVAL;
		}

		if (!fw_info->cfg_len) {
			dev_err(dev, "Firmware file dose not include configuration data\n");
			return -EINVAL;
		}
		if (!fw_info->fw_len) {
			dev_err(dev, "Firmware file dose not include raw firmware data\n");
			return -EINVAL;
		}

		/* Get the address of configuration data */
		fw_info->cfg_raw_data = fw_img->data;

		/* Get the address of firmware data */
		fw_info->fw_raw_data = fw_img->data + fw_info->cfg_len;

#if TSP_SEC_FACTORY
		data->fdata->fw_ver = fw_info->fw_ver;
		data->fdata->build_ver = fw_info->build_ver;
#endif
	}

	return 0;
}
#endif

static int mxt_wait_for_chg(struct mxt_data *data, u16 time)
{
	int timeout_counter = 0;

	msleep(time);

	if (data->pdata->read_chg) {
		while (data->pdata->read_chg()
			&& timeout_counter++ <= 20) {

			msleep(MXT_RESET_INTEVAL_TIME);
			dev_err(&data->client->dev, "Spend %d time waiting for chg_high\n",
				(MXT_RESET_INTEVAL_TIME * timeout_counter)
				 + time);
		}
	}

	return 0;
}

static int mxt_command_reset(struct mxt_data *data, u8 value)
{
	int error;

	mxt_write_object(data, MXT_GEN_COMMANDPROCESSOR_T6,
			MXT_COMMAND_RESET, value);

	error = mxt_wait_for_chg(data, MXT_SW_RESET_TIME);
	if (error)
		dev_err(&data->client->dev, "Not respond after reset command[%d]\n",
			value);

	return error;
}

static int mxt_command_calibration(struct mxt_data *data)
{
	int ret;
	ret = mxt_write_object(data, MXT_GEN_COMMANDPROCESSOR_T6,
						MXT_COMMAND_CALIBRATE, 1);
	return ret;
}

static int mxt_command_backup(struct mxt_data *data, u8 value)
{
	mxt_write_object(data, MXT_GEN_COMMANDPROCESSOR_T6,
			MXT_COMMAND_BACKUPNV, value);

	msleep(MXT_BACKUP_TIME);

	return 0;
}

static int mxt_flash_fw(struct mxt_fw_info *fw_info)
{
	struct mxt_data *data = fw_info->data;
	struct i2c_client *client = data->client_boot;
	struct device *dev = &data->client->dev;
	const u8 *fw_data = fw_info->fw_raw_data;
	size_t fw_size = fw_info->fw_len;
	unsigned int frame_size;
	unsigned int pos = 0;
	int ret;

	if (!fw_data) {
		dev_err(dev, "firmware data is Null\n");
		return -ENOMEM;
	}

	ret = mxt_check_bootloader(client, MXT_WAITING_BOOTLOAD_CMD);
	if (ret) {
		/*may still be unlocked from previous update attempt */
		ret = mxt_check_bootloader(client, MXT_WAITING_FRAME_DATA);
		if (ret)
			goto out;
	} else {
		dev_info(dev, "Unlocking bootloader\n");
		/* Unlock bootloader */
		mxt_unlock_bootloader(client);
	}
	while (pos < fw_size) {
		ret = mxt_check_bootloader(client,
					MXT_WAITING_FRAME_DATA);
		if (ret) {
			dev_err(dev, "Fail updating firmware. wating_frame_data err\n");
			goto out;
		}

		frame_size = ((*(fw_data + pos) << 8) | *(fw_data + pos + 1));

		/*
		* We should add 2 at frame size as the the firmware data is not
		* included the CRC bytes.
		*/

		frame_size += 2;

		/* Write one frame to device */
		mxt_fw_write(client, fw_data + pos, frame_size);

		ret = mxt_check_bootloader(client,
						MXT_FRAME_CRC_PASS);
		if (ret) {
			dev_err(dev, "Fail updating firmware. frame_crc err\n");
			goto out;
		}

		pos += frame_size;

		dev_dbg(dev, "Updated %d bytes / %zd bytes\n",
				pos, fw_size);

		msleep(20);
	}

	ret = mxt_wait_for_chg(data, MXT_SW_RESET_TIME);
	if (ret) {
		dev_err(dev, "Not respond after F/W  finish reset\n");
		goto out;
	}

	dev_info(dev, "success updating firmware\n");
out:
	return ret;
}

static void mxt_handle_init_data(struct mxt_data *data)
{
/*
 * Caution : This function is called before backup NV. So If you write
 * register vaules directly without config file in this function, it can
 * be a cause of that configuration CRC mismatch or unintended values are
 * stored in Non-volatile memory in IC. So I would recommed do not use
 * this function except for bring up case. Please keep this in your mind.
 */
	return;
}

static int mxt_read_id_info(struct mxt_data *data)
{
	int ret = 0;
	u8 id[MXT_INFOMATION_BLOCK_SIZE];

	/* Read IC information */
	ret = mxt_read_mem(data, 0, MXT_INFOMATION_BLOCK_SIZE, id);
	if (ret) {
		dev_err(&data->client->dev, "Read fail. IC information\n");
		goto out;
	} else {
		dev_info(&data->client->dev,
			"family: 0x%x variant: 0x%x version: 0x%x"
			" build: 0x%x matrix X,Y size:  %d,%d"
			" number of obect: %d\n"
			, id[0], id[1], id[2], id[3], id[4], id[5], id[6]);
		data->info.family_id = id[0];
		data->info.variant_id = id[1];
		data->info.version = id[2];
		data->info.build = id[3];
		data->info.matrix_xsize = id[4];
		data->info.matrix_ysize = id[5];
		data->info.object_num = id[6];
	}

out:
	return ret;
}

static int mxt_get_object_table(struct mxt_data *data)
{
	int error;
	int i;
	u16 reg;
	u8 reportid = 0;
	u8 buf[MXT_OBJECT_TABLE_ELEMENT_SIZE];

	for (i = 0; i < data->info.object_num; i++) {
		struct mxt_object *object = data->objects + i;

		reg = MXT_OBJECT_TABLE_START_ADDRESS +
				MXT_OBJECT_TABLE_ELEMENT_SIZE * i;
		error = mxt_read_mem(data, reg,
				MXT_OBJECT_TABLE_ELEMENT_SIZE, buf);
		if (error)
			return error;

		object->type = buf[0];
		object->start_address = (buf[2] << 8) | buf[1];
		/* the real size of object is buf[3]+1 */
		object->size = buf[3] + 1;
		/* the real instances of object is buf[4]+1 */
		object->instances = buf[4] + 1;
		object->num_report_ids = buf[5];

		dev_dbg(&data->client->dev,
			"Object:T%d\t\t\t Address:0x%x\tSize:%d\tInstance:%d\tReport Id's:%d\n",
			object->type, object->start_address, object->size,
			object->instances, object->num_report_ids);

		if (object->num_report_ids) {
			reportid += object->num_report_ids * object->instances;
			object->max_reportid = reportid;
		}
	}

	/* Store maximum reportid */
	data->max_reportid = reportid;
	dev_dbg(&data->client->dev, "maXTouch: %d report ID\n",
			data->max_reportid);

	return 0;
}

static void mxt_make_reportid_table(struct mxt_data *data)
{
	struct mxt_object *objects = data->objects;
	struct mxt_reportid *reportids = data->reportids;
	int i, j;
	int id = 0;

	for (i = 0; i < data->info.object_num; i++) {
		for (j = 0; j < objects[i].num_report_ids *
				objects[i].instances; j++) {
			id++;

			reportids[id].type = objects[i].type;
			reportids[id].index = j;

			dev_dbg(&data->client->dev, "Report_id[%d]:\tT%d\tIndex[%d]\n",
				id, reportids[id].type, reportids[id].index);
		}
	}
}

static int mxt_initialize(struct mxt_data *data)
{
	struct i2c_client *client = data->client;

	u32 read_info_crc, calc_info_crc;
	int ret;

	ret = mxt_read_id_info(data);
	if (ret)
		return ret;

	data->objects = kcalloc(data->info.object_num,
				sizeof(struct mxt_object),
				GFP_KERNEL);
	if (!data->objects) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		ret = -ENOMEM;
		goto out;
	}

	/* Get object table infomation */
	ret = mxt_get_object_table(data);
	if (ret)
		goto out;

	data->reportids = kcalloc(data->max_reportid + 1,
			sizeof(struct mxt_reportid),
			GFP_KERNEL);
	if (!data->reportids) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		ret = -ENOMEM;
		goto out;
	}

	/* Make report id table */
	mxt_make_reportid_table(data);

	/* Verify the info CRC */
	ret = mxt_read_info_crc(data, &read_info_crc);
	if (ret)
		goto out;

	ret = mxt_calculate_infoblock_crc(data, &calc_info_crc);
	if (ret)
		goto out;

	if (read_info_crc != calc_info_crc) {
		dev_err(&data->client->dev, "Infomation CRC error :[CRC 0x%06X!=0x%06X]\n",
				read_info_crc, calc_info_crc);
		ret = -EFAULT;
		goto out;
	}
	return 0;

out:
	return ret;
}

static int  mxt_rest_initialize(struct mxt_fw_info *fw_info)
{
	struct mxt_data *data = fw_info->data;
	struct device *dev = &data->client->dev;
	int ret;

	/* Restore memory and stop event handing */
	ret = mxt_command_backup(data, MXT_DISALEEVT_VALUE);
	if (ret) {
		dev_err(dev, "Failed Restore NV and stop event\n");
		goto out;
	}

	/* Write config */
	ret = mxt_write_config(fw_info);
	if (ret) {
		dev_err(dev, "Failed to write config from file\n");
		goto out;
	}

	/* Handle data for init */
	mxt_handle_init_data(data);

	/* Backup to memory */
	ret = mxt_command_backup(data, MXT_BACKUP_VALUE);
	if (ret) {
		dev_err(dev, "Failed backup NV data\n");
		goto out;
	}

	/* Soft reset */
	ret = mxt_command_reset(data, MXT_RESET_VALUE);
	if (ret) {
		dev_err(dev, "Failed Reset IC\n");
		goto out;
	}
#if TSP_PATCH
	if(data->patch.patch)
		ret = mxt_patch_init(data, data->patch.patch);
#endif	
out:
	return ret;
}

#ifdef CONFIG_OF
static struct regulator *vddo_vreg;
static u8 n_gpio_ldo_en;

void mxt_vdd_on(bool onoff)
{
	int ret;
	printk("[TSP] %s gpio[%d] on= %d\n",__func__, n_gpio_ldo_en,  onoff);

	if (!vddo_vreg) {
		pr_info("%s: tsp 1.8V[vddo] isn't seted.\n", __func__);
	}
	
	if (onoff) {		
//		if (!regulator_is_enabled(vddo_vreg)) {
			ret = regulator_enable(vddo_vreg);
			if (ret) {
				pr_err("%s: enable vddo failed, rc=%d\n", __func__, ret);
				return;
			}
			pr_info("%s: tsp 1.8V[vddo] on is finished.\n", __func__);
//		} else
//			pr_info("%s: tsp 1.8V[vddo] is already on.\n", __func__);
	} else {
//		if (regulator_is_enabled(vddo_vreg)) {
			ret = regulator_disable(vddo_vreg);
			if (ret) {
				pr_err("%s: enable vddo failed, rc=%d\n", __func__, ret);
				return;
			}
			pr_info("%s: tsp 1.8V[vddo] off is finished.\n", __func__);
//		} else
//			pr_info("%s: tsp 1.8V[vddo] is already off.\n", __func__);
	}

	
	gpio_direction_output(n_gpio_ldo_en, onoff);
	msleep(30);
	return;
}

void mxt_init_gpio(struct mxt_platform_data *pdata)
{
	int ret;

	printk("[TSP] %s, irq:%d \n", __func__, pdata->gpio_int);

	ret = gpio_request(pdata->gpio_int, "mxt_tsp_irq");
	if(ret) {
		pr_err("[TSP]%s: unable to request mxt_tsp_irq [%d]\n",	__func__, pdata->gpio_int);
		return;
	}

	gpio_tlmm_config(GPIO_CFG(pdata->gpio_int, 0, GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA), 1);

	ret = gpio_request(pdata->gpio_ldo_en, "mxt_gpio_ldo_en");
	if(ret) {
		pr_err("[TSP]%s: unable to request mxt_gpio_ldo_en [%d]\n",	__func__, pdata->gpio_ldo_en);
		return;
	}
	gpio_tlmm_config(GPIO_CFG(pdata->gpio_ldo_en, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA), GPIO_CFG_ENABLE);
#if 0
	gpio_tlmm_config(GPIO_CFG(pdata->gpio_vendor1, 0, GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA), 1);
	gpio_tlmm_config(GPIO_CFG(pdata->gpio_vendor2, 0, GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA), 1);
#endif
//	gpio_tlmm_config(GPIO_CFG(GPIO_TOUCH_SCL, 3, GPIO_CFG_INPUT, GPIO_CFG_PULL_UP, GPIO_CFG_2MA), 1);
//	gpio_tlmm_config(GPIO_CFG(GPIO_TOUCH_SDA, 3, GPIO_CFG_INPUT, GPIO_CFG_PULL_UP, GPIO_CFG_2MA), 1);


}

static int mxt_parse_dt(struct device *dev, struct mxt_platform_data *pdata)
{
	struct device_node *np = dev->of_node;

	pdata->gpio_ldo_en = of_get_named_gpio(np, "atmel,vdd_en-gpio", 0);
	n_gpio_ldo_en = pdata->gpio_ldo_en;
	pdata->gpio_int = of_get_named_gpio(np, "atmel,irq-gpio", 0);
#if 0
	pdata->gpio_vendor1 = of_get_named_gpio(np, "mxt,vendor1", 0);
	pdata->gpio_vendor2 = of_get_named_gpio(np, "mxt,vendor2", 0);
#endif
	return 0;
}

void mxts_register_callback(struct tsp_callbacks *cb)
{
	charger_callbacks = cb;
	pr_debug("[TSP]mxts_register_callback\n");
}

static int mxts_power_on(void)
{
	mxt_vdd_on(1);
	return 0;
}

static int mxts_power_off(void)
{
	mxt_vdd_on(0);
	return 0;
}
static u8 n_gpio_irq;
static bool mxts_read_chg(void)
{
	return gpio_get_value(n_gpio_irq);
}

#define TOUCH_MAX_X	480
#define TOUCH_MAX_Y	800
#define NUM_XNODE 19
#define NUM_YNODE 11
#define PROJECT_NAME	"G3518"
#define CONFIG_VER	"0128"


#else

#define mxt_vdd_on				NULL
#define mxt_init_gpio			NULL
#define mxt_parse_dt			NULL
#define mxts_register_callback	NULL
#define mxts_power_on			NULL
#define mxts_power_off			NULL
#define mxts_read_chg			NULL
#endif


static int mxt_power_on(struct mxt_data *data)
{
/*
 * If do not turn off the power during suspend, you can use deep sleep
 * or disable scan to use T7, T9 Object. But to turn on/off the power
 * is better.
 */
	int error = 0;

	if (data->mxt_enabled)
		return 0;

	if (!data->pdata->power_on) {
		dev_warn(&data->client->dev, "Power on function is not defined\n");
		error = -EINVAL;
		goto out;
	}

	error = data->pdata->power_on();

	gpio_tlmm_config(GPIO_CFG(data->pdata->gpio_int, 0, GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA), 1);
	//gpio_tlmm_config(GPIO_CFG(GPIO_TOUCH_SCL, 1, GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA), 1);
	//gpio_tlmm_config(GPIO_CFG(GPIO_TOUCH_SDA, 1, GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA), 1);

	if (error) {
		dev_err(&data->client->dev, "Failed to power on\n");
		goto out;
	}

	error = mxt_wait_for_chg(data, MXT_HW_RESET_TIME);
	if (error)
		dev_err(&data->client->dev, "Not respond after H/W reset\n");

	data->mxt_enabled = true;

out:
	return error;
}

static int mxt_power_off(struct mxt_data *data)
{
	int error = 0;

	if (!data->mxt_enabled)
		return 0;

	if (!data->pdata->power_off) {
		dev_warn(&data->client->dev, "Power off function is not defined\n");
		error = -EINVAL;
		goto out;
	}

	gpio_tlmm_config(GPIO_CFG(data->pdata->gpio_int, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA), 1); 
	//gpio_tlmm_config(GPIO_CFG(GPIO_TOUCH_SCL, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA), 1);
	//gpio_tlmm_config(GPIO_CFG(GPIO_TOUCH_SDA, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA), 1);

	error = data->pdata->power_off();
	if (error) {
		dev_err(&data->client->dev, "Failed to power off\n");
		goto out;
	}

	data->mxt_enabled = false;

out:
	return error;
}

/* Need to be called by function that is blocked with mutex */
static int mxt_start(struct mxt_data *data)
{
	int error = 0;

	printk("[TSP] %s, %d \n", __func__, __LINE__);

	if (data->mxt_enabled) {
		dev_err(&data->client->dev,
			"%s. but touch already on\n", __func__);
		return error;
	}

	error = mxt_power_on(data);

	if (error) {
		dev_err(&data->client->dev, "Fail to start touch\n");
	} else {
		if (system_rev == 0) {
			mxt_command_calibration(data);
			dev_err(&data->client->dev, "Force calibration\n");
		}
		enable_irq(data->client->irq);
	}
	dev_info(&data->client->dev, "[TSP] mxt_start end\n");

	return error;
}

/* Need to be called by function that is blocked with mutex */
static int mxt_stop(struct mxt_data *data)
{
	int error = 0;

	printk("[TSP] %s, %d \n", __func__, __LINE__);

	if (!data->mxt_enabled) {
		dev_err(&data->client->dev,
			"%s. but touch already off\n", __func__);
		return error;
	}
	disable_irq(data->client->irq);

	error = mxt_power_off(data);
	if (error) {
		dev_err(&data->client->dev, "Fail to stop touch\n");
		goto err_power_off;
	}
	mxt_release_all_finger(data);

#if ENABLE_TOUCH_KEY
	mxt_release_all_keys(data);
#endif

#if TOUCH_BOOSTER
	set_dvfs_lock(data, 2);
	printk("[TSP] %s : dvfs_lock free. (%d)\n ",__func__,__LINE__);
#endif
	return 0;

err_power_off:
	enable_irq(data->client->irq);
	return error;
}

static int mxt_reset(struct mxt_data *data)	//mxt_stop+mxt_start-irq dis&enable
{
	int error = 0;

	printk("[TSP] %s +\n", __func__);

	error = mxt_power_off(data);
	if (error) {
		dev_err(&data->client->dev, "Fail to stop touch\n");
		return error;
	}
	mxt_release_all_finger(data);

#if ENABLE_TOUCH_KEY
	mxt_release_all_keys(data);
#endif

#if TOUCH_BOOSTER
	set_dvfs_lock(data, 2);
	printk("[TSP] %s : dvfs_lock free. (%d)\n ",__func__,__LINE__);
#endif

	error = mxt_power_on(data);

	if (error) {
		dev_err(&data->client->dev, "Fail to start touch\n");
		return error;
	} else {
		if (system_rev == 0) {
			mxt_command_calibration(data);
			dev_err(&data->client->dev, "Force calibration\n");
		}
	}
	printk("[TSP] %s - \n", __func__);

	return 0;
}

static int mxt_make_highchg(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	struct mxt_message message;
	int count = data->max_reportid * 2;
	int error;

	/* Read dummy message to make high CHG pin */
	do {
		error = mxt_read_message(data, &message);
		if (error)
			return error;
	} while (message.reportid != 0xff && --count);

	if (!count) {
		dev_err(dev, "CHG pin isn't cleared\n");
		return -EBUSY;
	}

	return 0;
}

static int mxt_touch_finish_init(struct mxt_data *data)
{
	struct i2c_client *client = data->client;
	int error;
	unsigned int irq;

	irq = gpio_to_irq(data->pdata->gpio_int);

	error = request_threaded_irq(irq, NULL, mxt_irq_thread,
		data->pdata->irqflags, client->dev.driver->name, data);

	//dev_err(&client->dev, "%s, Err(%d, g:%d, f:%d, n:%s\n",__func__, error, data->pdata->gpio_int, (u8)data->pdata->irqflags, client->dev.driver->name);

	if (error) {
		dev_err(&client->dev, "Failed to register interrupt\n");
		goto err_req_irq;
	}

	error = mxt_make_highchg(data);
	if (error) {
		dev_err(&client->dev, "Failed to clear CHG pin\n");
		goto err_req_irq;
	}

#if TOUCH_BOOSTER
	mutex_init(&data->dvfs_lock);
	INIT_DELAYED_WORK(&data->work_dvfs_off, set_dvfs_off);
	INIT_DELAYED_WORK(&data->work_dvfs_chg, change_dvfs_lock);
	data->dvfs_lock_status = false;
#endif

	dev_info(&client->dev,  "Mxt touch controller initialized\n");

	/*
	* to prevent unnecessary report of touch event
	* it will be enabled in open function
	*/

	mxt_stop(data);

	/* for blocking to be excuted open function untile finishing ts init */
	complete_all(&data->init_done);
	return 0;

err_req_irq:
	return error;
}

static int mxt_touch_rest_init(struct mxt_fw_info *fw_info)
{
	struct mxt_data *data = fw_info->data;
	struct device *dev = &data->client->dev;
	int error;

	error = mxt_initialize(data);
	if (error) {
		dev_err(dev, "Failed to initialize\n");
		goto err_free_mem;
	}

	error = mxt_rest_initialize(fw_info);
	if (error) {
		dev_err(dev, "Failed to rest initialize\n");
		goto err_free_mem;
	}

	mxts_power_off();
	mxts_power_on();
	
	error = mxt_touch_finish_init(data);
	if (error)
		goto err_free_mem;

	return 0;

err_free_mem:
	kfree(data->objects);
	data->objects = NULL;
	kfree(data->reportids);
	data->reportids = NULL;
	return error;
}

static int mxt_enter_bootloader(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	int error;

	data->objects = kcalloc(data->info.object_num,
				     sizeof(struct mxt_object),
				     GFP_KERNEL);
	if (!data->objects) {
		dev_err(dev, "%s Failed to allocate memory\n",
			__func__);
		error = -ENOMEM;
		goto out;
	}

	/* Get object table information*/
	error = mxt_get_object_table(data);
	if (error)
		goto err_free_mem;

	/* Change to the bootloader mode */
	error = mxt_command_reset(data, MXT_BOOT_VALUE);
	if (error)
		goto err_free_mem;

err_free_mem:
	kfree(data->objects);
	data->objects = NULL;

out:
	return error;
}

static int mxt_flash_fw_on_probe(struct mxt_fw_info *fw_info)
{
	struct mxt_data *data = fw_info->data;
	struct device *dev = &data->client->dev;
	int error;

	error = mxt_read_id_info(data);

	if (error) {
		/* need to check IC is in boot mode */
		error = mxt_probe_bootloader(data->client_boot);
		if (error) {
			dev_err(dev, "Failed to verify bootloader's status\n");
			goto out;
		}

		dev_info(dev, "Updating firmware from boot-mode\n");
		goto load_fw;
	}

	dev_info(dev, "Updating firmware from app-mode : IC:0x%x,0x%x =! FW:0x%x,0x%x\n",
			data->info.version, data->info.build,
			fw_info->fw_ver, fw_info->build_ver);
	
	/* compare the version to verify necessity of firmware updating */
	if (data->info.version == fw_info->fw_ver
			&& data->info.build == fw_info->build_ver) {
		dev_info(dev, "Firmware version is same with in IC\n");
		goto out;
	}
	dev_info(dev, "Updating firmware from app-mode : IC:0x%x,0x%x =! FW:0x%x,0x%x\n",
			data->info.version, data->info.build,
			fw_info->fw_ver, fw_info->build_ver);

	error = mxt_enter_bootloader(data);
	if (error) {
		dev_err(dev, "Failed updating firmware\n");
		goto out;
	}

load_fw:
	error = mxt_flash_fw(fw_info);
	if (error)
		dev_err(dev, "Failed updating firmware\n");
	else
		dev_info(dev, "succeeded updating firmware\n");
out:
	return error;
}

static int mxt_touch_init_firmware(const struct firmware *fw,
		void *context)
{
	struct mxt_data *data = context;
	struct mxt_fw_info fw_info;
	int error;

	memset(&fw_info, 0, sizeof(struct mxt_fw_info));
	fw_info.data = data;

#if FW_UPDATE_ENABLE
	error = mxt_verify_fw(&fw_info, fw);
	if (error){
		printk("[TSP] %s, error :%d\n", __func__, error);
		goto ts_rest_init;
	}

	/* Skip update on boot up if firmware file does not have a header */
	if (!fw_info.hdr_len){		
		printk("[TSP] %s, hdr_len :%d\n", __func__, fw_info.hdr_len);
		goto ts_rest_init;
	}

	error = mxt_flash_fw_on_probe(&fw_info);
	if (error){		
		printk("[TSP] %s, fw error :%d\n", __func__, error);
		goto out;
	}
#endif

ts_rest_init:
	error = mxt_touch_rest_init(&fw_info);
out:
	if (error)
		/* complete anyway, so open() doesn't get blocked */
		complete_all(&data->init_done);
#if FW_UPDATE_ENABLE
	release_firmware(fw);
#endif
	return error;
}

static void mxt_request_firmware_work(const struct firmware *fw,
		void *context)
{
	struct mxt_data *data = context;
	mxt_touch_init_firmware(fw, data);
}

static int mxt_touch_init(struct mxt_data *data, bool nowait)
{
	struct i2c_client *client = data->client;
	const char *firmware_name =
		 data->pdata->firmware_name ?: MXT_DEFAULT_FIRMWARE_NAME;
	int ret = 0;

#if TSP_INFORM_CHARGER
	/* Register callbacks */
	/* To inform tsp , charger connection status*/
	data->callbacks.inform_charger = inform_charger;
	if (data->pdata->register_cb) {
		data->pdata->register_cb(&data->callbacks);
		inform_charger_init(data);
	}
#endif

	if (nowait) {
		const struct firmware *fw;
		char fw_path[MXT_MAX_FW_PATH];

		memset(&fw_path, 0, MXT_MAX_FW_PATH);

		snprintf(fw_path, MXT_MAX_FW_PATH, "%s%s",
			MXT_FIRMWARE_INKERNEL_PATH, firmware_name);

		dev_err(&client->dev, "%s\n", fw_path);

#if FW_UPDATE_ENABLE
		ret = request_firmware(&fw, fw_path, &client->dev);
		if (ret) {
			dev_err(&client->dev,
				"error requesting built-in firmware\n");
			goto out;
		}
#endif
		ret = mxt_touch_init_firmware(fw, data);
	} else {
		ret = request_firmware_nowait(THIS_MODULE, true, firmware_name,
				      &client->dev, GFP_KERNEL,
				      data, mxt_request_firmware_work);
		if (ret)
			dev_err(&client->dev,
				"cannot schedule firmware update (%d)\n", ret);
	}

out:
	return ret;
}
/*
static int  mxt_set_firmware_name(struct mxt_data *data)
{

	struct i2c_client *client = data->client_boot;
	u8 buf[3];
	int error = 0;

	data->info.object_num = 0x19;

	error = mxt_enter_bootloader(data);
	if (error) {
		dev_err(&client->dev, "mxt_set_firmware_name mxt_enter_bootloader Fail \n");
		error = 1;
	}
	
	if (i2c_master_recv(client, buf, sizeof(buf)) != sizeof(buf)) {
		dev_err(&client->dev, "%s: i2c recv failed\n",
			 __func__);
		error = 1;
	}

	dev_info(&client->dev, "Bootloader ID:0x%X Version:0x%X \n",buf[1], buf[2]);

	if(buf[1] == 0x21)
	{
		strcpy(data->pdata->firmware_name,MXT_FIRMWARE_NAME_REVISION_G);
		dev_info(&client->dev, "mxt_set_firmware_name MXT_FIRMWARE_NAME_REVISION_G \n");	
	}
	else
	{
		strcpy(data->pdata->firmware_name,MXT_FIRMWARE_NAME_REVISION_I);
		dev_info(&client->dev, "mxt_set_firmware_name MXT_FIRMWARE_NAME_REVISION_I \n");			
		
	}

out:
	return error;
}
*/

#define CONFIG_OPEN_CLOSE

#if defined(CONFIG_OPEN_CLOSE)
static int mxt_input_open(struct input_dev *dev)
{
	struct mxt_data *data = input_get_drvdata(dev);
	int ret;

	printk("[TSP] %s, %d \n", __func__, __LINE__);

	ret = wait_for_completion_interruptible_timeout(&data->init_done,
			msecs_to_jiffies(90 * MSEC_PER_SEC));

	if (ret < 0) {
		dev_err(&data->client->dev,
			"error while waiting for device to init (%d)\n", ret);
		ret = -ENXIO;
		goto err_open;
	}
	if (ret == 0) {
		dev_err(&data->client->dev,
			"timedout while waiting for device to init\n");
		ret = -ENXIO;
		goto err_open;
	}

	ret = mxt_start(data);
	if (ret)
		goto err_open;

	dev_dbg(&data->client->dev, "%s\n", __func__);

	return 0;

err_open:
	return ret;
}

static void mxt_input_close(struct input_dev *dev)
{
	struct mxt_data *data = input_get_drvdata(dev);

	printk("[TSP] %s, %d \n", __func__, __LINE__);

	mxt_stop(data);

	dev_dbg(&data->client->dev, "%s\n", __func__);
}

#elif defined(CONFIG_HAS_EARLYSUSPEND)
#define mxt_suspend	NULL
#define mxt_resume	NULL

static void mxt_early_suspend(struct early_suspend *h)
{
	struct mxt_data *data = container_of(h, struct mxt_data,
								early_suspend);
	
	printk("[TSP] %s, %d \n", __func__, __LINE__);

#if TSP_INFORM_CHARGER
	cancel_delayed_work_sync(&data->noti_dwork);
#endif

	mutex_lock(&data->input_dev->mutex);

	touch_is_pressed = 0;
	mxt_stop(data);

	mutex_unlock(&data->input_dev->mutex);
}

static void mxt_late_resume(struct early_suspend *h)
{
	struct mxt_data *data = container_of(h, struct mxt_data,

	printk("[TSP] %s, %d \n", __func__, __LINE__);
								early_suspend);
	mutex_lock(&data->input_dev->mutex);

	mxt_start(data);

	mutex_unlock(&data->input_dev->mutex);
}
#else
static int mxt_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mxt_data *data = i2c_get_clientdata(client);

	printk("[TSP] %s, %d \n", __func__, __LINE__);

	mutex_lock(&data->input_dev->mutex);

	touch_is_pressed = 0;
	mxt_stop(data);

	mutex_unlock(&data->input_dev->mutex);
	return 0;
}

static int mxt_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mxt_data *data = i2c_get_clientdata(client);

	printk("[TSP] %s, %d \n", __func__, __LINE__);

	mutex_lock(&data->input_dev->mutex);

	mxt_start(data);

	mutex_unlock(&data->input_dev->mutex);
	return 0;
}
#endif


/* Added for samsung dependent codes such as Factory test,
 * Touch booster, Related debug sysfs.
 */
#include "mxts_sec.c"

static int mxt_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct mxt_platform_data *pdata;
	struct mxt_data *data;
	struct input_dev *input_dev;
	u16 boot_address;
	int error = 0;
	int ret=0;


	printk(KERN_INFO "[TSP] %s,%d start + \n", __func__, __LINE__);
	
	touch_is_pressed = 0;

	if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev,
			sizeof(struct mxt_platform_data), GFP_KERNEL);
		if (!pdata) {
			dev_err(&client->dev, "Failed to allocate memory\n");
			return -ENOMEM;
		}

		ret = mxt_parse_dt(&client->dev, pdata);
		if (ret) {
			dev_err(&client->dev, "Error parsing dt %d\n", ret);
			return ret;
		}

		mxt_init_gpio(pdata);
		//pdata->vdd_on = zinitix_vdd_on;
		//pdata->is_vdd_on = NULL;
		n_gpio_irq = pdata->gpio_int;

		pdata->num_xnode = NUM_XNODE;
		pdata->num_ynode = NUM_YNODE;
		pdata->max_x = TOUCH_MAX_X - 1;
		pdata->max_y = TOUCH_MAX_Y - 1;
		pdata->irqflags = IRQF_TRIGGER_LOW | IRQF_ONESHOT;
		pdata->read_chg = mxts_read_chg;		// gpio_get_value(pdata->gpio_int);
		pdata->power_on = mxts_power_on;		
		pdata->power_off = mxts_power_off;		
		pdata->register_cb = mxts_register_callback;
		pdata->project_name = PROJECT_NAME;
		pdata->config_ver = CONFIG_VER;

	} else {
		pdata = client->dev.platform_data;

		if (!pdata) {
			dev_err(&client->dev, "Not exist platform data\n");
			return -EINVAL;
		}
	}


	
/*	if (!pdata) {
		dev_err(&client->dev, "Platform data is not proper\n");
		return -EINVAL;
	}
*/
	data = kzalloc(sizeof(struct mxt_data), GFP_KERNEL);
	if (!data) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	input_dev = input_allocate_device();
	if (!input_dev) {
		error = -ENOMEM;
		dev_err(&client->dev, "Input device allocation failed\n");
		goto err_allocate_input_device;
	}


	input_dev->name = "sec_touchscreen";
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;
	#ifdef CONFIG_OPEN_CLOSE
	input_dev->open = mxt_input_open;
	input_dev->close = mxt_input_close;
	#endif
	
	data->client = client;
	data->input_dev = input_dev;
	data->pdata = pdata;
	init_completion(&data->init_done);

	set_bit(EV_ABS, input_dev->evbit);
	set_bit(EV_KEY, input_dev->evbit);
	set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
	set_bit(BTN_TOUCH, input_dev->keybit);
	set_bit(BTN_TOOL_FINGER, input_dev->keybit);

#if ENABLE_TOUCH_KEY
	set_bit(KEY_MENU, input_dev->keybit);
	set_bit(KEY_BACK, input_dev->keybit);
#endif


	input_mt_init_slots(input_dev, MXT_MAX_FINGER);

	input_set_abs_params(input_dev, ABS_MT_POSITION_X,
				0, pdata->max_x, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y,
				0, pdata->max_y, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR,
				0, MXT_AREA_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_PRESSURE,
				0, MXT_AMPLITUDE_MAX, 0, 0);
#if TSP_USE_SHAPETOUCH
	input_set_abs_params(input_dev, ABS_MT_COMPONENT,
				0, MXT_COMPONENT_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_SUMSIZE,
				0, MXT_SUMSIZE_MAX, 0, 0);
#endif

	input_set_drvdata(input_dev, data);
	i2c_set_clientdata(client, data);


	vddo_vreg = regulator_get(&data->client->dev,"vddo");
	if (IS_ERR(vddo_vreg)){
		vddo_vreg = NULL;
		printk(KERN_INFO "info->vddo_vreg error\n");
		goto err_touch_init;
	}


	if (data->pdata->boot_address) {
		boot_address = data->pdata->boot_address;
	} else {
		if (client->addr == MXT_APP_LOW)
			boot_address = MXT_BOOT_LOW;
		else
			boot_address = MXT_BOOT_HIGH;
	}
	data->client_boot = i2c_new_dummy(client->adapter, boot_address);
	if (!data->client_boot) {
		dev_err(&client->dev, "Fail to register sub client[0x%x]\n",
			 boot_address);
		error = -ENODEV;
		goto err_create_sub_client;
	}

	/* regist input device */
	error = input_register_device(input_dev);
	if (error)
		goto err_register_input_device;

	error = mxt_sysfs_init(client);
	if (error < 0) {
		dev_err(&client->dev, "Failed to create sysfs\n");
		goto err_sysfs_init;
	}

	error = mxt_power_off(data);
	if (error) {
		dev_err(&client->dev, "Failed to power_off\n");
		goto err_power_on;
	}

	error = mxt_power_on(data);
	if (error) {
		dev_err(&client->dev, "Failed to power_on\n");
		goto err_power_on;
	}

//	error = mxt_set_firmware_name(data);
//	if (error) {
//		dev_err(&client->dev, "Failed to mxt_set_firmware_name \n");
//		//goto err_touch_init;
//	}
	
	error = mxt_touch_init(data, MXT_FIRMWARE_UPDATE_TYPE);
	if (error) {
		dev_err(&client->dev, "Failed to init driver\n");
		goto err_touch_init;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	data->early_suspend.suspend = mxt_early_suspend;
	data->early_suspend.resume = mxt_late_resume;
	register_early_suspend(&data->early_suspend);
#endif

	mxt_start(data);

	printk(KERN_INFO "[TSP] %s,%d end - \n", __func__, __LINE__);

	return 0;

err_touch_init:
	mxt_power_off(data);
err_power_on:
	mxt_sysfs_remove(data);
err_sysfs_init:
	input_unregister_device(input_dev);
	input_dev = NULL;
err_register_input_device:
	i2c_unregister_device(data->client_boot);
err_create_sub_client:
	input_free_device(input_dev);
err_allocate_input_device:
	kfree(data);

	return error;
}

static int mxt_remove(struct i2c_client *client)
{
	struct mxt_data *data = i2c_get_clientdata(client);

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&data->early_suspend);
#endif
	free_irq(client->irq, data);
	kfree(data->objects);
	kfree(data->reportids);
	input_unregister_device(data->input_dev);
	i2c_unregister_device(data->client_boot);
	mxt_sysfs_remove(data);
	mxt_power_off(data);
	kfree(data);

	return 0;
}

static struct i2c_device_id mxt_idtable[] = {
	{MXT_DEV_NAME, 0},
};

#ifdef CONFIG_OF
static struct of_device_id mxt_match_table[] = {
        { .compatible = "atmel,mxts_touch",},
        { },
};
#else
#define mxt_match_table NULL
#endif


MODULE_DEVICE_TABLE(i2c, mxt_idtable);

#if defined(CONFIG_PM) && !defined(CONFIG_HAS_EARLYSUSPEND) && !defined(CONFIG_OPEN_CLOSE)
static const struct dev_pm_ops mxt_pm_ops = {
	.suspend = mxt_suspend,
	.resume = mxt_resume,
};
#endif
static struct i2c_driver mxt_i2c_driver = {
	.id_table = mxt_idtable,
	.probe = mxt_probe,
	.remove = mxt_remove,
	.driver = {
		.owner	= THIS_MODULE,
		.name	= MXT_DEV_NAME,		
		.of_match_table = mxt_match_table,
#if defined(CONFIG_PM) && !defined(CONFIG_HAS_EARLYSUSPEND) && !defined(CONFIG_OPEN_CLOSE)
		.pm	= &mxt_pm_ops,
#endif
	},
};

static int __devinit mxt_ts_init(void)
{
	printk(KERN_INFO "[TSP] %s,%d \n", __func__, __LINE__);

	if (poweroff_charging) {
		printk("%s : LPM Charging Mode!!\n", __func__);
		return 0;
	}
	else
		return i2c_add_driver(&mxt_i2c_driver);
}

static void __exit mxt_ts_exit(void)
{
	printk(KERN_INFO "[TSP] %s,%d \n", __func__, __LINE__);
	i2c_del_driver(&mxt_i2c_driver);
}

module_init(mxt_ts_init);
module_exit(mxt_ts_exit);

MODULE_DESCRIPTION("Atmel MaXTouch driver");
MODULE_AUTHOR("bumwoo.lee<bw365.lee@samsung.com>");
MODULE_LICENSE("GPL");
