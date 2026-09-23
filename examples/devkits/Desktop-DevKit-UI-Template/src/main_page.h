#pragma once

#include "daisy.h"

class MainPage : public daisy::UiPage
{
  public:
    /** Simple alias for logging
   *  StartLog() for the destination should have already been called before the
   * page is opened.
   */
    using Log = daisy::Logger<daisy::LOGGER_EXTERNAL>;

    void Init() {}

    void Draw(const daisy::UiCanvasDescriptor &canvasDescriptor) override
    {
        auto *hw = reinterpret_cast<daisy::desktop_devkit::Hardware *>(
            canvasDescriptor.handle_);
        auto now = daisy::System::GetNow();
        hw->seed.SetLed((now & 511) < 255);

        // For now we'll just illuminate each LED with the state of the button
        for(size_t i = 0; i < 16; i++)
        {
            hw->SetLed(i, hw->button[i].Pressed() ? 1.f : 0.f);
        }
    }

    bool OnButton(uint16_t buttonID,
                  uint8_t  numberOfPresses,
                  bool     isRetriggering) override
    {
        switch(numberOfPresses)
        {
            case 0: Log::PrintLine("Released %d", buttonID + 1); break;
            case 1: Log::PrintLine("Pressed %d", buttonID + 1); break;
            case 2: Log::PrintLine("Long Pressed %d", buttonID + 1); break;
            default: break;
        }
        return false;
    }

    bool OnPotMoved(uint16_t potID, float newPosition)
    {
        // Print Format: "Pot {potID+1}: {newPosition}"
        daisy::FixedCapStr<16> valueStr("Pot ");
        valueStr.AppendInt(potID + 1);
        valueStr.Append(": ");
        valueStr.AppendFloat(newPosition, 3);
        Log::PrintLine(valueStr.Cstr());
        return false;
    }

    void OnShow() { Log::PrintLine("Page Opened!"); }
};
