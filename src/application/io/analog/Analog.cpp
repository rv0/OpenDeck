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

#include "Analog.h"
#include "sysex/src/SysExConf.h"
#include "system/Config.h"

#ifdef ANALOG_SUPPORTED

#include "core/src/general/Helpers.h"
#include "util/conversion/Conversion.h"
#include "util/configurable/Configurable.h"

using namespace IO;

Analog::Analog(HWA&      hwa,
               Filter&   filter,
               Database& database)
    : _hwa(hwa)
    , _filter(filter)
    , _database(database)
{
    Dispatcher.listen(Util::MessageDispatcher::messageSource_t::touchscreenAnalog,
                      Util::MessageDispatcher::listenType_t::fwd,
                      [this](const Util::MessageDispatcher::message_t& dispatchMessage) {
                          size_t index = dispatchMessage.componentIndex + Collection::startIndex(GROUP_TOUCHSCREEN_COMPONENTS);
                          processReading(index, dispatchMessage.midiValue);
                      });

    Dispatcher.listen(Util::MessageDispatcher::messageSource_t::system,
                      Util::MessageDispatcher::listenType_t::all,
                      [this](const Util::MessageDispatcher::message_t& dispatchMessage) {
                          switch (dispatchMessage.componentIndex)
                          {
                          case static_cast<uint8_t>(Util::MessageDispatcher::systemMessages_t::forceIOrefresh):
                          {
                              updateAll(true);
                          }
                          break;

                          default:
                              break;
                          }
                      });

    ConfigHandler.registerConfig(
        System::Config::block_t::analog,
        // read
        [this](uint8_t section, size_t index, uint16_t& value) {
            return sysConfigGet(static_cast<System::Config::Section::analog_t>(section), index, value);
        },

        // write
        [this](uint8_t section, size_t index, uint16_t value) {
            return sysConfigSet(static_cast<System::Config::Section::analog_t>(section), index, value);
        });
}

bool Analog::init()
{
    for (size_t i = 0; i < Collection::size(); i++)
    {
        reset(i);
    }

    return true;
}

void Analog::updateSingle(size_t index, bool forceRefresh)
{
    if (index >= maxComponentUpdateIndex())
    {
        return;
    }

    if (!forceRefresh)
    {
        uint16_t value;

        if (!_hwa.value(index, value))
        {
            return;
        }

        processReading(index, value);
    }
    else
    {
        if (_database.read(Database::Section::analog_t::enable, index))
        {
            analogDescriptor_t descriptor;
            fillAnalogDescriptor(index, descriptor);
            descriptor.dispatchMessage.midiValue = _filter.lastValue(index);
            sendMessage(index, descriptor);
        }
    }
}

void Analog::updateAll(bool forceRefresh)
{
    // check values
    for (size_t i = 0; i < Collection::size(GROUP_ANALOG_INPUTS); i++)
    {
        updateSingle(i, forceRefresh);
    }
}

size_t Analog::maxComponentUpdateIndex()
{
    return Collection::size(GROUP_ANALOG_INPUTS);
}

void Analog::processReading(size_t index, uint16_t value)
{
    // don't process component if it's not enabled
    if (!_database.read(Database::Section::analog_t::enable, index))
    {
        return;
    }

    analogDescriptor_t descriptor;
    fillAnalogDescriptor(index, descriptor);

    if (!_filter.isFiltered(index, descriptor.type, value, value))
    {
        return;
    }

    descriptor.dispatchMessage.midiValue = value;

    bool send = false;

    if (descriptor.type != type_t::button)
    {
        switch (descriptor.type)
        {
        case type_t::potentiometerControlChange:
        case type_t::potentiometerNote:
        case type_t::nrpn7bit:
        case type_t::nrpn14bit:
        case type_t::pitchBend:
        case type_t::controlChange14bit:
        {
            if (checkPotentiometerValue(index, descriptor))
            {
                send = true;
            }
        }
        break;

        case type_t::fsr:
        {
            if (checkFSRvalue(index, descriptor))
            {
                send = true;
            }
        }
        break;

        default:
            break;
        }
    }
    else
    {
        send = true;
    }

    if (send)
    {
        sendMessage(index, descriptor);
    }
}

bool Analog::checkPotentiometerValue(size_t index, analogDescriptor_t& descriptor)
{
    uint16_t maxLimit;

    if ((descriptor.type == type_t::nrpn14bit) || (descriptor.type == type_t::pitchBend) || (descriptor.type == type_t::controlChange14bit))
    {
        // 14-bit values are already read
        maxLimit = MIDI::MIDI_14_BIT_VALUE_MAX;
    }
    else
    {
        maxLimit = MIDI::MIDI_7_BIT_VALUE_MAX;

        MIDI::Split14bit split14bit;

        // use 7-bit MIDI ID and limits
        split14bit.split(descriptor.dispatchMessage.midiIndex);
        descriptor.dispatchMessage.midiIndex = split14bit.low();

        split14bit.split(descriptor.lowerLimit);
        descriptor.lowerLimit = split14bit.low();

        split14bit.split(descriptor.upperLimit);
        descriptor.upperLimit = split14bit.low();
    }

    if (descriptor.dispatchMessage.midiValue > maxLimit)
    {
        return false;
    }

    auto scale = [&](uint16_t value) {
        uint32_t scaled = 0;

        if (descriptor.lowerLimit > descriptor.upperLimit)
        {
            scaled = core::misc::mapRange(static_cast<uint32_t>(value), static_cast<uint32_t>(0), static_cast<uint32_t>(maxLimit), static_cast<uint32_t>(descriptor.upperLimit), static_cast<uint32_t>(descriptor.lowerLimit));

            if (!descriptor.inverted)
            {
                scaled = descriptor.upperLimit - (scaled - descriptor.lowerLimit);
            }
        }
        else
        {
            scaled = core::misc::mapRange(static_cast<uint32_t>(value), static_cast<uint32_t>(0), static_cast<uint32_t>(maxLimit), static_cast<uint32_t>(descriptor.lowerLimit), static_cast<uint32_t>(descriptor.upperLimit));

            if (descriptor.inverted)
            {
                scaled = descriptor.upperLimit - (scaled - descriptor.lowerLimit);
            }
        }

        return scaled;
    };

    auto scaledNew = scale(descriptor.dispatchMessage.midiValue);
    auto scaledOld = scale(_filter.lastValue(index));

    if (scaledNew == scaledOld)
    {
        return false;
    }

    descriptor.dispatchMessage.midiValue = scaledNew;

    return true;
}

