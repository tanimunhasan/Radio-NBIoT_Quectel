/*
 * SigFox.c
 *
 *  Created on: 21 May 2026
 *      Author: B4T
 */



//******************************************************************************
// File: SigFox.c
//******************************************************************************

#include "SigFox.h"
#include "user_config.h"

#include <string.h>
#include <stdio.h>
#include <msp430.h>

#if (USE_SIGFOX_RADIO)

#include "studiolib.h"
#include "hal_uart.h"
#include "hal_gpio.h"
#include "hal_system.h"

#ifndef ENABLE_PASSTHRU_MODE
#define ENABLE_PASSTHRU_MODE   0
#endif

#ifndef SIGFOX_DEFAULT_REGION
#define SIGFOX_DEFAULT_REGION  1U
#endif

#ifndef SIGFOX_DEMO_PAYLOAD_HEX
#define SIGFOX_DEMO_PAYLOAD_HEX "009100E20349"
#endif

/* -------------------------------------------------
 * local configuration
 * ------------------------------------------------- */
#define SIGFOX_CMD_BUFFER_LEN          128U
#define SIGFOX_RESP_BUFFER_LEN         192U
#define SIGFOX_SHORT_TIMEOUT_MS        1200U
#define SIGFOX_MED_TIMEOUT_MS          2500U
#define SIGFOX_SEND_TIMEOUT_MS         30000U
#define SIGFOX_DOWNLINK_TIMEOUT_MS     130000U
#define SIGFOX_BOOT_WAIT_MS            500U
#define SIGFOX_RESET_PULSE_MS          120U
#define SIGFOX_POWER_SETTLE_MS         200U
#define SIGFOX_FATAL_BLINK_COUNT       6U
#define SIGFOX_FATAL_BLINK_DELAY_MS    250U

/* -------------------------------------------------
 * local state
 * ------------------------------------------------- */
static SIGFOX_STATE_ENUM g_sigfoxState = SIGFOX_STATE_BOOT;
static SIGFOX_BOOT_STATE_ENUM g_bootState = SIGFOX_BOOT_PWR_EN;
static SIGFOX_INIT_STATE_ENUM g_initState = SIGFOX_INIT_READ_ID;
static SIGFOX_SEND_STATE_ENUM g_sendState = SIGFOX_SEND_IDLE;

static char g_cmdBuffer[SIGFOX_CMD_BUFFER_LEN];
static uint16_t g_cmdIndex = 0U;
static bool g_bridgeBannerPrinted = false;

static char g_respBuffer[SIGFOX_RESP_BUFFER_LEN];
static SIGFOX_STATUS_TYPE g_sigfoxStatus;

#if (ENABLE_PASSTHRU_MODE)
static bool g_passThruMode = true;
#endif

/* -------------------------------------------------
 * local helpers
 * ------------------------------------------------- */
static void SigFox_DelayMs(uint16_t ms);
static void SigFox_SendRaw(const char *cmd);
static bool SigFox_WaitForToken(const char *token, uint16_t timeoutMs, bool echoToDebug);
static bool SigFox_SendCommandAndWait(const char *cmd, const char *expected, uint16_t timeoutMs, bool echoToDebug);
static bool SigFox_SendCommandCollectResponse(const char *cmd,
                                              char *responseBuffer,
                                              uint16_t bufferSize,
                                              uint16_t timeoutMs,
                                              bool echoToDebug);
static void SigFox_PrintPrompt(void);
static void SigFox_ProcessTerminalInput(void);
static void SigFox_ProcessModemOutput(void);

#if (ENABLE_PASSTHRU_MODE)
static void SigFox_EnterPassThruMode(void);
static void SigFox_ExitPassThruMode(void);
#endif

static SIGFOX_STATE_ENUM SigFox_ProcessBootState(void);
static SIGFOX_STATE_ENUM SigFox_ProcessInitState(void);
static SIGFOX_STATE_ENUM SigFox_ProcessBridgeState(void);
static SIGFOX_STATE_ENUM SigFox_ProcessSendTestState(void);
static void SigFox_RequestFatalRecovery(void);

static const char *SigFox_GetUplinkCommand(SIGFOX_REGION_ENUM region);
static const char *SigFox_GetDownlinkCommand(SIGFOX_REGION_ENUM region);
static const char *SigFox_GetPowerCommand(SIGFOX_REGION_ENUM region);
static bool SigFox_IsHexChar(char ch);
static bool SigFox_IsHexString(const char *text, uint8_t *length);
static bool SigFox_CopyFirstHexToken(const char *src, char *dst, uint8_t expectedLen);
static void SigFox_CopyDownlinkIfPresent(const char *src);

