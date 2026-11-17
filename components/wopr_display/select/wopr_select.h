#pragma once

#include "esphome/core/defines.h"
#ifdef USE_SELECT

#include "esphome/components/select/select.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "../wopr_display.h"

namespace esphome
{
namespace wopr_display
{

class WOPRModeSelect : public select::Select, public Component, public Parented<WOPRDisplay>
{
  public:
    void setup() override;

  protected:
    void control(size_t index) override;

    ESPPreferenceObject pref_;
};

}  // namespace wopr_display
}  // namespace esphome
#endif  // USE_SELECT
