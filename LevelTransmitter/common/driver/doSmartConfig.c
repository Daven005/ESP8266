/*
 * smartConfig.c
 *
 *  Created on: 12 Jul 2016
 *      Author: User
 */

#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#ifdef USE_WIFI
#include "smartconfig.h"

#include "doSmartConfig.h"
#include "user_conf.h"
#include "config.h"
#include "sysCfg.h"
#include "debug.h"

void ICACHE_FLASH_ATTR smartConfig_done(sc_status status, void *pdata) {
	switch (status) {
	case SC_STATUS_WAIT:
		INFOP("SC_STATUS_WAIT\n");
		break;
	case SC_STATUS_FIND_CHANNEL:
		INFOP("SC_STATUS_FIND_CHANNEL\n");
		break;
	case SC_STATUS_GETTING_SSID_PSWD:
		INFOP("SC_STATUS_GETTING_SSID_PSWD\n");
		break;
	case SC_STATUS_LINK:
		INFOP("SC_STATUS_LINK\n");
		struct station_config *sta_conf = pdata;
		wifi_station_set_config(sta_conf);
		INFOP("Connected to %s (%s) %d", sta_conf->ssid, sta_conf->password, sta_conf->bssid_set);
		os_strcpy(sysCfg.sta_ssid, sta_conf->ssid);
		os_strcpy(sysCfg.sta_pwd, sta_conf->password);
		CFG_dirty();
		wifi_station_disconnect();
		wifi_station_connect();
		break;
	case SC_STATUS_LINK_OVER:
		INFOP("SC_STATUS_LINK_OVER\n");
		smartconfig_stop();
		checkSmartConfig(SC_HAS_STOPPED);
		break;
	}
}

bool ICACHE_FLASH_ATTR checkSmartConfig(enum SmartConfigAction action) {
	static bool doingSmartConfig = false;

	switch (action) {
	case SC_CHECK:
		break;
	case SC_HAS_STOPPED:
		INFOP("Finished smartConfig\n");
		stopFlash();
		doingSmartConfig = false;
		startConnection();
		break;
	case SC_TOGGLE:
		if (doingSmartConfig) {
			INFOP("Stop smartConfig\n");
			stopFlash();
			smartconfig_stop();
			doingSmartConfig = false;
			wifi_station_disconnect();
			wifi_station_connect();
			startConnection();
		} else {
			INFOP("Start smartConfig\n");
			stopConnection();
			startFlash(-1, 100, 900);
			doingSmartConfig = true;
			smartconfig_start(smartConfig_done, true);
		}
		break;
	}
	return doingSmartConfig;
}
#else
#pragma message "No WiFi"
#endif
