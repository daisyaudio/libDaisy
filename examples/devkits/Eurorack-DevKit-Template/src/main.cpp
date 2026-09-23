/** Eurorack DevKit Template
 *
 *  Basic template program for the Eurorack DevKit
 *
 *  This program:
 *  - initializes the Eurorack DevKit hardware
 *  - starts an audio callback to do pass through and update the controls
 *  - blinks the LEDs
 *  - Writes a signal to the DAC outputs
 *  - Sends MIDI notes received on the MIDI input to the MIDI output
 *  - Prints all input state over USB serial
 */
#include "eurorack_devkit.h"

using namespace daisy;
using namespace daisy::eurorack_devkit;

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
    hw.seed.StartLog(false);

    hw.seed.StartAudio(AudioCallback);

    hw.midi.StartReceive();

    auto     last_print     = System::GetNow();
    auto     last_dac_write = System::GetNow();
    uint16_t dac_value      = 0;

    while(true)
    {
        auto now = System::GetNow();
        // Blink the Seed's LED
        hw.seed.SetLed((now & 511) < 255);

        // Blink each color of the RGB LED at different rates:
        auto blink_red   = (now & 511) < 255;
        auto blink_green = (now & 1023) < 511;
        auto blink_blue  = (now & 2047) < 1023;
        hw.led1.Set(blink_red, blink_green, blink_blue);
        hw.led2.Set(blink_red);
        hw.led1.Update();
        hw.led2.Update();

        // Restart MIDI in the event of errors.
        hw.midi.Listen();

        // Handle MIDI events
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

        /** Write a ramp (cv out 1) and saw (cv out 2) waveform to the DAC outputs
         * Approx. 2.5s period for waveform (0.005s * (4096 / 8)) = 2.56s
         */
        if(now - last_dac_write > 5)
        {
            dac_value += 8;
            if(dac_value > 4095)
            {
                dac_value = 0;
            }
            hw.SetDacOut(DacHandle::Channel::ONE, dac_value);
            hw.SetDacOut(DacHandle::Channel::TWO, 4095 - dac_value);
            last_dac_write = now;
        }

        if(now - last_print > 100)
        {
            hw.PrintAllControls();
            last_print = now;
        }

        System::Delay(1);
    }

    return 0;
}