/**************************************************************************//**
 * @file     main.c
 * @version  V1.00
 * $Revision: 6 $
 * $Date: 14/08/04 9:58a $
 * @brief    MP3 player sample plays MP3 files stored on SD memory card
 *
 * @note
 * Copyright (C) 2016 Nuvoton Technology Corp. All rights reserved.
 *
 ******************************************************************************/
#include <stdio.h>
#include <string.h>
#include "M480.h"
#include "config.h"
#include "diskio.h"
#include "ff.h"

uint32_t volatile u32BuffPos = 0;
FATFS FatFs[_VOLUMES];               /* File system object for logical drive */

#ifdef __ICCARM__
#pragma data_alignment=32
BYTE Buff[16] ;                   /* Working buffer */
DMA_DESC_T DMA_DESC[2];
#endif

#ifdef __ARMCC_VERSION
__align(32) BYTE Buff[16] ;       /* Working buffer */
__align(32) DMA_DESC_T DMA_DESC[2];
#endif

uint8_t bAudioPlaying = 0;
extern signed int aPCMBuffer[2][PCM_BUFFER_SIZE];

/*---------------------------------------------------------*/
/* User Provided RTC Function for FatFs module             */
/*---------------------------------------------------------*/
/* This is a real time clock service to be called from     */
/* FatFs module. Any valid time must be returned even if   */
/* the system does not support an RTC.                     */
/* This function is not required in read-only cfg.         */

unsigned long get_fattime (void)
{
    unsigned long tmr;

    tmr=0x00000;

    return tmr;
}


void SDH0_IRQHandler(void)
{
    unsigned int volatile isr;
    unsigned int volatile ier;

    // FMI data abort interrupt
    if (SDH0->GINTSTS & SDH_GINTSTS_DTAIF_Msk) {
        /* ResetAllEngine() */
        SDH0->GCTL |= SDH_GCTL_GCTLRST_Msk;
    }

    //----- SD interrupt status
    isr = SDH0->INTSTS;
    if (isr & SDH_INTSTS_BLKDIF_Msk) {
        // block down
        extern uint8_t volatile _SDH_SDDataReady;
        _SDH_SDDataReady = TRUE;
        SDH0->INTSTS = SDH_INTSTS_BLKDIF_Msk;
    }

    if (isr & SDH_INTSTS_CDIF_Msk) { // port 0 card detect
        //----- SD interrupt status
        // it is work to delay 50 times for SD_CLK = 200KHz
        {
            int volatile i;         // delay 30 fail, 50 OK
            for (i=0; i<0x500; i++);  // delay to make sure got updated value from REG_SDISR.
            isr = SDH0->INTSTS;
        }

        if (isr & SDH_INTSTS_CDSTS_Msk) {
            printf("\n***** card remove !\n");
            SD0.IsCardInsert = FALSE;   // SDISR_CD_Card = 1 means card remove for GPIO mode
            memset(&SD0, 0, sizeof(SDH_INFO_T));
        } else {
            printf("***** card insert !\n");
            SDH_Open(SDH0, CardDetect_From_GPIO);
            SDH_Probe(SDH0);
        }

        SDH0->INTSTS = SDH_INTSTS_CDIF_Msk;
    }

    // CRC error interrupt
    if (isr & SDH_INTSTS_CRCIF_Msk) {
        if (!(isr & SDH_INTSTS_CRC16_Msk)) {
            //printf("***** ISR sdioIntHandler(): CRC_16 error !\n");
            // handle CRC error
        } else if (!(isr & SDH_INTSTS_CRC7_Msk)) {
            extern uint32_t _SDH_uR3_CMD;
            if (!_SDH_uR3_CMD) {
                //printf("***** ISR sdioIntHandler(): CRC_7 error !\n");
                // handle CRC error
            }
        }
        SDH0->INTSTS = SDH_INTSTS_CRCIF_Msk;      // clear interrupt flag
    }

    if (isr & SDH_INTSTS_DITOIF_Msk) {
        printf("***** ISR: data in timeout !\n");
        SDH0->INTSTS |= SDH_INTSTS_DITOIF_Msk;
    }

    // Response in timeout interrupt
    if (isr & SDH_INTSTS_RTOIF_Msk) {
        printf("***** ISR: response in timeout !\n");
        SDH0->INTSTS |= SDH_INTSTS_RTOIF_Msk;
    }
}

