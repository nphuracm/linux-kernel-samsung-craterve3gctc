/*
 *
 * Zinitix bt532 touchscreen driver
 *
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 * GNU General Public License for more details.
 *
 */


#define TSP_VERBOSE_DEBUG
#define SEC_FACTORY_TEST
#define SUPPORTED_TOUCH_KEY

#include <linux/module.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#if defined(CONFIG_PM_RUNTIME)
#include <linux/pm_runtime.h>
#endif
#include <linux/semaphore.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/regulator/consumer.h>

#include <zinitix_bt_craterq.h>
//#include <zinitix_bt_532.h>
//#include <zinitix_touch.h>
#include <linux/input/mt.h>
#include <linux/of_gpio.h>

#include "craterq_fw.h"
//#include "ct01_fw.h"

#ifdef  CONFIG_MACH_CRATERVE
#include <linux/leds.h>
static struct led_classdev touch_leds;
static u16 touch_sensi[2]={0};
static bool  touch_VAstate=0;
static struct mutex touchkey_sensi_lock;
static struct delayed_work work_clr_sensi;
#define CLR_SENSI_INVAL   (HZ/2)
#define ZINITIX_DEVICE_NAME  "SCH_P709E"
#define IC_INFO   "ZI"
#define FW_RELEASE_DATE  "1207"
#endif

#ifdef CONFIG_SEC_DVFS
#include <linux/cpufreq.h>
#define TOUCH_BOOSTER_DVFS

#define DVFS_STAGE_TRIPLE       3

#define DVFS_STAGE_DUAL         2
#define DVFS_STAGE_SINGLE       1
#define DVFS_STAGE_NONE         0
#endif

#ifdef TOUCH_BOOSTER_DVFS
#define TOUCH_BOOSTER_OFF_TIME	500
#define TOUCH_BOOSTER_CHG_TIME	300//130
#endif

#define ZINITIX_DEBUG			1
#define TOUCH_POINT_MODE			2

#define MAX_SUPPORTED_FINGER_NUM	5 /* max 10 */
#ifdef SUPPORTED_TOUCH_KEY
#define MAX_SUPPORTED_BUTTON_NUM	2 // 6 /* max 8 */
#define SUPPORTED_BUTTON_NUM		2
#endif

/* Upgrade Method*/
#define TOUCH_ONESHOT_UPGRADE		1
/* if you use isp mode, you must add i2c device :
name = "zinitix_isp" , addr 0x50*/

/* resolution offset */
#define ABS_PT_OFFSET				(-1)

#define TOUCH_FORCE_UPGRADE			1
#define USE_CHECKSUM				1
#define CHECK_HWID				0

#define CHIP_OFF_DELAY				50 /*ms*/
#define CHIP_ON_DELAY				15 /*ms*/
#define FIRMWARE_ON_DELAY			40 /*ms*/

#define DELAY_FOR_SIGNAL_DELAY			30 /*us*/
#define DELAY_FOR_TRANSCATION			50
#define DELAY_FOR_POST_TRANSCATION		10

enum power_control {
	POWER_OFF,
	POWER_ON,
	POWER_ON_SEQUENCE,
};

/* Key Enum */
enum key_event {
	ICON_BUTTON_UNCHANGE,
	ICON_BUTTON_DOWN,
	ICON_BUTTON_UP,
};

/* ESD Protection */
/*second : if 0, no use. if you have to use, 3 is recommended*/
#define ESD_TIMER_INTERVAL			1
#define SCAN_RATE_HZ				100
#define CHECK_ESD_TIMER				3

 /*Test Mode (Monitoring Raw Data) */
#define SEC_DND_N_COUNT				10
#define SEC_DND_FREQUENCY			90
#define SEC_PDND_N_COUNT			14
#define SEC_PDND_U_COUNT			24
#define SEC_PDND_FREQUENCY			59

#define MAX_RAW_DATA_SZ				576 /* 32x18 */
#define MAX_TRAW_DATA_SZ	\
	(MAX_RAW_DATA_SZ + 4*MAX_SUPPORTED_FINGER_NUM + 2)
/* preriod raw data interval */

#define RAWDATA_DELAY_FOR_HOST		100

struct raw_ioctl {
	int sz;
	u8 *buf;
};

struct reg_ioctl {
	int addr;
	int *val;
};

#define TOUCH_SEC_MODE			48
#define TOUCH_REF_MODE			10
#define TOUCH_NORMAL_MODE		5
#define TOUCH_DELTA_MODE		3
#define TOUCH_DND_MODE			6
#define TOUCH_PDND_MODE			11

/*  Other Things */
#define INIT_RETRY_CNT			5
#define I2C_SUCCESS			0
#define I2C_FAIL			1

/*---------------------------------------------------------------------*/

/* Register Map*/
#define BT532_SWRESET_CMD			0x0000
#define BT532_WAKEUP_CMD			0x0001

#define BT532_IDLE_CMD				0x0004
#define BT532_SLEEP_CMD				0x0005

#define BT532_CLEAR_INT_STATUS_CMD		0x0003
#define BT532_CALIBRATE_CMD			0x0006
#define BT532_SAVE_STATUS_CMD			0x0007
#define BT532_SAVE_CALIBRATION_CMD		0x0008
#define BT532_RECALL_FACTORY_CMD		0x000f

#define BT532_THRESHOLD				0x0020

#define BT532_DEBUG_REG				0x0115 /* 0~7 */

#define BT532_TOUCH_MODE			0x0010
#define BT532_CHIP_REVISION			0x0011
#define BT532_FIRMWARE_VERSION			0x0012

#define BT532_MINOR_FW_VERSION			0x0121

#define BT532_VENDOR_ID				0x001C
#define BT532_HW_ID				0x0014

#define BT532_DATA_VERSION_REG			0x0013
#define BT532_SUPPORTED_FINGER_NUM		0x0015
#define BT532_EEPROM_INFO			0x0018
#define BT532_INITIAL_TOUCH_MODE		0x0019

#define BT532_TOTAL_NUMBER_OF_X			0x0060
#define BT532_TOTAL_NUMBER_OF_Y			0x0061

#define BT532_DELAY_RAW_FOR_HOST		0x007f

#define BT532_BUTTON_SUPPORTED_NUM		0x00B0
#define BT532_BUTTON_SENSITIVITY		0x00B2
#define BT532_DUMMY_BUTTON_SENSITIVITY		0X00C8

#define BT532_X_RESOLUTION			0x00C0
#define BT532_Y_RESOLUTION			0x00C1
#define BT532_HOLD_POINT_THRESHOLD		0x00C3

#define BT532_POINT_STATUS_REG			0x0080
#define BT532_ICON_STATUS_REG			0x00AA

#define BT532_AFE_FREQUENCY			0x0100
#define BT532_DND_N_COUNT			0x0122
#define BT532_DND_U_COUNT			0x0135

#define BT532_RAWDATA_REG			0x0200

#define BT532_EEPROM_INFO_REG			0x0018

#define BT532_INT_ENABLE_FLAG			0x00f0
#define BT532_PERIODICAL_INTERRUPT_INTERVAL	0x00f1

#define BT532_BTN_WIDTH				0x016d
#define ZINITIX_BTN_SENSTIVITY		0x00b2

#define BT532_CHECKSUM_RESULT			0x012c

#define BT532_INIT_FLASH			0x01d0
#define BT532_WRITE_FLASH			0x01d1
#define BT532_READ_FLASH			0x01d2

#define DEBUG_I2C_CHECKSUM

#ifdef DEBUG_I2C_CHECKSUM
#define	ZINITIX_I2C_CHECKSUM_WCNT	0x016a
#define	ZINITIX_I2C_CHECKSUM_RESULT	0x016c
#define	ZINITIX_INTERNAL_FLAG_02 0x011e
#endif

/* Interrupt & status register flag bit
-------------------------------------------------
*/
#define BIT_PT_CNT_CHANGE	0
#define BIT_DOWN		1
#define BIT_MOVE		2
#define BIT_UP			3
#define BIT_PALM		4
#define BIT_PALM_REJECT		5
#define RESERVED_0		6
#define RESERVED_1		7
#define BIT_WEIGHT_CHANGE	8
#define BIT_PT_NO_CHANGE	9
#define BIT_REJECT		10
#define BIT_PT_EXIST		11
#define RESERVED_2		12
#define BIT_MUST_ZERO		13
#define BIT_DEBUG		14
#define BIT_ICON_EVENT		15

/* button */
#define BIT_O_ICON0_DOWN	0
#define BIT_O_ICON1_DOWN	1
#define BIT_O_ICON2_DOWN	2
#define BIT_O_ICON3_DOWN	3
#define BIT_O_ICON4_DOWN	4
#define BIT_O_ICON5_DOWN	5
#define BIT_O_ICON6_DOWN	6
#define BIT_O_ICON7_DOWN	7

#define BIT_O_ICON0_UP		8
#define BIT_O_ICON1_UP		9
#define BIT_O_ICON2_UP		10
#define BIT_O_ICON3_UP		11
#define BIT_O_ICON4_UP		12
#define BIT_O_ICON5_UP		13
#define BIT_O_ICON6_UP		14
#define BIT_O_ICON7_UP		15


#define SUB_BIT_EXIST		0
#define SUB_BIT_DOWN		1
#define SUB_BIT_MOVE		2
#define SUB_BIT_UP		3
#define SUB_BIT_UPDATE		4
#define SUB_BIT_WAIT		5


#define zinitix_bit_set(val, n)		((val) &= ~(1<<(n)), (val) |= (1<<(n)))
#define zinitix_bit_clr(val, n)		((val) &= ~(1<<(n)))
#define zinitix_bit_test(val, n)	((val) & (1<<(n)))
#define zinitix_swap_v(a, b, t)		((t) = (a), (a) = (b), (b) = (t))
#define zinitix_swap_16(s)		(((((s) & 0xff) << 8) | (((s) >> 8) \
								& 0xff)))

/* end header file */

#ifdef SEC_FACTORY_TEST
/* Touch Screen */
#define TSP_CMD_STR_LEN			32
#define TSP_CMD_RESULT_STR_LEN		512
#define TSP_CMD_PARAM_NUM		8
#define TSP_CMD_Y_NUM			18
#define TSP_CMD_X_NUM			30
#define TSP_CMD_NODE_NUM		(TSP_CMD_Y_NUM * TSP_CMD_X_NUM)

struct tsp_factory_info {
	struct list_head cmd_list_head;
	char cmd[TSP_CMD_STR_LEN];
	char cmd_param[TSP_CMD_PARAM_NUM];
	char cmd_result[TSP_CMD_RESULT_STR_LEN];
	char cmd_buff[TSP_CMD_RESULT_STR_LEN];
	struct mutex cmd_lock;
	bool cmd_is_running;
	u8 cmd_state;
};

struct tsp_raw_data {
	u16 ref_data[TSP_CMD_NODE_NUM];
	u16 pref_data[TSP_CMD_NODE_NUM];
	/*s16 scantime_data[TSP_CMD_NODE_NUM]; */
	s16 delta_data[TSP_CMD_NODE_NUM];
};

enum {
	WAITING = 0,
	RUNNING,
	OK,
	FAIL,
	NOT_APPLICABLE,
};

struct tsp_cmd {
	struct list_head list;
	const char *cmd_name;
	void (*cmd_func)(void *device_data);
};

static void fw_update(void *device_data);
static void get_fw_ver_bin(void *device_data);
static void get_fw_ver_ic(void *device_data);
static void get_threshold(void *device_data);
static void module_off_master(void *device_data);
static void module_on_master(void *device_data);
static void module_off_slave(void *device_data);
static void module_on_slave(void *device_data);
static void get_chip_vendor(void *device_data);
static void get_chip_name(void *device_data);
static void get_x_num(void *device_data);
static void get_y_num(void *device_data);
static void not_support_cmd(void *device_data);

/* Vendor dependant command */
static void run_reference_read(void *device_data);
static void get_reference(void *device_data);
static void run_preference_read(void *device_data);
static void get_preference(void *device_data);
static void run_delta_read(void *device_data);
static void get_delta(void *device_data);
static void flip_cover_enable(void *device_data);
static int set_conifg_flip_cover(int enables);
static int note_flip_open(void);
static int note_flip_close(void);

#ifdef CONFIG_MACH_CRATERVE
static void get_config_ver(void *device_data);
#endif

#ifdef TOUCH_BOOSTER_DVFS
static void boost_level(void *device_data);
#endif

#define TSP_CMD(name, func) .cmd_name = name, .cmd_func = func

static struct tsp_cmd tsp_cmds[] = {
	{TSP_CMD("fw_update", fw_update),},
	{TSP_CMD("get_fw_ver_bin", get_fw_ver_bin),},
	{TSP_CMD("get_fw_ver_ic", get_fw_ver_ic),},
	{TSP_CMD("get_threshold", get_threshold),},
	{TSP_CMD("module_off_master", module_off_master),},
	{TSP_CMD("module_on_master", module_on_master),},
	{TSP_CMD("module_off_slave", module_off_slave),},
	{TSP_CMD("module_on_slave", module_on_slave),},
	{TSP_CMD("get_chip_vendor", get_chip_vendor),},
	{TSP_CMD("get_chip_name", get_chip_name),},
	{TSP_CMD("get_x_num", get_x_num),},
	{TSP_CMD("get_y_num", get_y_num),},
	{TSP_CMD("not_support_cmd", not_support_cmd),},

	/* vendor dependant command */
	{TSP_CMD("run_reference_read", run_reference_read),},
	{TSP_CMD("get_reference", get_reference),},
	{TSP_CMD("run_dnd_read", run_preference_read),},
	{TSP_CMD("get_dnd", get_preference),},
	{TSP_CMD("run_delta_read", run_delta_read),},
	{TSP_CMD("get_delta", get_delta),},
	{TSP_CMD("flip_cover_enable", flip_cover_enable),},	
#ifdef TOUCH_BOOSTER_DVFS
        {TSP_CMD("boost_level", boost_level),},
#endif
#ifdef  CONFIG_MACH_CRATERVE
	{TSP_CMD("get_config_ver", get_config_ver),},
#endif
};
#endif

#define TSP_NORMAL_EVENT_MSG 1
static int m_ts_debug_mode = ZINITIX_DEBUG;

struct coord {
	u16	x;
	u16	y;
	u8	width;
	u8	sub_status;
#if (TOUCH_POINT_MODE == 2)
	u8	minor_width;
	u8	angle;
#endif
};

struct point_info {
	u16	status;
#if (TOUCH_POINT_MODE == 1)
	u16	event_flag;
#else
	u8	finger_cnt;
	u8	time_stamp;
#endif
	struct coord coord[MAX_SUPPORTED_FINGER_NUM];
};

#define TOUCH_V_FLIP	0x01
#define TOUCH_H_FLIP	0x02
#define TOUCH_XY_SWAP	0x04

struct capa_info {
	u16	vendor_id;
	u16	ic_revision;
	u16	fw_version;
	u16	fw_minor_version;
	u16	reg_data_version;
	u16	threshold;
	u16	key_threshold;
	//u16	dummy_threshold;
	u32	ic_fw_size;
	u32	MaxX;
	u32	MaxY;
	u32	MinX;
	u32	MinY;
	u8	gesture_support;
	u16	multi_fingers;
	u16	button_num;
	u16	ic_int_mask;
	u16	x_node_num;
	u16	y_node_num;
	u16	total_node_num;
	u16	hw_id;
	u16	afe_frequency;
#ifdef DEBUG_I2C_CHECKSUM
	u16	i2s_checksum;
#endif
};

enum work_state {
	NOTHING = 0,
	NORMAL,
	ESD_TIMER,
	EALRY_SUSPEND,
	SUSPEND,
	RESUME,
	LATE_RESUME,
	UPGRADE,
	REMOVE,
	SET_MODE,
	HW_CALIBRAION,
	RAW_DATA,
};

enum {
	BUILT_IN = 0,
	UMS,
	REQ_FW,
};

struct bt532_ts_info {
	struct i2c_client		*client;
	struct input_dev		*input_dev;
	struct bt532_ts_platform_data	*pdata;
	char				phys[32];
	struct mutex			prob_int_sync;

	struct capa_info		cap_info;
	struct point_info		touch_info;
	struct point_info		reported_touch_info;
	u16				icon_event_reg;
	u16				prev_icon_event;

	int				irq;
	u8				button[MAX_SUPPORTED_BUTTON_NUM];
	u8				work_state;
	struct semaphore		work_lock;

#if ESD_TIMER_INTERVAL
	struct workqueue_struct		*esd_tmr_workqueue;
	struct work_struct		tmr_work;
	struct timer_list		esd_timeout_tmr;
	struct timer_list		*p_esd_timeout_tmr;
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend		early_suspend;
#endif
	struct semaphore		raw_data_lock;
	u16				touch_mode;
	s16				cur_data[MAX_TRAW_DATA_SZ];
	u8				update;

#ifdef SEC_FACTORY_TEST
	struct tsp_factory_info		*factory_info;
	struct tsp_raw_data		*raw_data;
	struct device			*fac_dev_ts;
#ifdef SUPPORTED_TOUCH_KEY
	struct device			*fac_dev_tk;
#endif
#endif

#ifdef TOUCH_BOOSTER_DVFS
	struct delayed_work	work_dvfs_off;
	struct delayed_work	work_dvfs_chg;
	struct mutex		dvfs_lock;
	bool dvfs_lock_status;
	u8								finger_cnt1;
	int dvfs_boost_mode;
	int dvfs_freq;
	int dvfs_old_stauts;
	bool stay_awake;
#endif

	void	(*register_cb)(struct tsp_callbacks *);
	struct tsp_callbacks		callbacks;
	int				ta_status;

	struct regulator *vddo_vreg;
	bool device_enabled;

};

/*<= you must set key button mapping*/
static u32 BUTTON_MAPPING_KEY[MAX_SUPPORTED_BUTTON_NUM] = {
KEY_MENU, KEY_BACK};
//	KEY_DUMMY_MENU, KEY_MENU, KEY_DUMMY_HOME1,
//	KEY_DUMMY_HOME2, KEY_BACK, KEY_DUMMY_BACK};

void zinitix_vdd_on(struct bt532_ts_info *info,bool onoff);

/* define i2c sub functions*/
static inline s32 read_data(struct i2c_client *client,
	u16 reg, u8 *values, u16 length)
{
	s32 ret;
	int count = 0;
retry:
	/* select register*/
	ret = i2c_master_send(client , (u8 *)&reg , 2);
	if (ret < 0) {
		mdelay(1);

		if (++count < 8)
			goto retry;

		return ret;
	}
	/* for setup tx transaction. */
	udelay(DELAY_FOR_TRANSCATION);
	ret = i2c_master_recv(client , values , length);
	if (ret < 0)
		return ret;

	udelay(DELAY_FOR_POST_TRANSCATION);
	return length;
}

static inline s32 write_data(struct i2c_client *client,
	u16 reg, u8 *values, u16 length)
{
	s32 ret;
	int count = 0;
	u8 pkt[10]; /* max packet */
	pkt[0] = (reg) & 0xff; /* reg addr */
	pkt[1] = (reg >> 8)&0xff;
	memcpy((u8 *)&pkt[2], values, length);

retry:
	ret = i2c_master_send(client , pkt , length + 2);
	if (ret < 0) {
		mdelay(1);

		if (++count < 8)
			goto retry;

		return ret;
	}

	udelay(DELAY_FOR_POST_TRANSCATION);
	return length;
}

