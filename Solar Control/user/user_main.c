#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "stdout.h"
#include <ds18b20.h>
#include <dtoa.h>

#include "easygpio.h"
#include "config.h"
#include "wifi.h"
#include "debug.h"
#include "mqtt.h"
#include "smartconfig.h"
#include "user_config.h"
#include "version.h"
#include "time.h"
#include "overrideIO.h"
#include "flowMonitor.h"
#include "temperature.h"
#include "switch.h"
#include "check.h"
#include "pump.h"
#include "decodeMessage.h"
#include "user_main.h"

os_timer_t transmit_timer;
os_timer_t date_timer;
static os_timer_t process_timer;

MQTT_Client mqttClient;
static uint8 mqttConnected;

// Stuff for background process
typedef struct { MQTT_Client *mqttClient; char *topic; char *data; } mqttData_t;
#define QUEUE_SIZE 20
os_event_t taskQueue[QUEUE_SIZE];
#define EVENT_MQTT_CONNECTED 1
#define EVENT_MQTT_DATA 2
#define EVENT_PROCESS_TIMER 3
#define EVENT_TRANSMIT 4

static uint16 pressure = 0;
static uint8 wifiChannel = 255;

enum lastAction_t {
	IDLE, RESTART, FLASH, IPSCAN, SWITCH_SCAN,
	INIT_DONE, MQTT_DATA_CB, MQTT_DATA_FUNC, MQTT_CONNECTED_CB, MQTT_CONNECTED_FUNC,
	MQTT_DISCONNECTED_CB, SMART_CONFIG,
	PROCESS_CB, PROCESS_FUNC, DS18B20_CB, WIFI_CONNECT_CHANGE=100
} lastAction __attribute__ ((section (".noinit")));
enum lastAction_t savedLastAction;
enum SmartConfigAction {
	SC_CHECK, SC_HAS_STOPPED, SC_TOGGLE
};

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

