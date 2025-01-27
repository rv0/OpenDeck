#!/usr/bin/env bash

YAML_FILE=$1
GEN_DIR=$2
YAML_PARSER="dasel -n -p yaml --plain -f"
TARGET_NAME=$(basename "$YAML_FILE" .yml)
OUT_HEADER="$GEN_DIR"/Target.h
OUT_MAKEFILE="$GEN_DIR"/Defines.mk

#generated header contains cpp code
#generate USB names in dedicated header since this one is included in .c files
OUT_HEADER_USB="$GEN_DIR"/USBnames.h

echo "Generating target definitions..."

mkdir -p "$GEN_DIR"
echo "" > "$OUT_HEADER"
echo "" > "$OUT_MAKEFILE"
echo "" > "$OUT_HEADER_USB"

#################################################### CORE ####################################################

mcu=$($YAML_PARSER "$YAML_FILE" mcu)

## MCU processing first
MCU_GEN_DIR=$(dirname "$2")/../mcu/$mcu
MCU_YAML_FILE=$(dirname "$YAML_FILE")/../mcu/$mcu.yml
HW_TEST_YAML_FILE=$(dirname "$YAML_FILE")/../hw-test/$TARGET_NAME.yml

if [[ ! -f $MCU_YAML_FILE ]]
then
    echo "$MCU_YAML_FILE doesn't exist"
    exit 1
fi

if [[ ! -d $MCU_GEN_DIR ]]
then
    if ! ../scripts/gen_mcu.sh "$MCU_YAML_FILE" "$MCU_GEN_DIR"
    then
        exit 1
    fi
fi

if [[ -f $HW_TEST_YAML_FILE ]]
then
    #hw config files go into the same dir as target ones
    if ! ../scripts/gen_hwconfig.sh "$HW_TEST_YAML_FILE" "$GEN_DIR"
    then
        exit 1
    else
        printf "%s\n" "DEFINES += HW_TESTS_SUPPORTED" >> "$OUT_MAKEFILE"
    fi
fi

{
    printf "%s%s\n" '-include $(MAKEFILE_INCLUDE_PREFIX)$(BOARD_MCU_BASE_DIR)/' "$mcu/MCU.mk"
    printf "%s\n" "DEFINES += FW_UID=$(../scripts/gen_fw_uid.sh "$TARGET_NAME")"
} >> "$OUT_MAKEFILE"

hse_val=$($YAML_PARSER "$YAML_FILE" extClockMhz)

if [[ $hse_val != "null" ]]
then
    printf "%s%s\n" "DEFINES += HSE_VALUE=$hse_val" "000000" >> "$OUT_MAKEFILE"
fi

board_name=$($YAML_PARSER "$YAML_FILE" boardNameOverride)

if [[ $board_name == "null" ]]
then
    board_name=$TARGET_NAME
fi

printf "%s\n" "DEFINES += BOARD_STRING=\\\"$board_name\\\"" >> "$OUT_MAKEFILE"

########################################################################################################

{
    printf "%s\n\n" "#pragma once"
    printf "%s\n" "#include \"core/src/general/IO.h\""
    printf "%s\n" "#include \"board/Internal.h\""
    printf "%s\n\n" "#include <MCU.h>"
} >> "$OUT_HEADER"

#################################################### PERIPHERALS ####################################################

if [[ $($YAML_PARSER "$YAML_FILE" usb) == "true" ]]
then
    printf "%s\n" "DEFINES += USB_SUPPORTED" >> "$OUT_MAKEFILE"

    {
        printf "%s\n" "#if defined(FW_APP)"
        printf "%s\n" "#define USB_PRODUCT UNICODE_STRING(\"OpenDeck | $board_name\")"
        printf "%s\n" "#elif defined(FW_BOOT)"
        printf "%s\n" "#define USB_PRODUCT UNICODE_STRING(\"OpenDeck DFU | $board_name\")"
        printf "%s\n" "#endif"
    } >> "$OUT_HEADER_USB"
fi

if [[ "$($YAML_PARSER "$YAML_FILE" usbLink)" != "null" ]]
then
    if [[ "$($YAML_PARSER "$YAML_FILE" usbLink.type)" != "null" ]]
    then
        uart_channel_usb_link=$($YAML_PARSER "$YAML_FILE" usbLink.uartChannel)
        printf "%s\n" "DEFINES += UART_CHANNEL_USB_LINK=$uart_channel_usb_link" >> "$OUT_MAKEFILE"

        if [[ "$($YAML_PARSER "$YAML_FILE" usbLink.type)" == "host" ]]
        then
            {
                printf "%s\n" "DEFINES += USB_LINK_MCU"
                printf "%s\n" "DEFINES += FW_SELECTOR_NO_VERIFY_CRC"
                printf "%s\n" "#append this only if it wasn't appended already"
                printf "%s\n" 'ifeq (,$(findstring USB_SUPPORTED,$(DEFINES)))'
                printf "%s\n" "    DEFINES += USB_SUPPORTED"
                printf "%s\n" "endif"
            } >> "$OUT_MAKEFILE"
        elif [[ "$($YAML_PARSER "$YAML_FILE" usbLink.type)" == "device" ]]
        then
            {
                printf "%s\n" "#make sure slave MCUs don't have USB enabled"
                printf "%s\n" 'DEFINES := $(filter-out USB_SUPPORTED,$(DEFINES))'
            } >> "$OUT_MAKEFILE"
        fi
    fi
fi

if [[ "$($YAML_PARSER "$YAML_FILE" dinMIDI)" != "null" ]]
then
    uart_channel_din_midi=$($YAML_PARSER "$YAML_FILE" dinMIDI.uartChannel)

    if [[ -n "$uart_channel_usb_link" ]]
    then
        if [[ $uart_channel_usb_link -eq $uart_channel_din_midi ]]
        then
            echo "USB link channel and DIN MIDI channel cannot be the same"
            exit 1
        fi
    fi

    {
        printf "%s\n" "DEFINES += DIN_MIDI_SUPPORTED"
        printf "%s\n" "DEFINES += UART_CHANNEL_DIN=$uart_channel_din_midi"
    } >> "$OUT_MAKEFILE"
