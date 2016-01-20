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
LOCAL os_timer_t switch_timer;
LOCAL os_timer_t ipScan_timer;
LOCAL os_timer_t mqtt_timer;
LOCAL os_timer_t flash_timer;
LOCAL os_timer_t time_timer;
LOCAL os_timer_t msg_timer;

static int flashCount;
static unsigned int switchCount;
uint8 mqttConnected;
enum {
	IDLE, RESTART, FLASH, IPSCAN, SWITCH_SCAN, MQTT_DATA_CB, SMART_CONFIG
} lastAction __attribute__ ((section (".noinit")));
MQTT_Client mqttClient;
static uint32 minHeap = 0xffffffff;

static bool checkSmartConfig(enum SmartConfigAction action);
bool getUnmappedTemperature(int i, struct Temperature **t);
void publishError(uint8 err, int info);
static bool jsoneq(const char *json, jsmntok_t *tok, const char *s);

#define SWITCH 0 // GPI00
#define LED 5 // NB same as an_t t);

void user_rf_pre_init(void) {
}

uint32 ICACHE_FLASH_ATTR checkMinHeap(void) {
	uint32 heap = system_get_free_heap_size();
	if (heap < minHeap)
		minHeap = heap;
	return minHeap;
}

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

void ICACHE_FLASH_ATTR publishTemperatures(MQTT_Client* client, uint8 idx) {
	struct Temperature *t;

	if (mqttConnected) {
		char *topicBuf = (char*) os_malloc(100), *dataBuf = (char*) os_malloc(100);
		if (idx == 0xff) {
			for (idx = 0; idx < MAX_TEMPERATURE_SENSOR; idx++) {
				if (getUnmappedTemperature(idx, &t)) {
					os_sprintf(topicBuf, (const char*) "/Raw/%s/%s/info", sysCfg.device_id,
							t->address);
					os_sprintf(dataBuf, (const char*) "{ \"Type\":\"Temp\", \"Value\":\"%c%d.%d\"}",
							t->sign, t->val, t->fract);
					MQTT_Publish(client, topicBuf, dataBuf, strlen(dataBuf), 0, 0);
				}
			}
		} else {
			if (getUnmappedTemperature(idx, &t)) {
				os_sprintf(topicBuf, (const char*) "/Raw/%s/%s/info", sysCfg.device_id, t->address);
				os_sprintf(dataBuf, (const char*) "{ \"Type\":\"Temp\", \"Value\":\"%c%d.%d\"}",
						t->sign, t->val, t->fract);
				MQTT_Publish(client, topicBuf, dataBuf, strlen(dataBuf), 0, 0);
			}
		}
		checkMinHeap();
		os_free(topicBuf);
		os_free(dataBuf);
	}
}

void ICACHE_FLASH_ATTR extraPublishTemperatures(uint8 idx) {
	publishTemperatures(&mqttClient, idx);
}

void ICACHE_FLASH_ATTR publishError(uint8 err, int info) {
	static uint8 last_err = 0xff;
	static int last_info = -1;
	if (err == last_err && info == last_info)
		return; // Ignore repeated errors
	char *topicBuf = (char*) os_malloc(50), *dataBuf = (char*) os_malloc(100);
	os_sprintf(topicBuf, (const char*) "/Raw/%s/error", sysCfg.device_id);
	os_sprintf(dataBuf, (const char*) "{ \"error\":%d, \"info\":%d}", err, info);
	MQTT_Publish(&mqttClient, topicBuf, dataBuf, strlen(dataBuf), 0, 0);
	checkMinHeap();
	os_free(topicBuf);
	os_free(dataBuf);
}

void ICACHE_FLASH_ATTR publishInput(uint8 idx, uint8 val) {
	if (mqttConnected) {
		char *topicBuf = (char*) os_malloc(50), *dataBuf = (char*) os_malloc(100);

		os_sprintf(topicBuf, (const char*) "/Raw/%s/%d/info", sysCfg.device_id,
				idx + INPUT_SENSOR_ID_START);
		os_sprintf(dataBuf,
				(const char*) "{\"Name\":\"IP%d\", \"Type\":\"Input\", \"Value\":\"%d\"}", idx,
				val);
		MQTT_Publish(&mqttClient, topicBuf, dataBuf, strlen(dataBuf), 0, 0);
		checkMinHeap();
		os_free(topicBuf);
		os_free(dataBuf);
	}
}

