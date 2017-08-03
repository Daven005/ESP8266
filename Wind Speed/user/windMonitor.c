/*
 * windMonitor.c
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

#define DEBUG_OVERRIDE 1
#include "debug.h"

#include "gpio.h"
#include "easygpio.h"
#include "sysCfg.h"
#include "dtoa.h"
#include "IOdefs.h"
#include "windMonitor.h"
#include "user_conf.h"

//
static os_timer_t wind_timer;
static volatile uint16 isrWindCount; // Interrupt variable
static uint16 gatherTimeWindCount;
static uint32 monitorTimeWindCount;
static uint16 gathersCount;
static uint16 windMax;
static uint16 windMin;
static uint16 aboveCutIn;
static bool windOverridden;

void ICACHE_FLASH_ATTR overrideClearWind(void) {
	windOverridden = false;
	// Wait till next windTimerCb to update values
}

void ICACHE_FLASH_ATTR overrideSetWind(int value) {
	windOverridden = true;
	gatherTimeWindCount = value;
}

void ICACHE_FLASH_ATTR resetWindReadings(void) {
	if (!windOverridden)
		gatherTimeWindCount = 0;
	monitorTimeWindCount = gathersCount = monitorTimeWindCount = windMax = aboveCutIn = 0;
	windMin = 60000;
}

uint16 ICACHE_FLASH_ATTR windMaxReading(void) { //Knots
	if (sysCfg.settings[SET_PULSES_PER_KNOT] > 0)
		return windMax/sysCfg.settings[SET_PULSES_PER_KNOT];
	return 0;
}

uint16 ICACHE_FLASH_ATTR windMinReading(void) { //Knots
	if (sysCfg.settings[SET_PULSES_PER_KNOT] > 0)
		return windMin/sysCfg.settings[SET_PULSES_PER_KNOT];
	return 0;
}

uint16 ICACHE_FLASH_ATTR windNowReading(void) {
	if (gatherTimeWindCount > 0 && sysCfg.settings[SET_PULSES_PER_KNOT] > 0) {
		return gatherTimeWindCount / sysCfg.settings[SET_PULSES_PER_KNOT];
	}
	return 0;
}

uint16 ICACHE_FLASH_ATTR windAverageReading(void) { // Knots (rounded)
	if (sysCfg.settings[SET_PULSES_PER_KNOT] > 0)
		return (monitorTimeWindCount + sysCfg.settings[SET_PULSES_PER_KNOT]/2) / (gathersCount * sysCfg.settings[SET_PULSES_PER_KNOT]);
	return 0;
}

uint16 ICACHE_FLASH_ATTR windAboveCutIn(void) {
	return aboveCutIn;
}

static void isrWindMonitor(void *arg) {
	uint32 gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);
	if (gpio_status & BIT(FLOW_SENSOR)) {
		isrWindCount++;
	}
	GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status);
}

static void ICACHE_FLASH_ATTR windTimerCb(void) { // GATHER_TIME seconds

	if (windOverridden) {
		ETS_GPIO_INTR_DISABLE();
		isrWindCount = 0;
		ETS_GPIO_INTR_ENABLE();
	} else {
		ETS_GPIO_INTR_DISABLE();
		gatherTimeWindCount = isrWindCount;
		isrWindCount = 0;
		ETS_GPIO_INTR_ENABLE();
	}
	gathersCount++;
//	TESTP(gatherTimeWindCount > 0 ? ">" : "!");
	printWind();
	monitorTimeWindCount += gatherTimeWindCount;
	if (gatherTimeWindCount < windMin) {
		windMin = gatherTimeWindCount;
	}
	if (gatherTimeWindCount > windMax) {
		windMax = gatherTimeWindCount;
	}
	if (windNowReading() >= sysCfg.settings[SET_CUT_IN]) {
		aboveCutIn++;
	}
}

void ICACHE_FLASH_ATTR printWind(void) {
	TESTP("Wind Min: %d, Max: %d, Now: %d, Avg: %d, >=CutIn %d, ",
			windMin, windMax, gatherTimeWindCount, monitorTimeWindCount/gathersCount, aboveCutIn);
	TESTP("Knots Avg: %d, Now %d\n", windAverageReading(), windNowReading());
}

void ICACHE_FLASH_ATTR initWindMonitor(void) {
	easygpio_pinMode(FLOW_SENSOR, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	gpio_pin_intr_state_set(GPIO_ID_PIN(FLOW_SENSOR), GPIO_PIN_INTR_NEGEDGE); // Enable
	easygpio_attachInterrupt(FLOW_SENSOR, EASYGPIO_PULLUP, isrWindMonitor, NULL);
	gatherTimeWindCount = 0;
	resetWindReadings();
	os_timer_disarm(&wind_timer);
	os_timer_setfn(&wind_timer, (os_timer_func_t *)windTimerCb, NULL);
	os_timer_arm(&wind_timer, sysCfg.settings[SET_GATHER_TIME]*1000, true);
}

