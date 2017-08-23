/******************************************************************************
 * @file     usbd_audio.c
 * @brief    NuMicro series HSUSBD driver Sample file
 * @version  1.0.0
 * @date     03, Sep, 2016
 *
 * @note
 * Copyright (C) 2016 Nuvoton Technology Corp. All rights reserved.
 ******************************************************************************/

/*!<Includes */
#include <string.h>
#include "M480.h"
#include "usbd_audio.h"

uint32_t g_usbd_SampleRate = AUDIO_RATE;
uint32_t g_usbd_Class;

/*--------------------------------------------------------------------------*/
static volatile uint8_t bIsI2CIdle = TRUE;

/*--------------------------------------------------------------------------*/
static volatile uint8_t bPlayVolumeLAdjust = FALSE;
static volatile uint8_t bPlayVolumeRAdjust = FALSE;

/*--------------------------------------------------------------------------*/
/* Global variables for Audio class */
uint8_t  g_usbd_PlayMute = 0x00;
uint8_t  g_usbd_RecMute = 0x00;

int16_t  g_usbd_RecVolume = 0;
int16_t  g_usbd_PlayVolume = 0;

uint32_t g_usbd_UsbAudioState = 0;

uint8_t volatile u8PlayEn = 0;
uint8_t volatile u8AudioPlaying=0;
uint8_t volatile u8TxDataCntInBuffer=0;
uint8_t volatile u8PDMATxIdx=0;

uint8_t volatile u8RecEn = 0;
uint8_t volatile u8RxDataCntInBuffer=0;
uint8_t volatile u8PDMARxIdx=0;
uint8_t volatile g_usbd_txflag=0;
uint8_t volatile g_usbd_rxflag=0;

uint32_t volatile u32BuffLen = 0, u32RxBuffLen = 0, u32PacketSize = 0;
uint32_t volatile u32Sample = 0, u32AdjSample = 0;
/* Player Buffer and its pointer */
#ifdef __ICCARM__
#pragma data_alignment=4
uint32_t PcmPlayBuff[PDMA_TXBUFFER_CNT][BUFF_LEN] = {0};
uint32_t PcmPlayBuffLen[PDMA_TXBUFFER_CNT] = {0};

uint32_t PcmRecBuff[PDMA_RXBUFFER_CNT][BUFF_LEN] = {0};
uint8_t u8PcmRxBufFull[PDMA_RXBUFFER_CNT] = {0};

const uint8_t Volx[8] = {
    0x01,0x00,
    0x00,0x81,  // min
    0x00,0x00,  // max
    0x00,0x01   // res
};

const uint8_t Speedx[] = {
    0x02, 0x00,                         //number of sample rate triplets
    0x80, 0xBB, 0x00, 0x00, //48k Max
    0x80, 0xBB, 0x00, 0x00, //48k Max
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x77, 0x01, 0x00, //96k Max
    0x00, 0x77, 0x01, 0x00, //96k Max
    0x00, 0x00, 0x00, 0x00,
};
#else
__align(4) uint32_t PcmPlayBuff[PDMA_TXBUFFER_CNT][BUFF_LEN] = {0};
__align(4) uint32_t PcmPlayBuffLen[PDMA_TXBUFFER_CNT] = {0};

__align(4) uint32_t PcmRecBuff[PDMA_RXBUFFER_CNT][BUFF_LEN] = {0};
__align(4) uint8_t u8PcmRxBufFull[PDMA_RXBUFFER_CNT] = {0};

__align(4) const uint8_t Volx[8] = {
    0x01,0x00,
    0x00,0x81,  // min
    0x00,0x00,  // max
    0x00,0x01   // res
};

__align(4) const uint8_t Speedx[] = {
    0x02, 0x00,                         //number of sample rate triplets
    0x80, 0xBB, 0x00, 0x00, //48k Max
    0x80, 0xBB, 0x00, 0x00, //48k Max
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x77, 0x01, 0x00, //96k Max
    0x00, 0x77, 0x01, 0x00, //96k Max
    0x00, 0x00, 0x00, 0x00,
};
#endif

/* Player Buffer and its pointer */
volatile uint32_t u32BufPlayIdx = 0;
volatile uint32_t u32PlayBufPos=0, u32RecBufPos=0;
volatile uint32_t u32BufRecIdx = 0;

extern DMA_DESC_T DMA_TXDESC[PDMA_TXBUFFER_CNT];

/*--------------------------------------------------------------------------*/
/**
 * @brief       USBD Interrupt Service Routine
 *
 * @param[in]   None
 *
 * @return      None
 *
 * @details     This function is the USBD ISR
 */
