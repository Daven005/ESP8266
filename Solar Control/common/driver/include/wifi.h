/*
 * wifi.h
 *
 *  Created on: Dec 30, 2014
 *      Author: Minh
 */

#ifndef USER_WIFI_H_
#define USER_WIFI_H_
#include "os_type.h"

typedef void (*startUpCb_t)(void);
typedef void (*WifiCallback)(uint8_t);
void WIFI_Connect(uint8_t* ssid, uint8_t* pass, uint8_t* deviceName, WifiCallback cb);
uint16 WIFI_Attempts(void);
char * getBestSSID(void);
void initWiFi(startUpCb_t startUp);
#endif /* USER_WIFI_H_ */
