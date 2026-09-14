#include "rg_system.h"
#include "rg_input.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef ESP_PLATFORM
#include <driver/gpio.h>
#include <driver/adc.h>
// This is a lazy way to silence deprecation notices on some esp-idf versions...
// This hardcoded value is the first thing to check if something stops working!
#define ADC_ATTEN_DB_11 3
#else
#include <SDL2/SDL.h>
#endif

#if RG_BATTERY_DRIVER == 1
#include <esp_adc_cal.h>
static esp_adc_cal_characteristics_t adc_chars;
#endif

#ifdef RG_GAMEPAD_ADC_MAP
static rg_keymap_adc_t keymap_adc[] = RG_GAMEPAD_ADC_MAP;
#endif
#ifdef RG_GAMEPAD_GPIO_MAP
static rg_keymap_gpio_t keymap_gpio[] = RG_GAMEPAD_GPIO_MAP;
#endif
#ifdef RG_GAMEPAD_I2C_MAP
static rg_keymap_i2c_t keymap_i2c[] = RG_GAMEPAD_I2C_MAP;
#endif
#ifdef RG_GAMEPAD_KBD_MAP
static rg_keymap_kbd_t keymap_kbd[] = RG_GAMEPAD_KBD_MAP;
#endif
#ifdef RG_GAMEPAD_SERIAL_MAP
static rg_keymap_serial_t keymap_serial[] = RG_GAMEPAD_SERIAL_MAP;
#endif
#ifdef RG_GAMEPAD_VIRT_MAP
static rg_keymap_virt_t keymap_virt[] = RG_GAMEPAD_VIRT_MAP;
#endif
#ifdef RG_GAMEPAD_CONSOLE
// Bench remote control: gamepad keys, app launch and a few queries over the
// serial console (stdin). Lines are "<cmd> [args]", replies are "CTL ..."
// lines so a host script can sync on them. See console_exec() for the list.
#include <fcntl.h>
#include <unistd.h>
#include "rg_storage.h"
static uint32_t console_held = 0;     // keys held until "release"
static uint32_t console_tap = 0;      // keys held until console_tap_until
static int64_t console_tap_until = 0;
static char console_line[192];
static size_t console_line_len = 0;
// App switches must run on the app's main task: rg_system_restart() waits
// for the input task (all keys released) and touches the display, so calling
// it from the input task deadlocks. The command only queues the request;
// rg_input_read_gamepad() (called every frame by every app) executes it.
static char console_action[RG_PATH_MAX + 64];
static volatile bool console_action_pending = false;
#endif
static bool input_task_running = false;
static uint32_t gamepad_state = -1; // _Atomic
static uint32_t gamepad_mapped = 0;
static rg_battery_t battery_state = {0};

#define UPDATE_GLOBAL_MAP(keymap)                 \
    for (size_t i = 0; i < RG_COUNT(keymap); ++i) \
        gamepad_mapped |= keymap[i].key;          \

#ifdef ESP_PLATFORM
static inline int adc_get_raw(adc_unit_t unit, adc_channel_t channel)
{
    if (unit == ADC_UNIT_1)
    {
        return adc1_get_raw(channel);
    }
    else if (unit == ADC_UNIT_2)
    {
        int adc_raw_value = -1;
        if (adc2_get_raw(channel, ADC_WIDTH_MAX - 1, &adc_raw_value) != ESP_OK)
            RG_LOGE("ADC2 reading failed, this can happen while wifi is active.");
        return adc_raw_value;
    }
    RG_LOGE("Invalid ADC unit %d", (int)unit);
    return -1;
}
#endif

