#include "wopr_display.h"
#include "esphome/components/network/util.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include <cinttypes>
#include <cstring>

namespace esphome {
namespace wopr_display {

static const char *const TAG = "wopr_display";

// Matches esp-wopr: wait up to this long for the network before showing "OFFLINE"
static const uint32_t NETWORK_WAIT_MS = 15000;
static const uint32_t BOOT_MESSAGE_MS = 2000;

// Panel mode: each lamp is a 4x2 pixel block
static const int LAMP_W = 4;
static const int LAMP_H = 2;
static const uint32_t PANEL_TICK_MS = 20;

// Launch code mode: the code WOPR cracks at the end of the film
static const char *const LAUNCH_CODE = "CPE1704TKS";
static const uint16_t LAUNCH_CODE_ALL = (1 << 10) - 1;
static const uint32_t LAUNCH_CHURN_MS = 80;
static const uint32_t LAUNCH_HOLD_MS = 5000;

// Film mode, measured from the film's lamp-panel footage (125 lamps, 24 fps):
// lamps update on a ~104 ms clock, alternating a "flash on" and a "flash off" tick.
// Per second, ~35% of lamps flash in horizontal runs (lit for one tick in most cycles),
// ~30% change state once or twice, and ~35% never change. Density 75% matches the
// footage's lit levels (~72% on flash ticks, ~48% between).
static const uint8_t FILM_FLICKER_PERCENT = 35;
static const uint8_t FILM_FLASH_PERCENT = 70;
static const uint8_t FILM_REROLL_PERCENT = 35;
static const uint8_t FILM_MIGRATE_PERCENT = 10;

// 5x7 font, column-major, bit 0 = top row: space, 0-9, A-Z
static const uint8_t FONT[37][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31}, {0x18, 0x14, 0x12, 0x7F, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E}, {0x7E, 0x11, 0x11, 0x11, 0x7E},
    {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22}, {0x7F, 0x41, 0x41, 0x22, 0x1C},
    {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01}, {0x3E, 0x41, 0x49, 0x49, 0x7A},
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3F, 0x01},
    {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40}, {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E}, {0x7F, 0x09, 0x09, 0x09, 0x06},
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31},
    {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F}, {0x1F, 0x20, 0x40, 0x20, 0x1F},
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07},
    {0x61, 0x51, 0x49, 0x45, 0x43},
};

static const uint8_t *glyph_for(char c) {
    if (c >= '0' && c <= '9') {
        return FONT[1 + c - '0'];
    }
    if (c >= 'A' && c <= 'Z') {
        return FONT[11 + c - 'A'];
    }
    if (c >= 'a' && c <= 'z') {
        return FONT[11 + c - 'a'];
    }
    return FONT[0];
}

void WOPRDisplay::setup() {
    ESP_LOGCONFIG(TAG, "Setting up WOPR Display...");

    // Allocate display buffer (8 rows * num_matrices)
    this->display_buffer_ = new uint8_t[8 * this->num_matrices_]();
    this->lamp_deadline_ = new uint32_t[(this->width_() / LAMP_W) * (8 / LAMP_H)]();
    this->film_base_ = new uint8_t[8 * this->num_matrices_]();
    this->film_flicker_ = new uint8_t[8 * this->num_matrices_]();

    // Initialize SPI and display
    this->spi_setup();
    this->init_display_();
    this->spi_ready_ = true;

    // Restore state according to restore_mode
    this->animation_enabled_ = this->get_initial_state_with_restore_mode().value_or(true);
    this->publish_state(this->animation_enabled_);
    this->phase_start_ = millis();

    ESP_LOGCONFIG(TAG, "WOPR Display setup complete");
}

