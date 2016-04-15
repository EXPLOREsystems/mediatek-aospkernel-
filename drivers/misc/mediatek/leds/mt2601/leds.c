/*
* Copyright (C) 2011-2015 MediaTek Inc.
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

/*
 * drivers/leds/leds-mt65xx.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 * mt65xx leds driver
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/leds.h>
#include <linux/leds-mt65xx.h>
#include <linux/workqueue.h>
#include <linux/wakelock.h>
#include <linux/slab.h>
/* #include <cust_leds.h> */
/* #include <cust_leds_def.h> */
#include <mach/mt_pwm.h>
#include <mach/mt_boot.h>
/* #include <mach/mt_gpio.h> */
#include <mach/pmic_mt6329_hw_bank1.h>
#include <mach/pmic_mt6329_sw_bank1.h>
#include <mach/pmic_mt6329_hw.h>
#include <mach/pmic_mt6329_sw.h>
#include <mach/upmu_common_sw.h>
#include <mach/upmu_hw.h>
#include "leds_sw.h"
/* #include <linux/leds_sw.h> */
/* #include <mach/mt_pmic_feature_api.h> */
/* #include <mach/mt_boot.h> */


static DEFINE_MUTEX(leds_mutex);
static DEFINE_MUTEX(leds_pmic_mutex);
/* #define ISINK_CHOP_CLK */
/****************************************************************************
 * variables
 ***************************************************************************/
/* struct cust_mt65xx_led* bl_setting_hal = NULL; */
 static unsigned int bl_brightness_hal = 102;
static unsigned int bl_duty_hal = 21;
static unsigned int bl_div_hal = CLK_DIV1;
static unsigned int bl_frequency_hal = 32000;
struct wake_lock leds_suspend_lock;

/****************************************************************************
 * DEBUG MACROS
 ***************************************************************************/
static int debug_enable_led_hal = 0;
#define LEDS_DEBUG(format, args...) do { \
	if (debug_enable_led_hal) \
	{\
		printk(KERN_WARNING format, ##args);\
	} \
} while (0)

/****************************************************************************
 * custom APIs
***************************************************************************/
extern unsigned int brightness_mapping(unsigned int level);

/*****************PWM *************************************************/
static const int time_array_hal[PWM_DIV_NUM] = {256, 512, 1024, 2048, 4096, 8192, 16384, 32768};
static const unsigned int div_array_hal[PWM_DIV_NUM] = {1, 2, 4, 8, 16, 32, 64, 128};
static unsigned int backlight_PWM_div_hal = CLK_DIV1;/* this para come from cust_leds. */

/******************************************************************************
   for DISP backlight High resolution
******************************************************************************/
#ifdef LED_INCREASE_LED_LEVEL_MTKPATCH
#define MT_LED_INTERNAL_LEVEL_BIT_CNT 10
#endif
/****************************************************************************
 * func:return global variables
 ***************************************************************************/

void mt_leds_wake_lock_init(void)
{
	wake_lock_init(&leds_suspend_lock, WAKE_LOCK_SUSPEND, "leds wakelock");
}

unsigned int mt_get_bl_brightness(void)
{
	return bl_brightness_hal;
}

unsigned int mt_get_bl_duty(void)
{
	return bl_duty_hal;
}
unsigned int mt_get_bl_div(void)
{
	return bl_div_hal;
}
unsigned int mt_get_bl_frequency(void)
{
	return bl_frequency_hal;
}

unsigned int *mt_get_div_array(void)
{
	return &div_array_hal[0];
}

void mt_set_bl_duty(unsigned int level)
{
	bl_duty_hal = level;
}

void mt_set_bl_div(unsigned int div)
{
	bl_div_hal = div;
}

void mt_set_bl_frequency(unsigned int freq)
{
	 bl_frequency_hal = freq;
}

struct cust_mt65xx_led *mt_get_cust_led_list(void)
{
	return get_cust_led_list();
}


/****************************************************************************
 * internal functions
 ***************************************************************************/
static int brightness_mapto64(int level)
{
	if (level < 30)
		return (level >> 1) + 7;
	else if (level <= 120)
		return (level >> 2) + 14;
	else if (level <= 160)
		return level / 5 + 20;
	else
		return (level >> 3) + 33;
}



static int find_time_index(int time)
{
	int index = 0;
	while (index < 8)
	{
		if (time < time_array_hal[index])
			return index;
		else
			index++;
	}
	return PWM_DIV_NUM-1;
}

