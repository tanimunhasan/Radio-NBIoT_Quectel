# MKR1010 to MSP430FR5043 NB-IoT Gateway Integration

## Overview

This project uses an **Arduino MKR1010** as the sensor-reading controller and an **MSP430FR5043 + Quectel BC660K custom PCB** as the NB-IoT communication gateway.

The MKR1010 reads or generates sensor values, formats them into a UART packet, and sends the packet to the MSP430FR5043. The MSP430FR5043 parses the packet, builds the NB-IoT payload, and sends it to the server through the Quectel BC660K modem.

```text
Sensor / Test Values
        ↓
Arduino MKR1010
        ↓ UART
MSP430FR5043 Custom PCB
        ↓ UART
Quectel BC660K NB-IoT Modem
        ↓
Server
```

---

## Purpose of the MKR1010 Code

The MKR1010 code works as a **sensor packet sender** and **terminal bridge**.

It supports two operating modes:

1. **Command Mode**
   - User types commands from the PC terminal.
   - MKR1010 sends one packet manually when requested.
   - MKR1010 can forward raw commands to the MSP430.

2. **Automatic Mode**
   - User enables automatic sending using `auto on`.
   - MKR1010 sends the sensor packet every 1 minute.
   - User can stop automatic sending using `auto off`.

The MSP430FR5043 remains the gateway. The MKR1010 controls when sensor packets are sent.

---

## UART Connections

### Current Test Connection

For testing through the MSP430 debug UART:

```text
MKR1010 TX pin 14  →  MSP430 P2.1 RX
MKR1010 RX pin 13  ←  MSP430 P2.0 TX
MKR1010 GND        ↔  MSP430 GND
```

This allows the MKR1010 to act like a terminal and send commands or sensor packets to the MSP430.

---

### Final Automatic Integration Connection

For the final system, connect the MKR1010 to the MSP430 sensor UART:

```text
MKR1010 TX pin 14  →  MSP430 P4.4 RX  sensor UART
MKR1010 RX pin 13  ←  MSP430 P4.3 TX  sensor UART
MKR1010 GND        ↔  MSP430 GND
```

The PC/debug terminal can remain connected to the MSP430 debug UART:

```text
USB-TTL TX  →  MSP430 P2.1 RX
USB-TTL RX  ←  MSP430 P2.0 TX
USB-TTL GND ↔  MSP430 GND
```

---

## Important UART Warning

Do not connect two TX pins to the same MSP430 RX pin.

Wrong connection:

```text
MKR1010 TX  ┐
            ├── MSP430 RX    ❌ Not allowed
USB-TTL TX  ┘
```

This creates a UART conflict because two devices are trying to drive the same RX line.

Correct monitor-only setup:

```text
MKR1010 TX      →  MSP430 RX
MSP430 TX       →  MKR1010 RX
MSP430 TX       →  USB-TTL RX monitor only
USB-TTL TX      =  Not connected
All GND         ↔  Common GND
```

---

## Baud Rate

The current project uses:

```text
MKR1010 USB Serial terminal : 9600 baud
MKR1010 Serial1 to MSP430   : 9600 baud
MSP430 debug UART           : 9600 baud
MSP430 sensor UART          : 9600 baud
```

If the terminal shows unreadable characters, check that the terminal baud rate matches the code.

---

## Sensor Packet Format

The MKR1010 sends a packet in this format:

```text
$SENSOR,N2O=145,TEMP=22.6,HUM=84.1#
```

Packet fields:

| Field | Meaning | Example |
|---|---|---:|
| `N2O` | Nitrous oxide value in ppm | `145` |
| `TEMP` | Temperature in Celsius | `22.6` |
| `HUM` | Humidity in percentage | `84.1` |

The `$` character marks the start of the packet.

The `#` character marks the end of the packet.

---

## Hex Payload Example

For this packet:

```text
$SENSOR,N2O=145,TEMP=22.6,HUM=84.1#
```

The MSP430 converts the values into this hex body:

```text
009100E20349
```

Breakdown:

```text
00 91  00 E2  03 49
```

| Value | Decimal | Hex |
|---|---:|---:|
| N2O = 145 ppm | 145 | `0091` |
| Temperature = 22.6 × 10 | 226 | `00E2` |
| Humidity = 84.1 × 10 | 841 | `0349` |

So:

```text
0091 + 00E2 + 0349 = 009100E20349
```

---

## MKR1010 Terminal Commands

After flashing the MKR1010 code, open a serial terminal to the MKR1010 USB port.

Recommended terminal settings:

```text
Baud rate   : 9600
Line ending : No line ending
```

Available commands:

| Command | Description |
|---|---|
| `send` | Send one sensor packet immediately |
| `auto on` | Enable automatic sending every 1 minute |
| `auto off` | Disable automatic sending |
| `status` | Show current MKR1010 mode and sensor values |
| `set n2o 145` | Set the test N2O value |
| `set temp 22.6` | Set the test temperature value |
| `set hum 84.1` | Set the test humidity value |
| `help` | Show command list |

Any unknown command is forwarded directly to the MSP430.

Examples of forwarded commands:

