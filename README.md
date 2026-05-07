# Radio-NBIoT_Quectel

Firmware project for establishing an NB-IoT connection between an MSP430FR5043 microcontroller and a Quectel BC660K NB-IoT modem. The firmware provides modem boot/configuration, UART bridge mode, pass-through AT command testing, UDP/TCP demo sending, basic socket recovery, and an office-style payload builder.

## Project Overview

This project is intended for embedded NB-IoT development where an MSP430FR5043 controls a Quectel BC660K modem using AT commands over UART. The firmware starts the modem, synchronises the baud rate, configures modem settings, and then enters a terminal/bridge mode where the user can send AT commands or run built-in UDP/TCP demo transmissions.

The code is written in C and structured for Texas Instruments Code Composer Studio (CCS).

## Main Features

- MSP430FR5043 firmware written in C
- Quectel BC660K modem control through UART
- Debug terminal interface over a separate UART
- Automatic modem boot and baud-rate synchronisation
- Modem pass-through mode for direct AT command testing
- UDP demo transmission
- TCP demo transmission
- Basic network readiness check before sending
- Simple modem recovery path for fatal errors
- Payload generation using IMEI, SIM ID, RSSI, and a temporary gas value
- Separate HAL layer for UART, GPIO, and system utilities

## Hardware Used

| Item | Description |
|---|---|
| MCU | Texas Instruments MSP430FR5043 |
| Modem | Quectel BC660K NB-IoT modem |
| Debug interface | USB-to-UART adapter for terminal/debug output |
| SIM/APN | NB-IoT SIM with matching APN configuration |
| IDE | Texas Instruments Code Composer Studio |

## Pin Mapping

The current firmware uses the following MSP430FR5043 pins:

| Function | MSP430 Pin | Notes |
|---|---:|---|
| Debug UART TX | P2.0 | UCA3TXD |
| Debug UART RX | P2.1 | UCA3RXD |
| Sensor UART TX | P4.3 | UCA0TXD, prepared for external sensor interface |
| Sensor UART RX | P4.4 | UCA0RXD, prepared for external sensor interface |
| Modem UART TX | P5.0 | UCA2TXD to modem RX |
| Modem UART RX | P5.1 | UCA2RXD from modem TX |
| Modem INT | P5.2 | Modem interrupt input |
| Modem RESET | P5.3 | Active-low reset control |
| Modem PKEY | P1.6 | Power key control |
| Modem PEN | P1.7 | Modem power enable |
| Heartbeat LED | P6.0 | Debug/status LED |

## UART Settings

| Port | Purpose | Baud Rate | Format |
|---|---|---:|---|
| UCA3 | Debug terminal | 9600 | 8N1 |
| UCA2 | BC660K modem | 9600 after sync | 8N1 |
| UCA0 | Sensor UART | 9600 | 8N1 |

The modem boot routine first tries 9600 baud. If the modem does not respond, it tries 115200 baud, sends `AT+IPR=9600`, then switches back to 9600 baud.

## Repository Structure

```text
Radio-NBIoT_Quectel/
├── Common/
│   ├── studiolib.c
│   └── studiolib.h
├── Hal/
│   ├── hal_gpio.c
│   ├── hal_gpio.h
│   ├── hal_system.c
│   ├── hal_system.h
│   ├── hal_uart.c
│   └── hal_uart.h
├── Protocol/
│   ├── Protocol.c
│   └── Protocol.h
├── Radio/
│   ├── Nbiot.c
│   └── Nbiot.h
├── targetConfigs/
├── lnk_msp430fr5043.cmd
├── main.c
├── user_config.h
├── .ccsproject
├── .cproject
└── .project
```

## Firmware Flow

At startup, `main.c` performs the following sequence:

1. Stops the watchdog timer.
2. Initialises the MSP430 clock.
3. Initialises GPIO.
4. Initialises the debug UART.
5. Calls `NbIot_Init()`.
6. Continuously calls `NbIot_Task()` inside the main loop.

The NB-IoT state machine then moves through these major states:

```text
BOOT -> INIT -> BRIDGE -> UDP_TEST / TCP_TEST -> BRIDGE
```

If a fatal modem or network error occurs, the firmware enters a recovery routine, toggles the heartbeat LED, resets modem state variables, and restarts from the boot state.

## Build Configuration

The main feature flags are located in `user_config.h`:

```c
#define ENABLE_PASSTHRU_MODE 1
#define USE_NBIOT_RADIO 1
```

| Macro | Purpose |
|---|---|
| `ENABLE_PASSTHRU_MODE` | Enables direct modem pass-through mode from the debug terminal |
| `USE_NBIOT_RADIO` | Enables the NB-IoT radio module code |

Network endpoint and APN settings are currently defined in `Radio/Nbiot.h`. Before using the firmware in another environment, update the server IP, UDP/TCP port, local port, and APN macros to match your own network/server configuration.

## Importing the Project into Code Composer Studio

1. Clone or download this repository.
2. Open Code Composer Studio.
3. Go to **File > Import**.
4. Select **Code Composer Studio > CCS Projects**.
5. Browse to the repository folder.
6. Select the discovered project.
7. Click **Finish**.
8. Build the project using the active configuration.
9. Flash the firmware to the MSP430FR5043 target board.

## Terminal Setup

Connect a USB-to-UART adapter to the debug UART pins:

| USB-UART Adapter | MSP430FR5043 Debug UART |
|---|---|
| TX | P2.1 / Debug RX |
| RX | P2.0 / Debug TX |
| GND | GND |

Open a serial terminal using:

```text
Baud rate : 9600
Data bits : 8
Parity    : None
Stop bits : 1
Flow Ctrl : None
```

After reset, the firmware prints the modem bridge banner and help menu.

## Terminal Commands

| Command | Description |
|---|---|
| `help` | Prints available terminal commands |
| `AT` | Sends a basic AT command to the modem |
| `ATI` | Reads modem identification |
| `AT+CGMR` | Reads modem firmware version |
| `AT+CEREG?` | Checks network registration status |
| `AT+CGATT?` | Checks packet attach status |
| `AT+CSQ` | Reads signal quality |
| `payload` | Builds and prints a payload preview |
| `udp` | Performs network check and sends one UDP demo packet |
| `tcp` | Performs network check and sends one TCP demo packet |
| `passthru` | Enters direct modem pass-through mode |
| `exitpt` | Exits pass-through mode |

In pass-through mode, typed AT commands are forwarded directly to the modem. Use `exitpt` to return to normal firmware command mode.

## Payload Format

The demo payload is built in this format:

```text
IMEI,index,flags,SIM_ID,RSSI HEX_BODY
```

Example format:

```text
000000000000000,0000,10,00000000000000000000,99 00FA
```

The current gas value is a temporary fake/demo value generated in firmware. Replace `NbIot_GetFakeGasValue()` or the payload-loading logic with real sensor data when integrating with a live sensor.

## UDP/TCP Demo Behaviour

When `udp` or `tcp` is entered from the terminal, the firmware:

1. Loads the office-style demo payload.
2. Checks whether the modem is alive.
3. Checks NB-IoT network registration.
4. Checks packet attach status.
5. Attempts one recovery if the network is not ready.
6. Creates the PDP context.
7. Opens a UDP or TCP socket.
8. Sends the payload.
9. Waits for response where applicable.
10. Closes the socket.
11. Returns to bridge mode.

