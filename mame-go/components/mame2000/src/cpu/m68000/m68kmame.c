#include <stdio.h>
#include <stdlib.h>
//#include <mem.h>
#include "m68k.h"
#include "m68000.h"
#include "state.h"

#include "m68kopac.c"
#include "m68kopdm.c"
#include "m68kopnz.c"
#include "m68kops.c"
#include "m68kcpu.c"

#ifdef M68KJIT
/* The 68000 dynarec (xtensa-68000-dynarec, components/m68kjit) on this Musashi:
 * blocks are translated from the fixed ROM of the 68000's read map (MRA_ROM
 * ranges, read from its region as the opcode fetch reads them); banked ROM,
 * the BIOS bank and RAM stay with the interpreter. Only on machines with one
 * 68000: the block cache is per address, not per CPU. */
#include "glue_musashi31.c"
#include "driver.h"
#include "esp_heap_caps.h"

int m68kjit_enabled = 1;                /* the engine choice of the app (0: interpreter) */
/* banks a driver sets once and never switches (bit n: MRA_BANKn), e.g. the Neo
 * Geo BIOS in bank 3: their code is translated too */
unsigned m68kjit_fixed_banks;
/* a block is translated once its address was reached this many times */
#define M68KJIT_HOT 8
static int jit_on, jit_pending;
static unsigned jit_calls;

static struct { unsigned lo, hi; const unsigned char *base; } jit_rom[8];  /* byte at a: base[a] */
static int jit_nrom;

static bool jit_is_code(uint32_t a, int len)
{
	int i;
	for (i = 0; i < jit_nrom; i++)
		if (a >= jit_rom[i].lo && a + len - 1 <= jit_rom[i].hi)
			return true;
	return false;
}

static uint16_t jit_read_code16(uint32_t a)
{
	int i;
	for (i = 0; i < jit_nrom; i++)
		if (a >= jit_rom[i].lo && a + 1 <= jit_rom[i].hi)
			return READ_WORD(&jit_rom[i].base[a]);
	return 0;
}

/* after a few seconds on the interpreter: the code cache gets what PSRAM has
 * spare, less a margin; with too little the game stays on the interpreter */
static void jit_start(void)
{
	/* one contiguous block (rounded to 64 KB MMU pages), from the largest one PSRAM
	 * has, leaving it a margin; halved until it fits */
	size_t spare = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
	size_t block = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
	size_t size = block > 256 * 1024 ? (block - 192 * 1024) & ~(size_t)0xFFFF : 0;
	jit_pending = 0;
	if (size > 1024 * 1024) size = 1024 * 1024;
	for (; size >= 128 * 1024; size = (size / 2) & ~(size_t)0xFFFF)
		if (glue31_jit_init(jit_is_code, jit_read_code16, size, M68KJIT_HOT))
		{
			jit_on = 1;
			break;
		}
	printf("M68KJIT %s: code cache %u KB (PSRAM free %u KB, largest block %u KB)\n", jit_on ? "on" : "off (no room)",
		jit_on ? (unsigned)(size >> 10) : 0, (unsigned)(spare >> 10), (unsigned)(block >> 10));
}

static void jit_setup(void)
{
	const struct MemoryReadAddress *mra;
	int i, cpu = -1, n68k = 0;
	jit_on = 0;
	jit_nrom = 0;
	if (!m68kjit_enabled) return;
	for (i = 0; i < MAX_CPU && Machine->drv->cpu[i].cpu_type; i++)
		if ((Machine->drv->cpu[i].cpu_type & ~CPU_FLAGS_MASK) == CPU_M68000) { cpu = i; n68k++; }
	if (n68k != 1) return;
	for (mra = Machine->drv->cpu[cpu].memory_read; mra && mra->start != -1 && jit_nrom < 8; mra++)
	{
		int bank = -(int)(intptr_t)mra->handler - 9;            /* MRA_BANK1 = -10 ... */
		if (mra->handler == MRA_ROM)
		{
			jit_rom[jit_nrom].lo = mra->start;
			jit_rom[jit_nrom].hi = mra->end;
			jit_rom[jit_nrom].base = memory_region(REGION_CPU1 + cpu);
			jit_nrom++;
		}
		else if (bank >= 1 && bank <= 16 && (m68kjit_fixed_banks & (1u << bank)) && cpu_bankbase[bank])
		{
			jit_rom[jit_nrom].lo = mra->start;
			jit_rom[jit_nrom].hi = mra->end;
			jit_rom[jit_nrom].base = cpu_bankbase[bank] - mra->start;   /* bank reads are relative to the range */
			jit_nrom++;
		}
	}
	/* the dynarec starts later (jit_start): its code cache takes the PSRAM the
	 * game leaves once it has allocated everything (display surfaces included;
	 * Metal Slug leaves very little) */
	jit_pending = jit_nrom > 0;
	jit_calls = 0;
	printf("M68KJIT pending: %d fixed ROM range(s)", jit_nrom);
	for (i = 0; i < jit_nrom; i++) printf(" %06x-%06x", jit_rom[i].lo, jit_rom[i].hi);
	printf("\n");
}

