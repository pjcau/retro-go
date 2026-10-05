/* mame-go: a subset of MAME 0.37b5's drivers/system16.c (mame2000-libretro):
   the System 16 boards with one 68000, a Z80 and a YM2151 (+ uPD7759 or 7751).
   Kept families: aliensyn, altbeast, aurail, bayroute, eswat, fantzone, goldnaxe, shinobi, tetris, wb3. The System 18, Hang-On, Space Harrier, OutRun and X/Y
   board sections and the sets MAME marks as not working are left out.
   Regenerate from upstream rather than editing the game sections by hand. */
#include "../machine/system16.c"
#include "../sndhrdw/system16.c"
#include "../vidhrdw/system16.c"

/*


--------
  ASTORMBL
          3. In the ending, the 3 heroes are floating into a half bubble. (see picture). Also colour problems during ending as well.
          4. In the later Shooting gallery stage (like inside the car shop and the factory (mission 3)),
		  there is some garbage graphics (sprite of death monsters that appear where they should not)


	working:
		Alex Kidd
		Alien Storm (bootleg)
		Alien Syndrome
		Altered Beast (Ver 1)
		Altered Beast (Ver 2)	(No Sound)
		Atomic Point			(No Sound)
		Aurail					(Speech quality sounds poor)
		Aurail (317-0168)
		Bay Route
		Body Slam
	    Dump Matsumoto (Japan, Body Slam)
		Dynamite Dux (bootleg)
		Enduro Racer (bootleg)
		Enduro Racer (custom bootleg)
		E-Swat (bootleg)
		Fantasy Zone (Old Ver.)
		Fantasy Zone (New Ver.)
		Flash Point  (bootleg)
		Golden Axe (Ver 1)
		Golden Axe (Ver 2)
		Hang-on
		Heavyweight Champ: some minor graphics glitches
		Major League: No game over.
		Moonwalker (bootleg): Music Speed varies
		Outrun (set 1)
		Outrun (set 2)
		Outrun (custom bootleg)
		Passing Shot (bootleg)
		Passing Shot (4 player bootleg)
		Quartet: Glitch on highscore list
		Quartet (Japan): Glitch on highscore list
		Quartet 2: Glitch on highscore list
		Riot City
		SDI
		Shadow Dancer
		Shadow Dancer (Japan)
		Shinobi
		Shinobi (Sys16A Bootleg?)
		Space Harrier
		Super Hangon (bootleg)
		Tetris (bootleg)
		Time Scanner
		Toryumon
		Tough Turf (Japan)			(No Sound)
		Tough Turf (US)				(No Sound)
		Tough Turf (bootleg)	(No Speech Roms)
		Wonderboy 3 - Monster Lair
		Wonderboy 3 - Monster Lair (bootleg)
		Wrestle War

	not really working:
		Shadow Dancer (bootleg)

	protected:
		Alex Kidd (jpn?)
		Alien Syndrome
		Alien Syndrome
		Alien Syndrome (Japan)
		Alien Storm
		Alien Storm (2 Player)
		Bay Route (317-0116)
		Bay Route (protected bootleg 1)
		Bay Route (protected bootleg 2)
		Enduro Racer
		E-Swat
		Flash Point
		Golden Axe (Ver 1 317-0121 Japan)
		Golden Axe (Ver 2 317-0110)
		Golden Axe (Ver 2 317-0122)
		Golden Axe (protected bootleg)
		Jyuohki (Japan, altered beast)
		Moonwalker (317-0158)
		Moonwalker (317-0159)
		Passing Shot (317-0080)
		Shinobi (Sys16B 317-0049)
		Shinobi (Sys16A 317-0050)
		SDI (Japan, old version)
		Super Hangon
		Tetris (Type A)
		Tetris (Type B 317-0092)
		Wonderboy 3 - Monster Lair (317-0089)

	protected (No driver):
		Ace Attacker
		Action Fighter
		Bloxeed
		Clutch Hitter
		Cotton (Japan)
		Cotton
		DD Crew
		Dunk Shot
		Excite League
		Laser Ghost
		Line of Fire
		MVP
		Ryukyu
		Super Leagu
		Thunder Blade
		Thunder Blade (Japan)
		Turbo Outrun
		Turbo Outrun (Set 2)

	not working (No driver):
		After Burner
		After Burner II

*/

#define SYS16_CREDITS \
	"Thierry Lescot & Nao (Hardware Info)\n" \
	"Mirko Buffoni (MAME driver)\n" \
	"Andrew Prime\n" \
	"Phil Stroffolino"


//#define SPACEHARRIER_OFFSETS


/*
This should be enabled when the sprite manager fully handles the special
left/right side markers. This will fix graphics glitches in several games,
including ESwat, Alien Storm and Altered Beast.
*/
#define SPRITE_SIDE_MARKERS
#define TRANSPARENT_SHADOWS

#ifdef TRANSPARENT_SHADOWS
#define NumOfShadowColors 32
#define ShadowColorsMultiplier 2
extern int sys16_sh_shadowpal;
#else
#define ShadowColorsMultiplier 1
#endif
#ifdef TRANSPARENT_SHADOWS
extern int sys16_MaxShadowColors;
#endif

#include "driver.h"
#include "vidhrdw/generic.h"
#include "cpu/z80/z80.h"
#include "cpu/i8039/i8039.h"

/***************************************************************************/

READ_HANDLER( sys16_tileram_r );
WRITE_HANDLER( sys16_tileram_w );
READ_HANDLER( sys16_textram_r );
WRITE_HANDLER( sys16_textram_w );
extern int sys16_vh_start( void );
extern void sys16_vh_stop( void );
extern void sys16_vh_screenrefresh(struct osd_bitmap *bitmap,int full_refresh);
WRITE_HANDLER( sys16_paletteram_w );

extern int sys16_ho_vh_start( void );
extern void sys16_ho_vh_screenrefresh(struct osd_bitmap *bitmap,int full_refresh);

extern int sys16_or_vh_start( void );
extern void sys16_or_vh_screenrefresh(struct osd_bitmap *bitmap,int full_refresh);

extern int sys18_vh_start( void );
extern void sys18_vh_screenrefresh(struct osd_bitmap *bitmap,int full_refresh);

/* video driver constants (vary with game) */
extern int sys16_spritesystem;
extern int sys16_sprxoffset;
extern int sys16_bgxoffset;
extern int sys16_fgxoffset;
extern int *sys16_obj_bank;
extern int sys16_textmode;
extern int sys16_textlayer_lo_min;
extern int sys16_textlayer_lo_max;
extern int sys16_textlayer_hi_min;
extern int sys16_textlayer_hi_max;
extern int sys16_dactype;
extern int sys16_bg1_trans;
extern int sys16_bg_priority_mode;
extern int sys16_fg_priority_mode;
extern int sys16_bg_priority_value;
extern int sys16_fg_priority_value;
extern int sys16_spritelist_end;
extern int sys16_tilebank_switch;
extern int sys16_rowscroll_scroll;
extern int sys16_quartet_title_kludge;
void (* sys16_update_proc)( void );

/* video driver registers */
extern int sys16_refreshenable;
extern int sys16_clear_screen;
extern int sys16_tile_bank0;
extern int sys16_tile_bank1;
extern int sys16_bg_scrollx, sys16_bg_scrolly;
extern int sys16_bg_page[4];
extern int sys16_fg_scrollx, sys16_fg_scrolly;
extern int sys16_fg_page[4];

extern int sys16_bg2_scrollx, sys16_bg2_scrolly;
extern int sys16_bg2_page[4];
extern int sys16_fg2_scrollx, sys16_fg2_scrolly;
extern int sys16_fg2_page[4];

extern int sys18_bg2_active;
extern int sys18_fg2_active;
extern unsigned char *sys18_splittab_bg_x;
extern unsigned char *sys18_splittab_bg_y;
extern unsigned char *sys18_splittab_fg_x;
extern unsigned char *sys18_splittab_fg_y;

#ifdef SPACEHARRIER_OFFSETS
extern unsigned char *spaceharrier_patternoffsets;
#endif
extern unsigned char *gr_ver;
extern unsigned char *gr_hor;
extern unsigned char *gr_pal;
extern unsigned char *gr_flip;
extern int gr_palette;
extern int gr_palette_default;
extern unsigned char gr_colorflip[2][4];
extern unsigned char *gr_second_road;

/* video driver has access to these memory regions */
unsigned char *sys16_tileram;
unsigned char *sys16_textram;
unsigned char *sys16_spriteram;

/* other memory regions */
static unsigned char *sys16_workingram;
static unsigned char *sys16_extraram;
static unsigned char *sys16_extraram2;
static unsigned char *sys16_extraram3;
static unsigned char *sys16_extraram4;
static unsigned char *sys16_extraram5;

// 7751 emulation
WRITE_HANDLER( sys16_7751_audio_8255_w );
 READ_HANDLER( sys16_7751_audio_8255_r );
 READ_HANDLER( sys16_7751_sh_rom_r );
 READ_HANDLER( sys16_7751_sh_t1_r );
 READ_HANDLER( sys16_7751_sh_command_r );
WRITE_HANDLER( sys16_7751_sh_dac_w );
WRITE_HANDLER( sys16_7751_sh_busy_w );
WRITE_HANDLER( sys16_7751_sh_offset_a0_a3_w );
WRITE_HANDLER( sys16_7751_sh_offset_a4_a7_w );
WRITE_HANDLER( sys16_7751_sh_offset_a8_a11_w );
WRITE_HANDLER( sys16_7751_sh_rom_select_w );


// encryption decoding
void endurob2_decode_data(unsigned char *dest,unsigned char *source,int size);
void endurob2_decode_data2(unsigned char *dest,unsigned char *source,int size);
void enduror_decode_data(unsigned char *dest,unsigned char *source,int size);
void enduror_decode_data2(unsigned char *dest,unsigned char *source,int size);

void aurail_decode_data(unsigned char *dest,unsigned char *source,int size);
void aurail_decode_opcode1(unsigned char *dest,unsigned char *source,int size);
void aurail_decode_opcode2(unsigned char *dest,unsigned char *source,int size);

/***************************************************************************/

#define MWA_PALETTERAM	sys16_paletteram_w, &paletteram
#define MRA_PALETTERAM	paletteram_word_r

#define MRA_WORKINGRAM	MRA_BANK1
#define MWA_WORKINGRAM	MWA_BANK1,&sys16_workingram

#define MRA_SPRITERAM	MRA_BANK2
#define MWA_SPRITERAM	MWA_BANK2,&sys16_spriteram

#define MRA_TILERAM		sys16_tileram_r
#define MWA_TILERAM		sys16_tileram_w,&sys16_tileram

#define MRA_TEXTRAM		sys16_textram_r
#define MWA_TEXTRAM		sys16_textram_w,&sys16_textram

#define MRA_EXTRAM		MRA_BANK3
#define MWA_EXTRAM		MWA_BANK3,&sys16_extraram

#define MRA_EXTRAM2		MRA_BANK4
#define MWA_EXTRAM2		MWA_BANK4,&sys16_extraram2

#define MRA_EXTRAM3		MRA_BANK5
#define MWA_EXTRAM3		MWA_BANK5,&sys16_extraram3

#define MRA_EXTRAM4		MRA_BANK6
#define MWA_EXTRAM4		MWA_BANK6,&sys16_extraram4

#define MRA_EXTRAM5		MRA_BANK7
#define MWA_EXTRAM5		MWA_BANK7,&sys16_extraram5

/***************************************************************************/

#define MACHINE_DRIVER( GAMENAME,READMEM,WRITEMEM,INITMACHINE,GFXSIZE) \
static const struct MachineDriver GAMENAME = \
{ \
	{ \
		{ \
			CPU_M68000, \
			10000000, \
			READMEM,WRITEMEM,0,0, \
			sys16_interrupt,1 \
		}, \
		{ \
			CPU_Z80 | CPU_AUDIO_CPU, \
			4096000, \
			sound_readmem,sound_writemem,sound_readport,sound_writeport, \
			ignore_interrupt,1 \
		}, \
	}, \
	60, DEFAULT_60HZ_VBLANK_DURATION, \
	1, \
	INITMACHINE, \
	40*8, 28*8, { 0*8, 40*8-1, 0*8, 28*8-1 }, \
	GFXSIZE, \
	2048*ShadowColorsMultiplier,2048*ShadowColorsMultiplier, \
	0, \
	VIDEO_TYPE_RASTER | VIDEO_MODIFIES_PALETTE, \
	0, \
	sys16_vh_start, \
	sys16_vh_stop, \
	sys16_vh_screenrefresh, \
	SOUND_SUPPORTS_STEREO,0,0,0, \
	{ \
		{ \
			SOUND_YM2151, \
			&ym2151_interface \
		} \
	} \
};

#define MACHINE_DRIVER_7759( GAMENAME,READMEM,WRITEMEM,INITMACHINE,GFXSIZE, UPD7759INTF ) \
static const struct MachineDriver GAMENAME = \
{ \
	{ \
		{ \
			CPU_M68000, \
			10000000, \
			READMEM,WRITEMEM,0,0, \
			sys16_interrupt,1 \
		}, \
		{ \
			CPU_Z80 | CPU_AUDIO_CPU, \
			4096000, \
			sound_readmem_7759,sound_writemem,sound_readport,sound_writeport_7759, \
			ignore_interrupt,1 \
		}, \
	}, \
	60, DEFAULT_60HZ_VBLANK_DURATION, \
	1, \
	INITMACHINE, \
	40*8, 28*8, { 0*8, 40*8-1, 0*8, 28*8-1 }, \
	GFXSIZE, \
	2048*ShadowColorsMultiplier,2048*ShadowColorsMultiplier, \
	0, \
	VIDEO_TYPE_RASTER | VIDEO_MODIFIES_PALETTE, \
	0, \
	sys16_vh_start, \
	sys16_vh_stop, \
	sys16_vh_screenrefresh, \
	SOUND_SUPPORTS_STEREO,0,0,0, \
	{ \
		{ \
			SOUND_YM2151, \
			&ym2151_interface \
		}, { \
			SOUND_UPD7759, \
			&UPD7759INTF \
		} \
	} \
};


#define MACHINE_DRIVER_7751( GAMENAME,READMEM,WRITEMEM,INITMACHINE,GFXSIZE ) \
static const struct MachineDriver GAMENAME = \
{ \
	{ \
		{ \
			CPU_M68000, \
			10000000, \
			READMEM,WRITEMEM,0,0, \
			sys16_interrupt,1 \
		}, \
		{ \
			CPU_Z80 | CPU_AUDIO_CPU, \
			4096000, \
			sound_readmem_7751,sound_writemem,sound_readport_7751,sound_writeport_7751, \
			ignore_interrupt,1 \
		}, \
		{ \
			CPU_N7751 | CPU_AUDIO_CPU, \
			6000000/15,        /* 6Mhz crystal */ \
			readmem_7751,writemem_7751,readport_7751,writeport_7751, \
			ignore_interrupt,1 \
		} \
	}, \
	60, DEFAULT_60HZ_VBLANK_DURATION, \
	1, \
	INITMACHINE, \
	40*8, 28*8, { 0*8, 40*8-1, 0*8, 28*8-1 }, \
	GFXSIZE, \
	2048*ShadowColorsMultiplier,2048*ShadowColorsMultiplier, \
	0, \
	VIDEO_TYPE_RASTER | VIDEO_MODIFIES_PALETTE, \
	0, \
	sys16_vh_start, \
	sys16_vh_stop, \
	sys16_vh_screenrefresh, \
	SOUND_SUPPORTS_STEREO,0,0,0, \
	{ \
		{ \
			SOUND_YM2151, \
			&ym2151_interface \
		}, \
		{ \
			SOUND_DAC, \
			&sys16_7751_dac_interface \
		} \
	} \
};


#define MACHINE_DRIVER_18( GAMENAME,READMEM,WRITEMEM,INITMACHINE,GFXSIZE) \
static const struct MachineDriver GAMENAME = \
{ \
	{ \
		{ \
			CPU_M68000, \
			10000000, \
			READMEM,WRITEMEM,0,0, \
			sys16_interrupt,1 \
		}, \
		{ \
			CPU_Z80 | CPU_AUDIO_CPU, \
			4096000*2, /* overclocked to fix sound, but wrong! */ \
			sound_readmem_18,sound_writemem_18,sound_readport_18,sound_writeport_18, \
			ignore_interrupt,1 \
		}, \
	}, \
	60, DEFAULT_60HZ_VBLANK_DURATION, \
	1, \
	INITMACHINE, \
	40*8, 28*8, { 0*8, 40*8-1, 0*8, 28*8-1 }, \
	GFXSIZE, \
	2048*ShadowColorsMultiplier,2048*ShadowColorsMultiplier, \
	0, \
	VIDEO_TYPE_RASTER | VIDEO_MODIFIES_PALETTE, \
	0, \
	sys18_vh_start, \
	sys16_vh_stop, \
	sys18_vh_screenrefresh, \
	SOUND_SUPPORTS_STEREO,0,0,0, \
	{ \
		{ \
			SOUND_YM3438, \
			&ym3438_interface \
		}, \
		{ \
			SOUND_RF5C68, \
			&rf5c68_interface, \
		} \
	} \
};



static void (*sys16_custom_irq)(void);

static void sys16_onetime_init_machine(void)
{
	sys16_bg1_trans=0;
	sys16_rowscroll_scroll=0;
	sys18_splittab_bg_x=0;
	sys18_splittab_bg_y=0;
	sys18_splittab_fg_x=0;
	sys18_splittab_fg_y=0;

	sys16_quartet_title_kludge=0;

	sys16_custom_irq=NULL;

#ifdef TRANSPARENT_SHADOWS
	sys16_MaxShadowColors=NumOfShadowColors;
#endif

#ifdef SPACEHARRIER_OFFSETS
	spaceharrier_patternoffsets=0;
#endif
}

/***************************************************************************/

#ifdef MAMEGO
static int s16snd_core1;
static void s16snd_event(int kind, int value);
static void s16snd_try_enable(void);
#endif

int sys16_interrupt( void ){
#ifdef MAMEGO
	s16snd_try_enable();
#endif
	if(sys16_custom_irq) sys16_custom_irq();
	return 4; /* Interrupt vector 4, used by VBlank */
}

/***************************************************************************/

static void sound_cause_nmi(int chip)
{
#ifdef MAMEGO
	if (s16snd_core1) /* the uPD7759 asks the Z80 on core 1 for a byte */
	{
		extern void z80snd_set_nmi_line(int state);
		z80snd_set_nmi_line(ASSERT_LINE);
		z80snd_set_nmi_line(CLEAR_LINE);
		return;
	}
#endif
	cpu_set_nmi_line(1, PULSE_LINE);
}

static const struct MemoryReadAddress sound_readmem[] =
{
	{ 0x0000, 0x7fff, MRA_ROM },
	{ 0xe800, 0xe800, soundlatch_r },
	{ 0xf800, 0xffff, MRA_RAM },
	{ -1 }  /* end of table */
};

static const struct MemoryWriteAddress sound_writemem[] =
{
	{ 0x0000, 0x7fff, MWA_ROM },
	{ 0xf800, 0xffff, MWA_RAM },
	{ -1 }  /* end of table */
};

static const struct IOReadPort sound_readport[] =
{
	{ 0x01, 0x01, YM2151_status_port_0_r },
	{ 0xc0, 0xc0, soundlatch_r },
	{ -1 }	/* end of table */
};

static const struct IOWritePort sound_writeport[] =
{
	{ 0x00, 0x00, YM2151_register_port_0_w },
	{ 0x01, 0x01, YM2151_data_port_0_w },
	{ -1 }
};



// 7751 Sound

static const struct MemoryReadAddress sound_readmem_7751[] =
{
	{ 0x0000, 0x7fff, MRA_ROM },
	{ 0xe800, 0xe800, soundlatch_r },
	{ 0xf800, 0xffff, MRA_RAM },
	{ -1 }  /* end of table */
};

static const struct IOReadPort sound_readport_7751[] =
{
	{ 0x01, 0x01, YM2151_status_port_0_r },
//    { 0x0e, 0x0e, sys16_7751_audio_8255_r },
	{ 0xc0, 0xc0, soundlatch_r },
	{ -1 }	/* end of table */
};



static const struct IOWritePort sound_writeport_7751[] =
{
	{ 0x00, 0x00, YM2151_register_port_0_w },
	{ 0x01, 0x01, YM2151_data_port_0_w },
	{ 0x80, 0x80, sys16_7751_audio_8255_w },
	{ -1 }
};

static const struct MemoryReadAddress readmem_7751[] =
{
        { 0x0000, 0x03ff, MRA_ROM },
        { -1 }  /* end of table */
};

static const struct MemoryWriteAddress writemem_7751[] =
{
        { 0x0000, 0x03ff, MWA_ROM },
        { -1 }  /* end of table */
};

static const struct IOReadPort readport_7751[] =
{
        { I8039_t1,  I8039_t1,  sys16_7751_sh_t1_r },
        { I8039_p2,  I8039_p2,  sys16_7751_sh_command_r },
        { I8039_bus, I8039_bus, sys16_7751_sh_rom_r },
        { -1 }  /* end of table */
};

static const struct IOWritePort writeport_7751[] =
{
        { I8039_p1, I8039_p1, sys16_7751_sh_dac_w },
        { I8039_p2, I8039_p2, sys16_7751_sh_busy_w },
        { I8039_p4, I8039_p4, sys16_7751_sh_offset_a0_a3_w },
        { I8039_p5, I8039_p5, sys16_7751_sh_offset_a4_a7_w },
        { I8039_p6, I8039_p6, sys16_7751_sh_offset_a8_a11_w },
        { I8039_p7, I8039_p7, sys16_7751_sh_rom_select_w },
        { -1 }  /* end of table */
};

static const struct DACinterface sys16_7751_dac_interface =
{
        1,
        { 100 }
};


// 7759


static const struct MemoryReadAddress sound_readmem_7759[] =
{
	{ 0x0000, 0x7fff, MRA_ROM },
	{ 0x8000, 0xdfff, UPD7759_0_data_r },
	{ 0xe800, 0xe800, soundlatch_r },
	{ 0xf800, 0xffff, MRA_RAM },
	{ -1 }  /* end of table */
};

// some games (aurail, riotcity, eswat), seem to send different format data to the 7759
// this function changes that data to what the 7759 expects, but it sounds quite poor.
static WRITE_HANDLER( UPD7759_process_message_w )
{
	if((data & 0xc0) == 0x40) data=0xc0;
	else data&=0x3f;

	UPD7759_message_w(offset,data);
}