void ICACHE_FLASH_ATTR publishOutput(uint8 idx, uint8 val) {
	if (mqttConnected) {
		char *topicBuf = (char*) os_malloc(50), *dataBuf = (char*) os_malloc(100);

		os_sprintf(topicBuf, (const char*) "/Raw/%s/%d/info", sysCfg.device_id,
				idx + OUTPUT_SENSOR_ID_START);
		os_sprintf(dataBuf,
				(const char*) "{\"Name\":\"OP%d\", \"Type\":\"Output\", \"Value\":\"%d\"}", idx,
				val);
		MQTT_Publish(&mqttClient, topicBuf, dataBuf, strlen(dataBuf), 0, 0);
		INFOP("%s--->%s\n", topicBuf, dataBuf);
		checkMinHeap();
		os_free(topicBuf);
		os_free(dataBuf);
	} else {
		INFOP("o/p %d--->%d\n", idx, val);
	}
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
		publishOutput(idx, output(idx));
	}
}

void ICACHE_FLASH_ATTR publishDeviceInfo(MQTT_Client* client) {
	if (mqttConnected) {
		char *topic = (char *) os_zalloc(50);
		char *data = (char *) os_zalloc(200);
		int idx;

		os_sprintf(topic, "/Raw/%10s/info", sysCfg.device_id);
		os_sprintf(data,
			"{\"Name\":\"%s\", \"Location\":\"%s\", \"Version\":\"%s\", \"Updates\":%d, \"Inputs\":%d, \"RSSI\":%d, \"Settings\":[",
			sysCfg.deviceName, sysCfg.deviceLocation, version, sysCfg.updates, sysCfg.inputs, wifi_station_get_rssi());
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

void ICACHE_FLASH_ATTR mqttCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*) args;

	publishTemperatures(client, 0xff);
	publishAllInputs(client);
	publishAllOutputs(client);
}

void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status) {
	os_printf("WiFi status: %d\n", status);
	if (status == STATION_GOT_IP) {
		MQTT_Connect(&mqttClient);
	} else {
		os_timer_disarm(&mqtt_timer);
		MQTT_Disconnect(&mqttClient);
	}
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
	startFlashCount(250, action);
	switch (action) {
	case 1:
		if (!checkSmartConfig(SC_CHECK))
			boilerSwitchAction();
		break;
	case 2:
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

void ICACHE_FLASH_ATTR publishDeviceReset(MQTT_Client* client) {
	if (mqttConnected) {
		char *topic = (char *) os_zalloc(50);
		char *data = (char *) os_zalloc(200);
		int idx;

		os_sprintf(topic, "/Raw/%10s/reset", sysCfg.device_id);
		os_sprintf(data,
			"{\"Name\":\"%s\", \"Location\":\"%s\", \"Version\":\"%s\", \"Reason\":%d, \"LastAction\":%d}",
			sysCfg.deviceName, sysCfg.deviceLocation, version, system_get_rst_info()->reason, lastAction);
		MQTT_Publish(client, topic, data, strlen(data), 0, false);
		os_printf("%s=>%s\n", topic, data);
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t *args) {
	char topic[100];
	char data[150];

	MQTT_Client* client = (MQTT_Client*) args;
	os_printf("MQTT connected\n");
	mqttConnected = true;
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
	MQTT_Subscribe(client, "/App/Temp/hourly", 0);

	publishDeviceReset(client);
	publishDeviceInfo(client);

	os_timer_disarm(&mqtt_timer);
	os_timer_setfn(&mqtt_timer, (os_timer_func_t *) mqttCb, (void *) client);
	os_timer_arm(&mqtt_timer, sysCfg.updates * 1000, true);

	os_timer_disarm(&switch_timer);
	os_timer_setfn(&switch_timer, (os_timer_func_t *) switchTimerCb, NULL);
	os_timer_arm(&switch_timer, 100, true);

	initIO();

	easygpio_outputSet(LED, 0);
}

void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*) args;
	mqttConnected = false;
	os_timer_disarm(&mqtt_timer);
	os_printf("MQTT disconnected\r\n");
}

void ICACHE_FLASH_ATTR time_cb(void *arg) {
	incTime(); // Update every 1 second
}

static void ICACHE_FLASH_ATTR decodeSensorClear(char *idPtr, char *param, MQTT_Client* client) {
	if (strcmp("temperature", param) == 0) {
		uint8 idx = clearTemperatureOverride(idPtr);
		extraPublishTemperatures(idx);
	} else if (strcmp("output", param) == 0) {
		overrideClearOutput(atoi(idPtr));
	} else if (strcmp("input", param) == 0) {
		overrideClearInput(atoi(idPtr));
	}
}

//static void ICACHE_FLASH_ATTR printMap(void) {
//	int i;
//	for (i = 0; i < sizeof(sysCfg.mapping); i++) {
//		os_printf("[%d]=%d ", i, sysCfg.mapping[i]);
//	}
//	os_printf("\n");
//}

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
				publishDeviceInfo(client);
			}
		}
	} else if (strcmp("temperature", param) == 0) {
		uint8 idx = setTemperatureOverride(idPtr, valPtr);
		extraPublishTemperatures(idx);
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
		os_timer_disarm(&mqtt_timer);
		os_timer_arm(&mqtt_timer, sysCfg.updates * 1000, true);
	} else if (strcmp("inputs", param) == 0) {
		sysCfg.inputs = atoi(dataBuf);
	}
	publishDeviceInfo(client);
	CFG_Save();
}

