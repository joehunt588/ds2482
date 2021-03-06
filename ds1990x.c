/*
 * Copyright 2018-19 AM Maree/KSS Technologies (Pty) Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/*
 * ds1990x.c
 */

#include	"x_config.h"

#if		(halHAS_DS2482_100 == 1 || halHAS_DS2482_800 == 1) && (halHAS_DS1990X == 1)

#include	"ds1990x.h"
#include	"ds2482.h"

#include	"task_events.h"

#include	"syslog.h"
#include	"printfx.h"
#include	"x_errors_events.h"
#include	"systiming.h"								// timing debugging

#include	<stdint.h>
#include	<string.h>

#define	debugFLAG					0xC000

#define	debugTIMING					(debugFLAG & 0x0001)

#define	debugTRACK					(debugFLAG & 0x2000)
#define	debugPARAM					(debugFLAG & 0x4000)
#define	debugRESULT					(debugFLAG & 0x8000)

// ###################################### General macros ###########################################

/* In order to avoid multiple successive reads of the same iButton on the same OW channel
 * we filter reads based on the value of the iButton read and time expired since the last
 * successful read. If the same ID is read on the same channel within 'x' seconds, skip it */
#if		(halHAS_DS2482_800 == 1)
	ow_rom_t	LastROM[ds2482NUM_CHAN]		= { 0 } ;
	seconds_t	LastRead[ds2482NUM_CHAN]	= { 0 } ;
#elif	(halHAS_DS2482_100 == 1 && ESP32_VARIANT == ESP32_VAR_WROVERKIT) // breakout on ESP32-WROVER-KIT or M5FIRE ?
	ow_rom_t	LastROM		= { 0 } ;
	seconds_t	LastRead	= 0 ;
#endif
uint8_t		Family01Count = 0 ;
uint8_t		OWdelay	= ds1990READ_INTVL ;

// ################################# Application support functions #################################

int32_t	ds1990xHandleRead(int32_t iCount, void * pVoid) {
	/* To avoid registering multiple reads if iButton is held in place too long we enforce a
	 * period of 'x' seconds within which successive reads of the same tag will be ignored */
	seconds_t	NowRead = xTimeStampAsSeconds(sTSZ.usecs) ;
#if		(halHAS_DS2482_800 == 1)
	#if		(ESP32_VARIANT == ESP32_VAR_AC00)
	uint8_t	Chan = OWremapTable[sDS2482.CurChan] ;
	#elif	(ESP32_VARIANT == ESP32_VAR_AC01)
	uint8_t	Chan = sDS2482.CurChan ;
	#endif
	if ((LastROM[Chan].Value == sDS2482.ROM.Value) && (NowRead - LastRead[Chan]) <= OWdelay) {
		IF_PRINT(debugTRACK, "SAME iButton in 5sec, Skipped...\n") ;
		return erSUCCESS ;
	}
	LastROM[Chan].Value = sDS2482.ROM.Value ;
	LastRead[Chan]		= NowRead ;
	xTaskNotify(EventsHandle, 1UL << (Chan + se1W_FIRST), eSetBits) ;

#elif	(halHAS_DS2482_100 == 1 && ESP32_VARIANT == ESP32_VAR_WROVERKIT) // breakout on ESP32-WROVER-KIT or M5FIRE ?
	if ((LastROM.Value == sDS2482.ROM.Value) && (NowRead - LastRead) <= OWdelay) {
		IF_PRINT(debugTRACK, "SAME iButton in 5sec, Skipped...\n") ;
		return erSUCCESS ;
	}
	LastROM.Value	= sDS2482.ROM.Value ;
	LastRead		= NowRead ;
	xTaskNotify(EventsHandle, 1UL << se1W_FIRST, eSetBits) ;

#else
	#warning "Should this code be included ???"
#endif
	portYIELD() ;
	IF_PRINT(debugTRACK, "NEW iButton Read, or >5sec passed\n") ;
	IF_EXEC_1(debugTRACK, ds2482PrintROM, &sDS2482.ROM) ;
	return erSUCCESS ;
}

// ################### Identification, Diagnostics & Configuration functions #######################

int32_t	ds1990xDiscover(void) {
	if (Family01Count) {
		IF_PRINT(debugTRACK, "Family01 Count=%d\n", Family01Count) ;
		IF_SYSTIMER_INIT(debugTIMING, systimerDS18X20, systimerTICKS, "DS18X20", myMS_TO_TICKS(10), myMS_TO_TICKS(1000)) ;
	}
	return erSUCCESS ;
}

#endif
