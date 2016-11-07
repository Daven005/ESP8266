/*
 * check.h
 *
 *  Created on: 18 Jul 2016
 *      Author: User
 */

#ifndef DRIVER_INCLUDE_CHECK_H_
#define DRIVER_INCLUDE_CHECK_H_

void dump(uint8 *p, uint8 sz);
uint32 checkMinHeap(void);
void showTime(char *func, uint32 previous);
void checkTime(char *func, uint32 previous);
void checkTimeFunc(char *func, uint32 previous);
bool assert_true(char *s, bool condition);
void assert_equal(char *s, int a, int b);

#endif /* DRIVER_INCLUDE_CHECK_H_ */
