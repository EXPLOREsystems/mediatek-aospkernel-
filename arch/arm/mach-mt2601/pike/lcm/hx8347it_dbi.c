/*
* Copyright (C) 2014 MediaTek Inc.
*
* This program is free software: you can redistribute it and/or modify it under the terms of the
* GNU General Public License version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with this program.
* If not, see <http://www.gnu.org/licenses/>.
*/

#include <linux/string.h>
#include <linux/kernel.h>
#include <mach/mt_gpio.h>

#include "cust_gpio_usage.h"
#include "lcm_drv.h"


/* --------------------------------------------------------------------------- */
/* Local Constants */
/* --------------------------------------------------------------------------- */

/* #define ESD_SUPPORT */

#define FRAME_WIDTH  (240)
#define FRAME_HEIGHT (240)

#define ROW_OFFSET (80)

#define PHYSICAL_WIDTH  (25)	/* mm */
#define PHYSICAL_HEIGHT (25)	/* mm */

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define LCM_PRINT printk

/* --------------------------------------------------------------------------- */
/* Local Variables */
/* --------------------------------------------------------------------------- */

static LCM_UTIL_FUNCS lcm_util;
static unsigned int gpio_mode_backup[9];
static int is_io_backup;

#define SET_RESET_PIN(v)    (lcm_util.set_reset_pin((v)))

#define UDELAY(n)           (lcm_util.udelay(n))
#define MDELAY(n)           (lcm_util.mdelay(n))

#define LCM_ID              (0x95)

/* --------------------------------------------------------------------------- */
/* Local Functions */
/* --------------------------------------------------------------------------- */

static inline void send_ctrl_cmd(unsigned int cmd)
{
	lcm_util.send_cmd(cmd);
}


static inline void send_data_cmd(unsigned int data)
{
	lcm_util.send_data(data & 0xFF);
}


static inline unsigned char read_data_cmd(void)
{
	return 0xFF & lcm_util.read_data();
}


static inline void set_lcm_register(unsigned int regIndex, unsigned int regData)
{
	send_ctrl_cmd(regIndex);
	send_data_cmd(regData);
}


static inline void set_lcm_gpio_output_low(unsigned int GPIO)
{
	lcm_util.set_gpio_mode(GPIO, GPIO_MODE_00);
	lcm_util.set_gpio_dir(GPIO, GPIO_DIR_OUT);
	lcm_util.set_gpio_out(GPIO, GPIO_OUT_ZERO);
}


static inline void set_lcm_gpio_mode(unsigned int GPIO, unsigned int mode)
{
	lcm_util.set_gpio_mode(GPIO, mode);
}


static inline int get_lcm_gpio_mode(unsigned int GPIO)
{
	return mt_get_gpio_mode(GPIO);
}


static void backup_io_mode(void)
{
	if (is_io_backup)
		return;

#ifdef GPIO_LCD_IO_0_PIN
	gpio_mode_backup[0] = get_lcm_gpio_mode(GPIO_LCD_IO_0_PIN);
#endif

#ifdef GPIO_LCD_IO_1_PIN
	gpio_mode_backup[1] = get_lcm_gpio_mode(GPIO_LCD_IO_1_PIN);
#endif

#ifdef GPIO_LCD_IO_2_PIN
	gpio_mode_backup[2] = get_lcm_gpio_mode(GPIO_LCD_IO_2_PIN);
#endif

#ifdef GPIO_LCD_IO_3_PIN
	gpio_mode_backup[3] = get_lcm_gpio_mode(GPIO_LCD_IO_3_PIN);
#endif

#ifdef GPIO_LCD_IO_4_PIN
	gpio_mode_backup[4] = get_lcm_gpio_mode(GPIO_LCD_IO_4_PIN);
#endif

#ifdef GPIO_LCD_IO_5_PIN
	gpio_mode_backup[5] = get_lcm_gpio_mode(GPIO_LCD_IO_5_PIN);
#endif

#ifdef GPIO_LCD_IO_6_PIN
	gpio_mode_backup[6] = get_lcm_gpio_mode(GPIO_LCD_IO_6_PIN);
#endif

#ifdef GPIO_LCD_IO_7_PIN
	gpio_mode_backup[7] = get_lcm_gpio_mode(GPIO_LCD_IO_7_PIN);
#endif

	is_io_backup = 1;
}


