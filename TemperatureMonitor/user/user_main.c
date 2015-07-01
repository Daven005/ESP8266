#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "easygpio.h"
#include "stdout.h"
#include "LCD.h"

#include "config.h"
#include "wifi.h"
#include "debug.h"
#include "mqtt.h"
#include "user_config.h"

#define sleepms(x) os_delay_us(x*1000);

LOCAL os_timer_t switch_timer;
LOCAL os_timer_t display_timer;
LOCAL os_timer_t mqtt_start_timer;
LOCAL void ICACHE_FLASH_ATTR mqtt_start_cb(void *arg);
static void switchAction1(void);
static void switchAction2(void);
static void switchAction3(void);
void lightOff(void);
void lightOn(void);

MQTT_Client mqttClient;
#define MAX_ENTRY 20
#define MAX_NAME 32
typedef struct {char location [MAX_NAME]; char device[MAX_NAME]; char name[MAX_NAME]; char type[10]; char val[10];} temps_t;
temps_t valueEntries[MAX_ENTRY]; // Assume initialised to 0
static unsigned int switchCount;
static unsigned int lightCount;
static startTemp;

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

void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status) {
	ets_uart_printf("WiFi status: %d\r\n", status);
	if (status == STATION_GOT_IP){
		MQTT_Connect(&mqttClient);
	} else {
		MQTT_Disconnect(&mqttClient);
	}
}

void ICACHE_FLASH_ATTR switchTimerCb(uint32_t *args) { // 100mS
	const swMax = 100;

	if (!easygpio_inputGet(Switch)) { // Switch is active LOW
		switchCount++;
		if (switchCount > 5)
			lightOn();
		if (switchCount > swMax)
			switchCount = swMax;
	} else {
		if (0 < switchCount && switchCount < 5) {
			switchAction1();
			if (lightCount >= 1)
				lightOn(); // Keep on if already on
		} else if (5 <= switchCount && switchCount <= 20){
			switchAction2();
		} else if (21 <= switchCount && switchCount <= 50){
			switchAction3();
		}
		switchCount = 0;
	}
	if (lightCount >= 1)
		lightCount--;
	else
		lightOff();
}

int ICACHE_FLASH_ATTR tempsCount(void) {
	int cnt = 0;
	while (valueEntries[cnt].type[0] != 0) cnt++;
	return cnt;
}

void ICACHE_FLASH_ATTR displayEntry(uint8 idx) {
	showString(0, 0, valueEntries[idx].location);
	showString(0, 1, valueEntries[idx].device);
	showString(0, 2, valueEntries[idx].name);
	showString(0, 3, valueEntries[idx].type);
	showString(4, 5, valueEntries[idx].val);
}

void ICACHE_FLASH_ATTR transmitCb(uint32_t *args) {
	clearLcd();
	if (strlen(valueEntries[startTemp].type) > 0) {
		displayEntry(startTemp);
	}
}

void ICACHE_FLASH_ATTR lightOn(void) {
	easygpio_outputSet(LCD_Light, 0);
	lightCount = 100; // 10 secs
}

void ICACHE_FLASH_ATTR lightOff(void) {
	easygpio_outputSet(LCD_Light, 1);
	lightCount = 0;
}

void ICACHE_FLASH_ATTR printEntry(int idx) {
	if (strlen(valueEntries[idx].type) > 0)
		os_printf(">> %d %s %s %s %s %s\n", idx, valueEntries[idx].location,
			valueEntries[idx].device, valueEntries[idx].name, valueEntries[idx].type,
			valueEntries[idx].val);
	else
		os_printf(">> %d -\n", idx);
}
void ICACHE_FLASH_ATTR switchAction1(void) { // 500mS
	int idx;
	for (idx = 0; idx < MAX_ENTRY; idx++) {
		if (++startTemp >= MAX_ENTRY) startTemp = 0;
		if (strlen(valueEntries[startTemp].type) > 0) {
			printEntry(startTemp);
			transmitCb(NULL);
			return;
		}
	}
}


void ICACHE_FLASH_ATTR switchAction2(void) { // 2 secs
	int idx;
	for (idx=0; idx<MAX_ENTRY; idx++) {
		printEntry(idx);
	}
}

void ICACHE_FLASH_ATTR switchAction3(void) { // 5 secs
	lightOff();
}

void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*)args;
	os_printf("MQTT: Connected to %s:%d\r\n", sysCfg.mqtt_host, sysCfg.mqtt_port);
	MQTT_Subscribe(client, "/App/#", 0);

	os_timer_disarm(&display_timer);
	os_timer_setfn(&display_timer, (os_timer_func_t *)transmitCb, NULL);
	os_timer_arm(&display_timer, 5000, true);

	os_timer_disarm(&switch_timer);
	os_timer_setfn(&switch_timer, (os_timer_func_t *)switchTimerCb, NULL);
	os_timer_arm(&switch_timer, 100, true);
}

void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args) {
//	MQTT_Client* client = (MQTT_Client*)args;
	MQTT_Connect(&mqttClient);
}

void ICACHE_FLASH_ATTR storeEntry(uint8 idx, char *location, char *device, char *sensor, char *type, char *value) {
	strcpy(valueEntries[idx].location, location);
	strcpy(valueEntries[idx].device, device);
	strcpy(valueEntries[idx].name, sensor);
	strcpy(valueEntries[idx].type, type);
	strcpy(valueEntries[idx].val, value);
}