/* -------------------------------------------------
 * basic helpers
 * ------------------------------------------------- */
static void SigFox_DelayMs(uint16_t ms)
{
    while (ms--)
    {
        __delay_cycles(1000);
    }
}

static void SigFox_SendRaw(const char *cmd)
{
    hal_uart_ModemWriteString(cmd);
}

static bool SigFox_WaitForToken(const char *token, uint16_t timeoutMs, bool echoToDebug)
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
                hal_uart_DebugWriteChar((char)rx);
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

        SigFox_DelayMs(1U);
    }

    return false;
}

static bool SigFox_SendCommandAndWait(const char *cmd, const char *expected, uint16_t timeoutMs, bool echoToDebug)
{
    hal_uart_ModemFlush();
    DEBUG_STRING("\r\n[SIGFOX TX] ");
    DEBUG_STRING(cmd);

    SigFox_SendRaw(cmd);
    return SigFox_WaitForToken(expected, timeoutMs, echoToDebug);
}

static bool SigFox_SendCommandCollectResponse(const char *cmd,
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
    hal_uart_ModemFlush();

    DEBUG_STRING("\r\n[SIGFOX TX] ");
    DEBUG_STRING(cmd);

    SigFox_SendRaw(cmd);

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

            if ((echoToDebug == true) && (idx < (bufferSize - 1U)))
            {
                hal_uart_DebugWriteChar((char)rx);
            }

            if (idx >= 2U)
            {
                if ((responseBuffer[idx - 2U] == 'O') &&
                    (responseBuffer[idx - 1U] == 'K'))
                {
                    gotOk = true;
                }
            }

            if (idx >= 3U)
            {
                if ((responseBuffer[idx - 3U] == 'E') &&
                    (responseBuffer[idx - 2U] == 'R') &&
                    (responseBuffer[idx - 1U] == 'R'))
                {
                    gotError = true;
                }
            }

            if (idx >= 5U)
            {
                if ((responseBuffer[idx - 5U] == 'E') &&
                    (responseBuffer[idx - 4U] == 'R') &&
                    (responseBuffer[idx - 3U] == 'R') &&
                    (responseBuffer[idx - 2U] == 'O') &&
                    (responseBuffer[idx - 1U] == 'R'))
                {
                    gotError = true;
                }
            }
        }

        if ((gotOk == true) || (gotError == true))
        {
            break;
        }

        SigFox_DelayMs(1U);
        timeoutMs--;
    }

    if (echoToDebug == true)
    {
        if (gotOk == true)
        {
            DEBUG_STRING("\r\n[SIGFOX RX OK]\r\n");
        }
        else if (gotError == true)
        {
            DEBUG_STRING("\r\n[SIGFOX RX ERROR]\r\n");
        }
        else if (sawAny == true)
        {
            DEBUG_STRING("\r\n[SIGFOX RX DATA]\r\n");
        }
        else
        {
            DEBUG_STRING("\r\n[SIGFOX RX TIMEOUT]\r\n");
        }
    }

    if (gotError == true)
    {
        return false;
    }

    return sawAny;
}

static void SigFox_PrintPrompt(void)
{
    DEBUG_STRING("\r\n> ");
}

#if (ENABLE_PASSTHRU_MODE)
static void SigFox_EnterPassThruMode(void)
{
    g_passThruMode = true;
    hal_uart_ModemFlush();

    DEBUG_STRING("\r\n====================================\r\n");
    DEBUG_STRING(" SIGFOX PASS-THROUGH MODE ENABLED\r\n");
    DEBUG_STRING(" Type AT commands directly.\r\n");
    DEBUG_STRING(" Type 'exitpt' to leave pass-through mode.\r\n");
    DEBUG_STRING("====================================\r\n");

    SigFox_PrintPrompt();
}

static void SigFox_ExitPassThruMode(void)
{
    g_passThruMode = false;
    hal_uart_ModemFlush();

    DEBUG_STRING("\r\n====================================\r\n");
    DEBUG_STRING(" PASS-THROUGH MODE DISABLED\r\n");
    DEBUG_STRING(" Back to Sigfox firmware command mode.\r\n");
    DEBUG_STRING("====================================\r\n");

    SigFox_PrintPrompt();
}
#endif

/* -------------------------------------------------
 * public API
 * ------------------------------------------------- */
