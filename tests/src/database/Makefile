vpath application/%.cpp ../src
vpath common/%.cpp ../src

ifeq (,$(findstring USB_LINK_MCU,$(DEFINES)))
    SOURCES_$(shell basename $(dir $(lastword $(MAKEFILE_LIST)))) := \
    stubs/database/DB_ReadWrite.cpp \
    application/database/Database.cpp \
    application/database/CustomInit.cpp
endif