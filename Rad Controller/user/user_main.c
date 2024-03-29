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

//#define DEBUG_OVERRIDE
#include "debug.h"
#include "stdout.h"
#include "sysCfg.h"
#include "wifi.h"
#include "easygpio.h"
#include "flash.h"
#include "jsmn.h"
#include "mqtt.h"
#include "publish.h"
#include "switch.h"
#include "io.h"
#include "smartconfig.h"
#include "doSmartConfig.h"
#include "version.h"
#include "user_conf.h"
#include "temperature.h"
#include "time.h"
#include "decodeMessage.h"
#include "overrideTemp.h"
#include "user_main.h"

os_timer_t transmit_timer;
os_timer_t date_timer;

typedef struct {
	MQTT_Client *mqttClient;
	char *topic;
	char *data;
} mqttData_t;
#define QUEUE_SIZE 20
os_event_t taskQueue[QUEUE_SIZE];
enum backgroundEvent_t {
	EVENT_MQTT_CONNECTED = 1,
	EVENT_MQTT_DATA,
	EVENT_PROCESS_TIMER,
	EVENT_TRANSMIT,
	EVENT_PUBLISH_TEMPERATURE,
	EVENT_PUBLISH_DATA
};

#define str(p) #p
#define xstr(s) str(s)
#define CONFIG "Rad Controller"

static uint16 vcc;
static uint8 wifiChannel = 255;
uint8 mqttConnected;
bool httpSetupMode;
enum lastAction_t {
	IDLE,
	RESTART,
	FLASH,
	PROCESS_FUNC,
	SWITCH_SCAN,
	PUBLISH_DATA,
	INIT_DONE,
	MQTT_DATA_CB,
	MQTT_DATA_FUNC,
	MQTT_CONNECTED_CB,
	MQTT_CONNECTED_FUNC,
	MQTT_DISCONNECTED_CB,
	SMART_CONFIG,
	WIFI_CONNECT_CHANGE = 100
} lastAction __attribute__ ((section (".noinit")));
enum lastAction_t savedLastAction;

MQTT_Client mqttClient;

void user_rf_pre_init(void);
uint32 user_rf_cal_sector_set(void);

static void checkCanSleep(void);
static void transmitTimerCb(void);

void user_rf_pre_init(void) {
}

