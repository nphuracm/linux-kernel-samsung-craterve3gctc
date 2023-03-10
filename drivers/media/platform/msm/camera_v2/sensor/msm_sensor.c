/* Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <mach/gpiomux.h>
#include "msm_sensor.h"
#include "msm_sd.h"
#include "camera.h"
#include "msm_cci.h"
#include "msm_camera_io_util.h"
#include "msm_camera_i2c_mux.h"

//#ifndef CONFIG_MSMB_CAMERA_DEBUG
//#define CONFIG_MSMB_CAMERA_DEBUG
//#endif

#undef CDBG
#ifdef CONFIG_MSMB_CAMERA_DEBUG
#define CDBG(fmt, args...) pr_err(fmt, ##args)
#else
#define CDBG(fmt, args...) do { } while (0)
#endif

#if defined(CONFIG_MACH_KS01SKT) || defined(CONFIG_MACH_KS01KTT)\
	|| defined(CONFIG_MACH_KS01LGT)
#define IMX135_FW_ADDRESS 0x1A
uint16_t back_cam_fw_version = 0;
#endif
int led_torch_en;
int led_flash_en;

static int32_t msm_sensor_get_dt_data(struct device_node *of_node,
	struct msm_sensor_ctrl_t *s_ctrl)
{
	int32_t rc = 0, i = 0;
	struct msm_camera_gpio_conf *gconf = NULL;
	struct msm_camera_sensor_board_info *sensordata = NULL;
	uint16_t *gpio_array = NULL;
	uint16_t gpio_array_size = 0;
	uint32_t id_info[3];

	s_ctrl->sensordata = kzalloc(sizeof(
		struct msm_camera_sensor_board_info),
		GFP_KERNEL);
	if (!s_ctrl->sensordata) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		return -ENOMEM;
	}

	sensordata = s_ctrl->sensordata;


	rc = of_property_read_string(of_node, "qcom,sensor-name",
		&sensordata->sensor_name);
	CDBG("%s qcom,sensor-name %s, rc %d\n", __func__,
		sensordata->sensor_name, rc);
	if (rc < 0) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		goto FREE_SENSORDATA;
	}

	rc = of_property_read_u32(of_node, "qcom,cci-master",
		&s_ctrl->cci_i2c_master);
	CDBG("%s qcom,cci-master %d, rc %d\n", __func__, s_ctrl->cci_i2c_master,
		rc);
	if (rc < 0) {
		/* Set default master 0 */
		s_ctrl->cci_i2c_master = MASTER_0;
		rc = 0;
	}

	rc = msm_sensor_get_sub_module_index(of_node, &sensordata->sensor_info);
	if (rc < 0) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		goto FREE_SENSORDATA;
	}
	rc = of_property_read_u32(of_node, "qcom,mount-angle",
		&sensordata->sensor_info->sensor_mount_angle);
	CDBG("%s qcom,mount-angle %d, rc %d\n", __func__,
		sensordata->sensor_info->sensor_mount_angle, rc);
	if (rc < 0) {
		sensordata->sensor_info->is_mount_angle_valid = 0;
		sensordata->sensor_info->sensor_mount_angle = 0;
		rc = 0;
	} else {
		sensordata->sensor_info->is_mount_angle_valid = 1;
	}

	rc = msm_sensor_get_dt_csi_data(of_node, &sensordata->csi_lane_params);
	if (rc < 0) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		goto FREE_SENSOR_INFO;
	}

	rc = msm_camera_get_dt_vreg_data(of_node,
			&sensordata->power_info.cam_vreg,
			&sensordata->power_info.num_vreg);
	if (rc < 0)
		goto FREE_CSI;
	rc = msm_camera_get_dt_power_setting_data(of_node,
			sensordata->power_info.cam_vreg,
			sensordata->power_info.num_vreg,
			&sensordata->power_info.power_setting,
			&sensordata->power_info.power_setting_size);
	if (rc < 0) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		goto FREE_PS;
	}

	rc = msm_camera_get_dt_power_off_setting_data(of_node,
			sensordata->power_info.cam_vreg,
			sensordata->power_info.num_vreg,
			&sensordata->power_info.power_off_setting,
			&sensordata->power_info.power_off_setting_size);
	if (rc < 0) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		goto FREE_PS;
	}

	sensordata->power_info.gpio_conf = kzalloc(
			sizeof(struct msm_camera_gpio_conf), GFP_KERNEL);
	if (!sensordata->power_info.gpio_conf) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		rc = -ENOMEM;
		goto FREE_VREG;
	}
	gconf = sensordata->power_info.gpio_conf;

	gpio_array_size = of_gpio_count(of_node);
	CDBG("%s gpio count %d\n", __func__, gpio_array_size);

	if (gpio_array_size) {
		gpio_array = kzalloc(sizeof(uint16_t) * gpio_array_size,
			GFP_KERNEL);
		if (!gpio_array) {
			pr_err("%s failed %d\n", __func__, __LINE__);
			goto FREE_GPIO_CONF;
		}
		for (i = 0; i < gpio_array_size; i++) {
			gpio_array[i] = of_get_gpio(of_node, i);
			CDBG("%s gpio_array[%d] = %d\n", __func__, i,
				gpio_array[i]);
		}

		rc = msm_camera_get_dt_gpio_req_tbl(of_node, gconf,
			gpio_array, gpio_array_size);
		if (rc < 0) {
			pr_err("%s failed %d\n", __func__, __LINE__);
			goto FREE_GPIO_CONF;
		}

		rc = msm_camera_get_dt_gpio_set_tbl(of_node, gconf,
			gpio_array, gpio_array_size);
		if (rc < 0) {
			pr_err("%s failed %d\n", __func__, __LINE__);
			goto FREE_GPIO_REQ_TBL;
		}

		rc = msm_camera_init_gpio_pin_tbl(of_node, gconf,
			gpio_array, gpio_array_size);
		if (rc < 0) {
			pr_err("%s failed %d\n", __func__, __LINE__);
			goto FREE_GPIO_SET_TBL;
		}
	}
	rc = msm_sensor_get_dt_actuator_data(of_node,
					     &sensordata->actuator_info);
	if (rc < 0) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		goto FREE_GPIO_PIN_TBL;
	}

	sensordata->slave_info = kzalloc(sizeof(struct msm_camera_slave_info),
		GFP_KERNEL);
	if (!sensordata->slave_info) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		rc = -ENOMEM;
		goto FREE_ACTUATOR_INFO;
	}

	rc = of_property_read_u32_array(of_node, "qcom,slave-id",
		id_info, 3);
	if (rc < 0) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		goto FREE_SLAVE_INFO;
	}

	sensordata->slave_info->sensor_slave_addr = id_info[0];
	sensordata->slave_info->sensor_id_reg_addr = id_info[1];
	sensordata->slave_info->sensor_id = id_info[2];

	kfree(gpio_array);

	return rc;

FREE_SLAVE_INFO:
	kfree(s_ctrl->sensordata->slave_info);
FREE_ACTUATOR_INFO:
	kfree(s_ctrl->sensordata->actuator_info);
FREE_GPIO_PIN_TBL:
	kfree(s_ctrl->sensordata->power_info.gpio_conf->gpio_num_info);
FREE_GPIO_SET_TBL:
	kfree(s_ctrl->sensordata->power_info.gpio_conf->cam_gpio_set_tbl);