static inline s32 write_reg(struct i2c_client *client, u16 reg, u16 value)
{
	if (write_data(client, reg, (u8 *)&value, 2) < 0)
		return I2C_FAIL;

	return I2C_SUCCESS;
}

static inline s32 write_cmd(struct i2c_client *client, u16 reg)
{
	s32 ret;
	int count = 0;

retry:
	ret = i2c_master_send(client , (u8 *)&reg , 2);
	if (ret < 0) {
		mdelay(1);

		if (++count < 8)
			goto retry;

		return ret;
	}

	udelay(DELAY_FOR_POST_TRANSCATION);
	return I2C_SUCCESS;
}

static inline s32 read_raw_data(struct i2c_client *client,
		u16 reg, u8 *values, u16 length)
{
	s32 ret;
	int count = 0;

retry:
	/* select register */
	ret = i2c_master_send(client , (u8 *)&reg , 2);
	if (ret < 0) {
		mdelay(1);

		if (++count < 8)
			goto retry;

		return ret;
	}

	/* for setup tx transaction. */
	udelay(200);

	ret = i2c_master_recv(client , values , length);
	if (ret < 0)
		return ret;
	udelay(DELAY_FOR_POST_TRANSCATION);
	return length;
}

static inline s32 read_firmware_data(struct i2c_client *client,
	u16 addr, u8 *values, u16 length)
{
	s32 ret;
	/* select register*/

	ret = i2c_master_send(client , (u8 *)&addr , 2);
	if (ret < 0)
		return ret;

	/* for setup tx transaction. */
	mdelay(1);

	ret = i2c_master_recv(client , values , length);
	if (ret < 0)
		return ret;

	udelay(DELAY_FOR_POST_TRANSCATION);
	return length;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void bt532_ts_early_suspend(struct early_suspend *h);
static void bt532_ts_late_resume(struct early_suspend *h);
#endif

#define USE_OPEN_CLOSE

#ifdef USE_OPEN_CLOSE
static int  bt532_ts_open(struct input_dev *dev);
static void bt532_ts_close(struct input_dev *dev);
#endif

static bool bt532_power_control(struct bt532_ts_info *info, u8 ctl);

static bool init_touch(struct bt532_ts_info *info, bool force);
static bool mini_init_touch(struct bt532_ts_info *info);
static void clear_report_data(struct bt532_ts_info *info);
static void esd_timer_start(u16 sec, struct bt532_ts_info *info);
static void esd_timer_stop(struct bt532_ts_info *info);
static void esd_timeout_handler(unsigned long data);

static long ts_misc_fops_ioctl(struct file *filp, unsigned int cmd,
								unsigned long arg);
static int ts_misc_fops_open(struct inode *inode, struct file *filp);
static int ts_misc_fops_close(struct inode *inode, struct file *filp);

static const struct file_operations ts_misc_fops = {
	.owner = THIS_MODULE,
	.open = ts_misc_fops_open,
	.release = ts_misc_fops_close,
	.unlocked_ioctl = ts_misc_fops_ioctl,
};

static struct miscdevice touch_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "zinitix_touch_misc",
	.fops = &ts_misc_fops,
};

#define TOUCH_IOCTL_BASE			0xbc
#define TOUCH_IOCTL_GET_DEBUGMSG_STATE		_IOW(TOUCH_IOCTL_BASE, 0, int)
#define TOUCH_IOCTL_SET_DEBUGMSG_STATE		_IOW(TOUCH_IOCTL_BASE, 1, int)
#define TOUCH_IOCTL_GET_CHIP_REVISION		_IOW(TOUCH_IOCTL_BASE, 2, int)
#define TOUCH_IOCTL_GET_FW_VERSION		_IOW(TOUCH_IOCTL_BASE, 3, int)
#define TOUCH_IOCTL_GET_REG_DATA_VERSION	_IOW(TOUCH_IOCTL_BASE, 4, int)
#define TOUCH_IOCTL_VARIFY_UPGRADE_SIZE		_IOW(TOUCH_IOCTL_BASE, 5, int)
#define TOUCH_IOCTL_VARIFY_UPGRADE_DATA		_IOW(TOUCH_IOCTL_BASE, 6, int)
#define TOUCH_IOCTL_START_UPGRADE		_IOW(TOUCH_IOCTL_BASE, 7, int)
#define TOUCH_IOCTL_GET_X_NODE_NUM		_IOW(TOUCH_IOCTL_BASE, 8, int)
#define TOUCH_IOCTL_GET_Y_NODE_NUM		_IOW(TOUCH_IOCTL_BASE, 9, int)
#define TOUCH_IOCTL_GET_TOTAL_NODE_NUM		_IOW(TOUCH_IOCTL_BASE, 10, int)
#define TOUCH_IOCTL_SET_RAW_DATA_MODE		_IOW(TOUCH_IOCTL_BASE, 11, int)
#define TOUCH_IOCTL_GET_RAW_DATA		_IOW(TOUCH_IOCTL_BASE, 12, int)
#define TOUCH_IOCTL_GET_X_RESOLUTION		_IOW(TOUCH_IOCTL_BASE, 13, int)
#define TOUCH_IOCTL_GET_Y_RESOLUTION		_IOW(TOUCH_IOCTL_BASE, 14, int)
#define TOUCH_IOCTL_HW_CALIBRAION		_IOW(TOUCH_IOCTL_BASE, 15, int)
#define TOUCH_IOCTL_GET_REG			_IOW(TOUCH_IOCTL_BASE, 16, int)
#define TOUCH_IOCTL_SET_REG			_IOW(TOUCH_IOCTL_BASE, 17, int)
#define TOUCH_IOCTL_SEND_SAVE_STATUS		_IOW(TOUCH_IOCTL_BASE, 18, int)
#define TOUCH_IOCTL_DONOT_TOUCH_EVENT		_IOW(TOUCH_IOCTL_BASE, 19, int)

struct bt532_ts_info *misc_info;

static bool get_raw_data(struct bt532_ts_info *info, u8 *buff, int skip_cnt)
{
	struct i2c_client *client = info->client;
	struct bt532_ts_platform_data *pdata = info->pdata;
	u32 total_node = info->cap_info.total_node_num;
	int sz;
	int i;

	disable_irq(info->irq);

	down(&info->work_lock);
	if (info->work_state != NOTHING) {
		dev_err(&client->dev, "%s: Other process occupied (%d)\n",
				__func__, info->work_state);
	}

	info->work_state = RAW_DATA;

	for(i = 0; i < skip_cnt; i++) {
		while (gpio_get_value(pdata->gpio_int))
			msleep(1);

		write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
		msleep(1);
	}

	sz = total_node * 2;

	while (gpio_get_value(pdata->gpio_int))
		msleep(1);

	if (read_raw_data(client, BT532_RAWDATA_REG,
			(char *)buff, sz) < 0) {
			dev_err(&client->dev, "%s: Failed to read raw data\n", __func__);
		}
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
	info->work_state = NOTHING;
	enable_irq(info->irq);
	up(&info->work_lock);

	return true;

}

static bool ts_get_raw_data(struct bt532_ts_info *info)
{
	struct i2c_client *client = info->client;
	u32 total_node = info->cap_info.total_node_num;
	u32 sz;

	if (down_trylock(&info->raw_data_lock)) {
		dev_err(&client->dev, "%s: Failed to occupy work lock\n", __func__);
		info->touch_info.status = 0;

		return true;
	}

	sz = total_node * 2 + sizeof(struct point_info);

	if (read_raw_data(info->client, BT532_RAWDATA_REG,
			(char *)info->cur_data, sz) < 0) {
		dev_err(&client->dev, "%s: Failed to read raw data\n", __func__);
		up(&info->raw_data_lock);

		return false;
	}

	info->update = 1;
	memcpy((u8 *)(&info->touch_info), (u8 *)&info->cur_data[total_node],
			sizeof(struct point_info));
	up(&info->raw_data_lock);

	return true;
}

#ifdef DEBUG_I2C_CHECKSUM

static bool i2c_checksum(struct bt532_ts_info *touch_dev, s16 *pChecksum, u16 wlength)
{
	s16 checksum_result;
	s16 checksum_cur;
	int i;

	checksum_cur = 0;
	for(i=0; i<wlength; i++) {
		checksum_cur += (s16)pChecksum[i];
	}
	if (read_data(touch_dev->client,
		ZINITIX_I2C_CHECKSUM_RESULT,
		(u8 *)(&checksum_result), 2) < 0) {
		printk(KERN_INFO "error read i2c checksum rsult.-\r\n");
		return false;
	}
	if(checksum_cur != checksum_result) {
		printk(KERN_INFO "checksum error : %d,%d\n", checksum_cur, checksum_result);
		return false;
	}
	return true;
}
#endif

static bool ts_read_coord(struct bt532_ts_info *info)
{
	struct i2c_client *client = info->client;
#if (TOUCH_POINT_MODE == 1)
	int i;
#endif

	/* for  Debugging Tool */

	if (info->touch_mode != TOUCH_POINT_MODE) {
		if (ts_get_raw_data(info) == false)
			return false;

		dev_err(&client->dev, "status = 0x%04X\n", info->touch_info.status);

		goto out;
	}

#if (TOUCH_POINT_MODE == 1)
	memset(&info->touch_info,
			0x0, sizeof(struct point_info));

	if (read_data(info->client, BT532_POINT_STATUS_REG,
			(u8 *)(&info->touch_info), 4) < 0) {
		dev_err(&client->dev, "Failed to read point info\n");

		return false;
	}

	if (info->touch_info.event_flag == 0)
		goto out;

	for (i = 0; i < info->cap_info.multi_fingers; i++) {
		if (zinitix_bit_test(info->touch_info.event_flag, i)) {
			udelay(20);

			if (read_data(info->client, BT532_POINT_STATUS_REG + 2 + ( i * 4),
					(u8 *)(&info->touch_info.coord[i]),
					sizeof(struct coord)) < 0) {
				dev_err(&client->dev, "Failed to read point info\n");

				return false;
			}
		}
	}
#else
#ifdef DEBUG_I2C_CHECKSUM
	if(info->cap_info.i2s_checksum)
		if (write_reg(info->client,
				ZINITIX_I2C_CHECKSUM_WCNT, (sizeof(struct point_info)/2)) != I2C_SUCCESS)
				return false;
#endif
	if (read_data(info->client, BT532_POINT_STATUS_REG,
			(u8 *)(&info->touch_info), sizeof(struct point_info)) < 0) {
		dev_err(&client->dev, "Failed to read point info\n");

		return false;
	}
#ifdef DEBUG_I2C_CHECKSUM
	if(info->cap_info.i2s_checksum)
		if(i2c_checksum (info, (s16 *)(&info->touch_info), sizeof(struct point_info)/2) == false)
			return false;
#endif
#endif

out:
	/* error */
#ifdef DEBUG_I2C_CHECKSUM
	if (info->touch_info.status == 0x0) {
		//zinitix_debug_msg("periodical esd repeated int occured\r\n");
		if(write_cmd(info->client, BT532_CLEAR_INT_STATUS_CMD) != I2C_SUCCESS)
			return false;
		udelay(DELAY_FOR_SIGNAL_DELAY);	
		return true;
	}
#endif
	// error
	if (zinitix_bit_test(info->touch_info.status, BIT_MUST_ZERO)) {
		dev_err(&client->dev, "Invalid must zero bit(%04x)\n",
			info->touch_info.status);
#ifdef DEBUG_I2C_CHECKSUM
		udelay(DELAY_FOR_SIGNAL_DELAY);
		return false;
#endif
	}
#ifdef DEBUG_I2C_CHECKSUM
	if (zinitix_bit_test(info->touch_info.status, BIT_ICON_EVENT)) {
		if (read_data(info->client, BT532_ICON_STATUS_REG,
			(u8 *)(&info->icon_event_reg), 2) < 0) {
			dev_err(&client->dev, "Failed to read button info\n");
		return false;
	}
	}
#endif

	if(write_cmd(info->client, BT532_CLEAR_INT_STATUS_CMD) != I2C_SUCCESS)
		return false;

	return true;
}

#if ESD_TIMER_INTERVAL
static void esd_timeout_handler(unsigned long data)
{
	struct bt532_ts_info *info = (struct bt532_ts_info *)data;

	info->p_esd_timeout_tmr = NULL;
	queue_work(info->esd_tmr_workqueue, &info->tmr_work);
}

static void esd_timer_start(u16 sec, struct bt532_ts_info *info)
{
	mutex_lock(&info->prob_int_sync);
	if (info->p_esd_timeout_tmr != NULL)
		del_timer(info->p_esd_timeout_tmr);

	info->p_esd_timeout_tmr = NULL;
	init_timer(&(info->esd_timeout_tmr));
	info->esd_timeout_tmr.data = (unsigned long)(info);
	info->esd_timeout_tmr.function = esd_timeout_handler;
	info->esd_timeout_tmr.expires = jiffies + (HZ * sec);
	info->p_esd_timeout_tmr = &info->esd_timeout_tmr;
	add_timer(&info->esd_timeout_tmr);
	mutex_unlock(&info->prob_int_sync);
}

static void esd_timer_stop(struct bt532_ts_info *info)
{
	if (info->p_esd_timeout_tmr)
		del_timer(info->p_esd_timeout_tmr);

	info->p_esd_timeout_tmr = NULL;
}

static void ts_tmr_work(struct work_struct *work)
{
	struct bt532_ts_info *info =
				container_of(work, struct bt532_ts_info, tmr_work);
	struct i2c_client *client = info->client;

	printk(KERN_INFO "%s\n",__func__);

	if(down_trylock(&info->work_lock)) {
		dev_err(&client->dev, "%s: Failed to occupy work lock\n", __func__);
		esd_timer_start(CHECK_ESD_TIMER, info);

		return;
	}

	if (info->work_state != NOTHING) {
		dev_err(&client->dev, "%s: Other process occupied (%d)\n",
			__func__, info->work_state);
		up(&info->work_lock);

		return;
	}
	info->work_state = ESD_TIMER;

	disable_irq(info->irq);
	bt532_power_control(info, POWER_OFF);
	bt532_power_control(info, POWER_ON_SEQUENCE);

	clear_report_data(info);
	if (mini_init_touch(info) == false)
		goto fail_time_out_init;

	info->work_state = NOTHING;
	enable_irq(info->irq);
	up(&info->work_lock);
#if defined(TSP_VERBOSE_DEBUG)
	dev_info(&client->dev, "tmr queue work\n");
#endif

	return;

fail_time_out_init:
	dev_err(&client->dev, "Failed to restart\n");
	esd_timer_start(CHECK_ESD_TIMER, info);
	info->work_state = NOTHING;
	enable_irq(info->irq);
	up(&info->work_lock);

	return;
}
#endif

static bool bt532_power_sequence(struct bt532_ts_info *info)
{
	struct i2c_client *client = info->client;
	int retry = 0;

retry_power_sequence:
	if (write_reg(client, 0xc000, 0x0001) != I2C_SUCCESS)
		goto fail_power_sequence;

	udelay(10);

	if (write_cmd(client, 0xc004) != I2C_SUCCESS)
		goto fail_power_sequence;

	udelay(10);

	if (write_reg(client, 0xc002, 0x0001) != I2C_SUCCESS)
		goto fail_power_sequence;

	mdelay(2);

	if (write_reg(client, 0xc001, 0x0001) != I2C_SUCCESS)
		goto fail_power_sequence;

	msleep(FIRMWARE_ON_DELAY);	/* wait for checksum cal */

	return true;

fail_power_sequence:
	if (retry++ < 3) {
		msleep(CHIP_ON_DELAY);
		dev_err(&client->dev, "retry = %d\n", retry);

		goto retry_power_sequence;
	}

	dev_err(&client->dev, "Failed to send power sequence\n");

	return false;
}

static bool bt532_power_control(struct bt532_ts_info *info, u8 ctl)
{
	printk(KERN_INFO "%s\n",__func__);

	if (ctl == POWER_OFF) {
		//info->pdata->vdd_on(info->pdata, 0);
		zinitix_vdd_on(info, 0);
		msleep(CHIP_OFF_DELAY);
	} else if (ctl == POWER_ON_SEQUENCE) {
		//info->pdata->vdd_on(info->pdata, 1);
		zinitix_vdd_on(info, 1);
		msleep(CHIP_ON_DELAY);

		//zxt power on sequence
		return bt532_power_sequence(info);
	} else if (ctl == POWER_ON) {
		//info->pdata->vdd_on(info->pdata, 1);
		zinitix_vdd_on(info, 1);
	}

	return true;
}

#if TOUCH_ONESHOT_UPGRADE
static bool ts_check_need_upgrade(struct bt532_ts_info *info,
		u16 version, u16 minor_version, u16 reg_version, u16 hw_id)
{
	struct i2c_client *client = info->client;
	u16	new_version;
	u16	new_minor_version;
	u16	new_reg_version;
	u16	new_hw_id;

	new_version = (u16) (m_firmware_data[52] | (m_firmware_data[53]<<8));
	new_minor_version = (u16) (m_firmware_data[56] | (m_firmware_data[57]<<8));
	new_reg_version = (u16) (m_firmware_data[60] | (m_firmware_data[61]<<8));

	new_hw_id = (u16) (m_firmware_data[0x6b12] | (m_firmware_data[0x6b13]<<8));

	printk(KERN_INFO " %s : %d \n", __func__, __LINE__);

#if CHECK_HWID
	dev_err(&client->dev, "HW_ID = 0x%x, new HW_ID = 0x%x\n",
				hw_id, new_hw_id);
	if (hw_id != new_hw_id)
		return false;
#endif

#if defined(TSP_VERBOSE_DEBUG)
	dev_err(&client->dev, "version = 0x%x, new version = 0x%x\n",
				version, new_version);
#endif
	if (version < new_version)
		return true;
	else if (version > new_version)
		return false;

#if defined(TSP_VERBOSE_DEBUG)
	dev_err(&client->dev, "minor version = 0x%x, new minor version = 0x%x\n",
				minor_version, new_minor_version);
#endif
	if (minor_version < new_minor_version)
		return true;
	else if (minor_version > new_minor_version)
		return false;

	dev_err(&client->dev, "reg data version = 0x%x, new reg data"
				" version = 0x%x\n", reg_version, new_reg_version);
	if (reg_version < new_reg_version)
		return true;

	return false;
}
#endif

#define TC_SECTOR_SZ		8