void USBD20_IRQHandler(void)
{
    __IO uint32_t IrqStL, IrqSt;

    IrqStL = HSUSBD->GINTSTS & HSUSBD->GINTEN;    /* get interrupt status */

    if (!IrqStL)    return;

    /* USB Bus interrupt */
    if (IrqStL & HSUSBD_GINTSTS_USBIF_Msk) {
        IrqSt = HSUSBD->BUSINTSTS & HSUSBD->BUSINTEN;

        if (IrqSt & HSUSBD_BUSINTSTS_SOFIF_Msk)
            HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_SOFIF_Msk);

        if (IrqSt & HSUSBD_BUSINTSTS_RSTIF_Msk) {
            HSUSBD_SwReset();
            HSUSBD_ResetDMA();

            HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk);
            HSUSBD_SET_ADDR(0);
            HSUSBD_ENABLE_BUS_INT(HSUSBD_BUSINTEN_RSTIEN_Msk|HSUSBD_BUSINTEN_RESUMEIEN_Msk|HSUSBD_BUSINTEN_SUSPENDIEN_Msk);
            HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_RSTIF_Msk);
            HSUSBD_CLR_CEP_INT_FLAG(0x1ffc);
        }

        if (IrqSt & HSUSBD_BUSINTSTS_RESUMEIF_Msk) {
            HSUSBD_ENABLE_BUS_INT(HSUSBD_BUSINTEN_RSTIEN_Msk|HSUSBD_BUSINTEN_SUSPENDIEN_Msk);
            HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_RESUMEIF_Msk);
        }

        if (IrqSt & HSUSBD_BUSINTSTS_SUSPENDIF_Msk) {
            HSUSBD_ENABLE_BUS_INT(HSUSBD_BUSINTEN_RSTIEN_Msk | HSUSBD_BUSINTEN_RESUMEIEN_Msk);
            HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_SUSPENDIF_Msk);
        }

        if (IrqSt & HSUSBD_BUSINTSTS_HISPDIF_Msk) {
            HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk);
            HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_HISPDIF_Msk);
        }

        if (IrqSt & HSUSBD_BUSINTSTS_DMADONEIF_Msk) {
            g_hsusbd_DmaDone = 1;
            HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_DMADONEIF_Msk);

            if (!(HSUSBD->DMACTL & HSUSBD_DMACTL_DMARD_Msk)) {
                //HSUSBD_ENABLE_EP_INT(EPB, HSUSBD_EPINTEN_RXPKIEN_Msk);
            }

            if (HSUSBD->DMACTL & HSUSBD_DMACTL_DMARD_Msk) {
                if (g_hsusbd_ShortPacket == 1) {
                    HSUSBD->EP[EPA].EPRSPCTL = HSUSBD_EP_RSPCTL_SHORTTXEN;    // packet end
                    g_hsusbd_ShortPacket = 0;
                }
            }
        }

        if (IrqSt & HSUSBD_BUSINTSTS_PHYCLKVLDIF_Msk)
            HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_PHYCLKVLDIF_Msk);

        if (IrqSt & HSUSBD_BUSINTSTS_VBUSDETIF_Msk) {
            if (HSUSBD_IS_ATTACHED()) {
                /* USB Plug In */
                HSUSBD_ENABLE_USB();
            } else {
                /* USB Un-plug */
                HSUSBD_DISABLE_USB();
            }
            HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_VBUSDETIF_Msk);
        }
    }

    /* Control endpoint interrupt */
    if (IrqStL & HSUSBD_GINTSTS_CEPIF_Msk) {
        IrqSt = HSUSBD->CEPINTSTS & HSUSBD->CEPINTEN;

        if (IrqSt & HSUSBD_CEPINTSTS_SETUPTKIF_Msk) {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_SETUPTKIF_Msk);
            return;
        }

        if (IrqSt & HSUSBD_CEPINTSTS_SETUPPKIF_Msk) {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_SETUPPKIF_Msk);
            HSUSBD_ProcessSetupPacket();
            return;
        }

        if (IrqSt & HSUSBD_CEPINTSTS_OUTTKIF_Msk) {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_OUTTKIF_Msk);
            HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_STSDONEIEN_Msk);
            return;
        }

        if (IrqSt & HSUSBD_CEPINTSTS_INTKIF_Msk) {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
            if (!(IrqSt & HSUSBD_CEPINTSTS_STSDONEIF_Msk)) {
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_TXPKIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_TXPKIEN_Msk);
                HSUSBD_CtrlIn();
            } else {
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_TXPKIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_TXPKIEN_Msk|HSUSBD_CEPINTEN_STSDONEIEN_Msk);
            }
            return;
        }

        if (IrqSt & HSUSBD_CEPINTSTS_PINGIF_Msk) {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_PINGIF_Msk);
            return;
        }

        if (IrqSt & HSUSBD_CEPINTSTS_TXPKIF_Msk) {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
            HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_NAKCLR);
            if (g_hsusbd_CtrlInSize) {
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
            } else {
                if (g_hsusbd_CtrlZero == 1)
                    HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_ZEROLEN);
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk|HSUSBD_CEPINTEN_STSDONEIEN_Msk);
            }
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_TXPKIF_Msk);
            return;
        }

        if (IrqSt & HSUSBD_CEPINTSTS_RXPKIF_Msk) {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_RXPKIF_Msk);
            HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_NAKCLR);
            HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk|HSUSBD_CEPINTEN_STSDONEIEN_Msk);
            return;
        }

        if (IrqSt & HSUSBD_CEPINTSTS_NAKIF_Msk) {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_NAKIF_Msk);
            return;
        }

        if (IrqSt & HSUSBD_CEPINTSTS_STALLIF_Msk) {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STALLIF_Msk);
            return;
        }

        if (IrqSt & HSUSBD_CEPINTSTS_ERRIF_Msk) {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_ERRIF_Msk);
            return;
        }

        if (IrqSt & HSUSBD_CEPINTSTS_STSDONEIF_Msk) {
            HSUSBD_UpdateDeviceState();
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
            HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk);
            return;
        }

        if (IrqSt & HSUSBD_CEPINTSTS_BUFFULLIF_Msk) {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_BUFFULLIF_Msk);
            return;
        }

        if (IrqSt & HSUSBD_CEPINTSTS_BUFEMPTYIF_Msk) {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_BUFEMPTYIF_Msk);
            return;
        }
    }

    /* Non-control endpoint interrupt */
    if (IrqStL & HSUSBD_GINTSTS_EPAIF_Msk) {
        /* Isochronous in */
        IrqSt = HSUSBD->EP[EPA].EPINTSTS & HSUSBD->EP[EPA].EPINTEN;
        EPA_Handler();
        HSUSBD_CLR_EP_INT_FLAG(EPA, IrqSt);
    }

    if (IrqStL & HSUSBD_GINTSTS_EPBIF_Msk) {
        /* Isochronous out */
        IrqSt = HSUSBD->EP[EPB].EPINTSTS & HSUSBD->EP[EPB].EPINTEN;
        EPB_Handler();
        HSUSBD_CLR_EP_INT_FLAG(EPB, IrqSt);
    }

    if (IrqStL & HSUSBD_GINTSTS_EPCIF_Msk) {
        IrqSt = HSUSBD->EP[EPC].EPINTSTS & HSUSBD->EP[EPC].EPINTEN;
        HSUSBD_CLR_EP_INT_FLAG(EPC, IrqSt);
    }

    if (IrqStL & HSUSBD_GINTSTS_EPDIF_Msk) {
        IrqSt = HSUSBD->EP[EPD].EPINTSTS & HSUSBD->EP[EPD].EPINTEN;
        HSUSBD_CLR_EP_INT_FLAG(EPD, IrqSt);
    }

    if (IrqStL & HSUSBD_GINTSTS_EPEIF_Msk) {
        IrqSt = HSUSBD->EP[EPE].EPINTSTS & HSUSBD->EP[EPE].EPINTEN;
        HSUSBD->EP[EPE].EPDAT = u32AdjSample;
        HSUSBD->EP[EPE].EPTXCNT = 4;
        HSUSBD_CLR_EP_INT_FLAG(EPE, IrqSt);
    }

