#include "Arduino.h"
/*
  MKR1010 Sensor Packet Sender + Terminal Bridge

  Purpose:
  - Startup in command mode
  - User can type commands from PC terminal
  - Local echo enabled
  - Backspace works
  - MKR1010 can send $SENSOR packet manually
  - MKR1010 can automatically send $SENSOR packet every 1 minute
  - MSP430 remains only gateway

  Wiring for current debug test:
  MKR1010 TX pin 14 -> MSP430 P2.1 RX
  MKR1010 RX pin 13 <- MSP430 P2.0 TX
  MKR1010 GND       -> MSP430 GND

  Final wiring later:
  MKR1010 TX pin 14 -> MSP430 P4.4 RX  sensor UART
  MKR1010 RX pin 13 <- MSP430 P4.3 TX  sensor UART
  MKR1010 GND       -> MSP430 GND
*/

/* auto on, auto off, manual packet send, multiple options: read sensor then send to msp(quectel)--> cloud  */

#define USB_BAUDRATE      9600
#define MSP430_BAUDRATE   9600

#define LINE_BUFFER_SIZE  128

const unsigned long AUTO_SEND_INTERVAL_MS = 60000UL; // 1 minute

char lineBuffer[LINE_BUFFER_SIZE];
uint8_t lineIndex = 0;

bool autoModeEnabled = false;
unsigned long lastAutoSendMs = 0;

uint16_t n2oValue = 145;
float temperatureValue = 22.6;
float humidityValue = 84.1;

void setup()
{
  Serial.begin(USB_BAUDRATE);
  Serial1.begin(MSP430_BAUDRATE);

  delay(1500);

  Serial.println();
  Serial.println("========================================");
  Serial.println("MKR1010 Sensor Packet Terminal");
  Serial.println("Mode: COMMAND");
  Serial.println("========================================");
  printHelp();
  printPrompt();
}

void loop()
{
  handleUsbTerminalInput();
  handleMsp430Response();
  handleAutoMode();
}

void handleUsbTerminalInput()
{
  while (Serial.available() > 0)
  {
    char c = Serial.read();

    if (c == 0x08 || c == 0x7F)
    {
      handleBackspace();
    }
    else if (c == '\r' || c == '\n')
    {
      handleEnter();
    }
    else if (c >= 32 && c <= 126)
    {
      handlePrintable(c);
    }
  }
}

void handlePrintable(char c)
{
  if (lineIndex < LINE_BUFFER_SIZE - 1)
  {
    lineBuffer[lineIndex++] = c;
    lineBuffer[lineIndex] = '\0';

    // Local echo
    Serial.write(c);
  }
  else
  {
    Serial.println();
    Serial.println("[MKR] ERROR: input buffer full");
    clearLineBuffer();
    printPrompt();
  }
}

void handleBackspace()
{
  if (lineIndex > 0)
  {
    lineIndex--;
    lineBuffer[lineIndex] = '\0';

    // Move cursor back, erase char, move cursor back again
    Serial.print("\b \b");
  }
}

void handleEnter()
{
  Serial.println();

  if (lineIndex > 0)
  {
    lineBuffer[lineIndex] = '\0';
    handleCommand(lineBuffer);
    clearLineBuffer();
  }

  printPrompt();
}

