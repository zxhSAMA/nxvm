/* This file is a part of NXVM project. */

#include "stdio.h"

#include "debug/record.h"
#include "vcpuapi.h"
#include "vcpuins.h"

static t_cpu oldbcpu, newbcpu;
static t_bool flagbrec;
static t_cpurec bcpurec;

#define VCPUAPI_COMPARE 1
#define VCPUAPI_RECORD  0

t_nubit32 vcpuapiPrint(const t_string format, ...)
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

#ifdef VGLOBAL_BOCHS
static t_bool flagvalid = 0;
#define NEED_CPU_REG_SHORTCUTS 1
#include "d:/bochs-2.6/bochs.h"
#include "d:/bochs-2.6/cpu/cpu.h"

void vapiCallBackMachineStop() {}
void vapiSleep(t_nubit32 milisec) {}
void vapiCallBackDebugPrintRegs(t_bool bit32) {vcpuapiPrintReg(&vcpu);}

static void LoadSreg(t_cpu_sreg *rsreg, bx_dbg_sreg_t *rbsreg)
{
	t_nubit64 cdesc;
	rsreg->selector = rbsreg->sel;
	rsreg->flagvalid = rbsreg->valid;
	cdesc = ((t_nubit64)rbsreg->des_h << 32) | rbsreg->des_l;
	rsreg->dpl = (t_nubit4)_GetDesc_DPL(cdesc);
	if (_IsDescUser(cdesc)) {
		rsreg->base = (t_nubit32)_GetDescSeg_Base(cdesc);
		rsreg->limit = (t_nubit32)((_IsDescSegGranularLarge(cdesc) ?
			((_GetDescSeg_Limit(cdesc) << 12) | 0x0fff) : (_GetDescSeg_Limit(cdesc))));
		rsreg->seg.accessed = _IsDescUserAccessed(cdesc);
		rsreg->seg.executable = _IsDescCode(cdesc);
		if (rsreg->seg.executable) {
			rsreg->seg.exec.conform = _IsDescCodeConform(cdesc);
			rsreg->seg.exec.defsize = _IsDescCode32(cdesc);
			rsreg->seg.exec.readable = _IsDescCodeReadable(cdesc);
		} else {
			rsreg->seg.data.big = _IsDescDataBig(cdesc);
			rsreg->seg.data.expdown = _IsDescDataExpDown(cdesc);
			rsreg->seg.data.writable = _IsDescDataWritable(cdesc);
		}
	} else {
		rsreg->base = (t_nubit32)_GetDescSeg_Base(cdesc);
		rsreg->limit = (t_nubit32)((_IsDescSegGranularLarge(cdesc) ?
				(_GetDescSeg_Limit(cdesc) << 12 | 0x0fff) : (_GetDescSeg_Limit(cdesc))));
		rsreg->sys.type = (t_nubit4)_GetDesc_Type(cdesc);
	}
}

