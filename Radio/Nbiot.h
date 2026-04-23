#ifndef NBIOT_H_
#define NBIOT_H_

#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------
 * BC660K command definitions
 * ------------------------------------------------- */

/* basic modem commands */
#define NBIOT_CMD_AT                 "AT\r\n"
#define NBIOT_CMD_ATE0               "ATE0\r\n"
#define NBIOT_CMD_ATI                "ATI\r\n"
#define NBIOT_CMD_CGMR               "AT+CGMR\r\n"
#define NBIOT_CMD_CGSN               "AT+CGSN=1\r\n"
#define NBIOT_CMD_CSQ                "AT+CSQ\r\n"
#define NBIOT_CMD_CEREG_Q            "AT+CEREG?\r\n"
#define NBIOT_CMD_CEREG_URC          "AT+CEREG=5\r\n"
#define NBIOT_CMD_CGATT_Q            "AT+CGATT?\r\n"
#define NBIOT_CMD_CGPADDR            "AT+CGPADDR\r\n"
#define NBIOT_CMD_CSCON_URC          "AT+CSCON=1\r\n"
#define NBIOT_CMD_CMEE               "AT+CMEE=1\r\n"
#define NBIOT_CMD_QNBIOTEVENT        "AT+QNBIOTEVENT=1,1\r\n"
#define NBIOT_CMD_QCFG_DSEVENT_OFF   "AT+QCFG=\"dsevent\",0\r\n"
#define NBIOT_CMD_QICFG_DATAFORMAT   "AT+QICFG=\"dataformat\",0,0\r\n"

/* baud handling */
#define NBIOT_CMD_IPR_9600           "AT+IPR=9600\r\n"

/* keep-awake / test mode */
#define NBIOT_CMD_DISABLE_PSM        "AT+CPSMS=0\r\n"
#define NBIOT_CMD_DISABLE_CEDRXS     "AT+CEDRXS=0,5\r\n"
#define NBIOT_CMD_DISABLE_NPTWEDRXS  "AT+NPTWEDRXS=0,5\r\n"

/* socket command bases */
#define NBIOT_CMD_CREATE_PDP_BASE    "AT+CGDCONT=1,\"IP\""
#define NBIOT_CMD_OPEN_SOCKET_BASE   "AT+QIOPEN=0,0,"
#define NBIOT_CMD_CLOSE_SOCKET_BASE  "AT+QICLOSE="
#define NBIOT_CMD_SEND_DATA_BASE     "AT+QISEND="
#define NBIOT_CMD_READ_DATA_BASE     "AT+QIRD="

/* -------------------------------------------------
 * Office working UDP endpoint
 * ------------------------------------------------- */
#define NBIOT_TEST_SERVER_IP         "13.135.238.190"
#define NBIOT_TEST_UDP_SERVER_PORT   "51300"
#define NBIOT_TEST_TCP_SERVER_PORT   "51300"
#define NBIOT_TEST_LOCAL_PORT        "51300"
#define NBIOT_TEST_APN               "IOT.1NCE.NET"

/* -------------------------------------------------
 * Office-style payload definition
 *
 * Final transmitted message format:
 * IMEI,INDEX,FLAGS,SIM_ID,RSSI HEX_BODY
 *
 * Example:
 * 861214083357348,0000,10,89882280666220454783,20 69E6BE42030201030E4A0000
 * ------------------------------------------------- */
typedef struct
{
    char imei[20];
    char index[5];
    char flags[3];
    char simId[24];
    uint8_t rssi;
    char hexBody[80];
} NBIOT_OFFICE_PAYLOAD_TYPE;

/* -------------------------------------------------
 * Top-level state machine
 * ------------------------------------------------- */
typedef enum
{
    NBIOT_STATE_BOOT = 0,
    NBIOT_STATE_INIT,
    NBIOT_STATE_BRIDGE,
    NBIOT_STATE_UDP_TEST,
    NBIOT_STATE_TCP_TEST,
    NBIOT_STATE_ERROR
} NBIOT_STATE_ENUM;

typedef enum
{
    NBIOT_RES_BUSY = 0,
    NBIOT_RES_DONE,
    NBIOT_RES_ERROR
} NBIOT_RESULT_ENUM;

/* boot sub-state machine */
typedef enum
{
    NBIOT_BOOT_PWR_EN = 0,
    NBIOT_BOOT_RESET_ASSERT,
    NBIOT_BOOT_RESET_RELEASE,
    NBIOT_BOOT_WAIT_STARTUP,
    NBIOT_BOOT_TRY_AT_9600,
    NBIOT_BOOT_TRY_AT_115200,
    NBIOT_BOOT_SET_IPR_9600,
    NBIOT_BOOT_RECHECK_AT_9600,
    NBIOT_BOOT_CLEAR_ECHO,
    NBIOT_BOOT_FINISH
} NBIOT_BOOT_STATE_ENUM;

/* init sub-state machine */
typedef enum
{
    NBIOT_INIT_CMEE = 0,
    NBIOT_INIT_CSCON,
    NBIOT_INIT_CEREG_URC,
    NBIOT_INIT_QCFG_DSEVENT,
    NBIOT_INIT_QICFG_DATAFORMAT,
    NBIOT_INIT_QNBIOTEVENT,
    NBIOT_INIT_DISABLE_PSM,
    NBIOT_INIT_DISABLE_CEDRXS,
    NBIOT_INIT_DISABLE_NPTWEDRXS,
    NBIOT_INIT_QUERY_INFO,
    NBIOT_INIT_FINISH
} NBIOT_INIT_STATE_ENUM;

/* UDP test sub-state machine */
typedef enum
{
    NBIOT_UDP_IDLE = 0,
    NBIOT_UDP_CREATE_PDP,
    NBIOT_UDP_OPEN_SOCKET,
    NBIOT_UDP_BUILD_PAYLOAD,
    NBIOT_UDP_SEND_PAYLOAD,
    NBIOT_UDP_WAIT_RESPONSE,
    NBIOT_UDP_READ_RESPONSE,
    NBIOT_UDP_CLOSE_SOCKET,
    NBIOT_UDP_FINISH
} NBIOT_UDP_STATE_ENUM;

/* TCP test sub-state machine */
typedef enum
{
    NBIOT_TCP_IDLE = 0,
    NBIOT_TCP_CREATE_PDP,
    NBIOT_TCP_OPEN_SOCKET,
    NBIOT_TCP_BUILD_PAYLOAD,
    NBIOT_TCP_SEND_PAYLOAD,
    NBIOT_TCP_CLOSE_SOCKET,
    NBIOT_TCP_FINISH
} NBIOT_TCP_STATE_ENUM;

/* public API */
void NbIot_Init(void);
void NbIot_Task(void);
void NbIot_PrintHelp(void);
NBIOT_RESULT_ENUM NbIot_Process(void);

/* office payload helper functions */
void NbIot_LoadOfficeDemoPayload(void);
void NbIot_BuildOfficePayload(const NBIOT_OFFICE_PAYLOAD_TYPE *payload,
                              char *buffer,
                              uint16_t bufferSize);

/* test control */
void NbIot_StartUdpDemo(void);
void NbIot_StartTcpDemo(void);

#endif /* NBIOT_H_ */