```text
udp
payload
AT
AT+CSQ
$SENSOR,N2O=145,TEMP=22.6,HUM=84.1#
```

---

## Command Mode Test

After uploading the MKR1010 code:

1. Open the MKR1010 terminal.
2. Type:

```text
send
```

Expected output:

```text
[MKR -> MSP430 SENSOR] $SENSOR,N2O=145,TEMP=22.6,HUM=84.1#
```

The MSP430 should receive the packet, parse it, build the NB-IoT payload, and send it through the BC660K modem.

---

## Automatic Mode Test

To enable automatic packet sending:

```text
auto on
```

Expected output:

```text
[MKR] Automatic mode ENABLED
[MKR] Sending first packet now, then every 1 minute
[MKR -> MSP430 SENSOR] $SENSOR,N2O=145,TEMP=22.6,HUM=84.1#
```

After that, the MKR1010 sends one packet every 60 seconds.

To stop automatic sending:

```text
auto off
```

---

## Changing Test Values

You can change the test values from the MKR1010 terminal.

Example:

```text
set n2o 200
set temp 24.5
set hum 80.2
send
```

The MKR1010 will send:

```text
$SENSOR,N2O=200,TEMP=24.5,HUM=80.2#
```

---

## Recommended Development Steps

### Step 1: Test MKR1010 with MSP430 Debug UART

Use this wiring first:

```text
MKR1010 TX pin 14  →  MSP430 P2.1 RX
MKR1010 RX pin 13  ←  MSP430 P2.0 TX
MKR1010 GND        ↔  MSP430 GND
```

Then type:

```text
send
```

Confirm that the MSP430 receives the `$SENSOR...#` packet and sends the payload.

---

### Step 2: Enable MKR1010 Automatic Mode

Type:

```text
auto on
```

Confirm that the MKR1010 sends a packet every 1 minute.

---

### Step 3: Move MKR1010 to MSP430 Sensor UART

After testing, move the MKR1010 UART to the MSP430 sensor UART:

```text
MKR1010 TX pin 14  →  MSP430 P4.4 RX
MKR1010 RX pin 13  ←  MSP430 P4.3 TX
MKR1010 GND        ↔  MSP430 GND
```

Use the MSP430 debug UART only for monitoring and command input.

---

### Step 4: Replace Dummy Values with Real Sensor Values

In the MKR1010 code, replace the test variables:

```cpp
uint16_t n2oValue = 145;
float temperatureValue = 22.6;
float humidityValue = 84.1;
```

with real sensor readings.

The packet generation function should remain the same:

```cpp
String packet = "$SENSOR,N2O=" + String(n2oValue) +
                ",TEMP=" + String(temperatureValue, 1) +
                ",HUM=" + String(humidityValue, 1) + "#";
```

---

## Expected MSP430 Behaviour

When the MSP430 receives this:

```text
$SENSOR,N2O=145,TEMP=22.6,HUM=84.1#
```

It should:

1. Detect the packet.
2. Parse N2O, temperature, and humidity.
3. Store the latest values.
4. Build the payload.
5. Send it through the BC660K modem.
6. Print debug information.

Expected debug output:

```text
[MKR CMD] Sensor packet received: $SENSOR,N2O=145,TEMP=22.6,HUM=84.1#
[MKR CMD] Parsed OK: N2O=145 ppm, TEMPx10=226, HUMx10=841
[MKR CMD] Starting UDP send with MKR sensor payload...
```

Expected payload body:

```text
009100E20349
```

---

## Troubleshooting

### 1. MKR1010 terminal works, but MSP430 does not respond

Check UART wiring:

```text
MKR1010 TX must go to MSP430 RX
MKR1010 RX must go to MSP430 TX
GND must be common
```

Also check that no second TX device is connected to the same MSP430 RX pin.

---

### 2. MSP430 shows `ERROR` when typing `udp`

This can happen if the MSP430 is in modem pass-through mode and forwards `udp` directly to the BC660K modem.

Use the correct MSP430 command mode flow, or send the `$SENSOR...#` packet if your MSP430 firmware has special sensor packet handling enabled.

---

### 3. Backspace does not work correctly

Use a real serial terminal such as:

```text
Tera Term
PuTTY
CoolTerm
```

Arduino Serial Monitor may handle backspace differently depending on version.

---

### 4. Payload sends, but server does not decode temperature and humidity

The server may currently expect only one 2-byte N2O value.

Current full body format:

```text
N2O + Temperature + Humidity
```

Example:

```text
009100E20349
```

If the server expects only N2O, use only:

```text
0091
```

or update the server-side parser to decode all three values.

---

## Final System Summary

The final system should work like this:

```text
MKR1010:
  - Reads sensor values
  - Sends $SENSOR packet every 1 minute
  - Allows command/manual mode from USB terminal

MSP430FR5043:
  - Receives $SENSOR packet
  - Parses values
  - Builds NB-IoT payload
  - Sends payload through BC660K modem

BC660K:
  - Sends packet to cloud/server over NB-IoT
```

Final data flow:

```text
MKR1010 → MSP430FR5043 → Quectel BC660K → Server
```
