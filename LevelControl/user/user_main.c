
#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "gpio.h"
#include "easygpio.h"
#include "stdout.h"

#include "config.h"
#include "wifi.h"
#include "debug.h"
#include "mqtt.h"
#include "user_config.h"
#include "smartconfig.h"
#include "doSmartConfig.h"
#include "version.h"
#include "flowMonitor.h"
#include "switch.h"
#include "flash.h"
#include "check.h"
#include "http.h"
#include "decodeMessage.h"

static os_timer_t process_timer;
os_timer_t transmit_timer;
os_timer_t date_timer;

MQTT_Client mqttClient;
uint8 mqttConnected;

typedef struct { MQTT_Client *mqttClient; char *topic; char *data; } mqttData_t;
#define QUEUE_SIZE 20
os_event_t taskQueue[QUEUE_SIZE];
enum backgroundEvent_t {
	EVENT_MQTT_CONNECTED = 1,
	EVENT_MQTT_DATA,
	EVENT_PROCESS_TIMER,
	EVENT_TRANSMIT,
	EVENT_TEMPERATURE
};

uint16 currentPressure;
enum {TANK_UNKNOWN, TANK_LOW, TANK_OK} tankStatus = TANK_UNKNOWN;
enum {PUMP_AUTO, PUMP_OFF, PUMP_ON} pumpOverride = PUMP_AUTO;
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
static int pumpOnCount = 0;

void user_rf_pre_init(void);
void user_rf_pre_init(void){}

bool ICACHE_FLASH_ATTR mqttIsConnected(void) {
	return mqttConnected;
}

void ICACHE_FLASH_ATTR stopConnection(void) {
	MQTT_Disconnect(&mqttClient);
}

void ICACHE_FLASH_ATTR startConnection(void) {
	MQTT_Connect(&mqttClient);
}

void ICACHE_FLASH_ATTR setTankStatus(char *data) {
	if (os_strcmp("low", data) == 0) {
		tankStatus = TANK_LOW;
	} else if (os_strcmp("OK", data) == 0 || strcmp("high", data) == 0 ) {
		tankStatus = TANK_OK;
	} else {
		tankStatus = TANK_UNKNOWN;
	}
}

static void ICACHE_FLASH_ATTR processCb(uint32_t *args) {
	if (!system_os_post(USER_TASK_PRIO_1, EVENT_PROCESS_TIMER, (os_param_t) args))
		ERRORP("Can't post EVENT_PROCESS_TIMER\n");
}

void ICACHE_FLASH_ATTR processFunc(void) { // Every 100mS
	lastAction = PROCESS_FUNC;
	uint16 newPressure = system_adc_read();
	currentPressure = (currentPressure * 9 + newPressure)/10; // Average over 1 second
	switch (pumpOverride) {
	case PUMP_AUTO:
		if (currentPressure < sysCfg.settings[SET_PUMP_ON]) {
			easygpio_outputSet(PUMP, 1);
			easygpio_outputSet(LED, 1);
			pumpOnCount++;
			if (pumpOnCount >= MAX_PUMP_ON_WARNING && currentPressure < sysCfg.settings[SET_LOW_PRESSURE_WARNING])
				publishError(3, currentPressure);
			if (pumpOnCount == MAX_PUMP_ON_WARNING) { // 1 minute
				publishError(1, pumpOnCount);
			} else if (pumpOnCount == MAX_PUMP_ON_ERROR) { // 5 minutes
				publishError(2, pumpOnCount);
			} else if (pumpOnCount > MAX_PUMP_ON_ERROR) {
				pumpOnCount = MAX_PUMP_ON_ERROR+1;
			}
		} else if (currentPressure > sysCfg.settings[SET_PUMP_OFF]) {
			easygpio_outputSet(PUMP, 0);
			easygpio_outputSet(LED, 0);
			pumpOnCount = 0;
		}
		break;
	case PUMP_OFF:
		easygpio_outputSet(PUMP, 0);
		easygpio_outputSet(LED, 0);
		pumpOnCount = -1;
		break;
	case PUMP_ON:
		easygpio_outputSet(PUMP, 1);
		easygpio_outputSet(LED, 1);
		pumpOnCount++;
		break;
	}
}

void ICACHE_FLASH_ATTR _publishDeviceInfo(void) {
	static uint32 lastSaved = 0xffffffff;
	if (CFG_lastSaved() != lastSaved) {
		publishDeviceInfo(version, "RW_PUMP", wifiChannel, WIFI_Attempts(), getBestSSID(), (vcc =
				system_adc_read()));
		lastSaved = CFG_lastSaved();
	}
}

