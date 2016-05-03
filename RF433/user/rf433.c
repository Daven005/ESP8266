/*
 * rf433.c
 *
 *  Created on: 28 Apr 2016
 *      Author: User
 */

#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "easygpio.h"
#include "gpio.h"
#include "debug.h"
#include "user_config.h"

static uint32 code;
static uint16 timings[100];
static bool data[100];
static uint8 idx;
static uint32 lastTime;
static os_event_t *testQueue;
enum {IDLE, WAIT_START, GOT_IDLE, GOT_DATA, FINISHED_DATA} state = IDLE;
enum signal {SIG_INIT=1, SIG_TIMER, SIG_START, SIG_FINISHED, SIG_TEST, SIG_ERROR};

#define TEST_QUEUE_LEN  100
void rx_task(os_event_t *e);

void rf433_intr(void *arg) {
	uint32 gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS); //clear interrupt status
	bool rx = easygpio_inputGet(RF433_RX);
//	uint32 thisTime = system_get_rtc_time();
	system_os_post(USER_TASK_PRIO_0, SIG_TEST, system_get_rtc_time());
	if (rx != 1)system_os_post(USER_TASK_PRIO_0, SIG_ERROR, 0);
//	if (state != IDLE) {
//		uint16 timeDiff = (thisTime - lastTime) * system_rtc_clock_cali_proc();
//		bool rx = easygpio_inputGet(RF433_RX);
//		switch (state) {
//		case WAIT_START:
//			if (rx == 1 && timeDiff > 4000) {
//				idx = 0;
//				state = GOT_DATA;
//				system_os_post(USER_TASK_PRIO_0, SIG_START, idx);
//			}
//			break;
//		case GOT_DATA:
//			if (100 <= timeDiff && timeDiff <= 700 && idx < 100) {
//				data[idx] = rx;
//				timings[idx] = timeDiff;
//				idx++;
//			} else if (timeDiff >= 4000 && rx == 0) {
//				state = IDLE;
//				system_os_post(USER_TASK_PRIO_0, SIG_FINISHED, idx);
//			} else {
//				idx = 0;
//				state = WAIT_START;
//			}
//			break;
//		case FINISHED_DATA:
//			break;
//		}
//		lastTime = thisTime;
//	}
	GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status);
}

void ICACHE_FLASH_ATTR rf433_start(void) {
	lastTime =  system_get_time();
	idx = 0;
	state = WAIT_START;
	ETS_GPIO_INTR_ENABLE();
}

void ICACHE_FLASH_ATTR rf433_stop(void) {
	ETS_GPIO_INTR_DISABLE();
}

void ICACHE_FLASH_ATTR rf433_init(void) {
	easygpio_pinMode(RF433_TX, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputSet(RF433_TX, 0);
	easygpio_pinMode(RF433_RX, EASYGPIO_NOPULL, EASYGPIO_INPUT);
	easygpio_outputDisable(RF433_RX);
	gpio_pin_intr_state_set(RF433_RX, GPIO_PIN_INTR_POSEDGE); // GPIO_PIN_INTR_ANYEDGE);
	testQueue = (os_event_t *)os_malloc(sizeof(os_event_t)*TEST_QUEUE_LEN);
	system_os_task(rx_task, USER_TASK_PRIO_0, testQueue, TEST_QUEUE_LEN);
	system_os_post(USER_TASK_PRIO_0, SIG_INIT, 123);
	state = IDLE;
	TESTP("433\n");
	easygpio_attachInterrupt(RF433_RX, EASYGPIO_NOPULL, rf433_intr, NULL);
}

static ICACHE_FLASH_ATTR void printData() {
	int i;
	for (i=0; i<idx; i++) {
		os_printf("%03d ", timings[idx]);
	}
	os_printf("\n");
	for (i=0; i<idx; i++) {
		os_printf("  %d ", data[idx]);
	}
	os_printf("\n");

}

void ICACHE_FLASH_ATTR rf433_printTimings(int idx) {
	uint32 thisTime = system_get_time();
	uint16 timeDiff = thisTime - lastTime;
	switch (state) {
	case WAIT_START:
		os_printf("RF: WAIT_START %d\n", timeDiff);
		break;
	case GOT_DATA:
		os_printf("RF: GOT_DATA %d\n", idx);
		printData();
		break;
	case FINISHED_DATA:
		os_printf("RF: FINISHED_DATA %d\n", idx);
		printData();
		break;
	}
}

void ICACHE_FLASH_ATTR rx_task(os_event_t *e) {
	static int count=0;
	static int minTime = 10000;
	static int maxTime = 0;
	uint32 timeDiff;

	switch (e->sig) {
	case SIG_TEST:
		timeDiff = (system_get_rtc_time() - e->par) * system_rtc_clock_cali_proc();
		if (timeDiff < minTime) minTime = timeDiff;
		if (timeDiff > maxTime) maxTime = timeDiff;
		if (++count > 1000) {
			TESTP("min: %d max; %d\n", minTime, maxTime);
			count = 0;
			minTime = 10000;
			maxTime = 0;
		}
		break;
	case SIG_ERROR:
		TESTP("SIG_ERROR %d\n", e->par);
		break;
	case SIG_INIT:
		TESTP("SIG_INIT %d\n", e->par);
		break;
	case SIG_START:
		TESTP("SIG_START %d\n", e->par);
		break;
	case SIG_FINISHED:
		TESTP("SIG_FINISHED %d\n", e->par);
		if (e->par != 0) {
			rf433_stop();
			rf433_printTimings(e->par);
		}
		rf433_start();
		break;
	}
}
