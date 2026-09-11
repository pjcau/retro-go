/*
 * ST7796S display driver — 8-bit 8080 parallel (i80 bus)
 * For ESP32 Emu Turbo: ILI9488 driven landscape 480x320, RGB565, 20MHz write clock.
 *
 * Uses ESP-IDF esp_lcd_panel_io_i80 with async DMA and double-buffering.
 * Requires config.h defines: RG_GPIO_LCD_D0..D7, CS, DC, WR, RD, RST, BCKL,
 *                             RG_LCD_I80_CLK_HZ
 */

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_lcd_panel_io.h>
#include <driver/gpio.h>
#include <driver/ledc.h>

static esp_lcd_panel_io_handle_t lcd_io;
static esp_lcd_i80_bus_handle_t lcd_bus;

static QueueHandle_t free_bufs;
static QueueHandle_t pending_bufs;
static bool window_fresh; // no pixel data sent since the last set_window

#define I80_BUF_COUNT   5
#define I80_BUF_LENGTH  (LCD_BUFFER_LENGTH * 2)  /* bytes (LCD_BUFFER_LENGTH is pixels) */

/* ── DMA completion callback (ISR context) ────────────────────────── */

IRAM_ATTR
static bool on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *user_ctx)
{
    uint16_t *buf;
    BaseType_t woken = pdFALSE;
    if (xQueueReceiveFromISR(pending_bufs, &buf, &woken) == pdTRUE)
        xQueueSendFromISR(free_bufs, &buf, &woken);
    return woken == pdTRUE;
}

/* ── Command helper ───────────────────────────────────────────────── */

static inline void st7796_cmd(uint16_t cmd, const void *params, size_t len)
{
    esp_lcd_panel_io_tx_param(lcd_io, cmd, params, len);
}

/* ── Driver interface (called by rg_display.c) ────────────────────── */

static void lcd_set_backlight(float percent)
{
    float level = percent / 100.f;
    if (level < 0.f) level = 0.f;
    if (level > 1.f) level = 1.f;
#ifdef RG_GPIO_LCD_BCKL
    ledc_set_fade_time_and_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
                                 (int)(0xFF * level), 50, 0);
#endif
}

static void lcd_set_window(int left, int top, int width, int height)
{
    int right  = left + width  - 1;
    int bottom = top  + height - 1;

    const uint8_t caset[] = {left >> 8, left & 0xFF, right >> 8, right & 0xFF};
    const uint8_t raset[] = {top >> 8,  top & 0xFF,  bottom >> 8, bottom & 0xFF};

    st7796_cmd(0x2A, caset, sizeof(caset));  /* CASET */
    st7796_cmd(0x2B, raset, sizeof(raset));  /* RASET */
    window_fresh = true;
}

static inline uint16_t *lcd_get_buffer(size_t length)
{
    uint16_t *buf;
    if (xQueueReceive(free_bufs, &buf, pdMS_TO_TICKS(2500)) != pdTRUE)
        RG_PANIC("display");
    return buf;
}

static inline void lcd_send_buffer(uint16_t *buffer, size_t length)
{
    if (length > 0)
    {
        /* rg_display streams one window as several DMA chunks and the i80
         * IO layer sends a command with every chunk. 0x2C (Memory Write)
         * resets the write pointer to the window origin, so repeating it
         * would overwrite the first rows with every chunk and leave the
         * rest of the window untouched (seen on the first article: half
         * glyphs, stale rows). Only the first chunk after set_window may
         * use 0x2C; the rest continue with 0x3C (Memory Write Continue). */
        int cmd = window_fresh ? 0x2C : 0x3C;
        window_fresh = false;
        xQueueSend(pending_bufs, &buffer, portMAX_DELAY);
        esp_lcd_panel_io_tx_color(lcd_io, cmd, buffer, length * sizeof(uint16_t));
    }
    else
    {
        /* Buffer unused — return to pool */
        xQueueSend(free_bufs, &buffer, portMAX_DELAY);
    }
}

static void lcd_sync(void)
{
    /* Wait until all DMA transfers finish (all buffers returned to pool) */
    int retries = 2500;
    while (uxQueueMessagesWaiting(pending_bufs) > 0 && retries-- > 0)
        vTaskDelay(1);
}

static void lcd_set_rotation(int rotation)
{
    /* Fixed portrait orientation for ESP32 Emu Turbo */
    (void)rotation;
}