void SD_Inits(void)
{
    /* select multi-function pin */
    SYS->GPE_MFPL &= ~(SYS_GPE_MFPL_PE6MFP_Msk|SYS_GPE_MFPL_PE5MFP_Msk|SYS_GPE_MFPL_PE4MFP_Msk|SYS_GPE_MFPL_PE3MFP_Msk|
                       SYS_GPE_MFPL_PE2MFP_Msk|SYS_GPE_MFPL_PE7MFP_Msk);
    SYS->GPD_MFPH &= ~SYS_GPD_MFPH_PD13MFP_Msk;
    SYS->GPE_MFPL |= (SYS_GPE_MFPL_PE2MFP_SD0_DAT0|SYS_GPE_MFPL_PE3MFP_SD0_DAT1|SYS_GPE_MFPL_PE4MFP_SD0_DAT2|SYS_GPE_MFPL_PE5MFP_SD0_DAT3|
                      SYS_GPE_MFPL_PE6MFP_SD0_CLK|SYS_GPE_MFPL_PE7MFP_SD0_CMD);
    SYS->GPD_MFPH |= SYS_GPD_MFPH_PD13MFP_SD0_nCD;

    /* Select IP clock source */
    CLK_SetModuleClock(SDH0_MODULE, CLK_CLKSEL0_SDH0SEL_PLL, CLK_CLKDIV0_SDH0(10));
    /* Enable IP clock */
    CLK_EnableModuleClock(SDH0_MODULE);
}

void SYS_Init(void)
{
    /* Unlock protected registers */
    SYS_UnlockReg();

    /* Enable External XTAL (4~24 MHz) */
    CLK_EnableXtalRC(CLK_PWRCTL_HXTEN_Msk);

    /* Waiting for 12MHz clock ready */
    CLK_WaitClockReady(CLK_STATUS_HXTSTB_Msk);

    /* Set core clock as PLL_CLOCK from PLL */
    CLK_SetCoreClock(FREQ_192MHZ);

    /* Set both PCLK0 and PCLK1 as HCLK/2 */
    CLK->PCLKDIV = CLK_PCLKDIV_PCLK0DIV2 | CLK_PCLKDIV_PCLK1DIV2;

    /* Enable UART module clock */
    CLK_EnableModuleClock(UART0_MODULE);

    /* Enable I2S0 module clock */
    CLK_EnableModuleClock(I2S0_MODULE);

    /* Enable I2C2 module clock */
    CLK_EnableModuleClock(I2C2_MODULE);

    /* Enable PDMA module clock */
    CLK_EnableModuleClock(PDMA_MODULE);

    /* Select UART module clock source */
    CLK_SetModuleClock(UART0_MODULE, CLK_CLKSEL1_UART0SEL_HXT, CLK_CLKDIV0_UART0(1));

    /*---------------------------------------------------------------------------------------------------------*/
    /* Init I/O Multi-function                                                                                 */
    /*---------------------------------------------------------------------------------------------------------*/

    /* Init UART0 multi-function pins */
    SYS->GPD_MFPL &= ~(SYS_GPD_MFPL_PD2MFP_Msk | SYS_GPD_MFPL_PD3MFP_Msk);
    SYS->GPD_MFPL |= (SYS_GPD_MFPL_PD2MFP_UART0_RXD | SYS_GPD_MFPL_PD3MFP_UART0_TXD);

    SYS->GPF_MFPL = (SYS->GPF_MFPL & ~(SYS_GPF_MFPL_PF6MFP_Msk|SYS_GPF_MFPL_PF7MFP_Msk)) |
                    (SYS_GPF_MFPL_PF6MFP_I2S0_LRCK|SYS_GPF_MFPL_PF7MFP_I2S0_DO);
    SYS->GPF_MFPH = (SYS->GPF_MFPH & ~(SYS_GPF_MFPH_PF8MFP_Msk|SYS_GPF_MFPH_PF9MFP_Msk|SYS_GPF_MFPH_PF10MFP_Msk)) |
                    (SYS_GPF_MFPH_PF8MFP_I2S0_DI|SYS_GPF_MFPH_PF9MFP_I2S0_MCLK|SYS_GPF_MFPH_PF10MFP_I2S0_BCLK );

    SYS->GPD_MFPH &= ~(SYS_GPD_MFPH_PD8MFP_Msk | SYS_GPD_MFPH_PD9MFP_Msk);
    SYS->GPD_MFPH |= (SYS_GPD_MFPH_PD8MFP_I2C2_SDA | SYS_GPD_MFPH_PD9MFP_I2C2_SCL);

    PF->SMTEN |= GPIO_SMTEN_SMTEN10_Msk;
    PD->SMTEN |= GPIO_SMTEN_SMTEN9_Msk;
}