static void CopyBochsCpu(t_cpu *rcpu)
{
	bx_dbg_sreg_t bsreg;
	bx_dbg_global_sreg_t bgsreg;
	rcpu->eax = EAX;
	rcpu->ecx = ECX;
	rcpu->edx = EDX;
	rcpu->ebx = EBX;
	rcpu->esp = ESP;
	rcpu->ebp = EBP;
	rcpu->esi = ESI;
	rcpu->edi = EDI;
	rcpu->eip = EIP;
	rcpu->eflags = BX_CPU_THIS_PTR read_eflags();

	rcpu->es.sregtype = SREG_DATA;
	BX_CPU_THIS_PTR dbg_get_sreg(&bsreg, 0);
	LoadSreg(&rcpu->es, &bsreg);

	rcpu->cs.sregtype = SREG_CODE;
	BX_CPU_THIS_PTR dbg_get_sreg(&bsreg, 1);
	LoadSreg(&rcpu->cs, &bsreg);

	rcpu->ss.sregtype = SREG_STACK;
	BX_CPU_THIS_PTR dbg_get_sreg(&bsreg, 2);
	LoadSreg(&rcpu->ss, &bsreg);

	rcpu->ds.sregtype = SREG_DATA;
	BX_CPU_THIS_PTR dbg_get_sreg(&bsreg, 3);
	LoadSreg(&rcpu->ds, &bsreg);

	rcpu->fs.sregtype = SREG_DATA;
	BX_CPU_THIS_PTR dbg_get_sreg(&bsreg, 4);
	LoadSreg(&rcpu->fs, &bsreg);

	rcpu->gs.sregtype = SREG_DATA;
	BX_CPU_THIS_PTR dbg_get_sreg(&bsreg, 5);
	LoadSreg(&rcpu->gs, &bsreg);

	rcpu->ldtr.sregtype = SREG_LDTR;
	BX_CPU_THIS_PTR dbg_get_ldtr(&bsreg);
	LoadSreg(&rcpu->ldtr, &bsreg);
	
	rcpu->tr.sregtype = SREG_TR;
	BX_CPU_THIS_PTR dbg_get_tr(&bsreg);
	LoadSreg(&rcpu->tr, &bsreg);

	rcpu->gdtr.sregtype = SREG_GDTR;
	BX_CPU_THIS_PTR dbg_get_gdtr(&bgsreg);
	rcpu->gdtr.base = GetMax32(bgsreg.base);
	rcpu->gdtr.limit = bgsreg.limit;

	rcpu->idtr.sregtype = SREG_IDTR;
	BX_CPU_THIS_PTR dbg_get_idtr(&bgsreg);
	rcpu->idtr.base = GetMax32(bgsreg.base);
	rcpu->idtr.limit = bgsreg.limit;
	
	rcpu->cr0 = BX_CPU_THIS_PTR cr0.get32();
	rcpu->cr2 = GetMax32(BX_CPU_THIS_PTR cr2);
	rcpu->cr3 = GetMax32(BX_CPU_THIS_PTR cr3);
}
static t_bool vcpuapiCheckDiff()
{
	t_nubitcc i, j;
	t_bool flagdiff = 0x00;
	t_nubit32 mask = vcpuins.udf;
	if (!vcpu.flagignore) {
		if (vcpu.cr0 != newbcpu.cr0) {vcpuapiPrint("diff cr0\n");flagdiff = 0x01;}
		if (vcpu.cr2 != newbcpu.cr2) {vcpuapiPrint("diff cr2\n");flagdiff = 0x01;}
		if (vcpu.cr3 != newbcpu.cr3) {vcpuapiPrint("diff cr3\n");flagdiff = 0x01;}
		if (vcpu.eax != newbcpu.eax) {vcpuapiPrint("diff eax\n");flagdiff = 0x01;}
		if (vcpu.ebx != newbcpu.ebx) {vcpuapiPrint("diff ebx\n");flagdiff = 0x01;}
		if (vcpu.ecx != newbcpu.ecx) {vcpuapiPrint("diff ecx\n");flagdiff = 0x01;}
		if (vcpu.edx != newbcpu.edx) {vcpuapiPrint("diff edx\n");flagdiff = 0x01;}
		if (vcpu.esp != newbcpu.esp) {vcpuapiPrint("diff esp\n");flagdiff = 0x01;}
		if (vcpu.ebp != newbcpu.ebp) {vcpuapiPrint("diff ebp\n");flagdiff = 0x01;}
		if (vcpu.esi != newbcpu.esi) {vcpuapiPrint("diff esi\n");flagdiff = 0x01;}
		if (vcpu.edi != newbcpu.edi) {vcpuapiPrint("diff edi\n");flagdiff = 0x01;}
		if (vcpu.eip != newbcpu.eip) {vcpuapiPrint("diff eip\n");flagdiff = 0x01;}
		if (vcpu.es.selector != newbcpu.es.selector ||
			(vcpu.es.flagvalid && (
			vcpu.es.base != newbcpu.es.base ||
			vcpu.es.limit != newbcpu.es.limit ||
			vcpu.es.dpl != newbcpu.es.dpl))) {
				vcpuapiPrint("diff es (V=%04X/%08X/%08X/%1X, B=%04X/%08X/%08X/%1X)\n",
					vcpu.es.selector, vcpu.es.base, vcpu.es.limit, vcpu.es.dpl,
					newbcpu.es.selector, newbcpu.es.base, newbcpu.es.limit, newbcpu.es.dpl);
				flagdiff = 0x01;
		}
		if (vcpu.cs.selector != newbcpu.cs.selector ||
			vcpu.cs.base != newbcpu.cs.base ||
			vcpu.cs.limit != newbcpu.cs.limit ||
			vcpu.cs.dpl != newbcpu.cs.dpl) {
				vcpuapiPrint("diff cs (V=%04X/%08X/%08X/%1X, B=%04X/%08X/%08X/%1X)\n",
					vcpu.cs.selector, vcpu.cs.base, vcpu.cs.limit, vcpu.cs.dpl,
					newbcpu.cs.selector, newbcpu.cs.base, newbcpu.cs.limit, newbcpu.cs.dpl);
				flagdiff = 0x01;
		}
		if (vcpu.ss.selector != newbcpu.ss.selector ||
			vcpu.ss.base != newbcpu.ss.base ||
			vcpu.ss.limit != newbcpu.ss.limit ||
			vcpu.ss.dpl != newbcpu.ss.dpl) {
				vcpuapiPrint("diff ss (V=%04X/%08X/%08X/%1X, B=%04X/%08X/%08X/%1X)\n",
					vcpu.ss.selector, vcpu.ss.base, vcpu.ss.limit, vcpu.ss.dpl,
					newbcpu.ss.selector, newbcpu.ss.base, newbcpu.ss.limit, newbcpu.ss.dpl);
				flagdiff = 0x01;
		}
		if (vcpu.ds.selector != newbcpu.ds.selector ||
			(vcpu.ds.flagvalid && (
			vcpu.ds.base != newbcpu.ds.base ||
			vcpu.ds.limit != newbcpu.ds.limit ||
			vcpu.ds.dpl != newbcpu.ds.dpl))) {
				vcpuapiPrint("diff ds (V=%04X/%08X/%08X/%1X, B=%04X/%08X/%08X/%1X)\n",
					vcpu.ds.selector, vcpu.ds.base, vcpu.ds.limit, vcpu.ds.dpl,
					newbcpu.ds.selector, newbcpu.ds.base, newbcpu.ds.limit, newbcpu.ds.dpl);
				flagdiff = 0x01;
		}
		if (vcpu.fs.selector != newbcpu.fs.selector ||
			(vcpu.fs.flagvalid && (
			vcpu.fs.base != newbcpu.fs.base ||
			vcpu.fs.limit != newbcpu.fs.limit ||
			vcpu.fs.dpl != newbcpu.fs.dpl))) {
				vcpuapiPrint("diff fs (V=%04X/%08X/%08X/%1X, B=%04X/%08X/%08X/%1X)\n",
					vcpu.fs.selector, vcpu.fs.base, vcpu.fs.limit, vcpu.fs.dpl,
					newbcpu.fs.selector, newbcpu.fs.base, newbcpu.fs.limit, newbcpu.fs.dpl);
				flagdiff = 0x01;
		}
		if (vcpu.gs.selector != newbcpu.gs.selector ||
			(vcpu.gs.flagvalid && (
			vcpu.gs.base != newbcpu.gs.base ||
			vcpu.gs.limit != newbcpu.gs.limit ||
			vcpu.gs.dpl != newbcpu.gs.dpl))) {
				vcpuapiPrint("diff gs (V=%04X/%08X/%08X/%1X, B=%04X/%08X/%08X/%1X)\n",
					vcpu.gs.selector, vcpu.gs.base, vcpu.gs.limit, vcpu.gs.dpl,
					newbcpu.gs.selector, newbcpu.gs.base, newbcpu.gs.limit, newbcpu.gs.dpl);
				flagdiff = 0x01;
		}
		if (vcpu.tr.selector != newbcpu.tr.selector ||
			vcpu.tr.base != newbcpu.tr.base ||
			vcpu.tr.limit != newbcpu.tr.limit ||
			vcpu.tr.dpl != newbcpu.tr.dpl) {
				vcpuapiPrint("diff tr (V=%04X/%08X/%08X/%1X, B=%04X/%08X/%08X/%1X)\n",
					vcpu.tr.selector, vcpu.tr.base, vcpu.tr.limit, vcpu.tr.dpl,
					newbcpu.tr.selector, newbcpu.tr.base, newbcpu.tr.limit, newbcpu.tr.dpl);
				flagdiff = 0x01;
		}
		if (vcpu.ldtr.selector != newbcpu.ldtr.selector ||
			vcpu.ldtr.base != newbcpu.ldtr.base ||
			vcpu.ldtr.limit != newbcpu.ldtr.limit ||
			vcpu.ldtr.dpl != newbcpu.ldtr.dpl) {
				vcpuapiPrint("diff ldtr (V=%04X/%08X/%08X/%1X, B=%04X/%08X/%08X/%1X)\n",
					vcpu.ldtr.selector, vcpu.ldtr.base, vcpu.ldtr.limit, vcpu.ldtr.dpl,
					newbcpu.ldtr.selector, newbcpu.ldtr.base, newbcpu.ldtr.limit, newbcpu.ldtr.dpl);
				flagdiff = 0x01;
		}
		if (vcpu.gdtr.base != newbcpu.gdtr.base ||
			vcpu.gdtr.limit != newbcpu.gdtr.limit) {
				vcpuapiPrint("diff gdtr (V=%08X/%08X, B=%08X/%08X)\n",
					vcpu.gdtr.base, vcpu.gdtr.limit,
					newbcpu.gdtr.base, newbcpu.gdtr.limit);
				flagdiff = 1;
		}
		if (vcpu.idtr.base != newbcpu.idtr.base ||
			vcpu.idtr.limit != newbcpu.idtr.limit) {
				vcpuapiPrint("diff idtr (V=%08X/%08X, B=%08X/%08X)\n",
					vcpu.idtr.base, vcpu.idtr.limit,
					newbcpu.idtr.base, newbcpu.idtr.limit);
				flagdiff = 1;
		}
		if ((vcpu.eflags & ~mask) != (newbcpu.eflags & ~mask)) {
			vcpuapiPrint("diff flags: V=%08X, B=%08X\n", vcpu.eflags, newbcpu.eflags);
			flagdiff = 1;
		}
		for (i = 0;i < vcpurec.msize;++i) {
			for (j = 0;j < bcpurec.msize;++j) {
				if (vcpurec.mem[i].flagwrite == bcpurec.mem[j].flagwrite &&
					vcpurec.mem[i].linear == bcpurec.mem[j].linear) {
					if (vcpurec.mem[i].byte != bcpurec.mem[j].byte ||
						vcpurec.mem[i].data != bcpurec.mem[j].data) {
						flagdiff = 1;
					}
				}
			}
		}
		if (vcpuins.except) flagdiff = 1;
	}
	if (flagdiff) {
		vcpuapiPrint("BEFORE EXECUTION:\n");
		vcpuapiPrintReg(&oldbcpu);
		vcpuapiPrintSreg(&oldbcpu);
		vcpuapiPrintCreg(&oldbcpu);
		vcpuapiPrint("---------------------------------------------------\n");
		vcpuapiPrint("AFTER EXECUTION:\n");
		vcpuapiPrint("CURRENT BCPU:\n");
		for (i = 0;i < bcpurec.msize;++i) {
			vcpuapiPrint("[%c:L%08x/%1d/%08x]\n",
				bcpurec.mem[i].flagwrite ? 'W' : 'R', bcpurec.mem[i].linear,
				bcpurec.mem[i].byte, vcpurec.mem[i].data);
		}
		vcpuapiPrintReg(&newbcpu);
		vcpuapiPrintSreg(&newbcpu);
		vcpuapiPrintCreg(&newbcpu);
		vcpuapiPrint("---------------------------------------------------\n");
		vcpuapiPrint("CURRENT VCPU:\n");
		vcpuapiPrint("[E:L%08X]\n", vcpurec.linear);
		for (i = 0;i < vcpurec.msize;++i) {
			vcpuapiPrint("[%c:L%08x/%1d/%08x]\n",
				vcpurec.mem[i].flagwrite ? 'W' : 'R', vcpurec.mem[i].linear,
				vcpurec.mem[i].byte, vcpurec.mem[i].data);
		}
		vcpuapiPrintReg(&vcpu);
		vcpuapiPrintSreg(&vcpu);
		vcpuapiPrintCreg(&vcpu);
		vcpuapiPrint("---------------------------------------------------\n");
	}
	return flagdiff;
}
static void PrintPhysical(t_nubit32 physical, t_vaddrcc rdata, t_nubit8 byte, t_bool write)
{
	vcpuapiPrint("%s phy=%08x, data=", write ? "write" : "read", physical);
	switch (byte) {
	case 1: vcpuapiPrint("%02x",    d_nubit8(rdata)); break;
	case 2: vcpuapiPrint("%04x",    d_nubit16(rdata));break;
	case 3: vcpuapiPrint("%08x",    d_nubit24(rdata));break;
	case 4: vcpuapiPrint("%08x",    d_nubit32(rdata));break;
	case 6: vcpuapiPrint("%016llx", d_nubit48(rdata));break;
	case 8: vcpuapiPrint("%016llx", d_nubit64(rdata));break;
	default:vcpuapiPrint("invalid");break;}
	vcpuapiPrint(", byte=%01x\n", byte);
}
void vcpuapiReadPhysical(t_nubit32 physical, t_vaddrcc rdata, t_nubit8 byte)
{
	BX_CPU_THIS_PTR access_read_physical(physical, byte, (void *)rdata);
	//BX_MEM(0)->readPhysicalPage(BX_CPU(0), physical,byte, (void *)rdata);
	if (0) PrintPhysical(physical, rdata, byte, 0);
}
void vcpuapiWritePhysical(t_nubit32 physical, t_vaddrcc rdata, t_nubit8 byte)
{
	//BX_CPU_THIS_PTR access_write_physical(physical, byte, (void *)rdata);
	//BX_MEM(0)->writePhysicalPage(BX_CPU(0), physical, byte, (void *)rdata);
	if (0) PrintPhysical(physical, rdata, byte, 1);
}

