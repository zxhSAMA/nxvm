/* This file is a part of NXVM project. */

#include "stdio.h"
#include "stdarg.h"
#include "string.h"
#include "memory.h"

#include "vmachine.h"
#include "vapi.h"

/* Standard C Library */
char* STRCAT(char *_Dest, const char *_Source)
{return strcat(_Dest, _Source);}
char* STRCPY(char *_Dest, const char *_Source)
{return strcpy(_Dest, _Source);}
char* STRTOK(char *_Str, const char *_Delim)
{return strtok(_Str, _Delim);}
int STRCMP(const char *_Str1, const char *_Str2)
{return strcmp(_Str1, _Str2);}
int SPRINTF(char *_Dest, const char *_Format, ...)
{
	int nWrittenBytes = 0;
	va_list arg_ptr;
	va_start(arg_ptr, _Format);
	nWrittenBytes = vsprintf(_Dest, _Format, arg_ptr);
	va_end(arg_ptr);
	return nWrittenBytes;
}
FILE* FOPEN(const char *_Filename, const char *_Mode)
{return fopen(_Filename, _Mode);}
char* FGETS(char *_Buf, int _MaxCount, FILE *_File)
{return fgets(_Buf, _MaxCount, _File);}

/* General Functions */
t_nubit32 vapiPrint(const t_string format, ...)
{
	t_nubit32 nWrittenBytes = 0;
	va_list arg_ptr;
	va_start(arg_ptr, format);
	nWrittenBytes = vfprintf(stdout, format,arg_ptr);
	//nWrittenBytes = vsprintf(stringBuffer,format,arg_ptr);
	va_end(arg_ptr);
	fflush(stdout);
	return nWrittenBytes;
}
void vapiPrintIns(t_nubit16 segment, t_nubit16 offset, t_string ins)
{
	vapiPrint("%04X:%04X  %s\n",segment,offset,ins);
}

/* Record */

t_apirecord vapirecord;

#define _expression "cs:ip=%x:%x opcode=%x %x %x %x %x %x %x %x \
ax=%x bx=%x cx=%x dx=%x sp=%x bp=%x si=%x di=%x ds=%x es=%x ss=%x \
of=%1x sf=%1x zf=%1x cf=%1x af=%1x pf=%1x df=%1x if=%1x tf=%1x\n"
#define _rec (vapirecord.rec[(i + vapirecord.start) % VAPI_RECORD_SIZE])
#define _rec_of    (GetBit(_rec.flags, VCPU_FLAG_OF))
#define _rec_sf    (GetBit(_rec.flags, VCPU_FLAG_SF))
#define _rec_zf    (GetBit(_rec.flags, VCPU_FLAG_ZF))
#define _rec_cf    (GetBit(_rec.flags, VCPU_FLAG_CF))
#define _rec_af    (GetBit(_rec.flags, VCPU_FLAG_AF))
#define _rec_pf    (GetBit(_rec.flags, VCPU_FLAG_PF))
#define _rec_df    (GetBit(_rec.flags, VCPU_FLAG_DF))
#define _rec_tf    (GetBit(_rec.flags, VCPU_FLAG_TF))
#define _rec_if    (GetBit(_rec.flags, VCPU_FLAG_IF))

void vapiRecordDump(const t_string fname)
{
	t_nubitcc i = 0;
	FILE *dump = FOPEN(fname, "w");
	if (!dump) {
		vapiPrint("ERROR:\tcannot write dump file.\n");
		return;
	}
	if (!vapirecord.size) {
		vapiPrint("ERROR:\tno record to dump.\n");
		return;
	}
	while (i < vapirecord.size) {
		fprintf(dump, _expression,
			_rec.cs, _rec.ip,
			vramVarByte(_rec.cs,_rec.ip+0),vramVarByte(_rec.cs,_rec.ip+1),
			vramVarByte(_rec.cs,_rec.ip+2),vramVarByte(_rec.cs,_rec.ip+3),
			vramVarByte(_rec.cs,_rec.ip+4),vramVarByte(_rec.cs,_rec.ip+5),
			vramVarByte(_rec.cs,_rec.ip+6),vramVarByte(_rec.cs,_rec.ip+7),
			_rec.ax,_rec.bx,_rec.cx,_rec.dx,
			_rec.sp,_rec.bp,_rec.si,_rec.di,
			_rec.ds,_rec.es,_rec.ss,
			_rec_of,_rec_sf,_rec_zf,_rec_cf,
			_rec_af,_rec_pf,_rec_df,_rec_if,_rec_tf);
		++i;
	}
	vapiPrint("Record dumped to '%s'.\n", fname);
	fclose(dump);
}
void vapiRecordStart()
{
	vapirecord.start = 0;
	vapirecord.size = 0;
}
void vapiRecordExec()
{
#if VAPI_RECORD_SELECT_FIRST == 1
	if (vapirecord.size == VAPI_RECORD_SIZE) {
		vmachine.flagrecord = 0x00;
		return;
	}
#endif
	vapirecord.rec[(vapirecord.start + vapirecord.size) % VAPI_RECORD_SIZE]
		= vcpu;
	if (vapirecord.size == VAPI_RECORD_SIZE)
		vapirecord.start++;
	else vapirecord.size++;
}
void vapiRecordEnd() {}

