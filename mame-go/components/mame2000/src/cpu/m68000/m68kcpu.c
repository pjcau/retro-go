/* ======================================================================== */
/* ========================= LICENSING & COPYRIGHT ======================== */
/* ======================================================================== */

#if 0
static const char* copyright_notice =
"MUSASHI\n"
"Version 3.1 (2000-04-04)\n"
"A portable Motorola M680x0 processor emulation engine.\n"
"Copyright 1999-2000 Karl Stenerud.  All rights reserved.\n"
"\n"
"This code may be freely used for non-commercial purpooses as long as this\n"
"copyright notice remains unaltered in the source code and any binary files\n"
"containing this code in compiled form.\n"
"\n"
"Any commercial ventures wishing to use this code must contact the author\n"
"(Karl Stenerud) for commercial licensing terms.\n"
"\n"
"The latest version of this code can be obtained at:\n"
"http://members.xoom.com/kstenerud\n"
;
#endif


/* ======================================================================== */
/* ================================= NOTES ================================ */
/* ======================================================================== */



/* ======================================================================== */
/* ================================ INCLUDES ============================== */
/* ======================================================================== */

#include "m68kops.h"
#include <string.h>
#include "m68kcpu.h"

/* ======================================================================== */
/* ================================= DATA ================================= */
/* ======================================================================== */

int  m68ki_initial_cycles;
MAMEGO_DRAM int  m68ki_remaining_cycles = 0;                     /* Number of clocks remaining */
uint m68ki_tracing = 0;
uint m68ki_address_space;

#ifdef M68K_LOG_ENABLE
char* m68ki_cpu_names[9] =
{
	"Invalid CPU",
	"M68000",
	"M68010",
	"Invalid CPU",
	"M68EC020"
	"Invalid CPU",
	"Invalid CPU",
	"Invalid CPU",
	"M68020"
};
#endif /* M68K_LOG_ENABLE */

/* The CPU core */
MAMEGO_DRAM m68ki_cpu_core m68ki_cpu = {0};


/* Used by shift & rotate instructions */
uint8 m68ki_shift_8_table[65] =
{
	0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff
};
uint16 m68ki_shift_16_table[65] =
{
	0x0000, 0x8000, 0xc000, 0xe000, 0xf000, 0xf800, 0xfc00, 0xfe00, 0xff00,
	0xff80, 0xffc0, 0xffe0, 0xfff0, 0xfff8, 0xfffc, 0xfffe, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff
};
uint m68ki_shift_32_table[65] =
{
	0x00000000, 0x80000000, 0xc0000000, 0xe0000000, 0xf0000000, 0xf8000000,
	0xfc000000, 0xfe000000, 0xff000000, 0xff800000, 0xffc00000, 0xffe00000,
	0xfff00000, 0xfff80000, 0xfffc0000, 0xfffe0000, 0xffff0000, 0xffff8000,
	0xffffc000, 0xffffe000, 0xfffff000, 0xfffff800, 0xfffffc00, 0xfffffe00,
	0xffffff00, 0xffffff80, 0xffffffc0, 0xffffffe0, 0xfffffff0, 0xfffffff8,
	0xfffffffc, 0xfffffffe, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff
};


/* Number of clock cycles to use for exception processing.
 * I used 4 for any vectors that are undocumented for processing times.
 */
