/*
 * wifi.c
 *
 *  Created on: Dec 30, 2014
 *      Author: Minh
 */

#include "wifi.h"

#include <c_types.h>
#include <ets_sys.h>
#include <ip_addr.h>
#include <osapi.h>
#include <stddef.h>
#include <user_interface.h>

#include "debug.h"

static uint16 attempts;
static ETSTimer WiFiLinker;
WifiCallback wifiCb = NULL;
static uint8_t wifiStatus = STATION_IDLE, lastWifiStatus = STATION_IDLE;

static void ICACHE_FLASH_ATTR wifi_check_ip(void *arg) {
	struct ip_info ipConfig;

	os_timer_disarm(&WiFiLinker);
	wifi_get_ip_info(STATION_IF, &ipConfig);
	wifiStatus = wifi_station_get_connect_status();
	if (wifiStatus == STATION_GOT_IP && ipConfig.ip.addr != 0) {
		os_timer_setfn(&WiFiLinker, (os_timer_func_t *) wifi_check_ip, NULL);
		os_timer_arm(&WiFiLinker, 1000, 0);
	} else {
		attempts++;
		if (wifi_station_get_connect_status() == STATION_WRONG_PASSWORD) {
			TESTP("STATION_WRONG_PASSWORD\n");
			wifi_station_connect();
		} else if (wifi_station_get_connect_status() == STATION_NO_AP_FOUND) {
			TESTP("STATION_NO_AP_FOUND\n");
			wifi_station_connect();
		} else if (wifi_station_get_connect_status() == STATION_CONNECT_FAIL) {
			TESTP("STATION_CONNECT_FAIL\n");
			wifi_station_connect();
		} else {
			TESTP("STATION_IDLE\n");
		}
		os_timer_setfn(&WiFiLinker, (os_timer_func_t *) wifi_check_ip, NULL);
		os_timer_arm(&WiFiLinker, 500, 0);
	}
	if (wifiStatus != lastWifiStatus) {
		lastWifiStatus = wifiStatus;
		if (wifiCb)
			wifiCb(wifiStatus);
	}
}

uint16 ICACHE_FLASH_ATTR WIFI_Attempts(void) {
	return attempts;
}

void ICACHE_FLASH_ATTR WIFI_Connect(uint8_t* ssid, uint8_t* pass, uint8_t* deviceName, WifiCallback cb) {
	struct station_config stationConf;

	attempts = 0;
	INFOP("WIFI_INIT\n");
	wifi_set_opmode(STATION_MODE);
	wifi_station_set_auto_connect(FALSE);
	wifiCb = cb;

	os_memset(&stationConf, 0, sizeof(struct station_config));

	os_sprintf(stationConf.ssid, "%s", ssid);
	os_sprintf(stationConf.password, "%s", pass);

	wifi_station_set_config(&stationConf);
	wifi_station_set_hostname(deviceName);

	os_timer_disarm(&WiFiLinker);
	os_timer_setfn(&WiFiLinker, (os_timer_func_t *) wifi_check_ip, NULL);
	os_timer_arm(&WiFiLinker, 1000, 0);

	wifi_station_set_auto_connect(TRUE);
	wifi_station_connect();
	TESTP("Hostname is: %s\n", wifi_station_get_hostname());
}

