/*
 * io.c
 *
 *  Created on: 29 Jul 2016
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
#include "config.h"
#include "io.h"
#include "debug.h"
#include "user_conf.h"
#include "user_main.h"

bool pirStatus[3] = { false, false, false };
bool oldPIRstatus[3] = { false, false, false };
uint8 pirPins[3] = {0, PIN_PIR1, PIN_PIR2 };
static os_timer_t pir_timer[3];

const uint8 outputMap[MAX_OUTPUT] = { PIN_LED1, PIN_LED2, PIN_RELAY1, PIN_RELAY2 };

static bool currentOutputs[MAX_OUTPUT];
static bool outputOverrides[MAX_OUTPUT];
static enum speedSelect oldSpeed;

void ICACHE_FLASH_ATTR initOutputs(void) {
	int id;
	for (id = 0; id < MAX_OUTPUT; id++) {
		easygpio_pinMode(outputMap[id], EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
		easygpio_outputSet(outputMap[id], 0);
	}
}

void ICACHE_FLASH_ATTR initInputs(void) {
	easygpio_pinMode(PIN_DHT1, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_pinMode(PIN_DHT2, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_pinMode(PIN_PIR1, EASYGPIO_NOPULL, EASYGPIO_INPUT);
	easygpio_pinMode(PIN_PIR2, EASYGPIO_NOPULL, EASYGPIO_INPUT);
}

void ICACHE_FLASH_ATTR setOutput(uint8 id, bool set) {
	if (id < MAX_OUTPUT) {
		currentOutputs[id] = set;
		if (!outputOverrides[id]) {
			easygpio_outputSet(outputMap[id], set);
		}
	}
}

void ICACHE_FLASH_ATTR forceOutput(uint8 id, bool set) { // Sets override
	if (id < MAX_OUTPUT) {
		outputOverrides[id] = true;
		easygpio_outputSet(outputMap[id], set);
		TESTP("o/p %d(%d)-->%d\n", id, outputMap[id], set);
	}
}

void ICACHE_FLASH_ATTR clrOverride(uint8 id) {
	if (id < MAX_OUTPUT) {
		outputOverrides[id] = false;
		easygpio_outputSet(outputMap[id], currentOutputs[id]);
		TESTP("o/p %d(%d)x->%d\n", id, outputMap[id], currentOutputs[id]);
	}
}

void ICACHE_FLASH_ATTR setLED(enum led_t led) {
	static enum led_t oldLED = DARK;
	if (led != oldLED) {
		TESTP("LED = %d", led);
		oldLED = led;
	}
	switch (led) {
	case DARK:
		setOutput(LED_RED, 0);
		setOutput(LED_GREEN, 0);
		break;
	case RED:
		setOutput(LED_RED, 1);
		setOutput(LED_GREEN, 0);
		break;
	case GREEN:
		setOutput(LED_RED, 0);
		setOutput(LED_GREEN, 1);
		break;
	case YELLOW:
		setOutput(LED_RED, 1);
		setOutput(LED_GREEN, 1);
		break;
	}
}

enum speedSelect ICACHE_FLASH_ATTR getSpeed(void) {
	return oldSpeed;
}

void ICACHE_FLASH_ATTR setSpeed(enum speedSelect speed) {
	if (speed != oldSpeed) {
		TESTP("relay:%d\n", speed);
		oldSpeed = speed;
	}
	switch (speed) {
	case STOP:
		setOutput(RELAY1, 0);
		setOutput(RELAY2, 0);
		break;
	case SLOW:
		setOutput(RELAY1, 1);
		setOutput(RELAY2, 0);
		break;
	case FAST:
		setOutput(RELAY1, 1);
		setOutput(RELAY2, 1);
		break;
	}
}

void ICACHE_FLASH_ATTR printOutputs(void) {
	int idx;
	for (idx=PIR1; idx<=PIR2; idx++) {
		os_printf("PIR[%d]: %d (%d)\n", idx, pirStatus[idx], easygpio_inputGet(pirPins[idx]));
	}
	os_printf("OP: ");
	for (idx = 0; idx < MAX_OUTPUT; idx++) {
		os_printf("%d [%d] ", currentOutputs[idx], outputOverrides[idx]);
	}
	os_printf("\n");
}

static void ICACHE_FLASH_ATTR pirCb(uint32 args) {
	enum pir_t pir = args;
	TESTP("pirCb %d\n", pir);
	publishSensorData(SENSOR_PIR1 - 1 + pir, "PIR", 0);
	oldPIRstatus[pir] = pirStatus[pir] = false;
}

bool ICACHE_FLASH_ATTR pirState(enum pir_t pir) {
	return easygpio_inputGet(pirPins[pir]);
}

bool ICACHE_FLASH_ATTR checkPirActive(enum pir_t actionPir) {
	enum pir_t pir;

	for (pir = PIR1; pir <= PIR2; pir++) {
		if (pirState(pir)) {
			setPirActive(pir);
		}
	}
	switch (actionPir) {
	case PIR1:
	case PIR2:
		return pirStatus[actionPir];
	}
	publishError(2, actionPir);
	return false;
}

void ICACHE_FLASH_ATTR clearPirActive(enum pir_t pir) {
	pirStatus[pir] = false;
}

void ICACHE_FLASH_ATTR setPirActive(enum pir_t pir) {
	if (PIR1 <= pir && pir <= PIR2) {
		pirStatus[pir] = true;
		if (pirStatus[pir] != oldPIRstatus[pir]) {
			TESTP("PIR:%d ", pir);
			publishSensorData(SENSOR_PIR_ACTIVE1 - 1 + pir, "PIR ON", "1");
			oldPIRstatus[pir] = true;
		}
		os_timer_disarm(&pir_timer[pir]);
		os_timer_setfn(&pir_timer[pir], (os_timer_func_t *) pirCb, pir);
		os_timer_arm(&pir_timer[pir], sysCfg.settings[SETTING_PIR1_ON_TIME-1+pir]*1000*60, false); // Minutes
	} else  {
		ERRORP("Bad PIR # %d", pir);
	}
}