void WOPRDisplay::loop() {
    uint32_t now = millis();

    // Boot sequence: wait for network, show "WIFI OK" / "OFFLINE" for 2 s, then animate
    if (this->boot_phase_ == BootPhase::WAIT_NETWORK) {
        bool connected = network::is_connected();
        if (!connected && now - this->phase_start_ < NETWORK_WAIT_MS) {
            return;
        }
        const char *message = connected ? "WIFI OK" : "OFFLINE";
        this->clear_display_();
        this->draw_text_(0, message);
        this->update_display_();
        this->boot_phase_ = BootPhase::SHOW_MESSAGE;
        this->phase_start_ = now;
        return;
    }
    if (this->boot_phase_ == BootPhase::SHOW_MESSAGE) {
        if (now - this->phase_start_ < BOOT_MESSAGE_MS) {
            return;
        }
        this->boot_phase_ = BootPhase::RUNNING;
        this->enter_mode_();
        if (!this->animation_enabled_) {
            this->clear_display_();
            this->update_display_();
        }
    }

    if (!this->animation_enabled_ || now - this->last_update_ < this->next_interval_) {
        return;
    }
    this->last_update_ = now;
    bool changed = false;
    this->next_interval_ = this->render_frame_(now, changed);
    if (changed) {
        this->update_display_();
    }
}

void WOPRDisplay::dump_config() {
    ESP_LOGCONFIG(TAG, "WOPR Display:");
    ESP_LOGCONFIG(TAG, "  Number of Matrices: %d", this->num_matrices_);
    ESP_LOGCONFIG(TAG, "  Brightness: %d", this->brightness_);
    ESP_LOGCONFIG(TAG, "  Interval: %" PRIu32 " ms x 1-%" PRIu32, this->interval_ms_, this->interval_steps_);
    ESP_LOGCONFIG(TAG, "  Density: %d%%", this->density_);
    ESP_LOGCONFIG(TAG, "  Mode: %d", this->mode_);
    ESP_LOGCONFIG(TAG, "  Display Size: %dx8 pixels", this->num_matrices_ * 8);
}

void WOPRDisplay::write_state(bool state) {
    this->animation_enabled_ = state;
    this->publish_state(state);

    ESP_LOGD(TAG, "WOPR Display %s", state ? "enabled" : "disabled");

    if (state) {
        if (this->boot_phase_ == BootPhase::RUNNING) {
            this->enter_mode_();
        }
    } else {
        this->clear_display_();
        this->update_display_();
    }
}

void WOPRDisplay::set_brightness(uint8_t brightness) {
    this->brightness_ = brightness;
    if (this->spi_ready_) {
        this->write_command_(MAX7219_INTENSITY, brightness);
    }
}

void WOPRDisplay::set_mode(Mode mode) {
    this->mode_ = mode;
    ESP_LOGD(TAG, "Mode: %d", mode);
    if (this->boot_phase_ == BootPhase::RUNNING && this->animation_enabled_) {
        this->enter_mode_();
    }
}

void WOPRDisplay::init_display_() {
    // Initialize all MAX7219 chips
    this->write_command_(MAX7219_SHUTDOWN, 0);                  // Shutdown mode
    this->write_command_(MAX7219_DISPLAYTEST, 0);               // Normal operation
    this->write_command_(MAX7219_SCANLIMIT, 7);                 // Scan all 8 digits
    this->write_command_(MAX7219_DECODEMODE, 0);                // No decode mode
    this->write_command_(MAX7219_INTENSITY, this->brightness_); // Set brightness
    this->write_command_(MAX7219_SHUTDOWN, 1);                  // Normal operation

    // Clear display
    this->clear_display_();
    this->update_display_();

    ESP_LOGD(TAG, "MAX7219 display initialized");
}

void WOPRDisplay::write_command_(uint8_t command, uint8_t data) {
    this->enable();

    // Send command and data to all matrices in the chain
    for (int i = 0; i < this->num_matrices_; i++) {
        this->write_byte(command);
        this->write_byte(data);
    }

    this->disable();
}

void WOPRDisplay::clear_display_() {
    memset(this->display_buffer_, 0, 8 * this->num_matrices_);
}

bool WOPRDisplay::get_bit_(const uint8_t *buffer, int x, int y) const {
    if (x < 0 || x >= this->width_() || y < 0 || y >= 8) {
        return false;
    }
    return buffer[y * this->num_matrices_ + x / 8] & (1 << (7 - x % 8));
}

void WOPRDisplay::put_bit_(uint8_t *buffer, int x, int y, bool value) {
    if (x < 0 || x >= this->width_() || y < 0 || y >= 8) {
        return;
    }

    int idx = y * this->num_matrices_ + x / 8;
    uint8_t mask = 1 << (7 - x % 8);

    if (value) {
        buffer[idx] |= mask;
    } else {
        buffer[idx] &= ~mask;
    }
}

