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

#include "user_config.h"

#include "uart.h"
#include "stdout.h"
#include "easygpio.h"
#include "debug.h"
#include "flash.h"
#include "version.h"
#include "decodeCommand.h"
#include "user_main.h"

//static os_timer_t uartTimer;

#define QUEUE_SIZE 20
os_event_t taskQueue[QUEUE_SIZE];

uint8 mqttConnected;
bool httpSetupMode;
enum lastAction_t {
	IDLE,
	RESTART,
	FLASH,
	PROCESS_FUNC,
	SWITCH_SCAN,
	PUBLISH_DATA,
	INIT_DONE,
	MQTT_DATA_CB,
	MQTT_DATA_FUNC,
	MQTT_CONNECTED_CB,
	MQTT_CONNECTED_FUNC,
	MQTT_DISCONNECTED_CB,
	SMART_CONFIG,
	WIFI_CONNECT_CHANGE = 100
} lastAction __attribute__ ((section (".noinit")));
enum lastAction_t savedLastAction;

void user_rf_pre_init(void);

void user_rf_pre_init(void) {}

char * ICACHE_FLASH_ATTR getVersion(void) {
	return version;
}

static char * ICACHE_FLASH_ATTR readRx(RcvMsgBuff *rxBfr) {
	static int idx = 0;
	static char bfr[400];
	char c;

	ETS_UART_INTR_DISABLE();
	while (rxBfr->count > 0) {
		c = *(rxBfr->pReadPos);
		rxBfr->count--;
		rxBfr->pReadPos++;
        if (rxBfr->pReadPos == (rxBfr->pRcvMsgBuff + RX_BUFF_SIZE)) {
        	rxBfr->pReadPos = rxBfr->pRcvMsgBuff ;
        }
		if (c == '\r' || c == '\n') {
			if (idx > 0) { // Otherwise ignore blank line
				bfr[idx] = 0;
				idx = 0;
				ETS_UART_INTR_ENABLE();
				return bfr;
			}
		} else {
			if (idx < sizeof(bfr)-3) {
				bfr[idx++] = c;
			} else {
				ERRORP("Bfr overflow\n");
			}
		}
	}
    ETS_UART_INTR_ENABLE();
	return NULL;
}

static void ICACHE_FLASH_ATTR processRxFunc(RcvMsgBuff *rxBfr) {
	char *bfr  = readRx(rxBfr);
	while (bfr != NULL) {
//		TESTP("bfr: %s\n", bfr);
		decodeCommand(bfr);
		bfr = readRx(rxBfr);
	}
}

//static void ICACHE_FLASH_ATTR uartTimerCb(void) {
//	if (!system_os_post(USER_TASK_PRIO_1, EVENT_UART_TIMER, (os_param_t) 0))
//		ERRORP("Can't post EVENT_UART_TIMER\n");
//}

static void ICACHE_FLASH_ATTR backgroundTask(os_event_t *e) {
	static RcvMsgBuff *tempRcv = NULL;

	INFOP("Background task %d\n", e->sig);
	switch (e->sig) {
	case EVENT_RX:
		processRxFunc(tempRcv = (RcvMsgBuff *)e->par);
		break;
	case EVENT_RX_OVERFLOW:
		ERRORP("Rx overflow\n");
		processRxFunc((RcvMsgBuff *)e->par);
		break;
//	case EVENT_UART_TIMER:
//		if (tempRcv)
//			TESTP("UART c %d, r %d, w %d, s %x\n", tempRcv->count, tempRcv->pReadPos,
//					tempRcv->pWritePos, READ_PERI_REG(UART_INT_CLR(0)));
//		break;
	default:
		ERRORP("Bad background task event %d\n", e->sig);
		break;
	}
}

static void ICACHE_FLASH_ATTR startUp() {
	INFOP("\n%s ( %s ) starting ...\n", "MQTT Bridge", version);
	INFOP("wifi_get_phy_mode = %d\n", wifi_get_phy_mode());

//	os_timer_disarm(&uartTimer);
//	os_timer_setfn(&uartTimer, (os_timer_func_t *) uartTimerCb, (void *) 0);
//	os_timer_arm(&uartTimer, 10 * 1000, true);

	if ( !system_os_task(backgroundTask, USER_TASK_PRIO_1, taskQueue, QUEUE_SIZE))
		ERRORP("Can't set up background task\n");
	lastAction = INIT_DONE;
}

void ICACHE_FLASH_ATTR user_init(void) {
	uart_init(115200, 115200);
//	stdout_init();
	gpio_init();
	wifi_station_set_auto_connect(false); // Needs to be in user_init to apply to this session

	savedLastAction = lastAction;

	easygpio_pinMode(LED, EASYGPIO_PULLUP, EASYGPIO_OUTPUT);
	easygpio_outputSet(LED, 1);
	system_init_done_cb(&startUp);
}
