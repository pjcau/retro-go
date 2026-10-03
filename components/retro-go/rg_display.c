#include "rg_system.h"
#include "rg_line_hash.h"
#include "rg_scale_line.h"
#include "rg_display.h"

#include <stdlib.h>
#include <string.h>

#define LCD_BUFFER_LENGTH (RG_SCREEN_WIDTH * 4) // In pixels
#define RG_TASK_MSG_BAND 2 // msg.dataPtr is an rg_band_t (rg_display_submit_band)
#define BAND_QUEUE 36      // bands in flight between the emulator and the display task (mame-go: 4 internal + 32 PSRAM buffers)

// static rg_display_driver_t driver;
static rg_task_t *display_task_queue;
static rg_display_counters_t counters;
static rg_display_config_t config;
static rg_surface_t *border;
static rg_display_t display;
static int16_t map_viewport_to_source_x[RG_SCREEN_WIDTH + 1];
static int16_t map_viewport_to_source_y[RG_SCREEN_HEIGHT + 1];
static uint32_t screen_line_checksum[RG_SCREEN_HEIGHT + 1];
// D2: how many viewport pixels each source pixel is drawn as (rg_scale_line.h);
// source_x_count = 0 when the source is too wide for the table (map loop then)
static uint8_t source_x_repeat[RG_SCREEN_WIDTH * 2 + 2];
static int source_x_count;
static bool source_x_simple;    // every source pixel is drawn once or twice: rg_scale_line_pal12

#define LINE_IS_REPEATED(Y) (map_viewport_to_source_y[(Y)] == map_viewport_to_source_y[(Y) - 1])
// This is to avoid flooring a number that is approximated to .9999999 and be explicit about it
#define FLOAT_TO_INT(x) ((int)((x) + 0.1f))

static const char *SETTING_BACKLIGHT = "DispBacklight";
static const char *SETTING_SCALING = "DispScaling";
static const char *SETTING_FILTER = "DispFilter";
static const char *SETTING_ROTATION = "DispRotation";
static const char *SETTING_BORDER = "DispBorder";
static const char *SETTING_CUSTOM_ZOOM = "DispCustomZoom";

static void lcd_init(void);
static void lcd_deinit(void);
static void lcd_sync(void);
static void lcd_set_rotation(int rotation);
static void lcd_set_backlight(float percent);
static void lcd_set_window(int left, int top, int width, int height);
static inline uint16_t *lcd_get_buffer(size_t length);
static inline void lcd_send_buffer(uint16_t *buffer, size_t length);

#if RG_SCREEN_DRIVER == 0 || RG_SCREEN_DRIVER == 1 /* ILI9341/ST7789 */
#include "drivers/display/ili9341.h"
#elif RG_SCREEN_DRIVER == 2 /* ST7796S i80 parallel */
#include "drivers/display/st7796s_i80.h"
#elif RG_SCREEN_DRIVER == 99
#include "drivers/display/sdl2.h"
#else
#include "drivers/display/dummy.h"
#endif

static int draw_on_screen_display(int region_start, int region_end)
{
    static unsigned int area_dirty = 0;
    rg_margins_t margins = rg_gui_get_safe_area();
    int left = display.screen.width - margins.right - 28;
    int top = margins.top + 4;
    int border = 3;
    int width = 20;
    int height = 14;

    if (region_end < top + height)
        return top + height;

    // Low battery indicator
    if (rg_system_get_indicator(RG_INDICATOR_POWER_LOW) && ((counters.totalFrames / 20) & 1))
    {
        rg_display_clear_rect(left, top, width, height, C_RED); // Main body
        rg_display_clear_rect(left + width, top + height / 4, border, height / 2, C_RED); // The tab
        rg_display_clear_rect(left + border, top + border, width - border * 2, height - border * 2, C_BLACK); // The fill
        // memset(&screen_line_checksum[top], 0, sizeof(uint32_t) * height);
        area_dirty |= (1 << RG_INDICATOR_POWER_LOW);
    }
    else if (area_dirty)
    {
        if (display.viewport.width < display.screen.width || display.viewport.height < display.screen.height)
            rg_display_clear_rect(left, top, width + border, height, C_BLACK);
        memset(&screen_line_checksum[top], 0, sizeof(uint32_t) * height);
        area_dirty = 0;
    }
    return 0;
}

#define blend_pixels rg_blend_pixels // rg_scale_line.h holds the copy the PC test proves

// The frame being written: what a band (V2) must find again on the next call.
// Only the display task touches it.
static struct
{
    int next_block;       // first block (see block_start) not yet written
    int window_top;       // lcd window continuation
    int osd_next_call;
    int lines_updated;
    int64_t time_start;
    // V2i: the bands accepted and not yet sent stay where they are (pend[]),
    // the stage in PSRAM takes a band only when the emulator is short of buffers
    uint8_t *stage;
    size_t stage_size;
    bool active;          // a band frame has begun and is not sent yet
    uint32_t id;          // the frame being written; bands carry their frame's id
    uint32_t id_last;     // the id of the latest band accepted
    int avail_rows;       // source rows [0, avail_rows) of frame `id` are in pend (uncropped)
} frame;

