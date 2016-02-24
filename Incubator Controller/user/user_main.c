#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "easygpio.h"
#include "stdout.h"

#include "config.h"
#include "wifi.h"
#include "debug.h"
#include "mqtt.h"
#include "lcd.h"
#include "smartconfig.h"
#include "user_config.h"

#include "version.h"

LOCAL os_timer_t switch_timer;
LOCAL os_timer_t flash_timer;
LOCAL os_timer_t transmit_timer;
LOCAL os_timer_t display_timer;
LOCAL os_timer_t date_timer;

MQTT_Client mqttClient;
uint8 mqttConnected;

#define MAX_ENTRY 20
#define MAX_NAME 32
typedef struct {
	char location[MAX_NAME];
	char device[MAX_NAME];
	char name[MAX_NAME];
	char type[10];
	char val[10];
} temps_t;
temps_t valueEntries[MAX_ENTRY]; // Assume initialised to 0
static unsigned int switchCount;
static unsigned int lightCount;
static startTemp;

enum SmartConfigAction {
	SC_CHECK, SC_HAS_STOPPED, SC_TOGGLE
};
bool checkSmartConfig(enum SmartConfigAction);
void wifiConnectCb(uint8_t status);

void user_rf_pre_init() {
}

void ckID(char *s) {
	os_printf(s);
	if (strlen(sysCfg.device_id) < 3) {
		os_printf("+");
	}
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

void ICACHE_FLASH_ATTR lightOn(void) {
	easygpio_outputSet(LCD_Light, 0);
	lightCount = 100; // 10 secs
}

void ICACHE_FLASH_ATTR lightOff(void) {
	easygpio_outputSet(LCD_Light, 1);
	lightCount = 0;
}

void ICACHE_FLASH_ATTR smartConfigFlash_cb(void) {
	if (lightCount > 0)
		lightOff();
	else
		lightOn();
}

void ICACHE_FLASH_ATTR startFlash(int t, int repeat) {
	lightOn();
	os_timer_disarm(&flash_timer);
	os_timer_setfn(&flash_timer, (os_timer_func_t *) smartConfigFlash_cb, (void *) 0);
	os_timer_arm(&flash_timer, t, repeat);
}

void ICACHE_FLASH_ATTR stopFlash(void) {
	lightOff();
	os_timer_disarm(&flash_timer);
}

void ICACHE_FLASH_ATTR printEntry(int idx) {
	if (strlen(valueEntries[idx].type) > 0)
		os_printf(">> %d %s %s %s %s %s\n", idx, valueEntries[idx].location,
				valueEntries[idx].device, valueEntries[idx].name, valueEntries[idx].type,
				valueEntries[idx].val);
	else
		os_printf(">> %d -\n", idx);
}

void ICACHE_FLASH_ATTR nextEntry(void) { // 500mS
	int idx;
	for (idx = 0; idx < MAX_ENTRY; idx++) {
		if (++startTemp >= MAX_ENTRY)
			startTemp = 0;
		if (strlen(valueEntries[startTemp].type) > 0) {
			printEntry(startTemp);
			return;
		}
	}
}

void ICACHE_FLASH_ATTR printAll(void) {
	int idx;
	for (idx = 0; idx < MAX_ENTRY; idx++) {
		printEntry(idx);
	}
}

void ICACHE_FLASH_ATTR storeEntry(uint8 idx, char *location, char *device, char *sensor, char *type,
		char *value) {
	strcpy(valueEntries[idx].location, location);
	strcpy(valueEntries[idx].device, device);
	strcpy(valueEntries[idx].name, sensor);
	strcpy(valueEntries[idx].type, type);
	strcpy(valueEntries[idx].val, value);
}

int ICACHE_FLASH_ATTR findEntry(char *location, char *device, char *sensor, char *type) {
	int i;
	for (i = 0; i < MAX_ENTRY; i++) {
		if (strcmp(sensor, valueEntries[i].name) == 0 && strcmp(device, valueEntries[i].device) == 0
				&& strcmp(location, valueEntries[i].location) == 0
				&& strcmp(type, valueEntries[i].type) == 0)
			return i;
	}
	return -1;
}

int ICACHE_FLASH_ATTR findSlot(void) {
	int i;
	for (i = 0; i < MAX_ENTRY; i++) {
		if (strlen(valueEntries[i].type) == 0) {
			return i;
		}
	}
	return -1;
}

int ICACHE_FLASH_ATTR compareData(uint8 idx1, uint8 idx2) {
	int result;
	if ((result = strcmp(valueEntries[idx1].type, valueEntries[idx2].type)) != 0)
		return result;
	if ((result = strcmp(valueEntries[idx1].location, valueEntries[idx2].location)) != 0)
		return result;
	if ((result = strcmp(valueEntries[idx1].device, valueEntries[idx2].device)) != 0)
		return result;
	return strcmp(valueEntries[idx1].name, valueEntries[idx2].name);
}

void ICACHE_FLASH_ATTR swapData(int idx1, int idx2) {
	temps_t swp;
	memcpy(&swp, &valueEntries[idx1], sizeof(temps_t));
	memcpy(&valueEntries[idx1], &valueEntries[idx2], sizeof(temps_t));
	memcpy(&valueEntries[idx2], &swp, sizeof(temps_t));
}

void ICACHE_FLASH_ATTR sortData(void) {
	int outerIdx, innerIdx, swapped = true;
	for (outerIdx = 0; (outerIdx < MAX_ENTRY) && swapped; outerIdx++) {
		swapped = false;
		for (innerIdx = MAX_ENTRY - 1; innerIdx > outerIdx + 1; innerIdx--) {
			if (compareData(innerIdx, innerIdx - 1) > 0) {
				swapData(innerIdx, innerIdx - 1);
				// os_printf("%d<->%d\n", innerIdx, innerIdx - 1);
				swapped = true;
			}
		}
	}
}

bool ICACHE_FLASH_ATTR passesFilter(char *appTokens[]) {
	int filterID;
	bool anyMatched = false;
	int activeFilters = 0;
	for (filterID = 0; filterID < FILTER_COUNT; filterID++) {
		if (strlen(sysCfg.filters[filterID]) > 0)
			activeFilters++;
	}
	if (activeFilters == 0) {
		INFOP("No filters\n");
		return true;
	}

	for (filterID = 0; filterID < FILTER_COUNT && !anyMatched; filterID++) {
		if (strlen(sysCfg.filters[filterID]) > 0) {
			bool match = true;
			char *filterTokens[10];
			char bfr[100]; // splitString overwrites filter template!
			strcpy(bfr, sysCfg.filters[filterID]);
			int tokenCount = splitString((char *) bfr, '/', filterTokens);
			int tkn;

			for (tkn = 0; tkn < tokenCount && match; tkn++) {
				if (strcmp("+", filterTokens[tkn]) == 0)
					continue;
				if (strcmp("#", filterTokens[tkn]) == 0)
					break;
				if (strcmp(filterTokens[tkn], appTokens[tkn]) != 0)
					match = false;
			}
			if (match)
				anyMatched = true;
		}
	}
	INFOP("Filter matched %d - (%s)\n", anyMatched, sysCfg.filters[filterID]);
	return anyMatched;
}

void ICACHE_FLASH_ATTR saveData(char *location, char *device, char *sensor, char *type, char *value) {
	int slot;

	if ((slot = findEntry(location, device, sensor, type)) >= 0) {
		strcpy(valueEntries[slot].type, type); // Update Value
		strcpy(valueEntries[slot].val, value);
	} else if ((slot = findSlot()) >= 0) {
		//os_printf("Slot %d\n", slot);
		storeEntry(slot, location, device, sensor, type, value);
		sortData();
	} else {
		// Loose it
	}
}

void ICACHE_FLASH_ATTR publishData(MQTT_Client* client) {
	if (mqttConnected) {
		char topic[100];
		char data[100];
		os_sprintf(topic, (const char*) "/Raw/%s/0/info", sysCfg.device_id);
		os_sprintf(data, (const char*) "{\"Type\":\"RSSI\", \"Value\":%d}",
				wifi_station_get_rssi());
		MQTT_Publish(client, topic, data, strlen(data), 0, false);
		os_printf("%s=>%s\n", topic, data);
	}
}

void ICACHE_FLASH_ATTR publishDeviceInfo(MQTT_Client* client) {
	if (mqttConnected) {
		char topic[50];
		char data[200];
		int idx;
		os_sprintf(topic, "/Raw/%10s/info", sysCfg.device_id);
		os_sprintf(data,
				"{\"Name\":\"%s\", \"Location\":\"%s\", \"Version\":\"%s\", \"Updates\":%d, \"Inputs\":%d, \"Settings\":[",
				sysCfg.deviceName, sysCfg.deviceLocation, version, sysCfg.updates, sysCfg.inputs);
		for (idx = 0; idx < SETTINGS_SIZE; idx++) {
			if (idx != 0)
				os_sprintf(data + strlen(data), ", ");
			os_sprintf(data + strlen(data), "%d", sysCfg.settings[idx]);
		}
		os_sprintf(data + strlen(data), "]}");
		os_printf("%s=>%s\n", topic, data);
		MQTT_Publish(client, topic, data, strlen(data), 0, true);
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
		sysCfg.sta_ssid;
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
			wifi_station_disconnect();
			wifi_station_connect();
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

void ICACHE_FLASH_ATTR displayEntry(uint8 idx) {
	showString(0, 0, valueEntries[idx].location);
	showString(0, 1, valueEntries[idx].device);
	showString(0, 2, valueEntries[idx].name);
	showString(0, 3, valueEntries[idx].type);
	showString(4, 5, valueEntries[idx].val);
}

void ICACHE_FLASH_ATTR displayCb(void) {
	clearLcd();
	if (strlen(valueEntries[startTemp].type) > 0) {
		displayEntry(startTemp);
	}
}

void ICACHE_FLASH_ATTR switchAction(int action) {
	switch (action) {
	case 1:
		nextEntry();
		if (lightCount >= 1)
			lightOn(); // Keep on if already on
		break;
	case 2:
		lightOn();
		break;
	case 3:
		lightOff();
		break;
	case 4:
		printAll();
		break;
	case 5:
		checkSmartConfig(SC_TOGGLE);
		break;
	case 6: {
		int i;
		for (i = 0; i < FILTER_COUNT; i++) {
			os_printf("f[%d]->%s\n", i, sysCfg.filters[i]);
		}
	}
		break;
	}
}

void ICACHE_FLASH_ATTR switchTimerCb(uint32_t *args) {
	const swOnMax = 100;
	const swOffMax = 5;
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
				INFOP("SW action %d, light %d\n", switchPulseCount, lightCount);
				switchPulseCount = 0;
			}
			break;
		default:
			switchState = IDLE;
			break;
		}
	}
	if (lightCount >= 1)
		lightCount--;
	else
		lightOff();

}