uint8 m68ki_exception_cycle_table[3][256] =
{
	{ /* 000 */
		  4, /*  0: Reset - Initial Stack Pointer                      */
		  4, /*  1: Reset - Initial Program Counter                    */
		 50, /*  2: Bus Error                             (unemulated) */
		 50, /*  3: Address Error                         (unemulated) */
		 34, /*  4: Illegal Instruction                                */
		 38, /*  5: Divide by Zero -- ASG: changed from 42             */
		 40, /*  6: CHK -- ASG: chanaged from 44                       */
		 34, /*  7: TRAPV                                              */
		 34, /*  8: Privilege Violation                                */
		 34, /*  9: Trace                                              */
		  4, /* 10: 1010                                               */
		  4, /* 11: 1111                                               */
		  4, /* 12: RESERVED                                           */
		  4, /* 13: Coprocessor Protocol Violation        (unemulated) */
		  4, /* 14: Format Error                                       */
		 44, /* 15: Uninitialized Interrupt                            */
		  4, /* 16: RESERVED                                           */
		  4, /* 17: RESERVED                                           */
		  4, /* 18: RESERVED                                           */
		  4, /* 19: RESERVED                                           */
		  4, /* 20: RESERVED                                           */
		  4, /* 21: RESERVED                                           */
		  4, /* 22: RESERVED                                           */
		  4, /* 23: RESERVED                                           */
		 44, /* 24: Spurious Interrupt                                 */
		 44, /* 25: Level 1 Interrupt Autovector                       */
		 44, /* 26: Level 2 Interrupt Autovector                       */
		 44, /* 27: Level 3 Interrupt Autovector                       */
		 44, /* 28: Level 4 Interrupt Autovector                       */
		 44, /* 29: Level 5 Interrupt Autovector                       */
		 44, /* 30: Level 6 Interrupt Autovector                       */
		 44, /* 31: Level 7 Interrupt Autovector                       */
		 34, /* 32: TRAP #0 -- ASG: chanaged from 38                   */
		 34, /* 33: TRAP #1                                            */
		 34, /* 34: TRAP #2                                            */
		 34, /* 35: TRAP #3                                            */
		 34, /* 36: TRAP #4                                            */
		 34, /* 37: TRAP #5                                            */
		 34, /* 38: TRAP #6                                            */
		 34, /* 39: TRAP #7                                            */
		 34, /* 40: TRAP #8                                            */
		 34, /* 41: TRAP #9                                            */
		 34, /* 42: TRAP #10                                           */
		 34, /* 43: TRAP #11                                           */
		 34, /* 44: TRAP #12                                           */
		 34, /* 45: TRAP #13                                           */
		 34, /* 46: TRAP #14                                           */
		 34, /* 47: TRAP #15                                           */
		  4, /* 48: FP Branch or Set on Unknown Condition (unemulated) */
		  4, /* 49: FP Inexact Result                     (unemulated) */
		  4, /* 50: FP Divide by Zero                     (unemulated) */
		  4, /* 51: FP Underflow                          (unemulated) */
		  4, /* 52: FP Operand Error                      (unemulated) */
		  4, /* 53: FP Overflow                           (unemulated) */
		  4, /* 54: FP Signaling NAN                      (unemulated) */
		  4, /* 55: FP Unimplemented Data Type            (unemulated) */
		  4, /* 56: MMU Configuration Error               (unemulated) */
		  4, /* 57: MMU Illegal Operation Error           (unemulated) */
		  4, /* 58: MMU Access Level Violation Error      (unemulated) */
		  4, /* 59: RESERVED                                           */
		  4, /* 60: RESERVED                                           */
		  4, /* 61: RESERVED                                           */
		  4, /* 62: RESERVED                                           */
		  4, /* 63: RESERVED                                           */
		     /* 64-255: User Defined                                   */
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4
	},
	{ /* 010 */
		  4, /*  0: Reset - Initial Stack Pointer                      */
		  4, /*  1: Reset - Initial Program Counter                    */
		126, /*  2: Bus Error                             (unemulated) */
		126, /*  3: Address Error                         (unemulated) */
		 38, /*  4: Illegal Instruction                                */
		 44, /*  5: Divide by Zero                                     */
		 44, /*  6: CHK                                                */
		 34, /*  7: TRAPV                                              */
		 38, /*  8: Privilege Violation                                */
		 38, /*  9: Trace                                              */
		  4, /* 10: 1010                                               */
		  4, /* 11: 1111                                               */
		  4, /* 12: RESERVED                                           */
		  4, /* 13: Coprocessor Protocol Violation        (unemulated) */
		  4, /* 14: Format Error                                       */
		 44, /* 15: Uninitialized Interrupt                            */
		  4, /* 16: RESERVED                                           */
		  4, /* 17: RESERVED                                           */
		  4, /* 18: RESERVED                                           */
		  4, /* 19: RESERVED                                           */
		  4, /* 20: RESERVED                                           */
		  4, /* 21: RESERVED                                           */
		  4, /* 22: RESERVED                                           */
		  4, /* 23: RESERVED                                           */
		 46, /* 24: Spurious Interrupt                                 */
		 46, /* 25: Level 1 Interrupt Autovector                       */
		 46, /* 26: Level 2 Interrupt Autovector                       */
		 46, /* 27: Level 3 Interrupt Autovector                       */
		 46, /* 28: Level 4 Interrupt Autovector                       */
		 46, /* 29: Level 5 Interrupt Autovector                       */
		 46, /* 30: Level 6 Interrupt Autovector                       */
		 46, /* 31: Level 7 Interrupt Autovector                       */
		 38, /* 32: TRAP #0                                            */
		 38, /* 33: TRAP #1                                            */
		 38, /* 34: TRAP #2                                            */
		 38, /* 35: TRAP #3                                            */
		 38, /* 36: TRAP #4                                            */
		 38, /* 37: TRAP #5                                            */
		 38, /* 38: TRAP #6                                            */
		 38, /* 39: TRAP #7                                            */
		 38, /* 40: TRAP #8                                            */
		 38, /* 41: TRAP #9                                            */
		 38, /* 42: TRAP #10                                           */
		 38, /* 43: TRAP #11                                           */
		 38, /* 44: TRAP #12                                           */
		 38, /* 45: TRAP #13                                           */
		 38, /* 46: TRAP #14                                           */
		 38, /* 47: TRAP #15                                           */
		  4, /* 48: FP Branch or Set on Unknown Condition (unemulated) */
		  4, /* 49: FP Inexact Result                     (unemulated) */
		  4, /* 50: FP Divide by Zero                     (unemulated) */
		  4, /* 51: FP Underflow                          (unemulated) */
		  4, /* 52: FP Operand Error                      (unemulated) */
		  4, /* 53: FP Overflow                           (unemulated) */
		  4, /* 54: FP Signaling NAN                      (unemulated) */
		  4, /* 55: FP Unimplemented Data Type            (unemulated) */
		  4, /* 56: MMU Configuration Error               (unemulated) */
		  4, /* 57: MMU Illegal Operation Error           (unemulated) */
		  4, /* 58: MMU Access Level Violation Error      (unemulated) */
		  4, /* 59: RESERVED                                           */
		  4, /* 60: RESERVED                                           */
		  4, /* 61: RESERVED                                           */
		  4, /* 62: RESERVED                                           */
		  4, /* 63: RESERVED                                           */
		     /* 64-255: User Defined                                   */
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4
	},
	{ /* 020 */
		  4, /*  0: Reset - Initial Stack Pointer                      */
		  4, /*  1: Reset - Initial Program Counter                    */
		 50, /*  2: Bus Error                             (unemulated) */
		 50, /*  3: Address Error                         (unemulated) */
		 20, /*  4: Illegal Instruction                                */
		 38, /*  5: Divide by Zero                                     */
		 40, /*  6: CHK                                                */
		 20, /*  7: TRAPV                                              */
		 34, /*  8: Privilege Violation                                */
		 25, /*  9: Trace                                              */
		 20, /* 10: 1010                                               */
		 20, /* 11: 1111                                               */
		  4, /* 12: RESERVED                                           */
		  4, /* 13: Coprocessor Protocol Violation        (unemulated) */
		  4, /* 14: Format Error                                       */
		 30, /* 15: Uninitialized Interrupt                            */
		  4, /* 16: RESERVED                                           */
		  4, /* 17: RESERVED                                           */
		  4, /* 18: RESERVED                                           */
		  4, /* 19: RESERVED                                           */
		  4, /* 20: RESERVED                                           */
		  4, /* 21: RESERVED                                           */
		  4, /* 22: RESERVED                                           */
		  4, /* 23: RESERVED                                           */
		 30, /* 24: Spurious Interrupt                                 */
		 30, /* 25: Level 1 Interrupt Autovector                       */
		 30, /* 26: Level 2 Interrupt Autovector                       */
		 30, /* 27: Level 3 Interrupt Autovector                       */
		 30, /* 28: Level 4 Interrupt Autovector                       */
		 30, /* 29: Level 5 Interrupt Autovector                       */
		 30, /* 30: Level 6 Interrupt Autovector                       */
		 30, /* 31: Level 7 Interrupt Autovector                       */
		 20, /* 32: TRAP #0                                            */
		 20, /* 33: TRAP #1                                            */
		 20, /* 34: TRAP #2                                            */
		 20, /* 35: TRAP #3                                            */
		 20, /* 36: TRAP #4                                            */
		 20, /* 37: TRAP #5                                            */
		 20, /* 38: TRAP #6                                            */
		 20, /* 39: TRAP #7                                            */
		 20, /* 40: TRAP #8                                            */
		 20, /* 41: TRAP #9                                            */
		 20, /* 42: TRAP #10                                           */
		 20, /* 43: TRAP #11                                           */
		 20, /* 44: TRAP #12                                           */
		 20, /* 45: TRAP #13                                           */
		 20, /* 46: TRAP #14                                           */
		 20, /* 47: TRAP #15                                           */
		  4, /* 48: FP Branch or Set on Unknown Condition (unemulated) */
		  4, /* 49: FP Inexact Result                     (unemulated) */
		  4, /* 50: FP Divide by Zero                     (unemulated) */
		  4, /* 51: FP Underflow                          (unemulated) */
		  4, /* 52: FP Operand Error                      (unemulated) */
		  4, /* 53: FP Overflow                           (unemulated) */
		  4, /* 54: FP Signaling NAN                      (unemulated) */
		  4, /* 55: FP Unimplemented Data Type            (unemulated) */
		  4, /* 56: MMU Configuration Error               (unemulated) */
		  4, /* 57: MMU Illegal Operation Error           (unemulated) */
		  4, /* 58: MMU Access Level Violation Error      (unemulated) */
		  4, /* 59: RESERVED                                           */
		  4, /* 60: RESERVED                                           */
		  4, /* 61: RESERVED                                           */
		  4, /* 62: RESERVED                                           */
		  4, /* 63: RESERVED                                           */
		     /* 64-255: User Defined                                   */
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4
	}
};