static u8 ts_upgrade_firmware(struct bt532_ts_info *info,
	const u8 *firmware_data, u32 size)
{
	struct bt532_ts_platform_data *pdata = info->pdata;
	struct i2c_client *client = info->client;
	u16 flash_addr;
	u8 *verify_data;
	int retry_cnt = 0;
	int i;

	verify_data = kzalloc(size, GFP_KERNEL);
	if (verify_data == NULL) {
		dev_err(&client->dev, "%s: Failed to allocate memory\n", __func__);

		return false;
	}

retry_upgrade:
	bt532_power_control(info, POWER_OFF);
	bt532_power_control(info, POWER_ON);
	mdelay(100);

	if (write_reg(client, 0xc000, 0x0001) != I2C_SUCCESS)
		goto fail_upgrade;

	udelay(10);

	if (write_cmd(client, 0xc004) != I2C_SUCCESS)
		goto fail_upgrade;

	udelay(10);

	if (write_reg(client, 0xc002, 0x0001) != I2C_SUCCESS)
		goto fail_upgrade;

	mdelay(5);

#if defined(TSP_VERBOSE_DEBUG)
	dev_err(&client->dev, "init flash\n");
#endif

	if (write_reg(client, 0xc003, 0x0001) != I2C_SUCCESS)
		goto fail_upgrade;


	if (write_reg(client, 0xc104, 0x0001) != I2C_SUCCESS)
		goto fail_upgrade;


	if (write_cmd(client, BT532_INIT_FLASH) != I2C_SUCCESS) {
		dev_err(&client->dev, "Failed to init flash\n");

		goto fail_upgrade;
	}

	dev_err(&client->dev, "Write firmware data\n");
	for (flash_addr = 0; flash_addr < size; ) {
		for (i = 0; i < pdata->page_size / TC_SECTOR_SZ; i++) {
			/*zinitix_debug_msg("write :addr=%04x, len=%d\n",
							flash_addr, TC_SECTOR_SZ);*/
			if (write_data(client, BT532_WRITE_FLASH,
				(u8 *)&firmware_data[flash_addr],TC_SECTOR_SZ) < 0) {
				dev_err(&client->dev, "Failed to write firmare\n");

				goto fail_upgrade;
			}
			flash_addr += TC_SECTOR_SZ;
			udelay(100);
		}

		mdelay(30); /*for fuzing delay*/
	}

	if (write_reg(client, 0xc003, 0x0000) != I2C_SUCCESS)
		goto fail_upgrade;


	if (write_reg(client, 0xc104, 0x0000) != I2C_SUCCESS)
		goto fail_upgrade;


#if defined(TSP_VERBOSE_DEBUG)
	dev_err(&client->dev, "init flash\n");
#endif

	if (write_cmd(client, BT532_INIT_FLASH) != I2C_SUCCESS) {
		dev_err(&client->dev, "Failed to init flash\n");

		goto fail_upgrade;
	}

	dev_err(&client->dev, "Read firmware data\n");

	for (flash_addr = 0; flash_addr < size; ) {
		for (i = 0; i < pdata->page_size / TC_SECTOR_SZ; i++) {
			/*zinitix_debug_msg("read :addr=%04x, len=%d\n",
							flash_addr, TC_SECTOR_SZ);*/
			if (read_firmware_data(client, BT532_READ_FLASH,
				(u8*)&verify_data[flash_addr], TC_SECTOR_SZ) < 0) {
				dev_err(&client->dev, "Failed to read firmare\n");

				goto fail_upgrade;
			}

			flash_addr += TC_SECTOR_SZ;
		}
	}
	/* verify */
	dev_err(&client->dev, "verify firmware data\n");
	if (memcmp((u8 *)&firmware_data[0], (u8 *)&verify_data[0], size) == 0) {
		dev_err(&client->dev, "upgrade finished\n");
		kfree(verify_data);
		bt532_power_control(info, POWER_OFF);
		bt532_power_control(info, POWER_ON_SEQUENCE);

		return true;
	}

fail_upgrade:
	bt532_power_control(info, POWER_OFF);

	if (retry_cnt++ < INIT_RETRY_CNT) {
		dev_err(&client->dev, "Failed to upgrade. retry (%d)\n", retry_cnt);

		goto retry_upgrade;
	}

	if (verify_data != NULL)
		kfree(verify_data);

	dev_err(&client->dev, "Failed to upgrade\n");

	return false;
}

static bool ts_hw_calibration(struct bt532_ts_info *info)
{
	struct i2c_client *client = info->client;
	u16	chip_eeprom_info;
	int time_out = 0;

	if (write_reg(client, BT532_TOUCH_MODE, 0x07) != I2C_SUCCESS)
		return false;

	mdelay(10);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
	mdelay(10);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
	mdelay(50);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
	mdelay(10);

	if (write_cmd(client, BT532_CALIBRATE_CMD) != I2C_SUCCESS)
		return false;

	if (write_cmd(client, BT532_CLEAR_INT_STATUS_CMD) != I2C_SUCCESS)
		return false;

	mdelay(10);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);

	/* wait for h/w calibration*/
	do {
		mdelay(500);
		write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);

		if (read_data(client, BT532_EEPROM_INFO_REG,
			(u8 *)&chip_eeprom_info, 2) < 0)
			return false;

#if defined(TSP_VERBOSE_DEBUG)
		dev_info(&client->dev, "touch eeprom info = 0x%04X\n",
			chip_eeprom_info);
#endif
		if (!zinitix_bit_test(chip_eeprom_info, 0))
			break;

		if(time_out++ == 4){
			write_cmd(client, BT532_CALIBRATE_CMD);
			mdelay(10);
			write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
			dev_err(&client->dev, "h/w calibration retry timeout.\n");
		}

		if(time_out++ > 10){
			dev_err(&client->dev, "h/w calibration timeout.\n");
			break;
		}
	} while (1);

	if (write_reg(client, BT532_TOUCH_MODE, TOUCH_POINT_MODE) != I2C_SUCCESS)
		return false;

	if (info->cap_info.ic_int_mask != 0) {
		if (write_reg(client, BT532_INT_ENABLE_FLAG,
			info->cap_info.ic_int_mask) != I2C_SUCCESS)
			return false;
	}

	write_reg(client, 0xc003, 0x0001);
	write_reg(client, 0xc104, 0x0001);
	udelay(100);
	if (write_cmd(client, BT532_SAVE_CALIBRATION_CMD) != I2C_SUCCESS)
		return false;

	mdelay(1000);
	write_reg(client, 0xc003, 0x0000);
	write_reg(client, 0xc104, 0x0000);

	return true;
}

static bool init_touch(struct bt532_ts_info *info, bool force)
{
	struct bt532_ts_platform_data *pdata = info->pdata;
	struct i2c_client *client = info->client;
	struct capa_info *cap = &(info->cap_info);
	u16 reg_val;
	int i;
	u16 chip_eeprom_info;
#if USE_CHECKSUM
	u16 chip_check_sum;
	u8 checksum_err;
#endif
	int retry_cnt = 0;

retry_init:

	printk(KERN_INFO " %s, %d start\n", __func__, __LINE__);
	
	for(i = 0; i < INIT_RETRY_CNT; i++) {
		if (read_data(client, BT532_EEPROM_INFO_REG,
						(u8 *)&chip_eeprom_info, 2) < 0) {
			dev_err(&client->dev, "Failed to read eeprom info(%d)\n", i);
			mdelay(10);
			continue;
		} else
			break;
	}

	if (i == INIT_RETRY_CNT)
		goto fail_init;

#if USE_CHECKSUM
	checksum_err = 0;

	for (i = 0; i < INIT_RETRY_CNT; i++) {
		if (read_data(client, BT532_CHECKSUM_RESULT,
						(u8 *)&chip_check_sum, 2) < 0) {
			mdelay(10);
			continue;
		}

#if defined(TSP_VERBOSE_DEBUG)
		dev_err(&client->dev, "fw checksum: 0x%04X\n", chip_check_sum);
#endif

		if(chip_check_sum == 0x55aa)
			break;
		else {
			checksum_err = 1;
			break;
		}
	}

	if (i == INIT_RETRY_CNT || checksum_err) {
		dev_err(&client->dev, "Failed to check firmware data\n");
		if(checksum_err == 1 && retry_cnt < INIT_RETRY_CNT)
			retry_cnt = INIT_RETRY_CNT;

		goto fail_init;
	}
#endif

	if (write_cmd(client, BT532_SWRESET_CMD) != I2C_SUCCESS)
		goto fail_init;

	cap->button_num = SUPPORTED_BUTTON_NUM;

	reg_val = 0;
	zinitix_bit_set(reg_val, BIT_PT_CNT_CHANGE);
	zinitix_bit_set(reg_val, BIT_DOWN);
	zinitix_bit_set(reg_val, BIT_MOVE);
	zinitix_bit_set(reg_val, BIT_UP);
#if (TOUCH_POINT_MODE == 2)
	zinitix_bit_set(reg_val, BIT_PALM);
	zinitix_bit_set(reg_val, BIT_PALM_REJECT);
#endif

	if (cap->button_num > 0)
		zinitix_bit_set(reg_val, BIT_ICON_EVENT);

	cap->ic_int_mask = reg_val;

	if (write_reg(client, BT532_INT_ENABLE_FLAG, 0x0) != I2C_SUCCESS)
		goto fail_init;

	if (write_cmd(client, BT532_SWRESET_CMD) != I2C_SUCCESS)
		goto fail_init;

	/* get chip information */
	if (read_data(client, BT532_VENDOR_ID, (u8 *)&cap->vendor_id, 2) < 0)
		goto fail_init;


	if (read_data(client, BT532_CHIP_REVISION,
					(u8 *)&cap->ic_revision, 2) < 0)
		goto fail_init;

	printk(KERN_INFO " %s, veid:%d, icver:%d\n", __func__, cap->vendor_id, cap->ic_revision);


	cap->ic_fw_size = 32 * 1024;

	if (read_data(client, BT532_HW_ID, (u8 *)&cap->hw_id, 2) < 0)
		goto fail_init;

	if (read_data(client, BT532_THRESHOLD, (u8 *)&cap->threshold, 2) < 0)
		goto fail_init;

	if (read_data(client, BT532_BUTTON_SENSITIVITY,
					(u8 *)&cap->key_threshold, 2) < 0)
		goto fail_init;

/*	if (read_data(client, BT532_DUMMY_BUTTON_SENSITIVITY,
					(u8 *)&cap->dummy_threshold, 2) < 0)
		goto fail_init;
*/
	if (read_data(client, BT532_TOTAL_NUMBER_OF_X,
					(u8 *)&cap->x_node_num, 2) < 0)
		goto fail_init;

	if (read_data(client, BT532_TOTAL_NUMBER_OF_Y,
					(u8 *)&cap->y_node_num, 2) < 0)
		goto fail_init;

	cap->total_node_num = cap->x_node_num * cap->y_node_num;

	if (read_data(client, BT532_AFE_FREQUENCY,
					(u8 *)&cap->afe_frequency, 2) < 0)
		goto fail_init;

	//--------------------------------------------------------

	/* get chip firmware version */
	if (read_data(client, BT532_FIRMWARE_VERSION,
					(u8 *)&cap->fw_version, 2) < 0)
		goto fail_init;

	if (read_data(client, BT532_MINOR_FW_VERSION,
					(u8 *)&cap->fw_minor_version, 2) < 0)
		goto fail_init;

	if (read_data(client, BT532_DATA_VERSION_REG,
					(u8 *)&cap->reg_data_version, 2) < 0)
		goto fail_init;

	printk(KERN_INFO " %s, hwid:%d, fwver:%d\n", __func__, cap->hw_id, cap->fw_version);

#if TOUCH_ONESHOT_UPGRADE
	if (!force) {
		
		printk(KERN_INFO " updating : 0 \n");
		
		if (ts_check_need_upgrade(info, cap->fw_version,
				cap->fw_minor_version, cap->reg_data_version,
				cap->hw_id) == true) {

			printk(KERN_INFO " updating : 1 \n");

			if(ts_upgrade_firmware(info, m_firmware_data,
				cap->ic_fw_size) == false)
				goto fail_init;

			printk(KERN_INFO " updating : 2 \n");

			if(ts_hw_calibration(info) == false)
				goto fail_init;

			printk(KERN_INFO " updating : 3 \n");

			/* disable chip interrupt */
			if (write_reg(client, BT532_INT_ENABLE_FLAG, 0) != I2C_SUCCESS)
				goto fail_init;

			printk(KERN_INFO " updating : 4 \n");

			/* get chip firmware version */
			if (read_data(client, BT532_FIRMWARE_VERSION,
							(u8 *)&cap->fw_version, 2) < 0)
				goto fail_init;

			printk(KERN_INFO " updating : 5 \n");

			if (read_data(client, BT532_MINOR_FW_VERSION,
							(u8 *)&cap->fw_minor_version, 2) < 0)
				goto fail_init;

			printk(KERN_INFO " updating : 6 \n");

			if (read_data(client, BT532_DATA_VERSION_REG,
							(u8 *)&cap->reg_data_version, 2) < 0)
				goto fail_init;
			
			printk(KERN_INFO " updating : 7 -end \n");
		}
	}
#endif

	if (read_data(client, BT532_EEPROM_INFO_REG,
					(u8 *)&chip_eeprom_info, 2) < 0)
		goto fail_init;

	if (zinitix_bit_test(chip_eeprom_info, 0)) { /* hw calibration bit*/
		if(ts_hw_calibration(info) == false)
			goto fail_init;

		/* disable chip interrupt */
		if (write_reg(client, BT532_INT_ENABLE_FLAG, 0) != I2C_SUCCESS)
			goto fail_init;
	}

	/* initialize */
	if (write_reg(client, BT532_X_RESOLUTION,
					(u16)pdata->x_resolution) != I2C_SUCCESS)
		goto fail_init;

	if (write_reg(client, BT532_Y_RESOLUTION,
					(u16)pdata->y_resolution) != I2C_SUCCESS)
		goto fail_init;

	cap->MinX = (u32)0;
	cap->MinY = (u32)0;
	cap->MaxX = (u32)pdata->x_resolution;
	cap->MaxY = (u32)pdata->y_resolution;

	if (write_reg(client, BT532_BUTTON_SUPPORTED_NUM,
					(u16)cap->button_num) != I2C_SUCCESS)
		goto fail_init;

	if (write_reg(client, BT532_SUPPORTED_FINGER_NUM,
					(u16)MAX_SUPPORTED_FINGER_NUM) != I2C_SUCCESS)
		goto fail_init;

	cap->multi_fingers = MAX_SUPPORTED_FINGER_NUM;
	cap->gesture_support = 0;

	if (write_reg(client, BT532_INITIAL_TOUCH_MODE,
					TOUCH_POINT_MODE) != I2C_SUCCESS)
		goto fail_init;

	if (write_reg(client, BT532_TOUCH_MODE, info->touch_mode) != I2C_SUCCESS)
		goto fail_init;

	if (write_reg(client, BT532_INT_ENABLE_FLAG,
					cap->ic_int_mask) != I2C_SUCCESS)
		goto fail_init;

	/* soft calibration */
	if (write_cmd(client, BT532_CALIBRATE_CMD) != I2C_SUCCESS)
		goto fail_init;

#ifdef DEBUG_I2C_CHECKSUM
	if (read_data(client, ZINITIX_INTERNAL_FLAG_02,
			(u8 *)&reg_val, 2) < 0)
			goto fail_init;

	cap->i2s_checksum = !(!zinitix_bit_test(reg_val, 15));
	zinitix_printk("use i2s checksum = %d\r\n", cap->i2s_checksum);
#endif

	/* read garbage data */
	for (i = 0; i < 10; i++) {
		write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
		udelay(10);
	}

	if (info->touch_mode != TOUCH_POINT_MODE) { /* Test Mode */
		if (write_reg(client, BT532_DELAY_RAW_FOR_HOST,
						RAWDATA_DELAY_FOR_HOST) != I2C_SUCCESS) {
			dev_err(&client->dev, "%s: Failed to set DELAY_RAW_FOR_HOST\n",
						__func__);

			goto fail_init;
		}
	}
#if ESD_TIMER_INTERVAL
	if (write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
			SCAN_RATE_HZ * ESD_TIMER_INTERVAL) != I2C_SUCCESS)
		goto fail_init;
#endif

	if (write_reg(client, 0x003e,	1) != I2C_SUCCESS)
		goto fail_init;
	if (write_reg(client, 0x003f,	40) != I2C_SUCCESS)
		goto fail_init;
	if (write_reg(client, 0x00c6,	70) != I2C_SUCCESS)
		goto fail_init;
	if (write_reg(client, 0x00c7,	20) != I2C_SUCCESS)
		goto fail_init;
	if (write_reg(client, 0x00d4,	3000) != I2C_SUCCESS)
		goto fail_init;
	if (write_reg(client, 0x0033,	0x0103) != I2C_SUCCESS)
		goto fail_init;
	printk(KERN_INFO " %s, %d end \n", __func__, __LINE__);

	return true;

fail_init:

	
	printk(KERN_INFO " %s, %d fail \n", __func__, __LINE__);

	if (++retry_cnt <= INIT_RETRY_CNT) {
		bt532_power_control(info, POWER_OFF);
		bt532_power_control(info, POWER_ON_SEQUENCE);

		goto retry_init;

	} else if(retry_cnt == INIT_RETRY_CNT+1) {
		cap->ic_fw_size = 32*1024;

#if TOUCH_FORCE_UPGRADE
		if (ts_upgrade_firmware(info, m_firmware_data,
						cap->ic_fw_size) == false) {
			dev_err(&client->dev, "Failed to upgrade\n");

			return false;
		}
		mdelay(100);

		// hw calibration and make checksum
		if(ts_hw_calibration(info) == false)
			return false;

		goto retry_init;
#endif
	}

	dev_err(&client->dev, "Failed to initiallize\n");

	return false;
}

static bool mini_init_touch(struct bt532_ts_info *info)
{
	struct bt532_ts_platform_data *pdata = info->pdata;
	struct i2c_client *client = info->client;
	int i;
#if USE_CHECKSUM
	u16 chip_check_sum;

	if (read_data(client, BT532_CHECKSUM_RESULT,
					(u8 *)&chip_check_sum, 2) < 0)
		goto fail_mini_init;

	if(chip_check_sum != 0x55aa) {
		dev_err(&client->dev, "%s: Failed to check firmware data\n",
				__func__);

		goto fail_mini_init;
	}
#endif

	if (write_cmd(client, BT532_SWRESET_CMD) != I2C_SUCCESS)
		goto fail_mini_init;


	/* initialize */
	if (write_reg(client, BT532_X_RESOLUTION,
					(u16)(pdata->x_resolution)) != I2C_SUCCESS)
		goto fail_mini_init;

	if (write_reg(client,BT532_Y_RESOLUTION,
					(u16)(pdata->y_resolution)) != I2C_SUCCESS)
		goto fail_mini_init;

	if (write_reg(client, BT532_BUTTON_SUPPORTED_NUM,
					(u16)info->cap_info.button_num) != I2C_SUCCESS)
		goto fail_mini_init;

	if (write_reg(client, BT532_SUPPORTED_FINGER_NUM,
					(u16)MAX_SUPPORTED_FINGER_NUM) != I2C_SUCCESS)
		goto fail_mini_init;

	if (write_reg(client, BT532_INITIAL_TOUCH_MODE,
					TOUCH_POINT_MODE) != I2C_SUCCESS)
		goto fail_mini_init;

	if (write_reg(client, BT532_TOUCH_MODE, info->touch_mode) != I2C_SUCCESS)
		goto fail_mini_init;

	if (write_reg(client, BT532_INT_ENABLE_FLAG,
					info->cap_info.ic_int_mask) != I2C_SUCCESS)
		goto fail_mini_init;

	/* soft calibration */
	if (write_cmd(client, BT532_CALIBRATE_CMD) != I2C_SUCCESS)
		goto fail_mini_init;

	/* read garbage data */
	for (i = 0; i < 10; i++) {
		write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
		udelay(10);
	}

	if (info->touch_mode != TOUCH_POINT_MODE) {
		if (write_reg(client, BT532_DELAY_RAW_FOR_HOST,
						RAWDATA_DELAY_FOR_HOST) != I2C_SUCCESS)
			goto fail_mini_init;
	}

#if ESD_TIMER_INTERVAL
	if (write_reg(client, BT532_PERIODICAL_INTERRUPT_INTERVAL,
			SCAN_RATE_HZ * ESD_TIMER_INTERVAL) != I2C_SUCCESS)
		goto fail_mini_init;

	esd_timer_start(CHECK_ESD_TIMER, info);
#endif

	if (write_reg(client, 0x003e,	1) != I2C_SUCCESS)
		goto fail_mini_init;
	if (write_reg(client, 0x003f,	40) != I2C_SUCCESS)
		goto fail_mini_init;
	if (write_reg(client, 0x00c6,	70) != I2C_SUCCESS)
		goto fail_mini_init;
	if (write_reg(client, 0x00c7,	20) != I2C_SUCCESS)
		goto fail_mini_init;
	if (write_reg(client, 0x00d4,	3000) != I2C_SUCCESS)
		goto fail_mini_init;
	if (write_reg(client, 0x0033,	0x0103) != I2C_SUCCESS)
		goto fail_mini_init;
	return true;

fail_mini_init:
	
	printk(KERN_INFO "%s, fail mini init \n",__func__);

	bt532_power_control(info, POWER_OFF);
	bt532_power_control(info, POWER_ON_SEQUENCE);

	if(init_touch(info, false) == false) {
		dev_err(&client->dev, "Failed to initialize\n");

		return false;
	}

#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, info);
#endif
	return true;
}

