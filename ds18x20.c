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
 * ds18x20.c
 */

#include	"ds2482/ds18x20.h"
#include	"ds2482/ds2482.h"

#include	"x_debug.h"
#include	"x_errors_events.h"
#include	"x_systiming.h"					// timing debugging
#include	"x_syslog.h"
#include	"x_values_convert.h"
#include	"actuators.h"

#if		(halHAS_PCA9555 == 1)
	#include	"pca9555/pca9555.h"
#endif

#include	<string.h>

#define	debugFLAG					0xE003

#define	debugTIMING					(debugFLAG & 0x0001)
#define	debugDS18X20				(debugFLAG & 0x0002)

#define	debugTRACK					(debugFLAG & 0x2000)
#define	debugPARAM					(debugFLAG & 0x4000)
#define	debugRESULT					(debugFLAG & 0x8000)

ds18x20_t *	psDS18X20	= NULL ;
complex_t	sDS18X20Func = { .read = ds18x20GetTemperature, .mode = NULL } ;

// ############################ Forward declaration of local functions #############################

void	ds18x20EnableExtPSU(ds18x20_t * psDS18X20) ;
void	ds18x20DisableExtPSU(ds18x20_t * psDS18X20) ;
int32_t	ds18x20Discover(void)  ;

// ############################### ds18x20 (Family 10 & 28) support ################################

void	ds18x20PrintInfo(ds18x20_t * psDS18X20) {
	ds2482PrintROM(&psDS18X20->ROM) ;
	xprintf("  Tlsb=%02X  Tmsb=%02X  Thi=%02X  Tlo=%02X",
			psDS18X20->fam10.Tlsb, psDS18X20->fam10.Tmsb, psDS18X20->fam10.Thi, psDS18X20->fam10.Tlo) ;
	if (psDS18X20->ROM.Family == OWFAMILY_28) {
		xprintf("  Conf=%02X", psDS18X20->fam28.Conf) ;
	}
	xprintf("\n") ;
}

void	ds18x20EnableExtPSU(ds18x20_t * psDS18X20) {
	xActuatorBlock(psDS18X20->Ch) ;
	vActuateSetLevelDIG(psDS18X20->Ch, 1) ;
	pca9555DIG_OUT_WriteAll() ;
}

void	ds18x20DisableExtPSU(ds18x20_t * psDS18X20) {
	vActuateSetLevelDIG(psDS18X20->Ch, 0) ;
	pca9555DIG_OUT_WriteAll() ;
	xActuatorUnBlock(psDS18X20->Ch) ;
}

int32_t	ds18x20SelectAndAddress(ds18x20_t * psDS18X20) {
	IF_myASSERT(debugPARAM, INRANGE_SRAM(psDS18X20)) ;
	int32_t iRV = ds2482ChannelSelect(psDS18X20->Ch) ;
	IF_myASSERT(debugRESULT, iRV == erSUCCESS) ;

	iRV = OWReset() ;									// check if any device is there
	IF_myASSERT(debugRESULT, iRV == 1) ;

//	iRV = DS2482SetOverDrive() ;
//	IF_myASSERT(debugRESULT, iRV > erFAILURE) ;

	memcpy(&sDS2482.ROM, &psDS18X20->ROM, sizeof(ow_rom_t)) ;
	OWAddress(OW_CMD_MATCHROM) ;						// select the applicable device
	return 1 ;
}

int32_t	ds18x20ReadScratchPad(ds18x20_t * psDS18X20) {
	int32_t iRV, xCount = 0 ;
	do {
		iRV = ds18x20SelectAndAddress(psDS18X20) ;
		IF_myASSERT(debugRESULT, iRV == 1) ;

		iRV = OWWriteByteWait(DS18X20_READ_SP) ;		// request to read the scratch pad
		IF_myASSERT(debugRESULT, iRV > erFAILURE) ;

		memset(psDS18X20->RegX, 0xFF, sizeof(struct fam10)) ;	// preset all=0xFF to read
		OWBlock(psDS18X20->RegX, sizeof(struct fam10)) ;		// read the scratch pad
		iRV = OWCheckCRC(psDS18X20->RegX, SIZEOF_MEMBER(ds18x20_t, RegX)) ;
		IF_PRINT(debugRESULT, "SP Read: CRC=%02X %s %-'+b",
				psDS18X20->fam10.CRC, iRV ? "Passed" : "FAILED!!!",
				sizeof(struct fam10), psDS18X20->RegX) ;
		if (iRV == 0) { vTaskDelay(pdMS_TO_TICKS(20)) ; }
	} while (iRV != 1 && xCount++ < 10) ;
	return iRV ;
}

