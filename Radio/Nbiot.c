#include "Nbiot.h"
#include "user_config.h"

#include <string.h>
#include <stdio.h>
#include <msp430.h>

#if (USE_NBIOT_RADIO)

#include "studiolib.h"
#include "hal_uart.h"
#include "hal_gpio.h"
#include "hal_system.h"

#ifndef ENABLE_PASSTHRU_MODE
#define ENABLE_PASSTHRU_MODE   0
#endif

/* -------------------------------------------------
 * local configuration
 * ------------------------------------------------- */
#define NBIOT_CMD_BUFFER_LEN          128U
#define NBIOT_WORK_BUFFER_LEN         256U
#define NBIOT_PAYLOAD_BUFFER_LEN      256U
#define NBIOT_RECV_LINE_LEN           192U
#define NBIOT_RESP_BUFFER_LEN         256U
#define NBIOT_SENSOR_PACKET_LEN       96U

#define NBIOT_SHORT_TIMEOUT_MS        1200U
#define NBIOT_MED_TIMEOUT_MS          2500U
#define NBIOT_LONG_TIMEOUT_MS         15000U
#define NBIOT_BOOT_WAIT_MS            1500U
#define NBIOT_RESET_PULSE_MS          120U
#define NBIOT_POWER_SETTLE_MS         200U

#define NBIOT_RECOVERY_POLL_COUNT     5U
#define NBIOT_RECOVERY_POLL_MS        2000U

#define NBIOT_FATAL_BLINK_COUNT       10U
#define NBIOT_FATAL_BLINK_DELAY_MS    250U
#define NBIOT_FATAL_MAX_RETRIES       3U
#define NBIOT_FATAL_BACKOFF_BASE_MS   5000U
#define NBIOT_FATAL_BACKOFF_MAX_MS    30000U

/* local-only modem commands */
#define NBIOT_LOCAL_CMD_CFUN0         "AT+CFUN=0\r\n"
#define NBIOT_LOCAL_CMD_CFUN1         "AT+CFUN=1\r\n"
#define NBIOT_LOCAL_CMD_COPS_AUTO     "AT+COPS=0\r\n"
#define NBIOT_LOCAL_CMD_CGATT_1       "AT+CGATT=1\r\n"

/* -------------------------------------------------
 * local state
 * ------------------------------------------------- */
static NBIOT_STATE_ENUM g_nbiotState = NBIOT_STATE_BOOT;
static NBIOT_BOOT_STATE_ENUM g_bootState = NBIOT_BOOT_PWR_EN;
static NBIOT_INIT_STATE_ENUM g_initState = NBIOT_INIT_CMEE;
static NBIOT_UDP_STATE_ENUM g_udpState = NBIOT_UDP_IDLE;
static NBIOT_TCP_STATE_ENUM g_tcpState = NBIOT_TCP_IDLE;

static char g_cmdBuffer[NBIOT_CMD_BUFFER_LEN];
static uint16_t g_cmdIndex = 0U;
static bool g_bridgeBannerPrinted = false;

static NBIOT_OFFICE_PAYLOAD_TYPE g_officePayload;
static char g_payloadBuffer[NBIOT_PAYLOAD_BUFFER_LEN];
static char g_workBuffer[NBIOT_WORK_BUFFER_LEN];
static char g_recvLineBuffer[NBIOT_RECV_LINE_LEN];
static char g_respBuffer[NBIOT_RESP_BUFFER_LEN];
static uint8_t g_socketId = 0U;
static uint8_t g_fatalRecoveryCount = 0U;

typedef struct
{
    uint16_t n2oPpm;
    int16_t temperatureX10;
    uint16_t humidityX10;
    bool valid;
} NBIOT_MKR_SENSOR_TYPE;

static NBIOT_MKR_SENSOR_TYPE g_mkrSensorData = {0U, 0, 0U, false};
static char g_sensorPacketBuffer[NBIOT_SENSOR_PACKET_LEN];
static uint16_t g_sensorPacketIndex = 0U;

#if (ENABLE_PASSTHRU_MODE)
static bool g_passThruMode = true;
#endif

/* -------------------------------------------------
 * local helpers
 * ------------------------------------------------- */
static void NbIot_DelayMs(uint16_t ms);
static void NbIot_SendRaw(const char *cmd);
static bool NbIot_WaitForToken(const char *token, uint16_t timeoutMs, bool echoToDebug);
static bool NbIot_SendCommandAndWait(const char *cmd, const char *expected, uint16_t timeoutMs, bool echoToDebug);
static bool NbIot_SendCommandCollectResponse(const char *cmd,
                                             char *responseBuffer,
                                             uint16_t bufferSize,
                                             uint16_t timeoutMs,
                                             bool echoToDebug);
static bool NbIot_WaitForRecvLine(char *lineBuffer,
                                  uint16_t bufferSize,
                                  uint16_t timeoutMs,
                                  bool echoToDebug);
static bool NbIot_WaitForQIOpenResult(uint8_t socketId, uint16_t timeoutMs);
static bool NbIot_SendQIOpenAndWaitResult(const char *cmd, uint8_t socketId);
static void NbIot_PrintPrompt(void);
static void NbIot_RemoteTestFunction(void);
static void NbIot_HandleDownlinkCommand(const char *urcLine);

#if (ENABLE_PASSTHRU_MODE)
static void NbIot_EnterPassThruMode(void);
static void NbIot_ExitPassThruMode(void);
#endif

/* network helpers */
static bool NbIot_ParseCeregStat(const char *response, int *statOut);
static bool NbIot_ParseCgattStat(const char *response, int *statOut);
static bool NbIot_IsModemAlive(void);
static bool NbIot_IsNetworkRegistered(void);
static bool NbIot_IsPacketAttached(void);
static bool NbIot_TryRecoverNetworkOnce(void);
static bool NbIot_PrepareForSend(void);
/* payload information helpers */
static bool NbIot_ReadImei(char *imeiBuffer, uint16_t bufferSize);
static bool NbIot_ReadSimId(char *simBuffer, uint16_t bufferSize);
static bool NbIot_ReadRssi(uint8_t *rssiOut);
static bool NbIot_ParseDigitsFromResponse(const char *response,
                                          char *outBuffer,
                                          uint16_t outSize,
                                          uint8_t minDigits,
                                          uint8_t maxDigits);
static bool NbIot_ParseCsqRssi(const char *response, uint8_t *rssiOut);
static uint16_t NbIot_GetFakeGasValue(void);
static bool NbIot_ParseUint16AfterKey(const char *text, const char *key, uint16_t *outValue);
static bool NbIot_ParseSignedDecimalX10AfterKey(const char *text, const char *key, int16_t *outValue);
static bool NbIot_ParseUnsignedDecimalX10AfterKey(const char *text, const char *key, uint16_t *outValue);
static bool NbIot_ParseSensorPacket(const char *packet);
static void NbIot_PrintMkrSensorData(void);
/* socket robustness helpers */
static bool NbIot_CloseSocketQuiet(uint8_t socketId);
static bool NbIot_OpenUdpSocketRobust(uint8_t socketId);
static bool NbIot_OpenTcpSocketRobust(uint8_t socketId);

/* fatal recovery helpers */
static uint16_t NbIot_GetFatalBackoffMs(void);
static void NbIot_RequestFatalRecovery(void);

/* state handlers */
static NBIOT_STATE_ENUM NbIot_ProcessBootState(void);
static NBIOT_STATE_ENUM NbIot_ProcessInitState(void);
static NBIOT_STATE_ENUM NbIot_ProcessBridgeState(void);
static NBIOT_STATE_ENUM NbIot_ProcessUdpTestState(void);
static NBIOT_STATE_ENUM NbIot_ProcessTcpTestState(void);

static void NbIot_ProcessTerminalInput(void);
static void NbIot_ProcessSensorInput(void);
static void NbIot_ProcessModemOutput(void);

/* builders */
static void NbIot_BuildPdpCommand(char *buffer, uint16_t bufferSize, const char *apn);
static void NbIot_BuildUdpOpenCommand(char *buffer,
                                      uint16_t bufferSize,
                                      const char *serverIp,
                                      const char *serverPort,
                                      const char *localPort);
static void NbIot_BuildTcpOpenCommand(char *buffer,
                                      uint16_t bufferSize,
                                      const char *serverIp,
                                      const char *serverPort);
static void NbIot_BuildSendCommand(char *buffer,
                                   uint16_t bufferSize,
                                   uint8_t socketId,
                                   const char *payload);
static void NbIot_BuildCloseCommand(char *buffer,
                                    uint16_t bufferSize,
                                    uint8_t socketId);

/* -------------------------------------------------
 * basic helpers
 * ------------------------------------------------- */
static void NbIot_DelayMs(uint16_t ms)
{
    while (ms--)
    {
        __delay_cycles(1000);
    }
}

static void NbIot_SendRaw(const char *cmd)
{
    hal_uart_ModemWriteString(cmd);
}

static bool NbIot_WaitForToken(const char *token, uint16_t timeoutMs, bool echoToDebug)
{
    uint8_t rx;
    uint16_t matched = 0U;
    uint16_t tokenLen = (uint16_t)strlen(token);

    while (timeoutMs--)
    {
        while (hal_uart_ModemReadByte(&rx))
        {
            if (echoToDebug)
            {
                hal_uart_debug_write_char((char)rx);
            }

            if ((char)rx == token[matched])
            {
                matched++;
                if (matched >= tokenLen)
                {
                    return true;
                }
            }
            else
            {
                matched = ((char)rx == token[0]) ? 1U : 0U;
            }
        }

        NbIot_DelayMs(1U);
    }

    return false;
}

