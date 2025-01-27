/*

Copyright 2015-2022 Igor Petrovic

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/

#include "Viewtech.h"
#include "core/src/general/Timing.h"
#include "core/src/general/Helpers.h"

using namespace IO;

Viewtech::Viewtech(IO::Touchscreen::HWA& hwa)
    : _hwa(hwa)
{
    IO::Touchscreen::registerModel(IO::Touchscreen::model_t::viewtech, this);
}

bool Viewtech::init()
{
    Touchscreen::Model::_bufferCount = 0;
    return _hwa.init();
}

bool Viewtech::deInit()
{
    return _hwa.deInit();
}

bool Viewtech::setScreen(size_t screenID)
{
    screenID &= 0xFF;

    _hwa.write(0xA5);
    _hwa.write(0x5A);
    _hwa.write(0x04);
    _hwa.write(0x80);
    _hwa.write(0x03);
    _hwa.write(0x00);
    _hwa.write(screenID);

    return true;
}

Touchscreen::tsEvent_t Viewtech::update(Touchscreen::tsData_t& data)
{
    pollXY();

    auto    event = Touchscreen::tsEvent_t::none;
    uint8_t value = 0;

    while (_hwa.read(value))
    {
        Touchscreen::Model::_rxBuffer[Touchscreen::Model::_bufferCount++] = value;
    }

    // assumption - only one response is received at the time
    // if parsing fails, wipe the buffer
    if (Touchscreen::Model::_bufferCount)
    {
        // verify header first
        if (Touchscreen::Model::_rxBuffer[0] == 0xA5)
        {
            if (Touchscreen::Model::_bufferCount > 1)
            {
                if (Touchscreen::Model::_rxBuffer[1] == 0x5A)
                {
                    if (Touchscreen::Model::_bufferCount > 2)
                    {
                        // byte at index 2 holds response length, without first two bytes and without byte at index 2
                        if (Touchscreen::Model::_bufferCount >= static_cast<size_t>(3 + Touchscreen::Model::_rxBuffer[2]))
                        {
                            uint32_t response = Touchscreen::Model::_rxBuffer[2];
                            response <<= 8;
                            response |= Touchscreen::Model::_rxBuffer[3];
                            response <<= 8;
                            response |= Touchscreen::Model::_rxBuffer[4];
                            response <<= 8;
                            response |= Touchscreen::Model::_rxBuffer[5];

                            switch (response)
                            {
                            case static_cast<uint32_t>(response_t::buttonStateChange):
                            {
                                data.buttonState = Touchscreen::Model::_rxBuffer[6];
                                data.buttonID    = Touchscreen::Model::_rxBuffer[7];

                                event = Touchscreen::tsEvent_t::button;
                            }
                            break;

                            case static_cast<uint32_t>(response_t::xyUpdate):
                            {
                                if ((Touchscreen::Model::_rxBuffer[6] == 0x01) || (Touchscreen::Model::_rxBuffer[6] == 0x03))
                                {
                                    data.xPos = Touchscreen::Model::_rxBuffer[7] << 8;
                                    data.xPos |= Touchscreen::Model::_rxBuffer[8];

                                    data.yPos = Touchscreen::Model::_rxBuffer[9] << 8;
                                    data.yPos |= Touchscreen::Model::_rxBuffer[10];
                                    data.pressType = Touchscreen::Model::_rxBuffer[6] == 0x01 ? Touchscreen::pressType_t::initial : Touchscreen::pressType_t::hold;

                                    event = Touchscreen::tsEvent_t::coordinate;
                                }
                                else
                                {
                                    // screen released
                                    data.xPos      = 0;
                                    data.yPos      = 0;
                                    data.pressType = Touchscreen::pressType_t::none;
                                    event          = Touchscreen::tsEvent_t::coordinate;
                                }
                            }
                            break;

                            default:
                                break;
                            }

                            Touchscreen::Model::_bufferCount = 0;
                        }
                    }
                }
                else
                {
                    // header invalid - ignore the rest of the message
                    Touchscreen::Model::_bufferCount = 0;
                }
            }
        }
        else
        {
            // header invalid - ignore the rest of the message
            Touchscreen::Model::_bufferCount = 0;
        }
    }

    return event;
}

void Viewtech::setIconState(Touchscreen::icon_t& icon, bool state)
{
    // header
    _hwa.write(0xA5);
    _hwa.write(0x5A);

    // request size
    _hwa.write(0x05);

    // write variable
    _hwa.write(0x82);

    // icon address - for viewtech displays, address is stored in xPos element
    _hwa.write(MSB_WORD(icon.xPos));
    _hwa.write(LSB_WORD(icon.xPos));

    // value to set - 2 bytes are used, higher is always 0
    // inverted logic for setting state - 0 means on state, 1 is off
    _hwa.write(0x00);
    _hwa.write(state ? 0x00 : 0x01);
}

bool Viewtech::setBrightness(Touchscreen::brightness_t brightness)
{
    // header
    _hwa.write(0xA5);
    _hwa.write(0x5A);

    // request size
    _hwa.write(0x03);

    // register write
    _hwa.write(0x80);

    // brightness settting
    _hwa.write(0x01);

    // brightness value
    _hwa.write(_brightnessMapping[static_cast<uint8_t>(brightness)]);

    return true;
}

void Viewtech::pollXY()
{
    static uint32_t lastPollTime = 0;

    if ((core::timing::currentRunTimeMs() - lastPollTime) > XY_POLL_TIME_MS)
    {
        // header
        _hwa.write(0xA5);
        _hwa.write(0x5A);

        // request size
        _hwa.write(0x03);

        // register read
        _hwa.write(0x81);

        // read register 6 but request 5 bytes
        // reg6 contains touch status (1 byte)
        // reg7 contains x/y coordinates (4 bytes)
        _hwa.write(0x06);

        // read 5 bytes
        _hwa.write(0x05);

        lastPollTime = core::timing::currentRunTimeMs();
    }
}