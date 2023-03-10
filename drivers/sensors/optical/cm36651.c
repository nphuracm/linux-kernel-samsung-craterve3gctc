/*
 * driver/sensor/cm36651.c
 * Copyright (c) 2011 SAMSUNG ELECTRONICS
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/i2c.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <mach/hardware.h>
#include <linux/wakelock.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/i2c/cm36651.h>
#include <linux/regulator/consumer.h>
#include <linux/sensors_core.h>
#include <linux/of_gpio.h>
#include <linux/module.h>

/* Intelligent Cancelation*/
#define CM36651_CANCELATION
#ifdef CM36651_CANCELATION
#define CANCELATION_FILE_PATH	"/efs/prox_cal"
#endif

#define RGB_VENDOR	"CAPELLA"
#define RGB_CHIP_ID		"CM36651"

#define I2C_M_WR 0		/* for i2c Write */
#define I2c_M_RD 1		/* for i2c Read */

#define REL_RED		REL_X
#define REL_GREEN	REL_Y
#define REL_BLUE		REL_Z
#define REL_WHITE	REL_MISC

/* slave addresses */
#define CM36651_ALS 0x30 /* 7bits : 0x18 */
#define CM36651_PS	0x32 /* 7bits : 0x19 */

/* register addresses */
/* Ambient light sensor */
#define CS_CONF1	0x00
#define CS_CONF2	0x01
#define ALS_WH_M	0x02
#define ALS_WH_L	0x03
#define ALS_WL_M	0x04
#define ALS_WL_L	0x05
#define CS_CONF3	0x06

#define RED			0x00
#define GREEN		0x01
#define BLUE		0x02
#define WHITE		0x03

/* Proximity sensor */
#define PS_CONF1	0x00
#define PS_THD		0x01
#define PS_CANC		0x02
#define PS_CONF2	0x03

#define ALS_REG_NUM		3
#define PS_REG_NUM		4
#define PROX_READ_NUM	40

#if defined(CONFIG_LCD_CONNECTION_CHECK)
#if defined(CONFIG_MACH_S3VE_CHN_CTC)

extern int is_lcd_attached(void);
#endif
#endif
enum {
	LIGHT_ENABLED = BIT(0),
	PROXIMITY_ENABLED = BIT(1),
};

/* register settings */
static u8 als_reg_setting[ALS_REG_NUM][2] = {
	{0x00, 0x04},	/* CS_CONF1 */
	{0x01, 0x08},	/* CS_CONF2 */
	{0x06, 0x00}	/* CS_CONF3 */
};

static u8 ps_reg_setting[PS_REG_NUM][2] = {
	{0x00, 0x3C},	/* PS_CONF1 */
	{0x01, 0x07},	/* PS_THD */
	{0x02, 0x00},	/* PS_CANC */
	{0x03, 0x13},	/* PS_CONF2 */
};

 /* driver data */
struct cm36651_data {
	struct i2c_client *i2c_client;
	struct wake_lock prx_wake_lock;
	struct input_dev *proximity_input_dev;
	struct input_dev *light_input_dev;
	struct cm36651_platform_data *pdata;
	struct mutex power_lock;
	struct mutex read_lock;
	struct hrtimer light_timer;
	struct hrtimer prox_timer;
	struct workqueue_struct *light_wq;
	struct workqueue_struct *prox_wq;
	struct work_struct work_light;
	struct work_struct work_prox;
	struct device *proximity_dev;
	struct device *light_dev;
	ktime_t light_poll_delay;
	ktime_t prox_poll_delay;
	int irq;
	u8 power_state;
#ifdef CM36651_CANCELATION
	u8 prox_cal;
#endif
	u32  vdd_en ;

	struct regulator *vdd_2p85;
	struct regulator *vreg_1p8;

	int avg[3];
};

static int cm36651_i2c_read_byte(struct cm36651_data *cm36651,
	u8 addr, u8 *val)
{
	int err = 0;
	int retry = 3;
	struct i2c_msg msg[1];
	struct i2c_client *client = cm36651->i2c_client;

	if ((client == NULL) || (!client->adapter))
		return -ENODEV;

	/* send slave address & command */
	msg->addr = addr >> 1;
	msg->flags = I2C_M_RD;
	msg->len = 1;
	msg->buf = val;

	while (retry--) {
		err = i2c_transfer(client->adapter, msg, 1);
		if (err >= 0)
			return err;
	}
	pr_err("%s: i2c read failed at addr 0x%x: %d\n", __func__, addr, err);
	return err;
}

static int cm36651_i2c_read_word(struct cm36651_data *cm36651,
	u8 addr, u8 command, u16 *val)
{
	int err = 0;
	int retry = 3;
	struct i2c_client *client = cm36651->i2c_client;
	struct i2c_msg msg[2];
	unsigned char data[2] = {0,};
	u16 value = 0;

	if ((client == NULL) || (!client->adapter))
		return -ENODEV;

	while (retry--) {
		/* send slave address & command */
		msg[0].addr = addr>>1;
		msg[0].flags = I2C_M_WR;
		msg[0].len = 1;
		msg[0].buf = &command;

		/* read word data */
		msg[1].addr = addr>>1;
		msg[1].flags = I2C_M_RD;
		msg[1].len = 2;
		msg[1].buf = data;

		err = i2c_transfer(client->adapter, msg, 2);

		if (err >= 0) {
			value = (u16)data[1];
			*val = (value << 8) | (u16)data[0];
			return err;
		}
	}
	pr_err("%s, i2c transfer error ret=%d\n", __func__, err);
	return err;
}

static int cm36651_i2c_write_byte(struct cm36651_data *cm36651,
	u8 addr, u8 command, u8 val)
{
	int err = 0;
	struct i2c_client *client = cm36651->i2c_client;
	struct i2c_msg msg[1];
	unsigned char data[2];
	int retry = 3;

	if ((client == NULL) || (!client->adapter))
		return -ENODEV;

	while (retry--) {
		data[0] = command;
		data[1] = val;

		/* send slave address & command */
		msg->addr = addr>>1;
		msg->flags = I2C_M_WR;
		msg->len = 2;
		msg->buf = data;

		err = i2c_transfer(client->adapter, msg, 1);

		if (err >= 0)
			return 0;
	}
	pr_err("%s, i2c transfer error(%d)\n", __func__, err);
	return err;
}

static void cm36651_light_enable(struct cm36651_data *cm36651)
{
	/* enable setting */
	cm36651_i2c_write_byte(cm36651, CM36651_ALS, CS_CONF1,
		als_reg_setting[0][1]);

	hrtimer_start(&cm36651->light_timer, cm36651->light_poll_delay,
		      HRTIMER_MODE_REL);
}