static bool NbIot_SendCommandAndWait(const char *cmd, const char *expected, uint16_t timeoutMs, bool echoToDebug)
{
    DEBUG_STRING("\r\n[MDM TX] ");
    DEBUG_STRING(cmd);

    NbIot_SendRaw(cmd);
    return NbIot_WaitForToken(expected, timeoutMs, echoToDebug);
}

static bool NbIot_SendCommandCollectResponse(const char *cmd,
                                             char *responseBuffer,
                                             uint16_t bufferSize,
                                             uint16_t timeoutMs,
                                             bool echoToDebug)
{
    uint8_t rx;
    uint16_t idx = 0U;
    bool sawAny = false;
    bool gotOk = false;
    bool gotError = false;

    if ((responseBuffer == 0) || (bufferSize == 0U))
    {
        return false;
    }

    memset(responseBuffer, 0, bufferSize);

    /*
     * Clear old bytes before sending the new AT command.
     */
    hal_uart_ModemFlush();

    DEBUG_STRING("\r\n[MDM TX] ");
    DEBUG_STRING(cmd);

    NbIot_SendRaw(cmd);

    /*
     * IMPORTANT:
     * Keep this loop very lightweight.
     * Do not use strstr() on every received byte.
     * MSP430 can lose UART bytes if processing is too slow.
     */
    while (timeoutMs > 0U)
    {
        while (hal_uart_ModemReadByte(&rx))
        {
            sawAny = true;

            if (idx < (bufferSize - 1U))
            {
                responseBuffer[idx++] = (char)rx;
                responseBuffer[idx] = '\0';
            }

            /*
             * Detect final OK using only the last few bytes.
             * Expected modem ending: OK\r\n
             */
            if (idx >= 4U)
            {
                if ((responseBuffer[idx - 4U] == 'O') &&
                    (responseBuffer[idx - 3U] == 'K') &&
                    (responseBuffer[idx - 2U] == '\r') &&
                    (responseBuffer[idx - 1U] == '\n'))
                {
                    gotOk = true;
                    break;
                }
            }

            /*
             * Detect final ERROR using only the last few bytes.
             * Expected modem ending: ERROR\r\n
             */
            if (idx >= 7U)
            {
                if ((responseBuffer[idx - 7U] == 'E') &&
                    (responseBuffer[idx - 6U] == 'R') &&
                    (responseBuffer[idx - 5U] == 'R') &&
                    (responseBuffer[idx - 4U] == 'O') &&
                    (responseBuffer[idx - 3U] == 'R') &&
                    (responseBuffer[idx - 2U] == '\r') &&
                    (responseBuffer[idx - 1U] == '\n'))
                {
                    gotError = true;
                    break;
                }
            }
        }

        if ((gotOk == true) || (gotError == true))
        {
            break;
        }

        NbIot_DelayMs(1U);
        timeoutMs--;
    }

    if ((echoToDebug == true) && (sawAny == true))
    {
        if (gotOk == true)
        {
            DEBUG_STRING("\r\n[MDM RX OK]\r\n");
        }
        else if (gotError == true)
        {
            DEBUG_STRING("\r\n[MDM RX ERROR]\r\n");
        }
        else
        {
            DEBUG_STRING("\r\n[MDM RX TIMEOUT]\r\n");
        }

        DEBUG_STRING(responseBuffer);
        DEBUG_STRING("\r\n");
    }

    if (gotError == true)
    {
        return false;
    }

    return sawAny;
}
static bool NbIot_WaitForRecvLine(char *lineBuffer,
                                  uint16_t bufferSize,
                                  uint16_t timeoutMs,
                                  bool echoToDebug)
{
    uint8_t rx;
    uint16_t idx = 0U;

    if ((lineBuffer == 0) || (bufferSize == 0U))
    {
        return false;
    }

    memset(lineBuffer, 0, bufferSize);

    while (timeoutMs--)
    {
        while (hal_uart_ModemReadByte(&rx))
        {
            if (echoToDebug)
            {
                hal_uart_debug_write_char((char)rx);
            }

            if ((rx != '\r') && (rx != '\n'))
            {
                if (idx < (bufferSize - 1U))
                {
                    lineBuffer[idx++] = (char)rx;
                    lineBuffer[idx] = '\0';
                }
            }
            else
            {
                if (strstr(lineBuffer, "+QIURC: \"recv\"") != 0)
                {
                    return true;
                }

                idx = 0U;
                memset(lineBuffer, 0, bufferSize);
            }
        }

        NbIot_DelayMs(1U);
    }

    return false;
}

static bool NbIot_WaitForQIOpenResult(uint8_t socketId, uint16_t timeoutMs)
{
    uint8_t rx;
    char line[96];
    uint16_t idx = 0U;
    char successToken[24];

    memset(line, 0, sizeof(line));
    snprintf(successToken, sizeof(successToken), "+QIOPEN: %u,0", (unsigned int)socketId);

    while (timeoutMs--)
    {
        while (hal_uart_ModemReadByte(&rx))
        {
            hal_uart_debug_write_char((char)rx);

            if ((rx != '\r') && (rx != '\n'))
            {
                if (idx < (sizeof(line) - 1U))
                {
                    line[idx++] = (char)rx;
                    line[idx] = '\0';
                }
            }
            else
            {
                if (idx > 0U)
                {
                    if (strstr(line, "+QIOPEN:") != 0)
                    {
                        DEBUG_STRING("\r\n[SOCKET] QIOPEN URC: ");
                        DEBUG_STRING(line);
                        DEBUG_STRING("\r\n");

                        if (strstr(line, successToken) != 0)
                        {
                            return true;
                        }

                        return false;
                    }

                    if (strstr(line, "ERROR") != 0)
                    {
                        return false;
                    }

                    idx = 0U;
                    memset(line, 0, sizeof(line));
                }
            }
        }

        NbIot_DelayMs(1U);
    }

    return false;
}

static bool NbIot_SendQIOpenAndWaitResult(const char *cmd, uint8_t socketId)
{
    hal_uart_ModemFlush();

    DEBUG_STRING("\r\n[MDM TX] ");
    DEBUG_STRING(cmd);

    NbIot_SendRaw(cmd);

    if (!NbIot_WaitForToken("OK", NBIOT_MED_TIMEOUT_MS, true))
    {
        DEBUG_STRING("\r\n[SOCKET] QIOPEN command not accepted.\r\n");
        return false;
    }

    return NbIot_WaitForQIOpenResult(socketId, NBIOT_LONG_TIMEOUT_MS);
}

static void NbIot_PrintPrompt(void)
{
    DEBUG_STRING("\r\n> ");
}

#if (ENABLE_PASSTHRU_MODE)
static void NbIot_EnterPassThruMode(void)
{
    g_passThruMode = true;
    hal_uart_ModemFlush();

    DEBUG_STRING("\r\n====================================\r\n");
    DEBUG_STRING(" MODEM PASS-THROUGH MODE ENABLED\r\n");
    DEBUG_STRING(" Type AT commands directly.\r\n");
    DEBUG_STRING(" Type 'exitpt' to leave pass-through mode.\r\n");
    DEBUG_STRING("====================================\r\n");

    NbIot_PrintPrompt();
}

static void NbIot_ExitPassThruMode(void)
{
    g_passThruMode = false;
    hal_uart_ModemFlush();

    DEBUG_STRING("\r\n====================================\r\n");
    DEBUG_STRING(" PASS-THROUGH MODE DISABLED\r\n");
    DEBUG_STRING(" Back to firmware command mode.\r\n");
    DEBUG_STRING("====================================\r\n");

    NbIot_PrintPrompt();
}
#endif

/* -------------------------------------------------
 * downlink handlers
 * ------------------------------------------------- */
static void NbIot_RemoteTestFunction(void)
{
    DEBUG_STRING("\r\n[DOWNLINK] Tell me to call the function\r\n");
}

static void NbIot_HandleDownlinkCommand(const char *urcLine)
{
    if (urcLine == 0)
    {
        return;
    }

    if (strstr(urcLine, "\"CALL_FN\"") != 0)
    {
        DEBUG_STRING("\r\n[DOWNLINK] Command received: CALL_FN\r\n");
        NbIot_RemoteTestFunction();
    }
    else if (strstr(urcLine, "\"PING\"") != 0)
    {
        DEBUG_STRING("\r\n[DOWNLINK] Command received: PING\r\n");
    }
    else if (strstr(urcLine, "\"ZZ\"") != 0)
    {
        DEBUG_STRING("\r\n[DOWNLINK] Command received: ZZ\r\n");
    }
    else
    {
        DEBUG_STRING("\r\n[DOWNLINK] Unknown command.\r\n");
    }
}

/* -------------------------------------------------
 * network helpers
 * ------------------------------------------------- */
static bool NbIot_ParseCeregStat(const char *response, int *statOut)
{
    const char *p;
    int first = 0;
    int second = 0;

    if ((response == 0) || (statOut == 0))
    {
        return false;
    }

    p = strstr(response, "+CEREG:");
    if (p == 0)
    {
        return false;
    }

    p = strchr(p, ':');
    if (p == 0)
    {
        return false;
    }
    p++;

    while (*p == ' ')
    {
        p++;
    }

    if ((*p < '0') || (*p > '9'))
    {
        return false;
    }

    while ((*p >= '0') && (*p <= '9'))
    {
        first = (first * 10) + (*p - '0');
        p++;
    }

    while (*p == ' ')
    {
        p++;
    }

    if (*p == ',')
    {
        p++;

        while (*p == ' ')
        {
            p++;
        }

        if ((*p < '0') || (*p > '9'))
        {
            return false;
        }

        while ((*p >= '0') && (*p <= '9'))
        {
            second = (second * 10) + (*p - '0');
            p++;
        }

        *statOut = second;
    }
    else
    {
        *statOut = first;
    }

    return true;
}

