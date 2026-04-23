#ifndef HAL_HAL_GPIO_H_
#define HAL_HAL_GPIO_H_

#include <stdbool.h>
#include <stdint.h>

void Gpio_Init(void);
void Gpio_ToggleHeartbeatLed(void);

/* Modem control */
void ModemGpio_Init(void);
void Modem_PowerEnable(bool enable);
void Modem_ResetAssert(bool assertReset);
void Modem_PowerKeySet(bool level);
bool Modem_IntRead(void);

#endif /* HAL_HAL_GPIO_H_ */
