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

#pragma once

namespace IO
{
    class LEDs : public IO::Base
    {
        public:
        class Collection : public Common::BaseCollection<0>
        {
            public:
            Collection() = delete;
        };

        enum
        {
            GROUP_DIGITAL_OUTPUTS,
        };

        enum class rgbIndex_t : uint8_t
        {
            r,
            g,
            b
        };

        enum class color_t : uint8_t
        {
            off,
            red,
            green,
            yellow,
            blue,
            magenta,
            cyan,
            white,
            AMOUNT
        };

        enum class setting_t : uint8_t
        {
            blinkWithMIDIclock,
            unused,
            useStartupAnimation,
            AMOUNT
        };

        enum class controlType_t : uint8_t
        {
            midiInNoteSingleVal,
            localNoteSingleVal,
            midiInCCSingleVal,
            localCCSingleVal,
            pcSingleVal,
            preset,
            midiInNoteMultiVal,
            localNoteMultiVal,
            midiInCCMultiVal,
            localCCMultiVal,
            AMOUNT
        };

        enum class blinkSpeed_t : uint8_t
        {
            noBlink,
            s100ms,
            s200ms,
            s300ms,
            s400ms,
            s500ms,
            s600ms,
            s700ms,
            s800ms,
            s900ms,
            s1000ms,
            AMOUNT
        };

        enum class blinkType_t : uint8_t
        {
            timer,
            midiClock
        };

        enum class brightness_t : uint8_t
        {
            bOff,
            b25,
            b50,
            b75,
            b100
        };

        enum class dataSource_t : uint8_t
        {
            external,    // data from midi in
            internal     // data from local source (buttons, encoders...)
        };

        class HWA
        {
            public:
            HWA() {}
            virtual void   setState(size_t index, brightness_t brightness)                = 0;
            virtual size_t rgbIndex(size_t singleLEDindex)                                = 0;
            virtual size_t rgbSignalIndex(size_t rgbIndex, LEDs::rgbIndex_t rgbComponent) = 0;
        };

        LEDs(HWA&      hwa,
             Database& database)
        {}

        bool init() override
        {
            return false;
        }

        void updateSingle(size_t index, bool forceRefresh = false) override
        {
        }

        void updateAll(bool forceRefresh = false) override
        {
        }

        size_t maxComponentUpdateIndex() override
        {
            return 0;
        }
    };
}    // namespace IO