int32_t	ds18x20WriteScratchPad(ds18x20_t * psDS18X20) {
	int32_t iRV = ds18x20SelectAndAddress(psDS18X20) ;
	IF_myASSERT(debugRESULT, iRV == 1) ;

	iRV = OWWriteByteWait(DS18X20_WRITE_SP) ;// request to write the scratch pad
	IF_myASSERT(debugRESULT, iRV > erFAILURE) ;

	OWBlock(&psDS18X20->fam28.Thi, psDS18X20->ROM.Family == OWFAMILY_28 ? 3 : 2) ;	// Thi, Tlo [+Conf]
	IF_PRINT(debugDS18X20, "SP Write: %-'+b", psDS18X20->ROM.Family == OWFAMILY_28 ? 3 : 2, &psDS18X20->fam28.Thi) ;
	return 1 ;
}

int32_t	ds18x20CopyScratchPad(ds18x20_t * psDS18X20) {
	int32_t iRV = ds18x20SelectAndAddress(psDS18X20) ;
	IF_myASSERT(debugRESULT, iRV == 1) ;

#if		(DS18X20_EXT_POWER == 0)
	OWWriteBytePower(DS18X20_COPY_SP) ;		// request to write scratch pad to EE
	IF_myASSERT(debugRESULT, sDS2482.Regs.SPU == 1) ;

	vTaskDelay(pdMS_TO_TICKS(11)) ;						// keep SPU=1 for at least 10mS

	OWLevel(owMODE_STANDARD) ;				// then make SPU=0
	IF_myASSERT(debugRESULT, sDS2482.Regs.SPU == 0) ;
#else
	OWWriteByte(DS18X20_COPY_SP) ;
#endif
	return 1 ;
}

void	ds18x20ConvertAll(void) {
	for (int32_t Idx = 0; Idx < FamilyCount[idxOWFAM10_28]; ++Idx) {
		int32_t	iRV = ds2482ChannelSelect(psDS18X20[Idx].Ch) ;
		IF_myASSERT(debugRESULT, iRV > erFAILURE) ;

		iRV = OWReset() ;								// check if any device is there
		IF_myASSERT(debugRESULT, iRV == 1) ;

		iRV = OWWriteByteWait(OW_CMD_SKIPROM) ;// address all devices on bus
		IF_myASSERT(debugRESULT, iRV > erFAILURE) ;

#if		(DS18X20_EXT_POWER == 0)
		OWWriteBytePower(DS18X20_CONVERT) ;	// Trigger temperature conversion & SPU
		IF_myASSERT(debugRESULT, sDS2482.Regs.SPU == 1) ;
		vTaskDelay(pdMS_TO_TICKS(760)) ;
#else
		OWWriteByte(DS18X20_CONVERT) ;
#endif
	}

#if		(DS18X20_EXT_POWER == 0)
	for (int32_t Idx = 0; Idx < FamilyCount[idxOWFAM10_28]; ++Idx) {
		OWLevel(owMODE_STANDARD) ;
		IF_myASSERT(debugRESULT, sDS2482.Regs.SPU == 0) ;
	}
#endif
}

float	ds18x20GetTemperature(int32_t Idx) { return psDS18X20[Idx].xVal.f32 ; }

void	ds18x20ConvertTemperature(ds18x20_t * psDS18X20) {
	int32_t	iRV = ds18x20SelectAndAddress(psDS18X20) ;
	IF_myASSERT(debugRESULT, iRV == 1) ;
#if		(DS18X20_EXT_POWER == 0)
	OWWriteBytePower(DS18X20_CONVERT) ;	// Trigger temperature conversion & SPU
	IF_myASSERT(debugRESULT, sDS2482.Regs.SPU == 1) ;
	vTaskDelay(pdMS_TO_TICKS(752)) ;
	OWLevel(owMODE_STANDARD) ;
	IF_myASSERT(debugRESULT, sDS2482.Regs.SPU == 0) ;
#else
	OWWriteByte(DS18X20_CONVERT) ;		// Trigger temperature conversion
#endif
}

/**
 * ds18x20ReadTemperature() - Select, Read SP, Convert value & store
 * @param psDS18X20
 * @return
 */
int32_t	ds18x20ReadTemperature(ds18x20_t * psDS18X20) {
	int32_t	iRV = ds18x20SelectAndAddress(psDS18X20) ;
	IF_myASSERT(debugRESULT, iRV == 1) ;

	ds18x20ReadScratchPad(psDS18X20) ;

	// convert & store the temperature
	iRV = xConvert2sComp((psDS18X20->RegX[ds18x20MSB] << 8) | psDS18X20->RegX[ds18x20LSB], 9 + psDS18X20->Res) ;
	psDS18X20->xVal.f32 = (float) iRV / (2 << psDS18X20->Res) ;
	IF_PRINT(debugDS18X20, "%02X/%#M/%02X  Raw=%d  Val=%f\n",
			psDS18X20->ROM.Family, psDS18X20->ROM.TagNum, psDS18X20->ROM.CRC, iRV, psDS18X20->xVal.f32) ;
	return iRV ;
}