int ICACHE_FLASH_ATTR findEntry(char *location, char *device, char *sensor, char *type) {
	int i;
	for (i=0; i<MAX_ENTRY; i++) {
		if (strcmp(sensor, valueEntries[i].name) == 0
			&& strcmp(device, valueEntries[i].device) == 0
			&& strcmp(location, valueEntries[i].location) == 0
			&& strcmp(type, valueEntries[i].type) == 0
			) return i;
	}
	return -1;
}

int ICACHE_FLASH_ATTR findSlot(void) {
	int i;
	for (i=0; i<MAX_ENTRY; i++) {
		if (strlen(valueEntries[i].type) == 0) {
			 return i;
		}
	}
	return -1;
}

int ICACHE_FLASH_ATTR compareData(uint8 idx1, uint8 idx2) {
	int result;
	if ((result = strcmp(valueEntries[idx1].type, valueEntries[idx2].type)) != 0)
		return result;
	if ((result = strcmp(valueEntries[idx1].location, valueEntries[idx2].location)) != 0)
		return result;
	if ((result = strcmp(valueEntries[idx1].device, valueEntries[idx2].device)) != 0)
		return result;
	return strcmp(valueEntries[idx1].name, valueEntries[idx2].name);
}

void ICACHE_FLASH_ATTR swapData(int idx1, int idx2) {
	temps_t swp;
	memcpy(&swp, &valueEntries[idx1], sizeof(temps_t));
	memcpy(&valueEntries[idx1], &valueEntries[idx2], sizeof(temps_t));
	memcpy(&valueEntries[idx2], &swp, sizeof(temps_t));
}

void ICACHE_FLASH_ATTR sortData(void) {
	int outerIdx, innerIdx, swapped = true;
	for (outerIdx=0; (outerIdx < MAX_ENTRY) && swapped; outerIdx++) {
		swapped = false;
		for (innerIdx=MAX_ENTRY; innerIdx > outerIdx+1; innerIdx--) {
			wdt_feed();
			if (compareData(innerIdx, innerIdx-1) > 0) {
				swapData(innerIdx, innerIdx-1);
				swapped = true;
			}
		}
	}
}

void ICACHE_FLASH_ATTR saveData(char *location, char *device, char *sensor, char *type, char *value) {
	int slot;

	if ((slot = findEntry(location, device, sensor, type)) >= 0) {
		strcpy(valueEntries[slot].type, type); // Update Value
		strcpy(valueEntries[slot].val, value);
	} else if ((slot = findSlot()) >= 0) {
		storeEntry(slot, location, device, sensor, type, value);
		sortData();
	} else {
		// Loose it
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

	if (tokenCount > 0 && strcmp("Raw", tokens[0])== 0) {
		if (tokenCount == 4 && strcmp(sysCfg.device_id, tokens[1] )== 0 && strcmp("set", tokens[2] )== 0) {
			if (strlen(dataBuf) < NAME_SIZE-1) {
				if (strcmp("name", tokens[3]) == 0) {
					strcpy(sysCfg.deviceName, dataBuf);
				} else if (strcmp("location", tokens[3]) == 0){
					strcpy(sysCfg.deviceLocation, dataBuf);
				} else if (strcmp("updates", tokens[3]) == 0){
					sysCfg.updates = atoi(dataBuf);
					os_timer_disarm(&mqtt_timer);
					os_timer_arm(&mqtt_timer, sysCfg.updates*1000, true);
				} else if (strcmp("inputs", tokens[3]) == 0){
					sysCfg.inputs = atoi(dataBuf);
				}
				publishDeviceInfo(client);
				CFG_Save();
			}
		}
	}
	os_free(topicBuf);
	os_free(dataBuf);
}

LOCAL void ICACHE_FLASH_ATTR mqtt_start_cb(void *arg) {
	os_printf("\r\nWiFi starting ...\r\n");
	easygpio_pinMode(LCD_Light, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_pinMode(LCD_SCE, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_pinMode(LCD_clk, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_pinMode(LCD_Data, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_pinMode(LCD_D_C, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_pinMode(LCD_RST, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_pinMode(Switch, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_outputDisable(Switch);

	lcdInit();
	showString(1, 1, "MQTT Monitor");
	CFG_Load();
	memset(valueEntries, 0, sizeof(valueEntries));

	MQTT_InitConnection(&mqttClient, sysCfg.mqtt_host, sysCfg.mqtt_port, sysCfg.security);
	MQTT_InitClient(&mqttClient, sysCfg.device_id, sysCfg.mqtt_user, sysCfg.mqtt_pass, sysCfg.mqtt_keepalive, 1);

	MQTT_InitLWT(&mqttClient, "/lwt", "offline", 0, 0);
	MQTT_OnConnected(&mqttClient, mqttConnectedCb);
	MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
	MQTT_OnData(&mqttClient, mqttDataCb);

	WIFI_Connect(sysCfg.sta_ssid, sysCfg.sta_pwd, wifiConnectCb);
}

void user_init(void) {
	lcdReset();
	stdout_init();
	gpio_init();
	os_delay_us(1000000);

	os_timer_disarm(&mqtt_start_timer);
	os_timer_setfn(&mqtt_start_timer, (os_timer_func_t *)mqtt_start_cb, (void *)0);
	os_timer_arm(&mqtt_start_timer, 1000, false);
}
