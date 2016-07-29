/*
 * wifi.c
 *
 *  Created on: Dec 30, 2014
 *      Author: Minh
 */

#include "include/wifi.h"

#include <c_types.h>
#include <ets_sys.h>
#include <ip_addr.h>
#include <osapi.h>
#include <stddef.h>
#include <user_interface.h>

#include "debug.h"
#include "user_config.h"
#include "wifi.h"

static uint16 attempts;
static ETSTimer WiFiLinker;
WifiCallback wifiCb = NULL;
static uint8_t wifiStatus = STATION_IDLE, lastWifiStatus = STATION_IDLE;
static char bestSSID[33];
static startUpCb_t startUpCb;

char * ICACHE_FLASH_ATTR getBestSSID(void) {
	return bestSSID;
}

static void ICACHE_FLASH_ATTR wifiHandleEventCb(System_Event_t *evt) {
	INFOP("event %x\n", evt->event);
}

static void ICACHE_FLASH_ATTR wifi_station_scan_done(void *arg, STATUS status) {
  uint8 ssid[33];
  sint8 bestRSSI = -100;

  if (status == OK) {
    struct bss_info *bss_link = (struct bss_info *)arg;

    while (bss_link != NULL) {
      os_memset(ssid, 0, 33);
      if (os_strlen(bss_link->ssid) <= 32) {
        os_memcpy(ssid, bss_link->ssid, os_strlen(bss_link->ssid));
      } else {
        os_memcpy(ssid, bss_link->ssid, 32);
      }
      if (bss_link->rssi > bestRSSI && (strncmp(STA_SSID, ssid, strlen(STA_SSID)) == 0)) {
    	  strcpy(bestSSID, ssid);
    	  bestRSSI = bss_link->rssi;
      }
      TESTP("WiFi Scan: (%d,\"%s\",%d) best is %s\n", bss_link->authmode, ssid, bss_link->rssi, bestSSID);
      bss_link = bss_link->next.stqe_next;
    }
  } else {
	  os_printf("wifi_station_scan fail %d\n", status);
  }
	if (startUpCb)
		startUpCb();
	else
		ERRORP("startUpCb not defined\n");
}

void ICACHE_FLASH_ATTR initWiFi(startUpCb_t startUp) {
	startUpCb = startUp;
	wifi_set_phy_mode(PHY_MODE_11B);
	wifi_station_set_auto_connect(false);
	wifi_station_set_reconnect_policy(true);
	wifi_set_event_handler_cb(wifiHandleEventCb);
	wifi_set_opmode(STATION_MODE);
	wifi_station_scan(NULL, wifi_station_scan_done);
}

static void ICACHE_FLASH_ATTR wifi_check_ip(void *arg) {
	struct ip_info ipConfig;

	os_timer_disarm(&WiFiLinker);
	wifi_get_ip_info(STATION_IF, &ipConfig);
	wifiStatus = wifi_station_get_connect_status();
	if (wifiStatus == STATION_GOT_IP && ipConfig.ip.addr != 0) {
		os_timer_setfn(&WiFiLinker, (os_timer_func_t *) wifi_check_ip, NULL);
		os_timer_arm(&WiFiLinker, 2000, 0);
	} else {
		attempts++;
		if (wifi_station_get_connect_status() == STATION_WRONG_PASSWORD) {
			INFOP("STATION_WRONG_PASSWORD\n");
			wifi_station_connect();
		} else if (wifi_station_get_connect_status() == STATION_NO_AP_FOUND) {
			INFOP("STATION_NO_AP_FOUND\n");
			wifi_station_connect();
		} else if (wifi_station_get_connect_status() == STATION_CONNECT_FAIL) {
			INFOP("STATION_CONNECT_FAIL\n");
			wifi_station_connect();
		} else {
			INFOP("STATION_IDLE\n");
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
	INFOP("Hostname was: %s\n", wifi_station_get_hostname());
	wifi_station_set_hostname(deviceName);

	os_timer_disarm(&WiFiLinker);
	os_timer_setfn(&WiFiLinker, (os_timer_func_t *) wifi_check_ip, NULL);
	os_timer_arm(&WiFiLinker, 1000, 0);

	wifi_station_set_auto_connect(TRUE);
	wifi_station_connect();
	TESTP("Hostname is: %s\n", wifi_station_get_hostname());
}