void handleCommand(char *cmd)
{
  trimString(cmd);

  if (strlen(cmd) == 0)
  {
    return;
  }

  if (strcmp(cmd, "help") == 0)
  {
    printHelp();
  }
  else if (strcmp(cmd, "send") == 0)
  {
    sendSensorPacket();
  }
  else if ((strcmp(cmd, "auto on") == 0) || (strcmp(cmd, "auto") == 0))
  {
    autoModeEnabled = true;

    Serial.println("[MKR] Automatic mode ENABLED");
    Serial.println("[MKR] Sending first packet now, then every 1 minute");

    sendSensorPacket();
    lastAutoSendMs = millis();
  }
  else if (strcmp(cmd, "auto off") == 0)
  {
    autoModeEnabled = false;
    Serial.println("[MKR] Automatic mode DISABLED");
  }
  else if (strcmp(cmd, "status") == 0)
  {
    printStatus();
  }
  else if (strncmp(cmd, "set n2o ", 8) == 0)
  {
    n2oValue = (uint16_t)atoi(cmd + 8);
    Serial.print("[MKR] N2O updated to ");
    Serial.print(n2oValue);
    Serial.println(" ppm");
  }
  else if (strncmp(cmd, "set temp ", 9) == 0)
  {
    temperatureValue = atof(cmd + 9);
    Serial.print("[MKR] Temperature updated to ");
    Serial.print(temperatureValue, 1);
    Serial.println(" C");
  }
  else if (strncmp(cmd, "set hum ", 8) == 0)
  {
    humidityValue = atof(cmd + 8);
    Serial.print("[MKR] Humidity updated to ");
    Serial.print(humidityValue, 1);
    Serial.println(" %");
  }
  else
  {
    /*
      Anything else is forwarded directly to MSP430.

      Examples:
      exitpt
      udp
      payload
      AT
      AT+CSQ
      $SENSOR,N2O=145,TEMP=22.6,HUM=84.1#
    */
    Serial.print("[MKR -> MSP430 RAW] ");
    Serial.println(cmd);

    Serial1.print(cmd);
    Serial1.print("\r\n");
  }
}

void handleAutoMode()
{
  if (!autoModeEnabled)
  {
    return;
  }

  if ((unsigned long)(millis() - lastAutoSendMs) >= AUTO_SEND_INTERVAL_MS)
  {
    lastAutoSendMs = millis();

    /*
      For testing, I slightly change the values each minute.
      Later, replace this section with your real sensor reading.
    */
    n2oValue++;
    temperatureValue += 0.1;
    humidityValue += 0.1;

    sendSensorPacket();
  }
}

void sendSensorPacket()
{
  String packet = "$SENSOR,N2O=" + String(n2oValue) +
                  ",TEMP=" + String(temperatureValue, 1) +
                  ",HUM=" + String(humidityValue, 1) + "#";

  Serial.print("[MKR -> MSP430 SENSOR] ");
  Serial.println(packet);

  Serial1.print(packet);
  Serial1.print("\r\n");
}

void handleMsp430Response()
{
  while (Serial1.available() > 0)
  {
    char c = Serial1.read();
    Serial.write(c);
  }
}

void printStatus()
{
  Serial.println();
  Serial.println("========== MKR STATUS ==========");

  if (autoModeEnabled)
  {
    Serial.println("Mode      : AUTOMATIC");
    Serial.println("Interval  : 60 seconds");
  }
  else
  {
    Serial.println("Mode      : COMMAND");
  }

  Serial.print("N2O       : ");
  Serial.print(n2oValue);
  Serial.println(" ppm");

  Serial.print("TEMP      : ");
  Serial.print(temperatureValue, 1);
  Serial.println(" C");

  Serial.print("HUM       : ");
  Serial.print(humidityValue, 1);
  Serial.println(" %");

  Serial.println("================================");
}

void printHelp()
{
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  send         -> send one sensor packet now");
  Serial.println("  auto on      -> send sensor packet every 1 minute");
  Serial.println("  auto off     -> stop automatic sending");
  Serial.println("  status       -> show MKR mode and current values");
  Serial.println("  set n2o 145  -> set N2O value");
  Serial.println("  set temp 22.6");
  Serial.println("  set hum 84.1");
  Serial.println("  help");
  Serial.println();
  Serial.println("Anything else is forwarded directly to MSP430.");
  Serial.println("Example sensor packet:");
  Serial.println("  $SENSOR,N2O=145,TEMP=22.6,HUM=84.1#");
  Serial.println();
}

void printPrompt()
{
  Serial.print("MKR> ");
}

void clearLineBuffer()
{
  memset(lineBuffer, 0, sizeof(lineBuffer));
  lineIndex = 0;
}

void trimString(char *str)
{
  int start = 0;
  int end = strlen(str) - 1;
  int i = 0;

  while (str[start] == ' ' || str[start] == '\t')
  {
    start++;
  }

  while (end >= start && (str[end] == ' ' || str[end] == '\t'))
  {
    str[end] = '\0';
    end--;
  }

  if (start > 0)
  {
    while (str[start] != '\0')
    {
      str[i++] = str[start++];
    }

    str[i] = '\0';
  }
}
