#pragma once

#include "daisy_seed.h"

namespace daisy::pedal_devkit
{
/** Board support for the Pedal DevKit
 *
 *  Provides access to all HW on the DevKit with convenience methods for common
 * behaviors like initialization, and updating values.
 */
class Hardware
{
  public:
    Hardware() {}

    // SOM
    DaisySeed seed;

    /** Number of Pots on the DevKit */
    inline static constexpr size_t kNumPots = 6;

    // Analog Inputs
    /** Pot inputs. Labeled VR1 through VR6 on the PCB. */
    AnalogControl pot[kNumPots];
    AnalogControl expression;

    // Toggles
    Switch3 sw1;
    Switch  sw2, sw3;

    // Tactile Switches
    Switch sw4, sw5;

    // Footswitches
    Switch fsw_1, fsw_2;

    // SD Card detection
    Switch sd_detect;

    // LEDs
    Led    led1;
    RgbLed led2;

    // 5-Pin DIN MIDI via UART
    daisy::MidiUartHandler midi;

    /** Initialize all of the hardware, and start the ADC */
    void Init();

    /** Process all analog inputs, debounce switch inputs, and update internally
   * stored states */
    void UpdateAllControls();

    /** Print the state of all hardware inputs in a human readable block over
   * serial using the Seed's built-in USB port.
   *
   * Start the Seed's logger with `seed.StartLog()` prior to using this method.
   */
    void PrintAllControls();
};
} // namespace daisy::pedal_devkit
