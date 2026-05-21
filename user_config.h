/*
 * user_config.h
 *
 *  Created on: 23 Apr 2026
 *      Author: B4T
 */

#ifndef USER_CONFIG_H_
#define USER_CONFIG_H_


#define ENABLE_PASSTHRU_MODE            1

#define USE_NBIOT_RADIO                 0
#define USE_SIGFOX_RADIO                1

#if(USE_NBIOT_RADIO && USE_SIGFOX_RADIO)
#error "Only one radio backend can be enabled. Select NB-IoT or SigFox, not both."
#endif

#if(!USE_NBIOT_RADIO && !USE_SIGFOX_RADIO)
#error"No radio backend selected. Enable USE_NBIOT_RADIO or USE_SIGFOX_RADIO."
#endif

/* -------------------------------------------------
 * NB_IoT test endpoint
 * ------------------------------------------------- */
#define NBIOT_TEST_SERVER_IP         "13.135.238.190"
#define NBIOT_TEST_UDP_SERVER_PORT   "51300"
#define NBIOT_TEST_TCP_SERVER_PORT   "51300"
#define NBIOT_TEST_LOCAL_PORT        "51300"
#define NBIOT_TEST_APN               "IOT.1NCE.NET"

/* -------------------------------------------------
 * Sigfox configuration
 * -------------------------------------------------
 * UK/Europe is RCZ1.
 */
#define SIGFOX_DEFAULT_REGION        1U
#define SIGFOX_DEMO_PAYLOAD_HEX      "009100E20349"

#endif /* USER_CONFIG_H_ */