/**
 * ds18x20ConvertAndReadAll()
 * @brief	To trigger temperature conversion for FAM10 & FAM28 the same command is used.
 * @param	psEpWork
 * @return
 */
int32_t	ds18x20ConvertAndReadAll(ep_work_t * psEpWork) {
	if (FamilyCount[idxOWFAM10_28]) {
#if		0
		TIMETRACK("Convert Start\n") ;
		ds18x20ConvertAll() ;
		for (int32_t Idx = 0; Idx < psEpWork->Var.varDef.cv.varcount; ds18x20ReadTemperature(&psDS18X20[Idx++])) ;
		TIMETRACK("Convert End\n") ;
#elif	(DS18X20_EXT_POWER == 1)
		TIMETRACK("Convert Start\n") ;
		for (int32_t Idx = 0; Idx < psEpWork->Var.varDef.cv.varcount; ++Idx) {
			IF_SYSTIMER_START(debugTIMING, systimerDS18X20) ;
			ds18x20ConvertTemperature(&psDS18X20[Idx]) ;
			ds18x20ReadTemperature(&psDS18X20[Idx]) ;
			IF_SYSTIMER_STOP(debugTIMING, systimerDS18X20) ;
		}
		TIMETRACK("Convert End\n") ;
#endif
	}
	return erSUCCESS ;
}

int32_t	ds18x20Enumerate(int32_t Idx) {
	IF_myASSERT(debugPARAM, Idx < FamilyCount[idxOWFAM10_28]) ;
	ep_info_t	sEpInfo ;
	vEpGetInfoWithIndex(&sEpInfo, URI_DS18X20) ;		// setup pointers to static and work tables
	IF_myASSERT(debugRESULT, sEpInfo.pEpStatic && sEpInfo.pEpWork) ;

	// Do once-off initialization for work structure entries
	if (sEpInfo.pEpWork->Var.varDef.cv.varcount == 0) {
		sEpInfo.pEpWork->Var.varDef.cv.pntr	= 1 ;
		sEpInfo.pEpWork->Var.varVal.pvoid	= &sDS18X20Func ;
	}

	ds18x20_t * psDS18Xtemp = &psDS18X20[Idx] ;
	// Save the address info of the device just enumerated
	memcpy(&psDS18Xtemp->ROM, &sDS2482.ROM, sizeof(ow_rom_t)) ;
	psDS18Xtemp->Ch		= sDS2482.CurChan ;
	psDS18Xtemp->Idx	= Idx ;

	IF_EXEC_1(DS18X20_EXT_POWER, ds18x20EnableExtPSU, psDS18Xtemp) ;
	ds18x20ReadScratchPad(psDS18Xtemp) ;

	if (sDS2482.ROM.Family == OWFAMILY_28) {
		if ((psDS18Xtemp->fam28.Conf & 0x60) == 0x60) {
			psDS18Xtemp->fam28.Conf &= ~0x60 ;			// change to 9-bit mode
			ds18x20WriteScratchPad(psDS18Xtemp) ;
			ds18x20CopyScratchPad(psDS18Xtemp) ;
		}
		psDS18Xtemp->Res	= owFAM28_RES9B ;				// Changed 12 -> 9 bit resolution
	} else {
		psDS18Xtemp->Res	= owFAM28_RES9B ;				// default 9 bit resolution
	}
	sEpInfo.pEpWork->Var.varDef.cv.varcount++ ;			// Update work table number of devices enumerated
	return erSUCCESS ;
}

int32_t	ds18x20Discover(void) {
	psDS18X20 = malloc(FamilyCount[idxOWFAM10_28] * sizeof(ds18x20_t)) ;
	IF_myASSERT(debugRESULT, INRANGE_SRAM(psDS18X20)) ;

	IF_PRINT(debugDS18X20, "AutoEnum DS18X20: ") ;
	int32_t iRV = ds2482ScanAndCall(OWFAMILY_10, ds18x20Enumerate) ;
	iRV += ds2482ScanAndCall(OWFAMILY_28, ds18x20Enumerate) ;
	IF_PRINT(debugDS18X20, "\n") ;

	if (iRV != FamilyCount[idxOWFAM10_28]) {
		SL_ERR("Only %d of%d enumerated!!!", iRV, FamilyCount[idxOWFAM10_28]) ;
	}
	return erSUCCESS ;
}