bool rg_input_read_battery_raw(rg_battery_t *out)
{
    uint32_t raw_value = 0;
    bool present = true;
    bool charging = false;

#if RG_BATTERY_DRIVER == 1 /* ADC */
    for (int i = 0; i < 4; ++i)
    {
        int value = adc_get_raw(RG_BATTERY_ADC_UNIT, RG_BATTERY_ADC_CHANNEL);
        if (value < 0)
            return false;
        raw_value += esp_adc_cal_raw_to_voltage(value, &adc_chars);
    }
    raw_value /= 4;
#elif RG_BATTERY_DRIVER == 2 /* I2C */
    uint8_t data[5];
    if (!rg_i2c_read(0x20, -1, &data, 5))
        return false;
    raw_value = data[4];
    charging = data[4] == 255;
#else
    return false;
#endif

    if (!out)
        return true;

    *out = (rg_battery_t){
        .level = RG_MAX(0.f, RG_MIN(100.f, RG_BATTERY_CALC_PERCENT(raw_value))),
        .volts = RG_BATTERY_CALC_VOLTAGE(raw_value),
        .present = present,
        .charging = charging,
    };
    return true;
}

bool rg_input_read_gamepad_raw(uint32_t *out)
{
    uint32_t state = 0;

#if defined(RG_GAMEPAD_ADC_MAP)
    static int old_adc_values[RG_COUNT(keymap_adc)];
    for (size_t i = 0; i < RG_COUNT(keymap_adc); ++i)
    {
        const rg_keymap_adc_t *mapping = &keymap_adc[i];
        int value = adc_get_raw(mapping->unit, mapping->channel);
        if (value >= mapping->min && value <= mapping->max)
        {
            if (abs(old_adc_values[i] - value) < RG_GAMEPAD_ADC_FILTER_WINDOW)
                state |= mapping->key;
            // else
            //     RG_LOGD("Rejected input: %d", old_adc_values[i] - value);
            old_adc_values[i] = value;
        }
    }
#endif

#if defined(RG_GAMEPAD_GPIO_MAP)
    for (size_t i = 0; i < RG_COUNT(keymap_gpio); ++i)
    {
        const rg_keymap_gpio_t *mapping = &keymap_gpio[i];
        if (gpio_get_level(mapping->num) == mapping->level)
            state |= mapping->key;
    }
#endif

#if defined(RG_GAMEPAD_I2C_MAP)
    uint32_t buttons = 0;
#if defined(RG_I2C_GPIO_DRIVER)
    int data0 = rg_i2c_gpio_read_port(0), data1 = rg_i2c_gpio_read_port(1);
    if (data0 > -1) // && data1 > -1)
    {
        buttons = (data1 << 8) | (data0);
#elif defined(RG_TARGET_T_DECK_PLUS)
    uint8_t data[5];
    if (rg_i2c_read(T_DECK_KBD_ADDRESS, -1, &data, 5))
    {
        buttons = ((data[0] << 25) | (data[1] << 18) | (data[2] << 11) | ((data[3] & 0xF8) << 4) | (data[4]));
#else
    uint8_t data[5];
    if (rg_i2c_read(RG_I2C_GPIO_ADDR, -1, &data, 5))
    {
        buttons = (data[2] << 8) | (data[1]);
#endif
        for (size_t i = 0; i < RG_COUNT(keymap_i2c); ++i)
        {
            const rg_keymap_i2c_t *mapping = &keymap_i2c[i];
            if (((buttons >> mapping->num) & 1) == mapping->level)
                state |= mapping->key;
        }
    }
#endif

#if defined(RG_GAMEPAD_KBD_MAP)
#ifdef RG_TARGET_SDL2
    int numkeys = 0;
    const uint8_t *keys = SDL_GetKeyboardState(&numkeys);
    for (size_t i = 0; i < RG_COUNT(keymap_kbd); ++i)
    {
        const rg_keymap_kbd_t *mapping = &keymap_kbd[i];
        if (mapping->src < 0 || mapping->src >= numkeys)
            continue;
        if (keys[mapping->src])
            state |= mapping->key;
    }
#else
#warning "not implemented"
#endif
#endif

#if defined(RG_GAMEPAD_SERIAL_MAP)
    gpio_set_level(RG_GPIO_GAMEPAD_LATCH, 0);
    rg_usleep(5);
    gpio_set_level(RG_GPIO_GAMEPAD_LATCH, 1);
    rg_usleep(1);
    uint32_t buttons = 0;
    for (int i = 0; i < 16; i++)
    {
        buttons |= gpio_get_level(RG_GPIO_GAMEPAD_DATA) << (15 - i);
        gpio_set_level(RG_GPIO_GAMEPAD_CLOCK, 0);
        rg_usleep(1);
        gpio_set_level(RG_GPIO_GAMEPAD_CLOCK, 1);
        rg_usleep(1);
    }
    for (size_t i = 0; i < RG_COUNT(keymap_serial); ++i)
    {
        const rg_keymap_serial_t *mapping = &keymap_serial[i];
        if (((buttons >> mapping->num) & 1) == mapping->level)
            state |= mapping->key;
    }
#endif

#if defined(RG_GAMEPAD_VIRT_MAP)
    for (size_t i = 0; i < RG_COUNT(keymap_virt); ++i)
    {
        if (state == keymap_virt[i].src)
            state = keymap_virt[i].key;
    }
#endif

#if defined(RG_GAMEPAD_CONSOLE)
    state |= console_held | console_tap;
#endif

    if (out)
        *out = state;
    return true;
}

#if defined(RG_GAMEPAD_CONSOLE)
static uint32_t console_parse_keys(const char *names)
{
    static const struct { const char *name; rg_key_t key; } table[] = {
        {"up", RG_KEY_UP}, {"down", RG_KEY_DOWN}, {"left", RG_KEY_LEFT}, {"right", RG_KEY_RIGHT},
        {"a", RG_KEY_A}, {"b", RG_KEY_B}, {"x", RG_KEY_X}, {"y", RG_KEY_Y},
        {"start", RG_KEY_START}, {"select", RG_KEY_SELECT}, {"menu", RG_KEY_MENU},
        {"option", RG_KEY_OPTION}, {"l", RG_KEY_L}, {"r", RG_KEY_R}, {"all", RG_KEY_ALL},
    };
    uint32_t mask = 0;
    char buf[64];
    strncpy(buf, names, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    for (char *tok = strtok(buf, "+,"); tok; tok = strtok(NULL, "+,"))
    {
        bool found = false;
        for (size_t i = 0; i < RG_COUNT(table); ++i)
        {
            if (strcasecmp(tok, table[i].name) == 0)
                mask |= table[i].key, found = true;
        }
        if (!found)
            printf("CTL err unknown key '%s'\n", tok);
    }
    return mask;
}

static int console_ls_cb(const rg_scandir_t *file, void *arg)
{
    printf("CTL ls %c %8d %s\n", file->is_dir ? 'd' : 'f', (int)file->size, file->basename);
    return RG_SCANDIR_CONTINUE;
}

static int console_b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1; // '=' padding and anything else
}

// put <size> <path> : the host then streams base64 text (any line length,
// whitespace ignored) until `size` decoded bytes were written to <path>.
// Runs inline in the input task: only use it from the launcher, not while an
// emulator is reading the card.
static void console_put(const char *path, size_t size)
{
    FILE *fp = fopen(path, "wb");
    if (!fp)
    {
        printf("CTL put failed open %s\n", path);
        return;
    }
    uint8_t *out = malloc(3072);
    char in[1024];
    size_t written = 0, next_report = 0;
    uint32_t acc = 0;
    int bits = 0, n;
    int64_t start = rg_system_timer(), last = start;
    printf("CTL put ready %s %u\n", path, (unsigned)size);
    fflush(stdout);
    while (written < size)
    {
        if ((n = read(STDIN_FILENO, in, sizeof(in))) <= 0)
        {
            if (rg_system_timer() - last > 5000000)
            {
                printf("CTL put failed timeout at %u\n", (unsigned)written);
                break;
            }
            rg_usleep(200);
            continue;
        }
        last = rg_system_timer();
        size_t o = 0;
        for (int i = 0; i < n; ++i)
        {
            int v = console_b64_val(in[i]);
            if (v < 0)
                continue;
            acc = (acc << 6) | v;
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                out[o++] = (acc >> bits) & 0xFF;
            }
        }
        if (o > size - written)
            o = size - written;
        if (o && fwrite(out, 1, o, fp) != o)
        {
            printf("CTL put failed write at %u\n", (unsigned)written);
            break;
        }
        written += o;
        if (written >= next_report)
        {
            printf("CTL put %u\n", (unsigned)written);
            next_report = written + 262144;
        }
    }
    fclose(fp);
    free(out);
    printf("CTL put %s %u bytes %d ms\n", written == size ? "done" : "short", (unsigned)written,
           (int)((rg_system_timer() - start) / 1000));
}

static void console_exec(char *line)
{
    char *cmd = strtok(line, " ");
    if (!cmd)
        return;
    char *arg1 = strtok(NULL, " ");
    char *arg2 = strtok(NULL, " ");
    char *rest = strtok(NULL, "");   // remainder, may contain spaces (rom paths)

    if (strcmp(cmd, "ping") == 0)
    {
        printf("CTL pong app=%s\n", rg_system_get_app()->name);
    }
    else if (strcmp(cmd, "key") == 0 && arg1)
    {
        // key <names> [ms] : press for ms (default 100) then release
        int ms = arg2 ? atoi(arg2) : 100;
        console_tap = console_parse_keys(arg1);
        console_tap_until = rg_system_timer() + (int64_t)ms * 1000;
        printf("CTL key 0x%04x %dms\n", (unsigned)console_tap, ms);
    }
    else if (strcmp(cmd, "hold") == 0 && arg1)
    {
        console_held |= console_parse_keys(arg1);
        printf("CTL hold 0x%04x\n", (unsigned)console_held);
    }
    else if (strcmp(cmd, "release") == 0)
    {
        console_held &= arg1 ? ~console_parse_keys(arg1) : 0;
        console_tap = 0;
        printf("CTL hold 0x%04x\n", (unsigned)console_held);
    }
    else if (strcmp(cmd, "ls") == 0)
    {
        const char *path = arg1 ? arg1 : RG_STORAGE_ROOT;
        bool ok = rg_storage_scandir(path, console_ls_cb, NULL, RG_SCANDIR_FILES | RG_SCANDIR_DIRS | RG_SCANDIR_STAT);
        printf("CTL ls %s %s\n", ok ? "done" : "failed", path);
    }
    else if (strcmp(cmd, "put") == 0 && arg1 && arg2)
    {
        char path[RG_PATH_MAX + 1];
        snprintf(path, sizeof(path), "%s%s%s", arg2, rest ? " " : "", rest ? rest : "");
        console_put(path, (size_t)atoi(arg1));
    }
    else if (strcmp(cmd, "rm") == 0 && arg1)
    {
        char path[RG_PATH_MAX + 1];
        snprintf(path, sizeof(path), "%s%s%s%s%s", arg1, arg2 ? " " : "", arg2 ? arg2 : "", rest ? " " : "", rest ? rest : "");
        printf("CTL rm %s %s\n", rg_storage_delete(path) ? "done" : "failed", path);
    }
    else if ((strcmp(cmd, "launch") == 0 || strncmp(cmd, "resume", 6) == 0) && arg1 && arg2 && rest)
    {
        // launch <partition> <app> <rom path> : e.g. launch retro-core snes /sd/roms/snes/x.sfc
        // resume[N] ...                       : same, then load save-state slot N (default 0)
        snprintf(console_action, sizeof(console_action), "%s %s %s %s", cmd, arg1, arg2, rest);
        console_action_pending = true;
        printf("CTL %s %s %s %s\n", cmd, arg1, arg2, rest);
    }
    else if (strcmp(cmd, "hud") == 0 && arg1)
    {
        // hud on|off : the Debug HUD of the options menu (rg_system)
        rg_system_set_debug_hud(strcmp(arg1, "on") == 0);
        printf("CTL hud %s\n", rg_system_get_debug_hud() ? "on" : "off");
    }
    else if ((strcmp(cmd, "save") == 0 || strcmp(cmd, "load") == 0))
    {
        // save/load [slot] : emulator save-state, executed on the app's main task
        snprintf(console_action, sizeof(console_action), "%s %d", cmd, arg1 ? atoi(arg1) : 0);
        console_action_pending = true;
        printf("CTL %s queued\n", cmd);
    }
    else if (strcmp(cmd, "launcher") == 0 || strcmp(cmd, "reboot") == 0)
    {
        snprintf(console_action, sizeof(console_action), "%s", cmd);
        console_action_pending = true;
        printf("CTL %s\n", cmd);
    }
    else
    {
        printf("CTL err usage: ping | key <k[+k]> [ms] | hold <k> | release [k] | ls [path] | put <size> <path> | rm <path> | launch|resume <part> <app> <path> | save|load [slot] | hud on|off | launcher | reboot\n");
    }
}

static void console_poll(void)
{
    if (console_tap && rg_system_timer() >= console_tap_until)
        console_tap = 0;

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1)
    {
        if (c == '\n' || c == '\r')
        {
            if (console_line_len)
            {
                console_line[console_line_len] = 0;
                console_line_len = 0;
                console_exec(console_line);
            }
        }
        else if (console_line_len < sizeof(console_line) - 1)
            console_line[console_line_len++] = c;
    }
}
#endif

