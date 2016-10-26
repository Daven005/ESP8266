/*
 * user_main.c
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
#include "jsmn.h"
#include "smartconfig.h"
#include "doSmartConfig.h"
#include "io.h"
#include "flash.h"
#include "switch.h"
#include "check.h"
#include "publish.h"
#include "http.h"
#include "wifi.h"
#include "version.h"
#include "temperature.h"
#include "time.h"
#include "BoilerControl.h"
#include "user_main.h"

static os_timer_t process_timer;
static os_timer_t time_timer;
os_timer_t transmit_timer;
os_timer_t date_timer;

typedef struct {
	MQTT_Client *mqttClient;
	char *topic;
	char *data;
} mqttData_t;
#define QUEUE_SIZE 40
os_event_t taskQueue[QUEUE_SIZE];
#define EVENT_MQTT_CONNECTED 1
#define EVENT_MQTT_DATA 2
#define EVENT_PROCESS_TIMER 3
#define EVENT_TRANSMIT 4

uint8 mqttConnected;
enum lastAction_t {
	IDLE,
	RESTART,
	FLASH,
	PROCESS_FUNC,
	SWITCH_SCAN,
	INIT_DONE,
	MQTT_DATA_CB,
	MQTT_DATA_FUNC,
	MQTT_CONNECTED_CB,
	MQTT_DISCONNECTED_CB,
	SMART_CONFIG,
	WIFI_CONNECT_CHANGE = 100
} lastAction __attribute__ ((section (".noinit")));
enum lastAction_t savedLastAction;

MQTT_Client mqttClient;
static uint8 wifiChannel = 255;

void user_rf_pre_init(void);

void user_rf_pre_init(void) {
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

static void ICACHE_FLASH_ATTR publishAllInputs(MQTT_Client* client) {
	uint8 idx;
	for (idx = 0; idx < INPUTS && idx < sysCfg.inputs; idx++) {
		publishInput(idx, input(idx));
	}
}

static void ICACHE_FLASH_ATTR publishAllOutputs(MQTT_Client* client) {
	uint8 idx;
	for (idx = 0; idx < OUTPUTS && idx < sysCfg.outputs; idx++) {
		publishOutput(idx, outputState(idx));
	}
}

void ICACHE_FLASH_ATTR _publishDeviceInfo(void) {
	publishDeviceInfo(version, "Boiler Control", wifiChannel, WIFI_ConnectTime(), getBestSSID(), 0);
}

static void ICACHE_FLASH_ATTR transmitCb(uint32_t args) { // Depends on Update period
	uint32 t = system_get_time();
	if (!checkSmartConfig(SC_CHECK)) {
		if (!system_os_post(USER_TASK_PRIO_1, EVENT_TRANSMIT, 0)) // 0 indicates first pass at publishData
			ERRORP("Can't post EVENT_TRANSMIT\n");
	}
	checkTime("transmitCb", t);
}

void ICACHE_FLASH_ATTR resetTransmitTimer(void) {
	os_timer_disarm(&transmit_timer);
	os_timer_setfn(&transmit_timer, (os_timer_func_t *) transmitCb, NULL);
	os_timer_arm(&transmit_timer, sysCfg.updates * 1000, true); // Repeat
}

void ICACHE_FLASH_ATTR publishData(uint32 pass) {
	uint32 t = system_get_time();
	if (mqttConnected) {
		switch (pass) { // Split into multiple passes to minimise hogging
		case 0:
			publishAllTemperatures();
			if (!system_os_post(USER_TASK_PRIO_1, EVENT_TRANSMIT, 1))
				ERRORP("Can't post EVENT_TRANSMIT 1\n");
			break;
		case 1:
			publishAllInputs(&mqttClient);
			if (!system_os_post(USER_TASK_PRIO_1, EVENT_TRANSMIT, 2))
				ERRORP("Can't post EVENT_TRANSMIT 2\n");
			break;
		case 3:
			publishAllOutputs(&mqttClient);
			break;
		}
	}
	checkTime("publishData", t);
}

static void ICACHE_FLASH_ATTR printAll(void) {
	int idx;
	struct Temperature *t;

	os_printf("Temperature Mappings:\n");
	for (idx = 0; idx < sizeof(sysCfg.mapping); idx++) {
		if (printMappedTemperature(idx))
			os_printf("\n");
	}
	os_printf("\nOutputs: ");
	for (idx = 0; idx < OUTPUTS; idx++) {
		printOutput(idx);
	}
	os_printf("\nInputs: ");
	for (idx = 0; idx < INPUTS; idx++) {
		printInput(idx);
	}
	printIOreg();
	os_printf("\nSettings: ");
	for (idx = 0; idx < SETTINGS_SIZE; idx++) {
		os_printf("%d=%d ", idx, sysCfg.settings[idx]);
	}
	printDHW();
	printBCinfo();
}

static void ICACHE_FLASH_ATTR switchAction(int action) {
	startFlash(action, 50, 100);
	switch (action) {
	case 1:
		if (!checkSmartConfig(SC_CHECK))
			boilerSwitchAction();
		break;
	case 2:
		if (!checkSmartConfig(SC_CHECK))
			toggleHttpSetupMode();
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

static void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*) args;
	mqttConnected = false;
	ERRORP("MQTT disconnected\r\n");
	if (lastAction < WIFI_CONNECT_CHANGE)
		lastAction = MQTT_DISCONNECTED_CB;
}

static void ICACHE_FLASH_ATTR time_cb(void *arg) { // Update every 1 second
	incTime();
}

static void ICACHE_FLASH_ATTR mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len,
		const char *data, uint32_t data_len) {
	if (data == NULL || topic == NULL || topic_len == 0 || data_len == 0) {
		ERRORP("Empty topic/data in mqttDataCb %lx %lx %d %d\n", topic, data, topic_len, data_len);
	} else {
		char *topicBuf = (char*) os_zalloc(topic_len + 1), *dataBuf = (char*) os_zalloc(
				data_len + 1);
		mqttData_t *params = (mqttData_t *) os_malloc(sizeof(mqttData_t));
		if (topicBuf == NULL || dataBuf == NULL || params == NULL) {
			ERRORP("No memory for mqttDataCb %lx %lx %lx\n", topicBuf, dataBuf, params);
			os_delay_us(100);
		} else {
			os_memcpy(topicBuf, topic, topic_len);
			os_memcpy(dataBuf, data, data_len);
			params->mqttClient = (MQTT_Client*) args;
			params->topic = topicBuf;
			params->data = dataBuf;
			if (!system_os_post(USER_TASK_PRIO_1, EVENT_MQTT_DATA, (os_param_t) params)) {
				ERRORP("Can't post EVENT_MQTT_DATA\n");
				os_free(topicBuf);
				os_free(dataBuf);
				os_free(params);
			}
		}
	}
	lastAction = MQTT_DATA_CB;
}

static void ICACHE_FLASH_ATTR mqttDataFunction(MQTT_Client *client, char* topic, char *data) {
	char *tokens[10];
	uint32 t = system_get_time();

	lastAction = MQTT_DATA_FUNC;
	decodeMessage(client, topic, data);
	checkMinHeap();
	os_free(topic);
	os_free(data);
	lastAction = MQTT_DATA_CB;
}

static void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status) {
	TESTP("WiFi status: %d\n", status);
	if (status == STATION_GOT_IP) {
		wifiChannel = wifi_get_channel();
		MQTT_Connect(&mqttClient);
		tcp_listen(80);
	} else {
		lastAction = WIFI_CONNECT_CHANGE + status;
		mqttConnected = false;
		MQTT_Disconnect(&mqttClient);
	}
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

	os_sprintf(topic, "/Raw/%s/+/clear/#", sysCfg.device_id);
	INFOP("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	MQTT_Subscribe(client, "/App/date", 0);
	MQTT_Subscribe(client, "/App/Temp/hourly", 0);
	MQTT_Subscribe(client, "/App/refresh", 0);

	if (mqttConnected) { // Has REconnected
		_publishDeviceInfo();
		reconnections++;
		publishError(51, reconnections);
	} else {
		mqttConnected = true;
		publishDeviceReset(version, savedLastAction);
		_publishDeviceInfo();
		publishMapping();
	}
	checkMinHeap();
	os_free(topic);
	easygpio_outputSet(LED, 0); // Turn LED off when connected
	lastAction = MQTT_CONNECTED_CB;
	mqttConnected = true;
	checkTime("mqttConnectedFunc", t);
}

static void ICACHE_FLASH_ATTR processTimerCb(void) { // 2 sec
	if (!system_os_post(USER_TASK_PRIO_1, EVENT_PROCESS_TIMER, 0))
		ERRORP("Can't post EVENT_MQTT_CONNECTED\n");
}

static void ICACHE_FLASH_ATTR processTimerFunc(void) {
	uint32 t = system_get_time();
	lastAction = PROCESS_FUNC;
	checkInputs(false);
	checkControl();
	checkOutputs();
	ds18b20StartScan(NULL); // For next time
	checkTime("processTimerFunc", t);
}

static void ICACHE_FLASH_ATTR dateTimerCb(void) { // 10 mins
	os_printf("Nothing heard so restarting...\n");
	lastAction = RESTART;
	system_restart();
}

static void ICACHE_FLASH_ATTR initTime(void) {
	struct tm tm;
	tm.tm_year = 2016;
	tm.tm_mon = 10;
	tm.tm_mday = 1;
	tm.tm_hour = 12;
	tm.tm_min = 0;
	tm.tm_sec = 0;
	setTime(mktime(&tm));
}

static void ICACHE_FLASH_ATTR backgroundTask(os_event_t *e) {
	mqttData_t *mqttData;
	INFOP("Background task %d\n", e->sig);
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
		publishData(e->par);
		break;
	}
}

LOCAL void ICACHE_FLASH_ATTR startUp() {
	CFG_Load();
	CFG_print();
	os_printf("\n%s ( %s/%s ) starting ...\n", sysCfg.deviceLocation, sysCfg.deviceName, version);
	MQTT_InitConnection(&mqttClient, sysCfg.mqtt_host, sysCfg.mqtt_port, sysCfg.security);
	MQTT_InitClient(&mqttClient, sysCfg.device_id, sysCfg.mqtt_user, sysCfg.mqtt_pass,
			sysCfg.mqtt_keepalive, 1);
	char temp[100];
	os_sprintf(temp, "/Raw/%s/offline", sysCfg.device_id);
	MQTT_InitLWT(&mqttClient, temp, "offline", 0, 0);

	MQTT_OnConnected(&mqttClient, mqttConnectedCb);
	MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
	MQTT_OnData(&mqttClient, mqttDataCb);
	initPublish(&mqttClient);

	INFOP("SDK version is: %s\n", system_get_sdk_version());
	INFOP("Smart-Config version is: %s\n", smartconfig_get_version());
	INFO(system_print_meminfo());
	INFOP("Flashsize map %d; id %lx\n", system_get_flash_size_map(), spi_flash_get_id());

	if (os_strncmp(getBestSSID(), sysCfg.sta_ssid, 5) != 0) { // Dissimilar SSID
		os_strcpy(getBestSSID(), sysCfg.sta_ssid); // Use stored SSID; nb assumes same password
	}
	WIFI_Connect(getBestSSID(), sysCfg.sta_pwd, sysCfg.deviceName, wifiConnectCb);
	initSwitch(switchAction);

	os_timer_disarm(&time_timer);
	os_timer_setfn(&time_timer, (os_timer_func_t *) time_cb, (void *) 0);
	os_timer_arm(&time_timer, 1000, true);

	os_timer_disarm(&date_timer);
	os_timer_setfn(&date_timer, (os_timer_func_t *) dateTimerCb, (void *) 0);
	os_timer_arm(&date_timer, 10 * 60 * 1000, true);

	os_timer_disarm(&process_timer);
	os_timer_setfn(&process_timer, (os_timer_func_t *) processTimerCb, (void *) 0);
	os_timer_arm(&process_timer, 2000, true);

	os_timer_disarm(&transmit_timer);
	os_timer_setfn(&transmit_timer, (os_timer_func_t *) transmitCb, 0);
	os_timer_arm(&transmit_timer, sysCfg.updates * 1000, true);

	if (!system_os_task(backgroundTask, USER_TASK_PRIO_1, taskQueue, QUEUE_SIZE))
		ERRORP("Can't set up background task\n");

	initTemperature();
	ds18b20StartScan(NULL);
	initBoilerControl();
	lastAction = INIT_DONE;
}

static void ICACHE_FLASH_ATTR initDone_cb() {
	INFOP("Start WiFi Scan\n");
	initWiFi(PHY_MODE_11B, sysCfg.deviceName, sysCfg.sta_ssid, startUp);
}

void ICACHE_FLASH_ATTR user_init(void) {
	stdout_init();
	gpio_init();

	easygpio_pinMode(SWITCH, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_outputDisable(SWITCH);

	easygpio_pinMode(TEST_OP, EASYGPIO_NOPULL, EASYGPIO_INPUT);
	easygpio_outputDisable(TEST_OP);

	easygpio_pinMode(LED, EASYGPIO_PULLUP, EASYGPIO_OUTPUT);
	easygpio_outputSet(LED, 1);

	initIO();
	savedLastAction = lastAction;
	system_init_done_cb(&initDone_cb);
}