static const struct IOWritePort sound_writeport_7759[] =
{
	{ 0x00, 0x00, YM2151_register_port_0_w },
	{ 0x01, 0x01, YM2151_data_port_0_w },
	{ 0x40, 0x40, UPD7759_process_message_w },
	{ 0x80, 0x80, UPD7759_0_start_w },
	{ -1 }
};

static const struct UPD7759_interface upd7759_interface =
{
	1,			/* 1 chip */
	UPD7759_STANDARD_CLOCK,
	{ 60 }, 	/* volumes */
	{ REGION_CPU2 },			/* memory region 3 contains the sample data */
    UPD7759_SLAVE_MODE,
	{ sound_cause_nmi },
};

/* mame-go: the System 18 (YM3438, RF5C68), Sega 3D (YM2203, SegaPCM) and
   Super Hang-On / OutRun sound sections of the original are left out here */

static WRITE_HANDLER( sound_command_w ){
	//logerror("SOUND COMMAND %04x <- %02x\n", offset, data&0xff );
	soundlatch_w( 0,data&0xff );
#ifdef MAMEGO
	if (s16snd_core1) { s16snd_event(0, data & 0xff); s16snd_event(1, 0); return; }
#endif
	cpu_cause_interrupt( 1, 0 );
}

static WRITE_HANDLER( sound_command_nmi_w ){
	//logerror("SOUND COMMAND %04x <- %02x\n", offset, data&0xff );
	soundlatch_w( 0,data&0xff );
#ifdef MAMEGO
	if (s16snd_core1) { s16snd_event(0, data & 0xff); s16snd_event(2, 0); return; }
#endif
	cpu_set_nmi_line(1, PULSE_LINE);
}

static const struct YM2151interface ym2151_interface =
{
	1,			/* 1 chip */
	4096000,	/* 3.58 MHZ ? */
	{ YM3012_VOL(40,MIXER_PAN_LEFT,40,MIXER_PAN_RIGHT) },
	{ 0 }
};

#ifdef MAMEGO
#include "system16_snd1.c"
#endif




/***************************************************************************/

static const struct GfxLayout charlayout1 =
{
	8,8,	/* 8*8 chars */
	8192,	/* 8192 chars */
	3,	/* 3 bits per pixel */
	{ 0x20000*8, 0x10000*8, 0 },
		{ 0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
	8*8	/* every sprite takes 8 consecutive bytes */
};

static const struct GfxLayout charlayout2 =
{
	8,8,	/* 8*8 chars */
	16384,	/* 16384 chars */
	3,	/* 3 bits per pixel */
	{ 0x40000*8, 0x20000*8, 0 },
		{ 0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
	8*8	/* every sprite takes 8 consecutive bytes */
};

static const struct GfxLayout charlayout4 =
{
	8,8,	/* 8*8 chars */
	32768,	/* 32768 chars */
	3,	/* 3 bits per pixel */
	{ 0x80000*8, 0x40000*8, 0 },
		{ 0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
	8*8	/* every sprite takes 8 consecutive bytes */
};

static const struct GfxLayout charlayout8 =
{
	8,8,	/* 8*8 chars */
	4096,	/* 4096 chars */
	3,	/* 3 bits per pixel */
	{ 0x10000*8, 0x08000*8, 0 },
		{ 0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
	8*8	/* every sprite takes 8 consecutive bytes */
};

static const struct GfxDecodeInfo gfx1[] =
{
	{ REGION_GFX1, 0x00000, &charlayout1,	0, 256 },
	{ -1 } /* end of array */
};

static const struct GfxDecodeInfo gfx2[] =
{
	{ REGION_GFX1, 0x00000, &charlayout2,	0, 256 },
	{ -1 } /* end of array */
};

static const struct GfxDecodeInfo gfx4[] =
{
	{ REGION_GFX1, 0x00000, &charlayout4,	0, 256 },
	{ -1 } /* end of array */
};

static const struct GfxDecodeInfo gfx8[] =
{
	{ REGION_GFX1, 0x00000, &charlayout8,	0, 256 },
	{ -1 } /* end of array */
};

/***************************************************************************/

static void set_refresh( int data ){
	sys16_refreshenable = data&0x20;
	sys16_clear_screen  = data&1;
}

static void set_refresh_18( int data ){
	sys16_refreshenable = data&0x2;
//	sys16_clear_screen  = data&4;
}

static void set_refresh_3d( int data ){
	sys16_refreshenable = data&0x10;
}


static void set_tile_bank( int data ){
	sys16_tile_bank1 = data&0xf;
	sys16_tile_bank0 = (data>>4)&0xf;
}

static void set_tile_bank18( int data ){
	sys16_tile_bank0 = data&0xf;
	sys16_tile_bank1 = (data>>4)&0xf;
}

static void set_fg_page( int data ){
	sys16_fg_page[0] = data>>12;
	sys16_fg_page[1] = (data>>8)&0xf;
	sys16_fg_page[2] = (data>>4)&0xf;
	sys16_fg_page[3] = data&0xf;
}

static void set_bg_page( int data ){
	sys16_bg_page[0] = data>>12;
	sys16_bg_page[1] = (data>>8)&0xf;
	sys16_bg_page[2] = (data>>4)&0xf;
	sys16_bg_page[3] = data&0xf;
}

static void set_fg_page1( int data ){
	sys16_fg_page[1] = data>>12;
	sys16_fg_page[0] = (data>>8)&0xf;
	sys16_fg_page[3] = (data>>4)&0xf;
	sys16_fg_page[2] = data&0xf;
}

static void set_bg_page1( int data ){
	sys16_bg_page[1] = data>>12;
	sys16_bg_page[0] = (data>>8)&0xf;
	sys16_bg_page[3] = (data>>4)&0xf;
	sys16_bg_page[2] = data&0xf;
}

static void set_fg2_page( int data ){
	sys16_fg2_page[0] = data>>12;
	sys16_fg2_page[1] = (data>>8)&0xf;
	sys16_fg2_page[2] = (data>>4)&0xf;
	sys16_fg2_page[3] = data&0xf;
}

static void set_bg2_page( int data ){
	sys16_bg2_page[0] = data>>12;
	sys16_bg2_page[1] = (data>>8)&0xf;
	sys16_bg2_page[2] = (data>>4)&0xf;
	sys16_bg2_page[3] = data&0xf;
}


/***************************************************************************/
/*	Important: you must leave extra space when listing sprite ROMs
	in a ROM module definition.  This routine unpacks each sprite nibble
	into a byte, doubling the memory consumption. */

static void sys16_sprite_decode( int num_banks, int bank_size ){
	unsigned char *base = memory_region(REGION_GFX2);
	unsigned char *temp = (unsigned char*)malloc( bank_size );
	int i;

	if( !temp ) return;

	for( i = num_banks; i >0; i-- ){
		unsigned char *finish	= base + 2*bank_size*i;
		unsigned char *dest = finish - 2*bank_size;

		unsigned char *p1 = temp;
		unsigned char *p2 = temp+bank_size/2;

		unsigned char data;

		memcpy (temp, base+bank_size*(i-1), bank_size);

/*
	note: both pen#0 and pen#15 are transparent.
	we replace references to pen#15 with pen#0, to simplify the sprite rendering
*/
		do {
			data = *p2++;
#ifdef SPRITE_SIDE_MARKERS
			if( (data&0x0f) == 0x0f )
			{
				if((data&0xf0) !=0xf0 && (data&0xf0) !=0)
					*dest++ = data >> 4;
				else
					*dest++ = 0xff;
				*dest++ = 0xff;
			}
			else if( (data&0xf0) == 0xf0 )
			{
				*dest++ = 0x00;
				if( (data&0x0f) == 0x0f ) data &= 0xf0;
				*dest++ = data &0xf;
			}
			else
			{
				*dest++ = data >> 4;
				*dest++ = data & 0xF;
			}
#else
			{
				if( (data&0xf0) == 0xf0 ) data &= 0x0f;
				if( (data&0x0f) == 0x0f ) data &= 0xf0;
				*dest++ = data >> 4;
				*dest++ = data & 0xF;
			}
#endif

			data = *p1++;
#ifdef SPRITE_SIDE_MARKERS
			if( (data&0x0f) == 0x0f )
			{
				if((data&0xf0) !=0xf0 && (data&0xf0) !=0)
					*dest++ = data >> 4;
				else
					*dest++ = 0xff;
				*dest++ = 0xff;
			}
			else if( (data&0xf0) == 0xf0 )
			{
				*dest++ = 0x00;
				if( (data&0x0f) == 0x0f ) data &= 0xf0;
				*dest++ = data &0xf;
			}
			else
			{
				*dest++ = data >> 4;
				*dest++ = data & 0xF;
			}
#else
			{
				if( (data&0xf0) == 0xf0 ) data &= 0x0f;
				if( (data&0x0f) == 0x0f ) data &= 0xf0;
				*dest++ = data >> 4;
				*dest++ = data & 0xF;
			}
#endif
		} while( dest<finish );
	}
	free( temp );
}

static void sys16_sprite_decode2( int num_banks, int bank_size, int side_markers ){
	unsigned char *base = memory_region(REGION_GFX2);
	unsigned char *temp = (unsigned char*)malloc( bank_size );
	int i;

	if( !temp ) return;

	for( i = num_banks; i >0; i-- ){
		unsigned char *finish	= base + 2*bank_size*i;
		unsigned char *dest = finish - 2*bank_size;

		unsigned char *p1 = temp;
		unsigned char *p2 = temp+bank_size/4;
		unsigned char *p3 = temp+bank_size/2;
		unsigned char *p4 = temp+bank_size/4*3;

		unsigned char data;

		memcpy (temp, base+bank_size*(i-1), bank_size);

/*
	note: both pen#0 and pen#15 are transparent.
	we replace references to pen#15 with pen#0, to simplify the sprite rendering
*/
		do {
			data = *p4++;
#ifdef SPRITE_SIDE_MARKERS
			if( side_markers )
			{
				if( (data&0x0f) == 0x0f )
				{
					if((data&0xf0) !=0xf0 && (data&0xf0) !=0)
						*dest++ = data >> 4;
					else
						*dest++ = 0xff;
					*dest++ = 0xff;
				}
				else if( (data&0xf0) == 0xf0 )
				{
					*dest++ = 0x00;
					if( (data&0x0f) == 0x0f ) data &= 0xf0;
					*dest++ = data &0xf;
				}
				else
				{
					*dest++ = data >> 4;
					*dest++ = data & 0xF;
				}
			}
			else
#endif
			{
				if( (data&0xf0) == 0xf0 ) data &= 0x0f;
				if( (data&0x0f) == 0x0f ) data &= 0xf0;
				*dest++ = data >> 4;
				*dest++ = data & 0xF;
			}

			data = *p3++;
#ifdef SPRITE_SIDE_MARKERS
			if( side_markers )
			{
				if( (data&0x0f) == 0x0f )
				{
					if((data&0xf0) !=0xf0 && (data&0xf0) !=0)
						*dest++ = data >> 4;
					else
						*dest++ = 0xff;
					*dest++ = 0xff;
				}
				else if( (data&0xf0) == 0xf0 )
				{
					*dest++ = 0x00;
					if( (data&0x0f) == 0x0f ) data &= 0xf0;
					*dest++ = data &0xf;
				}
				else
				{
					*dest++ = data >> 4;
					*dest++ = data & 0xF;
				}
			}
			else
#endif
			{
				if( (data&0xf0) == 0xf0 ) data &= 0x0f;
				if( (data&0x0f) == 0x0f ) data &= 0xf0;
				*dest++ = data >> 4;
				*dest++ = data & 0xF;
			}


			data = *p2++;
#ifdef SPRITE_SIDE_MARKERS
			if( side_markers )
			{
				if( (data&0x0f) == 0x0f )
				{
					if((data&0xf0) !=0xf0 && (data&0xf0) !=0)
						*dest++ = data >> 4;
					else
						*dest++ = 0xff;
					*dest++ = 0xff;
				}
				else if( (data&0xf0) == 0xf0 )
				{
					*dest++ = 0x00;
					if( (data&0x0f) == 0x0f ) data &= 0xf0;
					*dest++ = data &0xf;
				}
				else
				{
					*dest++ = data >> 4;
					*dest++ = data & 0xF;
				}
			}
			else
#endif
			{
				if( (data&0xf0) == 0xf0 ) data &= 0x0f;
				if( (data&0x0f) == 0x0f ) data &= 0xf0;
				*dest++ = data >> 4;
				*dest++ = data & 0xF;
			}

			data = *p1++;
#ifdef SPRITE_SIDE_MARKERS
			if( side_markers )
			{
				if( (data&0x0f) == 0x0f )
				{
					if((data&0xf0) !=0xf0 && (data&0xf0) !=0)
						*dest++ = data >> 4;
					else
						*dest++ = 0xff;
					*dest++ = 0xff;
				}
				else if( (data&0xf0) == 0xf0 )
				{
					*dest++ = 0x00;
					if( (data&0x0f) == 0x0f ) data &= 0xf0;
					*dest++ = data &0xf;
				}
				else
				{
					*dest++ = data >> 4;
					*dest++ = data & 0xF;
				}
			}
			else
#endif
			{
				if( (data&0xf0) == 0xf0 ) data &= 0x0f;
				if( (data&0x0f) == 0x0f ) data &= 0xf0;
				*dest++ = data >> 4;
				*dest++ = data & 0xF;
			}

		} while( dest<finish );
	}
	free( temp );
}

int gr_bitmap_width;

static void generate_gr_screen(int w,int bitmap_width,int skip,int start_color,int end_color,int source_size)
{
	uint8_t *buf;
	uint8_t *gr = memory_region(REGION_GFX3);
	uint8_t *grr = NULL;
    int i,j,k;
    int center_offset=0;


	buf=(uint8_t*)malloc(source_size);
	if(buf==NULL) return;

	gr_bitmap_width = bitmap_width;

	memcpy(buf,gr,source_size);
	memset(gr,0,256*bitmap_width);

	if (w!=gr_bitmap_width)
	{
		if (skip>0) // needs mirrored RHS
			grr=gr;
		else
		{
			center_offset=(gr_bitmap_width-w);
			gr+=center_offset/2;
		}
	}

    // build gr_bitmap
	for (i=0; i<256; i++)
	{
		uint8_t last_bit;
		uint8_t color_data[4];

		color_data[0]=start_color; color_data[1]=start_color+1;
		color_data[2]=start_color+2; color_data[3]=start_color+3;
		last_bit=((buf[0]&0x80)==0)|(((buf[0x4000]&0x80)==0)<<1);
		for (j=0; j<w/8; j++)
		{
			for (k=0; k<8; k++)
			{
				uint8_t bit=((buf[0]&0x80)==0)|(((buf[0x4000]&0x80)==0)<<1);
				if (bit!=last_bit && bit==0 && i>1)
				{ // color flipped to 0,advance color[0]
					if (color_data[0]+end_color <= end_color)
					{
						color_data[0]+=end_color;
					}
					else
					{
						color_data[0]-=end_color;
					}
				}
				*gr = color_data[bit];
				last_bit=bit;
				buf[0] <<= 1; buf[0x4000] <<= 1; gr++;
			}
			buf++;
		}

		if (grr!=NULL)
		{ // need mirrored RHS
			uint8_t *_gr=gr-1;
			_gr -= skip;
			for (j=0; j<w-skip; j++)
			{
				*gr++ = *_gr--;
			}
			for (j=0; j<skip; j++) *gr++ = 0;
		}
		else if (center_offset!=0)
		{
			gr+=center_offset;
		}
	}

	i=1;
	while ( (1<<i) < gr_bitmap_width ) i++;
	gr_bitmap_width=i; // power of 2

}


/***************************************************************************/

#define io_player1_r input_port_0_r
#define io_player2_r input_port_1_r
#define io_player3_r input_port_5_r
#define io_player4_r input_port_6_r
#define io_service_r input_port_2_r

#define io_dip1_r input_port_3_r
#define io_dip2_r input_port_4_r
#define io_dip3_r input_port_5_r

/***************************************************************************/


static void patch_codeX( int offset, int data, int cpu ){
	int aligned_offset = offset&0xfffffe;
	unsigned char *RAM = memory_region(REGION_CPU1+cpu);
	int old_word = READ_WORD( &RAM[aligned_offset] );

	if( offset&1 )
		data = (old_word&0xff00)|data;
	else
		data = (old_word&0x00ff)|(data<<8);

	WRITE_WORD (&RAM[aligned_offset], data);
}

static void patch_code( int offset, int data ) {patch_codeX(offset,data,0);}
//static void patch_code2( int offset, int data ) {patch_codeX(offset,data,2);}

static void patch_z80code( int offset, int data ){
	unsigned char *RAM = memory_region(REGION_CPU2);
	RAM[offset] = data;
}

/***************************************************************************/

#define SYS16_JOY1 PORT_START \
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON3 ) \
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON1 ) \
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_BUTTON2 ) \
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_UNKNOWN ) \
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN  | IPF_8WAY ) \
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_UP    | IPF_8WAY ) \
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY ) \
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT  | IPF_8WAY )

#define SYS16_JOY2 PORT_START \
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON3 | IPF_COCKTAIL ) \
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL ) \
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_COCKTAIL ) \
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_UNKNOWN ) \
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN  | IPF_8WAY | IPF_COCKTAIL ) \
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_UP    | IPF_8WAY | IPF_COCKTAIL ) \
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_COCKTAIL ) \
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT  | IPF_8WAY | IPF_COCKTAIL )

#define SYS16_JOY3 PORT_START \
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON3 | IPF_PLAYER3 ) \
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER3 ) \
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_PLAYER3 ) \
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_UNKNOWN ) \
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN  | IPF_8WAY | IPF_PLAYER3 ) \
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_UP    | IPF_8WAY | IPF_PLAYER3 ) \
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_PLAYER3 ) \
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT  | IPF_8WAY | IPF_PLAYER3 )

#define SYS16_JOY1_SWAPPEDBUTTONS PORT_START \
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON3 ) \
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON2 ) \
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_BUTTON1 ) \
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_UNKNOWN ) \
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN  | IPF_8WAY ) \
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_UP    | IPF_8WAY ) \
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY ) \
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT  | IPF_8WAY )

#define SYS16_JOY2_SWAPPEDBUTTONS PORT_START \
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON3 | IPF_COCKTAIL ) \
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_COCKTAIL ) \
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL ) \
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_UNKNOWN ) \
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN  | IPF_8WAY | IPF_COCKTAIL ) \
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_UP    | IPF_8WAY | IPF_COCKTAIL ) \
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_COCKTAIL ) \
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT  | IPF_8WAY | IPF_COCKTAIL )

#define SYS16_SERVICE PORT_START \
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_COIN1 ) \
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_COIN2 ) \
	PORT_BITX(0x04, IP_ACTIVE_LOW, IPT_SERVICE, DEF_STR( Service_Mode ), KEYCODE_F2, IP_JOY_NONE ) \
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_COIN3 ) \
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_START1 ) \
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_START2 ) \
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNKNOWN ) \
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_UNKNOWN )

#define SYS16_COINAGE PORT_START \
	PORT_DIPNAME( 0x0f, 0x0f, DEF_STR( Coin_A ) ) \
	PORT_DIPSETTING(    0x07, DEF_STR( 4C_1C ) ) \
	PORT_DIPSETTING(    0x08, DEF_STR( 3C_1C ) ) \
	PORT_DIPSETTING(    0x09, DEF_STR( 2C_1C ) ) \
	PORT_DIPSETTING(    0x05, "2 Coins/1 Credit 5/3 6/4") \
	PORT_DIPSETTING(    0x04, "2 Coins/1 Credit 4/3") \
	PORT_DIPSETTING(    0x0f, DEF_STR( 1C_1C ) ) \
	PORT_DIPSETTING(    0x01, "1 Coin/1 Credit 2/3") \
	PORT_DIPSETTING(    0x02, "1 Coin/1 Credit 4/5") \
	PORT_DIPSETTING(    0x03, "1 Coin/1 Credit 5/6") \
	PORT_DIPSETTING(    0x06, DEF_STR( 2C_3C ) ) \
	PORT_DIPSETTING(    0x0e, DEF_STR( 1C_2C ) ) \
	PORT_DIPSETTING(    0x0d, DEF_STR( 1C_3C ) ) \
	PORT_DIPSETTING(    0x0c, DEF_STR( 1C_4C ) ) \
	PORT_DIPSETTING(    0x0b, DEF_STR( 1C_5C ) ) \
	PORT_DIPSETTING(    0x0a, DEF_STR( 1C_6C ) ) \
	PORT_DIPSETTING(    0x00, "Free Play (if Coin B too) or 1/1") \
	PORT_DIPNAME( 0xf0, 0xf0, DEF_STR( Coin_B ) ) \
	PORT_DIPSETTING(    0x70, DEF_STR( 4C_1C ) ) \
	PORT_DIPSETTING(    0x80, DEF_STR( 3C_1C ) ) \
	PORT_DIPSETTING(    0x90, DEF_STR( 2C_1C ) ) \
	PORT_DIPSETTING(    0x50, "2 Coins/1 Credit 5/3 6/4") \
	PORT_DIPSETTING(    0x40, "2 Coins/1 Credit 4/3") \
	PORT_DIPSETTING(    0xf0, DEF_STR( 1C_1C ) ) \
	PORT_DIPSETTING(    0x10, "1 Coin/1 Credit 2/3") \
	PORT_DIPSETTING(    0x20, "1 Coin/1 Credit 4/5") \
	PORT_DIPSETTING(    0x30, "1 Coin/1 Credit 5/6") \
	PORT_DIPSETTING(    0x60, DEF_STR( 2C_3C ) ) \
	PORT_DIPSETTING(    0xe0, DEF_STR( 1C_2C ) ) \
	PORT_DIPSETTING(    0xd0, DEF_STR( 1C_3C ) ) \
	PORT_DIPSETTING(    0xc0, DEF_STR( 1C_4C ) ) \
	PORT_DIPSETTING(    0xb0, DEF_STR( 1C_5C ) ) \
	PORT_DIPSETTING(    0xa0, DEF_STR( 1C_6C ) ) \
	PORT_DIPSETTING(    0x00, "Free Play (if Coin A too) or 1/1")

