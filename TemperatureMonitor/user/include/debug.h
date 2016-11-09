/*
 * debug.h
 *
 *  Created on: Dec 4, 2014
 *      Author: Minh
 */

#ifndef USER_DEBUG_H_
#define USER_DEBUG_H_


#ifdef DEBUG_OVERRIDE
#pragma message "DEBUG_OVERRIDE"
#define DEBUG 3
#else
#define DEBUG 2 // Normal case
#endif

#ifndef INFOP
#if DEBUG>=3
#define INFOP(...) os_printf(__VA_ARGS__);
#else
#define INFOP(...)
#endif
#endif

#ifndef INFO
#if DEBUG>=3
#define INFO(x) do { x; } while (0)
#else
#define INFO(x)
#endif
#endif

#ifndef TESTP
#if DEBUG>=2
#define TESTP(...) os_printf(__VA_ARGS__);
#else
#define TESTP(...)
#endif
#endif

#ifndef ERRORP
#if DEBUG>=1
#define ERRORP(...) os_printf(__VA_ARGS__);
#else
#define ERRORP(...)
#endif
#endif

#ifndef TEST
#if DEBUG>=2
#define TEST(x) do { x; } while (0)
#else
#define TEST(x)
#endif
#endif

#endif /* USER_DEBUG_H_ */
