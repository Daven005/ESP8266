#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "stdout.h"

#include "easygpio.h"
#include "config.h"
#include "wifi.h"
#include "debug.h"
#include "mqtt.h"
#include "smartconfig.h"
#include "user_config.h"
#include "version.h"
#include "time.h"
#include "ws2811.h"
#include "webServer.h"

static os_timer_t switch_timer;
static os_timer_t flash_timer;
static os_timer_t transmit_timer;
static os_timer_t date_timer;
static os_timer_t WS2811_timer;

MQTT_Client mqttClient;
uint8 mqttConnected;

#define LED 5
#define SWITCH 0 // GPIO 00
#define WS2811 13
static unsigned int switchCount;

static uint32 minHeap = 0xffffffff;

enum SmartConfigAction {
	SC_CHECK, SC_HAS_STOPPED, SC_TOGGLE
};
bool checkSmartConfig(enum SmartConfigAction);
void  user_webserver_init(uint32 port);

void user_rf_pre_init() {
}

uint32 checkMinHeap(void) {
	uint32 heap = system_get_free_heap_size();
	if (heap < minHeap) minHeap = heap;
	return minHeap;
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
		char *topic = (char *) os_zalloc(100);
		char *data = (char *) os_zalloc(100);
		os_sprintf(topic, (const char*) "/Raw/%s/0/info", sysCfg.device_id);
		os_sprintf(data, (const char*) "{ \"Type\":\"Level\", \"Value\":%d, \"RSSI\": %d}",
				system_adc_read(), wifi_station_get_rssi());
		MQTT_Publish(client, topic, data, strlen(data), 0, false);
		os_printf("%s=>%s\n", topic, data);
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR publishDeviceInfo(MQTT_Client* client) {
	if (mqttConnected) {
		char *topic = (char *) os_zalloc(50);
		char *data = (char *) os_zalloc(200);
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
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR publishDeviceReset(MQTT_Client* client) {
	if (mqttConnected) {
		char *topic = (char *) os_zalloc(50);
		char *data = (char *) os_zalloc(100);
		int idx;

		os_sprintf(topic, "/Raw/%10s/reset", sysCfg.device_id);
		os_sprintf(data,
			"{\"Name\":\"%s\", \"Location\":\"%s\", \"Version\":\"%s\", \"Reason\":\"%d\"}",
			sysCfg.deviceName, sysCfg.deviceLocation, version, system_get_rst_info()->reason);
		os_printf("%s=>%s\n", topic, data);
		MQTT_Publish(client, topic, data, strlen(data), 0, false);
		checkMinHeap();
		os_free(topic);
		os_free(data);
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
		checkMinHeap();
	}
}

void ICACHE_FLASH_ATTR switchAction(int action) {
	switch (action) {
	case 1:
		ws2811NextPlay();
		break;
	case 2:
		ws2811Continuous();
		break;
	case 3:
		break;
	case 4:
		ws2811Print();
		os_printf("minHeap: %d\n", checkMinHeap());
		break;
	case 5:
		checkSmartConfig(SC_TOGGLE);
		break;
	case 6:
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
				switchPulseCount = 0;
			}
			break;
		default:
			switchState = IDLE;
			break;
		}
	}
}

void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status) {
	os_printf("WiFi status: %d\r\n", status);
	if (status == STATION_GOT_IP) {
		MQTT_Connect(&mqttClient);
		 user_webserver_init(80);
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
	char *topic = (char*) os_zalloc(100);

	MQTT_Client* client = (MQTT_Client*) args;
	mqttConnected = true;
	os_printf("MQTT: Connected to %s:%d\n", sysCfg.mqtt_host, sysCfg.mqtt_port);

	os_sprintf(topic, "/Raw/%s/set/#", sysCfg.device_id);
	INFO("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	MQTT_Subscribe(client, "/App/date", 0);
	MQTT_Subscribe(client, "/App/test", 0);
	MQTT_Subscribe(client, "/App/pattern", 0);
	MQTT_Subscribe(client, "/App/play", 0);

	publishDeviceReset(client);
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

	checkMinHeap();
	os_free(topic);
	ws2811ResumePlay();
}

void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args) {
//	MQTT_Client* client = (MQTT_Client*)args;
	os_printf("MQTT Disconnected\n");
	mqttConnected = false;
	if (!checkSmartConfig(SC_CHECK)) {
		MQTT_Connect(&mqttClient);
	}
}

static void ICACHE_FLASH_ATTR ws2811TimerCb(uint32 count) {
	static uint8 startIdx = 0;
	if (count == 0) {
		if (startIdx >= 45)
			startIdx = 0;
		ws2811TestPattern(startIdx++);
	} else {
		ws2811TestPattern(count);
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
				ws2811SetTime((time_t) atol(dataBuf));
				os_timer_disarm(&date_timer); // Restart it
				os_timer_arm(&date_timer, 10 * 60 * 1000, false); //10 minutes
			} else if (tokenCount >= 2 && strcmp("test", tokens[1]) == 0) {
				int count = atoi(dataBuf);
				if (count >= 0) {
					os_timer_disarm(&WS2811_timer);
					os_timer_setfn(&WS2811_timer, (os_timer_func_t *) ws2811TimerCb, count);
					os_timer_arm(&WS2811_timer, 50, true);
				} else {
					ws2811TestPattern(-count);
				}
			} else if (tokenCount >= 2 && strcmp("pattern", tokens[1]) == 0) {
				patternSpec(dataBuf);
			} else if (tokenCount >= 2 && strcmp("play", tokens[1]) == 0) {
				playPatterns(dataBuf);
			}
		}
		checkMinHeap();
		os_free(topicBuf);
		os_free(dataBuf);
	}
}

static size_t ICACHE_FLASH_ATTR fs_size() { // returns the flash chip's size, in BYTES
  uint32_t id = spi_flash_get_id();
  uint8_t mfgr_id = id & 0xff;
  uint8_t type_id = (id >> 8) & 0xff; // not relevant for size calculation
  uint8_t size_id = (id >> 16) & 0xff; // lucky for us, WinBond ID's their chips as a form that lets us calculate the size
  if(mfgr_id != 0xEF) // 0xEF is WinBond; that's all we care about (for now)
    return 0;
  return 1 << size_id;
}

void ICACHE_FLASH_ATTR showSysInfo() {
	os_printf("SDK version is: %s\n", system_get_sdk_version());
	os_printf("Smart-Config version is: %s\n", smartconfig_get_version());
	system_print_meminfo();
	int sz = fs_size();
	os_printf("Flashsize map %d; id %lx (%lx - %d bytes)\n", system_get_flash_size_map(),
			spi_flash_get_id(), sz, sz / 8);
	os_printf("Boot mode: %d, version %d. Userbin %lx\n", system_get_boot_mode(),
			system_get_boot_version(), system_get_userbin_addr());
}

static void ICACHE_FLASH_ATTR initDone_cb() {
	CFG_Load();
	CFG_print();
	ws2811Init();
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

	WIFI_Connect(sysCfg.sta_ssid, sysCfg.sta_pwd, sysCfg.deviceName, wifiConnectCb);

	showSysInfo();
}

void ICACHE_FLASH_ATTR user_init(void) {
	stdout_init();
	gpio_init();
	wifi_station_set_auto_connect(false);
	wifi_station_set_reconnect_policy(true);

	easygpio_pinMode(LED, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputSet(LED, 1);
	easygpio_pinMode(WS2811, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputSet(WS2811, 0);
	easygpio_pinMode(12, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputSet(12, 0);
	system_init_done_cb(&initDone_cb);
}