int mt_led_set_pwm(int pwm_num, struct nled_setting *led)
{
	/* struct pwm_easy_config pwm_setting; */
	struct pwm_spec_config pwm_setting;
	int time_index = 0;
	pwm_setting.pwm_no = pwm_num;
	pwm_setting.mode = PWM_MODE_OLD;

	LEDS_DEBUG("[LED]led_set_pwm: mode=%d,pwm_no=%d\n", led->nled_mode, pwm_num);
	pwm_setting.clk_src = PWM_CLK_OLD_MODE_32K;

	switch (led->nled_mode)
	{
		case NLED_OFF:
			pwm_setting.PWM_MODE_OLD_REGS.THRESH = 0;
			pwm_setting.clk_div = CLK_DIV1;
			pwm_setting.PWM_MODE_OLD_REGS.DATA_WIDTH = 100;
			break;

		case NLED_ON:
			pwm_setting.PWM_MODE_OLD_REGS.THRESH = 30;
			pwm_setting.clk_div = CLK_DIV1;
			pwm_setting.PWM_MODE_OLD_REGS.DATA_WIDTH = 100;
			break;

		case NLED_BLINK:
			LEDS_DEBUG("[LED]LED blink on time = %d offtime = %d\n", led->blink_on_time, led->blink_off_time);
			time_index = find_time_index(led->blink_on_time + led->blink_off_time);
			LEDS_DEBUG("[LED]LED div is %d\n", time_index);
			pwm_setting.clk_div = time_index;
			pwm_setting.PWM_MODE_OLD_REGS.DATA_WIDTH = (led->blink_on_time + led->blink_off_time) * MIN_FRE_OLD_PWM / div_array_hal[time_index];
			pwm_setting.PWM_MODE_OLD_REGS.THRESH = (led->blink_on_time*100) / (led->blink_on_time + led->blink_off_time);
	}

	pwm_setting.PWM_MODE_FIFO_REGS.IDLE_VALUE = 0;
	pwm_setting.PWM_MODE_FIFO_REGS.GUARD_VALUE = 0;
	pwm_setting.PWM_MODE_FIFO_REGS.GDURATION = 0;
	pwm_setting.PWM_MODE_FIFO_REGS.WAVE_NUM = 0;
	pwm_set_spec_config(&pwm_setting);

	return 0;

}
static const int pmic_freqsel_array[] = {0, 4, 199, 499, 999, 1999, 1999, 1999};
int mt_led_blink_pmic(enum mt65xx_led_pmic pmic_type, struct nled_setting *led) {
	int time_index = 0;
	int duty = 0;
	LEDS_DEBUG("[LED]led_blink_pmic: pmic_type = %d\n", pmic_type);

	if ((pmic_type != MT65XX_LED_PMIC_NLED_ISINK0 && pmic_type != MT65XX_LED_PMIC_NLED_ISINK1 && pmic_type != MT65XX_LED_PMIC_NLED_ISINK2 && pmic_type != MT65XX_LED_PMIC_NLED_ISINK3) \
	|| led->nled_mode != NLED_BLINK) {
		return -1;
	}

	LEDS_DEBUG("[LED]LED blink on time = %d, off time = %d\n", led->blink_on_time, led->blink_off_time);
	time_index = (led->blink_on_time + led->blink_off_time) - 1;
	LEDS_DEBUG("[LED]LED index is %d, freqsel = %d\n", time_index, pmic_freqsel_array[time_index]);
	duty = 32 * led->blink_on_time / (led->blink_on_time + led->blink_off_time);
	upmu_set_rg_drv_2m_ck_pdn(0x0); /* Disable power down (Indicator no need?) */
	upmu_set_rg_drv_32k_ck_pdn(0x0); /* Disable power down */

	switch (pmic_type) {
		case MT65XX_LED_PMIC_NLED_ISINK0:
			upmu_set_rg_isink0_ck_pdn(0x0); /* Disable power down */
			upmu_set_rg_isink0_ck_sel(0x0); /* Freq = 32KHz for Indicator */
			upmu_set_isink_dim0_duty(duty);
			upmu_set_isink_ch0_mode(ISINK_PWM_MODE);
			upmu_set_isink_dim0_fsel(time_index);
			upmu_set_isink_ch0_step(0x0); /* 4mA */
			upmu_set_isink_sfstr0_tc(0x0); /* 0.5us */
			upmu_set_isink_sfstr0_en(0x0); /* Disable soft start */
			upmu_set_isink_breath0_trf_sel(0x0); /* 0.123s */
			upmu_set_isink_breath0_ton_sel(0x2); /* 0.523s */
			upmu_set_isink_breath0_toff_sel(0x5); /* 2.214s */
			upmu_set_rg_isink0_double_en(0x0); /* Disable double current */
			upmu_set_isink_phase0_dly_en(0x0); /* Disable phase delay */
			upmu_set_isink_chop0_en(0x0); /* Disable CHOP clk */
			upmu_set_isink_ch0_en(0x1); /* Turn on ISINK Channel 0 */
			break;

		case MT65XX_LED_PMIC_NLED_ISINK1:
			upmu_set_rg_isink1_ck_pdn(0x0); /* Disable power down */
			upmu_set_rg_isink1_ck_sel(0x0); /* Freq = 32KHz for Indicator */
			upmu_set_isink_dim1_duty(duty);
			upmu_set_isink_ch1_mode(ISINK_PWM_MODE);
			upmu_set_isink_dim1_fsel(time_index);
			upmu_set_isink_ch1_step(0x0); /* 4mA */
			upmu_set_isink_sfstr1_tc(0x0); /* 0.5us */
			upmu_set_isink_sfstr1_en(0x0); /* Disable soft start */
			upmu_set_isink_breath1_trf_sel(0x0); /* 0.123s */
			upmu_set_isink_breath1_ton_sel(0x2); /* 0.523s */
			upmu_set_isink_breath1_toff_sel(0x5); /* 2.214s */
			upmu_set_rg_isink1_double_en(0x0); /* Disable double current */
			upmu_set_isink_phase1_dly_en(0x0); /* Disable phase delay */
			upmu_set_isink_chop1_en(0x0); /* Disable CHOP clk */
			upmu_set_isink_ch1_en(0x1); /* Turn on ISINK Channel 1 */
			break;

		case MT65XX_LED_PMIC_NLED_ISINK2:
			upmu_set_rg_isink2_ck_pdn(0x0); /* Disable power down */
			upmu_set_rg_isink2_ck_sel(0x0); /* Freq = 32KHz for Indicator */
			upmu_set_isink_dim2_duty(duty);
			upmu_set_isink_ch2_mode(ISINK_PWM_MODE);
			upmu_set_isink_dim2_fsel(time_index);
			upmu_set_isink_ch2_step(0x0); /* 4mA */
			upmu_set_isink_sfstr2_tc(0x0); /* 0.5us */
			upmu_set_isink_sfstr2_en(0x0); /* Disable soft start */
			upmu_set_isink_breath2_trf_sel(0x0); /* 0.123s */
			upmu_set_isink_breath2_ton_sel(0x2); /* 0.523s */
			upmu_set_isink_breath2_toff_sel(0x5); /* 2.214s */
			upmu_set_rg_isink2_double_en(0x0); /* Disable double current */
			upmu_set_isink_phase2_dly_en(0x0); /* Disable phase delay */
			upmu_set_isink_chop2_en(0x0); /* Disable CHOP clk */
			upmu_set_isink_ch2_en(0x1); /* Turn on ISINK Channel 2 */
			break;

		case MT65XX_LED_PMIC_NLED_ISINK3:
			upmu_set_rg_isink3_ck_pdn(0x0); /* Disable power down */
			upmu_set_rg_isink3_ck_sel(0x0); /* Freq = 32KHz for Indicator */
			upmu_set_isink_dim3_duty(duty);
			upmu_set_isink_ch3_mode(ISINK_PWM_MODE);
			upmu_set_isink_dim3_fsel(time_index);
			upmu_set_isink_ch3_step(0x0); /* 4mA */
			upmu_set_isink_sfstr3_tc(0x0); /* 0.5us */
			upmu_set_isink_sfstr3_en(0x0); /* Disable soft start */
			upmu_set_isink_breath3_trf_sel(0x0); /* 0.123s */
			upmu_set_isink_breath3_ton_sel(0x2); /* 0.523s */
			upmu_set_isink_breath3_toff_sel(0x5); /* 2.214s */
			upmu_set_rg_isink3_double_en(0x0); /* Disable double current */
			upmu_set_isink_phase3_dly_en(0x0); /* Disable phase delay */
			upmu_set_isink_chop3_en(0x0); /* Disable CHOP clk */
			upmu_set_isink_ch3_en(0x1); /* Turn on ISINK Channel 3 */
			break;

		default:
			break;
	}
	return 0;
}


