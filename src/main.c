/* umac
 *
 * Micro Mac 128K emulator
 *
 * Main file with:
 * - umac_ entry points,
 * - main loop,
 * - address decoding/memory map, dispatch to VIA/SCC/disc
 * - Keyboard/mouse event dispatch
 *
 * Copyright 2024 Matt Evans
 *
 * Small portions of m68k interrupt code, error handling taken from
 * Musashi, which is Copyright 1998-2002 Karl Stenerud.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <setjmp.h>

#include "machw.h"
#include "m68k.h"
#include "via.h"
#include "scc.h"
#include "rom.h"
#include "disc.h"

#ifdef PICO
#include "pico.h"
#define FAST_FUNC(x)    __not_in_flash_func(x)
#else
#define FAST_FUNC(x)    x
#endif


#ifdef DEBUG
#define MDBG(...)       printf(__VA_ARGS__)
#else
#define MDBG(...)       do {} while(0)
#endif

#define MERR(...)       fprintf(stderr, __VA_ARGS__)

/* Data */
static unsigned int g_int_controller_pending = 0;      /* list of pending interrupts */
static unsigned int g_int_controller_highest_int = 0;  /* Highest pending interrupt */

uint8_t *_ram_base;
uint8_t *_rom_base;

int overlay = 1;
static uint64_t global_time_us = 0;
static int sim_done = 0;
static jmp_buf main_loop_jb;

static int disassemble = 0;

#define UMAC_EXECLOOP_QUANTUM   5000

static void    update_overlay_layout();

////////////////////////////////////////////////////////////////////////////////

static int      m68k_dump_regs(char *buf, int len)
{
        int r;
        int orig_len = len;
        for (int i = 0; i < 8; i++) {
                r = snprintf(buf, len, "D%d: %08x  ", i, m68k_get_reg(NULL, M68K_REG_D0 + i));
                buf += r;
                len -= r;
        }
        *buf++ = '\n';
        len--;
        for (int i = 0; i < 8; i++) {
                r = snprintf(buf, len, "A%d: %08x  ", i, m68k_get_reg(NULL, M68K_REG_A0 + i));
                buf += r;
                len -= r;
        }
        *buf++ = '\n';
        len--;
        r = snprintf(buf, len, "SR: %08x  SP: %08x USP: %08x ISP: %08x MSP: %08x\n",
                     m68k_get_reg(NULL, M68K_REG_SR),
                     m68k_get_reg(NULL, M68K_REG_SP),
                     m68k_get_reg(NULL, M68K_REG_USP),
                     m68k_get_reg(NULL, M68K_REG_ISP),
                     m68k_get_reg(NULL, M68K_REG_MSP));
        return orig_len - len;
}

/* Exit with an error message.  Use printf syntax. */
void exit_error(char* fmt, ...)
{
	static int guard_val = 0;
	char buff[500];
	unsigned int pc;
	va_list args;

	if(guard_val)
		return;
	else
		guard_val = 1;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
	pc = m68k_get_reg(NULL, M68K_REG_PPC);
	m68k_disassemble(buff, pc, M68K_CPU_TYPE_68000);
	fprintf(stderr, "At %04x: %s\n", pc, buff);

        m68k_dump_regs(buff, 500);
        fprintf(stderr, "%s", buff);
        sim_done = 1;
        longjmp(main_loop_jb, 1);
}


////////////////////////////////////////////////////////////////////////////////
// VIA-related controls
static void     via_ra_changed(uint8_t val)
{
        static uint8_t oldval = 0x10;
        // 7 = scc w/req,a,b (in, indicates RX pending, w/o IRQ)
        // 6 = vid.pg2 (screen buffer select)
        // 5 = hd.sel (SEL line, select head)
        // 4 = overlay
        // 3 = snd.pg2 (sound buffer select)
        // [2:0] = sound volume
        overlay = !!(val & 0x10);
        if ((oldval ^ val) & 0x10) {
                MDBG("OVERLAY CHANGING\n");
                update_overlay_layout();
        }

        oldval = val;
}

static void     via_rb_changed(uint8_t val)
{
        // 7 = sndres (sound enable/disable)
        // 6 = hblank
        // 5 = mouse8 (in, mouse Y2)
        // 4 = mouse4 (in, mouse X2)
        // 3 = mouse7 (in, 0 = button pressed)
        // [2:0] = RTC controls
        (void)val;
}

