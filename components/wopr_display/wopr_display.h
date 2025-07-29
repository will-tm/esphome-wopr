#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/spi/spi.h"
#include "esphome/core/hal.h"
#include "esphome/core/preferences.h"

namespace esphome
{
namespace wopr_display
{

// MAX7219 commands
static const uint8_t MAX7219_NOOP = 0x00;
static const uint8_t MAX7219_DIGIT0 = 0x01;
static const uint8_t MAX7219_DECODEMODE = 0x09;
static const uint8_t MAX7219_INTENSITY = 0x0A;
static const uint8_t MAX7219_SCANLIMIT = 0x0B;
static const uint8_t MAX7219_SHUTDOWN = 0x0C;
static const uint8_t MAX7219_DISPLAYTEST = 0x0F;

class WOPRDisplay
    : public switch_::Switch,
      public Component,
      public spi::
          SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_200KHZ>
{
  public:
    void setup() override;
    void loop() override;
    void dump_config() override;

    // Switch interface
    void write_state(bool state) override;

    // Configuration setters
    void set_num_matrices(uint8_t num_matrices)
    {
        this->num_matrices_ = num_matrices;
    }
    void set_brightness(uint8_t brightness)
    {
        this->brightness_ = brightness;
    }
    void set_interval_range(uint32_t min_ms, uint32_t max_ms)
    {
        this->min_interval_ms_ = min_ms;
        this->max_interval_ms_ = max_ms;
    }

  protected:
    uint8_t num_matrices_{12};
    uint8_t brightness_{0};
    uint32_t min_interval_ms_{333};
    uint32_t max_interval_ms_{1332};

    uint8_t *display_buffer_;
    uint32_t last_update_{0};
    uint32_t next_interval_{1000};
    bool animation_enabled_{true};

    ESPPreferenceObject pref_;

    void init_display_();
    void write_command_(uint8_t command, uint8_t data);
    void clear_display_();
    void set_pixel_(int x, int y, bool value);
    void update_display_();
    void animate_wopr_();
    uint32_t get_random_interval_();
};

}  // namespace wopr_display
}  // namespace esphome
