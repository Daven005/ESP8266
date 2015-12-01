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
#include "smartconfig.h"
#include "user_config.h"
#include "dht22.h"

#define sleepms(x) os_delay_us(x*1000);

LOCAL os_timer_t transmit_timer;
LOCAL os_timer_t process_timer;
LOCAL os_timer_t flash_timer;
LOCAL os_timer_t transmit_timer;
LOCAL os_timer_t date_timer;

MQTT_Client mqttClient;
uint8 mqttConnected;
const uint8 outputMap[MAX_OUTPUT] = { 0 };

bool currentOutputs[MAX_OUTPUT];
bool outputOverrides[MAX_OUTPUT];

#define RELAY 0
#define RELAY_ON 0
#define RELAY_OFF 1
#define LED 3

void user_rf_pre_init(){}

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

void ICACHE_FLASH_ATTR smartConfigFlash_cb(void) {
	easygpio_outputSet(LED, !easygpio_inputGet(LED));
}

void ICACHE_FLASH_ATTR startSmartConfigFlash(int t, int repeat) {
	easygpio_outputSet(LED, 1);
	os_timer_disarm(&flash_timer);
	os_timer_setfn(&flash_timer, (os_timer_func_t *) smartConfigFlash_cb, (void *) 0);
	os_timer_arm(&flash_timer, t, repeat);
}

void ICACHE_FLASH_ATTR stopSmartConfigFlash(void) {
	easygpio_outputSet(LED, 0);
	os_timer_disarm(&flash_timer);
}

void ICACHE_FLASH_ATTR publishData(MQTT_Client* client) {
	char topic[100];
	char data[100];
	struct dht_sensor_data* r = DHTRead();

	os_sprintf(topic, (const char*) "/Raw/%s/0/info", sysCfg.device_id);
	os_sprintf(data, (const char*) "{ \"Type\":\"Temp\", \"Value\":%d}",  (int) r->temperature);
	MQTT_Publish(client, topic, data, strlen(data), 0, false);
	os_sprintf(topic, (const char*) "/Raw/%s/1/info", sysCfg.device_id);
	os_sprintf(data, (const char*) "{ \"Type\":\"Hum\", \"Value\":%d}",  (int) r->humidity);
	MQTT_Publish(client, topic, data, strlen(data), 0, false);
}

void ICACHE_FLASH_ATTR publishError(uint8 err, uint8 info) {
	static uint8 lastError = 255;
	static uint8 lastInfo = 255;
	char topic[50];
	char data[100];
	if (err != lastError || info != lastInfo) {
		os_sprintf(topic, (const char*) "/Raw/%s/error", sysCfg.device_id);
		os_sprintf(data, (const char*) "{ \"error\":%d, \"info\":%d}", err, info);
		MQTT_Publish(&mqttClient, topic, data, strlen(data), 0, 0);
		lastError = err;
		lastInfo = info;
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
		os_printf("%s=>%s\n", topic, data);
		MQTT_Publish(client, topic, data, strlen(data), 0, true);
	}
}

void ICACHE_FLASH_ATTR processData(void) {
	struct dht_sensor_data* r = DHTRead();

	if (!outputOverrides[0]) {
		if (r->avgTemperature < sysCfg.settings[SETTING_SET_POINT]) {
			//os_printf("+");
			easygpio_outputSet(RELAY, RELAY_ON);
		} else {
			//os_printf("-");
			easygpio_outputSet(RELAY, RELAY_OFF);
		}
	}
	if (r->avgTemperature < sysCfg.settings[SETTING_MIN_ALARM]) {
		publishError(0, r->avgTemperature);
	} else if (r->avgTemperature > sysCfg.settings[SETTING_MAX_ALARM]) {
		publishError(1, r->avgTemperature);
	}
}

void ICACHE_FLASH_ATTR transmitCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*)args;
	processData();
	publishData(client);
}

void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status) {
	INFO("WiFi status: %d\r\n", status);
	if (status == STATION_GOT_IP){
		MQTT_Connect(&mqttClient);
	} else {
		mqttConnected = false;
		MQTT_Disconnect(&mqttClient);
	}
}