bool Analog::checkFSRvalue(size_t index, analogDescriptor_t& descriptor)
{
    // don't allow touchscreen components to be processed as FSR
    if (index >= Collection::size(GROUP_ANALOG_INPUTS))
    {
        return false;
    }

    if (descriptor.dispatchMessage.midiValue > 0)
    {
        if (!fsrState(index))
        {
            // sensor is really pressed
            setFSRstate(index, true);
            return true;
        }
    }
    else
    {
        if (fsrState(index))
        {
            setFSRstate(index, false);
            return true;
        }
    }

    return false;
}

void Analog::sendMessage(size_t index, analogDescriptor_t& descriptor)
{
    bool send    = true;
    bool forward = false;

    switch (descriptor.type)
    {
    case type_t::potentiometerControlChange:
    case type_t::potentiometerNote:
    case type_t::pitchBend:
        break;

    case type_t::fsr:
    {
        if (!fsrState(index))
        {
            descriptor.dispatchMessage.midiValue = 0;
            descriptor.dispatchMessage.message   = MIDI::messageType_t::noteOff;
        }
    }
    break;

    case type_t::button:
    {
        forward = true;
    }
    break;

    case type_t::controlChange14bit:
    {
        if (descriptor.dispatchMessage.midiIndex >= 96)
        {
            // not allowed
            send = false;
            break;
        }
    }
    break;

    default:
    {
        send = false;
    }
    break;
    }

    if (send)
    {
        Dispatcher.notify(Util::MessageDispatcher::messageSource_t::analog,
                          descriptor.dispatchMessage,
                          forward ? Util::MessageDispatcher::listenType_t::fwd : Util::MessageDispatcher::listenType_t::nonFwd);
    }
}

void Analog::reset(size_t index)
{
    setFSRstate(index, false);
    _filter.reset(index);
}

void Analog::setFSRstate(size_t index, bool state)
{
    uint8_t arrayIndex  = index / 8;
    uint8_t analogIndex = index - 8 * arrayIndex;

    BIT_WRITE(_fsrPressed[arrayIndex], analogIndex, state);
}

bool Analog::fsrState(size_t index)
{
    uint8_t arrayIndex  = index / 8;
    uint8_t analogIndex = index - 8 * arrayIndex;

    return BIT_READ(_fsrPressed[arrayIndex], analogIndex);
}

void Analog::fillAnalogDescriptor(size_t index, analogDescriptor_t& descriptor)
{
    descriptor.type                           = static_cast<type_t>(_database.read(Database::Section::analog_t::type, index));
    descriptor.inverted                       = _database.read(Database::Section::analog_t::invert, index);
    descriptor.lowerLimit                     = _database.read(Database::Section::analog_t::lowerLimit, index);
    descriptor.upperLimit                     = _database.read(Database::Section::analog_t::upperLimit, index);
    descriptor.dispatchMessage.componentIndex = index;
    descriptor.dispatchMessage.midiChannel    = _database.read(Database::Section::analog_t::midiChannel, index);
    descriptor.dispatchMessage.midiIndex      = _database.read(Database::Section::analog_t::midiID, index);
    descriptor.dispatchMessage.message        = _internalMsgToMIDIType[static_cast<uint8_t>(descriptor.type)];
}

std::optional<uint8_t> Analog::sysConfigGet(System::Config::Section::analog_t section, size_t index, uint16_t& value)
{
    int32_t readValue;
    auto    result = _database.read(Util::Conversion::sys2DBsection(section), index, readValue) ? System::Config::status_t::ack : System::Config::status_t::errorRead;

    switch (section)
    {
    case System::Config::Section::analog_t::midiID_MSB:
    case System::Config::Section::analog_t::lowerLimit_MSB:
    case System::Config::Section::analog_t::upperLimit_MSB:
        return System::Config::status_t::errorNotSupported;

    case System::Config::Section::analog_t::midiChannel:
    {
        // channels start from 0 in db, start from 1 in sysex
        if (result == System::Config::status_t::ack)
        {
            readValue++;
        }
    }
    break;

    default:
        break;
    }

    value = readValue;

    return result;
}

std::optional<uint8_t> Analog::sysConfigSet(System::Config::Section::analog_t section, size_t index, uint16_t value)
{
    switch (section)
    {
    case System::Config::Section::analog_t::midiID_MSB:
    case System::Config::Section::analog_t::lowerLimit_MSB:
    case System::Config::Section::analog_t::upperLimit_MSB:
        return System::Config::status_t::errorNotSupported;

    case System::Config::Section::analog_t::type:
    {
        reset(index);
    }
    break;

    default:
    {
        // channels start from 0 in db, start from 1 in sysex
        if (section == System::Config::Section::analog_t::midiChannel)
        {
            value--;
        }
    }
    break;
    }

    return _database.update(Util::Conversion::sys2DBsection(section), index, value) ? System::Config::status_t::ack : System::Config::status_t::errorWrite;
}

#endif