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
#include "smartconfig.h"
#include "time.h"
#include "timezone.h"
#include "version.h"

static struct Temperature temperature[MAX_SENSOR];
static int sensors;

enum SmartConfigAction {
	SC_CHECK, SC_HAS_STOPPED, SC_TOGGLE
};
LOCAL os_timer_t switch_timer;
LOCAL os_timer_t ipScan_timer;
LOCAL os_timer_t mqtt_timer;
LOCAL os_timer_t flash_timer;
LOCAL os_timer_t time_timer;
LOCAL os_timer_t msg_timer;

static time_t currentTime;

bool ICACHE_FLASH_ATTR getTemperature(int i, struct Temperature **t);
void ICACHE_FLASH_ATTR checkControl(void);
void ICACHE_FLASH_ATTR publishError(uint8 err, int info);

#define SWITCH 0 // GPI00
#define LED 5 // NB same as an output

static unsigned int switchCount;

#define INPUT_SENSOR_ID_START 10
const uint8 inputsGPIO[] = { 13, 12, 14, 16 };
const uint8 outputMap[MAX_OUTPUT] = { 4, 5 };
uint8 currentInputs[4];
bool currentOutputs[MAX_OUTPUT];
bool outputOverrides[MAX_OUTPUT];
bool checkSmartConfig(enum SmartConfigAction action);
extern void boilerSetCurrentTime(uint8 h, uint8 m);
uint8 mqttConnected;
enum {
	IDLE, RESTART, FLASH, IPSCAN, SWITCH_SCAN, MQTT_DATA_CB, SMART_CONFIG
} lastAction __attribute__ ((section (".noinit")));
MQTT_Client mqttClient;
static uint32 minHeap = 0xffffffff;

void user_rf_pre_init(void) {}
uint32 ICACHE_FLASH_ATTR checkMinHeap(void) {
	uint32 heap = system_get_free_heap_size();
	if (heap < minHeap)
		minHeap = heap;
	return minHeap;
}

bool ICACHE_FLASH_ATTR checkSetOutput(uint8 op, uint8 set) {
	if (op < MAX_OUTPUT) {
		if (!outputOverrides[op]) {
			if (set != 0) { // NB High == OFF
				easygpio_outputSet(outputMap[op], currentOutputs[op] = false);
			} else {
				easygpio_outputSet(outputMap[op], currentOutputs[op] = true);
			}
			return currentOutputs[op];
		}
	}
	publishError(2, op);
	return false;
}

void ICACHE_FLASH_ATTR flash_cb(void) {
	easygpio_outputSet(LED, !easygpio_inputGet(LED));
}

void ICACHE_FLASH_ATTR startFlash(int t, int repeat) {
	easygpio_outputSet(LED, 1);
	os_timer_disarm(&flash_timer);
	os_timer_setfn(&flash_timer, (os_timer_func_t *) flash_cb, (void *) 0);
	os_timer_arm(&flash_timer, t, repeat);
}

void ICACHE_FLASH_ATTR stopFlash(void) {
	easygpio_outputSet(LED, 0);
	os_timer_disarm(&flash_timer);
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

int ICACHE_FLASH_ATTR mappedTemperature(uint8 name) {
	struct Temperature *t;
	if (getTemperature(sysCfg.mapping[name], &t))
		return t->val;
	//publishError(1, name);
	return -1;
}

void ICACHE_FLASH_ATTR publishTemperatures(MQTT_Client* client) {
	uint16_t idx;
	struct Temperature *t;

	if (mqttConnected) {
		char topic[100];
		char data[100];
		for (idx = 0; idx < sensors; idx++) {
			if (getTemperature(idx, &t)) {
				os_sprintf(topic, (const char*) "/Raw/%s/%s/info", sysCfg.device_id, t->address);
				os_sprintf(data, (const char*) "{ \"Type\":\"Temp\", \"Value\":\"%c%d.%d\"}",
						t->sign, t->val, t->fract);
				MQTT_Publish(client, topic, data, strlen(data), 0, 0);
			}
		}
	}
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

void ICACHE_FLASH_ATTR publishInput(MQTT_Client* client, uint8 idx, uint8 val) {
	if (mqttConnected) {
		char *topicBuf = (char*) os_malloc(50), *dataBuf = (char*) os_malloc(100);

		os_sprintf(topicBuf, (const char*) "/Raw/%s/%d/info", sysCfg.device_id,
				idx + INPUT_SENSOR_ID_START);
		os_sprintf(dataBuf,
				(const char*) "{\"Name\":\"IP%d\", \"Type\":\"Input\", \"Value\":\"%d\"}", idx,
				val);
		MQTT_Publish(client, topicBuf, dataBuf, strlen(dataBuf), 0, 0);
		checkMinHeap();
		os_free(topicBuf);
		os_free(dataBuf);
	}
}

void ICACHE_FLASH_ATTR publishAllInputs(MQTT_Client* client) {
	uint8 idx;
	for (idx = 0; idx < sizeof(inputsGPIO) && idx < sysCfg.inputs; idx++) {
		publishInput(client, idx, easygpio_inputGet(inputsGPIO[idx]));
	}
}

void ICACHE_FLASH_ATTR mqttCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*) args;
	uint16_t idx;

	publishTemperatures(client);
	publishAllInputs(client);
	//publishOutputs(client);
}