int mt_backlight_set_pwm(int pwm_num, u32 level, u32 div, struct PWM_config *config_data)
{
	struct pwm_spec_config pwm_setting;
	pwm_setting.pwm_no = pwm_num;
	pwm_setting.mode = PWM_MODE_FIFO; /* new mode fifo and periodical mode */

	pwm_setting.pmic_pad = config_data->pmic_pad;

	if (config_data->div)
	{
		pwm_setting.clk_div = config_data->div;
		backlight_PWM_div_hal = config_data->div;
	}
	else
		pwm_setting.clk_div = div;

	if (config_data->clock_source)
		pwm_setting.clk_src = PWM_CLK_NEW_MODE_BLOCK;
	else
		pwm_setting.clk_src = PWM_CLK_NEW_MODE_BLOCK_DIV_BY_1625;

	if (config_data->High_duration && config_data->low_duration)
	{
		pwm_setting.PWM_MODE_FIFO_REGS.HDURATION = config_data->High_duration;
		pwm_setting.PWM_MODE_FIFO_REGS.LDURATION = pwm_setting.PWM_MODE_FIFO_REGS.HDURATION;
	}
	else
	{
		pwm_setting.PWM_MODE_FIFO_REGS.HDURATION = 4;
		pwm_setting.PWM_MODE_FIFO_REGS.LDURATION = 4;
	}
	pwm_setting.PWM_MODE_FIFO_REGS.IDLE_VALUE = 0;
	pwm_setting.PWM_MODE_FIFO_REGS.GUARD_VALUE = 0;
	pwm_setting.PWM_MODE_FIFO_REGS.STOP_BITPOS_VALUE = 31;
	pwm_setting.PWM_MODE_FIFO_REGS.GDURATION = (pwm_setting.PWM_MODE_FIFO_REGS.HDURATION+1)*32 - 1;
	pwm_setting.PWM_MODE_FIFO_REGS.WAVE_NUM = 0;

	LEDS_DEBUG("[LED]backlight_set_pwm:duty is %d\n", level);
	LEDS_DEBUG("[LED]backlight_set_pwm:clk_src/div/high/low is %d%d%d%d\n", pwm_setting.clk_src, pwm_setting.clk_div, pwm_setting.PWM_MODE_FIFO_REGS.HDURATION, pwm_setting.PWM_MODE_FIFO_REGS.LDURATION);
	if (level > 0 && level <= 32)
	{
		pwm_setting.PWM_MODE_FIFO_REGS.GUARD_VALUE = 0;
		pwm_setting.PWM_MODE_FIFO_REGS.SEND_DATA0 =  (1 << level) - 1;
		/* pwm_setting.PWM_MODE_FIFO_REGS.SEND_DATA1 = 0 ; */
		pwm_set_spec_config(&pwm_setting);
	} else if (level > 32 && level <= 64)
	{
		pwm_setting.PWM_MODE_FIFO_REGS.GUARD_VALUE = 1;
		level -= 32;
		pwm_setting.PWM_MODE_FIFO_REGS.SEND_DATA0 = (1 << level) - 1;
		/* pwm_setting.PWM_MODE_FIFO_REGS.SEND_DATA0 =  0xFFFFFFFF ; */
		/* pwm_setting.PWM_MODE_FIFO_REGS.SEND_DATA1 = (1 << level) - 1; */
		pwm_set_spec_config(&pwm_setting);
	} else
	{
		LEDS_DEBUG("[LED]Error level in backlight\n");
		/* mt_set_pwm_disable(pwm_setting.pwm_no); */
		/* mt_pwm_power_off(pwm_setting.pwm_no); */
		mt_pwm_disable(pwm_setting.pwm_no, config_data->pmic_pad);
	}
		/* printk("[LED]PWM con register is %x\n", INREG32(PWM_BASE + 0x0150)); */
	return 0;
}

void mt_led_pwm_disable(int pwm_num)
{
	struct cust_mt65xx_led *cust_led_list = get_cust_led_list();
	mt_pwm_disable(pwm_num, cust_led_list->config_data.pmic_pad);
}

void mt_backlight_set_pwm_duty(int pwm_num, u32 level, u32 div, struct PWM_config *config_data)
{
	mt_backlight_set_pwm(pwm_num, level, div, config_data);
}

