//#define DEBUG_OVERRIDE 1

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
#include "dtoa.h"
#include "user_main.h"

#include "user_conf.h"

os_timer_t transmit_timer;
static os_timer_t time_timer;
static os_timer_t process_timer;
static os_timer_t reconnect_timer;

static uint8 wifiChannel = 255;
static float externalTemperature = 10.0; // default to cool
static bool inWorkingHours = true; // Default to ON, ie daytime

MQTT_Client mqttClient;
uint8 mqttConnected;

enum lastAction_t {
	IDLE,
	RESTART,
	FLASH,
	IPSCAN,
	SWITCH_SCAN,
	INIT_DONE,
	MQTT_DATA_CB,
	MQTT_DATA_FUNC,
	MQTT_CONNECTED_CB,
	MQTT_CONNECTED_FUNC,
	MQTT_DISCONNECTED_CB,
	SMART_CONFIG,
	PROCESS_TIMER_FUNC,
	TRANSMIT_FUNC,
	DS18B20_CB,
	WIFI_CONNECT_CHANGE = 100
} lastAction __attribute__ ((section (".noinit")));
enum lastAction_t savedLastAction;

// Stuff for background process
typedef struct {
	MQTT_Client *mqttClient;
	char *topic;
	char *data;
} mqttData_t;
#define QUEUE_SIZE 20
os_event_t taskQueue[QUEUE_SIZE];
#define EVENT_MQTT_CONNECTED 1
#define EVENT_MQTT_DATA 2
#define EVENT_PROCESS_TIMER 3
#define EVENT_TRANSMIT 4

static void doWiFiConnect(void);
void user_rf_pre_init(void);
uint32 user_rf_cal_sector_set(void);

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