void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status) {
	os_printf("WiFi status: %d\r\n", status);
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
		INFO("Connected to %s (%s) %d", sta_conf->ssid, sta_conf->password, sta_conf->bssid_set);
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
	static bool doingSmartConfig = false;

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

void ICACHE_FLASH_ATTR printAll(void) {
	int idx;

	os_printf("Mapping: ");
	for (idx = 0; idx < sizeof(sysCfg.mapping); idx++) {
		os_printf("%d->%d (%d) ", idx, sysCfg.mapping[idx], mappedTemperature(idx));
	}
	os_printf("\n");
	os_printf("Outputs: ");
	for (idx = 0; idx < MAX_OUTPUT; idx++) {
		os_printf("%d->%d (%d) ", idx, outputMap[idx], currentOutputs[idx]);
	}
	os_printf("\n");
	os_printf("Settings: ");
	for (idx = 0; idx < SETTINGS_SIZE; idx++) {
		os_printf("%d=%d ", idx, sysCfg.settings[idx]);
	}
	os_printf("\n");

}

void ICACHE_FLASH_ATTR switchAction(int action) {
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
		MQTT_Publish(client, topic, data, strlen(data), 0, true);
	}
}

void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t *args) {
	char topic[100];
	char data[150];

	MQTT_Client* client = (MQTT_Client*) args;

	mqttConnected = true;
	os_sprintf(topic, "/Raw/%s/set/#", sysCfg.device_id);
	os_printf("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);
	os_sprintf(topic, "/Raw/%s/+/set/#", sysCfg.device_id);
	os_printf("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	publishDeviceInfo(client);

	os_timer_disarm(&mqtt_timer);
	os_timer_setfn(&mqtt_timer, (os_timer_func_t *) mqttCb, (void *) client);
	os_timer_arm(&mqtt_timer, sysCfg.updates * 1000, true);

	os_timer_disarm(&switch_timer);
	os_timer_setfn(&switch_timer, (os_timer_func_t *) switchTimerCb, NULL);
	os_timer_arm(&switch_timer, 100, true);
}

void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*) args;
	mqttConnected = false;
	os_timer_disarm(&mqtt_timer);
	os_printf("MQTT disconnected\r\n");
}

uint8 ICACHE_FLASH_ATTR sensorIdx(char* sensorID) {
	if (strlen(sensorID) > 2) { // not numeric sensorID
		int i;
		for (i = 0; i < MAX_SENSOR; i++) {
			if (strcmp(sensorID, temperature[i].address) == 0)
				return i;
		}
	} else {
		char *tail;
		long idx = strtol(sensorID, &tail, 10);
		if ((0 <= idx && idx < sensors))
			return idx;
	}
	return 0xff;
}

void ICACHE_FLASH_ATTR setTime(time_t t) {
	struct tm *tm;

	currentTime = t;
	tm = localtime(&t);
	applyDST(tm);
	boilerSetCurrentTime(tm->tm_hour, tm->tm_min);
}

