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

LOCAL os_timer_t flow_timer;
static uint32 timedFlowCount;
static uint32 flowCount;
static uint16 flowTimes;
static uint16 flowMax;

void resetFlowReadings(void) {
	timedFlowCount = 0;
	flowTimes = 0;
	flowMax = 0;
}

int flowPerReading(void) {
	return (timedFlowCount*1000)/sysCfg.settings[SET_FLOW_COUNT_PER_LITRE];
}

int flowMaxReading(void) {
	return (flowMax*1000)/sysCfg.settings[SET_FLOW_COUNT_PER_LITRE];
}

int flowTimesReading(void) {
	return flowTimes;
}

static void flowIntrHandler(void *arg) {
	uint32_t gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);
	if (gpio_status & BIT(FLOW_SENSOR)) {
		// This interrupt was intended for us - clear interrupt status
		GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status & BIT(FLOW_SENSOR));
		flowCount++;
	}
}

static void flowTimerCb(void) {
	static uint32 lastFlowCount = 0;
	uint32 thisFlowCount;

	ETS_GPIO_INTR_DISABLE();
	timedFlowCount += flowCount;
	flowCount = 0;
	ETS_GPIO_INTR_ENABLE();

	flowTimes++;
	thisFlowCount = timedFlowCount - lastFlowCount;
	if (thisFlowCount > flowMax) {
		flowMax = thisFlowCount;
	}
	lastFlowCount = timedFlowCount;
}

void ICACHE_FLASH_ATTR initFlowMonitor(void) {
	easygpio_pinMode(FLOW_SENSOR, EASYGPIO_NOPULL, EASYGPIO_INPUT);
	easygpio_attachInterrupt(FLOW_SENSOR, EASYGPIO_NOPULL, flowIntrHandler, NULL);
	gpio_pin_intr_state_set(GPIO_ID_PIN(FLOW_SENSOR), GPIO_PIN_INTR_NEGEDGE); // Enable
	timedFlowCount = 0;
	flowTimes = 0;
	flowMax = 0;
	os_timer_disarm(&flow_timer);
	os_timer_setfn(&flow_timer, (os_timer_func_t *)flowTimerCb, NULL);
	os_timer_arm(&flow_timer, sysCfg.settings[SET_FLOW_TIMER]*1000, true);
}