FREE_GPIO_REQ_TBL:
	kfree(s_ctrl->sensordata->power_info.gpio_conf->cam_gpio_req_tbl);
FREE_GPIO_CONF:
	kfree(s_ctrl->sensordata->power_info.gpio_conf);
FREE_VREG:
	kfree(s_ctrl->sensordata->power_info.cam_vreg);
FREE_PS:
	kfree(s_ctrl->sensordata->power_info.power_setting);
FREE_CSI:
	kfree(s_ctrl->sensordata->csi_lane_params);
FREE_SENSOR_INFO:
	kfree(s_ctrl->sensordata->sensor_info);
FREE_SENSORDATA:
	kfree(s_ctrl->sensordata);
	kfree(gpio_array);
	return rc;
}

int32_t msm_sensor_free_sensor_data(struct msm_sensor_ctrl_t *s_ctrl)
{
	if (!s_ctrl->pdev)
		return 0;
	kfree(s_ctrl->sensordata->slave_info);
	kfree(s_ctrl->sensordata->actuator_info);
	kfree(s_ctrl->sensordata->power_info.gpio_conf->gpio_num_info);
	kfree(s_ctrl->sensordata->power_info.gpio_conf->cam_gpio_set_tbl);
	kfree(s_ctrl->sensordata->power_info.gpio_conf->cam_gpio_req_tbl);
	kfree(s_ctrl->sensordata->power_info.gpio_conf);
	kfree(s_ctrl->sensordata->power_info.cam_vreg);
	kfree(s_ctrl->sensordata->power_info.power_setting);
	kfree(s_ctrl->sensordata->power_info.clk_info);
	kfree(s_ctrl->sensordata->csi_lane_params);
	kfree(s_ctrl->sensordata->sensor_info);
	kfree(s_ctrl->sensordata);
	return 0;
}

#if !defined(CONFIG_MACH_HLITE_CHN_SGLTE)
static struct msm_cam_clk_info cam_8960_clk_info[] = {
	[SENSOR_CAM_MCLK] = {"cam_clk", 24000000},
};
#endif

static struct msm_cam_clk_info cam_8974_clk_info[] = {
	[SENSOR_CAM_MCLK] = {"cam_src_clk", 24000000},
	[SENSOR_CAM_CLK] = {"cam_clk", 0},
};

int msm_sensor_power_down(struct msm_sensor_ctrl_t *s_ctrl,
	struct msm_camera_power_ctrl_t *power_info,
	enum msm_camera_device_type_t sensor_device_type,
	struct msm_camera_i2c_client *sensor_i2c_client)
{
	int rc = 0;
        pr_err("%s\n", __func__);
	if (!power_info || !sensor_i2c_client) {
		pr_err("%s:%d failed: power_info %p sensor_i2c_client %p\n",
			__func__, __LINE__, power_info, sensor_i2c_client);
		return -EINVAL;
	}
	
	if (s_ctrl->sensor_state == MSM_SENSOR_POWER_DOWN) {
		pr_err("%s:%d invalid sensor state %d\n", __func__, __LINE__,
			s_ctrl->sensor_state);
		return 0;
	}	
	
	pr_warn("[%s:%d]", __func__, __LINE__);
	rc = msm_camera_power_down(power_info, sensor_device_type,
		sensor_i2c_client);
	s_ctrl->sensor_state = MSM_SENSOR_POWER_DOWN;
	return rc;
}

int msm_sensor_power_up(struct msm_sensor_ctrl_t *s_ctrl,
	struct msm_camera_power_ctrl_t *power_info,
	struct msm_camera_i2c_client *sensor_i2c_client,
	struct msm_camera_slave_info *slave_info,
	const char *sensor_name)
{
	int rc;

	pr_err("%s\n", __func__);
	if (!s_ctrl || !power_info || !sensor_i2c_client || !slave_info ||
		!sensor_name) {
		pr_err("%s:%d failed: %p %p %p %p %p\n",
			__func__, __LINE__, s_ctrl, power_info,
			sensor_i2c_client, slave_info, sensor_name);
		return -EINVAL;
	}
	
	if (s_ctrl->sensor_state == MSM_SENSOR_POWER_UP) {
		pr_err("%s:%d invalid sensor state %d\n", __func__, __LINE__,
			s_ctrl->sensor_state);
		return -EINVAL;
	}

	pr_warn("[%s:%d] %s", __func__, __LINE__,
		sensor_name);
	rc = msm_camera_power_up(power_info, s_ctrl->sensor_device_type,
		sensor_i2c_client);
	if (rc < 0) {
            pr_err("%s : power up failed", __func__);
            return rc;
        }
	s_ctrl->sensor_state = MSM_SENSOR_POWER_UP;
	pr_err("%s: %d\n", __func__, s_ctrl->sensor_state);
#if 1
	rc = msm_sensor_check_id(s_ctrl, sensor_i2c_client, slave_info,
		sensor_name);
//	if (rc < 0)
//		msm_sensor_power_down(power_info, s_ctrl->sensor_device_type,
//		sensor_i2c_client);
#endif

	return rc;
}

