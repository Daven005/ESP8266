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
#include "include/user_conf.h"
#include "publish.h"

extern os_timer_t transmit_timer;
extern os_timer_t date_timer;

static int ICACHE_FLASH_ATTR splitString(char *string, char delim, char *tokens[]) {
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

static void ICACHE_FLASH_ATTR decodeSensorClear(char *idPtr, char *param, MQTT_Client* client) {
	if (strcmp("temperature", param) == 0) {
		uint8 idx = clearTemperatureOverride(idPtr);
	} else if (strcmp("output", param) == 0) {
		overrideClearOutput(atoi(idPtr));
	} else if (strcmp("flow", param) == 0) {
		overrideClearFlow();
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
	INFOP("decodeSensorSet: %d-%d %s\n", id, value, param);
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
			ERRORP("Invalid sensorID %s for 'name' (%d)\n", idPtr, sensorID);
			return; // can't be used as mapIdx
		}
		saveMapName(sensorID, valPtr);
	} else if (strcmp("setting", param) == 0) {
		if (0 <= id && id < SETTINGS_SIZE) {
			if (SET_MINIMUM <= value && value <= SET_MAXIMUM) {
				sysCfg.settings[id] = value;
				CFG_Save();
				INFOP("Setting %d = %d\n", id, sysCfg.settings[id]);
				_publishDeviceInfo();
			}
		}
	} else if (strcmp("flow", param) == 0) {
		overrideSetFlow(atoi(valPtr));
	} else if (strcmp("temperature", param) == 0) {
		uint8 idx = setTemperatureOverride(idPtr, valPtr);
		publishTemperature(idx);
	} else if (strcmp("output", param) == 0) {
		if (0 <= id && id < OUTPUTS) {
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
		os_timer_arm(&transmit_timer, sysCfgUpdates() * 1000, true);
	} else if (strcmp("inputs", param) == 0) {
		sysCfg.inputs = atoi(dataBuf);
	}
	_publishDeviceInfo();
	CFG_Save();
}

static void ICACHE_FLASH_ATTR saveCloudForecast(char *bfr) {
	jsmn_parser p;
	jsmntok_t t[20];
	int rootTokenCount, rootTokenIdx;

	jsmn_init(&p);
	rootTokenCount = jsmn_parse(&p, bfr, strlen(bfr), t, sizeof(t) / sizeof(t[0]));

	if (rootTokenCount < 0) {
		TESTP("Failed to parse JSON: %d\n", rootTokenCount);
		return;
	}
	if (rootTokenCount < 1 || t[0].type != JSMN_OBJECT) {/* Assume the top-level element is an object */
		TESTP("Object expected\n");
		return;
	}
	INFOP("%d tokens\n", rootTokenCount);
	for (rootTokenIdx = 1; rootTokenIdx < rootTokenCount; rootTokenIdx++) { /* Loop over all keys of the root object */
		if (jsoneq(bfr, &t[rootTokenIdx], "c")) {
			if (t[rootTokenIdx + 1].type == JSMN_ARRAY) {
				int arrayTokenIdx;
				int val;
				INFOP("Cloud: ");
				for (arrayTokenIdx = 0; arrayTokenIdx < t[rootTokenIdx + 1].size; arrayTokenIdx++) {
					jsmntok_t *itemToken = &t[rootTokenIdx + arrayTokenIdx + 2];
					val = atol(bfr + itemToken->start);
					setCloud(arrayTokenIdx, val);
					TESTP("[%d]=%d ", arrayTokenIdx, val);
				}
				INFOP("\n");
			}
		}
	}
}

static void ICACHE_FLASH_ATTR saveSunPosition(char *bfr) { // Single position
	jsmn_parser p;
	jsmntok_t root[20];
	int rootTokenCount, idx;
	int az = 0; // In case not matched
	int alt = 0;

	jsmn_init(&p);
	rootTokenCount = jsmn_parse(&p, bfr, strlen(bfr), root, sizeof(root) / sizeof(root[0]));

	if (rootTokenCount < 0) {
		ERRORP("saveSunPosition Failed to parse JSON: %d\n", rootTokenCount);
		return;
	}
	if (root[0].type != JSMN_OBJECT) {/* Assume the top-level element is an object */
		ERRORP("saveSunPosition Object expected\n");
		return;
	}
	for (idx = 1; idx < rootTokenCount; idx++) { /* Loop over all keys of the root object */
		if (jsoneq(bfr, &root[idx], "az")) {
			az = atoi(bfr + root[idx + 1].start);
			idx++;
		} else if (jsoneq(bfr, &root[idx], "alt")) {
			alt = atoi(bfr + root[idx + 1].start);
			idx++;
		}
	}
	setSun(0, az, alt);
}

static void ICACHE_FLASH_ATTR saveSunPositions(char *bfr) { // Single position
	jsmn_parser p;
	jsmntok_t root[20];
	int rootTokenCount, idx;
	int az = 0; // In case not matched
	int alt = 0;
	int hour;

	jsmn_init(&p);
	rootTokenCount = jsmn_parse(&p, bfr, strlen(bfr), root, sizeof(root) / sizeof(root[0]));

	if (rootTokenCount < 0) {
		ERRORP("saveSunPositions Failed to parse JSON: %d\n", rootTokenCount);
		return;
	}
	if (root[0].type != JSMN_ARRAY) {/* Assume the top-level element is an object */
		ERRORP("saveSunPositions Object expected\n");
		return;
	}
	INFO(printJSMN("root", 0, root, rootTokenCount-1););
	for (idx = 1, hour = 0; idx < rootTokenCount; ) { /* Loop over all keys of the root object */
		INFO(printJSMN("x", idx, &root[idx], 4););
		if (root[idx].type == JSMN_OBJECT) {
			int objectIdx, itemIdx;
			for (objectIdx=0, itemIdx=1; objectIdx < root[idx].size; objectIdx++) {
				if (jsoneq(bfr, &root[idx + itemIdx], "az")) {
					az = atoi(bfr + root[idx + itemIdx + 1].start);
					itemIdx += 2;
				} else if (jsoneq(bfr, &root[idx+ itemIdx], "alt")) {
					alt = atoi(bfr + root[idx + itemIdx + 1].start);
					itemIdx += 2;
				}
			}
			setSun(hour++, az, alt);
			idx += itemIdx;
		}
	}
}

static void ICACHE_FLASH_ATTR checkResetBoilerTemperature(MQTT_Client* client) {
	if (sunnyEnough() && mappedTemperature(MAP_TEMP_PANEL) >= 50 ) {
		MQTT_Publish(client, "/App/Set/Boiler Control/0", "50", 2, 0, 0);
		TESTP("Boiler DHW -> 50\n");
	} else {
		MQTT_Publish(client, "/App/Set/Boiler Control/0", "60", 2, 0, 0);
		TESTP("Boiler DHW -> 60\n");
	}
}

static void ICACHE_FLASH_ATTR setTemp(float t, struct Temperature* temp) {
	if (t >= 0) {
		temp->sign = '+';
		temp->val = (int) t;
		temp->fract = (t - temp->val) * 100.0;
	} else {
		temp->sign = '-';
		temp->val = -(int) t;
		temp->fract = -(t - temp->val) * 100.0;
	}
}

static double ICACHE_FLASH_ATTR atofloat(char *s) {
	float rez = 0, fact = 1;
	if (*s == '-') {
		s++;
		fact = -1;
	};
	for (int point_seen = 0; *s; s++) {
		if (*s == '.') {
			point_seen = 1;
			continue;
		};
		int d = *s - '0';
		if (0 <= d && d <= 9) {
			if (point_seen)
				fact /= 10.0f;
			rez = rez * 10.0f + (float) d;
		};
	};
	return rez * fact;
}

static void ICACHE_FLASH_ATTR saveTSbottom(char *data) {
	struct Temperature t;
	float temp = atofloat(data);
	setTemp(temp, &t);
	setUnmappedSensorTemperature("TS Bottom", DERIVED, t.val, t.fract);
}

void ICACHE_FLASH_ATTR decodeMessage(MQTT_Client* client, char* topic, char* data) {
	char* tokens[10];
	int tokenCount = splitString((char*) topic, '/', tokens);
	if (tokenCount > 0) {
		if (strcmp("Raw", tokens[0]) == 0) {
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
		} else if (strcmp("App", tokens[0]) == 0) {
			if (tokenCount == 2 && strcmp("date", tokens[1]) == 0) {
				os_timer_disarm(&date_timer); // Restart it
				os_timer_arm(&date_timer, 10 * 60 * 1000, false); //10 minutes
			} else if (tokenCount == 2 && strcmp("Refresh", tokens[1]) == 0) {
				publishData(); // publish all I/O & temps
			} else if (tokenCount == 3 && strcmp("Cloud", tokens[1]) == 0) {
				saveCloudForecast(data);
			} else if (tokenCount == 3 && strcmp("Sun", tokens[1]) == 0) {
				if (strcmp("current", tokens[2]) == 0) {
					saveSunPosition(data);
				} else if (strcmp("hourly", tokens[2]) == 0) {
					saveSunPositions(data);
				}
				checkResetBoilerTemperature(client);
			}
			if (tokenCount >= 5 && strcmp("TS Bottom", tokens[3]) == 0) {
				saveTSbottom(data);
			}
		}
	}
}