static void cm36651_light_disable(struct cm36651_data *cm36651)
{
	/* disable setting */
	cm36651_i2c_write_byte(cm36651, CM36651_ALS, CS_CONF1,
			       0x01);
	hrtimer_cancel(&cm36651->light_timer);
	cancel_work_sync(&cm36651->work_light);
}

/* sysfs */
static ssize_t cm36651_poll_delay_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct cm36651_data *cm36651 = dev_get_drvdata(dev);
	return sprintf(buf, "%lld\n", ktime_to_ns(cm36651->light_poll_delay));
}

static ssize_t cm36651_poll_delay_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	struct cm36651_data *cm36651 = dev_get_drvdata(dev);
	int64_t new_delay;
	int err;

	err = kstrtoll(buf, 10, &new_delay);
	if (err < 0)
		return err;

	mutex_lock(&cm36651->power_lock);
	if (new_delay != ktime_to_ns(cm36651->light_poll_delay)) {
		cm36651->light_poll_delay = ns_to_ktime(new_delay);
		if (cm36651->power_state & LIGHT_ENABLED) {
			cm36651_light_disable(cm36651);
			cm36651_light_enable(cm36651);
		}
		pr_info("%s, poll_delay = %lld\n", __func__, new_delay);
	}
	mutex_unlock(&cm36651->power_lock);

	return size;
}

static ssize_t light_enable_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	struct cm36651_data *cm36651 = dev_get_drvdata(dev);
	bool new_value;

	if (sysfs_streq(buf, "1")) {
		new_value = true;
	} else if (sysfs_streq(buf, "0")) {
		new_value = false;
	} else {
		pr_err("%s: invalid value %d\n", __func__, *buf);
		return -EINVAL;
	}

	pr_info("%s,new_value=%d\n", __func__, new_value);
	mutex_lock(&cm36651->power_lock);
	if (new_value && !(cm36651->power_state & LIGHT_ENABLED)) {
		cm36651->power_state |= LIGHT_ENABLED;
		cm36651_light_enable(cm36651);
	} else if (!new_value && (cm36651->power_state & LIGHT_ENABLED)) {
		cm36651_light_disable(cm36651);
		cm36651->power_state &= ~LIGHT_ENABLED;
	}
	mutex_unlock(&cm36651->power_lock);
	return size;
}

static ssize_t light_enable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct cm36651_data *cm36651 = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n",
		       (cm36651->power_state & LIGHT_ENABLED) ? 1 : 0);
}

#ifdef CM36651_CANCELATION
static int proximity_open_cancelation(struct cm36651_data *data)
{
	struct file *cancel_filp = NULL;
	int err = 0;
	mm_segment_t old_fs;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	cancel_filp = filp_open(CANCELATION_FILE_PATH, O_RDONLY, 0666);
	if (IS_ERR(cancel_filp)) {
		err = PTR_ERR(cancel_filp);
		if (err != -ENOENT)
			pr_err("%s: Can't open cancelation file\n", __func__);
		set_fs(old_fs);
		return err;
	}

	err = cancel_filp->f_op->read(cancel_filp,
		(char *)&data->prox_cal, sizeof(u8), &cancel_filp->f_pos);
	if (err != sizeof(u8)) {
		pr_err("%s: Can't read the cancel data from file\n", __func__);
		err = -EIO;
	}
	ps_reg_setting[2][1] = data->prox_cal;
	if (ps_reg_setting[2][1] != 0) /*If there is an offset cal data. */
		ps_reg_setting[1][1] = 0x09;

	pr_info("%s: proximity ps_data = %d, thresh = %d\n", __func__,
		data->prox_cal, ps_reg_setting[1][1]);

	filp_close(cancel_filp, current->files);
	set_fs(old_fs);

	return err;
}

static int proximity_store_cancelation(struct device *dev, bool do_calib)
{
	struct cm36651_data *cm36651 = dev_get_drvdata(dev);
	struct file *cancel_filp = NULL;
	mm_segment_t old_fs;
	int err = 0;

	if (do_calib) {
		mutex_lock(&cm36651->read_lock);
		cm36651_i2c_read_byte(cm36651, CM36651_PS, &cm36651->prox_cal);
		mutex_unlock(&cm36651->read_lock);
		ps_reg_setting[1][1] = 0x09;
	} else {
		cm36651->prox_cal = 0;
		ps_reg_setting[1][1] = cm36651->pdata->threshold;
	}
	cm36651_i2c_write_byte(cm36651, CM36651_PS, PS_THD,
		ps_reg_setting[1][1]);
	cm36651_i2c_write_byte(cm36651, CM36651_PS, PS_CANC,
		cm36651->prox_cal);

	if (!do_calib)
		msleep(150);
	pr_info("%s: prox_cal = 0x%x\n", __func__, cm36651->prox_cal);

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	cancel_filp = filp_open(CANCELATION_FILE_PATH,
			O_CREAT | O_TRUNC | O_WRONLY | O_SYNC, 0666);
	if (IS_ERR(cancel_filp)) {
		pr_err("%s: Can't open cancelation file\n", __func__);
		set_fs(old_fs);
		err = PTR_ERR(cancel_filp);
		return err;
	}

	err = cancel_filp->f_op->write(cancel_filp,
		(char *)&cm36651->prox_cal, sizeof(u8), &cancel_filp->f_pos);
	if (err != sizeof(u8)) {
		pr_err("%s: Can't write the cancel data to file\n", __func__);
		err = -EIO;
	}

	filp_close(cancel_filp, current->files);
	set_fs(old_fs);

	return err;
}

static ssize_t proximity_cancel_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t size)
{
	bool do_calib;
	int err;

	if (sysfs_streq(buf, "1")) { /* calibrate cancelation value */
		do_calib = true;
	} else if (sysfs_streq(buf, "0")) { /* reset cancelation value */
		do_calib = false;
	} else {
		pr_debug("%s: invalid value %d\n", __func__, *buf);
		return -EINVAL;
	}

	err = proximity_store_cancelation(dev, do_calib);
	if (err < 0) {
		pr_err("%s: proximity_store_cancelation() failed\n", __func__);
		return err;
	}

	return size;
}

static ssize_t proximity_cancel_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct cm36651_data *cm36651 = dev_get_drvdata(dev);

	return sprintf(buf, "%d,%d\n", cm36651->prox_cal,
		ps_reg_setting[1][1]);
}
#endif