void mt_backlight_set_pwm_div(int pwm_num, u32 level, u32 div, struct PWM_config *config_data)
{
	mt_backlight_set_pwm(pwm_num, level, div, config_data);
}

void mt_backlight_get_pwm_fsel(unsigned int bl_div, unsigned int *bl_frequency)
{

}

void mt_store_pwm_register(unsigned int addr, unsigned int value)
{

}

unsigned int mt_show_pwm_register(unsigned int addr)
{
	return 0;
}
int mt_brightness_set_pmic(enum mt65xx_led_pmic pmic_type, u32 level, u32 div)
{
	#define PMIC_BACKLIGHT_LEVEL	255

	u32 tmp_level = level;
	static bool backlight_init_flag;
	static bool first_time = true;
	static int previous_isink0_double;
	static int previous_isink1_double;
	static const unsigned char multiplier_mapping[PMIC_BACKLIGHT_LEVEL] = {
		   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	};
	static const unsigned char duty_mapping[PMIC_BACKLIGHT_LEVEL] = {
		      0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x06, 0x01, 0x01, 0x02, 0x01, 0x0a, 0x02, 0x0c, 0x06,
		0x02, 0x03, 0x10, 0x05, 0x12, 0x03, 0x06, 0x0a, 0x16, 0x05, 0x05, 0x04, 0x0c, 0x08, 0x06, 0x1c,
		0x05, 0x1e, 0x07, 0x0a, 0x10, 0x06, 0x08, 0x08, 0x12, 0x0c, 0x07, 0x07, 0x0d, 0x0d, 0x0a, 0x0a,
		0x08, 0x16, 0x0b, 0x0b, 0x0b, 0x09, 0x10, 0x0c, 0x0c, 0x11, 0x0a, 0x0d, 0x0d, 0x12, 0x1c, 0x0b,
		0x0b, 0x1e, 0x1e, 0x14, 0x0f, 0x0c, 0x15, 0x15, 0x10, 0x16, 0x0d, 0x0d, 0x11, 0x11, 0x11, 0x0e,
		0x0e, 0x12, 0x19, 0x19, 0x0f, 0x0f, 0x1a, 0x1a, 0x14, 0x14, 0x10, 0x10, 0x1c, 0x15, 0x15, 0x11,
		0x11, 0x16, 0x16, 0x1e, 0x12, 0x12, 0x17, 0x17, 0x17, 0x13, 0x13, 0x13, 0x13, 0x19, 0x19, 0x19,
		0x14, 0x14, 0x1a, 0x1a, 0x15, 0x15, 0x1b, 0x1b, 0x1b, 0x16, 0x16, 0x1c, 0x1c, 0x1c, 0x17, 0x17,
		0x17, 0x17, 0x1e, 0x1e, 0x1e, 0x18, 0x18, 0x18, 0x19, 0x19, 0x19, 0x19, 0x19, 0x1a, 0x1a, 0x1a,
		0x1a, 0x1a, 0x1a, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1d, 0x1d, 0x1d,
		0x1d, 0x1d, 0x1d, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x0f, 0x1a, 0x1a, 0x1a, 0x1a,
		0x14, 0x14, 0x14, 0x14, 0x10, 0x10, 0x10, 0x1c, 0x1c, 0x1c, 0x15, 0x15, 0x15, 0x15, 0x11, 0x11,
		0x11, 0x11, 0x16, 0x16, 0x16, 0x1e, 0x1e, 0x1e, 0x12, 0x12, 0x12, 0x17, 0x17, 0x17, 0x17, 0x17,
		0x17, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x14,
		0x14, 0x14, 0x14, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x15, 0x15, 0x15, 0x15, 0x1b, 0x1b, 0x1b, 0x1b,
		0x1b, 0x1b, 0x16, 0x16, 0x16, 0x16, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x17, 0x17, 0x17, 0x17, 0x17,
	};
	static const unsigned char current_mapping[PMIC_BACKLIGHT_LEVEL] = {
		   0, 1, 2, 3, 4, 2, 0, 3, 3, 2, 4, 0, 3, 0, 1,
		4, 3, 0, 2, 0, 4, 2, 1, 0, 3, 3, 4, 1, 2, 3, 0,
		4, 0, 3, 2, 1, 4, 3, 3, 1, 2, 4, 4, 2, 2, 3, 3,
		4, 1, 3, 3, 3, 4, 2, 3, 3, 2, 4, 3, 3, 2, 1, 4,
		4, 1, 1, 2, 3, 4, 2, 2, 3, 2, 4, 4, 3, 3, 3, 4,
		4, 3, 2, 2, 4, 4, 2, 2, 3, 3, 4, 4, 2, 3, 3, 4,
		4, 3, 3, 2, 4, 4, 3, 3, 3, 4, 4, 4, 4, 3, 3, 3,
		4, 4, 3, 3, 4, 4, 3, 3, 3, 4, 4, 3, 3, 3, 4, 4,
		4, 4, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
		4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
		4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 2, 2, 2, 2,
		3, 3, 3, 3, 4, 4, 4, 2, 2, 2, 3, 3, 3, 3, 4, 4,
		4, 4, 3, 3, 3, 2, 2, 2, 4, 4, 4, 3, 3, 3, 3, 3,
		3, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 4,
		4, 4, 4, 3, 3, 3, 3, 3, 4, 4, 4, 4, 3, 3, 3, 3,
		3, 3, 4, 4, 4, 4, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4,
	};
	LEDS_DEBUG("[LED]PMIC#%d:%d\n", pmic_type, level);
	mutex_lock(&leds_pmic_mutex);
	if ((pmic_type == MT65XX_LED_PMIC_LCD_ISINK) ||
	    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK0) ||
	    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK01))
	{
		if (backlight_init_flag == false)
		{
			/* LEDS_DEBUG("[LED]Backlight Init\n"); */
			upmu_set_rg_drv_2m_ck_pdn(0x0); /* Disable power down */
			upmu_set_rg_drv_32k_ck_pdn(0x0); /* Disable power down (backlight no need?) */

			/* For backlight: Current: based on LUT, PWM frequency: 20K, Duty: 20~100, Soft start: off, Phase shift: on */
			if ((pmic_type == MT65XX_LED_PMIC_LCD_ISINK) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK0) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK01))
			{
				/* ISINK0 */
				upmu_set_rg_isink0_ck_pdn(0x0); /* Disable power down */
				upmu_set_rg_isink0_ck_sel(0x1); /* Freq = 1Mhz for Backlight */
				upmu_set_isink_ch0_mode(ISINK_PWM_MODE);
				upmu_set_isink_sfstr0_en(0x0); /* Disable soft start */
				upmu_set_rg_isink0_double_en(0x0); /* Initially disable double current */
				previous_isink0_double = 0;
				upmu_set_isink_phase_dly_tc(0x0); /* TC = 0.5us */
				upmu_set_isink_phase0_dly_en(0x1); /* Enable phase delay */
				upmu_set_isink_chop0_en(0x1); /* Enable CHOP clk */
			}
			if ((pmic_type == MT65XX_LED_PMIC_LCD_ISINK) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK01))
			{
				/* ISINK1 */
				upmu_set_rg_isink1_ck_pdn(0x0); /* Disable power down */
				upmu_set_rg_isink1_ck_sel(0x1); /* Freq = 1Mhz for Backlight */
				upmu_set_isink_ch1_mode(ISINK_PWM_MODE);
				upmu_set_isink_sfstr1_en(0x0); /* Disable soft start */
				upmu_set_rg_isink1_double_en(0x0); /* Initially disable double current */
				previous_isink1_double = 0;
				upmu_set_isink_phase1_dly_en(0x1); /* Enable phase delay */
				upmu_set_isink_chop1_en(0x1); /* Enable CHOP clk */
			}
			if (pmic_type == MT65XX_LED_PMIC_LCD_ISINK)
			{
				/* ISINK2 */
				upmu_set_rg_isink2_ck_pdn(0x0); /* Disable power down */
				upmu_set_rg_isink2_ck_sel(0x1); /* Freq = 1Mhz for Backlight */
				upmu_set_isink_ch2_mode(ISINK_PWM_MODE);
				/* upmu_set_isink_ch2_step(0x5); // 24mA */
				upmu_set_isink_sfstr2_en(0x0); /* Disable soft start */
				upmu_set_rg_isink2_double_en(0x1); /* Enable double current */
				upmu_set_isink_phase2_dly_en(0x1); /* Enable phase delay */
				upmu_set_isink_chop2_en(0x1); /* Enable CHOP clk */
				/* ISINK3 */
				upmu_set_rg_isink3_ck_pdn(0x0); /* Disable power down */
				upmu_set_rg_isink3_ck_sel(0x1); /* Freq = 1Mhz for Backlight */
				upmu_set_isink_ch3_mode(ISINK_PWM_MODE);
				upmu_set_isink_sfstr3_en(0x0); /* Disable soft start */
				upmu_set_rg_isink3_double_en(0x1); /* Enable double current */
				upmu_set_isink_phase3_dly_en(0x1); /* Enable phase delay */
				upmu_set_isink_chop3_en(0x1); /* Enable CHOP clk */
			}
			backlight_init_flag = true;
		}

		if (level)
		{
			level = brightness_mapping(tmp_level);
			if (level > (PMIC_BACKLIGHT_LEVEL-1))
			{
				level = PMIC_BACKLIGHT_LEVEL-1;
			}

			/* LEDS_DEBUG("[LED]Level Mapping = %d\n", level); */
			/* LEDS_DEBUG("[LED]ISINK DIM Duty = %d\n", duty_mapping[level-1]); */
			/* LEDS_DEBUG("[LED]ISINK Current = %d\n", current_mapping[level-1]); */
			if ((previous_isink0_double != multiplier_mapping[level-1]) &&
			    ((pmic_type == MT65XX_LED_PMIC_LCD_ISINK) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK0) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK01)))
			{
				upmu_set_rg_isink0_double_en(0x0); /*Disable double current to avoid current spike.*/
			}
			if ((previous_isink1_double != multiplier_mapping[level-1]) &&
			   ((pmic_type == MT65XX_LED_PMIC_LCD_ISINK) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK01)))
			{
				upmu_set_rg_isink1_double_en(0x0); /*Disable double current to avoid current spike.*/
			}
			if ((pmic_type == MT65XX_LED_PMIC_LCD_ISINK) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK0) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK01))
			{
				upmu_set_isink_dim0_duty(duty_mapping[level-1]);
			}
			if ((pmic_type == MT65XX_LED_PMIC_LCD_ISINK) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK01))
			{
				upmu_set_isink_dim1_duty(duty_mapping[level-1]);
			}
			if (pmic_type == MT65XX_LED_PMIC_LCD_ISINK)
			{
				upmu_set_isink_dim2_duty(duty_mapping[level-1]);
				upmu_set_isink_dim3_duty(duty_mapping[level-1]);
			}
			if ((pmic_type == MT65XX_LED_PMIC_LCD_ISINK) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK0) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK01))
			{
				upmu_set_isink_ch0_step(current_mapping[level-1]);
			}
			if ((pmic_type == MT65XX_LED_PMIC_LCD_ISINK) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK01))
			{
				upmu_set_isink_ch1_step(current_mapping[level-1]);
			}
			if (pmic_type == MT65XX_LED_PMIC_LCD_ISINK)
			{
				upmu_set_isink_ch2_step(current_mapping[level-1]);
				upmu_set_isink_ch3_step(current_mapping[level-1]);
			}
			if (previous_isink0_double != multiplier_mapping[level-1])
			if ((pmic_type == MT65XX_LED_PMIC_LCD_ISINK) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK0) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK01))
			{
				upmu_set_rg_isink0_double_en(multiplier_mapping[level-1]);
				previous_isink0_double = multiplier_mapping[level-1];
			}
			if (previous_isink1_double != multiplier_mapping[level-1])
			if ((pmic_type == MT65XX_LED_PMIC_LCD_ISINK) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK01))
			{
				upmu_set_rg_isink1_double_en(multiplier_mapping[level-1]);
				previous_isink1_double = multiplier_mapping[level-1];
			}
			if ((pmic_type == MT65XX_LED_PMIC_LCD_ISINK) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK0) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK01))
			{
				upmu_set_isink_dim0_fsel(0x2); /* 20Khz */
			}
			if ((pmic_type == MT65XX_LED_PMIC_LCD_ISINK) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK01))
			{
				upmu_set_isink_dim1_fsel(0x2); /* 20Khz */
			}
			if (pmic_type == MT65XX_LED_PMIC_LCD_ISINK)
			{
				upmu_set_isink_dim2_fsel(0x2); /* 20Khz */
				upmu_set_isink_dim3_fsel(0x2); /* 20Khz */
			}
			if ((pmic_type == MT65XX_LED_PMIC_LCD_ISINK) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK0) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK01))
			{
				upmu_set_isink_ch0_en(0x1); /* Turn on ISINK Channel 0 */
			}
			if ((pmic_type == MT65XX_LED_PMIC_LCD_ISINK) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK01))
			{
				upmu_set_isink_ch1_en(0x1); /* Turn on ISINK Channel 1 */
			}
			if (pmic_type == MT65XX_LED_PMIC_LCD_ISINK)
			{
				upmu_set_isink_ch2_en(0x1); /* Turn on ISINK Channel 2 */
				upmu_set_isink_ch3_en(0x1); /* Turn on ISINK Channel 3 */
			}
			bl_duty_hal = level;
		}
		else
		{
			if ((pmic_type == MT65XX_LED_PMIC_LCD_ISINK) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK0) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK01))
			{
				upmu_set_isink_ch0_en(0x0); /* Turn off ISINK Channel 0 */
			}
			if ((pmic_type == MT65XX_LED_PMIC_LCD_ISINK) ||
			    (pmic_type == MT65XX_LED_PMIC_LCD_ISINK01))
			{
				upmu_set_isink_ch1_en(0x0); /* Turn off ISINK Channel 1 */
			}
			if (pmic_type == MT65XX_LED_PMIC_LCD_ISINK)
			{
				upmu_set_isink_ch2_en(0x0); /* Turn off ISINK Channel 2 */
				upmu_set_isink_ch3_en(0x0); /* Turn off ISINK Channel 3 */
			}
			bl_duty_hal = level;
		}
		mutex_unlock(&leds_pmic_mutex);
		return 0;
	}
	else if (pmic_type == MT65XX_LED_PMIC_NLED_ISINK1)
	{
		if (first_time == true)
		{
			upmu_set_isink_ch1_en(0x0); /* Turn off ISINK Channel 1 */
			first_time = false;
		}

		upmu_set_rg_drv_32k_ck_pdn(0x0); /* Disable power down */
		upmu_set_rg_isink1_ck_pdn(0x0); /* Disable power down */
		upmu_set_rg_isink1_ck_sel(0x0); /* Freq = 32KHz for Indicator */
		upmu_set_isink_dim1_duty(15); /* 16 / 32, no use for register mode */
		upmu_set_isink_ch1_mode(ISINK_PWM_MODE);
		upmu_set_isink_dim1_fsel(0x0); /* 1KHz, no use for register mode */
		upmu_set_isink_ch1_step(0x0); /* 4mA */
		upmu_set_isink_sfstr1_tc(0x0); /* 0.5us */
		upmu_set_isink_sfstr1_en(0x0); /* Disable soft start */
		upmu_set_rg_isink1_double_en(0x0); /* Disable double current */
		upmu_set_isink_phase1_dly_en(0x0); /* Disable phase delay */
		upmu_set_isink_chop1_en(0x0); /* Disable CHOP clk */

		if (level)
		{
			upmu_set_rg_drv_2m_ck_pdn(0x0); /* Disable power down (indicator no need?) */
			upmu_set_rg_drv_32k_ck_pdn(0x0); /* Disable power down */
			upmu_set_isink_ch1_en(0x1); /* Turn on ISINK Channel 1 */
		}
		else
		{
			upmu_set_isink_ch1_en(0x0); /* Turn off ISINK Channel 1 */
		}
		mutex_unlock(&leds_pmic_mutex);
		return 0;
	}
	else if (pmic_type == MT65XX_LED_PMIC_NLED_ISINK2)
	{
		if (first_time == true)
		{
			upmu_set_isink_ch2_en(0x0); /* Turn off ISINK Channel 2 */
			first_time = false;
		}

		upmu_set_rg_drv_32k_ck_pdn(0x0); /* Disable power down */
		upmu_set_rg_isink2_ck_pdn(0x0); /* Disable power down */
		upmu_set_rg_isink2_ck_sel(0x0); /* Freq = 32KHz for Indicator */
		upmu_set_isink_dim2_duty(15); /* 16 / 32, no use for register mode */
		upmu_set_isink_ch2_mode(ISINK_PWM_MODE);
		upmu_set_isink_dim2_fsel(0x0); /* 1KHz, no use for register mode */
		upmu_set_isink_ch2_step(0x0); /* 4mA */
		upmu_set_isink_sfstr2_tc(0x0); /* 0.5us */
		upmu_set_isink_sfstr2_en(0x0); /* Disable soft start */
		upmu_set_rg_isink2_double_en(0x0); /* Disable double current */
		upmu_set_isink_phase2_dly_en(0x0); /* Disable phase delay */
		upmu_set_isink_chop2_en(0x0); /* Disable CHOP clk */

		if (level)
		{
			upmu_set_rg_drv_2m_ck_pdn(0x0); /* Disable power down (indicator no need?) */
			upmu_set_rg_drv_32k_ck_pdn(0x0); /* Disable power down */
			upmu_set_isink_ch2_en(0x1); /* Turn on ISINK Channel 2 */
		}
		else
		{
			upmu_set_isink_ch2_en(0x0); /* Turn off ISINK Channel 2 */
		}
		mutex_unlock(&leds_pmic_mutex);
		return 0;
	}
	else if (pmic_type == MT65XX_LED_PMIC_NLED_ISINK3)
	{
		if (first_time == true)
		{
			upmu_set_isink_ch3_en(0x0); /* Turn off ISINK Channel 3 */
			first_time = false;
		}

		upmu_set_rg_drv_32k_ck_pdn(0x0); /* Disable power down */
		upmu_set_rg_isink3_ck_pdn(0x0); /* Disable power down */
		upmu_set_rg_isink3_ck_sel(0x0); /* Freq = 32KHz for Indicator */
		upmu_set_isink_dim3_duty(31); /* 100% duty cycle */
		upmu_set_isink_ch3_mode(ISINK_PWM_MODE);
		upmu_set_isink_dim3_fsel(0x0); /* 1KHz, no use for register mode */
		upmu_set_isink_ch3_step(0x4); /* 20mA */
		upmu_set_isink_sfstr3_tc(0x0); /* 0.5us */
		upmu_set_isink_sfstr3_en(0x0); /* Disable soft start */
		upmu_set_rg_isink3_double_en(0x0); /* Disable double current */
		upmu_set_isink_phase3_dly_en(0x0); /* Disable phase delay */
		upmu_set_isink_chop3_en(0x0); /* Disable CHOP clk */


		if (level)
		{
			upmu_set_rg_drv_2m_ck_pdn(0x0); /* Disable power down (indicator no need?) */
			upmu_set_rg_drv_32k_ck_pdn(0x0); /* Disable power down */
			upmu_set_isink_ch3_en(0x1); /* Turn on ISINK Channel 3 */
		}
		else
		{
			upmu_set_isink_ch3_en(0x0); /* Turn off ISINK Channel 3 */
		}
		mutex_unlock(&leds_pmic_mutex);
		return 0;
	}
	mutex_unlock(&leds_pmic_mutex);
	return -1;
}

