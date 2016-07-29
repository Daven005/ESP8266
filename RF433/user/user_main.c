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
#include "easygpio.h"
#include "stdout.h"
#include "user_config.h"
#include "config.h"
#include "wifi.h"
#include "debug.h"
#include "mqtt.h"
#include "jsmn.h"
#include "switch.h"
#include "flash.h"
#include "check.h"
#include "http.h"
#include "publish.h"
#include "smartconfig.h"
#include "doSmartConfig.h"
#include "user_main.h"
#include "version.h"
#include "time.h"
#include "xmit.h"

os_timer_t transmit_timer;
os_timer_t date_timer;
static uint8 wifiChannel = 255;

uint8 mqttConnected;
enum lastAction_t {
	IDLE, RESTART, FLASH, IPSCAN, SWITCH_SCAN,
	INIT_DONE, MQTT_DATA_CB, MQTT_DATA_FUNC, MQTT_CONNECTED_CB, MQTT_CONNECTED_FUNC,
	MQTT_DISCONNECTED_CB, SMART_CONFIG,
	PROCESS_CB, PROCESS_FUNC, DS18B20_CB, WIFI_CONNECT_CHANGE=100
} lastAction __attribute__ ((section (".noinit")));
enum lastAction_t savedLastAction;

typedef struct { MQTT_Client *mqttClient; char *topic; char *data; } mqttData_t;
#define QUEUE_SIZE 20
static os_event_t taskQueue[QUEUE_SIZE];
#define EVENT_MQTT_CONNECTED 1
#define EVENT_MQTT_DATA 2
#define EVENT_PROCESS_TIMER 3
#define EVENT_TRANSMIT 4

MQTT_Client mqttClient;
bool setupMode = false;
static char bestSSID[33];

void user_rf_pre_init(void);

void user_rf_pre_init(void) {}

bool ICACHE_FLASH_ATTR mqttIsConnected(void) {
	return mqttConnected;
}

void ICACHE_FLASH_ATTR stopConnection(void) {
	MQTT_Disconnect(&mqttClient);
}

void ICACHE_FLASH_ATTR startConnection(void) {
	MQTT_Connect(&mqttClient);
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

static void ICACHE_FLASH_ATTR publishData(void) {
	if (mqttConnected) {
		char *topic = (char *) os_zalloc(100);
		char *data = (char *) os_zalloc(100);
		if (topic == NULL || data == NULL) {
			ERRORP("malloc err %s/%s\n", topic, data);
			startFlash(-1, 50, 50); // fast
			return;
		}
		os_sprintf(topic, (const char*) "/Raw/%s/0/info", sysCfg.device_id);
		os_sprintf(data, (const char*) "{ \"Type\":\"Level\", \"Value\":%d}", system_adc_read());
		MQTT_Publish(&mqttClient, topic, data, strlen(data), 0, false);
		TESTP("%s=>%s\n", topic, data);
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR _publishDeviceInfo(void) {
	publishDeviceInfo(version, "RF433", wifiChannel, WIFI_Attempts(), getBestSSID(), 0);
}

static void ICACHE_FLASH_ATTR transmitCb(uint32_t args) { // Depends on Update period
	if (!checkSmartConfig(SC_CHECK)) {
		if (!system_os_post(USER_TASK_PRIO_1, EVENT_TRANSMIT, 0))
			ERRORP("Can't post EVENT_TRANSMIT\n");
	}
}

static void ICACHE_FLASH_ATTR transmitFunc(void) {
	if (!checkSmartConfig(SC_CHECK)) {
		publishData();
		checkMinHeap();
	}
}

static void ICACHE_FLASH_ATTR printAll() {
	os_printf("Test\n");
}

static void ICACHE_FLASH_ATTR switchAction(int action) {
	static bool socketOn = false;
	startFlash(action, 50, 100);
	switch (action) {
	case 1:
		printAll();
		os_printf("minHeap: %d\n", checkMinHeap());
		break;
	case 2:
		setGate(1);
		break;
	case 3:
		setSocket(3, socketOn = !socketOn);
		break;
	case 4:
		if (!checkSmartConfig(SC_CHECK))
			toggleHttpSetupMode();
		break;
	case 5:
		checkSmartConfig(SC_TOGGLE);
		break;
	}
}

static void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status) {
	os_printf("WiFi status: %d\r\n", status);
	if (status == STATION_GOT_IP) {
		MQTT_Connect(&mqttClient);
		tcp_listen(80);
		wifiChannel = wifi_get_channel();
	} else {
		mqttConnected = false;
		MQTT_Disconnect(&mqttClient);
	}
}

static void ICACHE_FLASH_ATTR dateTimerCb(void) {
	os_printf("Nothing heard so restarting...\n");
	system_restart();
}

static void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t *args) {
	if (!system_os_post(USER_TASK_PRIO_1, EVENT_MQTT_CONNECTED, (os_param_t) args))
		ERRORP("Can't post EVENT_MQTT_CONNECTED\n");
}

static void ICACHE_FLASH_ATTR mqttConnectedFunction(MQTT_Client *client) {
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


	MQTT_Subscribe(client, "/App/date", 0);
	MQTT_Subscribe(client, "/App/refresh", 0);

	if (mqttConnected) { // Has REconnected
		_publishDeviceInfo();
		reconnections++;
		publishError(51, reconnections);
	} else {
		publishDeviceReset(version, lastAction);
		_publishDeviceInfo();
		publishMapping();

		os_timer_disarm(&date_timer);
		os_timer_setfn(&date_timer, (os_timer_func_t *) dateTimerCb, NULL);
		os_timer_arm(&date_timer, 10 * 60 * 1000, false); //10 minutes

		os_timer_disarm(&transmit_timer);
		os_timer_setfn(&transmit_timer, (os_timer_func_t *) transmitCb, (void *) &mqttClient);
		os_timer_arm(&transmit_timer, sysCfgUpdates() * 1000, true);
	}
	checkMinHeap();
	os_free(topic);
	easygpio_outputSet(LED, 0); // Turn LED off when connected
	lastAction = MQTT_CONNECTED_FUNC;
	mqttConnected = true;
	checkTimeFunc("mqttConnectedFunc", t);
}

static void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args) {
//	MQTT_Client* client = (MQTT_Client*)args;
	os_printf("MQTT Disconnected\n");
	mqttConnected = false;
	if (!checkSmartConfig(SC_CHECK)) {
		MQTT_Connect(&mqttClient);
	}
}

