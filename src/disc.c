/* umac disc emulation
 *
 * Contains a PV wrapper around a cut-down version of Basilisk II's
 * sony.cpp disc driver, with copyright/licence as shown inline below.
 *
 * Remaining (top) code is Copyright 2024 Matt Evans.
 *
 */

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>

#include "disc.h"
#include "m68k.h"
#include "machw.h"

#ifdef DEBUG
#define DDBG(...)       printf(__VA_ARGS__)
#else
#define DDBG(...)       do {} while(0)
#endif

#define DERR(...)       fprintf(stderr, __VA_ARGS__)

extern void umac_disc_ejected();

// B2 decls:
static int16_t SonyOpen(uint32_t pb, uint32_t dce, uint32_t status);
static int16_t SonyPrime(uint32_t pb, uint32_t dce);
static int16_t SonyControl(uint32_t pb, uint32_t dce);
static int16_t SonyStatus(uint32_t pb, uint32_t dce);

static void    SonyInit(disc_descr_t discs[DISC_NUM_DRIVES]);

void    disc_init(disc_descr_t discs[DISC_NUM_DRIVES])
{
        SonyInit(discs);
}

/* This is the entrypoint redirected from the PV .Sony replacement driver.
 * Largely re-uses code from Basilisk!
 */
int     disc_pv_hook(uint8_t opcode)
{
        uint32_t a0 = m68k_get_reg(NULL, M68K_REG_A0);
        uint32_t a1 = m68k_get_reg(NULL, M68K_REG_A1);
        uint32_t a2 = m68k_get_reg(NULL, M68K_REG_A2);
        uint32_t d0 = 0;

        switch(opcode) {
        case 0: // Open
                DDBG("[Disc: OPEN]\n");
                d0 = SonyOpen(ADR24(a0), ADR24(a1), ADR24(a2));
                break;
        case 1: // Prime
                DDBG("[Disc: PRIME]\n");
                d0 = SonyPrime(ADR24(a0), ADR24(a1));
                break;
        case 2: // Control
                DDBG("[Disc: CONTROL]\n");
                d0 = SonyControl(ADR24(a0), ADR24(a1));
                break;
        case 3: // Status
                DDBG("[Disc: STATUS]\n");
                d0 = SonyStatus(ADR24(a0), ADR24(a1));
                break;

        default:
                DERR("[Disc PV op %02x unhandled!]\n", opcode);
                return -1;
        };

        // Return val:
        m68k_set_reg(M68K_REG_D0, d0);
        return 0;
}

////////////////////////////////////////////////////////////////////////////////
// Basilisk II code follows

#define WriteMacInt32(addr, val)        RAM_WR32(addr, val)
#define WriteMacInt16(addr, val)        RAM_WR16(addr, val)
#define WriteMacInt8(addr, val)         RAM_WR8(addr, val)
#define ReadMacInt32(addr)              RAM_RD32(addr)
#define ReadMacInt16(addr)              RAM_RD16(addr)
#define ReadMacInt8(addr)               RAM_RD8(addr)

#define Mac2HostAddr(addr)              (ram_get_base() + ADR24(addr))

// These access host pointers, in BE/unaligned:
#define WriteBEInt32(addr, val)         WRITE_LONG(((uint8_t *)(addr)), 0, val)
#define WriteBEInt16(addr, val)         WRITE_WORD(((uint8_t *)(addr)), 0, val)
#define WriteBEInt8(addr, val)          WRITE_BYTE(((uint8_t *)(addr)), 0, val)

/*
 *  sony.cpp - Replacement .Sony driver (floppy drives)
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 *  SEE ALSO
 *    Inside Macintosh: Devices, chapter 1 "Device Manager"
 *    Technote DV 05: "Drive Queue Elements"
 *    Technote DV 07: "Forcing Floppy Disk Size to be Either 400K or 800K"
 *    Technote DV 17: "Sony Driver: What Your Sony Drives For You"
 *    Technote DV 23: "Driver Education"
 *    Technote FL 24: "Don't Look at ioPosOffset for Devices"
 */

#include "b2_macos_util.h"


// Struct for each drive
typedef struct sony_drive_info {
	int num;			// Drive number
	uint8_t *data;          // If non-zero, direct mapping of block data
        unsigned int size;
	int to_be_mounted;	// Flag: drive must be mounted in accRun
	int read_only;		// Flag: force write protection
	uint32_t status;	// Mac address of drive status record
	void *op_ctx;
	disc_op_read op_read;   // Callback for read (when data == 0)
	disc_op_write op_write; //  ''    ''    write  ''
} sony_drinfo_t;

