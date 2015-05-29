/*
 * cyttsp5_i2c.c
 * Cypress TrueTouch(TM) Standard Product V5 I2C Module.
 * For use with Cypress Txx5xx parts.
 * Supported parts include:
 * TMA5XX
 *
 * Copyright (C) 2012-2014 Cypress Semiconductor
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, and only version 2, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Contact Cypress Semiconductor at www.cypress.com <ttdrivers@cypress.com>
 *
 */

#include "cyttsp5_regs.h"

#include <linux/i2c.h>
#include <linux/version.h>
#include <linux/dma-mapping.h>
#include <cust_eint.h>
#include "tpd.h"
#include "tpd_custom_tma445a.h"
#include "cyttsp5_core.h"

#define CY_I2C_DATA_SIZE  (2 * 256)

#define TPD_DMA_MAX_TRANSACTION_LENGTH  512
#define TPD_DMA_MAX_I2C_TRANSFER_SIZE (TPD_DMA_MAX_TRANSACTION_LENGTH-1)

extern struct cyttsp5_platform_data _cyttsp5_platform_data;

static const struct i2c_device_id cyttsp5_i2c_id[] = {{CYTTSP5_DEV_NAME,0},{}};
static struct i2c_board_info __initdata tma445a_i2c_tpd={ I2C_BOARD_INFO(CYTTSP5_DEV_NAME, TMA445A_I2C_ADDRESS), .platform_data = &_cyttsp5_platform_data, .irq = CUST_EINT_TOUCH_PANEL_NUM};

static u8 *TMAI2CDMABuf_va = NULL;
static u64 TMAI2CDMABuf_pa = NULL;

//Declare
extern struct tpd_device *tpd;
static struct i2c_driver tpd_i2c_driver;

static s32 i2c_dma_read(struct i2c_client *client, u8 *rxbuf, s32 len)
{
	int ret;
	s32 retry = 0;

	struct i2c_msg msg[1] =
	{
		{
			.addr = (client->addr & I2C_MASK_FLAG),
			.ext_flag = (client->ext_flag | I2C_ENEXT_FLAG | I2C_DMA_FLAG),
			.flags = I2C_M_RD,
			.buf = TMAI2CDMABuf_pa,
			.len = len,
			.timing = I2C_MASTER_CLOCK
		}
	};

	if (rxbuf == NULL)
		return -1;

	for (retry = 0; retry < 10; ++retry)
	{
		ret = i2c_transfer(client->adapter, &msg[0], 1);
		if (ret < 0)
		{
			TPD_DMESG("%s: I2C DMA read error retry=%d\n", __func__, retry);
			continue;
		}
		if (ret != 1)
		{
			TPD_DMESG("%s: I2C transfer error=%d\n", __func__, ret);
			return ret;
		}
		memcpy(rxbuf, TMAI2CDMABuf_va, len);
		return 0;
	}
	TPD_DMESG("%s: Dma I2C Read Error: %d byte(s), err-code: %d\n", __func__, len, ret);
	return ret;
}


static s32 i2c_dma_write(struct i2c_client *client, u8 *txbuf, s32 len)
{
	int ret;
	s32 retry = 0;
	u8 *wr_buf = TMAI2CDMABuf_va;

	struct i2c_msg msg =
	{
		.addr = (client->addr & I2C_MASK_FLAG),
		.ext_flag = (client->ext_flag | I2C_ENEXT_FLAG | I2C_DMA_FLAG),
		.flags = 0,
		.buf = TMAI2CDMABuf_pa,
		.len = len,
		.timing = I2C_MASTER_CLOCK
	};

	if (txbuf == NULL)
		return -1;

	memcpy(wr_buf, txbuf, len);

	for (retry = 0; retry < 5; ++retry)
	{
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret < 0)
		{
			TPD_DMESG("%s: I2C DMA write error retry=%d\n", __func__, retry);
			continue;
		}
		if (ret != 1)
		{
			TPD_DMESG("%s: I2C transfer error=%d\n", __func__, ret);
			return ret;
		}
		return 0;
	}
	TPD_DMESG("%s: Dma I2C Write Error: %d byte(s), err-code: %d\n", __func__, len, ret);
	return ret;
}