static void input_task(void *arg)
{
    uint8_t debounce[RG_KEY_COUNT];
    uint32_t local_gamepad_state = 0;
    uint32_t state;
    int64_t next_battery_update = 0;

    // Start the task with debounce history full to allow a button held during boot to be detected
    memset(debounce, 0xFF, sizeof(debounce));
    input_task_running = true;

    while (input_task_running)
    {
        if (rg_input_read_gamepad_raw(&state))
        {
            for (int i = 0; i < RG_KEY_COUNT; ++i)
            {
                uint32_t val = ((debounce[i] << 1) | ((state >> i) & 1));
                debounce[i] = val & 0xFF;

                if ((val & ((1 << RG_GAMEPAD_DEBOUNCE_PRESS) - 1)) == ((1 << RG_GAMEPAD_DEBOUNCE_PRESS) - 1))
                {
                    local_gamepad_state |= (1 << i); // Pressed
                }
                else if ((val & ((1 << RG_GAMEPAD_DEBOUNCE_RELEASE) - 1)) == 0)
                {
                    local_gamepad_state &= ~(1 << i); // Released
                }
            }
            gamepad_state = local_gamepad_state;
        }

#if defined(RG_GAMEPAD_CONSOLE)
        console_poll();
#endif

        if (rg_system_timer() >= next_battery_update)
        {
            rg_battery_t temp = {0};
            if (rg_input_read_battery_raw(&temp))
            {
                if (fabsf(battery_state.level - temp.level) < RG_BATTERY_UPDATE_THRESHOLD)
                    temp.level = battery_state.level;
                if (fabsf(battery_state.volts - temp.volts) < RG_BATTERY_UPDATE_THRESHOLD_VOLT)
                    temp.volts = battery_state.volts;
            }
            battery_state = temp;
            next_battery_update = rg_system_timer() + 2 * 1000000; // update every 2 seconds
        }

        rg_task_delay(10);
    }

    input_task_running = false;
    gamepad_state = -1;
}