typedef struct
{
    const uint8_t *rows;  // the band's pixels (its buffer, or the stage once staged)
    int first, last;      // source rows, uncropped
    void (*done)(void *);
    void *arg;
    uint32_t id;          // the frame it belongs to
    bool staged;
} pend_t;
static pend_t pend[64];
static int pend_n;
volatile int rg_display_band_starving;   // set by the emulator when it waits for a band buffer

// The blocks of lines a frame is written in (one lcd buffer each), fixed by the
// viewport: block k is viewport lines [block_start[k], block_start[k+1]). Bands
// (V2) write whole blocks, so a band edge never changes which lines a block
// holds, and the vertical filter blends exactly what it blends on a whole frame.
static uint16_t block_start[RG_SCREEN_HEIGHT + 2];
static int block_count;

static void compute_blocks(void)
{
    int draw_width = display.viewport.width, draw_height = display.viewport.height;
    if (display.viewport.left < 0)
        draw_width += display.viewport.left * 2;
    if (display.viewport.top < 0)
        draw_height += display.viewport.top * 2;
    int lines_per_buffer = draw_width > 0 ? LCD_BUFFER_LENGTH / draw_width : 1;
    int y = 0, n = 0;
    while (y < draw_height && n < RG_SCREEN_HEIGHT + 1)
    {
        int lines_to_copy = RG_MIN(lines_per_buffer, draw_height - y);
        // The vertical filter requires a block to start and end with unscaled lines
        if (display.viewport.filter_y)
        {
            while (lines_to_copy > 1 && (LINE_IS_REPEATED(y + lines_to_copy - 1) ||
                                         LINE_IS_REPEATED(y + lines_to_copy)))
                --lines_to_copy;
        }
        if (lines_to_copy < 1)
            break;
        block_start[n++] = y;
        y += lines_to_copy;
    }
    block_start[n] = y;
    block_count = n;
}

// The pixels of source row r (counted from the cropped top): the pending band
// of the frame being written that holds it, else rows + (r - row_base) * stride
static inline const void *source_row(const void *rows, int row_base, int stride, int r)
{
    if (pend_n)
    {
        int ru = r + (display.viewport.top < 0 ? -display.viewport.top * display.viewport.step_y : 0);
        for (int i = 0; i < pend_n; i++)
            if (pend[i].id == frame.id && ru >= pend[i].first && ru <= pend[i].last)
                return pend[i].rows + (ru - pend[i].first) * stride;
    }
    return rows + (r - row_base) * stride;
}

