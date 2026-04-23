#include "hal_gpio.h"
#include <msp430.h>

/* LED: P6.0 */
#define LED_DIR     P6DIR
#define LED_OUT     P6OUT
#define LED_PIN     BIT0

/* Modem control pins */
#define MDM_PKEY_DIR    P1DIR
#define MDM_PKEY_OUT    P1OUT
#define MDM_PKEY_PIN    BIT6

#define MDM_PEN_DIR     P1DIR
#define MDM_PEN_OUT     P1OUT
#define MDM_PEN_PIN     BIT7

#define MDM_INT_DIR     P5DIR
#define MDM_INT_IN      P5IN
#define MDM_INT_PIN     BIT2

#define MDM_RST_DIR     P5DIR
#define MDM_RST_OUT     P5OUT
#define MDM_RST_PIN     BIT3

void Gpio_Init(void)
{
    LED_DIR |= LED_PIN;
    LED_OUT &= ~LED_PIN;

    ModemGpio_Init();
}

void Gpio_ToggleHeartbeatLed(void)
{
    LED_OUT ^= LED_PIN;
}

void ModemGpio_Init(void)
{
    /* PKEY output - default low */
    MDM_PKEY_DIR |= MDM_PKEY_PIN;
    MDM_PKEY_OUT &= ~MDM_PKEY_PIN;

    /* PEN output - start low, enable later */
    MDM_PEN_DIR |= MDM_PEN_PIN;
    MDM_PEN_OUT &= ~MDM_PEN_PIN;

    /* RST output - inactive high */
    MDM_RST_DIR |= MDM_RST_PIN;
    MDM_RST_OUT |= MDM_RST_PIN;

    /* INT input */
    MDM_INT_DIR &= ~MDM_INT_PIN;
}

void Modem_PowerEnable(bool enable)
{
    if (enable)
        MDM_PEN_OUT |= MDM_PEN_PIN;
    else
        MDM_PEN_OUT &= ~MDM_PEN_PIN;
}

void Modem_ResetAssert(bool assertReset)
{
    if (assertReset)
        MDM_RST_OUT &= ~MDM_RST_PIN;
    else
        MDM_RST_OUT |= MDM_RST_PIN;
}

void Modem_PowerKeySet(bool level)
{
    if (level)
        MDM_PKEY_OUT |= MDM_PKEY_PIN;
    else
        MDM_PKEY_OUT &= ~MDM_PKEY_PIN;
}

bool Modem_IntRead(void)
{
    return (MDM_INT_IN & MDM_INT_PIN) ? true : false;
}
