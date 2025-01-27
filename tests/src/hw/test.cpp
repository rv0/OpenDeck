#ifndef USB_LINK_MCU
#ifdef HW_TESTS_SUPPORTED

#include "unity/Framework.h"
#include <string>
#include <filesystem>
#include "helpers/Misc.h"
#include "helpers/MIDI.h"
#include "helpers/Serial.h"
#include "application/database/Database.h"
#include "stubs/database/DB_ReadWrite.h"
#include <HWTestDefines.h>
#include "system/Builder.h"

namespace
{
    const std::string flash_cmd = "make -C ../src flash";

    enum class powerCycleType_t : uint8_t
    {
        standard,
        standardWithDeviceCheck
    };

    const std::string handshake_req            = "F0 00 53 43 00 00 01 F7";
    const std::string reboot_req               = "F0 00 53 43 00 00 7F F7";
    const std::string handshake_ack            = "F0 00 53 43 01 00 01 F7";
    const std::string factory_reset_req        = "F0 00 53 43 00 00 44 F7";
    const std::string btldr_req                = "F0 00 53 43 00 00 55 F7";
    const std::string backup_req               = "F0 00 53 43 00 00 1B F7";
    const std::string usb_power_off_cmd        = "echo write_high 1 > /dev/actl";
    const std::string usb_power_on_cmd         = "echo write_low 1 > /dev/actl";
    const std::string sysex_fw_update_delay_ms = "5";
    const uint32_t    startup_delay_ms         = 10000;
    const std::string fw_build_dir             = "../src/build/merged/";
    const std::string fw_build_type_subdir     = "release/";
    const std::string temp_midi_data_location  = "/tmp/temp_midi_data";
    const std::string backup_file_location     = "/tmp/backup.txt";
    bool              powerStatus              = false;

    DBstorageMock dbStorageMock;
    Database      database = Database(dbStorageMock, false);

    void powerOn()
    {
        // Send newline to arduino controller to make sure on/off commands
        // are properly parsed.
        TEST_ASSERT_EQUAL_INT(0, test::wsystem("echo > /dev/actl"));

        if (!powerStatus)
        {
            LOG(INFO) << "Turning USB devices on";
            TEST_ASSERT_EQUAL_INT(0, test::wsystem(usb_power_on_cmd));

            LOG(INFO) << "Waiting " << startup_delay_ms << " ms";
            test::sleepMs(startup_delay_ms);

            powerStatus = true;
        }
        else
        {
            LOG(INFO) << "Power already turned on, skipping";
        }
    }

    void powerOff()
    {
        // Send newline to arduino controller to make sure on/off commands
        // are properly parsed.
        TEST_ASSERT_EQUAL_INT(0, test::wsystem("echo > /dev/actl"));

        if (powerStatus)
        {
            LOG(INFO) << "Turning USB devices off";
            TEST_ASSERT_EQUAL_INT(0, test::wsystem(usb_power_off_cmd));

            LOG(INFO) << "Waiting " << startup_delay_ms / 2 << " ms";
            test::sleepMs(startup_delay_ms / 2);

            powerStatus = false;
        }
        else
        {
            LOG(INFO) << "Power already turned off, skipping";
        }
    }

    void cyclePower(powerCycleType_t powerCycleType)
    {
        auto cycle = [&]() {
            powerOff();
            powerOn();
        };

        if (powerCycleType != powerCycleType_t::standardWithDeviceCheck)
        {
            LOG(INFO) << "Cycling power without device check";
            cycle();
        }
        else
        {
            LOG(INFO) << "Cycling power with device check";

            // ensure the device is present
            do
            {
                cycle();
            } while (!MIDIHelper::devicePresent());
        }
    }

    void handshake()
    {
        LOG(INFO) << "Sending handshake";

        if (handshake_ack != MIDIHelper::sendRawSysEx(handshake_req))
        {
            LOG(ERROR) << "OpenDeck device not responding to handshake, attempting power cycle";
            cyclePower(powerCycleType_t::standardWithDeviceCheck);

            if (!MIDIHelper::devicePresent())
            {
                LOG(ERROR) << "OpenDeck device not found after power cycle";
                TEST_ASSERT(false == true);
            }
            else
            {
                if (handshake_ack != MIDIHelper::sendRawSysEx(handshake_req))
                {
                    LOG(ERROR) << "OpenDeck device not responding to handshake even after power cycle";
                    TEST_ASSERT(false == true);
                }
            }
        }
    }

    void reboot()
    {
        handshake();

        LOG(INFO) << "Reboting the device";
        TEST_ASSERT(std::string("") == MIDIHelper::sendRawSysEx(reboot_req, false));

        LOG(INFO) << "Waiting " << startup_delay_ms << " ms";
        test::sleepMs(startup_delay_ms);

        if (!MIDIHelper::devicePresent())
        {
            LOG(ERROR) << "OpenDeck device not found after reboot, attempting power cycle";
            cyclePower(powerCycleType_t::standardWithDeviceCheck);

            if (!MIDIHelper::devicePresent())
            {
                LOG(ERROR) << "OpenDeck device not found after power cycle";
                TEST_ASSERT(false == true);
            }
        }
        else
        {
            LOG(INFO) << "Device rebooted";
        }

        handshake();
        MIDIHelper::flush();
    }

