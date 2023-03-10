/* Copyright (c) 2010,2013, The Linux Foundation. All rights reserved.
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
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <mach/gpiomux.h>
#include <mach/msm_iomap.h>



#ifdef CONFIG_SEC_GPIO_DVS
#include <linux/errno.h>
#include <linux/secgpio_dvs.h>
#include <linux/platform_device.h>
#endif
struct msm_gpiomux_rec {
	struct gpiomux_setting *sets[GPIOMUX_NSETTINGS];
	int ref;
};
static DEFINE_SPINLOCK(gpiomux_lock);
static struct msm_gpiomux_rec *msm_gpiomux_recs;
static struct gpiomux_setting *msm_gpiomux_sets;
static unsigned msm_gpiomux_ngpio;

#ifdef CONFIG_SEC_GPIO_DVS

/****************************************************************/
/* Define value in accordance with
	the specification of each BB vendor. */
#if defined(CONFIG_MACH_CS03_SGLTE) || defined(CONFIG_MACH_MS01_LTE) || defined(CONFIG_MACH_MS01_LTE_KOR) || \
defined(CONFIG_MACH_Q7_CHN_SGLTE) || defined(CONFIG_MACH_VICTOR_CHN_SGLTE) || defined(CONFIG_MACH_VICTOR_CHN_SGLTE_CU)  /* If MSM8926 common feature is, replace to it */
#define AP_GPIO_COUNT	121
#else
#define AP_GPIO_COUNT	117
#endif
/****************************************************************/

enum {
	GPIO_IN_BIT  = 0,
	GPIO_OUT_BIT = 1
};

#define GPIO_IN_OUT(gpio)        (MSM_TLMM_BASE + 0x1004 + (0x10 * (gpio)))

#define GET_RESULT_GPIO(a, b, c)	\
	((a<<4 & 0xF0) | (b<<1 & 0xE) | (c & 0x1))

/****************************************************************/
/* Pre-defined variables. (DO NOT CHANGE THIS!!) */
static unsigned char checkgpiomap_result[GDVS_PHONE_STATUS_MAX][AP_GPIO_COUNT];
static struct gpiomap_result gpiomap_result = {
	.init = checkgpiomap_result[PHONE_INIT],
	.sleep = checkgpiomap_result[PHONE_SLEEP]
};
/****************************************************************/

static unsigned __msm_gpio_get_inout_lh(unsigned gpio)
{
	return __raw_readl(GPIO_IN_OUT(gpio)) & BIT(GPIO_IN_BIT);
}

static void msm8x26_check_gpio_status(unsigned char phonestate)
{
	struct gpiomux_setting val;

	u32 i;

	u8 temp_io = 0, temp_pdpu = 0, temp_lh = 0;

	pr_info("[secgpio_dvs][%s] state : %s\n", __func__,
		(phonestate == PHONE_INIT) ? "init" : "sleep");

	for (i = 0; i < AP_GPIO_COUNT; i++) {
		msm_gpiomux_read(i, &val);

		if (val.func == GPIOMUX_FUNC_GPIO) {
			if (val.dir == GPIOMUX_IN)
				temp_io = 0x01;	/* GPIO_IN */
			else if (val.dir == GPIOMUX_OUT_HIGH ||
					val.dir == GPIOMUX_OUT_LOW)
				temp_io = 0x02;	/* GPIO_OUT */
			else {
				temp_io = 0xF;	/* not alloc. */
				pr_err("[secgpio_dvs] gpio : %d, val.dir : %d, temp_io = 0x3",
					i, val.dir);
			}
		} else {
			temp_io = 0x0;		/* FUNC */
		}

		if (val.pull  == GPIOMUX_PULL_NONE)
			temp_pdpu = 0x00;
		else if (val.pull  == GPIOMUX_PULL_DOWN)
			temp_pdpu = 0x01;
		else if (val.pull == GPIOMUX_PULL_UP)
			temp_pdpu = 0x02;
		else if (val.pull == GPIOMUX_PULL_KEEPER)
			temp_pdpu = 0x03;
		else {
			temp_pdpu = 0x07;
			pr_err("[secgpio_dvs] gpio : %d, val.pull : %d, temp_pdpu : %d",
				i, val.pull, temp_pdpu);
		}

		if (val.func == GPIOMUX_FUNC_GPIO) {
			if (val.dir == GPIOMUX_OUT_LOW)
				temp_lh = 0x00;
			else if (val.dir == GPIOMUX_OUT_HIGH)
				temp_lh = 0x01;
			else if (val.dir == GPIOMUX_IN)
				temp_lh = __msm_gpio_get_inout_lh(i);
		} else
			temp_lh = 0;


		checkgpiomap_result[phonestate][i] =
			GET_RESULT_GPIO(temp_io, temp_pdpu, temp_lh);
	}

	pr_info("[secgpio_dvs][%s]-\n", __func__);

	return;
}

