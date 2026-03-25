#ifdef POINTING_DEVICE_ENABLE

#    include <math.h>
#    include "wait.h"
#    include "debug.h"
#    include "gpio.h"
#    include "timer.h"
#    include "pointing_device_internal.h"
#    include "modular_pmw3610.h"

// PMW3610 register map (page 0 unless specified)
// Based on public PMW3610 datasheet and upstream Zephyr driver.
// clang-format off
#define REG_PROD_ID             0x00
#define REG_REV_ID              0x01
#define REG_MOTION              0x02
#define REG_DELTA_X_L           0x03
#define REG_DELTA_Y_L           0x04
#define REG_DELTA_XY_H          0x05 // [7:4]=X[11:8], [3:0]=Y[11:8]
#define REG_OBSERVATION1        0x2d
#define REG_POWER_UP_RESET      0x3a
#define REG_SHUTDOWN            0x3b
#define REG_SPI_CLK_ON_REQ      0x41
#define REG_PERFORMANCE         0x11
#define REG_RUN_DOWNSHIFT       0x14
#define REG_REST1_RATE          0x15
#define REG_REST1_DOWNSHIFT     0x18
#define REG_BURST_READ          0x12

// Paging (write 0xFF to enter page-1, 0x00 to return to page-0)
#define REG_SPI_PAGE            0x7f

// Page-1 registers
#define REG1_RES_STEP           0x05 // [4:0] = resolution step (200 CPI per LSB), [6:5] axis invert

// Magic values
#define VAL_POWER_UP_RESET      0x96
#define VAL_SHUTDOWN            0xE7
#define VAL_SPI_CLK_ON_REQ      0xBA
#define VAL_SPI_CLK_OFF_REQ     0xB5


// SPI timing (us). Conservative values based on PMW33xx timings to avoid read corruption.
#define PMW3610_T_NCS_SCLK_US    1
#define PMW3610_T_SRAD_US        160
#define PMW3610_T_SRAD_BURST_US  PMW3610_T_SRAD_US
#define PMW3610_T_SRW_US         20
#define PMW3610_T_SWW_US         35

// clang-format on

static bool     connected[NUM_MODULAR_PMW3610]    = {false};
static bool     powered_down[NUM_MODULAR_PMW3610] = {true};
static uint16_t cpi_setting[NUM_MODULAR_PMW3610];

const pin_t PMW3610_SCLK_PINS[]   = MODULAR_PMW3610_SCLK_PINS;
const pin_t PMW3610_SDIO_PINS[]   = MODULAR_PMW3610_SDIO_PINS;
const pin_t PMW3610_CS_PINS[]     = MODULAR_PMW3610_CS_PINS;
const pin_t PMW3610_MOTION_PINS[] = MODULAR_PMW3610_MOTION_PINS;

static inline uint8_t pair_start_slot(uint8_t pair_index) {
    return (uint8_t)(pair_index * 2u);
}

// angle per sensor
static uint16_t angle_deg[NUM_MODULAR_PMW3610] = {0};
static uint32_t trackball_timeout_length       = 5 * 60 * 1000; // default: 5 min
static uint32_t trackball_timeout              = 0;

static inline int16_t sign_extend_12(uint16_t v) {
    return (v & 0x800) ? (int16_t)(v | 0xF000) : (int16_t)v;
}

static inline void cs_select(uint8_t i) {
    gpio_write_pin_low(PMW3610_CS_PINS[i]);
}
static inline void cs_deselect(uint8_t i) {
    gpio_write_pin_high(PMW3610_CS_PINS[i]);
}

void modular_pmw3610_init_slot(uint8_t index) {
    if (index >= NUM_MODULAR_PMW3610) {
        return;
    }

    gpio_set_pin_output(PMW3610_SCLK_PINS[index]);
    gpio_set_pin_output(PMW3610_SDIO_PINS[index]);
    gpio_set_pin_output(PMW3610_CS_PINS[index]);

    gpio_write_pin_high(PMW3610_CS_PINS[index]);
    gpio_write_pin_high(PMW3610_SCLK_PINS[index]); // idle high (SPI mode 3)

    powered_down[index] = true;
    connected[index]    = false;
    cpi_setting[index]  = PMW3610_DEFAULT_CPI;
    angle_deg[index]    = 0;
}

void modular_pmw3610_set_motion_mode_for_pair(uint8_t pair_index) {
    uint8_t first = pair_start_slot(pair_index);
    if ((first + 1u) >= NUM_MODULAR_PMW3610) {
        return;
    }

    gpio_set_pin_input_high(PMW3610_MOTION_PINS[first]);
}