static bool ICACHE_FLASH_ATTR jsoneq(const char *json, jsmntok_t *tok, const char *s) {
	if (tok->type == JSMN_STRING && (int) strlen(s) == tok->end - tok->start
			&& strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
		return true;
	}
	return false;
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
	char *tokens[10];

	os_timer_disarm(&msg_timer);
	os_timer_arm(&msg_timer, 10 * 60 * 1000, true); // Restart it

	MQTT_Client* client = (MQTT_Client*) args;

	os_memcpy(topicBuf, topic, topic_len);
	os_memcpy(dataBuf, data, data_len);
	TESTP("Receive topic: %s, data: %s \r\n", topicBuf, dataBuf);

	int tokenCount = splitString((char *) topicBuf, '/', tokens);

	if (tokenCount >= 4 && strcmp("Raw", tokens[0]) == 0) {
		if (tokenCount == 4 && strcmp(sysCfg.device_id, tokens[1]) == 0
				&& strcmp("set", tokens[2]) == 0) {
			if (strlen(dataBuf) < NAME_SIZE - 1) {
				decodeDeviceSet(tokens[3], dataBuf, client);
			}
		} else if (tokenCount == 5 && strcmp(sysCfg.device_id, tokens[1]) == 0) {
			if (strcmp("set", tokens[3]) == 0) {
				decodeSensorSet(dataBuf, tokens[2], tokens[4], client);
			} else if (strcmp("clear", tokens[3]) == 0) {
				decodeSensorClear(tokens[2], tokens[4], client);
			}
		}
	} else if (tokenCount >= 2 && strcmp("App", tokens[0]) == 0) {
		if (tokenCount == 2 && strcmp("date", tokens[1]) == 0) {
			setTime((time_t) atol(dataBuf));
			os_timer_disarm(&time_timer); // Restart it
			os_timer_arm(&time_timer, 10 * 60 * 1000, false); //10 minutes
		} else if (tokenCount == 3 && strcmp("Temp", tokens[1]) == 0) {
			if (strcmp("hourly", tokens[2]) == 0) {
				decodeTemps(dataBuf);
			} else if (strcmp("current", tokens[2]) == 0) {
				setOutsideTemp(0, atol(dataBuf));
			}
		}

	}
	checkMinHeap();
	os_free(topicBuf);
	os_free(dataBuf);
}

void ICACHE_FLASH_ATTR ipScan_cb(void *arg) {
	ds18b20StartScan();
	checkInputs(false);
	checkControl();
	checkOutputs();
}

void ICACHE_FLASH_ATTR msgTimerCb(void) {
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

LOCAL void ICACHE_FLASH_ATTR initDone_cb() {
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
	system_print_meminfo();
	INFOP("Flashsize map %d; id %lx\n", system_get_flash_size_map(), spi_flash_get_id());

	WIFI_Connect(sysCfg.sta_ssid, sysCfg.sta_pwd, sysCfg.deviceName, wifiConnectCb);

	os_timer_disarm(&time_timer);
	os_timer_setfn(&time_timer, (os_timer_func_t *) time_cb, (void *) 0);
	os_timer_arm(&time_timer, 1000, true);

	os_timer_disarm(&msg_timer);
	os_timer_setfn(&msg_timer, (os_timer_func_t *) msgTimerCb, (void *) 0);
	os_timer_arm(&msg_timer, 10 * 60 * 1000, true);

	initBoilerControl();

	os_timer_disarm(&ipScan_timer);
	os_timer_setfn(&ipScan_timer, (os_timer_func_t *) ipScan_cb, (void *) 0);
	os_timer_arm(&ipScan_timer, 2000, true);
}

void user_init(void) {
	stdout_init();
	gpio_init();

	easygpio_pinMode(SWITCH, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_outputDisable(SWITCH);

	easygpio_pinMode(LED, EASYGPIO_PULLUP, EASYGPIO_OUTPUT);
	easygpio_outputSet(LED, 1);

	wifi_station_set_auto_connect(false);
	wifi_station_set_reconnect_policy(true);

	system_init_done_cb(&initDone_cb);
}
