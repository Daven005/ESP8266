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

#include "gpio.h"
#include "easygpio.h"
#include "config.h"
#include "user_config.h"
#include "IOdefs.h"
#include "debug.h"
#include "dtoa.h"
#include "flowMonitor.h"

#define round(x) ((x)>=0?(int)((x)+0.5):(int)((x)-0.5))

LOCAL os_timer_t flow_timer;
uint16 oneSecFlowCount;
static uint16 oneSecCount;
static uint16 flowCount; // Interrupt variable
static uint16 flowCountPerReading;
static uint16 flowMax;
static uint16 flowMin;
static uint16 flowAverage; // NB is * 16
static double power; // watts
static double energy; // wattSeconds
static double flow; // l/s

 void ICACHE_FLASH_ATTR resetFlowReadings(void) {
	flowCountPerReading = oneSecCount = flowMax = 0;
	energy = 0.0;
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

uint16 ICACHE_FLASH_ATTR flowInLitresPerHour(void) {
	if (sysCfg.settings[SET_FLOW_COUNT_PER_LITRE] > 0)
		return ((uint32)flowCountPerReading*3600) / (oneSecCount * sysCfg.settings[SET_FLOW_COUNT_PER_LITRE]);
	return 0;
}

uint16 ICACHE_FLASH_ATTR flowAverageReading(void) { // ml/sec
	if (sysCfg.settings[SET_FLOW_COUNT_PER_LITRE] > 0)
		return ((uint32)flowAverage*1000) / (16 * sysCfg.settings[SET_FLOW_COUNT_PER_LITRE]);
	return 0;
}

uint16 ICACHE_FLASH_ATTR flowCurrentReading(void) {
	return oneSecFlowCount;
}

static void flowIntrHandler(void *arg) {
	uint32_t gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);
	if (gpio_status & BIT(FLOW_SENSOR)) {
		// This interrupt was intended for us - clear interrupt status
		flowCount++;
		GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status & BIT(FLOW_SENSOR));
	}
}

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

static void ICACHE_FLASH_ATTR flowTimerCb(void) { // 1 second

	ETS_GPIO_INTR_DISABLE();
	oneSecFlowCount = flowCount;
	flowCount = 0;
	ETS_GPIO_INTR_ENABLE();

	flowSetAverage(oneSecFlowCount);
	flowCountPerReading += oneSecFlowCount;
	if (oneSecFlowCount < flowMin) {
		flowMin = oneSecFlowCount;
	}
	if (oneSecFlowCount > flowMax) {
		flowMax = oneSecFlowCount;
	}
	oneSecCount++;
	calcFlows();
}

void ICACHE_FLASH_ATTR printFlows(void) {
	char s0[50], s1[50], s2[50];
	TESTP("fl:%s p:%s e:%s ",
			dtoStr(flow, 6, 3, s0),
			dtoStr(power, 10, 2, s1),
			dtoStr(energy, 12, 2, s2));
	TESTP("tp:%s ts:%s tb:%s\n",
			mappedStrTemperature(MAP_TEMP_PANEL, s0),
			mappedStrTemperature(MAP_TEMP_SUPPLY, s1),
			mappedStrTemperature(MAP_TEMP_TS_BOTTOM, s2));
}

void ICACHE_FLASH_ATTR initFlowMonitor(void) {
	easygpio_attachInterrupt(FLOW_SENSOR, EASYGPIO_PULLUP, flowIntrHandler, NULL);
	gpio_pin_intr_state_set(GPIO_ID_PIN(FLOW_SENSOR), GPIO_PIN_INTR_NEGEDGE); // Enable
	oneSecFlowCount = 0;
	flowAverage = 0;
	resetFlowReadings();
	os_timer_disarm(&flow_timer);
	os_timer_setfn(&flow_timer, (os_timer_func_t *)flowTimerCb, NULL);
	os_timer_arm(&flow_timer, 1000, true); // 1 second
}