#if 0
    if (IrqStL & HSUSBD_GINTSTS_EPFIF_Msk) {
        IrqSt = HSUSBD->EP[EPF].EPINTSTS & HSUSBD->EP[EPF].EPINTEN;
        HSUSBD_CLR_EP_INT_FLAG(EPF, IrqSt);
    }

    if (IrqStL & HSUSBD_GINTSTS_EPGIF_Msk) {
        IrqSt = HSUSBD->EP[EPG].EPINTSTS & HSUSBD->EP[EPG].EPINTEN;
        HSUSBD_CLR_EP_INT_FLAG(EPG, IrqSt);
    }

    if (IrqStL & HSUSBD_GINTSTS_EPHIF_Msk) {
        IrqSt = HSUSBD->EP[EPH].EPINTSTS & HSUSBD->EP[EPH].EPINTEN;
        HSUSBD_CLR_EP_INT_FLAG(EPH, IrqSt);
    }

    if (IrqStL & HSUSBD_GINTSTS_EPIIF_Msk) {
        IrqSt = HSUSBD->EP[EPI].EPINTSTS & HSUSBD->EP[EPI].EPINTEN;
        HSUSBD_CLR_EP_INT_FLAG(EPI, IrqSt);
    }

    if (IrqStL & HSUSBD_GINTSTS_EPJIF_Msk) {
        IrqSt = HSUSBD->EP[EPJ].EPINTSTS & HSUSBD->EP[EPJ].EPINTEN;
        HSUSBD_CLR_EP_INT_FLAG(EPJ, IrqSt);
    }

    if (IrqStL & HSUSBD_GINTSTS_EPKIF_Msk) {
        IrqSt = HSUSBD->EP[EPK].EPINTSTS & HSUSBD->EP[EPK].EPINTEN;
        HSUSBD_CLR_EP_INT_FLAG(EPK, IrqSt);
    }

    if (IrqStL & HSUSBD_GINTSTS_EPLIF_Msk) {
        IrqSt = HSUSBD->EP[EPL].EPINTSTS & HSUSBD->EP[EPL].EPINTEN;
        HSUSBD_CLR_EP_INT_FLAG(EPL, IrqSt);
    }
#endif
}

/**
 * @brief       EPA Handler
 *
 * @param[in]   None
 *
 * @return      None
 *
 * @details     This function is used to process EPA event
 */
/* Record */
void EPA_Handler(void)
{
    if (HSUSBD->EP[EPA].EPINTSTS & HSUSBD_EPINTSTS_TXPKIF_Msk) {
        g_usbd_txflag = 1;
    }
}

/**
 * @brief       EPB Handler
 *
 * @param[in]   None
 *
 * @return      None
 *
 * @details     This function is used to process EPB event
 */
