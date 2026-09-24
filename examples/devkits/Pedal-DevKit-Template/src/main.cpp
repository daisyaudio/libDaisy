/** Pedal DevKit Template
 *
 *  Basic template program for the Pedal DevKit
 *
 *  This program:
 *  - initializes the Pedal DevKit hardware
 *  - starts an audio callback to do pass through and update the controls
 *  - blinks the LEDs
 *  - Sends MIDI notes received on the MIDI input to the MIDI output
 *  - Prints all input states over USB serial
 */
#include "pedal_devkit.h"

using namespace daisy;
using namespace daisy::pedal_devkit;

using Log = Logger<LOGGER_INTERNAL>;

Hardware hw;

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    hw.UpdateAllControls();

    for(size_t i = 0; i < size; i++)
    {
        out[0][i] = in[0][i];
        out[1][i] = in[1][i];
    }
}

int main(void)
{
    hw.Init();
    System::Delay(50);
    Log::StartLog(false);

    hw.seed.StartAudio(AudioCallback);

    hw.midi.StartReceive();

    auto last_print = System::GetNow();

    while(true)
    {
        // Record the current time.
        auto now = System::GetNow();

        // Blink the Seed's LED
        hw.seed.SetLed((now & 511) < 255);

        // Blink each color of the RGB LED at different rates:
        auto blink_red   = (now & 511) < 255;
        auto blink_green = (now & 1023) < 511;
        auto blink_blue  = (now & 2047) < 1023;
        hw.led1.Set(blink_red);
        hw.led2.Set(blink_red, blink_green, blink_blue);
        hw.led1.Update();
        hw.led2.Update();

        hw.midi.Listen(); // Restart MIDI in the event of errors.

        /** Handle MIDI events */
        while(hw.midi.HasEvents())
        {
            auto msg = hw.midi.PopEvent();
            switch(msg.type)
            {
                case NoteOn:
                    // Echo note on messages to the MIDI out
                    {
                        uint8_t bytes[3] = {0x90, 0x00, 0x00};
                        bytes[1]         = msg.data[0];
                        bytes[2]         = msg.data[1];
                        hw.midi.SendMessage(bytes, 3);
                    }
                    break;
                default: break;
            }
        }

        // Print Everything
        if(now - last_print > 50)
        {
            hw.PrintAllControls();
            last_print = now;
        }

        // If we start doing more things, we can remove the delay
        System::Delay(1);
    }

    return 0;
}