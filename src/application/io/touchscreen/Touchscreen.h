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

#include <inttypes.h>
#include <stdlib.h>
#include <array>
#include "database/Database.h"
#include "io/common/Common.h"
#include "system/Config.h"
#include "util/messaging/Messaging.h"
#include "io/IOBase.h"

#ifdef TOUCHSCREEN_SUPPORTED

namespace IO
{
    class Touchscreen : public IO::Base
    {
        public:
        class Collection : public Common::BaseCollection<NR_OF_TOUCHSCREEN_COMPONENTS>
        {
            public:
            Collection() = delete;
        };

        enum class setting_t : uint8_t
        {
            enable,
            model,
            brightness,
            initialScreen,
            cdcPassthrough,
            AMOUNT
        };

        enum class mode_t : uint8_t
        {
            normal,
            cdcPassthrough
        };

        struct icon_t
        {
            uint16_t xPos      = 0;
            uint16_t yPos      = 0;
            uint16_t width     = 0;
            uint16_t height    = 0;
            uint16_t onScreen  = 0;
            uint16_t offScreen = 0;
        };

        enum class tsEvent_t : uint8_t
        {
            none,
            button,
            coordinate
        };

        enum class pressType_t : uint8_t
        {
            none,
            initial,
            hold
        };

        struct tsData_t
        {
            pressType_t pressType   = pressType_t::none;
            size_t      buttonID    = 0;
            bool        buttonState = false;
            uint16_t    xPos        = 0;
            uint16_t    yPos        = 0;
        };

        enum class brightness_t : uint8_t
        {
            _10,
            _25,
            _50,
            _75,
            _80,
            _90,
            _100
        };

        enum class model_t : uint8_t
        {
            nextion,
            viewtech,
            AMOUNT
        };

        class HWA : public ::IO::Common::Allocatable
        {
            public:
            virtual bool init()               = 0;
            virtual bool deInit()             = 0;
            virtual bool write(uint8_t value) = 0;
            virtual bool read(uint8_t& value) = 0;
        };

        class CDCPassthrough : public Common::Allocatable
        {
            public:
            virtual bool init()                                                       = 0;
            virtual bool deInit()                                                     = 0;
            virtual bool uartRead(uint8_t& value)                                     = 0;
            virtual bool uartWrite(uint8_t value)                                     = 0;
            virtual bool cdcRead(uint8_t* buffer, size_t& size, const size_t maxSize) = 0;
            virtual bool cdcWrite(uint8_t* buffer, size_t size)                       = 0;
        };

        class Model
        {
            public:
            virtual bool      init()                                 = 0;
            virtual bool      deInit()                               = 0;
            virtual bool      setScreen(size_t screenID)             = 0;
            virtual tsEvent_t update(tsData_t& tsData)               = 0;
            virtual void      setIconState(icon_t& icon, bool state) = 0;
            virtual bool      setBrightness(brightness_t brightness) = 0;

            protected:
            static constexpr size_t BUFFER_SIZE = 50;
            static uint8_t          _rxBuffer[BUFFER_SIZE];
            static size_t           _bufferCount;
        };

        Touchscreen(HWA&            hwa,
                    Database&       database,
                    CDCPassthrough& cdcPassthrough,
                    uint16_t        adcResolution);

        bool        init() override;
        void        updateSingle(size_t index, bool forceRefresh = false) override;
        void        updateAll(bool forceRefresh = false) override;
        size_t      maxComponentUpdateIndex() override;
        static void registerModel(model_t model, Model* instance);

        private:
        enum class analogType_t : uint8_t
        {
            horizontal,
            vertical
        };

        Model*                 modelInstance(model_t model);
        bool                   init(mode_t mode);
        bool                   deInit(mode_t mode);
        bool                   isInitialized() const;
        bool                   isInitialized(mode_t mode) const;
        void                   setScreen(size_t screenID);
        size_t                 activeScreen();
        void                   setIconState(size_t index, bool state);
        bool                   setBrightness(brightness_t brightness);
        void                   processButton(const size_t buttonID, const bool state);
        void                   processCoordinate(pressType_t pressType, uint16_t xPos, uint16_t yPos);
        void                   buttonHandler(size_t index, bool state);
        void                   analogHandler(size_t index, uint16_t value, uint16_t min, uint16_t max);
        void                   screenChangeHandler(size_t screenID);
        std::optional<uint8_t> sysConfigGet(System::Config::Section::touchscreen_t section, size_t index, uint16_t& value);
        std::optional<uint8_t> sysConfigSet(System::Config::Section::touchscreen_t section, size_t index, uint16_t value);

        HWA&                                                            _hwa;
        Database&                                                       _database;
        CDCPassthrough&                                                 _cdcPassthrough;
        const uint16_t                                                  ADC_RESOLUTION;
        size_t                                                          _activeScreenID                                = 0;
        bool                                                            _initialized                                   = false;
        mode_t                                                          _mode                                          = mode_t::normal;
        model_t                                                         _activeModel                                   = model_t::AMOUNT;
        bool                                                            _analogActive[Collection::size()]              = {};
        uint8_t                                                         _txBuffer[TSCREEN_CDC_PASSTHROUGH_BUFFER_SIZE] = {};
        uint8_t                                                         _rxBuffer[TSCREEN_CDC_PASSTHROUGH_BUFFER_SIZE] = {};
        static std::array<Model*, static_cast<size_t>(model_t::AMOUNT)> _models;
    };
}    // namespace IO

#else
#include "stub/Touchscreen.h"
#endif