void I2C2_Init(void)
{
    /* Open I2C2 and set clock to 100k */
    I2C_Open(I2C2, 100000);
}

// Configure PDMA to Scatter Gather mode */
void PDMA_Init(void)
{
    DMA_DESC[0].ctl = ((PCM_BUFFER_SIZE-1)<<PDMA_DSCT_CTL_TXCNT_Pos)|PDMA_WIDTH_32|PDMA_SAR_INC|PDMA_DAR_FIX|PDMA_REQ_SINGLE|PDMA_OP_SCATTER;
    DMA_DESC[0].src = (uint32_t)&aPCMBuffer[0][0];
    DMA_DESC[0].dest = (uint32_t)&I2S0->TXFIFO;
    DMA_DESC[0].offset = (uint32_t)&DMA_DESC[1] - (PDMA->SCATBA);

    DMA_DESC[1].ctl = ((PCM_BUFFER_SIZE-1)<<PDMA_DSCT_CTL_TXCNT_Pos)|PDMA_WIDTH_32|PDMA_SAR_INC|PDMA_DAR_FIX|PDMA_REQ_SINGLE|PDMA_OP_SCATTER;
    DMA_DESC[1].src = (uint32_t)&aPCMBuffer[1][0];
    DMA_DESC[1].dest = (uint32_t)&I2S0->TXFIFO;
    DMA_DESC[1].offset = (uint32_t)&DMA_DESC[0] - (PDMA->SCATBA);

    PDMA_Open(1 << 2);
    PDMA_SetTransferMode(2, PDMA_I2S0_TX, 1, (uint32_t)&DMA_DESC[0]);

    PDMA_EnableInt(2, 0);
    NVIC_EnableIRQ(PDMA_IRQn);
}

/*---------------------------------------------------------------------------------------------------------*/
/*  Main Function                                                                                          */
/*---------------------------------------------------------------------------------------------------------*/
int32_t main (void)
{
    TCHAR       sd_path[] = { '0', ':', 0 };    /* SD drive started from 0 */

    /* Init System, IP clock and multi-function I/O */
    SYS_Init();

    /* Init SD */
    SD_Inits();

    /* Init UART to 115200-8n1 for print message */
    UART_Open(UART0, 115200);

    printf("+------------------------------------------------------------------------+\n");
    printf("|                   MP3 Player Sample with NAU88L25 Codec                |\n");
    printf("+------------------------------------------------------------------------+\n");
    printf(" Please put MP3 files on SD card \n");

    SDH_Open_Disk(SDH0, CardDetect_From_GPIO);
    f_chdrive(sd_path);          /* set default path */

    /* Init I2C2 to access WAU88L25 */
    I2C2_Init();

    // select source from HXT(12MHz)
    CLK_SetModuleClock(I2S0_MODULE, CLK_CLKSEL3_I2S0SEL_HXT, 0);

    MP3Player();

    while(1);
}

/*** (C) COPYRIGHT 2014 Nuvoton Technology Corp. ***/