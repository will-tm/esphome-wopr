#include "esphome/core/defines.h"
#ifdef USE_NUMBER
#include "wopr_number.h"

namespace esphome {
namespace wopr_display {

void WOPRNumber::setup() {
    this->pref_ = this->make_entity_preference<float>();
    float value;
    if (!this->pref_.load(&value)) {
        // No saved value: start from the YAML-configured default on the display
        switch (this->type_) {
            case BRIGHTNESS:
                value = this->parent_->get_brightness();
                break;
            case INTERVAL:
                value = this->parent_->get_interval();
                break;
            case DENSITY:
                value = this->parent_->get_density();
                break;
            case DEFCON:
                value = this->parent_->get_defcon();
                break;
        }
    }
    this->control(value);
}

void WOPRNumber::control(float value) {
    switch (this->type_) {
        case BRIGHTNESS:
            this->parent_->set_brightness(value);
            break;
        case INTERVAL:
            this->parent_->set_interval(value);
            break;
        case DENSITY:
            this->parent_->set_density(value);
            break;
        case DEFCON:
            this->parent_->set_defcon(value);
            break;
    }
    this->pref_.save(&value);
    this->publish_state(value);
}

}  // namespace wopr_display
}  // namespace esphome
#endif  // USE_NUMBER