static ssize_t proximity_enable_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t size)
{
	struct cm36651_data *cm36651 = dev_get_drvdata(dev);
	//struct cm36651_platform_data *pdata =cm36651-> i2c_client->dev.platform_data;
	bool new_value;
	int err = 0;

	if (sysfs_streq(buf, "1")) {
		new_value = true;
	} else if (sysfs_streq(buf, "0")) {
		new_value = false;
	} else {
		pr_err("%s: invalid value %d\n", __func__, *buf);
		return -EINVAL;
	}

	pr_info("%s,new_value=%d, prox_cal %d\n", __func__,
		new_value, cm36651->prox_cal);
	mutex_lock(&cm36651->power_lock);
	if (new_value && !(cm36651->power_state & PROXIMITY_ENABLED)) {
		u8 val = 1;
		int i;
		gpio_direction_output(cm36651->pdata->vdd_en, 1);
		msleep(20);
#ifdef CM36651_CANCELATION
		/* open cancelation data */
		err = proximity_open_cancelation(cm36651);
		if (err < 0 && err != -ENOENT)
			pr_err("%s: proximity_open_cancelation() failed\n",
				__func__);
#endif
		cm36651->power_state |= PROXIMITY_ENABLED;
		/* enable settings */

		for (i = 0; i < 4; i++) {
			cm36651_i2c_write_byte(cm36651, CM36651_PS,
				ps_reg_setting[i][0], ps_reg_setting[i][1]);
		}

		val = gpio_get_value_cansleep(cm36651->pdata->p_out);
		/* 0 is close, 1 is far */
		input_report_abs(cm36651->proximity_input_dev,
			ABS_DISTANCE, val);
		input_sync(cm36651->proximity_input_dev);

		enable_irq(cm36651->irq);
		enable_irq_wake(cm36651->irq);
	} else if (!new_value && (cm36651->power_state & PROXIMITY_ENABLED)) {
		cm36651->power_state &= ~PROXIMITY_ENABLED;
		disable_irq_wake(cm36651->irq);
		disable_irq(cm36651->irq);
		/* disable settings */
		cm36651_i2c_write_byte(cm36651, CM36651_PS, PS_CONF1,
				       0x01);
		gpio_direction_output(cm36651->pdata->vdd_en, 0);
	}
	mutex_unlock(&cm36651->power_lock);
	return size;
}

static ssize_t proximity_enable_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct cm36651_data *cm36651 = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n",
		       (cm36651->power_state & PROXIMITY_ENABLED) ? 1 : 0);
}

static DEVICE_ATTR(poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
		   cm36651_poll_delay_show, cm36651_poll_delay_store);

static struct device_attribute dev_attr_light_enable =
__ATTR(enable, S_IRUGO | S_IWUSR | S_IWGRP,
	light_enable_show, light_enable_store);

static struct device_attribute dev_attr_proximity_enable =
__ATTR(enable, S_IRUGO | S_IWUSR | S_IWGRP,
	proximity_enable_show, proximity_enable_store);

static struct attribute *light_sysfs_attrs[] = {
	&dev_attr_light_enable.attr,
	&dev_attr_poll_delay.attr,
	NULL
};

static struct attribute_group light_attribute_group = {
	.attrs = light_sysfs_attrs,
};

static struct attribute *proximity_sysfs_attrs[] = {
	&dev_attr_proximity_enable.attr,
	NULL
};

static struct attribute_group proximity_attribute_group = {
	.attrs = proximity_sysfs_attrs,
};

static ssize_t proximity_avg_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct cm36651_data *cm36651 = dev_get_drvdata(dev);

	return sprintf(buf, "%d,%d,%d\n", cm36651->avg[0],
		cm36651->avg[1], cm36651->avg[2]);
}

static ssize_t proximity_avg_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct cm36651_data *cm36651 = dev_get_drvdata(dev);
	//struct cm36651_platform_data *pdata =cm36651-> i2c_client->dev.platform_data;
	bool new_value = false;

	if (sysfs_streq(buf, "1"))
		new_value = true;
	else if (sysfs_streq(buf, "0"))
		new_value = false;
	else {
		pr_err("%s, invalid value %d\n", __func__, *buf);
		return -EINVAL;
	}

	mutex_lock(&cm36651->power_lock);
	if (new_value) {
		if (!(cm36651->power_state & PROXIMITY_ENABLED)) {
			gpio_direction_output(cm36651->pdata->vdd_en, 1);
			msleep(20);
			cm36651_i2c_write_byte(cm36651, CM36651_PS, PS_CONF1,
			ps_reg_setting[0][1]);
		}
		hrtimer_start(&cm36651->prox_timer, cm36651->prox_poll_delay,
			HRTIMER_MODE_REL);
	} else if (!new_value) {
		hrtimer_cancel(&cm36651->prox_timer);
		cancel_work_sync(&cm36651->work_prox);
		if (!(cm36651->power_state & PROXIMITY_ENABLED)) {
			cm36651_i2c_write_byte(cm36651, CM36651_PS, PS_CONF1,
				0x01);
			gpio_direction_output(cm36651->pdata->vdd_en, 0);
		}
	}
	mutex_unlock(&cm36651->power_lock);

	return size;
}

static ssize_t proximity_state_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct cm36651_data *cm36651 = dev_get_drvdata(dev);
	//struct cm36651_platform_data *pdata =cm36651-> i2c_client->dev.platform_data;
	u8 proximity_value = 0;

	mutex_lock(&cm36651->power_lock);
	if (!(cm36651->power_state & PROXIMITY_ENABLED)) {
		gpio_direction_output(cm36651->pdata->vdd_en, 1);
		cm36651_i2c_write_byte(cm36651, CM36651_PS, PS_CONF1,
			ps_reg_setting[0][1]);
	msleep(20);
	}

	mutex_lock(&cm36651->read_lock);
	cm36651_i2c_read_byte(cm36651, CM36651_PS, &proximity_value);
	mutex_unlock(&cm36651->read_lock);

	if (!(cm36651->power_state & PROXIMITY_ENABLED)) {
		cm36651_i2c_write_byte(cm36651, CM36651_PS, PS_CONF1, 0x01);
		gpio_direction_output(cm36651->pdata->vdd_en, 0);
	}
	mutex_unlock(&cm36651->power_lock);

	return sprintf(buf, "%d\n", proximity_value);
}

static ssize_t proximity_thresh_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "prox_threshold = %d\n", ps_reg_setting[1][1]);
}

