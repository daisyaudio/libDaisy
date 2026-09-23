#pragma once

#include "daisy_seed.h"

namespace daisy::eurorack_devkit {

class Hardware {
public:
  Hardware() {}

  /** Number of Pots on the DevKit */
  inline static constexpr size_t kNumPots = 8;
  /** Number of CV inputs on the DevKit */
  inline static constexpr size_t kNumCVs = 4;

  // SOM
  DaisySeed seed;
  // Tactile Switches
  Switch sw1, sw2;
  // Toggle Switches
  Switch3 sw3;
  Switch sw4;
  // Pots
  AnalogControl pot[kNumPots];
  // Bipolar CV Inputs
  AnalogControl cv[kNumCVs];
  // Gate Inputs
  GateIn gate_in1, gate_in2;
  // MIDI
  MidiUartHandler midi;

  // LEDs
  RgbLed led1;
  Led led2;

  /** Initialize the hardware */
  void Init();

  /** Process all analog inputs, debounce digital inputs, and update their
   * internally stored states.
   */
  void UpdateAllControls();

  /** Write the code to the DAC output
   *  @param channel DAC channel to write to
   *  @param code the 0-4096 code to write corresponding to the output voltage
   */
  void SetDacOut(DacHandle::Channel channel, uint16_t code);

  /** Print the state of all hardware inputs in a human readable block over
   * serial using the Seed's built-in USB port.
   *
   * Start the Seed's logger with `seed.StartLog()` prior to using this method.
   */
  void PrintAllControls();
};

} // namespace daisy::eurorack_devkit