static bool NbIot_ParseCgattStat(const char *response, int *statOut)
{
    const char *p;
    int stat = 0;

    if ((response == 0) || (statOut == 0))
    {
        return false;
    }

    p = strstr(response, "+CGATT:");
    if (p == 0)
    {
        return false;
    }

    p = strchr(p, ':');
    if (p == 0)
    {
        return false;
    }
    p++;

    while (*p == ' ')
    {
        p++;
    }

    if ((*p < '0') || (*p > '9'))
    {
        return false;
    }

    while ((*p >= '0') && (*p <= '9'))
    {
        stat = (stat * 10) + (*p - '0');
        p++;
    }

    *statOut = stat;
    return true;
}

static bool NbIot_IsModemAlive(void)
{
    DEBUG_STRING("\r\n[NET] Check modem alive...\r\n");
    return NbIot_SendCommandAndWait(NBIOT_CMD_AT, "OK", NBIOT_SHORT_TIMEOUT_MS, true);
}

static bool NbIot_IsNetworkRegistered(void)
{
    int stat = -1;

    DEBUG_STRING("\r\n[NET] Check registration...\r\n");

    if (!NbIot_SendCommandCollectResponse(NBIOT_CMD_CEREG_Q,
                                          g_respBuffer,
                                          sizeof(g_respBuffer),
                                          NBIOT_MED_TIMEOUT_MS,
                                          true))
    {
        DEBUG_STRING("\r\n[NET] CEREG query failed.\r\n");
        return false;
    }

    if (!NbIot_ParseCeregStat(g_respBuffer, &stat))
    {
        DEBUG_STRING("\r\n[NET] Could not parse CEREG.\r\n");
        return false;
    }

    if ((stat == 1) || (stat == 5))
    {
        DEBUG_STRING("\r\n[NET] Registered OK.\r\n");
        return true;
    }

    DEBUG_STRING("\r\n[NET] Not registered.\r\n");
    return false;
}

static bool NbIot_IsPacketAttached(void)
{
    int stat = -1;

    DEBUG_STRING("\r\n[NET] Check packet attach...\r\n");

    if (!NbIot_SendCommandCollectResponse(NBIOT_CMD_CGATT_Q,
                                          g_respBuffer,
                                          sizeof(g_respBuffer),
                                          NBIOT_MED_TIMEOUT_MS,
                                          true))
    {
        DEBUG_STRING("\r\n[NET] CGATT query failed.\r\n");
        return false;
    }

    if (!NbIot_ParseCgattStat(g_respBuffer, &stat))
    {
        DEBUG_STRING("\r\n[NET] Could not parse CGATT.\r\n");
        return false;
    }

    if (stat == 1)
    {
        DEBUG_STRING("\r\n[NET] Packet attach OK.\r\n");
        return true;
    }

    DEBUG_STRING("\r\n[NET] Packet not attached.\r\n");
    return false;
}

static bool NbIot_TryRecoverNetworkOnce(void)
{
    uint8_t i;

    DEBUG_STRING("\r\n[NET] Recovery attempt...\r\n");

    (void)NbIot_SendCommandAndWait(NBIOT_LOCAL_CMD_CFUN0, "OK", NBIOT_MED_TIMEOUT_MS, true);
    NbIot_DelayMs(1000U);

    (void)NbIot_SendCommandAndWait(NBIOT_LOCAL_CMD_CFUN1, "OK", NBIOT_MED_TIMEOUT_MS, true);
    NbIot_DelayMs(4000U);

    (void)NbIot_SendCommandAndWait(NBIOT_LOCAL_CMD_COPS_AUTO, "OK", NBIOT_MED_TIMEOUT_MS, true);
    (void)NbIot_SendCommandAndWait(NBIOT_LOCAL_CMD_CGATT_1, "OK", NBIOT_MED_TIMEOUT_MS, true);

    for (i = 0U; i < NBIOT_RECOVERY_POLL_COUNT; i++)
    {
        DEBUG_STRING("\r\n[NET] Recovery poll...\r\n");

        if (NbIot_IsModemAlive() &&
            NbIot_IsNetworkRegistered())
        {
            if (NbIot_IsPacketAttached())
            {
                DEBUG_STRING("\r\n[NET] Recovery success.\r\n");
                return true;
            }
        }

        NbIot_DelayMs(NBIOT_RECOVERY_POLL_MS);
    }

    DEBUG_STRING("\r\n[NET] Recovery failed.\r\n");
    return false;
}

static bool NbIot_PrepareForSend(void)
{
    DEBUG_STRING("\r\n[NET] Quick network check...\r\n");

    if (NbIot_IsModemAlive() &&
        NbIot_IsNetworkRegistered())
    {
        if (NbIot_IsPacketAttached())
        {
            DEBUG_STRING("\r\n[NET] Ready to send.\r\n");
            return true;
        }

        DEBUG_STRING("\r\n[NET] Not attached. Trying AT+CGATT=1 once...\r\n");
        (void)NbIot_SendCommandAndWait(NBIOT_LOCAL_CMD_CGATT_1, "OK", NBIOT_MED_TIMEOUT_MS, true);
        NbIot_DelayMs(2000U);

        if (NbIot_IsPacketAttached())
        {
            DEBUG_STRING("\r\n[NET] Ready to send after attach.\r\n");
            return true;
        }
    }

    DEBUG_STRING("\r\n[NET] Not ready. Trying one recovery.\r\n");
    return NbIot_TryRecoverNetworkOnce();
}

/* -------------------------------------------------
 * socket helpers
 * ------------------------------------------------- */
static bool NbIot_CloseSocketQuiet(uint8_t socketId)
{
    NbIot_BuildCloseCommand(g_workBuffer,
                            sizeof(g_workBuffer),
                            socketId);

    DEBUG_STRING("\r\n[SOCKET] Cleanup old socket if any\r\n");

    hal_uart_ModemFlush();
    NbIot_SendRaw(g_workBuffer);

    (void)NbIot_WaitForToken("OK", 1500U, true);

    NbIot_DelayMs(800U);
    hal_uart_ModemFlush();
    return true;
}

static bool NbIot_OpenUdpSocketRobust(uint8_t socketId)
{
    uint8_t attempt;

    for (attempt = 0U; attempt < 2U; attempt++)
    {
        if (attempt > 0U)
        {
            DEBUG_STRING("\r\n[UDP] Retry socket open...\r\n");
        }

        NbIot_CloseSocketQuiet(socketId);

        NbIot_BuildUdpOpenCommand(g_workBuffer,
                                  sizeof(g_workBuffer),
                                  NBIOT_TEST_SERVER_IP,
                                  NBIOT_TEST_UDP_SERVER_PORT,
                                  NBIOT_TEST_LOCAL_PORT);

        DEBUG_STRING("\r\n[UDP] Open socket\r\n");
        DEBUG_STRING("Destination IP  : ");
        DEBUG_STRING(NBIOT_TEST_SERVER_IP);
        DEBUG_STRING("\r\nDestination Port: ");
        DEBUG_STRING(NBIOT_TEST_UDP_SERVER_PORT);
        DEBUG_STRING("\r\nLocal Port      : ");
        DEBUG_STRING(NBIOT_TEST_UDP_SERVER_PORT);
        DEBUG_STRING("\r\n");

        if (NbIot_SendQIOpenAndWaitResult(g_workBuffer, socketId))
        {
            DEBUG_STRING("\r\n[UDP] Socket open success.\r\n");
            return true;
        }

        DEBUG_STRING("\r\n[UDP] Socket open failed.\r\n");
        NbIot_DelayMs(1000U);
    }

    return false;
}

static bool NbIot_OpenTcpSocketRobust(uint8_t socketId)
{
    uint8_t attempt;

    for (attempt = 0U; attempt < 2U; attempt++)
    {
        if (attempt > 0U)
        {
            DEBUG_STRING("\r\n[TCP] Retry socket open...\r\n");
        }

        NbIot_CloseSocketQuiet(socketId);

        NbIot_BuildTcpOpenCommand(g_workBuffer,
                                  sizeof(g_workBuffer),
                                  NBIOT_TEST_SERVER_IP,
                                  NBIOT_TEST_TCP_SERVER_PORT);

        DEBUG_STRING("\r\n[TCP] Open socket\r\n");
        DEBUG_STRING("Destination IP  : ");
        DEBUG_STRING(NBIOT_TEST_SERVER_IP);
        DEBUG_STRING("\r\nDestination Port: ");
        DEBUG_STRING(NBIOT_TEST_TCP_SERVER_PORT);
        DEBUG_STRING("\r\n");

        if (NbIot_SendQIOpenAndWaitResult(g_workBuffer, socketId))
        {
            DEBUG_STRING("\r\n[TCP] Socket open success.\r\n");
            return true;
        }

        DEBUG_STRING("\r\n[TCP] Socket open failed.\r\n");
        NbIot_DelayMs(1000U);
    }

    return false;
}

/* -------------------------------------------------
 * office payload helpers
 * ------------------------------------------------- */
