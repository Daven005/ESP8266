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
#include "smartconfig.h"
#include "doSmartConfig.h"
#include "version.h"
#include "user_conf.h"
#include "temperature.h"
#include "time.h"
#include "decodeMessage.h"
#include "rtc.h"
#include "store.h"
#include "user_main.h"

os_timer_t monitor_timer;
os_timer_t closeDown_timer;
os_timer_t timeValid_timer;

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
	EVENT_PUBLISH_DATA,
	EVENT_PUBLISH_BULK_DATA
};
enum RFMode {
	RF_DEFAULT = 0, // RF_CAL or not after deep-sleep wake up, depends on init data byte 108.
	RF_CAL = 1,      // RF_CAL after deep-sleep wake up, there will be large current.
	RF_NO_CAL = 2,   // no RF_CAL after deep-sleep wake up, there will only be small current.
	RF_DISABLED = 4 // disable RF after deep-sleep wake up, just like modem sleep, there will be the smallest current.
};

#define CONFIG "Wind Speed"
#pragma message "Config: " CONFIG

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
int8_t publishedCount;
MQTT_Client mqttClient;
typedef bool checkRfFunc(void);

void user_rf_pre_init(void);
uint32 user_rf_cal_sector_set(void);

void user_rf_pre_init(void) {
//	system_deep_sleep_set_option(RF_DISABLED);
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
		if (sysCfg.settings[SET_VBAT_CAL_DIV] > 0 && sysCfg.settings[SET_VBAT_CAL_DIV] > 0) {
			vcc = (system_adc_read() * (uint32) sysCfg.settings[SET_VBAT_CAL_MULT])
					/ sysCfg.settings[SET_VBAT_CAL_DIV];
		} else {
			vcc = system_adc_read();
		}
		publishDeviceInfo(version, CONFIG, wifiChannel, WIFI_ConnectTime(), getBestSSID(), vcc);
		lastSaved = CFG_lastSaved();
	}
}

static void ICACHE_FLASH_ATTR mqttPublishedCb(uint32_t *args) {
	TESTP("MQTT published (%d). ", publishedCount);
	publishedCount--;
	if (recCount() == 0 && publishedCount == 0) {
		TESTP("Start MQTT disconnect. ");
		MQTT_Disconnect(&mqttClient);
	}
}

static bool ICACHE_FLASH_ATTR enoughToPublish(void) {
	return recCount() >= sysCfg.settings[SET_REPORTING_COUNT];
}

static void ICACHE_FLASH_ATTR gotoSleep(checkRfFunc rf) {
	TESTP("Sleep %dS, RF: %d\n", sysCfg.updates, rf());
	system_deep_sleep_set_option(rf() ? RF_CAL : RF_DISABLED);
	system_deep_sleep(sysCfg.updates * 1000 * 1000);
	os_delay_us(100);
}

static void ICACHE_FLASH_ATTR checkAllPublished(uint32_t *args) {
	TESTP("Publishing not complete (%d)\n", publishedCount);
	gotoSleep(enoughToPublish); // Anyway
}

static void ICACHE_FLASH_ATTR timeValidCheckCb(void) {
	if (!isTimeValid()) {
		os_timer_disarm(&timeValid_timer);
		os_timer_arm(&timeValid_timer, 1000, false); // Wait for /App/date message
	} else {
		gotoSleep(enoughToPublish);
	}
}

static void ICACHE_FLASH_ATTR enqCompleteCb(void) {
	if (recCount() >= sysCfg.settings[SET_REPORTING_COUNT] && mqttConnected) {
		if (!system_os_post(USER_TASK_PRIO_1, EVENT_PUBLISH_BULK_DATA, 0))
			ERRORP("Can't post EVENT_PUBLISH_BULK_DATA 1\n")
	} else if (!isTimeValid()) {
		os_timer_disarm(&timeValid_timer);
		os_timer_setfn(&timeValid_timer, (os_timer_func_t *) timeValidCheckCb, NULL);
		os_timer_arm(&timeValid_timer, 1000, false);  // Wait for /App/date message
	} else {
		gotoSleep(enoughToPublish);
	}
}

static void ICACHE_FLASH_ATTR monitoringComplete(void) {
	DataRec_t d;
	startFlash(-1, 10, 100);

	d.time = getTime();
	d.avgWind = windAverageReading();
	d.maxWind = windMaxReading();
	d.minWind = windMinReading();
	d.cutinWind = windAboveCutIn();
	enqRec(&d, enqCompleteCb);
}

