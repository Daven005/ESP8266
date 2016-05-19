/*
 * pumpControl.c
 *
 *  Created on: 5 Dec 2015
 *      Author: User
 */

#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "stdout.h"

#include "gpio.h"
#include "easygpio.h"
#include "config.h"
#include "user_config.h"
#include "IOdefs.h"
#include "pwm.h"
#include "eagle_soc.h"
#include "debug.h"
#include "flowMonitor.h"

static uint8 newDemand = 0;
static uint8 currentDemand = 0;
static int errorTntegral = 0;

void ICACHE_FLASH_ATTR initPump() {
	uint32 io_info[][3] = { { PWM_0_OUT_IO_MUX, PWM_0_OUT_IO_FUNC, PWM_0_OUT_IO_NUM } };
	uint32 duty[] = {0, 0, 0};

	pwm_init(10000, duty, 1, io_info);
	pwm_start();
}

void ICACHE_FLASH_ATTR setPumpPower(uint8 p) {
	TESTP("Pump->%d\n", p);
	pwm_set_duty((~p & 0xff), 0);
	pwm_start();
}

void ICACHE_FLASH_ATTR setPumpDemand(uint16 d) {
	if (d <= sysCfg.settings[SET_PUMP_MAX_DEMAND]) // Normally 0-100
		newDemand = d;
	else
		newDemand = sysCfg.settings[SET_PUMP_MAX_DEMAND];
	newDemand = newDemand * sysCfg.settings[SET_PUMP_MAX_FLOW] / sysCfg.settings[SET_PUMP_MAX_DEMAND]; // Normalise
}

void ICACHE_FLASH_ATTR processPump(void) {
	int flow = flowCurrentReading();

	int demandTerm = newDemand  * sysCfg.settings[SET_PUMP_BIAS_GAIN];
	int error = (newDemand - flow);
	int errorTerm = error * sysCfg.settings[SET_PUMP_PROPORTIONAL_GAIN];
	int integralTerm = errorTntegral + error * sysCfg.settings[SET_PUMP_INTEGRAL_GAIN];
	int pumpPower = demandTerm + errorTerm + integralTerm;

	TESTP("flow: %d, dt: %d, et: %d, it: %d, pump->%d\n", flow, demandTerm, errorTerm, integralTerm, pumpPower);
	pumpPower = pumpPower / 100;
	if (pumpPower > 255) {
		pumpPower = 255;
		if (error < 0)
			errorTntegral += error * sysCfg.settings[SET_PUMP_INTEGRAL_GAIN];
	} else if (pumpPower < 50) { // Stalls
		pumpPower = 0;
		if (error > 0)
			errorTntegral += error * sysCfg.settings[SET_PUMP_INTEGRAL_GAIN];
	} else {
		errorTntegral += error * sysCfg.settings[SET_PUMP_INTEGRAL_GAIN]; // Only if not reached pump limits
	}
	setPumpPower(pumpPower);
	currentDemand = newDemand;
}