/***************************************************************************/
// sys16B
ROM_START( aliensyn )
	ROM_REGION( 0x030000, REGION_CPU1 ) /* 68000 code */
	ROM_LOAD_EVEN( "11083.a4", 0x00000, 0x8000, 0xcb2ad9b3 )
	ROM_LOAD_ODD ( "11080.a1", 0x00000, 0x8000, 0xfe7378d9 )
	ROM_LOAD_EVEN( "11084.a5", 0x10000, 0x8000, 0x2e1ec7b1 )
	ROM_LOAD_ODD ( "11081.a2", 0x10000, 0x8000, 0x1308ee63 )
	ROM_LOAD_EVEN( "11085.a6", 0x20000, 0x8000, 0xcff78f39 )
	ROM_LOAD_ODD ( "11082.a3", 0x20000, 0x8000, 0x9cdc2a14 )

	ROM_REGION( 0x30000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "10702.b9",  0x00000, 0x10000, 0x393bc813 )
	ROM_LOAD( "10703.b10", 0x10000, 0x10000, 0x6b6dd9f5 )
	ROM_LOAD( "10704.b11", 0x20000, 0x10000, 0x911e7ebc )

	ROM_REGION( 0x080000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "10709.b1", 0x00000, 0x10000, 0xaddf0a90 )
	ROM_LOAD( "10713.b5", 0x10000, 0x10000, 0xececde3a )
	ROM_LOAD( "10710.b2", 0x20000, 0x10000, 0x992369eb )
	ROM_LOAD( "10714.b6", 0x30000, 0x10000, 0x91bf42fb )
	ROM_LOAD( "10711.b3", 0x40000, 0x10000, 0x29166ef6 )
	ROM_LOAD( "10715.b7", 0x50000, 0x10000, 0xa7c57384 )
	ROM_LOAD( "10712.b4", 0x60000, 0x10000, 0x876ad019 )
	ROM_LOAD( "10716.b8", 0x70000, 0x10000, 0x40ba1d48 )

	ROM_REGION( 0x28000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "10723.a7", 0x0000, 0x8000, 0x99953526 )
	ROM_LOAD( "10724.a8", 0x10000, 0x8000, 0xf971a817 )
	ROM_LOAD( "10725.a9", 0x18000, 0x8000, 0x6a50e08f )
	ROM_LOAD( "10726.a10",0x20000, 0x8000, 0xd50b7736 )
ROM_END

// sys16A - use a different sound chip?
ROM_START( aliensya )
	ROM_REGION( 0x030000, REGION_CPU1 ) /* 68000 code. I guessing the order a bit here */
	ROM_LOAD_EVEN( "10808", 0x00000, 0x8000, 0xe669929f )
	ROM_LOAD_ODD ( "10806", 0x00000, 0x8000, 0x9f7f8fdd )
	ROM_LOAD_EVEN( "10809", 0x10000, 0x8000, 0x9a424919 )
	ROM_LOAD_ODD ( "10807", 0x10000, 0x8000, 0x3d2c3530 )
	ROM_LOAD_EVEN( "10701", 0x20000, 0x8000, 0x92171751 )
	ROM_LOAD_ODD ( "10698", 0x20000, 0x8000, 0xc1e4fdc0 )

	ROM_REGION( 0x30000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "10739", 0x00000, 0x10000, 0xa29ec207 )
	ROM_LOAD( "10740", 0x10000, 0x10000, 0x47f93015 )
	ROM_LOAD( "10741", 0x20000, 0x10000, 0x4970739c )

	ROM_REGION( 0x080000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "10709.b1", 0x00000, 0x10000, 0xaddf0a90 )
	ROM_LOAD( "10713.b5", 0x10000, 0x10000, 0xececde3a )
	ROM_LOAD( "10710.b2", 0x20000, 0x10000, 0x992369eb )
	ROM_LOAD( "10714.b6", 0x30000, 0x10000, 0x91bf42fb )
	ROM_LOAD( "10711.b3", 0x40000, 0x10000, 0x29166ef6 )
	ROM_LOAD( "10715.b7", 0x50000, 0x10000, 0xa7c57384 )
	ROM_LOAD( "10712.b4", 0x60000, 0x10000, 0x876ad019 )
	ROM_LOAD( "10716.b8", 0x70000, 0x10000, 0x40ba1d48 )

	ROM_REGION( 0x28000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "10705", 0x00000, 0x8000, 0x777b749e )
	ROM_LOAD( "10706", 0x10000, 0x8000, 0xaa114acc )
	ROM_LOAD( "10707", 0x18000, 0x8000, 0x800c1d82 )
	ROM_LOAD( "10708", 0x20000, 0x8000, 0x5921ef52 )
ROM_END

ROM_START( aliensyj )
	ROM_REGION( 0x030000, REGION_CPU1 ) /* Custom 68000 code . I guessing the order a bit here */
// custom cpu 317-0033
	ROM_LOAD_EVEN( "epr10699.43", 0x00000, 0x8000, 0x3fd38d17 )
	ROM_LOAD_ODD ( "epr10696.26", 0x00000, 0x8000, 0xd734f19f )
	ROM_LOAD_EVEN( "epr10700.42", 0x10000, 0x8000, 0x3b04b252 )
	ROM_LOAD_ODD ( "epr10697.25", 0x10000, 0x8000, 0xf2bc123d )
	ROM_LOAD_EVEN( "10701", 0x20000, 0x8000, 0x92171751 )
	ROM_LOAD_ODD ( "10698", 0x20000, 0x8000, 0xc1e4fdc0 )

	ROM_REGION( 0x30000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "10739", 0x00000, 0x10000, 0xa29ec207 )
	ROM_LOAD( "10740", 0x10000, 0x10000, 0x47f93015 )
	ROM_LOAD( "10741", 0x20000, 0x10000, 0x4970739c )

	ROM_REGION( 0x080000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "10709.b1", 0x00000, 0x10000, 0xaddf0a90 )
	ROM_LOAD( "10713.b5", 0x10000, 0x10000, 0xececde3a )
	ROM_LOAD( "10710.b2", 0x20000, 0x10000, 0x992369eb )
	ROM_LOAD( "10714.b6", 0x30000, 0x10000, 0x91bf42fb )
	ROM_LOAD( "10711.b3", 0x40000, 0x10000, 0x29166ef6 )
	ROM_LOAD( "10715.b7", 0x50000, 0x10000, 0xa7c57384 )
	ROM_LOAD( "10712.b4", 0x60000, 0x10000, 0x876ad019 )
	ROM_LOAD( "10716.b8", 0x70000, 0x10000, 0x40ba1d48 )

	ROM_REGION( 0x28000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "10705", 0x00000, 0x8000, 0x777b749e )
	ROM_LOAD( "10706", 0x10000, 0x8000, 0xaa114acc )
	ROM_LOAD( "10707", 0x18000, 0x8000, 0x800c1d82 )
	ROM_LOAD( "10708", 0x20000, 0x8000, 0x5921ef52 )
ROM_END


ROM_START( aliensyb )
	ROM_REGION( 0x030000, REGION_CPU1 ) /* 68000 code */
	ROM_LOAD_EVEN( "as_typeb.a4", 0x00000, 0x8000, 0x17bf5304 )
	ROM_LOAD_ODD ( "as_typeb.a1", 0x00000, 0x8000, 0x4cd134df )
	ROM_LOAD_EVEN( "as_typeb.a5", 0x10000, 0x8000, 0xc8b791b0 )
	ROM_LOAD_ODD ( "as_typeb.a2", 0x10000, 0x8000, 0xbdcf4a30 )
	ROM_LOAD_EVEN( "as_typeb.a6", 0x20000, 0x8000, 0x1d0790aa )
	ROM_LOAD_ODD ( "as_typeb.a3", 0x20000, 0x8000, 0x1e7586b7 )

	ROM_REGION( 0x30000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "10702.b9",  0x00000, 0x10000, 0x393bc813 )
	ROM_LOAD( "10703.b10", 0x10000, 0x10000, 0x6b6dd9f5 )
	ROM_LOAD( "10704.b11", 0x20000, 0x10000, 0x911e7ebc )

	ROM_REGION( 0x080000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "10709.b1", 0x00000, 0x10000, 0xaddf0a90 )
	ROM_LOAD( "10713.b5", 0x10000, 0x10000, 0xececde3a )
	ROM_LOAD( "10710.b2", 0x20000, 0x10000, 0x992369eb )
	ROM_LOAD( "10714.b6", 0x30000, 0x10000, 0x91bf42fb )
	ROM_LOAD( "10711.b3", 0x40000, 0x10000, 0x29166ef6 )
	ROM_LOAD( "10715.b7", 0x50000, 0x10000, 0xa7c57384 )
	ROM_LOAD( "10712.b4", 0x60000, 0x10000, 0x876ad019 )
	ROM_LOAD( "10716.b8", 0x70000, 0x10000, 0x40ba1d48 )

	ROM_REGION( 0x28000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "10723.a7", 0x0000, 0x8000, 0x99953526 )
	ROM_LOAD( "10724.a8", 0x10000, 0x8000, 0xf971a817 )
	ROM_LOAD( "10725.a9", 0x18000, 0x8000, 0x6a50e08f )
	ROM_LOAD( "10726.a10",0x20000, 0x8000, 0xd50b7736 )
ROM_END




/***************************************************************************/

static const struct MemoryReadAddress aliensyn_readmem[] =
{
	{ 0x000000, 0x02ffff, MRA_ROM },
	{ 0x400000, 0x40ffff, MRA_TILERAM },
	{ 0x410000, 0x410fff, MRA_TEXTRAM },
	{ 0x440000, 0x440fff, MRA_SPRITERAM },
	{ 0x840000, 0x840fff, MRA_PALETTERAM },
	{ 0xc41002, 0xc41003, io_player1_r },
	{ 0xc41006, 0xc41007, io_player2_r },
	{ 0xc41000, 0xc41001, io_service_r },
	{ 0xc42002, 0xc42003, io_dip1_r },
	{ 0xc42000, 0xc42001, io_dip2_r },
	{ 0xc40000, 0xc40fff, MRA_EXTRAM },
	{ 0xffc000, 0xffffff, MRA_WORKINGRAM },
	{-1}
};

static const struct MemoryWriteAddress aliensyn_writemem[] =
{
	{ 0x000000, 0x02ffff, MWA_ROM },
	{ 0x400000, 0x40ffff, MWA_TILERAM },
	{ 0x410000, 0x410fff, MWA_TEXTRAM },
	{ 0x440000, 0x440fff, MWA_SPRITERAM },
	{ 0x840000, 0x840fff, MWA_PALETTERAM },
	{ 0xc00006, 0xc00007, sound_command_w },
	{ 0xc40000, 0xc40fff, MWA_EXTRAM },
	{ 0xffc000, 0xffffff, MWA_WORKINGRAM },
	{-1}
};

/***************************************************************************/

static void aliensyn_update_proc( void ){
	sys16_fg_scrollx = READ_WORD( &sys16_textram[0x0e98] );
	sys16_bg_scrollx = READ_WORD( &sys16_textram[0x0e9a] );
	sys16_fg_scrolly = READ_WORD( &sys16_textram[0x0e90] );
	sys16_bg_scrolly = READ_WORD( &sys16_textram[0x0e92] );

	set_fg_page( READ_WORD( &sys16_textram[0x0e80] ) );
	set_bg_page( READ_WORD( &sys16_textram[0x0e82] ) );

	set_refresh( READ_WORD( &sys16_extraram[0] ) ); // 0xc40001
}

static void aliensyn_init_machine( void ){
	static int bank[16] = { 0,0,0,0,0,0,0,6,0,0,0,4,0,2,0,0 };
	sys16_obj_bank = bank;
	sys16_bg_priority_mode=1;
	sys16_fg_priority_mode=1;

	sys16_update_proc = aliensyn_update_proc;
}

static void aliensyn_sprite_decode( void ){
	sys16_sprite_decode( 4,0x20000 );
}

static void init_aliensyn( void )
{
	sys16_onetime_init_machine();
	sys16_bg1_trans=1;
	aliensyn_sprite_decode();
}

/***************************************************************************/

INPUT_PORTS_START( aliensyn )
	SYS16_JOY1
	SYS16_JOY2
	SYS16_SERVICE
	SYS16_COINAGE

PORT_START	/* DSW1 */
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Unused ) )
	PORT_DIPSETTING(    0x01, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x02, 0x00, DEF_STR( Demo_Sounds ) )
	PORT_DIPSETTING(    0x02, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x0c, 0x0c, DEF_STR( Lives ) )
	PORT_DIPSETTING(    0x08, "2" )
	PORT_DIPSETTING(    0x0c, "3" )
	PORT_DIPSETTING(    0x04, "4" )
	PORT_BITX( 0,       0x00, IPT_DIPSWITCH_SETTING | IPF_CHEAT, "127", IP_KEY_NONE, IP_JOY_NONE )
	PORT_DIPNAME( 0x30, 0x30, "Timer" )
	PORT_DIPSETTING(    0x00, "120" )
	PORT_DIPSETTING(    0x10, "130" )
	PORT_DIPSETTING(    0x20, "140" )
	PORT_DIPSETTING(    0x30, "150" )
	PORT_DIPNAME( 0xc0, 0xc0, DEF_STR( Difficulty ) )
	PORT_DIPSETTING(    0x80, "Easy" )
	PORT_DIPSETTING(    0xc0, "Normal" )
	PORT_DIPSETTING(    0x40, "Hard" )
	PORT_DIPSETTING(    0x00, "Hardest" )
INPUT_PORTS_END

/***************************************************************************/

static const struct UPD7759_interface aliensyn_upd7759_interface =
{
	1,			/* 1 chip */
	480000,
	{ 60 }, 	/* volumes */
	{ REGION_CPU2 },			/* memory region 3 contains the sample data */
    UPD7759_SLAVE_MODE,
	{ sound_cause_nmi },
};

/****************************************************************************/

MACHINE_DRIVER_7759( machine_driver_aliensyn, \
	aliensyn_readmem,aliensyn_writemem,aliensyn_init_machine, gfx1, aliensyn_upd7759_interface )

/***************************************************************************/
// sys16B
ROM_START( altbeast )
	ROM_REGION( 0x040000, REGION_CPU1 ) /* 68000 code */
	ROM_LOAD_EVEN( "11705", 0x000000, 0x20000, 0x57dc5c7a )
	ROM_LOAD_ODD ( "11704", 0x000000, 0x20000, 0x33bbcf07 )

	ROM_REGION( 0x60000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "11674", 0x00000, 0x20000, 0xa57a66d5 )
	ROM_LOAD( "11675", 0x20000, 0x20000, 0x2ef2f144 )
	ROM_LOAD( "11676", 0x40000, 0x20000, 0x0c04acac )

	ROM_REGION( 0x100000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "epr11677.b1", 0x00000, 0x10000, 0xa01425cd )
	ROM_CONTINUE(            0x20000, 0x10000 )
	ROM_LOAD( "epr11681.b5", 0x10000, 0x10000, 0xd9e03363 )
	ROM_CONTINUE(            0x30000, 0x10000 )
	ROM_LOAD( "epr11678.b2", 0x40000, 0x10000, 0x17a9fc53 )
	ROM_CONTINUE(            0x60000, 0x10000 )
	ROM_LOAD( "epr11682.b6", 0x50000, 0x10000, 0xe3f77c5e )
	ROM_CONTINUE(            0x70000, 0x10000 )
	ROM_LOAD( "epr11679.b3", 0x80000, 0x10000, 0x14dcc245 )
	ROM_CONTINUE(            0xa0000, 0x10000 )
	ROM_LOAD( "epr11683.b7", 0x90000, 0x10000, 0xf9a60f06 )
	ROM_CONTINUE(            0xb0000, 0x10000 )
	ROM_LOAD( "epr11680.b4", 0xc0000, 0x10000, 0xf43dcdec )
	ROM_CONTINUE(            0xe0000, 0x10000 )
	ROM_LOAD( "epr11684.b8", 0xd0000, 0x10000, 0xb20c0edb )
	ROM_CONTINUE(            0xf0000, 0x10000 )

	ROM_REGION( 0x50000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "11671",		 0x00000, 0x08000, 0x2b71343b )
	ROM_LOAD( "opr11672",    0x10000, 0x20000, 0xbbd7f460 )
	ROM_LOAD( "opr11673",    0x30000, 0x20000, 0x400c4a36 )
ROM_END

ROM_START( jyuohki )
	ROM_REGION( 0x040000, REGION_CPU1 ) /* Custom 68000 code. */
// custom cpu 317-0065
	ROM_LOAD_EVEN( "epr11670.a7", 0x000000, 0x20000, 0xb748eb07 )
	ROM_LOAD_ODD ( "epr11669.a5", 0x000000, 0x20000, 0x005ecd11 )

	ROM_REGION( 0x60000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "11674", 0x00000, 0x20000, 0xa57a66d5 )
	ROM_LOAD( "11675", 0x20000, 0x20000, 0x2ef2f144 )
	ROM_LOAD( "11676", 0x40000, 0x20000, 0x0c04acac )

	ROM_REGION( 0x100000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "epr11677.b1", 0x00000, 0x10000, 0xa01425cd )
	ROM_CONTINUE(            0x20000, 0x10000 )
	ROM_LOAD( "epr11681.b5", 0x10000, 0x10000, 0xd9e03363 )
	ROM_CONTINUE(            0x30000, 0x10000 )
	ROM_LOAD( "epr11678.b2", 0x40000, 0x10000, 0x17a9fc53 )
	ROM_CONTINUE(            0x60000, 0x10000 )
	ROM_LOAD( "epr11682.b6", 0x50000, 0x10000, 0xe3f77c5e )
	ROM_CONTINUE(            0x70000, 0x10000 )
	ROM_LOAD( "epr11679.b3", 0x80000, 0x10000, 0x14dcc245 )
	ROM_CONTINUE(            0xa0000, 0x10000 )
	ROM_LOAD( "epr11683.b7", 0x90000, 0x10000, 0xf9a60f06 )
	ROM_CONTINUE(            0xb0000, 0x10000 )
	ROM_LOAD( "epr11680.b4", 0xc0000, 0x10000, 0xf43dcdec )
	ROM_CONTINUE(            0xe0000, 0x10000 )
	ROM_LOAD( "epr11684.b8", 0xd0000, 0x10000, 0xb20c0edb )
	ROM_CONTINUE(            0xf0000, 0x10000 )

	ROM_REGION( 0x50000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "11671",		 0x00000, 0x08000, 0x2b71343b )
	ROM_LOAD( "opr11672",    0x10000, 0x20000, 0xbbd7f460 )
	ROM_LOAD( "opr11673",    0x30000, 0x20000, 0x400c4a36 )
ROM_END

// sys16B
ROM_START( altbeas2 )
	ROM_REGION( 0x040000, REGION_CPU1 ) /* 68000 code */
	ROM_LOAD_EVEN( "epr11740", 0x000000, 0x20000, 0xce227542 )
	ROM_LOAD_ODD ( "epr11739", 0x000000, 0x20000, 0xe466eb65 )

	ROM_REGION( 0x60000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "11674", 0x00000, 0x20000, 0xa57a66d5 )
	ROM_LOAD( "11675", 0x20000, 0x20000, 0x2ef2f144 )
	ROM_LOAD( "11676", 0x40000, 0x20000, 0x0c04acac )

	ROM_REGION( 0x100000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "epr11677.b1", 0x00000, 0x10000, 0xa01425cd )
	ROM_CONTINUE(            0x20000, 0x10000 )
	ROM_LOAD( "epr11681.b5", 0x10000, 0x10000, 0xd9e03363 )
	ROM_CONTINUE(            0x30000, 0x10000 )
	ROM_LOAD( "epr11678.b2", 0x40000, 0x10000, 0x17a9fc53 )
	ROM_CONTINUE(            0x60000, 0x10000 )
	ROM_LOAD( "epr11682.b6", 0x50000, 0x10000, 0xe3f77c5e )
	ROM_CONTINUE(            0x70000, 0x10000 )
	ROM_LOAD( "epr11679.b3", 0x80000, 0x10000, 0x14dcc245 )
	ROM_CONTINUE(            0xa0000, 0x10000 )
	ROM_LOAD( "epr11683.b7", 0x90000, 0x10000, 0xf9a60f06 )
	ROM_CONTINUE(            0xb0000, 0x10000 )
	ROM_LOAD( "epr11680.b4", 0xc0000, 0x10000, 0xf43dcdec )
	ROM_CONTINUE(            0xe0000, 0x10000 )
	ROM_LOAD( "epr11684.b8", 0xd0000, 0x10000, 0xb20c0edb )
	ROM_CONTINUE(            0xf0000, 0x10000 )

	ROM_REGION( 0x50000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "opr11686",	 0x00000, 0x08000, 0x828a45b3 )	// ???
	ROM_LOAD( "opr11672",    0x10000, 0x20000, 0xbbd7f460 )
	ROM_LOAD( "opr11673",    0x30000, 0x20000, 0x400c4a36 )
ROM_END



/***************************************************************************/

static READ_HANDLER( altbeast_skip_r )
{
	if (cpu_get_pc()==0x3994) {cpu_spinuntil_int(); return 1<<8;}

	return READ_WORD(&sys16_workingram[0x301c]);
}

// ??? What is this, input test shows 4 bits to each player, but what does it do?
static READ_HANDLER( altbeast_io_r )
{
	return 0xff;
}

static const struct MemoryReadAddress altbeast_readmem[] =
{
	{ 0x000000, 0x03ffff, MRA_ROM },
	{ 0x400000, 0x40ffff, MRA_TILERAM },
	{ 0x410000, 0x410fff, MRA_TEXTRAM },
	{ 0x440000, 0x440fff, MRA_SPRITERAM },
	{ 0x840000, 0x840fff, MRA_PALETTERAM },
	{ 0xc41002, 0xc41003, io_player1_r },
	{ 0xc41006, 0xc41007, io_player2_r },
	{ 0xc41004, 0xc41005, altbeast_io_r },
	{ 0xc41000, 0xc41001, io_service_r },
	{ 0xc42002, 0xc42003, io_dip1_r },
	{ 0xc42000, 0xc42001, io_dip2_r },
	{ 0xc40000, 0xc40fff, MRA_EXTRAM },
	{ 0xfff01c, 0xfff01d, altbeast_skip_r },
	{ 0xffc000, 0xffffff, MRA_WORKINGRAM },
	{-1}
};

static const struct MemoryWriteAddress altbeast_writemem[] =
{
	{ 0x000000, 0x03ffff, MWA_ROM },
	{ 0x400000, 0x40ffff, MWA_TILERAM },
	{ 0x410000, 0x410fff, MWA_TEXTRAM },
	{ 0x440000, 0x440fff, MWA_SPRITERAM },
	{ 0x840000, 0x840fff, MWA_PALETTERAM },
	{ 0xc40000, 0xc40fff, MWA_EXTRAM },
	{ 0xfe0006, 0xfe0007, sound_command_w },
	{ 0xffc000, 0xffffff, MWA_WORKINGRAM },
	{-1}
};

/***************************************************************************/