static void ICACHE_FLASH_ATTR mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len,
		const char *data, uint32_t data_len) {
	char *topicBuf = (char*) os_malloc(topic_len + 1), *dataBuf = (char*) os_malloc(data_len + 1);
	if (topicBuf == NULL || dataBuf == NULL) {
		TESTP("malloc error %x %x\n", topicBuf, dataBuf);
		startFlash(-1, 50, 50); // fast
		return;
	}

	mqttData_t *params = (mqttData_t *)os_malloc(sizeof(mqttData_t));
	os_memcpy(topicBuf, topic, topic_len);
	topicBuf[topic_len] = 0;
	os_memcpy(dataBuf, data, data_len);
	dataBuf[data_len] = 0;

	INFOP("Receive topic: %s, data: %s\n", topicBuf, dataBuf);
	params->mqttClient = (MQTT_Client*) args;
	params->topic = topicBuf;
	params->data = dataBuf;
	if (!system_os_post(USER_TASK_PRIO_1, EVENT_MQTT_DATA, (os_param_t) params))
		ERRORP("Can't post EVENT_MQTT_DATA\n");
	lastAction = MQTT_DATA_CB;
}

static void ICACHE_FLASH_ATTR mqttDataFunction(MQTT_Client *client, char* topic, char *data) {
	uint32 t = system_get_time();

	lastAction = MQTT_DATA_FUNC;
	TESTP("mqd topic %s; data %s\n", topic, data);

	decodeMessage(client, topic, data);
	checkMinHeap();
	os_free(topic);
	os_free(data);
	checkTimeFunc("mqttDataFunc", t);
}

static void ICACHE_FLASH_ATTR showSysInfo() {
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
	INFOP("Background task %d; minHeap: %d\n", e->sig, checkMinHeap());
	switch (e->sig) {
	case EVENT_MQTT_CONNECTED:
		mqttConnectedFunction((MQTT_Client *) e->par);
		break;
	case EVENT_MQTT_DATA:
		mqttData = (mqttData_t *) e->par;
		mqttDataFunction(mqttData->mqttClient, mqttData->topic, mqttData->data);
		os_free(mqttData);
		break;
	case EVENT_TRANSMIT:
		transmitFunc();
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

	INFO(showSysInfo());
	if (strncmp(bestSSID, sysCfg.sta_ssid, 5) != 0) { // Dissimilar SSID
		strcpy(bestSSID, sysCfg.sta_ssid); // Use stored SSID; nb assumes same password
	}

	if ( !system_os_task(backgroundTask, USER_TASK_PRIO_1, taskQueue, QUEUE_SIZE))
		TESTP("Can't set up background task\n");
	WIFI_Connect(bestSSID, sysCfg.sta_pwd, sysCfg.deviceName, wifiConnectCb);

	initSwitch(switchAction);
	publishInit(&mqttClient);

	lastAction = INIT_DONE;
}

static void ICACHE_FLASH_ATTR initDone_cb() {
	INFOP("Start WiFi Scan\n");
	initWiFi(startUp);
}

void ICACHE_FLASH_ATTR user_init(void) {
	stdout_init();
	gpio_init();

	easygpio_pinMode(SWITCH, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_outputDisable(SWITCH);
	easygpio_pinMode(LED, EASYGPIO_PULLUP, EASYGPIO_OUTPUT);
	easygpio_outputSet(LED, 1);
	easygpio_pinMode(RF433_TX, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputSet(RF433_TX, 0);
	savedLastAction = lastAction;
	system_init_done_cb(&initDone_cb);
}