void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status) {
	os_printf("WiFi status: %d\r\n", status);
	if (status == STATION_GOT_IP) {
		MQTT_Connect(&mqttClient);
	} else {
		mqttConnected = false;
		MQTT_Disconnect(&mqttClient);
	}
}

void ICACHE_FLASH_ATTR transmitCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*) args;
	publishData(client);
}

void ICACHE_FLASH_ATTR dateTimerCb(void) {
	os_printf("Nothing heard so restarting...\n");
	system_restart();
}

void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t *args) {
	char topic[100];

	MQTT_Client* client = (MQTT_Client*) args;
	mqttConnected = true;
	INFOP("MQTT is Connected to %s:%d\n", sysCfg.mqtt_host, sysCfg.mqtt_port);

	os_sprintf(topic, "/Raw/%s/set/#", sysCfg.device_id);
	INFOP("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	os_sprintf(topic, "/Raw/%s/+/set/filter", sysCfg.device_id);
	INFOP("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	MQTT_Subscribe(client, "/App/#", 0);

	publishDeviceInfo(client);
	publishData(client);

	os_timer_disarm(&switch_timer);
	os_timer_setfn(&switch_timer, (os_timer_func_t *) switchTimerCb, NULL);
	os_timer_arm(&switch_timer, 100, true);

	os_timer_disarm(&display_timer);
	os_timer_setfn(&display_timer, (os_timer_func_t *) displayCb, NULL);
	os_timer_arm(&display_timer, 2000, true);

	os_timer_disarm(&date_timer);
	os_timer_setfn(&date_timer, (os_timer_func_t *) dateTimerCb, NULL);
	os_timer_arm(&date_timer, 10 * 60 * 1000, false); //10 minutes

	os_timer_disarm(&transmit_timer);
	os_timer_setfn(&transmit_timer, (os_timer_func_t *) transmitCb, (void *) client);
	os_timer_arm(&transmit_timer, sysCfg.updates * 1000, true);
	lightOff();
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
			} else if (tokenCount == 5 && strcmp(sysCfg.device_id, tokens[1]) == 0
					&& strcmp("set", tokens[3]) == 0 && strcmp("filter", tokens[4]) == 0) {
				uint8 filterID = atoi(tokens[2]);
				if (0 <= filterID && filterID <= 3) {
					if (strlen(dataBuf) < (sizeof(sysCfg.filters[0]) - 2)) {
						strcpy(sysCfg.filters[filterID], dataBuf);
						CFG_Save();
					}
				}
			}
		} else if (strcmp("App", tokens[0]) == 0) {
			if (tokenCount >= 2 && strcmp("date", tokens[1]) == 0) {
				os_timer_disarm(&date_timer); // Restart it
				os_timer_arm(&date_timer, 10 * 60 * 1000, false); //10 minutes
			} else if (tokenCount == 5) {
				if (passesFilter(&tokens[1])) {
					saveData(tokens[1], tokens[2], tokens[3], tokens[4], dataBuf);
				}
			}
		}
	}
	os_free(topicBuf);
	os_free(dataBuf);
}