static void ICACHE_FLASH_ATTR dateTimerCb(void) { // 10 mins
	os_printf("Nothing heard so restarting...\n");
	lastAction = RESTART;
	system_restart();
}

void ICACHE_FLASH_ATTR publishData() {
	if (mqttConnected) {
		char *topic = (char *) os_zalloc(100);
		char *data = (char *) os_zalloc(100);
		uint32 flowCountPerLitre = sysCfg.settings[SET_FLOW_COUNT_PER_LITRE]; // To convert to mL

		os_printf("%s=>%s\n", topic, data);
		os_sprintf(topic, (const char*) "/Raw/%s/0/info", sysCfg.device_id);
		os_sprintf(data, (const char*) "{ \"Type\":\"Pressure\", \"Value\":%d}", currentPressure);
		MQTT_Publish(&mqttClient, topic, data, strlen(data), 0, false);
		TESTP("%s=>%s\n", topic, data);

		os_sprintf(topic, (const char*) "/Raw/%s/1/info", sysCfg.device_id);
		os_sprintf(data, (const char*) "{ \"Type\":\"Pump\", \"Value\":%d}", pumpOnCount);
		MQTT_Publish(&mqttClient, topic, data, strlen(data), 0, false);
		TESTP("%s=>%s\n", topic, data);

		os_sprintf(topic, (const char*) "/Raw/%s/2/info", sysCfg.device_id);
		os_sprintf(data, (const char*) "{ \"Type\":\"FlowMax\", \"Value\":%d}", flowMaxReading());
		MQTT_Publish(&mqttClient, topic, data, strlen(data), 0, false);
		TESTP("%s=>%s\n", topic, data);

		os_sprintf(topic, (const char*) "/Raw/%s/3/info", sysCfg.device_id);
		os_sprintf(data, (const char*) "{ \"Type\":\"FlowTimes\", \"Value\":%d}", flowTimesReading());
		MQTT_Publish(&mqttClient, topic, data, strlen(data), 0, false);
		TESTP("%s=>%s\n", topic, data);

		os_sprintf(topic, (const char*) "/Raw/%s/4/info", sysCfg.device_id);
		os_sprintf(data, (const char*) "{ \"Type\":\"Flow\", \"Value\":%d}", flowPerReading());
		MQTT_Publish(&mqttClient, topic, data, strlen(data), 0, false);
		TESTP("%s=>%s\n", topic, data);

		resetFlowReadings();

		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

static void ICACHE_FLASH_ATTR transmitTimerCb(uint32_t *args) {
	if (!system_os_post(USER_TASK_PRIO_1, EVENT_MQTT_CONNECTED, (os_param_t) args))
		ERRORP("Can't post EVENT_MQTT_CONNECTED\n");
}

void ICACHE_FLASH_ATTR transmitTimerFunc(void) {
	publishData();
}

void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args) {
	MQTT_Connect(&mqttClient);
	mqttConnected = false;
	os_timer_disarm(&process_timer);
	lastAction = MQTT_DISCONNECTED_CB;
}

void ICACHE_FLASH_ATTR checkPumpOverride(void) {
	switch (pumpOverride) {
	case PUMP_AUTO:
		pumpOverride = PUMP_OFF;
		startFlash(-1, 200, 1800);
		break;
	case PUMP_OFF:
		pumpOverride = PUMP_ON;
		startFlash(-1, 1800, 200);
		break;
	case PUMP_ON:
		pumpOverride = PUMP_AUTO;
		stopFlash();
		break;
	}
	TESTP("PO=%d\n", pumpOverride);
}

void ICACHE_FLASH_ATTR switchAction(int action) {
	switch (action) {
	case 1:
		if (!checkSmartConfig(SC_CHECK))
			checkPumpOverride();
		break;
	case 2:
		break;
	case 3:
		publishData(&mqttClient);
		break;
	case 4:
		if (!checkSmartConfig(SC_CHECK))
			toggleHttpSetupMode();
		break;
	case 5:
		checkSmartConfig(SC_TOGGLE);
		break;
	case 6:
		break;
	}
}

static void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status) {
	os_printf("WiFi status: %d\n", status);
	if (status == STATION_GOT_IP) {
		wifiChannel = wifi_get_channel();
		MQTT_Connect(&mqttClient);
		tcp_listen(80); // for setting up SSID/PW
	} else {
		os_timer_disarm(&transmit_timer);
		MQTT_Disconnect(&mqttClient);
	}
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
		publishDeviceReset(version, lastAction);
		_publishDeviceInfo();
		// publishMapping();
	}
	char *topic = (char*) os_zalloc(100);
	os_sprintf(topic, "/Raw/%s/set/#", sysCfg.device_id);
	os_printf("Subscribe to: %s\n", topic);
	MQTT_Subscribe(&mqttClient, topic, 0);

	os_sprintf(topic, "/Raw/%s/+/set/#", sysCfg.device_id);
	os_printf("Subscribe to: %s\n", topic);
	MQTT_Subscribe(&mqttClient, topic, 0);

	MQTT_Subscribe(&mqttClient, "/App/rainwater/level", 0);

	MQTT_Subscribe(&mqttClient, "/App/date", 0);

	os_timer_disarm(&date_timer);
	os_timer_setfn(&date_timer, (os_timer_func_t *) dateTimerCb, (void *) 0);
	os_timer_arm(&date_timer, 10 * 60 * 1000, true);

	os_timer_disarm(&transmit_timer);
	os_timer_setfn(&transmit_timer, (os_timer_func_t *)transmitTimerCb, (void *)0);
	os_timer_arm(&transmit_timer, sysCfg.updates*1000, true);

	os_timer_disarm(&process_timer);
	os_timer_setfn(&process_timer, (os_timer_func_t *)processCb, (void *)0);
	os_timer_arm(&process_timer, 100, true); // 100mS

	checkMinHeap();
	os_free(topic);

	easygpio_outputSet(LED, 0); // Turn LED off when connected
	lastAction = MQTT_CONNECTED_FUNC;
	checkTimeFunc("mqttConnectedFunc", t);
}

