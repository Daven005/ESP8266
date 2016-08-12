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
#include <user_interface.h>
#include "gpio.h"
#include "easygpio.h"
#include "stdout.h"
#include "user_config.h"
#include "config.h"
#include "wifi.h"
#include "debug.h"
#include "check.h"
#include "mqtt.h"
#include "http.h"
#include "jsmn.h"
#ifdef USE_SMART_CONFIG
#include "doSmartconfig.h"
#endif
#include "version.h"
#ifdef USE_TEMPERATURE
#include "temperature.h"
#endif
#include "time.h"


typedef struct { MQTT_Client *mqttClient; char *topic; char *data; } mqttData_t;
#define QUEUE_SIZE 20
os_event_t taskQueue[QUEUE_SIZE];
#define EVENT_MQTT_CONNECTED 1
#define EVENT_MQTT_DATA 2
#define EVENT_PROCESS_TIMER 3
#define EVENT_TRANSMIT 4

#define str(p) #p
#define xstr(s) str(s)

#ifndef SLEEP_MODE
#ifndef USE_OUPUTS
#define CONFIG "EP Awake Pin:" xstr(DS18B20_PIN)
#else
#define CONFIG "EP Awake+Outputs Pin:" xstr(DS18B20_PIN)
#endif
#else
#define CONFIG "EP Sleep Pin:" xstr(DS18B20_PIN)
#endif

static os_timer_t transmit_timer;
static os_timer_t date_timer;
static os_timer_t process_timer;
static os_timer_t mqtt_timer;

static uint8 wifiChannel = 255;
static uint8 mqttConnected;
enum lastAction_t {
	IDLE, RESTART, FLASH, PROCESS_FUNC, SWITCH_SCAN,
	PUBLISH_DATA, INIT_DONE, MQTT_DATA_CB, MQTT_DATA_FUNC, MQTT_CONNECTED_CB,
	MQTT_CONNECTED_FUNC, MQTT_DISCONNECTED_CB, SMART_CONFIG, WIFI_CONNECT_CHANGE=100
} lastAction __attribute__ ((section (".noinit")));
enum lastAction_t savedLastAction;
static MQTT_Client mqttClient;
bool setupMode = false;
static char bestSSID[33];
static uint16 vcc=0;
void user_rf_pre_init(void) {}

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

static size_t ICACHE_FLASH_ATTR fs_size() { // returns the flash chip's size, in BYTES
  uint32_t id = spi_flash_get_id();
  uint8_t mfgr_id = id & 0xff;
  uint8_t type_id = (id >> 8) & 0xff; // not relevant for size calculation
  uint8_t size_id = (id >> 16) & 0xff; // lucky for us, WinBond ID's their chips as a form that lets us calculate the size
  if(mfgr_id != 0xEF) // 0xEF is WinBond; that's all we care about (for now)
    return 0;
  return 1 << size_id;
}

bool ICACHE_FLASH_ATTR mqttIsConnected(void) {
	return mqttConnected;
}

void ICACHE_FLASH_ATTR stopConnection(void) {
	MQTT_Disconnect(&mqttClient);
}

void ICACHE_FLASH_ATTR startConnection(void) {
	MQTT_Connect(&mqttClient);
}

void ICACHE_FLASH_ATTR publishData(MQTT_Client* client) {
	if (mqttConnected) {
		char *topic = (char *) os_zalloc(100);
		char *data = (char *) os_zalloc(100);
		os_sprintf(topic, (const char*) "/Raw/%s/0/info", sysCfg.device_id);
		os_sprintf(data, (const char*) "{ \"Type\":\"Level\", \"Value\":%d}", system_adc_read());
		MQTT_Publish(client, topic, data, strlen(data), 0, false);
		TESTP("%s=>%s\n", topic, data);
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

static void ICACHE_FLASH_ATTR _publishDeviceInfo(void) {
	publishDeviceInfo(version, CONFIG, wifiChannel, WIFI_ConnectTime(), bestSSID, (vcc=system_adc_read()));
}

void ICACHE_FLASH_ATTR checkCanSleep(void) {
#if SLEEP_MODE == 1
	if (!checkSmartConfig(SC_CHECK) && !setupMode) {
		TESTP("Sleep %dS\n", sysCfg.updates);
		system_deep_sleep_set_option(1); // Do RF calibration on wake up
		system_deep_sleep(sysCfg.updates * 1000 * 1000);
	} else {
		mqttPublishedCb(NULL);
	}
#endif
}

void ICACHE_FLASH_ATTR transmitCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*) args;
	if (!checkSmartConfig(SC_CHECK)) {
		publishData(client);
		checkMinHeap();
	}
}

