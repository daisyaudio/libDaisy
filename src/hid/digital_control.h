#pragma once
#include "sys/system.h"

namespace daisy
{
/** Debouncer for non-GPIO switch inputs independent of their source.
 *  For getting `Switch` like behavior from inputs connected to shiftregisters,
 *  etc.
 *
 *  Maintains the state and allows checks for transitions, timing, etc.
 *
 *  Unlike `AnalogControl`, this does not take a pointer to an externally
 * updated value. This is because the input to this may often be a callable
 * (e.g. shiftreg.State(x), gpio.Read()) that may not be stateful.
 */
struct DigitalControl
{
    uint8_t  state_;
    bool     flip_;
    uint32_t rising_edge_time_;

    DigitalControl() {}

    inline void Init(bool flip)
    {
        flip_  = flip;
        state_ = 0x00;
    }

    inline void Debounce(bool input)
    {
        uint8_t in_val;
        if(flip_)
            in_val = input ? 0 : 1;
        else
            in_val = input ? 1 : 0;
        state_ = (state_ << 1) | in_val;
        if(RisingEdge())
        {
            rising_edge_time_ = System::GetNow();
        }
    }

    inline bool RisingEdge() const { return state_ == 0x7f; }
    inline bool FallingEdge() const { return state_ == 0x80; }
    inline bool Pressed() const { return state_ == 0xff; }

    inline uint32_t TimeHeldMs() const
    {
        return Pressed() ? System::GetNow() - rising_edge_time_ : 0;
    }
};

} // namespace daisy