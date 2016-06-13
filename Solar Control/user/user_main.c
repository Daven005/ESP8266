#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "stdout.h"
#include <ds18b20.h>
#include "jsmn.h"

#include "easygpio.h"
#include "config.h"
#include "wifi.h"
#include "debug.h"
#include "mqtt.h"
#include "smartconfig.h"
#include "user_config.h"
#include "version.h"
#include "time.h"
#include "IOdefs.h"
#include "overrideIO.h"
#include "flowMonitor.h"
#include "temperatureMonitor.h"

static os_timer_t switch_timer;
static os_timer_t flash_timer;
static os_timer_t flashA_timer;
static os_timer_t transmit_timer;
static os_timer_t date_timer;
static os_timer_t process_timer;
static os_timer_t flowCheck_timer;

#define PROCESS_REPEAT 5000

MQTT_Client mqttClient;
uint8 mqttConnected;

static unsigned int switchCount;
static unsigned int flashCount;
static int flashActionCount;
static unsigned int flashActionOnTime;
static unsigned int flashActionOffTime;
static uint16 pressure = 0;
static uint8 wifiChannel = 255;
static char bestSSID[33];
static bool toggleState;

static uint32 minHeap = 0xffffffff;

enum {
	IDLE, RESTART, FLASH, IPSCAN, SWITCH_SCAN,
	INIT_DONE, MQTT_DATA_CB, MQTT_CONNECTED_CB, MQTT_DISCONNECTED_CB, SMART_CONFIG,
	PROCESS_CB, DS18B20_CB
} lastAction __attribute__ ((section (".noinit")));
enum SmartConfigAction {
	SC_CHECK, SC_HAS_STOPPED, SC_TOGGLE
};
bool checkSmartConfig(enum SmartConfigAction);
void startPumpNormal(void);
void startPumpOverride(void);
void stopPumpNormal(void);
void stopPumpOverride(void);
void turnOffOverride(void);
void checkActionFlash(void);

void user_rf_pre_init() {
}