static ssize_t proximity_thresh_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct cm36651_data *cm36651 = dev_get_drvdata(dev);
	u8 thresh_value = 0x09;
	int err = 0;

	err = kstrtou8(buf, 10, &thresh_value);
	if (err < 0)
		pr_err("%s, kstrtoint failed.", __func__);

	ps_reg_setting[1][1] = thresh_value;
	err = cm36651_i2c_write_byte(cm36651, CM36651_PS,
			PS_THD, ps_reg_setting[1][1]);
	if (err < 0) {
		pr_err("%s: cm36651_ps_reg is failed. %d\n", __func__,
		       err);
		return err;
	}
	pr_info("%s, new threshold = 0x%x\n",
		__func__, ps_reg_setting[1][1]);
	msleep(150);

	return size;
}


static ssize_t lightsensor_lux_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct cm36651_data *cm36651 = dev_get_drvdata(dev);
	u16 val_red = 0, val_green = 0, val_blue = 0, val_white = 0;

	mutex_lock(&cm36651->power_lock);
	if (!(cm36651->power_state & LIGHT_ENABLED))
		cm36651_light_enable(cm36651);

	mutex_lock(&cm36651->read_lock);
	cm36651_i2c_read_word(cm36651, CM36651_ALS, RED, &val_red);
	cm36651_i2c_read_word(cm36651, CM36651_ALS, GREEN, &val_green);
	cm36651_i2c_read_word(cm36651, CM36651_ALS, BLUE, &val_blue);
	cm36651_i2c_read_word(cm36651, CM36651_ALS, WHITE, &val_white);
	mutex_unlock(&cm36651->read_lock);

	if (!(cm36651->power_state & LIGHT_ENABLED))
		cm36651_light_disable(cm36651);

	mutex_unlock(&cm36651->power_lock);

	return sprintf(buf, "%d,%d,%d,%d\n",
		(int)val_red+1, (int)val_green+1, (int)val_blue+1,
		(int)val_white+1);
}

static ssize_t cm36651_vendor_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", RGB_VENDOR);
}

static ssize_t cm36651_name_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", RGB_CHIP_ID);
}

static DEVICE_ATTR(prox_avg, S_IRUGO | S_IWUSR, proximity_avg_show,
	proximity_avg_store);
#ifdef CM36651_CANCELATION
static DEVICE_ATTR(prox_cal, S_IRUGO | S_IWUSR, proximity_cancel_show,
	proximity_cancel_store);
#endif
static DEVICE_ATTR(state, S_IRUGO | S_IWUSR, proximity_state_show, NULL);

static DEVICE_ATTR(prox_thresh, S_IRUGO | S_IWUSR, proximity_thresh_show,
	proximity_thresh_store);

static DEVICE_ATTR(lux, S_IRUGO | S_IWUSR, lightsensor_lux_show, NULL);

static DEVICE_ATTR(vendor, S_IRUGO, cm36651_vendor_show, NULL);

static DEVICE_ATTR(name, S_IRUGO, cm36651_name_show, NULL);

static DEVICE_ATTR(raw_data, S_IRUGO, lightsensor_lux_show, NULL);

/* interrupt happened due to transition/change of near/far proximity state */
static irqreturn_t cm36651_irq_thread_fn(int irq, void *data)
{
	struct cm36651_data *cm36651 = data;
	u8 val = 1;
	u8 ps_data = 0;

	val = gpio_get_value_cansleep(cm36651->pdata->p_out);

	mutex_lock(&cm36651->read_lock);
	cm36651_i2c_read_byte(cm36651, CM36651_PS, &ps_data);
	mutex_unlock(&cm36651->read_lock);


	/* 0 is close, 1 is far */
	input_report_abs(cm36651->proximity_input_dev, ABS_DISTANCE, val);
	input_sync(cm36651->proximity_input_dev);
	wake_lock_timeout(&cm36651->prx_wake_lock, 3 * HZ);
	pr_info("%s: val = %d (close:0, far:1), ps_data = %d\n", __func__,
		val, ps_data);

	return IRQ_HANDLED;
}

static int cm36651_setup_reg(struct cm36651_data *cm36651)
{
	int err = 0, i = 0;
	u8 tmp = 0;

	/* ALS initialization */
	for (i = 0; i < ALS_REG_NUM; i++) {
		err = cm36651_i2c_write_byte(cm36651,
					     CM36651_ALS, als_reg_setting[i][0],
					     als_reg_setting[i][1]);
		if (err < 0) {
			pr_err("%s: cm36651_als_reg is failed. %d\n", __func__,
			       err);
			return err;
		}
	}

	/* PS initialization */
	for (i = 0; i < PS_REG_NUM; i++) {
		err = cm36651_i2c_write_byte(cm36651, CM36651_PS,
			ps_reg_setting[i][0], ps_reg_setting[i][1]);
		if (err < 0) {
			pr_err("%s: cm36651_ps_reg is failed. %d\n", __func__,
			       err);
			return err;
		}
	}

	/* printing the inital proximity value with no contact */
	msleep(50);
	mutex_lock(&cm36651->read_lock);
	err = cm36651_i2c_read_byte(cm36651, CM36651_PS, &tmp);
	mutex_unlock(&cm36651->read_lock);
	if (err < 0) {
		pr_err("%s: read ps_data failed\n", __func__);
		err = -EIO;
	}
	pr_err("%s: initial proximity value = %d\n", __func__, tmp);

	/* turn off */
	cm36651_i2c_write_byte(cm36651,   CM36651_ALS, CS_CONF1, 0x01);
	cm36651_i2c_write_byte(cm36651,   CM36651_ALS, PS_CONF1, 0x01);

	pr_info("%s is success.", __func__);
	return err;
}

static int cm36651_setup_irq(struct cm36651_data *cm36651)
{
	int rc = -EIO;
	struct cm36651_platform_data *pdata = cm36651->pdata;
	//struct cm36651_platform_data *pdata = cm36651->i2c_client->dev.platform_data;

	rc = gpio_request(pdata->p_out, "gpio_proximity_out");
	if (rc < 0) {
		pr_err("%s: gpio %d request failed (%d)\n",
		       __func__, pdata->p_out, rc);
		goto done;
	}

	rc = gpio_direction_input(pdata->p_out);
	if (rc < 0) {
		pr_err("%s: failed to set gpio %d as input (%d)\n",
		       __func__, pdata->p_out, rc);
		goto err_gpio_direction_input;
	}

	cm36651->irq = gpio_to_irq(pdata->p_out);
	rc = request_threaded_irq(cm36651->irq, NULL,
				  cm36651_irq_thread_fn,
				  IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				  "proximity_int", cm36651);
	if (rc < 0) {
		pr_err("%s: request_irq(%d) failed for gpio %d (%d)\n",
		       __func__, cm36651->irq, pdata->p_out, rc);
		goto err_request_irq;
	}

	/* start with interrupts disabled */
	disable_irq(cm36651->irq);

	goto done;

err_request_irq:
err_gpio_direction_input:
	gpio_free(pdata->p_out);
done:
	return rc;
}

