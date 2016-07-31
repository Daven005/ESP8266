//#define DEBUG_OVERRIDE 1

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
#include "mqtt.h"
#include "jsmn.h"
#include "doSmartConfig.h"
#include "smartconfig.h"
#include "version.h"

#include "switch.h"
#include "io.h"
#include "flash.h"
#include "check.h"
#include "dht22.h"
#include "time.h"
#include "user_main.h"

os_timer_t transmit_timer;
static os_timer_t time_timer;
static os_timer_t process_timer;
os_timer_t date_timer;

static uint8 wifiChannel = 255;
static float externalTemperature = 10.0; // default to cool
static bool timeIsON = true; // Default to ON, ie daytime

MQTT_Client mqttClient;
uint8 mqttConnected;

enum lastAction_t {
	IDLE, RESTART, FLASH, IPSCAN, SWITCH_SCAN,
	INIT_DONE, MQTT_DATA_CB, MQTT_DATA_FUNC, MQTT_CONNECTED_CB, MQTT_CONNECTED_FUNC,
	MQTT_DISCONNECTED_CB, SMART_CONFIG,
	PROCESS_TIMER_FUNC, TRANSMIT_FUNC, DS18B20_CB, WIFI_CONNECT_CHANGE=100
} lastAction __attribute__ ((section (".noinit")));
enum lastAction_t savedLastAction;

// Stuff for background process
typedef struct { MQTT_Client *mqttClient; char *topic; char *data; } mqttData_t;
#define QUEUE_SIZE 20
os_event_t taskQueue[QUEUE_SIZE];
#define EVENT_MQTT_CONNECTED 1
#define EVENT_MQTT_DATA 2
#define EVENT_PROCESS_TIMER 3
#define EVENT_TRANSMIT 4

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

