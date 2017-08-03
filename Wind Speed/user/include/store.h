/*
 * store.h
 *
 *  Created on: 25 Jul 2017
 *      Author: User
 */

#ifndef USER_INCLUDE_STORE_H_
#define USER_INCLUDE_STORE_H_

typedef void (*storeCb_t)(void);

#define MAX_DATA 64
#define MAGIC 0x35aabcd1L

typedef struct {
	time_t lastTimeSync;
	uint16 recCount;
	uint16 recFirst;
	uint16 recLast;
	uint32 magic;
	uint16 padding;
} StoreHeading_t;

typedef struct {
	uint16 minWind;
	uint16 maxWind;
	uint16 avgWind;
	uint16 cutinWind;
	time_t time;
	uint32 padding;
} DataRec_t;

void printHeading(char *msg);
void printRecords(void);
bool isHeaderValid(void);
bool initStore(time_t now);
uint16 recCount(void);
bool enqRec(DataRec_t *data , storeCb_t cb);
bool deqRec(DataRec_t *data);
time_t getLastTimeSync(void);
void setLastTimeSync(time_t t);

#endif /* USER_INCLUDE_STORE_H_ */