// List of drives handled by this driver
static sony_drinfo_t drives[DISC_NUM_DRIVES];

/*
 *  Get reference to drive info or drives.end() if not found
 */

static sony_drinfo_t *get_drive_info(int num)
{
        for (int i = 0; i < DISC_NUM_DRIVES; i++) {
                if (drives[i].num == num)
                        return &drives[i];
        }
        return 0;
}

/*
 *  Initialization
 */

void SonyInit(disc_descr_t discs[DISC_NUM_DRIVES])
{
        drives[0].num = 0;
        drives[0].to_be_mounted = 1;
        drives[0].read_only = discs[0].read_only;
        drives[0].data = discs[0].base;
        drives[0].size = discs[0].size;
        drives[0].op_ctx = discs[0].op_ctx;
        drives[0].op_read = discs[0].op_read;
        drives[0].op_write = discs[0].op_write;
        // FIXME: Disc 2
}

/*
 *  Set error code in DskErr
 */

static int16_t set_dsk_err(int16_t err)
{
	DDBG("set_dsk_err(%d)\n", err);
	WriteMacInt16(0x142, err);
	return err;
}

/*
 *  Find first free drive number, starting at num
 */

static int is_drive_number_free(int num)
{
        uint32_t e = ReadMacInt32(0x308 + qHead);
        while (e) {
                uint32_t d = e - dsQLink;
                if ((int)ReadMacInt16(d + dsQDrive) == num)
                        return 0;
                e = ReadMacInt32(e + qLink);
        }
        return 1;
}

int FindFreeDriveNumber(int num)
{
        while (!is_drive_number_free(num))
                num++;
        return num;
}

const int SonyRefNum = -5;                              // RefNum of driver

/*
 *  Driver Open() routine
 */
int16_t SonyOpen(uint32_t pb, uint32_t dce, uint32_t status)
{
	DDBG("SonyOpen\n");

	// Set up DCE
	WriteMacInt32(dce + dCtlPosition, 0);
	WriteMacInt16(dce + dCtlQHdr + qFlags, (ReadMacInt16(dce + dCtlQHdr + qFlags) & 0xff00) | 3);	// Version number, must be >=3 or System 8 will replace us

	// Set up fake SonyVars
	WriteMacInt32(0x134, 0xdeadbeef);

	// Clear DskErr
	set_dsk_err(0);

	// Install drives
        //for (int drnum = 0; drnum < NUM_DRIVES; drnum++) {
        const int drnum = 0;
        sony_drinfo_t *info = &drives[drnum];

        info->num = FindFreeDriveNumber(1); // ? 1 for internal, 2 for external
        info->to_be_mounted = 0;

        // Original code allocated drive status record here (invoked
        // trap to NewPtrSysClear), but our driver does this instead
        // (it's passed in via status parameter), to avoid having to
        // implement invocation of 68K traps/upcalls from sim env.
        info->status = status;
        DDBG(" DrvSts at %08x\n", info->status);

        // Set up drive status
        // ME: do 800K, double sided (see IM)
        WriteMacInt16(info->status + dsQType, sony);
        WriteMacInt8(info->status + dsInstalled, 1);
        WriteMacInt8(info->status + dsSides, 0xff); // 2 sides
        WriteMacInt8(info->status + dsTwoSideFmt, 0xff); //
        //WriteMacInt8(info->status + dsNewIntf, 0xff);
        WriteMacInt8(info->status + dsMFMDrive, 0);	// 0 = 400/800K GCR drive)
        WriteMacInt8(info->status + dsMFMDisk, 0);
        //WriteMacInt8(info->status + dsTwoMegFmt, 0xff);	// 1.44MB (0 = 720K)

        // If disk in drive...
        WriteMacInt8(info->status + dsDiskInPlace, 1);	// Inserted removable disk
        WriteMacInt8(info->status + dsWriteProt, info->read_only ? 0xff : 0);
        DDBG(" disk inserted, flagging for mount\n");
        info->to_be_mounted = 1;

        // Original code ddded drive to drive queue here (invoked trap
        // to AddDrive), but our driver does this after this PV call returns.
        // FIXME: In future return a bitmap of drives to add.
        (void)pb;

        return noErr;
}


/*
 *  Driver Prime() routine
 */

