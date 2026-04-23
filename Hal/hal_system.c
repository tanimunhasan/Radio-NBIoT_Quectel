/*
 * hal_system.c
 *
 *  Created on: 31 Mar 2026
 *      Author: B4T
 */


#include<msp430.h>
#include "studiolib.h"
#include "hal_uart.h"
#include "hal_system.h"
#include <stdbool.h>


static Uart_Config_t uartConfig =
{
     .baudRate = 9600,
     .dataBits = 8,
     .parityEnable = false
};

void baudRate(void)
{
    DEBUG_STRING("\r\nBaud: ");
    UART_Debug_SendInt(uartConfig.baudRate);
    DEBUG_STRING("\r\n");
    DEBUG_STRING("\r\nData Bits :");
    UART_Debug_SendInt(uartConfig.dataBits);
    DEBUG_STRING("\r\n");
    DEBUG_STRING("\r\n Parity Enable :");
    UART_Debug_SendInt(uartConfig.parityEnable);
    DEBUG_STRING("\r\n");

}