static void ICACHE_FLASH_ATTR mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len,
		const char *data, uint32_t data_len) {
	uint32 t = system_get_time();
	char *topicBuf = (char*) os_zalloc(topic_len + 1), *dataBuf = (char*) os_zalloc(data_len + 1);

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
	checkTime("mqttDataCb", t);
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
	case EVENT_TRANSMIT:
		transmitTimerFunc();
		break;
	default:
		ERRORP("Bad background task event %d\n", e->sig);
		break;
	}
}

LOCAL void ICACHE_FLASH_ATTR startUp(void) {
	CFG_Load();
	os_printf("\n%s ( %s ) starting ...\n", sysCfg.deviceName, version);

	MQTT_InitConnection(&mqttClient, sysCfg.mqtt_host, sysCfg.mqtt_port, sysCfg.security);
	MQTT_InitClient(&mqttClient, sysCfg.device_id, sysCfg.mqtt_user, sysCfg.mqtt_pass, sysCfg.mqtt_keepalive, 1);

	char str[100];
	os_sprintf(str, "/Raw/%s/offline", sysCfg.device_id);
	MQTT_InitLWT(&mqttClient, str, "offline", 0, 0);

	MQTT_OnConnected(&mqttClient, mqttConnectedCb);
	MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
	MQTT_OnData(&mqttClient, mqttDataCb);

	INFO(showSysInfo());
	if (os_strncmp(getBestSSID(), sysCfg.sta_ssid, 5) != 0) { // Dissimilar SSID
		os_strcpy(getBestSSID(), sysCfg.sta_ssid); // Use stored SSID; nb assumes same password
	}
	WIFI_Connect(getBestSSID(), sysCfg.sta_pwd, sysCfg.deviceName, wifiConnectCb);

	INFO(showSysInfo());
	initFlowMonitor();
	initSwitch(switchAction);

	if ( !system_os_task(backgroundTask, USER_TASK_PRIO_1, taskQueue, QUEUE_SIZE))
		ERRORP("Can't set up background task\n");
	lastAction = INIT_DONE;
}

static void ICACHE_FLASH_ATTR initDone_cb() {
	INFOP("Start WiFi Scan\n");
	initWiFi(startUp);
}

void ICACHE_FLASH_ATTR user_init(void) {
	stdout_init();
	gpio_init();
	savedLastAction = lastAction;

	easygpio_pinMode(LED, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_pinMode(LED2, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_pinMode(PUMP, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);

	easygpio_outputSet(LED, 1);
	easygpio_outputSet(LED2, 0);
	easygpio_outputSet(PUMP, 0);
	system_init_done_cb(&initDone_cb);
}