static void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status) {
	os_printf("WiFi status: %d\n", status);
	if (status == STATION_GOT_IP) {
		wifiChannel = wifi_get_channel();
		MQTT_Connect(&mqttClient);
	} else {
		MQTT_Disconnect(&mqttClient);
	}
}

static void ICACHE_FLASH_ATTR printAll(void) {
	int idx;

	os_printf("WiFi: %d\n", wifi_station_get_connect_status());
	os_printf("Settings: ");
	for (idx = 0; idx < SETTINGS_SIZE; idx++) {
		os_printf("%d=%d ", idx, sysCfg.settings[idx]);
	}
	os_printf("\n");
	os_printf("Updates: %d\n", sysCfg.updates);
	printWind();
	printHeading("");
	printRecords();
	os_printf("minHeap: %d\n", checkMinHeap());
}

static void ICACHE_FLASH_ATTR switchAction(int action) {
	static bool printFlag = false;
	startFlash(action, 50, 100);
	switch (action) {
	case 1:
		printAll();
		system_set_os_print((printFlag = !printFlag));
		break;
	case 2:

		break;
	case 3:
		if (!checkSmartConfig(SC_CHECK)) {
			if (!toggleHttpSetupMode()) {
//				checkCanSleep();
			}
		}
		break;
	case 5:
		checkSmartConfig(SC_TOGGLE);
		break;
	case 4:
		break;
	}
}

static void ICACHE_FLASH_ATTR publishBulkData(uint32 pass) {
	char bfr[300];
	DataRec_t windData;
	uint32 t = system_get_time();

	if (mqttConnected) {
		INFOP("Pub %d %d\n", pass, recCount());
		switch (pass) { // Split into multiple passes to minimise hogging
		case 0:
			MQTT_OnPublished(&mqttClient, mqttPublishedCb);
			publishedCount = 1;
			_publishDeviceInfo();
			if (!system_os_post(USER_TASK_PRIO_1, EVENT_PUBLISH_BULK_DATA, 1))
				ERRORP("Can't post EVENT_PUBLISH_BULK_DATA 1\n");
			break;
		default:
			if (recCount() > 0) {
				setLastTimeSync(getTime());
				if (deqRec(&windData)) {
					os_sprintf(bfr,
							"{\"time\":%ld,\"avgWind\":%d,\"maxWind\":%d,\"minWind\":%d,\"cutinWind\":%d}",
							windData.time, windData.avgWind, windData.maxWind, windData.minWind,
							windData.cutinWind);
					publishedCount++;
					publish("Wind", "/Raw/%s/bulkData", bfr);
				}
				if (!system_os_post(USER_TASK_PRIO_1, EVENT_PUBLISH_BULK_DATA, 1))
					ERRORP("Can't post EVENT_PUBLISH_BULK_DATA 2\n");
			} else { // Wait for all data to be published
				os_timer_disarm(&closeDown_timer);
				os_timer_setfn(&closeDown_timer, (os_timer_func_t *) checkAllPublished, NULL);
				os_timer_arm(&closeDown_timer, 5000, false); // Safety check
			}
			break;
		}
	}
}

void ICACHE_FLASH_ATTR publishData(uint32 pass) { // For decodeMessage (/App/refresh) compatibility

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
		startFlash(-1, 50, 500);
	}
	// This subscription used to get settings when starting up in SLEEP and non-SLEEP modes
	char *topic = (char*) os_zalloc(100);
	os_sprintf(topic, "/Raw/%s/set/#", sysCfg.device_id);
	INFOP("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	os_sprintf(topic, "/Raw/%s/+/set/#", sysCfg.device_id);
	INFOP("Subscribe to: %s\n", topic);
	MQTT_Subscribe(&mqttClient, topic, 0);

	MQTT_Subscribe(client, "/App/date", 0);

	checkMinHeap();
	os_free(topic);

	easygpio_outputSet(LED, LED_OFF); // Turn LED off when connected
	lastAction = MQTT_CONNECTED_FUNC;
	checkTimeFunc("mqttConnectedFunc", t);
}