bool modular_pmw3610_init(void) {
    for (uint8_t i = 0; i < NUM_MODULAR_PMW3610; i++) {
        modular_pmw3610_init_slot(i);
    }
    for (uint8_t pair = 0; (pair * 2u) < NUM_MODULAR_PMW3610; pair++) {
        modular_pmw3610_set_motion_mode_for_pair(pair);
    }

    modular_pmw3610_wake_up_all(false);
    return true;
}

report_mouse_t modular_pmw3610_get_all_report(report_mouse_t mouse_report) {
    for (uint8_t i = 0; i < NUM_MODULAR_PMW3610; i++) {
        mouse_report = modular_pmw3610_get_report(i, mouse_report);
    }
    modular_pmw3610_check_timeout();
    return mouse_report;
}

uint16_t modular_pmw3610_get_all_cpi(void) {
    uint16_t maxCPI = 0;
    for (uint8_t i = 0; i < NUM_MODULAR_PMW3610; i++) {
        uint16_t cpi = modular_pmw3610_get_cpi(i);
        if (cpi > maxCPI) maxCPI = cpi;
    }
    return maxCPI;
}

void modular_pmw3610_set_all_cpi(uint16_t cpi) {
    for (uint8_t i = 0; i < NUM_MODULAR_PMW3610; i++) {
        modular_pmw3610_set_cpi(i, cpi);
    }
}

void modular_pmw3610_power_down_all(void) {
    for (uint8_t i = 0; i < NUM_MODULAR_PMW3610; i++) {
        if (!powered_down[i]) modular_pmw3610_power_down(i);
    }
}

void modular_pmw3610_wake_up_all(bool connected_only) {
    for (uint8_t i = 0; i < NUM_MODULAR_PMW3610; i++) {
        modular_pmw3610_wake_up(i, connected_only);
    }
}

void modular_pmw3610_check_timeout(void) {
    if (trackball_timeout_length == 0) return;
    if (timer_expired32(timer_read32(), trackball_timeout)) {
        modular_pmw3610_power_down_all();
    }
}

bool modular_pmw3610_probe(uint8_t index) {
    if (index >= NUM_MODULAR_PMW3610) {
        return false;
    }

    modular_pmw3610_wake_up(index, false);
    return connected[index];
}

uint8_t modular_pmw3610_get_connected_count(void) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < NUM_MODULAR_PMW3610; i++)
        count += connected[i] ? 1 : 0;
    return count;
}

bool modular_pmw3610_is_connected(uint8_t index) {
    if (index >= NUM_MODULAR_PMW3610) return false;
    return connected[index];
}

uint8_t modular_pmw3610_get_connected_flags(uint8_t *out, uint8_t max) {
    if (!out || max == 0) return 0;
    uint8_t count = (NUM_MODULAR_PMW3610 > max) ? max : NUM_MODULAR_PMW3610;
    for (uint8_t i = 0; i < count; i++) {
        out[i] = connected[i] ? 1 : 0;
    }
    for (uint8_t i = count; i < max; i++) {
        out[i] = 0;
    }
    return count;
}

void modular_pmw3610_set_led_off_length(uint32_t length_ms) {
    trackball_timeout_length = length_ms;
    trackball_timeout        = timer_read32() + trackball_timeout_length;
}

bool modular_pmw3610_check_motion(uint8_t index) {
    if (!connected[index]) {
        return false;
    }
    return gpio_read_pin(PMW3610_MOTION_PINS[index]) == 0;
}

bool modular_pmw3610_check_motion_all(void) {
    for (uint8_t i = 0; i < NUM_MODULAR_PMW3610; i++) {
        if (modular_pmw3610_check_motion(i)) {
            return true;
        }
    }
    return false;
}

void modular_pmw3610_sync(uint8_t index) {
    cs_select(index);
    wait_us(1);
    cs_deselect(index);
}

// Bit-banged SPI (mode 3 like):
// - idle high, sample on rising edge
uint8_t modular_pmw3610_serial_read(uint8_t index) {
    gpio_set_pin_input(PMW3610_SDIO_PINS[index]);
    uint8_t byte = 0;
    for (uint8_t i = 0; i < 8; ++i) {
        // falling edge
        gpio_write_pin_low(PMW3610_SCLK_PINS[index]);
        wait_us(1);
        // rising edge: sample
        gpio_write_pin_high(PMW3610_SCLK_PINS[index]);
        byte = (uint8_t)((byte << 1) | gpio_read_pin(PMW3610_SDIO_PINS[index]));
        wait_us(1);
    }
    return byte;
}

void modular_pmw3610_serial_write(uint8_t index, uint8_t data) {
    gpio_set_pin_output(PMW3610_SDIO_PINS[index]);
    for (int8_t b = 7; b >= 0; b--) {
        // prepare next bit while clock is high, then shift on rising edge
        gpio_write_pin_low(PMW3610_SCLK_PINS[index]);
        if (data & (1 << b))
            gpio_write_pin_high(PMW3610_SDIO_PINS[index]);
        else
            gpio_write_pin_low(PMW3610_SDIO_PINS[index]);
        wait_us(1);
        gpio_write_pin_high(PMW3610_SCLK_PINS[index]);
        wait_us(1);
    }
    // Inter-op delay between xfers
    wait_us(4);
}

