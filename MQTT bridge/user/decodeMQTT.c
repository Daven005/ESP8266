/*
 * decodeMQTT.c
 *
 *  Created on: 7 Aug 2016
 *      Author: User
 */

#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "debug.h"
#include "flash.h"
#include "jsmn.h"
#include "mqtt.h"
#include "decodeMQTT.h"


static MQTT_Client mqttClient;

static void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t *args) {
	myMqttState.isConnected = true;
	okMessage("MQTT_Connected");
	startFlash(-1, 900, 100);
}

static void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args) {
	myMqttState.isConnected = false;
	okMessage("MQTT_Disconnected");
	startFlash(-1, 200, 1000);
}

static void ICACHE_FLASH_ATTR mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len,
		const char *data, uint32_t data_len) {
	char *topicBuf = (char*) os_zalloc(topic_len + 1), *dataBuf = (char*) os_zalloc(data_len + 1);

	os_memcpy(topicBuf, topic, topic_len);
	os_memcpy(dataBuf, data, data_len);
	os_printf(
			"{\"resp\":\"OK\", \"cmd\":\"MQTT_Data\", \"params\":{\"topic\":\"%s\", \"data\":\"%s\"}}\n",
			topicBuf, dataBuf);
	checkMinHeap();
	os_free(topicBuf);
	os_free(dataBuf);
}

void ICACHE_FLASH_ATTR initClient(char *cmd, char *bfr, jsmntok_t root[], int start, int max) {
	char *clientID;
	int idx;
	for (idx = start; idx < max; idx++) {
		if (jsoneq(bfr, &root[idx], "clientID")) {
			clientID = &bfr[root[idx + 1].start];
			bfr[root[idx + 1].end] = 0; // Overwrites trailing quote
			idx++;
		}
	}
	if (myMqttState.isInit) {
		if (!myMqttState.isClient) {
			MQTT_InitClient(&mqttClient, clientID, "", "", 120, 1);
			MQTT_OnConnected(&mqttClient, mqttConnectedCb);
			MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
			MQTT_OnData(&mqttClient, mqttDataCb);
			startFlash(-1, 500, 500);
			okMessage(cmd);
			myMqttState.isClient = true;
		} else {
			errorMessage(cmd, "Done already");
		}
	} else {
		startFlash(-1, 1000, 1000);
		errorMessage(cmd, "Client not set");
	}
}

void ICACHE_FLASH_ATTR initConnection(char *cmd, char *bfr, jsmntok_t root[], int start,
		int max) {
	char *host;
	uint16 port;
	int idx;
	for (idx = start; idx < max; idx++) {
		if (jsoneq(bfr, &root[idx], "host")) {
			host = &bfr[root[idx + 1].start];
			bfr[root[idx + 1].end] = 0; // Overwrites trailing quote
			idx++;
		}
		if (jsoneq(bfr, &root[idx], "port")) {
			port = atol(&bfr[root[idx + 1].start]);
			idx++;
		}
	}
//	TESTP("initConnection host: %s, port %d\n", host, port);
	if (!myMqttState.isInit) {
		MQTT_InitConnection(&mqttClient, host, port, 0); // Sets up mqttClient
		startFlash(-1, 200, 1000);
		okMessage(cmd);
		myMqttState.isInit = true;
	} else {
		errorMessage(cmd, "Done already");
	}
}

void ICACHE_FLASH_ATTR initLWT(char *cmd, char *bfr, jsmntok_t root[], int start, int max) {
	char *topic, *msg;
	int idx, qos, retain;
	for (idx = start; idx < max; idx++) {
		if (jsoneq(bfr, &root[idx], "topic")) {
			topic = &bfr[root[idx + 1].start];
			bfr[root[idx + 1].end] = 0; // Overwrites trailing quote
			idx++;
		}
		if (jsoneq(bfr, &root[idx], "msg")) {
			msg = &bfr[root[idx + 1].start];
			bfr[root[idx + 1].end] = 0; // Overwrites trailing quote
			idx++;
		}
	}
	if (myMqttState.isClient) {
		if (!myMqttState.isLWT) {
			MQTT_InitLWT(&mqttClient, topic, msg, 0, 0);
			startFlash(4, 100, 1000);
			okMessage(cmd);
			myMqttState.isLWT = true;
		} else {
			errorMessage(cmd, "Done already");
		}
	} else {
		startFlash(-1, 1000, 1000);
		errorMessage(cmd, "No client");
	}
}

