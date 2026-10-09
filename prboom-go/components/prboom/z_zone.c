/* Emacs style mode select   -*- C++ -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2000 by
 *  Jess Haas, Nicolas Kalkhof, Colin Phipps, Florian Schulze
 *  Copyright 2005, 2006 by
 *  Florian Schulze, Colin Phipps, Neil Stevens, Andrey Budko
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *  02111-1307, USA.
 *
 * DESCRIPTION:
 *      Zone Memory Allocation. Neat.
 *
 * Neat enough to be rewritten by Lee Killough...
 *
 * Must not have been real neat :)
 *
 * Made faster and more general, and added wrappers for all of Doom's
 * memory allocation functions, including malloc() and similar functions.
 * Added line and file numbers, in case of error. Added performance
 * statistics and tunables.
 *-----------------------------------------------------------------------------
 */


// use config.h if autoconf made one -- josh
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

#include "doomstat.h"
#include "lprintf.h"
#include "z_zone.h"

#define ZONE_PSRAM_RESERVE (1536 * 1024)

#define CHUNK_SIZE 4        // Minimum chunk size at which blocks are allocated
#define ZONEID  0x931d4a11  // signature for block header

typedef struct memblock
{
  uint32_t zoneid;
  uint32_t tag: 10;
  uint32_t size:22;

  struct memblock *next,*prev;
  void **user;

#ifdef DOOMMEM
  uint32_t site;              // index into zone_sites, 0 = unknown
#endif

#ifdef INSTRUMENTED
  const char *file;
  int line;
#endif

} memblock_t;

/* size of block header
 * cph - base on sizeof(memblock_t), which can be larger than CHUNK_SIZE on
 * 64bit architectures */
static const size_t HEADER_SIZE = (sizeof(memblock_t)+CHUNK_SIZE-1) & ~(CHUNK_SIZE-1);

static memblock_t *blockbytag[PU_MAX];

#ifdef DOOMMEM
/* Zone accounting for the heap hunt (`DOOMMEM=1`): live bytes and blocks per
 * tag, and the call sites holding them, so a falling free heap can be traced
 * to the code that allocated what is still there. Two adds per allocation and
 * four bytes of header; the build flag keeps it out of a release.
 *
 * Sites are return addresses. Resolve them with
 *   xtensa-esp32s3-elf-addr2line -pfiCe build/prboom-go.elf <addr>
 */
#define ZONE_SITES 128

typedef struct {
  void *ra;
  size_t bytes, blocks, bytes_prev;
} zone_site_t;

static zone_site_t zone_sites[ZONE_SITES];     // [0] collects the overflow
static size_t zone_bytes[PU_MAX], zone_blocks[PU_MAX];
static size_t zone_total_prev;
static unsigned zone_purges;                   // cache freed to keep the reserve
static unsigned zone_oom_purges;               // cache freed after malloc failed
static unsigned zone_allocs;                   // allocations since the last report

static uint32_t zone_site_index(void *ra)
{
  uint32_t h = (uint32_t)(uintptr_t)ra;
  h = (h >> 2) * 2654435761u;
  for (uint32_t i = 0; i < 16; i++)
  {
    uint32_t slot = 1 + ((h + i) % (ZONE_SITES - 1));
    if (zone_sites[slot].ra == ra)
      return slot;
    if (!zone_sites[slot].ra)
    {
      zone_sites[slot].ra = ra;
      return slot;
    }
  }
  return 0;   // this hash is full: charged to the unknown slot, never dropped
}

static void zone_account(memblock_t *block, void *ra)
{
  zone_allocs++;
  block->site = zone_site_index(ra);
  zone_sites[block->site].bytes += block->size;
  zone_sites[block->site].blocks++;
  zone_bytes[block->tag] += block->size;
  zone_blocks[block->tag]++;
}

static void zone_unaccount(memblock_t *block)
{
  zone_sites[block->site].bytes -= block->size;
  zone_sites[block->site].blocks--;
  zone_bytes[block->tag] -= block->size;
  zone_blocks[block->tag]--;
}