uint32 ICACHE_FLASH_ATTR checkMinHeap(void) {
	uint32 heap = system_get_free_heap_size();
	if (heap < minHeap) minHeap = heap;
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

void ICACHE_FLASH_ATTR stopFlash(void) {
	easygpio_outputSet(LED, 0);
	os_timer_disarm(&flash_timer);
}

void ICACHE_FLASH_ATTR flash_cb(void) {
	easygpio_outputSet(LED, !easygpio_inputGet(LED));
	if (flashCount)
		flashCount--;
	if (flashCount == 0)
		stopFlash();
}

void ICACHE_FLASH_ATTR startFlashCount(int t, int repeat, unsigned int f) {
	easygpio_outputSet(LED, 1);
	flashCount = f * 2;
	os_timer_disarm(&flash_timer);
	os_timer_arm(&flash_timer, t, repeat);
}

void ICACHE_FLASH_ATTR startFlash(int t, int repeat) {
	startFlashCount(t, repeat, 0);
}

void ICACHE_FLASH_ATTR stopActionFlash(void) {
	easygpio_outputSet(ACTION_LED, 0);
	os_timer_disarm(&flashA_timer);
}

void ICACHE_FLASH_ATTR flashAction_cb(void) {
	os_timer_disarm(&flashA_timer);
	if (easygpio_inputGet(ACTION_LED)) {
		if (flashActionCount > 0)
			flashActionCount--;
		if (flashActionCount == 0) {
			stopActionFlash();
		} else {
			easygpio_outputSet(ACTION_LED, 0);
			os_timer_arm(&flashA_timer, flashActionOffTime, false);
		}
	} else {
		easygpio_outputSet(ACTION_LED, 1);
		os_timer_arm(&flashA_timer, flashActionOnTime, false);
	}
}

void ICACHE_FLASH_ATTR startActionFlash(int flashCount, unsigned int onTime, unsigned int offTime) {
	TESTP("Start Flash %d %d/%d\n", flashCount, onTime, offTime);
	easygpio_outputSet(ACTION_LED, 1);
	flashActionCount = flashCount;
	flashActionOnTime = onTime;
	flashActionOffTime = offTime;
	os_timer_disarm(&flashA_timer);
	os_timer_arm(&flashA_timer, onTime, false);
}

void ICACHE_FLASH_ATTR checkActionFlash(void) {
	static pumpState_t lastPumpState = PUMP_UNKNOWN;
	pumpState_t thisPumpState = pumpState();

	if (thisPumpState == lastPumpState) return; // Don't restart flash
	lastPumpState = thisPumpState;
	if (toggleState) return;
	switch (thisPumpState) {
	case PUMP_OFF_NORMAL:
		startActionFlash(-1, 200, 1800);
		break;
	case PUMP_ON_NORMAL:
		startActionFlash(-1, 1800, 200);
		break;
	case PUMP_OFF_OVERRIDE:
		startActionFlash(-1, 500, 2500);
		break;
	case PUMP_ON_OVERRIDE :
		startActionFlash(-1, 2500, 500);
		break;
	}
}

void ICACHE_FLASH_ATTR publishAlarm(uint8 alarm, int info) {
	static uint8 last_alarm = 0xff;
	static int last_info = -1;
	if (alarm == last_alarm && info == last_info)
		return; // Ignore repeated identical alarms
	last_alarm = alarm;
	last_info = info;
	char *topic = (char*) os_malloc(50);
	char *data = (char*) os_malloc(100);
	os_sprintf(topic, (const char*) "/Raw/%s/alarm", sysCfg.device_id);
	os_sprintf(data, (const char*) "{ \"alarm\":%d, \"info\":%d}", alarm, info);
	MQTT_Publish(&mqttClient, topic, data, strlen(data), 0, 0);
	TESTP("********%s=>%s\n", topic, data);
	startActionFlash(-1, 200, 200);
	checkMinHeap();
	os_free(topic);
	os_free(data);
}

void ICACHE_FLASH_ATTR publishError(uint8 err, int info) {
	static uint8 last_err = 0xff;
	static int last_info = -1;
	if (err == last_err && info == last_info)
		return; // Ignore repeated errors
	last_err = err;
	last_info = info;
	char *topic = (char*) os_malloc(50);
	char *data = (char*) os_malloc(100);
	os_sprintf(topic, (const char*) "/Raw/%s/error", sysCfg.device_id);
	os_sprintf(data, (const char*) "{ \"error\":%d, \"info\":%d}", err, info);
	MQTT_Publish(&mqttClient, topic, data, strlen(data), 0, 0);
	TESTP("%s=>%s\n", topic, data);
	checkMinHeap();
	os_free(topic);
	os_free(data);
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
		TESTP("%s--->%s\n", topicBuf, dataBuf);
		checkMinHeap();
		os_free(topicBuf);
		os_free(dataBuf);
	} else {
		INFOP("o/p %d--->%d\n", idx, val);
	}
}