/* a console line every ~5 s of 68000 time: what the dynarec does */
static void jit_report(void)
{
	static unsigned calls;
	if (++calls % 3000) return;
	int i;
	printf("M68KJIT blocks %u insns %u native %u links %u runs %u steps %u code %u KB flushes %u; steps by MB:",
		(unsigned)m68kjit_stats.blocks, (unsigned)m68kjit_stats.insns, (unsigned)m68kjit_stats.native,
		(unsigned)m68kjit_stats.links, (unsigned)m68kjit_stats.block_runs, (unsigned)m68kjit_stats.steps,
		(unsigned)(m68kjit_stats.code_bytes >> 10), (unsigned)m68kjit_stats.flushes);
	for (i = 0; i < 16; i++)
		if (glue31_steps_by_mb[i]) printf(" %x:%u", i, (unsigned)glue31_steps_by_mb[i]);
	printf("\n");
}
#endif

void m68000_reset(void* param)
{
	m68k_set_cpu_type(M68K_CPU_TYPE_68000);
	m68k_pulse_reset();
#ifdef M68KJIT
	jit_setup();                        /* (glue31_jit_init() flushes the blocks) */
#endif
}

void m68000_exit(void)
{
	/* nothing to do ? */
}

int m68000_execute(int cycles)
{
#ifdef M68KJIT
	if (jit_pending && ++jit_calls == 1500)     /* ~2-3 s of 68000 time slices */
		jit_start();
	if (jit_on)
	{
		jit_report();
		return glue31_jit_execute(cycles);
	}
#endif
	return m68k_execute(cycles);
}

unsigned m68000_get_context(void *dst)
{
	return m68k_get_context(dst);
}

void m68000_set_context(void *src)
{
		m68k_set_context(src);
}

unsigned m68000_get_pc(void)
{
	return m68k_get_reg(NULL, M68K_REG_PC)&0x00ffffff;
}

void m68000_set_pc(unsigned val)
{
	m68k_set_reg(M68K_REG_PC, val&0x00ffffff);
}

unsigned m68000_get_sp(void)
{
	return m68k_get_reg(NULL, M68K_REG_SP);
}

void m68000_set_sp(unsigned val)
{
	m68k_set_reg(M68K_REG_SP, val);
}

