/* Copyright (c) 2013, The Linux Foundation. All rights reserved.
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

#include <linux/batterydata-lib.h>

static struct single_row_lut fcc_temp = {
	.x		= {-20, 0, 25, 40, 60},
	.y		= {1996, 1999, 2000, 1997, 1988},
	.cols	= 5
};

static struct single_row_lut fcc_sf = {
	.x		= {0},
	.y		= {100},
	.cols	= 1
};

static struct sf_lut rbatt_sf = {
	.rows		= 30,
	.cols		= 5,
	.row_entries		= {-20, 0, 25, 40, 60},
	.percent	= {100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50, 45, 40, 35, 30, 25, 20, 16, 13, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1},
	.sf		= {
				{1149, 261, 100, 80, 74},
				{1102, 263, 101, 81, 75},
				{1056, 265, 101, 82, 75},
				{991, 269, 104, 84, 76},
				{987, 266, 108, 87, 78},
				{901, 288, 118, 93, 80},
				{886, 237, 115, 93, 82},
				{884, 237, 126, 97, 84},
				{881, 237, 121, 103, 89},
				{906, 233, 102, 91, 83},
				{951, 231, 97, 81, 75},
				{1002, 231, 98, 82, 76},
				{1054, 232, 99, 83, 78},
				{1107, 243, 100, 85, 81},
				{1165, 263, 103, 84, 80},
				{1232, 287, 107, 83, 75},
				{1334, 309, 106, 83, 75},
				{1486, 323, 104, 84, 76},
				{1626, 347, 100, 81, 74},
				{1572, 356, 101, 82, 76},
				{1586, 344, 100, 83, 77},
				{1507, 346, 102, 84, 78},
				{1651, 362, 105, 87, 80},
				{1832, 383, 108, 90, 82},
				{2050, 405, 113, 92, 85},
				{2352, 426, 115, 92, 82},
				{2773, 446, 110, 88, 78},
				{3435, 481, 111, 89, 81},
				{5280, 548, 120, 96, 90},
				{23217, 1143, 158, 165, 337},
	}
};

static struct pc_temp_ocv_lut pc_temp_ocv = {
	.rows		= 31,
	.cols		= 5,
	.temp		= {-20, 0, 25, 40, 60},
	.percent	= {100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50, 45, 40, 35, 30, 25, 20, 16, 13, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0},
	.ocv		= {
				{4314, 4314, 4310, 4303, 4298},
				{4230, 4258, 4262, 4260, 4256},
				{4163, 4200, 4205, 4203, 4199},
				{4098, 4148, 4151, 4149, 4145},
				{4061, 4094, 4098, 4096, 4093},
				{3961, 4053, 4060, 4053, 4044},
				{3912, 3958, 3989, 3997, 3999},
				{3873, 3921, 3961, 3962, 3959},
				{3836, 3891, 3919, 3924, 3922},
				{3813, 3859, 3870, 3876, 3877},
				{3799, 3832, 3839, 3840, 3839},
				{3787, 3809, 3815, 3816, 3815},
				{3774, 3790, 3797, 3797, 3797},
				{3762, 3780, 3782, 3782, 3781},
				{3749, 3771, 3772, 3768, 3765},
				{3736, 3760, 3764, 3755, 3742},
				{3720, 3740, 3747, 3737, 3722},
				{3704, 3718, 3720, 3712, 3697},
				{3688, 3706, 3695, 3686, 3674},
				{3675, 3700, 3689, 3681, 3669},
				{3666, 3697, 3688, 3680, 3668},
				{3656, 3694, 3687, 3679, 3668},
				{3643, 3691, 3685, 3678, 3667},
				{3627, 3687, 3684, 3677, 3665},
				{3606, 3681, 3681, 3673, 3660},
				{3578, 3668, 3671, 3660, 3642},
				{3540, 3638, 3638, 3625, 3600},
				{3486, 3586, 3581, 3567, 3540},
				{3405, 3507, 3500, 3486, 3458},
				{3283, 3377, 3375, 3361, 3334},
				{3000, 3000, 3000, 3000, 3000}
	}
};

struct bms_battery_data Samsung_8x26_CS03Q_2000mAh_data = {
	.fcc				= 2000,
	.fcc_temp_lut			= &fcc_temp,
	.fcc_sf_lut				= &fcc_sf,
	.pc_temp_ocv_lut		= &pc_temp_ocv,
	.rbatt_sf_lut			= &rbatt_sf,
	.default_rbatt_mohm	= 193
};