    void factoryReset()
    {
        handshake();

        LOG(INFO) << "Performing factory reset";
        TEST_ASSERT(std::string("") == MIDIHelper::sendRawSysEx(factory_reset_req, false));

        LOG(INFO) << "Waiting " << startup_delay_ms << " ms";
        test::sleepMs(startup_delay_ms);

        if (!MIDIHelper::devicePresent())
        {
            LOG(ERROR) << "OpenDeck device not found after factory reset, attempting power cycle";
            cyclePower(powerCycleType_t::standardWithDeviceCheck);

            if (!MIDIHelper::devicePresent())
            {
                LOG(ERROR) << "OpenDeck device not found after power cycle";
                TEST_ASSERT(false == true);
            }
        }
        else
        {
            LOG(INFO) << "Factory reset complete";
        }

        handshake();
        MIDIHelper::flush();
    }

    void flash()
    {
        auto flash = [](std::string target, std::string args) {
            const size_t ALLOWED_REPEATS = 2;
            int          result          = -1;
            std::string  flashTarget     = " TARGET=" + target;

            for (int i = 0; i < ALLOWED_REPEATS; i++)
            {
                LOG(INFO) << "Flashing the device, attempt " << i + 1;
                result = test::wsystem(flash_cmd + flashTarget + " " + args);

                if (result)
                {
                    LOG(ERROR) << "Flashing failed";
                    cyclePower(powerCycleType_t::standard);
                }
                else
                {
                    LOG(INFO) << "Flashing successful";
                    break;
                }
            }

            return result;
        };

        TEST_ASSERT_EQUAL_INT32(0, flash(std::string(BOARD_STRING), std::string(FLASH_ARGS)));

#ifndef USB_SUPPORTED
        LOG(INFO) << "Flashing USB Link MCU";
        flash(std::string(USB_LINK_TARGET), std::string(FLASH_ARGS_USB_LINK));
#endif

        LOG(INFO) << "Waiting " << startup_delay_ms << " ms";
        test::sleepMs(startup_delay_ms);

        cyclePower(powerCycleType_t::standardWithDeviceCheck);
    }
}    // namespace

TEST_SETUP()
{
    LOG(INFO) << "Setting up test";

#ifdef TEST_FLASHING
    static bool flashed = false;
#endif

    // dummy db - used only to retrieve correct amount of supported presets
    TEST_ASSERT(database.init() == true);

#ifdef TEST_FLASHING
    if (!flashed)
    {
        cyclePower(powerCycleType_t::standard);
        flash();
        flashed = true;
    }
    else
#endif
    {
        cyclePower(powerCycleType_t::standardWithDeviceCheck);
    }

    factoryReset();
}

TEST_TEARDOWN()
{
    LOG(INFO) << "Tearing down test";

    test::wsystem("rm -f " + temp_midi_data_location);
    test::wsystem("rm -f " + backup_file_location);
    test::wsystem("killall amidi > /dev/null 2>&1");
    test::wsystem("killall olad > /dev/null 2>&1");
    test::wsystem("killall ssterm > /dev/null 2>&1");

    std::string cmdResponse;

    if (test::wsystem("pgrep ssterm", cmdResponse) == 0)
    {
        LOG(INFO) << "Waiting for ssterm process to be terminated";

        while (test::wsystem("pgrep ssterm", cmdResponse) == 0)
            ;
    }

    if (test::wsystem("pgrep olad", cmdResponse) == 0)
    {
        LOG(INFO) << "Waiting for olad process to be terminated";

        while (test::wsystem("pgrep olad", cmdResponse) == 0)
            ;
    }

    powerOff();
}