fi

if [[ "$($YAML_PARSER "$YAML_FILE" i2c)" != "null" ]]
then
    {
        printf "%s\n" "DEFINES += I2C_SUPPORTED"
        printf "%s\n" "DEFINES += I2C_CHANNEL=$($YAML_PARSER "$YAML_FILE" i2c.channel)"
    } >> "$OUT_MAKEFILE"
fi

if [[ "$($YAML_PARSER "$YAML_FILE" touchscreen)" != "null" ]]
then
    uart_channel_touchscreen=$($YAML_PARSER "$YAML_FILE" touchscreen.uartChannel)

    if [[ -n "$uart_channel_usb_link" ]]
    then
        if [[ $uart_channel_usb_link -eq $uart_channel_touchscreen ]]
        then
            echo "USB link channel and touchscreen channel cannot be the same"
            exit 1
        fi
    fi

    #guard against ommisions of touchscreen component amount by assigning the value to 0 if undefined
    nr_of_touchscreen_components=$($YAML_PARSER "$YAML_FILE" touchscreen.components | grep -v null | awk '{print$1}END{if(NR==0)print 0}')

    if [[ "$nr_of_touchscreen_components" -eq 0 ]]
    then
        echo "Amount of touchscreen components cannot be 0 or undefined"
        exit 1
    fi

    {
        printf "%s\n" "DEFINES += NR_OF_TOUCHSCREEN_COMPONENTS=$nr_of_touchscreen_components"
        printf "%s\n" "DEFINES += TOUCHSCREEN_SUPPORTED"
        printf "%s\n" "DEFINES += UART_CHANNEL_TOUCHSCREEN=$uart_channel_touchscreen"
    } >> "$OUT_MAKEFILE"
else
    printf "%s\n" "DEFINES += NR_OF_TOUCHSCREEN_COMPONENTS=0" >> "$OUT_MAKEFILE"
fi

if [[ $($YAML_PARSER "$YAML_FILE" bootloader.button) != "null" ]]
then
    port=$($YAML_PARSER "$YAML_FILE" bootloader.button.port)
    index=$($YAML_PARSER "$YAML_FILE" bootloader.button.index)

    {
        printf "%s\n" "#define BTLDR_BUTTON_PORT CORE_IO_PIN_PORT_DEF(${port})"
        printf "%s\n" "#define BTLDR_BUTTON_PIN CORE_IO_PIN_INDEX_DEF(${index})"
    } >> "$OUT_HEADER"

    if [[ "$($YAML_PARSER "$YAML_FILE" bootloader.button.activeState)" == "high" ]]
    then
        #active high
        printf "%s\n" "DEFINES += BTLDR_BUTTON_AH" >> "$OUT_MAKEFILE"
    fi
fi

if [[ $($YAML_PARSER "$YAML_FILE" dmx) != "null" ]]
then
    uart_channel_dmx=$($YAML_PARSER "$YAML_FILE" dmx.uartChannel)

    if [[ $uart_channel_dmx == "null" ]]
    then
        echo "DMX channel left unspecified"
        exit 1
    fi

    if [[ -n "$uart_channel_usb_link" ]]
    then
        if [[ $uart_channel_usb_link -eq $uart_channel_dmx ]]
        then
            echo "USB link channel and DMX channel cannot be the same"
            exit 1
        fi
    fi

    {
        printf "%s\n" "DEFINES += DMX_SUPPORTED" >> "$OUT_MAKEFILE"
        printf "%s\n" "DEFINES += UART_CHANNEL_DMX=$uart_channel_dmx"
    } >> "$OUT_MAKEFILE"
fi

########################################################################################################

#################################################### DIGITAL INPUTS ####################################################

