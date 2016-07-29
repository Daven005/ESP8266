/*
 * xmit.c
 *
 *  Created on: 3 May 2016
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
#include "xmit.h"
#include "user_config.h"
#include "config.h"

#define A_SYNC_MIN 2500
#define A_SYNC_XMIT 6000
#define A_SYNC_MAX 6000
#define A_ZERO_MIN 100
#define A_ZERO_XMIT 200
#define A_ZERO_MAX 300
#define A_ONE_MIN 420
#define A_ONE_XMIT 500
#define A_ONE_MAX 580
#define A_PERIOD 715


#define B_SYNC_MIN 9000
#define B_SYNC_XMIT 10000
#define B_SYNC_MAX 15000
#define B_ZERO_MIN 300
#define B_ZERO_XMIT 450
#define B_ZERO_MAX 500
#define B_ONE_MIN 800
#define B_ONE_XMIT 900
#define B_ONE_MAX 1300
#define B_PERIOD 1715

#define QUEUE_SIZE 20
static os_event_t taskQueue[QUEUE_SIZE];

static os_timer_t xmit_timer;
static int repeatCount;
static xmitType currentXmitType;
static unsigned long currentCode;

static void xmitCb(xmitType t);

static ICACHE_FLASH_ATTR startXmitTimer(xmitType t) {
	os_timer_disarm(&xmit_timer);
	os_timer_setfn(&xmit_timer, (os_timer_func_t *) xmitCb, t);
	switch (t) {
	case TYPE_A:
		os_timer_arm(&xmit_timer, A_SYNC_XMIT/1000, false);
		break;
	case TYPE_B:
		os_timer_arm(&xmit_timer, B_SYNC_XMIT/1000, false);
		break;
	}
}

static bool xmitA(unsigned long code) {
#define LENGTH_A 25
	easygpio_outputSet(RF433_TX, 0);
	os_delay_us(A_SYNC_XMIT);
	unsigned long mask = 1L << (LENGTH_A - 1);
	do {
		if (code & mask) {
			easygpio_outputSet(RF433_TX, 1);
			os_delay_us(A_ONE_XMIT);
			easygpio_outputSet(RF433_TX, 0);
			os_delay_us(A_PERIOD - A_ONE_XMIT);
		} else {
			easygpio_outputSet(RF433_TX, 1);
			os_delay_us(A_ZERO_XMIT);
			easygpio_outputSet(RF433_TX, 0);
			os_delay_us(A_PERIOD - A_ZERO_XMIT);
		}
		mask = mask >> 1;
	} while (mask != 0);
	easygpio_outputSet(RF433_TX, 0);
	repeatCount--;
	return (repeatCount <= 0);
}

static bool ICACHE_FLASH_ATTR xmitB(unsigned long code) {
#define LENGTH_B 24
	easygpio_outputSet(RF433_TX, 0);
	os_delay_us(B_SYNC_XMIT);
	easygpio_outputSet(RF433_TX, 1);
	os_delay_us(B_ZERO_XMIT);
	easygpio_outputSet(RF433_TX, 0);
	unsigned long mask = 1L << (LENGTH_B - 1);
	do {
		if (code & mask) {
			easygpio_outputSet(RF433_TX, 0);
			os_delay_us(B_ZERO_XMIT);
			easygpio_outputSet(RF433_TX, 1);
			os_delay_us(B_ONE_XMIT);
		} else {
			easygpio_outputSet(RF433_TX, 0);
			os_delay_us(B_ONE_XMIT);
			easygpio_outputSet(RF433_TX, 1);
			os_delay_us(B_ZERO_XMIT);
		}
		mask = mask >> 1;
	} while (mask != 0);
	easygpio_outputSet(RF433_TX, 0);
	repeatCount--;

	return (repeatCount <= 0);
}

static void ICACHE_FLASH_ATTR xmitCb(xmitType t) {
	if (!system_os_post(USER_TASK_PRIO_2, t, (os_param_t) 0))
		ERRORP("Can't post event\n");
}

static void ICACHE_FLASH_ATTR xmitTask(os_event_t *e) {
	INFOP("Xmit task %d; minHeap: %d\n", e->sig, checkMinHeap());
	switch (e->sig) {
	case TYPE_A:
		if (xmitA(currentCode)) {
			os_timer_disarm(&xmit_timer);
			TESTP("<<\n");
		} else {
			startXmitTimer(TYPE_A);
			TESTP("a");
		}
		break;
	case TYPE_B:
		if (xmitB(currentCode)) {
			os_timer_disarm(&xmit_timer);
			TESTP("<<\n");
		} else {
			startXmitTimer(TYPE_B);
			TESTP("b");
		}
		break;
	default:
		ERRORP("Bad event %d\n", e->sig);
		break;
	}
}

static void ICACHE_FLASH_ATTR startXmit(xmitType t, uint32 code) {
	if (!system_os_task(xmitTask, USER_TASK_PRIO_2, taskQueue, QUEUE_SIZE)) {
		TESTP("Can't set up xmit task\n");
	}
	if (repeatCount > 0) {
		ERRORP("Busy transmitting: %d %lx * %d  ", currentXmitType, currentCode, repeatCount);
		return;
	}
	easygpio_outputSet(RF433_TX, 1); // Get transmitter started
	switch (t) {
	case TYPE_A:
		repeatCount = sysCfg.settings[SET_REPEAT_A];
		break;
	case TYPE_B:
		repeatCount = sysCfg.settings[SET_REPEAT_B];
		break;
	}
	currentXmitType = t;
	currentCode = code;
	TESTP("xmit: %d %lx * %d  ", t, code, repeatCount);
	startXmitTimer(t);
}


void ICACHE_FLASH_ATTR setSocket(uint8 id, bool state) {
	uint32 code;
	switch (id) {
	case 1: code = (state) ? 0xa2a66 : 0xa2a78; break;
    case 2: code = (state) ? 0xa2b98 : 0xa2b86; break;
    case 3: code = (state) ? 0xa2e18 : 0xa2e06; break;
    default: return;
	}
	startXmit(TYPE_A, code);
}

void ICACHE_FLASH_ATTR setGate(uint8 id) {
	uint32 code;
	switch (id) {
		case 1: code = 0xb87e12; break;
        case 2: code = 0xb85e52; break;
        case 3: code = 0xcfc83d; break;
        default: return;
	}
	startXmit(TYPE_B, code);
}