void SigFox_Init(void)
{
    g_sigfoxState = SIGFOX_STATE_BOOT;
    g_bootState = SIGFOX_BOOT_PWR_EN;
    g_initState = SIGFOX_INIT_READ_ID;
    g_sendState = SIGFOX_SEND_IDLE;
    g_cmdIndex = 0U;
    g_bridgeBannerPrinted = false;

    memset(g_cmdBuffer, 0, sizeof(g_cmdBuffer));
    memset(g_respBuffer, 0, sizeof(g_respBuffer));
    memset(&g_sigfoxStatus, 0, sizeof(g_sigfoxStatus));

    g_sigfoxStatus.region = (SIGFOX_REGION_ENUM)SIGFOX_DEFAULT_REGION;
    (void)SigFox_LoadPayloadHex(SIGFOX_DEMO_PAYLOAD_HEX, false);
    g_sigfoxStatus.lastSendOk = false;
    g_sigfoxStatus.downlinkAvailable = false;

    DEBUG_STRING("\r\n====================================\r\n");
    DEBUG_STRING(" MSP430FR5043 Sigfox UART Bridge\r\n");
    DEBUG_STRING(" Debug UART : P2.0 TX, P2.1 RX\r\n");
    DEBUG_STRING(" Modem UART : P5.0 TX, P5.1 RX\r\n");
    DEBUG_STRING(" MDM_PKEY   : P1.6\r\n");
    DEBUG_STRING(" MDM_PEN    : P1.7\r\n");
    DEBUG_STRING(" MDM_INT    : P5.2\r\n");
    DEBUG_STRING(" MDM_RST    : P5.3\r\n");
    DEBUG_STRING(" Region     : RCZ1 UK/EU default\r\n");
    DEBUG_STRING("====================================\r\n");

    baudRate();

#if (ENABLE_PASSTHRU_MODE)
    DEBUG_STRING("\r\nBuild config: PASS-THROUGH FEATURE ENABLED\r\n");
#else
    DEBUG_STRING("\r\nBuild config: PASS-THROUGH FEATURE DISABLED\r\n");
#endif
}

SIGFOX_RESULT_ENUM SigFox_Process(void)
{
    SIGFOX_STATE_ENUM nextState = g_sigfoxState;

    switch (g_sigfoxState)
    {
    case SIGFOX_STATE_BOOT:
        nextState = SigFox_ProcessBootState();
        break;

    case SIGFOX_STATE_INIT:
        nextState = SigFox_ProcessInitState();
        break;

    case SIGFOX_STATE_BRIDGE:
        nextState = SigFox_ProcessBridgeState();
        break;

    case SIGFOX_STATE_SEND_TEST:
        nextState = SigFox_ProcessSendTestState();
        break;

    case SIGFOX_STATE_ERROR:
    default:
        DEBUG_STRING("\r\nSigfox state machine error.\r\n");
        return SIGFOX_RES_ERROR;
    }

    g_sigfoxState = nextState;

    if (g_sigfoxState == SIGFOX_STATE_ERROR)
    {
        return SIGFOX_RES_ERROR;
    }

    if ((g_sigfoxState == SIGFOX_STATE_BRIDGE) && g_bridgeBannerPrinted)
    {
        return SIGFOX_RES_DONE;
    }

    return SIGFOX_RES_BUSY;
}

void SigFox_Task(void)
{
    SIGFOX_RESULT_ENUM res = SigFox_Process();

    if (res == SIGFOX_RES_ERROR)
    {
        SigFox_RequestFatalRecovery();
        return;
    }
}

bool SigFox_LoadPayloadHex(const char *payloadHex, bool requestDownlink)
{
    uint8_t len = 0U;

    if (payloadHex == 0)
    {
        return false;
    }

    if (!SigFox_IsHexString(payloadHex, &len))
    {
        return false;
    }

    if ((len == 0U) || ((len & 0x01U) != 0U) || (len > SIGFOX_MAX_PAYLOAD_HEX_LEN))
    {
        return false;
    }

    memset(g_sigfoxStatus.payloadHex, 0, sizeof(g_sigfoxStatus.payloadHex));
    memcpy(g_sigfoxStatus.payloadHex, payloadHex, len);
    g_sigfoxStatus.requestDownlink = requestDownlink;
    g_sigfoxStatus.lastSendOk = false;
    g_sigfoxStatus.downlinkAvailable = false;

    return true;
}

