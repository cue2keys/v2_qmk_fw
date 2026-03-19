I2C_DRIVER_REQUIRED = yes
POINTING_DEVICE_ENABLE = yes
POINTING_DEVICE_DRIVER = custom
DEFERRED_EXEC_ENABLE := yes

OLED_DRIVER = ssd1306
OLED_TRANSPORT = i2c

SRC += kb_config.c
SRC += trackball.c
SRC += raw_hid_handler_via.c
SRC += drivers/encoder_dynamic_res.c
SRC += magkey_utils.c

LIB_SRC += i2clib.c
LIB_SRC += device_scanner.c
LIB_SRC += drivers/modular_adns5050.c
LIB_SRC += drivers/modular_pmw3610.c
LIB_SRC += encoder.c
LIB_SRC += display.c
LIB_SRC += pendant_led.c
LIB_SRC += flatcc/src/runtime/builder.c
LIB_SRC += flatcc/src/runtime/emitter.c
LIB_SRC += flatcc/src/runtime/refmap.c

# add generated schema headers and flatcc runtime headers staged by meta build
EXTRAINCDIRS += keyboards/cue2keys/generated/include
EXTRAINCDIRS += keyboards/cue2keys/flatcc/include

SECURE_ENABLE = yes
