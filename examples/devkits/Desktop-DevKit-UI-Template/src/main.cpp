/** Desktop DevKit UI Template
 *
 *  UI template program for the Desktop DevKit
 *
 *  This program uses the libDaisy UI framework to generate events from the input
 *  that can be handled within UI pages.
 *  This is a bit more complex than the simpler approach to handling UI in other examples,
 *  but provides a lot of flexibility for more involved applications.
 *
 *  Since the Desktop DevKit is USB powered via the port on the devkit, this build uses the
 *  external variant of the bootloader so that the seed on the devkit can be reprogrammed
 *  while connected to the single USB port on the desktop devkit.
 *
 *  This program:
 *  - initializes the Desktop DevKit hardware
 *  - starts an audio callback to do pass through, update the controls, and generate UI events
 *  - starts a low-priority callback to process the UI events
 *  - prints input data from the `main_page.h` event hooks over USB serial
 */
#include "desktop_devkit.h"
#include "userinterface.h"

using namespace daisy;
using namespace daisy::desktop_devkit;

/** Naive hysteresis filter for printing CV input changes
 *
 *  Only update the value if the input has changed by enough to print.
*/
struct CvChangeFilter
{
    float prev_val;
    bool  changed;

    /** simple constructor  */
    CvChangeFilter() : prev_val(0.f), changed(false) {}

    /** compares the input to the last value output, and updates it if it exceeds a threshold of 0.1 */
    inline float Process(const float in)
    {
        changed = false;
        if(std::abs(in - prev_val) > 0.01f)
        {
            prev_val = in;
            changed  = true;
        }
        return prev_val;
    }

    /** returns true the value changed last time `Process` was called */
    inline bool Changed() const { return changed; }
};

/** Global Hardware object */
Hardware hw;

/** Global UI object */
Ui ui;

/** FIFO of strings that we can use to move messages from the Audio callback
 * to be printed in the main while() loop.
 */
FIFO<FixedCapStr<32>, 16> msg_fifo;

/** state of 3-way toggle for output when changed */
int prev_toggle_pos = 0;

/** Simple filters for CV inputs to only output values when they have changed */
CvChangeFilter cv_change_filt[2];

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    hw.UpdateAllControls();
    ui.GenerateEvents();

    // Here we'll send some strings based on events not covered in the UI.
    //
    // Certain things (e.g. gate inputs, CV inputs, etc.) often make more sense to
    // handle within the audio callback so that there is a new value once per callback,
    // syncronized with the block of audio it was read in.
    //
    // A few of these (e.g. toggles) could be handled in the UI pages.

    // Gate Inputs
    if(hw.gate_in[0].Trig())
        msg_fifo.PushBack("Gate In 1: Trig!");
    if(hw.gate_in[1].Trig())
        msg_fifo.PushBack("Gate In 2: Trig!");

    // Input detection changes
    if(hw.sd_detect.FallingEdge())
        msg_fifo.PushBack("SD card removed");
    else if(hw.sd_detect.RisingEdge())
        msg_fifo.PushBack("SD card inserted");

    if(hw.left_detect.FallingEdge())
        msg_fifo.PushBack("Left input removed");
    else if(hw.left_detect.RisingEdge())
        msg_fifo.PushBack("Left input inserted");

    if(hw.right_detect.FallingEdge())
        msg_fifo.PushBack("Right input removed");
    else if(hw.right_detect.RisingEdge())
        msg_fifo.PushBack("Right input inserted");

    // 2-way Toggle
    if(hw.tog_sw17.FallingEdge())
        msg_fifo.PushBack("SW17: Down");
    else if(hw.tog_sw17.RisingEdge())
        msg_fifo.PushBack("SW17: Up");

    // 3-way Toggle
    auto new_tog_pos = hw.tog_sw18.Read();
    if(prev_toggle_pos != new_tog_pos)
    {
        switch(new_tog_pos)
        {
            case Switch3::POS_CENTER: msg_fifo.PushBack("SW18: Center"); break;
            case Switch3::POS_UP: msg_fifo.PushBack("SW18: Up"); break;
            case Switch3::POS_DOWN: msg_fifo.PushBack("SW18: Down"); break;
            default: break;
        }
        prev_toggle_pos = new_tog_pos;
    }

    // CV Inputs
    auto cv1 = cv_change_filt[0].Process(hw.cv[0].Value());
    if(cv_change_filt[0].Changed())
    {
        FixedCapStr<32> msg("CV1: ");
        msg.AppendFloat(cv1);
        msg_fifo.PushBack(msg);
    }
    auto cv2 = cv_change_filt[1].Process(hw.cv[1].Value());
    if(cv_change_filt[1].Changed())
    {
        FixedCapStr<32> msg("CV2: ");
        msg.AppendFloat(cv2);
        msg_fifo.PushBack(msg);
    }

    // Audio pass through
    for(size_t i = 0; i < size; i++)
    {
        out[0][i] = in[0][i];
        out[1][i] = in[1][i];
    }
}


/** This callback happens every 4ms (250Hz) and can be interrupted by
 * everything except for the SysTick interrupt.
 * The frequency of this callback is set in the `Hardware::StartLowPriorityCallback`
 * parameters.
 *
 * Handling the UI events here means that anything we do in the main while()
 * loop that may take an indeterminate time (file system I/O, QSPI Saving, etc.)
 * won't cause the UI to stall during those processes
*/
void LowPriorityCallback(void *data)
{
    // Handles events, and redraws the current UI pages
    ui.DoEvents();
}

int main(void)
{
    hw.Init();
    System::Delay(50);
    Logger<LOGGER_EXTERNAL>::StartLog(false);
    ui.Init(&hw);

    hw.StartLowPriorityCallback(LowPriorityCallback, 250);
    hw.seed.StartAudio(AudioCallback);

    auto     last_dac_write = System::GetNow();
    uint16_t dac_value      = 0;

    while(true)
    {
        auto now = System::GetNow();

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

        /** Print messages from the audio callback */
        while(!msg_fifo.IsEmpty())
        {
            auto msg = msg_fifo.PopFront();
            Logger<LOGGER_EXTERNAL>::PrintLine(msg.Cstr());
        }

        System::Delay(1);
    }

    return 0;
}