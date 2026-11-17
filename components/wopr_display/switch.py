import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch, spi

from . import WOPRDisplay

DEPENDENCIES = ["spi"]

CONF_NUM_MATRICES = "num_matrices"
CONF_BRIGHTNESS = "brightness"
CONF_MIN_INTERVAL = "min_interval_ms"
CONF_MAX_INTERVAL = "max_interval_ms"


def validate_intervals(config):
    if config[CONF_MAX_INTERVAL] < config[CONF_MIN_INTERVAL]:
        raise cv.Invalid(f"{CONF_MAX_INTERVAL} must be >= {CONF_MIN_INTERVAL}")
    return config


CONFIG_SCHEMA = cv.All(
    switch.switch_schema(WOPRDisplay, default_restore_mode="RESTORE_DEFAULT_ON")
    .extend(
        {
            cv.Optional(CONF_NUM_MATRICES, default=12): cv.int_range(min=1, max=32),
            cv.Optional(CONF_BRIGHTNESS, default=0): cv.int_range(min=0, max=15),
            cv.Optional(CONF_MIN_INTERVAL, default=333): cv.int_range(min=1),
            cv.Optional(CONF_MAX_INTERVAL, default=1332): cv.int_range(min=1),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(spi.spi_device_schema()),
    validate_intervals,
)


async def to_code(config):
    var = await switch.new_switch(config)
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)

    cg.add(var.set_num_matrices(config[CONF_NUM_MATRICES]))
    cg.add(var.set_brightness(config[CONF_BRIGHTNESS]))
    cg.add(var.set_interval_range(config[CONF_MIN_INTERVAL], config[CONF_MAX_INTERVAL]))