LOCAL void ICACHE_FLASH_ATTR initDone_cb() {
	CFG_Load();
	os_printf("\n%s starting ...\n", sysCfg.deviceName);

	MQTT_InitConnection(&mqttClient, sysCfg.mqtt_host, sysCfg.mqtt_port, sysCfg.security);
	MQTT_InitClient(&mqttClient, sysCfg.device_id, sysCfg.mqtt_user, sysCfg.mqtt_pass,
			sysCfg.mqtt_keepalive, 1);

	char temp[100];
	os_sprintf(temp, "/Raw/%s/offline", sysCfg.device_id);
	MQTT_InitLWT(&mqttClient, temp, "offline", 0, 0);

	MQTT_OnConnected(&mqttClient, mqttConnectedCb);
	MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
	MQTT_OnData(&mqttClient, mqttDataCb);

	os_printf("SDK version is: %s\n", system_get_sdk_version());
	os_printf("Smart-Config version is: %s\n", smartconfig_get_version());
	system_print_meminfo();
	os_printf("Flash size map %d; id %lx\n", system_get_flash_size_map(), spi_flash_get_id());

	WIFI_Connect(sysCfg.sta_ssid, sysCfg.sta_pwd, sysCfg.deviceName, wifiConnectCb);

	lcdInit();
	lightOn();
	showString(1, 1, "MQTT Monitor");
}

void user_init(void) {
	gpio_init();
	stdout_init();
	easygpio_pinMode(LCD_Light, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_pinMode(LCD_SCE, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_pinMode(LCD_clk, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_pinMode(LCD_Data, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_pinMode(LCD_D_C, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_pinMode(LCD_RST, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_pinMode(SWITCH, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_outputDisable(SWITCH);
	lcdReset();
	lightOn();
	wifi_station_set_auto_connect(false);
	wifi_station_set_reconnect_policy(true);

	system_init_done_cb(&initDone_cb);
}
