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
#include "easygpio.h"
#include "stdout.h"
#include "user_config.h"
#include "config.h"
#include "wifi.h"
#include "debug.h"
#include "mqtt.h"
#include "jsmn.h"
#include "smartconfig.h"
#include "version.h"
#ifdef USE_TEMPERATURE
#include "temperature.h"
#endif
#include "time.h"

enum SmartConfigAction {
	SC_CHECK, SC_HAS_STOPPED, SC_TOGGLE
};
static os_timer_t switch_timer;
static os_timer_t flash_timer;
static os_timer_t transmit_timer;
static os_timer_t date_timer;
static os_timer_t process_timer;
static os_timer_t setup_timer;
static os_timer_t mqtt_timer;

static uint8 wifiChannel = 255;
static int flashCount;
static unsigned int switchCount;
uint8 mqttConnected;
enum {
	IDLE, RESTART, FLASH, IPSCAN, SWITCH_SCAN,
	INIT_DONE, MQTT_DATA_CB, MQTT_CONNECTED_CB, MQTT_DISCONNECTED_CB, SMART_CONFIG
} lastAction __attribute__ ((section (".noinit")));
MQTT_Client mqttClient;
bool setupMode = false;
static char bestSSID[33];

static bool checkSmartConfig(enum SmartConfigAction action);
void publishError(uint8 err, int info);

void checkCanSleep();
void user_rf_pre_init(void) {}

uint32 ICACHE_FLASH_ATTR checkMinHeap(void) {
	static uint32 minHeap = 0xffffffff;
	uint32 heap = system_get_free_heap_size();
	if (heap < minHeap)
		minHeap = heap;
	return minHeap;
}

int ICACHE_FLASH_ATTR splitString(char *string, char delim, char *tokens[]) {
	char *endString;
	char *startString;

	startString = string;
	while (*string) {
		if (*string == delim)
			*string = '\0';
		string++;
	}
	endString = string;
	string = startString;
	int idx = 0;
	if (*string == '\0')
		string++; // Ignore 1st leading delimiter
	while (string < endString) {
		tokens[idx] = string;
		string++;
		idx++;
		while (*string++)
			;
	}
	return idx;
}

static size_t ICACHE_FLASH_ATTR fs_size() { // returns the flash chip's size, in BYTES
  uint32_t id = spi_flash_get_id();
  uint8_t mfgr_id = id & 0xff;
  uint8_t type_id = (id >> 8) & 0xff; // not relevant for size calculation
  uint8_t size_id = (id >> 16) & 0xff; // lucky for us, WinBond ID's their chips as a form that lets us calculate the size
  if(mfgr_id != 0xEF) // 0xEF is WinBond; that's all we care about (for now)
    return 0;
  return 1 << size_id;
}

#ifdef LED
void ICACHE_FLASH_ATTR stopFlash(void) {
	easygpio_outputSet(LED, 0);
	os_timer_disarm(&flash_timer);
}

void ICACHE_FLASH_ATTR flash_cb(void) {
	easygpio_outputSet(LED, !easygpio_inputGet(LED));
	if (flashCount > 0)
		flashCount--;
	else if (flashCount == 0)
		stopFlash();
	// else -ve => continuous
}

void ICACHE_FLASH_ATTR startFlashCount(int t, unsigned int f) {
	easygpio_outputSet(LED, 1);
	flashCount = f * 2;
	os_timer_disarm(&flash_timer);
	os_timer_setfn(&flash_timer, (os_timer_func_t *) flash_cb, (void *) 0);
	os_timer_arm(&flash_timer, t, true);
}

void ICACHE_FLASH_ATTR startFlash(int t, int repeat) {
	easygpio_outputSet(LED, 1);
	flashCount = -1;
	os_timer_disarm(&flash_timer);
	os_timer_setfn(&flash_timer, (os_timer_func_t *) flash_cb, (void *) 0);
	os_timer_arm(&flash_timer, t, true);
}
#endif