static void lcd_init(void)
{
    /* ── Backlight: start OFF to avoid flash during init ── */
#ifdef RG_GPIO_LCD_BCKL
    ledc_timer_config(&(ledc_timer_config_t){
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = 5000,
    });
    ledc_channel_config(&(ledc_channel_config_t){
        .gpio_num   = RG_GPIO_LCD_BCKL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 0,
    });
    ledc_fade_func_install(0);
#endif

    /* ── Buffer pools ── */
    free_bufs    = xQueueCreate(I80_BUF_COUNT, sizeof(uint16_t *));
    pending_bufs = xQueueCreate(I80_BUF_COUNT, sizeof(uint16_t *));

    for (int i = 0; i < I80_BUF_COUNT; i++)
    {
        void *buf = rg_alloc(I80_BUF_LENGTH, MEM_DMA);
        xQueueSend(free_bufs, &buf, portMAX_DELAY);
    }

    /* ── I80 bus: 8-bit data on GPIO4-11 ── */
    esp_lcd_i80_bus_config_t bus_cfg = {
        .clk_src    = LCD_CLK_SRC_DEFAULT,
        .dc_gpio_num = RG_GPIO_LCD_DC,
        .wr_gpio_num = RG_GPIO_LCD_WR,
        .data_gpio_nums = {
            RG_GPIO_LCD_D0, RG_GPIO_LCD_D1, RG_GPIO_LCD_D2, RG_GPIO_LCD_D3,
            RG_GPIO_LCD_D4, RG_GPIO_LCD_D5, RG_GPIO_LCD_D6, RG_GPIO_LCD_D7,
        },
        .bus_width          = 8,
        .max_transfer_bytes = RG_SCREEN_WIDTH * RG_SCREEN_HEIGHT * 2,
        .psram_trans_align  = 64,
        .sram_trans_align   = 4,
    };
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_cfg, &lcd_bus));

    /* ── Panel IO on the bus ── */
    esp_lcd_panel_io_i80_config_t io_cfg = {
        .cs_gpio_num        = RG_GPIO_LCD_CS,
        .pclk_hz            = RG_LCD_I80_CLK_HZ,
        .trans_queue_depth   = I80_BUF_COUNT,
        .dc_levels = {
            .dc_idle_level  = 0,
            .dc_cmd_level   = 0,
            .dc_dummy_level = 0,
            .dc_data_level  = 1,
        },
        .lcd_cmd_bits       = 8,
        .lcd_param_bits     = 8,
        .on_color_trans_done = on_color_trans_done,
        .user_ctx           = NULL,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(lcd_bus, &io_cfg, &lcd_io));

    /* ── RD pin: tie HIGH (we never read from display) ── */
#ifdef RG_GPIO_LCD_RD
    gpio_set_direction(RG_GPIO_LCD_RD, GPIO_MODE_OUTPUT);
    gpio_set_level(RG_GPIO_LCD_RD, 1);
#endif

    /* ── Hardware reset ── */
#ifdef RG_GPIO_LCD_RST
    gpio_set_direction(RG_GPIO_LCD_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(RG_GPIO_LCD_RST, 0);
    rg_usleep(100 * 1000);
    gpio_set_level(RG_GPIO_LCD_RST, 1);
    rg_usleep(120 * 1000);
#endif

    /* ── ST7796S init sequence ── */
    st7796_cmd(0x01, NULL, 0);       /* Software reset */
    rg_usleep(120 * 1000);

    st7796_cmd(0x11, NULL, 0);       /* Sleep out */
    rg_usleep(120 * 1000);

    /* MADCTL: MV=1 (row/column swap) + BGR → landscape 480x320 with
     * "up" toward the board's top edge (D-pad left, ABXY right). 0x28 is
     * rotation 1 of the usual ILI9488 table (0x48 portrait, 0x28, 0x88,
     * 0xE8); switch to 0xE8 if the image ever comes out upside-down. */
    st7796_cmd(0x36, (uint8_t[]){0x28}, 1);

    /* COLMOD: RGB565 */
    st7796_cmd(0x3A, (uint8_t[]){0x55}, 1);

    /* Display ON */
    st7796_cmd(0x29, NULL, 0);
    rg_usleep(20 * 1000);
}

static void lcd_deinit(void)
{
    lcd_sync();
    esp_lcd_panel_io_del(lcd_io);
    esp_lcd_del_i80_bus(lcd_bus);
    lcd_io  = NULL;
    lcd_bus = NULL;
}

const rg_display_driver_t rg_display_driver_st7796s = {
    .name = "st7796s_i80",
};
