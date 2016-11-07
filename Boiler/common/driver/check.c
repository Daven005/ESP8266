/*
 * check.c
 *
 *  Created on: 18 Jul 2016
 *      Author: User
 */
#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "check.h"
#include "debug.h"

static uint32 minHeap = 0xffffffff;

void ICACHE_FLASH_ATTR dump(uint8 *p, uint8 sz) {
	uint8 i;
	for (i=0; i<sz; i++) {
		if ((i % 16) == 0 ) {
			os_printf("\n%lx ", &p[i]);
		}
		os_printf("%02x ", p[i]);
	}
	os_printf("\n");
}

uint32 ICACHE_FLASH_ATTR checkMinHeap(void) {
	uint32 heap = system_get_free_heap_size();
	if (heap < minHeap) minHeap = heap;
	return minHeap;
}

void ICACHE_FLASH_ATTR showTime(char *func, uint32 previous) {
	uint32 now = system_get_time();

	TESTP("*** %d time in %s\n", (now-previous), func);
}

void ICACHE_FLASH_ATTR checkTime(char *func, uint32 previous) {
	uint32 now = system_get_time();
	if ((now-previous) > 15000) {
		TESTP("*** %d XS time in %s\n", (now-previous), func);
	}
}

void ICACHE_FLASH_ATTR checkTimeFunc(char *func, uint32 previous) {
	uint32 now = system_get_time();
	if ((now-previous) > 130000) {
		TESTP("*** %d XS time in %s\n", (now-previous), func);
	}
}

bool ICACHE_FLASH_ATTR assert_true(char *s, bool condition) {
	if (!condition) { TESTP("%s failed\n", s); return false;}
	return true;
}

void ICACHE_FLASH_ATTR assert_equal(char *s, int a, int b) {
	if (a != b) TESTP("%s failed (%d == %d)\n", s, a, b);
}
