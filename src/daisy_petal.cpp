#include "daisy_petal.h"

using namespace daisy;

#ifndef SAMPLE_RATE
//#define SAMPLE_RATE DSY_AUDIO_SAMPLE_RATE
#define SAMPLE_RATE 48014.f
#endif

// Hardware related defines.
// Switches
#define SW_1_PIN 8
#define SW_2_PIN 9
#define SW_3_PIN 10
#define SW_4_PIN 13
#define SW_5_PIN 25
#define SW_6_PIN 26
#define SW_7_PIN 7

// Encoder
#define ENC_A_PIN 28
#define ENC_B_PIN 27
#define ENC_CLICK_PIN 14

// Knobs
#define PIN_EXPRESSION 15
#define PIN_KNOB_1 16
#define PIN_KNOB_2 19
#define PIN_KNOB_3 17
#define PIN_KNOB_4 20
#define PIN_KNOB_5 18
#define PIN_KNOB_6 21

static constexpr I2CHandle::Config petal_led_i2c_config
    = {I2CHandle::Config::Peripheral::I2C_1,
       {{DSY_GPIOB, 8}, {DSY_GPIOB, 9}},
       I2CHandle::Config::Speed::I2C_1MHZ};

enum LedOrder
{
    LED_RING_1_R,
    LED_RING_1_G,
    LED_RING_1_B,
    LED_RING_5_R,
    LED_RING_5_G,
    LED_RING_5_B,
    LED_RING_2_R,
    LED_RING_2_G,
    LED_RING_2_B,
    LED_RING_6_R,
    LED_RING_6_G,
    LED_RING_6_B,
    LED_RING_3_R,
    LED_RING_3_G,
    LED_RING_3_B,
    LED_FS_1,
    LED_RING_4_R,
    LED_RING_4_G,
    LED_RING_4_B,
    LED_RING_7_R,
    LED_RING_7_G,
    LED_RING_7_B,
    LED_RING_8_R,
    LED_RING_8_G,
    LED_RING_8_B,
    LED_FS_2,
    LED_FS_3,
    LED_FS_4,
    LED_FAKE1,
    LED_FAKE2,
    LED_FAKE3,
    LED_FAKE4,
    LED_LAST,
};

static LedDriverPca9685<2, true>::DmaBuffer DMA_BUFFER_MEM_SECTION
    petal_led_dma_buffer_a,
    petal_led_dma_buffer_b;

void DaisyPetal::Init(bool boost)
{
    // Set Some numbers up for accessors.
    // Initialize the hardware.
    seed.Configure();
    seed.Init(boost);
    InitSwitches();
    InitEncoder();
    InitLeds();
    InitAnalogControls();
    SetAudioBlockSize(48);
    //seed.usb_handle.Init(UsbHandle::FS_INTERNAL);
}

void DaisyPetal::DelayMs(size_t del)
{
    seed.DelayMs(del);
}

void DaisyPetal::SetHidUpdateRates()
{
    for(size_t i = 0; i < KNOB_LAST; i++)
    {
        knob[i].SetSampleRate(AudioCallbackRate());
    }
    for(size_t i = 0; i < FOOTSWITCH_LED_LAST; i++)
    {
        footswitch_led[i].SetSampleRate(AudioCallbackRate());
    }
    expression.SetSampleRate(AudioCallbackRate());
}


void DaisyPetal::StartAudio(AudioHandle::InterleavingAudioCallback cb)
{
    seed.StartAudio(cb);
}

void DaisyPetal::StartAudio(AudioHandle::AudioCallback cb)
{
    seed.StartAudio(cb);
}

void DaisyPetal::ChangeAudioCallback(AudioHandle::InterleavingAudioCallback cb)
{
    seed.ChangeAudioCallback(cb);
}

void DaisyPetal::ChangeAudioCallback(AudioHandle::AudioCallback cb)
{
    seed.ChangeAudioCallback(cb);
}

void DaisyPetal::StopAudio()
{
    seed.StopAudio();
}

void DaisyPetal::SetAudioBlockSize(size_t size)
{
    seed.SetAudioBlockSize(size);
    SetHidUpdateRates();
}

size_t DaisyPetal::AudioBlockSize()
{
    return seed.AudioBlockSize();
}

void DaisyPetal::SetAudioSampleRate(SaiHandle::Config::SampleRate samplerate)
{
    seed.SetAudioSampleRate(samplerate);
    SetHidUpdateRates();
}

float DaisyPetal::AudioSampleRate()
{
    return seed.AudioSampleRate();
}

float DaisyPetal::AudioCallbackRate()
{
    return seed.AudioCallbackRate();
}

void DaisyPetal::StartAdc()
{
    seed.adc.Start();
}

void DaisyPetal::StopAdc()
{
    seed.adc.Stop();
}


void DaisyPetal::ProcessAnalogControls()
{
    for(size_t i = 0; i < KNOB_LAST; i++)
    {
        knob[i].Process();
    }
    expression.Process();
}

float DaisyPetal::GetKnobValue(Knob k)
{
    size_t idx;
    idx = k < KNOB_LAST ? k : KNOB_1;
    return knob[idx].Value();
}

float DaisyPetal::GetExpression()
{
    return expression.Value();
}

