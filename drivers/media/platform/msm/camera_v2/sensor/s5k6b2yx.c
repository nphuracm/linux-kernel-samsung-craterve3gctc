/* Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include "msm_sensor.h"
#define S5K6B2YX_SENSOR_NAME "s5k6b2yx"
DEFINE_MSM_MUTEX(s5k6b2yx_mut);
#if defined(CONFIG_MACH_HLITE_CHN_SGLTE)
extern struct msm_sensor_ctrl_t *g_sctrl[MAX_CAMERAS];
#endif
static struct msm_sensor_ctrl_t s5k6b2yx_s_ctrl;

static struct v4l2_subdev_info s5k6b2yx_subdev_info[] = {
	{
		.code   = V4L2_MBUS_FMT_SBGGR10_1X10,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.fmt    = 1,
		.order    = 0,
	},
};

static const struct i2c_device_id s5k6b2yx_i2c_id[] = {
	{S5K6B2YX_SENSOR_NAME, (kernel_ulong_t)&s5k6b2yx_s_ctrl},
	{ }
};

static int32_t msm_s5k6b2yx_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
#if defined(CONFIG_MACH_HLITE_CHN_SGLTE)
	int ret;
	ret = msm_sensor_i2c_probe(client, id, &s5k6b2yx_s_ctrl);
	g_sctrl[2]->sensor_i2c_client->i2c_func_tbl = s5k6b2yx_s_ctrl.sensor_i2c_client->i2c_func_tbl;
	g_sctrl[2]->sensor_i2c_client->client = s5k6b2yx_s_ctrl.sensor_i2c_client->client;
	g_sctrl[2]->sensor_device_type = s5k6b2yx_s_ctrl.sensor_device_type;
	return ret;	
#else
	return msm_sensor_i2c_probe(client, id, &s5k6b2yx_s_ctrl);
#endif
}
static struct i2c_driver s5k6b2yx_i2c_driver = {
	.id_table = s5k6b2yx_i2c_id,
	.probe  = msm_s5k6b2yx_i2c_probe,
	.driver = {
		.name = S5K6B2YX_SENSOR_NAME,
	},
};

static struct msm_camera_i2c_client s5k6b2yx_sensor_i2c_client = {
	.addr_type = MSM_CAMERA_I2C_WORD_ADDR,
};

static const struct of_device_id s5k6b2yx_dt_match[] = {
	{.compatible = "qcom,s5k6b2yx", .data = &s5k6b2yx_s_ctrl},
	{}
};

MODULE_DEVICE_TABLE(of, s5k6b2yx_dt_match);

static struct platform_driver s5k6b2yx_platform_driver = {
	.driver = {
		.name = "qcom,s5k6b2yx",
		.owner = THIS_MODULE,
		.of_match_table = s5k6b2yx_dt_match,
	},
};

static int32_t s5k6b2yx_platform_probe(struct platform_device *pdev)
{
	int32_t rc = 0;
	const struct of_device_id *match;
	match = of_match_device(s5k6b2yx_dt_match, &pdev->dev);
	rc = msm_sensor_platform_probe(pdev, match->data);
	return rc;
}

static int __init s5k6b2yx_init_module(void)
{
	int32_t rc = 0;
	pr_info("%s:%d\n", __func__, __LINE__);
	rc = platform_driver_probe(&s5k6b2yx_platform_driver,
		s5k6b2yx_platform_probe);
	if (!rc)
		return rc;
	pr_err("%s:%d rc %d\n", __func__, __LINE__, rc);
	return i2c_add_driver(&s5k6b2yx_i2c_driver);
}

static void __exit s5k6b2yx_exit_module(void)
{
	pr_info("%s:%d\n", __func__, __LINE__);
	if (s5k6b2yx_s_ctrl.pdev) {
		msm_sensor_free_sensor_data(&s5k6b2yx_s_ctrl);
		platform_driver_unregister(&s5k6b2yx_platform_driver);
	} else
		i2c_del_driver(&s5k6b2yx_i2c_driver);
	return;
}

static struct msm_sensor_ctrl_t s5k6b2yx_s_ctrl = {
	.sensor_i2c_client = &s5k6b2yx_sensor_i2c_client,
	.msm_sensor_mutex = &s5k6b2yx_mut,
	.sensor_v4l2_subdev_info = s5k6b2yx_subdev_info,
	.sensor_v4l2_subdev_info_size = ARRAY_SIZE(s5k6b2yx_subdev_info),
};

module_init(s5k6b2yx_init_module);
module_exit(s5k6b2yx_exit_module);
MODULE_DESCRIPTION("s5k6b2yx");
MODULE_LICENSE("GPL v2");
