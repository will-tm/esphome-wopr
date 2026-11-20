#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/socket/socket.h"
#include <memory>
#include "esphome/core/hal.h"

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
          SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_8MHZ>
{
  public:
    // Order must match MODES in select.py
    enum Mode : uint8_t {
        MODE_MOVIE,
        MODE_PANEL,
        MODE_LAUNCH_CODE,
        MODE_DEFCON,
        MODE_FILM,
        MODE_BURST,
        MODE_FLICKER,
        MODE_SPECTRUM,
        MODE_SPECTRUM_FINE
    };

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
    void set_interval_range(uint32_t min_ms, uint32_t max_ms)
    {
        this->interval_ms_ = min_ms;
        this->interval_steps_ = max_ms / min_ms;
    }

    // Runtime settings (also driven by the number/select platforms)
    void set_brightness(uint8_t brightness);
    void set_interval(uint32_t interval_ms)
    {
        this->interval_ms_ = interval_ms;
    }
    void set_density(uint8_t percent)
    {
        this->density_ = percent;
    }
    void set_defcon(uint8_t level)
    {
        this->defcon_ = level;
    }
    void set_mode(Mode mode);

    uint8_t get_brightness() const
    {
        return this->brightness_;
    }
    uint32_t get_interval() const
    {
        return this->interval_ms_;
    }
    uint8_t get_density() const
    {
        return this->density_;
    }
    uint8_t get_defcon() const
    {
        return this->defcon_;
    }

  protected:
    uint8_t num_matrices_{12};
    uint8_t brightness_{0};
    uint32_t interval_ms_{333};
    uint32_t interval_steps_{4};
    uint8_t density_{50};
    uint8_t defcon_{5};
    Mode mode_{MODE_MOVIE};
    bool spi_ready_{false};

    uint8_t *display_buffer_;
    uint32_t last_update_{0};
    uint32_t next_interval_{1000};
    bool animation_enabled_{true};

    enum class BootPhase : uint8_t { WAIT_NETWORK, SHOW_MESSAGE, RUNNING };
    BootPhase boot_phase_{BootPhase::WAIT_NETWORK};
    uint32_t phase_start_{0};

    // Panel mode: one toggle deadline per 4x2 lamp
    uint32_t *lamp_deadline_{nullptr};
    // Launch code mode
    uint16_t code_solved_{0};
    uint32_t code_next_lock_{0};
    uint32_t code_hold_until_{0};
    bool defcon_blink_{false};
    // Film mode: steady lamp states and the set of lamps that flash every cycle
    uint8_t *film_base_{nullptr};
    uint8_t *film_flicker_{nullptr};
    bool film_flash_on_{false};
    // Burst mode
    bool burst_active_{false};
    uint32_t burst_phase_end_{0};
    // Spectrum mode: crt-audio's frames (UDP), played out at their capture time plus the network's
    // delay, as bar heights in pixel rows
    static const int SPECTRUM_BANDS = 64;
    static const int SPECTRUM_QUEUE = 16;
    static const int SPECTRUM_DELAYS = 128;
    struct SpectrumFrame {
        uint32_t play_at;
        float level[SPECTRUM_BANDS];
    };
    std::unique_ptr<socket::Socket> spectrum_socket_;
    bool spectrum_opened_{false};
    bool spectrum_streaming_{false};
    uint32_t spectrum_counted_{0};  // frames played since spectrum_count_since_, for the log
    uint32_t spectrum_count_since_{0};
    float spectrum_unweight_[SPECTRUM_BANDS];
    SpectrumFrame spectrum_queue_[SPECTRUM_QUEUE];
    int spectrum_head_{0};
    int spectrum_count_{0};
    float spectrum_delays_[SPECTRUM_DELAYS];
    int spectrum_delay_count_{0};
    int spectrum_delay_next_{0};
    float spectrum_target_[SPECTRUM_BANDS]{};
    float *spectrum_bars_{nullptr};
    float *spectrum_peaks_{nullptr};
    uint32_t *spectrum_peak_at_{nullptr};
    uint32_t spectrum_seen_{0};
    uint32_t spectrum_last_{0};

    void init_display_();
    void write_command_(uint8_t command, uint8_t data);
    void clear_display_();
    bool get_bit_(const uint8_t *buffer, int x, int y) const;
    void put_bit_(uint8_t *buffer, int x, int y, bool value);
    void set_pixel_(int x, int y, bool value);
    void update_display_();
    int width_() const
    {
        return this->num_matrices_ * 8;
    }
    bool random_lit_() const;
    void draw_glyph_(int x, const uint8_t *cols);
    void draw_text_(int x, const char *text);
    int text_x_(const char *text) const;
    void enter_mode_();
    uint32_t render_frame_(uint32_t now, bool &changed);
    void animate_movie_();
    bool animate_panel_(uint32_t now);
    void animate_launch_code_(uint32_t now);
    void animate_defcon_();
    void film_set_flicker_run_(int x, int y, int len, bool flicker);
    void animate_film_();
    uint32_t animate_burst_(uint32_t now, bool &changed);
    void flicker_frame_();
    void spectrum_setup_();
    void spectrum_open_();
    void spectrum_receive_(uint32_t now);
    bool spectrum_parse_(const char *data, size_t len, double &t, float *level) const;
    void animate_spectrum_(uint32_t now, int bars, int bar_w, int gap);
    uint32_t get_random_interval_();
};

}  // namespace wopr_display
}  // namespace esphome