/* Disk */
#include "vfdd.h"
void vapiFloppyInsert(const t_string fname)
{
	t_nubitcc count;
	FILE *image = FOPEN(fname, "rb");
	if (image && vfdd.base) {
		count = fread((void *)vfdd.base, sizeof(t_nubit8), vfddGetImageSize, image);
		vfdd.flagexist = 0x01;
		fclose(image);
		vapiPrint("Floppy disk inserted.\n");
	} else
		vapiPrint("Cannot read floppy image from '%s'.\n", fname);
}
void vapiFloppyRemove(const t_string fname)
{
	t_nubitcc count;
	FILE *image;
	if (fname) {
		image = FOPEN(fname, "wb");
		if(image) {
			if (!vfdd.flagro)
				count = fwrite((void *)vfdd.base, sizeof(t_nubit8), vfddGetImageSize, image);
			vfdd.flagexist = 0x00;
			fclose(image);
		} else {
			vapiPrint("Cannot write floppy image to '%s'.\n", fname);
			return;
		}
	}
	vfdd.flagexist = 0x00;
	memset((void *)vfdd.base, 0x00, vfddGetImageSize);
	vapiPrint("Floppy disk removed.\n");
}
#include "vhdd.h"
void vapiHardDiskInsert(const t_string fname)
{
	t_nubitcc count;
	FILE *image = FOPEN(fname, "rb");
	if (image) {
		fseek(image, 0, SEEK_END);
		count = ftell(image);
		vhdd.ncyl = (t_nubit16)(count / vhdd.nhead / vhdd.nsector / vhdd.nbyte);
		fseek(image, 0, SEEK_SET);
		vhddAlloc();
		count = fread((void *)vhdd.base, sizeof(t_nubit8), vhddGetImageSize, image);
		vhdd.flagexist = 0x01;
		fclose(image);
		vapiPrint("Hard disk connected.\n");
	} else
		vapiPrint("Cannot read hard disk image from '%s'.\n", fname);
}
void vapiHardDiskRemove(const t_string fname)
{
	t_nubitcc count;
	FILE *image;
	if (fname) {
		image = FOPEN(fname, "wb");
		if(image) {
			if (!vhdd.flagro)
				count = fwrite((void *)vhdd.base, sizeof(t_nubit8), vhddGetImageSize, image);
			vhdd.flagexist = 0x00;
			fclose(image);
		} else {
			vapiPrint("Cannot write hard disk image to '%s'.\n", fname);
			return;
		}
	}
	vhdd.flagexist = 0x00;
	memset((void *)vhdd.base, 0x00, vhddGetImageSize);
	vapiPrint("Hard disk removed.\n");
}

/* Platform Related */
#if VGLOBAL_PLATFORM == VGLOBAL_VAR_WIN32
	#include "system/win32.h"
	void vapiSleep(t_nubit32 milisec) {win32Sleep(milisec);}
	void vapiDisplaySetScreen() {win32DisplaySetScreen(vmachine.flagmode);}
	void vapiDisplayPaint() {win32DisplayPaint(vmachine.flagmode);}
	void vapiStartMachine() {win32StartMachine(vmachine.flagmode);}
#elif VGLOBAL_PLATFORM == VGLOBAL_VAR_LINUX
	#include "system/linux.h"
	void vapiSleep(t_nubit32 milisec) {linuxSleep(milisec);}
	void vapiDisplaySetScreen() {linuxDisplaySetScreen();}
	void vapiDisplayPaint() {linuxDisplayPaint(0x01);}
	void vapiStartMachine() {linuxStartMachine();}
#endif

void vapiInit() {memset(&vapirecord, 0x00, sizeof(t_apirecord));}
void vapiFinal() {}