TEST_CASE(DatabaseInitialValues)
{
    constexpr size_t PARAM_SKIP = 2;

    // check only first and the last preset
    for (int preset = 0; preset < database.getSupportedPresets(); preset += (database.getSupportedPresets() - 1))
    {
        LOG(INFO) << "Checking initial values for preset " << preset + 1;
        TEST_ASSERT(MIDIHelper::setSingleSysExReq(System::Config::Section::global_t::presets, 0, preset) == true);
        TEST_ASSERT_EQUAL_UINT32(preset, MIDIHelper::readFromDevice(System::Config::Section::global_t::presets, 0));

        // MIDI block
        //----------------------------------
        // feature section
        // all values should be set to 0
        for (size_t i = 0; i < static_cast<uint8_t>(Protocol::MIDI::feature_t::AMOUNT); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::global_t::midiFeatures, i));

        // merge section
        // all values should be set to 0
        TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::global_t::midiMerge, static_cast<size_t>(Protocol::MIDI::mergeSetting_t::mergeType)));
        TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::global_t::midiMerge, static_cast<size_t>(Protocol::MIDI::mergeSetting_t::mergeUSBchannel)));
        TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::global_t::midiMerge, static_cast<size_t>(Protocol::MIDI::mergeSetting_t::mergeDINchannel)));

        // button block
        //----------------------------------
        // type section
        // all values should be set to 0 (default type)
        for (size_t i = 0; i < IO::Buttons::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::button_t::type, i));

        // midi message section
        // all values should be set to 0 (default/note)
        for (size_t i = 0; i < IO::Buttons::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::button_t::midiMessage, i));

        // midi id section
        // incremental values - first value should be 0, each successive value should be incremented by 1 for each group
        for (size_t group = 0; group < IO::Buttons::Collection::groups(); group++)
        {
            for (size_t i = 0; i < IO::Buttons::Collection::size(group); i += PARAM_SKIP)
                TEST_ASSERT_EQUAL_UINT32(i, MIDIHelper::readFromDevice(System::Config::Section::button_t::midiID, i + IO::Buttons::Collection::startIndex(group)));
        }

        // midi velocity section
        // all values should be set to 127
        for (size_t i = 0; i < IO::Buttons::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(127, MIDIHelper::readFromDevice(System::Config::Section::button_t::velocity, i));

        // midi channel section
        // all values should be set to 0
        // note: midi channels are in range 1-16 via sysex and written in range 0-15 in db
        for (size_t i = 0; i < IO::Buttons::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(1, MIDIHelper::readFromDevice(System::Config::Section::button_t::midiChannel, i));

        // encoders block
        //----------------------------------
        // enable section
        // all values should be set to 0
        for (size_t i = 0; i < IO::Encoders::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::encoder_t::enable, i));

        // invert section
        // all values should be set to 0
        for (size_t i = 0; i < IO::Encoders::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::encoder_t::invert, i));

        // mode section
        // all values should be set to 0
        for (size_t i = 0; i < IO::Encoders::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::encoder_t::mode, i));

        // midi id section
        for (size_t i = 0; i < IO::Encoders::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(i, MIDIHelper::readFromDevice(System::Config::Section::encoder_t::midiID, i));

        // midi channel section
        // all values should be set to 0
        // note: midi channels are in range 1-16 via sysex and written in range 0-15 in db
        for (size_t i = 0; i < IO::Encoders::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(1, MIDIHelper::readFromDevice(System::Config::Section::encoder_t::midiChannel, i));

        // pulses per step section
        // all values should be set to 4
        for (size_t i = 0; i < IO::Encoders::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(4, MIDIHelper::readFromDevice(System::Config::Section::encoder_t::pulsesPerStep, i));

        // analog block
        //----------------------------------
        // enable section
        // all values should be set to 0
        for (size_t i = 0; i < IO::Analog::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::analog_t::enable, i));

        // invert section
        // all values should be set to 0
        for (size_t i = 0; i < IO::Analog::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::analog_t::invert, i));

        // type section
        // all values should be set to 0
        for (size_t i = 0; i < IO::Analog::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::analog_t::invert, i));

        // midi id section
        // incremental values - first value should be 0, each successive value should be incremented by 1 for each group
        for (size_t group = 0; group < IO::Analog::Collection::groups(); group++)
        {
            for (size_t i = 0; i < IO::Analog::Collection::size(group); i += PARAM_SKIP)
                TEST_ASSERT_EQUAL_UINT32(i, MIDIHelper::readFromDevice(System::Config::Section::analog_t::midiID, i + IO::Analog::Collection::startIndex(group)));
        }

        // lower limit section
        // all values should be set to 0
        for (size_t i = 0; i < IO::Analog::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::analog_t::lowerLimit, i));

        // upper limit section
        // all values should be set to 16383
        for (size_t i = 0; i < IO::Analog::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(16383, MIDIHelper::readFromDevice(System::Config::Section::analog_t::upperLimit, i));

        // midi channel section
        // all values should be set to 0
        // note: midi channels are in range 1-16 via sysex and written in range 0-15 in db
        for (size_t i = 0; i < IO::Analog::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(1, MIDIHelper::readFromDevice(System::Config::Section::analog_t::midiChannel, i));

        // LED block
        //----------------------------------
        // global section
        // all values should be set to 0
        for (size_t i = 0; i < static_cast<uint8_t>(IO::LEDs::setting_t::AMOUNT); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::leds_t::global, i));

        // activation id section
        // incremental values - first value should be 0, each successive value should be incremented by 1 for each group
        for (size_t group = 0; group < IO::LEDs::Collection::groups(); group++)
        {
            for (size_t i = 0; i < IO::LEDs::Collection::size(group); i += PARAM_SKIP)
                TEST_ASSERT_EQUAL_UINT32(i, MIDIHelper::readFromDevice(System::Config::Section::leds_t::activationID, i + IO::LEDs::Collection::startIndex(group)));
        }

        // rgb enable section
        // all values should be set to 0
        for (size_t i = 0; i < IO::LEDs::Collection::size() / 3 + (IO::Touchscreen::Collection::size() / 3); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::leds_t::rgbEnable, i));

        // control type section
        // all values should be set to midiInNoteMultiVal
        for (size_t i = 0; i < IO::LEDs::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(IO::LEDs::controlType_t::midiInNoteMultiVal), MIDIHelper::readFromDevice(System::Config::Section::leds_t::controlType, i));

        // activation value section
        // all values should be set to 127
        for (size_t i = 0; i < IO::LEDs::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(127, MIDIHelper::readFromDevice(System::Config::Section::leds_t::activationValue, i));

        // midi channel section
        // all values should be set to 0
        // note: midi channels are in range 1-16 via sysex and written in range 0-15 in db
        for (size_t i = 0; i < IO::LEDs::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(1, MIDIHelper::readFromDevice(System::Config::Section::leds_t::midiChannel, i));

#ifdef I2C_SUPPORTED
        // i2c block
        //----------------------------------
        // display section
        TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::i2c_t::display, static_cast<size_t>(IO::Display::setting_t::enable)));
        TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::i2c_t::display, static_cast<size_t>(IO::Display::setting_t::deviceInfoMsg)));
        TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::i2c_t::display, static_cast<size_t>(IO::Display::setting_t::controller)));
        TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::i2c_t::display, static_cast<size_t>(IO::Display::setting_t::resolution)));
        TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::i2c_t::display, static_cast<size_t>(IO::Display::setting_t::MIDIeventTime)));
        TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::i2c_t::display, static_cast<size_t>(IO::Display::setting_t::MIDInotesAlternate)));
        TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::i2c_t::display, static_cast<size_t>(IO::Display::setting_t::octaveNormalization)));
