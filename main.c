#include <msp430.h>
#include <stdint.h>

#include "studiolib.h"
#include "hal_uart.h"
#include "hal_gpio.h"
#include "hal_system.h"
#include "Nbiot.h"

static void Clock_Init(void)
{
    CSCTL0_H = CSKEY_H;
    CSCTL1 = DCOFSEL_0;
    CSCTL2 = SELA__VLOCLK | SELS__DCOCLK | SELM__DCOCLK;
    CSCTL3 = DIVA__1 | DIVS__1 | DIVM__1;
    CSCTL0_H = 0;
}

void main(void)
{
    WDTCTL = WDTPW | WDTHOLD;

    Clock_Init();
    Gpio_Init();
    hal_uart_init();

    NbIot_Init();

    while (1)
    {
        NbIot_Task();
    }
}