static uint8_t  via_ra_in()
{
        return 0;
}

// Quadrature bits read from B[5:4] (Y=5, X=4)
static uint8_t via_quadbits = 0;
static uint8_t via_mouse_pressed = 0;

static uint8_t  via_rb_in()
{
        uint8_t v = via_quadbits;
        // Mouse not pressed!
        if (!via_mouse_pressed)
                v |= (1 << 3);
        return v;
}

/* Keyboard interface:
 *
 * Very roughly, it uses CB2 as bidirectional data and CB1 as clock
 * always from keyboard.  There's a handshake with the mac driving
 * data low as a "request to start clocking", with the kbd receiving a
 * byte (clocking out from SR) after that.  The mac does this by
 * "transmitting a byte" of all zeroes, which looks like pulling data
 * low.
 *
 * The VIA SR has a sequence of interrupts as follows:
 * - Mac pulls data low (transmits zero) then immediately loads SR
 *   with the data to TX (a command such as Inquiry)
 * - The VIA asserts SR IRQ when the command's transmitted (the kbd
 *   has woken and clocked it out).
 * - The keyboard -- some time later, importantly -- responds with
 *   a byte in SR, and VIA asserts SR IRQ again.
 *
 * The keyboard does nothing except for respond to commands from the
 * host (i.e. there's nothing proactively transmitted).
 */
#define KBD_CMD_GET_MODEL       0x16
#define KBD_CMD_INQUIRY         0x10
#define KBD_MODEL               5
#define KBD_RSP_NULL            0x7b

static int kbd_last_cmd = 0;
static uint64_t kbd_last_cmd_time = 0;

static void     via_sr_tx(uint8_t data)
{
        if (kbd_last_cmd) {
                MDBG("KBD: Oops, transmitting %02x whilst cmd %02x pending!\n",
                     data, kbd_last_cmd);
        }
        kbd_last_cmd = data;
        kbd_last_cmd_time = global_time_us;
}

static int kbd_pending_evt = -1;
/* Emulate the keyboard: receive commands (such as an inquiry, polling
 * for keypresses) and respond using via_sr_rx().
 */
static void     kbd_rx(uint8_t data)
{
        /* Respond to requests with potted keyboard banter */
        switch (data) {
        case KBD_CMD_GET_MODEL:
                via_sr_rx(0x01 | (KBD_MODEL << 1));
                break;
        case KBD_CMD_INQUIRY:
                if (kbd_pending_evt == -1) {
                        via_sr_rx(KBD_RSP_NULL);
                } else {
                        via_sr_rx(kbd_pending_evt);
                        kbd_pending_evt = -1;
                }
        break;

        default:
                MERR("KBD: Unhandled TX %02x\n", data);
        }
}

static void     kbd_check_work()
{
        /* Process a keyboard command a little later than the transmit
         * time (i.e. not immediately, which makes the mac feel rushed
         * and causes it to ignore the response to punish our
         * hastiness).
         */
        if (kbd_last_cmd &&
            ((global_time_us - kbd_last_cmd_time) > UMAC_EXECLOOP_QUANTUM)) {
                MDBG("KBD: got cmd 0x%x\n", kbd_last_cmd);
                kbd_rx(kbd_last_cmd);
                kbd_last_cmd = 0;
        }
}

void    umac_kbd_event(uint8_t scancode, int down)
{
        if (kbd_pending_evt >= 0) {
                MDBG("KBD: Received event %02x with event %02x pending!\n",
                     scancode, kbd_pending_evt);
                /* FIXME: Add a queue */
        }
        kbd_pending_evt = scancode | (down ? 0 : 0x80);
}

// VIA IRQ output hook:
static void     via_irq_set(int status)
{
        MDBG("[IRQ: VIA IRQ %d]\n", status);
        if (status) {
                // IRQ is asserted
                m68k_set_virq(1, 1);
        } else {
                // IRQ de-asserted
                m68k_set_virq(1, 0);
        }
}

// Ditto, for SCC
static int scc_irq_state = 0;
static void     scc_irq_set(int status)
{
        MDBG("[IRQ: SCC IRQ %d]\n", status);
        if (status) {
                m68k_set_virq(2, 1);
        } else {
                m68k_set_virq(2, 0);
        }
        scc_irq_state = status;
}