// Writes blocks [block_from, block_to) of `update`.
static inline void write_lines(const rg_surface_t *update, const void *rows, int row_base, int block_from, int block_to)
{
    bool filter_x = display.viewport.filter_x;
    bool filter_y = display.viewport.filter_y;
    int draw_left = display.viewport.left;
    int draw_top = display.viewport.top;
    int draw_width = display.viewport.width;
    int draw_height = display.viewport.height;

    int crop_left = 0;

    if (draw_left < 0)
    {
        crop_left += -draw_left * display.viewport.step_x;
        draw_width += draw_left * 2;
        draw_left = 0;
    }

    if (draw_top < 0)
        draw_height += draw_top * 2;

    const int format = update->format;
    const int stride = update->stride;
    const uint16_t *palette = update->palette;
    const size_t crop_bytes = crop_left * RG_PIXEL_GET_SIZE(format);

    const bool partial_update = RG_SCREEN_PARTIAL_UPDATES;

    // esp32-emu-turbo: exact 2x horizontal (e.g. 240 -> 480), unfiltered RGB565:
    // each source pixel becomes two with one 32-bit store instead of a
    // per-pixel map lookup (the display task shares core 1 with emulators)
    const bool fast_2x = !filter_x && format == RG_PIXEL_565_LE && crop_left == 0 &&
                         draw_width == update->width * 2 && !(update->width & 1) && !((uintptr_t)rows & 1);

    int lines_per_buffer = LCD_BUFFER_LENGTH / draw_width;

    // D1 (Arcade 60 fps plan): the "did this line change" test hashes the source
    // line (the pixels this viewport line is scaled from) and, once per update,
    // the palette, instead of the rendered line: fewer bytes (304 pens against
    // 868 bytes of RGB565 on the Neo Geo), and a block whose lines are all
    // unchanged is not rendered at all. Same decision as before: identical
    // source and palette give an identical rendered line.
    const int src_pixel_size = RG_PIXEL_GET_SIZE(format);
    const int src_first = fast_2x ? 0 : map_viewport_to_source_x[0];
    const int src_last = fast_2x ? update->width - 1 : map_viewport_to_source_x[draw_width - 1];
    const size_t src_offset = src_first * src_pixel_size;
    const size_t src_line_bytes = (src_last - src_first + 1) * src_pixel_size;
    const uint32_t pal_hash = (format & RG_PIXEL_PALETTE) ? rg_line_hash(palette, 256 * sizeof(uint16_t), 0) : 0;
    // D2: the fused scaler covers the whole viewport width from source pixel 0 (no crop)
    const bool fused_scale = !fast_2x && source_x_count > 0 && crop_left == 0 &&
                             draw_width == display.viewport.width && (format & RG_PIXEL_FORMAT) != RG_PIXEL_888;

    #define SOURCE_ROW(Y) (source_row(rows, row_base, stride, map_viewport_to_source_y[Y]) + crop_bytes)

    (void)lines_per_buffer;
    for (int k = block_from; k < block_to;)
    {
        int y = block_start[k];
        int lines_to_copy = block_start[++k] - y;

        int64_t t_wait = rg_system_timer();
        uint16_t *line_buffer = lcd_get_buffer(LCD_BUFFER_LENGTH);
        counters.dmaWaitTime += rg_system_timer() - t_wait;
        uint16_t *line_buffer_ptr = line_buffer;

        bool need_update = !partial_update;

        if (partial_update)
        {
            uint32_t checksum = 0xFFFFFFFF;
            for (int i = 0; i < lines_to_copy; ++i)
            {
                if (!(i > 0 && LINE_IS_REPEATED(y + i))) // a repeated line carries the previous hash
                    checksum = rg_line_hash(SOURCE_ROW(y + i) + src_offset, src_line_bytes, pal_hash);
                if (screen_line_checksum[draw_top + y + i] != checksum)
                {
                    screen_line_checksum[draw_top + y + i] = checksum;
                    need_update = true;
                }
            }
        }

        if (!need_update)
            y += lines_to_copy;
        else
        for (int i = 0; i < lines_to_copy; ++i)
        {
            if (i > 0 && LINE_IS_REPEATED(y))
            {
                memcpy(line_buffer_ptr, line_buffer_ptr - draw_width, draw_width * 2);
                line_buffer_ptr += draw_width;
            }
            else
            {
                const void *src = SOURCE_ROW(y);
                #define RENDER_LINE(PTR_TYPE, PIXEL) { \
                    const PTR_TYPE *buffer = (const PTR_TYPE *)src; \
                    for (int xx = 0; xx < draw_width; ++xx) { \
                        int x = map_viewport_to_source_x[xx]; \
                        *line_buffer_ptr++ = (PIXEL); \
                    } \
                }
                if (fast_2x)
                {
                    const uint16_t *src16 = (const uint16_t *)src;
                    uint32_t *dst = (uint32_t *)line_buffer_ptr;
                    for (int x = 0; x < draw_width / 2; x += 2)
                    {
                        uint32_t a = src16[x], b = src16[x + 1];
                        a = ((a << 8) | (a >> 8)) & 0xFFFF;
                        b = ((b << 8) | (b >> 8)) & 0xFFFF;
                        dst[x] = a | (a << 16);
                        dst[x + 1] = b | (b << 16);
                    }
                    line_buffer_ptr += draw_width;
                }
                else if (fused_scale)
                {
                    // D2: pattern-driven scaling with the horizontal filter fused in
                    if ((format & RG_PIXEL_PALETTE) && source_x_simple)
                        rg_scale_line_pal12(src, palette, source_x_repeat, source_x_count, line_buffer_ptr, filter_x);
                    else if (format & RG_PIXEL_PALETTE)
                        rg_scale_line_pal(src, palette, source_x_repeat, source_x_count, line_buffer_ptr, draw_width, filter_x);
                    else if (format == RG_PIXEL_565_LE)
                        rg_scale_line_565le(src, source_x_repeat, source_x_count, line_buffer_ptr, draw_width, filter_x);
                    else
                        rg_scale_line_565be(src, source_x_repeat, source_x_count, line_buffer_ptr, draw_width, filter_x);
                    line_buffer_ptr += draw_width;
                }
                else if (format & RG_PIXEL_PALETTE)
                    RENDER_LINE(uint8_t, palette[buffer[x]])
                else if (format == RG_PIXEL_565_LE)
                    RENDER_LINE(uint16_t, (buffer[x] << 8) | (buffer[x] >> 8))
                else
                    RENDER_LINE(uint16_t, buffer[x])
            }

            ++y;
        }

        if (filter_x && need_update && !fused_scale) // the fused scaler has filtered already
        {
            for (int i = 0; i < lines_to_copy; ++i)
            {
                uint16_t *buffer = line_buffer + i * draw_width;
                for (int x = 1; x < draw_width - 1; ++x)
                {
                    if (map_viewport_to_source_x[x] == map_viewport_to_source_x[x - 1])
                    {
                        buffer[x] = blend_pixels(buffer[x - 1], buffer[x + 1]);
                    }
                }
            }
        }

        if (filter_y && need_update)
        {
            int top = y - lines_to_copy;
            for (int i = 1; i < lines_to_copy - 1; ++i)
            {
                if (LINE_IS_REPEATED(top + i))
                {
                    uint16_t *lineA = line_buffer + (i - 1) * draw_width;
                    uint16_t *lineB = line_buffer + (i + 0) * draw_width;
                    uint16_t *lineC = line_buffer + (i + 1) * draw_width;
                    rg_blend_line(lineB, lineA, lineC, draw_width);
                }
            }
        }

        if (need_update)
        {
            int left = display.screen.margins.left + draw_left;
            int top = display.screen.margins.top + draw_top + y - lines_to_copy;
            if (top != frame.window_top)
                lcd_set_window(left, top, draw_width, draw_height - (y - lines_to_copy));
            lcd_send_buffer(line_buffer, draw_width * lines_to_copy);
            counters.sendCount++;
            frame.window_top = top + lines_to_copy;
            frame.lines_updated += lines_to_copy;
        }
        else
        {
            // Return unused buffer
            lcd_send_buffer(line_buffer, 0);
        }

        // Drawing the OSD as we progress reduces flicker compared to doing it once at the end
        if (frame.osd_next_call && draw_top + y >= frame.osd_next_call)
        {
            frame.osd_next_call = draw_on_screen_display(0, draw_top + y);
            frame.window_top = -1;
        }
    }
    #undef SOURCE_ROW
    frame.next_block = block_to;
}

