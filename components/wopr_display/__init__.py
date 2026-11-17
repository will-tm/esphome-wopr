import esphome.codegen as cg
from esphome.components import spi, switch

CODEOWNERS = ["@will-tm"]

CONF_WOPR_DISPLAY_ID = "wopr_display_id"

wopr_display_ns = cg.esphome_ns.namespace("wopr_display")
WOPRDisplay = wopr_display_ns.class_("WOPRDisplay", switch.Switch, cg.Component, spi.SPIDevice)