uint8 m68ki_ea_idx_cycle_table[64] =
{
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 0, /* ..01.000 no memory indirect, base NULL             */
	 5, /* ..01..01 memory indirect,    base NULL, outer NULL */
	 7, /* ..01..10 memory indirect,    base NULL, outer 16   */
	 7, /* ..01..11 memory indirect,    base NULL, outer 32   */
	 0,  5,  7,  7,  0,  5,  7,  7,  0,  5,  7,  7,
	 2, /* ..10.000 no memory indirect, base 16               */
	 7, /* ..10..01 memory indirect,    base 16,   outer NULL */
	 9, /* ..10..10 memory indirect,    base 16,   outer 16   */
	 9, /* ..10..11 memory indirect,    base 16,   outer 32   */
	 0,  7,  9,  9,  0,  7,  9,  9,  0,  7,  9,  9,
	 6, /* ..11.000 no memory indirect, base 32               */
	11, /* ..11..01 memory indirect,    base 32,   outer NULL */
	13, /* ..11..10 memory indirect,    base 32,   outer 16   */
	13, /* ..11..11 memory indirect,    base 32,   outer 32   */
	 0, 11, 13, 13,  0, 11, 13, 13,  0, 11, 13, 13
};


/* ======================================================================== */
/* =============================== CALLBACKS ============================== */
/* ======================================================================== */

/* Default callbacks used if the callback hasn't been set yet, or if the
 * callback is set to NULL
 */

/* Interrupt acknowledge */
static int default_int_ack_callback_data;
static int default_int_ack_callback(int int_level)
{
	default_int_ack_callback_data = int_level;
	CPU_INT_LEVEL = 0;
	return M68K_INT_ACK_AUTOVECTOR;
}

/* Breakpoint acknowledge */
static unsigned int default_bkpt_ack_callback_data;
static void default_bkpt_ack_callback(unsigned int data)
{
	default_bkpt_ack_callback_data = data;
}

/* Called when a reset instruction is executed */
static void default_reset_instr_callback(void)
{
}

/* Called when the program counter changed by a large value */
static unsigned int default_pc_changed_callback_data;
static void default_pc_changed_callback(unsigned int new_pc)
{
	default_pc_changed_callback_data = new_pc;
}

/* Called every time there's bus activity (read/write to/from memory */
static unsigned int default_set_fc_callback_data;
static void default_set_fc_callback(unsigned int new_fc)
{
	default_set_fc_callback_data = new_fc;
}

/* Called every instruction cycle prior to execution */
static void default_instr_hook_callback(void)
{
}



/* ======================================================================== */
/* ================================= API ================================== */
/* ======================================================================== */

/* Access the internals of the CPU */
unsigned int m68k_get_reg(void* context, m68k_register_t regnum)
{
	m68ki_cpu_core* cpu = context != NULL ?(m68ki_cpu_core*)context : &m68ki_cpu;

	switch(regnum)
	{
		case M68K_REG_D0:	return cpu->dar[0];
		case M68K_REG_D1:	return cpu->dar[1];
		case M68K_REG_D2:	return cpu->dar[2];
		case M68K_REG_D3:	return cpu->dar[3];
		case M68K_REG_D4:	return cpu->dar[4];
		case M68K_REG_D5:	return cpu->dar[5];
		case M68K_REG_D6:	return cpu->dar[6];
		case M68K_REG_D7:	return cpu->dar[7];
		case M68K_REG_A0:	return cpu->dar[8];
		case M68K_REG_A1:	return cpu->dar[9];
		case M68K_REG_A2:	return cpu->dar[10];
		case M68K_REG_A3:	return cpu->dar[11];
		case M68K_REG_A4:	return cpu->dar[12];
		case M68K_REG_A5:	return cpu->dar[13];
		case M68K_REG_A6:	return cpu->dar[14];
		case M68K_REG_A7:	return cpu->dar[15];
		case M68K_REG_PC:	return cpu->pc;
		case M68K_REG_SR:	return	cpu->t1_flag						|
									cpu->t0_flag						|
									(cpu->s_flag << 11)					|
									(cpu->m_flag << 11)					|
									cpu->int_mask						|
									((cpu->x_flag & XFLAG_SET) >> 4)	|
									((cpu->n_flag & NFLAG_SET) >> 4)	|
									((!cpu->not_z_flag) << 2)			|
									((cpu->v_flag & VFLAG_SET) >> 6)	|
									((cpu->c_flag & CFLAG_SET) >> 8);
		case M68K_REG_SP:	return cpu->dar[15];
		case M68K_REG_USP:	return cpu->s_flag ? cpu->sp[0] : cpu->dar[15];
		case M68K_REG_ISP:	return cpu->s_flag && !cpu->m_flag ? cpu->dar[15] : cpu->sp[4];
		case M68K_REG_MSP:	return cpu->s_flag && cpu->m_flag ? cpu->dar[15] : cpu->sp[6];
		case M68K_REG_SFC:	return cpu->sfc;
		case M68K_REG_DFC:	return cpu->dfc;
		case M68K_REG_VBR:	return cpu->vbr;
		case M68K_REG_CACR:	return cpu->cacr;
		case M68K_REG_CAAR:	return cpu->caar;
		case M68K_REG_PREF_ADDR:	return cpu->pref_addr;
		case M68K_REG_PREF_DATA:	return cpu->pref_data;
		case M68K_REG_PPC:	return cpu->ppc;
		case M68K_REG_IR:	return cpu->ir;
		case M68K_REG_CPU_TYPE:
			switch(cpu->cpu_type)
			{
				case CPU_TYPE_000:		return (unsigned int)M68K_CPU_TYPE_68000;
				case CPU_TYPE_010:		return (unsigned int)M68K_CPU_TYPE_68010;
				case CPU_TYPE_EC020:	return (unsigned int)M68K_CPU_TYPE_68EC020;
				case CPU_TYPE_020:		return (unsigned int)M68K_CPU_TYPE_68020;
			}
			return M68K_CPU_TYPE_INVALID;
		default:			return 0;
	}
	return 0;
}

