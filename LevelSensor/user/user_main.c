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

MQTT_Client mqttClient;
uint8 mqttConnected;

#define LED 5
#define SWITCH 0 // GPIO 00
static unsigned int switchCount;
static uint8 wifiChannel = 255;
static char bestSSID[33];

int ICACHE_FLASH_ATTR checkSmartConfig(int action);
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

void ICACHE_FLASH_ATTR startFlash(int t, int repeat) {
	easygpio_outputSet(LED, 1);
	os_timer_disarm(&flash_timer);
	os_timer_setfn(&flash_timer, (os_timer_func_t *)smartConfigFlash_cb, (void *)0);
	os_timer_arm(&flash_timer, t, repeat);
}

void ICACHE_FLASH_ATTR stopFlash(void) {
	easygpio_outputSet(LED, 0);
	os_timer_disarm(&flash_timer);
}

void ICACHE_FLASH_ATTR publishData(MQTT_Client* client) {
	char topic[100];
	char data[100];
	uint16 reading;

	os_sprintf(topic, (const char*) "/Raw/%s/0/info", sysCfg.device_id);
	os_sprintf(data, (const char*) "{ \"Type\":\"Level\", \"Value\":%d}",  reading = system_adc_read());
	MQTT_Publish(client, topic, data, strlen(data), 0, false);
	os_printf("%s=>%s\n", topic, data);
	startFlash(reading-400, false); // The higher the reading (the lower the level) to longer the flash
}

void ICACHE_FLASH_ATTR publishDeviceReset(MQTT_Client* client) {
	if (mqttConnected) {
		char *topic = (char *) os_zalloc(50);
		char *data = (char *) os_zalloc(100);
		int idx;

		os_sprintf(topic, "/Raw/%s/reset", sysCfg.device_id);
		os_sprintf(data,
			"{\"Name\":\"%s\", \"Location\":\"%s\", \"Version\":\"%s\", \"Reason\":\"%d\"}",
			sysCfg.deviceName, sysCfg.deviceLocation, version, system_get_rst_info()->reason);
		os_printf("%s=>%s\n", topic, data);
		MQTT_Publish(client, topic, data, strlen(data), 0, false);
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR publishDeviceInfo(MQTT_Client* client) {
	if (mqttConnected) {
		char topic[50];
		char data[300];
		int idx;
		struct ip_info ipConfig;

		wifi_get_ip_info(STATION_IF, &ipConfig);
		os_sprintf(topic, "/Raw/%10s/info", sysCfg.device_id);
		os_sprintf(data,
				"{\"Name\":\"%s\", \"Location\":\"%s\", \"Version\":\"%s\", "
				"\"Updates\":%d, \"Inputs\":%d, \"RSSI\":%d, \"Channel\": %d, \"Attempts\": %d, ",
				sysCfg.deviceName, sysCfg.deviceLocation, version, sysCfg.updates,
				sysCfg.inputs, wifi_station_get_rssi(), wifiChannel, WIFI_Attempts());
		os_sprintf(data + strlen(data), "\"IPaddress\":\"%d.%d.%d.%d\"", IP2STR(&ipConfig.ip.addr));
		os_sprintf(data + strlen(data),", \"AP\":\"%s\"", bestSSID);
		os_sprintf(data + strlen(data),", \"Settings\":[");
		for (idx = 0; idx< SETTINGS_SIZE; idx++) {
			if (idx != 0) os_sprintf(data+strlen(data), ", ");
			os_sprintf(data+strlen(data), "%d", sysCfg.settings[idx]);
		}
		os_sprintf(data+strlen(data), "]}");
		os_printf("%s=>%s\n", topic, data);
		MQTT_Publish(client, topic, data, strlen(data), 0, true);
	}
}

void ICACHE_FLASH_ATTR switchAction(void) {
	publishData(&mqttClient);
}

void ICACHE_FLASH_ATTR smartConfig_done(sc_status status, void *pdata) {
	switch(status) {
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
		wifi_station_disconnect();
		wifi_station_connect();
		break;
	case SC_STATUS_LINK_OVER:
		os_printf("SC_STATUS_LINK_OVER\n");
		smartconfig_stop();
		checkSmartConfig(1);
		break;
	}
}

int ICACHE_FLASH_ATTR checkSmartConfig(int action) {
	static doingSmartConfig = false;

	switch (action) {
	case 0:
		break;
	case 1:
		os_printf("finished smartConfig\n");
		stopFlash();
		doingSmartConfig = false;
		break;
	case 2:
		if (doingSmartConfig) {
			os_printf("Stop smartConfig\n");
			stopFlash();
			smartconfig_stop();
			doingSmartConfig = false;
		} else {
			os_printf("Start smartConfig\n");
			wifi_station_disconnect();
			startFlash(100, true);
			smartconfig_start(SC_TYPE_ESPTOUCH, smartConfig_done, false);
			doingSmartConfig = true;
		}
		break;
	}
	return doingSmartConfig;
}

void ICACHE_FLASH_ATTR transmitCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*)args;
	if (!checkSmartConfig(false)) {
		publishData(client);
	}
}

