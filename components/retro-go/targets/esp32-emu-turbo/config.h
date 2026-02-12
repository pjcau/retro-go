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
#define RG_STORAGE_SDSPI_SPEED      SDMMC_FREQ_HIGHSPEED

// Audio — External I2S DAC -> PAM8403 amplifier
#define RG_AUDIO_USE_INT_DAC        0   // No internal DAC on ESP32-S3
#define RG_AUDIO_USE_EXT_DAC        1   // External I2S DAC enabled

// Video — ST7796S 320x480, 8-bit 8080 parallel (custom driver)
#define RG_SCREEN_DRIVER            2   // 2 = ST7796S i80 parallel
#define RG_SCREEN_WIDTH             320
#define RG_SCREEN_HEIGHT            480
#define RG_SCREEN_ROTATE            0   // Portrait orientation
#define RG_SCREEN_BACKLIGHT         1

// Input — 12 buttons, all GPIO direct, active-low with external 10k pull-up
// No internal pull-up needed (pullup = 0), active-low (level = 0)
#define RG_GAMEPAD_GPIO_MAP {\
    {RG_KEY_UP,     .num = GPIO_NUM_40, .pullup = 0, .level = 0},\
    {RG_KEY_DOWN,   .num = GPIO_NUM_41, .pullup = 0, .level = 0},\
    {RG_KEY_LEFT,   .num = GPIO_NUM_42, .pullup = 0, .level = 0},\
    {RG_KEY_RIGHT,  .num = GPIO_NUM_1,  .pullup = 0, .level = 0},\
    {RG_KEY_A,      .num = GPIO_NUM_2,  .pullup = 0, .level = 0},\
    {RG_KEY_B,      .num = GPIO_NUM_48, .pullup = 0, .level = 0},\
    {RG_KEY_X,      .num = GPIO_NUM_47, .pullup = 0, .level = 0},\
    {RG_KEY_Y,      .num = GPIO_NUM_21, .pullup = 0, .level = 0},\
    {RG_KEY_START,  .num = GPIO_NUM_18, .pullup = 0, .level = 0},\
    {RG_KEY_SELECT, .num = GPIO_NUM_0,  .pullup = 0, .level = 0},\
    {RG_KEY_L,      .num = GPIO_NUM_35, .pullup = 0, .level = 0},\
    {RG_KEY_R,      .num = GPIO_NUM_19, .pullup = 0, .level = 0},\
    {RG_KEY_MENU,   .num = GPIO_NUM_0,  .pullup = 0, .level = 0},\
}

// Battery — IP5306 via I2C (custom driver, not ADC-based)
#define RG_BATTERY_DRIVER           0   // Disabled for now (IP5306 needs custom I2C driver)

// I2C Bus (IP5306 power management)
#define RG_GPIO_I2C_SDA             GPIO_NUM_33
#define RG_GPIO_I2C_SCL             GPIO_NUM_34

// SD Card SPI pins
#define RG_GPIO_SDSPI_MISO          GPIO_NUM_37
#define RG_GPIO_SDSPI_MOSI          GPIO_NUM_36
#define RG_GPIO_SDSPI_CLK           GPIO_NUM_38
#define RG_GPIO_SDSPI_CS            GPIO_NUM_39

// External I2S DAC pins (-> PAM8403 amplifier -> speaker)
#define RG_GPIO_SND_I2S_BCK         GPIO_NUM_15
#define RG_GPIO_SND_I2S_WS          GPIO_NUM_16
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
#define RG_GPIO_LCD_RD              GPIO_NUM_3
#define RG_GPIO_LCD_BCKL            GPIO_NUM_45
#define RG_LCD_I80_CLK_HZ           (20 * 1000 * 1000)  // 20 MHz write clock
