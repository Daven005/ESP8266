
#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "easygpio.h"
#include "stdout.h"

#include "config.h"
#include "wifi.h"
#include "debug.h"
#include "mqtt.h"
#include "user_config.h"
#include "smartconfig.h"
#include "version.h"

LOCAL os_timer_t switch_timer;
LOCAL os_timer_t smartConfigFlashTimer;
LOCAL os_timer_t pumpOverrideFlashTimer;
LOCAL os_timer_t process_timer;
LOCAL os_timer_t transmit_timer;

MQTT_Client mqttClient;
uint8 mqttConnected;
uint16 currentPressure;
enum {TANK_UNKNOWN, TANK_LOW, TANK_OK} tankStatus = TANK_UNKNOWN;
enum {PUMP_AUTO, PUMP_OFF, PUMP_ON} pumpOverride = PUMP_AUTO;
enum SmartConfigAction {SC_CHECK, SC_HAS_STOPPED, SC_TOGGLE};
#define LED 5
#define LED2 3
#define PUMP 4
#define SWITCH 0 // GPIO 00
static unsigned int switchCount;

// sysCfg.settings
#define SET_PUMP_ON 0
#define SET_PUMP_OFF 1

void user_rf_pre_init(){}

int ICACHE_FLASH_ATTR splitString(char *string, char delim, char *tokens[]) {
	char *endString; char *startString;

	startString = string;
	while (*string) {
		if (*string == delim) *string = '\0';
		string++;
	}
	endString = string;
	string = startString;
	int idx = 0;
	if (*string == '\0') string++; // Ignore 1st leading delimiter
	while (string < endString) {
		tokens[idx] = string;
		string++;
		idx++;
		while (*string++) ;
	}
	return idx;
}

void ICACHE_FLASH_ATTR smartConfigFlash_cb(void) {
	easygpio_outputSet(LED, !easygpio_inputGet(LED));
}

void ICACHE_FLASH_ATTR startSmartConfigFlash(int t, int repeat) {
	easygpio_outputSet(LED, 1);
	os_timer_disarm(&smartConfigFlashTimer);
	os_timer_setfn(&smartConfigFlashTimer, (os_timer_func_t *)smartConfigFlash_cb, (void *)0);
	os_timer_arm(&smartConfigFlashTimer, t, repeat);
}

void ICACHE_FLASH_ATTR stopSmartConfigFlash(void) {
	easygpio_outputSet(LED, 0);
	os_timer_disarm(&smartConfigFlashTimer);
}

void ICACHE_FLASH_ATTR pumpOverrideFlash_cb(void) {
	easygpio_outputSet(LED2, !easygpio_inputGet(LED2));
}

void ICACHE_FLASH_ATTR startPumpOverrideFlash(int t, int repeat) {
	easygpio_outputSet(LED2, 1);
	os_timer_disarm(&pumpOverrideFlashTimer);
	os_timer_setfn(&pumpOverrideFlashTimer, (os_timer_func_t *)pumpOverrideFlash_cb, (void *)0);
	os_timer_arm(&pumpOverrideFlashTimer, t, repeat);
}

void ICACHE_FLASH_ATTR stopPumpOverrideFlash(void) {
	easygpio_outputSet(LED2, 0);
	os_timer_disarm(&pumpOverrideFlashTimer);
}

void ICACHE_FLASH_ATTR publishError(uint8 err, uint8 info) {
	char topic[50];
	char data[100];
	os_sprintf(topic, (const char*) "/Raw/%s/error", sysCfg.device_id);
	os_sprintf(data, (const char*) "{ \"error\":%d, \"info\":%d}", err, info);
	MQTT_Publish(&mqttClient, topic, data, strlen(data), 0, 0);
}

