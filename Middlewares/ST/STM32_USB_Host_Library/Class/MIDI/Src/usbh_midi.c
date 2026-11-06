/**
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

#include "usbh_midi.h"
#include "daisy_core.h"

static MIDI_HandleTypeDef DMA_BUFFER_MEM_SECTION static_midi;

static USBH_StatusTypeDef USBH_MIDI_InterfaceInit(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_MIDI_InterfaceDeInit(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_MIDI_Process(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_MIDI_ClassRequest(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_MIDI_SOFProcess(USBH_HandleTypeDef *phost);

USBH_ClassTypeDef USBH_midi = {
    "MIDI",
    USB_AUDIO_CLASS,
    USBH_MIDI_InterfaceInit,
    USBH_MIDI_InterfaceDeInit,
    USBH_MIDI_ClassRequest,
    USBH_MIDI_Process,
    USBH_MIDI_SOFProcess,
    NULL,
};

#define EP_IN 0x80U

/**
 * @brief  USBH_MIDI_InterfaceInit
 *         The function init the MIDI class.
 * @param  phost: Host handle
 * @retval USBH Status
 */
static USBH_StatusTypeDef USBH_MIDI_InterfaceInit(USBH_HandleTypeDef *phost)
{
    USBH_UsrLog(__FUNCTION__);
    USBH_StatusTypeDef status;
    MIDI_HandleTypeDef *MIDI_Handle;

    // Single static instance of midi handle
    phost->pActiveClass->pData = &static_midi;
    MIDI_Handle = (MIDI_HandleTypeDef*)phost->pActiveClass->pData;
    USBH_memset(MIDI_Handle, 0, sizeof(MIDI_HandleTypeDef));

    /* Find MIDI Streaming interface (Alt 0 first) */
    uint8_t interface = USBH_FindInterface(phost, phost->pActiveClass->ClassCode,
            USB_MIDI_STREAMING_SUBCLASS, 0xFFU);

    if ((interface == 0xFFU) || (interface >= USBH_MAX_NUM_INTERFACES)) {
        USBH_DbgLog("Cannot find interface for %s class.", phost->pActiveClass->Name);
        return USBH_FAIL;
    }

    /* Check if device has Alt 1 (MIDI 2.0 UMP). USBH_SelectInterface
     * bounds-checks against CfgDesc.bNumInterfaces, which counts unique
     * interface numbers (NOT entries in Itf_Desc[]), so the Alt 1 index
     * would always fail that check. Pass the Alt 0 index to satisfy the
     * bounds check; the Alt 1 index drives the endpoint scan + SET_INTERFACE
     * below. */
    uint8_t alt0_idx = interface;
    uint8_t ifnum = phost->device.CfgDesc.Itf_Desc[interface].bInterfaceNumber;
    uint8_t alt1_idx = USBH_FindInterfaceIndex(phost, ifnum, 1U);
    uint8_t use_alt = 0U;

    if (alt1_idx != 0xFFU) {
        /* Device supports MIDI 2.0 -- the endpoint scan will use Alt 1 */
        interface = alt1_idx;
        use_alt = 1U;
        USBH_UsrLog("MIDI 2.0 Alt 1 found, selecting UMP mode");
    }

    status = USBH_SelectInterface(phost, alt0_idx);
    if (status != USBH_OK) {
        return USBH_FAIL;
    }

    /* Send SET_INTERFACE if using Alt 1. USBH_SetInterface is async via
     * USBH_CtlReq; calling it once just queues the transfer. We must
     * drive it to completion BEFORE opening the IN/OUT pipes below,
     * otherwise the host opens Alt 1 pipes while the device is still
     * on Alt 0 and the device sends nothing on those pipes. */
    if (use_alt == 1U) {
        USBH_StatusTypeDef setif_status;
        uint32_t setif_start = HAL_GetTick();
        do {
            setif_status = USBH_SetInterface(phost, ifnum, 1U);
            USBH_Delay(1U);
            if ((HAL_GetTick() - setif_start) > 100U) {
                USBH_ErrLog("SET_INTERFACE Alt 1 timeout");
                break;
            }
        } while (setif_status == USBH_BUSY);
        if (setif_status != USBH_OK) {
            USBH_ErrLog("SET_INTERFACE Alt 1 failed");
        }
    }

    MIDI_Handle->altSetting = use_alt;

    /* Find the endpoints */
    for (int ep = 0; ep < phost->device.CfgDesc.Itf_Desc[interface].bNumEndpoints; ++ep) {
        if (phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[ep].bEndpointAddress & EP_IN) {
            MIDI_Handle->InEp = phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[ep].bEndpointAddress;
            MIDI_Handle->InEpSize = phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[ep].wMaxPacketSize & 0x03FFU;
            if (MIDI_Handle->InEpSize > USBH_MIDI_RX_BUF_SIZE) {
                MIDI_Handle->InEpSize = USBH_MIDI_RX_BUF_SIZE;
            }
            /* Allocate and open input pipe */
            MIDI_Handle->InPipe = USBH_AllocPipe(phost, MIDI_Handle->InEp);
            USBH_OpenPipe(phost, MIDI_Handle->InPipe, MIDI_Handle->InEp,
                    phost->device.address, phost->device.speed, USB_EP_TYPE_BULK,
                    MIDI_Handle->InEpSize);
            (void)USBH_LL_SetToggle(phost, MIDI_Handle->InPipe, 0U);
            USBH_UsrLog("InEP[%d] %02x size=%u", ep, MIDI_Handle->InEp, MIDI_Handle->InEpSize);
        } else {
            MIDI_Handle->OutEp = phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[ep].bEndpointAddress;
            MIDI_Handle->OutEpSize = phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[ep].wMaxPacketSize & 0x03FFU;
            /* Allocate and open output pipe */
            MIDI_Handle->OutPipe = USBH_AllocPipe(phost, MIDI_Handle->OutEp);
            USBH_OpenPipe(phost, MIDI_Handle->OutPipe, MIDI_Handle->OutEp,
                    phost->device.address, phost->device.speed, USB_EP_TYPE_BULK,
                    MIDI_Handle->OutEpSize);
            (void)USBH_LL_SetToggle(phost, MIDI_Handle->OutPipe, 0U);
            USBH_UsrLog("OutEP[%d] %02x size=%u", ep, MIDI_Handle->OutEp, MIDI_Handle->OutEpSize);
        }
    }

    MIDI_Handle->state = MIDI_INIT;
    MIDI_Handle->error = MIDI_OK;

    return USBH_OK;
}

