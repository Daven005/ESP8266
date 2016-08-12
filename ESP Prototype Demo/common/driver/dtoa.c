/*
 * dtoa.c
 *
 *  Created on: 19 Jul 2016
 *      Author: User
 */
#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include "dtoa.h"

char * ICACHE_FLASH_ATTR dtoStr(double n, int width, int fractLen,  char *buffer) {
	uint8 i = 0;
	double intDiv, fracDiv, roundDiv;
	uint16 f;
	bool enable_zero = false;
	int intLen = width-fractLen;

	if (intLen < 0) intLen = 1;
	os_memset(buffer, 0, width+1);
	if (n == 0) {
		buffer[0] = '0';
		return buffer;
	}
	for (i = 0, intDiv = 10; i < intLen; i++) {
		intDiv *= 10;
	}
	for (i = 0, fracDiv = 1, roundDiv = 0.5; i < fractLen; i++) {
		fracDiv /= 10;
		roundDiv /= 10;
	}
	i = 0;
	if (n < 0) {
		buffer[i++] = '-';
		n = -n;
		n += roundDiv;
	} else {
		n += roundDiv;
	}
	for (; intDiv >= fracDiv && i < width; intDiv = intDiv / 10) {
		if (intDiv == 0.1) {
			if (!enable_zero) {
				buffer[i++] = '0';
			}
			buffer[i++] = '.';
			enable_zero = true;
		}
		f = n / intDiv;
		if (f > 9) {
			os_memset(buffer, '#', width);
			buffer[width] = 0;
			return buffer;
		}
		if (f != 0 || enable_zero) {
			buffer[i++] = (char) f + '0';
			n = n - (f * intDiv);
			enable_zero = true;
		}
	}
	buffer[i] = 0;
	return buffer;
}
