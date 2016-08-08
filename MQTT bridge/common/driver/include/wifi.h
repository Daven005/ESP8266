/*
 * wifi.h
 *
 *  Created on: Dec 30, 2014
 *      Author: Minh
 */

#ifndef USER_WIFI_H_
#define USER_WIFI_H_
#include "os_type.h"
#include "user_interface.h"

typedef void (*InitWiFiCb_t)(void);
typedef void (*WifiCallback)(uint8_t);
void WIFI_Connect(uint8_t* ssid, uint8_t* pass, uint8_t* deviceName, WifiCallback cb);
uint16 WIFI_ConnectTime(void);
char *getBestSSID(void);
STATUS WiFiScanStatus(void);
uint8 WiFiConnectStatus(void);
char *WiFiIPaddress(void);
char *WiFiMACaddress(void);
void initWiFi(enum phy_mode phyMode, char *deviceName, char *ssidPrfx, InitWiFiCb_t startUp);

#endif /* USER_WIFI_H_ */
