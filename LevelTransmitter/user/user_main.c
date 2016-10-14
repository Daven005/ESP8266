/*
 * user_main1.c
 *
 *  Created on: 19 May 2015
 *      Author: Administrator
 */
 
#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "stdout.h"
#include "debug.h"
#include "version.h"
#include "dtoa.h"
#include "debug.h"
#include "user_conf.h"
#include "user_main.h"
#include "flash.h"
#include "flash.h"
#include "hw_timer.h"
#include "easygpio.h"

static os_timer_t xmit_timer;
static uint16 level;
static union {
	uint32 word;
	uint8 data[4];
}xmit;
int wordBitCount = 0;
#define groupStart 0b101011
#define groupBit0  0b101111
#define groupBit1  0b110101
#define groupEnd   0b101101
#define GROUP_BITS 6
#define PULSE_WIDTH 400

uint32 user_rf_cal_sector_set(void);

static void ICACHE_FLASH_ATTR xmitWordCb(void) {
	os_printf("ADC = %d, ", level = system_adc_read());
	xmit.data[3] = 0xaa;
	xmit.data[2] = level >> 8;
	xmit.data[1] = level & 0xff;
	xmit.data[0] = xmit.data[3] + xmit.data[2] + xmit.data[1];
	os_printf("%08lx\n", xmit.word);
	wordBitCount = 34; // Start xmit
}

static uint8 sendGroup(uint8 count, uint8 bits) {
	static uint8 group;
	switch (count) {
	case 0:
	    easygpio_outputSet(XMIT, 1);
	    os_delay_us(1);
	    easygpio_outputSet(XMIT, 0);
	    return 0;
	case GROUP_BITS:
		group = bits;
		//No break
	default:
		if (group & (1 << 5)) {
		    easygpio_outputSet(XMIT, 1);
		    os_delay_us(2);
		    easygpio_outputSet(XMIT, 0);
		}
		group <<= 1;
		return count-1;
	}
}

static void hw_timer_cb(void) {
	static uint8 groupBitCount = 0;
	static uint8 groupBits;
	static uint32 word;

	groupBitCount = sendGroup(groupBitCount, groupBits);
	if (groupBitCount == 0) {
		switch (wordBitCount) {
		case 0:
			easygpio_outputSet(LED, 0);
			return;
		case 34:
			easygpio_outputSet(LED, 1);
			groupBits = groupStart;
			word = xmit.word;
			groupBitCount = GROUP_BITS;
			break;
		case 1:
			groupBits = groupEnd;
			groupBitCount = GROUP_BITS;
			break;
		default:
			if (word & (1 << 31))
				groupBits = groupBit1;
			else
				groupBits = groupBit0;
			word <<= 1;
			groupBitCount = GROUP_BITS;
			break;
		}
		wordBitCount--;
	}
}

static void ICACHE_FLASH_ATTR initDone_cb() {
	TESTP("Start test\n");
	easygpio_pinMode(LED, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputEnable(LED, 0);
	easygpio_pinMode(XMIT, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputEnable(XMIT, 0);
	os_timer_disarm(&xmit_timer);
	os_timer_setfn(&xmit_timer, (os_timer_func_t *) xmitWordCb, NULL);
	os_timer_arm(&xmit_timer, 2000, true); // repeat every 2S

    hw_timer_init(FRC1_SOURCE, 1);
    hw_timer_set_func(hw_timer_cb);
    hw_timer_arm(PULSE_WIDTH);
}

void ICACHE_FLASH_ATTR user_init(void) {
	stdout_init();
	gpio_init();
	wifi_station_disconnect();
	wifi_station_set_auto_connect(false);
	wifi_station_set_reconnect_policy(false);

	system_init_done_cb(&initDone_cb);
}