static inline void frame_begin(void)
{
    frame.active = true;
    frame.next_block = 0;
    frame.window_top = -1;
    frame.osd_next_call = 20;
    frame.lines_updated = 0;
    frame.time_start = rg_system_timer();
}

static inline void frame_end(void)
{
    frame.active = false;
    int draw_height = display.viewport.height;
    if (display.viewport.top < 0)
        draw_height += display.viewport.top * 2;
    if (frame.lines_updated > draw_height * 0.80f)
        counters.fullFrames++;
    else
        counters.partFrames++;
    counters.busyTime += rg_system_timer() - frame.time_start;
}

static inline void write_update(const rg_surface_t *update)
{
    int crop_top = display.viewport.top < 0 ? -display.viewport.top * display.viewport.step_y : 0;
    int draw_height = display.viewport.height;
    if (display.viewport.top < 0)
        draw_height += display.viewport.top * 2;
    (void)draw_height;
    frame_begin();
    write_lines(update, update->data + update->offset + crop_top * update->stride, 0, 0, block_count);
    frame_end();
}

// V2i of the Arcade 60 fps plan: the frame arrives as bands of source rows
// while the emulator draws the next ones. Accepted bands wait in pend[] where
// they are (their own buffer, internal RAM or the emulator's PSRAM reserve)
// and are scaled from there; a band's buffer goes back as soon as its rows are
// sent. Only when the emulator says it is short of buffers
// (rg_display_band_starving) is the oldest internal band copied to the PSRAM
// stage and released early. The next frame's bands are accepted while this
// frame's tail is still being sent; the frame switch happens when the tail is
// done. The blocks are the whole frame's, in order.
static inline int crop_top_rows(void)
{
    return display.viewport.top < 0 ? -display.viewport.top * display.viewport.step_y : 0;
}

// the first source row (uncropped) the frame being written still needs
static inline int need_row(void)
{
    return frame.next_block < block_count ? map_viewport_to_source_y[block_start[frame.next_block]] + crop_top_rows() : RG_SCREEN_HEIGHT * 4;
}

static void pend_remove(int i)
{
    if (pend[i].done)
        pend[i].done(pend[i].arg);
    memmove(&pend[i], &pend[i + 1], (pend_n - i - 1) * sizeof(pend_t));
    pend_n--;
}

// release the bands of the frame being written whose rows are all sent
static void pend_release_sent(void)
{
    int need = need_row();
    for (int i = 0; i < pend_n;)
        if (pend[i].id == frame.id && pend[i].last < need)
            pend_remove(i);
        else
            i++;
}

// the emulator is short of band buffers: move the oldest unsent internal band
// to the stage and give its buffer back
static void stage_oldest(const rg_surface_t *update)
{
    const int stride = update->stride;
    int need = need_row();
    for (int i = 0; i < pend_n; i++)
    {
        pend_t *b = &pend[i];
        if (b->staged)
            continue;
        if (b->id != frame.id && b->last >= need)
            return;                          // a next-frame band over rows this frame still needs: wait instead
        int from = b->id == frame.id && need > b->first ? need : b->first;
        if (from <= b->last)
            memcpy(frame.stage + (size_t)from * stride, b->rows + (size_t)(from - b->first) * stride,
                   (size_t)(b->last - from + 1) * stride);
        if (b->done)
            b->done(b->arg);
        b->done = NULL;
        b->rows = frame.stage + (size_t)b->first * stride;
        b->staged = true;
        rg_display_band_starving = 0;
        return;
    }
    rg_display_band_starving = 0;           // nothing to stage: everything is already in PSRAM
}