uint8_t modular_pmw3610_read_reg(uint8_t index, uint8_t reg_addr) {
    cs_select(index);
    wait_us(PMW3610_T_NCS_SCLK_US);
    modular_pmw3610_serial_write(index, (uint8_t)(reg_addr & 0x7F));
    wait_us(PMW3610_T_SRAD_US);
    uint8_t val = modular_pmw3610_serial_read(index);
    cs_deselect(index);
    wait_us(PMW3610_T_SRW_US);
    return val;
}

void modular_pmw3610_write_reg(uint8_t index, uint8_t reg_addr, uint8_t data) {
    cs_select(index);
    wait_us(PMW3610_T_NCS_SCLK_US);
    modular_pmw3610_serial_write(index, (uint8_t)(0x80 | reg_addr));
    modular_pmw3610_serial_write(index, data);
    cs_deselect(index);
    wait_us(PMW3610_T_SWW_US);
}

static void pmw3610_page_select(uint8_t index, bool page1) {
    modular_pmw3610_write_reg(index, REG_SPI_PAGE, page1 ? 0xFF : 0x00);
}

static void pmw3610_clock_on(uint8_t index) {
    modular_pmw3610_write_reg(index, REG_SPI_CLK_ON_REQ, VAL_SPI_CLK_ON_REQ);
}
static void pmw3610_clock_off(uint8_t index) {
    modular_pmw3610_write_reg(index, REG_SPI_CLK_ON_REQ, VAL_SPI_CLK_OFF_REQ);
}

static void pmw3610_basic_config(uint8_t index) {
    // Follow datasheet/Zephyr: request fast clock for configuration and init sequence.
    pmw3610_clock_on(index);
    wait_us(300);

    // Datasheet suggests clearing OBSERVATION1 and priming motion registers
    modular_pmw3610_write_reg(index, REG_OBSERVATION1, 0x00);
    wait_ms(10);
    (void)modular_pmw3610_read_reg(index, REG_MOTION);
    (void)modular_pmw3610_read_reg(index, REG_DELTA_X_L);
    (void)modular_pmw3610_read_reg(index, REG_DELTA_Y_L);
    (void)modular_pmw3610_read_reg(index, REG_DELTA_XY_H);

    // Performance and rest timing (reasonable defaults)
    modular_pmw3610_write_reg(index, REG_PERFORMANCE, 0x0D);
    modular_pmw3610_write_reg(index, REG_RUN_DOWNSHIFT, 0x04);
    modular_pmw3610_write_reg(index, REG_REST1_RATE, 0x04);
    modular_pmw3610_write_reg(index, REG_REST1_DOWNSHIFT, 0x0F);

    pmw3610_clock_off(index);
}

void modular_pmw3610_wake_up(uint8_t index, bool connected_only) {
    trackball_timeout = timer_read32() + trackball_timeout_length;

    if (!powered_down[index]) return;
    if (connected_only && !connected[index]) return;

    dprintf("pmw3610 %d: wake_up start\n", index);

    // Power-up sequence (CS high/low toggle) before reset.
    cs_deselect(index);
    wait_us(40);
    cs_select(index);
    wait_us(40);
    cs_deselect(index);
    wait_us(40);

    // Power-up reset sequence
    modular_pmw3610_write_reg(index, REG_POWER_UP_RESET, VAL_POWER_UP_RESET);
    wait_ms(50);

    uint8_t pid = modular_pmw3610_read_reg(index, REG_PROD_ID);
    if (pid != 0x3E) { // Expected product ID
        powered_down[index] = true;
        connected[index]    = false;
        dprintf("pmw3610 %d: unexpected pid, staying powered down\n", index);
        return;
    }

    powered_down[index] = false;
    connected[index]    = true;

    pmw3610_basic_config(index);
    modular_pmw3610_set_cpi(index, cpi_setting[index] ? cpi_setting[index] : PMW3610_DEFAULT_CPI);
    dprintf("pmw3610 %d: cpi set to %u\n", index, modular_pmw3610_get_cpi(index));
}