#else
#include "vapi.h"
#endif

static void xsregseg(t_cpu_sreg *rsreg, const t_string label)
{
	vcpuapiPrint("%s=%04X, Base=%08X, Limit=%08X, DPL=%01X, %s, ", label,
		rsreg->selector, rsreg->base, rsreg->limit,
		rsreg->dpl, rsreg->seg.accessed ? "A" : "a");
	if (rsreg->seg.executable) {
		vcpuapiPrint("Code, %s, %s, %s\n",
			rsreg->seg.exec.conform ? "C" : "c",
			rsreg->seg.exec.readable ? "Rw" : "rw",
			rsreg->seg.exec.defsize ? "32" : "16");
	} else {
		vcpuapiPrint("Data, %s, %s, %s\n",
			rsreg->seg.data.expdown ? "E" : "e",
			rsreg->seg.data.writable ? "RW" : "Rw",
			rsreg->seg.data.big ? "BIG" : "big");
	} 
}
static void xsregsys(t_cpu_sreg *rsreg, const t_string label)
{
	vcpuapiPrint("%s=%04X, Base=%08X, Limit=%08X, DPL=%01X, Type=%04X\n", label,
		rsreg->selector, rsreg->base, rsreg->limit,
		rsreg->dpl, rsreg->sys.type);
}
void vcpuapiPrintReg(t_cpu *rcpu)
{
	vcpuapiPrint( "EAX=%08X",  rcpu->eax);
	vcpuapiPrint(" EBX=%08X",  rcpu->ebx);
	vcpuapiPrint(" ECX=%08X",  rcpu->ecx);
	vcpuapiPrint(" EDX=%08X",  rcpu->edx);
	vcpuapiPrint("\nESP=%08X", rcpu->esp);
	vcpuapiPrint(" EBP=%08X",  rcpu->ebp);
	vcpuapiPrint(" ESI=%08X",  rcpu->esi);
	vcpuapiPrint(" EDI=%08X",  rcpu->edi);
	vcpuapiPrint("\nEIP=%08X", rcpu->eip);
	vcpuapiPrint(" EFL=%08X",  rcpu->eflags);
	vcpuapiPrint(": ");
	vcpuapiPrint("%s ", GetBit(rcpu->eflags, VCPU_EFLAGS_VM) ? "VM" : "vm");
	vcpuapiPrint("%s ", GetBit(rcpu->eflags, VCPU_EFLAGS_RF) ? "RF" : "rf");
	vcpuapiPrint("%s ", GetBit(rcpu->eflags, VCPU_EFLAGS_NT) ? "NT" : "nt");
	vcpuapiPrint("IOPL=%01X ", ((rcpu->eflags & VCPU_EFLAGS_IOPL) >> 12));
	vcpuapiPrint("%s ", GetBit(rcpu->eflags, VCPU_EFLAGS_OF) ? "OF" : "of");
	vcpuapiPrint("%s ", GetBit(rcpu->eflags, VCPU_EFLAGS_DF) ? "DF" : "df");
	vcpuapiPrint("%s ", GetBit(rcpu->eflags, VCPU_EFLAGS_IF) ? "IF" : "if");
	vcpuapiPrint("%s ", GetBit(rcpu->eflags, VCPU_EFLAGS_TF) ? "TF" : "tf");
	vcpuapiPrint("%s ", GetBit(rcpu->eflags, VCPU_EFLAGS_SF) ? "SF" : "sf");
	vcpuapiPrint("%s ", GetBit(rcpu->eflags, VCPU_EFLAGS_ZF) ? "ZF" : "zf");
	vcpuapiPrint("%s ", GetBit(rcpu->eflags, VCPU_EFLAGS_AF) ? "AF" : "af");
	vcpuapiPrint("%s ", GetBit(rcpu->eflags, VCPU_EFLAGS_PF) ? "PF" : "pf");
	vcpuapiPrint("%s ", GetBit(rcpu->eflags, VCPU_EFLAGS_CF) ? "CF" : "cf");
	vcpuapiPrint("\n");
}
void vcpuapiPrintSreg(t_cpu *rcpu)
{
	xsregseg(&rcpu->es, "ES");
	xsregseg(&rcpu->cs, "CS");
	xsregseg(&rcpu->ss, "SS");
	xsregseg(&rcpu->ds, "DS");
	xsregseg(&rcpu->fs, "FS");
	xsregseg(&rcpu->gs, "GS");
	xsregsys(&rcpu->tr, "TR  ");
	xsregsys(&rcpu->ldtr, "LDTR");
	vcpuapiPrint("GDTR Base=%08X, Limit=%04X; ",
		rcpu->gdtr.base, rcpu->gdtr.limit);
	vcpuapiPrint("IDTR Base=%08X, Limit=%04X\n",
		rcpu->idtr.base, rcpu->idtr.limit);
}
void vcpuapiPrintCreg(t_cpu *rcpu)
{
	vcpuapiPrint("CR0=%08X: %s %s %s %s %s %s; ", rcpu->cr0,
		GetBit(rcpu->cr0, VCPU_CR0_PG) ? "PG" : "pg",
		GetBit(rcpu->cr0, VCPU_CR0_ET) ? "ET" : "et",
		GetBit(rcpu->cr0, VCPU_CR0_TS) ? "TS" : "ts",
		GetBit(rcpu->cr0, VCPU_CR0_EM) ? "EM" : "em",
		GetBit(rcpu->cr0, VCPU_CR0_MP) ? "MP" : "mp",
		GetBit(rcpu->cr0, VCPU_CR0_PE) ? "PE" : "pe");
	vcpuapiPrint("CR2=PFLR=%08X; ", rcpu->cr2);
	vcpuapiPrint("CR3=PDBR=%08X\n", rcpu->cr3);
}

