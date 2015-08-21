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
#include "smartconfig.h"
#include "user_config.h"
#include "dht22.h"
#include "time.h"

#define sleepms(x) os_delay_us(x*1000);

static os_timer_t transmit_timer;
static os_timer_t process_timer;
static os_timer_t pir_timer;
static os_timer_t time_timer;
static os_timer_t switch_timer;
static os_timer_t flash_timer;
LOCAL os_timer_t date_timer;

static bool pirIsActive = false;

enum speedSelect {
	OFF, SLOW, FAST
};

static float externalTemperature = 10.0; // default to cool
static bool timeIsON = true; // Default to ON, ie daytime

static unsigned int switchCount;

MQTT_Client mqttClient;
uint8 mqttConnected;
const uint8 outputMap[MAX_OUTPUT] = { 3, 15, 4, 5 }; // LED1, LED2, RELAY1, RELAY2

bool currentOutputs[MAX_OUTPUT];
bool outputOverrides[MAX_OUTPUT];

// For mapped Outputs
#define LED1 0
#define LED2 1
#define RELAY1 2
#define RELAY2 3
#define RELAY_ON 1
#define RELAY_OFF 0

#define LED_RED LED1
#define LED_GREEN LED2

#define SWITCH 0 // GPIO 00
#define DHT1 12
#define DHT2 14
#define PIR1 13
#define PIR2 16

#define SENSOR_TEMPERATURE1 0
#define SENSOR_TEMPERATURE2 1
#define SENSOR_HUMIDITY1 2
#define SENSOR_HUMIDITY2 3
#define SENSOR_PIR1 4
#define SENSOR_PIR2 5

enum led_t {
	DARK, RED, GREEN, YELLOW
};

enum SmartConfigAction {
	SC_CHECK, SC_HAS_STOPPED, SC_TOGGLE
};
bool ICACHE_FLASH_ATTR checkSmartConfig(enum SmartConfigAction);
void user_rf_pre_init() {
}

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

