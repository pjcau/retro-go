import re
M='/private/tmp/claude-501/-Users-pierrejonnycau-Documents-WORKS-esp32-emu-turbo/425519e0-2de7-4435-8fda-55ed3bb1725b/scratchpad/m2k/src'
KEEP=['aliensyn','altbeast','aurail','bayroute','eswat','fantzone','goldnaxe','shinobi','tetris','wb3']
src=open(M+'/drivers/system16.c',encoding='latin-1').read().split('\n')
roms=[(i,re.match(r'ROM_START\( *(\w+) *\)',l).group(1)) for i,l in enumerate(src) if l.startswith('ROM_START(')]
games={}
for i,l in enumerate(src):
    m=re.match(r'GAMEX?\s*\(\s*[\d?]+,\s*(\w+),\s*(\w+),\s*(\w+),\s*(\w+),\s*(\w+),',l)
    if m: games[m.group(1)]=(i,m.group(2),l)
fam={}
for i,n in roms:
    p=games.get(n,(0,'0',''))[1]
    fam.setdefault(n if p=='0' else p,[]).append(i)
def start(i):
    while src[i-1].startswith('//') or src[i-1].startswith('/***') or src[i-1].strip()=='': i-=1
    return i
starts=sorted((start(v[0]),k) for k,v in fam.items())
first_game=min(v[0] for v in games.values())
gstart=first_game
while not src[gstart-1].startswith('/***') and gstart>first_game-12: gstart-=1
common=src[:starts[0][0]]
a=next(i for i,l in enumerate(common) if l.startswith('// SYS18 Sound'))
b=next(i for i,l in enumerate(common) if l.startswith('static WRITE_HANDLER( sound_command_w )'))
out=common[:a]+['/* mame-go: the System 18 (YM3438, RF5C68), Sega 3D (YM2203, SegaPCM) and', '   Super Hang-On / OutRun sound sections of the original are left out here */','']+common[b:]
for n,(s,k) in enumerate(starts):
    e=starts[n+1][0] if n+1<len(starts) else gstart
    if k in KEEP: out+=src[s:e]
kept=[]
for name,(i,p,l) in sorted(games.items(), key=lambda x:x[1][0]):
    f=name if p=='0' else p
    if f in KEEP and ('GAME_NOT_WORKING' not in l or p=='0'):   # a parent stays: its clones point at it
        out.append(l)
        if 'GAME_NOT_WORKING' not in l: kept.append(name)
hdr='''/* mame-go: a subset of MAME 0.37b5's drivers/system16.c (mame2000-libretro):
   the System 16 boards with one 68000, a Z80 and a YM2151 (+ uPD7759 or 7751).
   Kept families: %s. The System 18, Hang-On, Space Harrier, OutRun and X/Y
   board sections and the sets MAME marks as not working are left out.
   Regenerate from upstream rather than editing the game sections by hand. */
''' % ', '.join(KEEP)
text='\n'.join(out)+'\n'
# internal RAM is the scarce resource on the ESP32-S3 (.data lives there): the
# tables nothing writes go to flash
text=re.sub(r'\bstatic struct (MachineDriver|MemoryReadAddress|MemoryWriteAddress|IOReadPort|IOWritePort|GfxLayout|GfxDecodeInfo|YM2151interface|UPD7759_interface|DACinterface)\b', r'static const struct \1', text)
open('src/drivers/system16.c','w',encoding='latin-1').write(hdr+text)
mach=open(M+'/machine/system16.c',encoding='latin-1').read()
mach=re.sub(r'^unsigned short (\w+_decrypt_\w+\[16\]\[256\])', r'const unsigned short \1', mach, flags=re.M)
open('src/machine/system16.c','w',encoding='latin-1').write('/* mame-go: MAME 0.37b5 machine/system16.c with its decryption tables const (flash, not internal RAM) */\n'+mach)
print(len(out),'lines; games:',' '.join(kept))