int msm_sensor_match_id(struct msm_camera_i2c_client *sensor_i2c_client,
	struct msm_camera_slave_info *slave_info,
	const char *sensor_name)
{
	int rc = 0;
	uint16_t chipid = 0;
	enum msm_camera_i2c_data_type data_type = MSM_CAMERA_I2C_WORD_DATA;

	if (!sensor_i2c_client || !slave_info || !sensor_name) {
		pr_err("%s:%d failed: %p %p %p\n",
			__func__, __LINE__, sensor_i2c_client, slave_info,
			sensor_name);
		return -EINVAL;
	}
	printk("%s sensor_name=%s\n", __func__, sensor_name);

	if (sensor_i2c_client->data_type == MSM_CAMERA_I2C_BYTE_DATA)
		data_type = MSM_CAMERA_I2C_BYTE_DATA;


	pr_info("func %s line %d sid = 0x%X sensorid = 0x%X DATA TYPE = %d\n",
		__func__, __LINE__, sensor_i2c_client->cci_client->sid,
		slave_info->sensor_id, sensor_i2c_client->data_type);	

	rc = sensor_i2c_client->i2c_func_tbl->i2c_read(
		sensor_i2c_client, slave_info->sensor_id_reg_addr,
			&chipid, data_type);

	pr_info("func %s line %d chipid = 0x%X\n",
		__func__, __LINE__, chipid);
#if defined(CONFIG_MACH_VICTOR_CHN_SGLTE)

        sensor_i2c_client->i2c_func_tbl->i2c_write(sensor_i2c_client, 0x002C, 0x7000, MSM_CAMERA_I2C_WORD_DATA);
	sensor_i2c_client->i2c_func_tbl->i2c_write(sensor_i2c_client, 0x002E, 0x01A6, MSM_CAMERA_I2C_WORD_DATA);
        sensor_i2c_client->i2c_func_tbl->i2c_read( sensor_i2c_client, 0x0F12, &chipid, MSM_CAMERA_I2C_WORD_DATA);

	pr_err("%s: CONFIG_MACH_VICTOR_CHN_SGLTE READ ID: 0x%x \n", __func__, chipid);

	return 0;
#endif

	if (chipid != slave_info->sensor_id) {
	    sensor_i2c_client->cci_client->sid = 0x6E >> 1;
	    slave_info->sensor_id = 0x5B02;
		chipid = 0;
		usleep(10);
		pr_info("%s Slave ID1 =%x Sensor_ID1=%x\n", __func__, sensor_i2c_client->cci_client->sid, slave_info->sensor_id); 
	    rc = sensor_i2c_client->i2c_func_tbl->i2c_read(
		 sensor_i2c_client, slave_info->sensor_id_reg_addr,
			&chipid, MSM_CAMERA_I2C_WORD_DATA);
	   pr_info("%s: read id1: %x expected id1 %x:\n", __func__, chipid,
		slave_info->sensor_id);
		if (chipid != slave_info->sensor_id) {
			sensor_i2c_client->cci_client->sid = 0x20 >> 1;
			slave_info->sensor_id = 0x5B02;
			chipid = 0;
			pr_info("%s Slave ID2 =%x Sensor_ID2=%x\n", __func__, sensor_i2c_client->cci_client->sid, slave_info->sensor_id); 
			rc = sensor_i2c_client->i2c_func_tbl->i2c_read(
			 sensor_i2c_client, slave_info->sensor_id_reg_addr,
				&chipid, MSM_CAMERA_I2C_WORD_DATA);
				pr_err("%s: read id2: %x expected id2 %x:\n", __func__, chipid,
		slave_info->sensor_id);
			if (chipid != slave_info->sensor_id) {
				sensor_i2c_client->cci_client->sid = 0x20 >> 1;
				slave_info->sensor_id = 0x5B01;
				chipid = 0;
				printk("%s Slave ID3 =%x Sensor_ID3=%x\n", __func__, sensor_i2c_client->cci_client->sid, slave_info->sensor_id); 
				rc = sensor_i2c_client->i2c_func_tbl->i2c_read(
				 sensor_i2c_client, slave_info->sensor_id_reg_addr,
					&chipid, MSM_CAMERA_I2C_WORD_DATA);
					pr_err("%s: read id3: %x expected id3 %x:\n", __func__, chipid,
		slave_info->sensor_id);
			}
			if (chipid != slave_info->sensor_id) {
					sensor_i2c_client->cci_client->sid = 0x6a >> 1;
					slave_info->sensor_id = 0x6b20;
					chipid = 0;
					printk("%s s5k6b2yx Slave ID =%x Sensor_ID3=%x\n", __func__, sensor_i2c_client->cci_client->sid, slave_info->sensor_id); 
					rc = sensor_i2c_client->i2c_func_tbl->i2c_read(
					 sensor_i2c_client, slave_info->sensor_id_reg_addr,
						&chipid, MSM_CAMERA_I2C_WORD_DATA);
						pr_err("%s: read id3: %x expected id3 %x:\n", __func__, chipid,
			slave_info->sensor_id);
				}
			if (chipid != slave_info->sensor_id) {
				sensor_i2c_client->cci_client->sid = 0x40 >> 1;
#if defined(CONFIG_MACH_CRATERQ_CHN_OPEN)  || defined(CONFIG_MACH_CRATERVE_CHN_CTC)
				slave_info->sensor_id = 0xB401;
#else
				slave_info->sensor_id = 0x5B01;
#endif
				chipid = 0;
				pr_info("%s Slave ID3 =%x Sensor_ID3=%x\n", __func__, sensor_i2c_client->cci_client->sid, slave_info->sensor_id); 
				rc = sensor_i2c_client->i2c_func_tbl->i2c_read(
				 sensor_i2c_client, slave_info->sensor_id_reg_addr,
					&chipid, MSM_CAMERA_I2C_WORD_DATA);
					pr_err("%s: read id3: %x expected id3 %x:\n", __func__, chipid,
		slave_info->sensor_id);
			}
		}
	}
    pr_info("rc == %d \n", rc);
	pr_info("%s: read id: %x expected id %x:\n", __func__, chipid,
		slave_info->sensor_id);
	pr_warn("%s: read id: %x expected id %x:\n", __func__, chipid,
		slave_info->sensor_id);
	pr_info("%s: read id: %x expected id %x:\n", __func__, chipid,
		slave_info->sensor_id);

	if (chipid != slave_info->sensor_id) {
		pr_info("msm_sensor_match_id chip id doesnot match\n");
	}
	return 0;
}

static struct msm_sensor_ctrl_t *get_sctrl(struct v4l2_subdev *sd)
{
	return container_of(container_of(sd, struct msm_sd_subdev, sd),
		struct msm_sensor_ctrl_t, msm_sd);
}

static void msm_sensor_stop_stream(struct msm_sensor_ctrl_t *s_ctrl)
{
	if (s_ctrl->sensor_state != MSM_SENSOR_POWER_UP) {
		pr_err("%s:%d %s not in power up state\n", __func__, __LINE__,
			s_ctrl->sensordata->sensor_name);
		return;
	}
 
	if (s_ctrl->stop_setting.reg_setting) {
	s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_write_table(
		s_ctrl->sensor_i2c_client, &s_ctrl->stop_setting);
		kfree(s_ctrl->stop_setting.reg_setting);
		s_ctrl->stop_setting.reg_setting = NULL;
		s_ctrl->stop_setting.size = 0;
	}
	return;
}

static long msm_sensor_subdev_ioctl(struct v4l2_subdev *sd,
			unsigned int cmd, void *arg)
{
	struct msm_sensor_ctrl_t *s_ctrl = get_sctrl(sd);
	void __user *argp = (void __user *)arg;
	if (!s_ctrl) {
		pr_err("%s s_ctrl NULL\n", __func__);
		return -EBADF;
	}
	switch (cmd) {
	case VIDIOC_MSM_SENSOR_CFG:
		return s_ctrl->func_tbl->sensor_config(s_ctrl, argp);
	case VIDIOC_MSM_SENSOR_RELEASE:
            pr_warn("%s : msm_sensor_stop_stream", __func__);
            msm_sensor_stop_stream(s_ctrl);
            return 0;
	case MSM_SD_SHUTDOWN:
		pr_err("%s:%d MSM_SD_SHUTDOWN\n", __func__, __LINE__);
		return 0;
	case VIDIOC_MSM_SENSOR_NATIVE_CMD:
		if( s_ctrl->func_tbl->sensor_native_control != NULL )
			return s_ctrl->func_tbl->sensor_native_control(s_ctrl, argp);
		else
			pr_err("s_ctrl->func_tbl->sensor_native_control is NULL\n");
	case VIDIOC_MSM_SENSOR_GET_EXIF_DATA:
		    return s_ctrl->func_tbl->sensor_get_exif_data(s_ctrl, argp);
	case VIDIOC_MSM_SENSOR_GET_AF_STATUS:
		return s_ctrl->func_tbl->sensor_get_af_status(s_ctrl, argp);
			
	default:
		return -ENOIOCTLCMD;
	}
}