/*
 * This function is for light sensor.  It operates every a few seconds.
 * It asks for work to be done on a thread because i2c needs a thread
 * context (slow and blocking) and then reschedules the timer to run again.
 */
static enum hrtimer_restart cm36651_light_timer_func(struct hrtimer *timer)
{
	struct cm36651_data *cm36651
	    = container_of(timer, struct cm36651_data, light_timer);
	queue_work(cm36651->light_wq, &cm36651->work_light);
	hrtimer_forward_now(&cm36651->light_timer, cm36651->light_poll_delay);
	return HRTIMER_RESTART;
}

static void cm36651_work_func_light(struct work_struct *work)
{
	u16 val_red = 0, val_green = 0, val_blue = 0, val_white = 0;
	struct cm36651_data *cm36651 = container_of(work, struct cm36651_data,
						    work_light);

	mutex_lock(&cm36651->read_lock);
	cm36651_i2c_read_word(cm36651, CM36651_ALS, RED, &val_red);
	cm36651_i2c_read_word(cm36651, CM36651_ALS, GREEN, &val_green);
	cm36651_i2c_read_word(cm36651, CM36651_ALS, BLUE, &val_blue);
	cm36651_i2c_read_word(cm36651, CM36651_ALS, WHITE, &val_white);
	mutex_unlock(&cm36651->read_lock);

	input_report_rel(cm36651->light_input_dev, REL_RED, (int)val_red+1);
	input_report_rel(cm36651->light_input_dev, REL_GREEN, (int)val_green+1);
	input_report_rel(cm36651->light_input_dev, REL_BLUE, (int)val_blue+1);
	input_report_rel(cm36651->light_input_dev, REL_WHITE, (int)val_white+1);
	input_sync(cm36651->light_input_dev);
#ifdef CM36651_DEBUG
	pr_info("%s, red = %d, green = %d, blue = %d, white = %d\n",
		__func__, val_red+1, val_green+1, val_blue+1, val_white+1);
#endif
}

static void proxsensor_get_avg_val(struct cm36651_data *cm36651)
{
	int min = 0, max = 0, avg = 0;
	int i;
	u8 ps_data = 0;

	for (i = 0; i < PROX_READ_NUM; i++) {
		msleep(40);
		cm36651_i2c_read_byte(cm36651, CM36651_PS, &ps_data);
		avg += ps_data;

		if (!i)
			min = ps_data;
		else if (ps_data < min)
			min = ps_data;

		if (ps_data > max)
			max = ps_data;
	}
	avg /= PROX_READ_NUM;

	cm36651->avg[0] = min;
	cm36651->avg[1] = avg;
	cm36651->avg[2] = max;
}

static void cm36651_work_func_prox(struct work_struct *work)
{
	struct cm36651_data *cm36651 = container_of(work, struct cm36651_data,
						  work_prox);
	proxsensor_get_avg_val(cm36651);
}

static enum hrtimer_restart cm36651_prox_timer_func(struct hrtimer *timer)
{
	struct cm36651_data *cm36651
			= container_of(timer, struct cm36651_data, prox_timer);
	queue_work(cm36651->prox_wq, &cm36651->work_prox);
	hrtimer_forward_now(&cm36651->prox_timer, cm36651->prox_poll_delay);
	return HRTIMER_RESTART;
}

#ifdef CONFIG_OF
/*device tree parsing */

static int cm36651_parse_dt(struct device *dev, struct cm36651_platform_data *pdata)
{
	//u32 version_flags;
	struct device_node *np = dev->of_node;
	pdata->vdd_en= of_get_named_gpio(np, "cm36651,vdd_en-gpio", 0);
	pdata->p_out= of_get_named_gpio(np, "cm36651,p_out-gpio", 0);
	pdata->prox_cal_path = of_get_property(np, "cm36651,prox_cal_path",NULL);
//	pdata->vdd_2p85 = of_get_property(np, "cm36651,vdd_2p85",NULL);
	pr_err("cm36651_parse_dt complete, vdd_en:%d p_out:%d %s\n",pdata->vdd_en, pdata->p_out, pdata->prox_cal_path);
	return 0;
}
#else
static int cm36651_parse_dt(struct device *dev,
		struct cm36651_data *pdata)
	{
		return -ENODEV;
	}
#endif

static void cm36651_request_gpio(struct cm36651_platform_data *pdata)
{
	int ret;
	ret = gpio_request(pdata->vdd_en, "vdd_en");
	if(ret)
		pr_err("[cm36651]%s: gpio request fail\n",__func__);
	gpio_tlmm_config(GPIO_CFG(pdata->vdd_en, 0,
			GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA), 1);
	if (ret) {
		pr_err("[cm36651]%s: unable to request vdd_en [%d]\n",
				__func__, pdata->vdd_en);
		return;
	}
	ret = gpio_direction_output(pdata->vdd_en, 1);
	if (ret)
		pr_err("[cm36651]%s: unable to set_direction for vdd_en [%d]\n",__func__, pdata->vdd_en);
}

static void sensor_power_on_vdd(struct cm36651_data *info, int onoff)
{
	int ret;
	if (info->vdd_2p85 == NULL) {
		info->vdd_2p85 =regulator_get(&info->i2c_client->dev, "8226_l19");
		if (IS_ERR(info->vdd_2p85)){
			pr_err("%s: regulator_get failed for 8226_l19\n", __func__);
			return ;
		}
		ret = regulator_set_voltage(info->vdd_2p85, 2850000, 2850000);
		if (ret)
			pr_err("%s: error vsensor_2p85 setting voltage ret=%d\n",__func__, ret);
	}
	if (info->vreg_1p8 == NULL) {
		info->vreg_1p8 =regulator_get(&info->i2c_client->dev, "8226_l6");
		if (IS_ERR(info->vreg_1p8)){
			pr_err("%s: regulator_get failed for 8226_l6\n", __func__);
			return ;
		}
		ret = regulator_set_voltage(info->vreg_1p8, 1800000, 1800000);
		if (ret)
			pr_err("%s: error vreg_2p8 setting voltage ret=%d\n",__func__, ret);
	}
	if (onoff == 1) {
		ret = regulator_enable(info->vdd_2p85);
		if (ret)
			pr_err("%s: error enablinig regulator info->vdd_2p85\n", __func__);

		ret = regulator_enable(info->vreg_1p8);
		if (ret)
			pr_err("%s: error enablinig regulator info->vreg_1p8\n", __func__);
	}
    else if (onoff == 0) {
		if (regulator_is_enabled(info->vdd_2p85)) {
			ret = regulator_disable(info->vdd_2p85);
			if (ret)
				pr_err("%s: error vdd_2p85 disabling regulator\n",__func__);
		}
		if (regulator_is_enabled(info->vreg_1p8)) {
			ret = regulator_disable(info->vreg_1p8);
			if (ret)
				pr_err("%s: error vreg_1p8 disabling regulator\n",__func__);
		}
	}
	msleep(30);
	return;
}