void m68k_set_reg(m68k_register_t regnum, unsigned int value)
{
	switch(regnum)
	{
		case M68K_REG_D0:	REG_D[0] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_D1:	REG_D[1] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_D2:	REG_D[2] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_D3:	REG_D[3] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_D4:	REG_D[4] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_D5:	REG_D[5] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_D6:	REG_D[6] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_D7:	REG_D[7] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_A0:	REG_A[0] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_A1:	REG_A[1] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_A2:	REG_A[2] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_A3:	REG_A[3] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_A4:	REG_A[4] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_A5:	REG_A[5] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_A6:	REG_A[6] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_A7:	REG_A[7] = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_PC:	m68ki_jump(MASK_OUT_ABOVE_32(value)); return;
		case M68K_REG_SR:	m68ki_set_sr(value); return;
		case M68K_REG_SP:	REG_SP = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_USP:	if(FLAG_S)
								REG_USP = MASK_OUT_ABOVE_32(value);
							else
								REG_SP = MASK_OUT_ABOVE_32(value);
							return;
		case M68K_REG_ISP:	if(FLAG_S && !FLAG_M)
								REG_SP = MASK_OUT_ABOVE_32(value);
							else
								REG_ISP = MASK_OUT_ABOVE_32(value);
							return;
		case M68K_REG_MSP:	if(FLAG_S && FLAG_M)
								REG_SP = MASK_OUT_ABOVE_32(value);
							else
								REG_MSP = MASK_OUT_ABOVE_32(value);
							return;
		case M68K_REG_VBR:	REG_VBR = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_SFC:	REG_SFC = value & 7; return;
		case M68K_REG_DFC:	REG_DFC = value & 7; return;
		case M68K_REG_CACR:	REG_CACR = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_CAAR:	REG_CAAR = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_PPC:	REG_PPC = MASK_OUT_ABOVE_32(value); return;
		case M68K_REG_IR:	REG_IR = MASK_OUT_ABOVE_16(value); return;
		case M68K_REG_CPU_TYPE: m68k_set_cpu_type(value); return;
		default:			return;
	}
}

/* Set the callbacks */
void m68k_set_int_ack_callback(int  (*callback)(int int_level))
{
	CALLBACK_INT_ACK = callback ? callback : default_int_ack_callback;
}

void m68k_set_bkpt_ack_callback(void  (*callback)(unsigned int data))
{
	CALLBACK_BKPT_ACK = callback ? callback : default_bkpt_ack_callback;
}

void m68k_set_reset_instr_callback(void  (*callback)(void))
{
	CALLBACK_RESET_INSTR = callback ? callback : default_reset_instr_callback;
}

void m68k_set_pc_changed_callback(void  (*callback)(unsigned int new_pc))
{
	CALLBACK_PC_CHANGED = callback ? callback : default_pc_changed_callback;
}

void m68k_set_fc_callback(void  (*callback)(unsigned int new_fc))
{
	CALLBACK_SET_FC = callback ? callback : default_set_fc_callback;
}

void m68k_set_instr_hook_callback(void  (*callback)(void))
{
	CALLBACK_INSTR_HOOK = callback ? callback : default_instr_hook_callback;
}

#include <stdio.h>
/* Set the CPU type. */
void m68k_set_cpu_type(unsigned int cpu_type)
{
	switch(cpu_type)
	{
		case M68K_CPU_TYPE_68000:
			CPU_TYPE         = CPU_TYPE_000;
			CPU_ADDRESS_MASK = 0x00ffffff;
			CPU_SR_MASK      = 0xa71f; /* T1 -- S  -- -- I2 I1 I0 -- -- -- X  N  Z  V  C  */
			CYC_INSTRUCTION  = m68ki_cycles[0];
			CYC_EXCEPTION    = m68ki_exception_cycle_table[0];
			CYC_BCC_NOTAKE_B = -2;
			CYC_BCC_NOTAKE_W = 2;
			CYC_DBCC_F_NOEXP = -2;
			CYC_DBCC_F_EXP   = 2;
			CYC_SCC_R_FALSE  = 2;
			CYC_MOVEM_W      = 2;
			CYC_MOVEM_L      = 3;
			CYC_SHIFT        = 1;
			CYC_RESET        = 132;
			return;
		case M68K_CPU_TYPE_68010:
			CPU_TYPE         = CPU_TYPE_010;
			CPU_ADDRESS_MASK = 0x00ffffff;
			CPU_SR_MASK      = 0xa71f; /* T1 -- S  -- -- I2 I1 I0 -- -- -- X  N  Z  V  C  */
			CYC_INSTRUCTION  = m68ki_cycles[1];
			CYC_EXCEPTION    = m68ki_exception_cycle_table[1];
			CYC_BCC_NOTAKE_B = -4;
			CYC_BCC_NOTAKE_W = 0;
			CYC_DBCC_F_NOEXP = 0;
			CYC_DBCC_F_EXP   = 6;
			CYC_SCC_R_FALSE  = 0;
			CYC_MOVEM_W      = 2;
			CYC_MOVEM_L      = 3;
			CYC_SHIFT        = 1;
			CYC_RESET        = 130;
			return;
		case M68K_CPU_TYPE_68EC020:
			CPU_TYPE         = CPU_TYPE_EC020;
			CPU_ADDRESS_MASK = 0x00ffffff;
			CPU_SR_MASK      = 0xf71f; /* T1 T0 S  M  -- I2 I1 I0 -- -- -- X  N  Z  V  C  */
			CYC_INSTRUCTION  = m68ki_cycles[2];
			CYC_EXCEPTION    = m68ki_exception_cycle_table[2];
			CYC_BCC_NOTAKE_B = -2;
			CYC_BCC_NOTAKE_W = 0;
			CYC_DBCC_F_NOEXP = 0;
			CYC_DBCC_F_EXP   = 4;
			CYC_SCC_R_FALSE  = 0;
			CYC_MOVEM_W      = 2;
			CYC_MOVEM_L      = 2; 
			CYC_SHIFT        = 0;
			CYC_RESET        = 518;
			return;
		case M68K_CPU_TYPE_68020:
			CPU_TYPE         = CPU_TYPE_020;
			CPU_ADDRESS_MASK = 0xffffffff;
			CPU_SR_MASK      = 0xf71f; /* T1 T0 S  M  -- I2 I1 I0 -- -- -- X  N  Z  V  C  */
			CYC_INSTRUCTION  = m68ki_cycles[2];
			CYC_EXCEPTION    = m68ki_exception_cycle_table[2];
			CYC_BCC_NOTAKE_B = -2;
			CYC_BCC_NOTAKE_W = 0;
			CYC_DBCC_F_NOEXP = 0;
			CYC_DBCC_F_EXP   = 4;
			CYC_SCC_R_FALSE  = 0;
			CYC_MOVEM_W      = 2;
			CYC_MOVEM_L      = 2;
			CYC_SHIFT        = 0;
			CYC_RESET        = 518;
			return;
	}
}