void rg_input_init(void)
{
    RG_ASSERT(!input_task_running, "Input already initialized!");

#if defined(RG_GAMEPAD_ADC_MAP)
    RG_LOGI("Initializing ADC gamepad driver...");
    adc1_config_width(ADC_WIDTH_MAX - 1);
    for (size_t i = 0; i < RG_COUNT(keymap_adc); ++i)
    {
        const rg_keymap_adc_t *mapping = &keymap_adc[i];
        if (mapping->unit == ADC_UNIT_1)
            adc1_config_channel_atten(mapping->channel, mapping->atten);
        else if (mapping->unit == ADC_UNIT_2)
            adc2_config_channel_atten(mapping->channel, mapping->atten);
        else
            RG_LOGE("Invalid ADC unit %d!", (int)mapping->unit);
    }
    UPDATE_GLOBAL_MAP(keymap_adc);
#endif

#if defined(RG_GAMEPAD_GPIO_MAP)
    RG_LOGI("Initializing GPIO gamepad driver...");
    for (size_t i = 0; i < RG_COUNT(keymap_gpio); ++i)
    {
        const rg_keymap_gpio_t *mapping = &keymap_gpio[i];
        gpio_set_direction(mapping->num, GPIO_MODE_INPUT);
        if (mapping->pullup && mapping->pulldown)
            gpio_set_pull_mode(mapping->num, GPIO_PULLUP_PULLDOWN);
        else if (mapping->pullup || mapping->pulldown)
            gpio_set_pull_mode(mapping->num, mapping->pullup ? GPIO_PULLUP_ONLY : GPIO_PULLDOWN_ONLY);
        else
            gpio_set_pull_mode(mapping->num, GPIO_FLOATING);
    }
    UPDATE_GLOBAL_MAP(keymap_gpio);
#endif

#if defined(RG_GAMEPAD_I2C_MAP)
    RG_LOGI("Initializing I2C gamepad driver...");
    rg_i2c_init();
#if defined(RG_I2C_GPIO_DRIVER)
    for (size_t i = 0; i < RG_COUNT(keymap_i2c); ++i)
    {
        const rg_keymap_i2c_t *mapping = &keymap_i2c[i];
        if (mapping->pullup)
            rg_i2c_gpio_set_direction(mapping->num, RG_GPIO_INPUT_PULLUP);
        else
            rg_i2c_gpio_set_direction(mapping->num, RG_GPIO_INPUT);
    }
#elif defined(RG_TARGET_T_DECK_PLUS)
    rg_i2c_write_byte(T_DECK_KBD_ADDRESS, -1, T_DECK_KBD_MODE_RAW_CMD);
#endif
    UPDATE_GLOBAL_MAP(keymap_i2c);
#endif

#if defined(RG_GAMEPAD_KBD_MAP)
    RG_LOGI("Initializing KBD gamepad driver...");
    UPDATE_GLOBAL_MAP(keymap_kbd);
#endif

#if defined(RG_GAMEPAD_SERIAL_MAP)
    RG_LOGI("Initializing SERIAL gamepad driver...");
    gpio_set_direction(RG_GPIO_GAMEPAD_CLOCK, GPIO_MODE_OUTPUT);
    gpio_set_direction(RG_GPIO_GAMEPAD_LATCH, GPIO_MODE_OUTPUT);
    gpio_set_direction(RG_GPIO_GAMEPAD_DATA, GPIO_MODE_INPUT);
    gpio_set_level(RG_GPIO_GAMEPAD_LATCH, 0);
    gpio_set_level(RG_GPIO_GAMEPAD_CLOCK, 1);
    UPDATE_GLOBAL_MAP(keymap_serial);
#endif


#if RG_BATTERY_DRIVER == 1 /* ADC */
    RG_LOGI("Initializing ADC battery driver...");
    if (RG_BATTERY_ADC_UNIT == ADC_UNIT_1)
    {
        adc1_config_width(ADC_WIDTH_MAX - 1); // there is no adc2_config_width
        adc1_config_channel_atten(RG_BATTERY_ADC_CHANNEL, ADC_ATTEN_DB_11);
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_MAX - 1, 1100, &adc_chars);
    }
    else if (RG_BATTERY_ADC_UNIT == ADC_UNIT_2)
    {
        adc2_config_channel_atten(RG_BATTERY_ADC_CHANNEL, ADC_ATTEN_DB_11);
        esp_adc_cal_characterize(ADC_UNIT_2, ADC_ATTEN_DB_11, ADC_WIDTH_MAX - 1, 1100, &adc_chars);
    }
    else
    {
        RG_LOGE("Only ADC1 and ADC2 are supported for ADC battery driver!");
    }