/**
 * @brief  USBH_MIDI_InterfaceDeInit
 *         The function DeInit the Pipes used for the MIDI class.
 * @param  phost: Host handle
 * @retval USBH Status
 */
static USBH_StatusTypeDef USBH_MIDI_InterfaceDeInit(USBH_HandleTypeDef *phost)
{
    USBH_UsrLog(__FUNCTION__);
    MIDI_HandleTypeDef *MIDI_Handle = (MIDI_HandleTypeDef *) phost->pActiveClass->pData;
    if (MIDI_Handle) {
        if (MIDI_Handle->InPipe) {
            USBH_ClosePipe(phost, MIDI_Handle->InPipe);
            USBH_FreePipe(phost, MIDI_Handle->InPipe);
            MIDI_Handle->InPipe = 0U;     /* Reset the Channel as Free */
        }
        if (MIDI_Handle->OutPipe) {
            USBH_ClosePipe(phost, MIDI_Handle->OutPipe);
            USBH_FreePipe(phost, MIDI_Handle->OutPipe);
            MIDI_Handle->InPipe = 0U;     /* Reset the Channel as Free */
        }
        phost->pActiveClass->pData = 0U;
        MIDI_Handle->state = MIDI_INIT;
        MIDI_Handle->error = MIDI_OK;
    }
    return USBH_OK;
}

/**
 * @brief  USBH_MIDI_ClassRequest
 *         The function is responsible for handling Standard requests
 *         for MIDI class.
 * @param  phost: Host handle
 * @retval USBH Status
 */
static USBH_StatusTypeDef USBH_MIDI_ClassRequest(USBH_HandleTypeDef *phost)
{
    phost->pUser(phost, HOST_USER_CLASS_ACTIVE);
    return USBH_OK;
}

/**
 * @brief  USBH_MIDI_Process
 *         The function is for managing state machine for MIDI data transfers
 * @param  phost: Host handle
 * @retval USBH Status
 */