void ICACHE_FLASH_ATTR processTemperatureCb(void) {
	publishAllTemperatures();
}

void ICACHE_FLASH_ATTR processTimerCb(void) { // Depends on Updates
	ds18b20StartScan(processTemperatureCb);
	if (!system_os_post(USER_TASK_PRIO_1, EVENT_PROCESS_TIMER, 0))
		TESTP("Can't post EVENT_MQTT_CONNECTED\n");
}

void ICACHE_FLASH_ATTR processTimerFunc(void) {
	uint32 t = system_get_time();
	lastAction = PROCESS_FUNC;
	_publishDeviceInfo();
	checkTime("processTimerFunc", t);
}

void ICACHE_FLASH_ATTR printAll() {
	os_printf("Test\n");
}

#ifdef SWITCH
void ICACHE_FLASH_ATTR switchAction(int action) {
	startFlash(action, 50, 100);
	switch (action) {
	case 1:
		printAll();
		os_printf("minHeap: %d\n", checkMinHeap());
		break;
	case 2:
		break;
	case 3:
		if (!checkSmartConfig(SC_CHECK)) {
			if (!toggleHttpSetupMode())
				checkCanSleep();
		}
		break;
	case 4:
		break;
	case 5:
#ifdef USE_SMART_CONFIG
		checkSmartConfig(SC_TOGGLE);
#endif
		break;
	}
}
#endif

void ICACHE_FLASH_ATTR mqttPublishedCb(uint32_t *args) {
	os_timer_disarm(&mqtt_timer);
	os_timer_setfn(&mqtt_timer, (os_timer_func_t *) checkCanSleep, NULL);
	os_timer_arm(&mqtt_timer, 2000, false); // Allow time for subscribed messages to arrive
}

