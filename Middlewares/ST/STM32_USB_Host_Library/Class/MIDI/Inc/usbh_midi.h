/**
  ******************************************************************************
  * @file    usbh_midi.h
  * @author  Greg Burns
  * @author  MCD Application Team
  * @brief   This file contains all the prototypes for the usbh_midi.c
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2015 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                      www.st.com/SLA0044
  *
  ******************************************************************************
  */

/* Define to prevent recursive  ----------------------------------------------*/
#ifndef __USBH_MIDI_H
#define __USBH_MIDI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "usbh_core.h"

typedef enum {
    MIDI_INIT = 0,
    MIDI_IDLE,
    MIDI_RX,
    MIDI_RX_POLL,
    MIDI_RX_ERROR,
    MIDI_FATAL_ERROR
} MIDI_StateTypeDef;

typedef enum {
    MIDI_OK,
    MIDI_BUSY,
    MIDI_ERROR
} MIDI_ErrorTypeDef;

typedef void (*USBH_MIDI_RxCallback)(uint8_t* buff, size_t len, void* pUser);

#define USBH_MIDI_RX_BUF_SIZE 64
#define USBH_MIDI_TX_BUF_SIZE 64

/* Structure for MIDI process */
typedef struct _MIDI_Process {
    uint8_t              InPipe;
    uint8_t              InEp;
    uint16_t             InEpSize;
    uint8_t              OutPipe;
    uint8_t              OutEp;
    uint16_t             OutEpSize;
    MIDI_StateTypeDef    state;
    MIDI_ErrorTypeDef    error;
    USBH_MIDI_RxCallback callback;
    void* pUser;
    uint8_t rxBuffer[USBH_MIDI_RX_BUF_SIZE];
    uint8_t txBuffer[USBH_MIDI_TX_BUF_SIZE];
} MIDI_HandleTypeDef;

/* MIDI Class Codes */
#define USB_AUDIO_CLASS              0x01U
#define USB_MIDI_STREAMING_SUBCLASS  0x03U

extern USBH_ClassTypeDef  USBH_midi;
#define USBH_MIDI_CLASS   &USBH_midi

uint8_t USBH_MIDI_IsReady(USBH_HandleTypeDef *phost);

MIDI_ErrorTypeDef USBH_MIDI_Transmit(USBH_HandleTypeDef *phost,
        uint8_t* data, size_t len);

void USBH_MIDI_SetReceiveCallback(USBH_HandleTypeDef *phost,
        USBH_MIDI_RxCallback cb, void* pUser);

#ifdef __cplusplus
}
#endif

#endif  /* __USBH_MIDI_H */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
