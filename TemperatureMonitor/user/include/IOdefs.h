/*
 * IOdefs.h
 *
 *  Created on: 17 Jan 2017
 *      Author: User
 */

#ifndef USER_INCLUDE_IODEFS_H_
#define USER_INCLUDE_IODEFS_H_

typedef enum { _OFF = 0, _ON, AUTO } override_t;

#ifdef SLEEP_MODE
#undef OUTPUTS
#undef SWITCH
#else
#define SWITCH 0 // GPIO 00
#ifdef USE_INPUTS
#define SENSOR_1 14
#endif
#ifdef USE_OUTPUTS
#ifdef USE_I2C
#define OUTPUTS 8
#else
#define RELAY_1 16
#define RELAY_2 12
#define RELAY_3 13
#define RELAY_4 4
#endif
#ifdef INVERT_RELAYS
#define RELAY_ON 0
#define RELAY_OFF 1
#else
#define RELAY_ON 1
#define RELAY_OFF 0
#endif
#endif
#endif

#define LED 5
#define LED_ON 1
#define LED_OFF 0
// #define MOISTURE 4

#define DS18B20_PIN		2
#define DS18B20_MUX		PERIPHS_IO_MUX_GPIO2_U
#define DS18B20_FUNC	FUNC_GPIO2
#define CRC_ERROR_FLAG 12 // Output Pin that flags CRC error
#define READ_FLAG 13

#ifdef USE_I2C
#define I2C_MASTER_SDA_MUX PERIPHS_IO_MUX_MTMS_U
#define I2C_MASTER_SCL_MUX PERIPHS_IO_MUX_MTCK_U
#define I2C_MASTER_SDA_GPIO 14
#define I2C_MASTER_SCL_GPIO 13
#define I2C_MASTER_SDA_FUNC FUNC_GPIO14
#define I2C_MASTER_SCL_FUNC FUNC_GPIO13
#endif


#endif /* USER_INCLUDE_IODEFS_H_ */
