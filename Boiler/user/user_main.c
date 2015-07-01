/*
 * user_main1.c
 *
 *  Created on: 19 May 2015
 *      Author: Administrator
 */

#include <c_types.h>
#include <ds18b20.h>
#include <uart.h>
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

#define sleepms(x) os_delay_us(x*1000);

static struct Temperature temperature[MAX_SENSOR];
static int sensors;
static macString[20];

LOCAL os_timer_t switch_timer;
LOCAL os_timer_t ipScanTimer;
LOCAL os_timer_t mqtt_timer;
LOCAL os_timer_t mqtt_start_timer;
bool ICACHE_FLASH_ATTR getTemperature(int i, struct Temperature **t);
void ICACHE_FLASH_ATTR checkControl(void);
void ICACHE_FLASH_ATTR publishError(uint8 err, uint8 info);

static void switchAction(void);
#define Switch 0 // GPIO 00
static unsigned int switchCount;

#define INPUT_SENSOR_ID_START 10
const uint8 inputsGPIO[] = { 13, 12, 14, 16 };
const uint8 outputsGPIO[MAX_OUTPUT] = { 4, 5 };
uint8 currentInputs[4];
bool currentOutputs[MAX_OUTPUT];
bool outputOverrides[MAX_OUTPUT];

uint8 mqttConnected;

MQTT_Client mqttClient;

bool ICACHE_FLASH_ATTR checkSetOutput(uint8 op, uint8 set) {
	if (op < MAX_OUTPUT) {
		if (!outputOverrides[op]) {
			if (set != 0) { // NB High == OFF
				easygpio_outputSet(outputsGPIO[op], currentOutputs[op] = false);
			} else {
				easygpio_outputSet(outputsGPIO[op], currentOutputs[op] = true);
			}
			return currentOutputs[op];
		}
	}
	publishError(2, op);
	return false;
}

int ICACHE_FLASH_ATTR splitString(char *string, char delim, char *tokens[]) {
	char *endString; char *startString;

	startString = string;
	while (*string) {
		if (*string == delim) *string = '\0';
		string++;
	}
	endString = string;
	string = startString;
	int idx = 0;
	if (*string == '\0') string++; // Ignore 1st leading delimiter
	while (string < endString) {
		tokens[idx] = string;
		string++;
		idx++;
		while (*string++) ;
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

void ICACHE_FLASH_ATTR publishError(uint8 err, uint8 info) {
	char topic[50];
	char data[100];
	os_sprintf(topic, (const char*) "/Raw/%s/error", sysCfg.device_id);
	os_sprintf(data, (const char*) "{ \"error\":%d, \"info\":%d}", err, info);
	MQTT_Publish(&mqttClient, topic, data, strlen(data), 0, 0);
}

void ICACHE_FLASH_ATTR publishInput(MQTT_Client* client, uint8 idx, uint8 val) {
	if (mqttConnected) {
		char topic[50];
		char data[100];
		os_sprintf(topic, (const char*) "/Raw/%s/%d/info", sysCfg.device_id, idx+INPUT_SENSOR_ID_START);
		os_sprintf(data, (const char*) "{\"Name\":\"IP%d\", \"Type\":\"Input\", \"Value\":\"%d\"}", idx, val);
		MQTT_Publish(client, topic, data, strlen(data), 0, 0);
	}
}

void ICACHE_FLASH_ATTR publishAllInputs(MQTT_Client* client) {
	uint8 idx;
	for (idx = 0; idx < sizeof(inputsGPIO) && idx < sysCfg.inputs; idx++) {
		publishInput(client, idx, easygpio_inputGet(inputsGPIO[idx]));
	}
}

void ICACHE_FLASH_ATTR mqttCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*)args;
	uint16_t idx;

	publishTemperatures(client);
	publishAllInputs(client);
	//publishOutputs(client);
}

void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status) {
	os_printf("WiFi status: %d\r\n", status);
	if (status == STATION_GOT_IP){
		MQTT_Connect(&mqttClient);
	} else {
		os_timer_disarm(&mqtt_timer);
		MQTT_Disconnect(&mqttClient);
	}
}

void ICACHE_FLASH_ATTR switchAction(void) {
	int idx;

	os_printf("Mapping: ");
	for (idx=0; idx< sizeof(sysCfg.mapping); idx++) {
		os_printf("%d->%d (%d) ", idx, sysCfg.mapping[idx], mappedTemperature(idx));
	}
	os_printf("\n");
	os_printf("Outputs: ");
	for (idx=0; idx< MAX_OUTPUT; idx++) {
		os_printf("%d->%d (%d) ", idx, outputsGPIO[idx], currentOutputs[idx]);
	}
	os_printf("\n");
	os_printf("Settings: ");
	for (idx=0; idx< SETTINGS_SIZE; idx++) {
		os_printf("%d=%d ", idx, sysCfg.settings[idx]);
	}
	os_printf("\n");

}