static bool NbIot_ParseDigitsFromResponse(const char *response,
                                          char *outBuffer,
                                          uint16_t outSize,
                                          uint8_t minDigits,
                                          uint8_t maxDigits)
{
    const char *p;
    uint16_t count;

    if ((response == 0) || (outBuffer == 0) || (outSize == 0U))
    {
        return false;
    }

    memset(outBuffer, 0, outSize);

    p = response;

    while (*p != '\0')
    {
        if ((*p >= '0') && (*p <= '9'))
        {
            count = 0U;

            while ((p[count] >= '0') && (p[count] <= '9'))
            {
                count++;
            }

            if ((count >= minDigits) && (count <= maxDigits))
            {
                if (count >= outSize)
                {
                    count = outSize - 1U;
                }

                memcpy(outBuffer, p, count);
                outBuffer[count] = '\0';
                return true;
            }

            p += count;
        }
        else
        {
            p++;
        }
    }

    return false;
}

static bool NbIot_ParseCsqRssi(const char *response, uint8_t *rssiOut)
{
    const char *p;
    int rssi = 0;

    if ((response == 0) || (rssiOut == 0))
    {
        return false;
    }

    p = strstr(response, "+CSQ:");
    if (p == 0)
    {
        return false;
    }

    p = strchr(p, ':');
    if (p == 0)
    {
        return false;
    }

    p++;

    while (*p == ' ')
    {
        p++;
    }

    if ((*p < '0') || (*p > '9'))
    {
        return false;
    }

    while ((*p >= '0') && (*p <= '9'))
    {
        rssi = (rssi * 10) + (*p - '0');
        p++;
    }

    if (rssi > 99)
    {
        rssi = 99;
    }

    *rssiOut = (uint8_t)rssi;
    return true;
}

static bool NbIot_ReadImei(char *imeiBuffer, uint16_t bufferSize)
{
    if ((imeiBuffer == 0) || (bufferSize == 0U))
    {
        return false;
    }

    if (!NbIot_SendCommandCollectResponse(NBIOT_CMD_CGSN,
                                          g_respBuffer,
                                          sizeof(g_respBuffer),
                                          NBIOT_MED_TIMEOUT_MS,
                                          true))
    {
        return false;
    }

    return NbIot_ParseDigitsFromResponse(g_respBuffer,
                                         imeiBuffer,
                                         bufferSize,
                                         14U,
                                         17U);
}

static bool NbIot_ReadSimId(char *simBuffer, uint16_t bufferSize)
{
    if ((simBuffer == 0) || (bufferSize == 0U))
    {
        return false;
    }

    if (!NbIot_SendCommandCollectResponse(NBIOT_CMD_QCCID,
                                          g_respBuffer,
                                          sizeof(g_respBuffer),
                                          NBIOT_MED_TIMEOUT_MS,
                                          true))
    {
        return false;
    }

    return NbIot_ParseDigitsFromResponse(g_respBuffer,
                                         simBuffer,
                                         bufferSize,
                                         18U,
                                         22U);
}

static bool NbIot_ReadRssi(uint8_t *rssiOut)
{
    if (rssiOut == 0)
    {
        return false;
    }

    if (!NbIot_SendCommandCollectResponse(NBIOT_CMD_CSQ,
                                          g_respBuffer,
                                          sizeof(g_respBuffer),
                                          NBIOT_MED_TIMEOUT_MS,
                                          true))
    {
        return false;
    }

    return NbIot_ParseCsqRssi(g_respBuffer, rssiOut);
}

static uint16_t NbIot_GetFakeGasValue(void)
{
    static uint16_t seed = 1234U;

    seed = (uint16_t)((seed * 1103U) + 12345U);

    return (uint16_t)(seed % 501U);   /* fake gas value: 0 to 500 ppm */
}

static bool NbIot_ParseUint16AfterKey(const char *text, const char *key, uint16_t *outValue)
{
    const char *p;
    uint32_t value = 0UL;
    bool gotDigit = false;

    if ((text == 0) || (key == 0) || (outValue == 0))
    {
        return false;
    }

    p = strstr(text, key);
    if (p == 0)
    {
        return false;
    }

    p += strlen(key);

    while ((*p >= '0') && (*p <= '9'))
    {
        gotDigit = true;
        value = (value * 10UL) + (uint32_t)(*p - '0');

        if (value > 65535UL)
        {
            return false;
        }

        p++;
    }

    if (!gotDigit)
    {
        return false;
    }

    *outValue = (uint16_t)value;
    return true;
}

static bool NbIot_ParseSignedDecimalX10AfterKey(const char *text, const char *key, int16_t *outValue)
{
    const char *p;
    int32_t integerPart = 0L;
    int32_t decimalPart = 0L;
    int32_t valueX10;
    bool negative = false;
    bool gotDigit = false;

    if ((text == 0) || (key == 0) || (outValue == 0))
    {
        return false;
    }

    p = strstr(text, key);
    if (p == 0)
    {
        return false;
    }

    p += strlen(key);

    if (*p == '-')
    {
        negative = true;
        p++;
    }

    while ((*p >= '0') && (*p <= '9'))
    {
        gotDigit = true;
        integerPart = (integerPart * 10L) + (int32_t)(*p - '0');
        p++;
    }

    if (!gotDigit)
    {
        return false;
    }

    if (*p == '.')
    {
        p++;
        if ((*p >= '0') && (*p <= '9'))
        {
            decimalPart = (int32_t)(*p - '0');
        }
    }

    valueX10 = (integerPart * 10L) + decimalPart;

    if (negative)
    {
        valueX10 = -valueX10;
    }

    if ((valueX10 < -32768L) || (valueX10 > 32767L))
    {
        return false;
    }

    *outValue = (int16_t)valueX10;
    return true;
}

static bool NbIot_ParseUnsignedDecimalX10AfterKey(const char *text, const char *key, uint16_t *outValue)
{
    const char *p;
    uint32_t integerPart = 0UL;
    uint32_t decimalPart = 0UL;
    uint32_t valueX10;
    bool gotDigit = false;

    if ((text == 0) || (key == 0) || (outValue == 0))
    {
        return false;
    }

    p = strstr(text, key);
    if (p == 0)
    {
        return false;
    }

    p += strlen(key);

    while ((*p >= '0') && (*p <= '9'))
    {
        gotDigit = true;
        integerPart = (integerPart * 10UL) + (uint32_t)(*p - '0');
        p++;
    }

    if (!gotDigit)
    {
        return false;
    }

    if (*p == '.')
    {
        p++;
        if ((*p >= '0') && (*p <= '9'))
        {
            decimalPart = (uint32_t)(*p - '0');
        }
    }

    valueX10 = (integerPart * 10UL) + decimalPart;

    if (valueX10 > 65535UL)
    {
        return false;
    }

    *outValue = (uint16_t)valueX10;
    return true;
}

static bool NbIot_ParseSensorPacket(const char *packet)
{
    uint16_t n2o;
    int16_t tempX10;
    uint16_t humX10;

    if (packet == 0)
    {
        return false;
    }

    if (strncmp(packet, "$SENSOR", 7U) != 0)
    {
        return false;
    }

    if (!NbIot_ParseUint16AfterKey(packet, "N2O=", &n2o))
    {
        return false;
    }

    if (!NbIot_ParseSignedDecimalX10AfterKey(packet, "TEMP=", &tempX10))
    {
        return false;
    }

    if (!NbIot_ParseUnsignedDecimalX10AfterKey(packet, "HUM=", &humX10))
    {
        return false;
    }

    g_mkrSensorData.n2oPpm = n2o;
    g_mkrSensorData.temperatureX10 = tempX10;
    g_mkrSensorData.humidityX10 = humX10;
    g_mkrSensorData.valid = true;

    return true;
}

static void NbIot_PrintMkrSensorData(void)
{
    DEBUG_STRING("N2O=");
    UART_Debug_SendInt((int)g_mkrSensorData.n2oPpm);
    DEBUG_STRING(" ppm, TEMPx10=");
    UART_Debug_SendInt((int)g_mkrSensorData.temperatureX10);
    DEBUG_STRING(", HUMx10=");
    UART_Debug_SendInt((int)g_mkrSensorData.humidityX10);
    DEBUG_STRING(" ");
}

void NbIot_LoadOfficeDemoPayload(void)
{
    char imei[20];
    char simId[24];
    uint8_t rssi;
    uint16_t gasValue;

    memset(&g_officePayload, 0, sizeof(g_officePayload));
    memset(imei, 0, sizeof(imei));
    memset(simId, 0, sizeof(simId));

    /*
     * Read live values from modem.
     * If reading fails, fallback values are used.
     */
    if (!NbIot_ReadImei(imei, sizeof(imei)))
    {
        strcpy(imei, "000000000000000");
    }

    if (!NbIot_ReadSimId(simId, sizeof(simId)))
    {
        strcpy(simId, "00000000000000000000");
    }

    if (!NbIot_ReadRssi(&rssi))
    {
        rssi = 99U;
    }

    if (g_mkrSensorData.valid)
    {
        gasValue = g_mkrSensorData.n2oPpm;
    }
    else
    {
        gasValue = NbIot_GetFakeGasValue();
    }

    strcpy(g_officePayload.imei, imei);
    strcpy(g_officePayload.index, "0000");
    strcpy(g_officePayload.flags, "10");
    strcpy(g_officePayload.simId, simId);
    g_officePayload.rssi = rssi;

    if (g_mkrSensorData.valid)
    {
        /*
         * Hex body format, 6 bytes total:
         * N2O ppm       : uint16, ppm
         * Temperature   : int16, value x10, e.g. 22.6 C = 226 = 00E2
         * Humidity      : uint16, value x10, e.g. 84.1 % = 841 = 0349
         * Example body  : 009100E20349
         */
        snprintf(g_officePayload.hexBody,
                 sizeof(g_officePayload.hexBody),
                 "%04X%04X%04X",
                 (unsigned int)g_mkrSensorData.n2oPpm,
                 (unsigned int)((uint16_t)g_mkrSensorData.temperatureX10),
                 (unsigned int)g_mkrSensorData.humidityX10);
    }
    else
    {
        snprintf(g_officePayload.hexBody,
                 sizeof(g_officePayload.hexBody),
                 "%04X",
                 (unsigned int)gasValue);
    }
}

