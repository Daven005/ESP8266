/*
 * decodeWIFI.h
 *
 *  Created on: 7 Aug 2016
 *      Author: User
 */

#ifndef USER_INCLUDE_DECODEWIFI_H_
#define USER_INCLUDE_DECODEWIFI_H_

extern myMqttState_t myMqttState;

void setWiFiParams(char *cmd, char *bfr, jsmntok_t root[], int start, int max);
void getWiFiParams(char *cmd);

void WiFiScan(char *cmd);
void WiFiStatus(char *cmd);

void WiFiConnect(char *cmd, char *bfr, jsmntok_t root[], int start, int max);

#endif /* USER_INCLUDE_DECODEWIFI_H_ */
