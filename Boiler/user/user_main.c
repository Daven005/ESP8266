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
#include <uart.h>
#include <ds18b20.h>
#include <user_interface.h>
#include "easygpio.h"
#include "stdout.h"

#include "config.h"
#include "wifi.h"
#include "debug.h"
#include "mqtt.h"
#include "jsmn.h"
#include "smartconfig.h"
#include "io.h"
#include "version.h"
#include "temperature.h"
#include "time.h"
#include "BoilerControl.h"

enum SmartConfigAction {
	SC_CHECK, SC_HAS_STOPPED, SC_TOGGLE
};
static os_timer_t ipScan_timer;
static os_timer_t transmit_timer;
static os_timer_t time_timer;
static os_timer_t msg_timer;
static os_timer_t setup_timer;

typedef struct { MQTT_Client *mqttClient; char *topic; char *data; } mqttData_t;
#define QUEUE_SIZE 20
os_event_t taskQueue[QUEUE_SIZE];
#define EVENT_MQTT_CONNECTED 1
#define EVENT_MQTT_DATA 2
#define EVENT_PROCESS_TIMER 3
#define EVENT_TRANSMIT 4

uint8 mqttConnected;
enum lastAction_t {
	IDLE, RESTART, FLASH, PROCESS_FUNC, SWITCH_SCAN,
	INIT_DONE, MQTT_DATA_CB, MQTT_DATA_FUNC, MQTT_CONNECTED_CB, MQTT_DISCONNECTED_CB, SMART_CONFIG,
	WIFI_CONNECT_CHANGE=100
} lastAction __attribute__ ((section (".noinit")));
enum lastAction_t savedLastAction;
MQTT_Client mqttClient;
static uint32 minHeap = 0xffffffff;
bool setupMode = false;
static uint8 wifiChannel = 255;
static char bestSSID[33];

static bool checkSmartConfig(enum SmartConfigAction action);

void user_rf_pre_init(void) {
}

void ICACHE_FLASH_ATTR checkTime(char *func, uint32 previous) {
	uint32 now = system_get_time();
	if ((now-previous) > 8000) {
		TESTP("*** %d XS time in %s\n", (now-previous), func);
	}
}

uint32 ICACHE_FLASH_ATTR checkMinHeap(void) {
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

bool ICACHE_FLASH_ATTR mqttIsConnected(void) {
	return mqttConnected;
}

void ICACHE_FLASH_ATTR publishAllInputs(MQTT_Client* client) {
	uint8 idx;
	for (idx = 0; idx < MAX_INPUT && idx < sysCfg.inputs; idx++) {
		publishInput(idx, input(idx));
	}
}

void ICACHE_FLASH_ATTR publishAllOutputs(MQTT_Client* client) {
	uint8 idx;
	for (idx = 0; idx < MAX_OUTPUT; idx++) {
		publishOutput(idx, outputState(idx));
	}
}

void ICACHE_FLASH_ATTR _publishDeviceInfo(void) {
	publishDeviceInfo(version, "Solar Control", wifiChannel, WIFI_Attempts(), bestSSID, 0);
}

void ICACHE_FLASH_ATTR transmitCb(uint32_t args) { // Depends on Update period
	uint32 t = system_get_time();
	if (!checkSmartConfig(SC_CHECK)) {
		if (!system_os_post(USER_TASK_PRIO_1, EVENT_TRANSMIT, args))
			TESTP("Can't post EVENT_TRANSMIT\n");
	}
	checkTime("transmitCb", t);
}

void ICACHE_FLASH_ATTR publishData() {
	uint32 t = system_get_time();
	if (mqttConnected) {
		publishAllTemperatures();
		publishAllInputs(&mqttClient);
		publishAllOutputs(&mqttClient);
	}
	checkTime("publishData", t);
}

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

void ICACHE_FLASH_ATTR printAll(void) {
	int idx;
	struct Temperature *t;

	os_printf("Temperature Mappings:\n");
	for (idx = 0; idx < sizeof(sysCfg.mapping); idx++) {
		if (printMappedTemperature(idx))
			os_printf("\n");
	}
	os_printf("\nOutputs: ");
	for (idx = 0; idx < MAX_OUTPUT; idx++) {
		printOutput(idx);
	}
	os_printf("\nInputs: ");
	for (idx = 0; idx < MAX_INPUT; idx++) {
		printInput(idx);
	}
	printIOreg();
	os_printf("\nSettings: ");
	for (idx = 0; idx < SETTINGS_SIZE; idx++) {
		os_printf("%d=%d ", idx, sysCfg.settings[idx]);
	}
	printDHW();
	printBCinfo();
}

void ICACHE_FLASH_ATTR switchAction(int action) {
	startFlash(action, 50, 100);
	switch (action) {
	case 1:
		if (!checkSmartConfig(SC_CHECK))
			boilerSwitchAction();
		break;
	case 2:
		if (!checkSmartConfig(SC_CHECK))
			toggleHttpSetupMode();
		break;
	case 3:
		printAll();
		os_printf("minHeap: %d\n", checkMinHeap());
		break;
	case 4:
		break;
	case 5:
		checkSmartConfig(SC_TOGGLE);
		break;
	}
}

void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*) args;
	mqttConnected = false;
	ERRORP("MQTT disconnected\r\n");
	if (lastAction < WIFI_CONNECT_CHANGE) lastAction = MQTT_DISCONNECTED_CB;
}