/* The wrappers below call Z_Malloc, so without this every Z_Calloc/Z_Realloc/
 * Z_Strdup would be charged to z_zone.c instead of to its caller. */
static void zone_resite(void *p, void *ra)
{
  if (!p)
    return;
  memblock_t *block = (memblock_t *)((char *)p - HEADER_SIZE);
  zone_sites[block->site].bytes -= block->size;
  zone_sites[block->site].blocks--;
  block->site = zone_site_index(ra);
  zone_sites[block->site].bytes += block->size;
  zone_sites[block->site].blocks++;
}

static const char *const zone_tag_names[PU_MAX] = {
  "free", "static", "sound", "music", "level", "levspec", "cache"
};

void Z_LogStats(const char *where)
{
  size_t total = 0, blocks = 0, site_total = 0, site_blocks = 0;
  size_t order[8];
  int shown = 0;

  for (int tag = 0; tag < PU_MAX; tag++)
    total += zone_bytes[tag], blocks += zone_blocks[tag];

  /* The per-tag and the per-site totals are kept by different code (only
   * Z_ChangeTag touches tags, only zone_resite touches sites), so they
   * agreeing is a check on both. A report saying MISMATCH means the numbers
   * below cannot be trusted, not that something leaked. */
  for (int i = 0; i < ZONE_SITES; i++)
    site_total += zone_sites[i].bytes, site_blocks += zone_sites[i].blocks;
  if (site_total != total || site_blocks != blocks)
    lprintf(LO_INFO, "DOOMMEM MISMATCH sites %u/%u tags %u/%u\n",
            (unsigned)site_total, (unsigned)site_blocks,
            (unsigned)total, (unsigned)blocks);

  lprintf(LO_INFO, "DOOMMEM %s zone %u B (%+d since last)", where,
          (unsigned)total, (int)(total - zone_total_prev));
  for (int tag = PU_FREE + 1; tag < PU_MAX; tag++)
    if (zone_blocks[tag])
      lprintf(LO_INFO, " %s %u/%u", zone_tag_names[tag],
              (unsigned)zone_bytes[tag], (unsigned)zone_blocks[tag]);
  lprintf(LO_INFO, " allocs %u purges %u+%u\n", zone_allocs, zone_purges,
          zone_oom_purges);
  zone_total_prev = total;
  zone_allocs = 0;

  /* The eight sites holding the most, with what each gained since the last
   * report: a true leak is a site whose delta stays positive. */
  while (shown < 8)
  {
    int best = -1;
    for (int i = 0; i < ZONE_SITES; i++)
    {
      if (!zone_sites[i].blocks)
        continue;
      bool taken = false;
      for (int j = 0; j < shown; j++)
        taken |= (order[j] == (size_t)i);
      if (taken)
        continue;
      if (best < 0 || zone_sites[i].bytes > zone_sites[best].bytes)
        best = i;
    }
    if (best < 0)
      break;
    order[shown++] = best;
    lprintf(LO_INFO, "DOOMMEM   site %p %u B %u blk (%+d)\n", zone_sites[best].ra,
            (unsigned)zone_sites[best].bytes, (unsigned)zone_sites[best].blocks,
            (int)(zone_sites[best].bytes - zone_sites[best].bytes_prev));
    zone_sites[best].bytes_prev = zone_sites[best].bytes;
  }
}
#endif /* DOOMMEM */

#ifdef INSTRUMENTED

// statistics for evaluating performance
static int active_memory = 0;
static int purgable_memory = 0;

static void Z_DrawStats(void)            // Print allocation statistics
{
  if (gamestate != GS_LEVEL)
    return;

    unsigned long total_memory = active_memory + purgable_memory;
    double s = 100.0 / total_memory;

    doom_printf("%-5i\t%6.01f%%\tstatic\n"
            "%-5i\t%6.01f%%\tpurgable\n"
            "%-5li\t\ttotal\n",
            active_memory,
            active_memory*s,
            purgable_memory,
            purgable_memory*s,
            total_memory
            );
}

