/*
 * Protocol.c
 *
 *  Created on: 30 Mar 2026
 *      Author: B4T
 */

#include <msp430.h>
#include "Protocol.h"
#include <stdint.h>
#include <string.h>
#include <studiolib.h>

Car myCar;

void Protocol_Init(void)
{
    myCar.engine.rpm = 3000;
    myCar.speed = 60;
    myCar.engine.weight = 200;
    DEBUG_STRING("\r\nInit Done");

}

void Protocol_Update(void)
{
    myCar.engine.rpm += 500;
    myCar.speed +=10;
    myCar.engine.weight +=10;
    DEBUG_STRING("\r\nUpdated values");

}

void Protocol_PrintStatus()
{
    DEBUG_STRING("\r\nCar Status");
    DEBUG_STRING("\r\nEngine RPM:");
    UART_Debug_SendInt(myCar.engine.rpm);

    DEBUG_STRING("\r\n");

    DEBUG_STRING("\r\nSpeed: ");
    UART_Debug_SendInt(myCar.speed);
    DEBUG_STRING("\r\nWeight: ");
    UART_Debug_SendInt(myCar.engine.weight);


}
