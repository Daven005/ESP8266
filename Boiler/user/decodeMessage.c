/*
 * decodeMessage.c
 *
 *  Created on: 18 Jul 2016
 *      Author: User
 */
#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "stdout.h"
#include "jsmn.h"
#include "temperature.h"
#include "mqtt.h"
#include "config.h"
#include "decodeMessage.h"
#include "debug.h"
#include "publish.h"
#include "io.h"
#include "time.h"

#include "user_conf.h"
#include "user_main.h"

extern os_timer_t date_timer;
static char *nullStr = "";

static int ICACHE_FLASH_ATTR splitString(char *string, char delim, char *tokens[], int tokenCount) {
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
	int i;

	if (*string == '\0')
		string++; // Ignore 1st leading delimiter
	while (string < endString) {
		tokens[idx] = string;
		string++;
		idx++;
		while (*string++)
			;
	}
	for (i=idx; i<tokenCount; i++) {
		tokens[i] = nullStr;
	}
	return idx;
}

static void printTokens(char *tokens[], int count) {
	int i;
	for (i=0; i<count;i++) {
		os_printf("/%s", tokens[i]);
	}
}

static void saveMapName(uint8 sensorID, char *bfr) {
	uint8 mapIdx = 0xff;
	char *name = NULL;
	jsmn_parser p;
	jsmntok_t tokens[20];
	int r, i;

	jsmn_init(&p);
	r = jsmn_parse(&p, bfr, strlen(bfr), tokens, sizeof(tokens) / sizeof(tokens[0]));

	if (r < 0) {
		ERRORP("Failed to parse JSON: %d\n", r);
		return;
	}
	if (r < 1 || tokens[0].type != JSMN_OBJECT) {/* Assume the top-level element is an object */
		ERRORP("Object expected\n");
		return;
	}
	TESTP("%d tokens\n", r);
	for (i = 1; i < r; i++) { /* Loop over all keys of the root object */
		if (jsoneq(bfr, &tokens[i], "map")) {
			mapIdx = atoi(bfr + tokens[i + 1].start);
		} else if (jsoneq(bfr, &tokens[i], "name")) {
			if (tokens[i + 1].type == JSMN_STRING) {
				name = bfr + tokens[i + 1].start;
				bfr[tokens[i + 1].end] = 0; // Overwrites trailing quote
			}
		}
	}
	if (mapIdx < MAP_TEMP_SIZE && name != NULL) {
		os_strcpy(sysCfg.mappingName[mapIdx], name);
		CFG_dirty();
		printMappedTemperature(mapIdx);
		TESTP("\n");
	}
}

static void ICACHE_FLASH_ATTR decodeDeviceSet(char* param, char* dataBuf, MQTT_Client* client) {
	uint16 temp; bool updated = false;

	if (os_strcmp("name", param) == 0) {
		if (os_strcmp(sysCfg.deviceName, dataBuf) != 0) {
			os_strcpy(sysCfg.deviceName, dataBuf);
			updated = true;
		}
	} else if (os_strcmp("location", param) == 0) {
		if (os_strcmp(sysCfg.deviceLocation, dataBuf) != 0) {
			os_strcpy(sysCfg.deviceLocation, dataBuf);
			updated = true;
		}
	} else if (os_strcmp("updates", param) == 0) {
		temp = atoi(dataBuf);
		if (temp != sysCfg.updates) {
			sysCfg.updates = temp;
			updated = true;
		}
		resetTransmitTimer();
	} else if (os_strcmp("inputs", param) == 0) {
		temp = atoi(dataBuf);
		if (temp != sysCfg.inputs) {
			sysCfg.inputs = temp;
			updated = true;
		}
	} else if (os_strcmp("outputs", param) == 0) {
		temp = atoi(dataBuf);
		if (temp != sysCfg.outputs) {
			sysCfg.outputs = temp;
			updated = true;
		}
	}
	if (updated) _publishDeviceInfo();
	CFG_dirty();
}

static void ICACHE_FLASH_ATTR decodeSensorClear(char *idPtr, char *param, MQTT_Client* client) {
	if (os_strcmp("temperature", param) == 0) {
		uint8 idx = clearTemperatureOverride(idPtr);
		publishTemperature(idx);
	} else if (os_strcmp("output", param) == 0) {
		overrideClearOutput(atoi(idPtr));
	} else if (os_strcmp("input", param) == 0) {
		overrideClearInput(atoi(idPtr));
	}
}