////////////////////////////////////////////////////////////////////////////////
// IWM

static uint8_t iwm_regs[16];

void    iwm_write(unsigned int address, uint8_t data)
{
        unsigned int r = (address >> 9) & 0xf;
        MDBG("[IWM: WR %02x -> %d]\n", data, r);
        switch (r) {
        default:
                MDBG("[IWM: unhandled WR %02x to reg %d]\n", data, r);
        }
        iwm_regs[r] = data;
}

uint8_t iwm_read(unsigned int address)
{
        unsigned int r = (address >> 9) & 0xf;
        uint8_t data = iwm_regs[r];
        switch (r) {
        case 8:
                data = 0xff;
                break;
        case 14:
                data = 0x1f;
                break;
        default:
                MDBG("[IWM: unhandled RD of reg %d]\n", r);
        }
        MDBG("[IWM: RD %d <- %02x]\n", r, data);
        return data;
}

////////////////////////////////////////////////////////////////////////////////

static unsigned int  FAST_FUNC(cpu_read_instr_normal)(unsigned int address)
{
        /* Can check for 0x400000 (ROM) and otherwise RAM */
        if ((address & 0xf00000) != ROM_ADDR)
                return RAM_RD_ALIGNED_BE16(CLAMP_RAM_ADDR(address));
        else
                return ROM_RD_ALIGNED_BE16(address & (ROM_SIZE - 1));
}

static unsigned int  FAST_FUNC(cpu_read_instr_overlay)(unsigned int address)
{
        /* Need to check for both 0=ROM, 0x400000=ROM, and RAM at 0x600000...
         */
        if (IS_ROM(address))
                return ROM_RD_ALIGNED_BE16(address & (ROM_SIZE - 1));
        else /* RAM */
                return RAM_RD_ALIGNED_BE16(CLAMP_RAM_ADDR(address));
}

unsigned int (*cpu_read_instr)(unsigned int address) = cpu_read_instr_overlay;

/* Read data from RAM, ROM, or a device */
unsigned int    FAST_FUNC(cpu_read_byte)(unsigned int address)
{
        /* Most likely a RAM access, followed by a ROM access, then I/O */
        if (IS_RAM(address))
                return RAM_RD8(CLAMP_RAM_ADDR(address));
        if (IS_ROM(address))
                return ROM_RD8(address & (ROM_SIZE - 1));

        // decode IO etc
        if (IS_VIA(address))
                return via_read(address);
        if (IS_IWM(address))
                return iwm_read(address);
        if (IS_SCC_RD(address))
                return scc_read(address);
        if (IS_DUMMY(address))
                return 0;

        printf("Attempted to read byte from address %08x\n", address);
        return 0;
}

unsigned int    FAST_FUNC(cpu_read_word)(unsigned int address)
{
        if (IS_RAM(address))
                return RAM_RD16(CLAMP_RAM_ADDR(address));
        if (IS_ROM(address))
                return ROM_RD16(address & (ROM_SIZE - 1));

        if (IS_TESTSW(address))
                return 0;

        exit_error("Attempted to read word from address %08x", address);
        return 0;
}

unsigned int    FAST_FUNC(cpu_read_long)(unsigned int address)
{
        if (IS_RAM(address))
                return RAM_RD32(CLAMP_RAM_ADDR(address));
        if (IS_ROM(address))
                return ROM_RD32(address & (ROM_SIZE - 1));

        if (IS_TESTSW(address))
                return 0;

        exit_error("Attempted to read long from address %08x", address);
        return 0;
}


unsigned int    cpu_read_word_dasm(unsigned int address)
{
        if (IS_RAM(address))
                return RAM_RD16(CLAMP_RAM_ADDR(address));
        if (IS_ROM(address))
                return ROM_RD16(address & (ROM_SIZE - 1));

        exit_error("Disassembler attempted to read word from address %08x", address);
        return 0;
}

unsigned int    cpu_read_long_dasm(unsigned int address)
{
        if (IS_RAM(address))
                return RAM_RD32(CLAMP_RAM_ADDR(address));
        if (IS_ROM(address))
                return ROM_RD32(address & (ROM_SIZE - 1));

        exit_error("Dasm attempted to read long from address %08x", address);
        return 0;
}