#ifdef HEAPDUMP

#ifndef HEAPDUMP_DIR
#define HEAPDUMP_DIR "."
#endif

void W_PrintLump(FILE* fp, void* p);

void Z_DumpMemory(void)
{
  static int dump;
  char buf[PATH_MAX + 1];
  FILE* fp;
  size_t total_cache = 0, total_free = 0, total_malloc = 0;
  int tag;

  sprintf(buf, "%s/memdump.%d", HEAPDUMP_DIR, dump++);
  fp = fopen(buf, "w");
  for (tag = PU_FREE; tag < PU_MAX; tag++)
  {
    memblock_t* end_block, *block;
    block = blockbytag[tag];
    if (!block)
      continue;
    end_block = block->prev;
    while (1)
    {
      switch (block->tag) {
      case PU_FREE:
        fprintf(fp, "free %d\n", block->size);
        total_free += block->size;
        break;
      case PU_CACHE:
        fprintf(fp, "cache %s:%d:%d\n", block->file, block->line, block->size);
        total_cache += block->size;
        break;
      case PU_LEVEL:
        fprintf(fp, "level %s:%d:%d\n", block->file, block->line, block->size);
        total_malloc += block->size;
        break;
      default:
        fprintf(fp, "malloc %s:%d:%d", block->file, block->line, block->size);
        total_malloc += block->size;
        if (block->file)
          if (strstr(block->file,"w_memcache.c"))
            W_PrintLump(fp, (char*)block + HEADER_SIZE);
        fputc('\n', fp);
        break;
      }
      if (block == end_block)
        break;
      block=block->next;
    }
  }
  fprintf(fp, "malloc %d, cache %d, free %d, total %d\n",
    total_malloc, total_cache, total_free,
    total_malloc + total_cache + total_free);
  fclose(fp);
}
#endif
#endif

void Z_Close(void)
{
#ifdef INSTRUMENTED
  Z_DumpMemory();
#endif
  // Release everything
  Z_FreeTags(PU_FREE, PU_MAX);
}

void Z_Init(void)
{
  // Nothing to do
}

void *(Z_Malloc)(size_t size, int tag, void **user DA(const char *file, int line))
{
  memblock_t *block = NULL;

#ifdef INSTRUMENTED
#ifdef CHECKHEAP
  Z_CheckHeap();
#endif

  if (tag >= PU_PURGELEVEL && !user)
    I_Error ("Z_Malloc: An owner is required for purgable blocks"
#ifdef INSTRUMENTED
             "Source: %s:%d", file, line
#endif
       );
#endif

  if (!size)
    return user ? *user = NULL : NULL;           // malloc(0) returns NULL

  size = (size+CHUNK_SIZE-1) & ~(CHUNK_SIZE-1);  // round to chunk size

#ifdef ESP_PLATFORM
  // RG: the lump cache only shrank when one of *our* mallocs failed, so it
  // grew until PSRAM was empty and a non-zone allocation elsewhere (SD,
  // audio, display) got NULL and crashed (LoadProhibited after ~2 min of
  // demos, free PSRAM 7.2 MB -> 0.15 MB). Keep a reserve for everyone else.
  while (blockbytag[PU_CACHE] && heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < ZONE_PSRAM_RESERVE + size)
    (Z_FreeTags)(PU_CACHE, PU_CACHE, 2);
#endif

  while (!(block = (malloc)(size + HEADER_SIZE))) {
    if (!blockbytag[PU_CACHE])
      I_Error ("Z_Malloc: Failure trying to allocate %lu bytes"
#ifdef INSTRUMENTED
               "\nSource: %s:%d"
#endif
               ,(unsigned long) size
#ifdef INSTRUMENTED
               , file, line
#endif
      );
    // RG: Don't nuke the whole cache at once!
    (Z_FreeTags)(PU_CACHE, PU_CACHE, 2);
  }

  if (!blockbytag[tag])
  {
    blockbytag[tag] = block;
    block->next = block->prev = block;
  }
  else
  {
    blockbytag[tag]->prev->next = block;
    block->prev = blockbytag[tag]->prev;
    block->next = blockbytag[tag];
    blockbytag[tag]->prev = block;
  }

  block->size = size;

#ifdef INSTRUMENTED
  if (tag >= PU_PURGELEVEL)
    purgable_memory += block->size;
  else
    active_memory += block->size;
#endif

#ifdef INSTRUMENTED
  block->file = file;
  block->line = line;
#endif

  block->zoneid = ZONEID;     // signature required in block header
  block->tag = tag;           // tag
  block->user = user;         // user
#ifdef DOOMMEM
  zone_account(block, __builtin_return_address(0));
#endif
  block = (memblock_t *)((char *) block + HEADER_SIZE);
  if (user)                   // if there is a user
    *user = block;            // set user to point to new block

#ifdef INSTRUMENTED
  Z_DrawStats();           // print memory allocation stats
  // scramble memory -- weed out any bugs
  memset(block, gametic & 0xff, size);
#endif

  return block;
}