void NbIot_BuildOfficePayload(const NBIOT_OFFICE_PAYLOAD_TYPE *payload,
                              char *buffer,
                              uint16_t bufferSize)
{
    if ((payload == 0) || (buffer == 0) || (bufferSize == 0U))
    {
        return;
    }

    snprintf(buffer,
             bufferSize,
             "%s,%s,%s,%s,%u %s",
             payload->imei,
             payload->index,
             payload->flags,
             payload->simId,
             (unsigned int)payload->rssi,
             payload->hexBody);
}

/* -------------------------------------------------
 * command builders
 * ------------------------------------------------- */
static void NbIot_BuildPdpCommand(char *buffer, uint16_t bufferSize, const char *apn)
{
    if ((buffer == 0) || (bufferSize == 0U))
    {
        return;
    }

    if ((apn != 0) && (strlen(apn) > 0U))
    {
        snprintf(buffer,
                 bufferSize,
                 "%s,\"%s\"\r\n",
                 NBIOT_CMD_CREATE_PDP_BASE,
                 apn);
    }
    else
    {
        snprintf(buffer,
                 bufferSize,
                 "%s\r\n",
                 NBIOT_CMD_CREATE_PDP_BASE);
    }
}

static void NbIot_BuildUdpOpenCommand(char *buffer,
                                      uint16_t bufferSize,
                                      const char *serverIp,
                                      const char *serverPort,
                                      const char *localPort)
{
    (void)localPort;

    if ((buffer == 0) || (bufferSize == 0U))
    {
        return;
    }

    snprintf(buffer,
             bufferSize,
             "%s\"UDP\",\"%s\",%s,%s,1\r\n",
             NBIOT_CMD_OPEN_SOCKET_BASE,
             serverIp,
             serverPort,
             serverPort);
}

static void NbIot_BuildTcpOpenCommand(char *buffer,
                                      uint16_t bufferSize,
                                      const char *serverIp,
                                      const char *serverPort)
{
    if ((buffer == 0) || (bufferSize == 0U))
    {
        return;
    }

    snprintf(buffer,
             bufferSize,
             "%s\"TCP\",\"%s\",%s,%s,1\r\n",
             NBIOT_CMD_OPEN_SOCKET_BASE,
             serverIp,
             serverPort,
             serverPort);
}

static void NbIot_BuildSendCommand(char *buffer,
                                   uint16_t bufferSize,
                                   uint8_t socketId,
                                   const char *payload)
{
    uint16_t payloadLen = 0U;

    if ((buffer == 0) || (payload == 0) || (bufferSize == 0U))
    {
        return;
    }

    payloadLen = (uint16_t)strlen(payload);

    snprintf(buffer,
             bufferSize,
             "%s%u,%u,\"%s\",1\r\n",
             NBIOT_CMD_SEND_DATA_BASE,
             (unsigned int)socketId,
             (unsigned int)payloadLen,
             payload);
}

static void NbIot_BuildCloseCommand(char *buffer,
                                    uint16_t bufferSize,
                                    uint8_t socketId)
{
    if ((buffer == 0) || (bufferSize == 0U))
    {
        return;
    }

    snprintf(buffer,
             bufferSize,
             "%s%u\r\n",
             NBIOT_CMD_CLOSE_SOCKET_BASE,
             (unsigned int)socketId);
}

/* -------------------------------------------------
 * fatal recovery helpers
 * ------------------------------------------------- */
static uint16_t NbIot_GetFatalBackoffMs(void)
{
    uint32_t backoffMs;

    if (g_fatalRecoveryCount == 0U)
    {
        return NBIOT_FATAL_BACKOFF_BASE_MS;
    }

    backoffMs = (uint32_t)NBIOT_FATAL_BACKOFF_BASE_MS << (g_fatalRecoveryCount - 1U);

    if (backoffMs > NBIOT_FATAL_BACKOFF_MAX_MS)
    {
        backoffMs = NBIOT_FATAL_BACKOFF_MAX_MS;
    }

    return (uint16_t)backoffMs;
}

static void NbIot_RequestFatalRecovery(void)
{
    uint8_t i;
    uint16_t backoffMs;
    char msg[80];

    DEBUG_STRING("\r\n[FATAL] Fatal error detected.\r\n");

    for (i = 0U; i < NBIOT_FATAL_BLINK_COUNT; i++)
    {
        Gpio_ToggleHeartbeatLed();
        NbIot_DelayMs(NBIOT_FATAL_BLINK_DELAY_MS);
    }

    if (g_fatalRecoveryCount < 255U)
    {
        g_fatalRecoveryCount++;
    }

    snprintf(msg, sizeof(msg), "[FATAL] Recovery attempt count = %u\r\n",
             (unsigned int)g_fatalRecoveryCount);
    DEBUG_STRING(msg);

    if (g_fatalRecoveryCount > NBIOT_FATAL_MAX_RETRIES)
    {
        backoffMs = NbIot_GetFatalBackoffMs();

        snprintf(msg, sizeof(msg),
                 "[FATAL] Too many consecutive failures. Backing off for %u ms\r\n",
                 (unsigned int)backoffMs);
        DEBUG_STRING(msg);

        NbIot_DelayMs(backoffMs);
    }
    else
    {
        NbIot_DelayMs(1000U);
    }

    Modem_PowerEnable(true);
    Modem_PowerKeySet(false);

    Modem_ResetAssert(true);
    NbIot_DelayMs(NBIOT_RESET_PULSE_MS);
    Modem_ResetAssert(false);

    NbIot_DelayMs(NBIOT_BOOT_WAIT_MS);

    hal_uart_ModemFlush();

    g_nbiotState = NBIOT_STATE_BOOT;
    g_bootState = NBIOT_BOOT_PWR_EN;
    g_initState = NBIOT_INIT_CMEE;
    g_udpState = NBIOT_UDP_IDLE;
    g_tcpState = NBIOT_TCP_IDLE;
    g_cmdIndex = 0U;
    g_bridgeBannerPrinted = false;
    g_socketId = 0U;

    memset(g_cmdBuffer, 0, sizeof(g_cmdBuffer));
    memset(g_payloadBuffer, 0, sizeof(g_payloadBuffer));
    memset(g_workBuffer, 0, sizeof(g_workBuffer));
    memset(g_recvLineBuffer, 0, sizeof(g_recvLineBuffer));
    memset(g_respBuffer, 0, sizeof(g_respBuffer));
    memset(&g_officePayload, 0, sizeof(g_officePayload));

#if (ENABLE_PASSTHRU_MODE)
    g_passThruMode = false;
#endif

    DEBUG_STRING("[FATAL] Recovery complete. Restarting from BOOT.\r\n");
}

/* -------------------------------------------------
 * boot state machine
 * ------------------------------------------------- */
static NBIOT_STATE_ENUM NbIot_ProcessBootState(void)
{
    switch (g_bootState)
    {
    case NBIOT_BOOT_PWR_EN:
        DEBUG_STRING("\r\nModem GPIO init...\r\n");
        Modem_PowerKeySet(false);
        DEBUG_STRING("MDM_PEN = HIGH\r\n");
        Modem_PowerEnable(true);
        NbIot_DelayMs(NBIOT_POWER_SETTLE_MS);
        g_bootState = NBIOT_BOOT_RESET_ASSERT;
        break;

    case NBIOT_BOOT_RESET_ASSERT:
        DEBUG_STRING("MDM_RST = LOW pulse\r\n");
        Modem_ResetAssert(true);
        NbIot_DelayMs(NBIOT_RESET_PULSE_MS);
        g_bootState = NBIOT_BOOT_RESET_RELEASE;
        break;

    case NBIOT_BOOT_RESET_RELEASE:
        DEBUG_STRING("MDM_RST = HIGH release\r\n");
        Modem_ResetAssert(false);
        g_bootState = NBIOT_BOOT_WAIT_STARTUP;
        break;

    case NBIOT_BOOT_WAIT_STARTUP:
        DEBUG_STRING("Waiting modem boot...\r\n");
        NbIot_DelayMs(NBIOT_BOOT_WAIT_MS);
        (void)NbIot_WaitForToken("RDY", 1500U, true);
        g_bootState = NBIOT_BOOT_TRY_AT_9600;
        break;

    case NBIOT_BOOT_TRY_AT_9600:
        DEBUG_STRING("\r\nTrying modem sync at 9600...\r\n");
        hal_uart_initModemPort(MODEM_BAUD_9600);
        hal_uart_ModemFlush();

        if (NbIot_SendCommandAndWait(NBIOT_CMD_AT, "OK", NBIOT_SHORT_TIMEOUT_MS, true))
        {
            DEBUG_STRING("\r\nModem responded to AT.\r\n");
            g_bootState = NBIOT_BOOT_CLEAR_ECHO;
        }
        else
        {
            g_bootState = NBIOT_BOOT_TRY_AT_115200;
        }
        break;

    case NBIOT_BOOT_TRY_AT_115200:
        DEBUG_STRING("\r\nTrying modem sync at 115200...\r\n");
        hal_uart_initModemPort(MODEM_BAUD_115200);
        hal_uart_ModemFlush();

        if (NbIot_SendCommandAndWait(NBIOT_CMD_AT, "OK", NBIOT_SHORT_TIMEOUT_MS, true))
        {
            DEBUG_STRING("\r\nModem responded at 115200.\r\n");
            g_bootState = NBIOT_BOOT_SET_IPR_9600;
        }
        else
        {
            return NBIOT_STATE_ERROR;
        }
        break;

    case NBIOT_BOOT_SET_IPR_9600:
        if (NbIot_SendCommandAndWait(NBIOT_CMD_IPR_9600, "OK", NBIOT_MED_TIMEOUT_MS, true))
        {
            NbIot_DelayMs(150U);
            hal_uart_initModemPort(MODEM_BAUD_9600);
            hal_uart_ModemFlush();
            g_bootState = NBIOT_BOOT_RECHECK_AT_9600;
        }
        else
        {
            return NBIOT_STATE_ERROR;
        }
        break;

    case NBIOT_BOOT_RECHECK_AT_9600:
        DEBUG_STRING("\r\nRe-checking at 9600...\r\n");
        if (NbIot_SendCommandAndWait(NBIOT_CMD_AT, "OK", NBIOT_SHORT_TIMEOUT_MS, true))
        {
            g_bootState = NBIOT_BOOT_CLEAR_ECHO;
        }
        else
        {
            return NBIOT_STATE_ERROR;
        }
        break;

    case NBIOT_BOOT_CLEAR_ECHO:
        if (NbIot_SendCommandAndWait(NBIOT_CMD_ATE0, "OK", NBIOT_SHORT_TIMEOUT_MS, true))
        {
            g_bootState = NBIOT_BOOT_FINISH;
        }
        else
        {
            return NBIOT_STATE_ERROR;
        }
        break;

    case NBIOT_BOOT_FINISH:
    default:
        DEBUG_STRING("\r\nBoot state complete.\r\n");
        g_bootState = NBIOT_BOOT_PWR_EN;
        return NBIOT_STATE_INIT;
    }

    return NBIOT_STATE_BOOT;
}

