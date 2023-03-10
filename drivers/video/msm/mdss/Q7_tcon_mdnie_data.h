/* Copyright (c) 2009-2011, Code Aurora Forum. All rights reserved.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#ifndef _Q7_TCON_MDNIE_DATA_H_
#define _Q7_TCON_MDNIE_DATA_H_

#include "dsi_tcon_mdnie_lite.h"

#define MDNIE_COLOR_BLINDE_CMD_SIZE 18

char level_1_key_on[] = {
	0xF0,
	0x5A, 0x5A
};

char level_1_key_off[] = {
	0xF0,
	0xA5, 0xA5
};

char mdnie_app_name[][NAME_STRING_MAX] = {
	"UI_APP",
	"VIDEO_APP",
	"VIDEO_WARM_APP",
	"VIDEO_COLD_APP",
	"CAMERA_APP",
	"NAVI_APP",
	"GALLERY_APP",
	"VT_APP",
	"BROWSER_APP",
	"eBOOK_APP",
	"EMAIL_APP",
};

char mdnie_mode_name[][NAME_STRING_MAX] = {
	"DYNAMIC_MODE",
	"STANDARD_MODE",
#if defined(NATURAL_MODE_ENABLE)
	"NATURAL_MODE",
#endif
	"MOVIE_MODE",
	"MODE",
};

static char BYPASS_MDNIE_1[] ={
	0xE7,
	0x08, 0x00, 0x02, 0xd0, 0x05, 0x00, 0x00,
};
static char BYPASS_MDNIE_2[] ={
	0xE8, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static char BYPASS_MDNIE_3[] ={
	0xE9,
	0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00,
};
static char BYPASS_MDNIE_4[] ={
	0xEA,
	0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20,
};
static char BYPASS_MDNIE_5[] ={
	0xEB,
	0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0xFF,
};
static char BYPASS_MDNIE_6[] ={
	0xEC,
	0x04, 0x48, 0x1f, 0xc4, 0x1f, 0xf4, 0x1f, 0xe1, 0x04, 0x2b, 0x1f, 0xf4, 0x1f, 0xe1, 0x1f, 0xc4, 0x04, 0x5b,
};
static struct dsi_cmd_desc BYPASS_MDNIE[] = {
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(level_1_key_on)}, level_1_key_on},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(BYPASS_MDNIE_1)}, BYPASS_MDNIE_1},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(BYPASS_MDNIE_2)}, BYPASS_MDNIE_2},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(BYPASS_MDNIE_3)}, BYPASS_MDNIE_3},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(BYPASS_MDNIE_4)}, BYPASS_MDNIE_4},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(BYPASS_MDNIE_5)}, BYPASS_MDNIE_5},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(BYPASS_MDNIE_6)}, BYPASS_MDNIE_6},
	{{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(level_1_key_off)}, level_1_key_off},
};


static char NEGATIVE_MDNIE_1[] ={
	0xE7,
	0x08, 0x30, 0x03, 0x20, 0x05,0x00, 0x00,
};
static char NEGATIVE_MDNIE_2[] ={
	0xE8, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static char NEGATIVE_MDNIE_3[] ={
	0xE9,
	0xff, 0x00, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,
};
static char NEGATIVE_MDNIE_4[] ={
	0xEA,
	0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20,
};
static char NEGATIVE_MDNIE_5[] ={
	0xEB,
	0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0xFF,
};
static char NEGATIVE_MDNIE_6[] ={
	0xEC,
	0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
};
static struct dsi_cmd_desc NEGATIVE_MDNIE[] = {
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(level_1_key_on)}, level_1_key_on},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(NEGATIVE_MDNIE_1)}, NEGATIVE_MDNIE_1},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(NEGATIVE_MDNIE_2)}, NEGATIVE_MDNIE_2},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(NEGATIVE_MDNIE_3)}, NEGATIVE_MDNIE_3},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(NEGATIVE_MDNIE_4)}, NEGATIVE_MDNIE_4},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(NEGATIVE_MDNIE_5)}, NEGATIVE_MDNIE_5},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(NEGATIVE_MDNIE_6)}, NEGATIVE_MDNIE_6},
	{{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(level_1_key_off)}, level_1_key_off},
};


static char COLOR_BLIND_MDNIE_1[] ={
	0xE7,
	0x08, 0x30, 0x02, 0xd0, 0x05, 0x00, 0x00,
};
static char COLOR_BLIND_MDNIE_2[] ={
	0xE8, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static char COLOR_BLIND_MDNIE_3[] ={
	0xE9,
	0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00,
};
static char COLOR_BLIND_MDNIE_4[] ={
	0xEA,
	0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20,
};
static char COLOR_BLIND_MDNIE_5[] ={
	0xEB,
	0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0xFF,
};
static char COLOR_BLIND_MDNIE_6[] ={
	0xEC,
	0x04, 0x48, 0x1f, 0xc4, 0x1f, 0xf4, 0x1f, 0xe1, 0x04, 0x2b, 0x1f, 0xf4, 0x1f, 0xe1, 0x1f, 0xc4, 0x04, 0x5b,
};
static struct dsi_cmd_desc COLOR_BLIND_MDNIE[] = {
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(level_1_key_on)}, level_1_key_on},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(COLOR_BLIND_MDNIE_1)}, COLOR_BLIND_MDNIE_1},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(COLOR_BLIND_MDNIE_2)}, COLOR_BLIND_MDNIE_2},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(COLOR_BLIND_MDNIE_3)}, COLOR_BLIND_MDNIE_3},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(COLOR_BLIND_MDNIE_4)}, COLOR_BLIND_MDNIE_4},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(COLOR_BLIND_MDNIE_5)}, COLOR_BLIND_MDNIE_5},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(COLOR_BLIND_MDNIE_6)}, COLOR_BLIND_MDNIE_6},
	{{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(level_1_key_off)}, level_1_key_off},
};


static char CAMERA_1[] ={
	0xE7,
	0x08, 0x03, 0x02, 0xd0, 0x05, 0x00, 0x02,
};
static char CAMERA_2[] ={
	0xE8, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static char CAMERA_3[] ={
	0xE9,
	0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00,
};
static char CAMERA_4[] ={
	0xEA,
	0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20,
};
static char CAMERA_5[] ={
	0xEB,
	0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0xFF,
};
static char CAMERA_6[] ={
	0xEC,
	0x04, 0x48, 0x1f, 0xc4, 0x1f, 0xf4, 0x1f, 0xe1, 0x04, 0x2b, 0x1f, 0xf4, 0x1f, 0xe1, 0x1f, 0xc4, 0x04, 0x5b,
};
static struct dsi_cmd_desc CAMERA_MDNIE[] = {
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(level_1_key_on)}, level_1_key_on},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(CAMERA_1)}, CAMERA_1},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(CAMERA_2)}, CAMERA_2},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(CAMERA_3)}, CAMERA_3},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(CAMERA_4)}, CAMERA_4},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(CAMERA_5)}, CAMERA_5},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(CAMERA_6)}, CAMERA_6},
	{{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(level_1_key_off)}, level_1_key_off},
};


static char EBOOK_MDNIE_1[] ={
	0xE7,
	0x08, 0x30, 0x02, 0xd0, 0x05, 0x00, 0x00,
};
static char EBOOK_MDNIE_2[] ={
	0xE8, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static char EBOOK_MDNIE_3[] ={
	0xE9,
	0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xf5, 0x00, 0xff, 0x00, 0xe7, 0x00,
};
static char EBOOK_MDNIE_4[] ={
	0xEA,
	0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20,
};
static char EBOOK_MDNIE_5[] ={
	0xEB,
	0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0xFF,
};
static char EBOOK_MDNIE_6[] ={
	0xEC,
	0x04, 0x90, 0x1f, 0x88, 0x1f, 0xe9, 0x1f, 0xc3, 0x04, 0x55, 0x1f, 0xe9, 0x1f, 0xc3, 0x1f, 0x88, 0x04, 0xb5,
};
static struct dsi_cmd_desc EBOOK_MDNIE[] = {
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(level_1_key_on)}, level_1_key_on},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(EBOOK_MDNIE_1)}, EBOOK_MDNIE_1},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(EBOOK_MDNIE_2)}, EBOOK_MDNIE_2},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(EBOOK_MDNIE_3)}, EBOOK_MDNIE_3},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(EBOOK_MDNIE_4)}, EBOOK_MDNIE_4},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(EBOOK_MDNIE_5)}, EBOOK_MDNIE_5},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(EBOOK_MDNIE_6)}, EBOOK_MDNIE_6},
	{{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(level_1_key_off)}, level_1_key_off},
};


static char EMAIL_MDNIE_1[] ={
	0xE7,
	0x08, 0x30, 0x02, 0xd0, 0x05, 0x00, 0x00,
};
static char EMAIL_MDNIE_2[] ={
	0xE8, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static char EMAIL_MDNIE_3[] ={
	0xE9,
	0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xfd, 0x00, 0xff, 0x00, 0xf6, 0x00,
};
static char EMAIL_MDNIE_4[] ={
	0xEA,
	0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20,
};
static char EMAIL_MDNIE_5[] ={
	0xEB,
	0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0xFF,
};
static char EMAIL_MDNIE_6[] ={
	0xEC,
	0x04, 0x90, 0x1f, 0x88, 0x1f, 0xe9, 0x1f, 0xc3, 0x04, 0x55, 0x1f, 0xe9, 0x1f, 0xc3, 0x1f, 0x88, 0x04, 0xb5,
};
static struct dsi_cmd_desc EMAIL_MDNIE[] = {
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(level_1_key_on)}, level_1_key_on},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(EMAIL_MDNIE_1)}, EMAIL_MDNIE_1},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(EMAIL_MDNIE_2)}, EMAIL_MDNIE_2},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(EMAIL_MDNIE_3)}, EMAIL_MDNIE_3},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(EMAIL_MDNIE_4)}, EMAIL_MDNIE_4},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(EMAIL_MDNIE_5)}, EMAIL_MDNIE_5},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(EMAIL_MDNIE_6)}, EMAIL_MDNIE_6},
	{{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(level_1_key_off)}, level_1_key_off},
};


static char UI_1[] ={
	0xE7,
	0x08, 0x03, 0x02, 0xd0, 0x05, 0x00, 0x02,
};
static char UI_2[] ={
	0xE8, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static char UI_3[] ={
	0xE9,
	0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00,
};
static char UI_4[] ={
	0xEA,
	0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20,
};
static char UI_5[] ={
	0xEB,
	0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0xFF,
};
static char UI_6[] ={
	0xEC,
	0x04, 0x48, 0x1f, 0xc4, 0x1f, 0xf4, 0x1f, 0xe1, 0x04, 0x2b, 0x1f, 0xf4, 0x1f, 0xe1, 0x1f, 0xc4, 0x04, 0x5b,
};
static struct dsi_cmd_desc UI_MDNIE[] = {
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(level_1_key_on)}, level_1_key_on},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(UI_1)}, UI_1},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(UI_2)}, UI_2},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(UI_3)}, UI_3},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(UI_4)}, UI_4},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(UI_5)}, UI_5},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(UI_6)}, UI_6},
	{{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(level_1_key_off)}, level_1_key_off},
};


static char GALLERY_1[] ={
	0xE7,
	0x08, 0x03, 0x02, 0xd0, 0x05, 0x00, 0x06,
};
static char GALLERY_2[] ={
	0xE8, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static char GALLERY_3[] ={
	0xE9,
	0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00,
};
static char GALLERY_4[] ={
	0xEA,
	0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20,
};
static char GALLERY_5[] ={
	0xEB,
	0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0xFF,
};
static char GALLERY_6[] ={
	0xEC,
	0x04, 0x48, 0x1f, 0xc4, 0x1f, 0xf4, 0x1f, 0xe1, 0x04, 0x2b, 0x1f, 0xf4, 0x1f, 0xe1, 0x1f, 0xc4, 0x04, 0x5b,
};
static struct dsi_cmd_desc GALLERY_MDNIE[] = {
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(level_1_key_on)}, level_1_key_on},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(GALLERY_1)}, GALLERY_1},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(GALLERY_2)}, GALLERY_2},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(GALLERY_3)}, GALLERY_3},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(GALLERY_4)}, GALLERY_4},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(GALLERY_5)}, GALLERY_5},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(GALLERY_6)}, GALLERY_6},
	{{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(level_1_key_off)}, level_1_key_off},
};


static char VIDEO_1[] ={
	0xE7,
	0x08, 0x03, 0x02, 0xd0, 0x05, 0x00, 0x06,
};
static char VIDEO_2[] ={
	0xE8, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static char VIDEO_3[] ={
	0xE9,
	0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00,
};
static char VIDEO_4[] ={
	0xEA,
	0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20,
};
static char VIDEO_5[] ={
	0xEB,
	0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0xFF,
};
static char VIDEO_6[] ={
	0xEC,
	0x04, 0x48, 0x1f, 0xc4, 0x1f, 0xf4, 0x1f, 0xe1, 0x04, 0x2b, 0x1f, 0xf4, 0x1f, 0xe1, 0x1f, 0xc4, 0x04, 0x5b,
};
static struct dsi_cmd_desc VIDEO_MDNIE[] = {
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(level_1_key_on)}, level_1_key_on},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(VIDEO_1)}, VIDEO_1},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(VIDEO_2)}, VIDEO_2},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(VIDEO_3)}, VIDEO_3},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(VIDEO_4)}, VIDEO_4},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(VIDEO_5)}, VIDEO_5},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(VIDEO_6)}, VIDEO_6},
	{{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(level_1_key_off)}, level_1_key_off},
};

static char BROWSER_1[] ={
	0xE7,
	0x08, 0x03, 0x02, 0xd0, 0x05, 0x00, 0x02,
};
static char BROWSER_2[] ={
	0xE8, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static char BROWSER_3[] ={
	0xE9,
	0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00,
};
static char BROWSER_4[] ={
	0xEA,
	0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20,
};
static char BROWSER_5[] ={
	0xEB,
	0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0xFF,
};
static char BROWSER_6[] ={
	0xEC,
	0x04, 0x48, 0x1f, 0xc4, 0x1f, 0xf4, 0x1f, 0xe1, 0x04, 0x2b, 0x1f, 0xf4, 0x1f, 0xe1, 0x1f, 0xc4, 0x04, 0x5b,
};
static struct dsi_cmd_desc BROWSER_MDNIE[] = {
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(level_1_key_on)}, level_1_key_on},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(BROWSER_1)}, BROWSER_1},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(BROWSER_2)}, BROWSER_2},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(BROWSER_3)}, BROWSER_3},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(BROWSER_4)}, BROWSER_4},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(BROWSER_5)}, BROWSER_5},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(BROWSER_6)}, BROWSER_6},
	{{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(level_1_key_off)}, level_1_key_off},
};

static char VTCALL_1[] ={
	0xE7,
	0x08, 0x03, 0x02, 0xd0, 0x05, 0x00, 0x06,
};
static char VTCALL_2[] ={
	0xE8, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static char VTCALL_3[] ={
	0xE9,
	0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00,
};
static char VTCALL_4[] ={
	0xEA,
	0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20,
};
static char VTCALL_5[] ={
	0xEB,
	0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0xFF,
};
static char VTCALL_6[] ={
	0xEC,
	0x04, 0x48, 0x1f, 0xc4, 0x1f, 0xf4, 0x1f, 0xe1, 0x04, 0x2b, 0x1f, 0xf4, 0x1f, 0xe1, 0x1f, 0xc4, 0x04, 0x5b,
};
static struct dsi_cmd_desc VTCALL_MDNIE[] = {
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(level_1_key_on)}, level_1_key_on},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(VTCALL_1)}, VTCALL_1},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(VTCALL_2)}, VTCALL_2},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(VTCALL_3)}, VTCALL_3},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(VTCALL_4)}, VTCALL_4},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(VTCALL_5)}, VTCALL_5},
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 0, sizeof(VTCALL_6)}, VTCALL_6},
	{{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(level_1_key_off)}, level_1_key_off},
};


struct dsi_cmd_desc *mdnie_tune_value[MAX_APP_MODE][MAX_MODE] = {
 		/*"
			DYNAMIC_MODE
			STANDARD_MODE
			NATURAL_MODE
			MOVIE_MODE
			MODE
		*/
		// UI_APP
		{
			UI_MDNIE,
			UI_MDNIE,
#if defined(NATURAL_MODE_ENABLE)
			UI_MDNIE,
#endif
			UI_MDNIE,
			UI_MDNIE,
		},
		// VIDEO_APP
		{
			VIDEO_MDNIE,
			VIDEO_MDNIE,
#if defined(NATURAL_MODE_ENABLE)
			VIDEO_MDNIE,
#endif
			VIDEO_MDNIE,
			VIDEO_MDNIE,
		},
		// VIDEO_WARM_APP
		{
			VIDEO_MDNIE,
			VIDEO_MDNIE,
#if defined(NATURAL_MODE_ENABLE)
			VIDEO_MDNIE,
#endif
			VIDEO_MDNIE,
			VIDEO_MDNIE,
		},
		// VIDEO_COLD_APP
		{
			VIDEO_MDNIE,
			VIDEO_MDNIE,
#if defined(NATURAL_MODE_ENABLE)
			VIDEO_MDNIE,
#endif
			VIDEO_MDNIE,
			VIDEO_MDNIE,
		},
		// CAMERA_APP
		{
			CAMERA_MDNIE,
			CAMERA_MDNIE,
#if defined(NATURAL_MODE_ENABLE)
			CAMERA_MDNIE,
#endif
			CAMERA_MDNIE,
			CAMERA_MDNIE,
		},
		// NAVI_APP
		{
			NULL,
			NULL,
#if defined(NATURAL_MODE_ENABLE)
			NULL,
#endif
			NULL,
			NULL,
		},
		// GALLERY_APP
		{
			GALLERY_MDNIE,
			GALLERY_MDNIE,
#if defined(NATURAL_MODE_ENABLE)
			GALLERY_MDNIE,
#endif
			GALLERY_MDNIE,
			GALLERY_MDNIE,
		},
		// VT_APP
		{
			VTCALL_MDNIE,
			VTCALL_MDNIE,
#if defined(NATURAL_MODE_ENABLE)
			VTCALL_MDNIE,
#endif
			VTCALL_MDNIE,
			VTCALL_MDNIE,
		},
		// BROWSER_APP
		{
			BROWSER_MDNIE,
			BROWSER_MDNIE,
#if defined(NATURAL_MODE_ENABLE)
			BROWSER_MDNIE,
#endif
			BROWSER_MDNIE,
			BROWSER_MDNIE,
		},
		// eBOOK_APP
		{
			EBOOK_MDNIE,
			EBOOK_MDNIE,
#if defined(NATURAL_MODE_ENABLE)
			EBOOK_MDNIE,
#endif
			EBOOK_MDNIE,
			EBOOK_MDNIE,
		},
		// EMAIL_APP
		{
			EMAIL_MDNIE,
			EMAIL_MDNIE,
#if defined(NATURAL_MODE_ENABLE)
			EMAIL_MDNIE,
#endif
			EMAIL_MDNIE,
			EMAIL_MDNIE,
		},
};

#endif /*_Q7_TCON_MDNIE_DATA_H_*/