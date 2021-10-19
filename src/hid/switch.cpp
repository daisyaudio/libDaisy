#include "hid/switch.h"
using namespace daisy;

void Switch::Init(dsy_gpio_pin pin,
                  float        update_rate,
                  Type         t,
                  Polarity     pol,
                  Pull         pu)
{
    state_ = 0x00;
    t_     = t;
    // Flip may seem opposite to logical direction,
    // but here 1 is pressed, 0 is not.
    flip_         = pol == POLARITY_INVERTED ? true : false;
    hw_gpio_.pin  = pin;
    hw_gpio_.mode = DSY_GPIO_MODE_INPUT;
    switch(pu)
    {
        case PULL_UP: hw_gpio_.pull = DSY_GPIO_PULLUP; break;
        case PULL_DOWN: hw_gpio_.pull = DSY_GPIO_PULLDOWN; break;
        case PULL_NONE: hw_gpio_.pull = DSY_GPIO_NOPULL; break;
        default: hw_gpio_.pull = DSY_GPIO_PULLUP; break;
    }
    dsy_gpio_init(&hw_gpio_);
}
void Switch::Init(dsy_gpio_pin pin, float update_rate)
{
    Init(pin, update_rate, TYPE_MOMENTARY, POLARITY_INVERTED, PULL_UP);
}

void Switch::Debounce()
{
    // shift over, and introduce new state.
    state_ = (state_ << 1)
             | (flip_ ? !dsy_gpio_read(&hw_gpio_) : dsy_gpio_read(&hw_gpio_));
    // Set time at which button was pressed
    if(state_ == 0x7f)
        rising_edge_time_ = System::GetNow();
}
