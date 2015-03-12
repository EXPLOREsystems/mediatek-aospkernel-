/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/*******************************************************************************
 *
 * Filename:
 * ---------
 *   AudDrv_Kernelc
 *
 * Project:
 * --------
 *    Audio smart pa Function
 *
 * Description:
 * ------------
 *   Audio register
 *
 * Author:
 * -------
 * Chipeng Chang
 *
 *------------------------------------------------------------------------------
 * $Revision: #1 $
 * $Modtime:$
 * $Log:$
 *
 *
 *******************************************************************************/


/*****************************************************************************
 *                     C O M P I L E R   F L A G S
 *****************************************************************************/


/*****************************************************************************
 *                E X T E R N A L   R E F E R E N C E S
 *****************************************************************************/
#include "AudDrv_NXP.h"

#define TFA_I2C_CHANNEL     (3)

#define ECODEC_SLAVE_ADDR_WRITE 0x6c
#define ECODEC_SLAVE_ADDR_READ  0x6d

#define NXPEXTSPK_I2C_DEVNAME "TFA9887"

/*****************************************************************************
*           DEFINE AND CONSTANT
******************************************************************************
*/

#define AUDDRV_NXPSPK_NAME   "MediaTek Audio NXPSPK Driver"
#define AUDDRV_AUTHOR "MediaTek WCX"
#define RW_BUFFER_LENGTH (256)

/*****************************************************************************
*           V A R I A B L E     D E L A R A T I O N
*******************************************************************************/

static char auddrv_nxpspk_name[] = "AudioMTKNXPSPK";
/* I2C variable */
static struct i2c_client *new_client;
char WriteBuffer[RW_BUFFER_LENGTH];
char ReadBuffer[RW_BUFFER_LENGTH];


/* new I2C register method */
static const struct i2c_device_id NXPExt_i2c_id[] = { {NXPEXTSPK_I2C_DEVNAME, 0}, {} };
static struct i2c_board_info NXPExt_dev __initdata =
    { I2C_BOARD_INFO(NXPEXTSPK_I2C_DEVNAME, (ECODEC_SLAVE_ADDR_WRITE >> 1)) };

/* function declration */
static int NXPExtSpk_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int NXPExtSpk_i2c_remove(struct i2c_client *client);
void AudDrv_NXPSpk_Init(void);
bool NXPExtSpk_Register(void);
static int NXPExtSpk_register(void);
ssize_t NXPSpk_read_byte(u8 addr, u8 *returnData);


/* i2c driver */
struct i2c_driver NXPExtSpk_i2c_driver = {
	.probe = NXPExtSpk_i2c_probe,
	.remove = NXPExtSpk_i2c_remove,
	.driver = {
		   .name = NXPEXTSPK_I2C_DEVNAME,
		   },
	.id_table = NXPExt_i2c_id,
};

static int NXPExtSpk_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	new_client = client;
	new_client->timing = 400;
	printk("NXPExtSpk_i2c_probe\n");
	/* printk("client new timing=%dK\n", new_client->timing); */
	return 0;
}

static int NXPExtSpk_i2c_remove(struct i2c_client *client)
{
	new_client = NULL;
	i2c_unregister_device(client);
	i2c_del_driver(&NXPExtSpk_i2c_driver);
	return 0;
}

/* read write implementation */
/* read one register */
ssize_t NXPSpk_read_byte(u8 addr, u8 *returnData)
{
	char cmd_buf[1] = { 0x00 };
	char readData = 0;
	int ret = 0;
	cmd_buf[0] = addr;

	if (!new_client) {
		printk("NXPSpk_read_byte I2C client not initialized!!");
		return -1;
	}
	ret = i2c_master_send(new_client, &cmd_buf[0], 1);
	if (ret < 0) {
		printk("NXPSpk_read_byte read sends command error!!\n");
		return -1;
	}
	ret = i2c_master_recv(new_client, &readData, 1);
	if (ret < 0) {
		printk("NXPSpk_read_byte reads recv data error!!\n");
		return -1;
	}
	*returnData = readData;
	/* printk("addr 0x%x data 0x%x\n", addr, readData); */
	return 0;
}