void ICACHE_FLASH_ATTR initOutputs(void) {
	int id;
	for (id = 0; id < MAX_OUTPUT; id++) {
		easygpio_pinMode(outputMap[id], EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
		easygpio_outputSet(outputMap[id], 0);
	}
}

void ICACHE_FLASH_ATTR initInputs(void) {
	easygpio_pinMode(DHT1, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_pinMode(DHT2, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_pinMode(PIR1, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_pinMode(PIR2, EASYGPIO_PULLUP, EASYGPIO_INPUT);
}

void ICACHE_FLASH_ATTR setOutput(uint8 id, bool set) {
	if (id < MAX_OUTPUT) {
		currentOutputs[id] = set;
		if (!outputOverrides[id]) {
			easygpio_outputSet(outputMap[id], set);
		}
	}
}

void ICACHE_FLASH_ATTR forceOutput(uint8 id, bool set) { // Sets override
	if (id < MAX_OUTPUT) {
		outputOverrides[id] = true;
		easygpio_outputSet(outputMap[id], set);
		INFO("o/p %d(%d)->%d\n", id, outputMap[id], set);
	}
}

void ICACHE_FLASH_ATTR clrOverride(uint8 id) {
	if (id < MAX_OUTPUT) {
		outputOverrides[id] = false;
		easygpio_outputSet(outputMap[id], currentOutputs[id]);
		INFO("o/p %d(%d)-->%d\n", id, outputMap[id], currentOutputs[id]);
	}
}

void ICACHE_FLASH_ATTR setLED(enum led_t l) {
	switch (l) {
	case DARK:
		setOutput(LED_RED, 0);
		setOutput(LED_GREEN, 0);
		break;
	case RED:
		setOutput(LED_RED, 1);
		setOutput(LED_GREEN, 0);
		break;
	case GREEN:
		setOutput(LED_RED, 0);
		setOutput(LED_GREEN, 1);
		break;
	case YELLOW:
		setOutput(LED_RED, 1);
		setOutput(LED_GREEN, 1);
		break;
	}
}

void ICACHE_FLASH_ATTR smartConfigFlash_cb(void) {
	setLED(RED);
}

void ICACHE_FLASH_ATTR startSmartConfigFlash(int t, int repeat) {
	setLED(RED);
	os_timer_disarm(&flash_timer);
	os_timer_setfn(&flash_timer, (os_timer_func_t *) smartConfigFlash_cb, (void *) 0);
	os_timer_arm(&flash_timer, t, repeat);
}

void ICACHE_FLASH_ATTR stopSmartConfigFlash(void) {
	setLED(DARK);
	os_timer_disarm(&flash_timer);
}

void ICACHE_FLASH_ATTR publishError(uint8 err, uint8 info) {
	static uint8 lastError = 255;
	static uint8 lastInfo = 255;
	char topic[50];
	char data[100];
	if (err != lastError || info != lastInfo) {
		os_sprintf(topic, (const char*) "/Raw/%s/error", sysCfg.device_id);
		os_sprintf(data, (const char*) "{ \"error\":%d, \"info\":%d}", err, info);
		MQTT_Publish(&mqttClient, topic, data, strlen(data), 0, 0);
		lastError = err;
		lastInfo = info;
	}
}

void ICACHE_FLASH_ATTR publishSensorData(MQTT_Client* client, uint8 sensor, char *type, int data) {
	char topicBfr[100];
	char dataBfr[100];
	os_sprintf(topicBfr, (const char*) "/Raw/%s/%d/info", sysCfg.device_id, sensor);
	os_sprintf(dataBfr, (const char*) "{ \"Type\":\"%s\", \"Value\":%d}", type, data);
	MQTT_Publish(client, topicBfr, dataBfr, strlen(dataBfr), 0, false);
	INFO("%s=>%s\n", topicBfr, dataBfr);
}

void ICACHE_FLASH_ATTR publishData(MQTT_Client* client) {
	if (mqttConnected) {
		struct dht_sensor_data* r1 = dht1Read();
		struct dht_sensor_data* r2 = dht2Read();

		if (r1->success) {
			publishSensorData(client, SENSOR_TEMPERATURE1, "Temp", r1->temperature);
			publishSensorData(client, SENSOR_HUMIDITY1, "Hum", r1->humidity);
		} else {
			publishError(1, 1);
		}
		if (r2->success) {
			publishSensorData(client, SENSOR_TEMPERATURE2, "Temp", r2->temperature);
			publishSensorData(client, SENSOR_HUMIDITY2, "Hum", r2->humidity);
		} else {
			publishError(1, 2);
		}
	}
}

void ICACHE_FLASH_ATTR publishDeviceInfo(MQTT_Client* client) {
	if (mqttConnected) {
		char topic[50];
		char data[200];
		int idx;
		os_sprintf(topic, "/Raw/%10s/info", sysCfg.device_id);
		os_sprintf(data,
				"{\"Name\":\"%s\", \"Location\":\"%s\", \"Updates\":%d, \"Inputs\":%d, \"Settings\":[",
				sysCfg.deviceName, sysCfg.deviceLocation, sysCfg.updates, sysCfg.inputs);
		for (idx = 0; idx < SETTINGS_SIZE; idx++) {
			if (idx != 0)
				os_sprintf(data + strlen(data), ", ");
			os_sprintf(data + strlen(data), "%d", sysCfg.settings[idx]);
		}
		os_sprintf(data + strlen(data), "]}");
		INFO("%s=>%s\n", topic, data);
		MQTT_Publish(client, topic, data, strlen(data), 0, true);
	}
}

bool ICACHE_FLASH_ATTR checkHumidityHigh(void) {
	struct dht_sensor_data* r1 = dht1Read();
	struct dht_sensor_data* r2 = dht2Read();
	if (r1->success && r1->avgHumidity > sysCfg.settings[SETTING_HUMIDTY1])
		return true;
	if (r2->success && r2->avgHumidity > sysCfg.settings[SETTING_HUMIDTY2])
		return true;
	return false;
}

bool ICACHE_FLASH_ATTR checkTemperatureHigh(void) {
	struct dht_sensor_data* r1 = dht1Read();
	struct dht_sensor_data* r2 = dht2Read();
	if (r1->success && r1->avgTemperature > sysCfg.settings[SETTING_TEMPERATURE1]
			&& r1->avgTemperature > externalTemperature) // Will not cool down if external Temperature is higher!
		return true;
	if (r2->success && r2->avgTemperature > sysCfg.settings[SETTING_TEMPERATURE2]
			&& r2->avgTemperature > externalTemperature)
		return true;
	return false;
}

void ICACHE_FLASH_ATTR pirCb(void) {
	char topic[100];
	char data[100];

	os_sprintf(topic, (const char*) "/Raw/%s/%d/info", sysCfg.device_id, SENSOR_PIR1);
	os_sprintf(data, (const char*) "{ \"Type\":\"PIR\", \"Value\":0}");
	MQTT_Publish(&mqttClient, topic, data, strlen(data), 0, false);
	os_sprintf(topic, (const char*) "/Raw/%s/%d/info", sysCfg.device_id, SENSOR_PIR2);
	os_sprintf(data, (const char*) "{ \"Type\":\"PIR\", \"Value\":0}");
	MQTT_Publish(&mqttClient, topic, data, strlen(data), 0, false);
	pirIsActive = false;
}

void ICACHE_FLASH_ATTR timeOnCb(void) {
	os_printf("time Timeout\n");
	timeIsON = true; // default to ON if no time message being received
	externalTemperature = 0.0; // Assume if date not sent then nor is Temp
}

void ICACHE_FLASH_ATTR setPirActive(uint8 pir) {
	char topic[100];
	char data[100];

	INFO("p%d", pir);
	os_sprintf(topic, (const char*) "/Raw/%s/%d/info", sysCfg.device_id, SENSOR_PIR1 + pir);
	os_sprintf(data, (const char*) "{ \"Type\":\"PIR\", \"Value\":1}");
	MQTT_Publish(&mqttClient, topic, data, strlen(data), 0, false);

	os_timer_disarm(&pir_timer);
	os_timer_setfn(&pir_timer, (os_timer_func_t *) pirCb, NULL);
	os_timer_arm(&pir_timer, sysCfg.settings[SETTING_PIR_ON_TIME]*1000*60, false); // Minutes

	pirIsActive = true;
}

bool ICACHE_FLASH_ATTR checkPirActive(void) {
	if (easygpio_inputGet(PIR1)) {
		setPirActive(0);
	}
	if (easygpio_inputGet(PIR2)) {
		setPirActive(1);
	}
	return pirIsActive;
}

void ICACHE_FLASH_ATTR setSpeed(enum speedSelect s) {
	INFO("relay:%d\n", s);
	switch (s) {
	case OFF:
		setOutput(RELAY1, 0);
		setOutput(RELAY2, 0);
		break;
	case SLOW:
		setOutput(RELAY1, 1);
		setOutput(RELAY2, 0);
		break;
	case FAST:
		setOutput(RELAY1, 1);
		setOutput(RELAY2, 1);
		break;
	}
}

void ICACHE_FLASH_ATTR processData(void) { // Called every 500mS
	if (checkPirActive()) {
		INFO("<PIR>");
		setSpeed(FAST);
		setLED(RED);
	} else if (checkHumidityHigh()) {
		INFO("<humidity>");
		setSpeed(FAST);
		setLED(GREEN);
	} else if (checkTemperatureHigh()) {
		INFO("<temperature>");
		setSpeed(FAST);
		setLED(YELLOW);
	} else if (timeIsON) {
		INFO("<TIME>");
		setSpeed(SLOW);
		setLED(DARK);
	} else {
		setSpeed(OFF);
		setLED(DARK);
	}
}

void ICACHE_FLASH_ATTR transmitCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*) args;
	processData();
	publishData(client);
}

void ICACHE_FLASH_ATTR smartConfig_done(sc_status status, void *pdata) {
	switch (status) {
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
		checkSmartConfig(SC_HAS_STOPPED);
		break;
	}
}

bool ICACHE_FLASH_ATTR checkSmartConfig(enum SmartConfigAction action) {
	static doingSmartConfig = false;

	switch (action) {
	case SC_CHECK:
		break;
	case SC_HAS_STOPPED:
		os_printf("Finished smartConfig\n");
		stopSmartConfigFlash();
		doingSmartConfig = false;
		MQTT_Connect(&mqttClient);
		break;
	case SC_TOGGLE:
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

void ICACHE_FLASH_ATTR switchAction(int pressCount) {
	switch (pressCount) {
	case 1:
		setPirActive(0);
		break;
	case 2:
		pirIsActive = false;
		break;
	case 5:
		checkSmartConfig(SC_TOGGLE);
		break;
	}
}

void ICACHE_FLASH_ATTR switchTimerCb(uint32_t *args) {
	const swOnMax = 100;
	const swOffMax = 10;
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

void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status) {
	ets_uart_printf("WiFi status: %d\r\n", status);
	if (status == STATION_GOT_IP) {
		MQTT_Connect(&mqttClient);
	} else {
		mqttConnected = false;
		MQTT_Disconnect(&mqttClient);
	}
}

void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args) {
//	MQTT_Client* client = (MQTT_Client*)args;
	MQTT_Connect(&mqttClient);
	mqttConnected = false;
}

void ICACHE_FLASH_ATTR dateTimerCb(void) {
	os_printf("Nothing heard so restarting...\n");
	system_restart();
}

void ICACHE_FLASH_ATTR setDeviceParameters(char* dataBuf, char* function, MQTT_Client* client) {
	if (strlen(dataBuf) < NAME_SIZE - 1) {
		if (strcmp("name", function) == 0) {
			strcpy(sysCfg.deviceName, dataBuf);
		} else if (strcmp("location", function) == 0) {
			strcpy(sysCfg.deviceLocation, dataBuf);
		} else if (strcmp("updates", function) == 0) {
			sysCfg.updates = atoi(dataBuf);
			os_timer_disarm(&transmit_timer);
			os_timer_arm(&transmit_timer, sysCfg.updates * 1000, true);
		}
		publishDeviceInfo(client);
		CFG_Save();
	}
}

void ICACHE_FLASH_ATTR setExternalTemperature(char* dataBuf) {
	externalTemperature = atoi(dataBuf);
}

void ICACHE_FLASH_ATTR setTime(char* dataBuf) {
	time_t t = atoi(dataBuf);
	struct tm* timeInfo = localtime(&t);
	timeIsON = (sysCfg.settings[SETTING_START_ON] <= timeInfo->tm_hour
			&& timeInfo->tm_hour <= sysCfg.settings[SETTING_FINISH_ON]);
	os_printf("time - %02d:%02d <%02d-%02d> [%d]\n", timeInfo->tm_hour, timeInfo->tm_min,
			sysCfg.settings[SETTING_START_ON], sysCfg.settings[SETTING_FINISH_ON], timeIsON);
	os_timer_disarm(&time_timer);
	os_timer_setfn(&time_timer, (os_timer_func_t *) timeOnCb, NULL);
	os_timer_arm(&time_timer, 6 * 60 * 1000, false);
}

void ICACHE_FLASH_ATTR outputOverride(int id, int value) {
	if (0 <= id && id < MAX_OUTPUT) {
		switch (value) {
		case 0:
		case 1:
			forceOutput(id, value);
			break;
		case -1:
			clrOverride(id);
			break;
		}
	}
}

void ICACHE_FLASH_ATTR setSettings(int id, int value, MQTT_Client* client) {
	if (0 <= id && id < SETTINGS_SIZE) {
		if (SET_MINIMUM <= value && value <= SET_MAXIMUM) {
			sysCfg.settings[id] = value;
			CFG_Save();
			INFO("Setting %d = %d\n", id, sysCfg.settings[id]);
			publishDeviceInfo(client);
		}
	}
}

void ICACHE_FLASH_ATTR mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len,
		const char *data, uint32_t data_len) {
	char *topicBuf = (char*) os_zalloc(topic_len + 1), *dataBuf = (char*) os_zalloc(data_len + 1);
	char *tokens[10];

	MQTT_Client* client = (MQTT_Client*) args;

	os_memcpy(topicBuf, topic, topic_len);
	topicBuf[topic_len] = 0;
	os_memcpy(dataBuf, data, data_len);
	dataBuf[data_len] = 0;
	INFO("Receive topic: %s, data: %s \r\n", topicBuf, dataBuf);

	int tokenCount = splitString((char *) topicBuf, '/', tokens);

	if (tokenCount > 0) {
		if (strcmp("Raw", tokens[0]) == 0) {
			if (tokenCount == 4 && strcmp(sysCfg.device_id, tokens[1]) == 0
					&& strcmp("set", tokens[2]) == 0) {
				setDeviceParameters(dataBuf, tokens[3], client);
			} else if (tokenCount == 5 && strcmp(sysCfg.device_id, tokens[1]) == 0) {
				if (strcmp("set", tokens[3]) == 0) {
					int value = atoi(dataBuf);
					int id = atoi(tokens[2]);
					if (strcmp("setting", tokens[4]) == 0) {
						setSettings(id, value, client);
					} else if (strcmp("output", tokens[4]) == 0) {
						outputOverride(id, value);
					}
				}
			}
		} else if (strcmp("App", tokens[0]) == 0) {
			if (tokenCount >= 2 && strcmp("date", tokens[1]) == 0) {
				setTime(dataBuf);
				os_timer_disarm(&date_timer); // Restart it
				os_timer_arm(&date_timer, 10 * 60 * 1000, false); //10 minutes
			} else if (tokenCount >= 3 && strcmp("Temp", tokens[1]) == 0
					&& strcmp("current", tokens[2]) == 0) {
				setExternalTemperature(dataBuf);
			}
		}
	}
	os_free(topicBuf);
	os_free(dataBuf);
}

void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t *args) {
	char topic[100];

	MQTT_Client* client = (MQTT_Client*) args;
	mqttConnected = true;
	os_printf("MQTT: Connected to %s:%d\r\n", sysCfg.mqtt_host, sysCfg.mqtt_port);

	os_sprintf(topic, "/Raw/%s/set/#", sysCfg.device_id);
	os_printf("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	os_sprintf(topic, "/Raw/%s/+/set/#", sysCfg.device_id);
	os_printf("Subscribe to: %s\n", topic);
	MQTT_Subscribe(client, topic, 0);

	MQTT_Subscribe(client, "/App/date", 0);
	MQTT_Subscribe(client, "/App/Temp/current", 0);

	publishDeviceInfo(client);

	dht1Init(DHT11, DHT1, 2000);
	dht2Init(DHT11, DHT2, 2000);

	os_timer_disarm(&transmit_timer);
	os_timer_setfn(&transmit_timer, (os_timer_func_t *) transmitCb, (void *) client);
	os_timer_arm(&transmit_timer, sysCfg.updates * 1000, true);

	os_timer_disarm(&process_timer);
	os_timer_setfn(&process_timer, (os_timer_func_t *) processData, NULL);
	os_timer_arm(&process_timer, 500, true);

	os_timer_disarm(&switch_timer);
	os_timer_setfn(&switch_timer, (os_timer_func_t *) switchTimerCb, NULL);
	os_timer_arm(&switch_timer, 100, true);

	os_timer_disarm(&date_timer);
	os_timer_setfn(&date_timer, (os_timer_func_t *) dateTimerCb, NULL);
	os_timer_arm(&date_timer, 20 * 60 * 1000, false); // 20 minutes
}

LOCAL void ICACHE_FLASH_ATTR initDone_cbn() {
	CFG_Load();
	initOutputs();
	initInputs();
	os_printf("Hostname was: %s\n", wifi_station_get_hostname());
	wifi_station_set_hostname(sysCfg.deviceName);
	WIFI_Connect(sysCfg.sta_ssid, sysCfg.sta_pwd, wifiConnectCb);
	os_printf("Hostname is: %s\n", wifi_station_get_hostname());
	os_printf("SDK version is: %s\n", system_get_sdk_version());
	os_printf("Smart-Config version is: %s\n", smartconfig_get_version());
	system_print_meminfo();
	os_printf("Flashsize map %d; id %lx\n", system_get_flash_size_map(), spi_flash_get_id());

	MQTT_InitConnection(&mqttClient, sysCfg.mqtt_host, sysCfg.mqtt_port, sysCfg.security);
	MQTT_InitClient(&mqttClient, sysCfg.device_id, sysCfg.mqtt_user, sysCfg.mqtt_pass,
			sysCfg.mqtt_keepalive, 1);

	char temp[100];
	os_sprintf(temp, "/Raw/%s/offline", sysCfg.device_id);
	MQTT_InitLWT(&mqttClient, temp, "offline", 0, 0);

	MQTT_OnConnected(&mqttClient, mqttConnectedCb);
	MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
	MQTT_OnData(&mqttClient, mqttDataCb);

	wifi_set_opmode(STATION_MODE);
	wifi_station_set_auto_connect(true);
	wifi_station_set_reconnect_policy(true);
	WIFI_Connect(sysCfg.sta_ssid, sysCfg.sta_pwd, wifiConnectCb);
}

void user_init(void) {
	stdout_init();
	gpio_init();

	system_init_done_cb(&initDone_cbn);
}
