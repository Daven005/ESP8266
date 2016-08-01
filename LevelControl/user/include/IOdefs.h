/*
 * IOdefs.h
 *
 *  Created on: 5 Dec 2015
 *      Author: User
 */

#ifndef INCLUDE_IODEFS_H_
#define INCLUDE_IODEFS_H_

#define LED 5
#define LED2 3
#define PUMP 4
#define SWITCH 0 // GPIO 00
#define FLOW_SENSOR 14

// Just to allow temperature.c to compile
#define DS18B20_MUX		PERIPHS_IO_MUX_GPIO2_U
#define DS18B20_FUNC	FUNC_GPIO2
#define DS18B20_PIN		2

#endif /* INCLUDE_IODEFS_H_ */