static s32 i2c_read_bytes_dma(struct i2c_client *client, u8 *rxbuf, s32 len)
{
	s32 left = len;
	s32 read_len = 0;
	u8 *rd_buf = rxbuf;
	s32 ret = 0;

	while (left > 0)
	{
		if (left > TPD_DMA_MAX_TRANSACTION_LENGTH)
		{
			read_len = TPD_DMA_MAX_TRANSACTION_LENGTH;
		}
		else
		{
			read_len = left;
		}
		ret = i2c_dma_read(client, rd_buf, read_len);
		if (ret < 0)
		{
			TPD_DEBUG("%s: dma read failed\n", __func__);
			return -1;
		}

		left -= read_len;
		rd_buf += read_len;
	}
	return len;
}

static s32 i2c_write_bytes_dma(struct i2c_client *client, u8 *txbuf, s32 len)
{

	s32 ret = 0;
	s32 write_len = 0;
	s32 left = len;
	u8 *wr_buf = txbuf;

	while (left > 0)
	{
		if (left > TPD_DMA_MAX_I2C_TRANSFER_SIZE)
		{
			write_len = TPD_DMA_MAX_I2C_TRANSFER_SIZE;
		}
		else
		{
			write_len = left;
		}
		ret = i2c_dma_write(client, wr_buf, write_len);

		if (ret < 0)
		{
			TPD_DEBUG("%s:dma i2c write failed!\n", __func__);
			return -1;
		}

		left -= write_len;
		wr_buf += write_len;
	}
	return 0;
}

static int cyttsp5_i2c_read_default(struct device *dev, void *buf, int size)
{
	struct i2c_client *client = to_i2c_client(dev);
	int rc;

	if (!buf || !size || size > CY_I2C_DATA_SIZE)
		return -EINVAL;

	if(size<=MAX_TRANSACTION_LENGTH)
		rc = i2c_master_recv(client, buf, size);
	else
		rc = i2c_read_bytes_dma(client, buf, size);

	return (rc < 0) ? rc : rc != size ? -EIO : 0;
}

static int cyttsp5_i2c_read_default_nosize(struct device *dev, u8 *buf, u32 max)
{
	int rc;
	u32 size;

	if (!buf)
		return -EINVAL;

	rc = cyttsp5_i2c_read_default(dev, buf, 2);

	if (rc < 0)
		return (rc < 0) ? rc : -EIO;

	size = get_unaligned_le16(&buf[0]);
	if (!size || size == 2)
		return 0;

	if (size > max)
		return -EINVAL;

	rc= cyttsp5_i2c_read_default(dev, buf, size);

	return rc;
}

static int cyttsp5_i2c_write_read_specific(struct device *dev, u8 write_len,
		u8 *write_buf, u8 *read_buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	int rc;

	if (!write_buf || !write_len)
		return -EINVAL;

	rc = i2c_write_bytes_dma(client, write_buf, write_len);

	if (rc < 0)
		return (rc < 0) ? rc : -EIO;

	rc = 0;

	if (read_buf)
		rc = cyttsp5_i2c_read_default_nosize(dev, read_buf,
				CY_I2C_DATA_SIZE);

	return rc;
}

static struct cyttsp5_bus_ops cyttsp5_i2c_bus_ops = {
	.bustype = BUS_I2C,
	.read_default = cyttsp5_i2c_read_default,
	.read_default_nosize = cyttsp5_i2c_read_default_nosize,
	.write_read_specific = cyttsp5_i2c_write_read_specific,
};

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICETREE_SUPPORT
static struct of_device_id cyttsp5_i2c_of_match[] = {
	{ .compatible = "cy,cyttsp5_i2c_adapter", },
	{ }
};
MODULE_DEVICE_TABLE(of, cyttsp5_i2c_of_match);
#endif

static int cyttsp5_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *i2c_id)
{
#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICETREE_SUPPORT
	const struct of_device_id *match;
#endif
	int rc;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		TPD_DMESG("I2C functionality not Supported\n");
		return -EIO;
	}

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICETREE_SUPPORT
	match = of_match_device(of_match_ptr(cyttsp5_i2c_of_match), dev);
	if (match) {
		rc = cyttsp5_devtree_create_and_get_pdata(dev);
		if (rc < 0)
			return rc;
	}