/* Execute some instructions until we use up num_cycles clock cycles */
/* ASG: removed per-instruction interrupt checks */
MAMEGO_HOT int m68k_execute(int num_cycles)
{
	/* Make sure we're not stopped */
	if(!CPU_STOPPED)
	{
#ifdef MAMEGO
		{ extern uint m68ki_slice; m68ki_slice++; }   /* a new time slice (the exact idle skips, below) */
#endif
#if defined(MAMEGO) && !defined(ESP_PLATFORM)
		{ extern void m68ki_idle_asked(int cycles); m68ki_idle_asked(num_cycles); }
#endif
		/* Set our pool of clock cycles available */
		SET_CYCLES(num_cycles);
		m68ki_initial_cycles = num_cycles;

		/* ASG: update cycles */
		USE_CYCLES(CPU_INT_CYCLES);
		CPU_INT_CYCLES = 0;

		/* Main loop.  Keep going until we run out of clock cycles */
		do
		{
			/* Set tracing accodring to T1. (T0 is done inside instruction) */
			m68ki_trace_t1(); /* auto-disable (see m68kcpu.h) */

			/* Set the address space for reads */
			m68ki_use_data_space(); /* auto-disable (see m68kcpu.h) */

			/* Call external hook to peek at CPU */
			m68ki_instr_hook(); /* auto-disable (see m68kcpu.h) */

			/* Record previous program counter */
			REG_PPC = REG_PC;

			/* Read an instruction and call its handler */
			REG_IR = m68ki_read_imm_16();
			m68ki_instruction_jump_table[REG_IR]();
			USE_CYCLES(CYC_INSTRUCTION[REG_IR]);
#ifdef PCHIST /* PC analysis builds only (-DPCHIST): cycles per 16-byte block */
			{ extern void m68k_pchist(unsigned pc, unsigned cycles); m68k_pchist(REG_PPC, CYC_INSTRUCTION[REG_IR]); }
#endif

			/* Trace m68k_exception, if necessary */
			m68ki_exception_if_trace(); /* auto-disable (see m68kcpu.h) */
		} while(GET_CYCLES() > 0);

		/* set previous PC to current PC for the next entry into the loop */
		REG_PPC = REG_PC;

		/* ASG: update cycles */
		USE_CYCLES(CPU_INT_CYCLES);
		CPU_INT_CYCLES = 0;

		/* return how many clocks we used */
		return m68ki_initial_cycles - GET_CYCLES();
	}

	/* We get here if the CPU is stopped or halted */
	SET_CYCLES(0);
	CPU_INT_CYCLES = 0;

	return num_cycles;
}


int m68k_cycles_run(void)
{
	return m68ki_initial_cycles - GET_CYCLES();
}

int m68k_cycles_remaining(void)
{
	return GET_CYCLES();
}

/* Change the timeslice */
void m68k_modify_timeslice(int cycles)
{
	m68ki_initial_cycles += cycles;
	ADD_CYCLES(cycles);
}


void m68k_end_timeslice(void)
{
	m68ki_initial_cycles = GET_CYCLES();
	SET_CYCLES(0);
}


/* ASG: rewrote so that the int_level is a mask of the IPL0/IPL1/IPL2 bits */
/* KS: Modified so that IPL* bits match with mask positions in the SR
 *     and cleaned out remenants of the interrupt controller.
 */
void m68k_set_irq(unsigned int int_level)
{
	uint old_level = CPU_INT_LEVEL;
	CPU_INT_LEVEL = int_level << 8;

	/* A transition from < 7 to 7 always interrupts (NMI) */
	/* Note: Level 7 can also level trigger like a normal IRQ */
	if(old_level != 0x0700 && CPU_INT_LEVEL == 0x0700)
		m68ki_service_interrupt(7); /* Edge triggered level 7 (NMI) */
	else
		m68ki_check_interrupts(); /* Level triggered (IRQ) */
}


/* Pulse the RESET line on the CPU */
void m68k_pulse_reset(void)
{
	static uint emulation_initialized = 0;

	/* The first call to this function initializes the opcode handler jump table */
	if(!emulation_initialized)
	{
		m68ki_build_opcode_table();
		m68k_set_int_ack_callback(NULL);
		m68k_set_bkpt_ack_callback(NULL);
		m68k_set_reset_instr_callback(NULL);
		m68k_set_pc_changed_callback(NULL);
		m68k_set_fc_callback(NULL);
		m68k_set_instr_hook_callback(NULL);

		emulation_initialized = 1;
	}


	if(CPU_TYPE == 0)	/* KW 990319 */
		m68k_set_cpu_type(M68K_CPU_TYPE_68000);

	/* Clear all stop levels and eat up all remaining cycles */
	CPU_STOPPED = 0;
	SET_CYCLES(0);

	/* Turn off tracing */
	FLAG_T1 = FLAG_T0 = 0;
	m68ki_clear_trace();
	/* Interrupt mask to level 7 */
	FLAG_INT_MASK = 0x0700;
	/* Reset VBR */
	REG_VBR = 0;
	/* Go to supervisor mode */
	m68ki_set_sm_flag(SFLAG_SET | MFLAG_CLEAR);

	/* Invalidate the prefetch queue */
#if M68K_EMULATE_PREFETCH
	/* Set to arbitrary number since our first fetch is from 0 */
	CPU_PREF_ADDR = 0x1000;
#endif /* M68K_EMULATE_PREFETCH */

	/* Read the initial stack pointer and program counter */
	m68ki_jump(0);
	REG_SP = m68ki_read_imm_32();
	REG_PC = m68ki_read_imm_32();
	m68ki_jump(REG_PC);
}

/* Pulse the HALT line on the CPU */
void m68k_pulse_halt(void)
{
	CPU_STOPPED |= STOP_LEVEL_HALT;
}


/* Get and set the current CPU context */
/* This is to allow for multiple CPUs */
unsigned int m68k_context_size()
{
	return sizeof(m68ki_cpu_core);
}

unsigned int m68k_get_context(void* dst)
{
	if(dst) *(m68ki_cpu_core*)dst = m68ki_cpu;
	return sizeof(m68ki_cpu_core);
}

void m68k_set_context(void* src)
{
	if(src) m68ki_cpu = *(m68ki_cpu_core*)src;
}

void m68k_save_context(	void (*save_value)(char*, unsigned int))
{
	if(!save_value)
		return;

	save_value("CPU_TYPE"  , m68k_get_reg(NULL, M68K_REG_CPU_TYPE));
	save_value("D0"        , REG_D[0]);
	save_value("D1"        , REG_D[1]);
	save_value("D2"        , REG_D[2]);
	save_value("D3"        , REG_D[3]);
	save_value("D4"        , REG_D[4]);
	save_value("D5"        , REG_D[5]);
	save_value("D6"        , REG_D[6]);
	save_value("D7"        , REG_D[7]);
	save_value("A0"        , REG_A[0]);
	save_value("A1"        , REG_A[1]);
	save_value("A2"        , REG_A[2]);
	save_value("A3"        , REG_A[3]);
	save_value("A4"        , REG_A[4]);
	save_value("A5"        , REG_A[5]);
	save_value("A6"        , REG_A[6]);
	save_value("A7"        , REG_A[7]);
	save_value("PPC"       , REG_PPC);
	save_value("PC"        , REG_PC);
	save_value("USP"       , REG_USP);
	save_value("ISP"       , REG_ISP);
	save_value("MSP"       , REG_MSP);
	save_value("VBR"       , REG_VBR);
	save_value("SFC"       , REG_SFC);
	save_value("DFC"       , REG_DFC);
	save_value("CACR"      , REG_CACR);
	save_value("CAAR"      , REG_CAAR);
	save_value("SR"        , m68ki_get_sr());
	save_value("INT_LEVEL" , CPU_INT_LEVEL);
	save_value("INT_CYCLES", CPU_INT_CYCLES);
	save_value("STOPPED"   , (CPU_STOPPED & STOP_LEVEL_STOP) != 0);
	save_value("HALTED"    , (CPU_STOPPED & STOP_LEVEL_HALT) != 0);
	save_value("PREF_ADDR" , CPU_PREF_ADDR);
	save_value("PREF_DATA" , CPU_PREF_DATA);
}

