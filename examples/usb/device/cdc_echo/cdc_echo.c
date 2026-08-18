/*
 *  Copyright (C) 2021-2023 Texas Instruments Incorporated
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
 */

/* Adapted by TI for running on its platform and SDK */

#include <stdint.h>
#include <stdbool.h>

#if defined(SOC_AM64X) || defined (SOC_AM243X)
#include <usb/cdn/include/usb_init.h>
#include <usb/cdn/include/cdn_print.h>
#endif
#if defined(SOC_AM261X)
#include <usb/synp/soc/usb_init.h>
#endif
#include "tusb.h"

#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"
#include "ti_board_open_close.h"

/* CDC interface the array is sent out on, and how much of it to send */
#define CDC_SEND_ITF    (0U)
#define CDC_SEND_LEN    (6000U)

/* CFG_TUD_CDC_TX_BUFSIZE (tusb_config.h) is left untouched at 512 bytes,
 * so this array is bigger than one write can accept in a single call --
 * send_task() below feeds it out over several tud_cdc_n_write() calls as
 * ring buffer space frees up.
 */
static uint8_t  gSendBuf[CDC_SEND_LEN];
static uint32_t gSendOffset = 0;
static bool     gSendDone   = false;

static void send_task(void);

int cdc_echo_main(void)
{
    uint32_t i;

    Drivers_open();
    Board_driversOpen();

    /* fill with a recognizable pattern so the host side can verify the
     * bytes it receives instead of just counting them */
    for (i = 0; i < CDC_SEND_LEN; i++)
    {
        gSendBuf[i] = (uint8_t) (i & 0xFFU);
    }

    while (1)
    {
        #if defined(SOC_AM64X) || defined (SOC_AM243X)
        cusbd_dsr();   /* Cadence DSR task */
        #else
        USB_dwcTask(); /* Synopsis DWC task */
        #endif
        tud_task();   /* tinyusb device task */
        send_task();  /* push the array out over CDC */
    }
}

/* Send the CDC_SEND_LEN-byte array out over CDC interface CDC_SEND_ITF.
 *
 * This is one tud_cdc_n_write() call per pass instead of the byte-at-a-time
 * tud_cdc_n_write_char() loop the echo example used -- that removes the
 * per-byte function-call/branch overhead that dominated the echo path.
 *
 * The TX ring buffer is only CFG_TUD_CDC_TX_BUFSIZE (512) bytes deep, so a
 * single call can't accept all 6000 bytes at once. Each pass writes as much
 * of the remaining array as tud_cdc_n_write_available() reports room for,
 * and resumes from gSendOffset next pass, until the whole array has been
 * queued and flushed exactly once.
 */
static void send_task(void)
{
    if (gSendDone)
    {
        return;
    }

    if (!tud_cdc_n_connected(CDC_SEND_ITF))
    {
        return;
    }

    if (gSendOffset < CDC_SEND_LEN)
    {
        uint32_t remain    = CDC_SEND_LEN - gSendOffset;
        uint32_t available = tud_cdc_n_write_available(CDC_SEND_ITF);
        uint32_t chunk     = (available < remain) ? available : remain;

        if (chunk > 0)
        {
            uint32_t written = tud_cdc_n_write(CDC_SEND_ITF, &gSendBuf[gSendOffset], chunk);
            gSendOffset += written;
            tud_cdc_n_write_flush(CDC_SEND_ITF);
        }
    }
    else
    {
        /* make sure the final partial packet actually goes out on the wire */
        tud_cdc_n_write_flush(CDC_SEND_ITF);
        gSendDone = true;
    }
}