bool SigFox_LoadPayloadBytes(const uint8_t *payload, uint8_t length, bool requestDownlink)
{
    static const char hex[] = "0123456789ABCDEF";
    uint8_t i;

    if ((payload == 0) || (length == 0U) || (length > SIGFOX_MAX_PAYLOAD_BYTES))
    {
        return false;
    }

    memset(g_sigfoxStatus.payloadHex, 0, sizeof(g_sigfoxStatus.payloadHex));

    for (i = 0U; i < length; i++)
    {
        g_sigfoxStatus.payloadHex[i * 2U] = hex[(payload[i] >> 4U) & 0x0FU];
        g_sigfoxStatus.payloadHex[(i * 2U) + 1U] = hex[payload[i] & 0x0FU];
    }

    g_sigfoxStatus.requestDownlink = requestDownlink;
    g_sigfoxStatus.lastSendOk = false;
    g_sigfoxStatus.downlinkAvailable = false;

    return true;
}

const SIGFOX_STATUS_TYPE *SigFox_GetStatus(void)
{
    return &g_sigfoxStatus;
}

void SigFox_StartSendDemo(bool requestDownlink)
{
    if (!SigFox_LoadPayloadHex(SIGFOX_DEMO_PAYLOAD_HEX, requestDownlink))
    {
        DEBUG_STRING("\r\n[SIGFOX] Demo payload invalid.\r\n");
        return;
    }

    g_sendState = SIGFOX_SEND_SET_POWER;
}

void SigFox_ContinuousWaveOn(void)
{
    (void)SigFox_SendCommandAndWait(SIGFOX_CMD_CW_EU_ON, "OK", SIGFOX_MED_TIMEOUT_MS, true);
}

void SigFox_ContinuousWaveOff(void)
{
    (void)SigFox_SendCommandAndWait(SIGFOX_CMD_CW_EU_OFF, "OK", SIGFOX_MED_TIMEOUT_MS, true);
}

/* -------------------------------------------------
 * state handlers
 * ------------------------------------------------- */
static SIGFOX_STATE_ENUM SigFox_ProcessBootState(void)
{
    switch (g_bootState)
    {
    case SIGFOX_BOOT_PWR_EN:
        DEBUG_STRING("\r\nSigfox GPIO init...\r\n");
        Modem_PowerKeySet(false);
        hal_uart_initModemPort(MODEM_BAUD_9600);
        hal_uart_ModemFlush();
        DEBUG_STRING("MDM_PEN = HIGH\r\n");
        Modem_PowerEnable(true);
        SigFox_DelayMs(SIGFOX_POWER_SETTLE_MS);
        g_bootState = SIGFOX_BOOT_RESET_ASSERT;
        break;

    case SIGFOX_BOOT_RESET_ASSERT:
        DEBUG_STRING("MDM_RST = LOW pulse\r\n");
        Modem_ResetAssert(true);
        SigFox_DelayMs(SIGFOX_RESET_PULSE_MS);
        g_bootState = SIGFOX_BOOT_RESET_RELEASE;
        break;

    case SIGFOX_BOOT_RESET_RELEASE:
        DEBUG_STRING("MDM_RST = HIGH release\r\n");
        Modem_ResetAssert(false);
        g_bootState = SIGFOX_BOOT_WAIT_STARTUP;
        break;

    case SIGFOX_BOOT_WAIT_STARTUP:
        DEBUG_STRING("Waiting Sigfox modem boot...\r\n");
        SigFox_DelayMs(SIGFOX_BOOT_WAIT_MS);
        g_bootState = SIGFOX_BOOT_TRY_AT;
        break;

    case SIGFOX_BOOT_TRY_AT:
        DEBUG_STRING("\r\nTrying Sigfox sync at 9600...\r\n");
        hal_uart_initModemPort(MODEM_BAUD_9600);
        hal_uart_ModemFlush();

        if (SigFox_SendCommandAndWait(SIGFOX_CMD_AT, "OK", SIGFOX_SHORT_TIMEOUT_MS, true))
        {
            DEBUG_STRING("\r\nSigfox modem responded to AT.\r\n");
            g_bootState = SIGFOX_BOOT_FINISH;
        }
        else
        {
            return SIGFOX_STATE_ERROR;
        }
        break;

    case SIGFOX_BOOT_FINISH:
    default:
        DEBUG_STRING("\r\nSigfox boot state complete.\r\n");
        g_bootState = SIGFOX_BOOT_PWR_EN;
        return SIGFOX_STATE_INIT;
    }

    return SIGFOX_STATE_BOOT;
}