void ICACHE_FLASH_ATTR time_cb(void *arg) { // Update every 1 second
	incTime();
}

static void ICACHE_FLASH_ATTR decodeSensorClear(char *idPtr, char *param, MQTT_Client* client) {
	if (strcmp("temperature", param) == 0) {
		uint8 idx = clearTemperatureOverride(idPtr);
		publishTemperature(idx);
	} else if (strcmp("output", param) == 0) {
		overrideClearOutput(atoi(idPtr));
	} else if (strcmp("input", param) == 0) {
		overrideClearInput(atoi(idPtr));
	}
}

static void saveMapName(uint8 sensorID, char *bfr) {
	uint8 mapIdx = 0xff;
	char *name = NULL;
	jsmn_parser p;
	jsmntok_t t[20];
	int r, i;

	jsmn_init(&p);
	r = jsmn_parse(&p, bfr, strlen(bfr), t, sizeof(t) / sizeof(t[0]));

	if (r < 0) {
		INFOP("Failed to parse JSON: %d\n", r);
		return;
	}
	if (r < 1 || t[0].type != JSMN_OBJECT) {/* Assume the top-level element is an object */
		INFOP("Object expected\n");
		return;
	}
	INFOP("%d tokens\n", r);
	for (i = 1; i < r; i++) { /* Loop over all keys of the root object */
		if (jsoneq(bfr, &t[i], "map")) {
			mapIdx = atoi(bfr + t[i + 1].start);
		} else if (jsoneq(bfr, &t[i], "name")) {
			if (t[i + 1].type == JSMN_STRING) {
				name = bfr + t[i + 1].start;
				bfr[t[i + 1].end] = 0; // Overwrites trailing quote
			}
		}
	}
	if (mapIdx < MAP_TEMP_SIZE && name != NULL) {
		strcpy(sysCfg.mappingName[mapIdx], name);
		CFG_Save();
		printMappedTemperature(mapIdx);
		INFOP("\n");
	}
}

