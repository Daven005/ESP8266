/*
 * sounder.c
 *
 *  Created on: 8 Oct 2016
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
#include "user_conf.h"
#include "debug.h"
#include "sounder.h"
#include "IOdefs.h"

#ifdef SOUNDER
static os_timer_t sound_timer;
static uint8 count, beepCount;
static int freqCount;

static override_t override;

static void ICACHE_FLASH_ATTR soundTimerCb(void) {
	switch (override) {
	case AUTO:
		if (beepCount != 0) {
			freqCount++;
			if ((count & 1) && (count <= beepCount*2)) {
				easygpio_outputSet(SOUNDER, freqCount & 1);
			}
			if (freqCount & 1) {
				if (freqCount > 250) { // 1/2 second interval
					freqCount = 0;
					count++;
					if (count >= 10) { // 5S cycle
						count = 0;
					}
				}
			}
		}
		break;
	case _ON:
		freqCount++; // Doesn't matter if it overflows
		easygpio_outputSet(SOUNDER, freqCount & 1);
		break;
	default: break;
	}
}

void ICACHE_FLASH_ATTR initSounder(void) {
	easygpio_pinMode(SOUNDER, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputEnable(SOUNDER, 0);
	os_timer_disarm(&sound_timer);
	os_timer_setfn(&sound_timer, (os_timer_func_t *)soundTimerCb, NULL);
	os_timer_arm(&sound_timer, 2, true); // 250Hz - 2mS for each phase
	override = AUTO;
}

void ICACHE_FLASH_ATTR sounderClear(void) {
	if (beepCount) {
		TESTP("Beep Off\n");
		beepCount = 0;
	}
}

void ICACHE_FLASH_ATTR sounderAlarm(uint8 c) {
	TESTP("Beep: %d\n", c);
	beepCount = c;
	count = 0;
}

bool ICACHE_FLASH_ATTR sounderActive(void) {
	return (beepCount != 0);
}

void ICACHE_FLASH_ATTR overrideSetSounder(override_t o) {
	override = o;
}
#endif