static void ICACHE_FLASH_ATTR decodeSensorSet(char *valPtr, char *idPtr, char *param,
		MQTT_Client* client) {
	uint32 ts = system_get_time();
	int id = atoi(idPtr);
	int value = atoi(valPtr);
	checkTime("decodeSensorSet 1", ts);
	if (os_strcmp("mapping", param) == 0) {
		INFOP("Set sensor mapping %s -> %s\n", valPtr, idPtr);
		if (strlen(valPtr) == 0) return; // No mapping data
		uint8 mapIdx = atoi(valPtr);
		if (mapIdx >= MAP_TEMP_SIZE) return; // can't be used as mapIdx
		uint8 newMapValue = sensorIdx(idPtr);
		if (newMapValue >= MAP_TEMP_SIZE) return; // can't be used

		sysCfg.mapping[mapIdx] = newMapValue;
		CFG_dirty();
	} else if (os_strcmp("name", param) == 0) {
		INFOP("Set sensor name %s -> %s\n", idPtr, valPtr);
		int sensorID = sensorIdx(idPtr);
		if (sensorID >= MAP_TEMP_SIZE) {
			ERRORP("Invalid sensorID %s for 'name' (%d)\n", idPtr, sensorID);
			return; // can't be used as mapIdx
		}
		saveMapName(sensorID, valPtr);
	} else if (os_strcmp("setting", param) == 0) {
		if (0 <= id && id < SETTINGS_SIZE) {
			if (SET_MINIMUM <= value && value <= SET_MAXIMUM) {
				sysCfg.settings[id] = value;
				checkTime("decodeSensorSet 2", ts);
				CFG_dirty();
				checkTime("decodeSensorSet 3", ts);
				TESTP("Setting %d = %d (Solar?)\n", id, sysCfg.settings[id]);
				_publishDeviceInfo();
				checkTime("decodeSensorSet 4", ts);
			}
		}
	} else if (os_strcmp("temperature", param) == 0) {
		uint8 idx = setTemperatureOverride(idPtr, valPtr);
		publishTemperature(idx);
	} else if (os_strcmp("output", param) == 0) {
		if (0 <= id && id < OUTPUTS) {
			overrideSetOutput(id, value);
		}
	} else if (os_strcmp("input", param) == 0) {
		if (0 <= id && id < INPUTS) {
			overrideSetInput(id, value);
			publishInput(id, value);
		}
	}
}

static void ICACHE_FLASH_ATTR decodeTemps(char *bfr) {
	jsmn_parser p;
	jsmntok_t t[20];
	int r, i;

	jsmn_init(&p);
	r = jsmn_parse(&p, bfr, strlen(bfr), t, sizeof(t) / sizeof(t[0]));

	if (r < 0) {
		ERRORP("Failed to parse JSON: %d\n", r);
		return;
	}
	if (r < 1 || t[0].type != JSMN_OBJECT) {/* Assume the top-level element is an object */
		ERRORP("Object expected\n");
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

void ICACHE_FLASH_ATTR decodeMessage(MQTT_Client* client, char* topic, char* data) {
	uint32 ts = system_get_time();
#define MAX_TOKENS 10
	char* tokens[MAX_TOKENS];
	int tokenCount = splitString((char*) topic, '/', tokens, MAX_TOKENS);
	checkTime("decodeMessage 1", ts);
	if (tokenCount > 0) {
		if (os_strcmp("Raw", tokens[0]) == 0) {
			if (tokenCount == 4 && os_strcmp(sysCfg.device_id, tokens[1]) == 0
					&& os_strcmp("set", tokens[2]) == 0) {
				if (os_strlen(data) < NAME_SIZE - 1) {
					decodeDeviceSet(tokens[3], data, client);
				}
			} else if (tokenCount == 5 && os_strcmp(sysCfg.device_id, tokens[1]) == 0) {
				if (os_strcmp("set", tokens[3]) == 0) {
					decodeSensorSet(data, tokens[2], tokens[4], client);
				} else if (os_strcmp("clear", tokens[3]) == 0) {
					decodeSensorClear(tokens[2], tokens[4], client);
				}
			}
			checkTime("decodeMessage 2", ts);
		} else if (os_strcmp("App", tokens[0]) == 0) {
			if (tokenCount == 2 && os_strcmp("date", tokens[1]) == 0) {
				setTime((time_t) atol(data));
				os_timer_disarm(&date_timer); // Restart it
				os_timer_arm(&date_timer, 10 * 60 * 1000, false); //10 minutes
			} else if (tokenCount == 3 && os_strcmp("Temp", tokens[1]) == 0) {
				if (os_strcmp("hourly", tokens[2]) == 0) {
					decodeTemps(data);
				} else if (os_strcmp("current", tokens[2]) == 0) {
					setOutsideTemp(0, atol(data));
				}
			} else if (tokenCount == 2 && os_strcmp("Refresh", tokens[1]) == 0) {
				publishData(0); // publish all I/O & temps
			}
			checkTime("decodeMessage 3", ts);
		}
	}
	if (!checkTime("decodeMessage", ts))
		TEST(printTokens(tokens, 5); os_printf("%s=>%s\n", topic, data););
}