static void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*) args;
	mqttConnected = false;
	TESTP("MQTT disconnected\n");
	lastAction = MQTT_DISCONNECTED_CB;

	os_timer_disarm(&closeDown_timer);
	os_timer_setfn(&closeDown_timer, (os_timer_func_t *) gotoSleep, enoughToPublish); //(void) enoughToPublish());
	os_timer_arm(&closeDown_timer, 500, false); // Allow TCP to close down properly
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
	case EVENT_PUBLISH_BULK_DATA:
		publishBulkData(e->par);
		break;
	default:
		ERRORP("Bad background task event %d\n", e->sig)
		;
		break;
	}
}

static void ICACHE_FLASH_ATTR startUp() {
	INFOP("wifi_get_phy_mode = %d\n", wifi_get_phy_mode());

	MQTT_InitConnection(&mqttClient, sysCfg.mqtt_host, sysCfg.mqtt_port, sysCfg.security);
	MQTT_InitClient(&mqttClient, sysCfg.device_id, sysCfg.mqtt_user, sysCfg.mqtt_pass,
			sysCfg.mqtt_keepalive, 1);

//	char temp[100];
//	os_sprintf(temp, "/Raw/%s/offline", sysCfg.device_id);
//	MQTT_InitLWT(&mqttClient, temp, "offline", 0, 0);

	MQTT_OnConnected(&mqttClient, mqttConnectedCb);
	MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
	MQTT_OnData(&mqttClient, mqttDataCb);
	initPublish(&mqttClient);

	INFO(showSysInfo());
	if (os_strncmp(getBestSSID(), sysCfg.sta_ssid, 5) != 0) { // Dissimilar SSID
		os_strcpy(getBestSSID(), sysCfg.sta_ssid); // Use stored SSID; nb assumes same password
	}
	WIFI_Connect(getBestSSID(), sysCfg.sta_pwd, sysCfg.deviceName, wifiConnectCb);
	if (!system_os_task(backgroundTask, USER_TASK_PRIO_1, taskQueue, QUEUE_SIZE))
		ERRORP("Can't set up background task\n");
	lastAction = INIT_DONE;
}

static void ICACHE_FLASH_ATTR fpm_wakup_cb(void) {
	TESTP("fpm_wakup_cb\n");
}

static void ICACHE_FLASH_ATTR initDone_cb() {
	char bfr[100];
	bool timeValidFlag = true;
	CFG_init(100);
	CFG_print();
	os_sprintf(bfr, "%s/%s", sysCfg.deviceLocation, sysCfg.deviceName);
	TESTP("\n%s ( %s ) starting ...\n", bfr, version);
#if DEBUG>=3
	showSysInfo();
#endif
	startFlash(-1, 10, 1000);
	initWindMonitor();
	initSwitch(switchAction);
	os_timer_disarm(&monitor_timer);
	os_timer_setfn(&monitor_timer, (os_timer_func_t *) monitoringComplete, NULL);
	os_timer_arm(&monitor_timer, sysCfg.settings[SET_MONITOR_TIME]*1000, false); // Enough to get current wind state

	if (!isTimeValid()) {
		TESTP("Invalid time: ");
		timeValidFlag = false;
	}
	printTime();
	os_printf("\n");
	initStore(getTime());
	printHeading("Init hdr");

	TESTP("Starting [%d %d %d]\n", system_get_rst_info()->reason, recCount(), timeValidFlag);
	if (system_get_rst_info()->reason != REASON_DEEP_SLEEP_AWAKE
			|| recCount() >= sysCfg.settings[SET_REPORTING_COUNT] || !timeValidFlag) {
		initWiFi(PHY_MODE_11B, bfr, sysCfg.sta_ssid, startUp);
	} else {
		wifi_station_disconnect();
		wifi_set_opmode(NULL_MODE);
		wifi_fpm_open();
		wifi_fpm_set_sleep_type(MODEM_SLEEP_T);
		wifi_fpm_open();
		wifi_fpm_set_wakeup_cb(fpm_wakup_cb);
		wifi_fpm_do_sleep(0xFFFFFFF);
	}
}

void ICACHE_FLASH_ATTR user_init(void) {
//	system_set_os_print(true);
	stdout_init();
	gpio_init();
	os_printf(">>>\n");
	savedLastAction = lastAction;

	easygpio_pinMode(16, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_outputDisable(16);

	easygpio_pinMode(LED, EASYGPIO_PULLUP, EASYGPIO_OUTPUT);
	easygpio_outputEnable(LED, LED_ON);
	system_init_done_cb(&initDone_cb);
	i2c_master_gpio_init();
}
