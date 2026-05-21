/*
 * SigFox.h
 *
 *  Created on: 21 May 2026
 *      Author: B4T
 */

//******************************************************************************
// File: SigFox.h
//******************************************************************************

#ifndef SIGFOX_H_
#define SIGFOX_H_

#include <msp430.h>
#include <stdint.h>
#include <stdbool.h>
#include "user_config.h"

#if (USE_SIGFOX_RADIO)

/* -------------------------------------------------
 * Sigfox AT command definitions
 * ------------------------------------------------- */
#define SIGFOX_CMD_AT                "AT\r\n"
#define SIGFOX_CMD_GET_ID            "AT$I=10\r\n"
#define SIGFOX_CMD_GET_PAC           "AT$I=11\r\n"
#define SIGFOX_CMD_SLEEP             "AT$P=1\r\n"
#define SIGFOX_CMD_WAKE              "AT$P=0\r\n"
#define SIGFOX_CMD_GET_FREQ          "AT$IF?\r\n"
#define SIGFOX_CMD_SET_POWER_EU      "ATS302=15\r\n"
#define SIGFOX_CMD_SET_POWER_Z2Z4    "ATS302=24\r\n"
#define SIGFOX_CMD_PRIVATE_KEY       "ATS410=0\r\n"
#define SIGFOX_CMD_SAVE_CONFIG       "AT$WR\r\n"
#define SIGFOX_CMD_SEND_BASE         "AT$SF="
#define SIGFOX_CMD_CW_EU_ON          "AT$CW=868130000,1,15\r\n"
#define SIGFOX_CMD_CW_EU_OFF         "AT$CW=868130000,0,15\r\n"

#define SIGFOX_CMD_UL_RCZ1           "AT$IF=868130000\r\n"
#define SIGFOX_CMD_UL_RCZ2           "AT$IF=902200000\r\n"
#define SIGFOX_CMD_UL_RCZ3           "AT$IF=923200000\r\n"
#define SIGFOX_CMD_UL_RCZ4           "AT$IF=920800000\r\n"
#define SIGFOX_CMD_UL_RCZ5           "AT$IF=923300000\r\n"
#define SIGFOX_CMD_UL_RCZ6           "AT$IF=865200000\r\n"

#define SIGFOX_CMD_DL_RCZ1           "AT$DR=869525000\r\n"
#define SIGFOX_CMD_DL_RCZ2           "AT$DR=905200000\r\n"
#define SIGFOX_CMD_DL_RCZ3           "AT$DR=922200000\r\n"
#define SIGFOX_CMD_DL_RCZ4           "AT$DR=922300000\r\n"
#define SIGFOX_CMD_DL_RCZ5           "AT$DR=922300000\r\n"
#define SIGFOX_CMD_DL_RCZ6           "AT$DR=866300000\r\n"

#define SIGFOX_MAX_PAYLOAD_BYTES     12U
#define SIGFOX_MAX_PAYLOAD_HEX_LEN   24U
#define SIGFOX_DEVICE_ID_LEN         8U
#define SIGFOX_PAC_LEN               16U
#define SIGFOX_DOWNLINK_HEX_LEN      16U

typedef enum
{
    SIGFOX_REGION_RCZ1 = 1,     /* UK / Europe, 868 MHz */
    SIGFOX_REGION_RCZ2,
    SIGFOX_REGION_RCZ3,
    SIGFOX_REGION_RCZ4,
    SIGFOX_REGION_RCZ5,
    SIGFOX_REGION_RCZ6
} SIGFOX_REGION_ENUM;

typedef enum
{
    SIGFOX_STATE_BOOT = 0,
    SIGFOX_STATE_INIT,
    SIGFOX_STATE_BRIDGE,
    SIGFOX_STATE_SEND_TEST,
    SIGFOX_STATE_ERROR
} SIGFOX_STATE_ENUM;

typedef enum
{
    SIGFOX_RES_BUSY = 0,
    SIGFOX_RES_DONE,
    SIGFOX_RES_ERROR
} SIGFOX_RESULT_ENUM;

typedef enum
{
    SIGFOX_BOOT_PWR_EN = 0,
    SIGFOX_BOOT_RESET_ASSERT,
    SIGFOX_BOOT_RESET_RELEASE,
    SIGFOX_BOOT_WAIT_STARTUP,
    SIGFOX_BOOT_TRY_AT,
    SIGFOX_BOOT_FINISH
} SIGFOX_BOOT_STATE_ENUM;

typedef enum
{
    SIGFOX_INIT_READ_ID = 0,
    SIGFOX_INIT_READ_PAC,
    SIGFOX_INIT_SET_UPLINK_FREQ,
    SIGFOX_INIT_SET_DOWNLINK_FREQ,
    SIGFOX_INIT_SET_POWER,
    SIGFOX_INIT_SET_PRIVATE_KEY,
    SIGFOX_INIT_FINISH
} SIGFOX_INIT_STATE_ENUM;

typedef enum
{
    SIGFOX_SEND_IDLE = 0,
    SIGFOX_SEND_SET_POWER,
    SIGFOX_SEND_SEND_PAYLOAD,
    SIGFOX_SEND_FINISH
} SIGFOX_SEND_STATE_ENUM;

typedef struct
{
    char deviceId[SIGFOX_DEVICE_ID_LEN + 1U];
    char pac[SIGFOX_PAC_LEN + 1U];
    char payloadHex[SIGFOX_MAX_PAYLOAD_HEX_LEN + 1U];
    char downlinkHex[SIGFOX_DOWNLINK_HEX_LEN + 1U];
    SIGFOX_REGION_ENUM region;
    bool requestDownlink;
    bool downlinkAvailable;
    bool lastSendOk;
} SIGFOX_STATUS_TYPE;

/* public API */
void SigFox_Init(void);
void SigFox_Task(void);
void SigFox_PrintHelp(void);
SIGFOX_RESULT_ENUM SigFox_Process(void);

/* test control */
void SigFox_StartSendDemo(bool requestDownlink);
bool SigFox_LoadPayloadHex(const char *payloadHex, bool requestDownlink);
bool SigFox_LoadPayloadBytes(const uint8_t *payload, uint8_t length, bool requestDownlink);
const SIGFOX_STATUS_TYPE *SigFox_GetStatus(void);

/* optional RF test helpers */
void SigFox_ContinuousWaveOn(void);
void SigFox_ContinuousWaveOff(void);

#endif /* USE_SIGFOX_RADIO */

#endif /* SIGFOX_H_ */