#endif

	TMAI2CDMABuf_va = (u8 *)dma_alloc_coherent(NULL, TPD_DMA_MAX_TRANSACTION_LENGTH, &TMAI2CDMABuf_pa, GFP_KERNEL);
	if(!TMAI2CDMABuf_va)
	{
		TPD_DMESG("dma_alloc_coherent error\n");
	}

	rc = cyttsp5_probe(&cyttsp5_i2c_bus_ops, &client->dev, client->irq,
			  CY_I2C_DATA_SIZE);

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICETREE_SUPPORT
	if (rc && match)
		cyttsp5_devtree_clean_pdata(dev);
#endif

	return rc;
}

static int cyttsp5_i2c_remove(struct i2c_client *client)
{
#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICETREE_SUPPORT
	struct device *dev = &client->dev;
	const struct of_device_id *match;
#endif
	struct cyttsp5_core_data *cd = i2c_get_clientdata(client);

	cyttsp5_release(cd);

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICETREE_SUPPORT
	match = of_match_device(of_match_ptr(cyttsp5_i2c_of_match), dev);
	if (match)
		cyttsp5_devtree_clean_pdata(dev);
#endif

	return 0;
}



static int tpd_local_init(void)
{
	TPD_DMESG("TMA445A I2C Touchscreen Driver (Built %s @ %s)\n", __DATE__, __TIME__);

	if (i2c_add_driver(&tpd_i2c_driver) != 0) {
		TPD_DMESG("unable to add TMA445A i2c driver.");
		return -1;
	}

	if (tpd_load_status == 0)	/* if(tpd_load_status == 0) // disable auto load touch driver for linux3.0 porting */
	{
		TPD_DMESG("add error TMA445A touch panel driver.\n");
		i2c_del_driver(&tpd_i2c_driver);
		return -1;
	}

	input_set_abs_params(tpd->dev, ABS_MT_TRACKING_ID, 0, 4, 0, 0);

#ifdef TPD_HAVE_BUTTON
	tpd_button_setting(TPD_KEY_COUNT, tpd_keys_local, tpd_keys_dim_local);	/* initialize tpd button data */
#endif

#if (defined(TPD_WARP_START) && defined(TPD_WARP_END))
	TPD_DO_WARP = 1;
	memcpy(tpd_wb_start, tpd_wb_start_local, TPD_WARP_CNT * 4);
	memcpy(tpd_wb_end, tpd_wb_start_local, TPD_WARP_CNT * 4);
#endif

#if (defined(TPD_HAVE_CALIBRATION) && !defined(TPD_CUSTOM_CALIBRATION))
	memcpy(tpd_calmat, tpd_def_calmat_local, 8 * 4);
	memcpy(tpd_def_calmat, tpd_def_calmat_local, 8 * 4);
#endif


	TPD_DMESG("end %s, %d\n", __func__, __LINE__);
	tpd_type_cap = 1;

	return 0;
}



static struct i2c_driver tpd_i2c_driver = {
	.driver = {
		.name = CYTTSP5_DEV_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICETREE_SUPPORT
		.of_match_table = cyttsp5_i2c_of_match,
#endif
	},
	.probe = cyttsp5_i2c_probe,
	.remove = cyttsp5_i2c_remove,
	.id_table = cyttsp5_i2c_id,
};

static struct tpd_driver_t tpd_device_driver = {
	.tpd_device_name = CYTTSP5_DEV_NAME,
	.tpd_local_init = tpd_local_init,
#ifdef CONFIG_PM_SLEEP
	.suspend = tma445a_core_suspend,
	.resume = tma445a_core_resume,
#endif
#ifdef TPD_HAVE_BUTTON
	.tpd_have_button = 1,
#else
	.tpd_have_button = 0,
#endif
};

static int __init cyttsp5_i2c_init(void)
{
	TPD_DMESG("MedaTek TMA445A touch panel driver init\n");

	i2c_register_board_info(TPD_I2C_NUMBER, &tma445a_i2c_tpd, 1);
	if(tpd_driver_add(&tpd_device_driver) < 0)
		TPD_DMESG("add TMA445A driver failed\n");
	return 0;

}

static void __exit cyttsp5_i2c_exit(void)
{
	TPD_DMESG("MedaTek TMA445A touch panel driver exit\n");
	dma_free_coherent(NULL, TPD_DMA_MAX_TRANSACTION_LENGTH, TMAI2CDMABuf_va,
			   TMAI2CDMABuf_pa);
	tpd_driver_remove(&tpd_device_driver);
}

module_init(cyttsp5_i2c_init);
module_exit(cyttsp5_i2c_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cypress TrueTouch I2C driver");

