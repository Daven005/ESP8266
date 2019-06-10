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
#include "syscfg.h"
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

void user_rf_pre_init(void);
uint32 user_rf_cal_sector_set(void);

void user_rf_pre_init(void) {
}

uint32 ICACHE_FLASH_ATTR user_rf_cal_sector_set(void) {
	enum flash_size_map size_map = system_get_flash_size_map();
	uint32 rf_cal_sec = 0;

	switch (size_map) {
	case FLASH_SIZE_4M_MAP_256_256:
		rf_cal_sec = 128 - 5;
		break;
	case FLASH_SIZE_8M_MAP_512_512:
		rf_cal_sec = 256 - 5;
		break;
	case FLASH_SIZE_16M_MAP_512_512:
	case FLASH_SIZE_16M_MAP_1024_1024:
		rf_cal_sec = 512 - 5;
		break;
	case FLASH_SIZE_32M_MAP_512_512:
	case FLASH_SIZE_32M_MAP_1024_1024:
		rf_cal_sec = 1024 - 5;
		break;
	default:
		rf_cal_sec = 0;
		break;
	}
	TESTP("Flash type: %d, size 0x%x\n", size_map, rf_cal_sec);
	return rf_cal_sec;
}

static inline uint32_t clkCount(void) {
    uint32_t r;

    asm volatile ("rsr %0, ccount" : "=r"(r));
    return r;
}

static uint32 readEchoSensor(void) {
#define ECHO_MASK 1 << (ECHO-1) // GPIO5 bit mask
#define GPIO_IN ((volatile uint32_t*) 0x60000318)
	int32 startCount = 10000L;
	int32 timerCount = 100000L;
	uint32 startClock;
	uint32 endClock;

    easygpio_outputSet(TRIG, 1);
    os_delay_us(15);
    easygpio_outputSet(TRIG, 0);

    while (!GPIO_INPUT_GET(ECHO) && --startCount);
    if (startCount == 0) return 0xffff;
    startClock = clkCount();
    while (GPIO_INPUT_GET(ECHO) && --timerCount);
    endClock = clkCount();
//    TESTP("%ld, %ld = %ld\n", startClock, endClock, endClock-startClock);
    return endClock-startClock;
}

static void ICACHE_FLASH_ATTR xmitWordCb(void) {

#ifdef USE_PRESSURE_SENSOR
	TESTP("ADC = %d, ", level = system_adc_read());
#else
	TESTP("Echo = %d\n", level = (10*readEchoSensor()/80)/58);
#endif
	xmit.data[3] = 0xaa;
	xmit.data[2] = level >> 8;
	xmit.data[1] = level & 0xff;
	xmit.data[0] = xmit.data[3] + xmit.data[2] + xmit.data[1];
	TESTP("%08lx\n", xmit.word);
	wordBitCount = 34; // Start xmit
	hw_timer_restart(PULSE_WIDTH);
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
		case 0: // All done
			easygpio_outputSet(LED, 0);
			hw_timer_disable();
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
	TESTP("Start Level Transmitter\n");
	easygpio_pinMode(LED, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputEnable(LED, 0);
	easygpio_pinMode(XMIT, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputEnable(XMIT, 0);
	easygpio_pinMode(TRIG, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputEnable(TRIG, 0);
	easygpio_pinMode(ECHO, EASYGPIO_NOPULL, EASYGPIO_INPUT);

	os_timer_disarm(&xmit_timer);
	os_timer_setfn(&xmit_timer, (os_timer_func_t *) xmitWordCb, NULL);
	os_timer_arm(&xmit_timer, 2000, true); // repeat every 2S

    hw_timer_set_func(hw_timer_cb);
}

void ICACHE_FLASH_ATTR user_init(void) {
	stdout_init();
	gpio_init();
	TESTP("\nSDK version:%s  ", system_get_sdk_version());
	system_deep_sleep_set_option(4);
	system_phy_set_rfoption(4);

//	wifi_station_disconnect();
//	wifi_station_set_auto_connect(false);
//	wifi_station_set_reconnect_policy(false);

	system_init_done_cb(&initDone_cb);
}