uint32 ICACHE_FLASH_ATTR user_rf_cal_sector_set(void) {
	enum flash_size_map size_map = system_get_flash_size_map();
	uint32 rf_cal_sec = 0;

	switch (size_map) {
	case FLASH_SIZE_4M_MAP_256_256:
		rf_cal_sec = 128 - 5;
		break;
	case FLASH_SIZE_8M_MAP_512_512:
		rf_cal_sec = 256 - 5;
		break;
	case FLASH_SIZE_16M_MAP_512_512:
	case FLASH_SIZE_16M_MAP_1024_1024:
		rf_cal_sec = 512 - 5;
		break;
	case FLASH_SIZE_32M_MAP_512_512:
	case FLASH_SIZE_32M_MAP_1024_1024:
		rf_cal_sec = 1024 - 5;
		break;
	default:
		rf_cal_sec = 0;
		break;
	}
	TESTP("Flash type: %d, size 0x%x\n", size_map, rf_cal_sec);
	return rf_cal_sec;
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

void ICACHE_FLASH_ATTR _publishDeviceInfo(void) {
	static uint32 lastSaved = 0xffffffff;
	if (CFG_lastSaved() != lastSaved) {
		vcc = system_adc_read();
		publishDeviceInfo(version, CONFIG, wifiChannel, WIFI_ConnectTime(), getBestSSID(), vcc);
		lastSaved = CFG_lastSaved();
	}
}

static void ICACHE_FLASH_ATTR processTemperatureCb(void) {
	struct Temperature *t;

	getUnmappedTemperature(0, &t);
	setCurrentTemperature(t->val);
}

void ICACHE_FLASH_ATTR resetTransmitTimer(void) {
	os_timer_disarm(&transmit_timer);
	os_timer_setfn(&transmit_timer, (os_timer_func_t *) transmitTimerCb, NULL);
	os_timer_arm(&transmit_timer, sysCfg.updates * 1000, true); // Repeat
}

static void ICACHE_FLASH_ATTR transmitTimerCb(void) { // Depends on Updates
	if (!system_os_post(USER_TASK_PRIO_1, EVENT_TRANSMIT, 0))
		ERRORP("Can't post EVENT_TRANSMIT\n");
}

static void ICACHE_FLASH_ATTR transmitTimerFunc(void) {
	uint32 t = system_get_time();
	publishData(0);
	ds18b20StartScan(processTemperatureCb);
	lastAction = PROCESS_FUNC;
	checkTime("processTimerFunc", t);
}

static void ICACHE_FLASH_ATTR checkCanSleep(void) {
#ifdef SLEEP_MODE
	if (!checkSmartConfig(SC_CHECK) && !httpSetupMode) {
		MQTT_Disconnect(&mqttClient);
		TESTP("Sleep %dS\n", sysCfg.updates);
		system_deep_sleep_set_option(1); // Do RF calibration on wake up
		system_deep_sleep(sysCfg.updates * 1000 * 1000);
	} else {
		mqttPublishedCb(NULL);
	}
#else
	resetTransmitTimer();
#endif
}

static void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status) {
	os_printf("WiFi status: %d\n", status);
	if (status == STATION_GOT_IP) {
		wifiChannel = wifi_get_channel();
		MQTT_Connect(&mqttClient);
#ifndef ESP01
		tcp_listen(80); // for setting up SSID/PW
#endif
	} else {
		os_timer_disarm(&transmit_timer);
		MQTT_Disconnect(&mqttClient);
	}
}

static void ICACHE_FLASH_ATTR printAll(void) {
	int idx;

	os_printf("Temperature Mappings:\n");
	for (idx = 0; idx < sizeof(sysCfg.mapping); idx++) {
		if (printMappedTemperature(idx))
			os_printf("\n");
	}
	os_printf("Settings: ");
	for (idx = 0; idx < SETTINGS_SIZE; idx++) {
		os_printf("%d=%d ", idx, sysCfg.settings[idx]);
	}
	os_printf("\nOutputs: ");
	for (idx = 0; idx < OUTPUTS; idx++) {
		os_printf("%d=%d ", idx, getOutput(idx));
	}
	os_printf("\n");
}

static void ICACHE_FLASH_ATTR switchAction(int action) {
	static bool printFlag = false;
//	startFlash(action, 50, 100);
	switch (action) {
	case 1:
		if (!easygpio_inputGet(SWITCH_M)) { // Inverted
			nextOverrideAction();
		} else {
			doOverrideAction();
		}
		break;
	case 2:
		if (!easygpio_inputGet(SWITCH_M)) { // Inverted
			nextOverrideValue();
		} else {
			printAll();
			os_printf("minHeap: %d\n", checkMinHeap());
			system_set_os_print((printFlag = !printFlag));
		}
		break;
	case 3:
		if (!checkSmartConfig(SC_CHECK)) {
			if (!toggleHttpSetupMode())
				checkCanSleep();
		}
		break;
	case 5:
		checkSmartConfig(SC_TOGGLE);
		break;
	case 4:
		break;
	}
}