static void set_io_gpio_mode(void)
{
#ifdef GPIO_LCD_IO_0_PIN
	set_lcm_gpio_output_low(GPIO_LCD_IO_0_PIN);
#endif

#ifdef GPIO_LCD_IO_1_PIN
	set_lcm_gpio_output_low(GPIO_LCD_IO_1_PIN);
#endif

#ifdef GPIO_LCD_IO_2_PIN
	set_lcm_gpio_output_low(GPIO_LCD_IO_2_PIN);
#endif

#ifdef GPIO_LCD_IO_3_PIN
	set_lcm_gpio_output_low(GPIO_LCD_IO_3_PIN);
#endif

#ifdef GPIO_LCD_IO_4_PIN
	set_lcm_gpio_output_low(GPIO_LCD_IO_4_PIN);
#endif

#ifdef GPIO_LCD_IO_5_PIN
	set_lcm_gpio_output_low(GPIO_LCD_IO_5_PIN);
#endif

#ifdef GPIO_LCD_IO_6_PIN
	set_lcm_gpio_output_low(GPIO_LCD_IO_6_PIN);
#endif

#ifdef GPIO_LCD_IO_7_PIN
	set_lcm_gpio_output_low(GPIO_LCD_IO_7_PIN);
#endif
}


static void set_io_lcm_mode(void)
{
#ifdef GPIO_LCD_IO_0_PIN
	set_lcm_gpio_mode(GPIO_LCD_IO_0_PIN, gpio_mode_backup[0]);
#endif

#ifdef GPIO_LCD_IO_1_PIN
	set_lcm_gpio_mode(GPIO_LCD_IO_1_PIN, gpio_mode_backup[1]);
#endif

#ifdef GPIO_LCD_IO_2_PIN
	set_lcm_gpio_mode(GPIO_LCD_IO_2_PIN, gpio_mode_backup[2]);
#endif

#ifdef GPIO_LCD_IO_3_PIN
	set_lcm_gpio_mode(GPIO_LCD_IO_3_PIN, gpio_mode_backup[3]);
#endif

#ifdef GPIO_LCD_IO_4_PIN
	set_lcm_gpio_mode(GPIO_LCD_IO_4_PIN, gpio_mode_backup[4]);
#endif

#ifdef GPIO_LCD_IO_5_PIN
	set_lcm_gpio_mode(GPIO_LCD_IO_5_PIN, gpio_mode_backup[5]);
#endif

#ifdef GPIO_LCD_IO_6_PIN
	set_lcm_gpio_mode(GPIO_LCD_IO_6_PIN, gpio_mode_backup[6]);
#endif

#ifdef GPIO_LCD_IO_7_PIN
	set_lcm_gpio_mode(GPIO_LCD_IO_7_PIN, gpio_mode_backup[7]);
#endif
}

/* --------------------------------------------------------------------------- */
/* LCM Driver Implementations */
/* --------------------------------------------------------------------------- */
static void lcm_set_util_funcs(const LCM_UTIL_FUNCS *util)
{
	memcpy(&lcm_util, util, sizeof(LCM_UTIL_FUNCS));
}


static void lcm_get_params(LCM_PARAMS *params)
{
	memset(params, 0, sizeof(LCM_PARAMS));

	params->type = LCM_TYPE_DBI;
	params->ctrl = LCM_CTRL_PARALLEL_DBI;
	params->width = FRAME_WIDTH;
	params->height = FRAME_HEIGHT;

	params->physical_width = PHYSICAL_WIDTH;
	params->physical_height = PHYSICAL_HEIGHT;

	params->dbi.port = 0;
	params->dbi.data_width = LCM_DBI_DATA_WIDTH_8BITS;
	params->dbi.data_format.color_order = LCM_COLOR_ORDER_RGB;
	params->dbi.data_format.trans_seq = LCM_DBI_TRANS_SEQ_MSB_FIRST;
	params->dbi.data_format.padding = LCM_DBI_PADDING_ON_LSB;
	params->dbi.data_format.format = LCM_DBI_FORMAT_RGB666;
	params->dbi.data_format.width = LCM_DBI_DATA_WIDTH_8BITS;
	params->dbi.cpu_write_bits = LCM_DBI_CPU_WRITE_8_BITS;
	params->dbi.io_driving_current = LCM_DRIVING_CURRENT_8MA;

	params->dbi.parallel.write_setup = 1;
	params->dbi.parallel.write_hold = 2;
	params->dbi.parallel.write_wait = 5;
	params->dbi.parallel.read_setup = 2;
	params->dbi.parallel.read_hold = 8;
	params->dbi.parallel.read_latency = 16;
	params->dbi.parallel.wait_period = 0;
	params->dbi.parallel.cs_high_width = 0;

	params->dbi.te_mode = LCM_DBI_TE_MODE_VSYNC_ONLY;
	params->dbi.te_edge_polarity = LCM_POLARITY_RISING;
}


