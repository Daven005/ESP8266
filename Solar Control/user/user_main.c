#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "stdout.h"
#include <ds18b20.h>

#include "easygpio.h"
#include "config.h"
#include "wifi.h"
#include "debug.h"
#include "mqtt.h"
#include "smartconfig.h"
#include "user_config.h"
#include "version.h"
#include "time.h"
#include "IOdefs.h"
#include "flowMonitor.h"
#include "temperatureMonitor.h"

static os_timer_t switch_timer;
static os_timer_t flash_timer;
static os_timer_t flashA_timer;
static os_timer_t transmit_timer;
static os_timer_t date_timer;
static os_timer_t process_timer;

MQTT_Client mqttClient;
uint8 mqttConnected;

static unsigned int switchCount;
static unsigned int flashCount;
static unsigned int flashActionCount;
static uint8 wifiChannel = 255;
static char bestSSID[33];
static bool toggleState;

static uint32 minHeap = 0xffffffff;

enum SmartConfigAction {
	SC_CHECK, SC_HAS_STOPPED, SC_TOGGLE
};
bool checkSmartConfig(enum SmartConfigAction);

void user_rf_pre_init() {
}

uint32 ICACHE_FLASH_ATTR checkMinHeap(void) {
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

void ICACHE_FLASH_ATTR stopFlash(void) {
	easygpio_outputSet(LED, 0);
	os_timer_disarm(&flash_timer);
}

void ICACHE_FLASH_ATTR flash_cb(void) {
	easygpio_outputSet(LED, !easygpio_inputGet(LED));
	if (flashCount)
		flashCount--;
	if (flashCount == 0)
		stopFlash();
}

void ICACHE_FLASH_ATTR startFlashCount(int t, int repeat, unsigned int f) {
	easygpio_outputSet(LED, 1);
	flashCount = f * 2;
	os_timer_disarm(&flash_timer);
	os_timer_setfn(&flash_timer, (os_timer_func_t *) flash_cb, (void *) 0);
	os_timer_arm(&flash_timer, t, repeat);
}

void ICACHE_FLASH_ATTR startFlash(int t, int repeat) {
	startFlashCount(t, repeat, 0);
}

void ICACHE_FLASH_ATTR stopActionFlash(void) {
	easygpio_outputSet(ACTION_LED, 0);
	os_timer_disarm(&flashA_timer);
}

void ICACHE_FLASH_ATTR flashAction_cb(void) {
	easygpio_outputSet(ACTION_LED, !easygpio_inputGet(ACTION_LED));
	if (flashActionCount)
		flashActionCount--;
	if (flashActionCount == 0)
		stopActionFlash();
}

void ICACHE_FLASH_ATTR startActionFlashCount(int t, int repeat, unsigned int f) {
	easygpio_outputSet(ACTION_LED, 1);
	flashActionCount = f * 2;
	os_timer_disarm(&flashAction_cb);
	os_timer_setfn(&flashAction_cb, (os_timer_func_t *) flashAction_cb, (void *) 0);
	os_timer_arm(&flashAction_cb, t, repeat);
}

void ICACHE_FLASH_ATTR startActionFlash(int t, int repeat) {
	startActionFlashCount(t, repeat, 0);
}

void ICACHE_FLASH_ATTR publishAlarm(uint8 err, int info) {
	static uint8 last_err = 0xff;
	static int last_info = -1;
	if (err == last_err && info == last_info)
		return; // Ignore repeated errors
	char *topic = (char*) os_malloc(50), *data = (char*) os_malloc(100);
	os_sprintf(topic, (const char*) "/Raw/%s/alarm", sysCfg.device_id);
	os_sprintf(data, (const char*) "{ \"alarm\":%d, \"info\":%d}", err, info);
	MQTT_Publish(&mqttClient, topic, data, strlen(data), 0, 0);
	TESTP("%s=>$s\n", topic, data);
	checkMinHeap();
	os_free(topic);
	os_free(data);
}

void ICACHE_FLASH_ATTR publishError(uint8 err, int info) {
	static uint8 last_err = 0xff;
	static int last_info = -1;
	if (err == last_err && info == last_info)
		return; // Ignore repeated errors
	char *topic = (char*) os_malloc(50), *data = (char*) os_malloc(100);
	os_sprintf(topic, (const char*) "/Raw/%s/error", sysCfg.device_id);
	os_sprintf(data, (const char*) "{ \"error\":%d, \"info\":%d}", err, info);
	MQTT_Publish(&mqttClient, topic, data, strlen(data), 0, 0);
	TESTP("%s=>$s\n", topic, data);
	checkMinHeap();
	os_free(topic);
	os_free(data);
}

void ICACHE_FLASH_ATTR publishTemperatures(MQTT_Client* client) {
	uint16_t idx;
	struct Temperature *t;

	if (mqttConnected) {
		char *topic = (char *) os_zalloc(100);
		char *data = (char *) os_zalloc(100);
		if (topic == NULL || data == NULL) {
			startFlash(50, true); // fast
			return;
		}
		for (idx = 0; idx < temperatureSensorCount(); idx++) {
			if (getUnmappedTemperature(idx, &t)) {
				os_sprintf(topic, (const char*) "/Raw/%s/%s/info", sysCfg.device_id, t->address);
				os_sprintf(data, (const char*) "{ \"Type\":\"Temp\", \"Value\":\"%c%d.%d\"}",
						t->sign, t->val, t->fract);
				MQTT_Publish(client, topic, data, strlen(data), 0, 0);
				TESTP("%s==>%s\n", topic, data);
			}
		}
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR publishFlow(MQTT_Client* client) {
	if (mqttConnected) {
		char *topic = (char *) os_zalloc(100);
		char *data = (char *) os_zalloc(100);
		if (topic == NULL || data == NULL) {
			startFlash(50, true); // fast
			return;
		}

		os_sprintf(topic, (const char*) "/Raw/%s/2/info", sysCfg.device_id);
		os_sprintf(data, (const char*) "{ \"Type\":\"FlowMax\", \"Value\":%d}", flowMaxReading());
		MQTT_Publish(client, topic, data, strlen(data), 0, false);
		TESTP("%s=>%s\n", topic, data);

		os_sprintf(topic, (const char*) "/Raw/%s/3/info", sysCfg.device_id);
		os_sprintf(data, (const char*) "{ \"Type\":\"FlowTimes\", \"Value\":%d}", flowTimesReading());
		MQTT_Publish(client, topic, data, strlen(data), 0, false);
		TESTP("%s=>%s\n", topic, data);

		os_sprintf(topic, (const char*) "/Raw/%s/4/info", sysCfg.device_id);
		os_sprintf(data, (const char*) "{ \"Type\":\"Flow\", \"Value\":%d}", flowPerReading());
		MQTT_Publish(client, topic, data, strlen(data), 0, false);
		TESTP("%s=>%s\n", topic, data);

		resetFlowReadings();

		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR publishData(MQTT_Client* client) {
	if (mqttConnected) {
		publishTemperatures(client);
		publishFlow(client);
	}
}

void ICACHE_FLASH_ATTR publishDeviceInfo(MQTT_Client* client) {
	if (mqttConnected) {
		char *topic = (char *) os_zalloc(50);
		char *data = (char *) os_zalloc(300);
		int idx;
		struct ip_info ipConfig;

		if (topic == NULL || data == NULL) {
			startFlash(50, true); // fast
			return;
		}
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
		for (idx = 0; idx < SETTINGS_SIZE; idx++) {
			if (idx != 0)
				os_sprintf(data + strlen(data), ", ");
			os_sprintf(data + strlen(data), "%d", sysCfg.settings[idx]);
		}
		os_sprintf(data + strlen(data), "]}");
		MQTT_Publish(client, topic, data, strlen(data), 0, true);
		TESTP("%s=>%s\n", topic, data);
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

void ICACHE_FLASH_ATTR transmitCb(uint32_t *args) { // Depends on Update period
	MQTT_Client* client = (MQTT_Client*) args;
	if (!checkSmartConfig(SC_CHECK)) {
		publishData(client);
		checkMinHeap();
	}
}

void ICACHE_FLASH_ATTR switchAction(int action) {
	if (toggleState) {
		startFlashCount(100, true, action);
		switch (action) {
		case 1:
			break;
		case 2:
			saveLowReading();
			break;
		case 3:
			saveHighReading();
			break;
		case 4:
			break;
		case 5:
			checkSmartConfig(SC_TOGGLE);
			break;
		}
	} else {
		switch (action) {
		case 1:
			publishDeviceInfo(&mqttClient);
			publishData(&mqttClient);
			os_printf("minHeap: %d\n", checkMinHeap());
			break;
		case 2:
			break;
		case 3:
			break;
		case 4:
			break;
		case 5:
			break;
		}
	}
}

void ICACHE_FLASH_ATTR switchTimerCb(uint32_t *args) {
	const swOnMax = 100;
	const swOffMax = 5;
	static int switchPulseCount;
	static enum {
		IDLE, ON, OFF
	} switchState = IDLE;

	if ((toggleState = easygpio_inputGet(TOGGLE))) {
		easygpio_outputSet(ACTION_LED, 1);
	} else {
		easygpio_outputSet(ACTION_LED, 0);
	}
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
		wifiChannel = wifi_get_channel();
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
	TESTP("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	os_sprintf(topic, "/Raw/%s/+/set/#", sysCfg.device_id);
	TESTP("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	os_sprintf(topic, "/App/+/+/TS Bottom/#");
	TESTP("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	MQTT_Subscribe(client, "/App/date", 0);

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
	initFlowMonitor();
	initTemperatureMonitor();
	initPump();

	checkMinHeap();
	os_free(topic);
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
			} else if (tokenCount == 5 && strcmp(sysCfg.device_id, tokens[1] )== 0) {
				if (strcmp("set", tokens[3] )== 0) {
					int value = atoi(dataBuf);
					int id = atoi(tokens[2]);
					if (strcmp("setting", tokens[4]) == 0) {
						if (0 <= id && id < SETTINGS_SIZE) {
							if (SET_MINIMUM <= value && value <= SET_MAXIMUM) {
								sysCfg.settings[id] = value;
								if (id == SET_PUMP) {
									setPumpDemand(sysCfg.settings[SET_PUMP]); // system_adc_read());
								}
								CFG_Save();
								os_printf("Setting %d = %d\n", id, sysCfg.settings[id]);
								publishDeviceInfo(client);
							}
						}
					}
				}
			}
		} else if (strcmp("App", tokens[0]) == 0) {
			if (tokenCount == 2 && strcmp("date", tokens[1]) == 0) {
				os_timer_disarm(&date_timer); // Restart it
				os_timer_arm(&date_timer, 10 * 60 * 1000, false); //10 minutes
			}
			if (tokenCount >= 5 && strcmp("TS Bottom", tokens[3]) == 0) {
				saveTSbottom(dataBuf);
			}
		}
		checkMinHeap();
		os_free(topicBuf);
		os_free(dataBuf);
	}
}

void ICACHE_FLASH_ATTR processTimerCb(void) {
	startReadTemperatures();
	// setPumpPower(sysCfg.settings[SET_PUMP]); // system_adc_read());
	processPump();
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

static void ICACHE_FLASH_ATTR showSysInfo() {
	os_printf("SDK version is: %s\n", system_get_sdk_version());
	os_printf("Smart-Config version is: %s\n", smartconfig_get_version());
	system_print_meminfo();
	int sz = fs_size();
	os_printf("Flashsize map %d; id %lx (%lx - %d bytes)\n", system_get_flash_size_map(),
			spi_flash_get_id(), sz, sz / 8);
	os_printf("Boot mode: %d, version %d. Userbin %lx\n", system_get_boot_mode(),
			system_get_boot_version(), system_get_userbin_addr());
}

static void ICACHE_FLASH_ATTR startUp() {
	CFG_Load();
	CFG_print();
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
	os_timer_disarm(&process_timer);
	os_timer_setfn(&process_timer, (os_timer_func_t *) processTimerCb, NULL);
	os_timer_arm(&process_timer, 2000, true); // 2S

	showSysInfo();
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

void ICACHE_FLASH_ATTR wifi_handle_event_cb(System_Event_t *evt) {
	TESTP("WiFi event %x\n", evt->event);
}

void ICACHE_FLASH_ATTR user_init(void) {
	stdout_init();
	gpio_init();

	easygpio_pinMode(LED, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputSet(LED, 1);
	easygpio_pinMode(ACTION_LED, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputSet(LED, 0);
	easygpio_pinMode(PUMP, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputSet(PUMP, 1); // Inverted

	easygpio_pinMode(ANALOGUE_SELECT, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputSet(ANALOGUE_SELECT, 0);

	easygpio_pinMode(FLOW_SENSOR, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_outputDisable(FLOW_SENSOR);
	easygpio_pinMode(SWITCH, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_outputDisable(SWITCH);
	easygpio_pinMode(TOGGLE, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_outputDisable(TOGGLE);
	wifi_station_set_auto_connect(false);
	wifi_station_set_reconnect_policy(true);
	system_init_done_cb(&initDone_cb);
}
