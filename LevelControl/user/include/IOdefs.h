/*
 * IOdefs.h
 *
 *  Created on: 5 Dec 2015
 *      Author: User
 */

#ifndef INCLUDE_IODEFS_H_
#define INCLUDE_IODEFS_H_

#define LED 5
#define LEVEL_SIGNAL 4
#define PUMP 13
#define SWITCH 0 // GPIO 00
#define FLOW_SENSOR 12
#define SCOPE 16
#define SOUNDER 14

#define LED_OFF 0
#define LED_ON 1

// Just to allow temperature.c to compile
#define DS18B20_MUX		PERIPHS_IO_MUX_GPIO2_U
#define DS18B20_FUNC	FUNC_GPIO2
#define DS18B20_PIN		2

#endif /* INCLUDE_IODEFS_H_ */