void m68k_load_context(unsigned int (*load_value)(char*))
{
	unsigned int temp;

	m68k_set_cpu_type(load_value("CPU_TYPE"));
	REG_PPC = load_value("PPC");
	REG_PC = load_value("PC");
	m68ki_jump(REG_PC);
	CPU_INT_LEVEL = 0;
	m68ki_set_sr_noint(load_value("SR"));
	REG_D[0]       = load_value("D0");
	REG_D[1]       = load_value("D1");
	REG_D[2]       = load_value("D2");
	REG_D[3]       = load_value("D3");
	REG_D[4]       = load_value("D4");
	REG_D[5]       = load_value("D5");
	REG_D[6]       = load_value("D6");
	REG_D[7]       = load_value("D7");
	REG_A[0]       = load_value("A0");
	REG_A[1]       = load_value("A1");
	REG_A[2]       = load_value("A2");
	REG_A[3]       = load_value("A3");
	REG_A[4]       = load_value("A4");
	REG_A[5]       = load_value("A5");
	REG_A[6]       = load_value("A6");
	REG_A[7]       = load_value("A7");
	REG_USP        = load_value("USP");
	REG_ISP        = load_value("ISP");
	REG_MSP        = load_value("MSP");
	REG_VBR        = load_value("VBR");
	REG_SFC        = load_value("SFC");
	REG_DFC        = load_value("DFC");
	REG_CACR       = load_value("CACR");
	REG_CAAR       = load_value("CAAR");
	CPU_INT_LEVEL  = load_value("INT_LEVEL");
	CPU_INT_CYCLES = load_value("INT_CYCLES");

	CPU_STOPPED = 0;
	temp           = load_value("STOPPED");
	if(temp) CPU_STOPPED |= STOP_LEVEL_STOP;
	temp           = load_value("HALTED");
	if(temp) CPU_STOPPED |= STOP_LEVEL_HALT;

	CPU_PREF_ADDR  = load_value("PREF_ADDR");
	CPU_PREF_DATA  = load_value("PREF_DATA");
}



/* ======================================================================== */
/* ============================== END OF FILE ============================= */
/* ======================================================================== */

#ifdef MAMEGO
uint m68ki_idle_enable, m68ki_idle_whash, m68ki_idle_io;
uint m68ki_slice;             /* counts the calls of m68k_execute(): a turn of an exact skip lies inside one */
uint m68ki_idle_io_lo = 1, m68ki_idle_io_hi = 0; /* empty window until a driver sets one */
static uint idle_pc, idle_whash, idle_count, idle_regs[16];

#ifndef ESP_PLATFORM
/* PC analysis only (IDLESTAT=1): which short backward loops run, and why they are not idle */
#include <stdlib.h>
struct idle_stat { uint pc, ppc, n, wrote, regs, idle; };
static struct idle_stat idle_stats[256];
/* how much of the CPU's time the idle skip removes: cycles asked of
   m68k_execute(), and cycles thrown away by USE_ALL_CYCLES() in the idle check */
static unsigned long long idle_asked, idle_skipped;
void m68ki_idle_asked(int cycles) { idle_asked += cycles; }
static void idle_stat_dump(void)
{
	int i, j;
	fprintf(stderr, "IDLESTAT cycles asked %llu, skipped as idle %llu (%.1f %%), executed %.1f %%\n", idle_asked, idle_skipped,
		idle_asked ? 100.0 * idle_skipped / idle_asked : 0.0, idle_asked ? 100.0 - 100.0 * idle_skipped / idle_asked : 0.0);
	for (i = 0; i < 256; i++) for (j = i + 1; j < 256; j++)
		if (idle_stats[j].n > idle_stats[i].n) { struct idle_stat t = idle_stats[i]; idle_stats[i] = idle_stats[j]; idle_stats[j] = t; }
	for (i = 0; i < 8 && idle_stats[i].n; i++)
		fprintf(stderr, "IDLESTAT loop %06x<-%06x taken %u: busy writes %u, regs changed %u, idle %u\n",
			idle_stats[i].pc, idle_stats[i].ppc, idle_stats[i].n, idle_stats[i].wrote, idle_stats[i].regs, idle_stats[i].idle);
}
static void idle_stat(uint wrote, uint regs, uint idle)
{
	static int init;
	struct idle_stat *e = &idle_stats[(REG_PC ^ (REG_PC >> 8)) & 255];
	if (!getenv("IDLESTAT")) return;
	if (!init) { init = 1; atexit(idle_stat_dump); }
	if (e->pc != REG_PC) { if (e->n > 1000) return; memset(e, 0, sizeof(*e)); e->pc = REG_PC; e->ppc = REG_PPC; }
	e->n++; e->wrote += wrote; e->regs += regs; e->idle += idle;
}
#ifdef PCHIST
static unsigned pchist[0x100000 >> 4], pchist_hi;
static void pchist_dump(void)
{
	unsigned i, j, top[20] = {0}; unsigned long long tot = pchist_hi;
	for (i = 0; i < sizeof(pchist) / 4; i++) tot += pchist[i];
	for (j = 0; j < 20; j++) { unsigned b = 0; for (i = 0; i < sizeof(pchist) / 4; i++) if (pchist[i] > pchist[b]) b = i; top[j] = b;
		fprintf(stderr, "PCHIST %06x %5.1f%%\n", b << 4, 100.0 * pchist[b] / tot); pchist[b] = 0; }
	fprintf(stderr, "PCHIST above 1 MB %5.1f%%\n", 100.0 * pchist_hi / tot);
}
void m68k_pchist(unsigned pc, unsigned cycles)
{
	static int init;
	if (!init) { init = 1; atexit(pchist_dump); }
	if (pc < 0x100000) pchist[pc >> 4] += cycles; else pchist_hi += cycles;
}
#endif
#else
#define idle_stat(w, r, i) ((void)0)
#endif

/* ---- wait loops that count -------------------------------------------------
 * Metal Slug waits for the vertical blank in a loop that also counts its own
 * turns:
 *     addq.w #1,$106ee0 / clr.b $106edd / cmpi.b #0,$106ede / beq / cmpi.b #1,$106ed9 / bls
 * about 970 turns a frame, 55 % of the 68000's cycles in play (PCHIST). The
 * skip above calls it busy, because what it writes changes every turn.
 *
 * Such a loop is skipped exactly when it is PROVEN to be one. The code between
 * the loop's top and its backward branch is decoded; every instruction must be
 * one of: ADDQ/SUBQ #q,abs  CLR abs  TST abs  CMPI #imm,abs  BTST #imm,abs
 * Bcc, with absolute addresses only. Then everything the loop reads and writes
 * is known: one counter (the single ADDQ/SUBQ), constants (CLR), and reads
 * that must not touch the counter nor the I/O window, and no branch sees the
 * flags of the counter's own ADDQ/SUBQ (a CLR, TST or CMPI must come between;
 * BTST only sets Z and does not count). Its registers do not
 * change (checked every turn, as for the plain skip). Nothing inside the time
 * slice can change what its branches test (interrupts are taken between
 * slices, as the plain skip already relies on), so every remaining turn of the
 * slice is the same turn. N turns are then N times the turn's cycles (measured
 * on two consecutive turns) and N times the counter's step, with X as the last
 * ADDQ/SUBQ would leave it; N is the largest count the cycle budget allows the
 * interpreter to run in full. The rest of the slice runs normally.
 * M68KCOUNT=0 (PC) turns it off: frames and samples must match with it on. */