void ICACHE_FLASH_ATTR switchTimerCb(uint32_t *args) {
	const swMax = 10;

	if (!easygpio_inputGet(Switch)) { // Switch is active LOW
		switchCount++;
		if (switchCount > swMax) switchCount = swMax;
		if (switchCount > 5) {

		}
	} else {
		if (0 < switchCount && switchCount < 5) {
			switchAction();
		} else {

		}
		switchCount = 0;
	}
}

void ICACHE_FLASH_ATTR publishDeviceInfo(MQTT_Client* client) {
	if (mqttConnected) {
		char topic[50];
		char data[200];
		int idx;
		os_sprintf(topic, "/Raw/%10s/info", sysCfg.device_id);
		os_sprintf(data, "{\"Name\":\"%s\", \"Location\":\"%s\", \"Updates\":%d, \"Inputs\":%d, \"Settings\":[",
			sysCfg.deviceName, sysCfg.deviceLocation, sysCfg.updates, sysCfg.inputs);
		for (idx = 0; idx< SETTINGS_SIZE; idx++) {
			if (idx != 0) os_sprintf(data+strlen(data), ", ");
			os_sprintf(data+strlen(data), "%d", sysCfg.settings[idx]);
		}
		os_sprintf(data+strlen(data), "]}");
		MQTT_Publish(client, topic, data, strlen(data), 0, true);
	}
}

void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t *args) {
	char topic[100];
	char data[150];

	MQTT_Client* client = (MQTT_Client*)args;

	mqttConnected = true;
	os_sprintf(topic, "/Raw/%s/set/#", sysCfg.device_id);
	os_printf("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);
	os_sprintf(topic, "/Raw/%s/+/set/#", sysCfg.device_id);
	os_printf("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	publishDeviceInfo(client);

	os_timer_disarm(&mqtt_timer);
	os_timer_setfn(&mqtt_timer, (os_timer_func_t *)mqttCb, (void *)client);
	os_timer_arm(&mqtt_timer, sysCfg.updates*1000, true);

	os_timer_disarm(&switch_timer);
	os_timer_setfn(&switch_timer, (os_timer_func_t *)switchTimerCb, NULL);
	os_timer_arm(&switch_timer, 100, true);
}

void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*)args;
	mqttConnected = false;
	os_timer_disarm(&mqtt_timer);
	os_printf("MQTT disconnected\r\n");
}

uint8 ICACHE_FLASH_ATTR sensorIdx(char* sensorID) {
	if (strlen(sensorID) > 2) { // not numeric sensorID
		int i;
		for (i=0; i<MAX_SENSOR; i++) {
			if (strcmp(sensorID, temperature[i].address) == 0)
				return i;
		}
	} else {
		char *tail;
		long idx =  strtol(sensorID, &tail, 10);
		if ((0 <= idx && idx < sensors))
			return idx;
	}
	return 0xff;
}

void ICACHE_FLASH_ATTR mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t data_len)
{
	char *topicBuf = (char*)os_zalloc(topic_len+1),
		 *dataBuf = (char*)os_zalloc(data_len+1);
	char *tokens[10];

	MQTT_Client* client = (MQTT_Client*)args;

	os_memcpy(topicBuf, topic, topic_len);
	topicBuf[topic_len] = 0;
	os_memcpy(dataBuf, data, data_len);
	dataBuf[data_len] = 0;
	os_printf("Receive topic: %s, data: %s \r\n", topicBuf, dataBuf);

	int tokenCount = splitString((char *)topicBuf, '/', tokens);

	if (tokenCount > 0 && strcmp("Raw", tokens[0])== 0) {
		if (tokenCount == 4 && strcmp(sysCfg.device_id, tokens[1] )== 0 && strcmp("set", tokens[2] )== 0) {
			if (strlen(dataBuf) < NAME_SIZE-1) {
				if (strcmp("name", tokens[3]) == 0) {
					strcpy(sysCfg.deviceName, dataBuf);
				} else if (strcmp("location", tokens[3]) == 0){
					strcpy(sysCfg.deviceLocation, dataBuf);
				} else if (strcmp("updates", tokens[3]) == 0){
					sysCfg.updates = atoi(dataBuf);
					os_timer_disarm(&mqtt_timer);
					os_timer_arm(&mqtt_timer, sysCfg.updates*1000, true);
				} else if (strcmp("inputs", tokens[3]) == 0){
					sysCfg.inputs = atoi(dataBuf);
				}
				publishDeviceInfo(client);
				CFG_Save();
			}
		} else if (tokenCount == 5 && strcmp(sysCfg.device_id, tokens[1] )== 0) {
			if (strcmp("set", tokens[3] )== 0) {
				int value = atoi(dataBuf);
				int id = atoi(tokens[2]);
				if (strcmp("mapping", tokens[4]) == 0) {
					int sensorID = sensorIdx(tokens[2]);
					sysCfg.mapping[value] = sensorID;
					CFG_Save();
					os_printf("Map %d = %d\n", value, sysCfg.mapping[value]);
				} else if (strcmp("setting", tokens[4]) == 0) {
					if (0 <= id && id < SETTINGS_SIZE) {
						if (SET_MINIMUM <= value && value <= SET_MAXIMUM) {
							sysCfg.settings[id] = value;
							CFG_Save();
							os_printf("Setting %d = %d\n", id, sysCfg.settings[id]);
							publishDeviceInfo(client);
						}
					}
				} else if (strcmp("output", tokens[4]) == 0) {
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
						easygpio_outputSet(outputsGPIO[id], currentOutputs[id]);
						os_printf("<%d> Output %d set to %d\n", id, outputsGPIO[id], currentOutputs[id]);
					}
				}
			}
		}
	}
	os_free(topicBuf);
	os_free(dataBuf);
}

