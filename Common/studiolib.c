/*
 * studiolib.c
 *
 *  Created on: 27 Mar 2026
 *      Author: B4T
 */
#include <hal_uart.h>
#include <stdio.h>
#include <stdint.h>
#include "studiolib.h"

void DEBUG_STRING(const char *str)
{
    hal_uart_DebugWriteString(str);
}


void UART_sendFloat(float value)
{
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "%.2f", value);
    DEBUG_STRING(buffer);
}

void UART_sendHex(uint8_t byte)
{
    char hexBuffer[3];
    hexBuffer[0] = ((byte >> 4) > 9) ? (char)((byte >> 4) + 'A' - 10) : (char)((byte >> 4) + '0');
    hexBuffer[1] = ((byte & 0x0F) > 9) ? (char)((byte & 0x0F) + 'A' - 10) : (char)((byte & 0x0F) + '0');
    hexBuffer[2] = '\0';
    DEBUG_STRING(hexBuffer);
}

void printInt(int value)
{
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "%d", value);
    DEBUG_STRING(buffer);
}

void printUInt32(uint32_t value)
{
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)value);
    DEBUG_STRING(buffer);
}


void UART_Debug_SendInt(int value)
{
    char buf[16];
    int i = 0;
    int isNegative = 0;

    if (value == 0)
    {
        hal_uart_DebugWriteChar('0');
        return;
    }

    if (value < 0)
    {
        isNegative = 1;
        value = -value;
    }

    while (value > 0 && i < (int)(sizeof(buf) - 1))
    {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    if (isNegative)
    {
        buf[i++] = '-';
    }

    while (i > 0)
    {
        hal_uart_DebugWriteChar(buf[--i]);
    }
}