#endif

    // The first read returns bogus data in some drivers, waste it.
    rg_input_read_gamepad_raw(NULL);

    // Start background polling
#if defined(RG_GAMEPAD_CONSOLE)
    RG_LOGI("Console gamepad control enabled (stdin)");
    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
    rg_task_create("rg_input", &input_task, NULL, 5 * 1024, RG_TASK_PRIORITY_6, 1);
#else
    rg_task_create("rg_input", &input_task, NULL, 3 * 1024, RG_TASK_PRIORITY_6, 1);
#endif
    while (gamepad_state == -1)
        rg_task_yield();
    RG_LOGI("Input ready. state=" PRINTF_BINARY_16 "\n", PRINTF_BINVAL_16(gamepad_state));
}

void rg_input_deinit(void)
{
    input_task_running = false;
    // while (gamepad_state != -1)
    //     rg_task_yield();
    RG_LOGI("Input terminated.\n");
}

bool rg_input_key_is_present(rg_key_t mask)
{
    return (gamepad_mapped & mask) == mask;
}

// Runs queued console actions (launch/resume/save/load/reboot) at a frame
// boundary. Called from rg_system_tick(); NOT from rg_input_read_gamepad(),
// which snes9x calls from inside the emulated frame (S9xReadJoypad) — a
// save-state taken there is mid-instruction and unusable.
void rg_input_console_tick(void)
{
#ifdef RG_GAMEPAD_CONSOLE
    if (console_action_pending)
    {
        console_action_pending = false;
        char *cmd = strtok(console_action, " ");
        char *part = strtok(NULL, " ");
        char *app = strtok(NULL, " ");
        char *path = strtok(NULL, "");
        fflush(stdout);
        if (strcmp(cmd, "launch") == 0 && part && app && path)
            rg_system_switch_app(part, app, path, 0);
        else if (strncmp(cmd, "resume", 6) == 0 && part && app && path)
            rg_system_switch_app(part, app, path, RG_BOOT_RESUME | ((atoi(cmd + 6) << 4) & RG_BOOT_SLOT_MASK));
        else if (strcmp(cmd, "save") == 0 && part)
            printf("CTL save %s slot %d\n", rg_emu_save_state(atoi(part)) ? "done" : "failed", atoi(part));
        else if (strcmp(cmd, "load") == 0 && part)
            printf("CTL load %s slot %d\n", rg_emu_load_state(atoi(part)) ? "done" : "failed", atoi(part));
        else if (strcmp(cmd, "launcher") == 0)
            rg_system_switch_app(RG_APP_LAUNCHER, RG_APP_LAUNCHER, NULL, 0);
        else
            rg_system_restart();
    }
#endif
}