void ICACHE_FLASH_ATTR publishTemperatures(MQTT_Client* client, int idx) {
	struct Temperature *t;

	if (mqttConnected) {
		char *topic = (char *) os_zalloc(100);
		char *data = (char *) os_zalloc(100);
		if (topic == NULL || data == NULL) {
			startFlash(50, true); // fast
			return;
		}
		if (idx < 0) {
			for (idx = 0; idx < MAX_TEMPERATURE_SENSOR; idx++) {
				if (getUnmappedTemperature(idx, &t)) {
					os_sprintf(topic, (const char*) "/Raw/%s/%s/info", sysCfg.device_id,
							t->address);
					os_sprintf(data, (const char*) "{ \"Type\":\"Temp\", \"Value\":\"%c%d.%02d\"}",
							t->sign, t->val, t->fract);
					MQTT_Publish(client, topic, data, strlen(data), 0, 0);
					TESTP("%s==>%s\n", topic, data);
				}
			}
		} else {
			if (getUnmappedTemperature(idx, &t)) {
				os_sprintf(topic, (const char*) "/Raw/%s/%s/info", sysCfg.device_id, t->address);
				os_sprintf(data, (const char*) "{ \"Type\":\"Temp\", \"Value\":\"%c%d.%02d\"}",
						t->sign, t->val, t->fract);
				MQTT_Publish(client, topic, data, strlen(data), 0, 0);
				TESTP("%s==>%s\n", topic, data);
			}
		}
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR globalPublishTemperatures(void) {
	publishTemperatures(&mqttClient, -1);
}

void ICACHE_FLASH_ATTR publishSensorData(MQTT_Client* client) {
	if (mqttConnected) {
		char *topic = (char *) os_zalloc(100);
		char *data = (char *) os_zalloc(100);
		if (topic == NULL || data == NULL) {
			startFlash(50, true); // fast
			return;
		}

		os_sprintf(topic, (const char*) "/Raw/%s/2/info", sysCfg.device_id);
		os_sprintf(data, (const char*) "{ \"Type\":\"FlowMax\", \"Value\":%d}", flowMaxReading());
		MQTT_Publish(client, topic, data, strlen(data), 0, false);
		TESTP("%s=>%s\n", topic, data);

		os_sprintf(topic, (const char*) "/Raw/%s/4/info", sysCfg.device_id);
		os_sprintf(data, (const char*) "{ \"Type\":\"Flow\", \"Value\":%d}", flowPerReading());
		MQTT_Publish(client, topic, data, strlen(data), 0, false);
		TESTP("%s=>%s\n", topic, data);

		os_sprintf(topic, (const char*) "/Raw/%s/5/info", sysCfg.device_id);
		os_sprintf(data, (const char*) "{ \"Type\":\"Pressure\", \"Value\":%d}", pressure);
		MQTT_Publish(client, topic, data, strlen(data), 0, false);
		TESTP("%s=>%s\n", topic, data);

		resetFlowReadings();

		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR publishData(MQTT_Client* client) {
	if (mqttConnected) {
		publishTemperatures(client, -1); // All
		publishSensorData(client);
		publishOutput(OP_PUMP, outputState(OP_PUMP));
	}
}

void ICACHE_FLASH_ATTR publishDeviceInfo(MQTT_Client* client) {
	if (mqttConnected) {
		char *topic = (char *) os_zalloc(50);
		char *data = (char *) os_zalloc(300);
		int idx;
		struct ip_info ipConfig;

		if (topic == NULL || data == NULL) {
			startFlash(50, true); // fast
			return;
		}
		wifi_get_ip_info(STATION_IF, &ipConfig);

		os_sprintf(topic, "/Raw/%10s/info", sysCfg.device_id);
		os_sprintf(data,
				"{\"Name\":\"%s\", \"Location\":\"%s\", \"Version\":\"%s\", "
				"\"Updates\":%d, \"Inputs\":%d, \"RSSI\":%d, \"Channel\": %d, \"Attempts\": %d, ",
				sysCfg.deviceName, sysCfg.deviceLocation, version, sysCfg.updates,
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
		TESTP("%s=>%s\n", topic, data);
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
			"{\"Name\":\"%s\", \"Location\":\"%s\", \"Version\":\"%s\", \"Reason\":\"%d\", \"LastAction\":%d}",
			sysCfg.deviceName, sysCfg.deviceLocation, version, system_get_rst_info()->reason, lastAction);
		os_printf("%s=>%s\n", topic, data);
		MQTT_Publish(client, topic, data, strlen(data), 0, false);
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR smartConfig_done(sc_status status, void *pdata) {
	switch (status) {
	case SC_STATUS_WAIT:
		os_printf("SC_STATUS_WAIT\n");
		break;
	case SC_STATUS_FIND_CHANNEL:
		os_printf("SC_STATUS_FIND_CHANNEL\n");
		break;
	case SC_STATUS_GETTING_SSID_PSWD:
		os_printf("SC_STATUS_GETTING_SSID_PSWD\n");
		break;
	case SC_STATUS_LINK:
		os_printf("SC_STATUS_LINK\n");
		struct station_config *sta_conf = pdata;
		wifi_station_set_config(sta_conf);
		INFOP("Connected to %s (%s) %d", sta_conf->ssid, sta_conf->password, sta_conf->bssid_set);
		strcpy(sysCfg.sta_ssid, sta_conf->ssid);
		strcpy(sysCfg.sta_pwd, sta_conf->password);
		wifi_station_disconnect();
		wifi_station_connect();
		break;
	case SC_STATUS_LINK_OVER:
		os_printf("SC_STATUS_LINK_OVER\n");
		smartconfig_stop();
		checkSmartConfig(SC_HAS_STOPPED);
		break;
	}
}

bool ICACHE_FLASH_ATTR checkSmartConfig(enum SmartConfigAction action) {
	static doingSmartConfig = false;

	switch (action) {
	case SC_CHECK:
		break;
	case SC_HAS_STOPPED:
		os_printf("Finished smartConfig\n");
		stopFlash();
		doingSmartConfig = false;
		MQTT_Connect(&mqttClient);
		break;
	case SC_TOGGLE:
		if (doingSmartConfig) {
			os_printf("Stop smartConfig\n");
			stopFlash();
			smartconfig_stop();
			doingSmartConfig = false;
			MQTT_Connect(&mqttClient);
		} else {
			os_printf("Start smartConfig\n");
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

void ICACHE_FLASH_ATTR transmitCb(uint32_t *args) { // Depends on Update period
	MQTT_Client* client = (MQTT_Client*) args;
	if (!checkSmartConfig(SC_CHECK)) {
		publishData(client);
		checkMinHeap();
	}
}

void ICACHE_FLASH_ATTR switchAction(int action) {
	if (toggleState) {
		startFlashCount(100, false, action);
		switch (action) {
		case 1:
			break;
#if USE_PT100
			case 2:
			saveLowReading();
			break;
		case 3:
			saveHighReading();
			break;
#endif
		case 4:
			break;
		case 5:
			checkSmartConfig(SC_TOGGLE);
			break;
		}
	} else { // toggleState == false
		uint8 idx;
		TESTP("Action %d\n", action);
		switch (action) {
		case 1:
			publishDeviceInfo(&mqttClient);
			publishData(&mqttClient);
			for (idx=0; idx<MAX_OUTPUT; idx++)
				printOutput(idx);
			os_printf("\n");
			for (idx=0; idx<MAP_TEMP_SIZE; idx++)
				printMappedTemperature(idx);
			os_printf("minHeap: %d\n", checkMinHeap());
			break;
		case 2:
			stopPumpOverride();
			break;
		case 3:
			startPumpOverride();
			break;
		case 4:
			turnOffOverride();
			break;
		case 5:
			break;
		}
	}
}

void ICACHE_FLASH_ATTR switchTimerCb(uint32_t *args) {
	const swOnMax = 100;
	const swOffMax = 5;
	static int switchPulseCount;
	static enum {
		IDLE, ON, OFF
	} switchState = IDLE;

	if ((toggleState = easygpio_inputGet(TOGGLE))) {
		easygpio_outputSet(ACTION_LED, 1);
	} else {
		checkActionFlash();
	}
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

void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status) {
	os_printf("WiFi status: %d\r\n", status);
	if (status == STATION_GOT_IP) {
		MQTT_Connect(&mqttClient);
		wifiChannel = wifi_get_channel();
	} else {
		mqttConnected = false;
		MQTT_Disconnect(&mqttClient);
	}
}

void ICACHE_FLASH_ATTR dateTimerCb(void) {
	os_printf("Nothing heard so restarting...\n");
	lastAction = RESTART;
	system_restart();
}

static bool ICACHE_FLASH_ATTR jsoneq(const char *json, jsmntok_t *tok, const char *s) {
	if (tok->type == JSMN_STRING && (int) strlen(s) == tok->end - tok->start
			&& strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
		return true;
	}
	return false;
}

static void ICACHE_FLASH_ATTR decodeSensorClear(char *idPtr, char *param, MQTT_Client* client) {
	if (strcmp("temperature", param) == 0) {
		uint8 idx = clearTemperatureOverride(idPtr);
	} else if (strcmp("output", param) == 0) {
		overrideClearOutput(atoi(idPtr));
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
		TESTP("Failed to parse JSON: %d\n", r);
		return;
	}
	if (r < 1 || t[0].type != JSMN_OBJECT) {/* Assume the top-level element is an object */
		TESTP("Object expected\n");
		return;
	}
	TESTP("%d tokens\n", r);
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
		TESTP("\n");
	}
}

static void ICACHE_FLASH_ATTR decodeSensorSet(char *valPtr, char *idPtr, char *param,
		MQTT_Client* client) {
	int id = atoi(idPtr);
	int value = atoi(valPtr);
	TESTP("decodeSensorSet: %d-%d %s\n", id, value, param);
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
		publishTemperatures(&mqttClient, idx);
	} else if (strcmp("output", param) == 0) {
		if (0 <= id && id < MAX_OUTPUT) {
			overrideSetOutput(id, value);
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
	publishDeviceInfo(client);
	CFG_Save();
}

void ICACHE_FLASH_ATTR mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len,
		const char *data, uint32_t data_len) {
	char *topicBuf = (char*) os_zalloc(topic_len + 1), *dataBuf = (char*) os_zalloc(data_len + 1);
	char *tokens[10];

	MQTT_Client* client = (MQTT_Client*) args;
	lastAction = MQTT_DATA_CB;

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
					decodeDeviceSet(tokens[3], dataBuf, client);
				}
			} else if (tokenCount == 5 && strcmp(sysCfg.device_id, tokens[1]) == 0) {
				if (strcmp("set", tokens[3]) == 0) {
					decodeSensorSet(dataBuf, tokens[2], tokens[4], client);
				} else if (strcmp("clear", tokens[3]) == 0) {
					decodeSensorClear(tokens[2], tokens[4], client);
				}
			}
		} else if (strcmp("App", tokens[0]) == 0) {
			if (tokenCount == 2 && strcmp("date", tokens[1]) == 0) {
				os_timer_disarm(&date_timer); // Restart it
				os_timer_arm(&date_timer, 10 * 60 * 1000, false); //10 minutes
			}
			if (tokenCount >= 5 && strcmp("TS Bottom", tokens[3]) == 0) {
				saveTSbottom(dataBuf);
			}
		}
		checkMinHeap();
		os_free(topicBuf);
		os_free(dataBuf);
	}
}

void ICACHE_FLASH_ATTR readPressure(void) {
	SELECT_PRESSURE;
	pressure =  system_adc_read();
}

void ICACHE_FLASH_ATTR flowCheck_cb(uint32_t *args) {
	TESTP("*");
	if (flowAverageReading() <= 1) {
		switch (pumpState()) {
		case PUMP_OFF_NORMAL:
		case PUMP_OFF_OVERRIDE:
			break;
		case PUMP_ON_NORMAL:
			stopPumpOverride();
			publishAlarm(1, flowCurrentReading());
			break;
		case PUMP_ON_OVERRIDE:
			publishAlarm(2, flowCurrentReading());
			break;
		}
	}
}

void ICACHE_FLASH_ATTR turnOffOverride() {
	clearPumpOverride();
	checkActionFlash();
}

void ICACHE_FLASH_ATTR startPumpNormal(void) {
	switch (pumpState()) {
	case PUMP_OFF_NORMAL:
		TESTP("Start Pump\n");
		checkSetOutput(OP_PUMP, 1);
		break;
	case PUMP_ON_NORMAL:
		break;
	case PUMP_OFF_OVERRIDE:
	case PUMP_ON_OVERRIDE :
		return;
	}
	os_timer_disarm(&flowCheck_timer);
	os_timer_arm(&flowCheck_timer, PROCESS_REPEAT-500, 0);
}

void ICACHE_FLASH_ATTR startPumpOverride(void) {
	switch (pumpState()) {
	case PUMP_OFF_NORMAL:
	case PUMP_OFF_OVERRIDE:
	case PUMP_ON_NORMAL:
		TESTP("Start Pump with override\n");
		overrideSetOutput(OP_PUMP, 1);
		break;
	case PUMP_ON_OVERRIDE :
		return;
	}
	os_timer_disarm(&flowCheck_timer);
	os_timer_arm(&flowCheck_timer, 100000, 0); // Allow to run for 10S if override
}

void ICACHE_FLASH_ATTR stopPumpNormal(void) {
	switch (pumpState()) {
	case PUMP_OFF_NORMAL:
	case PUMP_OFF_OVERRIDE:
	case PUMP_ON_OVERRIDE :
		return;
	case PUMP_ON_NORMAL:
		TESTP("Stop Pump\n");
		checkSetOutput(OP_PUMP, 0);
		break;
	}
	os_timer_disarm(&flowCheck_timer);
}

void ICACHE_FLASH_ATTR stopPumpOverride(void) {
	switch (pumpState()) {
	case PUMP_OFF_NORMAL:
	case PUMP_ON_OVERRIDE :
	case PUMP_ON_NORMAL:
		TESTP("Stop Pump with override\n");
		overrideSetOutput(OP_PUMP, 1);
		break;
	case PUMP_OFF_OVERRIDE:
		return;
	}
	os_timer_disarm(&flowCheck_timer);
	overrideSetOutput(OP_PUMP, 0);
}

void ICACHE_FLASH_ATTR processPump(void) {
	static pumpDelayOn = 0;
	static pumpDelayOff = 0;

	TESTP("Flow = %d (Av=%d, Mx=%d, Mn=%d)\n", flowCurrentReading(), flowAverageReading(), flowMaxReading(), flowMinReading());
	if (mappedTemperature(MAP_TEMP_PANEL) > (mappedTemperature(MAP_TEMP_TS_BOTTOM) + sysCfg.settings[SET_PANEL_TEMP])) {
		pumpDelayOff = 0;
		if (pumpDelayOn < sysCfg.settings[SET_PUMP_DELAY]) {
			pumpDelayOn++;
		} else {
			startPumpNormal();
		}
	} else {
		pumpDelayOn = 0;
		if (pumpDelayOff < sysCfg.settings[SET_PUMP_DELAY]) {
			pumpDelayOff++;
		} else {
			stopPumpNormal();
		}
	}
	checkActionFlash();
}

void ICACHE_FLASH_ATTR processTimerCb(void) { // 5 sec
	lastAction = PROCESS_CB;
	startReadTemperatures();
	readPressure();
	processPump();

}

void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t *args) {
	char *topic = (char*) os_zalloc(100);

	MQTT_Client* client = (MQTT_Client*) args;
	lastAction = MQTT_CONNECTED_CB;
	mqttConnected = true;
	os_printf("MQTT: Connected to %s:%d\n", sysCfg.mqtt_host, sysCfg.mqtt_port);

	os_sprintf(topic, "/Raw/%s/set/#", sysCfg.device_id);
	TESTP("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	os_sprintf(topic, "/Raw/%s/+/set/#", sysCfg.device_id);
	TESTP("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	os_sprintf(topic, "/App/+/+/TS Bottom/#");
	TESTP("Subscribe to: %s\n", topic);
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
	easygpio_outputSet(LED, 0); // Turn LED off when connected
	initFlowMonitor();
	initTemperatureMonitor();

	checkMinHeap();
	os_free(topic);
}

void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args) {
//	MQTT_Client* client = (MQTT_Client*)args;
	os_printf("MQTT Disconnected\n");
	lastAction = MQTT_DISCONNECTED_CB;
	mqttConnected = false;
	if (!checkSmartConfig(SC_CHECK)) {
		MQTT_Connect(&mqttClient);
	}
}

static size_t ICACHE_FLASH_ATTR fs_size() { // returns the flash chip's size, in BYTES
	uint32_t id = spi_flash_get_id();
	uint8_t mfgr_id = id & 0xff;
	uint8_t type_id = (id >> 8) & 0xff; // not relevant for size calculation
	uint8_t size_id = (id >> 16) & 0xff; // lucky for us, WinBond ID's their chips as a form that lets us calculate the size
	if (mfgr_id != 0xEF) // 0xEF is WinBond; that's all we care about (for now)
		return 0;
	return 1 << size_id;
}

static void ICACHE_FLASH_ATTR showSysInfo() {
	os_printf("SDK version is: %s\n", system_get_sdk_version());
	os_printf("Smart-Config version is: %s\n", smartconfig_get_version());
	system_print_meminfo();
	int sz = fs_size();
	os_printf("Flashsize map %d; id %lx (%lx - %d bytes)\n", system_get_flash_size_map(),
			spi_flash_get_id(), sz, sz / 8);
	os_printf("Boot mode: %d, version %d. Userbin %lx\n", system_get_boot_mode(),
			system_get_boot_version(), system_get_userbin_addr());
}

static void ICACHE_FLASH_ATTR startUp() {
	CFG_Load();
	INFO(CFG_print());
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

	WIFI_Connect(bestSSID, sysCfg.sta_pwd, sysCfg.deviceName, wifiConnectCb);

	os_timer_disarm(&flash_timer);
	os_timer_setfn(&flash_timer, (os_timer_func_t *) flash_cb, (void *) 0);

	os_timer_disarm(&flashA_timer);
	os_timer_setfn(&flashA_timer, (os_timer_func_t *) flashAction_cb, (void *) 0);

	os_timer_disarm(&flowCheck_timer);
	os_timer_setfn(&flowCheck_timer, (os_timer_func_t *) flowCheck_cb, (void *) 0);

	os_timer_disarm(&process_timer);
	os_timer_setfn(&process_timer, (os_timer_func_t *) processTimerCb, NULL);
	os_timer_arm(&process_timer, PROCESS_REPEAT, true);

	lastAction = INIT_DONE;
	INFO(showSysInfo());
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

void ICACHE_FLASH_ATTR wifi_handle_event_cb(System_Event_t *evt) {
	TESTP("WiFi event %x\n", evt->event);
}

void ICACHE_FLASH_ATTR user_init(void) {
	stdout_init();
	gpio_init();
	initIO();
	wifi_station_set_auto_connect(false);
	wifi_station_set_reconnect_policy(true);
	system_init_done_cb(&initDone_cb);
}