static void altbeast_update_proc( void ){
	sys16_fg_scrollx = READ_WORD( &sys16_textram[0x0e98] );
	sys16_bg_scrollx = READ_WORD( &sys16_textram[0x0e9a] );
	sys16_fg_scrolly = READ_WORD( &sys16_textram[0x0e90] );
	sys16_bg_scrolly = READ_WORD( &sys16_textram[0x0e92] );

	set_fg_page( READ_WORD( &sys16_textram[0x0e80] ) );
	set_bg_page( READ_WORD( &sys16_textram[0x0e82] ) );

	set_tile_bank( READ_WORD( &sys16_workingram[0x3094] ) );
	set_refresh( READ_WORD( &sys16_extraram[0] ) );
}

static void altbeast_init_machine( void ){
	static int bank[16] = {0x00,0x02,0x04,0x06,0x08,0x0A,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
	sys16_obj_bank = bank;
	sys16_update_proc = altbeast_update_proc;
}

static void altbeas2_init_machine( void ){
	static int bank[16] = {0x00,0x00,0x02,0x00,0x04,0x00,0x06,0x00,0x08,0x00,0x0A,0x00,0x0C,0x00,0x00,0x00};
	sys16_obj_bank = bank;
	sys16_update_proc = altbeast_update_proc;
}

static void init_altbeast( void )
{
	sys16_onetime_init_machine();
	sys16_sprite_decode( 7,0x20000 );
}

/***************************************************************************/

INPUT_PORTS_START( altbeast )
	SYS16_JOY1
	SYS16_JOY2
	SYS16_SERVICE
	SYS16_COINAGE

PORT_START	/* DSW1 */
	PORT_DIPNAME( 0x01, 0x01, "Credits needed" )
	PORT_DIPSETTING(    0x01, "1 to start, 1 to continue")
	PORT_DIPSETTING(    0x00, "2 to start, 1 to continue")
	PORT_DIPNAME( 0x02, 0x00, DEF_STR( Demo_Sounds ) )
	PORT_DIPSETTING(    0x02, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x0c, 0x0c, DEF_STR( Lives ) )
	PORT_DIPSETTING(    0x08, "2" )
	PORT_DIPSETTING(    0x0c, "3" )
	PORT_DIPSETTING(    0x04, "4" )
	PORT_BITX( 0,       0x00, IPT_DIPSWITCH_SETTING | IPF_CHEAT, "240", IP_KEY_NONE, IP_JOY_NONE )
	PORT_DIPNAME( 0x30, 0x30, "Energy Meter" )
	PORT_DIPSETTING(    0x20, "2" )
	PORT_DIPSETTING(    0x30, "3" )
	PORT_DIPSETTING(    0x10, "4" )
	PORT_DIPSETTING(    0x00, "5" )
	PORT_DIPNAME( 0xc0, 0xc0, DEF_STR( Difficulty ) )
	PORT_DIPSETTING(    0x80, "Easy" )
	PORT_DIPSETTING(    0xc0, "Normal" )
	PORT_DIPSETTING(    0x40, "Hard" )
	PORT_DIPSETTING(    0x00, "Hardest" )
INPUT_PORTS_END

/***************************************************************************/

MACHINE_DRIVER_7759( machine_driver_altbeast, \
	altbeast_readmem,altbeast_writemem,altbeast_init_machine, gfx2,upd7759_interface )

MACHINE_DRIVER_7759( machine_driver_altbeas2, \
	altbeast_readmem,altbeast_writemem,altbeas2_init_machine, gfx2,upd7759_interface )
// sys16B
ROM_START( aurail )
	ROM_REGION( 0xc0000, REGION_CPU1 ) /* 68000 code */
	ROM_LOAD_EVEN( "13577", 0x000000, 0x20000, 0x6701b686 )
	ROM_LOAD_ODD ( "13576", 0x000000, 0x20000, 0x1e428d94 )
	/* empty 0x40000 - 0x80000 */
	ROM_LOAD_EVEN( "13447", 0x080000, 0x20000, 0x70a52167 )
	ROM_LOAD_ODD ( "13445", 0x080000, 0x20000, 0x28dfc3dd )

	ROM_REGION( 0xc0000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "aurail.a14", 0x00000, 0x20000, 0x0fc4a7a8 ) /* plane 1 */
	ROM_LOAD( "aurail.b14", 0x20000, 0x20000, 0xe08135e0 )
	ROM_LOAD( "aurail.a15", 0x40000, 0x20000, 0x1c49852f ) /* plane 2 */
	ROM_LOAD( "aurail.b15", 0x60000, 0x20000, 0xe14c6684 )
	ROM_LOAD( "aurail.a16", 0x80000, 0x20000, 0x047bde5e ) /* plane 3 */
	ROM_LOAD( "aurail.b16", 0xa0000, 0x20000, 0x6309fec4 )

	ROM_REGION( 0x200000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "aurail.b1",  0x000000, 0x020000, 0x5fa0a9f8 )
	ROM_LOAD( "aurail.b5",  0x020000, 0x020000, 0x0d1b54da )
	ROM_LOAD( "aurail.b2",  0x040000, 0x020000, 0x5f6b33b1 )
	ROM_LOAD( "aurail.b6",  0x060000, 0x020000, 0xbad340c3 )
	ROM_LOAD( "aurail.b3",  0x080000, 0x020000, 0x4e80520b )
	ROM_LOAD( "aurail.b7",  0x0a0000, 0x020000, 0x7e9165ac )
	ROM_LOAD( "aurail.b4",  0x0c0000, 0x020000, 0x5733c428 )
	ROM_LOAD( "aurail.b8",  0x0e0000, 0x020000, 0x66b8f9b3 )
	ROM_LOAD( "aurail.a1",  0x100000, 0x020000, 0x4f370b2b )
	ROM_LOAD( "aurail.b10", 0x120000, 0x020000, 0xf76014bf )
	ROM_LOAD( "aurail.a2",  0x140000, 0x020000, 0x37cf9cb4 )
	ROM_LOAD( "aurail.b11", 0x160000, 0x020000, 0x1061e7da )
	ROM_LOAD( "aurail.a3",  0x180000, 0x020000, 0x049698ef )
	ROM_LOAD( "aurail.b12", 0x1a0000, 0x020000, 0x7dbcfbf1 )
	ROM_LOAD( "aurail.a4",  0x1c0000, 0x020000, 0x77a8989e )
	ROM_LOAD( "aurail.b13", 0x1e0000, 0x020000, 0x551df422 )

	ROM_REGION( 0x50000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "13448",      0x0000, 0x8000, 0xb5183fb9 )
	ROM_LOAD( "aurail.a12", 0x10000,0x20000, 0xd3d9aaf9 )
	ROM_LOAD( "aurail.a12", 0x30000,0x20000, 0xd3d9aaf9 )
ROM_END

ROM_START( auraila )
	ROM_REGION( 0xc0000, REGION_CPU1 ) /* 68000 code */
// custom cpu 317-0168
	ROM_LOAD_EVEN( "epr13469.a7", 0x000000, 0x20000, 0xc628b69d )
	ROM_LOAD_ODD ( "epr13468.a5", 0x000000, 0x20000, 0xce092218 )
	/* 0x40000 - 0x80000 is empty, I will place decrypted opcodes here */
	ROM_LOAD_EVEN( "13447", 0x080000, 0x20000, 0x70a52167 )
	ROM_LOAD_ODD ( "13445", 0x080000, 0x20000, 0x28dfc3dd )

	ROM_REGION( 0xc0000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "aurail.a14", 0x00000, 0x20000, 0x0fc4a7a8 ) /* plane 1 */
	ROM_LOAD( "aurail.b14", 0x20000, 0x20000, 0xe08135e0 )
	ROM_LOAD( "aurail.a15", 0x40000, 0x20000, 0x1c49852f ) /* plane 2 */
	ROM_LOAD( "aurail.b15", 0x60000, 0x20000, 0xe14c6684 )
	ROM_LOAD( "aurail.a16", 0x80000, 0x20000, 0x047bde5e ) /* plane 3 */
	ROM_LOAD( "aurail.b16", 0xa0000, 0x20000, 0x6309fec4 )

	ROM_REGION( 0x200000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "aurail.b1",  0x000000, 0x020000, 0x5fa0a9f8 )
	ROM_LOAD( "aurail.b5",  0x020000, 0x020000, 0x0d1b54da )
	ROM_LOAD( "aurail.b2",  0x040000, 0x020000, 0x5f6b33b1 )
	ROM_LOAD( "aurail.b6",  0x060000, 0x020000, 0xbad340c3 )
	ROM_LOAD( "aurail.b3",  0x080000, 0x020000, 0x4e80520b )
	ROM_LOAD( "aurail.b7",  0x0a0000, 0x020000, 0x7e9165ac )
	ROM_LOAD( "aurail.b4",  0x0c0000, 0x020000, 0x5733c428 )
	ROM_LOAD( "aurail.b8",  0x0e0000, 0x020000, 0x66b8f9b3 )
	ROM_LOAD( "aurail.a1",  0x100000, 0x020000, 0x4f370b2b )
	ROM_LOAD( "aurail.b10", 0x120000, 0x020000, 0xf76014bf )
	ROM_LOAD( "aurail.a2",  0x140000, 0x020000, 0x37cf9cb4 )
	ROM_LOAD( "aurail.b11", 0x160000, 0x020000, 0x1061e7da )
	ROM_LOAD( "aurail.a3",  0x180000, 0x020000, 0x049698ef )
	ROM_LOAD( "aurail.b12", 0x1a0000, 0x020000, 0x7dbcfbf1 )
	ROM_LOAD( "aurail.a4",  0x1c0000, 0x020000, 0x77a8989e )
	ROM_LOAD( "aurail.b13", 0x1e0000, 0x020000, 0x551df422 )

	ROM_REGION( 0x50000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "13448",      0x0000, 0x8000, 0xb5183fb9 )
	ROM_LOAD( "aurail.a12", 0x10000,0x20000, 0xd3d9aaf9 )
	ROM_LOAD( "aurail.a12", 0x30000,0x20000, 0xd3d9aaf9 )
ROM_END


/***************************************************************************/

static READ_HANDLER( aurail_skip_r )
{
	if (cpu_get_pc()==0xe4e) {cpu_spinuntil_int(); return 0;}

	return READ_WORD(&sys16_workingram[0x274e]);
}

static const struct MemoryReadAddress aurail_readmem[] =
{
	{ 0x000000, 0x0bffff, MRA_ROM },
	{ 0x3f0000, 0x3fffff, MRA_EXTRAM },
	{ 0x400000, 0x40ffff, MRA_TILERAM },
	{ 0x410000, 0x410fff, MRA_TEXTRAM },
	{ 0x440000, 0x440fff, MRA_SPRITERAM },
	{ 0x840000, 0x840fff, MRA_PALETTERAM },
	{ 0xc41002, 0xc41003, io_player1_r },
	{ 0xc41006, 0xc41007, io_player2_r },
	{ 0xc41000, 0xc41001, io_service_r },
	{ 0xc42002, 0xc42003, io_dip1_r },
	{ 0xc42000, 0xc42001, io_dip2_r },
	{ 0xc40000, 0xc4ffff, MRA_EXTRAM2 },
	{ 0xfc0000, 0xfc0fff, MRA_EXTRAM3 },
	{ 0xffe74e, 0xffe74f, aurail_skip_r },
	{ 0xffc000, 0xffffff, MRA_WORKINGRAM },
	{-1}
};

static const struct MemoryWriteAddress aurail_writemem[] =
{
	{ 0x000000, 0x0bffff, MWA_ROM },
	{ 0x3f0000, 0x3fffff, MWA_EXTRAM },
	{ 0x400000, 0x40ffff, MWA_TILERAM },
	{ 0x410000, 0x410fff, MWA_TEXTRAM },
	{ 0x440000, 0x440fff, MWA_SPRITERAM },
	{ 0x840000, 0x840fff, MWA_PALETTERAM },
	{ 0xc40000, 0xc4ffff, MWA_EXTRAM2 },
	{ 0xfc0000, 0xfc0fff, MWA_EXTRAM3 },
	{ 0xfe0006, 0xfe0007, sound_command_w },
	{ 0xffc000, 0xffffff, MWA_WORKINGRAM },
	{-1}
};
/***************************************************************************/

static void aurail_update_proc (void)
{
	sys16_fg_scrollx = READ_WORD( &sys16_textram[0x0e98] );
	sys16_bg_scrollx = READ_WORD( &sys16_textram[0x0e9a] );
	sys16_fg_scrolly = READ_WORD( &sys16_textram[0x0e90] );
	sys16_bg_scrolly = READ_WORD( &sys16_textram[0x0e92] );

	set_fg_page( READ_WORD( &sys16_textram[0x0e80] ) );
	set_bg_page( READ_WORD( &sys16_textram[0x0e82] ) );

	set_tile_bank( READ_WORD( &sys16_extraram3[0x0002] ) );
	set_refresh( READ_WORD( &sys16_extraram2[0] ) );
}

static void aurail_init_machine( void ){
	static int bank[16] = {0x00,0x02,0x04,0x06,0x08,0x0A,0x0C,0x0E,0x10,0x12,0x14,0x16,0x18,0x1A,0x1C,0x1E};

	sys16_obj_bank = bank;
	sys16_spritesystem = 4;
	sys16_spritelist_end=0x8000;
	sys16_bg_priority_mode=1;

	sys16_update_proc = aurail_update_proc;
}

static void init_aurail (void)
{
	sys16_onetime_init_machine();
	sys16_sprite_decode (8,0x40000);
}

static void init_auraila(void)
{
	unsigned char *rom = memory_region(REGION_CPU1);
	int diff = 0x40000;	/* place decrypted opcodes in a empty hole */

	init_aurail();

	memory_set_opcode_base(0,rom+diff);

	memcpy(rom+diff,rom,0x40000);

	aurail_decode_data(rom,rom,0x10000);
	aurail_decode_opcode1(rom+diff,rom+diff,0x10000);
	aurail_decode_opcode2(rom+diff+0x10000,rom+diff+0x10000,0x10000);
}

/***************************************************************************/

INPUT_PORTS_START( aurail )
	SYS16_JOY1
	SYS16_JOY2
	SYS16_SERVICE
	SYS16_COINAGE

PORT_START	/* DSW1 */
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Cabinet ) )
	PORT_DIPSETTING(    0x01, DEF_STR( Upright ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Cocktail ) )
	PORT_DIPNAME( 0x02, 0x00, DEF_STR( Demo_Sounds ) )
	PORT_DIPSETTING(    0x02, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x0c, 0x0c, DEF_STR( Lives ) )
	PORT_DIPSETTING(    0x00, "2" )
	PORT_DIPSETTING(    0x0c, "3" )
	PORT_DIPSETTING(    0x08, "4" )
	PORT_DIPSETTING(    0x04, "5" )
	PORT_DIPNAME( 0x10, 0x10, DEF_STR( Bonus_Life ) )
	PORT_DIPSETTING(    0x10, "Normal" )
	PORT_DIPSETTING(    0x00, "Hard" )
	PORT_DIPNAME( 0x20, 0x20, DEF_STR( Difficulty ) )
	PORT_DIPSETTING(    0x20, "Normal" )
	PORT_DIPSETTING(    0x00, "Hard" )
	PORT_DIPNAME( 0x40, 0x40, "Controller select" )
	PORT_DIPSETTING(    0x40, "1 Player side" )
	PORT_DIPSETTING(    0x00, "2 Players side" )
	PORT_DIPNAME( 0x80, 0x80, DEF_STR( Unused ) )
	PORT_DIPSETTING(    0x80, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
INPUT_PORTS_END

/***************************************************************************/

MACHINE_DRIVER_7759( machine_driver_aurail, \
	aurail_readmem,aurail_writemem,aurail_init_machine, gfx4,upd7759_interface )

/***************************************************************************/
// sys16B
ROM_START( bayroute )
	ROM_REGION( 0xc0000, REGION_CPU1 ) /* 68000 code */
	ROM_LOAD_EVEN( "br.4a", 0x000000, 0x10000, 0x91c6424b )
	ROM_LOAD_ODD ( "br.1a", 0x000000, 0x10000, 0x76954bf3 )
	/* empty 0x20000-0x80000*/
	ROM_LOAD_EVEN( "br.5a", 0x080000, 0x10000, 0x9d6fd183 )
	ROM_LOAD_ODD ( "br.2a", 0x080000, 0x10000, 0x5ca1e3d2 )
	ROM_LOAD_EVEN( "br.6a", 0x0a0000, 0x10000, 0xed97ad4c )
	ROM_LOAD_ODD ( "br.3a", 0x0a0000, 0x10000, 0x0d362905 )

	ROM_REGION( 0x30000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "opr12462.a14", 0x00000, 0x10000, 0xa19943b5 )
	ROM_LOAD( "opr12463.a15", 0x10000, 0x10000, 0x62f8200d )
	ROM_LOAD( "opr12464.a16", 0x20000, 0x10000, 0xc8c59703 )

	ROM_REGION( 0x080000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "br_obj0o.1b", 0x00000, 0x10000, 0x098a5e82 )
	ROM_LOAD( "br_obj0e.5b", 0x10000, 0x10000, 0x85238af9 )
	ROM_LOAD( "br_obj1o.2b", 0x20000, 0x10000, 0xcc641da1 )
	ROM_LOAD( "br_obj1e.6b", 0x30000, 0x10000, 0xd3123315 )
	ROM_LOAD( "br_obj2o.3b", 0x40000, 0x10000, 0x84efac1f )
	ROM_LOAD( "br_obj2e.7b", 0x50000, 0x10000, 0xb73b12cb )
	ROM_LOAD( "br_obj3o.4b", 0x60000, 0x10000, 0xa2e238ac )
	ROM_LOAD( "br.8b",		 0x70000, 0x10000, 0xd8de78ff )

	ROM_REGION( 0x50000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "epr12459.a10", 0x00000, 0x08000, 0x3e1d29d0 )
	ROM_LOAD( "mpr12460.a11", 0x10000, 0x20000, 0x0bae570d )
	ROM_LOAD( "mpr12461.a12", 0x30000, 0x20000, 0xb03b8b46 )
ROM_END

ROM_START( bayrouta )
	ROM_REGION( 0xc0000, REGION_CPU1 ) /* 68000 code */
// custom cpu 317-0116
	ROM_LOAD_EVEN( "epr12517.a7", 0x000000, 0x20000, 0x436728a9 )
	ROM_LOAD_ODD ( "epr12516.a5", 0x000000, 0x20000, 0x4ff0353f )
	/* empty 0x40000-0x80000*/
	ROM_LOAD_EVEN( "epr12458.a8", 0x080000, 0x20000, 0xe7c7476a )
	ROM_LOAD_ODD ( "epr12456.a6", 0x080000, 0x20000, 0x25dc2eaf )

	ROM_REGION( 0x30000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "opr12462.a14", 0x00000, 0x10000, 0xa19943b5 )
	ROM_LOAD( "opr12463.a15", 0x10000, 0x10000, 0x62f8200d )
	ROM_LOAD( "opr12464.a16", 0x20000, 0x10000, 0xc8c59703 )

	ROM_REGION( 0x080000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "mpr12465.b1", 0x00000, 0x20000, 0x11d61b45 )
	ROM_LOAD( "mpr12467.b5", 0x20000, 0x20000, 0xc3b4e4c0 )
	ROM_LOAD( "mpr12466.b2", 0x40000, 0x20000, 0xa57f236f )
	ROM_LOAD( "mpr12468.b6", 0x60000, 0x20000, 0xd89c77de )

	ROM_REGION( 0x50000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "epr12459.a10", 0x00000, 0x08000, 0x3e1d29d0 )
	ROM_LOAD( "mpr12460.a11", 0x10000, 0x20000, 0x0bae570d )
	ROM_LOAD( "mpr12461.a12", 0x30000, 0x20000, 0xb03b8b46 )
ROM_END

ROM_START( bayrtbl1 )
	ROM_REGION( 0xc0000, REGION_CPU1 ) /* 68000 code */
	ROM_LOAD_EVEN( "b4.bin", 0x000000, 0x10000, 0xeb6646ae )
	ROM_LOAD_ODD ( "b2.bin", 0x000000, 0x10000, 0xecd9cd0e )
	/* empty 0x20000-0x80000*/
	ROM_LOAD_EVEN( "br.5a",  0x080000, 0x10000, 0x9d6fd183 )
	ROM_LOAD_ODD ( "br.2a",  0x080000, 0x10000, 0x5ca1e3d2 )
	ROM_LOAD_EVEN( "b8.bin", 0x0a0000, 0x10000, 0xe7ca0331 )
	ROM_LOAD_ODD ( "b6.bin", 0x0a0000, 0x10000, 0x2bc748a6 )

	ROM_REGION( 0x30000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "bs16.bin", 0x00000, 0x10000, 0xa8a5b310 )
	ROM_LOAD( "bs14.bin", 0x10000, 0x10000, 0x6bc4d0a8 )
	ROM_LOAD( "bs12.bin", 0x20000, 0x10000, 0xc1f967a6 )

	ROM_REGION( 0x080000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "br_obj0o.1b", 0x00000, 0x10000, 0x098a5e82 )
	ROM_LOAD( "br_obj0e.5b", 0x10000, 0x10000, 0x85238af9 )
	ROM_LOAD( "br_obj1o.2b", 0x20000, 0x10000, 0xcc641da1 )
	ROM_LOAD( "br_obj1e.6b", 0x30000, 0x10000, 0xd3123315 )
	ROM_LOAD( "br_obj2o.3b", 0x40000, 0x10000, 0x84efac1f )
	ROM_LOAD( "br_obj2e.7b", 0x50000, 0x10000, 0xb73b12cb )
	ROM_LOAD( "br_obj3o.4b", 0x60000, 0x10000, 0xa2e238ac )
	ROM_LOAD( "bs7.bin",     0x70000, 0x10000, 0x0c91abcc )

	ROM_REGION( 0x50000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "epr12459.a10", 0x00000, 0x08000, 0x3e1d29d0 )
	ROM_LOAD( "mpr12460.a11", 0x10000, 0x20000, 0x0bae570d )
	ROM_LOAD( "mpr12461.a12", 0x30000, 0x20000, 0xb03b8b46 )
ROM_END

ROM_START( bayrtbl2 )
	ROM_REGION( 0xc0000, REGION_CPU1 ) /* 68000 code */
	ROM_LOAD_EVEN( "br_04", 0x000000, 0x10000, 0x2e33ebfc )
	ROM_LOAD_ODD ( "br_06", 0x000000, 0x10000, 0x3db42313 )
	/* empty 0x20000-0x80000*/
	ROM_LOAD_EVEN( "br_03", 0x080000, 0x20000, 0x285d256b )
	ROM_LOAD_ODD ( "br_05", 0x080000, 0x20000, 0x552e6384 )

	ROM_REGION( 0x30000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "br_15",    0x00000, 0x10000, 0x050079a9 )
	ROM_LOAD( "br_16",    0x10000, 0x10000, 0xfc371928 )
	ROM_LOAD( "bs12.bin", 0x20000, 0x10000, 0xc1f967a6 )

	ROM_REGION( 0x080000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "br_11",       0x00000, 0x10000, 0x65232905 )
	ROM_LOAD( "br_obj0e.5b", 0x10000, 0x10000, 0x85238af9 )
	ROM_LOAD( "br_obj1o.2b", 0x20000, 0x10000, 0xcc641da1 )
	ROM_LOAD( "br_obj1e.6b", 0x30000, 0x10000, 0xd3123315 )
	ROM_LOAD( "br_obj2o.3b", 0x40000, 0x10000, 0x84efac1f )
	ROM_LOAD( "br_09",       0x50000, 0x10000, 0x05e9b840 )
	ROM_LOAD( "br_14",       0x60000, 0x10000, 0x4c4a177b )
	ROM_LOAD( "bs7.bin",     0x70000, 0x10000, 0x0c91abcc )

	ROM_REGION( 0x50000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "br_01", 0x00000, 0x10000, 0xb87156ec )
	ROM_LOAD( "br_02", 0x10000, 0x10000, 0xef63991b )
ROM_END

/***************************************************************************/

static const struct MemoryReadAddress bayroute_readmem[] =
{
	{ 0x000000, 0x0bffff, MRA_ROM },
	{ 0x500000, 0x503fff, MRA_EXTRAM3 },
	{ 0x600000, 0x600fff, MRA_SPRITERAM },
	{ 0x700000, 0x70ffff, MRA_TILERAM },
	{ 0x710000, 0x710fff, MRA_TEXTRAM },
	{ 0x800000, 0x800fff, MRA_PALETTERAM },
	{ 0x901002, 0x901003, io_player1_r },
	{ 0x901006, 0x901007, io_player2_r },
	{ 0x901000, 0x901001, io_service_r },
	{ 0x902002, 0x902003, io_dip1_r },
	{ 0x902000, 0x902001, io_dip2_r },
	{ 0x900000, 0x900fff, MRA_EXTRAM2 },

	{-1}
};

static const struct MemoryWriteAddress bayroute_writemem[] =
{
	{ 0x000000, 0x0bffff, MWA_ROM },
	{ 0x500000, 0x503fff, MWA_EXTRAM3 },
	{ 0x600000, 0x600fff, MWA_SPRITERAM },
	{ 0x700000, 0x70ffff, MWA_TILERAM },
	{ 0x710000, 0x710fff, MWA_TEXTRAM },
	{ 0x800000, 0x800fff, MWA_PALETTERAM },
	{ 0x900000, 0x900fff, MWA_EXTRAM2 },
	{ 0xff0006, 0xff0007, sound_command_w },

	{-1}
};

/***************************************************************************/

static void bayroute_update_proc( void ){
	sys16_fg_scrollx = READ_WORD( &sys16_textram[0x0e98] );
	sys16_bg_scrollx = READ_WORD( &sys16_textram[0x0e9a] );
	sys16_fg_scrolly = READ_WORD( &sys16_textram[0x0e90] );
	sys16_bg_scrolly = READ_WORD( &sys16_textram[0x0e92] );

	set_fg_page( READ_WORD( &sys16_textram[0x0e80] ) );
	set_bg_page( READ_WORD( &sys16_textram[0x0e82] ) );

	set_refresh( READ_WORD( &sys16_extraram2[0x0] ) );
}

static void bayroute_init_machine( void ){
	static int bank[16] = { 0,0,0,0,0,0,0,6,0,0,0,4,0,2,0,0 };
	sys16_obj_bank = bank;
	sys16_update_proc = bayroute_update_proc;
	sys16_spritesystem = 4;
	sys16_spritelist_end=0xc000;
}

static void init_bayroute( void )
{
	sys16_onetime_init_machine();
	sys16_sprite_decode( 4,0x20000 );
}

static void init_bayrouta( void )
{
	sys16_onetime_init_machine();
	sys16_sprite_decode( 2,0x40000 );
}

static void init_bayrtbl1( void )
{
	int i;

	sys16_onetime_init_machine();

	/* invert the graphics bits on the tiles */
	for (i = 0; i < 0x30000; i++)
		memory_region(REGION_GFX1)[i] ^= 0xff;

	sys16_sprite_decode( 4,0x20000 );
}
/***************************************************************************/

INPUT_PORTS_START( bayroute )
	SYS16_JOY1
	SYS16_JOY2
	SYS16_SERVICE
	SYS16_COINAGE

PORT_START
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x01, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x02, 0x02, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x02, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x0c, 0x0c, DEF_STR( Lives ) )
	PORT_DIPSETTING(    0x04, "1" )
	PORT_DIPSETTING(    0x0c, "3" )
	PORT_DIPSETTING(    0x08, "5" )
	PORT_BITX( 0,       0x00, IPT_DIPSWITCH_SETTING | IPF_CHEAT, "Unlimited", IP_KEY_NONE, IP_JOY_NONE )
	PORT_DIPNAME( 0x30, 0x30, DEF_STR( Bonus_Life ) )
	PORT_DIPSETTING(    0x30, "10000" )
	PORT_DIPSETTING(    0x20, "15000" )
	PORT_DIPSETTING(    0x10, "20000" )
	PORT_DIPSETTING(    0x00, "None" )
	PORT_DIPNAME( 0xc0, 0xc0, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0xc0, "A" )
	PORT_DIPSETTING(    0x80, "B" )
	PORT_DIPSETTING(    0x40, "C" )
	PORT_DIPSETTING(    0x00, "D" )

