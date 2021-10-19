#include "daisy_versio.h"

using namespace daisy;

#define PIN_TRIG_IN 24
#define PIN_SW 30
#define PIN_TOGGLE3_0A 6
#define PIN_TOGGLE3_0B 5
#define PIN_TOGGLE3_1A 1
#define PIN_TOGGLE3_1B 0

#define PIN_LED0_R 10
#define PIN_LED0_G 3
#define PIN_LED0_B 4
#define PIN_LED1_R 12
#define PIN_LED1_G 13
#define PIN_LED1_B 11
#define PIN_LED2_R 25
#define PIN_LED2_G 26
#define PIN_LED2_B 14
#define PIN_LED3_R 29
#define PIN_LED3_G 27
#define PIN_LED3_B 15

#define PIN_ADC_CV0 21
#define PIN_ADC_CV1 22
#define PIN_ADC_CV2 28
#define PIN_ADC_CV3 23
#define PIN_ADC_CV4 16
#define PIN_ADC_CV5 17
#define PIN_ADC_CV6 19


void DaisyVersio::Init(bool boost)
{
    // seed init
    seed.Configure();
    seed.Init(boost);
    seed.SetAudioBlockSize(48);
    float blockrate_ = seed.AudioSampleRate() / (float)seed.AudioBlockSize();

    // pin numbers
    uint8_t toggle_pina[] = {PIN_TOGGLE3_0A, PIN_TOGGLE3_1A};
    uint8_t toggle_pinb[] = {PIN_TOGGLE3_0B, PIN_TOGGLE3_1B};
    uint8_t ledr_pin[]    = {PIN_LED0_R, PIN_LED1_R, PIN_LED2_R, PIN_LED3_R};
    uint8_t ledg_pin[]    = {PIN_LED0_G, PIN_LED1_G, PIN_LED2_G, PIN_LED3_G};
    uint8_t ledb_pin[]    = {PIN_LED0_B, PIN_LED1_B, PIN_LED2_B, PIN_LED3_B};
    uint8_t adc_pin[]     = {PIN_ADC_CV0,
                         PIN_ADC_CV1,
                         PIN_ADC_CV2,
                         PIN_ADC_CV3,
                         PIN_ADC_CV4,
                         PIN_ADC_CV5,
                         PIN_ADC_CV6};

    // gate in and momentary switch
    tap.Init(seed.GetPin(PIN_SW));
    dsy_gpio_pin gate_gpio = seed.GetPin(PIN_TRIG_IN);
    gate.Init(&gate_gpio);


    // 3-position switches
    for(size_t i = 0; i < SW_LAST; i++)
    {
        sw[i].Init(seed.GetPin(toggle_pina[i]), seed.GetPin(toggle_pinb[i]));
    }

    // ADC
    AdcChannelConfig adc_cfg[KNOB_LAST];
    for(size_t i = 0; i < KNOB_LAST; i++)
    {
        adc_cfg[i].InitSingle(seed.GetPin(adc_pin[i]));
    }
    seed.adc.Init(adc_cfg, 7);

    for(size_t i = 0; i < KNOB_LAST; i++)
    {
        knobs[i].Init(seed.adc.GetPtr(i), blockrate_, true);
    }

    // RGB LEDs
    for(size_t i = 0; i < LED_LAST; i++)
    {
        dsy_gpio_pin r = seed.GetPin(ledr_pin[i]);
        dsy_gpio_pin g = seed.GetPin(ledg_pin[i]);
        dsy_gpio_pin b = seed.GetPin(ledb_pin[i]);
        leds[i].Init(r, g, b, true);
    }
}

void DaisyVersio::SetHidUpdateRates()
{
    for(size_t i = 0; i < KNOB_LAST; i++)
    {
        knobs[i].SetSampleRate(AudioCallbackRate());
    }
}


void DaisyVersio::StartAudio(AudioHandle::InterleavingAudioCallback cb)
{
    seed.StartAudio(cb);
}

void DaisyVersio::StartAudio(AudioHandle::AudioCallback cb)
{
    seed.StartAudio(cb);
}

void DaisyVersio::ChangeAudioCallback(AudioHandle::InterleavingAudioCallback cb)
{
    seed.ChangeAudioCallback(cb);
}

void DaisyVersio::ChangeAudioCallback(AudioHandle::AudioCallback cb)
{
    seed.ChangeAudioCallback(cb);
}

void DaisyVersio::StopAudio()
{
    seed.StopAudio();
}

void DaisyVersio::SetAudioBlockSize(size_t size)
{
    seed.SetAudioBlockSize(size);
    SetHidUpdateRates();
}

size_t DaisyVersio::AudioBlockSize()
{
    return seed.AudioBlockSize();
}

void DaisyVersio::SetAudioSampleRate(SaiHandle::Config::SampleRate samplerate)
{
    seed.SetAudioSampleRate(samplerate);
    SetHidUpdateRates();
}

float DaisyVersio::AudioSampleRate()
{
    return seed.AudioSampleRate();
}

float DaisyVersio::AudioCallbackRate()
{
    return seed.AudioCallbackRate();
}

void DaisyVersio::StartAdc()
{
    seed.adc.Start();
}

void DaisyVersio::StopAdc()
{
    seed.adc.Stop();
}

void DaisyVersio::ProcessAnalogControls()
{
    for(size_t i = 0; i < KNOB_LAST; i++)
    {
        knobs[i].Process();
    }
}

bool DaisyVersio::SwitchPressed()
{
    return tap.Pressed();
}

bool DaisyVersio::Gate()
{
    return !gate.State();
}

void DaisyVersio::SetLed(size_t idx, float red, float green, float blue)
{
    leds[idx].Set(red, green, blue);
}

void DaisyVersio::DelayMs(size_t del)
{
    seed.DelayMs(del);
}

void DaisyVersio::UpdateLeds()
{
    for(size_t i = 0; i < LED_LAST; i++)
    {
        leds[i].Update();
    }
}

float DaisyVersio::GetKnobValue(int idx)
{
    return knobs[idx].Value();
}

void DaisyVersio::UpdateExample()
{
    tap.Debounce();
    for(size_t i = 0; i < LED_LAST - 1; i++)
        SetLed(i, 0, 0, 0);

    SetLed(sw[0].Read(), knobs[0].Value(), knobs[1].Value(), knobs[2].Value());
    SetLed(sw[1].Read(), knobs[3].Value(), knobs[4].Value(), knobs[5].Value());

    SetLed(3, Gate(), SwitchPressed(), knobs[6].Value());
}
