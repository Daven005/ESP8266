/*
 * rtc.c
 *
 *  Created on: 25 Jul 2017
 *      Author: User
 */

#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>

//#define DEBUG_OVERRIDE
#include "debug.h"
#include "stdout.h"
#include "time.h"
#include "ds3231.h"
#include "rtc.h"

time_t ICACHE_FLASH_ATTR getTime(void) {
	struct tm time;
	time_t t;

	if (ds3231_getTime(&time)) {
		t = mktime(&time);
		return t;
	} else {
		TESTP("Can't set time\n");
	}
	return 0;
}

void ICACHE_FLASH_ATTR setTime(time_t t) {
	struct tm *time;
	time = localtime(&t);
	if (!ds3231_setTime(time)) {
		TESTP("Can't set time\n");
	}
}

bool ICACHE_FLASH_ATTR isTimeValid(void) {
	struct tm time;

	if (ds3231_getTime(&time)) {
		return ((2017 -1900) <= time.tm_year && time.tm_year <= (2050 - 1900));
	}
	return false;
}

void ICACHE_FLASH_ATTR printTime(void) {
	struct tm time;

	if (ds3231_getTime(&time)) {
		os_printf("%02d:%02d:%02d %d/%02d/%4d", time.tm_hour, time.tm_min, time.tm_sec, time.tm_mday,
				time.tm_mon+1, time.tm_year+1900);
	} else {
		TESTP("Can't get time\n")
	}
}