// Scale and send the blocks whose rows are here, releasing bands as they go;
// stop when another band waits in the queue (unless `drain`); at the end of
// the frame, the next frame takes over if its bands have started arriving.
static void write_available(const rg_surface_t *update, bool drain)
{
    const uint8_t *stage_base = frame.stage + (size_t)crop_top_rows() * update->stride;
    for (;;)
    {
        while (frame.next_block < block_count)
        {
            int k = frame.next_block;
            int last_row = map_viewport_to_source_y[block_start[k + 1] - 1] + crop_top_rows();
            if (last_row >= frame.avail_rows)
                return;
            if (rg_display_band_starving)
                stage_oldest(update);
            if (!drain && rg_task_messages_waiting(display_task_queue))
                return;
            write_lines(update, stage_base, 0, k, k + 1);
            pend_release_sent();
        }
        // the frame is done
        for (int i = 0; i < pend_n;)
            if (pend[i].id == frame.id) pend_remove(i); else i++;
        frame_end();
        if (frame.id == frame.id_last)
        {
            frame.avail_rows = 0;
            return;
        }
        frame_begin();                       // the next frame's bands have started arriving
        frame.id++;
        frame.avail_rows = 0;
        for (int i = 0; i < pend_n; i++)
            if (pend[i].id == frame.id && pend[i].last + 1 > frame.avail_rows)
                frame.avail_rows = pend[i].last + 1;
    }
}

static void accept_band(const rg_band_t *band)
{
    const rg_surface_t *update = band->frame;
    size_t need = (size_t)update->stride * update->height;

    if (frame.stage_size < need)
    {
        free(frame.stage);
        frame.stage = malloc(need);
        frame.stage_size = frame.stage ? need : 0;
        if (!frame.stage)
            RG_PANIC("band stage");
    }
    if (band->first == 0)
    {
        if (frame.active && frame.id_last != frame.id)
            write_available(update, true);   // two frames behind: finish the old one first
        frame.id_last++;
        if (!frame.active)
        {
            frame_begin();                   // nothing in flight: this band starts the frame now
            frame.id = frame.id_last;
        }
    }
    if (pend_n >= (int)(sizeof(pend) / sizeof(*pend)))
        write_available(update, true);       // cannot happen with the emulator's buffer count; be safe
    pend_t *b = &pend[pend_n++];
    b->rows = band->rows;
    b->first = band->first;
    b->last = band->first + band->count - 1;
    b->done = band->done;
    b->arg = band->arg;
    b->id = frame.id_last;
    b->staged = false;
    if (b->id == frame.id && b->last + 1 > frame.avail_rows)
        frame.avail_rows = b->last + 1;
}

static void update_viewport_scaling(void)
{
    int screen_width = display.screen.width;
    int screen_height = display.screen.height;
    int src_width = display.source.width;
    int src_height = display.source.height;
    int new_width = src_width;
    int new_height = src_height;

    if (config.scaling == RG_DISPLAY_SCALING_FULL)
    {
        new_width = screen_width;
        new_height = screen_height;
    }
    else if (config.scaling == RG_DISPLAY_SCALING_FIT)
    {
        new_width = FLOAT_TO_INT(screen_height * ((float)src_width / src_height));
        new_height = screen_height;
        if (new_width > screen_width) {
            new_width = screen_width;
            new_height = FLOAT_TO_INT(screen_width * ((float)src_height / src_width));
        }
    }
    else if (config.scaling == RG_DISPLAY_SCALING_ZOOM)
    {
        new_width = FLOAT_TO_INT(src_width * config.custom_zoom);
        new_height = FLOAT_TO_INT(src_height * config.custom_zoom);
    }

    // Everything works better when we use even dimensions!
    new_width &= ~1;
    new_height &= ~1;

    display.viewport.left = (screen_width - new_width) / 2;
    display.viewport.top = (screen_height - new_height) / 2;
    display.viewport.width = new_width;
    display.viewport.height = new_height;

    display.viewport.step_x = (float)src_width / display.viewport.width;
    display.viewport.step_y = (float)src_height / display.viewport.height;

    display.viewport.filter_x = (config.filter == RG_DISPLAY_FILTER_HORIZ || config.filter == RG_DISPLAY_FILTER_BOTH) &&
                                (config.scaling && (display.viewport.width % src_width) != 0);
    display.viewport.filter_y = (config.filter == RG_DISPLAY_FILTER_VERT || config.filter == RG_DISPLAY_FILTER_BOTH) &&
                                (config.scaling && (display.viewport.height % src_height) != 0);

    memset(screen_line_checksum, 0, sizeof(screen_line_checksum));

    for (int x = 0; x < screen_width; ++x)
        map_viewport_to_source_x[x] = FLOAT_TO_INT(x * display.viewport.step_x);
    // D2: the horizontal repeat pattern, covering the map's last source index
    // (one past the source width for some sizes: the map loop always read it)
    source_x_count = map_viewport_to_source_x[display.viewport.width - 1] + 1;
    if (source_x_count > (int)sizeof(source_x_repeat))
        source_x_count = 0;
    else
    {
        memset(source_x_repeat, 0, source_x_count);
        for (int x = 0; x < display.viewport.width; x++)
            source_x_repeat[map_viewport_to_source_x[x]]++;
    }
    source_x_simple = source_x_count > 0;
    for (int i = 0; i < source_x_count; i++)
        if (source_x_repeat[i] != 1 && source_x_repeat[i] != 2)
            source_x_simple = false;
    for (int y = 0; y < screen_height; ++y)
        map_viewport_to_source_y[y] = FLOAT_TO_INT(y * display.viewport.step_y);
    compute_blocks();

    RG_LOGI("%dx%d@%.3f => %dx%d@%.3f left:%d top:%d step_x:%.2f step_y:%.2f", src_width, src_height,
            (float)src_width / src_height, new_width, new_height, (float)new_width / new_height,
            display.viewport.left, display.viewport.top, display.viewport.step_x, display.viewport.step_y);
}