/* write register */
ssize_t NXPExt_write_byte(u8 addr, u8 writeData)
{
	char write_data[2] = { 0 };
	int ret = 0;
	if (!new_client) {
		printk("I2C client not initialized!!");
		return -1;
	}
	write_data[0] = addr;	/* ex. 0x01 */
	write_data[1] = writeData;
	ret = i2c_master_send(new_client, write_data, 2);
	if (ret < 0) {
		printk("write sends command error!!");
		return -1;
	}
	/* printk("addr 0x%x data 0x%x\n", addr, writeData); */
	return 0;
}


static int NXPExtSpk_register(void)
{
	printk("NXPExtSpk_register\n");
	i2c_register_board_info(TFA_I2C_CHANNEL, &NXPExt_dev, 1);
	if (i2c_add_driver(&NXPExtSpk_i2c_driver)) {
		printk("fail to add device into i2c");
		return -1;
	}
	return 0;
}


bool NXPExtSpk_Register(void)
{
	printk("NXPExtSpk_Register\n");
	NXPExtSpk_register();
	return true;
}

void AudDrv_NXPSpk_Init(void)
{
	printk("Set GPIO for AFE I2S output to external DAC\n");
}

/*****************************************************************************
 * FILE OPERATION FUNCTION
 *  AudDrv_nxpspk_ioctl
 *
 * DESCRIPTION
 *  IOCTL Msg handle
 *
 *****************************************************************************
 */
static long AudDrv_nxpspk_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;

	/* printk("AudDrv_nxpspk_ioctl cmd = 0x%x arg = %lu\n", cmd, arg); */

	switch (cmd) {
	default:
		{
			/* printk("AudDrv_nxpspk_ioctl Fail command: %x\n", cmd); */
			ret = -1;
			break;
		}
	}
	return ret;
}

static int AudDrv_nxpspk_probe(struct platform_device *dev)
{
	int ret = 0;
	printk("AudDrv_nxpspk_probe\n");

	if (ret < 0) {
		printk("AudDrv_nxpspk_probe request_irq MT6582_AP_BT_CVSD_IRQ_LINE Fail\n");
	}
	NXPExtSpk_Register();
	AudDrv_NXPSpk_Init();

	memset((void *)WriteBuffer, 0, RW_BUFFER_LENGTH);
	memset((void *)ReadBuffer, 0, RW_BUFFER_LENGTH);

	printk("-AudDrv_nxpspk_probe\n");
	return 0;
}

static int AudDrv_nxpspk_open(struct inode *inode, struct file *fp)
{
	return 0;
}

static ssize_t AudDrv_nxpspk_write(struct file *fp, const char __user *data, size_t count,
				   loff_t *offset)
{
	int written_size = count;
	int ret = 0;
	char *Write_ptr = WriteBuffer;
	unsigned char TempBuffer[3];
	/* printk("AudDrv_nxpspk_write count = %d\n", count); */
	if (!access_ok(VERIFY_READ, data, count)) {
		printk("AudDrv_nxpspk_write !access_ok\n");
		return count;
	} else {
		/* copy data from user space */
		if (copy_from_user(WriteBuffer, data, count)) {
			printk("printk Fail copy from user\n");
			return -1;
		}

		/* printk("data0 = 0x%x\n",  WriteBuffer[0]); */
		TempBuffer[0] = WriteBuffer[0];
		/* printk("written_size = %d\n", written_size); */
		Write_ptr++;
		if (written_size == 1) {
			/* printk("send first data\n"); */
			ret = i2c_master_send(new_client, &TempBuffer[0], 1);
			Write_ptr++;
			written_size--;
		}

		while (written_size >= 3) {
			TempBuffer[1] = *Write_ptr;
			Write_ptr++;
			TempBuffer[2] = *Write_ptr;
			Write_ptr++;
			ret = i2c_master_send(new_client, &TempBuffer[0], 3);
			written_size -= 2;
		}

		if (written_size == 2) {
			/* printk("send last data\n"); */
			TempBuffer[1] = *Write_ptr;
			ret = i2c_master_send(new_client, &TempBuffer[0], 2);
			written_size--;
		}

		if (ret < 0) {
			printk("write sends command error!!");
			return 0;
		}
	}
	return count;
}

