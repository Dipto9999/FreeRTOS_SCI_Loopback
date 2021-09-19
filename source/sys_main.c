/*
*   @file sys_main.c
*   @brief Application main file
*   @date 11-Dec-2018
*   @version 04.07.01
*
*   This File Contains an Empty Main Function,
*   Which Can Be Used for the Application.
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

#define LENGTH_BUFF 64
#define END_CHAR '\0'
#define MSG_DEADLINE pdMS_TO_TICKS(1000)
#define DEFAULT_STACK_SIZE 1000
#define DEFAULT_TASK_PRIORITY 4

static void sciDisplayText(sciBASE_t *sci, uint8_t *text, uint32_t length);

#define GENERATE_MSG(buff, tickCount) (snprintf((char *)msg, sizeof(msg), "Tick Count %08" PRIu32 "\r\n", tickCount))
#define WRITE_MSG(msg) sciDisplayText(sciREG, msg, strlen((const char*)msg)+1);
#define READ_MSG_BYTE() (uint8_t)(sciREG->RD & 0x000000FFU)

#define LOG_MSG(msg) sciDisplayText(scilinREG, msg, strlen((const char*)msg));
#define ABORT(msg) { LOG_MSG(msg); for(;;); }

#define TRUE 1
#define FALSE 0
#define ERROR -1

/*** LOCAL VARIABLES ***/

static SemaphoreHandle_t sciRxSem;
static StaticSemaphore_t sciRxSemBuffer;

/* Stack Buffer and TCB For RX Task. */
static uint32_t rxStackBuffer[DEFAULT_STACK_SIZE];
static StaticTask_t rxTaskBuffer;

/* Stack Buffer and TCB For TX Task. */
static uint32_t txStackBuffer[DEFAULT_STACK_SIZE];
static StaticTask_t txTaskBuffer;

/* Stack Buffer and TCB For Idle Task. */
static uint32_t IdleStackBuffer[DEFAULT_STACK_SIZE];
static StaticTask_t IdleTaskBuffer;

static uint8_t rxBuff[LENGTH_BUFF];

/*** FUNCTIONS ***/

/*
 * @fn void sciDisplayText()
 *   @brief
 *   This Function Must Send a Message Over SCI Exactly Once Per Second. The Processing Time to Send the
 *   Message Must be Accounted For When Calculating the Blocking Time to Ensure That the Receive Task
 *   Meets its Deadline.
 */
static void sciDisplayText(sciBASE_t *sci, uint8_t *text, uint32_t length) {
    while (length--) {
        while ((sci->FLR & 0x4) == 4); /* Wait Until Busy. */
        sciSendByte(sci, *text++); /* Send Out Text. */
    };
}

/*
 * @fn void vApplicationGetIdleTaskMemory()
 *   @brief Provides Static Memory Buffers For the Idle Task.
 *   This is a FreeRTOS Hook Function That Will Be Called Automatically When the Scheduler is Started.
 */
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize) {
    *ppxIdleTaskStackBuffer = IdleStackBuffer;
    *ppxIdleTaskTCBBuffer = &IdleTaskBuffer;
    *pulIdleTaskStackSize = 1000;
}

/*
 * @fn void sciNotification(void)
 *   @brief Handles SCI Interrupt Events
 *
 *   This Function Handles SCI RX Events. The Received Character
 *   Should Be Stored In a Buffer Until END_CHAR is Received. At This Point,
 *   the RX Task Should Be Notified So That the Full Message Can Be Processed.
 */
void sciNotification(sciBASE_t *sci, uint32_t flags) {
    static uint32_t buffIndex = 0;
    char rxChar;

    BaseType_t xTaskWoken;

    /*
     * Copy The Received Character to the Buffer.
     * Notify the RX task After Receiving the Terminating Character
     * Or No Storage Space in Buffer.
     */
    if ((sci == sciREG) && (flags & SCI_RX_INT)) {

        rxChar = READ_MSG_BYTE();

        /* Store Character From SCI Receive Data Buffer. */
        rxBuff[buffIndex++] = rxChar;

        xTaskWoken = pdFALSE;

        /* End of Message or Buffer Reached. */
        if (rxChar == END_CHAR || buffIndex == LENGTH_BUFF) {
            buffIndex = 0;

            xSemaphoreGiveFromISR(sciRxSem, &xTaskWoken);
            portYIELD_FROM_ISR(xTaskWoken);
        }
    }
}

/*
 *  @fn void sciRxRask()
 *   @brief Worker Task for Handling Received SCI Messages.
 */
static void sciRxTask(void *pvParameters) {
    static TickType_t xLastMsgTime = 0;

    for(;;) {
        TickType_t xCurrWakeTime = xTaskGetTickCount();
        /* Ticks Between Previous Task Execution and Wake Time Is Subtracted. */
        TickType_t waitTime = MSG_DEADLINE + (xLastMsgTime - xCurrWakeTime) ;

        /* Unable to Take Semaphore. */
        if (xSemaphoreTake(sciRxSem, waitTime) == pdFALSE) ABORT("RX Task Missed a Deadline!\r\n");

        xLastMsgTime = xTaskGetTickCount();
        LOG_MSG(rxBuff);
    }
}

/*
 *  @fn void sciTxRask()
 *   @brief Worker Task for Periodically Writing a Message to SCI
 *   Interface to Ensure that the RX Task Meets Its Deadline.
 */
static void sciTxTask(void *pvParameters) {
    uint8_t msg[LENGTH_BUFF];

    /* Initialize Tick Count Variable With the Current Time. */
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for(;;) {
        GENERATE_MSG(msg, xLastWakeTime);
        WRITE_MSG(msg);

        /* Next Message is Sent After MSG_DEADLINE Delay. */
        vTaskDelayUntil(
            &xLastWakeTime, // Time At Which Task Was Last Unblocked
            MSG_DEADLINE // Cycle Time Period
        );
    }
}

/* USER CODE END */

/** @fn void main(void)
*   @brief Application Main Function
*   @note This Function is Empty by Default.
*
*   This Function is Called After Startup.
*   The User Can Use This Function to Implement the Application.
*/
int main(void) {
/* USER CODE BEGIN (3) */
    /* Initialize SCI For Loopback TX and RX. */
    sciInit();
    sciEnableNotification(sciREG, SCI_RX_INT);
    sciEnableLoopback(sciREG, Digital_Lbk);

    /* Create Semaphore. */
    sciRxSem = xSemaphoreCreateBinaryStatic(&sciRxSemBuffer);

    /* Check if Semaphore is not NULL. */
    if (!sciRxSem) return ERROR;

    /* Create SCI TX Task. */
    xTaskCreateStatic(
        sciTxTask, //
        "SCI TX Task", // Task Name
        DEFAULT_STACK_SIZE, // Stack Size for Task
        NULL, // Parameter Pointers
        DEFAULT_TASK_PRIORITY, // Task Priority
        txStackBuffer, // Stack
        &txTaskBuffer // Task Control Block
    );

    /* Create SCI RX Task. */
    xTaskCreateStatic(
        sciRxTask, //
        "SCI RX Task", // Task Name
        DEFAULT_STACK_SIZE, // Stack Size for Task
        NULL, // Parameter Pointers
        DEFAULT_TASK_PRIORITY, // Task Priority
        rxStackBuffer, // Stack
        &rxTaskBuffer // Task Control Block
    );

    vTaskStartScheduler();
    for(;;);
/* USER CODE END */
}

/* USER CODE BEGIN (4) */
/* USER CODE END */