static bool load_border_file(const char *filename)
{
    RG_LOGI("Loading border file: %s", filename ?: "(none)");

    free(border), border = NULL;
    display.changed = true;

    if (filename && (border = rg_surface_load_image_file(filename, 0)))
    {
        if (border->width != rg_display_get_width() || border->height != rg_display_get_height())
        {
            rg_surface_t *resized = rg_surface_resize(border, rg_display_get_width(), rg_display_get_height());
            if (resized)
            {
                rg_surface_free(border);
                border = resized;
            }
        }
        return true;
    }
    return false;
}

IRAM_ATTR
static void display_task(void *arg)
{
    rg_task_msg_t msg;

    while (rg_task_peek(&msg))
    {
        // Received a shutdown request!
        if (msg.type == RG_TASK_MSG_STOP)
            break;

        if (display.changed)
        {
            while (pend_n)
                pend_remove(0);
            frame.next_block = block_count;
            frame.avail_rows = 0;
            frame.active = false;
            frame.id = frame.id_last;
            update_viewport_scaling();
            // Clear the screen if the viewport doesn't cover the entire screen because garbage could remain on the sides
            if (display.viewport.width < display.screen.width || display.viewport.height < display.screen.height)
            {
                if (border)
                    rg_display_write_rect(0, 0, border->width, border->height, 0, border->data, RG_DISPLAY_WRITE_NOSYNC);
                else
                    rg_display_clear_except(display.viewport.left, display.viewport.top, display.viewport.width, display.viewport.height, C_BLACK);
            }
            display.changed = false;
        }

        if (msg.type == RG_TASK_MSG_BAND)
        {
            const rg_band_t *band = msg.dataPtr;
            const rg_surface_t *update = band->frame;
            accept_band(band);
            rg_task_receive(&msg);      // the slot is free before the scaling starts
            write_available(update, false);
            if (rg_display_band_starving)
                stage_oldest(update);
        }
        else
        {
            write_update(msg.dataPtr);
            // draw_on_screen_display(0, display.screen.height);
            rg_task_receive(&msg);
        }

        lcd_sync();
    }
}

void rg_display_force_redraw(void)
{
    display.changed = true;
    // memset(screen_line_checksum, 0, sizeof(screen_line_checksum));
    rg_system_event(RG_EVENT_REDRAW, NULL);
    rg_display_sync(true);
}

const rg_display_t *rg_display_get_info(void)
{
    return &display;
}

rg_display_counters_t rg_display_get_counters(void)
{
    return counters;
}

int rg_display_get_width(void)
{
    // return display.screen.real_width - (display.screen.margins.left + display.screen.margins.right);
    return display.screen.width;
}

int rg_display_get_height(void)
{
    // return display.screen.real_height - (display.screen.margins.top + display.screen.margins.bottom);
    return display.screen.height;
}

void rg_display_set_scaling(display_scaling_t scaling)
{
    config.scaling = RG_MIN(RG_MAX(0, scaling), RG_DISPLAY_SCALING_COUNT - 1);
    rg_settings_set_number(NS_APP, SETTING_SCALING, config.scaling);
    display.changed = true;
}

display_scaling_t rg_display_get_scaling(void)
{
    return config.scaling;
}

void rg_display_set_custom_zoom(double factor)
{
    config.custom_zoom = RG_MIN(RG_MAX(0.1, factor), 2.0);
    rg_settings_set_number(NS_APP, SETTING_CUSTOM_ZOOM, config.custom_zoom);
    display.changed = true;
}

double rg_display_get_custom_zoom(void)
{
    return config.custom_zoom;
}

void rg_display_set_filter(display_filter_t filter)
{
    config.filter = RG_MIN(RG_MAX(0, filter), RG_DISPLAY_FILTER_COUNT - 1);
    rg_settings_set_number(NS_APP, SETTING_FILTER, config.filter);
    display.changed = true;
}

display_filter_t rg_display_get_filter(void)
{
    return config.filter;
}

void rg_display_set_rotation(display_rotation_t rotation)
{
    config.rotation = RG_MIN(RG_MAX(0, rotation), RG_DISPLAY_ROTATION_COUNT - 1);
    rg_settings_set_number(NS_APP, SETTING_SCALING, config.rotation);
    display.changed = true;
}

display_rotation_t rg_display_get_rotation(void)
{
    return config.rotation;
}

void rg_display_set_backlight(display_backlight_t percent)
{
    config.backlight = RG_MIN(RG_MAX(percent, RG_DISPLAY_BACKLIGHT_MIN), RG_DISPLAY_BACKLIGHT_MAX);
    rg_settings_set_number(NS_GLOBAL, SETTING_BACKLIGHT, config.backlight);
    lcd_set_backlight(config.backlight);
}