/* Write data to RAM or a device */
void    FAST_FUNC(cpu_write_byte)(unsigned int address, unsigned int value)
{
        if (IS_RAM(address)) {
                RAM_WR8(CLAMP_RAM_ADDR(address), value);
                return;
        }

        // decode IO
        if (IS_VIA(address)) {
                via_write(address, value);
                return;
        }
        if (IS_IWM(address)) {
                iwm_write(address, value);
                return;
        }
        if (IS_SCC_WR(address)) {
                scc_write(address, value);
                return;
        }
        if (IS_DUMMY(address))
                return;
        if (address == PV_SONY_ADDR) {
                int r = disc_pv_hook(value);
                if (r)
                        exit_error("Disc PV hook failed (%02x)", value);
                return;
        }
        printf("Ignoring write %02x to address %08x\n", value&0xff, address);
}

void    FAST_FUNC(cpu_write_word)(unsigned int address, unsigned int value)
{
        if (IS_RAM(address)) {
                RAM_WR16(CLAMP_RAM_ADDR(address), value);
                return;
        }
        printf("Ignoring write %04x to address %08x\n", value&0xffff, address);
}

void    FAST_FUNC(cpu_write_long)(unsigned int address, unsigned int value)
{
        if (IS_RAM(address)) {
                RAM_WR32(CLAMP_RAM_ADDR(address), value);
                return;
        }
        printf("Ignoring write %08x to address %08x\n", value, address);
}

/* Update function pointers for memory accessors based on overlay state/memory map layout */
static void     update_overlay_layout()
{
        if (overlay) {
                cpu_read_instr = cpu_read_instr_overlay;
        } else {
                cpu_read_instr = cpu_read_instr_normal;
        }
}

/* Called when the CPU pulses the RESET line */
void    cpu_pulse_reset(void)
{
        /* Reset IRQs etc. */
}

/* Called when the CPU acknowledges an interrupt */
int     cpu_irq_ack(int level)
{
        (void)level;
        /* Level really means line, so do an ack per device */
	return M68K_INT_ACK_AUTOVECTOR;
}

/* Implementation for the interrupt controller */
void    int_controller_set(unsigned int value)
{
	unsigned int old_pending = g_int_controller_pending;

	g_int_controller_pending |= (1<<value);

	if(old_pending != g_int_controller_pending && value > g_int_controller_highest_int)
	{
		g_int_controller_highest_int = value;
		m68k_set_irq(g_int_controller_highest_int);
	}
}

void    int_controller_clear(unsigned int value)
{
	g_int_controller_pending &= ~(1<<value);

	for(g_int_controller_highest_int = 7;g_int_controller_highest_int > 0;g_int_controller_highest_int--)
		if(g_int_controller_pending & (1<<g_int_controller_highest_int))
			break;

	m68k_set_irq(g_int_controller_highest_int);
}

/* Disassembler */
static void     make_hex(char* buff, unsigned int pc, unsigned int length)
{
	char* ptr = buff;

	for(;length>0;length -= 2)
	{
		sprintf(ptr, "%04x", cpu_read_word_dasm(pc));
		pc += 2;
		ptr += 4;
		if(length > 2)
			*ptr++ = ' ';
	}
}

void    cpu_instr_callback(int pc)
{
	static char buff[100];
	static char buff2[100];
	static unsigned int instr_size;

        if (!disassemble)
                return;

	instr_size = m68k_disassemble(buff, pc, M68K_CPU_TYPE_68000);
	make_hex(buff2, pc, instr_size);
	MDBG("E %03x: %-20s: %s\n", pc, buff2, buff);
	fflush(stdout);
}

int     umac_init(void *ram_base, void *rom_base, disc_descr_t discs[DISC_NUM_DRIVES])
{
        _ram_base = ram_base;
        _rom_base = rom_base;

	m68k_init();
	m68k_set_cpu_type(M68K_CPU_TYPE_68000);
	m68k_pulse_reset();

        struct via_cb vcb = { .ra_change = via_ra_changed,
                              .rb_change = via_rb_changed,
                              .ra_in = via_ra_in,
                              .rb_in = via_rb_in,
                              .sr_tx = via_sr_tx,
                              .irq_set = via_irq_set,
        };
        via_init(&vcb);
        struct scc_cb scb = { .irq_set = scc_irq_set,
        };
        scc_init(&scb);
        disc_init(discs);

        return 0;
}