int16_t SonyPrime(uint32_t pb, uint32_t dce)
{
        DDBG("Disc: PRIME %08x %08x\n", pb, dce);
	WriteMacInt32(pb + ioActCount, 0);

	// Drive valid and disk inserted?
        sony_drinfo_t *info = get_drive_info(ReadMacInt16(pb + ioVRefNum));
        DDBG("- info %p (ref %d)\n", (void *)info, ReadMacInt16(pb + ioVRefNum));
	if (!info)
		return set_dsk_err(nsDrvErr);
	if (!ReadMacInt8(info->status + dsDiskInPlace))
		return set_dsk_err(offLinErr);
	WriteMacInt8(info->status + dsDiskInPlace, 2);	// Disk accessed

	// Get parameters
	void *buffer = Mac2HostAddr(ReadMacInt32(pb + ioBuffer)); // FIXME
	size_t length = ReadMacInt32(pb + ioReqCount);
	uint32_t position = ReadMacInt32(dce + dCtlPosition);
	if ((length & 0x1ff) || (position & 0x1ff)) {
                DDBG("- Bad param: length 0x%lx, pos 0x%x\n", length, position);
		return set_dsk_err(paramErr);
        }
        if ((position + length) > info->size) {
                DDBG("- Off end: length 0x%lx, pos 0x%x\n", length, position);
		return set_dsk_err(paramErr);
        }

	size_t actual = 0;
	if ((ReadMacInt16(pb + ioTrap) & 0xff) == aRdCmd) {
                DDBG("DISC: READ %ld from +0x%x\n", length, position);
                if (info->data) {
                        DDBG(" (Read buffer: %p)\n", (void *)&info->data[position]);
                        memcpy(buffer, &info->data[position], length);
                } else {
                        if (info->op_read) {
                                DDBG(" (read op into buffer)\n");
                                int r = info->op_read(info->op_ctx, buffer, position, length);
                                if (r < 0)
                                        return set_dsk_err(paramErr);
                        } else {
                                DERR("No disc read strategy!\n");
                                return set_dsk_err(offLinErr);
                        }
                }

		// Clear TagBuf
		WriteMacInt32(0x2fc, 0);
		WriteMacInt32(0x300, 0);
		WriteMacInt32(0x304, 0);
	} else {
                DDBG("DISC: WRITE %ld to +0x%x\n", length, position);

		// Write
		if (info->read_only)
			return set_dsk_err(wPrErr);

                DDBG("DISC: WRITE %ld to +0x%x\n", length, position);
                if (info->data) {
                        DDBG(" (Write buffer: %p)\n", (void *)&info->data[position]);
                        memcpy(&info->data[position], buffer, length);
                } else {
                        if (info->op_write) {
                                DDBG(" (write op into buffer)\n");
                                int r = info->op_write(info->op_ctx, buffer, position, length);
                                if (r < 0)
                                        return set_dsk_err(paramErr);
                        } else {
                                DERR("No disc write strategy!\n");
                                return set_dsk_err(offLinErr);
                        }
                }
        }

	// Update ParamBlock and DCE
	WriteMacInt32(pb + ioActCount, actual);
	WriteMacInt32(dce + dCtlPosition, ReadMacInt32(dce + dCtlPosition) + actual);
	return set_dsk_err(noErr);
}


/*
 *  Driver Control() routine
 */

