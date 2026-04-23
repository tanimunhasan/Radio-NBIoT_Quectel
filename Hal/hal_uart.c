#include "hal_uart.h"
#include <msp430.h>

void hal_uart_initDebugPort(void)
{
    /* P2.0 = UCA3TXD, P2.1 = UCA3RXD */
    P2SEL1 |= BIT0 | BIT1;
    P2SEL0 &= ~(BIT0 | BIT1);

    P2DIR |= BIT0;
    P2DIR &= ~BIT1;

    UCA3CTLW0 = UCSWRST;
    UCA3CTLW0 |= UCSSEL__SMCLK;
    UCA3BRW = 6;
    UCA3MCTLW = 0x2081;
    UCA3CTLW0 &= ~UCSWRST;

    PM5CTL0 &= ~LOCKLPM5;
}

void hal_uart_initSensorPort(void)
{
    /* P4.3 = UCA0TXD, P4.4 = UCA0RXD */
    P4SEL1 &= ~(BIT3 | BIT4);
    P4SEL0 |= (BIT3 | BIT4);

    P4DIR |= BIT3;
    P4DIR &= ~BIT4;
    P4REN &= ~BIT4;

    UCA0CTLW0 = UCSWRST;
    UCA0CTLW0 |= UCSSEL__SMCLK;
    UCA0BRW = 6;
    UCA0MCTLW = 0x2081;
    UCA0CTLW0 &= ~UCSWRST;
    UCA0IE |= UCRXIE;
}

void hal_uart_initModemPort(ModemBaud_t baud)
{
    /* P5.0 = UCA2TXD, P5.1 = UCA2RXD */
    P5SEL1 |= BIT0 | BIT1;
    P5SEL0 &= ~(BIT0 | BIT1);

    P5DIR |= BIT0;
    P5DIR &= ~BIT1;

    UCA2CTLW0 = UCSWRST;
    UCA2CTLW0 |= UCSSEL__SMCLK;

    if (baud == MODEM_BAUD_9600)
    {
        /* Same style as your working debug UART setup */
        UCA2BRW = 6;
        UCA2MCTLW = 0x2081;
    }
    else
    {
        /* For higher-speed first sync attempt */
        UCA2BRW = 8;
        UCA2MCTLW = 0xD600;
    }

    UCA2CTLW0 &= ~UCSWRST;
}

void hal_uart_init(void)
{
    hal_uart_initDebugPort();
    hal_uart_initSensorPort();
    hal_uart_initModemPort(MODEM_BAUD_9600);
    hal_uart_SensorFlush();
    hal_uart_ModemFlush();
}

void hal_uart_SensorWriteByte(uint8_t data)
{
    while ((UCA0IFG & UCTXIFG) == 0) {}
    UCA0TXBUF = data;
}

void hal_uart_SensorWriteBuffer(const uint8_t *data, uint16_t len)
{
    uint16_t index;
    for (index = 0; index < len; index++)
    {
        hal_uart_SensorWriteByte(data[index]);
    }
}

bool hal_uart_SensorReadByte(uint8_t *data)
{
    if ((UCA0IFG & UCRXIFG) == 0)
    {
        return false;
    }

    *data = UCA0RXBUF;
    return true;
}

void hal_uart_SensorFlush(void)
{
    volatile uint8_t dummy;
    while (UCA0IFG & UCRXIFG)
    {
        dummy = UCA0RXBUF;
        (void)dummy;
    }
}

bool hal_uart_DebugReadByte(uint8_t *data)
{
    if ((UCA3IFG & UCRXIFG) == 0)
    {
        return false;
    }

    *data = UCA3RXBUF;
    return true;
}

void hal_uart_ModemWriteByte(uint8_t data)
{
    while ((UCA2IFG & UCTXIFG) == 0) {}
    UCA2TXBUF = data;
}

void hal_uart_ModemWriteString(const char *str)
{
    while (*str != '\0')
    {
        hal_uart_ModemWriteByte((uint8_t)(*str));
        str++;
    }
}

bool hal_uart_ModemReadByte(uint8_t *data)
{
    if ((UCA2IFG & UCRXIFG) == 0)
    {
        return false;
    }

    *data = UCA2RXBUF;
    return true;
}

void hal_uart_ModemFlush(void)
{
    volatile uint8_t dummy;
    while (UCA2IFG & UCRXIFG)
    {
        dummy = UCA2RXBUF;
        (void)dummy;
    }
}
