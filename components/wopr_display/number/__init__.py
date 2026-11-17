import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_BRIGHTNESS, CONF_INTERVAL, UNIT_MILLISECOND, UNIT_PERCENT

from .. import CONF_WOPR_DISPLAY_ID, WOPRDisplay, wopr_display_ns

CONF_DENSITY = "density"
CONF_DEFCON = "defcon"

WOPRNumber = wopr_display_ns.class_("WOPRNumber", number.Number, cg.Component)
NumberType = WOPRNumber.enum("Type")

# key: (type, min, max, step, unit, icon)
NUMBERS = {
    CONF_BRIGHTNESS: (NumberType.BRIGHTNESS, 0, 15, 1, cv.UNDEFINED, "mdi:brightness-6"),
    CONF_INTERVAL: (NumberType.INTERVAL, 50, 2000, 1, UNIT_MILLISECOND, "mdi:timer-outline"),
    CONF_DENSITY: (NumberType.DENSITY, 0, 100, 1, UNIT_PERCENT, "mdi:grid"),
    CONF_DEFCON: (NumberType.DEFCON, 1, 5, 1, cv.UNDEFINED, "mdi:alert-octagon"),
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_WOPR_DISPLAY_ID): cv.use_id(WOPRDisplay),
        **{
            cv.Optional(key): number.number_schema(WOPRNumber, unit_of_measurement=unit, icon=icon).extend(
                cv.COMPONENT_SCHEMA
            )
            for key, (_, _, _, _, unit, icon) in NUMBERS.items()
        },
    }
)


async def to_code(config):
    for key, (type_, min_value, max_value, step, _, _) in NUMBERS.items():
        if key not in config:
            continue
        var = await number.new_number(
            config[key], type_, min_value=min_value, max_value=max_value, step=step
        )
        await cg.register_component(var, config[key])
        await cg.register_parented(var, config[CONF_WOPR_DISPLAY_ID])