static void ICACHE_FLASH_ATTR decodeSensorSet(char *valPtr, char *idPtr, char *param,
		MQTT_Client* client) {
	int id = atoi(idPtr);
	int value = atoi(valPtr);
	if (strcmp("mapping", param) == 0) {
		if (strlen(valPtr) == 0) return; // No mapping data
		uint8 mapIdx = atoi(valPtr);
		if (mapIdx >= MAP_TEMP_SIZE) return; // can't be used as mapIdx
		uint8 newMapValue = sensorIdx(idPtr);
		if (newMapValue >= MAP_TEMP_SIZE) return; // can't be used

		sysCfg.mapping[mapIdx] = newMapValue;
		CFG_Save();
	} else if (strcmp("name", param) == 0) {
		int sensorID = sensorIdx(idPtr);
		if (sensorID >= MAP_TEMP_SIZE) {
			TESTP("Invalid sensorID %s for 'name' (%d)\n", idPtr, sensorID);
			return; // can't be used as mapIdx
		}
		saveMapName(sensorID, valPtr);
	} else if (strcmp("setting", param) == 0) {
		if (0 <= id && id < SETTINGS_SIZE) {
			if (SET_MINIMUM <= value && value <= SET_MAXIMUM) {
				sysCfg.settings[id] = value;
				CFG_Save();
				TESTP("Setting %d = %d\n", id, sysCfg.settings[id]);
				_publishDeviceInfo();
			}
		}
	} else if (strcmp("temperature", param) == 0) {
		uint8 idx = setTemperatureOverride(idPtr, valPtr);
		publishTemperature(idx);
	} else if (strcmp("output", param) == 0) {
		if (0 <= id && id < MAX_OUTPUT) {
			overrideSetOutput(id, value);
		}
	} else if (strcmp("input", param) == 0) {
		if (0 <= id && id < MAX_INPUT) {
			overrideSetInput(id, value);
			publishInput(id, value);
		}
	}
}

static void ICACHE_FLASH_ATTR decodeDeviceSet(char* param, char* dataBuf, MQTT_Client* client) {
	if (strcmp("name", param) == 0) {
		strcpy(sysCfg.deviceName, dataBuf);
	} else if (strcmp("location", param) == 0) {
		strcpy(sysCfg.deviceLocation, dataBuf);
	} else if (strcmp("updates", param) == 0) {
		sysCfg.updates = atoi(dataBuf);
		os_timer_disarm(&transmit_timer);
		os_timer_arm(&transmit_timer, sysCfg.updates * 1000, true);
	} else if (strcmp("inputs", param) == 0) {
		sysCfg.inputs = atoi(dataBuf);
	}
	_publishDeviceInfo();
	CFG_Save();
}

void ICACHE_FLASH_ATTR decodeTemps(char *bfr) {
	jsmn_parser p;
	jsmntok_t t[20];
	int r, i;

	jsmn_init(&p);
	r = jsmn_parse(&p, bfr, strlen(bfr), t, sizeof(t) / sizeof(t[0]));

	if (r < 0) {
		INFOP("Failed to parse JSON: %d\n", r);
		return;
	}
	if (r < 1 || t[0].type != JSMN_OBJECT) {/* Assume the top-level element is an object */
		INFOP("Object expected\n");
		return;
	}
	INFOP("%d tokens\n", r);
	for (i = 1; i < r; i++) { /* Loop over all keys of the root object */
		if (jsoneq(bfr, &t[i], "t")) {
			if (t[i + 1].type == JSMN_ARRAY) {
				int j;
				int val;
				INFOP("Temps: ");
				for (j = 0; j < t[i + 1].size; j++) {
					jsmntok_t *g = &t[i + j + 2];
					val = atol(bfr + g->start);
					setOutsideTemp(j + 1, val);
					INFOP("[%d]=%d ", j, val);
				}
				INFOP("\n");
			}
		}
	}
}

void ICACHE_FLASH_ATTR mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len,
		const char *data, uint32_t data_len) {
	char *topicBuf = (char*) os_zalloc(topic_len + 1), *dataBuf = (char*) os_zalloc(data_len + 1);

	mqttData_t *params = (mqttData_t *)os_malloc(sizeof(mqttData_t));
	os_memcpy(topicBuf, topic, topic_len);
	topicBuf[topic_len] = 0;
	os_memcpy(dataBuf, data, data_len);
	dataBuf[data_len] = 0;

	TESTP("Receive topic: %s, data: %s\n", topicBuf, dataBuf);
	params->mqttClient = (MQTT_Client*) args;
	params->topic = topicBuf;
	params->data = dataBuf;
	if (!system_os_post(USER_TASK_PRIO_1, EVENT_MQTT_DATA, (os_param_t) params))
		TESTP("Can't post EVENT_MQTT_DATA\n");
	lastAction = MQTT_DATA_CB;
}