int mt_brightness_set_pmic_duty_store(u32 level, u32 div)
{
	return -1;
}
int mt_mt65xx_led_set_cust(struct cust_mt65xx_led *cust, int level)
{
	struct nled_setting led_tmp_setting = {0, 0, 0};
	int tmp_level = level;
	LEDS_DEBUG("mt65xx_leds_set_cust: set brightness, name:%s, mode:%d, level:%d\n",
		cust->name, cust->mode, level);
	switch (cust->mode) {

		case MT65XX_LED_MODE_PWM:
			if (strcmp(cust->name, "lcd-backlight") == 0)
			{
				bl_brightness_hal = level;
				if (level == 0)
				{
					mt_pwm_disable(cust->data, cust->config_data.pmic_pad);

				} else
				{
					level = brightness_mapping(tmp_level);
					if (level == ERROR_BL_LEVEL)
						level = brightness_mapto64(tmp_level);

					mt_backlight_set_pwm(cust->data, level, bl_div_hal, &cust->config_data);
				}
				bl_duty_hal = level;

			} else
			{
				if (level == 0)
				{
					led_tmp_setting.nled_mode = NLED_OFF;
				} else
				{
					led_tmp_setting.nled_mode = NLED_ON;
				}
				mt_led_set_pwm(cust->data, &led_tmp_setting);
			}
			return 1;

		case MT65XX_LED_MODE_GPIO:
			LEDS_DEBUG("brightness_set_cust:go GPIO mode!!!!!\n");
			return ((cust_set_brightness)(cust->data))(level);

		case MT65XX_LED_MODE_PMIC:
			return mt_brightness_set_pmic(cust->data, level, bl_div_hal);

		case MT65XX_LED_MODE_CUST_LCM:
        		if (strcmp(cust->name, "lcd-backlight") == 0)
			{
				bl_brightness_hal = level;
			}
			LEDS_DEBUG("brightness_set_cust:backlight control by LCM\n");
			return ((cust_brightness_set)(cust->data))(level, bl_div_hal);


		case MT65XX_LED_MODE_CUST_BLS_PWM:
			if (strcmp(cust->name, "lcd-backlight") == 0)
			{
				bl_brightness_hal = level;
			}
			return ((cust_set_brightness)(cust->data))(level);

		case MT65XX_LED_MODE_NONE:
		default:
			break;
	}
	return -1;
}

