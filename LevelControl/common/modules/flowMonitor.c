/*
 * flowMonitor.c
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

//#define DEBUG_OVERRIDE
#include "debug.h"

#include "gpio.h"
#include "easygpio.h"
#include "sysCfg.h"
#include "dtoa.h"
#include "flowMonitor.h"
#include "user_conf.h"

#define round(x) ((x)>=0?(int)((x)+0.5):(int)((x)-0.5))

static os_timer_t flow_timer;
static uint16 oneSecFlowCount;
static volatile uint16 flowCount; // Interrupt variable
static uint16 flowCountPerReading;
static uint16 flowMax;
static uint16 flowMin;
static uint16 flowAverage; // NB is * 16
static uint16 secondsNotFlowingCount;

static bool flowOverridden;

#ifdef USE_FLOWS // use to avoid bringing in floats
#ifdef USE_ENERGY
static double power; // watts
static double energy; // wattSeconds
#endif
static double flow; // l/s

void ICACHE_FLASH_ATTR overrideClearFlow(void) {
	flowOverridden = false;
	// Wait till next flowTimerCb to update values
}

void ICACHE_FLASH_ATTR overrideSetFlow(int value) {
	flowOverridden = true;
	oneSecFlowCount = value;
}

void ICACHE_FLASH_ATTR resetFlowReadings(void) {
	if (!flowOverridden)
		oneSecFlowCount = 0;
	flowCountPerReading = flowMax = 0;
#ifdef USE_ENERGY
	energy = 0.0;
#endif
	flowMin = 60000;
}

static void ICACHE_FLASH_ATTR flowSetAverage(uint16 count) {
	flowAverage = (flowAverage * 4 + count * 16) / 5;
}

uint16 ICACHE_FLASH_ATTR flowMaxReading(void) {
	if (sysCfg.settings[SET_FLOW_COUNT_PER_LITRE] > 0)
		return ((uint32)flowMax*1000)/sysCfg.settings[SET_FLOW_COUNT_PER_LITRE];
	return 0;
}

uint16 ICACHE_FLASH_ATTR flowMinReading(void) {
	if (sysCfg.settings[SET_FLOW_COUNT_PER_LITRE] > 0)
		return ((uint32)flowMin*1000)/sysCfg.settings[SET_FLOW_COUNT_PER_LITRE];
	return 0;
}

uint32 ICACHE_FLASH_ATTR flowInLitresPerHour(void) {
	if (oneSecFlowCount > 0 && sysCfg.settings[SET_FLOW_COUNT_PER_LITRE] > 0) {
		return ((uint32) flowCountPerReading * 3600)
				/ (oneSecFlowCount * sysCfg.settings[SET_FLOW_COUNT_PER_LITRE]);
	}
	return 0;
}

uint16 ICACHE_FLASH_ATTR flowAverageReading(void) { // l/sec
	if (sysCfg.settings[SET_FLOW_COUNT_PER_LITRE] > 0)
		return ((uint32)flowAverage) / (16 * sysCfg.settings[SET_FLOW_COUNT_PER_LITRE]);
	return 0;
}

uint16 ICACHE_FLASH_ATTR flowCurrentReading(void) {
	return oneSecFlowCount;
}

void ICACHE_FLASH_ATTR startCheckIsFlowing(void) {
	secondsNotFlowingCount = 0;
}

uint16 ICACHE_FLASH_ATTR secondsNotFlowing(void) {
	return secondsNotFlowingCount;
}

void isrFlowMonitor(uint32 gpio_status) {
	if (gpio_status & BIT(FLOW_SENSOR)) {
		flowCount++;
	}
}

#ifdef USE_ENERGY
float ICACHE_FLASH_ATTR energyReading(void) {
	return energy;
}

void ICACHE_FLASH_ATTR calcFlows(void) {
#define SH 4.2 // Specific Heat = 4.2 KJ/K°C = KJ/l°C = KW/l°C
	double tp, ts, tb;
	mappedFloatPtrTemperature(MAP_TEMP_PANEL, &tp);
	mappedFloatPtrTemperature(MAP_TEMP_SUPPLY, &ts);
	mappedFloatPtrTemperature(MAP_TEMP_TS_BOTTOM, &tb);

	flow = (double) oneSecFlowCount / sysCfg.settings[SET_FLOW_COUNT_PER_LITRE]; // in litres/sec
	power = SH * flow * (ts - tb); // KW
	energy += power; // KWs
}
#else
void ICACHE_FLASH_ATTR calcFlows(void) {
	flow = (double) oneSecFlowCount / sysCfg.settings[SET_FLOW_COUNT_PER_LITRE]; // in litres/sec
}
#endif

static void ICACHE_FLASH_ATTR flowTimerCb(void) { // 1 second

	if (flowOverridden) {
		ETS_GPIO_INTR_DISABLE();
		flowCount = 0;
		ETS_GPIO_INTR_ENABLE();
	} else {
		ETS_GPIO_INTR_DISABLE();
		oneSecFlowCount = flowCount;
		flowCount = 0;
		ETS_GPIO_INTR_ENABLE();
	}

	flowSetAverage(oneSecFlowCount);
	if (flowAverage == 0) { // Use average to deal with any timing issues when demand oscillates
		INFOP("!");
		secondsNotFlowingCount++;
	} else {
		INFOP(">");
		secondsNotFlowingCount = 0;
	}
	flowCountPerReading += oneSecFlowCount;
	if (oneSecFlowCount < flowMin) {
		flowMin = oneSecFlowCount;
	}
	if (oneSecFlowCount > flowMax) {
		flowMax = oneSecFlowCount;
	}
	calcFlows();
}

#ifdef USE_ENERGY
void ICACHE_FLASH_ATTR printFlows(void) {
	char s0[50], s1[50], s2[50];
	TESTP("fl:%sl/S p:%skW e:%skWS ",
			dtoStr(flow*3600, 6, 3, s0),
			dtoStr(power, 10, 2, s1),
			dtoStr(energy, 12, 2, s2));
	TESTP("tp:%s ts:%s tb:%s\n",
			mappedStrTemperature(MAP_TEMP_PANEL, s0),
			mappedStrTemperature(MAP_TEMP_SUPPLY, s1),
			mappedStrTemperature(MAP_TEMP_TS_BOTTOM, s2));
}
#else
void ICACHE_FLASH_ATTR printFlows(void) {
	char s0[50];
	TESTP("fl:%s\n", dtoStr(flow*3600, 6, 3, s0));
}
#endif

void ICACHE_FLASH_ATTR initFlowMonitor(void) {
	easygpio_pinMode(FLOW_SENSOR, EASYGPIO_NOPULL, EASYGPIO_INPUT);
	oneSecFlowCount = 0;
	flowAverage = 0;
	resetFlowReadings();
	os_timer_disarm(&flow_timer);
	os_timer_setfn(&flow_timer, (os_timer_func_t *)flowTimerCb, NULL);
	os_timer_arm(&flow_timer, 1000, true); // 1 second
}
#endif // USE_FLOWS