display_backlight_t rg_display_get_backlight(void)
{
    return config.backlight;
}

void rg_display_set_border(const char *filename)
{
    free(config.border_file);
    config.border_file = NULL;

    if (load_border_file(filename))
    {
        rg_settings_set_string(NS_APP, SETTING_BORDER, filename);
        config.border_file = strdup(filename);
    }
    else
    {
        rg_settings_set_string(NS_APP, SETTING_BORDER, NULL);
        config.border_file = NULL;
    }
    display.changed = true;
}

char *rg_display_get_border(void)
{
    return rg_settings_get_string(NS_APP, SETTING_BORDER, NULL);
}

// Bench switch (console "lcd off|on"): drop every frame before it reaches the
// panel while the emulation and audio keep running, to tell display-bus noise
// from anything else.
bool rg_display_frozen = false;

static inline bool display_busy(void);

void rg_display_submit(const rg_surface_t *update, uint32_t flags)
{
    const int64_t time_start = rg_system_timer();

    // Those things should probably be asserted, but this is a new system let's be forgiving...
    if (!update || !update->data)
        return;

    if (rg_display_frozen)
    {
        counters.totalFrames++;
        return;
    }

    if (display.source.width != update->width || display.source.height != update->height)
    {
        rg_display_sync(true);
        display.source.width = update->width;
        display.source.height = update->height;
        display.changed = true;
    }

    // one update at a time, as when the queue held one message: emulators
    // reuse their frame buffers on the strength of it
    while (display_busy())
        rg_task_yield();
    rg_task_send(display_task_queue, &(rg_task_msg_t){.dataPtr = update});

    counters.blockTime += rg_system_timer() - time_start;
    counters.totalFrames++;
}

void rg_display_submit_band(const rg_band_t *band)
{
    const int64_t time_start = rg_system_timer();
    const rg_surface_t *update = band->frame;

    RG_ASSERT_ARG(band && update && band->rows && band->count > 0);

    if (rg_display_frozen)
    {
        if (band->done)
            band->done(band->arg);
        if (band->first == 0)
            counters.totalFrames++;
        return;
    }

    if (display.source.width != update->width || display.source.height != update->height)
    {
        rg_display_sync(true);
        display.source.width = update->width;
        display.source.height = update->height;
        display.changed = true;
    }

    // up to BAND_QUEUE bands wait here; the display task takes them in order
    rg_task_send(display_task_queue, &(rg_task_msg_t){.type = RG_TASK_MSG_BAND, .dataPtr = band});

    counters.blockTime += rg_system_timer() - time_start;
    if (band->first == 0)
        counters.totalFrames++;
}

// The display task is busy while a message waits and, with bands (V2h), while
// a band frame is still being sent after its messages were taken: anyone
// opening an lcd window of their own meanwhile (rg_display_write_rect, the
// hourglass of a state load) would interleave two i80 streams and hang the
// panel. The emulator sends whole frames, so waiting for the frame to end
// cannot wait for the caller itself.
static inline bool display_busy(void)
{
    return rg_task_messages_waiting(display_task_queue) || pend_n > 0 || frame.active;
}

bool rg_display_sync(bool block)
{
    while (block && display_busy())
        rg_task_yield();
    return !display_busy();
}

void rg_display_write_rect(int left, int top, int width, int height, int stride, const uint16_t *buffer, uint32_t flags)
{
    RG_ASSERT_ARG(buffer);

    // calc stride before clipping width
    stride = RG_MAX(stride, width * 2);

    // Clipping
    width = RG_MIN(width, display.screen.width - left);
    height = RG_MIN(height, display.screen.height - top);

    // This can happen when left or top is out of bound
    if (width < 0 || height < 0)
        return;

    // This will work for now because we rarely draw from different threads (so all we need is ensure
    // that we're not interrupting a display update). But what we SHOULD be doing is acquire a lock
    // before every call to lcd_set_window and release it only after the last call to lcd_send_buffer.
    if (!(flags & RG_DISPLAY_WRITE_NOSYNC))
        rg_display_sync(true);

    // This isn't really necessary but it makes sense to invalidate
    // the lines we're about to overwrite...
    for (size_t y = 0; y < height; ++y)
        screen_line_checksum[top + y] = 0;

    lcd_set_window(left + display.screen.margins.left, top + display.screen.margins.top, width, height);

    for (size_t y = 0; y < height;)
    {
        uint16_t *lcd_buffer = lcd_get_buffer(LCD_BUFFER_LENGTH);
        size_t num_lines = RG_MIN(LCD_BUFFER_LENGTH / width, height - y);

        // Copy line by line because stride may not match width
        for (size_t line = 0; line < num_lines; ++line)
        {
            uint16_t *src = (void *)buffer + ((y + line) * stride);
            uint16_t *dst = lcd_buffer + (line * width);
            if (flags & RG_DISPLAY_WRITE_NOSWAP)
            {
                memcpy(dst, src, width * 2);
            }
            else
            {
                for (size_t i = 0; i < width; ++i)
                    dst[i] = (src[i] >> 8) | (src[i] << 8);
            }
        }

        lcd_send_buffer(lcd_buffer, width * num_lines);
        y += num_lines;
    }

    lcd_sync();
}