#endif

#ifdef TOUCHSCREEN_SUPPORTED
        // touchscreen block
        //----------------------------------
        // setting section
        // all values should be set to 0
        for (size_t i = 0; i < static_cast<uint8_t>(IO::Touchscreen::setting_t::AMOUNT); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::touchscreen_t::setting, i));

        // x position section
        // all values should be set to 0
        for (size_t i = 0; i < IO::Touchscreen::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::touchscreen_t::xPos, i));

        // y position section
        // all values should be set to 0
        for (size_t i = 0; i < IO::Touchscreen::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::touchscreen_t::yPos, i));

        // width section
        // all values should be set to 0
        for (size_t i = 0; i < IO::Touchscreen::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::touchscreen_t::width, i));

        // height section
        // all values should be set to 0
        for (size_t i = 0; i < IO::Touchscreen::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::touchscreen_t::height, i));

        // on screen section
        // all values should be set to 0
        for (size_t i = 0; i < IO::Touchscreen::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::touchscreen_t::onScreen, i));

        // off screen section
        // all values should be set to 0
        for (size_t i = 0; i < IO::Touchscreen::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::touchscreen_t::offScreen, i));

        // page switch enabled section
        // all values should be set to 0
        for (size_t i = 0; i < IO::Touchscreen::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::touchscreen_t::pageSwitchEnabled, i));

        // page switch index section
        // all values should be set to 0
        for (size_t i = 0; i < IO::Touchscreen::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::touchscreen_t::pageSwitchIndex, i));

        // analog page section
        // all values should be set to 0
        for (size_t i = 0; i < IO::Touchscreen::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::touchscreen_t::analogPage, i));

        // analog start x coordinate section
        // all values should be set to 0
        for (size_t i = 0; i < IO::Touchscreen::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::touchscreen_t::analogStartXCoordinate, i));

        // analog end x coordinate section
        // all values should be set to 0
        for (size_t i = 0; i < IO::Touchscreen::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::touchscreen_t::analogEndXCoordinate, i));

        // analog start y coordinate section
        // all values should be set to 0
        for (size_t i = 0; i < IO::Touchscreen::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::touchscreen_t::analogStartYCoordinate, i));

        // analog end y coordinate section
        // all values should be set to 0
        for (size_t i = 0; i < IO::Touchscreen::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::touchscreen_t::analogEndYCoordinate, i));

        // analog type section
        // all values should be set to 0
        for (size_t i = 0; i < IO::Touchscreen::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::touchscreen_t::analogType, i));

        // analog reset on release section
        // all values should be set to 0
        for (size_t i = 0; i < IO::Touchscreen::Collection::size(); i += PARAM_SKIP)
            TEST_ASSERT_EQUAL_UINT32(0, MIDIHelper::readFromDevice(System::Config::Section::touchscreen_t::analogResetOnRelease, i));
#endif
    }
}

