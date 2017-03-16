/*
 * flash.c
 *
 *  Created on: 12 Jul 2016
 *      Author: User
 */
#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "debug.h"
#include "user_conf.h"
#include "flash.h"
#include "easygpio.h"
typedef enum {
	OFF, FLASHING_ON, FLASHING_OFF, WAIT
} flashState_t;

#ifdef LED
static os_timer_t flash_timer;
flashState_t flashState;
static int flashCount; // Count of flashes within pattern
static int flashCounter;
static int patternRepeatCount; // Count of pattern repeats
static unsigned int flashOnTime; // Individual ON & OFF times for flashes within a pattern
static unsigned int patternOffTime;
flashCb_t flashFinishedCb;
#else
#pragma message "No LED"
#endif
#ifdef ACTION_LED
static os_timer_t flashA_timer;
flashState_t flashActionState;
static int flashActionCount; // Count of flashes within pattern
static int flashActionCounter;
static int patternActionRepeatCount; // Count of pattern repeats
static unsigned int flashActionOnTime; // Individual ON & OFF times for flashes within a pattern
static unsigned int patternActionOffTime;
flashCb_t flashActionFinishedCb;
#endif

#ifdef LED
void ICACHE_FLASH_ATTR stopFlash(void) {
	easygpio_outputSet(LED, LED_OFF);
	flashState = OFF;
	os_timer_disarm(&flash_timer);
}

static void ICACHE_FLASH_ATTR flashCb(void) {
	os_timer_disarm(&flash_timer);
	switch (flashState) {
	case OFF:
		easygpio_outputSet(LED, LED_OFF);
		break;
	case FLASHING_ON:
		easygpio_outputSet(LED, LED_OFF);
		os_timer_arm(&flash_timer, flashOnTime, false);
		flashState = FLASHING_OFF;
		break;
	case FLASHING_OFF:
		flashCounter--;
		if (flashCounter > 0) {
			easygpio_outputSet(LED, LED_ON);
			os_timer_arm(&flash_timer, flashOnTime, false);
			flashState = FLASHING_ON;
		} else {
			easygpio_outputSet(LED, LED_OFF);
			os_timer_arm(&flash_timer, patternOffTime, false);
			flashState = WAIT;
		}
		break;
	case WAIT:
		if (patternRepeatCount > 0)
			patternRepeatCount--;
		if (patternRepeatCount == 0) {
			stopFlash();
			if (flashFinishedCb) flashFinishedCb();
		} else { // if -ve will keep going
			easygpio_outputSet(LED, LED_ON);
			flashCounter = flashCount;
			os_timer_arm(&flash_timer, flashOnTime, false);
			flashState = FLASHING_ON;
		}
		break;
	}
}

void startMultiFlashCb(int count, uint8 flashCount, unsigned int flashTime, unsigned int offTime,
		flashCb_t cb) {
	startMultiFlash(count, flashCount, flashTime, offTime);
	if (cb != NULL) flashFinishedCb = cb;
}

void startMultiFlash(int pCount, uint8 fCount, unsigned int flashTime, unsigned int offTime) {
	TESTP("Start Flash %d (*%d) %d/%d\n", pCount, fCount, flashTime, offTime);
	easygpio_pinMode(LED, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputEnable(LED, LED_ON);
	patternRepeatCount = pCount;
	flashCounter = flashCount = fCount;
	flashOnTime = flashTime;
	patternOffTime = offTime;
	flashFinishedCb = NULL;
	flashState = FLASHING_ON;
	os_timer_disarm(&flash_timer);
	os_timer_setfn(&flash_timer, (os_timer_func_t *) flashCb, (void *) 0);
	os_timer_arm(&flash_timer, flashOnTime, false);
}

void ICACHE_FLASH_ATTR startFlash(int count, unsigned int flashTime, unsigned int offTime) {
	startMultiFlash(count, 1, flashTime, offTime);
}
#endif

#ifdef ACTION_LED
void ICACHE_FLASH_ATTR stopActionFlash(void) {
	easygpio_outputSet(ACTION_LED, LED_OFF);
	flashActionState = OFF;
	os_timer_disarm(&flashA_timer);
}

static void ICACHE_FLASH_ATTR flashActionCb(void) {
	os_timer_disarm(&flashA_timer);
	switch (flashActionState) {
	case OFF:
		easygpio_outputSet(ACTION_LED, LED_OFF);
		break;
	case FLASHING_ON:
		easygpio_outputSet(ACTION_LED, LED_OFF);
		os_timer_arm(&flashA_timer, flashActionOnTime, false);
		flashActionState = FLASHING_OFF;
		break;
	case FLASHING_OFF:
		flashActionCounter--;
		if (flashActionCounter > 0) {
			easygpio_outputSet(ACTION_LED, LED_ON);
			os_timer_arm(&flashA_timer, flashActionOnTime, false);
			flashActionState = FLASHING_ON;
		} else {
			easygpio_outputSet(ACTION_LED, LED_OFF);
			os_timer_arm(&flashA_timer, patternActionOffTime, false);
			flashActionState = WAIT;
		}
		break;
	case WAIT:
		if (patternActionRepeatCount > 0)
			patternActionRepeatCount--;
		if (patternActionRepeatCount == 0) {
			stopActionFlash();
			if (flashActionFinishedCb) flashActionFinishedCb();
		} else { // if -ve will keep going
			easygpio_outputSet(ACTION_LED, LED_ON);
			flashActionCounter = flashActionCount;
			os_timer_arm(&flashA_timer, flashActionOnTime, false);
			flashActionState = FLASHING_ON;
		}
		break;
	}
}

void startActionMultiFlash(int pCount, uint8 fCount, unsigned int flashTime, unsigned int offTime) {
	TESTP("Start Action Flash %d (*%d) %d/%d\n", pCount, fCount, flashTime, offTime);
	easygpio_pinMode(ACTION_LED, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputEnable(ACTION_LED, LED_ON);
	patternActionRepeatCount = pCount;
	flashActionCounter = flashActionCount = fCount;
	flashActionOnTime = flashTime;
	patternActionOffTime = offTime;
	flashActionFinishedCb = NULL;
	flashActionState = FLASHING_ON;
	os_timer_disarm(&flashA_timer);
	os_timer_setfn(&flashA_timer, (os_timer_func_t *) flashActionCb, (void *) 0);
	os_timer_arm(&flashA_timer, flashActionOnTime, false);
}

void startActionMultiFlashCb(int count, uint8 flashCount, unsigned int flashTime, unsigned int offTime,
		flashCb_t cb) {
	startActionMultiFlash(count, flashCount, flashTime, offTime);
	if (cb != NULL) flashActionFinishedCb = cb;
}

void ICACHE_FLASH_ATTR startActionFlash(int count, unsigned int onTime, unsigned int offTime) {
	startActionMultiFlash(count, 1, onTime, offTime);
}
#endif