uint32_t rg_input_read_gamepad(void)
{
#ifdef RG_TARGET_SDL2
    SDL_PumpEvents();
#endif
    return gamepad_state;
}

bool rg_input_key_is_pressed(rg_key_t mask)
{
    return (bool)(rg_input_read_gamepad() & mask);
}

bool rg_input_wait_for_key(rg_key_t mask, bool pressed, int timeout_ms)
{
    int64_t expiration = timeout_ms < 0 ? INT64_MAX : (rg_system_timer() + timeout_ms * 1000);
    while (rg_input_key_is_pressed(mask) != pressed)
    {
        if (rg_system_timer() > expiration)
            return false;
        rg_task_delay(10);
    }
    return true;
}

rg_battery_t rg_input_read_battery(void)
{
    return battery_state;
}

const char *rg_input_get_key_name(rg_key_t key)
{
    switch (key)
    {
    case RG_KEY_UP: return "Up";
    case RG_KEY_RIGHT: return "Right";
    case RG_KEY_DOWN: return "Down";
    case RG_KEY_LEFT: return "Left";
    case RG_KEY_SELECT: return "Select";
    case RG_KEY_START: return "Start";
    case RG_KEY_MENU: return "Menu";
    case RG_KEY_OPTION: return "Option";
    case RG_KEY_A: return "A";
    case RG_KEY_B: return "B";
    case RG_KEY_X: return "X";
    case RG_KEY_Y: return "Y";
    case RG_KEY_L: return "Left Shoulder";
    case RG_KEY_R: return "Right Shoulder";
    case RG_KEY_NONE: return "None";
    default: return "Unknown";
    }
}
