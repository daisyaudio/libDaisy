#include "hid/encoder.h"

using namespace daisy;

void Encoder::Init(dsy_gpio_pin a,
                   dsy_gpio_pin b,
                   dsy_gpio_pin click,
                   float        update_rate)
{
    // Init GPIO for A, and B
    hw_a_.pin  = a;
    hw_a_.mode = DSY_GPIO_MODE_INPUT;
    hw_a_.pull = DSY_GPIO_PULLUP;
    hw_b_.pin  = b;
    hw_b_.mode = DSY_GPIO_MODE_INPUT;
    hw_b_.pull = DSY_GPIO_PULLUP;
    dsy_gpio_init(&hw_a_);
    dsy_gpio_init(&hw_b_);
    // Default Initialization for Switch
    sw_.Init(click);
    // Set initial states, etc.
    inc_ = 0;
    a_ = b_ = 0xff;
}

void Encoder::Debounce()
{
    // Shift Button states to debounce
    a_ = (a_ << 1) | dsy_gpio_read(&hw_a_);
    b_ = (b_ << 1) | dsy_gpio_read(&hw_b_);
    // Debounce built-in switch
    sw_.Debounce();
    // infer increment direction
    inc_ = 0; // reset inc_ first
    if((a_ & 0x03) == 0x02 && (b_ & 0x03) == 0x00)
    {
        inc_ = 1;
    }
    else if((b_ & 0x03) == 0x02 && (a_ & 0x03) == 0x00)
    {
        inc_ = -1;
    }
}