/* Play */
void EPB_Handler(void)
{
    volatile uint32_t u32len, tmp;

    if (HSUSBD->EP[EPB].EPINTSTS & HSUSBD_EPINTSTS_RXPKIF_Msk) {
        g_usbd_rxflag = 1;
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief       UAC Class Initial
 *
 * @param[in]   None
 *
 * @return      None
 *
 * @details     This function is used to configure endpoints for UAC class
 */
void UAC_Init(void)
{
    if ((g_usbd_SampleRate % 8000) == 0) {
        u32BuffLen = 768;
        u32RxBuffLen = 768;
    } else {
        u32BuffLen = 441;
        u32RxBuffLen = 444;
    }

    /* Configure USB controller */
    HSUSBD->OPER = 2; /* High Speed */
    /* Enable USB BUS, CEP and EPA , EPB global interrupt */
    HSUSBD_ENABLE_USB_INT(HSUSBD_GINTEN_USBIEN_Msk|HSUSBD_GINTEN_CEPIEN_Msk|HSUSBD_GINTEN_EPAIEN_Msk|HSUSBD_GINTEN_EPBIEN_Msk|HSUSBD_GINTEN_EPEIEN_Msk);
    /* Enable BUS interrupt */
    HSUSBD_ENABLE_BUS_INT(HSUSBD_BUSINTEN_DMADONEIEN_Msk|HSUSBD_BUSINTEN_RESUMEIEN_Msk|HSUSBD_BUSINTEN_RSTIEN_Msk|HSUSBD_BUSINTEN_VBUSDETIEN_Msk);
    /* Reset Address to 0 */
    HSUSBD_SET_ADDR(0);

    /*****************************************************/
    /* Control endpoint */
    HSUSBD_SetEpBufAddr(CEP, CEP_BUF_BASE, CEP_BUF_LEN);
    HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk|HSUSBD_CEPINTEN_STSDONEIEN_Msk);

    /*****************************************************/
    /* EPA ==> ISO IN endpoint, address 1 */
    HSUSBD_SetEpBufAddr(EPA, EPA_BUF_BASE, EPA_BUF_LEN);
    HSUSBD_SET_MAX_PAYLOAD(EPA, EPA_MAX_PKT_SIZE);
    HSUSBD_ConfigEp(EPA, ISO_IN_EP_NUM, HSUSBD_EP_CFG_TYPE_ISO, HSUSBD_EP_CFG_DIR_IN);
//    HSUSBD_ENABLE_EP_INT(EPA, HSUSBD_EPINTEN_TXPKIEN_Msk);

    /* EPB ==> ISO OUT endpoint, address 2 */
    HSUSBD_SetEpBufAddr(EPB, EPB_BUF_BASE, EPB_BUF_LEN);
    HSUSBD_SET_MAX_PAYLOAD(EPB, EPB_MAX_PKT_SIZE);
    HSUSBD_ConfigEp(EPB, ISO_OUT_EP_NUM, HSUSBD_EP_CFG_TYPE_ISO, HSUSBD_EP_CFG_DIR_OUT);
//    HSUSBD_ENABLE_EP_INT(EPB, HSUSBD_EPINTEN_RXPKIEN_Msk|HSUSBD_EPINTEN_SHORTRXIEN_Msk);

    // EPE ==> ISO IN endpoint, address 5
    HSUSBD_SetEpBufAddr(EPE, EPE_BUF_BASE, EPE_BUF_LEN);
    HSUSBD_SET_MAX_PAYLOAD(EPE, EPE_MAX_PKT_SIZE);
    HSUSBD_ConfigEp(EPE, ISO_FEEDBACK_ENDPOINT, HSUSBD_EP_CFG_TYPE_ISO, HSUSBD_EP_CFG_DIR_IN);
    HSUSBD_ENABLE_EP_INT(EPE, HSUSBD_EPINTEN_TXPKIEN_Msk);
}


/**
 * @brief       UAC class request
 *
 * @param[in]   None
 *
 * @return      None
 *
 * @details     This function is used to process UAC class requests
 */
void UAC_ClassRequest(void)
{
    if (gUsbCmd.bmRequestType & 0x80) {
        /* request data transfer direction */
        /* Get Feature Unit Control Request */
        // Device to host
        switch (gUsbCmd.bRequest) {
        /* Get current setting attribute */
        case UAC_CUR: {
            switch ((gUsbCmd.wIndex & 0xff00) >> 8) {
            case CLOCK_SOURCE_ID: {
                if (FREQ_CONTROL == ((gUsbCmd.wValue >> 8) & 0xff))
                    HSUSBD_PrepareCtrlIn((uint8_t *)&g_usbd_SampleRate, gUsbCmd.wLength);

                if (FREQ_VALID == ((gUsbCmd.wValue >> 8) & 0xff))
                    HSUSBD_PrepareCtrlIn((uint8_t *)&g_usbd_Class, gUsbCmd.wLength);

                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
                break;
            }

            case PLAY_FEATURE_UNITID: {
                if (UAC_MD_ENABLE_CONTROL == ((gUsbCmd.wValue >> 8) & 0xff))
                    HSUSBD_PrepareCtrlIn((uint8_t *)&g_usbd_PlayMute, gUsbCmd.wLength);

                if (UAC_MD_BALANCE_CONTROL == ((gUsbCmd.wValue >> 8) & 0xff))
                    HSUSBD_PrepareCtrlIn((uint8_t *)&g_usbd_PlayVolume, gUsbCmd.wLength);

                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
                break;
            }

            case REC_FEATURE_UNITID: {
                if (UAC_MD_ENABLE_CONTROL == ((gUsbCmd.wValue >> 8) & 0xff))
                    HSUSBD_PrepareCtrlIn((uint8_t *)&g_usbd_RecMute, gUsbCmd.wLength);

                if (UAC_MD_BALANCE_CONTROL == ((gUsbCmd.wValue >> 8) & 0xff))
                    HSUSBD_PrepareCtrlIn((uint8_t *)&g_usbd_RecVolume, gUsbCmd.wLength);

                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
                break;
            }

            default:
                /* Setup error, stall the device */
                HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
            }
            break;
        }

        case UAC_RANGE: {
            switch ((gUsbCmd.wValue & 0xff00) >> 8) {
            case FREQ_CONTROL: {
                if (CLOCK_SOURCE_ID == ((gUsbCmd.wIndex >> 8) & 0xff))
                    HSUSBD_PrepareCtrlIn((uint8_t *)Speedx, gUsbCmd.wLength);
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
                break;
            }

            case UAC_MD_BALANCE_CONTROL: {
                if (PLAY_FEATURE_UNITID == ((gUsbCmd.wIndex >> 8) & 0xff))
                    HSUSBD_PrepareCtrlIn((uint8_t *)Volx, gUsbCmd.wLength);

                if (REC_FEATURE_UNITID == ((gUsbCmd.wIndex >> 8) & 0xff))
                    HSUSBD_PrepareCtrlIn((uint8_t *)Volx, gUsbCmd.wLength);

                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
                break;
            }

            default:
                /* STALL control pipe */
                HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
            }
            break;
        }

        default:
            /* Setup error, stall the device */
            HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
        }
    } else {
        // Host to device
        switch (gUsbCmd.bRequest) {
        /* Set Current setting attribute */
        case UAC_CUR: {
            HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_OUTTKIEN_Msk | HSUSBD_CEPINTEN_RXPKIEN_Msk);
            switch ((gUsbCmd.wIndex & 0xff00) >> 8) {
            case 0x28: {
                HSUSBD_CtrlOut((uint8_t *)&g_usbd_Class, gUsbCmd.wLength);

                /* Status stage */
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
                HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_NAKCLR);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_STSDONEIEN_Msk);
                break;
            }

            case CLOCK_SOURCE_ID: {
                if (((gUsbCmd.wValue >> 8) & 0xff) == FREQ_CONTROL)
                    HSUSBD_CtrlOut((uint8_t *)&g_usbd_SampleRate, gUsbCmd.wLength);

                /* Status stage */
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
                HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_NAKCLR);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_STSDONEIEN_Msk);
                break;
            }
            case PLAY_FEATURE_UNITID: {
                if (((gUsbCmd.wValue >> 8) & 0xff) == UAC_MD_BALANCE_CONTROL)
                    HSUSBD_CtrlOut((uint8_t *)&g_usbd_PlayVolume, gUsbCmd.wLength);

                if (((gUsbCmd.wValue >> 8) & 0xff) == UAC_MD_ENABLE_CONTROL)
                    HSUSBD_CtrlOut((uint8_t *)&g_usbd_PlayMute, gUsbCmd.wLength);

                /* Status stage */
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
                HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_NAKCLR);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_STSDONEIEN_Msk);
                break;
            }
            case REC_FEATURE_UNITID: {
                if (((gUsbCmd.wValue >> 8) & 0xff) == UAC_MD_BALANCE_CONTROL)
                    HSUSBD_CtrlOut((uint8_t *)&g_usbd_RecVolume, gUsbCmd.wLength);

                if (((gUsbCmd.wValue >> 8) & 0xff) == UAC_MD_ENABLE_CONTROL)
                    HSUSBD_CtrlOut((uint8_t *)&g_usbd_RecMute, gUsbCmd.wLength);

                /* Status stage */
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
                HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_NAKCLR);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_STSDONEIEN_Msk);
                break;
            }

            default:
                /* STALL control pipe */
                HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
                break;
            }
            break;
        }

        default: {
            /* Setup error, stall the device */
            HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
            break;
        }
        }
    }
}