static void init_lcm_registers(void)
{
	int i;

	set_lcm_register(0xFF, 0x00);
	set_lcm_register(0x16, 0x80);
	set_lcm_register(0x18, 0x16);
	set_lcm_register(0x19, 0x01);
	set_lcm_register(0x1A, 0x05);
	set_lcm_register(0x26, 0x08);
	set_lcm_register(0x2F, 0x11);
	set_lcm_register(0x17, 0x06);
	set_lcm_register(0x1F, 0x88);
	MDELAY(5);
	set_lcm_register(0x1F, 0x80);
	MDELAY(5);
	set_lcm_register(0x1F, 0x90);
	MDELAY(5);
	set_lcm_register(0x1F, 0xD0);
	MDELAY(5);
	set_lcm_register(0x28, 0x38);
	MDELAY(50);

	send_ctrl_cmd(0x22);
	for (i = 0; i < FRAME_WIDTH * (FRAME_HEIGHT + ROW_OFFSET); i++) {
		send_data_cmd(0x00);
		send_data_cmd(0x00);
		send_data_cmd(0x00);
	}

	set_lcm_register(0x28, 0x3C);
	set_lcm_register(0x1B, 0x1E);
	set_lcm_register(0x1C, 0x02);
	set_lcm_register(0x1A, 0x05);
	set_lcm_register(0x24, 0x35);
	set_lcm_register(0x25, 0x57);
	set_lcm_register(0x23, 0x95);
	set_lcm_register(0x35, 0x00);
	set_lcm_register(0x36, 0x00);
	set_lcm_register(0x40, 0x00);
	set_lcm_register(0x41, 0x00);
	set_lcm_register(0x42, 0x00);
	set_lcm_register(0x43, 0x10);
	set_lcm_register(0x44, 0x10);
	set_lcm_register(0x45, 0x3b);
	set_lcm_register(0x46, 0x00);
	set_lcm_register(0x47, 0x3f);
	set_lcm_register(0x48, 0x00);
	set_lcm_register(0x49, 0x08);
	set_lcm_register(0x4A, 0x12);
	set_lcm_register(0x4B, 0x15);
	set_lcm_register(0x4C, 0x09);
	set_lcm_register(0x50, 0x05);
	set_lcm_register(0x51, 0x26);
	set_lcm_register(0x52, 0x16);
	set_lcm_register(0x53, 0x3f);
	set_lcm_register(0x54, 0x3f);
	set_lcm_register(0x55, 0x3F);
	set_lcm_register(0x56, 0x16);
	set_lcm_register(0x57, 0x7f);
	MDELAY(5);
	set_lcm_register(0x58, 0x1a);
	MDELAY(5);
	set_lcm_register(0x59, 0x07);
	MDELAY(5);
	set_lcm_register(0x5A, 0x0b);
	MDELAY(5);
	set_lcm_register(0x5B, 0x11);
	MDELAY(5);
	set_lcm_register(0x5C, 0x1f);
	MDELAY(5);
	set_lcm_register(0x5D, 0xDD);
	MDELAY(5);
	set_lcm_register(0x01, 0x00);
	MDELAY(5);
}


static void lcm_init(void)
{
	backup_io_mode();

	SET_RESET_PIN(1);
	SET_RESET_PIN(0);
	MDELAY(1);
	SET_RESET_PIN(1);
	MDELAY(10);

	init_lcm_registers();
}


static void lcm_suspend(void)
{
	send_ctrl_cmd(0x28);
	send_ctrl_cmd(0x10);
	MDELAY(5);

	backup_io_mode();
	set_io_gpio_mode();
}


