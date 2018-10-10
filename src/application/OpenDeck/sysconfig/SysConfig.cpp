/*
    OpenDeck MIDI platform firmware
    Copyright (C) 2015-2018 Igor Petrovic

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "SysConfig.h"
#include "Version.h"
#include "Layout.cpp"
#include "board/common/constants/LEDs.h"
#include "core/src/general/Timing.h"

bool SysConfig::onCustomRequest(uint8_t value)
{
    bool retVal = true;

    switch(value)
    {
        case SYSEX_CR_FIRMWARE_VERSION:
        addToResponse(SW_VERSION_MAJOR);
        addToResponse(SW_VERSION_MINOR);
        addToResponse(SW_VERSION_REVISION);
        break;

        case SYSEX_CR_HARDWARE_VERSION:
        addToResponse(BOARD_ID);
        addToResponse(HARDWARE_VERSION_MAJOR);
        addToResponse(HARDWARE_VERSION_MINOR);
        addToResponse(0);
        break;

        case SYSEX_CR_FIRMWARE_HARDWARE_VERSION:
        addToResponse(SW_VERSION_MAJOR);
        addToResponse(SW_VERSION_MINOR);
        addToResponse(SW_VERSION_REVISION);
        addToResponse(BOARD_ID);
        addToResponse(HARDWARE_VERSION_MAJOR);
        addToResponse(HARDWARE_VERSION_MINOR);
        addToResponse(0);
        break;

        case SYSEX_CR_REBOOT_APP:
        case SYSEX_CR_FACTORY_RESET:
        case SYSEX_CR_REBOOT_BTLDR:
        #ifdef LEDS_SUPPORTED
        leds.setAllOff();
        wait_ms(2500);
        #endif

        if (value == SYSEX_CR_FACTORY_RESET)
            database.factoryReset(initPartial);

        if (value == SYSEX_CR_REBOOT_BTLDR)
            board.reboot(rebootBtldr);
        else
            board.reboot(rebootApp);
        break;

        case SYSEX_CR_MAX_COMPONENTS:
        addToResponse(MAX_NUMBER_OF_BUTTONS+MAX_NUMBER_OF_ANALOG+MAX_TOUCHSCREEN_BUTTONS);
        addToResponse(MAX_NUMBER_OF_ENCODERS);
        addToResponse(MAX_NUMBER_OF_ANALOG);
        addToResponse(MAX_NUMBER_OF_LEDS);
        break;

        case SYSEX_CR_ENABLE_PROCESSING:
        processingEnabled = true;
        break;

        case SYSEX_CR_DISABLE_PROCESSING:
        processingEnabled = false;
        break;

        default:
        retVal = false;
        break;
    }

    if (retVal)
    {
        #ifdef DISPLAY_SUPPORTED
        display.displayMIDIevent(displayEventIn, midiMessageSystemExclusive_display, 0, 0, 0);
        #endif
    }

    return retVal;
}

bool SysConfig::onGet(uint8_t block, uint8_t section, uint16_t index, sysExParameter_t &value)
{
    bool success = true;
    encDec_14bit_t encDec_14bit;
    int32_t readValue = 0;

    switch(block)
    {
        case SYSEX_BLOCK_LEDS:
        #ifdef LEDS_SUPPORTED
        switch(section)
        {
            case sysExSection_leds_testColor:
            readValue = leds.getColor(index);
            break;

            case sysExSection_leds_testBlink:
            readValue = leds.getBlinkState(index);
            break;

            case sysExSection_leds_midiChannel:
            success = database.read(block, sysEx2DB_leds[section], index, readValue);

            //channels start from 0 in db, start from 1 in sysex
            if (success)
                readValue++;
            break;

            case sysExSection_leds_rgbEnable:
            success = database.read(block, sysEx2DB_leds[section], board.getRGBID(index), readValue);
            break;

            default:
            success = database.read(block, sysEx2DB_leds[section], index, readValue);
            break;
        }
        #else
        setError(ERROR_NOT_SUPPORTED);
        success = false;
        #endif
        break;

        case SYSEX_BLOCK_ANALOG:
        switch(section)
        {
            case sysExSection_analog_midiID_LSB:
            case sysExSection_analog_midiID_MSB:
            case sysExSection_analog_lowerLimit_LSB:
            case sysExSection_analog_lowerLimit_MSB:
            case sysExSection_analog_upperLimit_LSB:
            case sysExSection_analog_upperLimit_MSB:
            success = database.read(block, sysEx2DB_analog[section], index, readValue);

            if (success)
            {
                encDec_14bit.value = readValue;
                encDec_14bit.split14bit();

                switch(section)
                {
                    case sysExSection_analog_midiID_LSB:
                    case sysExSection_analog_lowerLimit_LSB:
                    case sysExSection_analog_upperLimit_LSB:
                    readValue = encDec_14bit.low;
                    break;

                    default:
                    readValue = encDec_14bit.high;
                    break;
                }
            }
            break;

            case sysExSection_analog_midiChannel:
            success = database.read(block, sysEx2DB_analog[section], index, readValue);

            //channels start from 0 in db, start from 1 in sysex
            if (success)
                readValue++;
            break;

            default:
            success = database.read(block, sysEx2DB_analog[section], index, readValue);
            break;
        }
        break;

        case SYSEX_BLOCK_BUTTONS:
        success = database.read(block, sysEx2DB_buttons[section], index, readValue);

        //channels start from 0 in db, start from 1 in sysex
        if ((section == sysExSection_buttons_midiChannel) && success)
            readValue++;
        break;

        case SYSEX_BLOCK_ENCODERS:
        success = database.read(block, sysEx2DB_encoders[section], index, readValue);

        //channels start from 0 in db, start from 1 in sysex
        if ((section == sysExSection_encoders_midiChannel) && success)
            readValue++;
        break;

        case DB_BLOCK_DISPLAY:
        #ifdef DISPLAY_SUPPORTED
        success = database.read(block, sysEx2DB_display[section], index, readValue);
        #else
        setError(ERROR_NOT_SUPPORTED);
        success = false;
        #endif
        break;

        default:
        success = database.read(block, section, index, readValue);
        break;
    }

    #ifdef DISPLAY_SUPPORTED
    display.displayMIDIevent(displayEventIn, midiMessageSystemExclusive_display, 0, 0, 0);
    #endif

    if (success)
    {
        value = readValue;
        return true;
    }
    else
    {
        value = 0;
        return false;
    }
}

bool SysConfig::onSet(uint8_t block, uint8_t section, uint16_t index, sysExParameter_t newValue)
{
    bool success = false;
    bool writeToDb = true;
    encDec_14bit_t encDec_14bit;

    switch(block)
    {
        case SYSEX_BLOCK_ANALOG:
        switch(section)
        {
            case sysExSection_analog_midiID_LSB:
            case sysExSection_analog_midiID_MSB:
            case sysExSection_analog_lowerLimit_LSB:
            case sysExSection_analog_lowerLimit_MSB:
            case sysExSection_analog_upperLimit_LSB:
            case sysExSection_analog_upperLimit_MSB:
            encDec_14bit.value = database.read(block, sysEx2DB_analog[section], index);
            encDec_14bit.split14bit();

            switch(section)
            {
                case sysExSection_analog_midiID_LSB:
                case sysExSection_analog_lowerLimit_LSB:
                case sysExSection_analog_upperLimit_LSB:
                encDec_14bit.low = newValue;
                break;

                default:
                encDec_14bit.high = newValue;
                break;
            }

            encDec_14bit.mergeTo14bit();
            success = database.update(block, sysEx2DB_analog[section], index, encDec_14bit.value);
            break;

            case sysExSection_analog_type:
            analog.debounceReset(index);
            success = database.update(block, sysEx2DB_analog[section], index, newValue);
            break;

            default:
            //channels start from 0 in db, start from 1 in sysex
            if (section == sysExSection_analog_midiChannel)
                newValue--;

            success = database.update(block, sysEx2DB_analog[section], index, newValue);
            break;
        }
        break;

        case SYSEX_BLOCK_MIDI:
        if (section == sysExSection_midi_feature)
        {
            switch(index)
            {
                case midiFeatureRunningStatus:
                #ifndef DIN_MIDI_SUPPORTED
                setError(ERROR_NOT_SUPPORTED);
                #else
                midi.setRunningStatusState(newValue);
                success = true;
                #endif
                break;

                case midiFeatureStandardNoteOff:
                newValue ? midi.setNoteOffMode(noteOffType_standardNoteOff) : midi.setNoteOffMode(noteOffType_noteOnZeroVel);
                success = true;
                break;

                case midiFeatureDinEnabled:
                #ifndef DIN_MIDI_SUPPORTED
                setError(ERROR_NOT_SUPPORTED);
                #else
                newValue ? setupMIDIoverUART() : board.resetUART(UART_MIDI_CHANNEL);
                success = true;
                #endif
                break;

                case midiFeatureMergeEnabled:
                #ifndef DIN_MIDI_SUPPORTED
                setError(ERROR_NOT_SUPPORTED);
                #else
                if (database.read(DB_BLOCK_MIDI, dbSection_midi_feature, midiFeatureDinEnabled))
                {
                    success = true;
                    //use recursive parsing when merging is active
                    midi.useRecursiveParsing(newValue);
                }
                else
                {
                    //invalid configuration - trying to configure merge functionality while din midi is disabled
                    setError(ERROR_WRITE);
                }
                #endif
                break;

                default:
                break;
            }
        }
        else if (section == sysExSection_midi_merge)
        {
            #ifndef DIN_MIDI_SUPPORTED
            setError(ERROR_NOT_SUPPORTED);
            #else
            switch(index)
            {
                case midiMergeType:
                if (database.read(DB_BLOCK_MIDI, dbSection_midi_feature, midiFeatureDinEnabled) && database.read(DB_BLOCK_MIDI, dbSection_midi_feature, midiFeatureMergeEnabled))
                {
                    if ((newValue >= 0) && (newValue < MIDI_MERGE_TYPES))
                    {
                        success = true;
                        configureMIDImerge((midiMergeType_t)newValue);
                    }
                    else
                    {
                        setError(ERROR_NEW_VALUE);
                    }
                }
                else
                {
                    //invalid configuration
                    setError(ERROR_WRITE);
                }
                break;

                case midiMergeUSBchannel:
                case midiMergeDINchannel:
                //unused for now
                writeToDb = false;
                success = true;
                break;

                default:
                break;
            }
            #endif
        }

        if (success && writeToDb)
            success = database.update(block, sysEx2DB_midi[section], index, newValue);
        break;

        case SYSEX_BLOCK_LEDS:
        #ifdef LEDS_SUPPORTED
        switch(section)
        {
            case sysExSection_leds_testColor:
            //no writing to database
            leds.setColor(index, (ledColor_t)newValue);
            success = true;
            writeToDb = false;
            break;

            case sysExSection_leds_testBlink:
            //no writing to database
            leds.setBlinkState(index, newValue ? blinkSpeed_500ms : blinkSpeed_noBlink);
            success = true;
            writeToDb = false;
            break;

            case sysExSection_leds_global:
            switch(index)
            {
                case ledGlobalParam_blinkMIDIclock:
                if ((newValue <= 1) && (newValue >= 0))
                {
                    success = true;
                    leds.setBlinkType((blinkType_t)newValue);
                }
                break;

                case ledGlobalParam_startUpRoutineState:
                if ((newValue <= 1) && (newValue >= 0))
                    success = true;
                break;

                case ledGlobalParam_fadeSpeed:
                #ifdef LED_FADING_SUPPORTED
                if ((newValue >= FADE_TIME_MIN) && (newValue <= FADE_TIME_MAX))
                {
                    leds.setFadeTime(newValue);
                    success = true;
                }
                #else
                setError(ERROR_NOT_SUPPORTED);
                #endif
                break;

                default:
                break;
            }

            //write to db if success is true and writing should take place
            if (success && writeToDb)
                success = database.update(block, sysEx2DB_leds[section], index, newValue);
            break;

            case sysExSection_leds_rgbEnable:
            //make sure to turn all three leds off before setting new state
            leds.setColor(board.getRGBaddress(board.getRGBID(index), rgb_R), colorOff);
            leds.setColor(board.getRGBaddress(board.getRGBID(index), rgb_G), colorOff);
            leds.setColor(board.getRGBaddress(board.getRGBID(index), rgb_B), colorOff);

            //write rgb enabled bit to led
            success = database.update(block, sysEx2DB_leds[section], board.getRGBID(index), newValue);

            if (newValue && success)
            {
                //copy over note activation local control and midi channel settings to all three leds from the current led index

                for (int i=0; i<3; i++)
                {
                    success = database.update(block, sysEx2DB_leds[sysExSection_leds_activationID], board.getRGBaddress(board.getRGBID(index), (rgbIndex_t)i), database.read(block, sysEx2DB_leds[sysExSection_leds_activationID], index));

                    if (!success)
                        break;

                    success = database.update(block, sysEx2DB_leds[sysExSection_leds_controlType], board.getRGBaddress(board.getRGBID(index), (rgbIndex_t)i), database.read(block, sysEx2DB_leds[sysExSection_leds_controlType], index));

                    if (!success)
                        break;

                    success = database.update(block, sysEx2DB_leds[sysExSection_leds_midiChannel], board.getRGBaddress(board.getRGBID(index), (rgbIndex_t)i), database.read(block, sysEx2DB_leds[sysExSection_leds_midiChannel], index));

                    if (!success)
                        break;
                }
            }
            break;

            case sysExSection_leds_activationID:
            case sysExSection_leds_controlType:
            case sysExSection_leds_midiChannel:

            //channels start from 0 in db, start from 1 in sysex
            if (section == sysExSection_leds_midiChannel)
                newValue--;

            //first, find out if RGB led is enabled for this led index
            if (database.read(block, sysEx2DB_leds[sysExSection_leds_rgbEnable], board.getRGBID(index)))
            {
                //rgb led enabled - copy these settings to all three leds
                for (int i=0; i<3; i++)
                {
                    success = database.update(block, sysEx2DB_leds[section], board.getRGBaddress(board.getRGBID(index), (rgbIndex_t)i), newValue);

                    if (!success)
                        break;
                }
            }
            else
            {
                //apply to single led only
                success = database.update(block, sysEx2DB_leds[section], index, newValue);
            }
            break;

            default:
            success = database.update(block, sysEx2DB_leds[section], index, newValue);
            break;
        }
        #else
        setError(ERROR_NOT_SUPPORTED);
        success = false;
        #endif
        break;

        case SYSEX_BLOCK_DISPLAY:
        #ifdef DISPLAY_SUPPORTED
        switch(section)
        {
            case sysExSection_display_features:
            switch(index)
            {
                case displayFeatureEnable:
                if ((newValue <= 1) && (newValue >= 0))
                {
                    success = true;

                    if (newValue)
                        display.init((displayController_t)database.read(DB_BLOCK_DISPLAY, dbSection_display_hw, displayHwController), (displayResolution_t)database.read(DB_BLOCK_DISPLAY, dbSection_display_hw, displayHwResolution));
                    else //init with invalid configuration
                        display.init(DISPLAY_CONTROLLERS, DISPLAY_RESOLUTIONS);
                }
                break;

                case displayFeatureWelcomeMsg:
                case displayFeatureVInfoMsg:
                if ((newValue <= 1) && (newValue >= 0))
                {
                    success = true;
                    //do nothing, these values are checked on startup
                }
                break;

                case displayFeatureMIDIeventRetention:
                if ((newValue <= 1) && (newValue >= 0))
                {
                    display.setRetentionState(newValue);
                    success = true;
                }
                break;

                case displayFeatureMIDInotesAlternate:
                if ((newValue <= 1) && (newValue >= 0))
                {
                    display.setAlternateNoteDisplay(newValue);
                    success = true;
                }
                break;

                case displayFeatureMIDIeventTime:
                if ((newValue >= MIN_MESSAGE_RETENTION_TIME) && (newValue <= MAX_MESSAGE_RETENTION_TIME))
                {
                    display.setRetentionTime(newValue * 1000);
                    success = true;
                }
                break;

                default:
                break;
            }
            break;

            case sysExSection_display_hw:
            switch(index)
            {
                case displayHwController:
                if ((newValue <= DISPLAY_CONTROLLERS) && (newValue >= 0))
                {
                    if (display.init((displayController_t)newValue, (displayResolution_t)database.read(DB_BLOCK_DISPLAY, sysEx2DB_display[section], displayHwResolution)))
                    {
                        display.displayHome();
                    }

                    success = true;
                }
                break;

                case displayHwResolution:
                if ((newValue <= DISPLAY_RESOLUTIONS) && (newValue >= 0))
                {
                    if (display.init((displayController_t)database.read(DB_BLOCK_DISPLAY, sysEx2DB_display[section], displayHwController), (displayResolution_t)newValue))
                    {
                        display.displayHome();
                    }

                    success = true;
                }
                break;

                default:
                break;
            }
            break;

            default:
            break;
        }
        #else
        setError(ERROR_NOT_SUPPORTED);
        #endif

        if (success)
            success = database.update(block, sysEx2DB_display[section], index, newValue);
        break;

        case SYSEX_BLOCK_BUTTONS:
        //channels start from 0 in db, start from 1 in sysex
        if (section == sysExSection_buttons_midiChannel)
            newValue--;

        success = database.update(block, sysEx2DB_buttons[section], index, newValue);
        break;

        case SYSEX_BLOCK_ENCODERS:
        //channels start from 0 in db, start from 1 in sysex
        if (section == sysExSection_encoders_midiChannel)
            newValue--;

        success = database.update(block, sysEx2DB_encoders[section], index, newValue);
        break;

        default:
        //rest of the blocks - no custom handling
        success = database.update(block, section, index, newValue);
        break;
    }

    if (success)
    {
        #ifdef DISPLAY_SUPPORTED
        display.displayMIDIevent(displayEventIn, midiMessageSystemExclusive_display, 0, 0, 0);
        #endif

        return true;
    }

    return false;
}

void SysConfig::onWrite(uint8_t sysExArray[], uint8_t arraysize)
{
    midi.sendSysEx(arraysize, sysExArray, true);
}

void SysConfig::init()
{
    setLayout(sysExLayout, SYSEX_BLOCKS);
    setupCustomRequests(customRequests, NUMBER_OF_CUSTOM_REQUESTS);
}

bool SysConfig::isProcessingEnabled()
{
    return processingEnabled;
}

#ifdef DIN_MIDI_SUPPORTED
void SysConfig::setupMIDIoverUART()
{
    board.initUART(UART_BAUDRATE_MIDI_STD, UART_MIDI_CHANNEL);

    midi.handleUARTread([](uint8_t &data)
    {
        return Board::uartRead(UART_MIDI_CHANNEL, data);
    });

    midi.handleUARTwrite([](uint8_t data)
    {
        return Board::uartWrite(UART_MIDI_CHANNEL, data);
    });
}
#endif

void SysConfig::setupMIDIoverUSB()
{
    #ifdef USB_SUPPORTED
    midi.handleUSBread(Board::usbReadMIDI);
    midi.handleUSBwrite(Board::usbWriteMIDI);
    #else
    //enable uart-to-usb link when usb isn't supported directly
    board.initUART(UART_BAUDRATE_MIDI_OD, UART_USB_LINK_CHANNEL);

    midi.handleUSBread([](USBMIDIpacket_t& USBMIDIpacket)
    {
        return Board::uartReadMIDI_OD(UART_USB_LINK_CHANNEL, USBMIDIpacket);
    });

    midi.handleUSBwrite([](USBMIDIpacket_t& USBMIDIpacket)
    {
        return Board::uartWriteMIDI_OD(UART_USB_LINK_CHANNEL, USBMIDIpacket);
    });
    #endif
}

bool SysConfig::sendCInfo(dbBlockID_t dbBlock, sysExParameter_t componentID)
{
    if (isConfigurationEnabled())
    {
        if ((rTimeMs() - lastCinfoMsgTime[dbBlock]) > COMPONENT_INFO_TIMEOUT)
        {
            sysExParameter_t cInfoMessage[] =
            {
                SYSEX_CM_COMPONENT_ID,
                (sysExParameter_t)dbBlock,
                (sysExParameter_t)componentID
            };

            sendCustomMessage(midi.usbMessage.sysexArray, cInfoMessage, 3);
            lastCinfoMsgTime[(uint8_t)dbBlock] = rTimeMs();
        }

        return true;
    }

    return false;
}

void SysConfig::configureMIDI()
{
    midi.setInputChannel(MIDI_CHANNEL_OMNI);
    midi.setNoteOffMode((noteOffType_t)database.read(DB_BLOCK_MIDI, dbSection_midi_feature, midiFeatureStandardNoteOff));
    midi.setRunningStatusState(database.read(DB_BLOCK_MIDI, dbSection_midi_feature, midiFeatureRunningStatus));
    midi.setChannelSendZeroStart(true);

    setupMIDIoverUSB();

    #ifdef DIN_MIDI_SUPPORTED
    if (database.read(DB_BLOCK_MIDI, dbSection_midi_feature, midiFeatureDinEnabled))
    {
        //use recursive parsing when merging is active
        midi.useRecursiveParsing(database.read(DB_BLOCK_MIDI, dbSection_midi_feature, midiFeatureMergeEnabled));

        if (database.read(DB_BLOCK_MIDI, dbSection_midi_feature, midiFeatureMergeEnabled))
            configureMIDImerge((midiMergeType_t)database.read(DB_BLOCK_MIDI, dbSection_midi_merge, midiMergeType));
        else
            setupMIDIoverUART();
    }
    else
    {
        board.resetUART(UART_MIDI_CHANNEL);
        midi.handleUARTread(nullptr);
        midi.handleUARTwrite(nullptr);
    }
    #endif
}

#ifdef DIN_MIDI_SUPPORTED
void SysConfig::configureMIDImerge(midiMergeType_t mergeType)
{
    switch(mergeType)
    {
        case midiMergeODmaster:
        board.setUARTloopbackState(UART_MIDI_CHANNEL, false);
        board.initUART(UART_BAUDRATE_MIDI_OD, UART_MIDI_CHANNEL);
        //read usb midi data and forward it to uart in od format
        //also use it to forward all incoming data from other boards to usb
        //write standard usb midi data
        midi.handleUSBread([](USBMIDIpacket_t& USBMIDIpacket)
        {
            //dump everything from MIDI in to USB MIDI out
            USBMIDIpacket_t slavePacket;

            if (Board::uartReadMIDI_OD(UART_MIDI_CHANNEL, slavePacket))
                #ifdef USB_SUPPORTED
                Board::usbWriteMIDI(slavePacket);
                #else
                Board::uartWriteMIDI_OD(UART_USB_LINK_CHANNEL, slavePacket);
                #endif

            #ifdef USB_SUPPORTED
            if (Board::usbReadMIDI(USBMIDIpacket))
            {
                Board::uartWriteMIDI_OD(UART_MIDI_CHANNEL, USBMIDIpacket);
                return true;
            }
            #else
            if (Board::uartReadMIDI_OD(UART_USB_LINK_CHANNEL, USBMIDIpacket))
            {
                Board::uartWriteMIDI_OD(UART_MIDI_CHANNEL, USBMIDIpacket);
                return true;
            }
            #endif

            return false;
        });

        #ifdef USB_SUPPORTED
        midi.handleUSBwrite(Board::usbWriteMIDI);
        #else
        midi.handleUSBwrite([](USBMIDIpacket_t& USBMIDIpacket)
        {
            return Board::uartWriteMIDI_OD(UART_USB_LINK_CHANNEL, USBMIDIpacket);
        });
        #endif
        //unused
        midi.handleUARTread(nullptr);
        midi.handleUARTwrite(nullptr);
        break;

        case midiMergeODslave:
        board.initUART(UART_BAUDRATE_MIDI_OD, UART_MIDI_CHANNEL);
        board.setUARTloopbackState(UART_MIDI_CHANNEL, false);
        midi.handleUSBread([](USBMIDIpacket_t& USBMIDIpacket)
        {
            if (Board::uartReadMIDI_OD(UART_MIDI_CHANNEL, USBMIDIpacket))
            {
                Board::uartWriteMIDI_OD(UART_MIDI_CHANNEL, USBMIDIpacket);
                return true;
            }
            else
            {
                return false;
            }
        });

        //write data to uart (opendeck format)
        midi.handleUSBwrite([](USBMIDIpacket_t& USBMIDIpacket)
        {
            return Board::uartWriteMIDI_OD(UART_MIDI_CHANNEL, USBMIDIpacket);
        });

        //no need for uart handlers
        midi.handleUARTread(nullptr);
        midi.handleUARTwrite(nullptr);
        break;

        case midiMergeDINtoDIN:
        //handle traffic directly in isr
        Board::setUARTloopbackState(UART_MIDI_CHANNEL, true);
        //no need for uart handlers
        midi.handleUARTread(nullptr);
        midi.handleUARTwrite(nullptr);
        break;

        case midiMergeDINtoUSB:
        setupMIDIoverUSB();
        setupMIDIoverUART();
        break;

        default:
        break;
    }
}
#endif