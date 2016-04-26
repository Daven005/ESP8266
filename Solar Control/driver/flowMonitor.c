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

LOCAL os_timer_t flow_timer;
static uint32 timedFlowCount;
static int oneSecFlowCount;
static int flowCount;
static int flowTimes;
static int flowMax;
static int flowAverage;

void ICACHE_FLASH_ATTR resetFlowReadings(void) {
	timedFlowCount = 0;
	flowTimes = 0;
	flowMax = 0;
}

int ICACHE_FLASH_ATTR flowPerReading(void) {
	if (sysCfg.settings[SET_FLOW_COUNT_PER_LITRE] > 0)
		return (timedFlowCount*1000)/sysCfg.settings[SET_FLOW_COUNT_PER_LITRE];
	return 0;
}

int ICACHE_FLASH_ATTR flowMaxReading(void) {
	return (flowMax*1000)/sysCfg.settings[SET_FLOW_COUNT_PER_LITRE];
}

int ICACHE_FLASH_ATTR flowTimesReading(void) {
	return flowTimes;
}

int ICACHE_FLASH_ATTR flowAverageReading(void) {
	return flowAverage;
}

int ICACHE_FLASH_ATTR flowCurrentReading(void) {
	return oneSecFlowCount;
}

static void flowIntrHandler(void *arg) {
	uint32_t gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);
	if (gpio_status & BIT(FLOW_SENSOR)) {
		// This interrupt was intended for us - clear interrupt status
		GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status & BIT(FLOW_SENSOR));
		flowCount++;
	}
}

static void ICACHE_FLASH_ATTR flowTimerCb(void) { // 1 second
	static uint32 lastFlowCount = 0;
	uint32 thisFlowCount;

	ETS_GPIO_INTR_DISABLE();
	oneSecFlowCount = flowCount;
	flowCount = 0;
	ETS_GPIO_INTR_ENABLE();

	flowAverage = (flowAverage*9 + oneSecFlowCount)/10;
	timedFlowCount += oneSecFlowCount;
	flowTimes++;
	thisFlowCount = timedFlowCount - lastFlowCount;
	if (thisFlowCount > flowMax) {
		flowMax = thisFlowCount;
	}
	lastFlowCount = timedFlowCount;
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