static int cm36651_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	int ret = -ENODEV;
	int retry = 4;
	int err = 0;

	struct cm36651_data *cm36651 = NULL;
	struct cm36651_platform_data *pdata = client->dev.platform_data;
	
	if  (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s: i2c functionality check failed!\n", __func__);
		return ret;
	}
	
	cm36651 = kzalloc(sizeof(struct cm36651_data), GFP_KERNEL);
	if (!cm36651) {
		pr_err("kzalloc error\n");
		err = -ENOMEM;
		goto done;
	}

	if(client->dev.of_node) {
		pdata = devm_kzalloc (&client->dev ,
			sizeof(struct cm36651_platform_data ), GFP_KERNEL);
		if(!pdata) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		return -ENOMEM;
		}
		err = cm36651_parse_dt(&client->dev, pdata);
		if(err)
			goto err_devicetree;
	} else
		pdata = client->dev.platform_data;


	if (!pdata) {
		pr_err("%s: missing pdata!\n", __func__);
		return ret;
	}

	cm36651_request_gpio(pdata);
			/* allocate driver_data */

	cm36651->pdata = pdata;
	cm36651->i2c_client = client;
	i2c_set_clientdata(client, cm36651);
	mutex_init(&cm36651->power_lock);
	mutex_init(&cm36651->read_lock);

	/* wake lock init for proximity sensor */
	wake_lock_init(&cm36651->prx_wake_lock, WAKE_LOCK_SUSPEND,
		       "prx_wake_lock");
	if (pdata->threshold)
		ps_reg_setting[1][1] = pdata->threshold;
	else
		#if defined(CONFIG_MACH_S3VE_CHN_CTC)
		ps_reg_setting[1][1] = 0x0f;
		#else
		ps_reg_setting[1][1] = 0x0a;
		#endif
		
#if defined(CONFIG_LCD_CONNECTION_CHECK) 
#if defined(CONFIG_MACH_S3VE_CHN_CTC)
	if  (!is_lcd_attached())
	{
		printk("%s: LCD not connected!,%d\n",__func__,((int)is_lcd_attached));
		sensor_power_on_vdd(cm36651,1);
	}