#ifdef TOUCH_BOOSTER_DVFS
static void zinitix_change_dvfs_lock(struct work_struct *work)
{
	struct bt532_ts_info *info =
		container_of(work,
			struct bt532_ts_info, work_dvfs_chg.work);
	int retval = 0;

	mutex_lock(&info->dvfs_lock);

	if (info->dvfs_boost_mode == DVFS_STAGE_DUAL) {
                if (info->stay_awake) {
                        dev_info(&info->client->dev,
                                        "%s: do fw update, do not change cpu frequency.\n",
                                        __func__);
                } else {
                        retval = set_freq_limit(DVFS_TOUCH_ID,
                                        MIN_TOUCH_LIMIT_SECOND);
                        info->dvfs_freq = MIN_TOUCH_LIMIT_SECOND;
                }
        } else if (info->dvfs_boost_mode == DVFS_STAGE_SINGLE ||
                        info->dvfs_boost_mode == DVFS_STAGE_TRIPLE) {
                retval = set_freq_limit(DVFS_TOUCH_ID, -1);
                info->dvfs_freq = -1;
        }

        if (retval < 0)
                dev_err(&info->client->dev,
                                "%s: booster change failed(%d).\n",
                                __func__, retval);

	mutex_unlock(&info->dvfs_lock);
}

static void zinitix_set_dvfs_off(struct work_struct *work)
{
	struct bt532_ts_info *info =
		container_of(work,
			struct bt532_ts_info, work_dvfs_off.work);
	int retval;

	if (info->stay_awake) {
                dev_info(&info->client->dev,
                                "%s: do fw update, do not change cpu frequency.\n",
                                __func__);
        } else {
                mutex_lock(&info->dvfs_lock);

                retval = set_freq_limit(DVFS_TOUCH_ID, -1);
                info->dvfs_freq = -1;

                if (retval < 0)
                        dev_err(&info->client->dev,
                                        "%s: booster stop failed(%d).\n",
                                        __func__, retval);
                info->dvfs_lock_status = false;

                mutex_unlock(&info->dvfs_lock);
        }

	/*mutex_lock(&info->dvfs_lock);
	retval = set_freq_limit(DVFS_TOUCH_ID, -1);
	if (retval < 0)
		dev_info(&info->client->dev,
			"%s: booster stop failed(%d).\n",
			__func__, retval);

	info->dvfs_lock_status = false;
	mutex_unlock(&info->dvfs_lock);*/
}

static void zinitix_set_dvfs_lock(struct bt532_ts_info *info,
					s32 on)
{
	int ret = 0;

	if (info->dvfs_boost_mode == DVFS_STAGE_NONE) {
                dev_dbg(&info->client->dev,
                                "%s: DVFS stage is none(%d)\n",
                                __func__, info->dvfs_boost_mode);
                return;
        }

        mutex_lock(&info->dvfs_lock);
        if (on == 0) {
                if (info->dvfs_lock_status)
                        schedule_delayed_work(&info->work_dvfs_off,
                                        msecs_to_jiffies(TOUCH_BOOSTER_OFF_TIME));
        } else if (on > 0) {
                cancel_delayed_work(&info->work_dvfs_off);

                if ((!info->dvfs_lock_status) || (info->dvfs_old_stauts < on)) {
                        cancel_delayed_work(&info->work_dvfs_chg);

                        if (info->dvfs_freq != MIN_TOUCH_LIMIT) {
                                if (info->dvfs_boost_mode == DVFS_STAGE_TRIPLE)
                                        ret = set_freq_limit(DVFS_TOUCH_ID,
                                                MIN_TOUCH_LIMIT_SECOND);
                                else
                                        ret = set_freq_limit(DVFS_TOUCH_ID,
                                                MIN_TOUCH_LIMIT);
                                info->dvfs_freq = MIN_TOUCH_LIMIT;

                                if (ret < 0)
					dev_err(&info->client->dev,
                                                        "%s: cpu first lock failed(%d)\n",
                                                        __func__, ret);
                        }
			schedule_delayed_work(&info->work_dvfs_chg,
				msecs_to_jiffies(TOUCH_BOOSTER_CHG_TIME));

                        info->dvfs_lock_status = true;
                }
        } else if (on < 0) {
                if (info->dvfs_lock_status) {
                        cancel_delayed_work(&info->work_dvfs_off);
                        cancel_delayed_work(&info->work_dvfs_chg);
                        schedule_work(&info->work_dvfs_off.work);
                }
        }
        info->dvfs_old_stauts = on;
        mutex_unlock(&info->dvfs_lock);

	/*mutex_lock(&info->dvfs_lock);
	if (on == 0) {
		if (info->dvfs_lock_status) {
			schedule_delayed_work(&info->work_dvfs_off,
				msecs_to_jiffies(TOUCH_BOOSTER_OFF_TIME));
		}
	} else if (on == 1) {
		cancel_delayed_work(&info->work_dvfs_off);
		if (!info->dvfs_lock_status) {
			ret = set_freq_limit(DVFS_TOUCH_ID,
					MIN_TOUCH_LIMIT);
			if (ret < 0)
				dev_info(&info->client->dev,
					"%s: cpu first lock failed(%d)\n",
					__func__, ret);

			schedule_delayed_work(&info->work_dvfs_chg,
				msecs_to_jiffies(TOUCH_BOOSTER_CHG_TIME));
			info->dvfs_lock_status = true;
		}
	} else if (on == 2) {
		if (info->dvfs_lock_status) {
			cancel_delayed_work(&info->work_dvfs_off);
			cancel_delayed_work(&info->work_dvfs_chg);
			schedule_work(&info->work_dvfs_off.work);
		}
	}
	mutex_unlock(&info->dvfs_lock);*/
}


static void zinitix_init_dvfs(struct bt532_ts_info *info)
{
	mutex_init(&info->dvfs_lock);

	info->dvfs_boost_mode = DVFS_STAGE_DUAL;

	INIT_DELAYED_WORK(&info->work_dvfs_off, zinitix_set_dvfs_off);
	INIT_DELAYED_WORK(&info->work_dvfs_chg, zinitix_change_dvfs_lock);

	info->dvfs_lock_status = false;
}
#endif

static void clear_report_data(struct bt532_ts_info *info)
{
	struct i2c_client *client = info->client;
	int i;
	u8 reported = 0;
	u8 sub_status;

	for (i = 0; i < info->cap_info.button_num; i++) {
		if (info->button[i] == ICON_BUTTON_DOWN) {
			info->button[i] = ICON_BUTTON_UP;
			input_report_key(info->input_dev, BUTTON_MAPPING_KEY[i], 0);
			reported = true;
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
			dev_info(&client->dev, "Button up = %d\n", i);
#endif
		}
	}

	for (i = 0; i < info->cap_info.multi_fingers; i++) {
		sub_status = info->reported_touch_info.coord[i].sub_status;
		if (zinitix_bit_test(sub_status, SUB_BIT_EXIST)) {
			input_mt_slot(info->input_dev, i);
			input_mt_report_slot_state(info->input_dev,	MT_TOOL_FINGER, 0);
			reported = true;
			dev_info(&client->dev, "Finger [%02d] up\n", i);
		}
		info->reported_touch_info.coord[i].sub_status = 0;
	}

	if (reported) {
		input_sync(info->input_dev);
	}

#ifdef TOUCH_BOOSTER_DVFS
	info->finger_cnt1=0;
	/*zinitix_set_dvfs_lock(info, info->finger_cnt1);*/
	zinitix_set_dvfs_lock(info, -1);
#endif
	
}
#if 0
static void zinitix_ta_cb(struct tsp_callbacks *cb, int ta_status)
{
	struct bt532_ts_info *info = misc_info;
	struct i2c_client *client = info->client;
	u16 val;

	dev_info(&client->dev,
		"%s: ta_status : %x\n", __func__, ta_status);

	info->ta_status = ta_status;
	val = ta_status;

	if (info->pdata->is_vdd_on() == 0) {
		/* ta_status 0: charger disconnected
		 * ta_status 1: charger connected
		 * ta_status 2: charger disconnected but tsp ic is power-off
		 * ta_status 3: charger connected but tsp ic is power-off */
		info->ta_status = ta_status ? 3 : 2;
		return;
	}

	switch (info->ta_status) {
	case 0:	/* TA detach */
		write_data(client, BT532_HOLD_POINT_THRESHOLD, (u8 *)&val, 2);
		write_cmd(client, BT532_SAVE_STATUS_CMD);
		break;
	case 1: /* TA attach */
		write_data(client, BT532_HOLD_POINT_THRESHOLD, (u8 *)&val, 2);
		write_cmd(client, BT532_SAVE_STATUS_CMD);
		break;
	}
}
#endif

#define	PALM_REPORT_WIDTH	200
#define	PALM_REJECT_WIDTH	255

static irqreturn_t bt532_touch_irq_handler(int irq, void *data)
{
	struct bt532_ts_info* info = (struct bt532_ts_info*)data;
	struct bt532_ts_platform_data *pdata = info->pdata;
	struct i2c_client *client = info->client;
	int i;
	u8 sub_status;
	u8 prev_sub_status;
	u16 prev_icon_data;
	u32 x, y, maxX, maxY;
	u32 w;
	u32 tmp;
#ifdef DEBUG_I2C_CHECKSUM
	u8 read_result = 1;
#endif
	u8 palm = 0;

	if (gpio_get_value(info->pdata->gpio_int)) {
		dev_err(&client->dev, "Invalid interrupt\n");

		return IRQ_HANDLED;
	}

	if (down_trylock(&info->work_lock)) {
		dev_err(&client->dev, "%s: Failed to occupy work lock\n", __func__);
		write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);

		return IRQ_HANDLED;
	}

	if (info->work_state != NOTHING) {
		dev_err(&client->dev, "%s: Other process occupied\n", __func__);
		udelay(DELAY_FOR_SIGNAL_DELAY);

		if (!gpio_get_value(info->pdata->gpio_int)) {
			write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
			udelay(DELAY_FOR_SIGNAL_DELAY);
		}

		goto out;
	}

	info->work_state = NORMAL;

#if ESD_TIMER_INTERVAL
	esd_timer_stop(info);
#endif

	if (ts_read_coord(info) == false || info->touch_info.status == 0xffff
		|| info->touch_info.status == 0x1) {
		// more retry
#ifdef DEBUG_I2C_CHECKSUM
		for(i=0; i< 50; i++) {	// about 30ms
			if(!(ts_read_coord(info) == false|| info->touch_info.status == 0xffff
			|| info->touch_info.status == 0x1))
				break;
		}

		if(i==50)
			read_result = 0;
	}
	if (!read_result) {
#endif
		dev_err(&client->dev, "Failed to read info coord\n");
		bt532_power_control(info, POWER_OFF);
		bt532_power_control(info, POWER_ON_SEQUENCE);

		clear_report_data(info);
		mini_init_touch(info);

		goto out;
	}

	/* invalid : maybe periodical repeated int. */
	if (info->touch_info.status == 0x0)
		goto out;

	if (zinitix_bit_test(info->touch_info.status, BIT_ICON_EVENT)) {
#ifndef DEBUG_I2C_CHECKSUM
		if (read_data(info->client, BT532_ICON_STATUS_REG,
			(u8 *)(&info->icon_event_reg), 2) < 0) {
			dev_err(&client->dev, "Failed to read button info\n");

			goto out;
		}
#endif
		prev_icon_data = info->icon_event_reg ^ info->prev_icon_event;

		for (i = 0; i < info->cap_info.button_num; i++) {
			if (zinitix_bit_test(info->icon_event_reg & prev_icon_data,
									(BIT_O_ICON0_DOWN + i))) {
				info->button[i] = ICON_BUTTON_DOWN;
				input_report_key(info->input_dev, BUTTON_MAPPING_KEY[i], 1);
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
				dev_info(&client->dev, "Button down = %d\n", i);
#endif
			#ifdef CONFIG_MACH_CRATERVE
				mutex_lock(&touchkey_sensi_lock);
				if(0 == touch_VAstate){
					if(read_data(client, BT532_BTN_WIDTH + i, (u8*)&touch_sensi[i], 2)<0)touch_sensi[i]=0;
					if (!zinitix_bit_test(info->icon_event_reg & prev_icon_data,
										(BIT_O_ICON0_DOWN + i)))touch_sensi[i]=0;
				}
				else touch_sensi[i]=0;
				schedule_delayed_work(&work_clr_sensi,CLR_SENSI_INVAL);
				mutex_unlock(&touchkey_sensi_lock);
			#endif
			}
		}

		for (i = 0; i < info->cap_info.button_num; i++) {
			if (zinitix_bit_test(info->icon_event_reg & prev_icon_data,
									(BIT_O_ICON0_UP + i))) {
				info->button[i] = ICON_BUTTON_UP;
				input_report_key(info->input_dev, BUTTON_MAPPING_KEY[i], 0);
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
				dev_info(&client->dev, "Button up = %d\n", i);
#endif
			}
		}

		info->prev_icon_event = info->icon_event_reg;
	}

	if (zinitix_bit_test(info->touch_info.status, BIT_PALM)) {
		dev_info(&client->dev, "Palm report\n");
		palm = 1;
	}

	if (zinitix_bit_test(info->touch_info.status, BIT_PALM_REJECT)){
		dev_info(&client->dev, "Palm reject\n");
		palm = 1;
	}

	for (i = 0; i < info->cap_info.multi_fingers; i++) {
		sub_status = info->touch_info.coord[i].sub_status;
		prev_sub_status = info->reported_touch_info.coord[i].sub_status;

		if (zinitix_bit_test(sub_status, SUB_BIT_EXIST)) {
			x = info->touch_info.coord[i].x;
			y = info->touch_info.coord[i].y;
			w = info->touch_info.coord[i].width;

			 /* transformation from touch to screen orientation */
			if (pdata->orientation & TOUCH_V_FLIP)
				y = info->cap_info.MaxY
					+ info->cap_info.MinY - y;

			if (pdata->orientation & TOUCH_H_FLIP)
				x = info->cap_info.MaxX
					+ info->cap_info.MinX - x;

			maxX = info->cap_info.MaxX;
			maxY = info->cap_info.MaxY;

			if (pdata->orientation & TOUCH_XY_SWAP) {
				zinitix_swap_v(x, y, tmp);
				zinitix_swap_v(maxX, maxY, tmp);
			}

			if (x > maxX || y > maxY) {
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
				dev_err(&client->dev,
							"Invalid coord %d : x=%d, y=%d\n", i, x, y);
#endif
				continue;
			}

			info->touch_info.coord[i].x = x;
			info->touch_info.coord[i].y = y;

			if (w == 0)
				w = 1;

			input_mt_slot(info->input_dev, i);
			input_mt_report_slot_state(info->input_dev, MT_TOOL_FINGER, 1);

#if (TOUCH_POINT_MODE == 2)
			if (palm == 0) {
				if(w >= PALM_REPORT_WIDTH)
					w = PALM_REPORT_WIDTH - 10;
			} else if (palm == 1) {	//palm report
				w = PALM_REPORT_WIDTH;
//				info->touch_info.coord[i].minor_width = PALM_REPORT_WIDTH;
			} else if (palm == 2){	// palm reject
//				x = y = 0;
				w = PALM_REJECT_WIDTH;
//				info->touch_info.coord[i].minor_width = PALM_REJECT_WIDTH;
			}
#endif

			input_report_abs(info->input_dev, ABS_MT_TOUCH_MAJOR, (u32)w);
			input_report_abs(info->input_dev, ABS_MT_PRESSURE, (u32)w);
			input_report_abs(info->input_dev, ABS_MT_WIDTH_MAJOR,
								(u32)((palm == 1)?w-40:w));
#if (TOUCH_POINT_MODE == 2)
			input_report_abs(info->input_dev,
				ABS_MT_TOUCH_MINOR, (u32)info->touch_info.coord[i].minor_width);
//			input_report_abs(info->input_dev,
//				ABS_MT_WIDTH_MINOR, (u32)info->touch_info.coord[i].minor_width);
			input_report_abs(info->input_dev, ABS_MT_ANGLE,
						(palm > 1)?70:info->touch_info.coord[i].angle - 90);
//			dev_info(&client->dev, "finger [%02d] angle = %03d\n", i,
//						info->touch_info.coord[i].angle);
			input_report_abs(info->input_dev, ABS_MT_PALM, (palm > 0)?1:0);
//			input_report_abs(info->input_dev, ABS_MT_PALM, 1);
#endif

			input_report_abs(info->input_dev, ABS_MT_POSITION_X, x);
			input_report_abs(info->input_dev, ABS_MT_POSITION_Y, y);

			if (zinitix_bit_test(sub_status, SUB_BIT_DOWN)) {
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
				dev_info(&client->dev, "Finger [%02d] x = %d, y = %d,"
							" w = %d\n", i, x, y, w);
#else
				dev_info(&client->dev, "Finger [%02d] down\n", i);
#endif

#ifdef TOUCH_BOOSTER_DVFS
				info->finger_cnt1++;
				/*zinitix_set_dvfs_lock(info, !!info->finger_cnt1);*/
#endif
#ifdef CONFIG_MACH_CRATERVE
				touch_VAstate=1;	
#endif
			}
		} else if (zinitix_bit_test(sub_status, SUB_BIT_UP)||
			zinitix_bit_test(prev_sub_status, SUB_BIT_EXIST)) {
			memset(&info->touch_info.coord[i], 0x0, sizeof(struct coord));
			input_mt_slot(info->input_dev, i);
			input_mt_report_slot_state(info->input_dev, MT_TOOL_FINGER, 0);
			dev_info(&client->dev, "Finger [%02d] up\n", i);

#ifdef TOUCH_BOOSTER_DVFS
			info->finger_cnt1--;
			/*zinitix_set_dvfs_lock(info, info->finger_cnt1);*/
#endif
#ifdef CONFIG_MACH_CRATERVE
				touch_VAstate=0;	
#endif
			
		} else {
			memset(&info->touch_info.coord[i], 0x0, sizeof(struct coord));
		}
	}
	memcpy((char *)&info->reported_touch_info, (char *)&info->touch_info,
			sizeof(struct point_info));
	input_sync(info->input_dev);

