#ifndef TOUCHPANEL_H__
#define TOUCHPANEL_H__

/* Pre-defined definition */
#define TPD_TYPE_CAPACITIVE
#define TPD_TYPE_RESISTIVE
#define TPD_I2C_NUMBER					0

#define VELOCITY_CUSTOM
#define TPD_VELOCITY_CUSTOM_X 			15
#define TPD_VELOCITY_CUSTOM_Y 			15

/* #define TPD_CLOSE_POWER_IN_SLEEP */
#define TPD_POWER_SOURCE_CUSTOM		 	MT6323_POWER_LDO_VIO28

#define MAX_TRANSACTION_LENGTH			8
#define MAX_I2C_TRANSFER_SIZE			(MAX_TRANSACTION_LENGTH - 2)
#define I2C_MASTER_CLOCK				400
#define MXT_I2C_ADDRESS				 	0x4A

#define TPD_DELAY						(2*HZ/100)

/* #define TPD_HAVE_BUTTON */
#define TPD_BUTTON_HEIGH				(100)
#define TPD_KEY_COUNT				   	3
#define TPD_KEYS						{ KEY_MENU, KEY_HOMEPAGE , KEY_BACK}
#define TPD_KEYS_DIM					{{90, 1010, 120, TPD_BUTTON_HEIGH}, {270, 1010, 120, TPD_BUTTON_HEIGH}, {450, 1010, 120, TPD_BUTTON_HEIGH} }


/* GPIO macro */
#define SET_GPIO_AS_INPUT(pin)	\
	do {\
		if (pin == GPIO_CTP_EINT_PIN)\
			mt_set_gpio_mode(pin, GPIO_CTP_EINT_PIN_M_GPIO);\
		else\
			mt_set_gpio_mode(pin, GPIO_CTP_RST_PIN_M_GPIO);\
		mt_set_gpio_dir(pin, GPIO_DIR_IN);\
		mt_set_gpio_pull_enable(pin, GPIO_PULL_DISABLE);\
	} while (0)

#define SET_GPIO_AS_INT(pin)	\
	do {\
		mt_set_gpio_mode(pin, GPIO_CTP_EINT_PIN_M_EINT);\
		mt_set_gpio_dir(pin, GPIO_DIR_IN);\
		mt_set_gpio_pull_enable(pin, GPIO_PULL_DISABLE);\
	} while (0)

#endif				/* TOUCHPANEL_H__ */