static void lcm_resume(void)
{
	set_io_lcm_mode();

	send_ctrl_cmd(0x11);
	MDELAY(120);
	send_ctrl_cmd(0x29);
}


static void lcm_update(unsigned int x, unsigned int y, unsigned int width, unsigned int height)
{
	unsigned short x0, y0, x1, y1;
	unsigned short h_X_start, l_X_start, h_X_end, l_X_end, h_Y_start, l_Y_start, h_Y_end,
	    l_Y_end;

	y = y + ROW_OFFSET;

	x0 = (unsigned short)x;
	y0 = (unsigned short)y;
	x1 = (unsigned short)x + width - 1;
	y1 = (unsigned short)y + height - 1;

	h_X_start = (x0 & 0xFF00) >> 8;
	l_X_start = x0 & 0x00FF;
	h_X_end = (x1 & 0xFF00) >> 8;
	l_X_end = x1 & 0x00FF;

	h_Y_start = (y0 & 0xFF00) >> 8;
	l_Y_start = y0 & 0x00FF;
	h_Y_end = (y1 & 0xFF00) >> 8;
	l_Y_end = y1 & 0x00FF;

	set_lcm_register(0x02, h_X_start);
	set_lcm_register(0x03, l_X_start);
	set_lcm_register(0x04, h_X_end);
	set_lcm_register(0x05, l_X_end);

	set_lcm_register(0x06, h_Y_start);
	set_lcm_register(0x07, l_Y_start);
	set_lcm_register(0x08, h_Y_end);
	set_lcm_register(0x09, l_Y_end);

	send_ctrl_cmd(0x22);
}


static void lcm_setbacklight(unsigned int level)
{
	LCM_PRINT("lcm_setbacklight = %d\n", level);

	if (level > 255)
		level = 255;

	send_ctrl_cmd(0x51);
	send_data_cmd(level);
}


static unsigned int lcm_compare_id(void)
{
	unsigned char read_buf;

	SET_RESET_PIN(1);
	SET_RESET_PIN(0);
	MDELAY(1);
	SET_RESET_PIN(1);
	MDELAY(10);

	send_ctrl_cmd(0x00);
	read_buf = read_data_cmd();

	LCM_PRINT("[HX] ID : 0x%X\n", read_buf);

	return (LCM_ID == read_buf) ? 1 : 0;
}

#ifdef ESD_SUPPORT

static unsigned int lcm_esd_check(void)
{
	unsigned int readData;
	unsigned int result = TRUE;

	send_ctrl_cmd(0x0A);
	readData = read_data_cmd();	/* dummy read */
	readData = read_data_cmd();

	if ((readData & 0x97) == 0x94)
		result = FALSE;

	return result;
}

static unsigned int lcm_esd_recover(void)
{
	lcm_init();

	return TRUE;
}

#endif

static unsigned int lcm_ata_check(unsigned char *buffer)
{
	unsigned int ret = 0;
	unsigned char read_buf;

	send_ctrl_cmd(0x00);
	read_buf = read_data_cmd();

	LCM_PRINT("ID = 0x%x\n", read_buf);

	if (read_buf == LCM_ID)
		ret = 1;
	else
		ret = 0;

	return ret;
}

static void lcm_enter_idle(void)
{
}

static void lcm_exit_idle(void)
{
}

static void lcm_change_fps(unsigned int mode)
{
	if (mode == 0) {	/* slowdown frame rate in idle mode */
	} else {		/* normal frame rate in idle mode */

	}
}

/* --------------------------------------------------------------------------- */
/* Get LCM Driver Hooks */
/* --------------------------------------------------------------------------- */
LCM_DRIVER hx8347it_dbi_lcm_drv = {
	.name = "hx8347it_dbi",
	.set_util_funcs = lcm_set_util_funcs,
	.get_params = lcm_get_params,
	.init = lcm_init,
	.suspend = lcm_suspend,
	.resume = lcm_resume,
	.set_backlight = lcm_setbacklight,
	.compare_id = lcm_compare_id,
	.update = lcm_update,
#ifdef ESD_SUPPORT
	.esd_check = lcm_esd_check,
	.esd_recover = lcm_esd_recover,
#endif
	.ata_check = lcm_ata_check,
	.enter_idle = lcm_enter_idle,
	.exit_idle = lcm_exit_idle,
	.change_fps = lcm_change_fps,
};