/**
 * @brief       Set Interface standard request
 *
 * @param[in]   u32AltInterface Interface
 *
 * @return      None
 *
 * @details     This function is used to set UAC Class relative setting
 */
void UAC_SetInterface(uint32_t u32AltInterface)
{
    if ((gUsbCmd.wIndex & 0xff) == 2) {
        /* Audio Iso OUT interface */
        if (u32AltInterface == 0)
            UAC_DeviceDisable(1);    /* stop play */
        else
            UAC_DeviceEnable(1);     /* start play */
    }

    if ((gUsbCmd.wIndex & 0xff) == 1) {
        /* Audio Iso IN interface */
        if (u32AltInterface == 1) { /* Start Record */
            g_usbd_UsbAudioState = UAC_START_AUDIO_RECORD;
            HSUSBD->EP[EPA].EPRSPCTL = HSUSBD_EPRSPCTL_ZEROLEN_Msk;
            UAC_DeviceEnable(0);
            g_usbd_txflag = 1;
        } else if (u32AltInterface == 0) { /* Stop Record */
            UAC_DeviceDisable(0);
            HSUSBD->EP[EPA].EPRSPCTL = HSUSBD_EPRSPCTL_ZEROLEN_Msk;
            g_usbd_UsbAudioState = UAC_STOP_AUDIO_RECORD;
        }
    }
}

/*******************************************************************/
/* For I2C transfer */
__IO uint32_t EndFlag0 = 0;
uint8_t Device_Addr0 = 0x1A;                /* WAU8822 Device ID */

typedef enum {
    E_RS_NONE,          // no re-sampling
    E_RS_UP,            // up sampling
    E_RS_DOWN           // down sampling
} RESAMPLE_STATE_T;

static void Delay(uint32_t t)
{
    volatile int32_t delay;

    delay = t;

    while(delay-- >= 0);
}