void ICACHE_FLASH_ATTR publishError(uint8 err, int info) {
	static uint8 last_err = 0xff;
	static int last_info = -1;
	if (err == last_err && info == last_info)
		return; // Ignore repeated errors
	char *topic = (char*) os_malloc(50), *data = (char*) os_malloc(100);
	if (topic == NULL || data == NULL) {
		startFlash(50, true); // fast
		return;
	}
	os_sprintf(topic, (const char*) "/Raw/%s/error", sysCfg.device_id);
	os_sprintf(data, (const char*) "{ \"error\":%d, \"info\":%d}", err, info);
	MQTT_Publish(&mqttClient, topic, data, strlen(data), 0, 0);
	checkMinHeap();
	os_free(topic);
	os_free(data);
}

void ICACHE_FLASH_ATTR publishData(MQTT_Client* client) {
	if (mqttConnected) {
		char *topic = (char *) os_zalloc(100);
		char *data = (char *) os_zalloc(100);
		os_sprintf(topic, (const char*) "/Raw/%s/0/info", sysCfg.device_id);
		os_sprintf(data, (const char*) "{ \"Type\":\"Level\", \"Value\":%d}", system_adc_read());
		MQTT_Publish(client, topic, data, strlen(data), 0, false);
		TESTP("%s=>%s\n", topic, data);
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR publishDeviceInfo(MQTT_Client* client) {
	if (mqttConnected) {
		char *topic = (char *) os_zalloc(50);
		char *data = (char *) os_zalloc(300);
		int idx;
		struct ip_info ipConfig;
#if SLEEP_MODE == 1
#define MODE "s"
#else
#define MODE "a"
#endif
		if (topic == NULL || data == NULL) {
			startFlash(50, true); // fast
			return;
		}
		wifi_get_ip_info(STATION_IF, &ipConfig);

		os_sprintf(topic, "/Raw/%10s/info", sysCfg.device_id);
		os_sprintf(data,
				"{\"Name\":\"%s\", \"Location\":\"%s\", \"Version\":\"%s(%s)\", "
				"\"Updates\":%d, \"Inputs\":%d, \"RSSI\":%d, \"Channel\": %d, \"Attempts\": %d, ",
				sysCfg.deviceName, sysCfg.deviceLocation, version, MODE, sysCfg.updates,
				sysCfg.inputs, wifi_station_get_rssi(), wifiChannel, WIFI_Attempts());
		os_sprintf(data + strlen(data), "\"IPaddress\":\"%d.%d.%d.%d\"", IP2STR(&ipConfig.ip.addr));
		os_sprintf(data + strlen(data),", \"AP\":\"%s\"", bestSSID);
		os_sprintf(data + strlen(data),", \"Settings\":[");
		for (idx = 0; idx < SETTINGS_SIZE; idx++) {
			if (idx != 0)
				os_sprintf(data + strlen(data), ", ");
			os_sprintf(data + strlen(data), "%d", sysCfg.settings[idx]);
		}
		os_sprintf(data + strlen(data), "]}");
		MQTT_Publish(client, topic, data, strlen(data), 0, true);
		INFOP("%s=>%s\n", topic, data);
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR publishDeviceReset(MQTT_Client* client) {
	if (mqttConnected) {
		char *topic = (char *) os_zalloc(50);
		char *data = (char *) os_zalloc(100);
		int idx;

		os_sprintf(topic, "/Raw/%10s/reset", sysCfg.device_id);
		os_sprintf(data,
				"{\"Name\":\"%s\", \"Location\":\"%s\", \"Version\":\"%s\", \"Reason\":%d, \"LastAction\":%d}",
				sysCfg.deviceName, sysCfg.deviceLocation, version, system_get_rst_info()->reason,
				lastAction);
		INFOP("%s=>%s\n", topic, data);
		MQTT_Publish(client, topic, data, strlen(data), 0, false);
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

#ifdef USE_SMART_CONFIG
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
		strcpy(sysCfg.sta_ssid, sta_conf->ssid);
		strcpy(sysCfg.sta_pwd, sta_conf->password);
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
		MQTT_Connect(&mqttClient);
		break;
	case SC_TOGGLE:
		if (doingSmartConfig) {
			INFOP("Stop smartConfig\n");
			stopFlash();
			smartconfig_stop();
			doingSmartConfig = false;
			wifi_station_disconnect();
			wifi_station_connect();
			MQTT_Connect(&mqttClient);
		} else {
			INFOP("Start smartConfig\n");
			MQTT_Disconnect(&mqttClient);
			mqttConnected = false;
			startFlash(100, true);
			doingSmartConfig = true;
			smartconfig_start(smartConfig_done, true);
		}
		break;
	}
	return doingSmartConfig;
}
#endif

void ICACHE_FLASH_ATTR transmitCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*) args;
	if (!checkSmartConfig(SC_CHECK)) {
		publishData(client);
		checkMinHeap();
	}
}

void ICACHE_FLASH_ATTR processCb() {

}

void ICACHE_FLASH_ATTR printAll() {
	os_printf("Test\n");
}

#ifdef USE_WEB_CONFIG
void ICACHE_FLASH_ATTR setup_cb(void) {
	setupMode = false;
	stopFlash();
}

void ICACHE_FLASH_ATTR toggleSetupMode(void) {
	setupMode = !setupMode;
	if (setupMode) {
		os_timer_disarm(&setup_timer);
		os_timer_setfn(&setup_timer, (os_timer_func_t *) setup_cb, (void *) 0);
		os_timer_arm(&setup_timer, 10 * 60 * 1000, false); // Allow 10 minutes
		startFlash(500, true);
	} else {
		os_timer_disarm(&setup_timer);
		stopFlash();
		checkCanSleep();
	}
}
#endif

#ifdef SWITCH
void ICACHE_FLASH_ATTR switchAction(int action) {
	startFlashCount(100, action);
	switch (action) {
	case 1:
		rf433_start();
		break;
	case 2:
		break;
	case 3:
		printAll();
		rf433_printTimings(0);
		os_printf("minHeap: %d\n", checkMinHeap());
		break;
	case 4:
		if (!checkSmartConfig(SC_CHECK))
			toggleSetupMode();
		break;
	case 5:
		checkSmartConfig(SC_TOGGLE);
		break;
	}
}

void ICACHE_FLASH_ATTR switchTimerCb(uint32_t *args) {
	const int swOnMax = 100;
	const int swOffMax = 5;
	static int switchPulseCount;
	static enum {
		IDLE, ON, OFF
	} switchState = IDLE;

	if (!easygpio_inputGet(SWITCH)) { // Switch is active LOW
		switch (switchState) {
		case IDLE:
			switchState = ON;
			switchCount++;
			switchPulseCount = 1;
			break;
		case ON:
			if (++switchCount > swOnMax)
				switchCount = swOnMax;
			break;
		case OFF:
			switchState = ON;
			switchCount = 0;
			switchPulseCount++;
			break;
		default:
			switchState = IDLE;
			break;
		}
	} else {
		switch (switchState) {
		case IDLE:
			break;
		case ON:
			switchCount = 0;
			switchState = OFF;
			break;
		case OFF:
			if (++switchCount > swOffMax) {
				switchState = IDLE;
				switchAction(switchPulseCount);
				switchPulseCount = 0;
			}
			break;
		default:
			switchState = IDLE;
			break;
		}
	}
}
#endif

void ICACHE_FLASH_ATTR mqttPublishedCb(uint32_t *args) {
	os_timer_disarm(&mqtt_timer);
	os_timer_setfn(&mqtt_timer, (os_timer_func_t *) checkCanSleep, NULL);
	os_timer_arm(&mqtt_timer, 2000, false); // Allow time for subscribed messages to arrive
}

void ICACHE_FLASH_ATTR checkCanSleep(void) {
#if SLEEP_MODE == 1
	if (!checkSmartConfig(SC_CHECK) && !setupMode) {
		TESTP("Sleep %dS\n", sysCfg.updates);
		system_deep_sleep_set_option(1); // Do RF calibration on wake up
		system_deep_sleep(sysCfg.updates * 1000 * 1000);
	} else {
		mqttPublishedCb(NULL);
	}
#endif
}

void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status) {
	os_printf("WiFi status: %d\r\n", status);
	if (status == STATION_GOT_IP) {
		MQTT_Connect(&mqttClient);
#ifdef WEB_CONFIG
		tcp_listen(80);
#endif
	} else {
		mqttConnected = false;
		MQTT_Disconnect(&mqttClient);
	}
}

void ICACHE_FLASH_ATTR dateTimerCb(void) {
	os_printf("Nothing heard so restarting...\n");
	system_restart();
}

void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t *args) {
	char *topic = (char*) os_zalloc(100), *data = (char*) os_zalloc(150 + 1);

	if (topic == NULL || data == NULL) {
		startFlash(50, true); // fast
		return;
	}
	MQTT_Client* client = (MQTT_Client*) args;
	mqttConnected = true;
	TESTP("MQTT: Connected to %s:%d\n", sysCfg.mqtt_host, sysCfg.mqtt_port);

	os_sprintf(topic, "/Raw/%s/set/#", sysCfg.device_id);
	INFOP("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);
	os_sprintf(topic, "/Raw/%s/+/set/#", sysCfg.device_id);
	INFOP("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);
	os_sprintf(topic, "/Raw/%s/+/clear/#", sysCfg.device_id);
	INFOP("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	MQTT_Subscribe(client, "/App/date", 0);

	publishDeviceReset(client);
	publishDeviceInfo(client);

	os_timer_disarm(&switch_timer);
	os_timer_setfn(&switch_timer, (os_timer_func_t *) switchTimerCb, NULL);
	os_timer_arm(&switch_timer, 100, true);

	os_timer_disarm(&date_timer);
	os_timer_setfn(&date_timer, (os_timer_func_t *) dateTimerCb, NULL);
	os_timer_arm(&date_timer, 10 * 60 * 1000, false); //10 minutes

	os_timer_disarm(&transmit_timer);
	os_timer_setfn(&transmit_timer, (os_timer_func_t *) transmitCb, (void *) client);
	os_timer_arm(&transmit_timer, sysCfg.updates * 1000, true);

	os_timer_disarm(&process_timer);
	os_timer_setfn(&process_timer, (os_timer_func_t *) processCb, NULL);
	os_timer_arm(&process_timer, 1000, true); // 1S

	checkMinHeap();
	os_free(topic);
	os_free(data);
#ifdef LED
	easygpio_outputSet(LED, 0);
#endif
	lastAction = MQTT_CONNECTED_CB;
}

void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args) {
//	MQTT_Client* client = (MQTT_Client*)args;
	os_printf("MQTT Disconnected\n");
	mqttConnected = false;
	if (!checkSmartConfig(SC_CHECK)) {
		MQTT_Connect(&mqttClient);
	}
}

void ICACHE_FLASH_ATTR mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len,
		const char *data, uint32_t data_len) {
	char *topicBuf = (char*) os_zalloc(topic_len + 1), *dataBuf = (char*) os_zalloc(data_len + 1);
	char *tokens[10];

	MQTT_Client* client = (MQTT_Client*) args;

	os_memcpy(topicBuf, topic, topic_len);
	topicBuf[topic_len] = 0;
	os_memcpy(dataBuf, data, data_len);
	dataBuf[data_len] = 0;
	os_printf("Receive topic: %s, data: %s \r\n", topicBuf, dataBuf);

	int tokenCount = splitString((char *) topicBuf, '/', tokens);

	if (tokenCount > 0) {
		if (strcmp("Raw", tokens[0]) == 0) {
			if (tokenCount == 4 && strcmp(sysCfg.device_id, tokens[1]) == 0
					&& strcmp("set", tokens[2]) == 0) {
				if (strlen(dataBuf) < NAME_SIZE - 1) {
					if (strcmp("name", tokens[3]) == 0) {
						strcpy(sysCfg.deviceName, dataBuf);
					} else if (strcmp("location", tokens[3]) == 0) {
						strcpy(sysCfg.deviceLocation, dataBuf);
					} else if (strcmp("updates", tokens[3]) == 0) {
						sysCfg.updates = atoi(dataBuf);
						os_timer_disarm(&transmit_timer);
						os_timer_arm(&transmit_timer, sysCfg.updates * 1000, true);
					}
					publishDeviceInfo(client);
					CFG_Save();
				}
			}
		} else if (strcmp("App", tokens[0]) == 0) {
			if (tokenCount >= 2 && strcmp("date", tokens[1]) == 0) {
				os_timer_disarm(&date_timer); // Restart it
				os_timer_arm(&date_timer, 10 * 60 * 1000, false); //10 minutes
			}
		}
		checkMinHeap();
		os_free(topicBuf);
		os_free(dataBuf);
		lastAction = MQTT_DATA_CB;
	}
}

void ICACHE_FLASH_ATTR showSysInfo() {
	INFOP("SDK version is: %s\n", system_get_sdk_version());
	INFOP("Smart-Config version is: %s\n", smartconfig_get_version());
	system_print_meminfo();
	int sz = fs_size();
	INFOP("Flashsize map %d; id %lx (%lx - %d bytes)\n", system_get_flash_size_map(),
			spi_flash_get_id(), sz, sz / 8);
	INFOP("Boot mode: %d, version %d. Userbin %lx\n", system_get_boot_mode(),
			system_get_boot_version(), system_get_userbin_addr());
}

static void ICACHE_FLASH_ATTR startUp() {
	CFG_Load();
	os_printf("\n%s ( %s ) starting ...\n", sysCfg.deviceName, version);

	MQTT_InitConnection(&mqttClient, sysCfg.mqtt_host, sysCfg.mqtt_port, sysCfg.security);
	MQTT_InitClient(&mqttClient, sysCfg.device_id, sysCfg.mqtt_user, sysCfg.mqtt_pass,
			sysCfg.mqtt_keepalive, 1);

	char temp[100];
	os_sprintf(temp, "/Raw/%s/offline", sysCfg.device_id);
	MQTT_InitLWT(&mqttClient, temp, "offline", 0, 0);

	MQTT_OnConnected(&mqttClient, mqttConnectedCb);
	MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
	MQTT_OnData(&mqttClient, mqttDataCb);

	showSysInfo();
	if (strncmp(bestSSID, sysCfg.sta_ssid, 5) != 0) { // Dissimilar SSID
		strcpy(bestSSID, sysCfg.sta_ssid); // Use stored SSID; nb assumes same password
	}
	WIFI_Connect(bestSSID, sysCfg.sta_pwd, sysCfg.deviceName, wifiConnectCb);

	os_timer_disarm(&switch_timer);
	os_timer_setfn(&switch_timer, (os_timer_func_t *) switchTimerCb, NULL);
	os_timer_arm(&switch_timer, 100, true);

	rf433_init();

#ifdef USE_TEMPERATURE
	ds18b20StartScan();
#endif
	lastAction = INIT_DONE;
}

#ifdef USE_WIFI_SCAN
static void ICACHE_FLASH_ATTR wifi_station_scan_done(void *arg, STATUS status) {
  uint8 ssid[33];
  int8 bestRSSI = -100;

  if (status == OK) {
    struct bss_info *bss_link = (struct bss_info *)arg;

    while (bss_link != NULL) {
      os_memset(ssid, 0, 33);
      if (os_strlen(bss_link->ssid) <= 32) {
        os_memcpy(ssid, bss_link->ssid, os_strlen(bss_link->ssid));
      } else {
        os_memcpy(ssid, bss_link->ssid, 32);
      }
      if (bss_link->rssi > bestRSSI) {
    	  bestRSSI = bss_link->rssi;
    	  strcpy(bestSSID, ssid);
      }
      TESTP("WiFi Scan: (%d,\"%s\",%d)\n", bss_link->authmode, ssid, bss_link->rssi);
      bss_link = bss_link->next.stqe_next;
    }
  } else {
	  os_printf("wifi_station_scan fail %d\n", status);
  }
  startUp();
}

static void ICACHE_FLASH_ATTR initDone_cb() {
	TESTP("Start WiFi Scan\n");
	wifi_set_opmode(STATION_MODE);
	wifi_station_scan(NULL, wifi_station_scan_done);
}
#endif

void ICACHE_FLASH_ATTR user_init(void) {
	stdout_init();
	gpio_init();

#ifdef SWITCH
	easygpio_pinMode(SWITCH, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_outputDisable(SWITCH);
#endif
#ifdef LED
	easygpio_pinMode(LED, EASYGPIO_PULLUP, EASYGPIO_OUTPUT);
	easygpio_outputSet(LED, 1);
#endif
	wifi_station_set_auto_connect(false);
	wifi_station_set_reconnect_policy(true);
#ifdef USE_WIFI_SCAN
	system_init_done_cb(&initDone_cb);
#else
	system_init_done_cb(&startUp);
#endif
}