int msm_sensor_config(struct msm_sensor_ctrl_t *s_ctrl, void __user *argp)
{
	struct sensorb_cfg_data *cdata = (struct sensorb_cfg_data *)argp;
	long rc = 0;
	int i = 0;
	mutex_lock(s_ctrl->msm_sensor_mutex);
	CDBG("%s:%d %s cfgtype = %d\n", __func__, __LINE__,
		s_ctrl->sensordata->sensor_name, cdata->cfgtype);
	switch (cdata->cfgtype) {
	case CFG_GET_SENSOR_INFO:
                pr_info("CFG_GET_SENSOR_INFO");
		memcpy(cdata->cfg.sensor_info.sensor_name,
			s_ctrl->sensordata->sensor_name,
			sizeof(cdata->cfg.sensor_info.sensor_name));
		cdata->cfg.sensor_info.session_id =
			s_ctrl->sensordata->sensor_info->session_id;
		for (i = 0; i < SUB_MODULE_MAX; i++)
			cdata->cfg.sensor_info.subdev_id[i] =
				s_ctrl->sensordata->sensor_info->subdev_id[i];
		cdata->cfg.sensor_info.is_mount_angle_valid =
			s_ctrl->sensordata->sensor_info->is_mount_angle_valid;
		cdata->cfg.sensor_info.sensor_mount_angle =
			s_ctrl->sensordata->sensor_info->sensor_mount_angle;
		pr_info("%s:%d sensor name %s\n", __func__, __LINE__,
			cdata->cfg.sensor_info.sensor_name);
		pr_info("%s:%d session id %d\n", __func__, __LINE__,
			cdata->cfg.sensor_info.session_id);
		for (i = 0; i < SUB_MODULE_MAX; i++)
			CDBG("%s:%d subdev_id[%d] %d\n", __func__, __LINE__, i,
				cdata->cfg.sensor_info.subdev_id[i]);
		CDBG("%s:%d mount angle valid %d value %d\n", __func__,
			__LINE__, cdata->cfg.sensor_info.is_mount_angle_valid,
			cdata->cfg.sensor_info.sensor_mount_angle);
		break;
	case CFG_SET_SLAVE_INFO: {
		struct msm_camera_sensor_slave_info sensor_slave_info;
		struct msm_camera_power_ctrl_t *p_ctrl;
		uint16_t size;
		int slave_index = 0;
		if (copy_from_user(&sensor_slave_info,
			(void *)cdata->cfg.setting,
				sizeof(sensor_slave_info))) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			rc = -EFAULT;
			break;
		}
		/* Update sensor slave address */
		if (sensor_slave_info.slave_addr) {
			s_ctrl->sensor_i2c_client->cci_client->sid =
				sensor_slave_info.slave_addr >> 1;
		}

		/* Update sensor address type */
		s_ctrl->sensor_i2c_client->addr_type =
			sensor_slave_info.addr_type;

		/* Update power up / down sequence */
		p_ctrl = &s_ctrl->sensordata->power_info;
		size = sensor_slave_info.power_setting_array.size;
		if (p_ctrl->power_setting_size < size) {
			struct msm_sensor_power_setting *tmp;
			tmp = kmalloc(sizeof(*tmp) * size, GFP_KERNEL);
			if (!tmp) {
				pr_err("%s: failed to alloc mem\n", __func__);
			rc = -ENOMEM;
			break;
			}
			kfree(p_ctrl->power_setting);
			p_ctrl->power_setting = tmp;
		}
		p_ctrl->power_setting_size = size;
		rc = copy_from_user(p_ctrl->power_setting, (void *)
			sensor_slave_info.power_setting_array.power_setting,
			size * sizeof(struct msm_sensor_power_setting));
		if (rc) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			kfree(sensor_slave_info.power_setting_array.
				power_setting);
			rc = -EFAULT;
			break;
		}
		CDBG("%s sensor id %x\n", __func__,
			sensor_slave_info.slave_addr);
		CDBG("%s sensor addr type %d\n", __func__,
			sensor_slave_info.addr_type);
		CDBG("%s sensor reg %x\n", __func__,
			sensor_slave_info.sensor_id_info.sensor_id_reg_addr);
		CDBG("%s sensor id %x\n", __func__,
			sensor_slave_info.sensor_id_info.sensor_id);
		for (slave_index = 0; slave_index <
			p_ctrl->power_setting_size; slave_index++) {
			CDBG("%s i %d power setting %d %d %ld %d\n", __func__,
				slave_index,
				p_ctrl->power_setting[slave_index].seq_type,
				p_ctrl->power_setting[slave_index].seq_val,
				p_ctrl->power_setting[slave_index].config_val,
				p_ctrl->power_setting[slave_index].delay);
		}
		break;
	}
	case CFG_WRITE_I2C_ARRAY: {
		struct msm_camera_i2c_reg_setting conf_array;
		struct msm_camera_i2c_burst_reg_array *burst_reg_setting = NULL;
		void *reg_setting = NULL;
		uint8_t *reg_data = NULL;
		uint32_t i = 0, size = 0;

		if (copy_from_user(&conf_array,
			(void *)cdata->cfg.setting,
			sizeof(struct msm_camera_i2c_reg_setting))) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			rc = -EFAULT;
			break;
		}

		if (conf_array.data_type == MSM_CAMERA_I2C_BURST_DATA) {

			burst_reg_setting = (void *)kzalloc(conf_array.size *
				(sizeof(struct msm_camera_i2c_burst_reg_array)), GFP_KERNEL);
			if (!burst_reg_setting) {
				pr_err("%s:%d failed\n", __func__, __LINE__);
				rc = -ENOMEM;
				break;
			}
			if (copy_from_user((void *)burst_reg_setting,
				(void *)conf_array.reg_setting,
				conf_array.size *
				sizeof(struct msm_camera_i2c_burst_reg_array))) {
				pr_err("%s:%d failed\n", __func__, __LINE__);
				kfree(burst_reg_setting);
				rc = -EFAULT;
				break;
			}

			size = conf_array.size;
			conf_array.size = 1;

			for (i = 0; i < size; i++) {
				reg_data = kzalloc(burst_reg_setting[i].reg_data_size *
					(sizeof(uint8_t)), GFP_KERNEL);
				if (!reg_data) {
					pr_err("%s:%d failed\n", __func__, __LINE__);
					rc = -ENOMEM;
					break;
				}
				if (copy_from_user(reg_data,
					(void *)burst_reg_setting[i].reg_data,
					burst_reg_setting[i].reg_data_size *
					(sizeof(uint8_t)))) {
					pr_err("%s:%d failed\n", __func__, __LINE__);
					kfree(reg_data);
					continue;
				}

				burst_reg_setting[i].reg_data = reg_data;
				conf_array.reg_setting = &burst_reg_setting[i];
				rc = s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_write_table(
					s_ctrl->sensor_i2c_client, &conf_array);
				if (rc < 0) {
					pr_err("%s:%d failed i2c_write_table rc %ld\n", __func__,
						__LINE__, rc);
				}
				kfree(reg_data);
			}

		} else {
			reg_setting = (void *)kzalloc(conf_array.size *
				(sizeof(struct msm_camera_i2c_reg_array)), GFP_KERNEL);
			if (!reg_setting) {
				pr_err("%s:%d failed\n", __func__, __LINE__);
				rc = -ENOMEM;
				break;
			}
			if (copy_from_user(reg_setting, (void *)conf_array.reg_setting,
				conf_array.size *
				sizeof(struct msm_camera_i2c_reg_array))) {
				pr_err("%s:%d failed\n", __func__, __LINE__);
				kfree(reg_setting);
				reg_setting = NULL;
				rc = -EFAULT;
				break;
			}

			conf_array.reg_setting = reg_setting;
			rc = s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_write_table(
				s_ctrl->sensor_i2c_client, &conf_array);
			if (rc < 0) {
				pr_err("%s:%d failed i2c_write_table rc %ld\n", __func__, __LINE__,
					rc);
			}
		}

		kfree(burst_reg_setting);
		burst_reg_setting = NULL;
		kfree(reg_setting);
		reg_setting = NULL;