TEST_CASE(FwUpdate)
{
    std::string syxPath = fw_build_dir + BOARD_STRING + "/" + fw_build_type_subdir + BOARD_STRING + ".sysex.syx";

    if (!std::filesystem::exists(syxPath))
    {
        LOG(ERROR) << ".syx file not found, aborting";
        TEST_ASSERT(false == true);
    }

    LOG(INFO) << "Entering bootloader mode";
    TEST_ASSERT(std::string("") == MIDIHelper::sendRawSysEx(btldr_req, false));

    LOG(INFO) << "Waiting " << startup_delay_ms / 2 << " ms";
    test::sleepMs(startup_delay_ms / 2);

    if (!MIDIHelper::devicePresent(true))
    {
        LOG(ERROR) << "OpenDeck DFU device not found after bootloader request";
        TEST_ASSERT(false == true);
    }
    else
    {
        LOG(INFO) << "Entered bootloader mode";
    }

    LOG(INFO) << "Sending firmware file to device";
    std::string cmd = std::string("amidi -p ") + MIDIHelper::amidiPort(OPENDECK_DFU_MIDI_DEVICE_NAME) + " -s " + syxPath + " -i " + sysex_fw_update_delay_ms;
    TEST_ASSERT_EQUAL_INT(0, test::wsystem(cmd));
    LOG(INFO) << "Firmware file sent successfully, waiting " << startup_delay_ms << " ms";
    test::sleepMs(startup_delay_ms);

    if (!MIDIHelper::devicePresent())
    {
        LOG(ERROR) << "OpenDeck device not found after firmware update, aborting";
        TEST_ASSERT(false == true);
    }

    handshake();
}

TEST_CASE(BackupAndRestore)
{
    LOG(INFO) << "Setting few random values in each available preset";

    for (int preset = 0; preset < database.getSupportedPresets(); preset++)
    {
        TEST_ASSERT(MIDIHelper::setSingleSysExReq(System::Config::Section::global_t::presets, static_cast<int>(Database::presetSetting_t::activePreset), preset) == true);
        TEST_ASSERT_EQUAL_UINT32(preset, MIDIHelper::readFromDevice(System::Config::Section::global_t::presets, static_cast<int>(Database::presetSetting_t::activePreset)));

        TEST_ASSERT(MIDIHelper::setSingleSysExReq(System::Config::Section::analog_t::midiID, 4, 15 + preset) == true);
#ifdef ENCODERS_SUPPORTED
        TEST_ASSERT(MIDIHelper::setSingleSysExReq(System::Config::Section::encoder_t::midiChannel, 1, 2 + preset) == true);
#endif
        TEST_ASSERT(MIDIHelper::setSingleSysExReq(System::Config::Section::button_t::velocity, 0, 90 + preset) == true);

        TEST_ASSERT_EQUAL_UINT32(15 + preset, MIDIHelper::readFromDevice(System::Config::Section::analog_t::midiID, 4));
#ifdef ENCODERS_SUPPORTED
        TEST_ASSERT_EQUAL_UINT32(2 + preset, MIDIHelper::readFromDevice(System::Config::Section::encoder_t::midiChannel, 1));
#endif
        TEST_ASSERT_EQUAL_UINT32(90 + preset, MIDIHelper::readFromDevice(System::Config::Section::button_t::velocity, 0));
    }

    LOG(INFO) << "Sending backup request";
    std::string cmd = std::string("amidi -p ") + MIDIHelper::amidiPort(OPENDECK_MIDI_DEVICE_NAME) + " -S \"" + backup_req + "\" -d -t 4 > " + backup_file_location;
    TEST_ASSERT_EQUAL_INT(0, test::wsystem(cmd));

    factoryReset();

    LOG(INFO) << "Verifying that the default values are active again";

    for (int preset = 0; preset < database.getSupportedPresets(); preset++)
    {
        TEST_ASSERT(MIDIHelper::setSingleSysExReq(System::Config::Section::global_t::presets, static_cast<int>(Database::presetSetting_t::activePreset), preset) == true);
        TEST_ASSERT_EQUAL_UINT32(preset, MIDIHelper::readFromDevice(System::Config::Section::global_t::presets, static_cast<int>(Database::presetSetting_t::activePreset)));

        TEST_ASSERT_EQUAL_UINT32(4, MIDIHelper::readFromDevice(System::Config::Section::analog_t::midiID, 4));
#ifdef ENCODERS_SUPPORTED
        TEST_ASSERT_EQUAL_UINT32(1, MIDIHelper::readFromDevice(System::Config::Section::encoder_t::midiChannel, 1));
#endif
        TEST_ASSERT_EQUAL_UINT32(127, MIDIHelper::readFromDevice(System::Config::Section::button_t::velocity, 0));
    }

    LOG(INFO) << "Restoring backup";

    // remove everything before the first line containing F0 00 53 43 01 00 1B F7
    TEST_ASSERT_EQUAL_INT(0, test::wsystem("sed -i '0,/^F0 00 53 43 01 00 1B F7$/d' " + backup_file_location));

    //...and also after the last line containing the same
    TEST_ASSERT_EQUAL_INT(0, test::wsystem("sed -i '/^F0 00 53 43 01 00 1B F7$/Q' " + backup_file_location));

    // send backup
    std::ifstream backupStream(backup_file_location);
    std::string   line;

    while (getline(backupStream, line))
    {
        TEST_ASSERT(MIDIHelper::sendRawSysEx(line) != std::string(""));
    }

    LOG(INFO) << "Verifying that the custom values are active again";

    for (int preset = 0; preset < database.getSupportedPresets(); preset++)
    {
        TEST_ASSERT(MIDIHelper::setSingleSysExReq(System::Config::Section::global_t::presets, static_cast<int>(Database::presetSetting_t::activePreset), preset) == true);
        TEST_ASSERT_EQUAL_UINT32(preset, MIDIHelper::readFromDevice(System::Config::Section::global_t::presets, static_cast<int>(Database::presetSetting_t::activePreset)));

        TEST_ASSERT_EQUAL_UINT32(15 + preset, MIDIHelper::readFromDevice(System::Config::Section::analog_t::midiID, 4));
#ifdef ENCODERS_SUPPORTED
        TEST_ASSERT_EQUAL_UINT32(2 + preset, MIDIHelper::readFromDevice(System::Config::Section::encoder_t::midiChannel, 1));
#endif
        TEST_ASSERT_EQUAL_UINT32(90 + preset, MIDIHelper::readFromDevice(System::Config::Section::button_t::velocity, 0));
    }
}

