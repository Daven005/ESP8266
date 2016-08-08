/*
 * decodeCommand.c
 *
 *  Created on: 2 Aug 2016
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
#include "wifi.h"
#include "decodeCommand.h"
#include "decodeMQTT.h"

const char *commands[] = {
		"Version", "WiFi_SetParams", "WiFi_GetParams", "WiFi_Scan", "WiFi_Connect", "WiFi_Status",
		"MQTT_Status", "MQTT_InitConnection", "MQTT_InitClient", "MQTT_InitLWT", "MQTT_Connect",
		"MQTT_Disconnect", "MQTT_Subscribe", "MQTT_Unsubscribe", "MQTT_Publish", NULL };
enum commandIDs {
	eVersion,
	eWiFi_SetParams,
	eWiFi_GetParams,
	eWiFi_Scan,
	eWiFi_Connect,
	eWiFi_Status,
	eMQTT_Status,
	eMQTT_InitConnection,
	eMQTT_InitClient,
	eMQTT_InitLWT,
	eMQTT_Connect,
	eMQTT_Disconnect,
	eMQTT_Subscribe,
	eMQTT_Unsubscribe,
	eMQTT_Publish
};

myMqttState_t myMqttState = { false, false, false, false, false };

static int ICACHE_FLASH_ATTR getCmdId(char *cmd) {
	int idx = 0;
	while (commands[idx] != NULL) {
		if (os_strcmp(commands[idx], cmd) == 0)
			return idx;
		idx++;
	}
	return -1;
}

void ICACHE_FLASH_ATTR okMessage(char *cmd) {
	os_printf("{\"resp\":\"OK\", \"cmd\":\"%s\"}\n", cmd);
}

void ICACHE_FLASH_ATTR errorMessage(char *cmd, char *msg) {
	os_printf("{\"resp\":\"Error\", \"cmd\":\"%s\", \"params\":{\"message\":\"%s\"}}\n", cmd, msg);
}

static void ICACHE_FLASH_ATTR mqttVersion(char *cmd) {
	os_printf("{\"resp\":\"OK\",\"cmd\":\"%s\",\"params\":{\"version\":\"%s\"}}\n", cmd,
			getVersion());
	startFlash(1, 200, 800);
}

void ICACHE_FLASH_ATTR decodeCommand(char *bfr) { // Single position
	jsmn_parser p;
	jsmntok_t root[20];
	int rootTokenCount, idx;
	char *cmd;

	jsmn_init(&p);
	rootTokenCount = jsmn_parse(&p, bfr, strlen(bfr), root, sizeof(root) / sizeof(root[0]));

	if (rootTokenCount < 0) {
		ERRORP("Failed to parse JSON: %d\n", rootTokenCount);
		return;
	}
	if (root[0].type != JSMN_OBJECT) {/* Assume the top-level element is an object */
		ERRORP("Object expected\n");
		return;
	}
	INFO(printJSMN("root", 0, root, rootTokenCount - 1)
	;
	);
	for (idx = 1; idx < rootTokenCount; idx++) { /* Loop over all keys of the root object */
		if (jsoneq(bfr, &root[idx], "cmd")) {
			bfr[root[idx + 1].end] = 0;
			cmd = &bfr[root[idx + 1].start];
		} else if (jsoneq(bfr, &root[idx], "params")) {
			if (root[idx + 1].type == JSMN_OBJECT) {
				bfr[root[idx + 1].end] = 0;
				INFOP("Params: %s \n", &bfr[root[idx + 1].start]);
				switch (getCmdId(cmd)) {
				case eWiFi_SetParams:
					setWiFiParams(cmd, bfr, root, idx + 1, rootTokenCount);
					break;
				case eWiFi_Connect:
					WiFiConnect(cmd, bfr, root, idx + 1, rootTokenCount);
					break;
				case eMQTT_InitConnection:
					initConnection(cmd, bfr, root, idx + 1, rootTokenCount);
					break;
				case eMQTT_InitClient:
					initClient(cmd, bfr, root, idx + 1, rootTokenCount);
					break;
				case eMQTT_InitLWT:
					initLWT(cmd, bfr, root, idx + 1, rootTokenCount);
					break;
				case eMQTT_Subscribe:
					subscribe(cmd, bfr, root, idx + 1, rootTokenCount);
					break;
				case eMQTT_Unsubscribe:
					unsubscribe(cmd, bfr, root, idx + 1, rootTokenCount);
					break;
				case eMQTT_Publish:
					publish(cmd, bfr, root, idx + 1, rootTokenCount);
					break;
				default:
					errorMessage(cmd, "Bad Command (params expected)");
					break;
				}
				return;
			}
		}
	}
	switch (getCmdId(cmd)) { // Commands with no parameters
	case eVersion:
		mqttVersion(cmd);
		break;
	case eMQTT_Status:
		mqttStatus(cmd);
		break;
	case eWiFi_GetParams:
		getWiFiParams(cmd);
		break;
	case eWiFi_Status:
		WiFiStatus(cmd);
		break;
	case eWiFi_Scan:
		WiFiScan(cmd);
		break;
	case eMQTT_Connect:
		mqttConnect(cmd);
		break;
	case eMQTT_Disconnect:
		mqttDisconnect(cmd);
		break;
	default:
		errorMessage(cmd, "Bad Command");
		break;
	}
}