/* -------------------------------------------------
 * init/config state machine
 * ------------------------------------------------- */
static NBIOT_STATE_ENUM NbIot_ProcessInitState(void)
{
    switch (g_initState)
    {
    case NBIOT_INIT_CMEE:
        if (NbIot_SendCommandAndWait(NBIOT_CMD_CMEE, "OK", NBIOT_SHORT_TIMEOUT_MS, true))
        {
            g_initState = NBIOT_INIT_CSCON;
        }
        else
        {
            return NBIOT_STATE_ERROR;
        }
        break;

    case NBIOT_INIT_CSCON:
        if (NbIot_SendCommandAndWait(NBIOT_CMD_CSCON_URC, "OK", NBIOT_SHORT_TIMEOUT_MS, true))
        {
            g_initState = NBIOT_INIT_CEREG_URC;
        }
        else
        {
            return NBIOT_STATE_ERROR;
        }
        break;

    case NBIOT_INIT_CEREG_URC:
        if (NbIot_SendCommandAndWait(NBIOT_CMD_CEREG_URC, "OK", NBIOT_SHORT_TIMEOUT_MS, true))
        {
            g_initState = NBIOT_INIT_QCFG_DSEVENT;
        }
        else
        {
            return NBIOT_STATE_ERROR;
        }
        break;

    case NBIOT_INIT_QCFG_DSEVENT:
        if (NbIot_SendCommandAndWait(NBIOT_CMD_QCFG_DSEVENT_OFF, "OK", NBIOT_SHORT_TIMEOUT_MS, true))
        {
            g_initState = NBIOT_INIT_QICFG_DATAFORMAT;
        }
        else
        {
            return NBIOT_STATE_ERROR;
        }
        break;

    case NBIOT_INIT_QICFG_DATAFORMAT:
        if (NbIot_SendCommandAndWait(NBIOT_CMD_QICFG_DATAFORMAT, "OK", NBIOT_SHORT_TIMEOUT_MS, true))
        {
            g_initState = NBIOT_INIT_QNBIOTEVENT;
        }
        else
        {
            return NBIOT_STATE_ERROR;
        }
        break;

    case NBIOT_INIT_QNBIOTEVENT:
        if (NbIot_SendCommandAndWait(NBIOT_CMD_QNBIOTEVENT, "OK", NBIOT_SHORT_TIMEOUT_MS, true))
        {
            g_initState = NBIOT_INIT_DISABLE_PSM;
        }
        else
        {
            return NBIOT_STATE_ERROR;
        }
        break;

    case NBIOT_INIT_DISABLE_PSM:
        if (NbIot_SendCommandAndWait(NBIOT_CMD_DISABLE_PSM, "OK", NBIOT_MED_TIMEOUT_MS, true))
        {
            g_initState = NBIOT_INIT_DISABLE_CEDRXS;
        }
        else
        {
            return NBIOT_STATE_ERROR;
        }
        break;

    case NBIOT_INIT_DISABLE_CEDRXS:
        if (NbIot_SendCommandAndWait(NBIOT_CMD_DISABLE_CEDRXS, "OK", NBIOT_MED_TIMEOUT_MS, true))
        {
            DEBUG_STRING("\r\nCEDRXS disabled.\r\n");
        }
        else
        {
            DEBUG_STRING("\r\nWarning: AT+CEDRXS=0,5 not accepted. Continuing...\r\n");
        }
        g_initState = NBIOT_INIT_DISABLE_NPTWEDRXS;
        break;

    case NBIOT_INIT_DISABLE_NPTWEDRXS:
        if (NbIot_SendCommandAndWait(NBIOT_CMD_DISABLE_NPTWEDRXS, "OK", NBIOT_MED_TIMEOUT_MS, true))
        {
            DEBUG_STRING("\r\nNPTWEDRXS disabled.\r\n");
        }
        else
        {
            DEBUG_STRING("\r\nWarning: AT+NPTWEDRXS=0,5 not accepted. Continuing...\r\n");
        }
        g_initState = NBIOT_INIT_QUERY_INFO;
        break;

    case NBIOT_INIT_QUERY_INFO:
        (void)NbIot_SendCommandAndWait(NBIOT_CMD_ATI, "OK", NBIOT_MED_TIMEOUT_MS, true);
        (void)NbIot_SendCommandAndWait(NBIOT_CMD_CGMR, "OK", NBIOT_MED_TIMEOUT_MS, true);
        (void)NbIot_SendCommandAndWait(NBIOT_CMD_CEREG_Q, "OK", NBIOT_MED_TIMEOUT_MS, true);
        (void)NbIot_SendCommandAndWait(NBIOT_CMD_CGATT_Q, "OK", NBIOT_MED_TIMEOUT_MS, true);
        (void)NbIot_SendCommandAndWait(NBIOT_CMD_CSQ, "OK", NBIOT_MED_TIMEOUT_MS, true);
        g_initState = NBIOT_INIT_FINISH;
        break;

    case NBIOT_INIT_FINISH:
    default:
        DEBUG_STRING("\r\nInitialisation state complete.\r\n");
        g_initState = NBIOT_INIT_CMEE;
        g_fatalRecoveryCount = 0U;
        return NBIOT_STATE_BRIDGE;
    }

    return NBIOT_STATE_INIT;
}

/* -------------------------------------------------
 * UDP test state machine
 * ------------------------------------------------- */
