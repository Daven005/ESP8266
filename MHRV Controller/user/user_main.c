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
#include "smartconfig.h"
#include "version.h"

#include "dht22.h"
#include "time.h"

static os_timer_t transmit_timer;
static os_timer_t process_timer;
static os_timer_t pir_timer[3];
static os_timer_t time_timer;
static os_timer_t switch_timer;
static os_timer_t flash_timer;
LOCAL os_timer_t date_timer;

static uint8 wifiChannel = 255;
static char bestSSID[33];

// For mapped Outputs
#define LED1 0
#define LED2 1
#define RELAY1 2
#define RELAY2 3
#define RELAY_ON 1
#define RELAY_OFF 0

#define LED_RED LED2
#define LED_GREEN LED1

#define SWITCH 0 // GPIO 00
#define PIN_DHT1 12
#define PIN_DHT2 14
#define PIN_PIR1 13
#define PIN_PIR2 16

enum speedSelect {
	OFF, SLOW, FAST
};

enum pir_t { PIR1=1, PIR2=2 };

bool pirStatus[3] = { false, false, false };
bool oldPIRstatus[3] = { false, false, false };
uint8 pirPins[3] = {0, PIN_PIR1, PIN_PIR2 };

static float externalTemperature = 10.0; // default to cool
static bool timeIsON = true; // Default to ON, ie daytime

static unsigned int switchCount;

MQTT_Client mqttClient;
uint8 mqttConnected;
const uint8 outputMap[MAX_OUTPUT] = { 2, 15, 4, 5 }; // LED1, LED2, RELAY1, RELAY2

bool currentOutputs[MAX_OUTPUT];
bool outputOverrides[MAX_OUTPUT];

// MQTT sensor IDs
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
enum {
	IDLE, RESTART, FLASH, IPSCAN, SWITCH_SCAN,
	INIT_DONE, MQTT_DATA_CB, MQTT_CONNECTED_CB, MQTT_DISCONNECTED_CB, SMART_CONFIG
} lastAction __attribute__ ((section (".noinit")));

uint32 ICACHE_FLASH_ATTR checkMinHeap(void) {
	static uint32 minHeap = 0xffffffff;
	uint32 heap = system_get_free_heap_size();
	if (heap < minHeap)
		minHeap = heap;
	return minHeap;
}

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
	easygpio_pinMode(PIN_DHT1, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_pinMode(PIN_DHT2, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_pinMode(PIN_PIR1, EASYGPIO_NOPULL, EASYGPIO_INPUT);
	easygpio_pinMode(PIN_PIR2, EASYGPIO_NOPULL, EASYGPIO_INPUT);
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
		INFOP("o/p %d(%d)->%d\n", id, outputMap[id], set);
	}
}

void ICACHE_FLASH_ATTR clrOverride(uint8 id) {
	if (id < MAX_OUTPUT) {
		outputOverrides[id] = false;
		easygpio_outputSet(outputMap[id], currentOutputs[id]);
		INFOP("o/p %d(%d)-->%d\n", id, outputMap[id], currentOutputs[id]);
	}
}