INPUT_PORTS_END

/***************************************************************************/

MACHINE_DRIVER_7759( machine_driver_bayroute, \
	bayroute_readmem,bayroute_writemem,bayroute_init_machine, gfx1,upd7759_interface )

/***************************************************************************

   Body Slam

***************************************************************************/

/***************************************************************************/
// sys16B
ROM_START( eswat )
	ROM_REGION( 0x080000, REGION_CPU1 ) /* 68000 code */
	ROM_LOAD_EVEN( "12657", 0x000000, 0x40000, 0xcfb935e9 )
	ROM_LOAD_ODD ( "12656", 0x000000, 0x40000, 0xbe3f9d28 )

	ROM_REGION( 0xc0000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "e12624r", 0x00000, 0x40000, 0xe7b8545e )
	ROM_LOAD( "e12625r", 0x40000, 0x40000, 0xb418582c )
	ROM_LOAD( "e12626r", 0x80000, 0x40000, 0xba65789b )

	ROM_REGION( 0x180000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "e12618r", 0x000000, 0x040000, 0x2d9ae975 )
	ROM_LOAD( "e12621r", 0x040000, 0x040000, 0x1e6c4cf7 )
	ROM_LOAD( "e12619r", 0x080000, 0x040000, 0x5f7ee6f6 )
	ROM_LOAD( "e12622r", 0x0c0000, 0x040000, 0x33251fde )
	ROM_LOAD( "e12620r", 0x100000, 0x040000, 0x905f9be2 )
	ROM_LOAD( "e12623r", 0x140000, 0x040000, 0xa25ea1fc )

	ROM_REGION( 0x30000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "e12617", 0x00000, 0x08000, 0x537930cb )
	ROM_LOAD( "e12616r",0x10000, 0x20000, 0xf213fa4a )
ROM_END

ROM_START( eswatbl )
	ROM_REGION( 0x080000, REGION_CPU1 ) /* 68000 code */
	ROM_LOAD_EVEN( "eswat_c.rom", 0x000000, 0x10000, 0x1028cc81 )
	ROM_LOAD_ODD ( "eswat_f.rom", 0x000000, 0x10000, 0xf7b2d388 )
	ROM_LOAD_EVEN( "eswat_b.rom", 0x020000, 0x10000, 0x87c6b1b5 )
	ROM_LOAD_ODD ( "eswat_e.rom", 0x020000, 0x10000, 0x937ddf9a )
	ROM_LOAD_EVEN( "eswat_a.rom", 0x040000, 0x08000, 0x2af4fc62 )
	ROM_LOAD_ODD ( "eswat_d.rom", 0x040000, 0x08000, 0xb4751e19 )

	ROM_REGION( 0xc0000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "ic19.bin", 0x00000, 0x40000, 0x375a5ec4 )
	ROM_LOAD( "ic20.bin", 0x40000, 0x40000, 0x3b8c757e )
	ROM_LOAD( "ic21.bin", 0x80000, 0x40000, 0x3efca25c )

	ROM_REGION( 0x180000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "ic9.bin",  0x000000, 0x040000, 0x0d1530bf )
	ROM_LOAD( "ic12.bin", 0x040000, 0x040000, 0x18ff0799 )
	ROM_LOAD( "ic10.bin", 0x080000, 0x040000, 0x32069246 )
	ROM_LOAD( "ic13.bin", 0x0c0000, 0x040000, 0xa3dfe436 )
	ROM_LOAD( "ic11.bin", 0x100000, 0x040000, 0xf6b096e0 )
	ROM_LOAD( "ic14.bin", 0x140000, 0x040000, 0x6773fef6 )

	ROM_REGION( 0x50000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "ic8.bin", 0x0000, 0x8000, 0x7efecf23 )
	ROM_LOAD( "ic6.bin", 0x10000, 0x40000, 0x254347c2 )
ROM_END
/***************************************************************************/

static READ_HANDLER( eswatbl_skip_r )
{
	if (cpu_get_pc()==0x65c) {cpu_spinuntil_int(); return 0xffff;}

	return READ_WORD(&sys16_workingram[0x0454]);
}

static const struct MemoryReadAddress eswat_readmem[] =
{
	{ 0x000000, 0x07ffff, MRA_ROM },
	{ 0x400000, 0x40ffff, MRA_TILERAM },
	{ 0x410000, 0x418fff, MRA_TEXTRAM },
	{ 0x440000, 0x440fff, MRA_SPRITERAM },
	{ 0x840000, 0x840fff, MRA_PALETTERAM },
	{ 0xc41002, 0xc41003, io_player1_r },
	{ 0xc41006, 0xc41007, io_player2_r },
	{ 0xc41000, 0xc41001, io_service_r },
	{ 0xc42002, 0xc42003, io_dip1_r },
	{ 0xc42000, 0xc42001, io_dip2_r },
	{ 0xffc454, 0xffc455, eswatbl_skip_r },
	{ 0xffc000, 0xffffff, MRA_WORKINGRAM },
	{-1}
};

static int eswat_tilebank0;

static WRITE_HANDLER( eswat_tilebank0_w )
{
	eswat_tilebank0 = data;
}

static const struct MemoryWriteAddress eswat_writemem[] =
{
	{ 0x000000, 0x07ffff, MWA_ROM },
	{ 0x3e2000, 0x3e2001, eswat_tilebank0_w },
	{ 0x400000, 0x40ffff, MWA_TILERAM },
	{ 0x410000, 0x418fff, MWA_TEXTRAM },
	{ 0x440000, 0x440fff, MWA_SPRITERAM },
	{ 0x840000, 0x840fff, MWA_PALETTERAM },
	{ 0xc42006, 0xc42007, sound_command_w },
	{ 0xc40000, 0xc4ffff, MWA_EXTRAM2 },
	{ 0xc80000, 0xc80001, MWA_NOP },
	{ 0xffc000, 0xffffff, MWA_WORKINGRAM },
	{-1}
};
/***************************************************************************/

static void eswat_update_proc( void ){
	sys16_fg_scrollx = READ_WORD( &sys16_textram[0x8008] ) ^ 0xffff;
	sys16_bg_scrollx = READ_WORD( &sys16_textram[0x8018] ) ^ 0xffff;
	sys16_fg_scrolly = READ_WORD( &sys16_textram[0x8000] );
	sys16_bg_scrolly = READ_WORD( &sys16_textram[0x8010] );

	set_fg_page( READ_WORD( &sys16_textram[0x8020] ) );
	set_bg_page( READ_WORD( &sys16_textram[0x8028] ) );
	set_refresh( READ_WORD( &sys16_extraram2[0] ) );

	sys16_tile_bank1 = (READ_WORD( &sys16_textram[0x8030] ))&0xf;
	sys16_tile_bank0 = eswat_tilebank0;
}

static void eswat_init_machine( void ){
	static int bank[16] = { 0,2,8,10,16,18,24,26,4,6,12,14,20,22,28,30};

	sys16_obj_bank = bank;
	sys16_sprxoffset = -0x23c;

	patch_code( 0x3897, 0x11 );

	sys16_update_proc = eswat_update_proc;
}

static void init_eswat( void ){
	sys16_onetime_init_machine();
	sys16_rowscroll_scroll=0x8000;
	sys18_splittab_fg_x=&sys16_textram[0x0f80];

	sys16_sprite_decode( 3,0x080000 );
}

/***************************************************************************/

INPUT_PORTS_START( eswat )
	SYS16_JOY1
	SYS16_JOY2
	SYS16_SERVICE
	SYS16_COINAGE

PORT_START	/* DSW1 */
	PORT_DIPNAME( 0x01, 0x01, "2 Credits to Start" )
	PORT_DIPSETTING(    0x01, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x02, 0x00, DEF_STR( Demo_Sounds ) )
	PORT_DIPSETTING(    0x02, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x04, 0x04, "Display Flip" )
	PORT_DIPSETTING(    0x04, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x08, 0x08, "Time" )
	PORT_DIPSETTING(    0x08, "Normal" )
	PORT_DIPSETTING(    0x00, "Hard" )
	PORT_DIPNAME( 0x30, 0x30, DEF_STR( Difficulty ) )
	PORT_DIPSETTING(    0x20, "Easy" )
	PORT_DIPSETTING(    0x30, "Normal" )
	PORT_DIPSETTING(    0x10, "Hard" )
	PORT_DIPSETTING(    0x00, "Hardest" )
	PORT_DIPNAME( 0xc0, 0xc0, DEF_STR( Lives ) )
	PORT_DIPSETTING(    0x00, "1" )
	PORT_DIPSETTING(    0x40, "2" )
	PORT_DIPSETTING(    0xc0, "3" )
	PORT_DIPSETTING(    0x80, "4" )
INPUT_PORTS_END

/***************************************************************************/

MACHINE_DRIVER_7759( machine_driver_eswat, \
	eswat_readmem,eswat_writemem,eswat_init_machine, gfx4,upd7759_interface )

/***************************************************************************/
// sys16A
ROM_START( fantzono )
	ROM_REGION( 0x030000, REGION_CPU1 ) /* 68000 code */
	ROM_LOAD_EVEN( "7385.43", 0x000000, 0x8000, 0x5cb64450 )
	ROM_LOAD_ODD ( "7382.26", 0x000000, 0x8000, 0x3fda7416 )
	ROM_LOAD_EVEN( "7386.42", 0x010000, 0x8000, 0x15810ace )
	ROM_LOAD_ODD ( "7383.25", 0x010000, 0x8000, 0xa001e10a )
	ROM_LOAD_EVEN( "7387.41", 0x020000, 0x8000, 0x0acd335d )
	ROM_LOAD_ODD ( "7384.24", 0x020000, 0x8000, 0xfd909341 )

	ROM_REGION( 0x18000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "7388.95", 0x00000, 0x08000, 0x8eb02f6b )
	ROM_LOAD( "7389.94", 0x08000, 0x08000, 0x2f4f71b8 )
	ROM_LOAD( "7390.93", 0x10000, 0x08000, 0xd90609c6 )

	ROM_REGION( 0x030000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "7392.10", 0x000000, 0x008000, 0x5bb7c8b6 )
	ROM_LOAD( "7396.11", 0x008000, 0x008000, 0x74ae4b57 )
	ROM_LOAD( "7393.17", 0x010000, 0x008000, 0x14fc7e82 )
	ROM_LOAD( "7397.18", 0x018000, 0x008000, 0xe05a1e25 )
	ROM_LOAD( "7394.23", 0x020000, 0x008000, 0x531ca13f )
	ROM_LOAD( "7398.24", 0x028000, 0x008000, 0x68807b49 )

	ROM_REGION( 0x10000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "7535.12", 0x0000, 0x8000, 0x0cb2126a )
ROM_END

ROM_START( fantzone )
	ROM_REGION( 0x030000, REGION_CPU1 ) /* 68000 code */
	ROM_LOAD_EVEN( "epr7385a.43", 0x000000, 0x8000, 0x4091af42 )
	ROM_LOAD_ODD ( "epr7382a.26", 0x000000, 0x8000, 0x77d67bfd )
	ROM_LOAD_EVEN( "epr7386a.42", 0x010000, 0x8000, 0xb0a67cd0 )
	ROM_LOAD_ODD ( "epr7383a.25", 0x010000, 0x8000, 0x5f79b2a9 )
	ROM_LOAD_EVEN( "7387.41", 0x020000, 0x8000, 0x0acd335d )
	ROM_LOAD_ODD ( "7384.24", 0x020000, 0x8000, 0xfd909341 )

	ROM_REGION( 0x18000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "7388.95", 0x00000, 0x08000, 0x8eb02f6b )
	ROM_LOAD( "7389.94", 0x08000, 0x08000, 0x2f4f71b8 )
	ROM_LOAD( "7390.93", 0x10000, 0x08000, 0xd90609c6 )

	ROM_REGION( 0x030000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "7392.10", 0x000000, 0x008000, 0x5bb7c8b6 )
	ROM_LOAD( "7396.11", 0x008000, 0x008000, 0x74ae4b57 )
	ROM_LOAD( "7393.17", 0x010000, 0x008000, 0x14fc7e82 )
	ROM_LOAD( "7397.18", 0x018000, 0x008000, 0xe05a1e25 )
	ROM_LOAD( "7394.23", 0x020000, 0x008000, 0x531ca13f )
	ROM_LOAD( "7398.24", 0x028000, 0x008000, 0x68807b49 )

	ROM_REGION( 0x10000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "epr7535a.12", 0x0000, 0x8000, 0xbc1374fa )
ROM_END


/***************************************************************************/

static READ_HANDLER( fantzone_skip_r )
{
	if (cpu_get_pc()==0x91b2) {cpu_spinuntil_int(); return 0xffff;}

	return READ_WORD(&sys16_workingram[0x022a]);
}

static const struct MemoryReadAddress fantzono_readmem[] =
{
	{ 0x000000, 0x02ffff, MRA_ROM },
	{ 0x400000, 0x40ffff, MRA_TILERAM },
	{ 0x410000, 0x410fff, MRA_TEXTRAM },
	{ 0x440000, 0x440fff, MRA_SPRITERAM },
	{ 0x840000, 0x840fff, MRA_PALETTERAM },
	{ 0xc41002, 0xc41003, io_player1_r },
	{ 0xc41006, 0xc41007, io_player2_r },
	{ 0xc41000, 0xc41001, io_service_r },
	{ 0xc42000, 0xc42001, io_dip1_r },
	{ 0xc42002, 0xc42003, io_dip2_r },
	{ 0xc40000, 0xc40003, MRA_EXTRAM2 },
	{ 0xffc22a, 0xffc22b, fantzone_skip_r },
	{ 0xffc000, 0xffffff, MRA_WORKINGRAM },
	{-1}
};

static const struct MemoryWriteAddress fantzono_writemem[] =
{
	{ 0x000000, 0x02ffff, MWA_ROM },
	{ 0x400000, 0x40ffff, MWA_TILERAM },
	{ 0x410000, 0x410fff, MWA_TEXTRAM },
	{ 0x440000, 0x440fff, MWA_SPRITERAM },
	{ 0x840000, 0x840fff, MWA_PALETTERAM },
	{ 0xc40000, 0xc40001, sound_command_nmi_w },
	{ 0xc40000, 0xc40003, MWA_EXTRAM2 },
	{ 0xc60000, 0xc60003, MWA_NOP },
	{ 0xffc000, 0xffffff, MWA_WORKINGRAM },
	{-1}
};

static const struct MemoryReadAddress fantzone_readmem[] =
{
	{ 0x000000, 0x02ffff, MRA_ROM },
	{ 0x400000, 0x40ffff, MRA_TILERAM },
	{ 0x410000, 0x410fff, MRA_TEXTRAM },
	{ 0x440000, 0x440fff, MRA_SPRITERAM },
	{ 0x840000, 0x840fff, MRA_PALETTERAM },
	{ 0xc41002, 0xc41003, io_player1_r },
	{ 0xc41006, 0xc41007, io_player2_r },
	{ 0xc41000, 0xc41001, io_service_r },
	{ 0xc42000, 0xc42001, io_dip1_r },
	{ 0xc42002, 0xc42003, io_dip2_r },
	{ 0xc40000, 0xc40003, MRA_EXTRAM2 },
	{ 0xffc22a, 0xffc22b, fantzone_skip_r },
	{ 0xffc000, 0xffffff, MRA_WORKINGRAM },
	{-1}
};

static const struct MemoryWriteAddress fantzone_writemem[] =
{
	{ 0x000000, 0x02ffff, MWA_ROM },
	{ 0x400000, 0x40ffff, MWA_TILERAM },
	{ 0x410000, 0x410fff, MWA_TEXTRAM },
	{ 0x440000, 0x440fff, MWA_SPRITERAM },
	{ 0x840000, 0x840fff, MWA_PALETTERAM },
	{ 0xc40000, 0xc40001, sound_command_nmi_w },
	{ 0xc40000, 0xc40003, MWA_EXTRAM2 },
	{ 0xc60000, 0xc60003, MWA_NOP },
	{ 0xffc000, 0xffffff, MWA_WORKINGRAM },
	{-1}
};

/***************************************************************************/

static void fantzone_update_proc( void ){
	sys16_fg_scrollx = READ_WORD( &sys16_textram[0x0ff8] ) & 0x01ff;
	sys16_bg_scrollx = READ_WORD( &sys16_textram[0x0ffa] ) & 0x01ff;
	sys16_fg_scrolly = READ_WORD( &sys16_textram[0x0f24] ) & 0x00ff;
	sys16_bg_scrolly = READ_WORD( &sys16_textram[0x0f26] ) & 0x01ff;

	set_fg_page1( READ_WORD( &sys16_textram[0x0e9e] ) );
	set_bg_page1( READ_WORD( &sys16_textram[0x0e9c] ) );

	set_refresh_3d( READ_WORD( &sys16_extraram2[2] ) );	// c40003
}

