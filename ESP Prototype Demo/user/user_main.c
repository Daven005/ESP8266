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

#include "version.h"

#define sleepms(x) os_delay_us(x*1000);

LOCAL os_timer_t switch_timer;
LOCAL os_timer_t flash_timer;
LOCAL os_timer_t transmit_timer;
LOCAL os_timer_t date_timer;

MQTT_Client mqttClient;
uint8 mqttConnected;

#define LED 5
#define SWITCH 0 // GPIO 00
static unsigned int switchCount;

enum SmartConfigAction {
	SC_CHECK, SC_HAS_STOPPED, SC_TOGGLE
};
bool ICACHE_FLASH_ATTR checkSmartConfig(enum SmartConfigAction);

void user_rf_pre_init() {
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
	if (mqttConnected) {
		char topic[100];
		char data[100];
		os_sprintf(topic, (const char*) "/Raw/%s/0/info", sysCfg.device_id);
		os_sprintf(data, (const char*) "{ \"Type\":\"Level\", \"Value\":%d, \"RSSI\": %d}",
				system_adc_read(), wifi_station_get_rssi());
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

void ICACHE_FLASH_ATTR switchAction(void) {
	publishData(&mqttClient);
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
	static doingSmartConfig = false;

	switch (action) {
	case SC_CHECK:
		break;
	case SC_HAS_STOPPED:
		os_printf("Finished smartConfig\n");
		stopSmartConfigFlash();
		doingSmartConfig = false;
		MQTT_Connect(&mqttClient);
		break;
	case SC_TOGGLE:
		if (doingSmartConfig) {
			os_printf("Stop smartConfig\n");
			stopSmartConfigFlash();
			smartconfig_stop();
			doingSmartConfig = false;
			MQTT_Connect(&mqttClient);
		} else {
			os_printf("Start smartConfig\n");
			MQTT_Disconnect(&mqttClient);
			mqttConnected = false;
			startSmartConfigFlash(100, true);
			doingSmartConfig = true;
			smartconfig_start(smartConfig_done, true);
		}
		break;
	}
	return doingSmartConfig;
}

void ICACHE_FLASH_ATTR transmitCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*) args;
	if (!checkSmartConfig(SC_CHECK)) {
		publishData(client);
	}
}

void ICACHE_FLASH_ATTR switchTimerCb(uint32_t *args) {
	const swMax = 100;

	if (!easygpio_inputGet(SWITCH)) { // Switch is active LOW
		switchCount++;
		if (switchCount > swMax)
			switchCount = swMax;
		if (switchCount > 20) {
			checkSmartConfig(SC_TOGGLE);
			switchCount = 0;
		}
	} else {
		if (0 < switchCount && switchCount < 5) {
			switchAction();
		} else {

		}
		switchCount = 0;
	}
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

void ICACHE_FLASH_ATTR dateTimerCb(void) {
	os_printf("Nothing heard so restarting...\n");
	system_restart();
}

void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t *args) {
	char topic[100];

	MQTT_Client* client = (MQTT_Client*) args;
	mqttConnected = true;
	os_printf("MQTT: Connected to %s:%d\n", sysCfg.mqtt_host, sysCfg.mqtt_port);

	os_sprintf(topic, "/Raw/%s/set/#", sysCfg.device_id);
	INFO("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	MQTT_Subscribe(client, "/App/date", 0);

	publishDeviceInfo(client);

	os_timer_disarm(&switch_timer);
	os_timer_setfn(&switch_timer, (os_timer_func_t *) switchTimerCb, NULL);
	os_timer_arm(&switch_timer, 100, true);

	os_timer_disarm(&date_timer);
	os_timer_setfn(&date_timer, (os_timer_func_t *) dateTimerCb, NULL);
	os_timer_arm(&date_timer, 10 * 60 * 1000, false); //10 minutes

	os_timer_disarm(&transmit_timer);
	os_timer_setfn(&transmit_timer, (os_timer_func_t *) transmitCb, (void *) client);
	os_timer_arm(&transmit_timer, sysCfg.updates * 1000, true);
	easygpio_outputSet(LED, 0); // Turn LED off when connected
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
			}
		} else if (strcmp("App", tokens[0]) == 0) {
			if (tokenCount >= 2 && strcmp("date", tokens[1]) == 0) {
				os_timer_disarm(&date_timer); // Restart it
				os_timer_arm(&date_timer, 10 * 60 * 1000, false); //10 minutes
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

	os_printf("Hostname was: %s\n", wifi_station_get_hostname());
	wifi_station_set_hostname(sysCfg.deviceName);
	WIFI_Connect(sysCfg.sta_ssid, sysCfg.sta_pwd, wifiConnectCb);
	os_printf("Hostname is: %s\n", wifi_station_get_hostname());
	os_printf("SDK version is: %s\n", system_get_sdk_version());
	os_printf("Smart-Config version is: %s\n", smartconfig_get_version());
	system_print_meminfo();
	os_printf("Flashsize map %d; id %lx\n", system_get_flash_size_map(), spi_flash_get_id());
}

void user_init(void) {
	stdout_init();
	gpio_init();
	wifi_station_set_auto_connect(false);
	wifi_station_set_reconnect_policy(true);

	easygpio_pinMode(LED, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputSet(LED, 1);
	system_init_done_cb(&initDone_cb);
}
