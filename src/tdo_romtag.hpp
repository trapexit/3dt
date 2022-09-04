/*
  ISC License

  Copyright (c) 2021, Antonio SJ Musumeci <trapexit@spawn.link>

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#pragma once

#include <cstdint>

namespace TDO
{
  struct ROMTag
  {
    uint8_t sub_systype;
    uint8_t type;
    uint8_t version;
    uint8_t revision;
    uint8_t flags;
    uint8_t type_specific;
    uint8_t reserved1;
    uint8_t reserved2;
    uint32_t offset;
    uint32_t size;
    uint32_t reserved3[4];
  };
}


/********************************************************************************/
/* ROM tag defines for RSA-able or RSA related components on a CD		*/

#define RSANODE			0x0F	/* rt_SubSysType for such components	*/

/* rt_Type definitions for rt_SubSysType RSANODE				*/
#define RSA_MUST_RSA		0x01	/* rt_Reserved2 = end-of-block siglen	*/
#define RSA_BLOCKS_ALWAYS	0x02
#define RSA_BLOCKS_SOMETIMES	0x03
#define RSA_BLOCKS_RANDOM	0x04
#define RSA_SIGNATURE_BLOCK	0x05	/* Block of digest signatures		*/
#define RSA_BOOT		0x06	/* Old CD dipir tag			*/
#define RSA_OS			0x07	/* CD's version of sherry, operator, fs	*/
#define RSA_CDINFO		0x08	/* Optional mastering information	*/
					/* rt_Offset       = Zeros		*/
					/* rt_Size         = Zeros		*/
					/* rt_Reserved3[0] = Copy of VolumeUniqueId */
					/* rt_Reserved3[1] = Random number in case unique ID isn't */
					/* rt_Reserved3[2] = Date & time stamp	*/
					/* rt_Reserved3[3] = Reserved		*/
#define RSA_NEWBOOT		0x09	/* Old CD dipir tag, double key scheme	*/
#define RSA_NEWNEWBOOT		0x0A	/* Old CD dipir tag, cheezo-encrypted	*/
#define RSA_NEWNEWGNUBOOT	0x0B	/* Old CD dipir tag, doubly encrypted	*/
#define RSA_BILLSTUFF		0x0C	/* Something that Bill Duvall asked for	*/
#define RSA_NEWKNEWNEWGNUBOOT	0x0D	/* Current CD dipir, quadruple secure	*/
					/* This is ridiculous (which is good)!	*/
#define RSA_OLD_MISCCODE	0x0F	/* Old misc_code tag (C&B and Sampler)	*/
#define RSA_MISCCODE		0x10	/* Current misc_code tag		*/
#define RSA_APP			0x11	/* Start of app area on a CD		*/
#define RSA_DRIVER		0x12	/* Downloadable device drivers		*/
#define RSA_DEVDIPIR		0x13	/* Dipir driver for non-CD device	*/
#define RSA_APPSPLASH		0x14	/* App splash screen image		*/
#define RSA_DEPOTCONFIG		0x15	/* Depot configuration file		*/
#define RSA_DEVICE_INFO		0x16	/* Device ID & related info		*/
#define	RSA_DEV_PERMS		0x17	/* List of devices which we can use	*/
#define	RSA_BOOT_OVERLAY	0x18	/* Overlay module for RSA_NEW*BOOT	*/



/********************************************************************************/
/* ROM tag defines for components in a system ROM				*/

#define	RT_SUBSYS_ROM		0x10	/* rt_SubSysType for such components	*/

/* rt_Type definitions for rt_SubSysType RT_SUBSYS_ROM				*/
#define	ROM_NULL		0x0	/* NULL ROM component type		*/

/* Boot code related ROM rt_Types						*/
#define	ROM_DIAGNOSTICS		0x10	/* In-ROM diagnostics code		*/
#define	ROM_DIAG_LOADER		0x11	/* Loader for downloadable diagnostics	*/
#define	ROM_VER_STRING		0x12	/* Version string for static screen	*/
					/* rt_Flags[0] = Show flag (1 = show version string) */
					/* rt_Version = Licensee ID		*/
					/*  0x00 = Not specified		*/
					/*  0x01 = Creative			*/
					/* rt_Reserved1 = VCoord for string	*/
					/* rt_Reserved2 = HCoord for string	*/

/* Dipir related ROM rt_Types							*/
#define	ROM_DIPIR	  0x20    	/* System ROM dipir code		*/
/* rt_Reserved3[0] = RAM download addr	*/
#define	ROM_DIPIR_DRIVERS 0x21  	/* Various dipir device drivers		*/

/* OS related ROM rt_Types							*/
#define	ROM_KERNEL_ROM 0x30	       /* Reduced kernel for ROM apps		*/
/* rt_Reserved3[0] = RAM download addr */
#define	ROM_KERNEL_CD  0x31	      /* Full but misc-less kernel for titles	*/
/* rt_Reserved3[0] = RAM download addr	*/
#define	ROM_OPERATOR   0x32	/* Operator for ROM apps or titles	*/
/* rt_Reserved3[0] = RAM download addr */
#define	ROM_FS	       0x33	/* Fs for ROM apps or titles */
/* rt_Reserved3[0] = RAM download addr */

/* Configuration related ROM rt_Types */
#define ROM_SYSINFO	0x40	/* SYSINFO (system information code) */
#define	ROM_FS_IMAGE	0x41	/* Mountable ROM file system */
#define	ROM_PLATFORM_ID	0x42	/* ID for the ROM release(see sysinfo.h) */
#define	ROM_ROM2_BASE	0x43	/* Base Address of the 2nd ROM Bank */
