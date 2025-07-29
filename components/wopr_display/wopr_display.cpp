#include "wopr_display.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <cstdlib>

namespace esphome {
namespace wopr_display {

static const char *const TAG = "wopr_display";

void WOPRDisplay::setup() {
    ESP_LOGCONFIG(TAG, "Setting up WOPR Display...");

    // Allocate display buffer (8 rows * num_matrices)
    this->display_buffer_ = new uint8_t[8 * this->num_matrices_]();

    // Initialize SPI and display
    this->spi_setup();
    this->init_display_();

    // Restore state from preferences
    this->pref_ = global_preferences->make_preference<bool>(this->get_object_id_hash());
    bool restored_state;
    if (this->pref_.load(&restored_state)) {
        this->animation_enabled_ = restored_state;
        this->publish_state(restored_state);
        ESP_LOGD(TAG, "Restored state: %s", restored_state ? "ON" : "OFF");
    } else {
        this->animation_enabled_ = true;
        this->publish_state(true);
        ESP_LOGD(TAG, "No saved state, defaulting to ON");
    }

    ESP_LOGCONFIG(TAG, "WOPR Display setup complete");
}

void WOPRDisplay::loop() {
    uint32_t now = millis();

    // Check if it's time for the next animation frame
    if (now - this->last_update_ >= this->next_interval_) {
        this->animate_wopr_();
        this->update_display_();

        this->last_update_ = now;
        this->next_interval_ = this->get_random_interval_();
    }
}

void WOPRDisplay::dump_config() {
    ESP_LOGCONFIG(TAG, "WOPR Display:");
    ESP_LOGCONFIG(TAG, "  Number of Matrices: %d", this->num_matrices_);
    ESP_LOGCONFIG(TAG, "  Brightness: %d", this->brightness_);
    ESP_LOGCONFIG(TAG, "  Interval Range: %d-%d ms", this->min_interval_ms_, this->max_interval_ms_);
    ESP_LOGCONFIG(TAG, "  Display Size: %dx8 pixels", this->num_matrices_ * 8);
}

void WOPRDisplay::write_state(bool state) {
    this->animation_enabled_ = state;
    this->publish_state(state);

    // Save state to preferences
    this->pref_.save(&state);

    ESP_LOGD(TAG, "WOPR Display %s", state ? "enabled" : "disabled");

    // If turning off, clear the display immediately
    if (!state) {
        this->clear_display_();
        this->update_display_();
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

void WOPRDisplay::set_pixel_(int x, int y, bool value) {
    if (x < 0 || x >= this->num_matrices_ * 8 || y < 0 || y >= 8) {
        return;
    }

    int matrix = x / 8;
    int bit = x % 8;
    int idx = y * this->num_matrices_ + matrix;

    if (value) {
        this->display_buffer_[idx] |= (1 << (7 - bit));
    } else {
        this->display_buffer_[idx] &= ~(1 << (7 - bit));
    }
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

void WOPRDisplay::animate_wopr_() {
    if (!this->animation_enabled_) {
        // If disabled, clear the display
        this->clear_display_();
        return;
    }

    // Generate random WOPR-style animation
    // This mimics the iconic random light patterns from the movie
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < this->num_matrices_ * 8; x++) {
            // Random chance to flip pixel state
            if (random() % 2 == 0) {
                bool new_state = random() % 2;
                this->set_pixel_(x, y, new_state);
            }
        }
    }
}

uint32_t WOPRDisplay::get_random_interval_() {
    // Return random interval between min and max (in milliseconds)
    uint32_t range = this->max_interval_ms_ - this->min_interval_ms_;
    return this->min_interval_ms_ + (random() % (range + 1));
}

}  // namespace wopr_display
}  // namespace esphome