void vcpuapiInit()
{
#ifdef VGLOBAL_BOCHS
	vcpuInit();
	oldbcpu = vcpu;
	newbcpu = vcpu;
	memset(&bcpurec, 0x00, sizeof(t_cpurec));
#if VCPUAPI_RECORD == 1
	recordInit();
#endif
#endif
}
void vcpuapiFinal()
{
#ifdef VGLOBAL_BOCHS
	vcpuFinal();
#if VCPUAPI_RECORD == 1
	recordFinal();
#endif
#endif
}
void vcpuapiExecBefore()
{
#ifdef VGLOBAL_BOCHS
	CopyBochsCpu(&oldbcpu);
	bcpurec.rcpu = oldbcpu;
	bcpurec.linear = bcpurec.rcpu.cs.base + bcpurec.rcpu.eip;
	bcpurec.msize = 0;
	flagbrec = 0;

#if VCPUAPI_RECORD == 1
	if (vcpuinsReadIns(bcpurec.linear, (t_vaddrcc)bcpurec.opcodes))
		bcpurec.oplen = 15;
	else
		bcpurec.oplen = 0;
#endif

	if (bcpurec.linear == 0x7c00/*0xa78f*/) {
		flagvalid = 1;
		vcpuapiPrint("NXVM and Bochs comparison starts here.\n");
#if VCPUAPI_RECORD == 1
		recordNow("d:/bx.log");
#endif
	}
	/*if (bcpurec.linear == 0x2eab) {
		flagvalid = 0;
		vcpuapiPrint("NXVM and Bochs comparison stops here.\n");
		BX_CPU_THIS_PTR magic_break = 1;
	}*/
	if (flagvalid) {
#if VCPUAPI_RECORD == 1
		recordExec(&bcpurec);
#endif
#if VCPUAPI_COMPARE == 1
		vcpu = oldbcpu;
		vcpuinsRefresh();
#endif
	}
#if VCPUAPI_RECORD == 1
	flagbrec = 1;
#endif
#endif
}
void vcpuapiExecAfter()
{
#ifdef VGLOBAL_BOCHS
	if (flagvalid) {
#if VCPUAPI_COMPARE == 1
		CopyBochsCpu(&newbcpu);
		if (vcpuapiCheckDiff()) BX_CPU_THIS_PTR magic_break = 1;
#endif
	}
#endif
}

void vcpuapiMemRec(t_nubit32 linear, t_vaddrcc rdata, t_nubit8 byte, t_bool write)
{
	t_nubitcc i;
	t_nubit64 cdata = 0;
	if (flagbrec) {
		bcpurec.mem[bcpurec.msize].byte = byte;
		for (i = 0;i < byte;++i)
			d_nubit8(GetRef(cdata) + i) = d_nubit8(rdata + i);
		bcpurec.mem[bcpurec.msize].data = cdata;
		bcpurec.mem[bcpurec.msize].linear = linear;
		bcpurec.mem[bcpurec.msize].flagwrite = write;
		bcpurec.msize++;
	}
}