//		kfree(reg_data);
//		reg_data = NULL;
		break;
	}
	case CFG_SLAVE_READ_I2C: {
		struct msm_camera_i2c_read_config read_config;
		uint16_t local_data = 0;
		uint16_t orig_slave_addr = 0, read_slave_addr = 0;
		if (copy_from_user(&read_config,
			(void *)cdata->cfg.setting,
			sizeof(struct msm_camera_i2c_read_config))) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			rc = -EFAULT;
			break;
		}
		read_slave_addr = read_config.slave_addr;
		CDBG("%s:CFG_SLAVE_READ_I2C:", __func__);
		CDBG("%s:slave_addr=0x%x reg_addr=0x%x, data_type=%d\n",
			__func__, read_config.slave_addr,
			read_config.reg_addr, read_config.data_type);
		if (s_ctrl->sensor_i2c_client->cci_client) {
			orig_slave_addr =
				s_ctrl->sensor_i2c_client->cci_client->sid;
			s_ctrl->sensor_i2c_client->cci_client->sid =
				read_slave_addr >> 1;
		} else if (s_ctrl->sensor_i2c_client->client) {
			orig_slave_addr =
				s_ctrl->sensor_i2c_client->client->addr;
			s_ctrl->sensor_i2c_client->client->addr =
				read_slave_addr >> 1;
		} else {
			pr_err("%s: error: no i2c/cci client found.", __func__);
			rc = -EFAULT;
			break;
		}
		CDBG("%s:orig_slave_addr=0x%x, new_slave_addr=0x%x",
				__func__, orig_slave_addr,
				read_slave_addr >> 1);
		rc = s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_read(
				s_ctrl->sensor_i2c_client,
				read_config.reg_addr,
				&local_data, read_config.data_type);
		if (rc < 0) {
			pr_err("%s:%d: i2c_read failed\n", __func__, __LINE__);
			break;
		}
		if (copy_to_user((void __user *)read_config.data,
			(void *)&local_data, sizeof(uint16_t))) {
			pr_err("%s:%d copy failed\n", __func__, __LINE__);
			rc = -EFAULT;
			break;
		}
		break;
	}	
	case CFG_WRITE_I2C_SEQ_ARRAY: {
		struct msm_camera_i2c_seq_reg_setting conf_array;
		struct msm_camera_i2c_seq_reg_array *reg_setting = NULL;

		if (copy_from_user(&conf_array,
			(void *)cdata->cfg.setting,
			sizeof(struct msm_camera_i2c_seq_reg_setting))) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			rc = -EFAULT;
			break;
		}

		reg_setting = kzalloc(conf_array.size *
			(sizeof(struct msm_camera_i2c_seq_reg_array)),
			GFP_KERNEL);
		if (!reg_setting) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			rc = -ENOMEM;
			break;
		}
		if (copy_from_user(reg_setting, (void *)conf_array.reg_setting,
			conf_array.size *
			sizeof(struct msm_camera_i2c_seq_reg_array))) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			kfree(reg_setting);
			reg_setting = NULL;
			rc = -EFAULT;
			break;
		}

		conf_array.reg_setting = reg_setting;
		rc = s_ctrl->sensor_i2c_client->i2c_func_tbl->
			i2c_write_seq_table(s_ctrl->sensor_i2c_client,
			&conf_array);
		kfree(reg_setting);
		reg_setting = NULL;
		break;
	}

	case CFG_POWER_UP:
                if (s_ctrl->func_tbl->sensor_power_up) {
                        CDBG("CFG_POWER_UP");
			rc = s_ctrl->func_tbl->sensor_power_up(s_ctrl,
				&s_ctrl->sensordata->power_info,
				s_ctrl->sensor_i2c_client,
				s_ctrl->sensordata->slave_info,
				s_ctrl->sensordata->sensor_name);
                } else
			rc = -EFAULT;
		break;

	case CFG_POWER_DOWN:
                if (s_ctrl->func_tbl->sensor_power_down) {
                        CDBG("CFG_POWER_DOWN");
			rc = s_ctrl->func_tbl->sensor_power_down(
				s_ctrl,
				&s_ctrl->sensordata->power_info,
				s_ctrl->sensor_device_type,
				s_ctrl->sensor_i2c_client);
                } else
			rc = -EFAULT;
		break;

	case CFG_SET_STOP_STREAM_SETTING: {
		struct msm_camera_i2c_reg_setting *stop_setting =
			&s_ctrl->stop_setting;
		struct msm_camera_i2c_reg_array *reg_setting = NULL;
		if (copy_from_user(stop_setting,
			(void *)cdata->cfg.setting,
			sizeof(struct msm_camera_i2c_reg_setting))) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			rc = -EFAULT;
			break;
		}

		reg_setting = stop_setting->reg_setting;
		stop_setting->reg_setting = kzalloc(stop_setting->size *
			(sizeof(struct msm_camera_i2c_reg_array)), GFP_KERNEL);
		if (!stop_setting->reg_setting) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			rc = -ENOMEM;
			break;
		}
		if (copy_from_user(stop_setting->reg_setting,
			(void *)reg_setting,
			stop_setting->size *
			sizeof(struct msm_camera_i2c_reg_array))) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			kfree(stop_setting->reg_setting);
			stop_setting->reg_setting = NULL;
			stop_setting->size = 0;
			rc = -EFAULT;
			break;
		}
		kfree(stop_setting->reg_setting);
		stop_setting->reg_setting =NULL;
		break;
	}
	case CFG_SET_GPIO_STATE: {
		struct msm_sensor_gpio_config gpio_config;
		struct msm_camera_power_ctrl_t *data = &s_ctrl->sensordata->power_info;
		if (copy_from_user(&gpio_config,
			(void *)cdata->cfg.setting,
			sizeof(gpio_config))) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
			rc = -EFAULT;
			break;
		}
		pr_info("%s: setting gpio: %d to %d\n", __func__,
			data->gpio_conf->gpio_num_info->gpio_num[gpio_config.gpio_name],
			gpio_config.config_val);
		gpio_set_value_cansleep(
			data->gpio_conf->gpio_num_info->gpio_num[gpio_config.gpio_name],
			gpio_config.config_val);
		break;
	}

    // Randy 10.08 s [
	case CFG_SET_SENSOR_OTP_CAL: {
        const uint16_t otp_start = 0xa3d, otp_end = 0xa42;
		uint16_t otp_cal_data[6], temp_data;
		int idx = 0;
        if (s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_write(
            s_ctrl->sensor_i2c_client,
            0x0A02, 0x0F, // Set the PAGE15 of OTP  set read mode of NVM controller Interface
            MSM_CAMERA_I2C_BYTE_DATA) < 0) {
          pr_err("%s:%d Failed I2C write\n", __func__, __LINE__);
		  rc = -EFAULT;
          break;
        }
        if (s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_write(
            s_ctrl->sensor_i2c_client,
            0x0A00, 0x01, // Set read mode of NVM controller Interface
            MSM_CAMERA_I2C_BYTE_DATA) < 0) {
          pr_err("%s:%d Failed I2C write\n", __func__, __LINE__);
		  rc = -EFAULT;
          break;
        }

        for (i = otp_start; i <= otp_end; i++) {
          if (s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_read(
              s_ctrl->sensor_i2c_client, i,
              &otp_cal_data[idx++], MSM_CAMERA_I2C_BYTE_DATA) < 0) {
            pr_err("%s:%d Failed I2C read\n", __func__, __LINE__);
			rc = -EFAULT;
            break;
          }
		  pr_err("%s: 0x%x, 0x%x\n", __func__, i, otp_cal_data[idx - 1]);
		  if ((i+1)%2) {
		    temp_data = ((otp_cal_data[idx - 2] << 8) & 0xFF00) |
			  (otp_cal_data[idx - 1] & 0xFF);
			// Valid check : +-50%
		    if (temp_data > 0x180 || temp_data < 0x80) {
			  pr_err("%s: range over (0x%x)\n", __func__, temp_data);
			  rc = -EFAULT;
			  break;
			}
		  }
        }
		
        if (s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_write(
            s_ctrl->sensor_i2c_client,
            0x0A00, 0x00, // Disable NVM controller
            MSM_CAMERA_I2C_BYTE_DATA) < 0) {
          pr_err("%s:%d Failed I2C write\n", __func__, __LINE__);
		  rc = -EFAULT;
          break;
        }

        if (rc >= 0) {
          if (s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_write(
              s_ctrl->sensor_i2c_client, 0x020E, otp_cal_data[2], // G msb
              MSM_CAMERA_I2C_BYTE_DATA) < 0) {
            pr_err("%s:%d Failed I2C write\n", __func__, __LINE__);
          }
          if (s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_write(
              s_ctrl->sensor_i2c_client, 0x020F, otp_cal_data[3], // G lsb
              MSM_CAMERA_I2C_BYTE_DATA) < 0) {
            pr_err("%s:%d Failed I2C write\n", __func__, __LINE__);
          }
          if (s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_write(
              s_ctrl->sensor_i2c_client, 0x0210, otp_cal_data[0], // R msb
              MSM_CAMERA_I2C_BYTE_DATA) < 0) {
            pr_err("%s:%d Failed I2C write\n", __func__, __LINE__);
          }
          if (s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_write(
              s_ctrl->sensor_i2c_client, 0x0211, otp_cal_data[1], // R lsb
              MSM_CAMERA_I2C_BYTE_DATA) < 0) {
            pr_err("%s:%d Failed I2C write\n", __func__, __LINE__);
          }
          if (s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_write(
              s_ctrl->sensor_i2c_client, 0x0212, otp_cal_data[4], // B msb
              MSM_CAMERA_I2C_BYTE_DATA) < 0) {
            pr_err("%s:%d Failed I2C write\n", __func__, __LINE__);
          }
          if (s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_write(
              s_ctrl->sensor_i2c_client, 0x0213, otp_cal_data[5], // B lsb
              MSM_CAMERA_I2C_BYTE_DATA) < 0) {
            pr_err("%s:%d Failed I2C write\n", __func__, __LINE__);
          }
          if (s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_write(
              s_ctrl->sensor_i2c_client, 0x0214, otp_cal_data[2], // G msb
              MSM_CAMERA_I2C_BYTE_DATA) < 0) {
            pr_err("%s:%d Failed I2C write\n", __func__, __LINE__);
          }
          if (s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_write(
              s_ctrl->sensor_i2c_client, 0x0215, otp_cal_data[3], // G lsb
              MSM_CAMERA_I2C_BYTE_DATA) < 0) {
            pr_err("%s:%d Failed I2C write\n", __func__, __LINE__);
          }
        }
		break;
	}
	// ]

	default:
		rc = -EFAULT;
		break;
	}

	mutex_unlock(s_ctrl->msm_sensor_mutex);

	return rc;
}