void ICACHE_FLASH_ATTR setLED(enum led_t led) {
	static enum led_t oldLED = DARK;
	if (led != oldLED) {
		TESTP("LED = %d", led);
		oldLED = led;
	}
	switch (led) {
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

void ICACHE_FLASH_ATTR startFlash(int t, int repeat) {
	setLED(RED);
	os_timer_disarm(&flash_timer);
	os_timer_setfn(&flash_timer, (os_timer_func_t *) smartConfigFlash_cb, (void *) 0);
	os_timer_arm(&flash_timer, t, repeat);
}

void ICACHE_FLASH_ATTR stopFlash(void) {
	setLED(DARK);
	os_timer_disarm(&flash_timer);
}

void ICACHE_FLASH_ATTR publishError(uint8 err, uint8 info) {
	static uint8 lastError = 255;
	static int lastInfo = -1;
	if (err == lastError && info == lastInfo)
		return; // Ignore repeated errors
	char *topic = (char*) os_malloc(50), *data = (char*) os_malloc(100);
	if (topic == NULL || data == NULL) {
		startFlash(50, true); // fast
		return;
	}
	os_sprintf(topic, (const char*) "/Raw/%s/error", sysCfg.device_id);
	os_sprintf(data, (const char*) "{ \"error\":%d, \"info\":%d}", err, info);
	MQTT_Publish(&mqttClient, topic, data, strlen(data), 0, 0);
	lastError = err;
	lastInfo = info;
	checkMinHeap();
	os_free(topic);
	os_free(data);
}

void ICACHE_FLASH_ATTR publishSensorData(MQTT_Client* client, uint8 sensor, char *type, int info) {
	if (mqttConnected) {
		char *topic = (char *) os_zalloc(100);
		char *data = (char *) os_zalloc(100);
		os_sprintf(topic, (const char*) "/Raw/%s/%d/info", sysCfg.device_id, sensor);
		os_sprintf(data, (const char*) "{ \"Type\":\"%s\", \"Value\":%d}", type, info);
		MQTT_Publish(client, topic, data, strlen(data), 0, false);
		TESTP("%s=>%s\n", topic, data);
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR publishDeviceInfo(MQTT_Client* client) {
	if (mqttConnected) {
		char *topic = (char *) os_zalloc(50);
		char *data = (char *) os_zalloc(300);
		int idx;
		struct ip_info ipConfig;

		if (topic == NULL || data == NULL) {
			startFlash(50, true); // fast
			return;
		}
		wifi_get_ip_info(STATION_IF, &ipConfig);
		os_sprintf(topic, "/Raw/%10s/info", sysCfg.device_id);
		os_sprintf(data,
				"{\"Name\":\"%s\", \"Location\":\"%s\", \"Version\":\"%s\", "
				"\"Updates\":%d, \"Inputs\":%d, \"RSSI\":%d, \"Channel\": %d, \"Attempts\": %d, ",
				sysCfg.deviceName, sysCfg.deviceLocation, version,
				sysCfg.updates, sysCfg.inputs, wifi_station_get_rssi(), wifiChannel, WIFI_Attempts());
		os_sprintf(data + strlen(data), "\"IPaddress\":\"%d.%d.%d.%d\"", IP2STR(&ipConfig.ip.addr));
		os_sprintf(data + strlen(data),", \"AP\":\"%s\"", bestSSID);
		os_sprintf(data + strlen(data),", \"Settings\":[");
		for (idx = 0; idx < SETTINGS_SIZE; idx++) {
			if (idx != 0)
				os_sprintf(data + strlen(data), ", ");
			os_sprintf(data + strlen(data), "%d", sysCfg.settings[idx]);
		}
		os_sprintf(data + strlen(data), "]}");
		MQTT_Publish(client, topic, data, strlen(data), 0, true);
		INFOP("%s=>%s\n", topic, data);
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR publishDeviceReset(MQTT_Client* client) {
	if (mqttConnected) {
		char *topic = (char *) os_zalloc(50);
		char *data = (char *) os_zalloc(100);
		int idx;

		os_sprintf(topic, "/Raw/%10s/reset", sysCfg.device_id);
		os_sprintf(data,
				"{\"Name\":\"%s\", \"Location\":\"%s\", \"Version\":\"%s\", \"Reason\":%d, \"LastAction\":%d}",
				sysCfg.deviceName, sysCfg.deviceLocation, version, system_get_rst_info()->reason,
				lastAction);
		INFOP("%s=>%s\n", topic, data);
		MQTT_Publish(client, topic, data, strlen(data), 0, false);
		checkMinHeap();
		os_free(topic);
		os_free(data);
	}
}

void ICACHE_FLASH_ATTR publishData(MQTT_Client* client) {
	if (mqttConnected) {
		struct dht_sensor_data* r1 = dht1Read();
		struct dht_sensor_data* r2 = dht2Read();

		if (r1->success) {
			publishSensorData(client, SENSOR_TEMPERATURE1, "Temp", (int)r1->temperature);
			publishSensorData(client, SENSOR_HUMIDITY1, "Hum", (int)r1->humidity);
		} else {
			publishError(1, 1);
		}
		if (r2->success) {
			publishSensorData(client, SENSOR_TEMPERATURE2, "Temp", (int)r2->temperature);
			publishSensorData(client, SENSOR_HUMIDITY2, "Hum", (int)r2->humidity);
		} else {
			publishError(1, 2);
		}
	}
}

void ICACHE_FLASH_ATTR printAll(void) {
	uint8 idx;
	struct dht_sensor_data* r1 = dht1Read();
	struct dht_sensor_data* r2 = dht2Read();

	for (idx=PIR1; idx<=PIR2; idx++) {
		os_printf("PIR[%d]: %d (%d)\n", idx, pirStatus[idx], easygpio_inputGet(pirPins[idx]));
	}
	os_printf("OP: ");
	for (idx=0; idx<MAX_OUTPUT; idx++) {
		os_printf(" %d", currentOutputs[idx]);
	}
	os_printf("\n");

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

void ICACHE_FLASH_ATTR pirCb(uint32 args) {
	enum pir_t pir = args;
	publishSensorData(&mqttClient, SENSOR_PIR1 - 1 + pir, "PIR", 0);
	pirStatus[pir] = false;
}

void ICACHE_FLASH_ATTR timeOnCb(void) {
	os_printf("time Timeout\n");
	timeIsON = true; // default to ON if no time message being received
	externalTemperature = 0.0; // Assume if date not sent then nor is Temp
}

void ICACHE_FLASH_ATTR setPirActive(enum pir_t pir) {
	pirStatus[pir] = true;
	if (pirStatus[pir] != oldPIRstatus[pir]) {
		TESTP("PIR:%d ", pir);
		publishSensorData(&mqttClient, SENSOR_PIR1-1+pir, "PIR", 1);
		oldPIRstatus[pir] = true;
	}
	os_timer_disarm(&pir_timer[pir]);
	os_timer_setfn(&pir_timer[pir], (os_timer_func_t *) pirCb, pir);
	os_timer_arm(&pir_timer[pir], sysCfg.settings[SETTING_PIR1_ON_TIME-1+pir]*1000*60, false); // Minutes
}

bool ICACHE_FLASH_ATTR checkPirActive(enum pir_t actionPir) {
	enum pir_t pir;

	for (pir = PIR1; pir <= PIR2; pir++) {
		if (easygpio_inputGet(pirPins[pir])) {
			setPirActive(pir);
		}
	}
	switch (actionPir) {
	case PIR1:
	case PIR2:
		return pirStatus[actionPir];
	}
	publishError(2, actionPir);
	return false;
}

void ICACHE_FLASH_ATTR clearPirActive(enum pir_t pir) {
	pirStatus[pir] = false;
}

void ICACHE_FLASH_ATTR setSpeed(enum speedSelect speed) {
	static enum speedSelect oldSpeed;
	if (speed != oldSpeed) {
		TESTP("relay:%d\n", speed);
		oldSpeed = speed;
	}
	switch (speed) {
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
	if (checkPirActive(sysCfg.settings[SETTING_PIR_ACTION])) {
		INFOP("<PIR>");
		setSpeed(FAST);
		setLED(RED);
	} else if (checkHumidityHigh()) {
		INFOP("<humidity>");
		setSpeed(FAST);
		setLED(GREEN);
	} else if (checkTemperatureHigh()) {
		INFOP("<temperature>");
		setSpeed(FAST);
		setLED(YELLOW);
	} else if (timeIsON) {
		INFOP("<TIME>");
		setSpeed(SLOW);
		setLED(DARK);
	} else {
		INFOP("<OFF>");
		setSpeed(OFF);
		setLED(DARK);
	}
	lastAction = IPSCAN;
}

void ICACHE_FLASH_ATTR transmitCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*) args;
	processData();
	publishData(client);
}

void ICACHE_FLASH_ATTR smartConfig_done(sc_status status, void *pdata) {
	switch (status) {
	case SC_STATUS_WAIT:
		INFOP("SC_STATUS_WAIT\n");
		break;
	case SC_STATUS_FIND_CHANNEL:
		INFOP("SC_STATUS_FIND_CHANNEL\n");
		break;
	case SC_STATUS_GETTING_SSID_PSWD:
		INFOP("SC_STATUS_GETTING_SSID_PSWD\n");
		break;
	case SC_STATUS_LINK:
		INFOP("SC_STATUS_LINK\n");
		struct station_config *sta_conf = pdata;
		wifi_station_set_config(sta_conf);
		INFOP("Connected to %s (%s) %d", sta_conf->ssid, sta_conf->password, sta_conf->bssid_set);
		strcpy(sysCfg.sta_ssid, sta_conf->ssid);
		strcpy(sysCfg.sta_pwd, sta_conf->password);
		wifi_station_disconnect();
		wifi_station_connect();
		break;
	case SC_STATUS_LINK_OVER:
		INFOP("SC_STATUS_LINK_OVER\n");
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
		INFOP("Finished smartConfig\n");
		stopFlash();
		doingSmartConfig = false;
		MQTT_Connect(&mqttClient);
		break;
	case SC_TOGGLE:
		if (doingSmartConfig) {
			INFOP("Stop smartConfig\n");
			stopFlash();
			smartconfig_stop();
			doingSmartConfig = false;
			wifi_station_disconnect();
			wifi_station_connect();
			MQTT_Connect(&mqttClient);
		} else {
			INFOP("Start smartConfig\n");
			MQTT_Disconnect(&mqttClient);
			mqttConnected = false;
			startFlash(100, true);
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
		setPirActive(sysCfg.settings[SETTING_PIR_ACTION]);
		break;
	case 2:
		clearPirActive(sysCfg.settings[SETTING_PIR_ACTION]);
		break;
	case 3:
		printAll();
		break;
	case 5:
		checkSmartConfig(SC_TOGGLE);
		break;
	}
}

void ICACHE_FLASH_ATTR switchTimerCb(uint32_t *args) {
	const int swOnMax = 100;
	const int swOffMax = 5;
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
	applyDST(timeInfo);
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
			INFOP("Setting %d = %d\n", id, sysCfg.settings[id]);
			publishDeviceInfo(client);
		}
	}
}

void ICACHE_FLASH_ATTR mqttDataCb(uint32_t *args, const char* topicBuf, uint32_t topic_len,
		const char *dataBuf, uint32_t data_len) {
	char *topic = (char*) os_zalloc(topic_len + 1), *data = (char*) os_zalloc(data_len + 1);
	char *tokens[10];

	MQTT_Client* client = (MQTT_Client*) args;

	os_memcpy(topic, topicBuf, topic_len);
	topic[topic_len] = 0;
	os_memcpy(data, dataBuf, data_len);
	data[data_len] = 0;
	INFOP("Receive topic: %s, data: %s \r\n", topic, data);

	int tokenCount = splitString((char *) topic, '/', tokens);

	if (tokenCount > 0) {
		if (strcmp("Raw", tokens[0]) == 0) {
			if (tokenCount == 4 && strcmp(sysCfg.device_id, tokens[1]) == 0
					&& strcmp("set", tokens[2]) == 0) {
				setDeviceParameters(data, tokens[3], client);
			} else if (tokenCount == 5 && strcmp(sysCfg.device_id, tokens[1]) == 0) {
				if (strcmp("set", tokens[3]) == 0) {
					int value = atoi(data);
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
				setTime(data);
				os_timer_disarm(&date_timer); // Restart it
				os_timer_arm(&date_timer, 10 * 60 * 1000, false); //10 minutes
			} else if (tokenCount >= 3 && strcmp("Temp", tokens[1]) == 0
					&& strcmp("current", tokens[2]) == 0) {
				setExternalTemperature(data);
			}
		}
	}
	checkMinHeap();
	os_free(topic);
	os_free(data);
	lastAction = MQTT_DATA_CB;
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

	dht1Init(sysCfg.settings[SETTING_DHT1], PIN_DHT1, 2000);
	dht2Init(sysCfg.settings[SETTING_DHT2], PIN_DHT2, 2000);

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
	lastAction = MQTT_CONNECTED_CB;
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

	wifi_set_opmode(STATION_MODE);
	wifi_station_set_auto_connect(true);
	wifi_station_set_reconnect_policy(true);
	WIFI_Connect(bestSSID, sysCfg.sta_pwd, sysCfg.deviceName, wifiConnectCb);
	lastAction = INIT_DONE;
}

static void ICACHE_FLASH_ATTR wifi_station_scan_done(void *arg, STATUS status) {
  uint8 ssid[33];
  int8 bestRSSI = -100;

  if (status == OK) {
    struct bss_info *bss_link = (struct bss_info *)arg;

    while (bss_link != NULL) {
      os_memset(ssid, 0, 33);
      if (os_strlen(bss_link->ssid) <= 32) {
        os_memcpy(ssid, bss_link->ssid, os_strlen(bss_link->ssid));
      } else {
        os_memcpy(ssid, bss_link->ssid, 32);
      }
      if (bss_link->rssi > bestRSSI) {
    	  bestRSSI = bss_link->rssi;
    	  strcpy(bestSSID, ssid);
      }
      TESTP("WiFi Scan: (%d,\"%s\",%d) best is %s\n", bss_link->authmode, ssid, bss_link->rssi, bestSSID);
      bss_link = bss_link->next.stqe_next;
    }
  } else {
	  os_printf("wifi_station_scan fail %d\n", status);
  }
  startUp();
}

static void ICACHE_FLASH_ATTR initDone_cb() {
	TESTP("Start WiFi Scan\n");
	wifi_set_opmode(STATION_MODE);
	wifi_station_scan(NULL, wifi_station_scan_done);
}

void user_init(void) {
	stdout_init();
	gpio_init();
	initOutputs();
	initInputs();

	system_init_done_cb(&initDone_cb);
}
