vpath application/%.cpp ../src
vpath common/%.cpp ../src
vpath modules/%.cpp ../
vpath modules/%.c ../

#common for all targets
SOURCES_COMMON := \
modules/unity/src/unity.c \
modules/dbms/src/LESSDB.cpp \
modules/EmuEEPROM/src/EmuEEPROM.cpp \
modules/midi/src/MIDI.cpp \
modules/u8g2/csrc/u8x8_string.c \
modules/u8g2/csrc/u8x8_setup.c \
modules/u8g2/csrc/u8x8_u8toa.c \
modules/u8g2/csrc/u8x8_8x8.c \
modules/u8g2/csrc/u8x8_u16toa.c \
modules/u8g2/csrc/u8x8_display.c \
modules/u8g2/csrc/u8x8_fonts.c \
modules/u8g2/csrc/u8x8_byte.c \
modules/u8g2/csrc/u8x8_cad.c \
modules/u8g2/csrc/u8x8_gpio.c \
modules/u8g2/csrc/u8x8_d_ssd1306_128x64_noname.c \
modules/u8g2/csrc/u8x8_d_ssd1306_128x32.c

ifeq ($(ARCH), avr)
    SOURCES_COMMON += board/$(ARCH)/common/FlashPages.cpp
else
    SOURCES_COMMON += $(MCU_DIR)/FlashPages.cpp
endif

#common include dirs
INCLUDE_DIRS_COMMON := \
-I"./" \
-I"./unity" \
-I"../modules/" \
-I"../src/" \
-I"../src/bootloader/" \
-I"../src/application/" \
-I"../src/board/$(ARCH)/variants/$(MCU_FAMILY)" \
-I"../src/$(MCU_DIR)" \
-I"../src/$(BOARD_TARGET_DIR)/"
