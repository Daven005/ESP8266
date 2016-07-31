/*
 * IOdefs.h
 *
 *  Created on: 5 Dec 2015
 *      Author: User
 */

#ifndef INCLUDE_IODEFS_H_
#define INCLUDE_IODEFS_H_

// For mapped Outputs
#define LED1 0
#define LED2 1
#define RELAY1 2
#define RELAY2 3
#define RELAY_ON 1
#define RELAY_OFF 0

#define LED_RED LED2
#define LED_GREEN LED1

#define PIN_LED1 2
#define PIN_LED2 15
#define PIN_RELAY1 4
#define PIN_RELAY2 5

#define SWITCH 0 // GPIO 00
#define LED PIN_LED1
#define PIN_DHT1 12
#define PIN_DHT2 14
#define PIN_PIR1 13
#define PIN_PIR2 16

// Not used but needed for compile of common
#define DS18B20_MUX		PERIPHS_IO_MUX_GPIO2_U
#define DS18B20_FUNC	FUNC_GPIO2
#define DS18B20_PIN		2

#endif /* INCLUDE_IODEFS_H_ */