#ifdef TOUCH_BOOSTER_DVFS
	zinitix_set_dvfs_lock(info, info->finger_cnt1);
#endif

out:
	if (info->work_state == NORMAL) {
#if ESD_TIMER_INTERVAL
		esd_timer_start(CHECK_ESD_TIMER, info);
#endif
		info->work_state = NOTHING;
	}

	up(&info->work_lock);

	return IRQ_HANDLED;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void bt532_ts_late_resume(struct early_suspend *h)
{
	struct bt532_ts_info *info = misc_info;

	printk(KERN_INFO " %s, %d \n", __func__, __LINE__);

	if (info == NULL)
		return;

	down(&info->work_lock);
	if (info->work_state != RESUME
		&& info->work_state != EALRY_SUSPEND) {
		zinitix_printk("invalid work proceedure (%d)\r\n",
			info->work_state);
		up(&info->work_lock);
		return;
	}
#ifdef ZCONFIG_PM_SLEEP
	write_cmd(info->client, BT532_WAKEUP_CMD);
	mdelay(1);
#else
	bt532_power_control(info, POWER_ON_SEQUENCE);
#endif
	if (mini_init_touch(info) == false)
		goto fail_late_resume;
/*
	if (info->ta_status == 2)
		zinitix_ta_cb(charger_callbacks, 0);
	else if (info->ta_status == 3)
		zinitix_ta_cb(charger_callbacks, 1);
*/
	enable_irq(info->irq);
	info->work_state = NOTHING;
	up(&info->work_lock);
	zinitix_printk("late resume--\n");
	return;
fail_late_resume:
	zinitix_printk("failed to late resume\n");
	enable_irq(info->irq);
	info->work_state = NOTHING;
	up(&info->work_lock);
	return;
}

static void bt532_ts_early_suspend(struct early_suspend *h)
{
	struct bt532_ts_info *info = misc_info;
	/*info = container_of(h, struct bt532_ts_info, early_suspend);*/

	printk(KERN_INFO " %s, %d \n", __func__, __LINE__);

	if (info == NULL)
		return;

	disable_irq(info->irq);
#if ESD_TIMER_INTERVAL
	flush_work(&info->tmr_work);
#endif

	down(&info->work_lock);
	if (info->work_state != NOTHING) {
		zinitix_printk("invalid work proceedure (%d)\r\n",
			info->work_state);
		up(&info->work_lock);
		enable_irq(info->irq);
		return;
	}
	info->work_state = EALRY_SUSPEND;

	clear_report_data(info);

#if ESD_TIMER_INTERVAL
	/*write_reg(info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);*/
	esd_timer_stop(info);
#endif

#ifdef ZCONFIG_PM_SLEEP
	write_reg(info->client, BT532_INT_ENABLE_FLAG, 0x0);

	udelay(100);
	if (write_cmd(info->client, BT532_SLEEP_CMD) != I2C_SUCCESS) {
		zinitix_printk("failed to enter into sleep mode\n");
		up(&info->work_lock);
		return;
	}
#else
	bt532_power_control(info, POWER_OFF);
#endif
	zinitix_printk("early suspend--\n");
	up(&info->work_lock);
	return;
}
#endif	/* CONFIG_HAS_EARLYSUSPEND */

#ifdef USE_OPEN_CLOSE
static int  bt532_ts_open(struct input_dev *dev)
{
	struct bt532_ts_info *info = misc_info;

	printk(KERN_INFO "%s, %d \n", __func__, __LINE__);

	if (info == NULL)
		return 0;

	down(&info->work_lock);
	if (info->work_state != RESUME
		&& info->work_state != EALRY_SUSPEND) {
		zinitix_printk("invalid work proceedure (%d)\r\n",
			info->work_state);
		up(&info->work_lock);
		return 0;
	}

	bt532_power_control(info, POWER_ON_SEQUENCE);

	if (mini_init_touch(info) == false)
		goto fail_late_resume;
/*
	if (info->ta_status == 2)
		zinitix_ta_cb(charger_callbacks, 0);
	else if (info->ta_status == 3)
		zinitix_ta_cb(charger_callbacks, 1);
*/
	enable_irq(info->irq);
	info->work_state = NOTHING;
	up(&info->work_lock);
	zinitix_printk("late resume--\n");
	return 0;
fail_late_resume:
	zinitix_printk("failed to late resume\n");
	enable_irq(info->irq);
	info->work_state = NOTHING;
	up(&info->work_lock);
	return 0;
}

static void bt532_ts_close(struct input_dev *dev)
{
	struct bt532_ts_info *info = misc_info;
	/*info = container_of(h, struct bt532_ts_info, early_suspend);*/

	printk(KERN_INFO "%s, %d \n", __func__, __LINE__);

	if (info == NULL)
		return;

	disable_irq(info->irq);
#if ESD_TIMER_INTERVAL
	flush_work(&info->tmr_work);
#endif

	down(&info->work_lock);
	if (info->work_state != NOTHING) {
		zinitix_printk("invalid work proceedure (%d)\r\n",
			info->work_state);
		up(&info->work_lock);
		enable_irq(info->irq);
		return;
	}
	info->work_state = EALRY_SUSPEND;

	clear_report_data(info);

#if ESD_TIMER_INTERVAL
	/*write_reg(info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);*/
	esd_timer_stop(info);
#endif

	bt532_power_control(info, POWER_OFF);

	zinitix_printk("early suspend--\n");
	up(&info->work_lock);
	return;
}
#endif	/* USE_OPEN_CLOSE */


#if defined(CONFIG_PM) && !defined(CONFIG_HAS_EARLYSUSPEND)
static int bt532_ts_resume(struct device *dev)
{
	struct bt532_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;

	printk(KERN_INFO " %s, %d \n", __func__, __LINE__);

	down(&info->work_lock);
	if (info->work_state != SUSPEND) {
		dev_err(&client->dev, "%s: Invalid work proceedure (%d)\n",
				__func__, info->work_state);
		up(&info->work_lock);

		return 0;
	}

	bt532_power_control(info, POWER_ON_SEQUENCE);

#ifdef CONFIG_HAS_EARLYSUSPEND
	info->work_state = RESUME;
#else
	enable_irq(info->irq);
	info->work_state = NOTHING;

	if (mini_init_touch(info) == false)
		dev_err(&client->dev, "Failed to resume\n");
#endif

#if defined(TSP_VERBOSE_DEBUG)
	dev_info(&client->dev, "resume\n");
#endif
	up(&info->work_lock);

	return 0;
}

static int bt532_ts_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bt532_ts_info *info = i2c_get_clientdata(client);

	printk(KERN_INFO " %s, %d \n", __func__, __LINE__);

#ifndef CONFIG_HAS_EARLYSUSPEND
	disable_irq(info->irq);
#endif
#if ESD_TIMER_INTERVAL
	flush_work(&info->tmr_work);
#endif

	down(&info->work_lock);
	if (info->work_state != NOTHING	&& info->work_state != EALRY_SUSPEND) {
		dev_err(&client->dev, "%s: Invalid work proceedure (%d)\n",
			__func__, info->work_state);
		up(&info->work_lock);
#ifndef CONFIG_HAS_EARLYSUSPEND
		enable_irq(info->irq);
#endif
		return 0;
	}

#ifndef CONFIG_HAS_EARLYSUSPEND
	clear_report_data(info);

#if ESD_TIMER_INTERVAL
	esd_timer_stop(info);
#endif
#endif
	bt532_power_control(info, POWER_OFF);
	info->work_state = SUSPEND;

#if defined(TSP_VERBOSE_DEBUG)
	dev_info(&client->dev, "suspend\n");
#endif
	up(&info->work_lock);

	return 0;
}
#endif

static bool ts_set_touchmode(u16 value){
	int i;

	disable_irq(misc_info->irq);

	down(&misc_info->work_lock);
	if (misc_info->work_state != NOTHING) {
		dev_err(&misc_info->client->dev, "%s: Other process occupied (%d)\n",
			__func__, misc_info->work_state);
		enable_irq(misc_info->irq);
		up(&misc_info->work_lock);

		return -1;
	}

	misc_info->work_state = SET_MODE;

	if (value == TOUCH_DND_MODE) {
		if (write_reg(misc_info->client, BT532_DND_N_COUNT,
						SEC_DND_N_COUNT) != I2C_SUCCESS)
			dev_err(&misc_info->client->dev, "Failed to set DND_N_COUNT\n");

		if (write_reg(misc_info->client, BT532_AFE_FREQUENCY,
						SEC_DND_FREQUENCY) != I2C_SUCCESS)
			dev_err(&misc_info->client->dev,  "Failed to set AFE_FREQUENCY\n");

	} else if (value == TOUCH_PDND_MODE) {
		if (write_reg(misc_info->client, BT532_DND_N_COUNT,
						SEC_PDND_N_COUNT) != I2C_SUCCESS)
			dev_err(&misc_info->client->dev, "Failed to set PDND_N_COUNT\n");

		if (write_reg(misc_info->client, BT532_DND_U_COUNT,
						SEC_PDND_U_COUNT) != I2C_SUCCESS)
			dev_err(&misc_info->client->dev, "Failed to set PDND_N_COUNT\n");

		if (write_reg(misc_info->client, BT532_AFE_FREQUENCY,
						SEC_PDND_FREQUENCY) != I2C_SUCCESS)
			dev_err(&misc_info->client->dev,  "Failed to set AFE_FREQUENCY\n");
	} else if (misc_info->touch_mode == TOUCH_DND_MODE) {
		if (write_reg(misc_info->client, BT532_AFE_FREQUENCY,
						misc_info->cap_info.afe_frequency) != I2C_SUCCESS)
			dev_err(&misc_info->client->dev, "Failed to set AFE_FREQUENCY %d\n",
					misc_info->cap_info.afe_frequency);
	} else if (misc_info->touch_mode == TOUCH_PDND_MODE) {

		if (write_reg(misc_info->client, BT532_DND_U_COUNT,
						2) != I2C_SUCCESS)
			dev_err(&misc_info->client->dev, "Failed to set PDND_N_COUNT\n");

		if (write_reg(misc_info->client, BT532_AFE_FREQUENCY,
						misc_info->cap_info.afe_frequency) != I2C_SUCCESS)
			dev_err(&misc_info->client->dev, "Failed to set AFE_FREQUENCY %d\n",
					misc_info->cap_info.afe_frequency);
	}

	if(value == TOUCH_SEC_MODE)
		misc_info->touch_mode = TOUCH_POINT_MODE;
	else
		misc_info->touch_mode = value;

#if defined(TSP_VERBOSE_DEBUG)
	dev_info(&misc_info->client->dev, "tsp_set_testmode, touchkey_testmode"
				" = %d\n", misc_info->touch_mode);
#endif

	if(misc_info->touch_mode != TOUCH_POINT_MODE) {
		if (write_reg(misc_info->client, BT532_DELAY_RAW_FOR_HOST,
						RAWDATA_DELAY_FOR_HOST) != I2C_SUCCESS)
			dev_err(&misc_info->client->dev, "%s: Failed to set"
						" DELAY_RAW_FOR_HOST\n", __func__);
	}

	if (write_reg(misc_info->client, BT532_TOUCH_MODE,
					misc_info->touch_mode) != I2C_SUCCESS)
		dev_err(&misc_info->client->dev,  "Failed to set TOUCH_MODE\n");

	// clear garbage data
	for(i=0; i < 10; i++) {
		mdelay(20);
		write_cmd(misc_info->client, BT532_CLEAR_INT_STATUS_CMD);
	}

	misc_info->work_state = NOTHING;
	enable_irq(misc_info->irq);
	up(&misc_info->work_lock);

	return 1;
}

static int ts_upgrade_sequence(const u8 *firmware_data)
{
	disable_irq(misc_info->irq);
	down(&misc_info->work_lock);
	misc_info->work_state = UPGRADE;

	printk(KERN_INFO " %s, %d \n", __func__, __LINE__);

#if ESD_TIMER_INTERVAL
	esd_timer_stop(misc_info);
#endif
	clear_report_data(misc_info);

	if (ts_upgrade_firmware(misc_info, firmware_data,
				misc_info->cap_info.ic_fw_size) == false)
		goto out;


	if (init_touch(misc_info, true) == false)
		goto out;


#if ESD_TIMER_INTERVAL
	esd_timer_start(CHECK_ESD_TIMER, misc_info);
#endif

	enable_irq(misc_info->irq);
	misc_info->work_state = NOTHING;
	up(&misc_info->work_lock);

	printk(KERN_INFO " %s, %d end \n", __func__, __LINE__);
	
	return 0;

out:
	enable_irq(misc_info->irq);
	misc_info->work_state = NOTHING;
	up(&misc_info->work_lock);
	return -1;
}

#ifdef SEC_FACTORY_TEST
static inline void set_cmd_result(struct bt532_ts_info *info, char *buff, int len)
{
	strncat(info->factory_info->cmd_result, buff, len);
}

static inline void set_default_result(struct bt532_ts_info *info)
{
	char delim = ':';
	memset(info->factory_info->cmd_result, 0x00, ARRAY_SIZE(info->factory_info->cmd_result));
	memcpy(info->factory_info->cmd_result, info->factory_info->cmd, strlen(info->factory_info->cmd));
	strncat(info->factory_info->cmd_result, &delim, 1);
}

static void flip_cover_enable(void *device_data)
{
	struct bt532_ts_info *info = (struct bt532_ts_info *)device_data;
	struct tsp_factory_info *finfo = info->factory_info;
	int status = 0;
	char buff[16] = {0};

	set_default_result(info);

	status = set_conifg_flip_cover(finfo->cmd_param[0]);

	dev_info(&info->client->dev, "%s: flip_cover %s %s.\n",
			__func__,
			finfo->cmd_param ? "enable" : "disable", 
			status == 0 ? "successed" : "failed");

	set_cmd_result(info, buff, strnlen(buff, sizeof(buff)));

	finfo->cmd_state = 2;

	dev_info(&info->client->dev, "%s\n", __func__);
}

extern int check_sm5502_jig_state(void);

static int set_conifg_flip_cover(int enables)
{
	int retval = 0;

	printk("[TSP] set_conifg_flip_cover : check_sm5502_jig_state() = %d !!\n",check_sm5502_jig_state());

	if(check_sm5502_jig_state() == 1)
	{
		printk("[TSP] set_conifg_flip_cover returned !!\n");
		return retval;
	}

	if (enables) {
	        retval = note_flip_close();
	} else {
	        retval = note_flip_open();
	}

	return retval;
}

static int note_flip_open(void)
{
	disable_irq(misc_info->irq);
	down(&misc_info->work_lock);

	if (misc_info->work_state != NOTHING) {
		printk(KERN_INFO"other process occupied.. (%d)\r\n",
			misc_info->work_state);
		enable_irq(misc_info->irq);
		up(&misc_info->work_lock);
		return -1;
	}
	misc_info->work_state = SET_MODE;

//	if (write_reg(misc_info->client, ZINITIX_SENSITIVITY, 80) != I2C_SUCCESS) {
//		printk(KERN_INFO "flip close sensitivity error..\n");
//		enable_irq(misc_info->irq);
//		misc_info->work_state = NOTHING;		
//		up(&misc_info->work_lock);
//		return 0;
//	}
	if (write_reg(misc_info->client, ZINITIX_BTN_SENSTIVITY, 80) != I2C_SUCCESS) {
		printk(KERN_INFO "flip close btn sensitivity error..\n");
		enable_irq(misc_info->irq);
		misc_info->work_state = NOTHING;		
		up(&misc_info->work_lock);
		return -1;
	}		
	misc_info->work_state = NOTHING;	
	enable_irq(misc_info->irq);
	up(&misc_info->work_lock);
	
	return 0;
}

static int note_flip_close(void)
{
	disable_irq(misc_info->irq);
	down(&misc_info->work_lock);
	if (misc_info->work_state != NOTHING) {
		printk(KERN_INFO "other process occupied.. (%d)\n",
			misc_info->work_state);
		enable_irq(misc_info->irq);
		up(&misc_info->work_lock);		
		return -1;
	}
	misc_info->work_state = SET_MODE;

//	if (write_reg(misc_info->client, ZINITIX_SENSITIVITY, 150) != I2C_SUCCESS) {
//		printk(KERN_INFO "flip close sensitivity error..\n");
//		enable_irq(misc_info->irq);
//		misc_info->work_state = NOTHING;		
//		up(&misc_info->work_lock);
//		return 0;
//	}
	if (write_reg(misc_info->client, ZINITIX_BTN_SENSTIVITY, 350) != I2C_SUCCESS) {
		printk(KERN_INFO "flip close btn sensitivity error..\n");
		enable_irq(misc_info->irq);
		misc_info->work_state = NOTHING;		
		up(&misc_info->work_lock);
		return -1;
	}		
	misc_info->work_state = NOTHING;	
	enable_irq(misc_info->irq);
	up(&misc_info->work_lock);
	
	return 0;
}

#define MAX_FW_PATH 255
#define TSP_FW_FILENAME "zinitix_fw.bin"

static void fw_update(void *device_data)
{
	struct bt532_ts_info *info = (struct bt532_ts_info *)device_data;
	struct i2c_client *client = info->client;
	struct tsp_factory_info *finfo = info->factory_info;
	int ret = 0;
	const u8 *buff = 0;
	mm_segment_t old_fs = {0};
	struct file *fp = NULL;
	long fsize = 0, nread = 0;
	char fw_path[MAX_FW_PATH+1];

	printk(KERN_INFO " %s, %d \n", __func__, __LINE__);

	set_default_result(info);

	switch (info->factory_info->cmd_param[0]) {
	case BUILT_IN:
		ret = ts_upgrade_sequence((u8*)m_firmware_data);
		if(ret<0) {
			info->factory_info->cmd_state = FAIL;
			return;
		}
		break;

	case UMS:
		old_fs = get_fs();
		set_fs(get_ds());

		snprintf(fw_path, MAX_FW_PATH, "/sdcard/%s", TSP_FW_FILENAME);
		fp = filp_open(fw_path, O_RDONLY, 0);
		if (IS_ERR(fp)) {
			dev_err(&client->dev, "Failed to open %s (%d)\n", fw_path, (s32)fp);
			info->factory_info->cmd_state = FAIL;

			goto err_open;
		}

		fsize = fp->f_path.dentry->d_inode->i_size;

		if(fsize != info->cap_info.ic_fw_size) {
			dev_err(&client->dev, "Invalid fw size!!\n");
			info->factory_info->cmd_state = FAIL;

			goto err_open;
		}

		buff = kzalloc((size_t)fsize, GFP_KERNEL);
		if (!buff) {
			dev_err(&client->dev, "%s: Failed to allocate memory\n",
						__func__);
			info->factory_info->cmd_state = FAIL;

			goto err_alloc;
		}

		nread = vfs_read(fp, (char __user *)buff, fsize, &fp->f_pos);
		if (nread != fsize) {
			info->factory_info->cmd_state = FAIL;

			goto err_fw_size;
		}

		ret = ts_upgrade_sequence((u8*)buff);
		if(ret<0) {
			kfree(buff);
			info->factory_info->cmd_state = FAIL;

			return;
		}
		break;

	default:
		dev_err(&client->dev, "Invalid fw file type!!\n");

		goto not_support;
	}

	info->factory_info->cmd_state = OK;
	snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff) , "%s", "OK");
	set_cmd_result(info, finfo->cmd_buff,
					strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));