TEST_CASE(MIDIData)
{
    std::string cmd;
    std::string response;

    auto changePreset = [&](bool redirect) {
        LOG(INFO) << "Switching preset";

        if (redirect)
            cmd = std::string("amidi -p ") + MIDIHelper::amidiPort(OPENDECK_MIDI_DEVICE_NAME) + " -S \"F0 00 53 43 00 00 01 00 00 02 00 00 00 00 F7\" -d -t 3 > " + temp_midi_data_location;
        else
            cmd = std::string("amidi -p ") + MIDIHelper::amidiPort(OPENDECK_MIDI_DEVICE_NAME) + " -S \"F0 00 53 43 00 00 01 00 00 02 00 00 00 00 F7\" -d -t 3";

        TEST_ASSERT_EQUAL_INT(0, test::wsystem(cmd, response));
    };

    auto cleanupResponse = [&]() {
        // drop empty lines, remove sysex, place each new channel midi message in new line
        std::string cleanupFile = "clean_response";
        test::wsystem("cat " + temp_midi_data_location + " | xargs | sed 's/F7 /F7\\n/g' | sed 's/F0/\\nF0/g' | grep -v '^F0' | xargs | sed 's/ /&\\n/3;P;D' > " + cleanupFile, response);
        test::wsystem("grep -c . " + cleanupFile, response);
        const auto receivedMessages = stoi(response);
        test::wsystem("cat " + cleanupFile, response);
        test::wsystem("rm " + cleanupFile + " " + temp_midi_data_location);

        return receivedMessages;
    };

    changePreset(true);
    size_t receivedMessages = cleanupResponse();
    LOG(INFO) << "Received " << receivedMessages << " USB messages after preset change:\n"
              << response;
    TEST_ASSERT_EQUAL_INT(IO::Buttons::Collection::size(IO::Buttons::GROUP_DIGITAL_INPUTS), receivedMessages);

    // run the same test for DIN MIDI

    reboot();

#ifdef DIN_MIDI_SUPPORTED
#ifdef TEST_DIN_MIDI
    auto monitor = [&]() {
        LOG(INFO) << "Monitoring DIN MIDI interface " << OUT_DIN_MIDI_PORT;
        cmd = std::string("amidi -p ") + MIDIHelper::amidiPort(OUT_DIN_MIDI_PORT) + " -d > " + temp_midi_data_location + " &";
        TEST_ASSERT_EQUAL_INT(0, test::wsystem(cmd));
    };

    auto stopMonitoring = []() {
        test::wsystem("killall amidi > /dev/null");
    };

    monitor();
    changePreset(false);
    stopMonitoring();
    receivedMessages = cleanupResponse();

    LOG(INFO) << "Verifying that no data reached DIN MIDI interface due to the DIN MIDI being disabled";
    TEST_ASSERT_EQUAL_INT(0, receivedMessages);

    LOG(INFO) << "Enabling DIN MIDI";
    TEST_ASSERT(MIDIHelper::setSingleSysExReq(System::Config::Section::global_t::midiFeatures, Protocol::MIDI::feature_t::dinEnabled, 1) == true);
    monitor();
    changePreset(false);
    stopMonitoring();
    receivedMessages = cleanupResponse();
    LOG(INFO) << "Received " << receivedMessages << " DIN MIDI messages after preset change:\n"
              << response;
    TEST_ASSERT_EQUAL_INT(IO::Buttons::Collection::size(IO::Buttons::GROUP_DIGITAL_INPUTS), receivedMessages);

    // enable DIN MIDI passthrough, send data to DIN MIDI in to device and expect the same message passed to output port
    LOG(INFO) << "Enabling DIN MIDI merge";
    TEST_ASSERT(MIDIHelper::setSingleSysExReq(System::Config::Section::global_t::midiFeatures, Protocol::MIDI::feature_t::mergeEnabled, 1) == true);

    LOG(INFO) << "Enabling DIN to DIN passthrough";
    TEST_ASSERT(MIDIHelper::setSingleSysExReq(System::Config::Section::global_t::midiMerge, Protocol::MIDI::mergeSetting_t::mergeType, Protocol::MIDI::mergeType_t::DINtoDIN) == true);

    monitor();
    std::string msg = "90 00 7F";
    LOG(INFO) << "Sending data to DIN MIDI interface " << IN_DIN_MIDI_PORT;
    LOG(INFO) << "Message: " << msg;
    cmd = "amidi -p " + MIDIHelper::amidiPort(IN_DIN_MIDI_PORT) + " -S \"" + msg + "\"";
    TEST_ASSERT_EQUAL_INT(0, test::wsystem(cmd));
    test::sleepMs(1000);
    stopMonitoring();
    receivedMessages = cleanupResponse();
    LOG(INFO) << "Received " << receivedMessages << " DIN MIDI messages on passthrough interface:\n"
              << response;
    TEST_ASSERT_EQUAL_INT(1, receivedMessages);
    TEST_ASSERT(response.find(msg) != std::string::npos);
#endif
#endif
}

