/*
 * Protocol.h
 *
 *  Created on: 30 Mar 2026
 *      Author: B4T
 */

#ifndef PROTOCOL_PROTOCOL_H_
#define PROTOCOL_PROTOCOL_H_


typedef struct
{
    int rpm;
    int weight;
}Engine;

typedef struct
{
    Engine engine;
    int speed;
}Car;

void Protocol_Init(void);
void Protocol_Update(void);
void Protocol_PrintStatus(void);

#endif /* PROTOCOL_PROTOCOL_H_ */