if (fp != NULL) {
err_fw_size:
	kfree(buff);
err_alloc:
	filp_close(fp, NULL);
err_open:
	set_fs(old_fs);
}

	return;

not_support:
	snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff) , "%s", "NG");
	set_cmd_result(info, finfo->cmd_buff,
					strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));

	return;
}

static void get_fw_ver_bin(void *device_data)
{
	struct bt532_ts_info *info = (struct bt532_ts_info *)device_data;
	struct i2c_client *client = info->client;
	struct tsp_factory_info *finfo = info->factory_info;
	u16 fw_version, fw_minor_version, reg_version, hw_id, vendor_id;
	u32 version, length;

	set_default_result(info);

	/* To Do */
	/* modify m_firmware_data */
	fw_version = (u16)(m_firmware_data[52] | (m_firmware_data[53] << 8));
	fw_minor_version = (u16)(m_firmware_data[56] | (m_firmware_data[57] << 8));
	reg_version = (u16)(m_firmware_data[60] | (m_firmware_data[61] << 8));
	hw_id = (u16)(m_firmware_data[0x6b12] | (m_firmware_data[0x6b13] << 8));
	vendor_id = ntohs(*(u16 *)&m_firmware_data[0x6b22]);
	version = (u32)((u32)(hw_id & 0xff) << 16) | ((fw_version & 0xf ) << 12)
				| ((fw_minor_version & 0xf) << 8) | (reg_version & 0xff);

	length = sizeof(vendor_id);
	snprintf(finfo->cmd_buff, length + 1, "%s", (u8 *)&vendor_id);
	snprintf(finfo->cmd_buff + length, sizeof(finfo->cmd_buff) - length,
				"%06X", version);
	set_cmd_result(info, finfo->cmd_buff,
					strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));
	finfo->cmd_state = OK;

	dev_info(&client->dev, "%s: %s(%d)\n", __func__, finfo->cmd_buff,
				strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));

	return;
}

static void get_fw_ver_ic(void *device_data)
{
	struct bt532_ts_info *info = (struct bt532_ts_info *)device_data;
	struct i2c_client *client = info->client;
	struct tsp_factory_info *finfo = info->factory_info;
	u16 fw_version, fw_minor_version, reg_version, hw_id, vendor_id;
	u32 version, length;

	set_default_result(info);

	fw_version = info->cap_info.fw_version;
	fw_minor_version = info->cap_info.fw_minor_version;
	reg_version = info->cap_info.reg_data_version;
	hw_id = info->cap_info.hw_id;
	vendor_id = ntohs(info->cap_info.vendor_id);
	version = (u32)((u32)(hw_id & 0xff) << 16) | ((fw_version & 0xf) << 12)
				| ((fw_minor_version & 0xf) << 8) | (reg_version & 0xff);

	length = sizeof(vendor_id);
	snprintf(finfo->cmd_buff, length + 1, "%s", (u8 *)&vendor_id);
	snprintf(finfo->cmd_buff + length, sizeof(finfo->cmd_buff) - length,
				"%06X", version);
	set_cmd_result(info, finfo->cmd_buff,
					strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));
	finfo->cmd_state = OK;

	dev_info(&client->dev, "%s: %s(%d)\n", __func__, finfo->cmd_buff,
				strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));

	return;
}

static void get_threshold(void *device_data)
{
	struct bt532_ts_info *info = (struct bt532_ts_info *)device_data;
	struct i2c_client *client = info->client;
	struct tsp_factory_info *finfo = info->factory_info;

	set_default_result(info);

	snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff),
				"%d", info->cap_info.threshold);
	set_cmd_result(info, finfo->cmd_buff,
					strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));
	finfo->cmd_state = OK;

	dev_info(&client->dev, "%s: %s(%d)\n", __func__, finfo->cmd_buff,
				strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));

	return;
}

static void module_off_master(void *device_data)
{
	return;
}

static void module_on_master(void *device_data)
{
	return;
}

static void module_off_slave(void *device_data)
{
	return;
}

static void module_on_slave(void *device_data)
{
	return;
}

#define BT532_VENDOR_NAME "ZINITIX"

static void get_chip_vendor(void *device_data)
{
	struct bt532_ts_info *info = (struct bt532_ts_info *)device_data;
	struct i2c_client *client = info->client;
	struct tsp_factory_info *finfo = info->factory_info;

	set_default_result(info);

	snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff),
				"%s", BT532_VENDOR_NAME);
	set_cmd_result(info, finfo->cmd_buff,
					strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));
	finfo->cmd_state = OK;

	dev_info(&client->dev, "%s: %s(%d)\n", __func__, finfo->cmd_buff,
				strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));

	return;
}

#define BT532_CHIP_NAME "ZFT400"

static void get_chip_name(void *device_data)
{
	struct bt532_ts_info *info = (struct bt532_ts_info *)device_data;
	struct i2c_client *client = info->client;
	struct tsp_factory_info *finfo = info->factory_info;

	set_default_result(info);

	snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff), "%s", BT532_CHIP_NAME);
	set_cmd_result(info, finfo->cmd_buff,
					strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));
	finfo->cmd_state = OK;

	dev_info(&client->dev, "%s: %s(%d)\n", __func__, finfo->cmd_buff,
				strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));

	return;
}

static void get_x_num(void *device_data)
{
	struct bt532_ts_info *info = (struct bt532_ts_info *)device_data;
	struct i2c_client *client = info->client;
	struct tsp_factory_info *finfo = info->factory_info;

	set_default_result(info);

	snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff),
				"%u", info->cap_info.x_node_num);
	set_cmd_result(info, finfo->cmd_buff,
					strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));
	finfo->cmd_state = OK;

	dev_info(&client->dev, "%s: %s(%d)\n", __func__, finfo->cmd_buff,
				strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));

	return;
}

static void get_y_num(void *device_data)
{
	struct bt532_ts_info *info = (struct bt532_ts_info *)device_data;
	struct i2c_client *client = info->client;
	struct tsp_factory_info *finfo = info->factory_info;

	set_default_result(info);

	snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff),
				"%u", info->cap_info.y_node_num);
	set_cmd_result(info, finfo->cmd_buff,
					strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));
	finfo->cmd_state = OK;

	dev_info(&client->dev, "%s: %s(%d)\n", __func__, finfo->cmd_buff,
				strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));

	return;
}

static void not_support_cmd(void *device_data)
{
	struct bt532_ts_info *info = (struct bt532_ts_info *)device_data;
	struct i2c_client *client = info->client;
	struct tsp_factory_info *finfo = info->factory_info;

	set_default_result(info);

	sprintf(finfo->cmd_buff, "%s", "NA");
	set_cmd_result(info, finfo->cmd_buff,
					strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));
	info->factory_info->cmd_state = NOT_APPLICABLE;

	dev_info(&client->dev, "%s: \"%s(%d)\"\n", __func__, finfo->cmd_buff,
				strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));

	return;
}

static void run_reference_read(void *device_data)
{
	struct bt532_ts_info *info = (struct bt532_ts_info *)device_data;
	struct i2c_client *client = info->client;
	struct tsp_factory_info *finfo = info->factory_info;
	struct tsp_raw_data *raw_data = info->raw_data;
	u16 min, max;
	s32 i,j;

	set_default_result(info);

	ts_set_touchmode(TOUCH_DND_MODE);
	get_raw_data(info, (u8 *)raw_data->ref_data, 10);
	ts_set_touchmode(TOUCH_POINT_MODE);

	min = 0xFFFF;
	max = 0x0000;

	for(i = 0; i < info->cap_info.x_node_num; i++)
	{
		for(j = 0; j < info->cap_info.y_node_num; j++)
		{
			dev_info(&client->dev, "ref_data[%d][%d] : %d \n", i, j,
					raw_data->ref_data[i * info->cap_info.y_node_num + j]);

			if(raw_data->ref_data[i * info->cap_info.y_node_num + j] < min &&
				raw_data->ref_data[i * info->cap_info.y_node_num + j] != 0)
				min = raw_data->ref_data[i * info->cap_info.y_node_num + j];

			if(raw_data->ref_data[i * info->cap_info.y_node_num + j] > max)
				max = raw_data->ref_data[i * info->cap_info.y_node_num + j];

		}
		/*pr_info("\n");*/
	}

	snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff), "%d,%d\n", min, max);
	set_cmd_result(info, finfo->cmd_buff,
					strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));
	finfo->cmd_state = OK;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__, finfo->cmd_buff,
				strlen(finfo->cmd_buff));

	return;
}

static void get_reference(void *device_data)
{
	struct bt532_ts_info *info = (struct bt532_ts_info *)device_data;
	struct i2c_client *client = info->client;
	struct tsp_factory_info *finfo = info->factory_info;
	struct tsp_raw_data *raw_data = info->raw_data;
	unsigned int val;
	int x_node, y_node;
	int node_num;

	set_default_result(info);

	x_node = finfo->cmd_param[0];
	y_node = finfo->cmd_param[1];

	if (x_node < 0 || x_node >= info->cap_info.x_node_num ||
		y_node < 0 || y_node >= info->cap_info.y_node_num) {
		snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff), "%s", "abnormal");
		set_cmd_result(info, finfo->cmd_buff,
						strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));
		info->factory_info->cmd_state = FAIL;

		return;
	}

	node_num = x_node * info->cap_info.y_node_num + y_node;

	val = raw_data->ref_data[node_num];
	snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff), "%u", val);
	set_cmd_result(info, finfo->cmd_buff,
					strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));
	finfo->cmd_state = OK;

	dev_info(&client->dev, "%s: %s(%d)\n", __func__, finfo->cmd_buff,
				strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));

	return;
}

static void run_preference_read(void *device_data)
{
	struct bt532_ts_info *info = (struct bt532_ts_info *)device_data;
	struct i2c_client *client = info->client;
	struct tsp_factory_info *finfo = info->factory_info;
	struct tsp_raw_data *raw_data = info->raw_data;
	u16 min, max;
	s32 i,j;

	set_default_result(info);

	ts_set_touchmode(TOUCH_PDND_MODE);
	get_raw_data(info, (u8 *)raw_data->pref_data, 10);
	ts_set_touchmode(TOUCH_POINT_MODE);

	min = 0xFFFF;
	max = 0x0000;

	for(i = 0; i < info->cap_info.x_node_num; i++)
	{
		for(j = 0; j < info->cap_info.y_node_num; j++)
		{
			dev_info(&client->dev, "pref_data[%d][%d] : %d ", i, j,
					raw_data->pref_data[i * info->cap_info.y_node_num + j]);

			if (raw_data->pref_data[i * info->cap_info.y_node_num + j] < min &&
				raw_data->pref_data[i * info->cap_info.y_node_num + j] != 0)
				min = raw_data->pref_data[i * info->cap_info.y_node_num + j];

			if(raw_data->pref_data[i * info->cap_info.y_node_num + j] > max)
				max = raw_data->pref_data[i * info->cap_info.y_node_num + j];

		}
		pr_info("\n");
	}

	snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff), "%d,%d\n", min, max);
	set_cmd_result(info, finfo->cmd_buff,
					strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));
	finfo->cmd_state = OK;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__, finfo->cmd_buff,
				strlen(finfo->cmd_buff));

	return;
}

static void get_preference(void *device_data)
{
	struct bt532_ts_info *info = (struct bt532_ts_info *)device_data;
	struct i2c_client *client = info->client;
	struct tsp_factory_info *finfo = info->factory_info;
	struct tsp_raw_data *raw_data = info->raw_data;
	unsigned int val;
	int x_node, y_node;
	int node_num;

	set_default_result(info);

	x_node = finfo->cmd_param[0];
	y_node = finfo->cmd_param[1];

	if (x_node < 0 || x_node >= info->cap_info.x_node_num ||
		y_node < 0 || y_node >= info->cap_info.y_node_num) {
		snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff), "%s", "abnormal");
		set_cmd_result(info, finfo->cmd_buff,
						strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));
		info->factory_info->cmd_state = FAIL;

		return;
	}

	node_num = x_node * info->cap_info.y_node_num + y_node;

	val = raw_data->pref_data[node_num];
	snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff), "%u", val);
	set_cmd_result(info, finfo->cmd_buff,
					strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));
	finfo->cmd_state = OK;

	dev_info(&client->dev, "%s: %s(%d)\n", __func__, finfo->cmd_buff,
				strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));

	return;
}

static void run_delta_read(void *device_data)
{
	struct bt532_ts_info *info = (struct bt532_ts_info *)device_data;
	struct i2c_client *client = info->client;
	struct tsp_factory_info *finfo = info->factory_info;
	struct tsp_raw_data *raw_data = info->raw_data;
	s16 min, max;
	s32 i,j;

	set_default_result(info);

	ts_set_touchmode(TOUCH_DELTA_MODE);
	get_raw_data(info, (u8 *)(u8 *)raw_data->delta_data, 10);
	ts_set_touchmode(TOUCH_POINT_MODE);
	finfo->cmd_state = OK;

	min = (s16)0x7FFF;
	max = (s16)0x8000;

	for(i = 0; i < 30; i++)
	{
		for(j = 0; j < 18; j++)
		{
			/*printk("delta_data : %d ", raw_data->delta_data[j+i]);*/

			if(raw_data->delta_data[j*30+i] < min)
				min = raw_data->delta_data[j*30+i];

			if(raw_data->delta_data[j*30+i] > max)
				max = raw_data->delta_data[j*30+i];

		}
		/*printk("\n");*/
	}

	snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff), "%d,%d\n", min, max);
	set_cmd_result(info, finfo->cmd_buff,
					strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));
	finfo->cmd_state = OK;

	dev_info(&client->dev, "%s: \"%s\"(%d)\n", __func__, finfo->cmd_buff,
				strlen(finfo->cmd_buff));

	return;
}

static void get_delta(void *device_data)
{
	struct bt532_ts_info *info = (struct bt532_ts_info *)device_data;
	struct i2c_client *client = info->client;
	struct tsp_factory_info *finfo = info->factory_info;
	struct tsp_raw_data *raw_data = info->raw_data;
	unsigned int val;
	int x_node, y_node;
	int node_num;

	set_default_result(info);

	x_node = finfo->cmd_param[0];
	y_node = finfo->cmd_param[1];

	if (x_node < 0 || x_node > info->cap_info.x_node_num ||
		y_node < 0 || y_node > info->cap_info.y_node_num) {
		snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff), "%s", "abnormal");
		set_cmd_result(info, finfo->cmd_buff,
						strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));
		info->factory_info->cmd_state = FAIL;

		return;
	}

	node_num = x_node * info->cap_info.x_node_num + y_node;

	val = raw_data->delta_data[node_num];
	snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff), "%u", val);
	set_cmd_result(info, finfo->cmd_buff,
					strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));
	info->factory_info->cmd_state = OK;

	dev_info(&client->dev, "%s: %s(%d)\n", __func__, finfo->cmd_buff,
				strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));

	return;
}

#ifdef TOUCH_BOOSTER_DVFS
static void boost_level(void *device_data)
{
	struct bt532_ts_info *info = (struct bt532_ts_info *)device_data;
	struct i2c_client *client = info->client;
	struct tsp_factory_info *finfo = info->factory_info;

	int retval = 0;

	dev_info(&client->dev, "%s\n", __func__);

	set_default_result(info);

	info->dvfs_boost_mode = info->factory_info->cmd_param[0];

	dev_info(&client->dev,
			"%s: dvfs_boost_mode = %d\n",
			__func__, info->dvfs_boost_mode);

	snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff), "OK");
	finfo->cmd_state = OK;

	if (info->dvfs_boost_mode == DVFS_STAGE_NONE) {
		retval = set_freq_limit(DVFS_TOUCH_ID, -1);
		if (retval < 0) {
			dev_err(&info->client->dev,
					"%s: booster stop failed(%d).\n",
					__func__, retval);
			snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff), "NG");
			finfo->cmd_state = FAIL;

			info->dvfs_lock_status = false;
		}
	}

	set_cmd_result(info, finfo->cmd_buff,
			strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));

	mutex_lock(&finfo->cmd_lock);
	finfo->cmd_is_running = false;
	mutex_unlock(&finfo->cmd_lock);

	finfo->cmd_state = WAITING;

	return;
}
#endif

static ssize_t store_cmd(struct device *dev, struct device_attribute
				  *devattr, const char *buf, size_t count)
{
	struct bt532_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	struct tsp_factory_info *finfo = info->factory_info;
	char *cur, *start, *end;
	char buff[TSP_CMD_STR_LEN] = {0};
	int len, i;
	struct tsp_cmd *tsp_cmd_ptr = NULL;
	char delim = ',';
	bool cmd_found = false;
	int param_cnt = 0;

	if (finfo->cmd_is_running == true) {
		dev_err(&client->dev, "%s: other cmd is running\n", __func__);

		goto err_out;
	}

	/* check lock  */
	mutex_lock(&finfo->cmd_lock);
	finfo->cmd_is_running = true;
	mutex_unlock(&finfo->cmd_lock);

	finfo->cmd_state = RUNNING;

	for (i = 0; i < ARRAY_SIZE(finfo->cmd_param); i++)
		finfo->cmd_param[i] = 0;

	len = (int)count;
	if (*(buf + len - 1) == '\n')
		len--;

	memset(finfo->cmd, 0x00, ARRAY_SIZE(finfo->cmd));
	memcpy(finfo->cmd, buf, len);

	cur = strchr(buf, (int)delim);
	if (cur)
		memcpy(buff, buf, cur - buf);
	else
		memcpy(buff, buf, len);

	/* find command */
	list_for_each_entry(tsp_cmd_ptr, &finfo->cmd_list_head, list) {
		if (!strcmp(buff, tsp_cmd_ptr->cmd_name)) {
			cmd_found = true;
			break;
		}
	}

	/* set not_support_cmd */
	if (!cmd_found) {
		list_for_each_entry(tsp_cmd_ptr, &finfo->cmd_list_head, list) {
			if (!strcmp("not_support_cmd", tsp_cmd_ptr->cmd_name))
				break;
		}
	}

	/* parsing parameters */
	if (cur && cmd_found) {
		cur++;
		start = cur;
		memset(buff, 0x00, ARRAY_SIZE(buff));
		do {
			if (*cur == delim || cur - buf == len) {
				end = cur;
				memcpy(buff, start, end - start);
				*(buff + strlen(buff)) = '\0';
				finfo->cmd_param[param_cnt] =
								(int)simple_strtol(buff, NULL, 10);
				start = cur + 1;
				memset(buff, 0x00, ARRAY_SIZE(buff));
				param_cnt++;
			}
			cur++;
		} while (cur - buf <= len);
	}

	dev_info(&client->dev, "cmd = %s\n", tsp_cmd_ptr->cmd_name);
/*	for (i = 0; i < param_cnt; i++)
		dev_info(&client->dev, "cmd param %d= %d\n", i, finfo->cmd_param[i]);*/

	tsp_cmd_ptr->cmd_func(info);

err_out:
	return count;
}