static NBIOT_STATE_ENUM NbIot_ProcessUdpTestState(void)
{
    switch (g_udpState)
    {
    case NBIOT_UDP_IDLE:
        return NBIOT_STATE_BRIDGE;

    case NBIOT_UDP_CREATE_PDP:
        DEBUG_STRING("\r\n[UDP] Create PDP context\r\n");

        if (!NbIot_PrepareForSend())
        {
            DEBUG_STRING("\r\n[UDP] Network not ready. Abort this send.\r\n");
            g_udpState = NBIOT_UDP_FINISH;
            break;
        }

        NbIot_BuildPdpCommand(g_workBuffer, sizeof(g_workBuffer), NBIOT_TEST_APN);

        if (NbIot_SendCommandAndWait(g_workBuffer, "OK", NBIOT_MED_TIMEOUT_MS, true))
        {
            NbIot_DelayMs(300U);
            g_udpState = NBIOT_UDP_OPEN_SOCKET;
        }
        else
        {
            DEBUG_STRING("\r\n[UDP] PDP create failed. Abort this send.\r\n");
            g_udpState = NBIOT_UDP_FINISH;
        }
        break;

    case NBIOT_UDP_OPEN_SOCKET:
        g_socketId = 0U;

        if (NbIot_OpenUdpSocketRobust(g_socketId))
        {
            g_udpState = NBIOT_UDP_BUILD_PAYLOAD;
        }
        else
        {
            DEBUG_STRING("\r\n[UDP] QIOPEN failed after retry. Abort this send.\r\n");
            g_udpState = NBIOT_UDP_FINISH;
        }
        break;

    case NBIOT_UDP_BUILD_PAYLOAD:
        DEBUG_STRING("\r\n[UDP] Build payload\r\n");

        NbIot_BuildOfficePayload(&g_officePayload,
                                 g_payloadBuffer,
                                 sizeof(g_payloadBuffer));

        DEBUG_STRING("Payload: ");
        DEBUG_STRING(g_payloadBuffer);
        DEBUG_STRING("\r\n");

        g_udpState = NBIOT_UDP_SEND_PAYLOAD;
        break;

    case NBIOT_UDP_SEND_PAYLOAD:
        DEBUG_STRING("\r\n[UDP] Send payload\r\n");

        NbIot_BuildSendCommand(g_workBuffer,
                               sizeof(g_workBuffer),
                               g_socketId,
                               g_payloadBuffer);

        if (NbIot_SendCommandAndWait(g_workBuffer, "SEND OK", NBIOT_LONG_TIMEOUT_MS, true))
        {
            g_udpState = NBIOT_UDP_WAIT_RESPONSE;
        }
        else
        {
            DEBUG_STRING("\r\n[UDP] SEND OK not received.\r\n");
            g_udpState = NBIOT_UDP_CLOSE_SOCKET;
        }
        break;

    case NBIOT_UDP_WAIT_RESPONSE:
        DEBUG_STRING("\r\n[UDP] Wait server response\r\n");

        if (NbIot_WaitForRecvLine(g_recvLineBuffer,
                                  sizeof(g_recvLineBuffer),
                                  NBIOT_LONG_TIMEOUT_MS,
                                  true))
        {
            DEBUG_STRING("\r\n[UDP] Full recv URC captured.\r\n");
            DEBUG_STRING("[UDP] URC line: ");
            DEBUG_STRING(g_recvLineBuffer);
            DEBUG_STRING("\r\n");

            NbIot_HandleDownlinkCommand(g_recvLineBuffer);
        }
        else
        {
            DEBUG_STRING("\r\n[UDP] No server response after SEND OK.\r\n");
        }

        g_udpState = NBIOT_UDP_CLOSE_SOCKET;
        break;

    case NBIOT_UDP_CLOSE_SOCKET:
        DEBUG_STRING("\r\n[UDP] Close socket\r\n");

        NbIot_BuildCloseCommand(g_workBuffer,
                                sizeof(g_workBuffer),
                                g_socketId);

        if (!NbIot_SendCommandAndWait(g_workBuffer, "OK", NBIOT_MED_TIMEOUT_MS, true))
        {
            DEBUG_STRING("\r\n[UDP] Warning: close socket did not return OK.\r\n");
        }

        g_udpState = NBIOT_UDP_FINISH;
        break;

    case NBIOT_UDP_FINISH:
    default:
        DEBUG_STRING("\r\n[UDP] Demo send complete.\r\n");
        g_udpState = NBIOT_UDP_IDLE;
        NbIot_PrintPrompt();
        return NBIOT_STATE_BRIDGE;
    }

    return NBIOT_STATE_UDP_TEST;
}

/* -------------------------------------------------
 * TCP test state machine
 * ------------------------------------------------- */
static NBIOT_STATE_ENUM NbIot_ProcessTcpTestState(void)
{
    switch (g_tcpState)
    {
    case NBIOT_TCP_IDLE:
        return NBIOT_STATE_BRIDGE;

    case NBIOT_TCP_CREATE_PDP:
        DEBUG_STRING("\r\n[TCP] Create PDP context\r\n");

        if (!NbIot_PrepareForSend())
        {
            DEBUG_STRING("\r\n[TCP] Network not ready. Abort this send.\r\n");
            g_tcpState = NBIOT_TCP_FINISH;
            break;
        }

        NbIot_BuildPdpCommand(g_workBuffer, sizeof(g_workBuffer), NBIOT_TEST_APN);

        if (NbIot_SendCommandAndWait(g_workBuffer, "OK", NBIOT_MED_TIMEOUT_MS, true))
        {
            NbIot_DelayMs(300U);
            g_tcpState = NBIOT_TCP_OPEN_SOCKET;
        }
        else
        {
            DEBUG_STRING("\r\n[TCP] PDP create failed. Abort this send.\r\n");
            g_tcpState = NBIOT_TCP_FINISH;
        }
        break;

    case NBIOT_TCP_OPEN_SOCKET:
        g_socketId = 0U;

        if (NbIot_OpenTcpSocketRobust(g_socketId))
        {
            g_tcpState = NBIOT_TCP_BUILD_PAYLOAD;
        }
        else
        {
            DEBUG_STRING("\r\n[TCP] QIOPEN failed after retry. Abort this send.\r\n");
            g_tcpState = NBIOT_TCP_FINISH;
        }
        break;

    case NBIOT_TCP_BUILD_PAYLOAD:
        DEBUG_STRING("\r\n[TCP] Build payload\r\n");

        NbIot_BuildOfficePayload(&g_officePayload,
                                 g_payloadBuffer,
                                 sizeof(g_payloadBuffer));

        DEBUG_STRING("Payload: ");
        DEBUG_STRING(g_payloadBuffer);
        DEBUG_STRING("\r\n");

        g_tcpState = NBIOT_TCP_SEND_PAYLOAD;
        break;

    case NBIOT_TCP_SEND_PAYLOAD:
        DEBUG_STRING("\r\n[TCP] Send payload\r\n");

        NbIot_BuildSendCommand(g_workBuffer,
                               sizeof(g_workBuffer),
                               g_socketId,
                               g_payloadBuffer);

        if (NbIot_SendCommandAndWait(g_workBuffer, "SEND OK", NBIOT_LONG_TIMEOUT_MS, true))
        {
            g_tcpState = NBIOT_TCP_CLOSE_SOCKET;
        }
        else
        {
            DEBUG_STRING("\r\n[TCP] SEND OK not received.\r\n");
            g_tcpState = NBIOT_TCP_CLOSE_SOCKET;
        }
        break;

    case NBIOT_TCP_CLOSE_SOCKET:
        DEBUG_STRING("\r\n[TCP] Close socket\r\n");

        NbIot_BuildCloseCommand(g_workBuffer,
                                sizeof(g_workBuffer),
                                g_socketId);

        if (!NbIot_SendCommandAndWait(g_workBuffer, "OK", NBIOT_MED_TIMEOUT_MS, true))
        {
            DEBUG_STRING("\r\n[TCP] Warning: close socket did not return OK.\r\n");
        }

        g_tcpState = NBIOT_TCP_FINISH;
        break;

    case NBIOT_TCP_FINISH:
    default:
        DEBUG_STRING("\r\n[TCP] Demo send complete.\r\n");
        g_tcpState = NBIOT_TCP_IDLE;
        NbIot_PrintPrompt();
        return NBIOT_STATE_BRIDGE;
    }

    return NBIOT_STATE_TCP_TEST;
}

void NbIot_StartUdpDemo(void)
{
    NbIot_LoadOfficeDemoPayload();
    g_tcpState = NBIOT_TCP_IDLE;
    g_udpState = NBIOT_UDP_CREATE_PDP;
}

void NbIot_StartTcpDemo(void)
{
    NbIot_LoadOfficeDemoPayload();
    g_udpState = NBIOT_UDP_IDLE;
    g_tcpState = NBIOT_TCP_CREATE_PDP;
}

/* -------------------------------------------------
 * bridge state
 * ------------------------------------------------- */
static NBIOT_STATE_ENUM NbIot_ProcessBridgeState(void)
{
    if (!g_bridgeBannerPrinted)
    {
        DEBUG_STRING("\r\nModem bridge ready at 9600.\r\n");
        NbIot_PrintHelp();
        NbIot_PrintPrompt();
        g_bridgeBannerPrinted = true;

#if (ENABLE_PASSTHRU_MODE)
        if (g_passThruMode)
        {
            DEBUG_STRING("\r\nDefault startup: PASS-THROUGH MODE ENABLED\r\n");
            DEBUG_STRING("Type AT commands directly. Use 'exitpt' to leave.\r\n");
            NbIot_PrintPrompt();
        }
        else
        {
            DEBUG_STRING("\r\nDefault startup: NORMAL FIRMWARE MODE\r\n");
        }
#else
        DEBUG_STRING("\r\nDefault startup: NORMAL FIRMWARE MODE\r\n");
#endif
    }

    NbIot_ProcessSensorInput();
    NbIot_ProcessTerminalInput();
    NbIot_ProcessModemOutput();

    if (g_udpState != NBIOT_UDP_IDLE)
    {
        return NBIOT_STATE_UDP_TEST;
    }

    if (g_tcpState != NBIOT_TCP_IDLE)
    {
        return NBIOT_STATE_TCP_TEST;
    }

    return NBIOT_STATE_BRIDGE;
}

static void NbIot_ProcessSensorInput(void)
{
    uint8_t rx;

    while (hal_uart_SensorReadByte(&rx))
    {
        if ((rx == '\r') || (rx == '\n'))
        {
            continue;
        }

        if (g_sensorPacketIndex < (NBIOT_SENSOR_PACKET_LEN - 1U))
        {
            g_sensorPacketBuffer[g_sensorPacketIndex++] = (char)rx;
            g_sensorPacketBuffer[g_sensorPacketIndex] = '\0';
        }
        else
        {
            DEBUG_STRING("\r\n[MKR UART] ERROR: sensor packet too long. Buffer cleared.\r\n");
            memset(g_sensorPacketBuffer, 0, sizeof(g_sensorPacketBuffer));
            g_sensorPacketIndex = 0U;
            continue;
        }

        if (rx == '#')
        {
            DEBUG_STRING("\r\n[MKR UART RX] ");
            DEBUG_STRING(g_sensorPacketBuffer);
            DEBUG_STRING("\r\n");

            if (NbIot_ParseSensorPacket(g_sensorPacketBuffer))
            {
                DEBUG_STRING("[MKR UART] Parsed OK: ");
                NbIot_PrintMkrSensorData();
                DEBUG_STRING("[MKR UART] Starting UDP send with MKR sensor payload...\r\n");
                NbIot_StartUdpDemo();
            }
            else
            {
                DEBUG_STRING("[MKR UART] ERROR: invalid sensor packet.\r\n");
            }

            memset(g_sensorPacketBuffer, 0, sizeof(g_sensorPacketBuffer));
            g_sensorPacketIndex = 0U;
            return;
        }
    }
}