unsigned m68000_get_reg(int regnum)
{
	switch( regnum )
	{
		case M68K_PC:  return m68k_get_reg(NULL, M68K_REG_PC);
		case M68K_SP:  return m68k_get_reg(NULL, M68K_REG_SP);
		case M68K_ISP: return m68k_get_reg(NULL, M68K_REG_ISP);
		case M68K_USP: return m68k_get_reg(NULL, M68K_REG_USP);
		case M68K_SR:  return m68k_get_reg(NULL, M68K_REG_SR);
		case M68K_D0:  return m68k_get_reg(NULL, M68K_REG_D0);
		case M68K_D1:  return m68k_get_reg(NULL, M68K_REG_D1);
		case M68K_D2:  return m68k_get_reg(NULL, M68K_REG_D2);
		case M68K_D3:  return m68k_get_reg(NULL, M68K_REG_D3);
		case M68K_D4:  return m68k_get_reg(NULL, M68K_REG_D4);
		case M68K_D5:  return m68k_get_reg(NULL, M68K_REG_D5);
		case M68K_D6:  return m68k_get_reg(NULL, M68K_REG_D6);
		case M68K_D7:  return m68k_get_reg(NULL, M68K_REG_D7);
		case M68K_A0:  return m68k_get_reg(NULL, M68K_REG_A0);
		case M68K_A1:  return m68k_get_reg(NULL, M68K_REG_A1);
		case M68K_A2:  return m68k_get_reg(NULL, M68K_REG_A2);
		case M68K_A3:  return m68k_get_reg(NULL, M68K_REG_A3);
		case M68K_A4:  return m68k_get_reg(NULL, M68K_REG_A4);
		case M68K_A5:  return m68k_get_reg(NULL, M68K_REG_A5);
		case M68K_A6:  return m68k_get_reg(NULL, M68K_REG_A6);
		case M68K_A7:  return m68k_get_reg(NULL, M68K_REG_A7);
		case M68K_PREF_ADDR:  return m68k_get_reg(NULL, M68K_REG_PREF_ADDR);
		case M68K_PREF_DATA:  return m68k_get_reg(NULL, M68K_REG_PREF_DATA);
		case REG_PREVIOUSPC: return m68k_get_reg(NULL, M68K_REG_PPC);
/* TODO: return contents of [SP + wordsize * (REG_SP_CONTENTS-regnum)] */
		default:
			if( regnum < REG_SP_CONTENTS )
			{
				unsigned offset = m68k_get_reg(NULL, M68K_REG_SP) + 4 * (REG_SP_CONTENTS - regnum);
				if( offset < 0xfffffd )
					return m68k_read_memory_32( offset );
			}
	}
	return 0;
}

void m68000_set_reg(int regnum, unsigned val)
{
	switch( regnum )
	{
		case M68K_PC:  m68k_set_reg(M68K_REG_PC, val); break;
		case M68K_SP:  m68k_set_reg(M68K_REG_SP, val); break;
		case M68K_ISP: m68k_set_reg(M68K_REG_ISP, val); break;
		case M68K_USP: m68k_set_reg(M68K_REG_USP, val); break;
		case M68K_SR:  m68k_set_reg(M68K_REG_SR, val); break;
		case M68K_D0:  m68k_set_reg(M68K_REG_D0, val); break;
		case M68K_D1:  m68k_set_reg(M68K_REG_D1, val); break;
		case M68K_D2:  m68k_set_reg(M68K_REG_D2, val); break;
		case M68K_D3:  m68k_set_reg(M68K_REG_D3, val); break;
		case M68K_D4:  m68k_set_reg(M68K_REG_D4, val); break;
		case M68K_D5:  m68k_set_reg(M68K_REG_D5, val); break;
		case M68K_D6:  m68k_set_reg(M68K_REG_D6, val); break;
		case M68K_D7:  m68k_set_reg(M68K_REG_D7, val); break;
		case M68K_A0:  m68k_set_reg(M68K_REG_A0, val); break;
		case M68K_A1:  m68k_set_reg(M68K_REG_A1, val); break;
		case M68K_A2:  m68k_set_reg(M68K_REG_A2, val); break;
		case M68K_A3:  m68k_set_reg(M68K_REG_A3, val); break;
		case M68K_A4:  m68k_set_reg(M68K_REG_A4, val); break;
		case M68K_A5:  m68k_set_reg(M68K_REG_A5, val); break;
		case M68K_A6:  m68k_set_reg(M68K_REG_A6, val); break;
		case M68K_A7:  m68k_set_reg(M68K_REG_A7, val); break;
/* TODO: set contents of [SP + wordsize * (REG_SP_CONTENTS-regnum)] */
		default:
			if( regnum < REG_SP_CONTENTS )
			{
				unsigned offset = m68k_get_reg(NULL, M68K_REG_SP) + 4 * (REG_SP_CONTENTS - regnum);
				if( offset < 0xfffffd )
					m68k_write_memory_16( offset, val );
			}
	}
}


static void* m68000_file;
static unsigned int m68000_load_value(char* identifier)
{
	unsigned int value;
	state_load_UINT32(m68000_file, "m68000", cpu_getactivecpu(), identifier, &value, 1);
	return value;
}

