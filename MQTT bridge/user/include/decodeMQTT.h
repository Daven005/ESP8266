/*
 * decodeMQTT.h
 *
 *  Created on: 7 Aug 2016
 *      Author: User
 */

#ifndef USER_INCLUDE_DECODEMQTT_H_
#define USER_INCLUDE_DECODEMQTT_H_
#include "jsmn.h"
#include "decodeCommand.h"

extern myMqttState_t myMqttState;

void initClient(char *cmd, char *bfr, jsmntok_t root[], int start, int max);
void initConnection(char *cmd, char *bfr, jsmntok_t root[], int start, int max);
void initLWT(char *cmd, char *bfr, jsmntok_t root[], int start, int max);

void mqttConnect(char *cmd);
void mqttDisconnect(char *cmd);
void subscribe(char *cmd, char *bfr, jsmntok_t root[], int start, int max);
void unsubscribe(char *cmd, char *bfr, jsmntok_t root[], int start, int max);
void publish(char *cmd, char *bfr, jsmntok_t root[], int start, int max);
void mqttStatus(char *cmd);

#endif /* USER_INCLUDE_DECODEMQTT_H_ */
