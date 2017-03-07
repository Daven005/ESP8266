/*
 * publish.c
 *
 *  Created on: 11 Jul 2016
 *      Author: User
 */
//#define DEBUG_OVERRIDE
#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "wifi.h"
#include "debug.h"
#include "check.h"
#include "flash.h"
#include "wifi.h"
#include "mqtt.h"
#include "temperature.h"
#include "publish.h"

#include "user_conf.h"
#include "sysCfg.h"
#include "check.h"

static MQTT_Client *mqttClient;

static bool checkClient(char *s) {
	if (mqttIsConnected() && mqttClient != NULL)
		return true;
	ERRORP("No client for %s (%lx)\n", s, mqttClient);
	return false;
}

static bool ICACHE_FLASH_ATTR checkAlloc(void *topic, void *data) {
	if (topic == NULL || data == NULL) {
		ERRORP("malloc err %s/%s\n", topic, data);
		startFlash(-1, 50, 50); // fast
		if (topic) os_free(topic);
		return false;
	}
	return true;
}

static void ICACHE_FLASH_ATTR printMQTTstate(void) {
	ERRORP("State: MQTT-%x, TCP-%x\n", mqttClient->mqtt_state, mqttClient->connState);
	startFlash(-1, 1000, 1000);
}

#ifdef READ_TEMPERATURES
void ICACHE_FLASH_ATTR publishAllTemperatures(void) {
	struct Temperature *t;
	int idx;

	if (checkClient("publishAllTemperatures")) {
		char *topic = (char*) os_malloc(100), *data = (char*) os_malloc(100);
		if (!checkAlloc(topic, data)) return;

		for (idx = 0; idx < MAX_TEMPERATURE_SENSOR; idx++) {
			if (getUnmappedTemperature(idx, &t)) {
				os_sprintf(topic, (const char*) "/Raw/%s/%s/info", sysCfg.device_id, t->address);
				os_sprintf(data, (const char*) "{ \"Type\":\"Temp\", \"Value\":\"%c%d.%02d\"}",
						t->sign, t->val, t->fract);
				if (!MQTT_Publish(mqttClient, topic, data, os_strlen(data), 0, 0))
					printMQTTstate();
				TESTP("%s=>%s\n", topic, data);
			}
		}
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR publishTemperature(int idx) {
	struct Temperature *t;

	if (checkClient("publishTemperature")) {
		char *topic = (char*) os_malloc(100), *data = (char*) os_malloc(100);
		if (!checkAlloc(topic, data)) return;

		if (getUnmappedTemperature(idx, &t)) {
			os_sprintf(topic, (const char*) "/Raw/%s/%s/info", sysCfg.device_id, t->address);
			os_sprintf(data, (const char*) "{ \"Type\":\"Temp\", \"Value\":\"%c%d.%02d\"}", t->sign,
					t->val, t->fract);
			if (!MQTT_Publish(mqttClient, topic, data, os_strlen(data), 0, 0))
				printMQTTstate();
			INFOP("%s=>%s\n", topic, data);
		}
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}
#endif

void ICACHE_FLASH_ATTR publishAnalogue(uint16 val) {

	if (checkClient("publishAnalogue")) {
		char *topic = (char*) os_malloc(100), *data = (char*) os_malloc(100);
		if (!checkAlloc(topic, data)) return;

		os_sprintf(topic, (const char*) "/Raw/%s/A1/info", sysCfg.device_id);
		os_sprintf(data, (const char*) "{ \"Type\":\"Level\", \"Value\":%d}", val);
		if (!MQTT_Publish(mqttClient, topic, data, os_strlen(data), 0, 0))
			printMQTTstate();
		TESTP("%s=>%s\n", topic, data);

		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR publishError(uint8 err, int info) {
	static uint8 last_err = 0xff;
	static int last_info = -1;
	if (err == last_err && info == last_info)
		return; // Ignore repeated errors
	char *topic = (char*) os_malloc(50), *data = (char*) os_malloc(100);
	if (!checkAlloc(topic, data)) return;

	os_sprintf(topic, (const char*) "/Raw/%s/error", sysCfg.device_id);
	os_sprintf(data, (const char*) "{ \"error\":%d, \"info\":%d}", err, info);
	if (checkClient("publishError")) {
		if (!MQTT_Publish(mqttClient, topic, data, os_strlen(data), 0, 0))
			printMQTTstate();
		TESTP("********");
	} else {
		TESTP("--------");
	}
	TESTP("%s=>%s\n", topic, data);
	checkMinHeap();
	os_free(topic);
	os_free(data);
}

void ICACHE_FLASH_ATTR publishAlarm(uint8 alarm, int info) {
	static uint8 last_alarm = 0xff;
	static int last_info = -1;
	if (alarm == last_alarm && info == last_info)
		return; // Ignore repeated identical alarms
	last_alarm = alarm;
	last_info = info;
	char *topic = (char*) os_zalloc(100);
	char *data = (char*) os_malloc(100);
	if (!checkAlloc(topic, data)) return;

	os_sprintf(topic, (const char*) "/Raw/%s/alarm", sysCfg.device_id);
	os_sprintf(data, (const char*) "{ \"alarm\":%d, \"info\":%d}", alarm, info);
	if (checkClient("publishAlarm")) {
		if (!MQTT_Publish(mqttClient, topic, data, os_strlen(data), 0, 0))
			printMQTTstate();
		TESTP("********");
	} else {
		TESTP("--------");
	}
	TESTP("%s=>%s\n", topic, data);
	checkMinHeap();
	os_free(topic);
	os_free(data);
}

void ICACHE_FLASH_ATTR publishDeviceReset(char *version, int lastAction) {
	if (checkClient("publishDeviceReset")) {
		int idx;
		char *topic = (char *) os_zalloc(100);
		char *data = (char *) os_zalloc(200);
		if (!checkAlloc(topic, data)) return;

		os_sprintf(topic, "/Raw/%10s/reset", sysCfg.device_id);
		os_sprintf(data,
				"{\"Name\":\"%s\", \"Location\":\"%s\", \"Version\":\"%s\", \"Reason\":%d, \"LastAction\":%d}",
				sysCfg.deviceName, sysCfg.deviceLocation, version, system_get_rst_info()->reason,
				lastAction);
		if (!MQTT_Publish(mqttClient, topic, data, os_strlen(data), 0, false))
			printMQTTstate();
		TESTP("%s=>%s\n", topic, data);
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR publishDeviceInfo(char *version, char *mode, uint8 wifiChannel,
		uint16 wifiConnectTime, char *bestSSID, uint16 vcc) {
	if (checkClient("publishDeviceInfo")) {
		int idx;
		struct ip_info ipConfig;
		char *topic = (char *) os_zalloc(100);
		char *data = (char *) os_zalloc(500);
		if (!checkAlloc(topic, data)) return;

		wifi_get_ip_info(STATION_IF, &ipConfig);

		os_sprintf(topic, "/Raw/%10s/info", sysCfg.device_id);
		os_sprintf(data, "{\"Name\":\"%s\", \"Location\":\"%s\", \"Version\":\"%s(%s)\", "
				"\"Updates\":%d, \"Inputs\":%d, \"Outputs\":%d, "
				"\"RSSI\":%d, \"Channel\": %d, \"ConnectTime\": %d, \"Vcc\": %d, ",
				sysCfg.deviceName, sysCfg.deviceLocation, version, mode, sysCfg.updates,
#ifdef INPUTS
				sysCfg.inputs,
#else
				0,
#endif
#ifdef OUTPUTS
				sysCfg.outputs,
#else
				0,
#endif
				wifi_station_get_rssi(), wifiChannel, wifiConnectTime, vcc);
		os_sprintf(data + os_strlen(data), "\"IPaddress\":\"%d.%d.%d.%d\"",
				IP2STR(&ipConfig.ip.addr));
		os_sprintf(data + os_strlen(data), ", \"AP\":\"%s\"", bestSSID);
		os_sprintf(data + os_strlen(data), ", \"Settings\":[");
		for (idx = 0; idx < SETTINGS_SIZE; idx++) {
			if (idx != 0)
				os_sprintf(data + os_strlen(data), ", ");
			os_sprintf(data + os_strlen(data), "%d", sysCfg.settings[idx]);
		}
		os_sprintf(data + os_strlen(data), "]}");
		if (!MQTT_Publish(mqttClient, topic, data, os_strlen(data), 0, true))
			printMQTTstate();
		INFOP("%s=>%s\n", topic, data);
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

#ifdef READ_TEMPERATURES
void ICACHE_FLASH_ATTR publishMapping(void) {
#define MSG_SIZE 1200
	if (checkClient("publishMapping")) {
		int idx;
		char *topic = (char *) os_zalloc(50);
		char *data = (char *) os_zalloc(MSG_SIZE);
		if (!checkAlloc(topic, data)) return;

		os_sprintf(topic, "/Raw/%10s/mapping", sysCfg.device_id);
		os_sprintf(data, "[");
		for (idx = 0; idx <= MAP_TEMP_LAST; idx++) {
			if (os_strlen(unmappedSensorID(idx)) == 0) {
				struct Temperature *t;
				getUnmappedTemperature(idx, &t);
				INFO(dump((void *)t, sizeof(*t)););
				ERRORP("Missing temperature %d\n", idx); // NB Outside temperature may not yet be received
			} else {
				if (os_strlen(data) > (MSG_SIZE - 80)) {
					ERRORP("No space for mapping %d: %d\n", idx, os_strlen(data));
				} else {
					if (idx != 0)
						os_sprintf(data + os_strlen(data), ", ");
					os_sprintf(data + os_strlen(data),
							"{\"map\":%d,\"name\":\"%s\", \"sensorID\": \"%s\"}",
							sysCfg.mapping[idx], sysCfg.mappingName[idx],
							unmappedSensorID(sysCfg.mapping[idx]));
				}
			}
		}
		os_sprintf(data + os_strlen(data), "]");
		if (!MQTT_Publish(mqttClient, topic, data, os_strlen(data), 0, true))
			printMQTTstate();
		INFOP("%s=>%s\n", topic, data);
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}
#endif

#ifdef INPUTS
void ICACHE_FLASH_ATTR publishInput(uint8 idx, uint8 val) {
	if (checkClient("publishInput")) {
		char *topic = (char*) os_zalloc(100), *data = (char*) os_malloc(100);
		if (!checkAlloc(topic, data)) return;

		os_sprintf(topic, (const char*) "/Raw/%s/%d/info", sysCfg.device_id,
				idx + INPUT_SENSOR_ID_START);
		os_sprintf(data, (const char*) "{\"Name\":\"IP%d\", \"Type\":\"Input\", \"Value\":\"%d\"}",
				idx, val);
		INFOP("%s-->%s", topic, data);
		if (!MQTT_Publish(mqttClient, topic, data, os_strlen(data), 0, 0))
			printMQTTstate();
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}
#endif

#ifdef OUTPUTS
void ICACHE_FLASH_ATTR publishOutput(uint8 idx, uint8 val) {
	if (checkClient("publishOutput")) {
		char *topic = (char*) os_malloc(100), *data = (char*) os_malloc(200);
		if (!checkAlloc(topic, data)) return;

		os_sprintf(topic, (const char*) "/Raw/%s/%d/info", sysCfg.device_id,
				idx + OUTPUT_SENSOR_ID_START);
		os_sprintf(data, (const char*) "{\"Name\":\"OP%d\", \"Type\":\"Output\", \"Value\":\"%d\"}",
				idx, val);
		INFOP("%s-->%s", topic, data);
		if (!MQTT_Publish(mqttClient, topic, data, os_strlen(data), 0, 0))
			printMQTTstate();
		checkMinHeap();
		os_free(topic);
		os_free(data);
	} else {
		INFOP("o/p %d--->%d\n", idx, val);
	}
}
#endif

void ICACHE_FLASH_ATTR initPublish(MQTT_Client* client) {
	INFOP("initPublish\n");
	mqttClient = client;
}