static void m68000_save_value(char* identifier, unsigned int value)
{
	state_save_UINT32(m68000_file, "m68000", cpu_getactivecpu(), identifier, &value, 1);
}

void m68000_state_load(void *file)
{
	m68000_file = file;
	m68k_load_context(m68000_load_value);
}

void m68000_state_save(void *file)
{
	m68000_file = file;
	m68k_save_context(m68000_save_value);
}

void m68000_set_nmi_line(int state)
{
	switch(state)
	{
		case CLEAR_LINE:
			m68k_set_irq(0);
			break;
		case ASSERT_LINE:
			m68k_set_irq(7);
			break;
		default:
			m68k_set_irq(7);
			break;
	}
}

void m68000_set_irq_line(int irqline, int state)
{
	switch(state)
	{
		case CLEAR_LINE:
			m68k_set_irq(0);
			break;
		case ASSERT_LINE:
			m68k_set_irq(irqline);
			break;
		default:
			m68k_set_irq(irqline);
			break;
	}
}

void m68000_set_irq_callback(int (*callback)(int irqline))
{
	m68k_set_int_ack_callback(callback);
}


const char *m68000_info(void *context, int regnum)
{
	switch( regnum )
	{
		case CPU_INFO_NAME: return "68000";
		case CPU_INFO_FAMILY: return "Motorola 68K";
		case CPU_INFO_VERSION: return "3.0";
		case CPU_INFO_FILE: return __FILE__;
		case CPU_INFO_CREDITS: return "Copyright 1999-2000 Karl Stenerud. All rights reserved. (2.1 fixes HJB)";
	}
	return "";
}

unsigned m68000_dasm(char *buffer, unsigned pc)
{
	change_pc32(pc);
	sprintf( buffer, "$%04X", m68k_read_immediate_16(pc) );
	return 2;
}

/****************************************************************************
 * M68010 section
 ****************************************************************************/
#if HAS_M68010
void m68010_reset(void* param)
{
	m68k_set_cpu_type(M68K_CPU_TYPE_68010);
	m68k_pulse_reset();
}

unsigned m68010_get_reg(int regnum)
{
	switch( regnum )
	{
		case M68K_VBR: return m68k_get_reg(NULL, M68K_REG_VBR);
		case M68K_SFC: return m68k_get_reg(NULL, M68K_REG_SFC);
		case M68K_DFC: return m68k_get_reg(NULL, M68K_REG_DFC);
		default:       return m68000_get_reg(regnum);
	}
	return 0;
}

void m68010_set_reg(int regnum, unsigned val)
{
	switch( regnum )
	{
		case M68K_VBR: m68k_set_reg(M68K_REG_VBR, val); break;
		case M68K_SFC: m68k_set_reg(M68K_REG_SFC, val); break;
		case M68K_DFC: m68k_set_reg(M68K_REG_DFC, val); break;
		default:       m68000_set_reg(regnum, val); break;
	}
}

static void* m68010_file;
static unsigned int m68010_load_value(char* identifier)
{
	unsigned int value;
	state_load_UINT32(m68010_file, "m68010", cpu_getactivecpu(), identifier, &value, 1);
	return value;
}

static void m68010_save_value(char* identifier, unsigned int value)
{
	state_save_UINT32(m68010_file, "m68010", cpu_getactivecpu(), identifier, &value, 1);
}

void m68010_state_load(void *file)
{
	m68010_file = file;
	m68k_load_context(m68010_load_value);
}

void m68010_state_save(void *file)
{
	m68010_file = file;
	m68k_save_context(m68010_save_value);
}

const char *m68010_info(void *context, int regnum)
{
	switch( regnum )
	{
		case CPU_INFO_NAME: return "68010";
	}
	return m68000_info(context,regnum);
}

unsigned m68010_dasm(char *buffer, unsigned pc)
{
	change_pc32(pc);
	sprintf( buffer, "$%04X", m68k_read_immediate_16(pc) );
	return 2;
}
#endif

/****************************************************************************
 * M680EC20 section
 ****************************************************************************/
#if HAS_M68EC020
void m68ec020_reset(void* param)
{
	m68k_set_cpu_type(M68K_CPU_TYPE_68EC020);
	m68k_pulse_reset();
}