void WOPRDisplay::set_pixel_(int x, int y, bool value) {
    this->put_bit_(this->display_buffer_, x, y, value);
}

void WOPRDisplay::update_display_() {
    for (int row = 0; row < 8; row++) {
        this->enable();

        // Send row data to all matrices
        for (int matrix = 0; matrix < this->num_matrices_; matrix++) {
            this->write_byte(MAX7219_DIGIT0 + row);
            this->write_byte(this->display_buffer_[row * this->num_matrices_ + matrix]);
        }

        this->disable();
    }
}

bool WOPRDisplay::random_lit_() const {
    return random_uint32() % 100 < this->density_;
}

void WOPRDisplay::draw_glyph_(int x, const uint8_t *cols) {
    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            this->set_pixel_(x + col, row, cols[col] & (1 << row));
        }
    }
}

void WOPRDisplay::draw_text_(int x, const char *text) {
    for (; *text; text++, x += 6) {
        this->draw_glyph_(x, glyph_for(*text));
    }
}

int WOPRDisplay::text_x_(const char *text) const {
    int text_width = strlen(text) * 6 - 1;
    return text_width < this->width_() ? (this->width_() - text_width) / 2 : 0;
}

void WOPRDisplay::enter_mode_() {
    uint32_t now = millis();
    this->clear_display_();

    int lamps = (this->width_() / LAMP_W) * (8 / LAMP_H);
    for (int i = 0; i < lamps; i++) {
        // Stagger lamps so they don't all toggle on the same tick
        this->lamp_deadline_[i] = now + random_uint32() % (this->interval_ms_ * this->interval_steps_);
    }

    this->code_solved_ = 0;
    this->code_next_lock_ = now + this->get_random_interval_() * 2;
    this->code_hold_until_ = 0;

    // Film mode: carve each row into 2-4 lamp runs, ~35% of which flicker; the rest hold a steady state
    memset(this->film_flicker_, 0, 8 * this->num_matrices_);
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < this->width_();) {
            int len = 2 + random_uint32() % 3;
            this->film_set_flicker_run_(x, y, len, random_uint32() % 100 < FILM_FLICKER_PERCENT);
            x += len;
        }
    }
    this->film_flash_on_ = false;

    this->next_interval_ = 0;  // render first frame on the next loop
}

uint32_t WOPRDisplay::render_frame_(uint32_t now, bool &changed) {
    switch (this->mode_) {
        case MODE_PANEL:
            changed = this->animate_panel_(now);
            return PANEL_TICK_MS;
        case MODE_LAUNCH_CODE:
            this->animate_launch_code_(now);
            changed = true;
            return LAUNCH_CHURN_MS;
        case MODE_DEFCON:
            this->animate_defcon_();
            changed = true;
            // DEFCON 5 flickers at ~1.7x the interval, DEFCON 1 at ~0.3x
            return this->interval_ms_ * this->defcon_ / 3;
        case MODE_FILM:
            this->animate_film_();
            changed = true;
            // Two ticks per cycle; 333 ms / 3 = 111 ms, close to the measured ~104 ms
            return this->interval_ms_ / 3;
        case MODE_MOVIE:
        default:
            this->animate_movie_();
            changed = true;
            return this->get_random_interval_();
    }
}

void WOPRDisplay::animate_movie_() {
    // Each pixel has a 50% chance to be re-rolled; re-rolled pixels light up with `density` probability
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < this->width_(); x++) {
            if (random_uint32() % 2 == 0) {
                this->set_pixel_(x, y, this->random_lit_());
            }
        }
    }
}

bool WOPRDisplay::animate_panel_(uint32_t now) {
    bool changed = false;
    int lamps_x = this->width_() / LAMP_W;
    int lamps = lamps_x * (8 / LAMP_H);
    for (int i = 0; i < lamps; i++) {
        if ((int32_t) (now - this->lamp_deadline_[i]) < 0) {
            continue;
        }
        bool lit = this->random_lit_();
        int x0 = (i % lamps_x) * LAMP_W;
        int y0 = (i / lamps_x) * LAMP_H;
        for (int y = y0; y < y0 + LAMP_H; y++) {
            for (int x = x0; x < x0 + LAMP_W; x++) {
                this->set_pixel_(x, y, lit);
            }
        }
        this->lamp_deadline_[i] = now + this->get_random_interval_();
        changed = true;
    }
    return changed;
}

