/*
 * hal_system.h
 *
 *  Created on: 31 Mar 2026
 *      Author: B4T
 */

#ifndef HAL_HAL_SYSTEM_H_
#define HAL_HAL_SYSTEM_H_

#include<stdio.h>
#include<stdint.h>
#include<msp430.h>
#include <stdbool.h>

typedef struct
{
    uint16_t baudRate;
    uint8_t dataBits;
    bool parityEnable;

}Uart_Config_t;


void baudRate(void);
#endif /* HAL_HAL_SYSTEM_H_ */
