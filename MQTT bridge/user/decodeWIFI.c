/*
 * decodeWIFI.c
 *
 *  Created on: 7 Aug 2016
 *      Author: User
 */

#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "debug.h"
#include "flash.h"
#include "jsmn.h"
#include "wifi.h"
#include "decodeCommand.h"
#include "decodeWIFI.h"

#define NAME_SIZE 33

static phyMode = PHY_MODE_11B;

static uint8 wifiChannel = 255;
static char deviceName[NAME_SIZE], ssidPrefix[NAME_SIZE], ssid[NAME_SIZE];

static void ICACHE_FLASH_ATTR setPhyMode(char *s) {
	if (s) {
		int temp = atoi(s);
		if (PHY_MODE_11B <= temp && temp <= PHY_MODE_11N) phyMode = temp;
	}
}

static void ICACHE_FLASH_ATTR wifiParamsMessage(char *cmd) {
	os_printf("{\"resp\":\"OK\",\"cmd\":\"%s\",\"params\":{"
			"\"name\":\"%s\",\"phymode\":%d,\"scanstatus\":%d,\"ssidprefix\":\"%s\",\"ssid\":\"%s\""
			"}}\n",
			cmd, deviceName, phyMode, WiFiScanStatus(), ssidPrefix, ssid);
}

static void ICACHE_FLASH_ATTR wifiStatusMessage(char *cmd) {
	os_printf("{\"resp\":\"OK\",\"cmd\":\"%s\",\"params\":{"
			"\"status\":%d,\"ssid\":\"%s\",\"channel\":%d,\"rssi\":%d,"
			"\"connectTime\":%ld,\"IP\":\"%s\",\"MAC\":\"%s\""
			"}}\n", cmd, WiFiConnectStatus(), ssid, wifiChannel, wifi_station_get_rssi(),
			WIFI_ConnectTime(), WiFiIPaddress(), WiFiMACaddress());
}

void ICACHE_FLASH_ATTR setWiFiParams(char *cmd, char *bfr, jsmntok_t root[], int start, int max) {
	int idx;

	for (idx = start; idx < max; idx++) {
		if (jsoneq(bfr, &root[idx], "name")) {
			bfr[root[idx + 1].end] = 0; // Overwrites trailing quote
			os_strcpy(deviceName, &bfr[root[idx + 1].start]);
			idx++;
		}
		if (jsoneq(bfr, &root[idx], "phymode")) {
			bfr[root[idx + 1].end] = 0; // Overwrites trailing quote
			setPhyMode(&bfr[root[idx + 1].start]);
			idx++;
		}
		if (jsoneq(bfr, &root[idx], "ssidprefix")) {
			bfr[root[idx + 1].end] = 0; // Overwrites trailing quote
			os_strcpy(ssidPrefix, &bfr[root[idx + 1].start]);
			idx++;
		}
	}
	TESTP("setWiFiParams ssidPrefix: %s, devicename: %s\n", ssidPrefix, deviceName);
	startFlash(1, 100, 2000);
	wifiParamsMessage(cmd);
}

void ICACHE_FLASH_ATTR getWiFiParams(char *cmd) {
	wifiParamsMessage(cmd);
}

void ICACHE_FLASH_ATTR WiFiStatus(char *cmd) {
	wifiStatusMessage(cmd);
}

static void ICACHE_FLASH_ATTR WiFiConnectCb(uint8_t status) {
	TESTP("WiFi status: %d\n", status);
	if (status == STATION_GOT_IP) {
		wifiChannel = wifi_get_channel();
		startFlash(-1, 20, 2000);
		myMqttState.isWiFi = true;
	} else {
		myMqttState.isWiFi = false;
	}
	wifiStatusMessage("WiFi_Connected");
}

static void ICACHE_FLASH_ATTR initWiFiScanCb(void) {
	if (WiFiScanStatus() == OK) {
		os_strcpy(ssid, getBestSSID());
		os_printf("ssid %s, getBestSSID %s\n", ssid, getBestSSID());
	}
	wifiParamsMessage("WiFi_ScanComplete");
	startFlash(-1, 500, 1000);
}

void ICACHE_FLASH_ATTR WiFiScan(char *cmd) {
	initWiFi(phyMode, deviceName, ssidPrefix, initWiFiScanCb);
	startFlash(-1, 200, 1000);
	okMessage(cmd);
}

void ICACHE_FLASH_ATTR WiFiConnect(char *cmd, char *bfr, jsmntok_t root[], int start, int max) {
	int idx;
	char *password = NULL;

	for (idx = start; idx < max; idx++) {
		if (jsoneq(bfr, &root[idx], "ssid")) {
			bfr[root[idx + 1].end] = 0; // Overwrites trailing quote
			os_strncpy(ssid, &bfr[root[idx + 1].start], NAME_SIZE-1);
			idx++;
		}
		if (jsoneq(bfr, &root[idx], "password")) {
			bfr[root[idx + 1].end] = 0; // Overwrites trailing quote
			password = &bfr[root[idx + 1].start];
			idx++;
		}
	}
	if (os_strlen(ssid) == 0) { // NB This allows ssid to override scan ssid
		os_strcpy(ssid, getBestSSID());
	}
	if (os_strlen(ssid) == 0 || password == NULL) {
		errorMessage(cmd, "Missing parameter");
	} else {
		TESTP("WiFiConnect ssid: %s, password: %s\n", ssid, password);
		if (!myMqttState.isWiFi) {
			WIFI_Connect(ssid, password, deviceName, WiFiConnectCb);
			startFlash(-1, 200, 1000);
			okMessage(cmd);
		} else {
			errorMessage(cmd, "Done already");
		}
	}
}
