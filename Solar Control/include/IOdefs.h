/*
 * IOdefs.h
 *
 *  Created on: 5 Dec 2015
 *      Author: User
 */

#ifndef INCLUDE_IODEFS_H_
#define INCLUDE_IODEFS_H_

#define LED 5
#define SWITCH 0 // GPIO 00
#define PUMP 13
#define FLOW_SENSOR 4
#define ANALOGUE_SELECT 12

#define DS18B20_MUX		PERIPHS_IO_MUX_GPIO2_U
#define DS18B20_FUNC	FUNC_GPIO2
#define DS18B20_PIN		2

#define PWM_0_OUT_IO_MUX PERIPHS_IO_MUX_MTCK_U
#define PWM_0_OUT_IO_NUM PUMP
#define PWM_0_OUT_IO_FUNC FUNC_GPIO13

#define SELECT_PTC easygpio_outputSet(ANALOGUE_SELECT, 0);
#define SELECT_PRESSURE easygpio_outputSet(ANALOGUE_SELECT, 1);

#endif /* INCLUDE_IODEFS_H_ */