void rg_display_clear_rect(int left, int top, int width, int height, uint16_t color_le)
{
    const uint16_t color_be = (color_le << 8) | (color_le >> 8);
    int pixels_remaining = width * height;
    if (pixels_remaining > 0)
    {
        lcd_set_window(left + display.screen.margins.left, top + display.screen.margins.top, width, height);
        while (pixels_remaining > 0)
        {
            uint16_t *buffer = lcd_get_buffer(LCD_BUFFER_LENGTH);
            int pixels = RG_MIN(pixels_remaining, LCD_BUFFER_LENGTH);
            for (size_t j = 0; j < pixels; ++j)
                buffer[j] = color_be;
            lcd_send_buffer(buffer, pixels);
            pixels_remaining -= pixels;
        }
    }
}

void rg_display_clear_except(int left, int top, int width, int height, uint16_t color_le)
{
    // Clear everything except the specified area
    // FIXME: Do not ignore left/top...
    int left_offset = -display.screen.margins.left;
    int top_offset = -display.screen.margins.top;
    int horiz = (display.screen.real_width - width + 1) / 2;
    int vert = (display.screen.real_height - height + 1) / 2;
    rg_display_clear_rect(left_offset, top_offset, horiz, display.screen.real_height, color_le); // Left
    rg_display_clear_rect(left_offset + horiz + width, top_offset, horiz, display.screen.real_height, color_le); // Right
    rg_display_clear_rect(left_offset + horiz, top_offset, display.screen.real_width - horiz * 2, vert, color_le); // Top
    rg_display_clear_rect(left_offset + horiz, top_offset + vert + height, display.screen.real_width - horiz * 2, vert, color_le); // Bottom
}

void rg_display_clear(uint16_t color_le)
{
    // Same rule as rg_display_write_rect: never open a window while the display task is
    // still streaming a frame (shutdown_cleanup clears the screen right after a submit;
    // interleaving the two i80 streams hung the panel driver on the first article).
    // Not in rg_display_clear_rect: the display task itself calls that one for the OSD.
    if (display_task_queue) // rg_display_init clears before the task exists
        rg_display_sync(true);
    // We ignore margins here, we want to fill the entire screen
    rg_display_clear_rect(-display.screen.margins.left, -display.screen.margins.top, display.screen.real_width,
                          display.screen.real_height, color_le);
}

void rg_display_deinit(void)
{
    rg_task_send(display_task_queue, &(rg_task_msg_t){.type = RG_TASK_MSG_STOP});
    // lcd_set_backlight(0);
    lcd_deinit();
    RG_LOGI("Display terminated.\n");
}

void rg_display_init(void)
{
    RG_LOGI("Initialization...\n");
    // TO DO: We probably should call the setters to ensure valid values...
    config = (rg_display_config_t){
        .backlight = rg_settings_get_number(NS_GLOBAL, SETTING_BACKLIGHT, 80),
        .scaling = rg_settings_get_number(NS_APP, SETTING_SCALING, RG_DISPLAY_SCALING_FIT),
        .filter = rg_settings_get_number(NS_APP, SETTING_FILTER, RG_DISPLAY_FILTER_BOTH),
        .rotation = rg_settings_get_number(NS_APP, SETTING_ROTATION, RG_DISPLAY_ROTATION_AUTO),
        .border_file = rg_settings_get_string(NS_APP, SETTING_BORDER, NULL),
        .custom_zoom = rg_settings_get_number(NS_APP, SETTING_CUSTOM_ZOOM, 1.0),
    };
    display = (rg_display_t){
        .screen.real_width = RG_SCREEN_WIDTH,
        .screen.real_height = RG_SCREEN_HEIGHT,
        .screen.width = RG_SCREEN_WIDTH,
        .screen.height = RG_SCREEN_HEIGHT,
        .screen.margins = RG_SCREEN_VISIBLE_AREA,
        .changed = true,
    };
    display.screen.width -= display.screen.margins.left + display.screen.margins.right;
    display.screen.height -= display.screen.margins.top + display.screen.margins.bottom;
    lcd_init();
    rg_display_clear(C_BLACK);
    rg_task_delay(80); // Wait for the screen be cleared before turning on the backlight (40ms doesn't seem to be enough...)
    lcd_set_backlight(config.backlight);
    // V2h: bands queue up to BAND_QUEUE deep; whole-frame updates keep the old
    // one-at-a-time behaviour (rg_display_submit waits for an empty queue first)
    display_task_queue = rg_task_create_ex("rg_display", &display_task, NULL, 4 * 1024, RG_TASK_PRIORITY_6, 1, BAND_QUEUE);
    if (config.border_file)
        load_border_file(config.border_file);
    RG_LOGI("Display ready.\n");
}