static ssize_t AudDrv_nxpspk_read(struct file *fp, char __user *data, size_t count,
				  loff_t *offset)
{
	int read_count = count;
	int ret = 0;
	char *Read_ptr = &ReadBuffer[0];
	/* printk("AudDrv_nxpspk_read  count = %d\n", count); */
	if (!access_ok(VERIFY_READ, data, count)) {
		printk("AudDrv_nxpspk_read !access_ok\n");
		return count;
	} else {
		/* copy data from user space */
		if (copy_from_user(ReadBuffer, data, count)) {
			printk("printk Fail copy from user\n");
			return -1;
		}
		/* printk("data0 = 0x%x data1 = 0x%x\n",  ReadBuffer[0], ReadBuffer[1]); */

		/*
		   ret = i2c_master_send(new_client,  &ReadBuffer[0], 1);
		   if (ret < 0)
		   {
		   printk("AudDrv_nxpspk_read read sends command error!!\n");
		   return -1;
		   }
		 */

		/* printk("i2c_master_recv read_count = %d\n", read_count); */
		ret = i2c_master_recv(new_client, Read_ptr, read_count);

		if (ret < 0) {
			printk("write sends command error!!");
			return 0;
		}
		/* printk("data0 = 0x%x data1 = 0x%x\n",  ReadBuffer[0], ReadBuffer[1]); */
		if (copy_to_user((void __user *)data, (void *)ReadBuffer, count)) {
			printk("printk Fail copy from user\n");
			return -1;
		}
	}
	return count;
}


/**************************************************************************
 * STRUCT
 *  File Operations and misc device
 *
 **************************************************************************/

static struct file_operations AudDrv_nxpspk_fops = {
	.owner = THIS_MODULE,
	.open = AudDrv_nxpspk_open,
	.unlocked_ioctl = AudDrv_nxpspk_ioctl,
	.write = AudDrv_nxpspk_write,
	.read = AudDrv_nxpspk_read,
};

static struct miscdevice AudDrv_nxpspk_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "nxpspk",
	.fops = &AudDrv_nxpspk_fops,
};

/***************************************************************************
 * FUNCTION
 *  AudDrv_nxpspk_mod_init / AudDrv_nxpspk_mod_exit
 *
 * DESCRIPTION
 *  Module init and de-init (only be called when system boot up)
 *
 **************************************************************************/

static struct platform_driver AudDrv_nxpspk = {
	.probe = AudDrv_nxpspk_probe,
	.driver = {
		   .name = auddrv_nxpspk_name,
		   },
};

static struct platform_device *AudDrv_NXPSpk_dev;

static int AudDrv_nxpspk_mod_init(void)
{
	int ret = 0;
	printk("+AudDrv_nxpspk_mod_init\n");


	printk("platform_device_alloc\n");
	AudDrv_NXPSpk_dev = platform_device_alloc("AudioMTKNXPSPK", -1);
	if (!AudDrv_NXPSpk_dev) {
		return -ENOMEM;
	}

	printk("platform_device_add\n");

	ret = platform_device_add(AudDrv_NXPSpk_dev);
	if (ret != 0) {
		platform_device_put(AudDrv_NXPSpk_dev);
		return ret;
	}
	/* Register platform DRIVER */
	ret = platform_driver_register(&AudDrv_nxpspk);
	if (ret) {
		printk("AudDrv Fail:%d - Register DRIVER\n", ret);
		return ret;
	}
	/* register MISC device */
	if ((ret = misc_register(&AudDrv_nxpspk_device))) {
		printk("AudDrv_nxpspk_mod_init misc_register Fail:%d\n", ret);
		return ret;
	}

	printk("-AudDrv_nxpspk_mod_init\n");
	return 0;
}

static void AudDrv_nxpspk_mod_exit(void)
{
	printk("+AudDrv_nxpspk_mod_exit\n");

	printk("-AudDrv_nxpspk_mod_exit\n");
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(AUDDRV_NXPSPK_NAME);
MODULE_AUTHOR(AUDDRV_AUTHOR);

module_init(AudDrv_nxpspk_mod_init);
module_exit(AudDrv_nxpspk_mod_exit);

EXPORT_SYMBOL(NXPSpk_read_byte);
EXPORT_SYMBOL(NXPExt_write_byte);