#ifdef DMX_SUPPORTED
TEST_CASE(DMX)
{
    LOG(INFO) << "Starting OLA daemon";
    test::wsystem("olad -f --no-http");
    test::sleepMs(3000);

    auto verify = [](bool state) {
        std::string    cmd           = "ola_dev_info | grep -q " + std::string(BOARD_STRING);
        const uint32_t waitTimeMs    = 1000;
        const uint32_t stopWaitAfter = 22000;    // ola searches after 20sec, 2 more just in case
        uint32_t       totalWaitTime = 0;

        while (test::wsystem(cmd))
        {
            test::sleepMs(waitTimeMs);
            totalWaitTime += waitTimeMs;

            if (totalWaitTime == stopWaitAfter)
            {
                break;
            }
        }

        if (!state)
        {
            if (totalWaitTime == stopWaitAfter)
            {
                LOG(INFO) << "OLA didn't detect the device while DMX was disabled";
                return true;
            }
            else
            {
                LOG(ERROR) << "OLA detected the device while DMX was disabled";
                return false;
            }
        }
        else
        {
            if (totalWaitTime == stopWaitAfter)
            {
                LOG(ERROR) << "OLA didn't detect the device while DMX was enabled";
                return false;
            }
            else
            {
                LOG(INFO) << "OLA detected the device while DMX was enabled";
                return true;
            }
        }
    };

    LOG(INFO) << "DMX is disabled, checking if the device isn't detectable by OLA";
    TEST_ASSERT(verify(false));

    LOG(INFO) << "Enabling DMX";
    TEST_ASSERT(MIDIHelper::setSingleSysExReq(System::Config::Section::global_t::dmx, static_cast<int>(Protocol::DMX::setting_t::enable), 1) == true);

    LOG(INFO) << "Checking if the device is detectable by OLA";
    TEST_ASSERT(verify(true));

    // midi part should remain functional as well
    TEST_ASSERT(handshake_ack == MIDIHelper::sendRawSysEx(handshake_req));
}
#endif