uint m68ki_count_enable;      /* set by the board that has been gated for it (the Neo Geo init); off elsewhere */
static uint cl_pc = ~0u, cl_ppc, cl_ok, cl_addr, cl_size, cl_sub, cl_q, cl_turns, cl_lastv, cl_slice;
static int cl_prev, cl_cpp;

static uint cl_op16(uint a) { return m68k_read_immediate_16(ADDRESS_68K(a)); }

/* the absolute operand at *pc (mode 7, reg 0 or 1): its address; 0 = not absolute */
static int cl_abs(uint op, uint *pc, uint *addr)
{
	if ((op & 0x3f) == 0x38) { *addr = ADDRESS_68K((uint)MAKE_INT_16(cl_op16(*pc))); *pc += 2; return 1; }
	if ((op & 0x3f) == 0x39) { *addr = ADDRESS_68K((cl_op16(*pc) << 16) | cl_op16(*pc + 2)); *pc += 4; return 1; }
	return 0;
}

static int cl_overlap(uint a, uint an, uint b, uint bn) { return a < b + bn && b < a + an; }

static int cl_verify(uint top, uint last)
{
	static const uint bytes[3] = { 1, 2, 4 };
	uint pc = top, n = 0, i, counters = 0, other[12][2];
	uint live = 0;      /* N Z V C still come from the counter's ADDQ/SUBQ */

	while (pc <= last)
	{
		uint at = pc, op = cl_op16(pc), sz = (op >> 6) & 3, addr;
		pc += 2;
		if ((op & 0xf000) == 0x6000)                         /* Bcc (not BRA, not BSR) */
		{
			if (((op >> 8) & 0xf) < 2 || (op & 0xff) == 0xff) return 0;
			/* no branch may test the counter's own flags: "subq #1,cnt / bne loop" is a
			   delay loop that ends when the counter reaches zero, not a wait */
			if (live) return 0;
			if (at == last) return counters == 1;        /* the loop's own backward branch: done */
			/* a branch inside the loop may only go forward: no loop within the loop,
			   so no instruction runs twice in a turn */
			if ((op & 0xff) ? (op & 0x80) != 0 : (cl_op16(pc) & 0x8000) != 0) return 0;
			if (!(op & 0xff)) pc += 2;
			continue;
		}
		if (at == last) return 0;                            /* the branch must be the last instruction */
		if ((op & 0xf000) == 0x5000 && sz != 3)              /* ADDQ / SUBQ #q,abs */
		{
			if (!cl_abs(op, &pc, &addr) || counters++) return 0;
			cl_addr = addr; cl_size = bytes[sz]; cl_sub = (op >> 8) & 1; cl_q = ((op >> 9) & 7) ? ((op >> 9) & 7) : 8;
			live = 1;
			continue;
		}
		if (n >= 12) return 0;
		if (((op & 0xff00) == 0x4200 || (op & 0xff00) == 0x4a00) && sz != 3)   /* CLR abs, TST abs */
		{
			if (!cl_abs(op, &pc, &addr)) return 0;
			live = 0;                                    /* N Z V C redefined */
		}
		else if ((op & 0xff00) == 0x0c00 && sz != 3)         /* CMPI #imm,abs */
		{
			pc += sz == 2 ? 4 : 2;
			if (!cl_abs(op, &pc, &addr)) return 0;
			live = 0;
		}
		else if ((op & 0xffc0) == 0x0800)                    /* BTST #imm,abs (a byte) */
		{
			pc += 2;
			sz = 0;
			if (!cl_abs(op, &pc, &addr)) return 0;
		}
		else
			return 0;
		/* reads of the I/O window may change by themselves; CLR there has side effects */
		if (addr - m68ki_idle_io_lo <= m68ki_idle_io_hi - m68ki_idle_io_lo) return 0;
		other[n][0] = addr; other[n][1] = bytes[sz]; n++;
	}
	(void)i;
	return 0;                                                    /* ran past the branch: not an instruction boundary */
}

/* in the idle check, same PC and registers as the last turn, no I/O: 1 = a proven counting wait loop (turns skipped when measured) */
static int cl_check(void)
{
	int rem = GET_CYCLES(), cpp;
	if (!m68ki_count_enable)
		return 0;
	if (cl_pc != REG_PC || cl_ppc != REG_PPC)
	{
		uint n, k;
		cl_pc = REG_PC; cl_ppc = REG_PPC; cl_turns = 0; cl_prev = 0;
		cl_ok = cl_verify(REG_PC, REG_PPC);
		if (cl_ok)
		{
			/* nothing else in the loop may touch the counter: decode once more for the operands */
			static const uint bytes[3] = { 1, 2, 4 };
			uint pc = REG_PC;
			while (pc <= REG_PPC && cl_ok)
			{
				uint op = cl_op16(pc), sz = (op >> 6) & 3, addr = 0, isctr = 0;
				pc += 2;
				if ((op & 0xf000) == 0x6000) { if (!(op & 0xff)) pc += 2; continue; }
				if ((op & 0xf000) == 0x5000) isctr = 1;
				else if ((op & 0xff00) == 0x0c00) pc += sz == 2 ? 4 : 2;
				else if ((op & 0xffc0) == 0x0800) { pc += 2; sz = 0; }
				cl_abs(op, &pc, &addr);
				if (!isctr && cl_overlap(addr, bytes[sz], cl_addr, cl_size)) cl_ok = 0;
			}
			if (cl_addr - m68ki_idle_io_lo <= m68ki_idle_io_hi - m68ki_idle_io_lo) cl_ok = 0;
		}
		(void)n; (void)k;
	}
	if (!cl_ok)
		return 0;
	cpp = cl_prev - rem;                 /* the cycles of the turn that just ended */
	{
		/* a turn counts when it cost what the one before it cost and moved the
		   counter by exactly one step (the ADDQ/SUBQ ran once, on this path) */
		uint mask = cl_size == 4 ? 0xffffffffu : (1u << (8 * cl_size)) - 1;
		uint cur = cl_size == 1 ? m68k_read_memory_8(cl_addr) : cl_size == 2 ? m68k_read_memory_16(cl_addr) : m68k_read_memory_32(cl_addr);
		uint step = (cl_sub ? cl_lastv - cur : cur - cl_lastv) & mask;
		/* and lay inside one time slice (interrupts and timers run between slices) */
		cl_turns = (cl_slice == m68ki_slice && cl_prev > rem && cpp == cl_cpp && step == cl_q) ? cl_turns + 1 : 0;
		cl_slice = m68ki_slice;
		cl_lastv = cur & mask;
	}
	cl_cpp = cpp;
	cl_prev = rem;
	if (cl_turns >= 2 && cpp > 0 && rem > cpp)
	{
		/* every check the interpreter makes during a turn sees more than rem - cpp
		   cycles left, so it runs n full turns from here exactly when rem - n*cpp > 0 */
		uint n = (uint)(rem - 1) / (uint)cpp, mask = cl_size == 4 ? 0xffffffffu : (1u << (8 * cl_size)) - 1, v, last;
		v = cl_lastv;
		v = (cl_sub ? v - n * cl_q : v + n * cl_q) & mask;
		last = (cl_sub ? v + cl_q : v - cl_q) & mask;     /* the counter before the last turn's step */
		if (cl_size == 1) m68k_write_memory_8(cl_addr, v);
		else if (cl_size == 2) m68k_write_memory_16(cl_addr, v);
		else m68k_write_memory_32(cl_addr, v);
		/* X as the last ADDQ/SUBQ leaves it (carry or borrow of that step); the other
		   flags come from the instructions after it, the same every turn */
		FLAG_X = (cl_sub ? last < cl_q : v < cl_q) ? XFLAG_SET : 0;
		USE_CYCLES(n * cpp);
		cl_prev = GET_CYCLES();
		cl_lastv = v;
#ifndef ESP_PLATFORM
		idle_skipped += (unsigned long long)n * cpp;
#endif
	}
	return 1;
}