static void ICACHE_FLASH_ATTR publishOutputs(void) { // param for compatibility
	char *topic = (char *) os_zalloc(100);
	char *data = (char *) os_zalloc(100);
	uint8 i;
	if (topic == NULL || data == NULL) {
		startFlash(-1, 50, 50); // fast
		return;
	}
	for (i = 0; i < sysCfg.outputs; i++) {
		os_sprintf(topic, (const char*) "/Raw/%s/%d/info", sysCfg.device_id, i);
		os_sprintf(data, "{ \"Type\":\"Output\", \"Value\":\"%d\"}", getOutput(i));
		MQTT_Publish(&mqttClient, topic, data, strlen(data), 0, false);
		INFOP("%s=>%s\n", topic, data);
	}
	checkMinHeap();
	os_free(topic);
	os_free(data);
	lastAction = PUBLISH_DATA;
}

void ICACHE_FLASH_ATTR publishData(uint32 pass) {
	uint32 t = system_get_time();
	if (mqttConnected) {
		switch (pass) { // Split into multiple passes to minimise hogging
		case 0:
			publishAllTemperatures();
			if (!system_os_post(USER_TASK_PRIO_1, EVENT_PUBLISH_DATA, 1))
				ERRORP("Can't post EVENT_PUBLISH_DATA 1\n")
			;
			break;
		case 1:
			_publishDeviceInfo();
			if (!system_os_post(USER_TASK_PRIO_1, EVENT_PUBLISH_DATA, 2))
				ERRORP("Can't post EVENT_PUBLISH_DATA 2\n")
			;
			break;
		case 2:
			if (!system_os_post(USER_TASK_PRIO_1, EVENT_PUBLISH_DATA, 3))
				ERRORP("Can't post EVENT_PUBLISH_DATA 3\n")
			;
			break;
		case 3:
			publishOutputs();
			break;
		}
	} else {
		ERRORP("mqtt not connected\n");
	}
	checkTime("publishData", t);
}

static void ICACHE_FLASH_ATTR dateTimerCb(void) { // 10 mins
	os_printf("Nothing heard so restarting...\n");
	lastAction = RESTART;
	system_restart();
}

static void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t *args) {
	if (!system_os_post(USER_TASK_PRIO_1, EVENT_MQTT_CONNECTED, (os_param_t) args))
		ERRORP("Can't post EVENT_MQTT_CONNECTED\n");
}

static void ICACHE_FLASH_ATTR mqttConnectedFunction(MQTT_Client *client) {
	uint32 t = system_get_time();
	static int reconnections = 0;

	TESTP("MQTT: Connected to %s:%d\n", sysCfg.mqtt_host, sysCfg.mqtt_port);

	if (mqttConnected) { // Has REconnected
		_publishDeviceInfo();
		reconnections++;
		publishError(51, reconnections);
	} else {
		mqttConnected = true; // To enable messages to be published
		publishDeviceReset(version, savedLastAction);
		_publishDeviceInfo();
		publishMapping();
	}
	char *topic = (char*) os_zalloc(100);
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

	os_timer_disarm(&date_timer);
	os_timer_setfn(&date_timer, (os_timer_func_t *) dateTimerCb, (void *) 0);
	os_timer_arm(&date_timer, 10 * 60 * 1000, true);

	transmitTimerCb(); // Get temperature reading etc started
	ds18b20StartScan(processTemperatureCb);
	checkMinHeap();
	os_free(topic);

	easygpio_outputSet(LED, 0); // Turn LED off when connected
	clearLcd();
	initOverride();
	lastAction = MQTT_CONNECTED_FUNC;
	checkTimeFunc("mqttConnectedFunc", t);
}

static void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*) args;
	mqttConnected = false;
	os_timer_disarm(&transmit_timer);
	TESTP("MQTT disconnected\r\n");
	lastAction = MQTT_DISCONNECTED_CB;
}