static SIGFOX_STATE_ENUM SigFox_ProcessInitState(void)
{
    switch (g_initState)
    {
    case SIGFOX_INIT_READ_ID:
        if (SigFox_SendCommandCollectResponse(SIGFOX_CMD_GET_ID,
                                              g_respBuffer,
                                              sizeof(g_respBuffer),
                                              SIGFOX_MED_TIMEOUT_MS,
                                              true))
        {
            (void)SigFox_CopyFirstHexToken(g_respBuffer,
                                           g_sigfoxStatus.deviceId,
                                           SIGFOX_DEVICE_ID_LEN);
            DEBUG_STRING("\r\nDevice ID: ");
            DEBUG_STRING(g_sigfoxStatus.deviceId);
            DEBUG_STRING("\r\n");
            g_initState = SIGFOX_INIT_READ_PAC;
        }
        else
        {
            return SIGFOX_STATE_ERROR;
        }
        break;

    case SIGFOX_INIT_READ_PAC:
        if (SigFox_SendCommandCollectResponse(SIGFOX_CMD_GET_PAC,
                                              g_respBuffer,
                                              sizeof(g_respBuffer),
                                              SIGFOX_MED_TIMEOUT_MS,
                                              true))
        {
            (void)SigFox_CopyFirstHexToken(g_respBuffer,
                                           g_sigfoxStatus.pac,
                                           SIGFOX_PAC_LEN);
            DEBUG_STRING("\r\nPAC: ");
            DEBUG_STRING(g_sigfoxStatus.pac);
            DEBUG_STRING("\r\n");
            g_initState = SIGFOX_INIT_SET_UPLINK_FREQ;
        }
        else
        {
            return SIGFOX_STATE_ERROR;
        }
        break;

    case SIGFOX_INIT_SET_UPLINK_FREQ:
        if (SigFox_SendCommandAndWait(SigFox_GetUplinkCommand(g_sigfoxStatus.region),
                                      "OK",
                                      SIGFOX_MED_TIMEOUT_MS,
                                      true))
        {
            g_initState = SIGFOX_INIT_SET_DOWNLINK_FREQ;
        }
        else
        {
            return SIGFOX_STATE_ERROR;
        }
        break;

    case SIGFOX_INIT_SET_DOWNLINK_FREQ:
        if (SigFox_SendCommandAndWait(SigFox_GetDownlinkCommand(g_sigfoxStatus.region),
                                      "OK",
                                      SIGFOX_MED_TIMEOUT_MS,
                                      true))
        {
            g_initState = SIGFOX_INIT_SET_POWER;
        }
        else
        {
            return SIGFOX_STATE_ERROR;
        }
        break;

    case SIGFOX_INIT_SET_POWER:
        if (SigFox_SendCommandAndWait(SigFox_GetPowerCommand(g_sigfoxStatus.region),
                                      "OK",
                                      SIGFOX_MED_TIMEOUT_MS,
                                      true))
        {
            g_initState = SIGFOX_INIT_SET_PRIVATE_KEY;
        }
        else
        {
            return SIGFOX_STATE_ERROR;
        }
        break;

    case SIGFOX_INIT_SET_PRIVATE_KEY:
        if (SigFox_SendCommandAndWait(SIGFOX_CMD_PRIVATE_KEY,
                                      "OK",
                                      SIGFOX_MED_TIMEOUT_MS,
                                      true))
        {
            g_initState = SIGFOX_INIT_FINISH;
        }
        else
        {
            return SIGFOX_STATE_ERROR;
        }
        break;

    case SIGFOX_INIT_FINISH:
    default:
        DEBUG_STRING("\r\nSigfox init complete.\r\n");
        g_initState = SIGFOX_INIT_READ_ID;
        return SIGFOX_STATE_BRIDGE;
    }

    return SIGFOX_STATE_INIT;
}

static SIGFOX_STATE_ENUM SigFox_ProcessBridgeState(void)
{
    if (!g_bridgeBannerPrinted)
    {
        DEBUG_STRING("\r\nSigfox bridge ready at 9600.\r\n");
        SigFox_PrintHelp();
        SigFox_PrintPrompt();
        g_bridgeBannerPrinted = true;

#if (ENABLE_PASSTHRU_MODE)
        if (g_passThruMode)
        {
            DEBUG_STRING("\r\nDefault startup: PASS-THROUGH MODE ENABLED\r\n");
            DEBUG_STRING("Type AT commands directly. Use 'exitpt' to leave.\r\n");
            SigFox_PrintPrompt();
        }
        else
        {
            DEBUG_STRING("\r\nDefault startup: NORMAL FIRMWARE MODE\r\n");
        }
#else
        DEBUG_STRING("\r\nDefault startup: NORMAL FIRMWARE MODE\r\n");
#endif
    }

    SigFox_ProcessTerminalInput();
    SigFox_ProcessModemOutput();

    if (g_sendState != SIGFOX_SEND_IDLE)
    {
        return SIGFOX_STATE_SEND_TEST;
    }

    return SIGFOX_STATE_BRIDGE;
}