void (Z_Free)(void *p DA(const char *file, int line))
{
  memblock_t *block = (memblock_t *)((char *) p - HEADER_SIZE);

#ifdef INSTRUMENTED
#ifdef CHECKHEAP
  Z_CheckHeap();
#endif
#endif

  if (!p)
    return;

  if (block->zoneid != ZONEID)
    I_Error("Z_Free: freed a pointer without ZONEID"
#ifdef INSTRUMENTED
            "\nSource: %s:%d"
            "\nSource of malloc: %s:%d"
            , file, line, block->file, block->line
#endif
           );
  block->zoneid = 0;          // Nullify id so another free fails

#ifdef DOOMMEM
  zone_unaccount(block);
#endif

  if (block->user)            // Nullify user if one exists
    *block->user = NULL;

  if (block == block->next)
    blockbytag[block->tag] = NULL;
  else
    if (blockbytag[block->tag] == block)
      blockbytag[block->tag] = block->next;
  block->prev->next = block->next;
  block->next->prev = block->prev;

#ifdef INSTRUMENTED
  if (block->tag >= PU_PURGELEVEL)
    purgable_memory -= block->size;
  else
    active_memory -= block->size;

  /* scramble memory -- weed out any bugs */
  memset(block, gametic & 0xff, block->size + HEADER_SIZE);
#endif

  (free)(block);

#ifdef INSTRUMENTED
      Z_DrawStats();           // print memory allocation stats
#endif
}

void (Z_FreeTags)(int lowtag, int hightag, int max DA(const char *file, int line))
{
#ifdef HEAPDUMP
  Z_DumpMemory();
#endif

  lowtag = MAX(lowtag, PU_FREE+1);
  hightag = MIN(hightag, PU_MAX-1);

  for (;lowtag <= hightag; hightag--)
  {
    if (!blockbytag[hightag])
      continue;
    memblock_t *block = blockbytag[hightag];
    memblock_t *end_block = block->prev;
    while (max--)
    {
      memblock_t *next = block->next;
#ifdef INSTRUMENTED
      (Z_Free)((char *) block + HEADER_SIZE, file, line);
#else
      (Z_Free)((char *) block + HEADER_SIZE);
#endif
      if (block == end_block)
        break;
      block = next;               // Advance to next block
    }
  }
}

