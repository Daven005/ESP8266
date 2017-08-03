/*
 * store.c
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
#include "store.h"
#include "at24c.h"

static StoreHeading_t heading;
typedef enum { Free, WritingRecord, WritingHeading } BusyWriting_t;
static BusyWriting_t busyWriting;
static os_timer_t writingTimer;
static storeCb_t storeCb;

bool ICACHE_FLASH_ATTR isHeaderValid(void) {
	if (!(heading.magic == MAGIC &&
			heading.recCount < MAX_DATA &&
			heading.recFirst < MAX_DATA &&
			heading.recLast < MAX_DATA)) {
		return false;
	}
	if (heading.recFirst <= heading.recLast) {
		if ((heading.recLast - heading.recFirst) != heading.recCount) {
			return false;
		}
	} else {
		if ((heading.recLast + (MAX_DATA - heading.recFirst) != heading.recCount)) {
			return false;
		}
	}
	return true;
}

uint16 ICACHE_FLASH_ATTR recCount(void) {
	return heading.recCount;
}

static uint16 recordAddress(uint8 rec) {
	return (rec * sizeof(DataRec_t) + sizeof(StoreHeading_t));
}

static void ICACHE_FLASH_ATTR saveHeading (void) {
	at24c_writeInPage(0, (uint8*) &heading, sizeof(heading), false);
}

bool ICACHE_FLASH_ATTR initStore(time_t now) {
	bool isValid;
	at24c_readBytes(0, (uint8*) &heading, sizeof(heading));
	if (!(isValid = isHeaderValid())) {
		TESTP("Header was corrupt:"); printRecords();
		heading.magic = MAGIC;
		heading.recCount = heading.recFirst = heading.recLast = 0;
	}
	heading.lastTimeSync = now;
	saveHeading();
	return isValid;
}

void ICACHE_FLASH_ATTR printHeading(char *msg) {
	os_printf("%s: %ld, %d, %d-%d, %08x\n", msg,
			heading.lastTimeSync, heading.recCount, heading.recFirst, heading.recLast, heading.magic);
}

static void printData(DataRec_t *d) {
	os_printf("%ld, %d %d-%d <%d>\n",
			d->time, d->avgWind, d->minWind, d->maxWind, d->cutinWind);
}

void ICACHE_FLASH_ATTR printRecords(void) {
	int i, r;
	DataRec_t d;

	for (i=0, r = heading.recFirst; i<heading.recCount; i++) {
		at24c_readBytes(recordAddress(r), (uint8 *)&d, sizeof(DataRec_t));
		os_printf("%d:", r);
		printData(&d);
		if (++r >= (MAX_DATA-1)) r = 0;
	}
}

static void ICACHE_FLASH_ATTR timer_cb(void) {
	switch (busyWriting) {
	case Free: return;
	case WritingRecord: // Record written , now write heading
		heading.recCount++;
		heading.recLast++;
		if (heading.recLast >= (MAX_DATA-1)) heading.recLast = 0;
		saveHeading();
		os_timer_disarm(&writingTimer);
		os_timer_arm(&writingTimer, 10, false);
		busyWriting = WritingHeading;
		break;
	case WritingHeading:
		printHeading("Written hdr");
		// printRecords();
		busyWriting = Free;
		if (storeCb) {
			storeCb();
			storeCb = NULL;
		}
		break;
	}
}

bool ICACHE_FLASH_ATTR enqRec(DataRec_t *data, storeCb_t cb) {
	if (busyWriting != Free) {
		TESTP("EnQ Busy writing\n");
		return false;
	}
	if (heading.recCount >= (MAX_DATA-1)) {
		TESTP("Store full\n");
		return false;
	}
	storeCb = cb;
	at24c_writeInPage(recordAddress(heading.recLast), (uint8*) data, sizeof(DataRec_t), false);
	printData(data);
	busyWriting = WritingRecord;
	os_timer_disarm(&writingTimer);
	os_timer_setfn(&writingTimer, (os_timer_func_t *) timer_cb, NULL);
	os_timer_arm(&writingTimer, 10, false);
	return true;
}

bool ICACHE_FLASH_ATTR deqRec(DataRec_t *data) {
	if (busyWriting != Free) {
		INFOP("DeQ Busy writing\n"); // Happens during publishBulkData
		return false;
	}
	if (heading.recCount == 0) {
		TESTP("No more records\n");
		return false;
	}
	at24c_readBytes(recordAddress(heading.recFirst), (uint8 *)data, sizeof(DataRec_t));
	heading.recFirst++;
	if (heading.recFirst >= (MAX_DATA-1)) heading.recFirst = 0;
	heading.recCount--;
	saveHeading();
	printHeading("After deq");
	busyWriting = WritingHeading;
	os_timer_disarm(&writingTimer);
	os_timer_setfn(&writingTimer, (os_timer_func_t *) timer_cb, NULL);
	os_timer_arm(&writingTimer, 10, false);
	return true;
}

time_t ICACHE_FLASH_ATTR getLastTimeSync(void) {
	if (isHeaderValid()) return heading.lastTimeSync;
	return 0;
}

void ICACHE_FLASH_ATTR setLastTimeSync(time_t t) {
	heading.lastTimeSync = t; // Assumes header will be rewritten soon
	return;
}
