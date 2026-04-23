#ifndef HAL_HAL_UART_H_
#define HAL_HAL_UART_H_

#include <msp430.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    MODEM_BAUD_9600 = 0,
    MODEM_BAUD_115200
} ModemBaud_t;

typedef struct
{
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint16_t count;
    uint16_t size;
    volatile bool lock;
    uint8_t *buffer;
} DrvUart_RingBuffer_t;

void hal_uart_init(void);

/* Debug UART */
void hal_uart_initDebugPort(void);
bool hal_uart_DebugReadByte(uint8_t *data);

/* Sensor UART */
void hal_uart_initSensorPort(void);
void hal_uart_SensorWriteByte(uint8_t data);
void hal_uart_SensorWriteBuffer(const uint8_t *data, uint16_t len);
bool hal_uart_SensorReadByte(uint8_t *data);
void hal_uart_SensorFlush(void);

/* Modem UART */
void hal_uart_initModemPort(ModemBaud_t baud);
void hal_uart_ModemWriteByte(uint8_t data);
void hal_uart_ModemWriteString(const char *str);
bool hal_uart_ModemReadByte(uint8_t *data);
void hal_uart_ModemFlush(void);

#endif /* HAL_HAL_UART_H_ */
