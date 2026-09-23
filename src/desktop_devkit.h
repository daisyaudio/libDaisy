#pragma once
#include "daisy_seed.h"

namespace daisy::desktop_devkit
{
class Hardware
{
  public:
    Hardware() {}

    using LedDriver = LedDriverPca9685<1, true>;
    using ButtonSr  = ShiftRegister4021<2, 1>;

    DaisySeed       seed;
    Switch          tog_sw17;
    Switch3         tog_sw18;
    ButtonSr        button_sr;
    GateIn          gate_in[2];
    AnalogControl   pot[8], cv[2];
    DigitalControl  button[16];
    MidiUartHandler midi;
    LedDriver       led_driver;
    TimerHandle     tim5_handle;
    // Plug Detection
    Switch sd_detect, left_detect, right_detect;

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

    /** Reset all LEDs to their off state */
    void ClearLeds();

    /** Flush the current LED values to the hardware.
   *  Changes to the LED brightnesses will only take effect
   * when this method is called.
   */
    void UpdateLeds();

    /** Set the brightness for a specific LED
   *  @param idx index of the LED from 0-15 corresponding to D1 to D16
   *  @param brightness brightness level from 0-1
   */
    void SetLed(int idx, float brightness);

    /** This starts a recurring callback at a specified interval. The callback is
   * run on the lowest priority interrupt level. This provides a hook for
   * non-background tasks that should interrupt low level activity like disk
   * i/o, etc.
   *
   * @param cb callback function to take place at the specified frequency
   * @param target_freq target frequency in hertz for the callback to run
   * @param data optional pointer to context data accessible from the callback
   */
    void StartLowPriorityCallback(TimerHandle::PeriodElapsedCallback cb,
                                  uint32_t target_freq,
                                  void *   data = nullptr);

    /** Stops an ongoing low-priority callback */
    void StopLowPriorityCallback();
};

} // namespace daisy::desktop_devkit