void ICACHE_FLASH_ATTR switchTimerCb(uint32_t *args) {
	const swMax = 100;

	if (!easygpio_inputGet(SWITCH)) { // Switch is active LOW
		switchCount++;
		if (switchCount > swMax) switchCount = swMax;
		if (switchCount > 20) {
			checkSmartConfig(2);
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
	TESTP("WiFi status: %d\n", status);
	if (status == STATION_GOT_IP){
		wifiChannel = wifi_get_channel();
		MQTT_Connect(&mqttClient);
	} else {
		mqttConnected = false;
		MQTT_Disconnect(&mqttClient);
	}
}

void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t *args) {
	char topic[100];

	MQTT_Client* client = (MQTT_Client*)args;
	mqttConnected = true;
	os_printf("MQTT: Connected to %s:%d\r\n", sysCfg.mqtt_host, sysCfg.mqtt_port);

	os_sprintf(topic, "/Raw/%s/set/#", sysCfg.device_id);
	os_printf("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	MQTT_Subscribe(client, "/App/ssid", 0);

	publishDeviceInfo(client);

	os_timer_disarm(&switch_timer);
	os_timer_setfn(&switch_timer, (os_timer_func_t *)switchTimerCb, NULL);
	os_timer_arm(&switch_timer, 100, true);

	os_timer_disarm(&transmit_timer);
	os_timer_setfn(&transmit_timer, (os_timer_func_t *)transmitCb, (void *)client);
	os_timer_arm(&transmit_timer, sysCfg.updates*1000, true);
	easygpio_outputSet(LED, 0); // Turn LED off when connected
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

LOCAL void ICACHE_FLASH_ATTR startUp() {
	os_printf("\nLevelSensor starting ...\n");
	CFG_Load();

	MQTT_InitConnection(&mqttClient, sysCfg.mqtt_host, sysCfg.mqtt_port, sysCfg.security);
	MQTT_InitClient(&mqttClient, sysCfg.device_id, sysCfg.mqtt_user, sysCfg.mqtt_pass, sysCfg.mqtt_keepalive, 1);

	char temp[100];
	os_sprintf(temp, "/Raw/%s/offline", sysCfg.device_id);
	MQTT_InitLWT(&mqttClient, temp, "offline", 0, 0);

	MQTT_OnConnected(&mqttClient, mqttConnectedCb);
	MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
	MQTT_OnData(&mqttClient, mqttDataCb);

	WIFI_Connect(bestSSID, sysCfg.sta_pwd, sysCfg.deviceName, wifiConnectCb);
}

static void ICACHE_FLASH_ATTR wifi_station_scan_done(void *arg, STATUS status) {
	 uint8 ssid[33];
	  sint8 bestRSSI = -100;

	  if (status == OK) {
	    struct bss_info *bss_link = (struct bss_info *)arg;

	    while (bss_link != NULL) {
	      os_memset(ssid, 0, 33);
	      if (os_strlen(bss_link->ssid) <= 32) {
	        os_memcpy(ssid, bss_link->ssid, os_strlen(bss_link->ssid));
	      } else {
	        os_memcpy(ssid, bss_link->ssid, 32);
	      }
	      if (bss_link->rssi > bestRSSI && (strncmp(STA_SSID, ssid, strlen(STA_SSID)) == 0)) {
	    	  strcpy(bestSSID, ssid);
	    	  bestRSSI = bss_link->rssi;
	      }
	      TESTP("WiFi Scan: (%d,\"%s\",%d) best is %s\n", bss_link->authmode, ssid, bss_link->rssi, bestSSID);
	      bss_link = bss_link->next.stqe_next;
	    }
	  } else {
		  os_printf("wifi_station_scan fail %d\n", status);
	  }
	  startUp();
}

static void ICACHE_FLASH_ATTR initDone_cb() {
	TESTP("Start WiFi Scan\n");
	wifi_set_opmode(STATION_MODE);
	wifi_station_scan(NULL, wifi_station_scan_done);
}

void user_init(void) {
	stdout_init();
	gpio_init();
	wifi_station_set_auto_connect(false);

	easygpio_pinMode(LED, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputSet(LED, 1);
	system_init_done_cb(&initDone_cb);
}