/****************************************************************/
/* Define appropriate variable in accordance with
	the specification of each BB vendor */
static struct gpio_dvs msm8x26_gpio_dvs = {
	.result = &gpiomap_result,
	.check_gpio_status = msm8x26_check_gpio_status,
	.count = AP_GPIO_COUNT,
};
/****************************************************************/
#endif

static int msm_gpiomux_store(unsigned gpio, enum msm_gpiomux_setting which,
	struct gpiomux_setting *setting, struct gpiomux_setting *old_setting)
{
	struct msm_gpiomux_rec *rec = msm_gpiomux_recs + gpio;
	unsigned set_slot = gpio * GPIOMUX_NSETTINGS + which;
	unsigned long irq_flags;
	int status = 0;

	if (!msm_gpiomux_recs)
		return -EFAULT;

	if (gpio >= msm_gpiomux_ngpio)
		return -EINVAL;

	spin_lock_irqsave(&gpiomux_lock, irq_flags);

	if (old_setting) {
		if (rec->sets[which] == NULL)
			status = 1;
		else
			*old_setting =  *(rec->sets[which]);
	}

	if (setting) {
		msm_gpiomux_sets[set_slot] = *setting;
		rec->sets[which] = &msm_gpiomux_sets[set_slot];
	} else {
		rec->sets[which] = NULL;
	}

	spin_unlock_irqrestore(&gpiomux_lock, irq_flags);
	return status;
}

int msm_gpiomux_write(unsigned gpio, enum msm_gpiomux_setting which,
	struct gpiomux_setting *setting, struct gpiomux_setting *old_setting)
{
	int ret;
	unsigned long irq_flags;
	struct gpiomux_setting *new_set;
	struct msm_gpiomux_rec *rec = msm_gpiomux_recs + gpio;

	ret = msm_gpiomux_store(gpio, which, setting, old_setting);
	if (ret < 0)
		return ret;

	spin_lock_irqsave(&gpiomux_lock, irq_flags);

	new_set = rec->ref ? rec->sets[GPIOMUX_ACTIVE] :
		rec->sets[GPIOMUX_SUSPENDED];
	if (new_set)
		__msm_gpiomux_write(gpio, *new_set);

	spin_unlock_irqrestore(&gpiomux_lock, irq_flags);
	return ret;
}
EXPORT_SYMBOL(msm_gpiomux_write);

int msm_gpiomux_get(unsigned gpio)
{
	struct msm_gpiomux_rec *rec = msm_gpiomux_recs + gpio;
	unsigned long irq_flags;

	if (!msm_gpiomux_recs)
		return -EFAULT;

	if (gpio >= msm_gpiomux_ngpio)
		return -EINVAL;

	spin_lock_irqsave(&gpiomux_lock, irq_flags);
	if (rec->ref++ == 0 && rec->sets[GPIOMUX_ACTIVE])
		__msm_gpiomux_write(gpio, *rec->sets[GPIOMUX_ACTIVE]);
	spin_unlock_irqrestore(&gpiomux_lock, irq_flags);
	return 0;
}
EXPORT_SYMBOL(msm_gpiomux_get);