void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status) {
	os_printf("WiFi status: %d\r\n", status);
	if (status == STATION_GOT_IP) {
		MQTT_Connect(&mqttClient);
#ifdef WEB_CONFIG
		tcp_listen(80);
#endif
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
	if (!system_os_post(USER_TASK_PRIO_1, EVENT_MQTT_CONNECTED, (os_param_t) args))
		TESTP("Can't post EVENT_MQTT_CONNECTED\n");
}

void ICACHE_FLASH_ATTR mqttConnectedFunction(MQTT_Client *client) {
	uint32 t = system_get_time();
	char *topic = (char*) os_zalloc(100);
	static int reconnections = 0;

	os_printf("MQTT: Connected to %s:%d\n", sysCfg.mqtt_host, sysCfg.mqtt_port);

	os_sprintf(topic, "/Raw/%s/set/#", sysCfg.device_id);
	INFOP("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);
	os_sprintf(topic, "/Raw/%s/+/set/#", sysCfg.device_id);
	INFOP("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);
	os_sprintf(topic, "/Raw/%s/+/clear/#", sysCfg.device_id);
	INFOP("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	MQTT_Subscribe(client, "/App/date", 0);

	publishDeviceReset();
	_publishDeviceInfo();

	initSwitch();
	initPublish(&mqttClient);
	os_timer_disarm(&date_timer);
	os_timer_setfn(&date_timer, (os_timer_func_t *) dateTimerCb, NULL);
	os_timer_arm(&date_timer, 10 * 60 * 1000, false); //10 minutes

	os_timer_disarm(&transmit_timer);
	os_timer_setfn(&transmit_timer, (os_timer_func_t *) transmitCb, (void *) client);
	os_timer_arm(&transmit_timer, sysCfg.updates * 1000, true);

	os_timer_disarm(&process_timer);
	os_timer_setfn(&process_timer, (os_timer_func_t *) processTimerCb, NULL);
	os_timer_arm(&process_timer, 1000, true); // 1S

	checkMinHeap();
	os_free(topic);
#ifdef LED
	easygpio_outputSet(LED, 0);
#endif
	lastAction = MQTT_CONNECTED_CB;
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

	mqttData_t *params = (mqttData_t *)os_malloc(sizeof(mqttData_t));
	os_memcpy(topicBuf, topic, topic_len);
	topicBuf[topic_len] = 0;
	os_memcpy(dataBuf, data, data_len);
	dataBuf[data_len] = 0;

	TESTP("Receive topic: %s, data: %s\n", topicBuf, dataBuf);
	params->mqttClient = (MQTT_Client*) args;
	params->topic = topicBuf;
	params->data = dataBuf;
	if (!system_os_post(USER_TASK_PRIO_1, EVENT_MQTT_DATA, (os_param_t) params))
		TESTP("Can't post EVENT_MQTT_DATA\n");
	lastAction = MQTT_DATA_CB;
}

void ICACHE_FLASH_ATTR mqttDataFunction(MQTT_Client *client, char* topic, char *data) {
	char *tokens[10];
	uint32 t = system_get_time();

	lastAction = MQTT_DATA_FUNC;

	int tokenCount = splitString((char *) topic, '/', tokens);
	if (tokenCount > 0) {
		if (strcmp("Raw", tokens[0]) == 0) {
			if (tokenCount == 4 && strcmp(sysCfg.device_id, tokens[1]) == 0
					&& strcmp("set", tokens[2]) == 0) {
				if (strlen(data) < NAME_SIZE - 1) {
					if (strcmp("name", tokens[3]) == 0) {
						strcpy(sysCfg.deviceName, data);
					} else if (strcmp("location", tokens[3]) == 0) {
						strcpy(sysCfg.deviceLocation, data);
					} else if (strcmp("updates", tokens[3]) == 0) {
						sysCfg.updates = atoi(data);
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
		checkMinHeap();
		os_free(topic);
		os_free(data);
		lastAction = MQTT_DATA_CB;
	}
}

void ICACHE_FLASH_ATTR showSysInfo() {
	INFOP("SDK version is: %s\n", system_get_sdk_version());
	INFOP("Smart-Config version is: %s\n", smartconfig_get_version());
	system_print_meminfo();
	int sz = fs_size();
	INFOP("Flashsize map %d; id %lx (%lx - %d bytes)\n", system_get_flash_size_map(),
			spi_flash_get_id(), sz, sz / 8);
	INFOP("Boot mode: %d, version %d. Userbin %lx\n", system_get_boot_mode(),
			system_get_boot_version(), system_get_userbin_addr());
}

static void ICACHE_FLASH_ATTR backgroundTask(os_event_t *e) {
	mqttData_t *mqttData;
	TESTP("Background task %d\n", e->sig);
	switch (e->sig) {
	case EVENT_MQTT_CONNECTED:
		mqttConnectedFunction((MQTT_Client *) e->par);
		break;
	case EVENT_MQTT_DATA:
		mqttData = (mqttData_t *) e->par;
		mqttDataFunction(mqttData->mqttClient, mqttData->topic, mqttData->data);
		os_free(mqttData);
		break;
	case EVENT_PROCESS_TIMER:
		processTimerFunc();
		break;
	case EVENT_TRANSMIT:
#ifdef USE_OUTPUTS
		publishData();
#endif
		break;
	}
}

static void ICACHE_FLASH_ATTR startUp() {
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

	showSysInfo();
	if (strncmp(bestSSID, sysCfg.sta_ssid, 5) != 0) { // Dissimilar SSID
		strcpy(bestSSID, sysCfg.sta_ssid); // Use stored SSID; nb assumes same password
	}
	WIFI_Connect(bestSSID, sysCfg.sta_pwd, sysCfg.deviceName, wifiConnectCb);
	if ( !system_os_task(backgroundTask, USER_TASK_PRIO_1, taskQueue, QUEUE_SIZE))
		TESTP("Can't set up background task\n");

#ifdef USE_TEMPERATURE
	ds18b20StartScan(processTemperatureCb);
#endif
	lastAction = INIT_DONE;
}

#ifdef USE_WIFI_SCAN
static void ICACHE_FLASH_ATTR wifi_station_scan_done(void *arg, STATUS status) {
  uint8 ssid[33];
  int8 bestRSSI = -100;

  if (status == OK) {
    struct bss_info *bss_link = (struct bss_info *)arg;

    while (bss_link != NULL) {
      os_memset(ssid, 0, 33);
      if (os_strlen(bss_link->ssid) <= 32) {
        os_memcpy(ssid, bss_link->ssid, os_strlen(bss_link->ssid));
      } else {
        os_memcpy(ssid, bss_link->ssid, 32);
      }
      if (bss_link->rssi > bestRSSI) {
    	  bestRSSI = bss_link->rssi;
    	  strcpy(bestSSID, ssid);
      }
      TESTP("WiFi Scan: (%d,\"%s\",%d)\n", bss_link->authmode, ssid, bss_link->rssi);
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
#endif

void ICACHE_FLASH_ATTR user_init(void) {
	stdout_init();
	gpio_init();

#ifdef SWITCH
	easygpio_pinMode(SWITCH, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_outputDisable(SWITCH);
#endif
#ifdef LED
	easygpio_pinMode(LED, EASYGPIO_PULLUP, EASYGPIO_OUTPUT);
	easygpio_outputSet(LED, 1);
#endif
	wifi_station_set_auto_connect(false);
	wifi_station_set_reconnect_policy(true);
#ifdef USE_WIFI_SCAN
	system_init_done_cb(&initDone_cb);
#else
	system_init_done_cb(&startUp);
#endif
}