report_modular_pmw3610_t modular_pmw3610_read_burst(uint8_t index) {
    report_modular_pmw3610_t out = {0};
    if (powered_down[index]) return out;

    cs_select(index);
    wait_us(PMW3610_T_NCS_SCLK_US);
    modular_pmw3610_serial_write(index, REG_BURST_READ);
    wait_us(35);
    uint8_t motion = modular_pmw3610_serial_read(index);
    uint8_t dx_l   = modular_pmw3610_serial_read(index);
    uint8_t dy_l   = modular_pmw3610_serial_read(index);
    uint8_t xy_h   = modular_pmw3610_serial_read(index);
    cs_deselect(index);
    wait_us(PMW3610_T_SRW_US);

    if ((motion & 0x80) == 0) {
        return out; // no motion
    }

    uint16_t x12 = (uint16_t)(((xy_h << 4) & 0xF00) | dx_l);
    uint16_t y12 = (uint16_t)(((xy_h << 8) & 0xF00) | dy_l);
    out.dx       = sign_extend_12(x12);
    out.dy       = sign_extend_12(y12);
    return out;
}

void modular_pmw3610_set_cpi(uint8_t index, uint16_t cpi) {
    // PMW3610: resolution in 200 CPI steps (1..16 => 200..3200)
    uint16_t rcpi = constrain(cpi, 200, 3200);
    uint8_t  step = (uint8_t)(rcpi / 200);
    if (step == 0) step = 1;
    if (step > 16) step = 16;

    cpi_setting[index] = rcpi;
    if (powered_down[index]) return;

    pmw3610_clock_on(index);
    wait_us(300);
    pmw3610_page_select(index, true); // page-1

    uint8_t val = modular_pmw3610_read_reg(index, REG1_RES_STEP);
    val         = (uint8_t)((val & ~0x1F) | (step & 0x1F));
    modular_pmw3610_write_reg(index, REG1_RES_STEP, val);

    pmw3610_page_select(index, false); // page-0
    pmw3610_clock_off(index);
}

uint16_t modular_pmw3610_get_cpi(uint8_t index) {
    pmw3610_clock_on(index);
    wait_us(300);
    pmw3610_page_select(index, true);
    uint8_t step = (uint8_t)(modular_pmw3610_read_reg(index, REG1_RES_STEP) & 0x1F);
    pmw3610_page_select(index, false);
    pmw3610_clock_off(index);

    if (step == 0) step = 1; // safety
    return (uint16_t)(step * 200);
}

bool modular_pmw3610_check_signature(uint8_t index) {
    uint8_t pid = modular_pmw3610_read_reg(index, REG_PROD_ID);
    uint8_t rid = modular_pmw3610_read_reg(index, REG_REV_ID);
    printf("pid: %d, %d\n", pid, rid);
    return (pid == 0x3E && rid != 0x00);
}

// shut down, no motion can be detected
void modular_pmw3610_power_down(uint8_t index) {
    if (!powered_down[index]) {
        powered_down[index] = true;
        modular_pmw3610_write_reg(index, REG_SHUTDOWN, VAL_SHUTDOWN);
        dprintf("pmw3610 %d powered down\n", index);
    }
}

// define this function elsewhere to customize per-sensor report processing
__attribute__((weak)) report_mouse_t modular_pmw3610_get_report_custom(uint8_t index, report_mouse_t mouse_report) {
    return mouse_report;
}

report_mouse_t modular_pmw3610_get_report(uint8_t index, report_mouse_t mouse_report) {
    if (!connected[index]) return mouse_report;
    if (powered_down[index]) return mouse_report;

    bool motion_gpio_active = modular_pmw3610_check_motion(index);
    if (!motion_gpio_active) {
        return mouse_report;
    }

    report_modular_pmw3610_t data = modular_pmw3610_read_burst(index);
    if (data.dx != 0 || data.dy != 0) {
        // refresh timeout but only for connected sensors
        modular_pmw3610_wake_up(index, true);

        double            rad   = angle_deg[index] * (M_PI / 180.0);
        mouse_xy_report_t x_rev = (mouse_xy_report_t)(cos(rad) * data.dx + -sin(rad) * data.dy);
        mouse_xy_report_t y_rev = (mouse_xy_report_t)(sin(rad) * data.dx + cos(rad) * data.dy);
        mouse_report.x += x_rev;
        mouse_report.y += y_rev;

        //        dprintf("pmw3610 %d: raw(dx=%d,dy=%d) rotated(dx=%d,dy=%d) angle=%d, motion: %lu\n", index, data.dx, data.dy, x_rev, y_rev, angle_deg[index], gpio_read_pin(PMW3610_MOTION_PINS[index]));
    }
    return modular_pmw3610_get_report_custom(index, mouse_report);
}

void modular_pmw3610_set_angle(uint8_t index, uint16_t value) {
    angle_deg[index] = value % 360;
}
void modular_pmw3610_add_angle(uint8_t index, int16_t value) {
    angle_deg[index] = (uint16_t)((360 + angle_deg[index] + value) % 360);
}
uint16_t modular_pmw3610_get_angle(uint8_t index) {
    return angle_deg[index];
}

#endif