#ifdef TEST_IO
TEST_CASE(InputOutput)
{
    std::string response;

    auto monitor = [&]() {
        LOG(INFO) << "Monitoring USB MIDI interface";
        auto cmd = std::string("amidi -p ") + MIDIHelper::amidiPort(OPENDECK_MIDI_DEVICE_NAME) + " -d > " + temp_midi_data_location + " &";
        TEST_ASSERT_EQUAL_INT(0, test::wsystem(cmd));
    };

    auto stopMonitoring = []() {
        test::wsystem("killall amidi > /dev/null");
    };

#ifdef TEST_IO_ANALOG
    for (size_t i = 0; i < hwTestAnalogDescriptor.size(); i++)
    {
        LOG(INFO) << "Enabling analog input " << hwTestAnalogDescriptor.at(i).index;
        TEST_ASSERT(MIDIHelper::setSingleSysExReq(System::Config::Section::analog_t::enable, hwTestAnalogDescriptor.at(i).index, 1) == true);
    }
#endif

    monitor();

#ifdef TEST_IO_SWITCHES
    for (size_t i = 0; i < hwTestSwDescriptor.size(); i++)
    {
        LOG(INFO) << "Toggling switch " << hwTestSwDescriptor.at(i).index;

        test::wsystem(std::string("echo write_high ") + std::to_string(hwTestSwDescriptor.at(i).pin) + " > /dev/actl");
        test::sleepMs(1000);
        test::wsystem(std::string("echo write_low ") + std::to_string(hwTestSwDescriptor.at(i).pin) + " > /dev/actl");
        test::sleepMs(1000);
    }
#endif

#ifdef TEST_IO_ANALOG
    for (size_t i = 0; i < hwTestAnalogDescriptor.size(); i++)
    {
        LOG(INFO) << "Toggling analog input " << hwTestAnalogDescriptor.at(i).index;

        test::wsystem(std::string("echo write_high ") + std::to_string(hwTestAnalogDescriptor.at(i).pin) + " > /dev/actl");
        test::sleepMs(1000);
        test::wsystem(std::string("echo write_low ") + std::to_string(hwTestAnalogDescriptor.at(i).pin) + " > /dev/actl");
        test::sleepMs(1000);
    }
#endif

    stopMonitoring();

    LOG(INFO) << "Printing response";
    test::wsystem("cat " + temp_midi_data_location + " | grep -v '^F0'");

    LOG(INFO) << "Verifying if received messages are valid";

#ifdef TEST_IO_SWITCHES
    for (size_t i = 0; i < hwTestSwDescriptor.size(); i++)
    {
        std::stringstream sstream;
        sstream << std::uppercase << std::setfill('0') << std::setw(2) << std::hex << hwTestSwDescriptor.at(i).index;

        std::string msgOn  = "90 " + sstream.str() + " 00";
        std::string msgOff = "90 " + sstream.str() + " 7F";

        LOG(INFO) << "Searching for: " << msgOn;
        TEST_ASSERT_EQUAL_INT(0, test::wsystem("cat " + temp_midi_data_location + " | grep \"" + msgOn + "\"", response));

        LOG(INFO) << "Searching for: " << msgOff;
        TEST_ASSERT_EQUAL_INT(0, test::wsystem("cat " + temp_midi_data_location + " | grep \"" + msgOff + "\"", response));
    }
#endif

#ifdef TEST_IO_ANALOG
    for (size_t i = 0; i < hwTestAnalogDescriptor.size(); i++)
    {
        std::stringstream sstream;
        sstream << std::uppercase << std::setfill('0') << std::setw(2) << std::hex << hwTestAnalogDescriptor.at(i).index;

        std::string msgOn  = "B0 " + sstream.str() + " 00";
        std::string msgOff = "B0 " + sstream.str() + " 7F";

        LOG(INFO) << "Searching for: " << msgOn;
        TEST_ASSERT_EQUAL_INT(0, test::wsystem("cat " + temp_midi_data_location + " | grep \"" + msgOn + "\"", response));

        LOG(INFO) << "Searching for: " << msgOff;
        TEST_ASSERT_EQUAL_INT(0, test::wsystem("cat " + temp_midi_data_location + " | grep \"" + msgOff + "\"", response));
    }
#endif

#ifdef TEST_IO_LEDS
    LOG(INFO) << "Checking LED output";

    for (size_t i = 0; i < hwTestLEDDescriptor.size(); i++)
    {
        LOG(INFO) << "Toggling LED " << hwTestLEDDescriptor.at(i).index << " on";
        TEST_ASSERT(MIDIHelper::setSingleSysExReq(System::Config::Section::leds_t::testColor, hwTestLEDDescriptor.at(i).index, 1) == true);
        test::sleepMs(1000);

        LOG(INFO) << "Verifying that the LED is turned on";
        std::string ledOn = "read " + std::to_string(hwTestLEDDescriptor.at(i).pin);

#ifdef LED_EXT_INVERT
        ledOn += " ok: 0";
#else
        ledOn += " ok: 1";
#endif

        test::wsystem("echo read " + std::to_string(hwTestLEDDescriptor.at(i).pin) + " | ssterm /dev/actl > " + temp_midi_data_location + " &", response);
        test::sleepMs(1000);

        test::wsystem("killall ssterm", response);
        while (test::wsystem("pgrep ssterm", response) == 0)
            ;

        LOG(INFO) << "Printing response";
        test::wsystem("cat " + temp_midi_data_location);

        TEST_ASSERT_EQUAL_INT(0, test::wsystem("cat " + temp_midi_data_location + " | grep \"" + ledOn + "\"", response));

        LOG(INFO) << "Toggling LED " << hwTestLEDDescriptor.at(i).index << " off";
        TEST_ASSERT(MIDIHelper::setSingleSysExReq(System::Config::Section::leds_t::testColor, hwTestLEDDescriptor.at(i).index, 0) == true);
        test::sleepMs(1000);

        LOG(INFO) << "Verifying that the LED is turned off";
        std::string ledOff = "read " + std::to_string(hwTestLEDDescriptor.at(i).pin);

#ifdef LED_EXT_INVERT
        ledOff += " ok: 1";
#else
        ledOff += " ok: 0";
#endif

        test::wsystem("echo read " + std::to_string(hwTestLEDDescriptor.at(i).pin) + " | ssterm /dev/actl > " + temp_midi_data_location + " &", response);
        test::sleepMs(1000);

        test::wsystem("killall ssterm", response);
        while (test::wsystem("pgrep ssterm", response) == 0)
            ;

        LOG(INFO) << "Printing response";
        test::wsystem("cat " + temp_midi_data_location);

        TEST_ASSERT_EQUAL_INT(0, test::wsystem("cat " + temp_midi_data_location + " | grep \"" + ledOff + "\"", response));
    }
#endif
}
#endif

#endif
#endif