int msm_sensor_check_id(struct msm_sensor_ctrl_t *s_ctrl,
	struct msm_camera_i2c_client *sensor_i2c_client,
	struct msm_camera_slave_info *slave_info,
	const char *sensor_name)
{
	int rc;
	pr_err("func %s line %d slave_info->sensor_id = 0x%X\n",
		__func__, __LINE__, slave_info->sensor_id);

	if (s_ctrl->func_tbl->sensor_match_id)
		rc = s_ctrl->func_tbl->sensor_match_id(sensor_i2c_client,
			slave_info, sensor_name);
	else
		rc = msm_sensor_match_id(sensor_i2c_client,
			slave_info, sensor_name);
	if (rc < 0)
		pr_err("%s:%d match id failed rc %d\n", __func__, __LINE__, rc);
	return rc;
}
static int32_t msm_sensor_power(struct v4l2_subdev *sd, int on)
{
	int rc = 0;
	struct msm_sensor_ctrl_t *s_ctrl = get_sctrl(sd);
	if (!on)
		rc = s_ctrl->func_tbl->sensor_power_down(
			s_ctrl,
			&s_ctrl->sensordata->power_info,
			s_ctrl->sensor_device_type,
			s_ctrl->sensor_i2c_client);
	return rc;
}

static int msm_sensor_v4l2_enum_fmt(struct v4l2_subdev *sd,
	unsigned int index, enum v4l2_mbus_pixelcode *code)
{
	struct msm_sensor_ctrl_t *s_ctrl = get_sctrl(sd);

	if ((unsigned int)index >= s_ctrl->sensor_v4l2_subdev_info_size)
		return -EINVAL;

	*code = s_ctrl->sensor_v4l2_subdev_info[index].code;
	return 0;
}

static struct v4l2_subdev_core_ops msm_sensor_subdev_core_ops = {
	.ioctl = msm_sensor_subdev_ioctl,
	.s_power = msm_sensor_power,
};

static struct v4l2_subdev_video_ops msm_sensor_subdev_video_ops = {
	.enum_mbus_fmt = msm_sensor_v4l2_enum_fmt,
};

static struct v4l2_subdev_ops msm_sensor_subdev_ops = {
	.core = &msm_sensor_subdev_core_ops,
	.video  = &msm_sensor_subdev_video_ops,
};

static struct msm_sensor_fn_t msm_sensor_func_tbl = {
	.sensor_config = msm_sensor_config,
	.sensor_power_up = msm_sensor_power_up,
	.sensor_power_down = msm_sensor_power_down,
	.sensor_match_id = msm_sensor_match_id,
};

static struct msm_camera_i2c_fn_t msm_sensor_cci_func_tbl = {
	.i2c_read = msm_camera_cci_i2c_read,
	.i2c_read_seq = msm_camera_cci_i2c_read_seq,
	.i2c_write = msm_camera_cci_i2c_write,
	.i2c_write_table = msm_camera_cci_i2c_write_table,
	.i2c_write_seq_table = msm_camera_cci_i2c_write_seq_table,
	.i2c_write_table_w_microdelay =
		msm_camera_cci_i2c_write_table_w_microdelay,
	.i2c_util = msm_sensor_cci_i2c_util,
	.i2c_write_conf_tbl = msm_camera_cci_i2c_write_conf_tbl,
};

static struct msm_camera_i2c_fn_t msm_sensor_qup_func_tbl = {
	.i2c_read = msm_camera_qup_i2c_read,
	.i2c_read_seq = msm_camera_qup_i2c_read_seq,
	.i2c_write = msm_camera_qup_i2c_write,
	.i2c_write_table = msm_camera_qup_i2c_write_table,
	.i2c_write_seq_table = msm_camera_qup_i2c_write_seq_table,
	.i2c_write_table_w_microdelay =
		msm_camera_qup_i2c_write_table_w_microdelay,
};