void ICACHE_FLASH_ATTR process_cb(void) { // Every 100mS
	static uint32 pumpOnCount = 0;

	currentPressure = system_adc_read();
	switch (pumpOverride) {
	case PUMP_AUTO:
		if (currentPressure < sysCfg.settings[SET_PUMP_ON]) {
			easygpio_outputSet(PUMP, 1);
			easygpio_outputSet(LED, 1);
			pumpOnCount++;
			if (pumpOnCount == MAX_PUMP_ON_WARNING) { // 1 minute
				publishError(1, 0);
			} else if (pumpOnCount == MAX_PUMP_ON_ERROR) { // 5 minutes
				publishError(2, tankStatus);
			} else if (pumpOnCount >= MAX_PUMP_ON_ERROR) {
				pumpOnCount = MAX_PUMP_ON_ERROR;
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
		pumpOnCount = 0;
		break;
	case PUMP_ON:
		easygpio_outputSet(PUMP, 1);
		easygpio_outputSet(LED, 1);
		pumpOnCount = 0;
		break;
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
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR publishData(MQTT_Client* client) {
	char topic[100];
	char data[100];

	os_sprintf(topic, (const char*) "/Raw/%s/0/info", sysCfg.device_id);
	os_sprintf(data, (const char*) "{ \"Type\":\"Pressure\", \"Value\":%d}",  currentPressure);
	MQTT_Publish(client, topic, data, strlen(data), 0, false);
	os_printf("%s=>%s\n", topic, data);
}

void ICACHE_FLASH_ATTR publishDeviceInfo(MQTT_Client* client) {
	if (mqttConnected) {
		char topic[50];
		char data[200];
		int idx;
		os_sprintf(topic, "/Raw/%10s/info", sysCfg.device_id);
		os_sprintf(data, "{\"Name\":\"%s\", \"Location\":\"%s\", \"Updates\":%d, \"Inputs\":%d, \"Settings\":[",
			sysCfg.deviceName, sysCfg.deviceLocation, sysCfg.updates, sysCfg.inputs);
		 pp_soft_wdt_stop();
		 for (idx = 0; idx< SETTINGS_SIZE; idx++) {
			if (idx != 0) os_sprintf(data+strlen(data), ", ");
			os_sprintf(data+strlen(data), "%d", sysCfg.settings[idx]);
		}
		os_sprintf(data+strlen(data), "]}");
		os_printf("%s=>%s\n", topic, data);
		pp_soft_wdt_restart();
		MQTT_Publish(client, topic, data, strlen(data), 0, true);
	}
}

void ICACHE_FLASH_ATTR transmitCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*)args;
	publishData(client);
}

void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args) {
	MQTT_Connect(&mqttClient);
	mqttConnected = false;
	os_timer_disarm(&process_timer);
}

void ICACHE_FLASH_ATTR checkAppMsg(MQTT_Client* client, int tokenCount, char *tokens[], char *dataBuf) {
	if (tokenCount == 3 && strcmp("rainwater", tokens[1] )== 0 && strcmp("level", tokens[2] )== 0) {
		if (strcmp("low", dataBuf) == 0) {
			tankStatus = TANK_LOW;
		} else if (strcmp("OK", dataBuf) == 0 || strcmp("high", dataBuf) == 0 ) {
			tankStatus = TANK_OK;
		} else {
			tankStatus = TANK_UNKNOWN;
		}
	}
}

void ICACHE_FLASH_ATTR checkRawMsg(MQTT_Client* client, int tokenCount, char *tokens[], char *dataBuf) {
	if (tokenCount == 4 && strcmp(sysCfg.device_id, tokens[1] )== 0 && strcmp("set", tokens[2] )== 0) {
		if (strlen(dataBuf) < NAME_SIZE-1) {
			if (strcmp("name", tokens[3]) == 0) {
				strcpy(sysCfg.deviceName, dataBuf);
			} else if (strcmp("location", tokens[3]) == 0){
				strcpy(sysCfg.deviceLocation, dataBuf);
			} else if (strcmp("updates", tokens[3]) == 0){
				sysCfg.updates = atoi(dataBuf);
				os_timer_disarm(&transmit_timer);
				os_timer_arm(&transmit_timer, sysCfg.updates*1000, true);
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
						CFG_Save();
						os_printf("Setting %d = %d\n", id, sysCfg.settings[id]);
						publishDeviceInfo(client);
					}
				}
			}
		}
	}
}

void ICACHE_FLASH_ATTR mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t data_len)
{
	char *topicBuf = (char*)os_zalloc(topic_len+1),
		 *dataBuf = (char*)os_zalloc(data_len+1);
	char *tokens[10];

	MQTT_Client* client = (MQTT_Client*)args;

	os_memcpy(topicBuf, topic, topic_len);
	topicBuf[topic_len] = 0;
	os_memcpy(dataBuf, data, data_len);
	dataBuf[data_len] = 0;
	os_printf("Receive topic: %s, data: %s \r\n", topicBuf, dataBuf);

	int tokenCount = splitString((char *)topicBuf, '/', tokens);

	if (tokenCount > 0) {
		if(strcmp("Raw", tokens[0])== 0) {
			checkRawMsg(client, tokenCount, tokens, dataBuf);
		} else if(strcmp("App", tokens[0])== 0) {
			checkAppMsg(client, tokenCount, tokens, dataBuf);
		}
	}
	os_free(topicBuf);
	os_free(dataBuf);
}