static void NbIot_ProcessTerminalInput(void)
{
    uint8_t rx;

    while (hal_uart_DebugReadByte(&rx))
    {
        if ((rx == '\r') || (rx == '\n'))
        {
            DEBUG_STRING("\r\n");

            if (g_cmdIndex > 0U)
            {
                g_cmdBuffer[g_cmdIndex] = '\0';

                if (strncmp(g_cmdBuffer, "$SENSOR", 7U) == 0)
                {
                    DEBUG_STRING("[MKR CMD] Sensor packet received: ");
                    DEBUG_STRING(g_cmdBuffer);
                    DEBUG_STRING("\r\n");

                    if (NbIot_ParseSensorPacket(g_cmdBuffer))
                    {
                        DEBUG_STRING("[MKR CMD] Parsed OK: ");
                        NbIot_PrintMkrSensorData();
                        DEBUG_STRING("[MKR CMD] Starting UDP send with MKR sensor payload...\r\n");
                        g_cmdIndex = 0U;
                        NbIot_StartUdpDemo();
                        return;
                    }
                    else
                    {
                        DEBUG_STRING("[MKR CMD] ERROR: invalid sensor packet. Expected: $SENSOR,N2O=145,TEMP=22.6,HUM=84.1#\r\n");
                        g_cmdIndex = 0U;
                        NbIot_PrintPrompt();
                        return;
                    }
                }

#if (ENABLE_PASSTHRU_MODE)
                if (g_passThruMode)
                {
                    if (strcmp(g_cmdBuffer, "exitpt") == 0)
                    {
                        g_cmdIndex = 0U;
                        NbIot_ExitPassThruMode();
                        return;
                    }
                    else
                    {
                        hal_uart_ModemWriteString(g_cmdBuffer);
                        hal_uart_ModemWriteString("\r\n");
                    }

                    g_cmdIndex = 0U;
                    NbIot_PrintPrompt();
                    continue;
                }
#endif

                if (strcmp(g_cmdBuffer, "help") == 0)
                {
                    NbIot_PrintHelp();
                }
                else if (strcmp(g_cmdBuffer, "payload") == 0)
                {
                    NbIot_LoadOfficeDemoPayload();
                    NbIot_BuildOfficePayload(&g_officePayload,
                                             g_payloadBuffer,
                                             sizeof(g_payloadBuffer));
                    DEBUG_STRING("Payload preview: ");
                    DEBUG_STRING(g_payloadBuffer);
                    DEBUG_STRING("\r\n");
                }
                else if (strcmp(g_cmdBuffer, "udp") == 0)
                {
                    DEBUG_STRING("Starting UDP demo send...\r\n");
                    g_cmdIndex = 0U;
                    NbIot_StartUdpDemo();
                    return;
                }
                else if (strcmp(g_cmdBuffer, "tcp") == 0)
                {
                    DEBUG_STRING("Starting TCP demo send...\r\n");
                    g_cmdIndex = 0U;
                    NbIot_StartTcpDemo();
                    return;
                }
#if (ENABLE_PASSTHRU_MODE)
                else if (strcmp(g_cmdBuffer, "passthru") == 0)
                {
                    g_cmdIndex = 0U;
                    NbIot_EnterPassThruMode();
                    return;
                }
#endif
                else
                {
                    hal_uart_ModemWriteString(g_cmdBuffer);
                    hal_uart_ModemWriteString("\r\n");
                }

                g_cmdIndex = 0U;
            }

            NbIot_PrintPrompt();
        }
        else if ((rx == 0x08U) || (rx == 0x7FU))
        {
            if (g_cmdIndex > 0U)
            {
                g_cmdIndex--;
                DEBUG_STRING("\b \b");
            }
        }
        else
        {
            if (g_cmdIndex < (NBIOT_CMD_BUFFER_LEN - 1U))
            {
                g_cmdBuffer[g_cmdIndex++] = (char)rx;
                hal_uart_debug_write_char((char)rx);
            }
        }
    }
}

static void NbIot_ProcessModemOutput(void)
{
    uint8_t rx;

    while (hal_uart_ModemReadByte(&rx))
    {
        hal_uart_debug_write_char((char)rx);
    }
}

/* -------------------------------------------------
 * public API
 * ------------------------------------------------- */
void NbIot_Init(void)
{
    g_nbiotState = NBIOT_STATE_BOOT;
    g_bootState = NBIOT_BOOT_PWR_EN;
    g_initState = NBIOT_INIT_CMEE;
    g_udpState = NBIOT_UDP_IDLE;
    g_tcpState = NBIOT_TCP_IDLE;
    g_cmdIndex = 0U;
    g_bridgeBannerPrinted = false;

    memset(&g_officePayload, 0, sizeof(g_officePayload));
    memset(g_payloadBuffer, 0, sizeof(g_payloadBuffer));
    memset(g_workBuffer, 0, sizeof(g_workBuffer));
    memset(g_recvLineBuffer, 0, sizeof(g_recvLineBuffer));
    memset(g_respBuffer, 0, sizeof(g_respBuffer));
    memset(g_sensorPacketBuffer, 0, sizeof(g_sensorPacketBuffer));
    g_sensorPacketIndex = 0U;
    g_mkrSensorData.valid = false;

    DEBUG_STRING("\r\n====================================\r\n");
    DEBUG_STRING(" MSP430FR5043 BC660K UART Bridge\r\n");
    DEBUG_STRING(" Debug UART : P2.0 TX, P2.1 RX\r\n");
    DEBUG_STRING(" Sensor UART: P4.3 TX, P4.4 RX\r\n");
    DEBUG_STRING(" Modem UART : P5.0 TX, P5.1 RX\r\n");
    DEBUG_STRING(" MDM_PKEY   : P1.6\r\n");
    DEBUG_STRING(" MDM_PEN    : P1.7\r\n");
    DEBUG_STRING(" MDM_INT    : P5.2\r\n");
    DEBUG_STRING(" MDM_RST    : P5.3\r\n");
    DEBUG_STRING("====================================\r\n");

    baudRate();

#if (ENABLE_PASSTHRU_MODE)
    DEBUG_STRING("\r\nBuild config: PASS-THROUGH FEATURE ENABLED\r\n");
#else
    DEBUG_STRING("\r\nBuild config: PASS-THROUGH FEATURE DISABLED\r\n");
#endif
}

NBIOT_RESULT_ENUM NbIot_Process(void)
{
    NBIOT_STATE_ENUM nextState = g_nbiotState;

    switch (g_nbiotState)
    {
    case NBIOT_STATE_BOOT:
        nextState = NbIot_ProcessBootState();
        break;

    case NBIOT_STATE_INIT:
        nextState = NbIot_ProcessInitState();
        break;

    case NBIOT_STATE_BRIDGE:
        nextState = NbIot_ProcessBridgeState();
        break;

    case NBIOT_STATE_UDP_TEST:
        nextState = NbIot_ProcessUdpTestState();
        break;

    case NBIOT_STATE_TCP_TEST:
        nextState = NbIot_ProcessTcpTestState();
        break;

    case NBIOT_STATE_ERROR:
    default:
        DEBUG_STRING("\r\nNB-IoT state machine error.\r\n");
        return NBIOT_RES_ERROR;
    }

    g_nbiotState = nextState;

    if (g_nbiotState == NBIOT_STATE_ERROR)
    {
        return NBIOT_RES_ERROR;
    }

    if ((g_nbiotState == NBIOT_STATE_BRIDGE) && g_bridgeBannerPrinted)
    {
        return NBIOT_RES_DONE;
    }

    return NBIOT_RES_BUSY;
}

void NbIot_Task(void)
{
    NBIOT_RESULT_ENUM res = NbIot_Process();

    if (res == NBIOT_RES_ERROR)
    {
        NbIot_RequestFatalRecovery();
        return;
    }
}

void NbIot_PrintHelp(void)
{
    DEBUG_STRING("\r\n========== NB-IoT Help ==========\r\n");
    DEBUG_STRING("Type any AT command and press Enter.\r\n");
    DEBUG_STRING("Useful commands:\r\n");
    DEBUG_STRING("  AT\r\n");
    DEBUG_STRING("  ATI\r\n");
    DEBUG_STRING("  AT+CGMR\r\n");
    DEBUG_STRING("  AT+CEREG?\r\n");
    DEBUG_STRING("  AT+CGATT?\r\n");
    DEBUG_STRING("  AT+CSQ\r\n");
    DEBUG_STRING("  payload    -> show office-format payload preview\r\n");
    DEBUG_STRING("  $SENSOR,N2O=145,TEMP=22.6,HUM=84.1# -> parse and send UDP\r\n");
    DEBUG_STRING("  udp        -> quick check, one recovery if needed, then send UDP\r\n");
    DEBUG_STRING("  tcp        -> quick check, one recovery if needed, then send TCP\r\n");
#if (ENABLE_PASSTHRU_MODE)
    DEBUG_STRING("  passthru   -> enter modem pass-through mode\r\n");
    DEBUG_STRING("  exitpt     -> leave pass-through mode\r\n");
#endif
    DEBUG_STRING("  help\r\n");
    DEBUG_STRING("================================\r\n");
}

#endif /* USE_NBIOT_RADIO */
