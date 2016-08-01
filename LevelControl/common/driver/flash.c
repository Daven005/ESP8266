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
#include "user_config.h"
#include "debug.h"
#include "flash.h"

#ifdef LED
static os_timer_t flash_timer;
static bool flashState;
static int flashCount;
static unsigned int flashOnTime;
static unsigned int flashOffTime;
#endif
#ifdef ACTION_LED
static os_timer_t flashA_timer;
static bool flashActionState;
static int flashActionCount;
static unsigned int flashActionOnTime;
static unsigned int flashActionOffTime;
#endif

#ifdef LED
void ICACHE_FLASH_ATTR stopFlash(void) {
	easygpio_outputSet(LED, 0);
	os_timer_disarm(&flash_timer);
}

static void ICACHE_FLASH_ATTR flashCb(void) {
	os_timer_disarm(&flash_timer);
	if (flashState) {
		if (flashCount > 0)
			flashCount--;
		if (flashCount == 0) {
			stopFlash();
		} else { // if -ve will keep going
			easygpio_outputSet(LED, (flashState=0));
			os_timer_arm(&flash_timer, flashOffTime, false);
		}
	} else {
		easygpio_outputSet(LED, (flashState=1));
		os_timer_arm(&flash_timer, flashOnTime, false);
	}
}

void ICACHE_FLASH_ATTR startFlash(int count, unsigned int onTime, unsigned int offTime) {
	TESTP("Start Flash %d %d/%d\n", count, onTime, offTime);
	easygpio_outputSet(LED, (flashState=1));
	flashCount = count;
	flashOnTime = onTime;
	flashOffTime = offTime;
	os_timer_disarm(&flash_timer);
	os_timer_setfn(&flash_timer, (os_timer_func_t *) flashCb, (void *) 0);
	os_timer_arm(&flash_timer, onTime, false);
}
#endif

#ifdef ACTION_LED
void ICACHE_FLASH_ATTR stopActionFlash(void) {
	easygpio_outputSet(ACTION_LED, 0);
	os_timer_disarm(&flashA_timer);
}

static void ICACHE_FLASH_ATTR flashActionCb(void) {
	os_timer_disarm(&flashA_timer);
	if (flashActionState) {
		if (flashActionCount > 0)
			flashActionCount--;
		if (flashActionCount == 0) {
			stopActionFlash();
		} else { // if -ve will keep going
			easygpio_outputSet(ACTION_LED, (flashActionState = 0));
			os_timer_arm(&flashA_timer, flashActionOffTime, false);
		}
	} else {
		easygpio_outputSet(ACTION_LED, (flashActionState = 1));
		os_timer_arm(&flashA_timer, flashActionOnTime, false);
	}
}

void ICACHE_FLASH_ATTR startActionFlash(int count, unsigned int onTime, unsigned int offTime) {
	TESTP("Start Action Flash %d %d/%d\n", count, onTime, offTime);
	easygpio_outputSet(ACTION_LED, (flashActionState = 1));
	flashActionCount = count;
	flashActionOnTime = onTime;
	flashActionOffTime = offTime;
	os_timer_disarm(&flashA_timer);
	os_timer_setfn(&flashA_timer, (os_timer_func_t *) flashActionCb, (void *) 0);
	os_timer_arm(&flashA_timer, onTime, false);
}
#endif