void ICACHE_FLASH_ATTR mqttDataFunction(MQTT_Client *client, char* topic, char *data) {
	char *tokens[10];
	uint32 t = system_get_time();

	lastAction = MQTT_DATA_FUNC;

	int tokenCount = splitString((char *) topic, '/', tokens);

	if (tokenCount >= 4 && strcmp("Raw", tokens[0]) == 0) {
		if (tokenCount == 4 && strcmp(sysCfg.device_id, tokens[1]) == 0
				&& strcmp("set", tokens[2]) == 0) {
			if (strlen(data) < NAME_SIZE - 1) {
				decodeDeviceSet(tokens[3], data, client);
			}
		} else if (tokenCount == 5 && strcmp(sysCfg.device_id, tokens[1]) == 0) {
			if (strcmp("set", tokens[3]) == 0) {
				decodeSensorSet(data, tokens[2], tokens[4], client);
			} else if (strcmp("clear", tokens[3]) == 0) {
				decodeSensorClear(tokens[2], tokens[4], client);
			}
		}
	} else if (tokenCount >= 2 && strcmp("App", tokens[0]) == 0) {
		if (tokenCount == 2 && strcmp("date", tokens[1]) == 0) {
			setTime((time_t) atol(data));
			os_timer_disarm(&msg_timer); // Restart it
			os_timer_arm(&msg_timer, 10 * 60 * 1000, false); //10 minutes
		} else if (tokenCount == 3 && strcmp("Temp", tokens[1]) == 0) {
			if (strcmp("hourly", tokens[2]) == 0) {
				decodeTemps(data);
			} else if (strcmp("current", tokens[2]) == 0) {
				setOutsideTemp(0, atol(data));
			}
		} else if (tokenCount == 2 && strcmp("Refresh", tokens[1]) == 0) {
			publishData(); // publish all I/O & temps
		}
	}
	checkMinHeap();
	os_free(topic);
	os_free(data);
	lastAction = MQTT_DATA_CB;
}

void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status) {
	TESTP("WiFi status: %d\n", status);
	if (status == STATION_GOT_IP) {
		wifiChannel = wifi_get_channel();
		MQTT_Connect(&mqttClient);
		tcp_listen(80);
	} else {
		lastAction = WIFI_CONNECT_CHANGE+status;
		mqttConnected = false;
		MQTT_Disconnect(&mqttClient);
	}
}

void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t *args) {
	if (!system_os_post(USER_TASK_PRIO_1, EVENT_MQTT_CONNECTED, (os_param_t) args))
		TESTP("Can't post EVENT_MQTT_CONNECTED\n");
}