void DaisyPetal::ProcessDigitalControls()
{
    encoder.Debounce();
    for(size_t i = 0; i < SW_LAST; i++)
    {
        switches[i].Debounce();
    }
}


void DaisyPetal::ClearLeds()
{
    // Using Color
    //    Color c;
    //    c.Init(Color::PresetColor::OFF);
    //    for(size_t i = 0; i < RING_LED_LAST; i++)
    //    {
    //        ring_led[i].SetColor(c);
    //    }
    for(size_t i = 0; i < RING_LED_LAST; i++)
    {
        SetRingLed(static_cast<RingLed>(i), 0.0f, 0.0f, 0.0f);
    }
    for(size_t i = 0; i < FOOTSWITCH_LED_LAST; i++)
    {
        SetFootswitchLed(static_cast<FootswitchLed>(i), 0.0f);
    }
}

void DaisyPetal::UpdateLeds()
{
    led_driver_.SwapBuffersAndTransmit();
}

void DaisyPetal::SetRingLed(RingLed idx, float r, float g, float b)
{
    uint8_t r_addr[RING_LED_LAST] = {LED_RING_1_R,
                                     LED_RING_2_R,
                                     LED_RING_3_R,
                                     LED_RING_4_R,
                                     LED_RING_5_R,
                                     LED_RING_6_R,
                                     LED_RING_7_R,
                                     LED_RING_8_R};
    uint8_t g_addr[RING_LED_LAST] = {LED_RING_1_G,
                                     LED_RING_2_G,
                                     LED_RING_3_G,
                                     LED_RING_4_G,
                                     LED_RING_5_G,
                                     LED_RING_6_G,
                                     LED_RING_7_G,
                                     LED_RING_8_G};
    uint8_t b_addr[RING_LED_LAST] = {LED_RING_1_B,
                                     LED_RING_2_B,
                                     LED_RING_3_B,
                                     LED_RING_4_B,
                                     LED_RING_5_B,
                                     LED_RING_6_B,
                                     LED_RING_7_B,
                                     LED_RING_8_B};


    led_driver_.SetLed(r_addr[idx], r);
    led_driver_.SetLed(g_addr[idx], g);
    led_driver_.SetLed(b_addr[idx], b);
}
void DaisyPetal::SetFootswitchLed(FootswitchLed idx, float bright)
{
    uint8_t fs_addr[FOOTSWITCH_LED_LAST]
        = {LED_FS_1, LED_FS_2, LED_FS_3, LED_FS_4};
    led_driver_.SetLed(fs_addr[idx], bright);
}

void DaisyPetal::InitSwitches()
{
    //    // button1
    //    button1.Init(seed.GetPin(SW_1_PIN), callback_rate_);
    //    // button2
    //    button2.Init(seed.GetPin(SW_2_PIN), callback_rate_);
    //
    //    buttons[BUTTON_1] = &button1;
    //    buttons[BUTTON_2] = &button2;
    uint8_t pin_numbers[SW_LAST] = {
        SW_1_PIN,
        SW_2_PIN,
        SW_3_PIN,
        SW_4_PIN,
        SW_5_PIN,
        SW_6_PIN,
        SW_7_PIN,
    };

    for(size_t i = 0; i < SW_LAST; i++)
    {
        switches[i].Init(seed.GetPin(pin_numbers[i]));
    }
}

void DaisyPetal::InitEncoder()
{
    dsy_gpio_pin a, b, click;
    a     = seed.GetPin(ENC_A_PIN);
    b     = seed.GetPin(ENC_B_PIN);
    click = seed.GetPin(ENC_CLICK_PIN);
    encoder.Init(a, b, click);
}

void DaisyPetal::InitLeds()
{
    // LEDs are on the LED Driver.

    // Need to figure out how we want to handle that.
    uint8_t   addr[2] = {0x00, 0x01};
    I2CHandle i2c;
    i2c.Init(petal_led_i2c_config);
    led_driver_.Init(i2c, addr, petal_led_dma_buffer_a, petal_led_dma_buffer_b);
    ClearLeds();
    UpdateLeds();
}

void DaisyPetal::InitAnalogControls()
{
    // Set order of ADCs based on CHANNEL NUMBER
    // KNOB_LAST + 1 because of Expression input
    AdcChannelConfig cfg[KNOB_LAST + 1];
    // Init with Single Pins
    cfg[KNOB_1].InitSingle(seed.GetPin(PIN_KNOB_1));
    cfg[KNOB_2].InitSingle(seed.GetPin(PIN_KNOB_2));
    cfg[KNOB_3].InitSingle(seed.GetPin(PIN_KNOB_3));
    cfg[KNOB_4].InitSingle(seed.GetPin(PIN_KNOB_4));
    cfg[KNOB_5].InitSingle(seed.GetPin(PIN_KNOB_5));
    cfg[KNOB_6].InitSingle(seed.GetPin(PIN_KNOB_6));
    // Special case for Expression
    cfg[KNOB_LAST].InitSingle(seed.GetPin(PIN_EXPRESSION));

    seed.adc.Init(cfg, KNOB_LAST + 1);
    // Make an array of pointers to the knob.
    for(int i = 0; i < KNOB_LAST; i++)
    {
        knob[i].Init(seed.adc.GetPtr(i), AudioCallbackRate());
    }
    expression.Init(seed.adc.GetPtr(KNOB_LAST), AudioCallbackRate());
}
