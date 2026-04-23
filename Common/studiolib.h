/*
 * studiolib.h
 *
 *  Created on: 27 Mar 2026
 *      Author: B4T
 */

#ifndef COMMON_STUDIOLIB_H_
#define COMMON_STUDIOLIB_H_

#include <stdint.h>

void DEBUG_STRING(const char *str);
void UART_Debug_SendInt(int value);
void printInt(int value);
void UART_sendHex(uint8_t byte);
void UART_sendFloat(float value);
void printUInt32(uint32_t value);


void hal_uart_debug_write_char(char c);

void hal_uart_DebugWriteString(const char *str);
#endif /* COMMON_STUDIOLIB_H_ */
