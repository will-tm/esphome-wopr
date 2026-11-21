#include "wopr_display.h"
#include "esphome/components/network/util.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

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

// Burst mode, ported from jebbett/WOPR (WOPR.ino): flicker random pixels at 45 fps,
// then freeze the frame. Flicker lasts 1-4x the interval (333-1332 ms at the default),
// the freeze a uniform 0.5-1.5x the interval (166-500 ms).
// The original changes 20 pixels per frame on a 40x8 display; scale that to the panel width.
static const uint32_t BURST_FRAME_MS = 1000 / 45;
static const int BURST_PIXELS_PER_TOGGLE = (40 * 8) / 20;

// Spectrum mode: crt-audio (philips-crt-80/macos/crt-audio) sends {"t", "spectrum": [64 values],
// "spectrum96": [96 values], "loudness", "beat"?} on UDP 8931, 60 times a second: Music Assistant's
// analysis, A-weighted mel bands from 40 Hz to 16 kHz, 0..1 for -60..0 dB. The 96 bands are used: one
// per column, or three per bar in the 32-bar mode. Drawn as the CRT and RGB matrix draw them: 40% of
// the A-weighting given back (the bass), bars over -54..-18 dB, 2 px wide with a 1 px gap.
static const uint16_t SPECTRUM_PORT = 8931;
static const float SPECTRUM_F_MIN = 40, SPECTRUM_F_MAX = 16000;
static const float SPECTRUM_UNWEIGHT_SHARE = 0.4f;
static const float SPECTRUM_DB_FLOOR = -54, SPECTRUM_DB_TOP = -18;
static const uint32_t SPECTRUM_FRAME_MS = 16;
static const uint32_t SPECTRUM_BUFFER_MS = 60;  // covers the Wi-Fi jitter (15-50 ms measured on the Pis)
static const uint32_t SPECTRUM_STALE_MS = 200;  // no frame for this long: the music stopped, bars fall
static const float SPECTRUM_FALL_PER_S = 1.8f;  // bars fall at most this much of the height per second
static const uint32_t SPECTRUM_PEAK_HOLD_MS = 250;
static const float SPECTRUM_PEAK_FALL_PER_S = 3.0f;

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
    this->spectrum_setup_();

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
    this->spectrum_receive_(now);

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
    // A row to all matrices in one SPI write (register, data per matrix): far less overhead than a
    // write per byte, for the spectrum modes' 60 frames a second
    uint8_t row_data[2 * 32];
    for (int row = 0; row < 8; row++) {
        for (int matrix = 0; matrix < this->num_matrices_; matrix++) {
            row_data[2 * matrix] = MAX7219_DIGIT0 + row;
            row_data[2 * matrix + 1] = this->display_buffer_[row * this->num_matrices_ + matrix];
        }
        this->enable();
        this->write_array(row_data, 2 * this->num_matrices_);
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

    this->burst_active_ = true;
    this->burst_phase_end_ = now + this->get_random_interval_();

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
        case MODE_BURST:
            return this->animate_burst_(now, changed);
        case MODE_FLICKER:
            // Burst without the freeze
            this->flicker_frame_();
            changed = true;
            return BURST_FRAME_MS;
        case MODE_SPECTRUM:
            // 2 px bars with a 1 px gap across the panel
            this->animate_spectrum_(now, this->width_() / 3, 2, 1, true);
            changed = true;
            return SPECTRUM_FRAME_MS;
        case MODE_SPECTRUM_FINE:
            // One 1 px bar per band across the panel, without peak dots
            this->animate_spectrum_(now, SPECTRUM_BANDS, 1, 0, false);
            changed = true;
            return SPECTRUM_FRAME_MS;
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

uint32_t WOPRDisplay::animate_burst_(uint32_t now, bool &changed) {
    if ((int32_t) (now - this->burst_phase_end_) >= 0) {
        this->burst_active_ = !this->burst_active_;
        this->burst_phase_end_ = now + (this->burst_active_ ? this->get_random_interval_()
                                                            : this->interval_ms_ / 2 + random_uint32() % (this->interval_ms_ + 1));
    }
    if (!this->burst_active_) {
        // Frozen frame: sleep until the next burst
        return this->burst_phase_end_ - now;
    }

    this->flicker_frame_();
    changed = true;
    return BURST_FRAME_MS;
}

void WOPRDisplay::flicker_frame_() {
    int toggles = (this->width_() * 8) / BURST_PIXELS_PER_TOGGLE;
    for (int i = 0; i < toggles; i++) {
        this->set_pixel_(random_uint32() % this->width_(), random_uint32() % 8, this->random_lit_());
    }
}

static float a_weighting_db(float f) {
    double f2 = std::max(f, 1.0f) * (double) std::max(f, 1.0f);
    double ra = 12194.0 * 12194.0 * f2 * f2 /
                ((f2 + 20.6 * 20.6) * std::sqrt((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)) * (f2 + 12194.0 * 12194.0));
    return std::min(std::max(2.0 + 20.0 * std::log10(std::max(ra, 1e-12)), -50.0), 6.0);
}

void WOPRDisplay::spectrum_setup_() {
    // Per band, SPECTRUM_UNWEIGHT_SHARE of its A-weighting at the middle of the band (in mel), as a
    // 0..1 offset (1 = 60 dB), as sendspin-bridge computes it.
    auto mel = [](float f) { return 2595.0f * std::log10(1.0f + f / 700.0f); };
    float lo = mel(SPECTRUM_F_MIN), hi = mel(SPECTRUM_F_MAX);
    for (int b = 0; b < SPECTRUM_BANDS; b++) {
        float m = lo + (hi - lo) * (b + 0.5f) / SPECTRUM_BANDS;
        float f = 700.0f * (std::pow(10.0f, m / 2595.0f) - 1.0f);
        this->spectrum_unweight_[b] = SPECTRUM_UNWEIGHT_SHARE * a_weighting_db(f) / 60.0f;
    }
    int bars = std::max(SPECTRUM_BANDS, this->width_() / 3);  // the most either layout draws
    this->spectrum_bars_ = new float[bars]();
    this->spectrum_peaks_ = new float[bars]();
    this->spectrum_peak_at_ = new uint32_t[bars]();
}

void WOPRDisplay::spectrum_open_() {
    // Opened once the network is up: setup() runs before the network stack starts
    this->spectrum_opened_ = true;
    this->spectrum_socket_ = socket::socket_ip(SOCK_DGRAM, IPPROTO_IP);
    if (this->spectrum_socket_ == nullptr || this->spectrum_socket_->setblocking(false) != 0) {
        ESP_LOGW(TAG, "Spectrum: no UDP socket");
        this->spectrum_socket_ = nullptr;
        return;
    }
    struct sockaddr_storage address;
    socklen_t length = socket::set_sockaddr_any((struct sockaddr *) &address, sizeof(address), SPECTRUM_PORT);
    if (length == 0 || this->spectrum_socket_->bind((struct sockaddr *) &address, length) != 0) {
        ESP_LOGW(TAG, "Spectrum: cannot bind UDP %u", SPECTRUM_PORT);
        this->spectrum_socket_ = nullptr;
        return;
    }
    ESP_LOGI(TAG, "Spectrum: listening on UDP %u", SPECTRUM_PORT);
}

bool WOPRDisplay::spectrum_parse_(const char *data, size_t len, double &t, float *level) const {
    // The datagram's JSON, read field by field: {"t":123.4567,"spectrum":[0.123,...],...}
    std::string text(data, len);
    size_t at = text.find("\"t\":");
    size_t spectrum = text.find("\"spectrum96\":[");
    if (at == std::string::npos || spectrum == std::string::npos) {
        return false;
    }
    t = strtod(text.c_str() + at + 4, nullptr);
    const char *p = text.c_str() + spectrum + 14;
    for (int b = 0; b < SPECTRUM_BANDS; b++) {
        char *end;
        float v = strtof(p, &end);
        if (end == p) {
            return false;
        }
        // Undo part of the A-weighting; a band at the floor stays 0
        level[b] = v > 0 ? std::min(1.0f, std::max(0.0f, v - this->spectrum_unweight_[b])) : 0.0f;
        p = end;
        while (*p == ',' || *p == ' ') {
            p++;
        }
    }
    return true;
}

void WOPRDisplay::spectrum_receive_(uint32_t now) {
    if (!this->spectrum_opened_ && network::is_connected()) {
        this->spectrum_open_();
    }
    if (this->spectrum_socket_ == nullptr) {
        return;
    }
    char buf[1500];  // a datagram is ~900 bytes
    bool listening = (this->mode_ == MODE_SPECTRUM || this->mode_ == MODE_SPECTRUM_FINE) && this->animation_enabled_;
    for (;;) {
        ssize_t len = this->spectrum_socket_->read(buf, sizeof(buf));
        if (len <= 0) {
            return;
        }
        double t;
        SpectrumFrame frame;
        if (!listening || !this->spectrum_parse_(buf, len, t, frame.level)) {
            continue;
        }
        // Play out at the capture time `t` (crt-audio's clock, seconds) plus the quickest delivery
        // seen over the last SPECTRUM_DELAYS frames plus SPECTRUM_BUFFER_MS, so Wi-Fi jitter doesn't
        // bunch the frames. A jump of the delay (crt-audio restarted) starts over.
        float delay = (float) ((double) now - t * 1000.0);
        if (this->spectrum_delay_count_ > 0) {
            int last = (this->spectrum_delay_next_ + SPECTRUM_DELAYS - 1) % SPECTRUM_DELAYS;
            if (std::fabs(delay - this->spectrum_delays_[last]) > 5000) {
                this->spectrum_delay_count_ = 0;
                this->spectrum_count_ = 0;
            }
        }
        this->spectrum_delays_[this->spectrum_delay_next_] = delay;
        this->spectrum_delay_next_ = (this->spectrum_delay_next_ + 1) % SPECTRUM_DELAYS;
        this->spectrum_delay_count_ = std::min(this->spectrum_delay_count_ + 1, SPECTRUM_DELAYS);
        float base = delay;
        for (int i = 0; i < this->spectrum_delay_count_; i++) {
            base = std::min(base, this->spectrum_delays_[i]);
        }
        frame.play_at = (uint32_t) (t * 1000.0 + base) + SPECTRUM_BUFFER_MS;
        if (this->spectrum_count_ == SPECTRUM_QUEUE) {  // full: drop the oldest
            this->spectrum_head_ = (this->spectrum_head_ + 1) % SPECTRUM_QUEUE;
            this->spectrum_count_--;
        }
        this->spectrum_queue_[(this->spectrum_head_ + this->spectrum_count_) % SPECTRUM_QUEUE] = frame;
        this->spectrum_count_++;
    }
}

void WOPRDisplay::animate_spectrum_(uint32_t now, int bars, int bar_w, int gap, bool peaks) {
    // The frames that are due; the latest sets the targets
    while (this->spectrum_count_ > 0 &&
           (int32_t) (now - this->spectrum_queue_[this->spectrum_head_].play_at) >= 0) {
        memcpy(this->spectrum_target_, this->spectrum_queue_[this->spectrum_head_].level, sizeof(this->spectrum_target_));
        this->spectrum_head_ = (this->spectrum_head_ + 1) % SPECTRUM_QUEUE;
        this->spectrum_count_--;
        if (!this->spectrum_streaming_) {
            ESP_LOGI(TAG, "Spectrum: frames arriving");
            this->spectrum_streaming_ = true;
            this->spectrum_counted_ = 0;
            this->spectrum_count_since_ = now;
        }
        if (++this->spectrum_counted_, now - this->spectrum_count_since_ >= 30000) {
            ESP_LOGD(TAG, "Spectrum: %.1f frames/s", this->spectrum_counted_ * 1000.0f / (now - this->spectrum_count_since_));
            this->spectrum_counted_ = 0;
            this->spectrum_count_since_ = now;
        }
        this->spectrum_seen_ = now;
    }
    if (now - this->spectrum_seen_ > SPECTRUM_STALE_MS) {
        memset(this->spectrum_target_, 0, sizeof(this->spectrum_target_));
        if (this->spectrum_streaming_) {
            ESP_LOGI(TAG, "Spectrum: frames stopped");
            this->spectrum_streaming_ = false;
        }
    }
    float dt = std::min((now - this->spectrum_last_) / 1000.0f, 0.2f);
    this->spectrum_last_ = now;

    this->clear_display_();
    int pitch = bar_w + gap;
    int x0 = std::max(0, (this->width_() - (bars * pitch - gap)) / 2);
    for (int i = 0; i < bars; i++) {
        // Each bar: the average of its bands, scaled over SPECTRUM_DB_FLOOR..SPECTRUM_DB_TOP
        int b0 = i * SPECTRUM_BANDS / bars, b1 = std::max((i + 1) * SPECTRUM_BANDS / bars, b0 + 1);
        float sum = 0;
        for (int b = b0; b < b1; b++) {
            sum += this->spectrum_target_[b];
        }
        float db = sum / (b1 - b0) * 60 - 60;
        float target = std::min(1.0f, std::max(0.0f, (db - SPECTRUM_DB_FLOOR) / (SPECTRUM_DB_TOP - SPECTRUM_DB_FLOOR)));

        float &bar = this->spectrum_bars_[i];
        bar = std::max(target, bar - SPECTRUM_FALL_PER_S * dt);
        float &peak = this->spectrum_peaks_[i];
        if (bar >= peak) {
            peak = bar;
            this->spectrum_peak_at_[i] = now;
        } else if (now - this->spectrum_peak_at_[i] > SPECTRUM_PEAK_HOLD_MS) {
            peak = std::max(bar, peak - SPECTRUM_PEAK_FALL_PER_S * dt);
        }

        int height = (int) std::lround(bar * 8);
        int peak_row = (int) std::lround(peak * 8);
        for (int x = x0 + i * pitch; x < x0 + i * pitch + bar_w; x++) {
            for (int h = 0; h < height; h++) {
                this->set_pixel_(x, 7 - h, true);
            }
            if (peaks && peak_row > height) {
                this->set_pixel_(x, 8 - peak_row, true);
            }
        }
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