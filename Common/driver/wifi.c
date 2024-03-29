/*
 * wifi.c
 *
 *  Created on: Dec 30, 2014
 *      Author: Minh
 */

#include <c_types.h>
#include <ets_sys.h>
#include <ip_addr.h>
#include <osapi.h>
#include "user_interface.h"
#include "user_conf.h"
#include "debug.h"
#include "wifi.h"

static uint32 startConnectTime;
static uint32 connectTime;
//static ETSTimer WiFiLinker;
WifiCallback wifiCb = NULL;
static uint8_t wifiConnectStatus = STATION_IDLE;
static char bestSSID[NAME_SIZE],  ssidPrefix[NAME_SIZE];
static InitWiFiCb_t initWiFiCb;
static STATUS scanStatus = PENDING;
static char ipAddressStr[20], macAddressStr[30];

char * ICACHE_FLASH_ATTR getBestSSID(void) {
	return bestSSID;
}

STATUS ICACHE_FLASH_ATTR WiFiScanStatus(void) {
	return scanStatus;
}

uint8 ICACHE_FLASH_ATTR WiFiConnectStatus(void) {
	return wifiConnectStatus;
}

char * ICACHE_FLASH_ATTR WiFiIPaddress(void) {
	return ipAddressStr;
}

char * ICACHE_FLASH_ATTR WiFiMACaddress(void) {
	return macAddressStr;
}

static void ICACHE_FLASH_ATTR wifiHandleEventCb(System_Event_t *evt) {
	uint8 macAddress[10];

	wifiConnectStatus = wifi_station_get_connect_status();
	switch (evt->event) {
	case EVENT_STAMODE_CONNECTED:
		TESTP("CONNECTED: %s, chanl: %d, MAC: " MACSTR "\n", evt->event_info.connected.ssid,
				evt->event_info.connected.channel, MAC2STR(evt->event_info.connected.bssid));
		connectTime = system_get_time() - startConnectTime;
		break;
	case EVENT_STAMODE_DISCONNECTED:
		TESTP("DISCONNECTED: %s, Reason: %d, MAC: " MACSTR "\n", evt->event_info.disconnected.ssid,
				evt->event_info.disconnected.reason, MAC2STR(evt->event_info.disconnected.bssid));
		break;
	case EVENT_STAMODE_AUTHMODE_CHANGE:
		TESTP("AUTHMODE_CHANGE: From %d to %d\n", evt->event_info.auth_change.old_mode,  evt->event_info.auth_change.old_mode);
		break;
	case EVENT_STAMODE_GOT_IP:
		TESTP("GOT_IP: " IPSTR " Mask:" IPSTR " GW:" IPSTR "\n", IP2STR(&evt->event_info.got_ip.ip),
				IP2STR(&evt->event_info.got_ip.mask), IP2STR(&evt->event_info.got_ip.gw));
		os_sprintf(ipAddressStr, IPSTR, IP2STR(&evt->event_info.got_ip.ip));
		if (wifi_get_macaddr(0, macAddress)) {
			os_sprintf(macAddressStr, MACSTR, MAC2STR(macAddress));
		}
		connectTime = system_get_time() - startConnectTime;
		break;
	case EVENT_STAMODE_DHCP_TIMEOUT:
		TESTP("DHCP_TIMEOUT\n");
		break;
	}
	if (wifiCb) {
		wifiCb(wifiConnectStatus);
	} else {
		ERRORP("No wifiCb\n");
	}
}

static void ICACHE_FLASH_ATTR wifi_station_scanCb(void *arg, STATUS status) {
	uint8 ssid[33];
	sint8 bestRSSI = -100;

	if ((scanStatus = status) == OK) {
		struct bss_info *bss_link = (struct bss_info *) arg;
		if (bss_link == NULL)
			ERRORP("No station info from WiFi scan\n");
		while (bss_link != NULL) {
			os_memset(ssid, 0, 33);
			if (os_strlen(bss_link->ssid) <= 32) {
				os_memcpy(ssid, bss_link->ssid, os_strlen(bss_link->ssid));
			} else {
				os_memcpy(ssid, bss_link->ssid, 32);
			}
			if (bss_link->rssi > bestRSSI
					&& (os_strncmp(ssidPrefix, ssid, os_strlen(ssidPrefix)) == 0)) {
				os_strcpy(bestSSID, ssid);
				bestRSSI = bss_link->rssi;
			}
			TESTP("WiFi Scan: (%d,\"%s\",%d) best is %s\n", bss_link->authmode, ssid,
					bss_link->rssi, bestSSID);
			bss_link = bss_link->next.stqe_next;
		}
	} else {
		ERRORP("wifi_station_scan fail %d\n", status);
	}
	if (initWiFiCb)
		initWiFiCb();
	else
		ERRORP("startUpCb not defined\n");
}

void ICACHE_FLASH_ATTR initWiFi(enum phy_mode phyMode, char *deviceName, char *ssidPrfx, InitWiFiCb_t cb) {
	uint8 errorFlags = 0;
	initWiFiCb = cb;
	if (!wifi_set_phy_mode(phyMode)) errorFlags |= 1 << 0;
	if (!wifi_set_opmode(STATION_MODE)) errorFlags |= 1 << 1;
	if (!wifi_station_set_hostname(deviceName)) errorFlags |= 1 << 2;
	os_strncpy(ssidPrefix, ssidPrfx, NAME_SIZE-1);
	if (!wifi_station_set_auto_connect(false)) errorFlags |= 1 << 3;
	if (!wifi_station_set_reconnect_policy(false)) errorFlags |= 1 << 4;
	wifi_set_event_handler_cb(wifiHandleEventCb);
	if (!wifi_station_scan(NULL, wifi_station_scanCb)) errorFlags |= 1 << 5;
	if (errorFlags)
		ERRORP("InitWiFi errors %x\n", errorFlags);
}

uint32 ICACHE_FLASH_ATTR WIFI_ConnectTime(void) { // mS
	return connectTime/1000;
}

void ICACHE_FLASH_ATTR WIFI_Connect(uint8_t* ssid, uint8_t* pass, uint8_t* deviceName, WifiCallback cb) {
	struct station_config stationConf;
	uint8 errorFlags = 0;

	startConnectTime = system_get_time();
	wifiCb = cb;
	if (!wifi_set_opmode(STATION_MODE)) errorFlags |= 1 << 0;
	if (!wifi_station_set_auto_connect(false)) errorFlags |= 1 << 1;
	os_memset(&stationConf, 0, sizeof(struct station_config));
	os_strcpy(stationConf.ssid, ssid);
	os_strcpy(stationConf.password, pass);
	if (!wifi_station_set_config(&stationConf)) errorFlags |= 1 << 2;
	if (!wifi_station_set_auto_connect(true)) errorFlags |= 1 << 3;
	if (!wifi_station_connect()) errorFlags |= 1 << 4;
	if (errorFlags)
		ERRORP("WIFI_Connect errors %x\n", errorFlags);
	TESTP("Hostname is: %s\n", wifi_station_get_hostname());
}