void ICACHE_FLASH_ATTR mqttConnectedFunction(MQTT_Client *client) {
	uint32 t = system_get_time();
	char *topic = (char*) os_zalloc(100);
	static int reconnections = 0;

	os_printf("MQTT: Connected to %s:%d\n", sysCfg.mqtt_host, sysCfg.mqtt_port);

	os_sprintf(topic, "/Raw/%s/set/#", sysCfg.device_id);
	TESTP("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	os_sprintf(topic, "/Raw/%s/+/set/#", sysCfg.device_id);
	TESTP("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	os_sprintf(topic, "/Raw/%s/+/clear/#", sysCfg.device_id);
	INFOP("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	MQTT_Subscribe(client, "/App/date", 0);
	MQTT_Subscribe(client, "/App/Temp/hourly", 0);
	MQTT_Subscribe(client, "/App/refresh", 0);

	if (mqttConnected) { // Has REconnected
		_publishDeviceInfo();
		reconnections++;
		publishError(51, reconnections);
	} else {
		publishDeviceReset();
		_publishDeviceInfo();
		publishMapping();
	}
	checkMinHeap();
	os_free(topic);
	easygpio_outputSet(LED, 0); // Turn LED off when connected
	lastAction = MQTT_CONNECTED_CB;
	mqttConnected = true;
	checkTime("mqttConnectedFunc", t);
}

void ICACHE_FLASH_ATTR processTemperatureCb(void) {
	publishAllTemperatures();
}

void ICACHE_FLASH_ATTR processTimerCb(void) { // 2 sec
	ds18b20StartScan(processTemperatureCb);
	if (!system_os_post(USER_TASK_PRIO_1, EVENT_PROCESS_TIMER, 0))
		TESTP("Can't post EVENT_MQTT_CONNECTED\n");
}

void ICACHE_FLASH_ATTR processTimerFunc(void) {
	uint32 t = system_get_time();
	lastAction = PROCESS_FUNC;
	checkInputs(false);
	checkControl();
	checkOutputs();
	checkTime("processTimerFunc", t);
}

void ICACHE_FLASH_ATTR msgTimerCb(void) { // 10 mins
	os_printf("Nothing heard so restarting...\n");
	lastAction = RESTART;
	system_restart();
}

void ICACHE_FLASH_ATTR initTime(void) {
	struct tm tm;
	tm.tm_year = 2015;
	tm.tm_mon = 10;
	tm.tm_mday = 1;
	tm.tm_hour = 12;
	tm.tm_min = 0;
	tm.tm_sec = 0;
	setTime(mktime(&tm));
}

static void ICACHE_FLASH_ATTR backgroundTask(os_event_t *e) {
	mqttData_t *mqttData;
	TESTP("Background task %d\n", e->sig);
	switch (e->sig) {
	case EVENT_MQTT_CONNECTED:
		mqttConnectedFunction((MQTT_Client *) e->par);
		break;
	case EVENT_MQTT_DATA:
		mqttData = (mqttData_t *) e->par;
		mqttDataFunction(mqttData->mqttClient, mqttData->topic, mqttData->data);
		os_free(mqttData);
		break;
	case EVENT_PROCESS_TIMER:
		processTimerFunc();
		break;
	case EVENT_TRANSMIT:
		publishData();
		break;
	}
}

LOCAL void ICACHE_FLASH_ATTR startUp() {
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

	INFOP("SDK version is: %s\n", system_get_sdk_version());
	INFOP("Smart-Config version is: %s\n", smartconfig_get_version());
	INFO(system_print_meminfo());
	INFOP("Flashsize map %d; id %lx\n", system_get_flash_size_map(), spi_flash_get_id());

	WIFI_Connect(bestSSID, sysCfg.sta_pwd, sysCfg.deviceName, wifiConnectCb);

	os_timer_disarm(&time_timer);
	os_timer_setfn(&time_timer, (os_timer_func_t *) time_cb, (void *) 0);
	os_timer_arm(&time_timer, 1000, true);

	os_timer_disarm(&msg_timer);
	os_timer_setfn(&msg_timer, (os_timer_func_t *) msgTimerCb, (void *) 0);
	os_timer_arm(&msg_timer, 10 * 60 * 1000, true);

	os_timer_disarm(&ipScan_timer);
	os_timer_setfn(&ipScan_timer, (os_timer_func_t *) processTimerCb, (void *) 0);
	os_timer_arm(&ipScan_timer, 2000, true);

	os_timer_disarm(&transmit_timer);
	os_timer_setfn(&transmit_timer, (os_timer_func_t *) transmitCb, 0);
	os_timer_arm(&transmit_timer, sysCfg.updates * 1000, true);

	if ( !system_os_task(backgroundTask, USER_TASK_PRIO_1, taskQueue, QUEUE_SIZE))
		TESTP("Can't set up background task\n");

	initTemperature();
	initBoilerControl();
	lastAction = INIT_DONE;
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
  startUp();
}

static void ICACHE_FLASH_ATTR initDone_cb() {
	TESTP("Start WiFi Scan\n");
	wifi_set_opmode(STATION_MODE);
	wifi_station_scan(NULL, wifi_station_scan_done);
}

void user_init(void) {
	stdout_init();
	gpio_init();

	easygpio_pinMode(SWITCH, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_outputDisable(SWITCH);

	easygpio_pinMode(TEST_OP, EASYGPIO_NOPULL, EASYGPIO_INPUT);
	easygpio_outputDisable(TEST_OP);

	easygpio_pinMode(LED, EASYGPIO_PULLUP, EASYGPIO_OUTPUT);
	easygpio_outputSet(LED, 1);

	initIO();
	savedLastAction = lastAction;
	wifi_station_set_auto_connect(false);
	wifi_station_set_reconnect_policy(true);

	system_init_done_cb(&initDone_cb);
}