int msm_sensor_platform_probe(struct platform_device *pdev, void *data)
{
	int rc = 0;
	struct msm_sensor_ctrl_t *s_ctrl =
		(struct msm_sensor_ctrl_t *)data;
	struct msm_camera_cci_client *cci_client = NULL;
	uint32_t session_id;
	s_ctrl->pdev = pdev;
	s_ctrl->dev = &pdev->dev;
	CDBG("%s called data %p\n", __func__, data);
	CDBG("%s pdev name %s\n", __func__, pdev->id_entry->name);

	if (pdev->dev.of_node) {
		rc = msm_sensor_get_dt_data(pdev->dev.of_node, s_ctrl);
		if (rc < 0) {
			pr_err("%s failed line %d\n", __func__, __LINE__);
			return rc;
		}
	}
	s_ctrl->sensordata->power_info.dev = &pdev->dev;
	s_ctrl->sensor_device_type = MSM_CAMERA_PLATFORM_DEVICE;
	s_ctrl->sensor_i2c_client->cci_client = kzalloc(sizeof(
		struct msm_camera_cci_client), GFP_KERNEL);
	if (!s_ctrl->sensor_i2c_client->cci_client) {
		pr_err("%s failed line %d\n", __func__, __LINE__);
		rc = -ENOMEM;
		return rc;
	}
	/* TODO: get CCI subdev */
	cci_client = s_ctrl->sensor_i2c_client->cci_client;
	cci_client->cci_subdev = msm_cci_get_subdev();
	cci_client->cci_i2c_master = s_ctrl->cci_i2c_master;
	cci_client->sid =
		s_ctrl->sensordata->slave_info->sensor_slave_addr >> 1;
	cci_client->retries = 3;
	cci_client->id_map = 0;
	if (!s_ctrl->func_tbl)
		s_ctrl->func_tbl = &msm_sensor_func_tbl;
	if (!s_ctrl->sensor_i2c_client->i2c_func_tbl)
		s_ctrl->sensor_i2c_client->i2c_func_tbl =
			&msm_sensor_cci_func_tbl;
	if (!s_ctrl->sensor_v4l2_subdev_ops)
		s_ctrl->sensor_v4l2_subdev_ops = &msm_sensor_subdev_ops;
	
	s_ctrl->sensordata->power_info.clk_info =
		kzalloc(sizeof(cam_8974_clk_info), GFP_KERNEL);
	if (!s_ctrl->sensordata->power_info.clk_info) {
		pr_err("%s:%d failed nomem\n", __func__, __LINE__);
		kfree(cci_client);
		return -ENOMEM;
	}
	memcpy(s_ctrl->sensordata->power_info.clk_info, cam_8974_clk_info,
		sizeof(cam_8974_clk_info));
	s_ctrl->sensordata->power_info.clk_info_size =
		ARRAY_SIZE(cam_8974_clk_info);
#if defined(CONFIG_MACH_KS01SKT) || defined(CONFIG_MACH_KS01KTT)\
		|| defined(CONFIG_MACH_KS01LGT)
	rc = s_ctrl->func_tbl->sensor_power_up(s_ctrl,
		&s_ctrl->sensordata->power_info,
		s_ctrl->sensor_i2c_client,
		s_ctrl->sensordata->slave_info,
		s_ctrl->sensordata->sensor_name);
	if (rc < 0) {
		pr_err("%s %s power up failed\n", __func__,
			s_ctrl->sensordata->sensor_name);
		kfree(cci_client);
		kfree(s_ctrl->sensordata->power_info.clk_info);
		cci_client = NULL;
		return rc;
	}
#endif

#if defined(CONFIG_MACH_KS01SKT) || defined(CONFIG_MACH_KS01KTT)\
	|| defined(CONFIG_MACH_KS01LGT)

/*IMX135 FIRMWARE VERSION CHECK*/
	rc = s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_read(
			s_ctrl->sensor_i2c_client,
			IMX135_FW_ADDRESS,
			&back_cam_fw_version, MSM_CAMERA_I2C_BYTE_DATA);
	if (rc < 0) {
		printk("%s: read failed\n", __func__);
		kfree(cci_client);
		cci_client = NULL;
		return rc;
	}
	printk("%s: back_cam_fw_version: %x \n", __func__, back_cam_fw_version);
#endif

	v4l2_subdev_init(&s_ctrl->msm_sd.sd,
		s_ctrl->sensor_v4l2_subdev_ops);
	snprintf(s_ctrl->msm_sd.sd.name,
		sizeof(s_ctrl->msm_sd.sd.name), "%s",
		s_ctrl->sensordata->sensor_name);
	v4l2_set_subdevdata(&s_ctrl->msm_sd.sd, pdev);
	s_ctrl->msm_sd.sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	media_entity_init(&s_ctrl->msm_sd.sd.entity, 0, NULL, 0);
	s_ctrl->msm_sd.sd.entity.type = MEDIA_ENT_T_V4L2_SUBDEV;
	s_ctrl->msm_sd.sd.entity.group_id = MSM_CAMERA_SUBDEV_SENSOR;
	s_ctrl->msm_sd.sd.entity.name =
		s_ctrl->msm_sd.sd.name;

	rc = camera_init_v4l2(&s_ctrl->pdev->dev, &session_id);
	if(rc < 0) {
		pr_err("%s camera_init_v4l2 call failed!\n", __func__);
		return rc;
	}

	s_ctrl->sensordata->sensor_info->session_id = session_id;
	s_ctrl->msm_sd.close_seq = MSM_SD_CLOSE_2ND_CATEGORY | 0x3;
	msm_sd_register(&s_ctrl->msm_sd);

#if defined(CONFIG_MACH_KS01SKT) || defined(CONFIG_MACH_KS01KTT)\
	|| defined(CONFIG_MACH_KS01LGT)
	s_ctrl->func_tbl->sensor_power_down(
		s_ctrl,
		&s_ctrl->sensordata->power_info,
		s_ctrl->sensor_device_type,
		s_ctrl->sensor_i2c_client);
#endif
	pr_warn("%s rc %d session_id %d\n", __func__, rc, session_id);
	pr_warn("%s %s probe succeeded\n", __func__,
		s_ctrl->sensordata->sensor_name);

	return rc;
}

