/*
 * ESP32 Emu Turbo — Retro-Go Target Configuration
 *
 * Hardware: ESP32-S3 N16R8, ST7796S 4.0" 320x480 8-bit i80 parallel,
 *           12 buttons (GPIO direct), I2S audio -> PAM8403,
 *           SD card via SPI, IP5306 battery management.
 *
 * GPIO source of truth: software/main/board_config.h
 */

// Target definition
#define RG_TARGET_NAME             "ESP32-EMU-TURBO"

// Storage — SD card via SPI
#define RG_STORAGE_ROOT             "/sd"
#define RG_STORAGE_SDSPI_HOST       SPI2_HOST
// 20 MHz, not HIGHSPEED(40): SD traces are ~150mm with 6 vias and 40 MHz
// is unreliable on this board (board_config.h SD_SPI_FREQ_KHZ, R30-MED-2).
#define RG_STORAGE_SDSPI_SPEED      SDMMC_FREQ_DEFAULT

// Audio — ESP32-S3 PDM sigma-delta on GPIO17 -> C22 DC-block -> PAM8403.
// The board has no I2S DAC: drivers/audio/pdm.c modulates 16-bit PCM to a
// 1-bit stream on the data pin alone (same config as the board-validated
// Phase-1 firmware, software/main/audio.c). Closes audit R30-HIGH-3;
// bench-validate levels on a proto before calling audio done.
#define RG_AUDIO_USE_INT_DAC        0   // No internal DAC on ESP32-S3
#define RG_AUDIO_USE_EXT_DAC        0   // No external DAC on this board
#define RG_AUDIO_USE_PDM            1   // drivers/audio/pdm.c (IDF5 PDM TX)
// Start silent on this board revision: with no PDM reconstruction filter
// (first-article R38-MED-1) the carrier is a loud hiss whenever the channel
// runs, so audio is opt-in from the menu until the v2 RC filter / audio
// coprocessor lands. pdm.c keeps the channel off at volume 0.
#define RG_AUDIO_DEFAULT_VOLUME     0

// Video — ILI9488 panel (native 320x480 portrait), 8-bit 8080 parallel
// (custom driver). The panel sits along the long axis of the handheld
// (D-pad left, ABXY right), so the framebuffer is LANDSCAPE 480x320: the
// driver sets MADCTL MV to swap the panel's row/column axes (verified on the
// first article 2026-09-11; the earlier "portrait 320x480, 2x vertical"
// plan would have shown games rotated by 90 degrees).
#define RG_SCREEN_DRIVER            2   // 2 = i80 parallel (st7796s_i80.h)
#define RG_SCREEN_WIDTH             480
#define RG_SCREEN_HEIGHT            320
#define RG_SCREEN_ROTATE            0   // Fixed: orientation is baked into MADCTL
// 0: the panel backlight (LED-A) is hard-wired to +3V3 — always on, no
// GPIO, no PWM. GPIO45 (the old BCKL guess) is really BTN_L + the
// VDD_SPI strap (audit R30-CRIT-2).
#define RG_SCREEN_BACKLIGHT         0