static SIGFOX_STATE_ENUM SigFox_ProcessSendTestState(void)
{
    char sendCmd[48];
    uint16_t timeout;

    switch (g_sendState)
    {
    case SIGFOX_SEND_SET_POWER:
        if (SigFox_SendCommandAndWait(SigFox_GetPowerCommand(g_sigfoxStatus.region),
                                      "OK",
                                      SIGFOX_MED_TIMEOUT_MS,
                                      true))
        {
            g_sendState = SIGFOX_SEND_SEND_PAYLOAD;
        }
        else
        {
            g_sendState = SIGFOX_SEND_IDLE;
            return SIGFOX_STATE_ERROR;
        }
        break;

    case SIGFOX_SEND_SEND_PAYLOAD:
        memset(sendCmd, 0, sizeof(sendCmd));

        if (g_sigfoxStatus.requestDownlink)
        {
            snprintf(sendCmd,
                     sizeof(sendCmd),
                     "%s%s,1\r\n",
                     SIGFOX_CMD_SEND_BASE,
                     g_sigfoxStatus.payloadHex);
            timeout = SIGFOX_DOWNLINK_TIMEOUT_MS;
        }
        else
        {
            snprintf(sendCmd,
                     sizeof(sendCmd),
                     "%s%s\r\n",
                     SIGFOX_CMD_SEND_BASE,
                     g_sigfoxStatus.payloadHex);
            timeout = SIGFOX_SEND_TIMEOUT_MS;
        }

        DEBUG_STRING("\r\nSending Sigfox payload: ");
        DEBUG_STRING(g_sigfoxStatus.payloadHex);
        DEBUG_STRING("\r\n");

        g_sigfoxStatus.lastSendOk = SigFox_SendCommandCollectResponse(sendCmd,
                                                                      g_respBuffer,
                                                                      sizeof(g_respBuffer),
                                                                      timeout,
                                                                      true);

        if (g_sigfoxStatus.lastSendOk)
        {
            SigFox_CopyDownlinkIfPresent(g_respBuffer);

            if (g_sigfoxStatus.downlinkAvailable)
            {
                DEBUG_STRING("\r\nDownlink RX: ");
                DEBUG_STRING(g_sigfoxStatus.downlinkHex);
                DEBUG_STRING("\r\n");
            }

            DEBUG_STRING("\r\nSigfox send finished.\r\n");
            g_sendState = SIGFOX_SEND_FINISH;
        }
        else
        {
            DEBUG_STRING("\r\nSigfox send failed.\r\n");
            g_sendState = SIGFOX_SEND_IDLE;
            return SIGFOX_STATE_BRIDGE;
        }
        break;

    case SIGFOX_SEND_FINISH:
    default:
        g_sendState = SIGFOX_SEND_IDLE;
        return SIGFOX_STATE_BRIDGE;
    }

    return SIGFOX_STATE_SEND_TEST;
}

