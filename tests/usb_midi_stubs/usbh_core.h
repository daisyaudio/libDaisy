#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Only the host API used by the MIDI class is replaced. The tests compile
   the production MIDI class and its real public handle definition. */
typedef enum { USBH_OK, USBH_BUSY, USBH_FAIL } USBH_StatusTypeDef;
typedef enum {
    USBH_URB_IDLE, USBH_URB_DONE, USBH_URB_NOTREADY, USBH_URB_NYET,
    USBH_URB_ERROR, USBH_URB_STALL
} USBH_URBStateTypeDef;

#define USBH_MAX_NUM_INTERFACES 1
#define USB_EP_TYPE_BULK 2
#define HOST_USER_CLASS_ACTIVE 1
#define USBH_UsrLog(...) ((void)0)
#define USBH_DbgLog(...) ((void)0)
#define USBH_memset memset

struct _USBH_HandleTypeDef;
typedef struct {
    const char *Name;
    uint8_t ClassCode;
    USBH_StatusTypeDef (*Init)(struct _USBH_HandleTypeDef *);
    USBH_StatusTypeDef (*DeInit)(struct _USBH_HandleTypeDef *);
    USBH_StatusTypeDef (*Requests)(struct _USBH_HandleTypeDef *);
    USBH_StatusTypeDef (*BgndProcess)(struct _USBH_HandleTypeDef *);
    USBH_StatusTypeDef (*SOFProcess)(struct _USBH_HandleTypeDef *);
    void *pData;
} USBH_ClassTypeDef;

typedef struct _USBH_HandleTypeDef {
    USBH_ClassTypeDef *pActiveClass;
    struct {
        uint8_t address, speed;
        struct {
            struct {
                uint8_t bNumEndpoints;
                struct {
                    uint8_t bEndpointAddress;
                    uint16_t wMaxPacketSize;
                } Ep_Desc[2];
            } Itf_Desc[USBH_MAX_NUM_INTERFACES];
        } CfgDesc;
    } device;
    void (*pUser)(struct _USBH_HandleTypeDef *, uint8_t);
} USBH_HandleTypeDef;

uint32_t HAL_GetTick(void);
uint8_t USBH_FindInterface(USBH_HandleTypeDef *, uint8_t, uint8_t, uint8_t);
USBH_StatusTypeDef USBH_SelectInterface(USBH_HandleTypeDef *, uint8_t);
uint8_t USBH_AllocPipe(USBH_HandleTypeDef *, uint8_t);
USBH_StatusTypeDef USBH_OpenPipe(USBH_HandleTypeDef *, uint8_t, uint8_t,
                                uint8_t, uint8_t, uint8_t, uint16_t);
USBH_StatusTypeDef USBH_ClosePipe(USBH_HandleTypeDef *, uint8_t);
USBH_StatusTypeDef USBH_FreePipe(USBH_HandleTypeDef *, uint8_t);
USBH_StatusTypeDef USBH_LL_SetToggle(USBH_HandleTypeDef *, uint8_t, uint8_t);
USBH_URBStateTypeDef USBH_LL_GetURBState(USBH_HandleTypeDef *, uint8_t);
uint32_t USBH_LL_GetLastXferSize(USBH_HandleTypeDef *, uint8_t);
USBH_StatusTypeDef USBH_ClrFeature(USBH_HandleTypeDef *, uint8_t);
USBH_StatusTypeDef USBH_BulkSendData(USBH_HandleTypeDef *, uint8_t *, uint16_t,
                                    uint8_t, uint8_t);
USBH_StatusTypeDef USBH_BulkReceiveData(USBH_HandleTypeDef *, uint8_t *,
                                      uint16_t, uint8_t);