static void ICACHE_FLASH_ATTR publishSensorData(MQTT_Client* client) {
	if (mqttConnected) {
		char s[20];
		char *topic = (char *) os_zalloc(100);
		char *data = (char *) os_zalloc(100);
		if (topic == NULL || data == NULL) {
			TESTP("malloc err %s/%s\n", topic, data);
			startFlash(-1, 50, 50); // fast
			return;
		}

		os_sprintf(topic, (const char*) "/Raw/%s/4/info", sysCfg.device_id);
		os_sprintf(data, (const char*) "{ \"Type\":\"Flow\", \"Value\":%d}", flowInLitresPerHour());
		MQTT_Publish(client, topic, data, strlen(data), 0, false);
		INFOP("%s=>%s\n", topic, data);

		os_sprintf(topic, (const char*) "/Raw/%s/5/info", sysCfg.device_id);
		os_sprintf(data, (const char*) "{ \"Type\":\"Pressure\", \"Value\":%d}", pressure);
		MQTT_Publish(client, topic, data, strlen(data), 0, false);
		INFOP("%s=>%s\n", topic, data);

		os_sprintf(topic, (const char*) "/Raw/%s/6/info", sysCfg.device_id);
		os_sprintf(data, (const char*) "{ \"Type\":\"Energy\", \"Value\":%s}",
				dtoStr(energyReading(), 10, 3, s));
		MQTT_Publish(client, topic, data, strlen(data), 0, false);
		INFOP("%s=>%s\n", topic, data);

		resetFlowReadings();

		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR publishData(MQTT_Client* client) {
	uint32 t = system_get_time();
	if (mqttConnected) {
		publishAllTemperatures();
		publishSensorData(client);
		publishOutput(OP_PUMP, outputState(OP_PUMP));
	}
	checkTime("publishData", t);
}

void ICACHE_FLASH_ATTR _publishDeviceInfo(void) {
	publishDeviceInfo(version, "Solar Control", wifiChannel, WIFI_Attempts(), getBestSSID(), 0);
}

static void ICACHE_FLASH_ATTR transmitCb(uint32_t args) { // Depends on Update period
	if (!checkSmartConfig(SC_CHECK)) {
		if (!system_os_post(USER_TASK_PRIO_1, EVENT_TRANSMIT, 0))
			ERRORP("Can't post EVENT_TRANSMIT\n");
	}
}

static void ICACHE_FLASH_ATTR switchAction(int action) {
	uint8 idx;

	if (switchInConfigMode()) {
		startFlash(action, 50, 100);
		switch (action) {
		case 1:
			for (idx=0; idx<MAX_OUTPUT; idx++)
				printOutput(idx);
			os_printf("\n");
			for (idx=0; idx<MAP_TEMP_SIZE; idx++) {
				printMappedTemperature(idx);
				os_printf("\n");
			}
			os_printf("minHeap: %d\n", checkMinHeap());
			break;
		case 2:
			break;
		case 3:
			if (!checkSmartConfig(SC_CHECK)) {
				toggleHttpSetupMode();
			}
			break;
		case 4:
			break;
		case 5:
			checkSmartConfig(SC_TOGGLE);
			break;
		}
	} else { // switch in Normal Mode
		TESTP("Action %d\n", action);
		switch (action) {
		case 1:
			break;
		case 2:
			stopPumpOverride();
			break;
		case 3:
			startPumpOverride();
			break;
		case 4:
			turnOffOverride();
			break;
		case 5:
			break;
		}
	}
}

static void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status) {
	TESTP("WiFi status: %d\n", status);
	if (status == STATION_GOT_IP) {
		MQTT_Connect(&mqttClient);
		wifiChannel = wifi_get_channel();
	} else {
		lastAction = WIFI_CONNECT_CHANGE+status;
		mqttConnected = false;
		MQTT_Disconnect(&mqttClient);
	}
}

static void ICACHE_FLASH_ATTR dateTimerCb(void) {
	os_printf("Nothing heard so restarting...\n");
	lastAction = RESTART;
	system_restart();
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

static void ICACHE_FLASH_ATTR readPressure(void) {
	pressure =  system_adc_read();
}

static void ICACHE_FLASH_ATTR processTemperatureCb(void) {
	if (!system_os_post(USER_TASK_PRIO_1, EVENT_PROCESS_TIMER, 0))
		ERRORP("Can't post EVENT_MQTT_CONNECTED\n");
}

static void ICACHE_FLASH_ATTR processTimerCb(void) { // PROCESS_REPEAT - 5 sec
	ds18b20StartScan(processTemperatureCb);
}

static void ICACHE_FLASH_ATTR processTimerFunc(void) {
	uint32 t = system_get_time();
	readPressure();
	processPump();
	if (switchInConfigMode()) printFlows();
	lastAction = PROCESS_FUNC;
	checkTimeFunc("processTimerFunc", t);
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

	os_sprintf(topic, "/App/+/+/TS Bottom/#");
	INFOP("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	MQTT_Subscribe(client, "/App/date", 0);
	MQTT_Subscribe(client, "/App/refresh", 0);
	MQTT_Subscribe(client, "/App/Cloud/hourly", 0);
	MQTT_Subscribe(client, "/App/Sun/+", 0);

	MQTT_Publish(client, "/App/refresh", "", 0, 0, false); // To get TS_BOTTOM

	if (mqttConnected) { // Has REconnected
		_publishDeviceInfo();
		reconnections++;
		publishError(51, reconnections);
	} else {
		publishDeviceReset(version, lastAction);
		_publishDeviceInfo();
		publishMapping();
		initFlowMonitor();
		initTemperature();

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
	lastAction = MQTT_DISCONNECTED_CB;
	mqttConnected = false;
	if (!checkSmartConfig(SC_CHECK)) {
		MQTT_Connect(&mqttClient);
	}
}

static size_t ICACHE_FLASH_ATTR fs_size() { // returns the flash chip's size, in BYTES
	uint32_t id = spi_flash_get_id();
	uint8_t mfgr_id = id & 0xff;
	uint8_t type_id = (id >> 8) & 0xff; // not relevant for size calculation
	uint8_t size_id = (id >> 16) & 0xff; // lucky for us, WinBond ID's their chips as a form that lets us calculate the size
	if (mfgr_id != 0xEF) // 0xEF is WinBond; that's all we care about (for now)
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
		publishData(&mqttClient);
		break;
	}
}

static void ICACHE_FLASH_ATTR startUp() {
	CFG_Load();
	INFO(CFG_print());
	os_printf("\n%s ( %s ) starting ...\n", sysCfg.deviceName, version);
	TESTP("wifi_get_phy_mode = %d\n", wifi_get_phy_mode());

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

	os_timer_disarm(&process_timer);
	os_timer_setfn(&process_timer, (os_timer_func_t *) processTimerCb, NULL);
	os_timer_arm(&process_timer, PROCESS_REPEAT, true);

	if ( !system_os_task(backgroundTask, USER_TASK_PRIO_1, taskQueue, QUEUE_SIZE))
		TESTP("Can't set up background task\n");

	initSwitch(switchAction);
	initPump();
	publishInit(&mqttClient);
	lastAction = INIT_DONE;
	INFO(showSysInfo());
}

static void ICACHE_FLASH_ATTR initDone_cb() {
	INFOP("Start WiFi Scan\n");
	initWiFi(startUp);
}

void ICACHE_FLASH_ATTR user_init(void) {
	stdout_init();
	gpio_init();
	initIO();
	savedLastAction = lastAction;
	system_init_done_cb(&initDone_cb);
}
