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

    uint8_t interface = USBH_FindInterface(phost, phost->pActiveClass->ClassCode,
            USB_MIDI_STREAMING_SUBCLASS, 0xFFU);

    if ((interface == 0xFFU) || (interface >= USBH_MAX_NUM_INTERFACES)) {
        USBH_DbgLog("Cannot find interface for %s class.", phost->pActiveClass->Name);
        return USBH_FAIL;
    }
    status = USBH_SelectInterface(phost, interface);
    if (status != USBH_OK) {
        return USBH_FAIL;
    }

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
            MIDI_Handle->OutPipe = 0U;    /* Reset the Channel as Free */
        }
        phost->pActiveClass->pData = 0U;
        MIDI_Handle->state = MIDI_INIT;
        MIDI_Handle->error = MIDI_OK;
        MIDI_Handle->txActive = 0U;
        MIDI_Handle->txSize = 0U;
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
                hMidi->state = MIDI_RX;
                if (hMidi->callback) {
                    hMidi->callback(hMidi->rxBuffer, sz, hMidi->pUser);
                }
            } else {
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

void USBH_MIDI_SetReceiveCallback(USBH_HandleTypeDef *phost, USBH_MIDI_RxCallback cb, void* pUser)
{
    USBH_UsrLog(__FUNCTION__);
    MIDI_HandleTypeDef *hMidi = (MIDI_HandleTypeDef*)phost->pActiveClass->pData;
    hMidi->callback = cb;
    hMidi->pUser = pUser;
}

/**
 * @brief  Wait for an in-flight OUT transfer to complete.
 * @param  phost:   Host handle
 * @param  hMidi:   MIDI handle
 * @param  timeout: Timeout in ms
 * @retval MIDI_OK on success, MIDI_BUSY on timeout, MIDI_ERROR on failure
 */
static MIDI_ErrorTypeDef USBH_MIDI_WaitTxDone(USBH_HandleTypeDef *phost,
                                              MIDI_HandleTypeDef *hMidi,
                                              uint32_t timeout)
{
    uint32_t start = HAL_GetTick();
    for (;;)
    {
        USBH_URBStateTypeDef st = USBH_LL_GetURBState(phost, hMidi->OutPipe);
        if (st == USBH_URB_DONE)
        {
            hMidi->txActive = 0U;
            return MIDI_OK;
        }
        if (st == USBH_URB_ERROR || st == USBH_URB_STALL)
        {
            hMidi->txActive = 0U;
            USBH_ClrFeature(phost, hMidi->OutEp);
            return MIDI_ERROR;
        }
        // USBH_URB_IDLE: hardware hasn't finished yet - keep polling
        if ((HAL_GetTick() - start) > timeout)
            return MIDI_BUSY;
        if (st == USBH_URB_NOTREADY)
        {
            // A NAK did not deliver the packet. Retry the same DMA buffer.
            if (USBH_BulkSendData(phost, hMidi->txBuffer, hMidi->txSize,
                                  hMidi->OutPipe, 1) != USBH_OK)
            {
                hMidi->txActive = 0U;
                return MIDI_ERROR;
            }
        }
    }
}

/**
 * @brief  Transmit MIDI data on the OUT bulk pipe. The caller's buffer is
 *         first copied into the handle's txBuffer, which resides in
 *         DMA-accessible memory. If a previous transfer is still in flight,
 *         this function blocks until it completes (up to 50 ms).
 * @param  phost: Host handle
 * @param  data:  Pointer to USB-MIDI Event Packet(s)
 * @param  len:   Length in bytes, split into DMA-buffer-sized chunks
 * @retval MIDI_OK when queued, MIDI_BUSY if no new data was queued before
 *         timeout, MIDI_ERROR on failure or a timeout after partial submission.
 *         The final chunk remains in flight until the next nonempty call.
 */
MIDI_ErrorTypeDef USBH_MIDI_Transmit(USBH_HandleTypeDef *phost, uint8_t* data, size_t len)
{
    if (!phost || !phost->pActiveClass || !phost->pActiveClass->pData)
        return MIDI_ERROR;

    MIDI_HandleTypeDef *hMidi = (MIDI_HandleTypeDef*)phost->pActiveClass->pData;
    if (len == 0U)
        return MIDI_OK;
    if (!data || hMidi->OutEpSize == 0U)
        return MIDI_ERROR;
    uint8_t submitted = 0U;

    /* Send data in OutEpSize size chunks. For typical USB-MIDI traffic
       (4-byte events, OutEpSize = 64) this loop only executes once.*/
    while (len)
    {
        /* Keep ownership of the buffer until this chunk completes, including
           after a timeout. URB_IDLE also means a transfer was just submitted. */
        if (hMidi->txActive)
        {
            MIDI_ErrorTypeDef rc = USBH_MIDI_WaitTxDone(phost, hMidi, 50);
            if (rc != MIDI_OK)
            {
                // A caller may retry BUSY, but must not replay a sent prefix.
                return (submitted && rc == MIDI_BUSY) ? MIDI_ERROR : rc;
            }
        }
        size_t sz = (len <= hMidi->OutEpSize) ? len : hMidi->OutEpSize;
        if (sz > USBH_MIDI_TX_BUF_SIZE)
            sz = USBH_MIDI_TX_BUF_SIZE;
        memcpy(hMidi->txBuffer, data, sz);
        hMidi->txSize = (uint16_t)sz;
        if (USBH_BulkSendData(phost, hMidi->txBuffer, hMidi->txSize,
                              hMidi->OutPipe, 1) != USBH_OK)
            return MIDI_ERROR;
        hMidi->txActive = 1U;
        submitted = 1U;
        data += sz;
        len -= sz;
    }

    return MIDI_OK;
}

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