void mt_mt65xx_led_work(struct work_struct *work)
{
	struct mt65xx_led_data *led_data =
		container_of(work, struct mt65xx_led_data, work);

	LEDS_DEBUG("[LED]%s:%d\n", led_data->cust.name, led_data->level);
	mutex_lock(&leds_mutex);
	mt_mt65xx_led_set_cust(&led_data->cust, led_data->level);
	mutex_unlock(&leds_mutex);
}

void mt_mt65xx_led_set(struct led_classdev *led_cdev, enum led_brightness level)
{
	struct mt65xx_led_data *led_data =
		container_of(led_cdev, struct mt65xx_led_data, cdev);

#ifdef LED_INCREASE_LED_LEVEL_MTKPATCH

		if (level >> LED_RESERVEBIT_SHIFT)
		{
			if (LED_RESERVEBIT_PATTERN != (level >> LED_RESERVEBIT_SHIFT))
			{
				/* sanity check for hidden code */
				printk("incorrect input : %d,%d\n" , level , (level >> LED_RESERVEBIT_SHIFT));
				return;
			}

			if (MT65XX_LED_MODE_CUST_BLS_PWM != led_data->cust.mode)
			{
				/* only BLS PWM support expand bit */
				printk("Not BLS PWM %d\n" , led_data->cust.mode);
				return;
			}

			level &= ((1 << LED_RESERVEBIT_SHIFT) - 1);

			if ((level + 1) > (1 << MT_LED_INTERNAL_LEVEL_BIT_CNT))
			{
				/* clip to max value */
				level = (1 << MT_LED_INTERNAL_LEVEL_BIT_CNT) - 1;
			}

			led_cdev->brightness = ((level + (1 << (MT_LED_INTERNAL_LEVEL_BIT_CNT - 9))) >> (MT_LED_INTERNAL_LEVEL_BIT_CNT - 8));/* brightness is 8 bit level */
			if (led_cdev->brightness > led_cdev->max_brightness)
			{
				led_cdev->brightness = led_cdev->max_brightness;
			}

			if (led_data->level != level)
			{
				led_data->level = level;
				mt_mt65xx_led_set_cust(&led_data->cust, led_data->level);
			}
		}
		else
		{
			if (led_data->level != level)
			{
				led_data->level = level;
				if (strcmp(led_data->cust.name, "lcd-backlight") != 0)
				{
					LEDS_DEBUG("[LED]Set NLED directly %d at time %lu\n", led_data->level, jiffies);
					schedule_work(&led_data->work);
				}
				else
				{
					LEDS_DEBUG("[LED]Set Backlight directly %d at time %lu\n", led_data->level, jiffies);
					if (MT65XX_LED_MODE_CUST_BLS_PWM == led_data->cust.mode)
					{
						mt_mt65xx_led_set_cust(&led_data->cust, ((((1 << MT_LED_INTERNAL_LEVEL_BIT_CNT) - 1)*level + 127)/255));
					}
					else
					{
						mt_mt65xx_led_set_cust(&led_data->cust, led_data->level);
					}
				}
			}
		}
#else
	/* do something only when level is changed */
	if (led_data->level != level) {
		led_data->level = level;
		if (strcmp(led_data->cust.name, "lcd-backlight"))
		{
			schedule_work(&led_data->work);
		}
		else
		{
			LEDS_DEBUG("[LED]Set Backlight directly %d at time %lu\n", led_data->level, jiffies);
			mt_mt65xx_led_set_cust(&led_data->cust, led_data->level);
		}
	}
#endif
}

