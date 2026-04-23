#include "Nbiot.h"

#include <string.h>
#include <stdio.h>
#include <msp430.h>

#include "studiolib.h"
#include "hal_uart.h"
#include "hal_gpio.h"
#include "hal_system.h"

/* -------------------------------------------------
 * local configuration
 * ------------------------------------------------- */
#define NBIOT_CMD_BUFFER_LEN        128U
#define NBIOT_WORK_BUFFER_LEN       192U
#define NBIOT_PAYLOAD_BUFFER_LEN    160U
#define NBIOT_RECV_LINE_LEN         192U

#define NBIOT_SHORT_TIMEOUT_MS      1200U
#define NBIOT_MED_TIMEOUT_MS        2500U
#define NBIOT_LONG_TIMEOUT_MS       15000U
#define NBIOT_BOOT_WAIT_MS          1500U
#define NBIOT_RESET_PULSE_MS        120U
#define NBIOT_POWER_SETTLE_MS       200U

/* -------------------------------------------------
 * local state
 * ------------------------------------------------- */
static NBIOT_STATE_ENUM g_nbiotState = NBIOT_STATE_BOOT;
static NBIOT_BOOT_STATE_ENUM g_bootState = NBIOT_BOOT_PWR_EN;
static NBIOT_INIT_STATE_ENUM g_initState = NBIOT_INIT_CMEE;
static NBIOT_UDP_STATE_ENUM g_udpState = NBIOT_UDP_IDLE;
static NBIOT_TCP_STATE_ENUM g_tcpState = NBIOT_TCP_IDLE;

static char g_cmdBuffer[NBIOT_CMD_BUFFER_LEN];
static uint16_t g_cmdIndex = 0;
static bool g_bridgeBannerPrinted = false;

static NBIOT_OFFICE_PAYLOAD_TYPE g_officePayload;
static char g_payloadBuffer[NBIOT_PAYLOAD_BUFFER_LEN];
static char g_workBuffer[NBIOT_WORK_BUFFER_LEN];
static char g_recvLineBuffer[NBIOT_RECV_LINE_LEN];
static uint8_t g_socketId = 0;

/* -------------------------------------------------
 * local helpers
 * ------------------------------------------------- */
static void NbIot_DelayMs(uint16_t ms);
static void NbIot_SendRaw(const char *cmd);
static bool NbIot_WaitForToken(const char *token, uint16_t timeoutMs, bool echoToDebug);
static bool NbIot_SendCommandAndWait(const char *cmd, const char *expected, uint16_t timeoutMs, bool echoToDebug);
static bool NbIot_WaitForRecvUrc(uint16_t timeoutMs, bool echoToDebug);
static bool NbIot_WaitForRecvLine(char *lineBuffer,
                                  uint16_t bufferSize,
                                  uint16_t timeoutMs,
                                  bool echoToDebug);
static void NbIot_PrintPrompt(void);
static void NbIot_RemoteTestFunction(void);
static void NbIot_HandleDownlinkCommand(const char *urcLine);

static NBIOT_STATE_ENUM NbIot_ProcessBootState(void);
static NBIOT_STATE_ENUM NbIot_ProcessInitState(void);
static NBIOT_STATE_ENUM NbIot_ProcessBridgeState(void);
static NBIOT_STATE_ENUM NbIot_ProcessUdpTestState(void);
static NBIOT_STATE_ENUM NbIot_ProcessTcpTestState(void);

static void NbIot_ProcessTerminalInput(void);
static void NbIot_ProcessModemOutput(void);

/* payload / socket command builders */
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
static void NbIot_BuildReadCommand(char *buffer,
                                   uint16_t bufferSize,
                                   uint8_t socketId,
                                   uint16_t readLen);

static void NbIot_DelayMs(uint16_t ms)
{
    while (ms--)
    {
        __delay_cycles(1000);   /* ~1 MHz */
    }
}

static void NbIot_SendRaw(const char *cmd)
{
    hal_uart_ModemWriteString(cmd);
}

static bool NbIot_WaitForToken(const char *token, uint16_t timeoutMs, bool echoToDebug)
{
    uint8_t rx;
    uint16_t matched = 0;
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

        NbIot_DelayMs(1);
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

static bool NbIot_WaitForRecvUrc(uint16_t timeoutMs, bool echoToDebug)
{
    return NbIot_WaitForToken("+QIURC: \"recv\"", timeoutMs, echoToDebug);
}

/* capture full modem line that contains +QIURC: "recv" */
static bool NbIot_WaitForRecvLine(char *lineBuffer,
                                  uint16_t bufferSize,
                                  uint16_t timeoutMs,
                                  bool echoToDebug)
{
    uint8_t rx;
    uint16_t idx = 0;

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

                idx = 0;
                memset(lineBuffer, 0, bufferSize);
            }
        }

        NbIot_DelayMs(1);
    }

    return false;
}

