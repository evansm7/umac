/* umac VIA emulation
 *
 * Bare minimum support for ports A/B, shift register, and IRQs.
 * A couple of Mac-specific assumptions in here, as per comments...
 *
 * Copyright 2024 Matt Evans
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <inttypes.h>
#include <stdio.h>

#include "via.h"

#ifdef DEBUG
#define VDBG(...)       printf(__VA_ARGS__)
#else
#define VDBG(...)       do {} while(0)
#endif

#define VERR(...)       fprintf(stderr, __VA_ARGS__)

#define VIA_RB          0
#define VIA_RA          1
#define VIA_DDRB        2
#define VIA_DDRA        3
#define VIA_T1CL        4
#define VIA_T1CH        5
#define VIA_T1LL        6
#define VIA_T1LH        7
#define VIA_T2CL        8
#define VIA_T2CH        9
#define VIA_SR          10
#define VIA_ACR         11
#define VIA_PCR         12
#define VIA_IFR         13
#define  VIA_IRQ_CA     0x01
#define  VIA_IRQ_CB     0x02
#define  VIA_IRQ_SR     0x04
#define VIA_IER         14
#define VIA_RA_ALT      15 // No-handshake version

static const char *dbg_regnames[] = {
        "VIA_RB",
        "VIA_RA",
        "VIA_DDRB",
        "VIA_DDRA",
        "VIA_T1CL",
        "VIA_T1CH",
        "VIA_T1LL",
        "VIA_T1LH",
        "VIA_T2CL",
        "VIA_T2CH",
        "VIA_SR",
        "VIA_ACR",
        "VIA_PCR",
        "VIA_IFR",
        "VIA_IER",
        "VIA_RA_ALT",
};

static uint8_t via_regs[16];
static struct via_cb via_callbacks;
static int irq_status = 0;
static uint8_t irq_active = 0;
static uint8_t irq_enable = 0;

void    via_init(struct via_cb *cb)
{
        for (int i = 0; i < 16; i++)
                via_regs[i] = 0;
        via_regs[VIA_RA] = 0x10; // Overlay, FIXME
        if (cb)
                via_callbacks = *cb;
}

static void via_update_rega(uint8_t data)
{
        if ((via_regs[VIA_RA] ^ data) && via_callbacks.ra_change)
                via_callbacks.ra_change(data);
}

static void via_update_regb(uint8_t data)
{
        if ((via_regs[VIA_RB] ^ data) && via_callbacks.rb_change)
                via_callbacks.rb_change(data);
}

static int sr_tx_pending = -1;

static void via_update_sr(uint8_t data)
{
        /* Mac assumption: SR active when ACR SR control selects
         * external clock:
         */
        if ((via_regs[VIA_ACR] & 0x1c) == 0x1c) {
                if (sr_tx_pending >= 0) {
                        /* Doh! */
                        VDBG("[VIA: SR send whilst send (%02x) active!]\n", sr_tx_pending);
                }
                /* When SR's written, the ROM'll wait for the IRQ
                 * indicating the byte's transmitted.  At that point,
                 * it'll expect a response (but not too soon).  So,
                 * flag that the TX occurred, and mark tbe byte
                 * pending to deal with in the runloop "a little bit
                 * later" -- the response seems to get lost if it's
                 * reflected back too soon.
                 */
                sr_tx_pending = data;
                irq_active |= VIA_IRQ_SR;
        } else if ((via_regs[VIA_ACR] & 0x1c) == 0x18) {
                /* Mac sends a byte of zeroes fuelled by phi2, as a
                 * method to pull KbdData low (to get the kbd's
                 * attention).  The d/s implies SRMC=110 completion
                 * should also trigger IRQ vector 2, but empirically
                 * this screws things up and code doesn't seem to
                 * expect it.  So don't trigger IRQ.
                 */
                VDBG("[VIA: SR send (val %02x)]\n", data);
                via_regs[VIA_SR] = 0;
        }
}

/* Called when the VIA_IRQ_SR interrupt is acknowledged, i.e. that the
 * Mac's aware of the last TX/RX.  We can then use it to pace out the
 * transmit callback action -- the response then cannot race with the
 * IRQ showing the TX has completed.
 */
static void via_sr_done()
{
        if (sr_tx_pending >= 0) {
                uint8_t data = (uint8_t)sr_tx_pending;
                sr_tx_pending = -1;
                if (via_callbacks.sr_tx)
                        via_callbacks.sr_tx(data);
        }
}

// 6 Timer 1
// 5 Timer 2
// 4 Keyboard clock
// 3 Keyboard data bit
// 2 Keyboard data ready
// 1 CA2: Vertical blanking interrupt
// 0 CA1: One-second interrupt