static void ICACHE_FLASH_ATTR mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len,
		const char *data, uint32_t data_len) {
	uint32 t = system_get_time();
	char *topicBuf = (char*) os_zalloc(topic_len + 1), *dataBuf = (char*) os_zalloc(data_len + 1);

	mqttData_t *params = (mqttData_t *) os_malloc(sizeof(mqttData_t));
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
	checkTime("mqttDataCb", t);
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

#if DEBUG>=3
static size_t ICACHE_FLASH_ATTR fs_size(void) { // returns the flash chip's size, in BYTES
	uint32_t id = spi_flash_get_id();
	uint8_t mfgr_id = id & 0xff;
	uint8_t type_id = (id >> 8) & 0xff;// not relevant for size calculation
	uint8_t size_id = (id >> 16) & 0xff;// lucky for us, WinBond ID's their chips as a form that lets us calculate the size
	if (mfgr_id != 0xEF)// 0xEF is WinBond; that's all we care about (for now)
	return 0;
	return 1 << size_id;
}

static void ICACHE_FLASH_ATTR showSysInfo(void) {
	INFOP("SDK version is: %s\n", system_get_sdk_version());
	INFOP("Smart-Config version is: %s\n", smartconfig_get_version());
	INFO(system_print_meminfo());
	int sz = fs_size();
	INFOP("Flashsize map %d; id %lx (%lx - %d bytes)\n", system_get_flash_size_map(),
			spi_flash_get_id(), sz, sz / 8);
	INFOP("Boot mode: %d, version %d. Userbin %lx\n", system_get_boot_mode(),
			system_get_boot_version(), system_get_userbin_addr());
}
#endif

static void ICACHE_FLASH_ATTR backgroundTask(os_event_t *e) {
	mqttData_t *mqttData;
	INFOP("Background task %d/%d\n", e->sig, e->par);
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
		transmitTimerFunc();
		break;
	case EVENT_PUBLISH_DATA:
		publishData(e->par);
		break;
	default:
		ERRORP("Bad background task event %d\n", e->sig)
		;
		break;
	}
}

static void ICACHE_FLASH_ATTR startUp() {
	initTemperature();
	ds18b20SearchDevices();

	INFOP("wifi_get_phy_mode = %d\n", wifi_get_phy_mode());

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

	INFO(showSysInfo());
	if (os_strncmp(getBestSSID(), sysCfg.sta_ssid, 5) != 0) { // Dissimilar SSID
		os_strcpy(getBestSSID(), sysCfg.sta_ssid); // Use stored SSID; nb assumes same password
	}
	WIFI_Connect(getBestSSID(), sysCfg.sta_pwd, sysCfg.deviceName, wifiConnectCb);
#ifdef SWITCH
	initSwitch(switchAction);
#endif
	if (!system_os_task(backgroundTask, USER_TASK_PRIO_1, taskQueue, QUEUE_SIZE))
		ERRORP("Can't set up background task\n");
	lastAction = INIT_DONE;
}

static void ICACHE_FLASH_ATTR initDone_cb() {
	char bfr[100];
#ifdef SLEEP_MODE
	CFG_init(100);
#else
	CFG_init(2000);
#endif
	CFG_print();
	os_sprintf(bfr, "%s/%s", sysCfg.deviceLocation, sysCfg.deviceName);
	TESTP("\n%s ( %s ) starting ...\n", bfr, version);
#if DEBUG>=3
	showSysInfo();
#endif
	initWiFi(PHY_MODE_11B, bfr, sysCfg.sta_ssid, startUp);
}

void ICACHE_FLASH_ATTR user_init(void) {
	system_set_os_print(true);
	stdout_init();
	gpio_init();
	savedLastAction = lastAction;

	easygpio_pinMode(SWITCH_M, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_outputDisable(SWITCH_M);
	easygpio_pinMode(SWITCH, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_outputDisable(SWITCH);
	easygpio_pinMode(LED, EASYGPIO_PULLUP, EASYGPIO_OUTPUT);
	easygpio_outputEnable(LED, 1);
	initOutput();
	lcdInit();
	easygpio_outputSet(LED, 1);
	showString(2, 5, "Rad Control");
	showLargeNumString(1, 1, "??");
	system_init_done_cb(&initDone_cb);
}