if [[ "$($YAML_PARSER "$YAML_FILE" buttons)" != "null" ]]
then
    printf "%s\n" "DEFINES += DIGITAL_INPUTS_SUPPORTED" >> "$OUT_MAKEFILE"

    digital_in_type=$($YAML_PARSER "$YAML_FILE" buttons.type)

    declare -i nr_of_digital_inputs
    nr_of_digital_inputs=0

    if [[ $digital_in_type == native ]]
    then
        nr_of_digital_inputs=$($YAML_PARSER "$YAML_FILE" buttons.pins --length)

        unset port_duplicates
        unset port_array
        unset index_array
        unset port_array_unique
        declare -A port_duplicates
        declare -a port_array
        declare -a index_array
        declare -a port_array_unique

        for ((i=0; i<nr_of_digital_inputs; i++))
        do
            port=$($YAML_PARSER "$YAML_FILE" buttons.pins.["$i"].port)
            index=$($YAML_PARSER "$YAML_FILE" buttons.pins.["$i"].index)

            port_array+=("$port")
            index_array+=("$index")

            {
                printf "%s\n" "#define DIN_PORT_${i} CORE_IO_PIN_PORT_DEF(${port})"
                printf "%s\n" "#define DIN_PIN_${i} CORE_IO_PIN_INDEX_DEF(${index})"
            } >> "$OUT_HEADER"

            if [[ -z ${port_duplicates[$port]} ]]
            then
                port_array_unique+=("$port")
            fi

            port_duplicates["$port"]=1
        done

        printf "%s\n" "#define NR_OF_DIGITAL_INPUT_PORTS ${#port_array_unique[@]}" >> "$OUT_HEADER"

        {
            printf "%s\n" "namespace {"
            printf "%s\n" "constexpr inline core::io::pinPort_t dInPorts[NR_OF_DIGITAL_INPUT_PORTS] = {"
        } >> "$OUT_HEADER"

        for ((i=0; i<${#port_array_unique[@]}; i++))
        do
            {
                printf "%s\n" "CORE_IO_PIN_PORT_DEF(${port_array_unique[$i]}),"
            } >> "$OUT_HEADER"
        done

        {
            printf "%s\n" "};"
            printf "%s\n" "constexpr inline core::io::mcuPin_t dInPins[NR_OF_DIGITAL_INPUTS] = {"
        } >> "$OUT_HEADER"

        for ((i=0; i<nr_of_digital_inputs; i++))
        do
            printf "%s\n" "CORE_IO_MCU_PIN_VAR(DIN_PORT_${i}, DIN_PIN_${i})," >> "$OUT_HEADER"
        done

        {
            printf "%s\n" "};" >> "$OUT_HEADER"
        } >> "$OUT_HEADER"

        {
            printf "%s\n" "constexpr inline uint8_t buttonIndexToUniquePortIndex[NR_OF_DIGITAL_INPUTS] = {"
        } >> "$OUT_HEADER"

        for ((i=0; i<nr_of_digital_inputs; i++))
        do
            for ((port=0; port<${#port_array_unique[@]}; port++))
            do
                if [[ ${port_array[$i]} == "${port_array_unique[$port]}" ]]
                then
                    printf "%s\n" "$port," >> "$OUT_HEADER"
                fi
            done
        done

        {
            printf "%s\n" "};" >> "$OUT_HEADER"
            printf "%s\n" "constexpr inline uint8_t buttonIndexToPinIndex[NR_OF_DIGITAL_INPUTS] = {"
        } >> "$OUT_HEADER"

        for ((i=0; i<nr_of_digital_inputs; i++))
        do
            printf "%s\n" "${index_array[i]}," >> "$OUT_HEADER"
        done

        {
            printf "%s\n" "};" >> "$OUT_HEADER"
            printf "%s\n" "}"
        } >> "$OUT_HEADER"

        printf "%s\n" "DEFINES += NATIVE_BUTTON_INPUTS" >> "$OUT_MAKEFILE"

        if [[ "$($YAML_PARSER "$YAML_FILE" buttons.extPullups)" == "true" ]]
        then
            printf "%s\n" "DEFINES += BUTTONS_EXT_PULLUPS" >> "$OUT_MAKEFILE"
        fi
    elif [[ $digital_in_type == shiftRegister ]]
    then
        port=$($YAML_PARSER "$YAML_FILE" buttons.pins.data.port)
        index=$($YAML_PARSER "$YAML_FILE" buttons.pins.data.index)

        {
            printf "%s\n" "#define SR_IN_DATA_PORT CORE_IO_PIN_PORT_DEF(${port})"
            printf "%s\n" "#define SR_IN_DATA_PIN CORE_IO_PIN_INDEX_DEF(${index})"
        } >> "$OUT_HEADER"

        port=$($YAML_PARSER "$YAML_FILE" buttons.pins.clock.port)
        index=$($YAML_PARSER "$YAML_FILE" buttons.pins.clock.index)

        {
            printf "%s\n" "#define SR_IN_CLK_PORT CORE_IO_PIN_PORT_DEF(${port})"
            printf "%s\n" "#define SR_IN_CLK_PIN CORE_IO_PIN_INDEX_DEF(${index})"
        } >> "$OUT_HEADER"

        port=$($YAML_PARSER "$YAML_FILE" buttons.pins.latch.port)
        index=$($YAML_PARSER "$YAML_FILE" buttons.pins.latch.index)

        {
            printf "%s\n" "#define SR_IN_LATCH_PORT CORE_IO_PIN_PORT_DEF(${port})"
            printf "%s\n" "#define SR_IN_LATCH_PIN CORE_IO_PIN_INDEX_DEF(${index})"
        } >> "$OUT_HEADER"

        number_of_in_sr=$($YAML_PARSER "$YAML_FILE" buttons.shiftRegisters)
        nr_of_digital_inputs=$(( 8 * "$number_of_in_sr"))

        printf "%s\n" "DEFINES += NUMBER_OF_IN_SR=$number_of_in_sr" >> "$OUT_MAKEFILE"
    elif [[ $digital_in_type == matrix ]]
    then
        number_of_rows=0
        number_of_columns=0

        if [[ $($YAML_PARSER "$YAML_FILE" buttons.rows.type) == "shiftRegister" ]]
        then
            number_of_rows=8

            port=$($YAML_PARSER "$YAML_FILE" buttons.rows.pins.data.port)
            index=$($YAML_PARSER "$YAML_FILE" buttons.rows.pins.data.index)

            {
                printf "%s\n" "#define SR_IN_DATA_PORT CORE_IO_PIN_PORT_DEF(${port})"
                printf "%s\n" "#define SR_IN_DATA_PIN CORE_IO_PIN_INDEX_DEF(${index})"
            } >> "$OUT_HEADER"

            port=$($YAML_PARSER "$YAML_FILE" buttons.rows.pins.clock.port)
            index=$($YAML_PARSER "$YAML_FILE" buttons.rows.pins.clock.index)

            {
                printf "%s\n" "#define SR_IN_CLK_PORT CORE_IO_PIN_PORT_DEF(${port})"
                printf "%s\n" "#define SR_IN_CLK_PIN CORE_IO_PIN_INDEX_DEF(${index})"
            } >> "$OUT_HEADER"

            port=$($YAML_PARSER "$YAML_FILE" buttons.rows.pins.latch.port)
            index=$($YAML_PARSER "$YAML_FILE" buttons.rows.pins.latch.index)

            {
                printf "%s\n" "#define SR_IN_LATCH_PORT CORE_IO_PIN_PORT_DEF(${port})"
                printf "%s\n" "#define SR_IN_LATCH_PIN CORE_IO_PIN_INDEX_DEF(${index})"
            } >> "$OUT_HEADER"

        elif [[ $($YAML_PARSER "$YAML_FILE" buttons.rows.type) == "native" ]]
        then
            number_of_rows=$($YAML_PARSER "$YAML_FILE" buttons.rows.pins --length)

           for ((i=0; i<number_of_rows; i++))
            do
                port=$($YAML_PARSER "$YAML_FILE" buttons.rows.pins.["$i"].port)
                index=$($YAML_PARSER "$YAML_FILE" buttons.rows.pins.["$i"].index)

                {
                    printf "%s\n" "#define DIN_PORT_${i} CORE_IO_PIN_PORT_DEF(${port})"
                    printf "%s\n" "#define DIN_PIN_${i} CORE_IO_PIN_INDEX_DEF(${index})"
                } >> "$OUT_HEADER"
            done

            {
                printf "%s\n" "namespace {"
                printf "%s\n" "constexpr inline core::io::mcuPin_t dInPins[NUMBER_OF_BUTTON_ROWS] = {"
            } >> "$OUT_HEADER"

            for ((i=0; i<number_of_rows; i++))
            do
                printf "%s\n" "CORE_IO_MCU_PIN_VAR(DIN_PORT_${i}, DIN_PIN_${i})," >> "$OUT_HEADER"
            done

            {
                printf "%s\n" "};"
                printf "%s\n" "}"
            } >> "$OUT_HEADER"
        else
            echo "Invalid button row type specified"
            exit 1
        fi

        if [[ $($YAML_PARSER "$YAML_FILE" buttons.columns.pins --length) -eq 3 ]]
        then
            number_of_columns=8

            for ((i=0; i<3; i++))
            do
                port=$($YAML_PARSER "$YAML_FILE" buttons.columns.pins.decA"$i".port)
                index=$($YAML_PARSER "$YAML_FILE" buttons.columns.pins.decA"$i".index)

                {
                    printf "%s\n" "#define DEC_BM_PORT_A${i} CORE_IO_PIN_PORT_DEF(${port})"
                    printf "%s\n" "#define DEC_BM_PIN_A${i} CORE_IO_PIN_INDEX_DEF(${index})"
                } >> "$OUT_HEADER"
            done
        else
            echo "Invalid number of columns specified"
            exit 1
        fi

        nr_of_digital_inputs=$(("$number_of_columns" * "$number_of_rows"))

        {
            printf "%s\n" "DEFINES += NUMBER_OF_BUTTON_COLUMNS=$number_of_columns"
            printf "%s\n" "DEFINES += NUMBER_OF_BUTTON_ROWS=$number_of_rows"
        } >> "$OUT_MAKEFILE"
    fi

    if [[ "$($YAML_PARSER "$YAML_FILE" buttons.indexing)" != "null" ]]
    then
        nr_of_digital_inputs=$($YAML_PARSER "$YAML_FILE" buttons.indexing --length)

        {
            printf "%s\n" "namespace {"
            printf "%s\n" "constexpr inline uint8_t buttonIndexes[NR_OF_DIGITAL_INPUTS] = {" 
        } >> "$OUT_HEADER"

        for ((i=0; i<nr_of_digital_inputs; i++))
        do
            index=$($YAML_PARSER "$YAML_FILE" buttons.indexing.["$i"])
            printf "%s\n" "${index}," >> "$OUT_HEADER"
        done

        {
            printf "%s\n" "};"
            printf "%s\n" "}"
        } >> "$OUT_HEADER"

        printf "%s\n" "DEFINES += BUTTON_INDEXING" >> "$OUT_MAKEFILE"
    fi

    printf "%s\n" "DEFINES += NR_OF_DIGITAL_INPUTS=$nr_of_digital_inputs" >> "$OUT_MAKEFILE"
else
    {
        printf "%s\n" "DEFINES += NR_OF_DIGITAL_INPUTS=0"
    } >> "$OUT_MAKEFILE"
fi

########################################################################################################

#################################################### DIGITAL OUTPUTS ####################################################

if [[ "$($YAML_PARSER "$YAML_FILE" leds.external)" != "null" ]]
then
    printf "%s\n" "DEFINES += DIGITAL_OUTPUTS_SUPPORTED" >> "$OUT_MAKEFILE"

    digital_out_type=$($YAML_PARSER "$YAML_FILE" leds.external.type)

    declare -i nr_of_digital_outputs
    nr_of_digital_outputs=0

    if [[ $digital_out_type == "native" ]]
    then
        nr_of_digital_outputs=$($YAML_PARSER "$YAML_FILE" leds.external.pins --length)

        unset port_duplicates
        unset port_array
        unset index_array
        unset port_array_unique
        declare -A port_duplicates
        declare -a port_array
        declare -a index_array
        declare -a port_array_unique

        for ((i=0; i<nr_of_digital_outputs; i++))
        do
            port=$($YAML_PARSER "$YAML_FILE" leds.external.pins.["$i"].port)
            index=$($YAML_PARSER "$YAML_FILE" leds.external.pins.["$i"].index)

            port_array+=("$port")
            index_array+=("$index")

            {
                printf "%s\n" "#define DOUT_PORT_${i} CORE_IO_PIN_PORT_DEF(${port})"
                printf "%s\n" "#define DOUT_PIN_${i} CORE_IO_PIN_INDEX_DEF(${index})"
            } >> "$OUT_HEADER"

            if [[ -z ${port_duplicates[$port]} ]]
            then
                port_array_unique+=("$port")
            fi

            port_duplicates["$port"]=1
        done

        {
            printf "%s\n" "#define NR_OF_DIGITAL_OUTPUT_PORTS ${#port_array_unique[@]}"
            printf "%s\n" "namespace {"
            printf "%s\n" "constexpr inline core::io::pinPort_t dOutPorts[NR_OF_DIGITAL_OUTPUT_PORTS] = {"
        } >> "$OUT_HEADER"

        for ((i=0; i<${#port_array_unique[@]}; i++))
        do
            {
                printf "%s\n" "CORE_IO_PIN_PORT_DEF(${port_array_unique[$i]}),"
            } >> "$OUT_HEADER"
        done

        {
            printf "%s\n" "};"
            printf "%s\n" "constexpr inline core::io::portWidth_t dOutPortsClearMask[NR_OF_DIGITAL_OUTPUT_PORTS] = {"
        } >> "$OUT_HEADER"

        for ((port=0; port<${#port_array_unique[@]}; port++))
        do
            unset mask
            declare -i mask
            mask=0xFFFFFFFF

            for ((i=0; i<nr_of_digital_outputs; i++))
            do
                if [[ ${port_array[$i]} == "${port_array_unique[$port]}" ]]
                then
                    ((mask&=~(1 << index_array[i])))
                fi
            done

            printf "%s\n" "static_cast<core::io::portWidth_t>($mask)," >> "$OUT_HEADER"
        done

        {
            printf "%s\n" "};"
            printf "%s\n" "constexpr inline core::io::mcuPin_t dOutPins[NR_OF_DIGITAL_OUTPUTS] = {"
        } >> "$OUT_HEADER"

        for ((i=0; i<nr_of_digital_outputs; i++))
        do
            printf "%s\n" "CORE_IO_MCU_PIN_VAR(DOUT_PORT_${i}, DOUT_PIN_${i})," >> "$OUT_HEADER"
        done

        {
            printf "%s\n" "};" >> "$OUT_HEADER"
            printf "%s\n" "constexpr inline uint8_t ledIndexToUniquePortIndex[NR_OF_DIGITAL_OUTPUTS] = {"
        } >> "$OUT_HEADER"

        for ((i=0; i<nr_of_digital_outputs; i++))
        do
            for ((port=0; port<${#port_array_unique[@]}; port++))
            do
                if [[ ${port_array[$i]} == "${port_array_unique[$port]}" ]]
                then
                    printf "%s\n" "$port," >> "$OUT_HEADER"
                fi
            done
        done

        {
            printf "%s\n" "};" >> "$OUT_HEADER"
            printf "%s\n" "constexpr inline uint8_t ledIndexToPinIndex[NR_OF_DIGITAL_OUTPUTS] = {"
        } >> "$OUT_HEADER"

        for ((i=0; i<nr_of_digital_outputs; i++))
        do
            printf "%s\n" "${index_array[i]}," >> "$OUT_HEADER"
        done

        {
            printf "%s\n" "};" >> "$OUT_HEADER"
            printf "%s\n" "}"
        } >> "$OUT_HEADER"

        printf "%s\n" "DEFINES += NATIVE_LED_OUTPUTS" >> "$OUT_MAKEFILE"
    elif [[ $digital_out_type == shiftRegister ]]
    then
        port=$($YAML_PARSER "$YAML_FILE" leds.external.pins.data.port)
        index=$($YAML_PARSER "$YAML_FILE" leds.external.pins.data.index)

        {
            printf "%s\n" "#define SR_OUT_DATA_PORT CORE_IO_PIN_PORT_DEF(${port})"
            printf "%s\n" "#define SR_OUT_DATA_PIN CORE_IO_PIN_INDEX_DEF(${index})"
        } >> "$OUT_HEADER"

        port=$($YAML_PARSER "$YAML_FILE" leds.external.pins.clock.port)
        index=$($YAML_PARSER "$YAML_FILE" leds.external.pins.clock.index)

        {
            printf "%s\n" "#define SR_OUT_CLK_PORT CORE_IO_PIN_PORT_DEF(${port})"
            printf "%s\n" "#define SR_OUT_CLK_PIN CORE_IO_PIN_INDEX_DEF(${index})"
        } >> "$OUT_HEADER"

        port=$($YAML_PARSER "$YAML_FILE" leds.external.pins.latch.port)
        index=$($YAML_PARSER "$YAML_FILE" leds.external.pins.latch.index)

        {
            printf "%s\n" "#define SR_OUT_LATCH_PORT CORE_IO_PIN_PORT_DEF(${port})"
            printf "%s\n" "#define SR_OUT_LATCH_PIN CORE_IO_PIN_INDEX_DEF(${index})"
        } >> "$OUT_HEADER"

        port=$($YAML_PARSER "$YAML_FILE" leds.external.pins.enable.port)
        index=$($YAML_PARSER "$YAML_FILE" leds.external.pins.enable.index)

        {
            printf "%s\n" "#define SR_OUT_OE_PORT CORE_IO_PIN_PORT_DEF(${port})"
            printf "%s\n" "#define SR_OUT_OE_PIN CORE_IO_PIN_INDEX_DEF(${index})"
        } >> "$OUT_HEADER"

        number_of_out_sr=$($YAML_PARSER "$YAML_FILE" leds.external.shiftRegisters)
        nr_of_digital_outputs=$((number_of_out_sr * 8))

        printf "%s\n" "DEFINES += NUMBER_OF_OUT_SR=$number_of_out_sr" >> "$OUT_MAKEFILE"
    elif [[ $digital_out_type == matrix ]]
    then
        number_of_led_columns=8
        number_of_led_rows=$($YAML_PARSER "$YAML_FILE" leds.external.rows.pins --length)

        for ((i=0; i<3; i++))
        do
            port=$($YAML_PARSER "$YAML_FILE" leds.external.columns.pins.decA"$i".port)
            index=$($YAML_PARSER "$YAML_FILE" leds.external.columns.pins.decA"$i".index)

            {
                printf "%s\n" "#define DEC_LM_PORT_A${i} CORE_IO_PIN_PORT_DEF(${port})"
                printf "%s\n" "#define DEC_LM_PIN_A${i} CORE_IO_PIN_INDEX_DEF(${index})"
            } >> "$OUT_HEADER"
        done

        for ((i=0; i<"$number_of_led_rows"; i++))
        do
            port=$($YAML_PARSER "$YAML_FILE" leds.external.rows.pins.["$i"].port)
            index=$($YAML_PARSER "$YAML_FILE" leds.external.rows.pins.["$i"].index)

            {
                printf "%s\n" "#define LED_ROW_PORT_${i} CORE_IO_PIN_PORT_DEF(${port})"
                printf "%s\n" "#define LED_ROW_PIN_${i} CORE_IO_PIN_INDEX_DEF(${index})"
            } >> "$OUT_HEADER"
        done

        {
            printf "%s\n" "namespace {"
            printf "%s\n" "constexpr inline core::io::mcuPin_t dOutPins[NUMBER_OF_LED_ROWS] = {"
        } >> "$OUT_HEADER"

        for ((i=0; i<"$number_of_led_rows"; i++))
        do
            printf "%s\n" "CORE_IO_MCU_PIN_VAR(LED_ROW_PORT_${i}, LED_ROW_PIN_${i})," >> "$OUT_HEADER"
        done

        {
            printf "%s\n" "};"
            printf "%s\n" "}"
        } >> "$OUT_HEADER"

        nr_of_digital_outputs=$(("$number_of_led_columns" * "$number_of_led_rows"))

        {
            printf "%s\n" "DEFINES += NUMBER_OF_LED_COLUMNS=$number_of_led_columns"
            printf "%s\n" "DEFINES += NUMBER_OF_LED_ROWS=$number_of_led_rows"
        } >> "$OUT_MAKEFILE"
    fi

    if [[ "$($YAML_PARSER "$YAML_FILE" leds.external.indexing)" != "null" ]]
    then
        nr_of_digital_outputs=$($YAML_PARSER "$YAML_FILE" leds.external.indexing --length)

        {
            printf "%s\n" "namespace {"
            printf "%s\n" "constexpr inline uint8_t ledIndexes[NR_OF_DIGITAL_OUTPUTS] = {"
        } >> "$OUT_HEADER"

        for ((i=0; i<nr_of_digital_outputs; i++))
        do
            index=$($YAML_PARSER "$YAML_FILE" leds.external.indexing.["$i"])
            printf "%s\n" "${index}," >> "$OUT_HEADER"
        done

        {
            printf "%s\n" "};"
            printf "%s\n" "}"
        } >> "$OUT_HEADER"

        printf "%s\n" "DEFINES += LED_INDEXING" >> "$OUT_MAKEFILE"
    fi

    {
        printf "%s\n" "DEFINES += NR_OF_DIGITAL_OUTPUTS=$nr_of_digital_outputs"
    } >> "$OUT_MAKEFILE"

    if [[ "$($YAML_PARSER "$YAML_FILE" leds.external.invert)" == "true" ]]
    then
        printf "%s\n" "DEFINES += LED_EXT_INVERT" >> "$OUT_MAKEFILE"
    fi
else
    {
        printf "%s\n" "DEFINES += NR_OF_DIGITAL_OUTPUTS=0"
    } >> "$OUT_MAKEFILE"
fi

if [[ "$($YAML_PARSER "$YAML_FILE" leds.internal)" != "null" ]]
then
    printf "%s\n" "DEFINES += LED_INDICATORS" >> "$OUT_MAKEFILE"
    printf "%s\n" "DEFINES += LED_INDICATORS_CTL" >> "$OUT_MAKEFILE"

    if [[ "$($YAML_PARSER "$YAML_FILE" leds.internal.invert)" == "true" ]]
    then
        printf "%s\n" "DEFINES += LED_INT_INVERT" >> "$OUT_MAKEFILE"
    fi

    if [[ $($YAML_PARSER "$YAML_FILE" leds.internal.pins.din) != "null" ]]
    then
        port=$($YAML_PARSER "$YAML_FILE" leds.internal.pins.din.rx.port)
        index=$($YAML_PARSER "$YAML_FILE" leds.internal.pins.din.rx.index)

        {
            printf "%s\n" "#define LED_MIDI_IN_DIN_PORT CORE_IO_PIN_PORT_DEF(${port})"
            printf "%s\n" "#define LED_MIDI_IN_DIN_PIN CORE_IO_PIN_INDEX_DEF(${index})"
        } >> "$OUT_HEADER"

        port=$($YAML_PARSER "$YAML_FILE" leds.internal.pins.din.tx.port)
        index=$($YAML_PARSER "$YAML_FILE" leds.internal.pins.din.tx.index)

        {
            printf "%s\n" "#define LED_MIDI_OUT_DIN_PORT CORE_IO_PIN_PORT_DEF(${port})"
            printf "%s\n" "#define LED_MIDI_OUT_DIN_PIN CORE_IO_PIN_INDEX_DEF(${index})"
        } >> "$OUT_HEADER"
    fi

    if [[ $($YAML_PARSER "$YAML_FILE" leds.internal.pins.usb) != "null" ]]
    then
        port=$($YAML_PARSER "$YAML_FILE" leds.internal.pins.usb.rx.port)
        index=$($YAML_PARSER "$YAML_FILE" leds.internal.pins.usb.rx.index)

        {
            printf "%s\n" "#define LED_MIDI_IN_USB_PORT CORE_IO_PIN_PORT_DEF(${port})"
            printf "%s\n" "#define LED_MIDI_IN_USB_PIN CORE_IO_PIN_INDEX_DEF(${index})"
        } >> "$OUT_HEADER"

        port=$($YAML_PARSER "$YAML_FILE" leds.internal.pins.usb.tx.port)
        index=$($YAML_PARSER "$YAML_FILE" leds.internal.pins.usb.tx.index)

        {
            printf "%s\n" "#define LED_MIDI_OUT_USB_PORT CORE_IO_PIN_PORT_DEF(${port})"
            printf "%s\n" "#define LED_MIDI_OUT_USB_PIN CORE_IO_PIN_INDEX_DEF(${index})"
        } >> "$OUT_HEADER"
    fi
fi

########################################################################################################

#################################################### ANALOG INPUTS ####################################################

if [[ "$($YAML_PARSER "$YAML_FILE" analog)" != "null" ]]
then
    printf "%s\n" "DEFINES += ADC_SUPPORTED" >> "$OUT_MAKEFILE"

    if [[ "$($YAML_PARSER "$YAML_FILE" analog.extReference)" == "true" ]]
    then
        printf "%s\n" "DEFINES += ADC_EXT_REF" >> "$OUT_MAKEFILE"
    fi

    analog_filter_median=$($YAML_PARSER "$YAML_FILE" analog.filter.median)
    analog_filter_ema=$($YAML_PARSER "$YAML_FILE" analog.filter.ema)

    # use filters by default if not specified
    if [[ $analog_filter_median != "false" ]]
    then
        printf "%s\n" "DEFINES += ANALOG_USE_MEDIAN_FILTER" >> "$OUT_MAKEFILE"
    fi

    if [[ $analog_filter_ema != "false" ]]
    then
        printf "%s\n" "DEFINES += ANALOG_USE_EMA_FILTER" >> "$OUT_MAKEFILE"
    fi

    analog_in_type=$($YAML_PARSER "$YAML_FILE" analog.type)

    declare -i nr_of_analog_inputs
    nr_of_analog_inputs=0

    if [[ $analog_in_type == "native" ]]
    then
        nr_of_analog_inputs=$($YAML_PARSER "$YAML_FILE" analog.pins --length)

        for ((i=0; i<nr_of_analog_inputs; i++))
        do
            port=$($YAML_PARSER "$YAML_FILE" analog.pins.["$i"].port)
            index=$($YAML_PARSER "$YAML_FILE" analog.pins.["$i"].index)

            {
                printf "%s\n" "#define AIN_PORT_${i} CORE_IO_PIN_PORT_DEF(${port})"
                printf "%s\n" "#define AIN_PIN_${i} CORE_IO_PIN_INDEX_DEF(${index})"
            } >> "$OUT_HEADER"

        done

        {
            printf "%s\n" "namespace {"
            printf "%s\n" "constexpr inline core::io::mcuPin_t aInPins[MAX_ADC_CHANNELS] = {"
        } >> "$OUT_HEADER"

        for ((i=0; i<nr_of_analog_inputs; i++))
        do
            printf "%s\n" "CORE_IO_MCU_PIN_VAR(AIN_PORT_${i}, AIN_PIN_${i})," >> "$OUT_HEADER"
        done

        {
            printf "%s\n" "};"
            printf "%s\n" "}"
        } >> "$OUT_HEADER"

        {
            printf "%s\n" "DEFINES += MAX_ADC_CHANNELS=$nr_of_analog_inputs"
            printf "%s\n" "DEFINES += NATIVE_ANALOG_INPUTS"
        } >> "$OUT_MAKEFILE"

    elif [[ $analog_in_type == 4067 ]]
    then
        for ((i=0; i<4; i++))
        do
            port=$($YAML_PARSER "$YAML_FILE" analog.pins.s"$i".port)
            index=$($YAML_PARSER "$YAML_FILE" analog.pins.s"$i".index)

            {
                printf "%s\n" "#define MUX_PORT_S${i} CORE_IO_PIN_PORT_DEF(${port})"
                printf "%s\n" "#define MUX_PIN_S${i} CORE_IO_PIN_INDEX_DEF(${index})"
            } >> "$OUT_HEADER"
        done

        number_of_mux=$($YAML_PARSER "$YAML_FILE" analog.multiplexers)

        for ((i=0; i<"$number_of_mux"; i++))
        do
            port=$($YAML_PARSER "$YAML_FILE" analog.pins.z"$i".port)
            index=$($YAML_PARSER "$YAML_FILE" analog.pins.z"$i".index)

            {
                printf "%s\n" "#define MUX_PORT_INPUT_${i} CORE_IO_PIN_PORT_DEF(${port})"
                printf "%s\n" "#define MUX_PIN_INPUT_${i} CORE_IO_PIN_INDEX_DEF(${index})"
            } >> "$OUT_HEADER"
        done

        {
            printf "%s\n" "namespace {"
            printf "%s\n" "constexpr inline core::io::mcuPin_t aInPins[MAX_ADC_CHANNELS] = {"
        } >> "$OUT_HEADER"

        for ((i=0; i<"$number_of_mux"; i++))
        do
            printf "%s\n" "CORE_IO_MCU_PIN_VAR(MUX_PORT_INPUT_${i}, MUX_PIN_INPUT_${i})," >> "$OUT_HEADER"
        done

        {
            printf "%s\n" "};"
            printf "%s\n" "}"
        } >> "$OUT_HEADER"

        nr_of_analog_inputs=$((16 * "$number_of_mux"))

        {
            printf "%s\n" "DEFINES += NUMBER_OF_MUX=$number_of_mux"
            printf "%s\n" "DEFINES += NUMBER_OF_MUX_INPUTS=16"
            printf "%s\n" "DEFINES += MAX_ADC_CHANNELS=$number_of_mux"
        } >> "$OUT_MAKEFILE"
    elif [[ $analog_in_type == 4051 ]]
    then
        for ((i=0; i<3; i++))
        do
            port=$($YAML_PARSER "$YAML_FILE" analog.pins.s"$i".port)
            index=$($YAML_PARSER "$YAML_FILE" analog.pins.s"$i".index)

            {
                printf "%s\n" "#define MUX_PORT_S${i} CORE_IO_PIN_PORT_DEF(${port})"
                printf "%s\n" "#define MUX_PIN_S${i} CORE_IO_PIN_INDEX_DEF(${index})"
            } >> "$OUT_HEADER"
        done

        number_of_mux=$($YAML_PARSER "$YAML_FILE" analog.multiplexers)

        for ((i=0; i<"$number_of_mux"; i++))
        do
            port=$($YAML_PARSER "$YAML_FILE" analog.pins.z"$i".port)
            index=$($YAML_PARSER "$YAML_FILE" analog.pins.z"$i".index)

            {
                printf "%s\n" "#define MUX_PORT_INPUT_${i} CORE_IO_PIN_PORT_DEF(${port})"
                printf "%s\n" "#define MUX_PIN_INPUT_${i} CORE_IO_PIN_INDEX_DEF(${index})"
            } >> "$OUT_HEADER"
        done

        {
            printf "%s\n" "namespace {"
            printf "%s\n" "constexpr inline core::io::mcuPin_t aInPins[MAX_ADC_CHANNELS] = {"
        } >> "$OUT_HEADER"

        for ((i=0; i<"$number_of_mux"; i++))
        do
            printf "%s\n" "CORE_IO_MCU_PIN_VAR(MUX_PORT_INPUT_${i}, MUX_PIN_INPUT_${i})," >> "$OUT_HEADER"
        done

        {
            printf "%s\n" "};"
            printf "%s\n" "}"
        } >> "$OUT_HEADER"

        nr_of_analog_inputs=$((8 * "$number_of_mux"))

        {
            printf "%s\n" "DEFINES += NUMBER_OF_MUX=$number_of_mux"
            printf "%s\n" "DEFINES += NUMBER_OF_MUX_INPUTS=8"
            printf "%s\n" "DEFINES += MAX_ADC_CHANNELS=$number_of_mux"
        } >> "$OUT_MAKEFILE"
    fi

    if [[ "$($YAML_PARSER "$YAML_FILE" analog.indexing)" != "null" ]]
    then
        nr_of_analog_inputs=$($YAML_PARSER "$YAML_FILE" analog.indexing --length)

        {
            printf "%s\n" "namespace {"
            printf "%s\n" "constexpr inline uint8_t analogIndexes[NR_OF_ANALOG_INPUTS] = {"
        } >> "$OUT_HEADER"

        for ((i=0; i<nr_of_analog_inputs; i++))
        do
            index=$($YAML_PARSER "$YAML_FILE" analog.indexing.["$i"])
            printf "%s\n" "${index}," >> "$OUT_HEADER"
        done

        {
            printf "%s\n" "};"
            printf "%s\n" "}"
        } >> "$OUT_HEADER"

        printf "%s\n" "DEFINES += ANALOG_INDEXING" >> "$OUT_MAKEFILE"
    fi

    printf "%s\n" "DEFINES += NR_OF_ANALOG_INPUTS=$nr_of_analog_inputs" >> "$OUT_MAKEFILE"
else
    {
        printf "%s\n" "DEFINES += NR_OF_ANALOG_INPUTS=0"
        printf "%s\n" "DEFINES += MAX_ADC_CHANNELS=0" 
    } >> "$OUT_MAKEFILE"
fi

########################################################################################################

#################################################### UNUSED OUTPUTS ####################################################

declare -i unused_pins
unused_pins=$($YAML_PARSER "$YAML_FILE" unused-io --length)

if [[ $unused_pins -ne 0 ]]
then
    for ((i=0; i<unused_pins; i++))
    do
        port=$($YAML_PARSER "$YAML_FILE" unused-io.["$i"].port)
        index=$($YAML_PARSER "$YAML_FILE" unused-io.["$i"].index)

        {
            printf "%s\n" "#define UNUSED_PORT_${i} CORE_IO_PIN_PORT_DEF(${port})"
            printf "%s\n" "#define UNUSED_PIN_${i} CORE_IO_PIN_INDEX_DEF(${index})"
        } >> "$OUT_HEADER"
    done

    {
        printf "%s\n" "namespace {"
        printf "%s\n" "constexpr inline Board::detail::io::unusedIO_t unusedPins[TOTAL_UNUSED_IO] = {"
    } >> "$OUT_HEADER"

    for ((i=0; i<unused_pins; i++))
    do
        mode=$($YAML_PARSER "$YAML_FILE" unused-io.["$i"].mode)

        case $mode in
            "in-pull")
                {
                    printf "%s\n" "{ .pin = { .port = UNUSED_PORT_${i}, .index = UNUSED_PIN_${i}",
                    printf "\n%s\n" "#ifdef __AVR__"
                    printf "%s\n" ".mode = core::io::pinMode_t::input, },"
                    printf "%s\n" "#else"
                    printf "%s\n" ".mode = core::io::pinMode_t::input, .pull = core::io::pullMode_t::up, },"
                    printf "%s\n" "#endif"
                    printf "%s\n" ".state = true, },"
                } >> "$OUT_HEADER"
                ;;

            "out-low")
                {
                    printf "%s\n" "{ .pin = { .port = UNUSED_PORT_${i}, .index = UNUSED_PIN_${i}",
                    printf "\n%s\n" "#ifdef __AVR__"
                    printf "%s\n" ".mode = core::io::pinMode_t::output, },"
                    printf "%s\n" "#else"
                    printf "%s\n" ".mode = core::io::pinMode_t::outputPP, .pull = core::io::pullMode_t::none, },"
                    printf "%s\n" "#endif"
                    printf "%s\n" ".state = false, },"
                } >> "$OUT_HEADER"
                ;;

            "out-high")
                {
                    printf "%s\n" "{ .pin = { .port = UNUSED_PORT_${i}, .index = UNUSED_PIN_${i}",
                    printf "\n%s\n" "#ifdef __AVR__"
                    printf "%s\n" ".mode = core::io::pinMode_t::output, },"
                    printf "%s\n" "#else"
                    printf "%s\n" ".mode = core::io::pinMode_t::outputPP, .pull = core::io::pullMode_t::none, }," >> "$OUT_HEADER"
                    printf "%s\n" "#endif"
                    printf "%s\n" ".state = true, }," >> "$OUT_HEADER"
                } >> "$OUT_HEADER"
                ;;

            *)
                echo "Incorrect unused pin mode specified"
                exit 1
                ;;
        esac
    done

    {
        printf "%s\n" "};"
        printf "%s\n" "}"
    } >> "$OUT_HEADER"

    printf "%s\n" "DEFINES += TOTAL_UNUSED_IO=$unused_pins" >> "$OUT_MAKEFILE"
fi

########################################################################################################

printf "\n%s" "#include \"board/common/Map.h.include\"" >> "$OUT_HEADER"