void ICACHE_FLASH_ATTR time_cb(void *arg) {
	setTime(currentTime + 1); // Update every 1 second
}

static void ICACHE_FLASH_ATTR decodeSensorSet(int value, char *idPtr, char *param,
		MQTT_Client* client) {
	int id = atoi(idPtr);
	if (strcmp("mapping", param) == 0) {
		int sensorID = sensorIdx(idPtr);
		sysCfg.mapping[value] = sensorID;
		CFG_Save();
		os_printf("Map %d = %d\n", value, sysCfg.mapping[value]);
	} else if (strcmp("setting", param) == 0) {
		if (0 <= id && id < SETTINGS_SIZE) {
			if (SET_MINIMUM <= value && value <= SET_MAXIMUM) {
				sysCfg.settings[id] = value;
				CFG_Save();
				os_printf("Setting %d = %d\n", id, sysCfg.settings[id]);
				publishDeviceInfo(client);
			}
		}
	} else if (strcmp("output", param) == 0) {
		if (0 <= id && id < MAX_OUTPUT) {
			switch (value) {
			case 0:
				currentOutputs[id] = 1;
				outputOverrides[id] = true;
				break;
			case 1:
				currentOutputs[id] = 0;
				outputOverrides[id] = true;
				break;
			case -1:
				outputOverrides[id] = false;
				break;
			}
			easygpio_outputSet(outputMap[id], currentOutputs[id]);
			os_printf("<%d> Output %d set to %d\n", idPtr, outputMap[id], currentOutputs[id]);
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
void ICACHE_FLASH_ATTR mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len,
		const char *data, uint32_t data_len) {
	char *topicBuf = (char*) os_zalloc(topic_len + 1), *dataBuf = (char*) os_zalloc(data_len + 1);
	char *tokens[10];

	os_timer_disarm(&msg_timer);
	os_timer_arm(&msg_timer, 10 * 60 * 1000, true); // Restart it

	MQTT_Client* client = (MQTT_Client*) args;

	os_memcpy(topicBuf, topic, topic_len);
	os_memcpy(dataBuf, data, data_len);
	os_printf("Receive topic: %s, data: %s \r\n", topicBuf, dataBuf);

	int tokenCount = splitString((char *) topicBuf, '/', tokens);

	if (tokenCount >= 4 && strcmp("Raw", tokens[0]) == 0) {
		if (tokenCount == 4 && strcmp(sysCfg.device_id, tokens[1]) == 0
				&& strcmp("set", tokens[2]) == 0) {
			if (strlen(dataBuf) < NAME_SIZE - 1) {
				decodeDeviceSet(tokens[3], dataBuf, client);
			}
		} else if (tokenCount == 5 && strcmp(sysCfg.device_id, tokens[1]) == 0) {
			if (strcmp("set", tokens[3]) == 0) {
				int value = atoi(dataBuf);
				decodeSensorSet(value, tokens[2], tokens[4], client);
			}
		}
	} else if (tokenCount >= 2 && strcmp("App", tokens[0]) == 0) {
		if (tokenCount == 2 && strcmp("date", tokens[1]) == 0) {
			setTime((time_t) atol(dataBuf));
			os_timer_disarm(&time_timer); // Restart it
			os_timer_arm(&time_timer, 10 * 60 * 1000, false); //10 minutes
		}
	}
	checkMinHeap();
	os_free(topicBuf);
	os_free(dataBuf);
}

bool ICACHE_FLASH_ATTR getTemperature(int i, struct Temperature **t) {
	if (i >= MAX_SENSOR)
		return false;
	if (!temperature[i].set)
		return false;
	*t = &temperature[i];
	return true;
}

int ICACHE_FLASH_ATTR ds18b20() {
	int gpio = 0;
	int r, i;
	uint8_t addr[8], data[12];
	int idx = 0;

	for (i = 0; i < MAX_SENSOR; i++) {
		temperature[i].set = false;
	}

	ds_init(gpio);
	reset();
	write( DS1820_SKIP_ROM, 1);
	write( DS1820_CONVERT_T, 1);

	//750ms 1x, 375ms 0.5x, 188ms 0.25x, 94ms 0.12x
	os_delay_us(750 * 1000);
	//wdt_feed();

	reset_search();
	do {
		r = ds_search(addr);
		if (r) {
			if (crc8(addr, 7) != addr[7])
				os_printf("CRC mismatch, crc=%xd, addr[7]=%xd\n", crc8(addr, 7), addr[7]);

			switch (addr[0]) {
			case DS18B20:
				// os_printf( "Device is DS18B20 family.\n" );
				break;

			default:
				os_printf("Device is unknown family.\n");
				return 1;
			}
		} else {
			break;
		}

		reset();
		select(addr);
		write( DS1820_READ_SCRATCHPAD, 0);

		for (i = 0; i < 9; i++) {
			data[i] = read();
		}

		uint16_t tReading, tVal, tFract;
		char tSign;

		tReading = (data[1] << 8) | data[0];
		if (tReading & 0x8000) {
			tReading = (tReading ^ 0xffff) + 1;				// 2's complement
			tSign = '-';
		} else {
			tSign = '+';
		}
		tVal = tReading >> 4;  // separate off the whole and fractional portions
		tFract = (tReading & 0xf) * 100 / 16;
		os_sprintf(temperature[idx].address, "%02x%02x%02x%02x%02x%02x%02x%02x", addr[0], addr[1],
				addr[2], addr[3], addr[4], addr[5], addr[6], addr[7]);
		//os_printf("%s %c%d.%02d\n", temperature[idx].address, tSign, tVal, tFract);

		temperature[idx].set = true;
		temperature[idx].sign = tSign;
		temperature[idx].val = tVal;
		temperature[idx].fract = tFract;
		idx++;
	} while (true);
	sensors = idx;
	return r;
}

void ICACHE_FLASH_ATTR checkInputs(void) {
	uint8 val, idx;
	for (idx = 0; idx < sizeof(inputsGPIO); idx++) {
		val = easygpio_inputGet(inputsGPIO[idx]);
		if (val != currentInputs[idx]) {
			currentInputs[idx] = val;
			publishInput(&mqttClient, idx, val);
		}
	}
}
void ICACHE_FLASH_ATTR ipScan_cb(void *arg) {
	ds18b20();
	checkInputs();
	checkControl();
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

	os_printf("SDK version is: %s\n", system_get_sdk_version());
	os_printf("Smart-Config version is: %s\n", smartconfig_get_version());
	system_print_meminfo();
	os_printf("Flashsize map %d; id %lx\n", system_get_flash_size_map(), spi_flash_get_id());

	WIFI_Connect(sysCfg.sta_ssid, sysCfg.sta_pwd, sysCfg.deviceName, wifiConnectCb);

	easygpio_pinMode(SWITCH, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_outputDisable(SWITCH);

	uint8 idx;
	for (idx = 0; idx < sizeof(inputsGPIO); idx++) {
		easygpio_pinMode(inputsGPIO[idx], EASYGPIO_PULLUP, EASYGPIO_INPUT);
		easygpio_outputDisable(inputsGPIO[idx]);
	}
	for (idx = 0; idx < sizeof(outputMap); idx++) {
		easygpio_pinMode(outputMap[idx], EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
		easygpio_outputSet(outputMap[idx], (currentOutputs[idx] = 1)); // NB High == OFF
	}

	os_timer_disarm(&time_timer);
	os_timer_setfn(&time_timer, (os_timer_func_t *) time_cb, (void *) 0);
	os_timer_arm(&time_timer, 1000, true);

	os_timer_disarm(&msg_timer);
	os_timer_setfn(&msg_timer, (os_timer_func_t *) msgTimerCb, (void *) 0);
	os_timer_arm(&msg_timer, 10 * 60 * 1000, true);

	os_timer_disarm(&ipScan_timer);
	os_timer_setfn(&ipScan_timer, (os_timer_func_t *) ipScan_cb, (void *) 0);
	os_timer_arm(&ipScan_timer, 11100, true);
}

void user_init(void) {
	stdout_init();
	gpio_init();
	wifi_station_set_auto_connect(false);
	wifi_station_set_reconnect_policy(true);

	system_init_done_cb(&initDone_cb);
}