void ICACHE_FLASH_ATTR dateTimerCb(void) {
	os_printf("Nothing heard so restarting...\n");
	system_restart();
}

void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t *args) {
	char topic[100];

	MQTT_Client* client = (MQTT_Client*)args;
	mqttConnected = true;
	os_printf("MQTT: Connected to %s:%d\r\n", sysCfg.mqtt_host, sysCfg.mqtt_port);

	os_sprintf(topic, "/Raw/%s/set/#", sysCfg.device_id);
	os_printf("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	os_sprintf(topic, "/Raw/%s/+/set/#", sysCfg.device_id);
	os_printf("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	MQTT_Subscribe(client, "/App/date", 0);

	publishDeviceInfo(client);

	DHTInit(DHT11, 2000);
	os_timer_disarm(&transmit_timer);
	os_timer_setfn(&transmit_timer, (os_timer_func_t *)transmitCb, (void *)client);
	os_timer_arm(&transmit_timer, sysCfg.updates*1000, true);

	os_timer_disarm(&date_timer);
	os_timer_setfn(&date_timer, (os_timer_func_t *) dateTimerCb, NULL);
	os_timer_arm(&date_timer, 10 * 60 * 1000, false); //10 minutes

	os_timer_disarm(&process_timer);
	os_timer_setfn(&process_timer, (os_timer_func_t *)processData, NULL);
	os_timer_arm(&process_timer, 1000, true);
}

void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args) {
//	MQTT_Client* client = (MQTT_Client*)args;
	MQTT_Connect(&mqttClient);
	mqttConnected = false;
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

	if (tokenCount > 0) {
		if (strcmp("Raw", tokens[0])== 0) {
			if (tokenCount == 4 && strcmp(sysCfg.device_id, tokens[1] )== 0 && strcmp("set", tokens[2] )== 0) {
				if (strlen(dataBuf) < NAME_SIZE-1) {
					if (strcmp("name", tokens[3]) == 0) {
						strcpy(sysCfg.deviceName, dataBuf);
					} else if (strcmp("location", tokens[3]) == 0){
						strcpy(sysCfg.deviceLocation, dataBuf);
					} else if (strcmp("updates", tokens[3]) == 0){
						sysCfg.updates = atoi(dataBuf);
						os_timer_disarm(&transmit_timer);
						os_timer_arm(&transmit_timer, sysCfg.updates*1000, true);
					}
					publishDeviceInfo(client);
					CFG_Save();
				}
			} else if (tokenCount == 5 && strcmp(sysCfg.device_id, tokens[1] )== 0) {
				if (strcmp("set", tokens[3] )== 0) {
					int value = atoi(dataBuf);
					int id = atoi(tokens[2]);
					if (strcmp("setting", tokens[4]) == 0) {
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
							easygpio_outputSet(outputMap[id], currentOutputs[id]);
							os_printf("<%d> Output %d set to %d\n", id, outputMap[id], currentOutputs[id]);
						}
					}
				}
			}
		} else if (strcmp("App", tokens[0])== 0) {
			if (tokenCount >= 2 && strcmp("ssid", tokens[1] )== 0) {
				strcpy(sysCfg.sta_ssid, dataBuf);
				CFG_Save();
				if (tokenCount >= 3 && strcmp("restart", tokens[2] )== 0) {
					system_restart();
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

	INFO("Hostname was: %s\n", wifi_station_get_hostname());
	wifi_station_set_hostname(sysCfg.deviceName);
	WIFI_Connect(sysCfg.sta_ssid, sysCfg.sta_pwd, wifiConnectCb);
	INFO("Hostname is: %s\n", wifi_station_get_hostname());
	INFO("SDK version is: %s\n", system_get_sdk_version());
	INFO("Smart-Config version is: %s\n", smartconfig_get_version());
	system_print_meminfo();
	INFO("Flashsize map %d; id %lx\n\n", system_get_flash_size_map(), spi_flash_get_id());
}

void user_init(void) {
	stdout_init();
	gpio_init();
	easygpio_pinMode(LED, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_pinMode(RELAY, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputSet(RELAY, RELAY_OFF);

	system_init_done_cb(&initDone_cb);
}
