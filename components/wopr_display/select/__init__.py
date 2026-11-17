import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select

from .. import CONF_WOPR_DISPLAY_ID, WOPRDisplay, wopr_display_ns

WOPRModeSelect = wopr_display_ns.class_("WOPRModeSelect", select.Select, cg.Component)

# Order must match WOPRDisplay::Mode
MODES = ["Movie", "Panel", "Launch code", "DEFCON", "Film"]

CONFIG_SCHEMA = (
    select.select_schema(WOPRModeSelect, icon="mdi:television-classic")
    .extend({cv.GenerateID(CONF_WOPR_DISPLAY_ID): cv.use_id(WOPRDisplay)})
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await select.new_select(config, options=MODES)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_WOPR_DISPLAY_ID])