int16_t SonyControl(uint32_t pb, uint32_t dce)
{
	uint16_t code = ReadMacInt16(pb + csCode);
	DDBG("SonyControl %d\n", code);

	// General codes
	switch (code) {
		case 1:		// KillIO (not supported)
			return set_dsk_err(-1);

		case 9:		// Track cache control (ignore, assume that host OS does the caching)
			return set_dsk_err(noErr);

                case 65: {	// Periodic action (accRun, "insert" disks on startup)
                        static int complained_yet = 0;
                        if (!complained_yet) {
                                DERR("SonyControl:accRun: Not supported!\n");
                                complained_yet = 1;
                        }
                        // The original emulation code hooked this to mount_mountable_volumes,
                        // which called back to PostEvent(diskEvent).
			return set_dsk_err(-1);
                }
	}

	// Drive valid?
	sony_drinfo_t *info = get_drive_info(ReadMacInt16(pb + ioVRefNum));
	if (!info)
		return set_dsk_err(nsDrvErr);

	// Drive-specific codes
	int16_t err = noErr;
	switch (code) {
		case 5:			// Verify disk
			if (ReadMacInt8(info->status + dsDiskInPlace) <= 0) {
				err = offLinErr;
			}
			break;

		case 6:			// Format disk
			if (info->read_only) {
				err = wPrErr;
/*			} else if (ReadMacInt8(info->status + dsDiskInPlace) > 0) {
				if (!SysFormat(info->fh))
					err = writErr;
*/
			} else
				err = offLinErr;
			break;

		case 7:			// Eject
			if (ReadMacInt8(info->status + dsDiskInPlace) > 0) {
                                DERR("DISC: EJECT\n");
//				SysEject(info->fh);
				WriteMacInt8(info->status + dsDiskInPlace, 0);

                                umac_disc_ejected();
			}
			break;

		case 8:			// Set tag buffer (ignore, not supported)
			break;
#ifdef FUTURE_EXTRA_STUFF
		case 21:		// Get drive icon
			WriteMacInt32(pb + csParam, SonyDriveIconAddr);
			break;

		case 22:		// Get disk icon
			WriteMacInt32(pb + csParam, SonyDiskIconAddr);
			break;
#endif
		case 23:		// Get drive info
			if (info->num == 1) {
				WriteMacInt32(pb + csParam, 0x0004);	// Internal SuperDrive
			} else {
				WriteMacInt32(pb + csParam, 0x0104);	// External SuperDrive
			}
			break;

#ifdef FUTURE_EXTRA_STUFF
		case 0x4350:	// Enable/disable retries ('CP') (not supported)
			break;

		case 0x4744:	// Get raw track data ('GD') (not supported)
			break;

		case 0x5343:	// Format and write to disk ('SC') in one pass, used by DiskCopy to speed things up
			if (!ReadMacInt8(info->status + dsDiskInPlace)) {
				err = offLinErr;
			} else if (info->read_only) {
				err = wPrErr;
			} else {
				// Assume that the disk is already formatted and only write the data
				void *data = Mac2HostAddr(ReadMacInt32(pb + csParam + 2));
				size_t actual = Sys_write(info->fh, data, 0, 2880*512);
				if (actual != 2880*512)
					err = writErr;
			}
			break;
#endif
		default:
			DERR("WARNING: Unknown SonyControl(%d)\n", code);
			err = controlErr;
			break;
	}
        (void)dce;

	return set_dsk_err(err);
}


/*
 *  Driver Status() routine
 */

int16_t SonyStatus(uint32_t pb, uint32_t dce)
{
	uint16_t code = ReadMacInt16(pb + csCode);
	DDBG("SonyStatus %d\n", code);

	// Drive valid?
        sony_drinfo_t *info = get_drive_info(ReadMacInt16(pb + ioVRefNum));
	if (!info)
		return set_dsk_err(nsDrvErr);

	int16_t err = noErr;
	switch (code) {
		case 6:			// Return list of supported disk formats
			if (ReadMacInt16(pb + csParam) > 0) {	// At least one entry requested?
				uint32_t adr = ReadMacInt32(pb + csParam + 2);
				WriteMacInt16(pb + csParam, 1);		// 1 format supported
				WriteMacInt32(adr, 2880);			// 2880 sectors
				WriteMacInt32(adr + 4, 0xd2120050);	// DD, 2 heads, 18 secs/track, 80 tracks

				// Upper byte of format flags:
				//  bit #7: number of tracks, sectors, and heads is valid
				//  bit #6: current disk has this format
				//  bit #5: <unused>
				//  bit #4: double density
				//  bits #3..#0: number of heads
			} else {
				err = paramErr;
			}
			break;

		case 8:			// Get drive status
                        memcpy(pb + csParam + ram_get_base(), ram_get_base() + info->status, 22);
			break;

		case 10:		// Get disk type and MFM info
                        DERR("**** FIXME status op 10\n");
                        // Hack!
			WriteMacInt32(pb + csParam, 0xfe);
                        //ReadMacInt32(info->status + dsMFMDrive) & 0xffffff00 | 0xfe);	// 0xfe = SWIM2 controller
			break;
#ifdef FUTURE_EXTRA_STUFF
		case 0x4350:	// Measure disk speed at a given track ('CP') (not supported)
			break;

		case 0x4456:	// Duplicator (DiskCopy) version supported ('DV'), enables the 'SC' control code above
			WriteMacInt16(pb + csParam, 0x0410);	// Version 4.1 and later
			break;

		case 0x5250:	// Get floppy info record ('RP') (not supported)
			break;
#endif
		case 0x5343:	// Get address header format byte ('SC')
			WriteMacInt8(pb + csParam, 0x02);	// 500 kbit/s (HD) MFM
			break;

		default:
			DERR("WARNING: Unknown SonyStatus(%d)\n", code);
			err = statusErr;
			break;
	}
        (void)dce;

	return set_dsk_err(err);
}