// Input — 12 buttons, GPIO direct, active-low with external 10k pull-up
// (board_config.h BTN_* is the source of truth — audit R30-CRIT-1/2/3).
// RIGHT=GPIO2 / A=GPIO1: module pin 38 is IO2 and pin 39 is IO1 (WROOM-1
// datasheet); the project pin table had them swapped until the first
// article showed D-pad RIGHT acting as A (R39-HIGH-1, 2026-09-11).
// L (GPIO45) is the one exception on pull-up: R14 is DNP by design
// (external pull-up on the VDD_SPI strap would force 1.8V and kill the
// Octal PSRAM), so it uses the chip-internal pull-up (.pullup = 1).
#define RG_GAMEPAD_GPIO_MAP {\
    {RG_KEY_UP,     .num = GPIO_NUM_40, .pullup = 0, .level = 0},\
    {RG_KEY_DOWN,   .num = GPIO_NUM_41, .pullup = 0, .level = 0},\
    {RG_KEY_LEFT,   .num = GPIO_NUM_42, .pullup = 0, .level = 0},\
    {RG_KEY_RIGHT,  .num = GPIO_NUM_2,  .pullup = 0, .level = 0},\
    {RG_KEY_A,      .num = GPIO_NUM_1,  .pullup = 0, .level = 0},\
    {RG_KEY_B,      .num = GPIO_NUM_48, .pullup = 0, .level = 0},\
    {RG_KEY_X,      .num = GPIO_NUM_47, .pullup = 0, .level = 0},\
    {RG_KEY_Y,      .num = GPIO_NUM_21, .pullup = 0, .level = 0},\
    {RG_KEY_START,  .num = GPIO_NUM_18, .pullup = 0, .level = 0},\
    {RG_KEY_SELECT, .num = GPIO_NUM_0,  .pullup = 0, .level = 0},\
    {RG_KEY_L,      .num = GPIO_NUM_45, .pullup = 1, .level = 0},\
    {RG_KEY_R,      .num = GPIO_NUM_3,  .pullup = 0, .level = 0},\
}

// MENU has no GPIO of its own: SW13 presses START+SELECT together through
// the D1 BAT54C diode OR-gate (net MENU_K). Decode that exact combination
// as MENU, matching software/main/input.c BTN_MENU_COMBO.
#define RG_GAMEPAD_VIRT_MAP {\
    {RG_KEY_MENU,   .src = RG_KEY_START | RG_KEY_SELECT},\
}

// Battery — no driver: the board's IP5306 variant has no I2C routed
// (and GPIO33/34 are module-internal Octal PSRAM lines — never drive them).
#define RG_BATTERY_DRIVER           0

// SD Card SPI pins (board_config.h SD_* — audit R30-CRIT-1)
#define RG_GPIO_SDSPI_MISO          GPIO_NUM_43
#define RG_GPIO_SDSPI_MOSI          GPIO_NUM_44
#define RG_GPIO_SDSPI_CLK           GPIO_NUM_38
#define RG_GPIO_SDSPI_CS            GPIO_NUM_39

// PDM audio data pin (-> C22 -> PAM8403 -> speaker). No BCK/WS: GPIO15/16
// are unconnected on the board and PDM needs no clock pin.
#define RG_GPIO_SND_I2S_DATA        GPIO_NUM_17

// Display i80 parallel bus pins (used by st7796s_i80.h driver)
#define RG_GPIO_LCD_D0              GPIO_NUM_4
#define RG_GPIO_LCD_D1              GPIO_NUM_5
#define RG_GPIO_LCD_D2              GPIO_NUM_6
#define RG_GPIO_LCD_D3              GPIO_NUM_7
#define RG_GPIO_LCD_D4              GPIO_NUM_8
#define RG_GPIO_LCD_D5              GPIO_NUM_9
#define RG_GPIO_LCD_D6              GPIO_NUM_10
#define RG_GPIO_LCD_D7              GPIO_NUM_11
#define RG_GPIO_LCD_CS              GPIO_NUM_12
#define RG_GPIO_LCD_RST             GPIO_NUM_13
#define RG_GPIO_LCD_DC              GPIO_NUM_14
#define RG_GPIO_LCD_WR              GPIO_NUM_46
// No RD and no BCKL define on purpose: panel RD (pin 12) and LED-A
// (pin 33) are hard-tied to +3V3 on the PCB — neither reaches a GPIO.
// The driver #ifdef-skips both blocks when the defines are absent
// (audit R30-CRIT-2; GPIO3 is BTN_R, GPIO45 is BTN_L/VDD_SPI strap).
#define RG_LCD_I80_CLK_HZ           (20 * 1000 * 1000)  // 20 MHz write clock