static ssize_t show_cmd_status(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct bt532_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	struct tsp_factory_info *finfo = info->factory_info;

	dev_info(&client->dev, "tsp cmd: status:%d\n", finfo->cmd_state);

	if (finfo->cmd_state == WAITING)
		snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff), "WAITING");

	else if (finfo->cmd_state == RUNNING)
		snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff), "RUNNING");

	else if (finfo->cmd_state == OK)
		snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff), "OK");

	else if (finfo->cmd_state == FAIL)
		snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff), "FAIL");

	else if (finfo->cmd_state == NOT_APPLICABLE)
		snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff), "NOT_APPLICABLE");

	return snprintf(buf, sizeof(finfo->cmd_buff),
					"%s\n", finfo->cmd_buff);
}

static ssize_t show_cmd_result(struct device *dev, struct device_attribute
				    *devattr, char *buf)
{
	struct bt532_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	struct tsp_factory_info *finfo = info->factory_info;

	dev_info(&client->dev, "tsp cmd: result: %s\n", finfo->cmd_result);

	mutex_lock(&finfo->cmd_lock);
	finfo->cmd_is_running = false;
	mutex_unlock(&finfo->cmd_lock);

	finfo->cmd_state = WAITING;

	return snprintf(buf, sizeof(finfo->cmd_result),
					"%s\n", finfo->cmd_result);
}

static DEVICE_ATTR(cmd, S_IWUSR | S_IWGRP, NULL, store_cmd);
static DEVICE_ATTR(cmd_status, S_IRUGO, show_cmd_status, NULL);
static DEVICE_ATTR(cmd_result, S_IRUGO, show_cmd_result, NULL);

static struct attribute *touchscreen_attributes[] = {
	&dev_attr_cmd.attr,
	&dev_attr_cmd_status.attr,
	&dev_attr_cmd_result.attr,
	NULL,
};

static struct attribute_group touchscreen_attr_group = {
	.attrs = touchscreen_attributes,
};

#ifdef SUPPORTED_TOUCH_KEY
static ssize_t show_touchkey_threshold(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bt532_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	struct capa_info *cap = &(info->cap_info);

	cap->key_threshold=80;
	dev_info(&client->dev, "%s: key threshold = %d\n", __func__, cap->key_threshold);
	return snprintf(buf, 13, "%d", cap->key_threshold);
}

static char enable = '0';
static ssize_t show_enable_dummy_key(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%c", enable);
}

static ssize_t store_enable_dummy_key(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct bt532_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;

	if ((buf[0] - '0' <= 1) && count == 2)
		enable = *buf;
	else {
		dev_err(&client->dev, "%s: Invalid parameter\n", __func__);
		goto err_out;
	}

	dev_info(&client->dev, "%s: Extra button event %c\n", __func__, enable);

err_out:
	return count;
}

static ssize_t show_touchkey_sensitivity(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	struct bt532_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	u16 val;
	int ret;
	int i;
	
	#if 1
	if (!strcmp(attr->attr.name, "touchkey_menu"))
		i = 0;
	else if (!strcmp(attr->attr.name, "touchkey_back"))
		i = 1;
	#else

	if (!strcmp(attr->attr.name, "touchkey_dummy_btn1"))
		i = 0;
	else if (!strcmp(attr->attr.name, "touchkey_menu"))
		i = 1;
	else if (!strcmp(attr->attr.name, "touchkey_dummy_btn3"))
		i = 2;
	else if (!strcmp(attr->attr.name, "touchkey_dummy_btn4"))
		i = 3;
	else if (!strcmp(attr->attr.name, "touchkey_back"))
		i = 4;
	else if (!strcmp(attr->attr.name, "touchkey_dummy_btn6"))
		i = 5;
	#endif
	else {
		dev_err(&client->dev, "%s: Invalid attribut\n", __func__);

		goto err_out;
	}

	ret = read_data(client, BT532_BTN_WIDTH + i, (u8*)&val, 2);
	if (ret < 0) {
		dev_err(&client->dev, "%s: Failed to read %d's key sensitivity\n",
					__func__, i);

		goto err_out;
	}

	dev_info(&client->dev, "%s: %d's key sensitivity = %d\n",
				__func__, i, val);
#ifdef CONFIG_MACH_CRATERVE
	mutex_lock(&touchkey_sensi_lock);
	if(1 == touch_VAstate || 1 != misc_info->button[i])val = touch_sensi[i];
	if(val>2000 || val<0)val=0;
	touch_sensi[i]=0;
	mutex_unlock(&touchkey_sensi_lock);
#endif
	return snprintf(buf, 6, "%d", val);

err_out:
	return sprintf(buf, "NG");
}

static ssize_t show_back_key_raw_data(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t show_menu_key_raw_data(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t touch_led_control(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct bt532_ts_info *info = dev_get_drvdata(dev);
	u8 data;
	//data = 1 on, 0 off
	sscanf(buf, "%hhu", &data);
//	data = (data == 0) ? 2 : 1;
printk("[TKEY] %s : %d _ %d\n",__func__,data,__LINE__);
	if(data==1){	
		gpio_direction_output(info->pdata->gpio_led_en, 1);
	}
	else{
		gpio_direction_output(info->pdata->gpio_led_en, 0);
	}

	return size;
}

#ifdef  CONFIG_MACH_CRATERVE
static void new_touch_led_control(struct led_classdev *led_cdev,enum led_brightness state)
{
	if(state==LED_OFF){	
		gpio_direction_output(misc_info->pdata->gpio_led_en, 0);
	}
	else{
		gpio_direction_output(misc_info->pdata->gpio_led_en, 1);
	}
}

static void get_config_ver(void *device_data)
{
	struct bt532_ts_info *info = (struct bt532_ts_info *)device_data;
	struct i2c_client *client = info->client;
	struct tsp_factory_info *finfo = info->factory_info;

	set_default_result(info);

	snprintf(finfo->cmd_buff, sizeof(finfo->cmd_buff), "%s_%s_%s",
	    ZINITIX_DEVICE_NAME, IC_INFO, FW_RELEASE_DATE);
	set_cmd_result(info, finfo->cmd_buff,
					strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));
	finfo->cmd_state = OK;

	dev_info(&client->dev, "%s: %s(%d)\n", __func__, finfo->cmd_buff,
				strnlen(finfo->cmd_buff, sizeof(finfo->cmd_buff)));
}
static void clr_sensi_func(struct work_struct *work)
{
	touch_sensi[0]=0;
	touch_sensi[1]=0;
}
#endif


static DEVICE_ATTR(touchkey_threshold, S_IRUGO,
					show_touchkey_threshold, NULL);
static DEVICE_ATTR(extra_button_event, S_IWUSR | S_IWGRP | S_IRUGO,
					show_enable_dummy_key, store_enable_dummy_key);
static DEVICE_ATTR(touchkey_dummy_btn1, S_IRUGO,
					show_touchkey_sensitivity, NULL);
static DEVICE_ATTR(touchkey_menu, S_IRUGO,
					show_touchkey_sensitivity, NULL);
static DEVICE_ATTR(touchkey_dummy_btn3, S_IRUGO,
					show_touchkey_sensitivity, NULL);
static DEVICE_ATTR(touchkey_dummy_btn4, S_IRUGO,
					show_touchkey_sensitivity, NULL);
static DEVICE_ATTR(touchkey_back, S_IRUGO,
					show_touchkey_sensitivity, NULL);
static DEVICE_ATTR(touchkey_dummy_btn6, S_IRUGO,
					show_touchkey_sensitivity, NULL);
static DEVICE_ATTR(touchkey_raw_menu, S_IRUGO, show_menu_key_raw_data, NULL);
static DEVICE_ATTR(touchkey_raw_back, S_IRUGO, show_back_key_raw_data, NULL);
static DEVICE_ATTR(brightness, 0664, NULL, touch_led_control);


static struct attribute *touchkey_attributes[] = {
	&dev_attr_touchkey_threshold.attr,
	&dev_attr_extra_button_event.attr,
	&dev_attr_touchkey_dummy_btn1.attr,
	&dev_attr_touchkey_menu.attr,
	&dev_attr_touchkey_dummy_btn3.attr,
	&dev_attr_touchkey_dummy_btn4.attr,
	&dev_attr_touchkey_back.attr,
	&dev_attr_touchkey_dummy_btn6.attr,
	&dev_attr_touchkey_raw_menu.attr,
	&dev_attr_touchkey_raw_back.attr,
	&dev_attr_brightness.attr,
	NULL,
};
static struct attribute_group touchkey_attr_group = {
	.attrs = touchkey_attributes,
};
#endif

static int init_sec_factory(struct bt532_ts_info *info)
{
	struct tsp_factory_info *factory_info;
	struct tsp_raw_data *raw_data;
	int ret;
	int i;

	factory_info = kzalloc(sizeof(struct tsp_factory_info), GFP_KERNEL);
	raw_data = kzalloc(sizeof(struct tsp_raw_data), GFP_KERNEL);
	if (unlikely(!factory_info || !raw_data)) {
		dev_err(&info->client->dev, "%s: Failed to allocate memory\n",
				__func__);
		ret = -ENOMEM;

		goto err_alloc;
	}

	INIT_LIST_HEAD(&factory_info->cmd_list_head);
	for(i = 0; i < ARRAY_SIZE(tsp_cmds); i++)
		list_add_tail(&tsp_cmds[i].list, &factory_info->cmd_list_head);

	info->fac_dev_ts = device_create(sec_class, NULL, 0, info, "tsp");
	if (unlikely(!info->fac_dev_ts)) {
		dev_err(&info->client->dev, "Failed to create factory dev\n");
		ret = -ENODEV;

		goto err_create_device;
	}

#ifdef SUPPORTED_TOUCH_KEY
	info->fac_dev_tk = device_create(sec_class, NULL, 0, info, "sec_touchkey");
	if (IS_ERR(info->fac_dev_tk)) {
		dev_err(&info->client->dev, "Failed to create factory dev\n");
		ret = -ENODEV;

		goto err_create_device;
	}
#endif

	ret = sysfs_create_group(&info->fac_dev_ts->kobj, &touchscreen_attr_group);
	if (unlikely(ret)) {
		dev_err(&info->client->dev, "Failed to create"
					" touchscreen sysfs group\n");

		goto err_create_sysfs_ts;
	}

#ifdef SUPPORTED_TOUCH_KEY
	ret = sysfs_create_group(&info->fac_dev_tk->kobj, &touchkey_attr_group);
	if (unlikely(ret)) {
		dev_err(&info->client->dev, "Failed to create touchkey sysfs group\n");

		goto err_create_sysfs_tk;
	}
#endif

	mutex_init(&factory_info->cmd_lock);
	factory_info->cmd_is_running = false;

	info->factory_info = factory_info;
	info->raw_data = raw_data;

	return ret;

err_create_sysfs_tk:
	sysfs_remove_group(&info->fac_dev_ts->kobj, &touchscreen_attr_group);
err_create_sysfs_ts:
err_create_device:
	kfree(raw_data);
	kfree(factory_info);
err_alloc:

	return ret;
}

static void exit_sec_factory(struct bt532_ts_info *info)
{
	sysfs_remove_group(&info->fac_dev_ts->kobj, &touchscreen_attr_group);
#ifdef SUPPORTED_TOUCH_KEY
	sysfs_remove_group(&info->fac_dev_tk->kobj, &touchkey_attr_group);
#endif

	kfree(info->raw_data);
	kfree(info->factory_info);
}
#endif

static int ts_misc_fops_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int ts_misc_fops_close(struct inode *inode, struct file *filp)
{
	return 0;
}

static long ts_misc_fops_ioctl(struct file *filp,
	unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct raw_ioctl raw_ioctl;
	u8 *u8Data;
	int ret = 0;
	size_t sz = 0;
	u16 version;
	u16 mode;

	struct reg_ioctl reg_ioctl;
	u16 val;
	int nval = 0;

	if (misc_info == NULL)
		return -1;

	/* zinitix_debug_msg("cmd = %d, argp = 0x%x\n", cmd, (int)argp); */

	switch (cmd) {

	case TOUCH_IOCTL_GET_DEBUGMSG_STATE:
		ret = m_ts_debug_mode;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_SET_DEBUGMSG_STATE:
		if (copy_from_user(&nval, argp, 4)) {
			printk(KERN_INFO "[zinitix_touch] error : copy_from_user\n");
			return -1;
		}
		if (nval)
			printk(KERN_INFO "[zinitix_touch] on debug mode (%d)\n",
				nval);
		else
			printk(KERN_INFO "[zinitix_touch] off debug mode (%d)\n",
				nval);
		m_ts_debug_mode = nval;
		break;

	case TOUCH_IOCTL_GET_CHIP_REVISION:
		ret = misc_info->cap_info.ic_revision;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_FW_VERSION:
		ret = misc_info->cap_info.fw_version;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_REG_DATA_VERSION:
		ret = misc_info->cap_info.reg_data_version;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_VARIFY_UPGRADE_SIZE:
		if (copy_from_user(&sz, argp, sizeof(size_t)))
			return -1;

		printk(KERN_INFO "firmware size = %d\r\n", sz);
		if (misc_info->cap_info.ic_fw_size != sz) {
			printk(KERN_INFO "firmware size error\r\n");
			return -1;
		}
		break;

	case TOUCH_IOCTL_VARIFY_UPGRADE_DATA:
		if (copy_from_user(m_firmware_data,
			argp, misc_info->cap_info.ic_fw_size))
			return -1;

		version = (u16) (m_firmware_data[52] | (m_firmware_data[53]<<8));

		printk(KERN_INFO "firmware version = %x\r\n", version);

		if (copy_to_user(argp, &version, sizeof(version)))
			return -1;
		break;

	case TOUCH_IOCTL_START_UPGRADE:
		return ts_upgrade_sequence((u8*)m_firmware_data);

	case TOUCH_IOCTL_GET_X_RESOLUTION:
		ret = misc_info->pdata->x_resolution;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_Y_RESOLUTION:
		ret = misc_info->pdata->y_resolution;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_X_NODE_NUM:
		ret = misc_info->cap_info.x_node_num;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_Y_NODE_NUM:
		ret = misc_info->cap_info.y_node_num;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_GET_TOTAL_NODE_NUM:
		ret = misc_info->cap_info.total_node_num;
		if (copy_to_user(argp, &ret, sizeof(ret)))
			return -1;
		break;

	case TOUCH_IOCTL_HW_CALIBRAION:
		ret = -1;
		disable_irq(misc_info->irq);
		down(&misc_info->work_lock);
		if (misc_info->work_state != NOTHING) {
			printk(KERN_INFO"other process occupied.. (%d)\r\n",
				misc_info->work_state);
			up(&misc_info->work_lock);
			return -1;
		}
		misc_info->work_state = HW_CALIBRAION;
		mdelay(100);

		/* h/w calibration */
		if(ts_hw_calibration(misc_info) == true)
			ret = 0;

		mode = misc_info->touch_mode;
		if (write_reg(misc_info->client,
			BT532_TOUCH_MODE, mode) != I2C_SUCCESS) {
			printk(KERN_INFO "failed to set touch mode %d.\n",
				mode);
			goto fail_hw_cal;
		}

		if (write_cmd(misc_info->client,
			BT532_SWRESET_CMD) != I2C_SUCCESS)
			goto fail_hw_cal;

		enable_irq(misc_info->irq);
		misc_info->work_state = NOTHING;
		up(&misc_info->work_lock);
		return ret;
fail_hw_cal:
		enable_irq(misc_info->irq);
		misc_info->work_state = NOTHING;
		up(&misc_info->work_lock);
		return -1;

	case TOUCH_IOCTL_SET_RAW_DATA_MODE:
		if (misc_info == NULL) {
			zinitix_debug_msg("misc device NULL?\n");
			return -1;
		}
		if (copy_from_user(&nval, argp, 4)) {
			printk(KERN_INFO "[zinitix_touch] error : copy_from_user\r\n");
			misc_info->work_state = NOTHING;
			return -1;
		}
		ts_set_touchmode((u16)nval);

		return 0;

	case TOUCH_IOCTL_GET_REG:
		if (misc_info == NULL) {
			zinitix_debug_msg("misc device NULL?\n");
			return -1;
		}
		down(&misc_info->work_lock);
		if (misc_info->work_state != NOTHING) {
			printk(KERN_INFO "other process occupied.. (%d)\n",
				misc_info->work_state);
			up(&misc_info->work_lock);
			return -1;
		}

		misc_info->work_state = SET_MODE;

		if (copy_from_user(&reg_ioctl,
			argp, sizeof(struct reg_ioctl))) {
			misc_info->work_state = NOTHING;
			up(&misc_info->work_lock);
			printk(KERN_INFO "[zinitix_touch] error : copy_from_user\n");
			return -1;
		}

		if (read_data(misc_info->client,
			reg_ioctl.addr, (u8 *)&val, 2) < 0)
			ret = -1;

		nval = (int)val;

		if (copy_to_user(reg_ioctl.val, (u8 *)&nval, 4)) {
			misc_info->work_state = NOTHING;
			up(&misc_info->work_lock);
			printk(KERN_INFO "[zinitix_touch] error : copy_to_user\n");
			return -1;
		}

		zinitix_debug_msg("read : reg addr = 0x%x, val = 0x%x\n",
			reg_ioctl.addr, nval);

		misc_info->work_state = NOTHING;
		up(&misc_info->work_lock);
		return ret;

	case TOUCH_IOCTL_SET_REG:

		if (misc_info == NULL) {
			zinitix_debug_msg("misc device NULL?\n");
			return -1;
		}
		down(&misc_info->work_lock);
		if (misc_info->work_state != NOTHING) {
			printk(KERN_INFO "other process occupied.. (%d)\n",
				misc_info->work_state);
			up(&misc_info->work_lock);
			return -1;
		}

		misc_info->work_state = SET_MODE;
		if (copy_from_user(&reg_ioctl,
				argp, sizeof(struct reg_ioctl))) {
			misc_info->work_state = NOTHING;
			up(&misc_info->work_lock);
			printk(KERN_INFO "[zinitix_touch] error : copy_from_user\n");
			return -1;
		}

		if (copy_from_user(&val, reg_ioctl.val, 4)) {
			misc_info->work_state = NOTHING;
			up(&misc_info->work_lock);
			printk(KERN_INFO "[zinitix_touch] error : copy_from_user\n");
			return -1;
		}

		if (write_reg(misc_info->client,
			reg_ioctl.addr, val) != I2C_SUCCESS)
			ret = -1;

		zinitix_debug_msg("write : reg addr = 0x%x, val = 0x%x\r\n",
			reg_ioctl.addr, val);
		misc_info->work_state = NOTHING;
		up(&misc_info->work_lock);
		return ret;

	case TOUCH_IOCTL_DONOT_TOUCH_EVENT:

		if (misc_info == NULL) {
			zinitix_debug_msg("misc device NULL?\n");
			return -1;
		}
		down(&misc_info->work_lock);
		if (misc_info->work_state != NOTHING) {
			printk(KERN_INFO"other process occupied.. (%d)\r\n",
				misc_info->work_state);
			up(&misc_info->work_lock);
			return -1;
		}

		misc_info->work_state = SET_MODE;
		if (write_reg(misc_info->client,
			BT532_INT_ENABLE_FLAG, 0) != I2C_SUCCESS)
			ret = -1;
		zinitix_debug_msg("write : reg addr = 0x%x, val = 0x0\r\n",
			BT532_INT_ENABLE_FLAG);

		misc_info->work_state = NOTHING;
		up(&misc_info->work_lock);
		return ret;

	case TOUCH_IOCTL_SEND_SAVE_STATUS:
		if (misc_info == NULL) {
			zinitix_debug_msg("misc device NULL?\n");
			return -1;
		}
		down(&misc_info->work_lock);
		if (misc_info->work_state != NOTHING) {
			printk(KERN_INFO"other process occupied.. (%d)\r\n",
				misc_info->work_state);
			up(&misc_info->work_lock);
			return -1;
		}
		misc_info->work_state = SET_MODE;
		ret = 0;
		write_reg(misc_info->client, 0xc003, 0x0001);
		write_reg(misc_info->client, 0xc104, 0x0001);
		if (write_cmd(misc_info->client,
			BT532_SAVE_STATUS_CMD) != I2C_SUCCESS)
			ret =  -1;

		mdelay(1000);	/* for fusing eeprom */
		write_reg(misc_info->client, 0xc003, 0x0000);
		write_reg(misc_info->client, 0xc104, 0x0000);

		misc_info->work_state = NOTHING;
		up(&misc_info->work_lock);
		return ret;

	case TOUCH_IOCTL_GET_RAW_DATA:
		if (misc_info == NULL) {
			zinitix_debug_msg("misc device NULL?\n");
			return -1;
		}

		if (misc_info->touch_mode == TOUCH_POINT_MODE)
			return -1;

		down(&misc_info->raw_data_lock);
		if (misc_info->update == 0) {
			up(&misc_info->raw_data_lock);
			return -2;
		}

		if (copy_from_user(&raw_ioctl,
			argp, sizeof(raw_ioctl))) {
			up(&misc_info->raw_data_lock);
			printk(KERN_INFO "[zinitix_touch] error : copy_from_user\r\n");
			return -1;
		}

		misc_info->update = 0;

		u8Data = (u8 *)&misc_info->cur_data[0];

		if (copy_to_user(raw_ioctl.buf, (u8 *)u8Data,
			raw_ioctl.sz)) {
			up(&misc_info->raw_data_lock);
			return -1;
		}

		up(&misc_info->raw_data_lock);
		return 0;

	default:
		break;
	}
	return 0;
}

#ifdef CONFIG_OF
void zinitix_vdd_on(struct bt532_ts_info *info,bool onoff)
{
	printk(KERN_INFO " %s called with ON= %d\n",__func__, onoff);

	if(onoff){
		if (info->vddo_vreg){
			if(!regulator_is_enabled(info->vddo_vreg))
			regulator_enable(info->vddo_vreg);
		}
	}
	/*else{
		if (info->vddo_vreg)
			regulator_disable(info->vddo_vreg);
	}
	*/	
	gpio_direction_output(info->pdata->gpio_ldo_en, onoff);
	msleep(30);
	return;
}

void zinitix_init_gpio(struct bt532_ts_platform_data *pdata)
{
	int ret;

	//gpio_tlmm_config(GPIO_CFG(pdata->gpio_scl, 0,GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA), 1);
	//gpio_tlmm_config(GPIO_CFG(pdata->gpio_sda, 0,GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA), 1);

	printk(KERN_INFO " %s, irq:%d \n", __func__, pdata->gpio_int);

	ret = gpio_request(pdata->gpio_int, "zinitix_tsp_irq");
	if(ret) {
		pr_err("[TSP]%s: unable to request zinitix_tsp_irq [%d]\n",
			__func__, pdata->gpio_int);
		return;
	}

	gpio_tlmm_config(GPIO_CFG(pdata->gpio_int, 0,
		GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA), 1);

	ret = gpio_request(pdata->gpio_ldo_en, "zinitix_gpio_ldo_en");
	if(ret) {
		pr_err("[TSP]%s: unable to request zinitix_gpio_ldo_en [%d]\n",
			__func__, pdata->gpio_ldo_en);
		return;
	}
	gpio_tlmm_config(GPIO_CFG(pdata->gpio_ldo_en, 0,
		GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA), GPIO_CFG_ENABLE);

	ret = gpio_request(pdata->gpio_led_en, "zinitix_gpio_led_en");
	if(ret) {
		pr_err("[TSP]%s: unable to request zinitix_gpio_led_en [%d]\n",
			__func__, pdata->gpio_led_en);
		return;
	}
	gpio_tlmm_config(GPIO_CFG(pdata->gpio_led_en, 0,
		GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA), GPIO_CFG_ENABLE);	
#if 0
	gpio_tlmm_config(GPIO_CFG(pdata->gpio_vendor1, 0,
		GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA), 1);

	gpio_tlmm_config(GPIO_CFG(pdata->gpio_vendor2, 0,
		GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA), 1);
#endif
}

static int zinitix_parse_dt(struct device *dev,
                        struct bt532_ts_platform_data *pdata)
{
	struct device_node *np = dev->of_node;
	u32 temp;

	pdata->gpio_ldo_en = of_get_named_gpio(np, "zinitix,vdd_en-gpio", 0);
	pdata->gpio_led_en = of_get_named_gpio(np, "zinitix,led_en-gpio", 0);
	pdata->gpio_int = of_get_named_gpio(np, "zinitix,irq-gpio", 0);

	//pdata->gpio_sda = of_get_named_gpio_flags(np, "zinitix,sda-gpio", 0, &pdata->sda_gpio_flags);
	//pdata->gpio_scl = of_get_named_gpio_flags(np, "zinitix,scl-gpio", 0, &pdata->scl_gpio_flags);
#if 0
	pdata->gpio_vendor1 = of_get_named_gpio(np, "zinitix,vendor1", 0);
	pdata->gpio_vendor2 = of_get_named_gpio(np, "zinitix,vendor2", 0);
#endif
	of_property_read_u32(np, "zinitix,x_resolution", &temp);
	pdata->x_resolution = (u16)temp;

	of_property_read_u32(np, "zinitix,y_resolution", &temp);
	pdata->y_resolution = (u16)temp;

	of_property_read_u32(np, "zinitix,page_size", &temp);
	pdata->page_size = (u16)temp;

	of_property_read_u32(np, "zinitix,orientation", &temp);
	pdata->orientation = (u8)temp;
	return 0;
}
#endif

/*
static ssize_t zinitix_enabled_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	long input;
	int ret = 0;

	ret = kstrtol(buf, 10, &input);
	if(ret || ret < 0){
            printk(KERN_INFO "Error value zinitix_enabled_store \n");
            return ret;
        }
	pr_info("TSP enabled: %d\n",(int)input);

	if(input == 1)
		bt532_ts_resume(dev);
	else if(input == 0)
		bt532_ts_suspend(dev);
	else
		return -EINVAL;

	return count;
}

static struct device_attribute attrs[] = {
	__ATTR(enabled, (S_IRUGO | S_IWUSR | S_IWGRP),
			NULL,
			zinitix_enabled_store),
};
*/

static int bt532_ts_probe(struct i2c_client *client,
		const struct i2c_device_id *i2c_id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
//	struct bt532_ts_platform_data *pdata = client->dev.platform_data;
	struct bt532_ts_platform_data *pdata;
	struct bt532_ts_info *info;
	struct input_dev *input_dev;
	int ret = 0;
	int i;

//	dev_err(&client->dev, "TSP Vendor check: %d %d\n",
//		gpio_get_value(GPIO_TSP_VENDOR1), gpio_get_value(GPIO_TSP_VENDOR2));



	printk(KERN_INFO " %s\n", __func__);
	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "Not compatible i2c function\n");

		return -EIO;
	}

	if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev,
			sizeof(struct bt532_ts_platform_data), GFP_KERNEL);
		if (!pdata) {
			dev_err(&client->dev, "Failed to allocate memory\n");
			return -ENOMEM;
		}

		ret = zinitix_parse_dt(&client->dev, pdata);
		if (ret) {
			dev_err(&client->dev, "Error parsing dt %d\n", ret);
			return ret;
		}

		zinitix_init_gpio(pdata);
		//pdata->vdd_on = zinitix_vdd_on;
		pdata->is_vdd_on = NULL;
        } else {
		pdata = client->dev.platform_data;

		if (!pdata) {
			dev_err(&client->dev, "Not exist platform data\n");
			return -EINVAL;
		}
	}


