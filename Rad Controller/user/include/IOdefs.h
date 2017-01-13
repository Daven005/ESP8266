/*
 * IOdefs.h
 *
 *  Created on: 4 Jan 2017
 *      Author: User
 */

#ifndef USER_INCLUDE_IODEFS_H_
#define USER_INCLUDE_IODEFS_H_

#define RELAY_1 16
//#define RELAY_2 14
//#define RELAY_3 12
//#define RELAY_4 13

#ifdef INVERT_RELAYS
#define RELAY_ON 0
#define RELAY_OFF 1
#else
#define RELAY_ON 1
#define RELAY_OFF 0
#endif

#define SWITCH 0
#define SWITCH_M 4

#define LED 5
#define LCD_Light 5 // Same as LED
#define LCD_SCE 15
#define LCD_clk 14
#define LCD_Data 13
#define LCD_D_C 12
//#define LCD_RST 2

#define DS18B20_MUX		PERIPHS_IO_MUX_GPIO2_U
#define DS18B20_FUNC	FUNC_GPIO2
#define DS18B20_PIN		2

#endif /* USER_INCLUDE_IODEFS_H_ */