int msm_sensor_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id, struct msm_sensor_ctrl_t *s_ctrl)
{
	int rc = 0;
#if defined(CONFIG_MACH_HLITE_CHN_SGLTE)
	s_ctrl = (struct msm_sensor_ctrl_t *)(id->driver_data);
	s_ctrl->sensor_device_type = MSM_CAMERA_I2C_DEVICE;
	if (!s_ctrl->sensor_i2c_client->i2c_func_tbl)
		s_ctrl->sensor_i2c_client->i2c_func_tbl =
			&msm_sensor_qup_func_tbl;
	if (s_ctrl->sensor_i2c_client != NULL) {
		s_ctrl->sensor_i2c_client->client = client;
	}
	s_ctrl->sensor_i2c_client->client->addr = 0x6A;
#else
	uint32_t session_id;
	CDBG("%s %s_i2c_probe called\n", __func__, client->name);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s %s i2c_check_functionality failed\n",
			__func__, client->name);
		rc = -EFAULT;
		return rc;
	}

	if (!client->dev.of_node) {
		CDBG("msm_sensor_i2c_probe: of_node is NULL");
	s_ctrl = (struct msm_sensor_ctrl_t *)(id->driver_data);
	if (!s_ctrl) {
		pr_err("%s:%d sensor ctrl structure NULL\n", __func__,
			__LINE__);
		return -EINVAL;
		}
		s_ctrl->sensordata = client->dev.platform_data;
	} else {
		CDBG("msm_sensor_i2c_probe: of_node exisists");
		rc = msm_sensor_get_dt_data(client->dev.of_node, s_ctrl);
		if (rc < 0) {
			pr_err("%s failed line %d\n", __func__, __LINE__);
			return rc;
		}
	}

	s_ctrl->sensor_device_type = MSM_CAMERA_I2C_DEVICE;
	s_ctrl->sensordata = client->dev.platform_data;
	if (s_ctrl->sensordata == NULL) {
		pr_err("%s %s NULL sensor data\n", __func__, client->name);
		return -EFAULT;
	}

	if (s_ctrl->sensor_i2c_client != NULL) {
		s_ctrl->sensor_i2c_client->client = client;
		s_ctrl->sensordata->power_info.dev = &client->dev;
		if (s_ctrl->sensordata->slave_info->sensor_slave_addr)
			s_ctrl->sensor_i2c_client->client->addr =
				s_ctrl->sensordata->slave_info->
				sensor_slave_addr;
	} else {
		pr_err("%s %s sensor_i2c_client NULL\n",
			__func__, client->name);
		rc = -EFAULT;
		return rc;
	}

	if (!s_ctrl->func_tbl)
		s_ctrl->func_tbl = &msm_sensor_func_tbl;
	if (!s_ctrl->sensor_i2c_client->i2c_func_tbl)
		s_ctrl->sensor_i2c_client->i2c_func_tbl =
			&msm_sensor_qup_func_tbl;
	if (!s_ctrl->sensor_v4l2_subdev_ops)
		s_ctrl->sensor_v4l2_subdev_ops = &msm_sensor_subdev_ops;

	if (!client->dev.of_node) {
		s_ctrl->sensordata->power_info.clk_info =
			kzalloc(sizeof(cam_8960_clk_info), GFP_KERNEL);
		if (!s_ctrl->sensordata->power_info.clk_info) {
			pr_err("%s:%d failed nomem\n", __func__, __LINE__);
			return -ENOMEM;
		}
		memcpy(s_ctrl->sensordata->power_info.clk_info,
			cam_8960_clk_info, sizeof(cam_8960_clk_info));
		s_ctrl->sensordata->power_info.clk_info_size =
			ARRAY_SIZE(cam_8960_clk_info);
	}

	rc = s_ctrl->func_tbl->sensor_power_up(s_ctrl,
		&s_ctrl->sensordata->power_info,
		s_ctrl->sensor_i2c_client,
		s_ctrl->sensordata->slave_info,
		s_ctrl->sensordata->sensor_name);
	if (rc < 0) {
		pr_err("%s %s power up failed\n", __func__, client->name);
		kfree(s_ctrl->sensordata->power_info.clk_info);
		return rc;
	}

	CDBG("%s %s probe succeeded\n", __func__, client->name);
	snprintf(s_ctrl->msm_sd.sd.name,
		sizeof(s_ctrl->msm_sd.sd.name), "%s", id->name);
	v4l2_i2c_subdev_init(&s_ctrl->msm_sd.sd, client,
		s_ctrl->sensor_v4l2_subdev_ops);
	v4l2_set_subdevdata(&s_ctrl->msm_sd.sd, client);
	s_ctrl->msm_sd.sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	media_entity_init(&s_ctrl->msm_sd.sd.entity, 0, NULL, 0);
	s_ctrl->msm_sd.sd.entity.type = MEDIA_ENT_T_V4L2_SUBDEV;
	s_ctrl->msm_sd.sd.entity.group_id = MSM_CAMERA_SUBDEV_SENSOR;
	s_ctrl->msm_sd.sd.entity.name =
		s_ctrl->msm_sd.sd.name;

	rc = camera_init_v4l2(&s_ctrl->sensor_i2c_client->client->dev,
		&session_id);
	if(rc < 0) {
		pr_err("%s camera_init_v4l2 call failed!\n", __func__);
		s_ctrl->func_tbl->sensor_power_down(
							s_ctrl,
						    &s_ctrl->sensordata->power_info,
						    s_ctrl->sensor_device_type,
						    s_ctrl->sensor_i2c_client);
		return rc;
	}
	CDBG("%s rc %d session_id %d\n", __func__, rc, session_id);
	s_ctrl->sensordata->sensor_info->session_id = session_id;
	s_ctrl->msm_sd.close_seq = MSM_SD_CLOSE_2ND_CATEGORY | 0x3;
	msm_sd_register(&s_ctrl->msm_sd);
	CDBG("%s:%d\n", __func__, __LINE__);

	s_ctrl->func_tbl->sensor_power_down(
		s_ctrl,
		&s_ctrl->sensordata->power_info,
		s_ctrl->sensor_device_type,
		s_ctrl->sensor_i2c_client);
#endif
	return rc;
}
int32_t msm_sensor_init_default_params(struct msm_sensor_ctrl_t *s_ctrl)
{
	int32_t                       rc = -ENOMEM;
	struct msm_camera_cci_client *cci_client = NULL;
	struct msm_cam_clk_info      *clk_info = NULL;
	if (!s_ctrl) {
		pr_err("%s:%d failed: invalid params s_ctrl %p\n", __func__,
			__LINE__, s_ctrl);
		return -EINVAL;
	}
	s_ctrl->sensor_device_type = MSM_CAMERA_PLATFORM_DEVICE;
	if (!s_ctrl->sensor_i2c_client) {
		pr_err("%s:%d failed: invalid params sensor_i2c_client %p\n",
			__func__, __LINE__, s_ctrl->sensor_i2c_client);
		return -EINVAL;
	}
	s_ctrl->sensor_i2c_client->cci_client = kzalloc(sizeof(
		struct msm_camera_cci_client), GFP_KERNEL);
	if (!s_ctrl->sensor_i2c_client->cci_client) {
		pr_err("%s:%d failed: no memory cci_client %p\n", __func__,
			__LINE__, s_ctrl->sensor_i2c_client->cci_client);
		return -ENOMEM;
	}
	cci_client = s_ctrl->sensor_i2c_client->cci_client;
	cci_client->cci_subdev = msm_cci_get_subdev();
	if (!s_ctrl->func_tbl)
		s_ctrl->func_tbl = &msm_sensor_func_tbl;
	if (!s_ctrl->sensor_i2c_client->i2c_func_tbl)
		s_ctrl->sensor_i2c_client->i2c_func_tbl =
			&msm_sensor_cci_func_tbl;
	if (!s_ctrl->sensor_v4l2_subdev_ops)
		s_ctrl->sensor_v4l2_subdev_ops = &msm_sensor_subdev_ops;
	clk_info = kzalloc(sizeof(cam_8974_clk_info), GFP_KERNEL);
	if (!clk_info) {
		pr_err("%s:%d failed no memory clk_info %p\n", __func__,
			__LINE__, clk_info);
		rc = -ENOMEM;
		goto FREE_CCI_CLIENT;
	}
	memcpy(clk_info, cam_8974_clk_info, sizeof(cam_8974_clk_info));
	s_ctrl->sensordata->power_info.clk_info = clk_info;
	s_ctrl->sensordata->power_info.clk_info_size =
		ARRAY_SIZE(cam_8974_clk_info);
	return 0;
FREE_CCI_CLIENT:
	kfree(cci_client);
	return rc;
}