static USBH_StatusTypeDef USBH_MIDI_Process(USBH_HandleTypeDef *phost)
{
    if (!phost->pActiveClass || !phost->pActiveClass->pData)
        return USBH_FAIL;

    MIDI_HandleTypeDef *hMidi = (MIDI_HandleTypeDef*)phost->pActiveClass->pData;
    USBH_StatusTypeDef error = USBH_OK;
    USBH_URBStateTypeDef rxStatus;

    switch (hMidi->state) {
        case MIDI_INIT:
            hMidi->state = MIDI_IDLE;
            break;
        case MIDI_IDLE:
            hMidi->state = MIDI_RX;
            break;
        case MIDI_RX:
            // Always returns USBH_OK, call USBH_LL_GetURBState() for status
            USBH_BulkReceiveData(phost, hMidi->rxBuffer, hMidi->InEpSize, hMidi->InPipe);
            hMidi->state = MIDI_RX_POLL;
            break;
        case MIDI_RX_POLL:
            rxStatus = USBH_LL_GetURBState(phost, hMidi->InPipe);
            if (rxStatus == USBH_URB_NOTREADY || rxStatus == USBH_URB_IDLE) {
                hMidi->state = MIDI_RX_POLL;
            } else if (rxStatus == USBH_URB_DONE) {
                size_t sz = USBH_LL_GetLastXferSize(phost, hMidi->InPipe);
                if (sz > USBH_MIDI_RX_BUF_SIZE) {
                    sz = USBH_MIDI_RX_BUF_SIZE;
                }
                /* Copy out and re-arm before the callback so the pipe is
                   never idle while a packet is processed (the device would
                   otherwise NAK and drop under load). */
                if (sz > 0U) {
                    USBH_memcpy(hMidi->procBuffer, hMidi->rxBuffer, sz);
                }
                USBH_BulkReceiveData(phost, hMidi->rxBuffer, hMidi->InEpSize,
                        hMidi->InPipe);
                hMidi->state = MIDI_RX_POLL;
                if (sz > 0U && hMidi->callback) {
                    hMidi->callback(hMidi->procBuffer, sz, hMidi->pUser);
                }
            } else {
                USBH_ErrLog("MIDI URB unexpected state=%u", (unsigned)rxStatus);
                hMidi->state = MIDI_RX_ERROR;
                error = USBH_FAIL;
            }
            break;
        case MIDI_RX_ERROR:
            error = USBH_ClrFeature(phost, hMidi->InEp);
            if (error == USBH_FAIL) {
                USBH_MIDI_InterfaceDeInit(phost);
                hMidi->state = MIDI_FATAL_ERROR;
            } else {
                hMidi->state = MIDI_IDLE;
            }
            break;
        case MIDI_FATAL_ERROR:
            return USBH_FAIL;
    }
    return error;
}

/**
 * @brief  USBH_MIDI_SOFProcess
 *         The function is for SOF state
 * @param  phost: Host handle
 * @retval USBH Status
 */
static USBH_StatusTypeDef USBH_MIDI_SOFProcess(USBH_HandleTypeDef *phost)
{
    /* Prevent unused argument(s) compilation warning */
    UNUSED(phost);
    return USBH_OK;
}

uint8_t USBH_MIDI_GetAltSetting(USBH_HandleTypeDef *phost)
{
    MIDI_HandleTypeDef *hMidi = (MIDI_HandleTypeDef*)phost->pActiveClass->pData;
    if (hMidi) {
        return hMidi->altSetting;
    }
    return 0U;
}

void USBH_MIDI_SetReceiveCallback(USBH_HandleTypeDef *phost, USBH_MIDI_RxCallback cb, void* pUser)
{
    USBH_UsrLog(__FUNCTION__);
    MIDI_HandleTypeDef *hMidi = (MIDI_HandleTypeDef*)phost->pActiveClass->pData;
    hMidi->callback = cb;
    hMidi->pUser = pUser;
}

MIDI_ErrorTypeDef USBH_MIDI_Transmit(USBH_HandleTypeDef *phost, uint8_t* data, size_t len)
{
    MIDI_HandleTypeDef *hMidi = (MIDI_HandleTypeDef*)phost->pActiveClass->pData;
    while(len)
    {
        size_t cap = (hMidi->OutEpSize < USBH_MIDI_TX_BUF_SIZE)
                         ? hMidi->OutEpSize : USBH_MIDI_TX_BUF_SIZE;
        size_t sz  = (len <= cap) ? len : cap;

        // Stage into the DMA-coherent handle buffer: the caller's buffer
        // may be cached (AXI SRAM) and the OTG DMA does no cache
        // maintenance, so it must read from the DMA section.
        USBH_memcpy(hMidi->txBuffer, data, sz);
        USBH_BulkSendData(phost, hMidi->txBuffer, (uint16_t)sz, hMidi->OutPipe, 1);

        // Drive the OUT URB to completion: poll the URB state and resubmit
        // on NOTREADY (device NAK). Without this the URB is submitted and
        // abandoned, so a NAK is never retried and data never arrives.
        uint32_t start = HAL_GetTick();
        for(;;)
        {
            USBH_URBStateTypeDef st
                = USBH_LL_GetURBState(phost, hMidi->OutPipe);
            if(st == USBH_URB_DONE)
                break;
            if(st == USBH_URB_ERROR || st == USBH_URB_STALL)
            {
                USBH_ClrFeature(phost, hMidi->OutEp);
                return MIDI_ERROR;
            }
            if(st == USBH_URB_NOTREADY)
                USBH_BulkSendData(phost, hMidi->txBuffer, (uint16_t)sz, hMidi->OutPipe, 1);
            if((HAL_GetTick() - start) > 50U)
                return MIDI_BUSY;
            USBH_Delay(1);
        }
        data += sz;
        len -= sz;
    }
    return MIDI_OK;
}

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