void RecoveryFromArbLost(void)
{
    I2C2->CTL0 &= ~I2C_CTL0_I2CEN_Msk;
    I2C2->CTL0 |= I2C_CTL0_I2CEN_Msk;
}
/*---------------------------------------------------------------------------------------------------------*/
/*  Write 9-bit data to 7-bit address register of WAU8822 with I2C2                                        */
/*---------------------------------------------------------------------------------------------------------*/
void I2C_WriteNAU8822(uint8_t u8addr, uint16_t u16data)
{
    bIsI2CIdle = FALSE;
restart:
    I2C_START(I2C2);
    I2C_WAIT_READY(I2C2);

    I2C_SET_DATA(I2C2, 0x1A << 1);
    I2C_SET_CONTROL_REG(I2C2, I2C_CTL_SI);
    I2C_WAIT_READY(I2C2);
    if(I2C_GET_STATUS(I2C2) == 0x38) {
        RecoveryFromArbLost();
        goto restart;
    } else if(I2C_GET_STATUS(I2C2) != 0x18)
        goto stop;

    I2C_SET_DATA(I2C2, (uint8_t)((u8addr << 1) | (u16data >> 8)));
    I2C_SET_CONTROL_REG(I2C2, I2C_CTL_SI);
    I2C_WAIT_READY(I2C2);
    if(I2C_GET_STATUS(I2C2) == 0x38) {
        RecoveryFromArbLost();
        goto restart;
    } else if(I2C_GET_STATUS(I2C2) != 0x28)
        goto stop;

    I2C_SET_DATA(I2C2, (uint8_t)(u16data & 0x00FF));
    I2C_SET_CONTROL_REG(I2C2, I2C_CTL_SI);
    I2C_WAIT_READY(I2C2);
    if(I2C_GET_STATUS(I2C2) == 0x38) {
        RecoveryFromArbLost();
        goto restart;
    } else if(I2C_GET_STATUS(I2C2) != 0x28)
        goto stop;

stop:
    I2C_STOP(I2C2);
    while(I2C2->CTL0 & I2C_CTL0_STO_Msk);

    bIsI2CIdle = TRUE;
    EndFlag0 = 1;
}

static void ATOM_I2C_WriteNAU8822(uint8_t u8addr, uint16_t u16data)
{
    if(!bIsI2CIdle)
        while (EndFlag0 == 0);

    I2C_WriteNAU8822(u8addr, u16data);
}

uint32_t u32OldSampleRate=0;

/* config play sampling rate */
void WAU8822_ConfigSampleRate(uint32_t u32SampleRate)
{
    if(u32SampleRate == u32OldSampleRate)
        return;

    u32OldSampleRate = u32SampleRate;

    printf("[NAU8822] Configure Sampling Rate to %d\n", u32SampleRate);

    if((u32SampleRate % 8) == 0) {
        I2C_WriteNAU8822(36, 0x008);    //12.288Mhz
        I2C_WriteNAU8822(37, 0x00C);
        I2C_WriteNAU8822(38, 0x093);
        I2C_WriteNAU8822(39, 0x0E9);
    } else {
        I2C_WriteNAU8822(36, 0x007);    //11.2896Mhz
        I2C_WriteNAU8822(37, 0x021);
        I2C_WriteNAU8822(38, 0x161);
        I2C_WriteNAU8822(39, 0x026);
    }

    switch (u32SampleRate) {
    case 44100:
        I2C_WriteNAU8822(6, 0x14D);    /* Divide by 2, 48K */
        I2C_WriteNAU8822(7, 0x000);    /* 48K for internal filter coefficients */
        u32BuffLen = 441;
        u32RxBuffLen = 444;
        break;

    case 48000:
        I2C_WriteNAU8822(6, 0x14D);    /* Divide by 2, 48K */
        I2C_WriteNAU8822(7, 0x000);    /* 48K for internal filter coefficients */
        HSUSBD_SET_MAX_PAYLOAD(EPA, 24);
        u32BuffLen = 768;
        u32RxBuffLen = 768;
        u32Sample = u32AdjSample = 0x00060000;
        u32PacketSize = 6;
        break;

    case 96000:
        I2C_WriteNAU8822(6, 0x109);
        I2C_WriteNAU8822(72, 0x013);
        HSUSBD_SET_MAX_PAYLOAD(EPA, 48);
        u32BuffLen = 768;
        u32RxBuffLen = 768;
        u32Sample = u32AdjSample = 0x000c0000;
        u32PacketSize = 12;
        break;

    case 192000:
        I2C_WriteNAU8822(6, 0x109);
        I2C_WriteNAU8822(72, 0x017);
        HSUSBD_SET_MAX_PAYLOAD(EPA, 96);
        u32BuffLen = 768;
        u32RxBuffLen = 768;
        break;
    }
}

void NAU8822_Setup()
{
    I2C_WriteNAU8822(0,  0x000);   /* Reset all registers */
    Delay(0x200);

    I2C_WriteNAU8822(1,  0x02F);
    I2C_WriteNAU8822(2,  0x1B3);   /* Enable L/R Headphone, ADC Mix/Boost, ADC */
    I2C_WriteNAU8822(3,  0x07F);   /* Enable L/R main mixer, DAC */
    I2C_WriteNAU8822(4,  0x010);   /* 16-bit word length, I2S format, Stereo */
    I2C_WriteNAU8822(5,  0x000);   /* Companding control and loop back mode (all disable) */
    I2C_WriteNAU8822(10, 0x008);   /* DAC soft mute is disabled, DAC oversampling rate is 128x */
    I2C_WriteNAU8822(14, 0x108);   /* ADC HP filter is disabled, ADC oversampling rate is 128x */
    I2C_WriteNAU8822(15, 0x1FF);   /* ADC left digital volume control */
    I2C_WriteNAU8822(16, 0x1FF);   /* ADC right digital volume control */
    I2C_WriteNAU8822(44, 0x000);   /* LLIN/RLIN is not connected to PGA */
    I2C_WriteNAU8822(47, 0x060);   /* LLIN connected, and its Gain value */
    I2C_WriteNAU8822(48, 0x060);   /* RLIN connected, and its Gain value */
    I2C_WriteNAU8822(50, 0x001);   /* Left DAC connected to LMIX */
    I2C_WriteNAU8822(51, 0x001);   /* Right DAC connected to RMIX */
}