void ICACHE_FLASH_ATTR publishSensorData(uint8 sensor, char *type, int info) {
	if (mqttConnected) {
		char *topic = (char *) os_zalloc(100);
		char *data = (char *) os_zalloc(100);
		os_sprintf(topic, (const char*) "/Raw/%s/%d/info", sysCfg.device_id, sensor);
		os_sprintf(data, (const char*) "{ \"Type\":\"%s\", \"Value\":%d}", type, info);
		MQTT_Publish(&mqttClient, topic, data, strlen(data), 0, false);
		TESTP("%s=>%s\n", topic, data);
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR _publishDeviceInfo(void) {
	publishDeviceInfo(version, "MHRV", wifiChannel, WIFI_Attempts(), getBestSSID(), 0);
}

void ICACHE_FLASH_ATTR publishData(void) {
	if (mqttConnected) {
		struct dht_sensor_data* r1 = dhtRead(1);
		struct dht_sensor_data* r2 = dhtRead(2);

		if (r1->success) {
			publishSensorData(SENSOR_TEMPERATURE1, "Temp", (int)r1->temperature);
			publishSensorData(SENSOR_HUMIDITY1, "Hum", (int)r1->humidity);
		} else {
			publishError(1, 1);
		}
		if (r2->success) {
			publishSensorData(SENSOR_TEMPERATURE2, "Temp", (int)r2->temperature);
			publishSensorData(SENSOR_HUMIDITY2, "Hum", (int)r2->humidity);
		} else {
			publishError(1, 2);
		}
	}
}

static void ICACHE_FLASH_ATTR printAll(void) {
	struct dht_sensor_data* r1 = dhtRead(1);
	struct dht_sensor_data* r2 = dhtRead(2);

	printOutputs();
	CFG_printSettings();
	if (r1->success) {
		os_printf("Temp 1: %d", (int)r1->temperature);
		os_printf(" Hum 1: %d\n", (int)r1->humidity);
	} else {
		os_printf("Can't read DHT 1 (type %d, pin %d, error %d)\n", r1->sensorType, r1->pin, r1->error);
	}
	if (r2->success) {
		os_printf("Temp 2: %d", (int)r2->temperature);
		os_printf(" Hum 2: %d\n", (int)r2->humidity);
	} else {
		os_printf("Can't read DHT 2 (type %d, pin %d, error %d)\n", r2->sensorType, r2->pin, r2->error);
	}
}

static bool ICACHE_FLASH_ATTR checkHumidityHigh(void) {
	struct dht_sensor_data* r1 = dhtRead(1);
	struct dht_sensor_data* r2 = dhtRead(2);
	if (r1->success && r1->avgHumidity > sysCfg.settings[SETTING_HUMIDTY1])
		return true;
	if (r2->success && r2->avgHumidity > sysCfg.settings[SETTING_HUMIDTY2])
		return true;
	return false;
}

static bool ICACHE_FLASH_ATTR checkTemperatureHigh(void) {
	struct dht_sensor_data* r1 = dhtRead(1);
	struct dht_sensor_data* r2 = dhtRead(2);
	if (r1->success && r1->avgTemperature > sysCfg.settings[SETTING_TEMPERATURE1]
			&& r1->avgTemperature > externalTemperature) // Will not cool down if external Temperature is higher!
		return true;
	if (r2->success && r2->avgTemperature > sysCfg.settings[SETTING_TEMPERATURE2]
			&& r2->avgTemperature > externalTemperature)
		return true;
	return false;
}

static void ICACHE_FLASH_ATTR processData(void) {
	if (checkPirActive(sysCfg.settings[SETTING_PIR_ACTION])) {
		INFOP("<PIR>");
		setSpeed(FAST);
		setLED(YELLOW);
	} else if (checkHumidityHigh()) {
		INFOP("<humidity>");
		setSpeed(FAST);
		setLED(GREEN);
	} else if (checkTemperatureHigh()) {
		INFOP("<temperature>");
		setSpeed(FAST);
		setLED(RED);
	} else if (timeIsON) {
		INFOP("<TIME>");
		setSpeed(SLOW);
		setLED(DARK);
	} else {
		INFOP("<OFF>");
		setSpeed(OFF);
		setLED(DARK);
	}
}

static void ICACHE_FLASH_ATTR processTimerCb(uint32_t *args) { // Called every 500mS
	if (!system_os_post(USER_TASK_PRIO_1, EVENT_PROCESS_TIMER, 0))
		ERRORP("Can't post EVENT_PROCESS_TIMER\n");
}

static void ICACHE_FLASH_ATTR processTimerFunc(void) {
	uint32 t = system_get_time();
	processData();
	lastAction = PROCESS_TIMER_FUNC;
	checkTimeFunc("processTimerFunc", t);
}

static void ICACHE_FLASH_ATTR transmitCb(uint32_t *args) {
	if (!system_os_post(USER_TASK_PRIO_1, EVENT_TRANSMIT, 0))
		ERRORP("Can't post EVENT_TRANSMIT\n");
}

static void ICACHE_FLASH_ATTR transmitFunc(void) {
	uint32 t = system_get_time();
	publishData();
	lastAction = TRANSMIT_FUNC;
	checkTimeFunc("transmitFunc", t);
}

static void ICACHE_FLASH_ATTR switchAction(int pressCount) {
	startFlash(pressCount, 50, 100);
	switch (pressCount) {
	case 1:
		setPirActive(sysCfg.settings[SETTING_PIR_ACTION]);
		processData();
		break;
	case 2:
		clearPirActive(sysCfg.settings[SETTING_PIR_ACTION]);
		processData();
		break;
	case 3:
		printAll();
		break;
	case 4:
		if (!checkSmartConfig(SC_CHECK)) {
			toggleHttpSetupMode();
		}
		break;
	case 5:
		checkSmartConfig(SC_TOGGLE);
		break;
	}
}

static void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status) {
	os_printf("WiFi status: %d\n", status);
	if (status == STATION_GOT_IP) {
		MQTT_Connect(&mqttClient);
		wifiChannel = wifi_get_channel();
	} else {
		mqttConnected = false;
		MQTT_Disconnect(&mqttClient);
	}
}

static void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args) {
//	MQTT_Client* client = (MQTT_Client*)args;
	MQTT_Connect(&mqttClient);
	mqttConnected = false;
}

static void ICACHE_FLASH_ATTR dateTimerCb(void) {
	os_printf("Nothing heard so restarting...\n");
	system_restart();
}

void ICACHE_FLASH_ATTR setExternalTemperature(char* dataBuf) {
	externalTemperature = atoi(dataBuf);
}

static void ICACHE_FLASH_ATTR timeOnCb(void) {
	ERRORP("time Timeout\n");
	timeIsON = true; // default to ON if no time message being received
	externalTemperature = 0.0; // Assume if date not sent then nor is Temp
}