void ICACHE_FLASH_ATTR mqttConnect(char *cmd) {
	if (myMqttState.isClient) {
		MQTT_Connect(&mqttClient);
		okMessage(cmd);
	} else {
		errorMessage(cmd, "No client");
	}
}

void ICACHE_FLASH_ATTR mqttDisconnect(char *cmd) {
	if (myMqttState.isClient) {
		MQTT_Disconnect(&mqttClient);
		okMessage(cmd);
	} else {
		errorMessage(cmd, "No client");
	}
}

void ICACHE_FLASH_ATTR subscribe(char *cmd, char *bfr, jsmntok_t root[], int start, int max) {
	char *topic;
	uint16 qos;
	int idx;
	for (idx = start; idx < max; idx++) {
		if (jsoneq(bfr, &root[idx], "topic")) {
			topic = &bfr[root[idx + 1].start];
			bfr[root[idx + 1].end] = 0; // Overwrites trailing quote
			idx++;
		}
		if (jsoneq(bfr, &root[idx], "qos")) {
			qos = atol(&bfr[root[idx + 1].start]);
			idx++;
		}
	}
	INFOP("subscribe topic: %s, qos %d\n", topic, qos);
	if (myMqttState.isConnected) {
		if (MQTT_Subscribe(&mqttClient, topic, qos)) {
			startFlash(5, 200, 800);
			okMessage(cmd);
		} else {
			startFlash(-1, 200, 200);
			errorMessage(cmd, "Failed");
		}
	} else {
		errorMessage(cmd, "Not connected");
	}
}

void ICACHE_FLASH_ATTR unsubscribe(char *cmd, char *bfr, jsmntok_t root[], int start,
		int max) {
	char *topic;
	int idx;
	for (idx = start; idx < max; idx++) {
		if (jsoneq(bfr, &root[idx], "topic")) {
			topic = &bfr[root[idx + 1].start];
			bfr[root[idx + 1].end] = 0; // Overwrites trailing quote
			idx++;
		}
	}
	INFOP("subscribe topic: %s, qos %d\n", topic);
	if (myMqttState.isConnected) {
		if (MQTT_UnSubscribe(&mqttClient, topic)) {
			startFlash(6, 200, 800);
			okMessage(cmd);
		} else {
			startFlash(-1, 200, 200);
			errorMessage(cmd, "Failed");
		}
	} else {
		errorMessage(cmd, "Not connected");
	}
}

void ICACHE_FLASH_ATTR publish(char *cmd, char *bfr, jsmntok_t root[], int start, int max) {
	char *topic, *data;
	int idx, qos, retain;
	for (idx = start; idx < max; idx++) {
		if (jsoneq(bfr, &root[idx], "topic")) {
			topic = &bfr[root[idx + 1].start];
			bfr[root[idx + 1].end] = 0; // Overwrites trailing quote
			idx++;
		}
		if (jsoneq(bfr, &root[idx], "data")) {
			data = &bfr[root[idx + 1].start];
			bfr[root[idx + 1].end] = 0; // Overwrites trailing quote
			idx++;
		}
		if (jsoneq(bfr, &root[idx], "qos")) {
			qos = atol(&bfr[root[idx + 1].start]);
			idx++;
		}
		if (jsoneq(bfr, &root[idx], "retain")) {
			retain = bfr[root[idx + 1].start] == 't';
			idx++;
		}
	}
	if (myMqttState.isConnected) {
		if (MQTT_Publish(&mqttClient, topic, data, os_strlen(data), qos, 0)) {
			startFlash(7, 200, 800);
			okMessage(cmd);
		} else {
			errorMessage(cmd, "Failed");
		}
	} else {
		errorMessage(cmd, "Not connected");
	}
}

void ICACHE_FLASH_ATTR mqttStatus(char *cmd) {
	os_printf("{\"resp\":\"OK\",\"cmd\":\"%s\",\"params\":"
			"{\"WiFi\":%d,\"initl\":%d,\"client\":%d,\"lwt\":%d,\"conctd\":%d,\"heap\":%d}}\n", cmd,
			myMqttState.isWiFi, myMqttState.isInit, myMqttState.isClient, myMqttState.isLWT,
			myMqttState.isConnected, checkMinHeap());
	startFlash(1, 200, 800);
}