int  mt_mt65xx_blink_set(struct led_classdev *led_cdev,
			unsigned long *delay_on,
			unsigned long *delay_off)
{
	struct mt65xx_led_data *led_data =
		container_of(led_cdev, struct mt65xx_led_data, cdev);
	static int got_wake_lock;
	struct nled_setting nled_tmp_setting = {0, 0, 0};

	/* only allow software blink when delay_on or delay_off changed */
	if (*delay_on != led_data->delay_on || *delay_off != led_data->delay_off) {
		led_data->delay_on = *delay_on;
		led_data->delay_off = *delay_off;
		if (led_data->delay_on && led_data->delay_off) { /* enable blink */
			led_data->level = 255; /* when enable blink  then to set the level  (255) */
			/* AP PWM all support OLD mode in MT6589 */
			if (led_data->cust.mode == MT65XX_LED_MODE_PWM)
			{
				nled_tmp_setting.nled_mode = NLED_BLINK;
				nled_tmp_setting.blink_off_time = led_data->delay_off;
				nled_tmp_setting.blink_on_time = led_data->delay_on;
				mt_led_set_pwm(led_data->cust.data, &nled_tmp_setting);
				return 0;
			}
			else if ((led_data->cust.mode == MT65XX_LED_MODE_PMIC) && (led_data->cust.data == MT65XX_LED_PMIC_NLED_ISINK0
				|| led_data->cust.data == MT65XX_LED_PMIC_NLED_ISINK1 || led_data->cust.data == MT65XX_LED_PMIC_NLED_ISINK2))
			{
				/* if(get_chip_eco_ver() == CHIP_E2) { */
				if (1) {
					nled_tmp_setting.nled_mode = NLED_BLINK;
					nled_tmp_setting.blink_off_time = led_data->delay_off;
					nled_tmp_setting.blink_on_time = led_data->delay_on;
					mt_led_blink_pmic(led_data->cust.data, &nled_tmp_setting);
					return 0;
				} else {
					wake_lock(&leds_suspend_lock);
				}
			}
			else if (!got_wake_lock) {
				wake_lock(&leds_suspend_lock);
				got_wake_lock = 1;
			}
		}
		else if (!led_data->delay_on && !led_data->delay_off) { /* disable blink */
			/* AP PWM all support OLD mode in MT6589 */
			if (led_data->cust.mode == MT65XX_LED_MODE_PWM)
			{
				nled_tmp_setting.nled_mode = NLED_OFF;
				mt_led_set_pwm(led_data->cust.data, &nled_tmp_setting);
				return 0;
			}
			else if ((led_data->cust.mode == MT65XX_LED_MODE_PMIC) && (led_data->cust.data == MT65XX_LED_PMIC_NLED_ISINK0
				|| led_data->cust.data == MT65XX_LED_PMIC_NLED_ISINK1 || led_data->cust.data == MT65XX_LED_PMIC_NLED_ISINK2))
			{
				/* if(get_chip_eco_ver() == CHIP_E2) { */
				if (1) {
					mt_brightness_set_pmic(led_data->cust.data, 0, 0);
					return 0;
				} else {
					wake_unlock(&leds_suspend_lock);
				}
			}
			else if (got_wake_lock) {
				wake_unlock(&leds_suspend_lock);
				got_wake_lock = 0;
			}
		}
		return -1;
	}

	/* delay_on and delay_off are not changed */
	return 0;
}
