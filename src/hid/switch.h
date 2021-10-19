#pragma once
#ifndef DSY_SWITCH_H
#define DSY_SWITCH_H
#include "daisy_core.h"
#include "per/gpio.h"
#include "sys/system.h"

namespace daisy
{
/** 
    Generic Class for handling momentary/latching switches \n 
    Inspired/influenced by Mutable Instruments (pichenettes) Switch classes
    @author Stephen Hensley
    @date December 2019
    @ingroup controls
*/
class Switch
{
  public:
    /** Specifies the expected behavior of the switch */
    enum Type
    {
        TYPE_TOGGLE,    /**< & */
        TYPE_MOMENTARY, /**< & */
    };
    /** Specifies whether the pressed is HIGH or LOW. */
    enum Polarity
    {
        POLARITY_NORMAL,   /**< & */
        POLARITY_INVERTED, /**< & */
    };

    /** Specifies whether to use built-in Pull Up/Down resistors to hold button at a given state when not engaged. */
    enum Pull
    {
        PULL_UP,   /**< & */
        PULL_DOWN, /**< & */
        PULL_NONE, /**< & */
    };

    Switch() {}
    ~Switch() {}

    /** 
    Initializes the switch object with a given port/pin combo.
    \param pin port/pin object to tell the switch which hardware pin to use.
    \param update_rate Does nothing. Backwards compatibility until next breaking update.
    \param t switch type -- Default: TYPE_MOMENTARY
    \param pol switch polarity -- Default: POLARITY_INVERTED
    \param pu switch pull up/down -- Default: PULL_UP
    */
    void
    Init(dsy_gpio_pin pin, float update_rate, Type t, Polarity pol, Pull pu);

    /**
       Simplified Init.
       \param pin port/pin object to tell the switch which hardware pin to use.
       \param update_rate Left for backwards compatibility until next breaking change.
    */
    void Init(dsy_gpio_pin pin, float update_rate = 0.f);

    /** 
    Called at update_rate to debounce and handle timing for the switch.
    In order for events not to be missed, its important that the Edge/Pressed checks
    be made at the same rate as the debounce function is being called.
    */
    void Debounce();

    /** \return true if a button was just pressed. */
    inline bool RisingEdge() const { return state_ == 0x7f; }

    /** \return true if the button was just released */
    inline bool FallingEdge() const { return state_ == 0x80; }

    /** \return true if the button is held down (or if the toggle is on) */
    inline bool Pressed() const { return state_ == 0xff; }

    /** \return true if the button is held down, without debouncing */
    inline bool RawState()
    {
        return flip_ ? !dsy_gpio_read(&hw_gpio_) : dsy_gpio_read(&hw_gpio_);
    }

    /** \return the time in milliseconds that the button has been held (or toggle has been on) */
    inline float TimeHeldMs() const
    {
        return Pressed() ? System::GetNow() - rising_edge_time_ : 0;
    }

    /** Left for backwards compatability until next breaking change
     * \param update_rate Doesn't do anything
    */
    inline void SetUpdateRate(float update_rate) {}

  private:
    Type     t_;
    dsy_gpio hw_gpio_;
    uint8_t  state_;
    bool     flip_;
    float    rising_edge_time_;
};

} // namespace daisy
#endif