void ICACHE_FLASH_ATTR setTime(char* dataBuf) {
	time_t t = atoi(dataBuf);
	struct tm* timeInfo = localtime(&t);
//	applyDST(timeInfo);  //Not required and uses too much iRAM
	timeIsON = (sysCfg.settings[SETTING_START_ON] <= timeInfo->tm_hour
			&& timeInfo->tm_hour <= sysCfg.settings[SETTING_FINISH_ON]);
	TESTP("time - %02d:%02d <%02d-%02d> [%d]\n", timeInfo->tm_hour, timeInfo->tm_min,
			sysCfg.settings[SETTING_START_ON], sysCfg.settings[SETTING_FINISH_ON], timeIsON);
	os_timer_disarm(&time_timer);
	os_timer_setfn(&time_timer, (os_timer_func_t *) timeOnCb, NULL);
	os_timer_arm(&time_timer, 6 * 60 * 1000, false);
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
	INFOP("mqd topic %s; data %s\n", topic, data);

	decodeMessage(client, topic, data);
	checkMinHeap();
	os_free(topic);
	os_free(data);
	checkTimeFunc("mqttDataFunc", t);
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
	MQTT_Subscribe(client, "/App/Temp/current", 0);
	MQTT_Subscribe(client, "/App/refresh", 0);

	if (mqttConnected) { // Has REconnected
		_publishDeviceInfo();
		reconnections++;
		publishError(51, reconnections);
	} else {
		mqttConnected = true;
		publishDeviceReset(version, lastAction);
		_publishDeviceInfo();
		publishMapping();
		dhtInit(1, sysCfg.settings[SETTING_DHT1], PIN_DHT1, 2000);
		dhtInit(2, sysCfg.settings[SETTING_DHT2], PIN_DHT2, 2000);

		os_timer_disarm(&date_timer);
		os_timer_setfn(&date_timer, (os_timer_func_t *) dateTimerCb, NULL);
		os_timer_arm(&date_timer, 10 * 60 * 1000, false); //10 minutes

		os_timer_disarm(&transmit_timer);
		os_timer_setfn(&transmit_timer, (os_timer_func_t *) transmitCb, (void *) &mqttClient);
		os_timer_arm(&transmit_timer, sysCfgUpdates() * 1000, true);
	}
	checkMinHeap();
	os_free(topic);
	easygpio_outputSet(LED2, 0); // Turn LED off when connected
	lastAction = MQTT_CONNECTED_FUNC;
	checkTimeFunc("mqttConnectedFunc", t);
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
	case EVENT_PROCESS_TIMER:
		processTimerFunc();
		break;
	case EVENT_TRANSMIT:
		transmitFunc();
		break;
	}
}

static void ICACHE_FLASH_ATTR startUp() {
	CFG_Load();
	CFG_print();

	MQTT_InitConnection(&mqttClient, sysCfg.mqtt_host, sysCfg.mqtt_port, sysCfg.security);
	MQTT_InitClient(&mqttClient, sysCfg.device_id, sysCfg.mqtt_user, sysCfg.mqtt_pass,
			sysCfg.mqtt_keepalive, 1);

	char temp[100];
	os_sprintf(temp, "/Raw/%s/offline", sysCfg.device_id);
	MQTT_InitLWT(&mqttClient, temp, "offline", 0, 0);

	MQTT_OnConnected(&mqttClient, mqttConnectedCb);
	MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
	MQTT_OnData(&mqttClient, mqttDataCb);

	WIFI_Connect(getBestSSID(), sysCfg.sta_pwd, sysCfg.deviceName, wifiConnectCb);

	if ( !system_os_task(backgroundTask, USER_TASK_PRIO_1, taskQueue, QUEUE_SIZE))
		TESTP("Can't set up background task\n");

	os_timer_disarm(&process_timer);
	os_timer_setfn(&process_timer, (os_timer_func_t *) processTimerCb, NULL);
	os_timer_arm(&process_timer, 500, true); // repeat every 500mS

	initSwitch(switchAction);
	initPublish(&mqttClient);
	lastAction = INIT_DONE;
}

static void ICACHE_FLASH_ATTR initDone_cb() {
	INFOP("Start WiFi Scan\n");
	initWiFi(startUp);
}

void ICACHE_FLASH_ATTR user_init(void) {
	stdout_init();
	gpio_init();
	initOutputs();
	initInputs();
	savedLastAction = lastAction;
	system_init_done_cb(&initDone_cb);
}
