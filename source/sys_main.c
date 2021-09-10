/** @file sys_main.c 
*   @brief Application main file
*   @date 11-Dec-2018
*   @version 04.07.01
*
*   This file contains an empty main function,
*   which can be used for the application.
*/

/* 
* Copyright (C) 2009-2018 Texas Instruments Incorporated - www.ti.com 
* 
* 
*  Redistribution and use in source and binary forms, with or without 
*  modification, are permitted provided that the following conditions 
*  are met:
*
*    Redistributions of source code must retain the above copyright 
*    notice, this list of conditions and the following disclaimer.
*
*    Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in the 
*    documentation and/or other materials provided with the   
*    distribution.
*
*    Neither the name of Texas Instruments Incorporated nor the names of
*    its contributors may be used to endorse or promote products derived
*    from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
*  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
*  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
*  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
*  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
*  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
*  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
*  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
*  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
*/

/* USER CODE BEGIN (0) */
/* USER CODE END */

/* Include Files */

#include "sys_common.h"

/* USER CODE BEGIN (1) */
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include "sci.h"
#include "FreeRTOS.h"
#include "os_portmacro.h"
#include "os_task.h"
#include "os_semphr.h"
/* USER CODE END */

/* USER CODE BEGIN (2) */

/*** MACROS ***/

#define MSG_BUF_LEN 64
#define TERM_CHAR '\0'
#define MSG_DEADLINE pdMS_TO_TICKS(1000)
#define DEFAULT_STACK_SIZE 1000

static void sciDisplayText(sciBASE_t *sci, uint8 *text, uint32 length);

#define GENERATE_MSG(buf, tickCount) (snprintf((char *)msg, sizeof(msg), \
                                               "Tick Count %08" PRIu32 "\r\n", tickCount))
#define WRITE_MSG(msg) sciDisplayText(sciREG, msg, strlen((const char*)msg)+1);
#define READ_MSG_BYTE() (uint8)(sciREG->RD & 0x000000FFU)

#define LOG_MSG(msg) sciDisplayText(scilinREG, msg, strlen((const char*)msg));
#define ABORT(msg) { \
        LOG_MSG(msg); \
        for(;;); \
}

/*** LOCAL VARIABLES ***/

static SemaphoreHandle_t sciRxSem;
static StaticSemaphore_t sciRxSemBuffer;

/* Stack and storage buffer space for RX task */
static uint32 rxStackBuffer[DEFAULT_STACK_SIZE];
static StaticTask_t rxTaskBuffer;

/* Stack and storage buffer space for Idle task */
static uint32 IdleStackBuffer[DEFAULT_STACK_SIZE];
static StaticTask_t IdleTaskBuffer;

static uint8 rxBuf[MSG_BUF_LEN];


/*** FUNCTIONS ***/

/** @fn void sciDisplayText()
*   @brief
*
*   This function must send a message over SCI exactly once per second. The processing time to send the
*   message must be accounted for when calculating the blocking time to ensure that the receive task
*   meets its deadline.
*/
static void sciDisplayText(sciBASE_t *sci, uint8 *text, uint32 length)
{
    while (length--)
    {
        while ((sci->FLR & 0x4) == 4)
            ; /* wait until busy */
        sciSendByte(sci, *text++); /* send out text   */
    };
}

/** @fn void vApplicationGetIdleTaskMemory()
*   @brief Provides static memory buffers for the idle task
*
*   This is a FreeRTOS hook function that will be called automatically when the scheduler is started.
*/
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                       StackType_t **ppxIdleTaskStackBuffer,
                                       uint32 *pulIdleTaskStackSize)
{
    *ppxIdleTaskStackBuffer = IdleStackBuffer;
    *ppxIdleTaskTCBBuffer = &IdleTaskBuffer;
    *pulIdleTaskStackSize = 1000;
}

/** @fn void sciNotification(void)
*   @brief Handles SCI interrupt events
*
*   This function handles SCI RX events. The received character
*   should be stored in a buffer until TERM_CHAR is received. At this point,
*   the RX task should be notified so that the full message can be processed.
*/
void sciNotification(sciBASE_t *sci, uint32 flags)
{
    if((sci == sciREG) && (flags & SCI_RX_INT))
    {
        // TODO Buffer the received character, notify the RX task after receiving the terminating character
    }
}

/** @fn void sciTxRask()
*   @brief Worker task for handling received SCI messages
*/
static void sciRxTask(void *pvParameters)
{
    static TickType_t lastMsgTime = 0;

    for(;;)
    {
       TickType_t curTime = xTaskGetTickCount();
       TickType_t waitTime = lastMsgTime + MSG_DEADLINE - curTime;
       BaseType_t ret;

       ret = xSemaphoreTake(sciRxSem, waitTime);
       if(ret == pdFALSE)
       {
           ABORT("RX Task missed a deadline!\r\n");
       }

       lastMsgTime = xTaskGetTickCount();
       LOG_MSG(rxBuf);
    }
}

// TODO Create a task that periodically writes a message to the SCI interface to ensure that the RX task meets its deadline

/* USER CODE END */

/** @fn void main(void)
*   @brief Application main function
*   @note This function is empty by default.
*
*   This function is called after startup.
*   The user can use this function to implement the application.
*/
int main(void)
{
/* USER CODE BEGIN (3) */
    sciInit();
    sciEnableNotification(sciREG, SCI_RX_INT);
    sciEnableLoopback(sciREG, Digital_Lbk);
    sciRxSem = xSemaphoreCreateBinaryStatic(&sciRxSemBuffer);

    xTaskCreateStatic(sciRxTask, "SCI RX Task", DEFAULT_STACK_SIZE, NULL, 1, rxStackBuffer, &rxTaskBuffer);

    vTaskStartScheduler();
    for(;;);
/* USER CODE END */
}

/* USER CODE BEGIN (4) */
/* USER CODE END */