static void fantzono_init_machine( void ){
	static int bank[16] = { 00,01,02,03,00,01,02,03,00,01,02,03,00,01,02,03};

	sys16_obj_bank = bank;
	sys16_textmode=1;
	sys16_spritesystem = 3;
	sys16_sprxoffset = -0xbe;
//	sys16_fgxoffset = sys16_bgxoffset = 8;
	sys16_fg_priority_mode=3;				// fixes end of game priority
	sys16_fg_priority_value=0xd000;

	patch_code( 0x20e7, 0x16 );
	patch_code( 0x30ef, 0x16 );

	// solving Fantasy Zone scrolling bug
	patch_code(0x308f,0x00);

	// invincible
/*	patch_code(0x224e,0x4e);
	patch_code(0x224f,0x71);
	patch_code(0x2250,0x4e);
	patch_code(0x2251,0x71);

	patch_code(0x2666,0x4e);
	patch_code(0x2667,0x71);
	patch_code(0x2668,0x4e);
	patch_code(0x2669,0x71);

	patch_code(0x25c0,0x4e);
	patch_code(0x25c1,0x71);
	patch_code(0x25c2,0x4e);
	patch_code(0x25c3,0x71);
*/

	sys16_update_proc = fantzone_update_proc;
}

static void fantzone_init_machine( void ){
	static int bank[16] = { 00,01,02,03,00,01,02,03,00,01,02,03,00,01,02,03};

	sys16_obj_bank = bank;
	sys16_textmode=1;
	sys16_spritesystem = 3;
	sys16_sprxoffset = -0xbe;
	sys16_fg_priority_mode=3;				// fixes end of game priority
	sys16_fg_priority_value=0xd000;

	patch_code( 0x2135, 0x16 );
	patch_code( 0x3649, 0x16 );

	// solving Fantasy Zone scrolling bug
	patch_code(0x35e9,0x00);

	sys16_update_proc = fantzone_update_proc;
}

static void init_fantzone( void )
{
	sys16_onetime_init_machine();
	sys16_sprite_decode( 3,0x010000 );
}
/***************************************************************************/

INPUT_PORTS_START( fantzone )
	SYS16_JOY1
	SYS16_JOY2
	SYS16_SERVICE
	SYS16_COINAGE

PORT_START	/* DSW1 */
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Cabinet ) )
	PORT_DIPSETTING(    0x01, DEF_STR( Upright ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Cocktail ) )
	PORT_DIPNAME( 0x02, 0x00, DEF_STR( Demo_Sounds ) )
	PORT_DIPSETTING(    0x02, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x0c, 0x0c, DEF_STR( Lives ) )
	PORT_DIPSETTING(    0x08, "2" )
	PORT_DIPSETTING(    0x0c, "3" )
	PORT_DIPSETTING(    0x04, "4" )
	PORT_BITX( 0,       0x00, IPT_DIPSWITCH_SETTING | IPF_CHEAT, "240", IP_KEY_NONE, IP_JOY_NONE )
	PORT_DIPNAME( 0x30, 0x30, "Extra Ship Cost" )
	PORT_DIPSETTING(    0x30, "5000" )
	PORT_DIPSETTING(    0x20, "10000" )
	PORT_DIPSETTING(    0x10, "15000" )
	PORT_DIPSETTING(    0x00, "20000" )
	PORT_DIPNAME( 0xc0, 0xc0, DEF_STR( Difficulty ) )
	PORT_DIPSETTING(    0x80, "Easy" )
	PORT_DIPSETTING(    0xc0, "Normal" )
	PORT_DIPSETTING(    0x40, "Hard" )
	PORT_DIPSETTING(    0x00, "Hardest" )

INPUT_PORTS_END

/***************************************************************************/

MACHINE_DRIVER( machine_driver_fantzono, \
	fantzono_readmem,fantzono_writemem,fantzono_init_machine, gfx8 )
MACHINE_DRIVER( machine_driver_fantzone, \
	fantzone_readmem,fantzone_writemem,fantzone_init_machine, gfx8 )

/***************************************************************************/
// sys16B
ROM_START( goldnaxe )
	ROM_REGION( 0x0c0000, REGION_CPU1 ) /* 68000 code */
	ROM_LOAD_EVEN( "epr12523.a7", 0x00000, 0x20000, 0x8e6128d7 )
	ROM_LOAD_ODD ( "epr12522.a5", 0x00000, 0x20000, 0xb6c35160 )
	/* emtpy 0x40000 - 0x80000 */
	ROM_LOAD_EVEN( "epr12521.a8", 0x80000, 0x20000, 0x5001d713 )
	ROM_LOAD_ODD ( "epr12519.a6", 0x80000, 0x20000, 0x4438ca8e )

	ROM_REGION( 0x60000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "epr12385", 0x00000, 0x20000, 0xb8a4e7e0 )
	ROM_LOAD( "epr12386", 0x20000, 0x20000, 0x25d7d779 )
	ROM_LOAD( "epr12387", 0x40000, 0x20000, 0xc7fcadf3 )

	ROM_REGION( 0x180000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "mpr12378.b1", 0x000000, 0x40000, 0x119e5a82 )
	ROM_LOAD( "mpr12379.b4", 0x040000, 0x40000, 0x1a0e8c57 )
	ROM_LOAD( "mpr12380.b2", 0x080000, 0x40000, 0xbb2c0853 )
	ROM_LOAD( "mpr12381.b5", 0x0c0000, 0x40000, 0x81ba6ecc )
	ROM_LOAD( "mpr12382.b3", 0x100000, 0x40000, 0x81601c6f )
	ROM_LOAD( "mpr12383.b6", 0x140000, 0x40000, 0x5dbacf7a )

	ROM_REGION( 0x30000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "epr12390",     0x00000, 0x08000, 0x399fc5f5 )
	ROM_LOAD( "mpr12384.a11", 0x10000, 0x20000, 0x6218d8e7 )
ROM_END

ROM_START( goldnaxj )
	ROM_REGION( 0x0c0000, REGION_CPU1 ) /* 68000 code */
// Custom cpu 317-0121
	ROM_LOAD_EVEN( "epr12540.a7", 0x00000, 0x20000, 0x0c7ccc6d )
	ROM_LOAD_ODD ( "epr12539.a5", 0x00000, 0x20000, 0x1f24f7d0 )
	/* emtpy 0x40000 - 0x80000 */
	ROM_LOAD_EVEN( "epr12521.a8", 0x80000, 0x20000, 0x5001d713 )
	ROM_LOAD_ODD ( "epr12519.a6", 0x80000, 0x20000, 0x4438ca8e )

	ROM_REGION( 0x60000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "epr12385", 0x00000, 0x20000, 0xb8a4e7e0 )
	ROM_LOAD( "epr12386", 0x20000, 0x20000, 0x25d7d779 )
	ROM_LOAD( "epr12387", 0x40000, 0x20000, 0xc7fcadf3 )

	ROM_REGION( 0x180000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "mpr12378.b1", 0x000000, 0x40000, 0x119e5a82 )
	ROM_LOAD( "mpr12379.b4", 0x040000, 0x40000, 0x1a0e8c57 )
	ROM_LOAD( "mpr12380.b2", 0x080000, 0x40000, 0xbb2c0853 )
	ROM_LOAD( "mpr12381.b5", 0x0c0000, 0x40000, 0x81ba6ecc )
	ROM_LOAD( "mpr12382.b3", 0x100000, 0x40000, 0x81601c6f )
	ROM_LOAD( "mpr12383.b6", 0x140000, 0x40000, 0x5dbacf7a )

	ROM_REGION( 0x30000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "epr12390",     0x00000, 0x08000, 0x399fc5f5 )
	ROM_LOAD( "mpr12384.a11", 0x10000, 0x20000, 0x6218d8e7 )
ROM_END


ROM_START( goldnabl )
	ROM_REGION( 0x0c0000, REGION_CPU1 ) /* 68000 code */
// protected code
	ROM_LOAD_EVEN( "ga6.a22", 0x00000, 0x10000, 0xf95b459f )
	ROM_LOAD_ODD ( "ga4.a20", 0x00000, 0x10000, 0x83eabdf5 )
	ROM_LOAD_EVEN( "ga11.a27",0x20000, 0x10000, 0xf4ef9349 )
	ROM_LOAD_ODD ( "ga8.a24", 0x20000, 0x10000, 0x37a65839 )
	/* emtpy 0x40000 - 0x80000 */
	ROM_LOAD_EVEN( "epr12521.a8", 0x80000, 0x20000, 0x5001d713 )
	ROM_LOAD_ODD ( "epr12519.a6", 0x80000, 0x20000, 0x4438ca8e )

	ROM_REGION( 0x60000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "ga33.b16", 0x00000, 0x10000, 0x84587263 )
	ROM_LOAD( "ga32.b15", 0x10000, 0x10000, 0x63d72388 )
	ROM_LOAD( "ga31.b14", 0x20000, 0x10000, 0xf8b6ae4f )
	ROM_LOAD( "ga30.b13", 0x30000, 0x10000, 0xe29baf4f )
	ROM_LOAD( "ga29.b12", 0x40000, 0x10000, 0x22f0667e )
	ROM_LOAD( "ga28.b11", 0x50000, 0x10000, 0xafb1a7e4 )

	ROM_REGION( 0x180000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "ga34.b17", 0x000000, 0x10000, 0x28ba70c8 )
	ROM_LOAD( "ga35.b18", 0x010000, 0x10000, 0x2ed96a26 )
	ROM_LOAD( "ga23.a14", 0x020000, 0x10000, 0x84dccc5b )
	ROM_LOAD( "ga18.a9",  0x030000, 0x10000, 0xde346006 )
	ROM_LOAD( "mpr12379.b4", 0x040000, 0x40000, 0x1a0e8c57 )
	ROM_LOAD( "ga36.b19", 0x080000, 0x10000, 0x101d2fff )
	ROM_LOAD( "ga37.b20", 0x090000, 0x10000, 0x677e64a6 )
	ROM_LOAD( "ga19.a10", 0x0a0000, 0x10000, 0x11794d05 )
	ROM_LOAD( "ga20.a11", 0x0b0000, 0x10000, 0xad1c1c90 )
	ROM_LOAD( "mpr12381.b5", 0x0c0000, 0x40000, 0x81ba6ecc )
	ROM_LOAD( "mpr12382.b3", 0x100000, 0x40000, 0x81601c6f )
	ROM_LOAD( "mpr12383.b6", 0x140000, 0x40000, 0x5dbacf7a )

	ROM_REGION( 0x30000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "epr12390",     0x00000, 0x08000, 0x399fc5f5 )
	ROM_LOAD( "mpr12384.a11", 0x10000, 0x20000, 0x6218d8e7 )
ROM_END


/***************************************************************************/

static READ_HANDLER( goldnaxe_skip_r )
{
	if (cpu_get_pc()==0x3cb0) {cpu_spinuntil_int(); return 0xffff;}

	return READ_WORD(&sys16_workingram[0x2c1c]);
}

static READ_HANDLER( ga_io_players_r ) {return (io_player1_r(offset) << 8) | io_player2_r(offset);}
static READ_HANDLER( ga_io_service_r )
{
	return (io_service_r(offset) << 8) | (READ_WORD(&sys16_workingram[0x2c96]) & 0x00ff);
}

static const struct MemoryReadAddress goldnaxe_readmem[] =
{
	{ 0x000000, 0x0bffff, MRA_ROM },

	{ 0x100000, 0x10ffff, MRA_TILERAM },
	{ 0x110000, 0x110fff, MRA_TEXTRAM },
	{ 0x140000, 0x140fff, MRA_PALETTERAM },
	{ 0x1f0000, 0x1f0003, MRA_EXTRAM },
	{ 0x200000, 0x200fff, MRA_SPRITERAM },
	{ 0xc41002, 0xc41003, io_player1_r },
	{ 0xc41006, 0xc41007, io_player2_r },
	{ 0xc41000, 0xc41001, io_service_r },
	{ 0xc42002, 0xc42003, io_dip1_r },
	{ 0xc42000, 0xc42001, io_dip2_r },
	{ 0xc40000, 0xc40fff, MRA_EXTRAM2 },
	{ 0xffecd0, 0xffecd1, ga_io_players_r },
	{ 0xffec96, 0xffec97, ga_io_service_r },
	{ 0xffec1c, 0xffec1d, goldnaxe_skip_r },
	{ 0xffc000, 0xffffff, MRA_WORKINGRAM },
	{-1}
};

static WRITE_HANDLER( ga_sound_command_w )
{
	if( (data&0xff000000)==0 )
		sound_command_w(offset,data>>8);
	COMBINE_WORD_MEM(&sys16_workingram[0x2cfc],data);
}

static const struct MemoryWriteAddress goldnaxe_writemem[] =
{
	{ 0x000000, 0x0bffff, MWA_ROM },
	{ 0x100000, 0x10ffff, MWA_TILERAM },
	{ 0x110000, 0x110fff, MWA_TEXTRAM },
	{ 0x140000, 0x140fff, MWA_PALETTERAM },
	{ 0x1f0000, 0x1f0003, MWA_EXTRAM },
	{ 0x200000, 0x200fff, MWA_SPRITERAM },
	{ 0xc40000, 0xc40fff, MWA_EXTRAM2 },
	{ 0xc43000, 0xc43001, MWA_NOP },
	{ 0xffecfc, 0xffecfd, ga_sound_command_w },
	{ 0xffc000, 0xffffff, MWA_WORKINGRAM },
	{-1}
};
/***************************************************************************/

static void goldnaxe_update_proc( void ){
	sys16_fg_scrollx = READ_WORD( &sys16_textram[0x0e98] );
	sys16_bg_scrollx = READ_WORD( &sys16_textram[0x0e9a] );
	sys16_fg_scrolly = READ_WORD( &sys16_textram[0x0e90] );
	sys16_bg_scrolly = READ_WORD( &sys16_textram[0x0e92] );

	set_fg_page( READ_WORD( &sys16_textram[0x0e80] ) );
	set_bg_page( READ_WORD( &sys16_textram[0x0e82] ) );
	set_tile_bank( READ_WORD( &sys16_workingram[0x2c94] ) );
	set_refresh( READ_WORD( &sys16_extraram2[0] ) );
}

static void goldnaxe_init_machine( void ){
	static int bank[16] = { 0,2,8,10,16,18,0,0,4,6,12,14,20,22,0,0 };

	sys16_obj_bank = bank;

	patch_code( 0x3CB2, 0x60 );
	patch_code( 0x3CB3, 0x1e );

	sys16_sprxoffset = -0xb8;
	sys16_update_proc = goldnaxe_update_proc;
}

static void init_goldnaxe( void )
{
	sys16_onetime_init_machine();
	sys16_sprite_decode( 3,0x80000 );
}

static void init_goldnabl( void )
{
	int i;

	sys16_onetime_init_machine();

	/* invert the graphics bits on the tiles */
	for (i = 0; i < 0x60000; i++)
		memory_region(REGION_GFX1)[i] ^= 0xff;
	sys16_sprite_decode( 3,0x80000 );
}

/***************************************************************************/

INPUT_PORTS_START( goldnaxe )
	SYS16_JOY1
	SYS16_JOY2
	SYS16_SERVICE
	SYS16_COINAGE