/*
	if (!pdata) {
		dev_err(&client->dev, "Not exist platform data\n");

		return -EINVAL;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "Not compatible i2c function\n");

		return -EIO;
	}
*/
	info = kzalloc(sizeof(struct bt532_ts_info), GFP_KERNEL);
	if (!info) {
		dev_err(&client->dev, "%s: Failed to allocate memory\n", __func__);

		return -ENOMEM;
	}

	i2c_set_clientdata(client, info);
	info->client = client;
	info->pdata = pdata;

	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&client->dev, "Failed to allocate input device\n");
		ret = -ENOMEM;

		goto err_alloc;
	}

	info->input_dev = input_dev;
//
	info->vddo_vreg = regulator_get(&info->client->dev,"vddo");
	if (IS_ERR(info->vddo_vreg)){
		info->vddo_vreg = NULL;
		printk(KERN_INFO "info->vddo_vreg error\n");
		goto err_power_sequence;
	}
//
	// power on
	if (bt532_power_control(info, POWER_ON_SEQUENCE) == false) {
		ret = -EPERM;

		goto err_power_sequence;
	}

	memset(&info->reported_touch_info,
		0x0, sizeof(struct point_info));

	/* init touch mode */
	info->touch_mode = TOUCH_POINT_MODE;
	misc_info = info;

	if(init_touch(info, false) == false) {
		
		printk(KERN_INFO " %s, %d - init_touch fail \n", __func__, __LINE__);
		ret = -EPERM;
		goto err_input_register_device;
	}

	for (i = 0; i < MAX_SUPPORTED_BUTTON_NUM; i++)
		info->button[i] = ICON_BUTTON_UNCHANGE;

	snprintf(info->phys, sizeof(info->phys),
		"%s/input0", dev_name(&client->dev));
	input_dev->name = "sec_touchscreen";
	input_dev->id.bustype = BUS_I2C;
/*	input_dev->id.vendor = 0x0001; */
	input_dev->phys = info->phys;
/*	input_dev->id.product = 0x0002; */
/*	input_dev->id.version = 0x0100; */

	set_bit(EV_SYN, info->input_dev->evbit);
	set_bit(EV_KEY, info->input_dev->evbit);
	set_bit(EV_ABS, info->input_dev->evbit);
	set_bit(INPUT_PROP_DIRECT, info->input_dev->propbit);
	set_bit(EV_LED, info->input_dev->evbit);
	set_bit(LED_MISC, info->input_dev->ledbit);

	for (i = 0; i < MAX_SUPPORTED_BUTTON_NUM; i++)
		set_bit(BUTTON_MAPPING_KEY[i], info->input_dev->keybit);

	if (pdata->orientation & TOUCH_XY_SWAP) {
		input_set_abs_params(info->input_dev, ABS_MT_POSITION_Y,
			info->cap_info.MinX,
			info->cap_info.MaxX + ABS_PT_OFFSET,
			0, 0);
		input_set_abs_params(info->input_dev, ABS_MT_POSITION_X,
			info->cap_info.MinY,
			info->cap_info.MaxY + ABS_PT_OFFSET,
			0, 0);
	} else {
		input_set_abs_params(info->input_dev, ABS_MT_POSITION_X,
			info->cap_info.MinX,
			info->cap_info.MaxX + ABS_PT_OFFSET,
			0, 0);
		input_set_abs_params(info->input_dev, ABS_MT_POSITION_Y,
			info->cap_info.MinY,
			info->cap_info.MaxY + ABS_PT_OFFSET,
			0, 0);
	}

	input_set_abs_params(info->input_dev, ABS_MT_TOUCH_MAJOR,
		0, 255, 0, 0);
	input_set_abs_params(info->input_dev, ABS_MT_WIDTH_MAJOR,
		0, 255, 0, 0);

#if (TOUCH_POINT_MODE == 2)
	input_set_abs_params(info->input_dev, ABS_MT_TOUCH_MINOR,
		0, 255, 0, 0);
/*	input_set_abs_params(info->input_dev, ABS_MT_WIDTH_MINOR,
		0, 255, 0, 0); */
	input_set_abs_params(info->input_dev, ABS_MT_ORIENTATION,
		-128, 127, 0, 0);
	input_set_abs_params(info->input_dev, ABS_MT_ANGLE,
		-90, 90, 0, 0);
	input_set_abs_params(info->input_dev, ABS_MT_PALM,
		0, 1, 0, 0);
#endif

	set_bit(MT_TOOL_FINGER, info->input_dev->keybit);
	input_mt_init_slots(info->input_dev, info->cap_info.multi_fingers);

	input_set_drvdata(info->input_dev, info);
	ret = input_register_device(info->input_dev);
	if (ret) {
		dev_err(&client->dev, "Unable to register %s input device\n",
					info->input_dev->name);

		goto err_input_register_device;
	}

	/* configure irq */
	info->irq = gpio_to_irq(pdata->gpio_int);
	if (info->irq < 0)
		dev_err(&client->dev, "Invalid GPIO_TOUCH_IRQ\n");

	info->work_state = NOTHING;
	sema_init(&info->work_lock, 1);

#if ESD_TIMER_INTERVAL
	mutex_init(&info->prob_int_sync);
	esd_timer_start(CHECK_ESD_TIMER, info);
#endif

#ifdef TOUCH_BOOSTER_DVFS
	zinitix_init_dvfs(info);
#endif

	ret = request_threaded_irq(info->irq, NULL, bt532_touch_irq_handler,
		IRQF_TRIGGER_FALLING | IRQF_ONESHOT , ZINITIX_NAME, info);

	if (ret) {
		dev_err(&client->dev, "Unable to register irq\n");

		goto err_request_irq;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	info->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	info->early_suspend.suspend = bt532_ts_early_suspend;
	info->early_suspend.resume = bt532_ts_late_resume;
	register_early_suspend(&info->early_suspend);
#endif

#ifdef USE_OPEN_CLOSE
	input_dev->open = bt532_ts_open;
	input_dev->close = bt532_ts_close;
#endif

#if defined(CONFIG_PM_RUNTIME)
	pm_runtime_enable(&client->dev);
#endif

	info->register_cb = info->pdata->register_cb;
	//info->callbacks.inform_charger = zinitix_ta_cb;
	if (info->register_cb)
		info->register_cb(&info->callbacks);

	sema_init(&info->raw_data_lock, 1);

	ret = misc_register(&touch_misc_device);
	if (ret) {
		dev_err(&client->dev, "Failed to register touch misc device\n");

		goto err_misc_register;
	}

#ifdef SEC_FACTORY_TEST
	init_sec_factory(info);
#endif

#if ESD_TIMER_INTERVAL
	
	INIT_WORK(&info->tmr_work, ts_tmr_work);
	info->esd_tmr_workqueue =
		create_singlethread_workqueue("esd_tmr_workqueue");

	if (!info->esd_tmr_workqueue) {
		dev_err(&client->dev, "Failed to create esd tmr work queue\n");
		ret = -EPERM;

		goto err_kthread_create_failed;
	}

#endif

#ifdef  CONFIG_MACH_CRATERVE
	mutex_init(&touchkey_sensi_lock);
	write_cmd(client, BT532_CLEAR_INT_STATUS_CMD);
	touch_leds.name = "button-backlight";
	touch_leds.brightness = LED_FULL;
	touch_leds.max_brightness = LED_FULL;
	touch_leds.brightness_set = new_touch_led_control;

	ret = led_classdev_register(&client->dev,&touch_leds);

	if(ret)
		printk(KERN_ERR "led_cdev register failed");

	INIT_DELAYED_WORK(&work_clr_sensi, clr_sensi_func);
#endif	
	printk(KERN_INFO " %s, %d end \n", __func__, __LINE__);

	return 0;

#if ESD_TIMER_INTERVAL
err_kthread_create_failed:
	kfree(info->factory_info);
	kfree(info->raw_data);
#endif
err_misc_register:
	free_irq(info->irq, info);
err_request_irq:
	input_unregister_device(info->input_dev);
err_input_register_device:
	input_free_device(info->input_dev);
err_power_sequence:
err_alloc:
	kfree(info);

	printk(KERN_INFO " %s, %d fail \n", __func__, __LINE__);
	dev_info(&client->dev, "Failed to probe\n");

	return ret;
}

static int bt532_ts_remove(struct i2c_client *client)
{
	struct bt532_ts_info *info = i2c_get_clientdata(client);
	struct bt532_ts_platform_data *pdata = info->pdata;

	printk(KERN_INFO " %s, %d \n", __func__, __LINE__);

	disable_irq(info->irq);
	down(&info->work_lock);

	info->work_state = REMOVE;

#ifdef SEC_FACTORY_TEST
	exit_sec_factory(info);
#endif

#ifdef TOUCH_BOOSTER_DVFS
	mutex_destroy(&info->dvfs_lock);
#endif

#if ESD_TIMER_INTERVAL
	flush_work(&info->tmr_work);
	/*write_reg(info->client, BT532_PERIODICAL_INTERRUPT_INTERVAL, 0);*/
	esd_timer_stop(info);
	destroy_workqueue(info->esd_tmr_workqueue);
#endif

	if (info->irq)
		free_irq(info->irq, info);

	misc_deregister(&touch_misc_device);

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&info->early_suspend);
#endif

	if (gpio_is_valid(pdata->gpio_int) != 0)
		gpio_free(pdata->gpio_int);

	input_unregister_device(info->input_dev);
	input_free_device(info->input_dev);
	up(&info->work_lock);
	kfree(info);

	return 0;
}

static struct i2c_device_id bt532_idtable[] = {
	{ZINITIX_NAME, 0},
	{ }
};

#if defined(CONFIG_PM) && !defined(CONFIG_HAS_EARLYSUSPEND)
static const struct dev_pm_ops bt532_ts_pm_ops = {
#if defined(CONFIG_PM_RUNTIME)
	SET_RUNTIME_PM_OPS(bt532_ts_suspend, bt532_ts_resume, NULL)
#else
	SET_SYSTEM_SLEEP_PM_OPS(bt532_ts_suspend, bt532_ts_resume)
#endif
};
#endif

#ifdef CONFIG_OF
static struct of_device_id zinitix_match_table[] = {
        { .compatible = "zinitix,zinitix_touch",},
        { },
};
#else
#define zinitix_match_table NULL
#endif

static struct i2c_driver bt532_ts_driver = {
	.probe	= bt532_ts_probe,
	.remove	= bt532_ts_remove,
	.id_table	= bt532_idtable,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= ZINITIX_NAME,
		.of_match_table = zinitix_match_table,
#if defined(CONFIG_PM) && !defined(CONFIG_HAS_EARLYSUSPEND)
		.pm	= &bt532_ts_pm_ops,
#endif
	},
};

static int __devinit bt532_ts_init(void)
{
	printk(KERN_INFO " %s\n", __func__);
	return i2c_add_driver(&bt532_ts_driver);
}

static void __exit bt532_ts_exit(void)
{
	printk(KERN_INFO " %s\n", __func__);
	i2c_del_driver(&bt532_ts_driver);
}

module_init(bt532_ts_init);
module_exit(bt532_ts_exit);

MODULE_DESCRIPTION("touch-screen device driver using i2c interface");
MODULE_AUTHOR("<junik.lee@samsung.com>");
MODULE_LICENSE("GPL");