int msm_gpiomux_put(unsigned gpio)
{
	struct msm_gpiomux_rec *rec = msm_gpiomux_recs + gpio;
	unsigned long irq_flags;

	if (!msm_gpiomux_recs)
		return -EFAULT;

	if (gpio >= msm_gpiomux_ngpio)
		return -EINVAL;

	spin_lock_irqsave(&gpiomux_lock, irq_flags);
	BUG_ON(rec->ref == 0);
	if (--rec->ref == 0 && rec->sets[GPIOMUX_SUSPENDED])
		__msm_gpiomux_write(gpio, *rec->sets[GPIOMUX_SUSPENDED]);
	spin_unlock_irqrestore(&gpiomux_lock, irq_flags);
	return 0;
}
EXPORT_SYMBOL(msm_gpiomux_put);

void msm_tlmm_misc_reg_write(enum msm_tlmm_misc_reg misc_reg, int val)
{
	writel_relaxed(val, MSM_TLMM_BASE + misc_reg);
	/* ensure the write completes before returning */
	mb();
}

int msm_gpiomux_init(size_t ngpio)
{
	if (!ngpio)
		return -EINVAL;

	if (msm_gpiomux_recs)
		return -EPERM;

	msm_gpiomux_recs = kzalloc(sizeof(struct msm_gpiomux_rec) * ngpio,
				   GFP_KERNEL);
	if (!msm_gpiomux_recs)
		return -ENOMEM;

	/* There is no need to zero this memory, as clients will be blindly
	 * installing settings on top of it.
	 */
	msm_gpiomux_sets = kmalloc(sizeof(struct gpiomux_setting) * ngpio *
		GPIOMUX_NSETTINGS, GFP_KERNEL);
	if (!msm_gpiomux_sets) {
		kfree(msm_gpiomux_recs);
		msm_gpiomux_recs = NULL;
		return -ENOMEM;
	}

	msm_gpiomux_ngpio = ngpio;

	return 0;
}
EXPORT_SYMBOL(msm_gpiomux_init);

void msm_gpiomux_install_nowrite(struct msm_gpiomux_config *configs,
				unsigned nconfigs)
{
	unsigned c, s;
	int rc;

	for (c = 0; c < nconfigs; ++c) {
		for (s = 0; s < GPIOMUX_NSETTINGS; ++s) {
			rc = msm_gpiomux_store(configs[c].gpio, s,
				configs[c].settings[s], NULL);
			if (rc)
				pr_err("%s: write failure: %d\n", __func__, rc);
		}
	}
}

void msm_gpiomux_install(struct msm_gpiomux_config *configs, unsigned nconfigs)
{
	unsigned c, s;
	int rc;

	for (c = 0; c < nconfigs; ++c) {
		for (s = 0; s < GPIOMUX_NSETTINGS; ++s) {
			rc = msm_gpiomux_write(configs[c].gpio, s,
				configs[c].settings[s], NULL);
			if (rc)
				pr_err("%s: write failure: %d\n", __func__, rc);
		}
	}
}
EXPORT_SYMBOL(msm_gpiomux_install);

int msm_gpiomux_init_dt(void)
{
	int rc;
	unsigned int ngpio;
	struct device_node *of_gpio_node;

	of_gpio_node = of_find_compatible_node(NULL, NULL, "qcom,msm-gpio");
	if (!of_gpio_node) {
		pr_err("%s: Failed to find qcom,msm-gpio node\n", __func__);
		return -ENODEV;
	}

	rc = of_property_read_u32(of_gpio_node, "ngpio", &ngpio);
	if (rc) {
		pr_err("%s: Failed to find ngpio property in msm-gpio device node %d\n"
				, __func__, rc);
		return rc;
	}

	return msm_gpiomux_init(ngpio);
}
EXPORT_SYMBOL(msm_gpiomux_init_dt);

#ifdef CONFIG_SEC_GPIO_DVS
static struct platform_device secgpio_dvs_device = {
	.name	= "secgpio_dvs",
	.id		= -1,
/****************************************************************/
/* Designate appropriate variable pointer
	in accordance with the specification of each BB vendor. */
	.dev.platform_data = &msm8x26_gpio_dvs,
/****************************************************************/
};

static struct platform_device *secgpio_dvs_devices[] __initdata = {
	&secgpio_dvs_device,
};

static int __init secgpio_dvs_device_init(void)
{
	return platform_add_devices(
		secgpio_dvs_devices, ARRAY_SIZE(secgpio_dvs_devices));
}
arch_initcall(secgpio_dvs_device_init);
#endif