void WOPRDisplay::animate_launch_code_(uint32_t now) {
    if (this->code_solved_ == LAUNCH_CODE_ALL) {
        if ((int32_t) (now - this->code_hold_until_) >= 0) {
            this->enter_mode_();
        }
    } else if ((int32_t) (now - this->code_next_lock_) >= 0) {
        // Lock a random unsolved character, like the film
        int unsolved[10];
        int count = 0;
        for (int i = 0; i < 10; i++) {
            if (!(this->code_solved_ & (1 << i))) {
                unsolved[count++] = i;
            }
        }
        this->code_solved_ |= 1 << unsolved[random_uint32() % count];
        this->code_next_lock_ = now + this->get_random_interval_() * 2;
        if (this->code_solved_ == LAUNCH_CODE_ALL) {
            this->code_hold_until_ = now + LAUNCH_HOLD_MS;
        }
    }

    int x = this->text_x_(LAUNCH_CODE);
    for (int i = 0; i < 10; i++, x += 6) {
        bool solved = this->code_solved_ & (1 << i);
        this->draw_glyph_(x, solved ? glyph_for(LAUNCH_CODE[i]) : FONT[1 + random_uint32() % 36]);
    }
}

void WOPRDisplay::animate_defcon_() {
    char text[] = "DEFCON 0";
    text[7] = '0' + this->defcon_;
    int x0 = this->text_x_(text);
    int x1 = x0 + strlen(text) * 6 - 1;

    // Flicker everywhere except a one-pixel margin around the text
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < this->width_(); x++) {
            this->set_pixel_(x, y, (x < x0 - 1 || x > x1) && this->random_lit_());
        }
    }

    // DEFCON 1: the text itself blinks
    this->defcon_blink_ = !this->defcon_blink_;
    if (this->defcon_ > 1 || this->defcon_blink_) {
        this->draw_text_(x0, text);
    }
}

void WOPRDisplay::film_set_flicker_run_(int x, int y, int len, bool flicker) {
    for (int i = x; i < x + len; i++) {
        this->put_bit_(this->film_flicker_, i, y, flicker);
        this->put_bit_(this->film_base_, i, y, !flicker && this->random_lit_());
    }
}

void WOPRDisplay::animate_film_() {
    this->film_flash_on_ = !this->film_flash_on_;

    if (this->film_flash_on_) {
        // Occasionally move a run between the steady and flicker sets so the pattern drifts
        if (random_uint32() % 100 < FILM_MIGRATE_PERCENT) {
            int x = random_uint32() % this->width_();
            int y = random_uint32() % 8;
            this->film_set_flicker_run_(x, y, 2 + random_uint32() % 3, !this->get_bit_(this->film_flicker_, x, y));
        }
    }

    for (int y = 0; y < 8; y++) {
        bool run_lit = false;
        for (int x = 0; x < this->width_(); x++) {
            if (this->get_bit_(this->film_flicker_, x, y)) {
                // Neighbouring flicker lamps tend to flash together
                if (!this->get_bit_(this->film_flicker_, x - 1, y) || random_uint32() % 100 < 30) {
                    run_lit = random_uint32() % 100 < FILM_FLASH_PERCENT;
                }
                this->set_pixel_(x, y, this->film_flash_on_ && run_lit);
            } else {
                if (this->film_flash_on_ && random_uint32() % 100 < FILM_REROLL_PERCENT) {
                    this->put_bit_(this->film_base_, x, y, this->random_lit_());
                }
                this->set_pixel_(x, y, this->get_bit_(this->film_base_, x, y));
            }
        }
    }
}

uint32_t WOPRDisplay::get_random_interval_() {
    // Random multiple of the interval: 333/666/999/1332 ms by default
    return this->interval_ms_ * (1 + random_uint32() % this->interval_steps_);
}

}  // namespace wopr_display
}  // namespace esphome