/**
  * @brief  UAC_DeviceEnable. To enable the device to play or record audio data.
  * @param  None.
  * @retval None.
  */
void UAC_DeviceEnable(uint32_t bIsPlay)
{
    if (bIsPlay) {
        /* Enable play hardware */
        u8PlayEn = 1;
        TIMER_Start(TIMER0);
    } else {
        /* Enable record hardware */
        if(!u8RecEn)
            AudioStartRecord(g_usbd_SampleRate);

        u8RecEn = 1;
    }
}


/**
  * @brief  UAC_DeviceDisable. To disable the device to play or record audio data.
  * @param  None.
  * @retval None.
  */
void UAC_DeviceDisable(uint32_t bIsPlay)
{
    if (bIsPlay) {
        /* Disable play hardware/stop play */
        u8PlayEn = 0;
        /* Disable I2S Tx function */
        I2S_DISABLE_TXDMA(I2S0);
        I2S_DISABLE_TX(I2S0);

        /* Disable PDMA channel */
        PDMA->PAUSE |= (1 << PDMA_I2S_TX_CH);

        printf("Stop Play ...\n");

        /* Reset some variables */
        u32BufPlayIdx = 0;
        u32PlayBufPos = 0;
        u8PDMATxIdx = 0;
        u8AudioPlaying = 0;
        u8TxDataCntInBuffer = 0;

        /* flush PCM buffer */
        memset(PcmPlayBuff, 0, sizeof(PcmPlayBuff));

        /* stop usbd dma and flush FIFO */
        HSUSBD_ResetDMA();
        g_hsusbd_DmaDone = 1;
        HSUSBD->EP[EPB].EPRSPCTL |= HSUSBD_EPRSPCTL_FLUSH_Msk;
        TIMER0->CNT = 0x7657;
    } else {
        /* Disable record hardware/stop record */
        u8RecEn = 0;

        /* Disable I2S Rx function */
        I2S_DISABLE_RXDMA(I2S0);
        I2S_DISABLE_RX(I2S0);

        /* Disable PDMA channel */
        PDMA->PAUSE |= (1 << PDMA_I2S_RX_CH);
        printf("Stop Record .. \n");

        /* Reset some variables */
        u32RecBufPos = 0;
        u32BufRecIdx = 0;
        u8PDMARxIdx = 0;

        /* flush PCM buffer */
        memset(u8PcmRxBufFull, 0, sizeof(u8PcmRxBufFull));
        HSUSBD->EP[EPA].EPRSPCTL |= HSUSBD_EPRSPCTL_FLUSH_Msk;
    }
}

/**
  * @brief  GetPlayData, To get data from ISO OUT to play buffer.
  * @param  None.
  * @retval None.
  */
void UAC_GetPlayData(void)
{
    volatile uint32_t u32len, tmp;

    /* if buffer has enough data, play it!! */
    if (!u8AudioPlaying && (u8TxDataCntInBuffer >= (PDMA_TXBUFFER_CNT/2+1))) {
        AudioStartPlay(g_usbd_SampleRate);
        u8AudioPlaying = 1;
    }

    if (HSUSBD->DMACTL & HSUSBD_DMACTL_DMAEN_Msk)
        return;

    // get data from FIFO
    u32len = HSUSBD->EP[EPB].EPDATCNT & 0xffff;
    if (u32len == 0)
        return;

    tmp = u32len >> 2;  /* byte to word */

    /* Ring buffer check */
    PDMA_DisableInt(PDMA_I2S_TX_CH, 0);
    if ((u32PlayBufPos + tmp) > u32BuffLen) {
        PcmPlayBuffLen[u32BufPlayIdx] = u32PlayBufPos;
        DMA_TXDESC[u32BufPlayIdx].ctl = (DMA_TXDESC[u32BufPlayIdx].ctl & ~PDMA_DSCT_CTL_TXCNT_Msk )|((u32PlayBufPos-1)<<PDMA_DSCT_CTL_TXCNT_Pos);
        u32PlayBufPos = 0;
        u32BufPlayIdx ++;

        /* change buffer index */
        if(u32BufPlayIdx >= PDMA_TXBUFFER_CNT)
            u32BufPlayIdx=0;
        /* increase data count in buffer */
        u8TxDataCntInBuffer ++;
    }
    PDMA_EnableInt(PDMA_I2S_TX_CH, 0);

    /* active usbd DMA to read data from FIFO and then send to I2S */
    HSUSBD_SET_DMA_WRITE(ISO_OUT_EP_NUM);
    HSUSBD_ENABLE_BUS_INT(HSUSBD_BUSINTEN_DMADONEIEN_Msk|HSUSBD_BUSINTEN_SUSPENDIEN_Msk|HSUSBD_BUSINTEN_RSTIEN_Msk|HSUSBD_BUSINTEN_VBUSDETIEN_Msk);
    HSUSBD_SET_DMA_ADDR((uint32_t)&PcmPlayBuff[u32BufPlayIdx][u32PlayBufPos]);
    HSUSBD_SET_DMA_LEN(u32len);
    g_hsusbd_DmaDone = 0;
    HSUSBD_ENABLE_DMA();

    /* wait usbd dma complete */
    while(1) {
        if (g_hsusbd_DmaDone)
            break;

        if (!HSUSBD_IS_ATTACHED())
            break;

        if ((HSUSBD->DMACTL & HSUSBD_DMACTL_DMAEN_Msk) == 0)
            break;
    }

    u32PlayBufPos += tmp;
    g_usbd_rxflag = 0;
}