/* ---- idle turns longer than one loop --------------------------------------
 * Capcom's CPS1 games wait for the vertical blank in their task scheduler:
 *     clr.b flag / lea table,a0 / move.w #15,d0
 *  l: move.w #$2600,sr / tst.b flag / bne top / move.b (a0),d1 / cmpi.b #4,d1 /
 *     bcc run / move.w #$2000,sr / lea $10(a0),a0 / dbra d0,l / bra top
 * 50 to 66 % of the 68000's cycles of every CPS1 game measured (PCHIST). The
 * skip above never sees it: its turn is the outer loop, 44 bytes back, and the
 * inner DBRA, whose registers change every time, keeps resetting the single
 * loop it tracks.
 *
 * Here every backward branch within m68ki_idle_span bytes has a slot of its
 * own, by the address it lands on. A turn is what happens between two visits
 * of the same slot. When three consecutive turns start with the same
 * registers and SR, wrote the same things (the sum of the write hashes of the
 * checks in between) with nothing in the I/O window, and cost the same
 * cycles, the turns that fit in the rest of the time slice are identical to
 * them: memory is what those writes left, nothing inside a slice changes it,
 * and the CPU is deterministic. They are taken off the cycle count in one go,
 * as many whole turns as the interpreter would have run in full; the rest of
 * the slice runs normally. Exact, unlike the skip above, which drops the
 * unfinished turn too. Two things it relies on, both checked or stated:
 * - no access to the I/O window inside the turn, reads included (a read there
 *   may have a side effect or change with time by itself): M68KI_COUNT_READ;
 * - a turn lies inside one time slice (interrupts and timers run between
 *   slices): the slot remembers the slice it was last visited in.
 * And one it cannot check: nothing else changes what the 68000 reads during a
 * slice. True on the PC and on the board for the CPS1 YM2151 boards, whose
 * sound Z80 on core 1 reaches the 68000 only through the I/O window's latches.
 * Off unless a board's init turns it on; M68KTURN=0 on the PC. */
uint m68ki_turn_enable, m68ki_idle_span = 32, m68ki_idle_ior;
#define TURN_SLOTS 8
static struct turn_slot { uint pc, sr, regs[16], wsum, delta, io, count, slice; int rem, cost; } turn_slots[TURN_SLOTS];
static uint turn_wsum, turn_io;

static void turn_check(uint whash, uint io)
{
	struct turn_slot *t = &turn_slots[(REG_PC >> 1) & (TURN_SLOTS - 1)];
	int rem = GET_CYCLES();
	uint sr = m68ki_get_sr();

	turn_wsum += whash;       /* every write since the last check, whoever it was for */
	turn_io += io + m68ki_idle_ior;   /* any access to the I/O window, write or read */
	m68ki_idle_ior = 0;
	if (t->pc == REG_PC && t->sr == sr && t->io == turn_io && t->slice == m68ki_slice
		&& !memcmp(t->regs, REG_DA, sizeof(t->regs)))
	{
		uint delta = turn_wsum - t->wsum;
		int cost = t->rem - rem;
		t->count = (cost > 0 && delta == t->delta && cost == t->cost) ? t->count + 1 : 0;
		t->delta = delta;
		t->cost = cost;
		if (t->count >= 2 && cost > 0 && rem > cost)
		{
			/* the interpreter runs n more whole turns exactly when rem - n*cost > 0 */
			uint n = (uint)(rem - 1) / (uint)cost;
			USE_CYCLES(n * cost);
			rem = GET_CYCLES();
#ifndef ESP_PLATFORM
			idle_skipped += (unsigned long long)n * cost;
#endif
		}
	}
	else
	{
		t->pc = REG_PC;
		t->sr = sr;
		memcpy(t->regs, REG_DA, sizeof(t->regs));
		t->count = 0;
		t->cost = 0;
		t->delta = ~0u;
	}
	t->wsum = turn_wsum;
	t->io = turn_io;
	t->rem = rem;
	t->slice = m68ki_slice;
}

MAMEGO_HOT void m68ki_idle_check(void)
{
	uint whash = m68ki_idle_whash, io = m68ki_idle_io;
	int same = REG_PC == idle_pc && !io && whash == idle_whash && !memcmp(idle_regs, REG_DA, sizeof(idle_regs));

	m68ki_idle_whash = 0; /* hashes cover one pass of the loop */
	m68ki_idle_io = 0;
	if (m68ki_turn_enable)
		turn_check(whash, io);
	idle_stat(io || (REG_PC == idle_pc && whash != idle_whash), REG_PC == idle_pc && memcmp(idle_regs, REG_DA, sizeof(idle_regs)) != 0, same && idle_count >= 2);
	/* same place, same registers, but what it wrote changed: a wait loop that counts? */
	if (!same && !io && REG_PC == idle_pc && !memcmp(idle_regs, REG_DA, sizeof(idle_regs)) && cl_check())
	{
		idle_whash = whash;
		idle_count = 0;
		return;
	}
	if (same)
	{
		if (++idle_count >= 3)
		{
			idle_count = 0;
#ifndef ESP_PLATFORM
			if (GET_CYCLES() > 0) idle_skipped += GET_CYCLES();
#endif
			USE_ALL_CYCLES();
		}
		return;
	}
	idle_pc = REG_PC;
	idle_whash = whash;
	idle_count = 0;
	memcpy(idle_regs, REG_DA, sizeof(idle_regs));
}
#endif
