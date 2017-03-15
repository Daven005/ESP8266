/*
 * IOdefs.h
 *
 *  Created on: 17 Jan 2017
 *      Author: User
 */

#ifndef USER_INCLUDE_IODEFS_H_
#define USER_INCLUDE_IODEFS_H_

#ifdef SLEEP_MODE
#undef OUTPUTS
#undef SWITCH
#else
#define SWITCH 0 // GPIO 00
#ifdef USE_OUTPUTS
#define OUTPUTS 5
#define RELAY_1 4
#define RELAY_2 14
#define RELAY_3 12
#define RELAY_4 13

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
#define MOISTURE 4

#define DS18B20_MUX		PERIPHS_IO_MUX_GPIO2_U
#define DS18B20_FUNC	FUNC_GPIO2
#define DS18B20_PIN		2


#endif /* USER_INCLUDE_IODEFS_H_ */