void (Z_ChangeTag)(void *ptr, int tag DA(const char *file, int line))
{
  memblock_t *block = (memblock_t *)((char *) ptr - HEADER_SIZE);

  if (!ptr || tag == block->tag)
    return;

#ifdef INSTRUMENTED
#ifdef CHECKHEAP
  Z_CheckHeap();
#endif

  if (tag >= PU_PURGELEVEL && !block->user)
    I_Error ("Z_ChangeTag: an owner is required for purgable blocks\n"
#ifdef INSTRUMENTED
             "Source: %s:%d"
             "\nSource of malloc: %s:%d"
             , file, line, block->file, block->line
#endif
            );
#endif

  if (block->zoneid != ZONEID)
    I_Error ("Z_ChangeTag: freed a pointer without ZONEID"
#ifdef INSTRUMENTED
             "\nSource: %s:%d"
             "\nSource of malloc: %s:%d"
             , file, line, block->file, block->line
#endif
            );

  if (block == block->next)
    blockbytag[block->tag] = NULL;
  else
    if (blockbytag[block->tag] == block)
      blockbytag[block->tag] = block->next;
  block->prev->next = block->next;
  block->next->prev = block->prev;

  if (!blockbytag[tag])
  {
    blockbytag[tag] = block;
    block->next = block->prev = block;
  }
  else
  {
    blockbytag[tag]->prev->next = block;
    block->prev = blockbytag[tag]->prev;
    block->next = blockbytag[tag];
    blockbytag[tag]->prev = block;
  }

#ifdef INSTRUMENTED
  if (block->tag < PU_PURGELEVEL && tag >= PU_PURGELEVEL)
  {
    active_memory -= block->size;
    purgable_memory += block->size;
  }
  else
    if (block->tag >= PU_PURGELEVEL && tag < PU_PURGELEVEL)
    {
      active_memory += block->size;
      purgable_memory -= block->size;
    }
#endif

#ifdef DOOMMEM
  zone_bytes[block->tag] -= block->size;
  zone_blocks[block->tag]--;
  zone_bytes[tag] += block->size;
  zone_blocks[tag]++;
#endif

  block->tag = tag;
}

void *(Z_Realloc)(void *ptr, size_t n, int tag, void **user DA(const char *file, int line))
{
  void *p = (Z_Malloc)(n, tag, user DA(file, line));
  if (ptr)
    {
      memblock_t *block = (memblock_t *)((char *) ptr - HEADER_SIZE);
      memcpy(p, ptr, n <= block->size ? n : block->size);
      (Z_Free)(ptr DA(file, line));
      if (user) // in case Z_Free nullified same user
        *user=p;
    }
#ifdef DOOMMEM
  zone_resite(p, __builtin_return_address(0));
#endif
  return p;
}

void *(Z_Calloc)(size_t n1, size_t n2, int tag, void **user DA(const char *file, int line))
{
  void *p = (n1*=n2) ? memset((Z_Malloc)(n1, tag, user DA(file, line)), 0, n1) : NULL;
#ifdef DOOMMEM
  zone_resite(p, __builtin_return_address(0));
#endif
  return p;
}

char *(Z_Strdup)(const char *s, int tag, void **user DA(const char *file, int line))
{
  size_t len = strlen(s) + 1;
  char *p = memcpy((Z_Malloc)(len, tag, user DA(file, line)), s, len);
#ifdef DOOMMEM
  zone_resite(p, __builtin_return_address(0));
#endif
  return p;
}

void (Z_CheckHeap)(DAC(const char *file, int line))
{
#if 0
  memblock_t *block;   // Start at base of zone mem
  if (block)
  do {                        // Consistency check (last node treated special)
    if ((block->next != zone &&
         (memblock_t *)((char *) block+HEADER_SIZE+block->size) != block->next)
        || block->next->prev != block || block->prev->next != block)
      I_Error("Z_CheckHeap: Block size does not touch the next block\n"
#ifdef INSTRUMENTED
              "Source: %s:%d"
              "\nSource of offending block: %s:%d"
              , file, line, block->file, block->line
#endif
              );
//#ifdef INSTRUMENTED
// shouldn't be needed anymore, was just for testing
#if 0
    if (((int)block->file < 0x00001000) && (block->file != NULL) && (block->tag != 0)) {
      block->file = NULL;
    }
#endif
  } while ((block=block->next) != zone);
#endif
}