static void SigFox_ProcessTerminalInput(void)
{
    uint8_t rx;

    while (hal_uart_DebugReadByte(&rx))
    {
        if ((rx == '\r') || (rx == '\n'))
        {
            g_cmdBuffer[g_cmdIndex] = '\0';
            DEBUG_STRING("\r\n");

            if (g_cmdIndex > 0U)
            {
#if (ENABLE_PASSTHRU_MODE)
                if (g_passThruMode)
                {
                    if (strcmp(g_cmdBuffer, "exitpt") == 0)
                    {
                        g_cmdIndex = 0U;
                        SigFox_ExitPassThruMode();
                        return;
                    }
                    else
                    {
                        hal_uart_ModemWriteString(g_cmdBuffer);
                        hal_uart_ModemWriteString("\r\n");
                    }

                    g_cmdIndex = 0U;
                    SigFox_PrintPrompt();
                    continue;
                }
#endif

                if (strcmp(g_cmdBuffer, "help") == 0)
                {
                    SigFox_PrintHelp();
                }
                else if (strcmp(g_cmdBuffer, "id") == 0)
                {
                    DEBUG_STRING("Device ID: ");
                    DEBUG_STRING(g_sigfoxStatus.deviceId);
                    DEBUG_STRING("\r\n");
                }
                else if (strcmp(g_cmdBuffer, "pac") == 0)
                {
                    DEBUG_STRING("PAC: ");
                    DEBUG_STRING(g_sigfoxStatus.pac);
                    DEBUG_STRING("\r\n");
                }
                else if (strcmp(g_cmdBuffer, "payload") == 0)
                {
                    DEBUG_STRING("Payload preview: ");
                    DEBUG_STRING(g_sigfoxStatus.payloadHex);
                    DEBUG_STRING("\r\n");
                }
                else if (strcmp(g_cmdBuffer, "send") == 0)
                {
                    DEBUG_STRING("Starting Sigfox uplink send...\r\n");
                    g_cmdIndex = 0U;
                    SigFox_StartSendDemo(false);
                    return;
                }
                else if (strcmp(g_cmdBuffer, "senddl") == 0)
                {
                    DEBUG_STRING("Starting Sigfox uplink with callback/downlink...\r\n");
                    g_cmdIndex = 0U;
                    SigFox_StartSendDemo(true);
                    return;
                }
                else if (strcmp(g_cmdBuffer, "sleep") == 0)
                {
                    (void)SigFox_SendCommandAndWait(SIGFOX_CMD_SLEEP, "OK", SIGFOX_MED_TIMEOUT_MS, true);
                }
                else if (strcmp(g_cmdBuffer, "wake") == 0)
                {
                    (void)SigFox_SendCommandAndWait(SIGFOX_CMD_WAKE, "OK", SIGFOX_MED_TIMEOUT_MS, true);
                }
                else if (strcmp(g_cmdBuffer, "cw_on") == 0)
                {
                    SigFox_ContinuousWaveOn();
                }
                else if (strcmp(g_cmdBuffer, "cw_off") == 0)
                {
                    SigFox_ContinuousWaveOff();
                }
#if (ENABLE_PASSTHRU_MODE)
                else if (strcmp(g_cmdBuffer, "passthru") == 0)
                {
                    g_cmdIndex = 0U;
                    SigFox_EnterPassThruMode();
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

            SigFox_PrintPrompt();
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
            if (g_cmdIndex < (SIGFOX_CMD_BUFFER_LEN - 1U))
            {
                g_cmdBuffer[g_cmdIndex++] = (char)rx;
                hal_uart_DebugWriteChar((char)rx);
            }
        }
    }
}

static void SigFox_ProcessModemOutput(void)
{
    uint8_t rx;

    while (hal_uart_ModemReadByte(&rx))
    {
        hal_uart_DebugWriteChar((char)rx);
    }
}

void SigFox_PrintHelp(void)
{
    DEBUG_STRING("\r\n========== Sigfox Help ==========" "\r\n");
    DEBUG_STRING("Type any AT command and press Enter.\r\n");
    DEBUG_STRING("Useful commands:\r\n");
    DEBUG_STRING("  AT\r\n");
    DEBUG_STRING("  AT$I=10      -> device ID\r\n");
    DEBUG_STRING("  AT$I=11      -> PAC\r\n");
    DEBUG_STRING("  AT$IF?       -> uplink frequency\r\n");
    DEBUG_STRING("  id           -> show stored device ID\r\n");
    DEBUG_STRING("  pac          -> show stored PAC\r\n");
    DEBUG_STRING("  payload      -> show demo payload\r\n");
    DEBUG_STRING("  send         -> send demo uplink\r\n");
    DEBUG_STRING("  senddl       -> send demo uplink with callback/downlink\r\n");
    DEBUG_STRING("  sleep        -> AT$P=1\r\n");
    DEBUG_STRING("  wake         -> AT$P=0\r\n");
    DEBUG_STRING("  cw_on        -> continuous wave test ON\r\n");
    DEBUG_STRING("  cw_off       -> continuous wave test OFF\r\n");
#if (ENABLE_PASSTHRU_MODE)
    DEBUG_STRING("  passthru     -> enter modem pass-through mode\r\n");
    DEBUG_STRING("  exitpt       -> leave pass-through mode\r\n");
#endif
    DEBUG_STRING("  help\r\n");
    DEBUG_STRING("================================\r\n");
}

static void SigFox_RequestFatalRecovery(void)
{
    uint8_t i;

    DEBUG_STRING("\r\n[SIGFOX] Fatal recovery: power cycling modem.\r\n");

    for (i = 0U; i < SIGFOX_FATAL_BLINK_COUNT; i++)
    {
        Gpio_ToggleHeartbeatLed();
        SigFox_DelayMs(SIGFOX_FATAL_BLINK_DELAY_MS);
    }

    Modem_PowerEnable(false);
    SigFox_DelayMs(1000U);
    Modem_PowerEnable(true);
    SigFox_DelayMs(500U);

    SigFox_Init();
}

/* -------------------------------------------------
 * utility helpers
 * ------------------------------------------------- */
static const char *SigFox_GetUplinkCommand(SIGFOX_REGION_ENUM region)
{
    switch (region)
    {
    case SIGFOX_REGION_RCZ1: return SIGFOX_CMD_UL_RCZ1;
    case SIGFOX_REGION_RCZ2: return SIGFOX_CMD_UL_RCZ2;
    case SIGFOX_REGION_RCZ3: return SIGFOX_CMD_UL_RCZ3;
    case SIGFOX_REGION_RCZ4: return SIGFOX_CMD_UL_RCZ4;
    case SIGFOX_REGION_RCZ5: return SIGFOX_CMD_UL_RCZ5;
    case SIGFOX_REGION_RCZ6: return SIGFOX_CMD_UL_RCZ6;
    default:                 return SIGFOX_CMD_UL_RCZ1;
    }
}

static const char *SigFox_GetDownlinkCommand(SIGFOX_REGION_ENUM region)
{
    switch (region)
    {
    case SIGFOX_REGION_RCZ1: return SIGFOX_CMD_DL_RCZ1;
    case SIGFOX_REGION_RCZ2: return SIGFOX_CMD_DL_RCZ2;
    case SIGFOX_REGION_RCZ3: return SIGFOX_CMD_DL_RCZ3;
    case SIGFOX_REGION_RCZ4: return SIGFOX_CMD_DL_RCZ4;
    case SIGFOX_REGION_RCZ5: return SIGFOX_CMD_DL_RCZ5;
    case SIGFOX_REGION_RCZ6: return SIGFOX_CMD_DL_RCZ6;
    default:                 return SIGFOX_CMD_DL_RCZ1;
    }
}

static const char *SigFox_GetPowerCommand(SIGFOX_REGION_ENUM region)
{
    if ((region == SIGFOX_REGION_RCZ2) || (region == SIGFOX_REGION_RCZ4))
    {
        return SIGFOX_CMD_SET_POWER_Z2Z4;
    }

    return SIGFOX_CMD_SET_POWER_EU;
}

static bool SigFox_IsHexChar(char ch)
{
    return (((ch >= '0') && (ch <= '9')) ||
            ((ch >= 'A') && (ch <= 'F')) ||
            ((ch >= 'a') && (ch <= 'f')));
}

static bool SigFox_IsHexString(const char *text, uint8_t *length)
{
    uint8_t len = 0U;

    if (text == 0)
    {
        return false;
    }

    while (*text != '\0')
    {
        if (!SigFox_IsHexChar(*text))
        {
            return false;
        }

        len++;
        text++;

        if (len > SIGFOX_MAX_PAYLOAD_HEX_LEN)
        {
            return false;
        }
    }

    if (length != 0)
    {
        *length = len;
    }

    return true;
}

static bool SigFox_CopyFirstHexToken(const char *src, char *dst, uint8_t expectedLen)
{
    uint8_t count = 0U;
    const char *start = 0;

    if ((src == 0) || (dst == 0) || (expectedLen == 0U))
    {
        return false;
    }

    while (*src != '\0')
    {
        if (SigFox_IsHexChar(*src))
        {
            if (count == 0U)
            {
                start = src;
            }

            count++;

            if (count == expectedLen)
            {
                memcpy(dst, start, expectedLen);
                dst[expectedLen] = '\0';
                return true;
            }
        }
        else
        {
            count = 0U;
            start = 0;
        }

        src++;
    }

    dst[0] = '\0';
    return false;
}

static void SigFox_CopyDownlinkIfPresent(const char *src)
{
    const char *rx;
    uint8_t i;

    if (src == 0)
    {
        return;
    }

    rx = strstr(src, "RX=");
    if (rx == 0)
    {
        return;
    }

    rx += 3;

    for (i = 0U; i < SIGFOX_DOWNLINK_HEX_LEN; i++)
    {
        if (!SigFox_IsHexChar(rx[i]))
        {
            return;
        }
    }

    memcpy(g_sigfoxStatus.downlinkHex, rx, SIGFOX_DOWNLINK_HEX_LEN);
    g_sigfoxStatus.downlinkHex[SIGFOX_DOWNLINK_HEX_LEN] = '\0';
    g_sigfoxStatus.downlinkAvailable = true;
}

#endif /* USE_SIGFOX_RADIO */