bool ICACHE_FLASH_ATTR getTemperature(int i, struct Temperature **t) {
	if (i >= MAX_SENSOR) return false;
	if (!temperature[i].set) return false;
	*t = &temperature[i];
	return true;
}

int ICACHE_FLASH_ATTR ds18b20() {
	int gpio = 0;
	int r, i;
	uint8_t addr[8], data[12];
	int idx = 0;

	for (i=0; i<MAX_SENSOR; i++) {
		temperature[i].set = false;
	}

	ds_init( gpio );
	reset();
	write( DS1820_SKIP_ROM, 1 );
	write( DS1820_CONVERT_T, 1 );

	//750ms 1x, 375ms 0.5x, 188ms 0.25x, 94ms 0.12x
	os_delay_us( 750*1000 );
	wdt_feed();

	reset_search();
	do {
		r = ds_search( addr );
		if (r) {
			if( crc8( addr, 7 ) != addr[7] )
				os_printf( "CRC mismatch, crc=%xd, addr[7]=%xd\n", crc8( addr, 7 ), addr[7] );

			switch (addr[0]) {
			case DS18B20:
				// os_printf( "Device is DS18B20 family.\n" );
				break;

			default:
				os_printf( "Device is unknown family.\n" );
				return 1;
			}
		} else {
			break;
		}

		reset();
		select( addr );
		write( DS1820_READ_SCRATCHPAD, 0 );

		for( i = 0; i < 9; i++ ) {
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
		os_sprintf(temperature[idx].address, "%02x%02x%02x%02x%02x%02x%02x%02x",
			addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], addr[6], addr[7]);
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
	for (idx=0; idx < sizeof(inputsGPIO); idx++) {
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

void ICACHE_FLASH_ATTR mqtt_start_cb(void *arg) {
	os_printf("\r\nWiFi starting ...\r\n");

	CFG_Load();
	MQTT_InitConnection(&mqttClient, sysCfg.mqtt_host, sysCfg.mqtt_port, sysCfg.security);
	MQTT_InitClient(&mqttClient, sysCfg.device_id, sysCfg.mqtt_user, sysCfg.mqtt_pass, sysCfg.mqtt_keepalive, 1);

	MQTT_InitLWT(&mqttClient, "/lwt", "offline", 0, 0);
	MQTT_OnConnected(&mqttClient, mqttConnectedCb);
	MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
	MQTT_OnData(&mqttClient, mqttDataCb);

	WIFI_Connect(sysCfg.sta_ssid, sysCfg.sta_pwd, wifiConnectCb);
	uint8_t macaddr[6];
	wifi_get_macaddr(STATION_IF, macaddr);
	os_sprintf(macString, MACSTR, MAC2STR(macaddr));
	os_printf("MAC: %s\r\n", macString);

	easygpio_pinMode(Switch, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_outputDisable(Switch);

	uint8 idx;
	for (idx=0; idx<sizeof(inputsGPIO); idx++) {
		easygpio_pinMode(inputsGPIO[idx], EASYGPIO_PULLUP, EASYGPIO_INPUT);
		easygpio_outputDisable(inputsGPIO[idx]);
	}
	for (idx=0; idx<sizeof(outputsGPIO); idx++) {
		easygpio_pinMode(outputsGPIO[idx], EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
		easygpio_outputSet(outputsGPIO[idx], (currentOutputs[idx] = 1)); // NB High == OFF
	}
}

void user_init(void) {
	uart_init(BIT_RATE_115200, BIT_RATE_115200);
	stdout_init();
	gpio_init();
	os_delay_us(1000000);

	os_timer_disarm(&ipScanTimer);
	os_timer_setfn(&ipScanTimer, (os_timer_func_t *)ipScan_cb, (void *)0);
	os_timer_arm(&ipScanTimer, 11100, true);

	os_timer_disarm(&mqtt_start_timer);
	os_timer_setfn(&mqtt_start_timer, (os_timer_func_t *)mqtt_start_cb, (void *)0);
	os_timer_arm(&mqtt_start_timer, 1000, false);
}