void ICACHE_FLASH_ATTR publishSensorData(uint8 sensor, char *type, char *info) {
	if (mqttConnected) {
		char *topic = (char *) os_zalloc(100);
		char *data = (char *) os_zalloc(100);
		os_sprintf(topic, (const char*) "/Raw/%s/%d/info", sysCfg.device_id, sensor);
		os_sprintf(data, (const char*) "{ \"Type\":\"%s\", \"Value\":%s}", type, info);
		MQTT_Publish(&mqttClient, topic, data, strlen(data), 0, false);
		INFOP("%s=>%s\n", topic, data);
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR _publishDeviceInfo(void) {
	publishDeviceInfo(version, "MHRV", wifiChannel, WIFI_ConnectTime(), getBestSSID(), 0);
}

void ICACHE_FLASH_ATTR publishData(void) {
	char bfr[20];
	if (mqttConnected) {
		struct dht_sensor_data* r1 = dhtRead(1);
		struct dht_sensor_data* r2 = dhtRead(2);

		if (r1->valid) {
			publishSensorData(SENSOR_TEMPERATURE1, "Temp", dtoStr(r1->temperature, 5, 1, bfr));
			publishSensorData(SENSOR_HUMIDITY1, "Hum", dtoStr(r1->humidity, 5, 1, bfr));
		} else {
			publishError(61, r1->error);
		}
		if (r2->valid) {
			publishSensorData(SENSOR_TEMPERATURE2, "Temp", dtoStr(r2->temperature, 5, 1, bfr));
			publishSensorData(SENSOR_HUMIDITY2, "Hum", dtoStr(r2->humidity, 5, 1, bfr));
		} else {
			publishError(62, r2->error);
		}
		publishSensorData(SENSOR_PIR1, "PIR", pirState(PIR1) ? "1" : "0");
		publishSensorData(SENSOR_PIR2, "PIR", pirState(PIR2) ? "1" : "0");
		os_sprintf(bfr, "%d", getSpeed());
		publishSensorData(SENSOR_FAN, "Fan", bfr);
	}
}

static void ICACHE_FLASH_ATTR printAll(void) {
	struct dht_sensor_data* r1 = dhtRead(1);
	struct dht_sensor_data* r2 = dhtRead(2);

	printOutputs();
	CFG_printSettings();
	if (r1->valid) {
		os_printf("Temp 1: %d", (int) r1->temperature);
		os_printf(" Hum 1: %d\n", (int) r1->humidity);
	} else {
		os_printf("Can't read DHT 1 (type %d, pin %d, error %d)\n", r1->sensorType, r1->pin,
				r1->error);
	}
	r1->printRaw ^= 1;
	if (r2->valid) {
		os_printf("Temp 2: %d", (int) r2->temperature);
		os_printf(" Hum 2: %d\n", (int) r2->humidity);
	} else {
		os_printf("Can't read DHT 2 (type %d, pin %d, error %d)\n", r2->sensorType, r2->pin,
				r2->error);
	}
	r2->printRaw ^= 1;
}

static bool ICACHE_FLASH_ATTR checkHumidityHigh(void) {
	const float hysterisis = 2.0;
	struct dht_sensor_data* r1 = dhtRead(1);
	struct dht_sensor_data* r2 = dhtRead(2);
	static bool hum1High, hum2High;
	if (r1->success) {
		if (hum1High) {
			if (r1->avgHumidity < sysCfg.settings[SETTING_HUMIDTY1] - hysterisis)
				hum1High = false;
		} else {
			if (r1->avgHumidity > sysCfg.settings[SETTING_HUMIDTY1] + hysterisis)
				hum1High = true;
		}
	} else {
		hum1High = false;
	}
	if (r2->success) {
		if (hum2High) {
			if (r2->avgHumidity < sysCfg.settings[SETTING_HUMIDTY1] - hysterisis)
				hum2High = false;
		} else {
			if (r2->avgHumidity > sysCfg.settings[SETTING_HUMIDTY1] + hysterisis)
				hum2High = true;
		}
	} else {
		hum2High = false;
	}
	return hum1High || hum2High;
}

static bool ICACHE_FLASH_ATTR checkTemperatureHigh(void) {
	const float hysterisis = 0.5;
	struct dht_sensor_data* r1 = dhtRead(1);
	struct dht_sensor_data* r2 = dhtRead(2);
	static bool temp1High, temp2High;
	if (r1->success) {
		if (temp1High) {
			if (r1->avgTemperature < (float) sysCfg.settings[SETTING_TEMPERATURE1] - hysterisis)
				temp1High = false;
		} else {
			if (r1->avgTemperature > (float) sysCfg.settings[SETTING_TEMPERATURE1] + hysterisis)
				temp1High = true;
		}
	} else {
		temp1High = false;
	}
	if (r2->success) {
		if (temp2High) {
			if (r2->avgTemperature < (float) sysCfg.settings[SETTING_TEMPERATURE2] - hysterisis)
				temp2High = false;
		} else {
			if (r2->avgTemperature > (float) sysCfg.settings[SETTING_TEMPERATURE2] + hysterisis)
				temp2High = true;
		}
	} else {
		temp2High = false;
	}
	return temp1High || temp2High;
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
	} else if (inWorkingHours) {
		INFOP("<TIME>");
		setSpeed(SLOW);
		setLED(DARK);
	} else {
		INFOP("<OFF>");
		setSpeed(STOP);
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
		setPirFanActive(sysCfg.settings[SETTING_PIR_ACTION]);
		processData();
		break;
	case 2:
		clearPirFanActive(sysCfg.settings[SETTING_PIR_ACTION]);
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
	struct station_config cfg;

	os_timer_disarm(&reconnect_timer);
	switch (status) {
	case STATION_GOT_IP:
		MQTT_Connect(&mqttClient);
		wifiChannel = wifi_get_channel();
		tcp_listen(80); // for setting up SSID/PW
		return;
	case STATION_IDLE:
	case STATION_CONNECTING:
		TESTP("}");
		return;
	case STATION_WRONG_PASSWORD:
		wifi_station_get_config(&cfg);
		TESTP("WiFi Connect Fail Wrong Password (%s/%s)\n", cfg.ssid, cfg.password);
		break;
	case STATION_NO_AP_FOUND:
		TESTP("WiFi Connect Fail no AP\n");
		break;
	case STATION_CONNECT_FAIL:
		TESTP("WiFi Connect Fail\n");
		break;
	default:
		TESTP("Bad wifiConnect_status %d\n", status);
	}
	mqttConnected = false;
	MQTT_Disconnect(&mqttClient);
	os_timer_setfn(&reconnect_timer, (os_timer_func_t *) doWiFiConnect, NULL);
	os_timer_arm(&reconnect_timer, 5 * 1000, false); // Try again in 5 secs
}

static void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args) {
//	MQTT_Client* client = (MQTT_Client*)args;
	MQTT_Connect(&mqttClient);
	mqttConnected = false;
}

void ICACHE_FLASH_ATTR setExternalTemperature(char* dataBuf) {
	externalTemperature = atoi(dataBuf);
}

static void ICACHE_FLASH_ATTR timeOnCb(void) {
	ERRORP("setTime Timeout\n");
	inWorkingHours = true; // default to ON if no time message being received
	externalTemperature = 0.0; // Assume if date not sent then nor is Temp
	lastAction = RESTART;
	system_restart();
}

void ICACHE_FLASH_ATTR setTime(char* dataBuf) {
	time_t t = atoi(dataBuf);
	struct tm* timeInfo = localtime(&t);
	inWorkingHours = (sysCfg.settings[SETTING_START_ON] <= timeInfo->tm_hour
			&& timeInfo->tm_hour <= sysCfg.settings[SETTING_FINISH_ON]);
	INFOP("time - %02d:%02d <%02d-%02d> [%d]\n", timeInfo->tm_hour, timeInfo->tm_min,
			sysCfg.settings[SETTING_START_ON], sysCfg.settings[SETTING_FINISH_ON], inWorkingHours);
	os_timer_disarm(&time_timer);
	os_timer_setfn(&time_timer, (os_timer_func_t *) timeOnCb, NULL);
	os_timer_arm(&time_timer, 6 * 60 * 1000, false); // 6 minutes
}

static void ICACHE_FLASH_ATTR mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len,
		const char *data, uint32_t data_len) {
	lastAction = MQTT_DATA_CB;
	char *topicBuf = (char*) os_malloc(topic_len + 1), *dataBuf = (char*) os_malloc(data_len + 1);
	if (topicBuf == NULL || dataBuf == NULL) {
		TESTP("malloc error %x %x\n", topicBuf, dataBuf);
		startFlash(-1, 50, 50); // fast
		return;
	}

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
	lastAction = MQTT_CONNECTED_FUNC;
	uint32 t = system_get_time();
	char *topic = (char*) os_zalloc(100);
	static int reconnections = 0;

	TESTP("MQTT: Connected to %s:%d\n", sysCfg.mqtt_host, sysCfg.mqtt_port);

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
		// publishMapping();

		os_timer_disarm(&transmit_timer);
		os_timer_setfn(&transmit_timer, (os_timer_func_t *) transmitCb, (void *) &mqttClient);
		os_timer_arm(&transmit_timer, sysCfgUpdates() * 1000, true);
	}
	checkMinHeap();
	os_free(topic);
	easygpio_outputSet(LED2, 0); // Turn LED off when connected
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

