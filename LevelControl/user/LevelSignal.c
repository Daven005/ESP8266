/*
 * LevelSignal.c
 *
 *  Created on: 12 Sep 2016
 *      Author: User
 */

#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "gpio.h"
#include "easygpio.h"
#include "stdout.h"
#include "ioDefs.h"
#include "LevelSignal.h"
#include "debug.h"

static volatile uint16 pulseSpace;
#define QUEUE_SIZE 40
static os_event_t taskQueue[QUEUE_SIZE];
#define groupIdle  0b111111
#define groupStart 0b101011
#define groupBit0  0b101111
#define groupBit1  0b110101
#define groupEnd   0b101101
#define GROUP_BITS 6
#define PULSE_WIDTH 400

union {
	uint32 word;
	uint8 data[4];
} recv;

volatile enum {
	IDLE, STARTED
} state = IDLE;
;

static uint16 level;

void isrLevelSignal(uint32 gpio_status) { // -ve edge
	static uint32 lastTime;
	static uint8 group;
	static uint8 groupBitCount;

	if (gpio_status & BIT(LEVEL_SIGNAL)) {
		uint32 t = system_get_time();
		pulseSpace = t - lastTime;
		lastTime = t;
		if ((2 * PULSE_WIDTH / 3) < pulseSpace && pulseSpace < (5 * PULSE_WIDTH / 2)) {
			if (pulseSpace < (3 * PULSE_WIDTH / 2)) { // 1
				group |= 1;
			} else {
				// 0 is represented by a missing pulse
				// There are never 2 consecutive 0's
				// So a missing pulse represents 01 (2 bits)
				easygpio_outputSet(SCOPE, 1);
				group &= ~1;
				group <<= 1;
				group |= 1;
				groupBitCount++;
			}
			if (state == IDLE) {
				if ((group & 0x3f) == groupStart) {
					state = STARTED;
					system_os_post(USER_TASK_PRIO_0, 0, (os_param_t) groupStart);
					groupBitCount = 0;
					group = 0;
				} else {
					group <<= 1;
				}
			} else { // STARTED
				groupBitCount++;
				if (groupBitCount >= GROUP_BITS) {
					system_os_post(USER_TASK_PRIO_0, 0, (os_param_t) group);
					groupBitCount = 0;
					group = 0;
				} else {
					group <<= 1;
				}
			}
		} else { // Maybe just starting or noise
			groupBitCount = 1;
			group = 1;
		}
		easygpio_outputSet(SCOPE, 0);
	}
}

static void ICACHE_FLASH_ATTR checkRecv(void) {
	level = (recv.data[2] << 8) + recv.data[1];
	if (recv.data[3] != 0xaa || (uint8)(recv.data[3] + recv.data[2] + recv.data[1]) != recv.data[0]) {
		ERRORP("%08lx = %02x, %d, %02x\n", recv.word, recv.data[3], level, recv.data[0]);
		ERRORP("Bad packet\n");
	}
}

static void ICACHE_FLASH_ATTR backgroundTask(os_event_t *e) {
	static bitCount = 0;

	switch (e->par & 0x3f) {
	case groupStart:
		bitCount = 0;
		recv.word = 0;
		break;
	case groupBit1:
		recv.word <<= 1;
		recv.word |= 1;
		break;
	case groupBit0:
		recv.word <<= 1;
		break;
	case groupEnd:
		checkRecv();
		state = IDLE;
		break;
	case groupIdle:
		state = IDLE;
		break;
	default:
		os_printf("[%0x]", e->par);
		state = IDLE;
		break;
	}
}

uint16 ICACHE_FLASH_ATTR getLevel(void) {
	return level;
}

void ICACHE_FLASH_ATTR initLevelSignal(void) {
	os_printf("initLevelSignal\n");
	if (!system_os_task(backgroundTask, USER_TASK_PRIO_0, taskQueue, QUEUE_SIZE))
		ERRORP("Can't set up background task\n");

	easygpio_pinMode(LEVEL_SIGNAL, EASYGPIO_NOPULL, EASYGPIO_INPUT);
	easygpio_pinMode(SCOPE, EASYGPIO_NOPULL, EASYGPIO_INPUT);
	easygpio_outputEnable(SCOPE, 0);
}
