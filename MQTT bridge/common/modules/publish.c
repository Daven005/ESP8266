/*
 * publish.c
 *
 *  Created on: 11 Jul 2016
 *      Author: User
 */

#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "wifi.h"
#include "user_config.h"
#include "debug.h"
#include "flash.h"
#include "wifi.h"
#include "mqtt.h"
#include "temperature.h"
#include "publish.h"

#include "../user/include/config_.h"
#include "check.h"

static MQTT_Client *mqttClient;

void ICACHE_FLASH_ATTR publishAllTemperatures(void) {
	struct Temperature *t;
	int idx;

	if (mqttIsConnected()) {
		char *topic = (char*) os_malloc(100), *data = (char*) os_malloc(100);
		if (topic == NULL || data == NULL) {
			ERRORP("malloc err %s/%s\n", topic, data);
			startFlash(-1, 50, 50); // fast
			return;
		}
		for (idx = 0; idx < MAX_TEMPERATURE_SENSOR; idx++) {
			if (getUnmappedTemperature(idx, &t)) {
				os_sprintf(topic, (const char*) "/Raw/%s/%s/info", sysCfg.device_id, t->address);
				os_sprintf(data, (const char*) "{ \"Type\":\"Temp\", \"Value\":\"%c%d.%02d\"}",
						t->sign, t->val, t->fract);
				MQTT_Publish(mqttClient, topic, data, os_strlen(data), 0, 0);
				INFOP("%s=>%s\n", topic, data);
			}
		}
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR publishTemperature(int idx) {
	struct Temperature *t;

	if (mqttIsConnected()) {
		char *topic = (char*) os_malloc(100), *data = (char*) os_malloc(100);
		if (topic == NULL || data == NULL) {
			ERRORP("malloc err %s/%s\n", topic, data);
			startFlash(-1, 50, 50); // fast
			return;
		}
		if (getUnmappedTemperature(idx, &t)) {
			os_sprintf(topic, (const char*) "/Raw/%s/%s/info", sysCfg.device_id, t->address);
			os_sprintf(data, (const char*) "{ \"Type\":\"Temp\", \"Value\":\"%c%d.%02d\"}", t->sign,
					t->val, t->fract);
			MQTT_Publish(mqttClient, topic, data, os_strlen(data), 0, 0);
			INFOP("%s=>%s\n", topic, data);
		}
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
	if (topic == NULL || data == NULL) {
		ERRORP("malloc err %s/%s\n", topic, data);
		startFlash(-1, 50, 50); // fast
		return;
	}
	os_sprintf(topic, (const char*) "/Raw/%s/error", sysCfg.device_id);
	os_sprintf(data, (const char*) "{ \"error\":%d, \"info\":%d}", err, info);
	MQTT_Publish(mqttClient, topic, data, os_strlen(data), 0, 0);
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
	char *topic = (char*) os_malloc(50);
	char *data = (char*) os_malloc(100);
	if (topic == NULL || data == NULL) {
		ERRORP("malloc err %s/%s\n", topic, data);
		startFlash(-1, 50, 50); // fast
		return;
	}
	os_sprintf(topic, (const char*) "/Raw/%s/alarm", sysCfg.device_id);
	os_sprintf(data, (const char*) "{ \"alarm\":%d, \"info\":%d}", alarm, info);
	MQTT_Publish(mqttClient, topic, data, os_strlen(data), 0, 0);
	TESTP("********%s=>%s\n", topic, data);
	startFlash(-1, 200, 200);
	checkMinHeap();
	os_free(topic);
	os_free(data);
}

void ICACHE_FLASH_ATTR publishDeviceReset(char *version, int lastAction) {
	if (mqttIsConnected()) {
		char *topic = (char *) os_zalloc(50);
		char *data = (char *) os_zalloc(200);
		int idx;
		if (topic == NULL || data == NULL) {
			ERRORP("malloc err %s/%s\n", topic, data);
			startFlash(-1, 50, 50); // fast
			return;
		}

		os_sprintf(topic, "/Raw/%10s/reset", sysCfg.device_id);
		os_sprintf(data,
			"{\"Name\":\"%s\", \"Location\":\"%s\", \"Version\":\"%s\", \"Reason\":%d, \"LastAction\":%d}",
			sysCfg.deviceName, sysCfg.deviceLocation, version, system_get_rst_info()->reason, lastAction);
		MQTT_Publish(mqttClient, topic, data, os_strlen(data), 0, false);
		INFOP("%s=>%s\n", topic, data);
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR publishDeviceInfo(char *version, char *mode,
		uint8 wifiChannel, uint16 wifiAttempts, char *bestSSID, uint16 vcc) {
	if (mqttIsConnected()) {
		char *topic = (char *) os_zalloc(50);
		char *data = (char *) os_zalloc(500);
		int idx;
		struct ip_info ipConfig;
		if (topic == NULL || data == NULL) {
			ERRORP("malloc err %s/%s\n", topic, data);
			startFlash(-1, 50, 50); // fast
			return;
		}
		wifi_get_ip_info(STATION_IF, &ipConfig);

		os_sprintf(topic, "/Raw/%10s/info", sysCfg.device_id);
		os_sprintf(data,
				"{\"Name\":\"%s\", \"Location\":\"%s\", \"Version\":\"%s(%s)\", "
					"\"Updates\":%d, \"Inputs\":%d, \"Outputs\":%d, "
					"\"RSSI\":%d, \"Channel\": %d, \"Attempts\": %d, \"Vcc\": %d, ",
				sysCfg.deviceName, sysCfg.deviceLocation, version, mode,
				sysCfg.updates,
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
				wifi_station_get_rssi(), wifiChannel, WIFI_Attempts(), vcc);
		os_sprintf(data + os_strlen(data), "\"IPaddress\":\"%d.%d.%d.%d\"", IP2STR(&ipConfig.ip.addr));
		os_sprintf(data + os_strlen(data), ", \"AP\":\"%s\"", bestSSID);
		os_sprintf(data + os_strlen(data), ", \"Settings\":[");
		for (idx = 0; idx < SETTINGS_SIZE; idx++) {
			if (idx != 0)
				os_sprintf(data + os_strlen(data), ", ");
			os_sprintf(data + os_strlen(data), "%d", sysCfg.settings[idx]);
		}
		os_sprintf(data + os_strlen(data), "]}");
		MQTT_Publish(mqttClient, topic, data, os_strlen(data), 0, true);
		INFOP("%s=>%s\n", topic, data);
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR publishMapping(void) {
	if (mqttIsConnected()) {
		char *topic = (char *) os_zalloc(50);
		char *data = (char *) os_zalloc(500);
		int idx;

		if (topic == NULL || data == NULL) {
			ERRORP("malloc err %s/%s\n", topic, data);
			startFlash(-1, 50, 50); // fast
			return;
		}
		os_sprintf(topic, "/Raw/%10s/mapping", sysCfg.device_id);
		os_sprintf(data, "[");
		for (idx=0; idx<MAP_TEMP_SIZE; idx++) {
			if (idx != 0)
				os_sprintf(data + os_strlen(data), ", ");
			os_sprintf(data + os_strlen(data), "{\"map\":%d,\"name\":\"%s\", \"sensorID\": \"%s\"}",
					sysCfg.mapping[idx], sysCfg.mappingName[idx], unmappedSensorID(idx));
		}
		os_sprintf(data + os_strlen(data), "]");
		MQTT_Publish(mqttClient, topic, data, os_strlen(data), 0, true);
		INFOP("%s=>%s\n", topic, data);
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

#ifdef INPUTS
void ICACHE_FLASH_ATTR publishInput(uint8 idx, uint8 val) {
	if (mqttIsConnected()) {
		char *topic = (char*) os_malloc(50), *data = (char*) os_malloc(100);
		if (topic == NULL || data == NULL) {
			ERRORP("malloc err %s/%s\n", topic, data);
			startFlash(-1, 50, 50); // fast
			return;
		}
		os_sprintf(topic, (const char*) "/Raw/%s/%d/info", sysCfg.device_id,
				idx + INPUT_SENSOR_ID_START);
		os_sprintf(data,
				(const char*) "{\"Name\":\"IP%d\", \"Type\":\"Input\", \"Value\":\"%d\"}", idx,
				val);
		INFOP("%s-->%s", topic, data);
		MQTT_Publish(mqttClient, topic, data, os_strlen(data), 0, 0);
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}
#endif

#ifdef OUTPUTS
void ICACHE_FLASH_ATTR publishOutput(uint8 idx, uint8 val) {
	if (mqttIsConnected()) {
		char *topic = (char*) os_malloc(100), *data = (char*) os_malloc(200);
		if (topic == NULL || data == NULL) {
			ERRORP("malloc err %s/%s\n", topic, data);
			startFlash(-1, 20, 20); // fast
			return;
		}
		os_sprintf(topic, (const char*) "/Raw/%s/%d/info", sysCfg.device_id,
				idx + OUTPUT_SENSOR_ID_START);
		os_sprintf(data,
				(const char*) "{\"Name\":\"OP%d\", \"Type\":\"Output\", \"Value\":\"%d\"}", idx,
				val);
		INFOP("%s-->%s", topic, data);
		if (!MQTT_Publish(mqttClient, topic, data, os_strlen(data), 0, 0))
			ERRORP("Error with %s--->%s\n", topic, data);
		checkMinHeap();
		os_free(topic);
		os_free(data);
	} else {
		INFOP("o/p %d--->%d\n", idx, val);
	}
}
#endif

void ICACHE_FLASH_ATTR initPublish(MQTT_Client* client) {
	mqttClient = client;
}
