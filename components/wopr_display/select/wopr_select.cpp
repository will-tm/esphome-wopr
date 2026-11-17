#include "esphome/core/defines.h"
#ifdef USE_SELECT
#include "wopr_select.h"

namespace esphome {
namespace wopr_display {

void WOPRModeSelect::setup() {
    this->pref_ = this->make_entity_preference<uint8_t>();
    uint8_t index;
    if (!this->pref_.load(&index) || !this->has_index(index)) {
        index = WOPRDisplay::MODE_MOVIE;
    }
    this->control(index);
}

void WOPRModeSelect::control(size_t index) {
    this->parent_->set_mode(static_cast<WOPRDisplay::Mode>(index));
    uint8_t stored = index;
    this->pref_.save(&stored);
    this->publish_state(index);
}

}  // namespace wopr_display
}  // namespace esphome
#endif  // USE_SELECT