static void via_assess_irq()
{
        int irq = 0;
        static uint8_t last_active = 0;
        uint8_t active = irq_enable & irq_active & 0x7f;
        irq = active != 0;

        if (active != last_active) {
                VDBG("[VIA: IRQ state now %02x]\n", active);
                last_active = active;
        }
        if (irq != irq_status) {
                via_callbacks.irq_set(irq);
                irq_status = irq;
        }
}

/* A[12:9] select regs */
void    via_write(unsigned int address, uint8_t data)
{
        unsigned int r = (address >> 9) & 0xf;
        const char *rname = dbg_regnames[r];
        (void)rname; // Unused if !DEBUG
        VDBG("[VIA: WR %02x -> %s (0x%x)]\n", data, rname, r);

        int dowrite = 1;
        switch (r) {
        case VIA_RA:
        case VIA_RA_ALT:
                via_update_rega(data);
                r = VIA_RA;
                break;
        case VIA_RB:
                via_update_regb(data);
                break;
        case VIA_DDRA:
                break; // FIXME
        case VIA_DDRB:
                break; // FIXME
        case VIA_SR:
                via_update_sr(data);
                dowrite = 0;
                break;
        case VIA_IER:
                if (data & 0x80)
                        irq_enable |= data & 0x7f;
                else
                        irq_enable &= ~(data & 0x7f);
                break;
        case VIA_IFR: {
                int which_acked = (irq_active & data);
                irq_active &= ~data;
                /* If ISR is acking the SR IRQ, a TX or RX is complete,
                 * and we might want to then trigger other actions.
                 */
                if (which_acked & VIA_IRQ_SR)
                        via_sr_done();
        } break;
        case VIA_PCR:
                VDBG("VIA PCR %02x\n", data);
                break;
        default:
                VDBG("[VIA: unhandled WR %02x to %s (reg 0x%x)]\n", data, rname, r);
        }

        if (dowrite)
                via_regs[r] = data;
        via_assess_irq();
}

uint8_t via_read_ifr()
{
        uint8_t active = irq_enable & irq_active & 0x7f;
        return irq_active | (active ? 0x80 : 0);
}

static uint8_t via_read_rega()
{
        uint8_t data = (via_callbacks.ra_in) ? via_callbacks.ra_in() : 0;
        uint8_t ddr = via_regs[VIA_DDRA];
        // DDR=1 is output, so take ORA version:
        return (ddr & via_regs[VIA_RA]) | (~ddr & data);
}

static uint8_t via_read_regb()
{
        uint8_t data = (via_callbacks.rb_in) ? via_callbacks.rb_in() : 0;
        uint8_t ddr = via_regs[VIA_DDRB];
        return (ddr & via_regs[VIA_RB]) | (~ddr & data);
}

uint8_t via_read(unsigned int address)
{
        unsigned int r = (address >> 9) & 0xf;
        const char *rname = dbg_regnames[r];
        uint8_t data = via_regs[r];
        (void)rname; // Unused if !DEBUG

        switch (r) {
        case VIA_RA:
        case VIA_RA_ALT:
                data = via_read_rega();
                break;

        case VIA_RB:
                data = via_read_regb();
                break;

        case VIA_SR:
                irq_active &= ~0x04;
                break;

        case VIA_IER:
                data = 0x80 | irq_enable;
                break;

        case VIA_IFR:
                data = via_read_ifr();
                break;
        default:
                VDBG("[VIA: unhandled RD of %s (reg 0x%x)]\n", rname, r);
        }
        VDBG("[VIA: RD %02x <- %s (0x%x)]\n", data, rname, r);
        via_assess_irq();
        return data;
}

/* Time param in us */
void    via_tick(uint64_t time)
{
        (void)time;
        // FIXME: support actual timers.....!
}

/* External world pipes CA1/CA2 events (passage of time) in here:
 */
void    via_caX_event(int ca)
{
        if (ca == 1) {
                irq_active |= VIA_IRQ_CA;
        } else if (ca == 2) {
                irq_active |= VIA_IRQ_CB;
        }
        via_assess_irq();
}

void    via_sr_rx(uint8_t val)
{
        /* If SR config in ACR is external (yes! a Mac assumption!)
         * then fill SR with value and trigger SR IRQ:
         */
        VDBG("[VIAL sr_rx %02x (acr %02x)]\n", val, via_regs[VIA_ACR]);
        if ((via_regs[VIA_ACR] & 0x1c) == 0x0c) {
                via_regs[VIA_SR] = val;
                irq_active |= VIA_IRQ_SR;
                VDBG("[VIA sr_rx received, IRQ pending]\n");
                via_assess_irq();
        } else {
                VDBG("[VIA ACR SR state %02x, not receiving]\n", via_regs[VIA_ACR]);
        }
}