static void ICACHE_FLASH_ATTR doWiFiConnect(void) {
	static int WiFiConnectCount = 0;
	TESTP("doWiFiConnect: %d\n", ++WiFiConnectCount);
	WIFI_Connect(getBestSSID(), sysCfg.sta_pwd, sysCfg.deviceName, wifiConnectCb);
}

static uint32 ICACHE_FLASH_ATTR fs_size() { // returns the flash chip's size, in BYTES
  uint32_t id = spi_flash_get_id();
  uint8_t mfgr_id = id & 0xff;
  uint8_t type_id = (id >> 8) & 0xff; // not relevant for size calculation
  uint8_t size_id = (id >> 16) & 0xff; // lucky for us, WinBond ID's their chips as a form that lets us calculate the size
//  if(mfgr_id != 0xEF) // 0xEF is WinBond; that's all we care about (for now)
//    return 0;
  return (uint32)1 << size_id;
}

static void ICACHE_FLASH_ATTR showSysInfo() {
	TESTP("SDK version is: %s\n", system_get_sdk_version());
	TESTP("Smart-Config version is: %s\n", smartconfig_get_version());
	system_print_meminfo();
	uint32 sz = fs_size();
	TESTP("Flash size map %d; id %lx (0x%lx {%ld} bytes)\n", system_get_flash_size_map(),
			spi_flash_get_id(), sz, sz / 8);
	TEST(user_rf_cal_sector_set());
	TESTP("Boot mode: %d, version %d. Userbin %lx\n", system_get_boot_mode(),
			system_get_boot_version(), system_get_userbin_addr());
}

static void ICACHE_FLASH_ATTR startUp() {
	lastAction = INIT_DONE;

	MQTT_InitConnection(&mqttClient, sysCfg.mqtt_host, sysCfg.mqtt_port, sysCfg.security);
	MQTT_InitClient(&mqttClient, sysCfg.device_id, sysCfg.mqtt_user, sysCfg.mqtt_pass,
			sysCfg.mqtt_keepalive, 1);

	char temp[100];
	os_sprintf(temp, "/Raw/%s/offline", sysCfg.device_id);
	MQTT_InitLWT(&mqttClient, temp, "offline", 0, 0);

	MQTT_OnConnected(&mqttClient, mqttConnectedCb);
	MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
	MQTT_OnData(&mqttClient, mqttDataCb);

	doWiFiConnect();

	if (!system_os_task(backgroundTask, USER_TASK_PRIO_1, taskQueue, QUEUE_SIZE))
		TESTP("Can't set up background task\n");

	dhtInit(1, sysCfg.settings[SETTING_DHT1], PIN_DHT1, 2000);
	dhtInit(2, sysCfg.settings[SETTING_DHT2], PIN_DHT2, 2000);
	initSwitch(switchAction);
	initPublish(&mqttClient);

	os_timer_disarm(&process_timer);
	os_timer_setfn(&process_timer, (os_timer_func_t *) processTimerCb, NULL);
	os_timer_arm(&process_timer, 500, true); // repeat every 500mS
	TEST(showSysInfo());
}

static void ICACHE_FLASH_ATTR initDone_cb() {
	char bfr[100];
	CFG_Load();
	os_sprintf(bfr, "%s/%s", sysCfg.deviceLocation, sysCfg.deviceName);
	TESTP("\n%s ( V %s ) starting ...\n", bfr, version);
	initWiFi(PHY_MODE_11B, bfr, sysCfg.sta_ssid, startUp);
}

void ICACHE_FLASH_ATTR user_init(void) {
	stdout_init();
	gpio_init();
	initOutputs();
	initInputs();
	savedLastAction = lastAction;
	system_init_done_cb(&initDone_cb);
}