void AudioStartPlay(uint32_t u32SampleRate)
{
    UAC_DeviceEnable(1);

    /* Configure TX PDMA SG table */
    PDMA_WriteTxSGTable();

    /* Configure WAU8822 to specific sample rate */
    WAU8822_ConfigSampleRate(u32SampleRate);

    /* Enable I2S Tx function */
    I2S_ENABLE_TXDMA(I2S0);
    I2S_ENABLE_TX(I2S0);

    /* Enable PDMA channel */
    PDMA->CHCTL |= (1 << PDMA_I2S_TX_CH);
    printf("Start Play ... \n");

    // workaround for PDMA suspend
    PDMA->DSCT[PDMA_I2S_TX_CH].CTL = 0;
    PDMA->DSCT[PDMA_I2S_TX_CH].CTL = 2;
}

/**
  * @brief  SendRecData, prepare the record data for next ISO transfer.
  * @param  None.
  * @retval None.
  */
void UAC_SendRecData(void)
{
    uint32_t volatile i;

    PDMA_DisableInt(PDMA_I2S_RX_CH, 0);
    /* when record buffer full, send data to host */
    if(u8PcmRxBufFull[u32BufRecIdx]) {
        if ((u32RecBufPos + u32PacketSize) > u32RxBuffLen) {
            /* Set empty flag */
            u8PcmRxBufFull[u32BufRecIdx] = 0;
            u32RecBufPos = 0;
            /* Change to next PCM buffer */
            u32BufRecIdx ++;
            if(u32BufRecIdx >= PDMA_RXBUFFER_CNT)
                u32BufRecIdx=0;
        }

        for (i=0; i<u32PacketSize; i++) {
            HSUSBD->EP[EPA].EPDAT = PcmRecBuff[u32BufRecIdx][u32RecBufPos++];
        }
        HSUSBD->EP[EPA].EPTXCNT = (u32PacketSize << 2);
        g_usbd_txflag = 0;
    } else {
        /* send zero packet when no data*/
        HSUSBD->EP[EPA].EPRSPCTL = HSUSBD_EPRSPCTL_ZEROLEN_Msk;
    }
    PDMA_EnableInt(PDMA_I2S_RX_CH, 0);
}

void AudioStartRecord(uint32_t u32SampleRate)
{
    /* Configure RX PDMA SG table */
    PDMA_WriteRxSGTable();

    /* Configure WAU8822 to specific sample rate */
    WAU8822_ConfigSampleRate(u32SampleRate);

    /* Enable I2S Tx function */
    I2S_ENABLE_RXDMA(I2S0);
    I2S_ENABLE_RX(I2S0);

    /* Enable PDMA channel */
    PDMA->CHCTL |= (1 << PDMA_I2S_RX_CH);
    printf("Start Record ... \n");

    PDMA->DSCT[PDMA_I2S_RX_CH].CTL = 0;
    PDMA->DSCT[PDMA_I2S_RX_CH].CTL = 2;
}

/* adjust codec PLL */
void AdjustCodecPll(RESAMPLE_STATE_T r)
{
    static uint16_t tb0[3][3] = {{0x00C, 0x093, 0x0E9}, // 8.192
        {0x00E, 0x1D2, 0x1E3},  // * 1.005 = 8.233
        {0x009, 0x153, 0x1EF}
    }; // * .995 = 8.151
    static uint16_t tb1[3][3] = {{0x021, 0x131, 0x026}, // 7.526
        {0x024, 0x010, 0x0C5},  // * 1.005 = 7.563
        {0x01F, 0x076, 0x191}
    }; // * .995 = 7.488
    static RESAMPLE_STATE_T current = E_RS_NONE;
    int i, s;

    if(r == current)
        return;
    else
        current = r;
    switch(r) {
    case E_RS_UP:
        s = 1;
        break;
    case E_RS_DOWN:
        s = 2;
        break;
    case E_RS_NONE:
    default:
        s = 0;
    }

    if((g_usbd_SampleRate % 8) == 0) {
        for(i=0; i<3; i++)
            ATOM_I2C_WriteNAU8822(37+i, tb0[s][i]);
    } else {
        for(i=0; i<3; i++)
            ATOM_I2C_WriteNAU8822(37+i, tb1[s][i]);
    }
}

//======================================================
void TMR0_IRQHandler(void)
{
    TIMER_ClearIntFlag(TIMER0);

    if (u8AudioPlaying) {
        if((u8TxDataCntInBuffer >= (PDMA_TXBUFFER_CNT/2)) && (u8TxDataCntInBuffer <= (PDMA_TXBUFFER_CNT/2+1)))
            AdjustCodecPll(E_RS_NONE);
        else if(u8TxDataCntInBuffer >= (PDMA_TXBUFFER_CNT-2))
            AdjustCodecPll(E_RS_UP);
        else
            AdjustCodecPll(E_RS_DOWN);
    }
}



