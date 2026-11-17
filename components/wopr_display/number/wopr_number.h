#pragma once

#include "esphome/core/defines.h"
#ifdef USE_NUMBER

#include "esphome/components/number/number.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "../wopr_display.h"

namespace esphome
{
namespace wopr_display
{

class WOPRNumber : public number::Number, public Component, public Parented<WOPRDisplay>
{
  public:
    enum Type : uint8_t { BRIGHTNESS, INTERVAL, DENSITY, DEFCON };

    explicit WOPRNumber(Type type) : type_(type)
    {
    }

    void setup() override;

  protected:
    void control(float value) override;

    Type type_;
    ESPPreferenceObject pref_;
};

}  // namespace wopr_display
}  // namespace esphome
#endif  // USE_NUMBER