void ICACHE_FLASH_ATTR checkPumpOverride(void) {
	switch (pumpOverride) {
	case PUMP_AUTO:
		pumpOverride = PUMP_OFF;
		startPumpOverrideFlash(200, true);
		break;
	case PUMP_OFF:
		pumpOverride = PUMP_ON;
		startPumpOverrideFlash(500, true);
		break;
	case PUMP_ON:
		pumpOverride = PUMP_AUTO;
		stopPumpOverrideFlash();
		break;
	}
	os_printf("PO=%d\n", pumpOverride);
}

void ICACHE_FLASH_ATTR switchAction(int action) {
	switch (action) {
	case 1:
		checkPumpOverride();
		break;
	case 2:
		publishData(&mqttClient);
		break;
	case 3:
		break;
	case 4:
		break;
	case 5:
		checkSmartConfig(SC_TOGGLE);
		break;
	case 6:
		break;
	}
}

void ICACHE_FLASH_ATTR switchTimerCb(uint32_t *args) {
	const swOnMax = 100;
	const swOffMax = 5;
	static int switchPulseCount;
	static enum {
		IDLE, ON, OFF
	} switchState = IDLE;

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

static void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t *args) {
	char topic[100];

	MQTT_Client* client = (MQTT_Client*)args;
	mqttConnected = true;
	os_printf("MQTT: Connected to %s:%d\r\n", sysCfg.mqtt_host, sysCfg.mqtt_port);

	os_sprintf(topic, "/Raw/%s/set/#", sysCfg.device_id);
	os_printf("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	os_sprintf(topic, "/Raw/%s/+/set/#", sysCfg.device_id);
	os_printf("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	MQTT_Subscribe(client, "/App/rainwater/level", 0);

	publishDeviceReset(client);
	publishDeviceInfo(client);

	os_timer_disarm(&switch_timer);
	os_timer_setfn(&switch_timer, (os_timer_func_t *)switchTimerCb, NULL);
	os_timer_arm(&switch_timer, 100, true);

	os_timer_disarm(&transmit_timer);
	os_timer_setfn(&transmit_timer, (os_timer_func_t *)transmitCb, (void *)client);
	os_timer_arm(&transmit_timer, sysCfg.updates*1000, true);
	easygpio_outputSet(LED, 0); // Turn LED off when connected

	// Check pressure every 100mS
	os_timer_disarm(&process_timer);
	os_timer_setfn(&process_timer, (os_timer_func_t *)process_cb, (void *)0);
	os_timer_arm(&process_timer, 100, true);
}

static void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status) {
	ets_uart_printf("WiFi status: %d\r\n", status);
	if (status == STATION_GOT_IP){
		MQTT_Connect(&mqttClient);
	} else {
		mqttConnected = false;
		MQTT_Disconnect(&mqttClient);
	}
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

LOCAL void ICACHE_FLASH_ATTR initDone_cb(void) {
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

	WIFI_Connect(sysCfg.sta_ssid, sysCfg.sta_pwd, wifiConnectCb);

	showSysInfo();
}

void ICACHE_FLASH_ATTR smartConfig_done(sc_status status, void *pdata) {
	switch(status) {
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
		wifi_station_disconnect();
		wifi_station_connect();
		break;
	case SC_STATUS_LINK_OVER:
		os_printf("SC_STATUS_LINK_OVER\n");
		smartconfig_stop();
		checkSmartConfig(1);
		break;
	}
}

int ICACHE_FLASH_ATTR checkSmartConfig(int action) {
	static doingSmartConfig = false;

	switch (action) {
	case 0:
		break;
	case 1:
		os_printf("finished smartConfig\n");
		stopSmartConfigFlash();
		doingSmartConfig = false;
		break;
	case 2:
		if (doingSmartConfig) {
			os_printf("Stop smartConfig\n");
			stopSmartConfigFlash();
			smartconfig_stop();
			doingSmartConfig = false;
			MQTT_Connect(&mqttClient);
		} else {
			os_printf("Start smartConfig\n");
			MQTT_Disconnect(&mqttClient);
			mqttConnected = false;
			startSmartConfigFlash(100, true);
			doingSmartConfig = true;
			smartconfig_start(smartConfig_done, true);
		}
		break;
	}
	return doingSmartConfig;
}

void ICACHE_FLASH_ATTR user_init(void) {
	stdout_init();
	gpio_init();
	wifi_station_set_auto_connect(false);
	easygpio_pinMode(LED, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_pinMode(LED2, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_pinMode(PUMP, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputSet(LED, 1);
	easygpio_outputSet(LED2, 0);
	easygpio_outputSet(PUMP, 0);
	system_init_done_cb(&initDone_cb);
}