#endif
#endif

	gpio_direction_output(pdata->vdd_en, 1);
	do {
		retry--;
	/* Check if the device is there or not. */
		ret = cm36651_i2c_write_byte(cm36651, CM36651_PS,
			CS_CONF1, 0x01);
		if (ret < 0) {
			pr_err("%s: checking i2c error.(%d), retry %d\n",
				__func__, ret, retry);
			msleep(20);
		} else {
			break;
		}
	} while (retry);
	if (ret < 0) {
		pr_err("%s: cm36651 is not connected.(%d)\n", __func__, ret);
		gpio_direction_output(pdata->vdd_en, 0);
		goto err_setup_reg;
	}
	/* setup initial registers */
	ret = cm36651_setup_reg(cm36651);
	if (ret < 0) {
		pr_err("%s: could not setup regs\n", __func__);
		gpio_direction_output(pdata->vdd_en, 0);
		goto err_setup_reg;
	}
	gpio_direction_output(pdata->vdd_en, 0);

	/* allocate proximity input_device */
	cm36651->proximity_input_dev = input_allocate_device();
	if (!cm36651->proximity_input_dev) {
		pr_err("%s: could not allocate proximity input device\n",
		       __func__);
		goto err_input_allocate_device_proximity;
	}

	input_set_drvdata(cm36651->proximity_input_dev, cm36651);
	cm36651->proximity_input_dev->name = "proximity_sensor";
	input_set_capability(cm36651->proximity_input_dev, EV_ABS,
			     ABS_DISTANCE);
	input_set_abs_params(cm36651->proximity_input_dev, ABS_DISTANCE, 0, 1,
			     0, 0);

	ret = input_register_device(cm36651->proximity_input_dev);
	if (ret < 0) {
		input_free_device(cm36651->proximity_input_dev);
		pr_err("%s: could not register input device\n", __func__);
		goto err_input_register_device_proximity;
	}

	ret = sysfs_create_group(&cm36651->proximity_input_dev->dev.kobj,
				 &proximity_attribute_group);
	if (ret) {
		pr_err("%s: could not create sysfs group\n", __func__);
		goto err_sysfs_create_group_proximity;
	}

	/* setup irq */
	ret = cm36651_setup_irq(cm36651);
	if (ret) {
		pr_err("%s: could not setup irq\n", __func__);
		goto err_setup_irq;
	}

	/* For factory test mode, we use timer to get average proximity data. */
	/* prox_timer settings. we poll for light values using a timer. */
	hrtimer_init(&cm36651->prox_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	cm36651->prox_poll_delay = ns_to_ktime(2000 * NSEC_PER_MSEC);/*2 sec*/
	cm36651->prox_timer.function = cm36651_prox_timer_func;

	/* the timer just fires off a work queue request.  we need a thread
	   to read the i2c (can be slow and blocking). */
	cm36651->prox_wq = create_singlethread_workqueue("cm36651_prox_wq");
	if (!cm36651->prox_wq) {
		ret = -ENOMEM;
		pr_err("%s: could not create prox workqueue\n", __func__);
		goto err_create_prox_workqueue;
	}
	/* this is the thread function we run on the work queue */
	INIT_WORK(&cm36651->work_prox, cm36651_work_func_prox);

	/* allocate lightsensor input_device */
	cm36651->light_input_dev = input_allocate_device();
	if (!cm36651->light_input_dev) {
		pr_err("%s: could not allocate light input device\n", __func__);
		goto err_input_allocate_device_light;
	}

	input_set_drvdata(cm36651->light_input_dev, cm36651);
	cm36651->light_input_dev->name = "light_sensor";
	input_set_capability(cm36651->light_input_dev, EV_REL, REL_RED);
	input_set_capability(cm36651->light_input_dev, EV_REL, REL_GREEN);
	input_set_capability(cm36651->light_input_dev, EV_REL, REL_BLUE);
	input_set_capability(cm36651->light_input_dev, EV_REL, REL_WHITE);

	ret = input_register_device(cm36651->light_input_dev);
	if (ret < 0) {
		input_free_device(cm36651->light_input_dev);
		pr_err("%s: could not register input device\n", __func__);
		goto err_input_register_device_light;
	}

	ret = sysfs_create_group(&cm36651->light_input_dev->dev.kobj,
				 &light_attribute_group);
	if (ret) {
		pr_err("%s: could not create sysfs group\n", __func__);
		goto err_sysfs_create_group_light;
	}

	/* light_timer settings. we poll for light values using a timer. */
	hrtimer_init(&cm36651->light_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	cm36651->light_poll_delay = ns_to_ktime(200 * NSEC_PER_MSEC);
	cm36651->light_timer.function = cm36651_light_timer_func;

	/* the timer just fires off a work queue request.  we need a thread
	   to read the i2c (can be slow and blocking). */
	cm36651->light_wq = create_singlethread_workqueue("cm36651_light_wq");
	if (!cm36651->light_wq) {
		ret = -ENOMEM;
		pr_err("%s: could not create light workqueue\n", __func__);
		goto err_create_light_workqueue;
	}

	/* this is the thread function we run on the work queue */
	INIT_WORK(&cm36651->work_light, cm36651_work_func_light);

	cm36651->proximity_dev = device_create(sensors_class,
		NULL, 0, NULL, "proximity_sensor");
	if (IS_ERR(cm36651->proximity_dev)) {
		pr_err("%s: could not create proximity_dev\n", __func__);
		goto err_proximity_device_create;
	}

	cm36651->light_dev = device_create(sensors_class,
					NULL, 0, NULL, "light_sensor");
	if (IS_ERR(cm36651->light_dev)) {
		pr_err("%s: could not create light_dev\n", __func__);
		goto err_light_device_create;
	}

	if (device_create_file(cm36651->proximity_dev, &dev_attr_state) < 0) {
		pr_err("%s: could not create device file(%s)!\n", __func__,
			   dev_attr_state.attr.name);
		goto err_proximity_device_create_file1;
	}

#ifdef CM36651_CANCELATION
	if (device_create_file(cm36651->proximity_dev,
		&dev_attr_prox_cal) < 0) {
		pr_err("%s: could not create device file(%s)!\n", __func__,
			   dev_attr_prox_cal.attr.name);
		goto err_proximity_device_create_file2;
	}
#endif

	if (device_create_file(cm36651->proximity_dev,
		&dev_attr_prox_avg) < 0) {
		pr_err("%s: could not create device file(%s)!\n", __func__,
		       dev_attr_prox_avg.attr.name);
		goto err_proximity_device_create_file3;
	}

	if (device_create_file(cm36651->proximity_dev,
		&dev_attr_vendor) < 0) {
		pr_err("%s: could not create device file(%s)!\n", __func__,
		       dev_attr_vendor.attr.name);
		goto err_proximity_device_create_file4;
	}

	if (device_create_file(cm36651->proximity_dev,
		&dev_attr_name) < 0) {
		pr_err("%s: could not create device file(%s)!\n", __func__,
		       dev_attr_name.attr.name);
		goto err_proximity_device_create_file5;
	}

	if (device_create_file(cm36651->proximity_dev,
		&dev_attr_prox_thresh) < 0) {
		pr_err("%s: could not create device file(%s)!\n", __func__,
			   dev_attr_prox_thresh.attr.name);
		goto err_proximity_device_create_file6;
	}

	dev_set_drvdata(cm36651->proximity_dev, cm36651);


	if (device_create_file(cm36651->light_dev, &dev_attr_lux) < 0) {
		pr_err("%s: could not create device file(%s)!\n", __func__,
			   dev_attr_lux.attr.name);
		goto err_light_device_create_file1;
	}
	if (device_create_file(cm36651->light_dev, &dev_attr_raw_data) < 0) {
		pr_err("%s: could not create device file(%s)!\n", __func__,
			   dev_attr_raw_data.attr.name);
		goto err_light_device_create_file2;
	}
	if (device_create_file(cm36651->light_dev, &dev_attr_vendor) < 0) {
		pr_err("%s: could not create device file(%s)!\n", __func__,
			   dev_attr_vendor.attr.name);
		goto err_light_device_create_file3;
	}
	if (device_create_file(cm36651->light_dev, &dev_attr_name) < 0) {
		pr_err("%s: could not create device file(%s)!\n", __func__,
			   dev_attr_name.attr.name);
		goto err_light_device_create_file4;
	}
	dev_set_drvdata(cm36651->light_dev, cm36651);
	pr_info("%s is success.\n", __func__);
#ifdef CONFIG_SENSOR_USE_SYMLINK
	err =  sensors_initialize_symlink(cm36651->proximity_input_dev);
	if (err) {
		pr_err("%s: cound not make proximity sensor symlink(%d).\n",
			__func__, err);
		goto err_sensors_initialize_symlink_proximity;
	}
	
	err =  sensors_initialize_symlink(cm36651->light_input_dev);
	if (err) {
		pr_err("%s: cound not make light sensor symlink(%d).\n",
			__func__, err);
		goto err_sensors_initialize_symlink_light;
	}
#endif
	goto done;

err_devicetree:
	printk("\n error in device tree");
#ifdef CONFIG_SENSOR_USE_SYMLINK
	err_sensors_initialize_symlink_proximity:
		sensors_delete_symlink(cm36651->proximity_input_dev);
	
	err_sensors_initialize_symlink_light:
		sensors_delete_symlink(cm36651->light_input_dev);
#endif
/* error, unwind it all */
err_light_device_create_file4:
	device_remove_file(cm36651->light_dev, &dev_attr_vendor);
err_light_device_create_file3:
	device_remove_file(cm36651->light_dev, &dev_attr_raw_data);
err_light_device_create_file2:
	device_remove_file(cm36651->light_dev, &dev_attr_lux);
err_light_device_create_file1:
	device_remove_file(cm36651->proximity_dev, &dev_attr_prox_thresh);
err_proximity_device_create_file6:
	device_remove_file(cm36651->proximity_dev, &dev_attr_name);
err_proximity_device_create_file5:
	device_remove_file(cm36651->proximity_dev, &dev_attr_vendor);
err_proximity_device_create_file4:
	device_remove_file(cm36651->proximity_dev, &dev_attr_prox_avg);
err_proximity_device_create_file3:
#ifdef CM36651_CANCELATION
	device_remove_file(cm36651->proximity_dev, &dev_attr_prox_cal);
err_proximity_device_create_file2:
#endif
	device_remove_file(cm36651->proximity_dev, &dev_attr_state);
err_proximity_device_create_file1:
err_light_device_create:
	device_destroy(sensors_class, 0);
err_proximity_device_create:
	destroy_workqueue(cm36651->light_wq);
err_create_light_workqueue:
	sysfs_remove_group(&cm36651->light_input_dev->dev.kobj,
			   &light_attribute_group);
err_sysfs_create_group_light:
	input_unregister_device(cm36651->light_input_dev);
err_input_register_device_light:
err_input_allocate_device_light:
	destroy_workqueue(cm36651->prox_wq);
err_create_prox_workqueue:
	free_irq(cm36651->irq, cm36651);
	gpio_free(cm36651->pdata->p_out);
err_setup_irq:
	sysfs_remove_group(&cm36651->proximity_input_dev->dev.kobj,
			   &proximity_attribute_group);
err_sysfs_create_group_proximity:
	input_unregister_device(cm36651->proximity_input_dev);
err_input_register_device_proximity:
err_input_allocate_device_proximity:
err_setup_reg:
	wake_lock_destroy(&cm36651->prx_wake_lock);
	mutex_destroy(&cm36651->read_lock);
	mutex_destroy(&cm36651->power_lock);
#if !defined(CONFIG_MACH_S3VE_CHN_CTC)
	sensor_power_on_vdd(cm36651,0);
#endif
	kfree(cm36651);
done:
	return ret;
}

static int cm36651_i2c_remove(struct i2c_client *client)
{
	struct cm36651_data *cm36651 = i2c_get_clientdata(client);
	//struct cm36651_platform_data *pdata = client->dev.platform_data;

	/* free irq */
	free_irq(cm36651->irq, cm36651);
	gpio_free(cm36651->pdata->p_out);

	/* destroy workqueue */
	destroy_workqueue(cm36651->light_wq);
	destroy_workqueue(cm36651->prox_wq);

	/* device off */
	if (cm36651->power_state) {
		if (cm36651->power_state & LIGHT_ENABLED)
			cm36651_light_disable(cm36651);
		if (cm36651->power_state & PROXIMITY_ENABLED) {
			cm36651_i2c_write_byte(cm36651, CM36651_PS, PS_CONF1,
					       0x01);
			gpio_direction_output(cm36651->pdata->vdd_en, 0);
		}
	}

	/* sysfs destroy */
	device_remove_file(cm36651->light_dev, &dev_attr_lux);
	device_remove_file(cm36651->light_dev, &dev_attr_raw_data);
	device_remove_file(cm36651->light_dev, &dev_attr_vendor);
	device_remove_file(cm36651->light_dev, &dev_attr_name);
	device_remove_file(cm36651->proximity_dev, &dev_attr_prox_avg);
#ifdef CM36651_CANCELATION
	device_remove_file(cm36651->proximity_dev, &dev_attr_prox_cal);
#endif
	device_remove_file(cm36651->proximity_dev, &dev_attr_state);
	device_remove_file(cm36651->proximity_dev, &dev_attr_vendor);
	device_remove_file(cm36651->proximity_dev, &dev_attr_name);

	device_destroy(sensors_class, 0);

	/* input device destroy */
	sysfs_remove_group(&cm36651->light_input_dev->dev.kobj,
			   &light_attribute_group);
	input_unregister_device(cm36651->light_input_dev);
	sysfs_remove_group(&cm36651->proximity_input_dev->dev.kobj,
			   &proximity_attribute_group);
	input_unregister_device(cm36651->proximity_input_dev);

	/* lock destroy */
	mutex_destroy(&cm36651->read_lock);
	mutex_destroy(&cm36651->power_lock);
	wake_lock_destroy(&cm36651->prx_wake_lock);

	kfree(cm36651);

	return 0;
}

static int cm36651_suspend(struct device *dev)
{
	/* We disable power only if proximity is disabled.  If proximity
	   is enabled, we leave power on because proximity is allowed
	   to wake up device.  We remove power without changing
	   cm36651->power_state because we use that state in resume.
	 */
	struct cm36651_data *cm36651 = dev_get_drvdata(dev);

	if (cm36651->power_state & LIGHT_ENABLED)
		cm36651_light_disable(cm36651);
	if (!(cm36651->power_state & PROXIMITY_ENABLED)) {
		gpio_free(cm36651->pdata->p_out);
		sensor_power_on_vdd(cm36651,0);
	}

	return 0;
}

static int cm36651_resume(struct device *dev)
{
	struct cm36651_data *cm36651 = dev_get_drvdata(dev);
	int ret = 0;
	if (!(cm36651->power_state & PROXIMITY_ENABLED)) {
		ret = gpio_request(cm36651->pdata->p_out, "gpio_proximity_out");
		if (ret) {
			pr_err("%s gpio request %d err\n", __func__,
				cm36651->pdata->p_out);
		}
		sensor_power_on_vdd(cm36651,1);
	}
	if (cm36651->power_state & LIGHT_ENABLED)
		cm36651_light_enable(cm36651);

	return 0;
}

static const struct i2c_device_id cm36651_device_id[] = {
	{"cm36651", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, cm36651_device_id);

static const struct dev_pm_ops cm36651_pm_ops = {
	.suspend = cm36651_suspend,
	.resume = cm36651_resume
};

#ifdef CONFIG_OF
static struct of_device_id cm36651_match_table[] = {
	{ .compatible = "capella,cm36651",},
	{},
};
#else
#define cm36651_match_table NULL
#endif

static struct i2c_driver cm36651_i2c_driver = {
	.driver = {
		   .name = "cm36651",
		   .owner = THIS_MODULE,
		   .pm = &cm36651_pm_ops,
		   .of_match_table = cm36651_match_table,

		   },
	.probe = cm36651_i2c_probe,
	.remove = cm36651_i2c_remove,
	.id_table = cm36651_device_id,
};

static int __init cm36651_init(void)
{
	return i2c_add_driver(&cm36651_i2c_driver);
}

static void __exit cm36651_exit(void)
{
	i2c_del_driver(&cm36651_i2c_driver);
}

module_init(cm36651_init);
module_exit(cm36651_exit);

MODULE_AUTHOR("Samsung Electronics");
MODULE_DESCRIPTION("RGB Sensor device driver for cm36651");
MODULE_LICENSE("GPL");