static void NbIot_PrintPrompt(void)
{
    DEBUG_STRING("\r\n> ");
}

/* -------------------------------------------------
 * downlink test function + command handler
 * ------------------------------------------------- */
static void NbIot_RemoteTestFunction(void)
{
    DEBUG_STRING("\r\n[DOWNLINK] Tell me to call the function\r\n");
    DEBUG_STRING("\r\n YOU ARE THE BEST\r\n");
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
 * office payload functions
 * ------------------------------------------------- */
void NbIot_LoadOfficeDemoPayload(void)
{
    memset(&g_officePayload, 0, sizeof(g_officePayload));

    strcpy(g_officePayload.imei,   "861214083357348");
    strcpy(g_officePayload.index,  "0000");
    strcpy(g_officePayload.flags,  "10");
    strcpy(g_officePayload.simId,  "89882280666220454783");
    g_officePayload.rssi = 20;
    strcpy(g_officePayload.hexBody, "69E6BE42030201030E4A0000");
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
    (void)localPort;   /* use destination port as local port */

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

static void NbIot_BuildReadCommand(char *buffer,
                                   uint16_t bufferSize,
                                   uint8_t socketId,
                                   uint16_t readLen)
{
    if ((buffer == 0) || (bufferSize == 0U))
    {
        return;
    }

    snprintf(buffer,
             bufferSize,
             "%s%u,%u\r\n",
             NBIOT_CMD_READ_DATA_BASE,
             (unsigned int)socketId,
             (unsigned int)readLen);
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
        (void)NbIot_WaitForToken("RDY", 1500, true);
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
            NbIot_DelayMs(150);
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
        (void)NbIot_SendCommandAndWait(NBIOT_CMD_CGPADDR, "OK", NBIOT_MED_TIMEOUT_MS, true);
        g_initState = NBIOT_INIT_FINISH;
        break;

    case NBIOT_INIT_FINISH:
    default:
        DEBUG_STRING("\r\nInitialisation state complete.\r\n");
        g_initState = NBIOT_INIT_CMEE;
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
        NbIot_BuildPdpCommand(g_workBuffer, sizeof(g_workBuffer), NBIOT_TEST_APN);

        if (NbIot_SendCommandAndWait(g_workBuffer, "OK", NBIOT_MED_TIMEOUT_MS, true))
        {
            g_udpState = NBIOT_UDP_OPEN_SOCKET;
        }
        else
        {
            return NBIOT_STATE_ERROR;
        }
        break;

    case NBIOT_UDP_OPEN_SOCKET:
        DEBUG_STRING("\r\n[UDP] Open socket\r\n");
        g_socketId = 0;

        NbIot_BuildUdpOpenCommand(g_workBuffer,
                                  sizeof(g_workBuffer),
                                  NBIOT_TEST_SERVER_IP,
                                  NBIOT_TEST_UDP_SERVER_PORT,
                                  NBIOT_TEST_LOCAL_PORT);

        DEBUG_STRING("\r\nDestination IP  : ");
        DEBUG_STRING(NBIOT_TEST_SERVER_IP);
        DEBUG_STRING("\r\nDestination Port: ");
        DEBUG_STRING(NBIOT_TEST_UDP_SERVER_PORT);
        DEBUG_STRING("\r\nLocal Port      : ");
        DEBUG_STRING(NBIOT_TEST_UDP_SERVER_PORT);
        DEBUG_STRING("\r\n");

        NbIot_SendRaw(g_workBuffer);

        if (NbIot_WaitForToken("+QIOPEN", NBIOT_LONG_TIMEOUT_MS, true))
        {
            g_udpState = NBIOT_UDP_BUILD_PAYLOAD;
        }
        else
        {
            DEBUG_STRING("\r\n[UDP] No QIOPEN response.\r\n");
            return NBIOT_STATE_ERROR;
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
            return NBIOT_STATE_ERROR;
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

            /* For current AWS/Python bridge test, the returned payload
             * is expected in the URC line itself. Skip QIRD for now.
             */
            g_udpState = NBIOT_UDP_CLOSE_SOCKET;
        }
        else
        {
            DEBUG_STRING("\r\n[UDP] No server response after SEND OK. Closing socket.\r\n");
            g_udpState = NBIOT_UDP_CLOSE_SOCKET;
        }
        break;

    case NBIOT_UDP_READ_RESPONSE:
        DEBUG_STRING("\r\n[UDP] Read server response\r\n");

        NbIot_BuildReadCommand(g_workBuffer,
                               sizeof(g_workBuffer),
                               g_socketId,
                               64U);

        if (NbIot_SendCommandAndWait(g_workBuffer, "OK", NBIOT_LONG_TIMEOUT_MS, true))
        {
            DEBUG_STRING("\r\n[UDP] Response read complete.\r\n");
        }
        else
        {
            DEBUG_STRING("\r\n[UDP] Warning: QIRD did not finish with OK.\r\n");
        }

        g_udpState = NBIOT_UDP_CLOSE_SOCKET;
        break;

    case NBIOT_UDP_CLOSE_SOCKET:
        DEBUG_STRING("\r\n[UDP] Close socket\r\n");

        NbIot_BuildCloseCommand(g_workBuffer,
                                sizeof(g_workBuffer),
                                g_socketId);

        if (NbIot_SendCommandAndWait(g_workBuffer, "OK", NBIOT_MED_TIMEOUT_MS, true))
        {
            g_udpState = NBIOT_UDP_FINISH;
        }
        else
        {
            DEBUG_STRING("\r\n[UDP] Warning: close socket did not return OK.\r\n");
            g_udpState = NBIOT_UDP_FINISH;
        }
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
        NbIot_BuildPdpCommand(g_workBuffer, sizeof(g_workBuffer), NBIOT_TEST_APN);

        if (NbIot_SendCommandAndWait(g_workBuffer, "OK", NBIOT_MED_TIMEOUT_MS, true))
        {
            g_tcpState = NBIOT_TCP_OPEN_SOCKET;
        }
        else
        {
            return NBIOT_STATE_ERROR;
        }
        break;

    case NBIOT_TCP_OPEN_SOCKET:
        DEBUG_STRING("\r\n[TCP] Open socket\r\n");
        g_socketId = 0;

        NbIot_BuildTcpOpenCommand(g_workBuffer,
                                  sizeof(g_workBuffer),
                                  NBIOT_TEST_SERVER_IP,
                                  NBIOT_TEST_TCP_SERVER_PORT);

        DEBUG_STRING("\r\nDestination IP  : ");
        DEBUG_STRING(NBIOT_TEST_SERVER_IP);
        DEBUG_STRING("\r\nDestination Port: ");
        DEBUG_STRING(NBIOT_TEST_TCP_SERVER_PORT);
        DEBUG_STRING("\r\n");

        NbIot_SendRaw(g_workBuffer);

        if (NbIot_WaitForToken("+QIOPEN", NBIOT_LONG_TIMEOUT_MS, true))
        {
            g_tcpState = NBIOT_TCP_BUILD_PAYLOAD;
        }
        else
        {
            DEBUG_STRING("\r\n[TCP] No QIOPEN response.\r\n");
            return NBIOT_STATE_ERROR;
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
            return NBIOT_STATE_ERROR;
        }
        break;

    case NBIOT_TCP_CLOSE_SOCKET:
        DEBUG_STRING("\r\n[TCP] Close socket\r\n");

        NbIot_BuildCloseCommand(g_workBuffer,
                                sizeof(g_workBuffer),
                                g_socketId);

        if (NbIot_SendCommandAndWait(g_workBuffer, "OK", NBIOT_MED_TIMEOUT_MS, true))
        {
            g_tcpState = NBIOT_TCP_FINISH;
        }
        else
        {
            DEBUG_STRING("\r\n[TCP] Warning: close socket did not return OK.\r\n");
            g_tcpState = NBIOT_TCP_FINISH;
        }
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
    }

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

    DEBUG_STRING("\r\n====================================\r\n");
    DEBUG_STRING("\r\n*** NBIOT BUILD: DOWNLINK_V2 ***\r\n");
    DEBUG_STRING(" MSP430FR5043 BC660K UART Bridge\r\n");
    DEBUG_STRING(" Debug UART : P2.0 TX, P2.1 RX\r\n");
    DEBUG_STRING(" Modem UART : P5.0 TX, P5.1 RX\r\n");
    DEBUG_STRING(" MDM_PKEY   : P1.6\r\n");
    DEBUG_STRING(" MDM_PEN    : P1.7\r\n");
    DEBUG_STRING(" MDM_INT    : P5.2\r\n");
    DEBUG_STRING(" MDM_RST    : P5.3\r\n");
    DEBUG_STRING("====================================\r\n");

    baudRate();
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
        DEBUG_STRING("\r\nModem sync/config/socket test failed.\r\n");
        g_nbiotState = NBIOT_STATE_ERROR;

        while (1)
        {
            Gpio_ToggleHeartbeatLed();
            NbIot_DelayMs(300);
        }
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
    DEBUG_STRING("  AT+CGPADDR\r\n");
    DEBUG_STRING("  payload   -> show office-format payload preview\r\n");
    DEBUG_STRING("  udp       -> send one UDP office-format payload and wait for full recv URC\r\n");
    DEBUG_STRING("  tcp       -> send one TCP office-format payload\r\n");
    DEBUG_STRING("  help\r\n");
    DEBUG_STRING("================================\r\n");
}
