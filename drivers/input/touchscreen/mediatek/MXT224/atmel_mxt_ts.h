
/*
 * Atmel maXTouch Touchscreen driver
 *
 * Copyright (C) 2010 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __LINUX_ATMEL_MXT_TS_H
#define __LINUX_ATMEL_MXT_TS_H

#include <linux/types.h>

/* The platform data for the Atmel maXTouch touchscreen driver */
struct mxt_platform_data {
	unsigned long irqflags;
	u8 t19_num_keys;
	const unsigned int *t19_keymap;
	int t15_num_keys;
	const unsigned int *t15_keymap;
	unsigned long gpio_reset;
	const char *cfg_name;
	const char *input_name;
};

#define MXT224_DEV_NAME "atmel_mxt_ts"

#define MXT224_I2C_DMA
#define TPD_DMA_MAX_TRANSACTION_LENGTH  512
#define TPD_DMA_MAX_I2C_TRANSFER_SIZE (TPD_DMA_MAX_TRANSACTION_LENGTH-1)
#define MXT_BACKUP_TIME		50	/* msec */

#endif /* __LINUX_ATMEL_MXT_TS_H */