void    umac_opt_disassemble(int enable)
{
        disassemble = enable;
}

#define MOUSE_MAX_PENDING_PIX   30

static int pending_mouse_deltax = 0;
static int pending_mouse_deltay = 0;

/* Provide mouse input (movement, button) data.
 *
 * X is positive going right; Y is positive going upwards.
 */
void    umac_mouse(int deltax, int deltay, int button)
{
        pending_mouse_deltax += deltax;
        pending_mouse_deltay += deltay;

        /* Clamp if the UI has flooded with lots and lots of steps!
         */
        if (pending_mouse_deltax > MOUSE_MAX_PENDING_PIX)
                pending_mouse_deltax = MOUSE_MAX_PENDING_PIX;
        if (pending_mouse_deltax < -MOUSE_MAX_PENDING_PIX)
                pending_mouse_deltax = -MOUSE_MAX_PENDING_PIX;
        if (pending_mouse_deltay > MOUSE_MAX_PENDING_PIX)
                pending_mouse_deltay = MOUSE_MAX_PENDING_PIX;
        if (pending_mouse_deltay < -MOUSE_MAX_PENDING_PIX)
                pending_mouse_deltay = -MOUSE_MAX_PENDING_PIX;

        /* FIXME: The movement might take a little time, but this
         * posts the button status immediately.  Probably OK, but the
         * mismatch might be perceptible.
         */
        via_mouse_pressed = button;
}

static void     mouse_tick()
{
        /* Periodically, check if the mouse X/Y deltas are non-zero.
         * If a movement is required, encode one step in X and/or Y
         * and deduct from the pending delta.
         *
         * The step ultimately posts an SCC IRQ, so we _don't_ try to
         * make any more steps while an IRQ is currently pending.
         * (Currently, that means a previous step's DCD IRQ event
         * hasn't yet been consumed by the OS handler.  In future, if
         * SCC is extended with other IRQ types, then just checking
         * the IRQ status is technically too crude, but should still
         * be fine given the timeframes.)
         */
        if (pending_mouse_deltax == 0 && pending_mouse_deltay == 0)
                return;

        if (scc_irq_state == 1)
                return;

        static int old_dcd_a = 0;
        static int old_dcd_b = 0;

        /* Mouse X/Y quadrature signals are wired to:
         *  VIA Port B[4] & SCC DCD_A for X
         *  VIA Port B[5] & SCC DCD_B for Y
         *
         * As VIA mouse signals aren't sampled until IRQ, can do this
         * in one step, toggling existing DCD states and setting VIA
         * either equal or opposite to DCD:
         */
        int dcd_a = old_dcd_a;
        int dcd_b = old_dcd_b;
        int deltax = pending_mouse_deltax;
        int deltay = pending_mouse_deltay;
        uint8_t qb = via_quadbits;

        if (deltax) {
                dcd_a = !dcd_a;
                qb = (qb & ~0x10) | ((deltax < 0) == dcd_a ? 0x10 : 0);
                pending_mouse_deltax += (deltax > 0) ? -1 : 1;
                MDBG("  px %d, oldpx %d", pending_mouse_deltax, deltax);
        }

        if (deltay) {
                dcd_b = !dcd_b;
                qb = (qb & ~0x20) | ((deltay < 0) == dcd_b ? 0x20 : 0);
                pending_mouse_deltay += (deltay > 0) ? -1 : 1;
                MDBG("  py %d, oldpy %d", pending_mouse_deltay, deltay);
        }
        MDBG("\n");

        via_quadbits = qb;
        old_dcd_a = dcd_a;
        old_dcd_b = dcd_b;
        scc_set_dcd(dcd_a, dcd_b);
}

void    umac_reset()
{
        overlay = 1;
        m68k_pulse_reset();
}

/* Called by the disc code when an eject op happens. */
void    umac_disc_ejected()
{
#ifdef SIM
        exit(1);
#else
        umac_reset();
#endif
}

/* Run the emulator for about a frame.
 * Returns 0 for not-done, 1 when an exit/done condition arises.
 */
int     umac_loop()
{
        setjmp(main_loop_jb);

        const int us = UMAC_EXECLOOP_QUANTUM;
        m68k_execute(us*8);
        global_time_us += us;

        // Device polling
        via_tick(global_time_us);
        mouse_tick();
        kbd_check_work();

	return sim_done;
}