PORT_START	/* DSW1 */
	PORT_DIPNAME( 0x01, 0x01, "Credits needed" )
	PORT_DIPSETTING(    0x01, "1 to start, 1 to continue")
	PORT_DIPSETTING(    0x00, "2 to start, 1 to continue")
	PORT_DIPNAME( 0x02, 0x00, DEF_STR( Demo_Sounds ) )
	PORT_DIPSETTING(    0x02, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x0c, 0x0c, DEF_STR( Lives ) )
	PORT_DIPSETTING(    0x08, "1" )
	PORT_DIPSETTING(    0x0c, "2" )
	PORT_DIPSETTING(    0x04, "3" )
	PORT_DIPSETTING(    0x00, "5" )
	PORT_DIPNAME( 0x30, 0x30, "Energy Meter" )
	PORT_DIPSETTING(    0x20, "2" )
	PORT_DIPSETTING(    0x30, "3" )
	PORT_DIPSETTING(    0x10, "4" )
	PORT_DIPSETTING(    0x00, "5" )
	PORT_DIPNAME( 0x40, 0x40, DEF_STR( Unused ) )
	PORT_DIPSETTING(    0x40, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x80, 0x80, DEF_STR( Unused ) )
	PORT_DIPSETTING(    0x80, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
INPUT_PORTS_END

/***************************************************************************/

MACHINE_DRIVER_7759( machine_driver_goldnaxe, \
	goldnaxe_readmem,goldnaxe_writemem,goldnaxe_init_machine, gfx2,upd7759_interface )

/***************************************************************************/
// sys16B
ROM_START( goldnaxa )
	ROM_REGION( 0x0c0000, REGION_CPU1 ) /* 68000 code */
	ROM_LOAD_EVEN( "epr12545.a2", 0x00000, 0x40000, 0xa97c4e4d )
	ROM_LOAD_ODD ( "epr12544.a1", 0x00000, 0x40000, 0x5e38f668 )

	ROM_REGION( 0x60000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "epr12385", 0x00000, 0x20000, 0xb8a4e7e0 )
	ROM_LOAD( "epr12386", 0x20000, 0x20000, 0x25d7d779 )
	ROM_LOAD( "epr12387", 0x40000, 0x20000, 0xc7fcadf3 )

	ROM_REGION( 0x180000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "mpr12378.b1", 0x000000, 0x40000, 0x119e5a82 )
	ROM_LOAD( "mpr12379.b4", 0x040000, 0x40000, 0x1a0e8c57 )
	ROM_LOAD( "mpr12380.b2", 0x080000, 0x40000, 0xbb2c0853 )
	ROM_LOAD( "mpr12381.b5", 0x0c0000, 0x40000, 0x81ba6ecc )
	ROM_LOAD( "mpr12382.b3", 0x100000, 0x40000, 0x81601c6f )
	ROM_LOAD( "mpr12383.b6", 0x140000, 0x40000, 0x5dbacf7a )

	ROM_REGION( 0x30000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "epr12390",     0x00000, 0x08000, 0x399fc5f5 )
	ROM_LOAD( "mpr12384.a11", 0x10000, 0x20000, 0x6218d8e7 )
ROM_END

ROM_START( goldnaxb )
	ROM_REGION( 0x0c0000, REGION_CPU1 ) /* 68000 code */
// Custom 68000 ver 317-0110
	ROM_LOAD_EVEN( "epr12389.a2", 0x00000, 0x40000, 0x35d5fa77 )
	ROM_LOAD_ODD ( "epr12388.a1", 0x00000, 0x40000, 0x72952a93 )

	ROM_REGION( 0x60000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "epr12385", 0x00000, 0x20000, 0xb8a4e7e0 )
	ROM_LOAD( "epr12386", 0x20000, 0x20000, 0x25d7d779 )
	ROM_LOAD( "epr12387", 0x40000, 0x20000, 0xc7fcadf3 )

	ROM_REGION( 0x180000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "mpr12378.b1", 0x000000, 0x40000, 0x119e5a82 )
	ROM_LOAD( "mpr12379.b4", 0x040000, 0x40000, 0x1a0e8c57 )
	ROM_LOAD( "mpr12380.b2", 0x080000, 0x40000, 0xbb2c0853 )
	ROM_LOAD( "mpr12381.b5", 0x0c0000, 0x40000, 0x81ba6ecc )
	ROM_LOAD( "mpr12382.b3", 0x100000, 0x40000, 0x81601c6f )
	ROM_LOAD( "mpr12383.b6", 0x140000, 0x40000, 0x5dbacf7a )

	ROM_REGION( 0x30000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "epr12390",     0x00000, 0x08000, 0x399fc5f5 )
	ROM_LOAD( "mpr12384.a11", 0x10000, 0x20000, 0x6218d8e7 )
ROM_END

ROM_START( goldnaxc )
	ROM_REGION( 0x0c0000, REGION_CPU1 ) /* 68000 code */
// Custom 68000 ver 317-0122
	ROM_LOAD_EVEN( "epr12543.a2", 0x00000, 0x40000, 0xb0df9ca4 )
	ROM_LOAD_ODD ( "epr12542.a1", 0x00000, 0x40000, 0xb7994d3c )

	ROM_REGION( 0x60000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "epr12385", 0x00000, 0x20000, 0xb8a4e7e0 )
	ROM_LOAD( "epr12386", 0x20000, 0x20000, 0x25d7d779 )
	ROM_LOAD( "epr12387", 0x40000, 0x20000, 0xc7fcadf3 )

	ROM_REGION( 0x180000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "mpr12378.b1", 0x000000, 0x40000, 0x119e5a82 )
	ROM_LOAD( "mpr12379.b4", 0x040000, 0x40000, 0x1a0e8c57 )
	ROM_LOAD( "mpr12380.b2", 0x080000, 0x40000, 0xbb2c0853 )
	ROM_LOAD( "mpr12381.b5", 0x0c0000, 0x40000, 0x81ba6ecc )
	ROM_LOAD( "mpr12382.b3", 0x100000, 0x40000, 0x81601c6f )
	ROM_LOAD( "mpr12383.b6", 0x140000, 0x40000, 0x5dbacf7a )

	ROM_REGION( 0x30000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "epr12390",     0x00000, 0x08000, 0x399fc5f5 )
	ROM_LOAD( "mpr12384.a11", 0x10000, 0x20000, 0x6218d8e7 )
ROM_END


/***************************************************************************/

static READ_HANDLER( goldnaxa_skip_r )
{
	if (cpu_get_pc()==0x3ca0) {cpu_spinuntil_int(); return 0xffff;}

	return READ_WORD(&sys16_workingram[0x2c1c]);
}

// This version has somekind of hardware comparitor for collision detection,
// and a hardware multiplier.
static int ga_hardware_collision_data[5];
static WRITE_HANDLER( ga_hardware_collision_w )
{
	static int bit=1;
	ga_hardware_collision_data[offset/2]=data;
	if(offset==4)
	{
		if(ga_hardware_collision_data[2] <= ga_hardware_collision_data[0] &&
			ga_hardware_collision_data[2] >= ga_hardware_collision_data[1])
		{
			ga_hardware_collision_data[4] |=bit;
		}
		bit=bit<<1;
	}
	if(offset==8) bit=1;
}

static READ_HANDLER( ga_hardware_collision_r )
{
	return ga_hardware_collision_data[4];
}

static int ga_hardware_multiplier_data[4];
static WRITE_HANDLER( ga_hardware_multiplier_w )
{
	ga_hardware_multiplier_data[offset/2]=data;
}

static READ_HANDLER( ga_hardware_multiplier_r )
{
	if(offset==6)
		return ga_hardware_multiplier_data[0] * ga_hardware_multiplier_data[1];
	else
		return ga_hardware_multiplier_data[offset/2];
}

static const struct MemoryReadAddress goldnaxa_readmem[] =
{
	{ 0x000000, 0x07ffff, MRA_ROM },

	{ 0x100000, 0x10ffff, MRA_TILERAM },
	{ 0x110000, 0x110fff, MRA_TEXTRAM },
	{ 0x140000, 0x140fff, MRA_PALETTERAM },
	{ 0x1e0008, 0x1e0009, ga_hardware_collision_r },
	{ 0x1f0000, 0x1f0007, ga_hardware_multiplier_r },
	{ 0x1f1008, 0x1f1009, ga_hardware_collision_r },
	{ 0x1f2000, 0x1f2003, MRA_EXTRAM },
	{ 0x200000, 0x200fff, MRA_SPRITERAM },
	{ 0xc41002, 0xc41003, io_player1_r },
	{ 0xc41006, 0xc41007, io_player2_r },
	{ 0xc41000, 0xc41001, io_service_r },
	{ 0xc42002, 0xc42003, io_dip1_r },
	{ 0xc42000, 0xc42001, io_dip2_r },
	{ 0xc40000, 0xc40fff, MRA_EXTRAM2 },
	{ 0xffecd0, 0xffecd1, ga_io_players_r },
	{ 0xffec96, 0xffec97, ga_io_service_r },
	{ 0xffec1c, 0xffec1d, goldnaxa_skip_r },
	{ 0xffc000, 0xffffff, MRA_WORKINGRAM },
	{-1}
};

static const struct MemoryWriteAddress goldnaxa_writemem[] =
{
	{ 0x000000, 0x07ffff, MWA_ROM },
	{ 0x100000, 0x10ffff, MWA_TILERAM },
	{ 0x110000, 0x110fff, MWA_TEXTRAM },
	{ 0x140000, 0x140fff, MWA_PALETTERAM },
	{ 0x1e0000, 0x1e0009, ga_hardware_collision_w },
	{ 0x1f0000, 0x1f0003, ga_hardware_multiplier_w },
	{ 0x1f1000, 0x1f1009, ga_hardware_collision_w },
	{ 0x1f2000, 0x1f2003, MWA_EXTRAM },
	{ 0x200000, 0x200fff, MWA_SPRITERAM },
	{ 0xc40000, 0xc40fff, MWA_EXTRAM2 },
	{ 0xffecfc, 0xffecfd, ga_sound_command_w },
	{ 0xffc000, 0xffffff, MWA_WORKINGRAM },
	{-1}
};
/***************************************************************************/

static void goldnaxa_update_proc( void ){
	sys16_fg_scrollx = READ_WORD( &sys16_textram[0x0e98] );
	sys16_bg_scrollx = READ_WORD( &sys16_textram[0x0e9a] );
	sys16_fg_scrolly = READ_WORD( &sys16_textram[0x0e90] );
	sys16_bg_scrolly = READ_WORD( &sys16_textram[0x0e92] );

	set_fg_page( READ_WORD( &sys16_textram[0x0e80] ) );
	set_bg_page( READ_WORD( &sys16_textram[0x0e82] ) );
	set_tile_bank( READ_WORD( &sys16_workingram[0x2c94] ) );
	set_refresh( READ_WORD( &sys16_extraram2[0] ) );
}

static void goldnaxa_init_machine( void ){
	static int bank[16] = { 0,2,8,10,16,18,0,0,4,6,12,14,20,22,0,0 };

	sys16_obj_bank = bank;

	patch_code( 0x3CA2, 0x60 );
	patch_code( 0x3CA3, 0x1e );

	sys16_sprxoffset = -0xb8;
	sys16_update_proc = goldnaxa_update_proc;
}

/***************************************************************************/

MACHINE_DRIVER_7759( machine_driver_goldnaxa, \
	goldnaxa_readmem,goldnaxa_writemem,goldnaxa_init_machine, gfx2,upd7759_interface )

/***************************************************************************/
// sys16B
ROM_START( shinobi )
	ROM_REGION( 0x040000, REGION_CPU1 ) /* 68000 code */
	ROM_LOAD_EVEN( "shinobi.a4", 0x000000, 0x10000, 0xb930399d )
	ROM_LOAD_ODD ( "shinobi.a1", 0x000000, 0x10000, 0x343f4c46 )
	ROM_LOAD_EVEN( "epr11283",   0x020000, 0x10000, 0x9d46e707 )
	ROM_LOAD_ODD ( "epr11281",   0x020000, 0x10000, 0x7961d07e )

	ROM_REGION( 0x30000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "shinobi.b9",  0x00000, 0x10000, 0x5f62e163 )
	ROM_LOAD( "shinobi.b10", 0x10000, 0x10000, 0x75f8fbc9 )
	ROM_LOAD( "shinobi.b11", 0x20000, 0x10000, 0x06508bb9 )

	ROM_REGION( 0x080000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "epr11290.10", 0x00000, 0x10000, 0x611f413a )
	ROM_LOAD( "epr11294.11", 0x10000, 0x10000, 0x5eb00fc1 )
	ROM_LOAD( "epr11291.17", 0x20000, 0x10000, 0x3c0797c0 )
	ROM_LOAD( "epr11295.18", 0x30000, 0x10000, 0x25307ef8 )
	ROM_LOAD( "epr11292.23", 0x40000, 0x10000, 0xc29ac34e )
	ROM_LOAD( "epr11296.24", 0x50000, 0x10000, 0x04a437f8 )
	ROM_LOAD( "epr11293.29", 0x60000, 0x10000, 0x41f41063 )
	ROM_LOAD( "epr11297.30", 0x70000, 0x10000, 0xb6e1fd72 )

	ROM_REGION( 0x20000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "shinobi.a7", 0x0000, 0x8000, 0x2457a7cf )
	ROM_LOAD( "shinobi.a8", 0x10000, 0x8000, 0xc8df8460 )
	ROM_LOAD( "shinobi.a9", 0x18000, 0x8000, 0xe5a4cf30 )

ROM_END

ROM_START( shinobib )
	ROM_REGION( 0x040000, REGION_CPU1 ) /* 68000 code */
// Custom cpu 317-0049
	ROM_LOAD_EVEN( "epr11282", 0x000000, 0x10000, 0x5f2e5524 )
	ROM_LOAD_ODD ( "epr11280", 0x000000, 0x10000, 0xbdfe5c38 )
	ROM_LOAD_EVEN( "epr11283", 0x020000, 0x10000, 0x9d46e707 )
	ROM_LOAD_ODD ( "epr11281", 0x020000, 0x10000, 0x7961d07e )

	ROM_REGION( 0x30000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "shinobi.b9",  0x00000, 0x10000, 0x5f62e163 )
	ROM_LOAD( "shinobi.b10", 0x10000, 0x10000, 0x75f8fbc9 )
	ROM_LOAD( "shinobi.b11", 0x20000, 0x10000, 0x06508bb9 )

	ROM_REGION( 0x080000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "epr11290.10", 0x00000, 0x10000, 0x611f413a )
	ROM_LOAD( "epr11294.11", 0x10000, 0x10000, 0x5eb00fc1 )
	ROM_LOAD( "epr11291.17", 0x20000, 0x10000, 0x3c0797c0 )
	ROM_LOAD( "epr11295.18", 0x30000, 0x10000, 0x25307ef8 )
	ROM_LOAD( "epr11292.23", 0x40000, 0x10000, 0xc29ac34e )
	ROM_LOAD( "epr11296.24", 0x50000, 0x10000, 0x04a437f8 )
	ROM_LOAD( "epr11293.29", 0x60000, 0x10000, 0x41f41063 )
	ROM_LOAD( "epr11297.30", 0x70000, 0x10000, 0xb6e1fd72 )

	ROM_REGION( 0x20000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "shinobi.a7", 0x0000, 0x8000, 0x2457a7cf )
	ROM_LOAD( "shinobi.a8", 0x10000, 0x8000, 0xc8df8460 )
	ROM_LOAD( "shinobi.a9", 0x18000, 0x8000, 0xe5a4cf30 )

ROM_END

/***************************************************************************/

static READ_HANDLER( shinobi_skip_r )
{
	if (cpu_get_pc()==0x32e0) {cpu_spinuntil_int(); return 1<<8;}

	return READ_WORD(&sys16_workingram[0x301c]);
}

static const struct MemoryReadAddress shinobi_readmem[] =
{
	{ 0x000000, 0x03ffff, MRA_ROM },
	{ 0x400000, 0x40ffff, MRA_TILERAM },
	{ 0x410000, 0x410fff, MRA_TEXTRAM },
	{ 0x440000, 0x440fff, MRA_SPRITERAM },
	{ 0x840000, 0x840fff, MRA_PALETTERAM },
	{ 0xc41002, 0xc41003, io_player1_r },
	{ 0xc41006, 0xc41007, io_player2_r },
	{ 0xc41000, 0xc41001, io_service_r },
	{ 0xc42002, 0xc42003, io_dip1_r },
	{ 0xc42000, 0xc42001, io_dip2_r },
	{ 0xc40000, 0xc40001, MRA_EXTRAM2 },
	{ 0xc43000, 0xc43001, MRA_NOP },
	{ 0xfff01c, 0xfff01d, shinobi_skip_r },
	{ 0xffc000, 0xffffff, MRA_WORKINGRAM },
	{-1}
};

static const struct MemoryWriteAddress shinobi_writemem[] =
{
	{ 0x000000, 0x03ffff, MWA_ROM },
	{ 0x400000, 0x40ffff, MWA_TILERAM },
	{ 0x410000, 0x410fff, MWA_TEXTRAM },
	{ 0x440000, 0x440fff, MWA_SPRITERAM },
	{ 0x840000, 0x840fff, MWA_PALETTERAM },
	{ 0xc40000, 0xc40001, MWA_EXTRAM2 },
	{ 0xc43000, 0xc43001, MWA_NOP },
	{ 0xfe0006, 0xfe0007, sound_command_w },
	{ 0xffc000, 0xffffff, MWA_WORKINGRAM },
	{-1}
};

/***************************************************************************/

static void shinobi_update_proc( void ){
	sys16_fg_scrollx = READ_WORD( &sys16_textram[0x0e98] );
	sys16_bg_scrollx = READ_WORD( &sys16_textram[0x0e9a] );
	sys16_fg_scrolly = READ_WORD( &sys16_textram[0x0e90] );
	sys16_bg_scrolly = READ_WORD( &sys16_textram[0x0e92] );

	set_fg_page( READ_WORD( &sys16_textram[0x0e80] ) );
	set_bg_page( READ_WORD( &sys16_textram[0x0e82] ) );

	set_refresh( READ_WORD( &sys16_extraram2[0] ) );
}

static void shinobi_init_machine( void ){
	static int bank[16] = { 0,0,0,0,0,0,0,6,0,0,0,4,0,2,0,0 };
	sys16_obj_bank = bank;
	sys16_dactype = 1;
	sys16_update_proc = shinobi_update_proc;
}

static void init_shinobi( void )
{
	sys16_onetime_init_machine();
	sys16_sprite_decode( 4,0x20000 );
}

/***************************************************************************/

INPUT_PORTS_START( shinobi )
	SYS16_JOY1
	SYS16_JOY2
	SYS16_SERVICE
	SYS16_COINAGE

PORT_START
	PORT_DIPNAME( 0x01, 0x00, DEF_STR( Cabinet ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Upright ) )
	PORT_DIPSETTING(    0x01, DEF_STR( Cocktail ) )
	PORT_DIPNAME( 0x02, 0x00, DEF_STR( Demo_Sounds ) )
	PORT_DIPSETTING(    0x02, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x0c, 0x0c, DEF_STR( Lives ) )
	PORT_DIPSETTING(    0x08, "2" )
	PORT_DIPSETTING(    0x0c, "3" )
	PORT_DIPSETTING(    0x04, "5" )
	PORT_BITX( 0,       0x00, IPT_DIPSWITCH_SETTING | IPF_CHEAT, "240", IP_KEY_NONE, IP_JOY_NONE )
	PORT_DIPNAME( 0x30, 0x30, DEF_STR( Difficulty ) )
	PORT_DIPSETTING(    0x20, "Easy" )
	PORT_DIPSETTING(    0x30, "Normal" )
	PORT_DIPSETTING(    0x10, "Hard" )
	PORT_DIPSETTING(    0x00, "Hardest" )
	PORT_DIPNAME( 0x40, 0x40, "Enemy's Bullet Speed" )
	PORT_DIPSETTING(    0x40, "Slow" )
	PORT_DIPSETTING(    0x00, "Fast" )
	PORT_DIPNAME( 0x80, 0x80, "Language" )
	PORT_DIPSETTING(    0x80, "Japanese" )
	PORT_DIPSETTING(    0x00, "English" )

INPUT_PORTS_END

/***************************************************************************/

MACHINE_DRIVER_7759( machine_driver_shinobi, \
	shinobi_readmem,shinobi_writemem,shinobi_init_machine, gfx1,upd7759_interface )

/***************************************************************************/
// sys16A
ROM_START( shinobia )
	ROM_REGION( 0x040000, REGION_CPU1 ) /* 68000 code */
// custom cpu 317-0050
	ROM_LOAD_EVEN( "epr11262.42", 0x000000, 0x10000, 0xd4b8df12 )
	ROM_LOAD_ODD ( "epr11260.27", 0x000000, 0x10000, 0x2835c95d )
	ROM_LOAD_EVEN( "epr11263.43", 0x020000, 0x10000, 0xa2a620bd )
	ROM_LOAD_ODD ( "epr11261.25", 0x020000, 0x10000, 0xa3ceda52 )

	ROM_REGION( 0x30000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "epr11264.95", 0x00000, 0x10000, 0x46627e7d )
	ROM_LOAD( "epr11265.94", 0x10000, 0x10000, 0x87d0f321 )
	ROM_LOAD( "epr11266.93", 0x20000, 0x10000, 0xefb4af87 )

	ROM_REGION( 0x080000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "epr11290.10", 0x00000, 0x10000, 0x611f413a )
	ROM_LOAD( "epr11294.11", 0x10000, 0x10000, 0x5eb00fc1 )
	ROM_LOAD( "epr11291.17", 0x20000, 0x10000, 0x3c0797c0 )
	ROM_LOAD( "epr11295.18", 0x30000, 0x10000, 0x25307ef8 )
	ROM_LOAD( "epr11292.23", 0x40000, 0x10000, 0xc29ac34e )
	ROM_LOAD( "epr11296.24", 0x50000, 0x10000, 0x04a437f8 )
	ROM_LOAD( "epr11293.29", 0x60000, 0x10000, 0x41f41063 )
	ROM_LOAD( "epr11297.30", 0x70000, 0x10000, 0xb6e1fd72 )

	ROM_REGION( 0x20000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "epr11267.12", 0x0000, 0x8000, 0xdd50b745 )

	ROM_REGION( 0x1000, REGION_CPU3 )      /* 4k for 7751 onboard ROM */
	ROM_LOAD( "7751.bin",     0x0000, 0x0400, 0x6a9534fc ) /* 7751 - U34 */

	ROM_REGION( 0x08000, REGION_SOUND1 ) /* 7751 sound data */
	ROM_LOAD( "epr11268.1", 0x0000, 0x8000, 0x6d7966da )
ROM_END


ROM_START( shinobl )
	ROM_REGION( 0x040000, REGION_CPU1 ) /* 68000 code */
// Star Bootleg
	ROM_LOAD_EVEN( "b3",          0x000000, 0x10000, 0x38e59646 )
	ROM_LOAD_ODD ( "b1",          0x000000, 0x10000, 0x8529d192 )
	ROM_LOAD_EVEN( "epr11263.43", 0x020000, 0x10000, 0xa2a620bd )
	ROM_LOAD_ODD ( "epr11261.25", 0x020000, 0x10000, 0xa3ceda52 )

// Beta Bootleg
//	ROM_LOAD_EVEN( "4",           0x000000, 0x10000, 0xc178a39c )
//	ROM_LOAD_ODD ( "2",           0x000000, 0x10000, 0x5ad8ebf2 )
//	ROM_LOAD_EVEN( "epr11263.43", 0x020000, 0x10000, 0xa2a620bd )
//	ROM_LOAD_ODD ( "epr11261.25", 0x020000, 0x10000, 0xa3ceda52 )

	ROM_REGION( 0x30000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "epr11264.95", 0x00000, 0x10000, 0x46627e7d )
	ROM_LOAD( "epr11265.94", 0x10000, 0x10000, 0x87d0f321 )
	ROM_LOAD( "epr11266.93", 0x20000, 0x10000, 0xefb4af87 )

	ROM_REGION( 0x080000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "epr11290.10", 0x00000, 0x10000, 0x611f413a )
	ROM_LOAD( "epr11294.11", 0x10000, 0x10000, 0x5eb00fc1 )
	ROM_LOAD( "epr11291.17", 0x20000, 0x10000, 0x3c0797c0 )
	ROM_LOAD( "epr11295.18", 0x30000, 0x10000, 0x25307ef8 )
	ROM_LOAD( "epr11292.23", 0x40000, 0x10000, 0xc29ac34e )
	ROM_LOAD( "epr11296.24", 0x50000, 0x10000, 0x04a437f8 )
	ROM_LOAD( "epr11293.29", 0x60000, 0x10000, 0x41f41063 )
//	ROM_LOAD( "epr11297.30", 0x70000, 0x10000, 0xb6e1fd72 )
	ROM_LOAD( "b17",         0x70000, 0x10000, 0x0315cf42 )	// Beta bootleg uses the rom above.

	ROM_REGION( 0x20000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "epr11267.12", 0x0000, 0x8000, 0xdd50b745 )

	ROM_REGION( 0x1000, REGION_CPU3 )      /* 4k for 7751 onboard ROM */
	ROM_LOAD( "7751.bin",     0x0000, 0x0400, 0x6a9534fc ) /* 7751 - U34 */

	ROM_REGION( 0x08000, REGION_SOUND1 ) /* 7751 sound data */
	ROM_LOAD( "epr11268.1", 0x0000, 0x8000, 0x6d7966da )
ROM_END

/***************************************************************************/

static const struct MemoryReadAddress shinobl_readmem[] =
{
	{ 0x000000, 0x03ffff, MRA_ROM },
	{ 0x400000, 0x40ffff, MRA_TILERAM },
	{ 0x410000, 0x410fff, MRA_TEXTRAM },
	{ 0x440000, 0x440fff, MRA_SPRITERAM },
	{ 0x840000, 0x840fff, MRA_PALETTERAM },
	{ 0xc41002, 0xc41003, io_player1_r },
	{ 0xc41006, 0xc41007, io_player2_r },
	{ 0xc41000, 0xc41001, io_service_r },
	{ 0xc42000, 0xc42001, io_dip1_r },
	{ 0xc42002, 0xc42003, io_dip2_r },
	{ 0xc40000, 0xc40fff, MRA_EXTRAM2 },
	{ 0xffc000, 0xffffff, MRA_WORKINGRAM },
	{-1}
};

static const struct MemoryWriteAddress shinobl_writemem[] =
{
	{ 0x000000, 0x03ffff, MWA_ROM },
	{ 0x400000, 0x40ffff, MWA_TILERAM },
	{ 0x410000, 0x410fff, MWA_TEXTRAM },
	{ 0x440000, 0x440fff, MWA_SPRITERAM },
	{ 0x840000, 0x840fff, MWA_PALETTERAM },
	{ 0xc40000, 0xc40001, sound_command_nmi_w },
	{ 0xc40000, 0xc40fff, MWA_EXTRAM2 },
	{ 0xffc000, 0xffffff, MWA_WORKINGRAM },
	{-1}
};

/***************************************************************************/

static void shinobl_update_proc( void ){
	sys16_fg_scrollx = READ_WORD( &sys16_textram[0x0ff8] ) & 0x01ff;
	sys16_bg_scrollx = READ_WORD( &sys16_textram[0x0ffa] ) & 0x01ff;
	sys16_fg_scrolly = READ_WORD( &sys16_textram[0x0f24] ) & 0x00ff;
	sys16_bg_scrolly = READ_WORD( &sys16_textram[0x0f26] ) & 0x01ff;

	set_fg_page( READ_WORD( &sys16_textram[0x0e9e] ) );
	set_bg_page( READ_WORD( &sys16_textram[0x0e9c] ) );

	set_refresh_3d( READ_WORD( &sys16_extraram2[2] ) );

}

static void shinobl_init_machine( void ){
	static int bank[16] = {0,2,4,6,1,3,5,7,0,0,0,0,0,0,0,0};
	sys16_obj_bank = bank;
	sys16_textmode=1;
	sys16_spritesystem = 2;
	sys16_sprxoffset = -0xbc;
	sys16_fgxoffset = sys16_bgxoffset = 7;
	sys16_tilebank_switch=0x2000;

	sys16_dactype = 1;
	sys16_update_proc = shinobl_update_proc;
}



/***************************************************************************/

MACHINE_DRIVER_7751( machine_driver_shinobl, \
	shinobl_readmem,shinobl_writemem,shinobl_init_machine, gfx1)

/***************************************************************************/

// sys16A custom
ROM_START( tetris )
	ROM_REGION( 0x020000, REGION_CPU1 ) /* 68000 code */
	ROM_LOAD_EVEN( "epr12201.rom", 0x000000, 0x8000, 0x338e9b51 )
	ROM_LOAD_ODD ( "epr12200.rom", 0x000000, 0x8000, 0xfb058779 )

	ROM_REGION( 0x30000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "epr12202.rom", 0x00000, 0x10000, 0x2f7da741 )
	ROM_LOAD( "epr12203.rom", 0x10000, 0x10000, 0xa6e58ec5 )
	ROM_LOAD( "epr12204.rom", 0x20000, 0x10000, 0x0ae98e23 )

	ROM_REGION( 0x010000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "epr12169.rom", 0x0000, 0x8000, 0xdacc6165 )
	ROM_LOAD( "epr12170.rom", 0x8000, 0x8000, 0x87354e42 )

	ROM_REGION( 0x10000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "epr12205.rom", 0x0000, 0x8000, 0x6695dc99 )
ROM_END

// sys16B
ROM_START( tetrisbl )
	ROM_REGION( 0x020000, REGION_CPU1 ) /* 68000 code */
	ROM_LOAD_EVEN( "rom2.bin", 0x000000, 0x10000, 0x4d165c38 )
	ROM_LOAD_ODD ( "rom1.bin", 0x000000, 0x10000, 0x1e912131 )

	ROM_REGION( 0x30000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "scr01.rom", 0x00000, 0x10000, 0x62640221 )
	ROM_LOAD( "scr02.rom", 0x10000, 0x10000, 0x9abd183b )
	ROM_LOAD( "scr03.rom", 0x20000, 0x10000, 0x2495fd4e )

	ROM_REGION( 0x020000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "obj0-o.rom", 0x00000, 0x10000, 0x2fb38880 )
	ROM_LOAD( "obj0-e.rom", 0x10000, 0x10000, 0xd6a02cba )

	ROM_REGION( 0x10000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "s-prog.rom", 0x0000, 0x8000, 0xbd9ba01b )
ROM_END

// sys16B
ROM_START( tetrisa )
	ROM_REGION( 0x020000, REGION_CPU1 ) /* 68000 code */
// Custom Cpu 317-0092
	ROM_LOAD_EVEN( "tetris.a7", 0x000000, 0x10000, 0x9ce15ac9 )
	ROM_LOAD_ODD ( "tetris.a5", 0x000000, 0x10000, 0x98d590ca )

	ROM_REGION( 0x30000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "scr01.rom", 0x00000, 0x10000, 0x62640221 )
	ROM_LOAD( "scr02.rom", 0x10000, 0x10000, 0x9abd183b )
	ROM_LOAD( "scr03.rom", 0x20000, 0x10000, 0x2495fd4e )

	ROM_REGION( 0x020000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "obj0-o.rom", 0x00000, 0x10000, 0x2fb38880 )
	ROM_LOAD( "obj0-e.rom", 0x10000, 0x10000, 0xd6a02cba )

	ROM_REGION( 0x10000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "s-prog.rom", 0x0000, 0x8000, 0xbd9ba01b )
ROM_END

/***************************************************************************/

static const struct MemoryReadAddress tetris_readmem[] =
{
	{ 0x000000, 0x01ffff, MRA_ROM },
	{ 0x400000, 0x40ffff, MRA_TILERAM },
	{ 0x410000, 0x410fff, MRA_TEXTRAM },
	{ 0x418000, 0x41803f, MRA_EXTRAM2 },
	{ 0x440000, 0x440fff, MRA_SPRITERAM },
	{ 0x840000, 0x840fff, MRA_PALETTERAM },
	{ 0xc40000, 0xc40001, MRA_EXTRAM },
	{ 0xc41002, 0xc41003, io_player1_r },
	{ 0xc41006, 0xc41007, io_player2_r },
	{ 0xc41000, 0xc41001, io_service_r },
	{ 0xc42002, 0xc42003, io_dip1_r },
	{ 0xc42000, 0xc42001, io_dip2_r },
	{ 0xc80000, 0xc80001, MRA_NOP },
	{ 0xffc000, 0xffffff, MRA_WORKINGRAM },
	{-1}
};

static const struct MemoryWriteAddress tetris_writemem[] =
{
	{ 0x000000, 0x01ffff, MWA_ROM },
	{ 0x400000, 0x40ffff, MWA_TILERAM },
	{ 0x410000, 0x410fff, MWA_TEXTRAM },
	{ 0x418000, 0x41803f, MWA_EXTRAM2 },
	{ 0x440000, 0x440fff, MWA_SPRITERAM },
	{ 0x840000, 0x840fff, MWA_PALETTERAM },
	{ 0xc40000, 0xc40001, MWA_EXTRAM },
	{ 0xc42006, 0xc42007, sound_command_w },
	{ 0xc43034, 0xc43035, MWA_NOP },
	{ 0xc80000, 0xc80001, MWA_NOP },
	{ 0xffc000, 0xffffff, MWA_WORKINGRAM },
	{-1}
};
/***************************************************************************/

static void tetris_update_proc( void ){
	sys16_fg_scrollx = READ_WORD( &sys16_textram[0x0e98] );
	sys16_bg_scrollx = READ_WORD( &sys16_textram[0x0e9a] );
	sys16_fg_scrolly = READ_WORD( &sys16_textram[0x0e90] );
	sys16_bg_scrolly = READ_WORD( &sys16_textram[0x0e92] );

	set_fg_page( READ_WORD( &sys16_extraram2[0x38] ) );
	set_bg_page( READ_WORD( &sys16_extraram2[0x28] ) );

	set_refresh( READ_WORD( &sys16_extraram[0x0] ) );
}

static void tetris_init_machine( void ){
	static int bank[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

	sys16_obj_bank = bank;

	patch_code( 0xba6, 0x4e );
	patch_code( 0xba7, 0x71 );

	sys16_sprxoffset = -0x40;
	sys16_update_proc = tetris_update_proc;
}

static void init_tetris( void )
{
	sys16_onetime_init_machine();
	sys16_sprite_decode( 1,0x10000 );
}

static void init_tetrisbl( void )
{
	sys16_onetime_init_machine();
	sys16_sprite_decode( 1,0x20000 );
}
/***************************************************************************/

INPUT_PORTS_START( tetris )
	SYS16_JOY1
	SYS16_JOY2
	SYS16_SERVICE
	SYS16_COINAGE /* unconfirmed */

PORT_START	/* DSW1 */
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x01, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x02, 0x00, DEF_STR( Demo_Sounds ) )
	PORT_DIPSETTING(    0x02, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x0c, 0x0c, DEF_STR( Unknown ) )	// from the code it looks like some kind of difficulty
	PORT_DIPSETTING(    0x0c, "A" )					// level, but all 4 levels points to the same place
	PORT_DIPSETTING(    0x08, "B" )					// so it doesn't actually change anything!!
	PORT_DIPSETTING(    0x04, "C" )
	PORT_DIPSETTING(    0x00, "D" )
	PORT_DIPNAME( 0x30, 0x30, DEF_STR( Difficulty ) )
	PORT_DIPSETTING(    0x20, "Easy" )
	PORT_DIPSETTING(    0x30, "Normal" )
	PORT_DIPSETTING(    0x10, "Hard" )
	PORT_DIPSETTING(    0x00, "Hardest" )
	PORT_DIPNAME( 0x40, 0x40, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x40, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x80, 0x80, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x80, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
INPUT_PORTS_END

/***************************************************************************/

MACHINE_DRIVER( machine_driver_tetris, \
	tetris_readmem,tetris_writemem,tetris_init_machine, gfx1 )

/***************************************************************************/
// sys16B
ROM_START( wb3 )
	ROM_REGION( 0x40000, REGION_CPU1 ) /* 68000 code */
	ROM_LOAD_EVEN( "epr12259.a7", 0x000000, 0x20000, 0x54927c7e )
	ROM_LOAD_ODD ( "epr12258.a5", 0x000000, 0x20000, 0x01f5898c )

	ROM_REGION( 0x30000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "epr12124.a14", 0x00000, 0x10000, 0xdacefb6f )
	ROM_LOAD( "epr12125.a15", 0x10000, 0x10000, 0x9fc36df7 )
	ROM_LOAD( "epr12126.a16", 0x20000, 0x10000, 0xa693fd94 )

	ROM_REGION( 0x080000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "epr12093.b4", 0x000000, 0x010000, 0x4891e7bb )
	ROM_LOAD( "epr12097.b8", 0x010000, 0x010000, 0xe645902c )
	ROM_LOAD( "epr12091.b2", 0x020000, 0x010000, 0x8409a243 )
	ROM_LOAD( "epr12095.b6", 0x030000, 0x010000, 0xe774ec2c )
	ROM_LOAD( "epr12090.b1", 0x040000, 0x010000, 0xaeeecfca )
	ROM_LOAD( "epr12094.b5", 0x050000, 0x010000, 0x615e4927 )
	ROM_LOAD( "epr12092.b3", 0x060000, 0x010000, 0x5c2f0d90 )
	ROM_LOAD( "epr12096.b7", 0x070000, 0x010000, 0x0cd59d6e )

	ROM_REGION( 0x10000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "epr12127.a10", 0x0000, 0x8000, 0x0bb901bb )
ROM_END

ROM_START( wb3a )
	ROM_REGION( 0x40000, REGION_CPU1 ) /* 68000 code */
// Custom CPU 317-0089
	ROM_LOAD_EVEN( "epr12137.a7", 0x000000, 0x20000, 0x6f81238e )
	ROM_LOAD_ODD ( "epr12136.a5", 0x000000, 0x20000, 0x4cf05003 )

	ROM_REGION( 0x30000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "epr12124.a14", 0x00000, 0x10000, 0xdacefb6f )
	ROM_LOAD( "epr12125.a15", 0x10000, 0x10000, 0x9fc36df7 )
	ROM_LOAD( "epr12126.a16", 0x20000, 0x10000, 0xa693fd94 )

	ROM_REGION( 0x080000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "epr12093.b4", 0x000000, 0x010000, 0x4891e7bb )
	ROM_LOAD( "epr12097.b8", 0x010000, 0x010000, 0xe645902c )
	ROM_LOAD( "epr12091.b2", 0x020000, 0x010000, 0x8409a243 )
	ROM_LOAD( "epr12095.b6", 0x030000, 0x010000, 0xe774ec2c )
	ROM_LOAD( "epr12090.b1", 0x040000, 0x010000, 0xaeeecfca )
	ROM_LOAD( "epr12094.b5", 0x050000, 0x010000, 0x615e4927 )
	ROM_LOAD( "epr12092.b3", 0x060000, 0x010000, 0x5c2f0d90 )
	ROM_LOAD( "epr12096.b7", 0x070000, 0x010000, 0x0cd59d6e )

	ROM_REGION( 0x10000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "epr12127.a10", 0x0000, 0x8000, 0x0bb901bb )
ROM_END

/***************************************************************************/

static const struct MemoryReadAddress wb3_readmem[] =
{
	{ 0x000000, 0x03ffff, MRA_ROM },
	{ 0x400000, 0x40ffff, MRA_TILERAM },
	{ 0x410000, 0x410fff, MRA_TEXTRAM },
	{ 0x440000, 0x440fff, MRA_SPRITERAM },
	{ 0x840000, 0x840fff, MRA_PALETTERAM },
	{ 0xc41002, 0xc41003, io_player1_r },
	{ 0xc41006, 0xc41007, io_player2_r },
	{ 0xc41000, 0xc41001, io_service_r },
	{ 0xc42002, 0xc42003, io_dip1_r },
	{ 0xc42000, 0xc42001, io_dip2_r },
	{ 0xffc000, 0xffffff, MRA_WORKINGRAM },
	{-1}
};

static WRITE_HANDLER( wb3_sound_command_w )
{
	if( (data&0xff000000)==0 )
		sound_command_w(offset,data>>8);
}

static const struct MemoryWriteAddress wb3_writemem[] =
{
	{ 0x000000, 0x03ffff, MWA_ROM },
	{ 0x3f0000, 0x3f0003, MWA_NOP },
	{ 0x400000, 0x40ffff, MWA_TILERAM },
	{ 0x410000, 0x410fff, MWA_TEXTRAM },
	{ 0x440000, 0x440fff, MWA_SPRITERAM },
	{ 0x840000, 0x840fff, MWA_PALETTERAM },
	{ 0xc40000, 0xc40001, MWA_EXTRAM2 },
	{ 0xffc008, 0xffc009, wb3_sound_command_w },
	{ 0xffc000, 0xffffff, MWA_WORKINGRAM },
	{-1}
};
/***************************************************************************/

static void wb3_update_proc( void ){
	sys16_fg_scrollx = READ_WORD( &sys16_textram[0x0e98] );
	sys16_bg_scrollx = READ_WORD( &sys16_textram[0x0e9a] );
	sys16_fg_scrolly = READ_WORD( &sys16_textram[0x0e90] );
	sys16_bg_scrolly = READ_WORD( &sys16_textram[0x0e92] );

	set_fg_page( READ_WORD( &sys16_textram[0x0e80] ) );
	set_bg_page( READ_WORD( &sys16_textram[0x0e82] ) );
	set_refresh( READ_WORD( &sys16_extraram2[0] ) );
}


static void wb3_init_machine( void ){
	static int bank[16] = {4,0,2,0,6,0,0,0x06,0,0,0,0x04,0,0x02,0,0};

	sys16_obj_bank = bank;

	sys16_update_proc = wb3_update_proc;
}

static void init_wb3(void)
{
	sys16_onetime_init_machine();
	sys16_sprite_decode( 4,0x20000 );
}

/***************************************************************************/

INPUT_PORTS_START( wb3 )
	SYS16_JOY1
	SYS16_JOY2
	SYS16_SERVICE
	SYS16_COINAGE

PORT_START	/* DSW1 */
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x01, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x02, 0x02, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x02, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x0c, 0x0c, DEF_STR( Lives ) )
	PORT_DIPSETTING(    0x00, "2" )
	PORT_DIPSETTING(    0x0c, "3" )
	PORT_DIPSETTING(    0x08, "4" )
	PORT_DIPSETTING(    0x04, "5" )
	PORT_DIPNAME( 0x10, 0x10, DEF_STR( Bonus_Life ) )		//??
	PORT_DIPSETTING(    0x10, "5000/10000/18000/30000" )
	PORT_DIPSETTING(    0x00, "5000/15000/30000" )
	PORT_DIPNAME( 0x20, 0x20, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x20, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x40, 0x40, "Allow Round Select" )
	PORT_DIPSETTING(    0x40, DEF_STR( No ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Yes ) )			// no collision though
	PORT_DIPNAME( 0x80, 0x80, DEF_STR( Unused ) )
	PORT_DIPSETTING(    0x80, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )

INPUT_PORTS_END

/***************************************************************************/

MACHINE_DRIVER( machine_driver_wb3, \
	wb3_readmem,wb3_writemem,wb3_init_machine, gfx1 )

/***************************************************************************/
// sys16B
ROM_START( wb3bl )
	ROM_REGION( 0x040000, REGION_CPU1 ) /* 68000 code */
	ROM_LOAD_EVEN( "wb3_03", 0x000000, 0x10000, 0x0019ab3b )
	ROM_LOAD_ODD ( "wb3_05", 0x000000, 0x10000, 0x196e17ee )
	ROM_LOAD_EVEN( "wb3_02", 0x020000, 0x10000, 0xc87350cb )
	ROM_LOAD_ODD ( "wb3_04", 0x020000, 0x10000, 0x565d5035 )

	ROM_REGION( 0x30000, REGION_GFX1 | REGIONFLAG_DISPOSE ) /* tiles */
	ROM_LOAD( "wb3_14", 0x00000, 0x10000, 0xd3f20bca )
	ROM_LOAD( "wb3_15", 0x10000, 0x10000, 0x96ff9d52 )
	ROM_LOAD( "wb3_16", 0x20000, 0x10000, 0xafaf0d31 )

	ROM_REGION( 0x080000*2, REGION_GFX2 ) /* sprites */
	ROM_LOAD( "epr12093.b4", 0x000000, 0x010000, 0x4891e7bb )
	ROM_LOAD( "epr12097.b8", 0x010000, 0x010000, 0xe645902c )
	ROM_LOAD( "epr12091.b2", 0x020000, 0x010000, 0x8409a243 )
	ROM_LOAD( "epr12095.b6", 0x030000, 0x010000, 0xe774ec2c )
	ROM_LOAD( "epr12090.b1", 0x040000, 0x010000, 0xaeeecfca )
	ROM_LOAD( "epr12094.b5", 0x050000, 0x010000, 0x615e4927 )
	ROM_LOAD( "epr12092.b3", 0x060000, 0x010000, 0x5c2f0d90 )
	ROM_LOAD( "epr12096.b7", 0x070000, 0x010000, 0x0cd59d6e )

	ROM_REGION( 0x10000, REGION_CPU2 ) /* sound CPU */
	ROM_LOAD( "epr12127.a10", 0x0000, 0x8000, 0x0bb901bb )
ROM_END

/***************************************************************************/

static const struct MemoryReadAddress wb3bl_readmem[] =
{
	{ 0x000000, 0x03ffff, MRA_ROM },
	{ 0x400000, 0x40ffff, MRA_TILERAM },
	{ 0x410000, 0x410fff, MRA_TEXTRAM },
	{ 0x440000, 0x440fff, MRA_SPRITERAM },
	{ 0x840000, 0x840fff, MRA_PALETTERAM },
	{ 0xc41002, 0xc41003, io_player1_r },
	{ 0xc41004, 0xc41005, io_player2_r },
	{ 0xc41000, 0xc41001, io_service_r },
	{ 0xc42002, 0xc42003, io_dip1_r },
	{ 0xc42000, 0xc42001, io_dip2_r },
	{ 0xc46000, 0xc4601f, MRA_EXTRAM3 },
	{ 0xff0000, 0xffffff, MRA_WORKINGRAM },
	{-1}
};

static const struct MemoryWriteAddress wb3bl_writemem[] =
{
	{ 0x000000, 0x03ffff, MWA_ROM },
	{ 0x3f0000, 0x3f0003, MWA_NOP },
	{ 0x400000, 0x40ffff, MWA_TILERAM },
	{ 0x410000, 0x410fff, MWA_TEXTRAM },
	{ 0x440000, 0x440fff, MWA_SPRITERAM },
	{ 0x840000, 0x840fff, MWA_PALETTERAM },
	{ 0xc42006, 0xc42007, sound_command_w },
	{ 0xc40000, 0xc40001, MWA_EXTRAM2 },
	{ 0xc44000, 0xc44001, MWA_NOP },
	{ 0xc46000, 0xc4601f, MWA_EXTRAM3 },
	{ 0xff0000, 0xffffff, MWA_WORKINGRAM },
	{-1}
};

/***************************************************************************/

static void wb3bl_update_proc( void ){
	sys16_fg_scrollx = READ_WORD( &sys16_workingram[0xc030] );
	sys16_bg_scrollx = READ_WORD( &sys16_workingram[0xc038] );
	sys16_fg_scrolly = READ_WORD( &sys16_workingram[0xc032] );
	sys16_bg_scrolly = READ_WORD( &sys16_workingram[0xc03c] );

	set_fg_page( READ_WORD( &sys16_textram[0x0ff6] ) );
	set_bg_page( READ_WORD( &sys16_textram[0x0ff4] ) );
	set_refresh( READ_WORD( &sys16_extraram2[0] ) );
}

static void wb3bl_init_machine( void ){
	static int bank[16] = {4,0,2,0,6,0,0,0x06,0,0,0,0x04,0,0x02,0,0};

	sys16_obj_bank = bank;

	patch_code( 0x17058, 0x4e );
	patch_code( 0x17059, 0xb9 );
	patch_code( 0x1705a, 0x00 );
	patch_code( 0x1705b, 0x00 );
	patch_code( 0x1705c, 0x09 );
	patch_code( 0x1705d, 0xdc );
	patch_code( 0x1705e, 0x4e );
	patch_code( 0x1705f, 0xf9 );
	patch_code( 0x17060, 0x00 );
	patch_code( 0x17061, 0x01 );
	patch_code( 0x17062, 0x70 );
	patch_code( 0x17063, 0xe0 );
	patch_code( 0x1a3a, 0x31 );
	patch_code( 0x1a3b, 0x7c );
	patch_code( 0x1a3c, 0x80 );
	patch_code( 0x1a3d, 0x00 );
	patch_code( 0x23df8, 0x14 );
	patch_code( 0x23df9, 0x41 );
	patch_code( 0x23dfa, 0x10 );
	patch_code( 0x23dfd, 0x14 );
	patch_code( 0x23dff, 0x1c );

	sys16_update_proc = wb3bl_update_proc;
}

static void init_wb3bl(void)
{
	int i;

	sys16_onetime_init_machine();

	/* invert the graphics bits on the tiles */
	for (i = 0; i < 0x30000; i++)
		memory_region(REGION_GFX1)[i] ^= 0xff;

	sys16_sprite_decode( 4,0x20000 );
}

/***************************************************************************/

MACHINE_DRIVER( machine_driver_wb3bl, \
	wb3bl_readmem,wb3bl_writemem,wb3bl_init_machine, gfx1 )
GAME( 1987, aliensyn, 0,        aliensyn, aliensyn, aliensyn, ROT0,         "Sega",    "Alien Syndrome (set 1)")
GAME( 1988, altbeast, 0,        altbeast, altbeast, altbeast, ROT0,         "Sega",    "Altered Beast (Version 1)")
GAMEX(1988, altbeas2, altbeast, altbeas2, altbeast, altbeast, ROT0,         "Sega",    "Altered Beast (Version 2)", GAME_NO_SOUND)
GAME( 1990, aurail,   0,        aurail,   aurail,   aurail,   ROT0,         "Sega / Westone", "Aurail (set 1)")
GAME( 1990, auraila,  aurail,   aurail,   aurail,   auraila,  ROT0,         "Sega / Westone", "Aurail (set 2)")
GAME( 1989, bayroute, 0,        bayroute, bayroute, bayroute, ROT0,         "Sunsoft / Sega", "Bay Route (set 1)")
GAMEX(1989, eswat,    0,        eswat,    eswat,    eswat,    ROT0,         "Sega",    "E-Swat", GAME_NOT_WORKING)
GAME( 1989, eswatbl,  eswat,    eswat,    eswat,    eswat,    ROT0,         "bootleg", "E-Swat (bootleg)")
GAME( 1986, fantzone, 0,        fantzone, fantzone, fantzone, ROT0,         "Sega",    "Fantasy Zone (Japan New Ver.)")
GAME( 1986, fantzono, fantzone, fantzono, fantzone, fantzone, ROT0,         "Sega",    "Fantasy Zone (Old Ver.)")
GAME( 1989, goldnaxe, 0,        goldnaxe, goldnaxe, goldnaxe, ROT0,         "Sega",    "Golden Axe (Version 1)")
GAME( 1989, goldnaxa, goldnaxe, goldnaxa, goldnaxe, goldnaxe, ROT0,         "Sega",    "Golden Axe (Version 2)")
GAME( 1987, shinobi,  0,        shinobi,  shinobi,  shinobi,  ROT0,         "Sega",    "Shinobi (set 1)")
GAME( 1987, shinobl,  shinobi,  shinobl,  shinobi,  shinobi,  ROT0,         "bootleg", "Shinobi (bootleg)")
GAMEX(1988, tetris,   0,        tetris,   tetris,   tetris,   ROT0,         "Sega",    "Tetris (Sega Set 1)", GAME_NOT_WORKING)
GAME( 1988, tetrisbl, tetris,   tetris,   tetris,   tetrisbl, ROT0,         "bootleg", "Tetris (Sega bootleg)")
GAME( 1988, wb3,      0,        wb3,      wb3,      wb3,      ROT0,         "Sega / Westone", "Wonder Boy III - Monster Lair (set 1)")
GAME( 1988, wb3bl,    wb3,      wb3bl,    wb3,      wb3bl,    ROT0,         "bootleg", "Wonder Boy III - Monster Lair (bootleg)")