unsigned m68ec020_get_reg(int regnum)
{
	switch( regnum )
	{
		case M68K_MSP: return m68k_get_reg(NULL, M68K_REG_MSP);
		case M68K_CACR:  return m68k_get_reg(NULL, M68K_REG_CACR);
		case M68K_CAAR:  return m68k_get_reg(NULL, M68K_REG_CAAR);
		default:       return m68010_get_reg(regnum);
	}
	return 0;
}

void m68ec020_set_reg(int regnum, unsigned val)
{
	switch( regnum )
	{
		case M68K_MSP:  m68k_set_reg(M68K_REG_MSP, val); break;
		case M68K_CACR: m68k_set_reg(M68K_REG_CACR, val); break;
		case M68K_CAAR: m68k_set_reg(M68K_REG_CAAR, val); break;
		default:        m68010_set_reg(regnum, val); break;
	}
}

static void* m68ec020_file;
static unsigned int m68ec020_load_value(char* identifier)
{
	unsigned int value;
	state_load_UINT32(m68ec020_file, "m68ec020", cpu_getactivecpu(), identifier, &value, 1);
	return value;
}

static void m68ec020_save_value(char* identifier, unsigned int value)
{
	state_save_UINT32(m68ec020_file, "m68ec020", cpu_getactivecpu(), identifier, &value, 1);
}

void m68ec020_state_load(void *file)
{
	m68ec020_file = file;
	m68k_load_context(m68ec020_load_value);
}

void m68ec020_state_save(void *file)
{
	m68ec020_file = file;
	m68k_save_context(m68ec020_save_value);
}

const char *m68ec020_info(void *context, int regnum)
{
	switch( regnum )
	{
		case CPU_INFO_NAME: return "68EC020";
	}
	return m68010_info(context,regnum);
}

unsigned m68ec020_dasm(char *buffer, unsigned pc)
{
	change_pc32(pc);
	sprintf( buffer, "$%04X", m68k_read_immediate_16(pc) );
	return 2;
}
#endif

/****************************************************************************
 * M68020 section
 ****************************************************************************/
#if HAS_M68020
void m68020_reset(void* param)
{
	m68k_set_cpu_type(M68K_CPU_TYPE_68020);
	m68k_pulse_reset();
}

const char *m68020_info(void *context, int regnum)
{
	switch( regnum )
	{
		case CPU_INFO_NAME: return "68020";
	}
	return m68ec020_info(context,regnum);
}

unsigned m68020_get_pc(void)
{
	return m68k_get_reg(NULL, M68K_REG_PC);
}

void m68020_set_pc(unsigned val)
{
	m68k_set_reg(M68K_REG_PC, val);
}

unsigned m68020_get_reg(int regnum)
{
	switch( regnum )
	{
		case M68K_PC:  return m68k_get_reg(NULL, M68K_REG_PC);
		case REG_PREVIOUSPC: return m68k_get_reg(NULL, M68K_REG_PPC);
	}
	return m68ec020_get_reg(regnum);
}

void m68020_set_reg(int regnum, unsigned val)
{
	switch( regnum )
	{
		case M68K_PC:  m68k_set_reg(M68K_REG_PC, val); break;
	}
	m68ec020_set_reg(regnum, val);
}

static void* m68020_file;
static unsigned int m68020_load_value(char* identifier)
{
	unsigned int value;
	state_load_UINT32(m68020_file, "m68020", cpu_getactivecpu(), identifier, &value, 1);
	return value;
}

static void m68020_save_value(char* identifier, unsigned int value)
{
	state_save_UINT32(m68020_file, "m68020", cpu_getactivecpu(), identifier, &value, 1);
}

void m68020_state_load(void *file)
{
	m68020_file = file;
	m68k_load_context(m68020_load_value);
}

void m68020_state_save(void *file)
{
	m68020_file = file;
	m68k_save_context(m68020_save_value);
}

unsigned m68020_dasm(char *buffer, unsigned pc)
{
	change_pc32(pc);
	sprintf( buffer, "$%04X", m68k_read_immediate_16(pc) );
	return 2;
}
#endif
