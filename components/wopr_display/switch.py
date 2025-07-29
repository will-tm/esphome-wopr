import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch, spi
from esphome.const import (
    CONF_ID,
)
from esphome import pins

DEPENDENCIES = ["spi"]

CONF_CS_PIN = "cs_pin"
CONF_NUM_MATRICES = "num_matrices"
CONF_BRIGHTNESS = "brightness"
CONF_MIN_INTERVAL = "min_interval_ms"
CONF_MAX_INTERVAL = "max_interval_ms"

wopr_display_ns = cg.esphome_ns.namespace("wopr_display")
WOPRDisplay = wopr_display_ns.class_("WOPRDisplay", switch.Switch, cg.Component, spi.SPIDevice)

CONFIG_SCHEMA = (
    switch.switch_schema(WOPRDisplay)
    .extend(
        {
            cv.GenerateID(): cv.declare_id(WOPRDisplay),
            cv.Optional(CONF_NUM_MATRICES, default=12): cv.int_range(min=1, max=32),
            cv.Optional(CONF_BRIGHTNESS, default=0): cv.int_range(min=0, max=15),
            cv.Optional(CONF_MIN_INTERVAL, default=333): cv.positive_int,
            cv.Optional(CONF_MAX_INTERVAL, default=1332): cv.positive_int,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(spi.spi_device_schema())
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await switch.register_switch(var, config)
    await spi.register_spi_device(var, config)

    cg.add(var.set_num_matrices(config[CONF_NUM_MATRICES]))
    cg.add(var.set_brightness(config[CONF_BRIGHTNESS]))
    cg.add(var.set_interval_range(config[CONF_MIN_INTERVAL], config[CONF_MAX_INTERVAL]))