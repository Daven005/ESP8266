/*
 * overrideTemp.c
 *
 *  Created on: 7 Jan 2017
 *      Author: User
 */

#include <c_types.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "user_conf.h"
#include "config.h"
#include "sysCfg.h"
#include "overrideTemp.h"
#include "io.h"
#include "debug.h"

static enum { NOVALUE, TEMPERATURE, HOUR, MINUTE } valueSelected;
static enum { NOACTION, PLUS, MINUS, DEFAULT, CLEAR } actionSelected;
static os_timer_t finished_timer;
static os_timer_t minute_timer;
static uint8 currentTemperature;

static void ICACHE_FLASH_ATTR restartTimer(void) {
	os_timer_disarm(&finished_timer);
	os_timer_arm(&finished_timer, 30*1000, false);
	lcdLight(1);
}

static void ICACHE_FLASH_ATTR showDefaultActions(void) {
	showString(0, 4, "            ");
}

static void ICACHE_FLASH_ATTR showDefaultHeadings(void) {
	showString(0, 5, "Tm OT OH OM");
}

void ICACHE_FLASH_ATTR setCurrentTemperature(uint8 val) {
	showLargeNum(1, 0, currentTemperature = val);
}
bool ICACHE_FLASH_ATTR overrideUpdating(void) {
	return valueSelected != NOVALUE;
}

void ICACHE_FLASH_ATTR nextOverrideValue(void) {
	if (++valueSelected > MINUTE) valueSelected = NOVALUE;
	showDefaultActions();
	showDefaultHeadings();
	actionSelected = NOACTION;
	restartTimer();
	switch (valueSelected) {
	case NOVALUE:
		break;
	case TEMPERATURE:
		showInvertedString(3, 5, "OT");
		break;
	case HOUR:
		showInvertedString(6, 5, "OH");
		break;
	case MINUTE:
		showInvertedString(9, 5, "OM");
		break;
	}
}

void ICACHE_FLASH_ATTR nextOverrideAction(void) {
	if (++actionSelected > CLEAR) actionSelected = NOACTION;
	showDefaultActions();
	restartTimer();
	switch (actionSelected) {
	case NOACTION:
		showDefaultActions();
		break;
	case PLUS:
		showString(valueSelected*3+1, 4, "+");
		break;
	case MINUS:
		showString(valueSelected*3+1, 4, "-");
		break;
	case DEFAULT:
		showString(valueSelected*3+1, 4, "=");
		break;
	case CLEAR:
		showString(valueSelected*3+1, 4, "0");
		break;
	}
}

static void showOverrideValues(void) {
	if (sysCfg.overrideTemp != 0) {
		showLargeNum(1, 1, sysCfg.overrideTemp);
	} else {
		showLargeNumString(1, 1, "  ");
	}
	showLargeNum(1, 2, sysCfg.overrideHour);
	showLargeNum(1, 3, sysCfg.overrideMinute);
	TESTP("%d - %02d:%02d\n", sysCfg.overrideTemp, sysCfg.overrideHour, sysCfg.overrideMinute);
}

static enum override_t ICACHE_FLASH_ATTR checkControllerOverride(void) {
	if (sysCfg.overrideTemp != 0 && (sysCfg.overrideHour !=0 || sysCfg.overrideMinute != 0)) {
		if (currentTemperature < sysCfg.overrideTemp) {
			return OVERRIDE_ON;
		} else {
			return OVERRIDE_OFF;
		}
	}
	return NO_OVERRIDE;
}

void ICACHE_FLASH_ATTR doOverrideAction(void) {
	restartTimer();
	switch (valueSelected) {
	case NOVALUE:
		return;
	case TEMPERATURE:
		switch (actionSelected) {
		case NOACTION:
			return;
		case PLUS:
			if (sysCfg.overrideTemp < 28) sysCfg.overrideTemp++;
			break;
		case MINUS:
			if (sysCfg.overrideTemp != 0) sysCfg.overrideTemp--;
			break;
		case DEFAULT:
			sysCfg.overrideTemp = currentTemperature;
			break;
		case CLEAR:
			sysCfg.overrideTemp = 0;
			break;
		}
		break;
	case HOUR:
		switch (actionSelected) {
		case NOACTION:
			return;
		case PLUS:
			if (sysCfg.overrideHour < 23) sysCfg.overrideHour++;
			break;
		case MINUS:
			if (sysCfg.overrideHour != 0) sysCfg.overrideHour--;
			break;
		case DEFAULT:
			sysCfg.overrideHour = 1;
			break;
		case CLEAR:
			sysCfg.overrideHour = 0;
			break;
		}
		break;
	case MINUTE:
		switch (actionSelected) {
		case NOACTION:
			return;
		case PLUS:
			if (sysCfg.overrideMinute < 59) sysCfg.overrideMinute++;
			break;
		case MINUS:
			if (sysCfg.overrideMinute != 0) sysCfg.overrideMinute--;
			break;
		case DEFAULT:
		case CLEAR:
			sysCfg.overrideMinute = 0;
			break;
		}
		break;
	}
	CFG_dirty();
	showOverrideValues();
	setLevelTwoOverride(1, checkControllerOverride());
}

static void ICACHE_FLASH_ATTR overrideTimerCb(void) {
	valueSelected = NOVALUE;
	actionSelected = NOACTION;
	lcdLight(0);
	showDefaultHeadings();
	showDefaultActions();
}

static void ICACHE_FLASH_ATTR minuteTimerCb(void) {
	if (sysCfg.overrideMinute > 0) {
		sysCfg.overrideMinute--;
		CFG_dirty();
	} else {
		if (sysCfg.overrideHour > 0) {
			sysCfg.overrideHour--;
			sysCfg.overrideMinute = 59;
			CFG_dirty();
		}
	}
	TESTP("+");
	showOverrideValues();
	setLevelTwoOverride(1, checkControllerOverride());
}

void ICACHE_FLASH_ATTR initOverride(void) {
	os_timer_disarm(&finished_timer);
	os_timer_setfn(&finished_timer, (os_timer_func_t *) overrideTimerCb, NULL);
	os_timer_disarm(&minute_timer);
	os_timer_setfn(&minute_timer, (os_timer_func_t *) minuteTimerCb, NULL);
	os_timer_arm(&minute_timer, 60*1000, true);
	showDefaultHeadings();
	showDefaultActions();
	showOverrideValues();
	restartTimer();
}
