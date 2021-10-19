#include "daisy_field.h"
#ifndef SAMPLE_RATE
#define SAMPLE_RATE DSY_AUDIO_SAMPLE_RATE /**< & */
#endif

//// Rev1 Pins
//// Hardware related defines.
//#define SW_1_PIN 29    /**< & */
//#define SW_2_PIN 28    /**< & */
//#define SW_3_PIN 27    /**< & */
//#define GATE_OUT_PIN 0 /**< & */
//#define GATE_IN_PIN 1  /**< & */
//
//// keyboard switches shift register
//#define KB_SW_SR_CS_PIN 8  /**< & */
//#define KB_SW_SR_CLK_PIN 9 /**< & */
//#define KB_SW_SR_D1_PIN 10 /**< & */
//#define KB_SW_SR_D2_PIN 11 /**< & */
//
//// UART for MIDI via TRS jacks on Field
//#define MIDI_OUT_PIN 14 /**< & */
//#define MIDI_IN_PIN 15  /**< & */
//
////CD4051 Select Pin controls
//#define MUX_SEL_0_PIN 21 /**< & */
//#define MUX_SEL_1_PIN 20 /**< & */
//#define MUX_SEL_2_PIN 19 /**< & */
//
//#define MUX_ADC_PIN 16 /**< & */
//#define CV1_ADC_PIN 17 /**< & */
//#define CV2_ADC_PIN 18 /**< & */
//#define CV3_ADC_PIN 23 /**< & */
//#define CV4_ADC_PIN 22 /**< & */

// Rev2 Pins
#define PIN_GATE_IN 0
#define PIN_SPI_CS 7
#define PIN_SPI_SCK 8
#define PIN_OLED_CMD 9
#define PIN_SPI_MOSI 10
#define PIN_I2C_SCL 11
#define PIN_I2C_SDA 12
#define PIN_MIDI_OUT 13
#define PIN_MIDI_IN 14
#define PIN_GATE_OUT 15
#define PIN_ADC_POT_MUX 16
#define PIN_ADC_CV_1 17
#define PIN_ADC_CV_2 18
#define PIN_MUX_SEL_2 19
#define PIN_MUX_SEL_1 20
#define PIN_MUX_SEL_0 21
#define PIN_CD4021_D1 26
#define PIN_CD4021_CS 27
#define PIN_CD4021_CLK 28
#define PIN_SW_2 29
#define PIN_SW_1 30

// IT LOOKS LIKE THESE MAY NEED TO GET SWAPPED.....
#define PIN_DAC_2 22    // Jumped on Rev2 from 24
#define PIN_DAC_1 23    // Jumped on Rev2 from 25
#define PIN_ADC_CV_4 24 // Jumped on Rev2 from 22
#define PIN_ADC_CV_3 25 // Jumped on Rev2 from 23

using namespace daisy;

static constexpr I2CHandle::Config field_led_i2c_config
    = {I2CHandle::Config::Peripheral::I2C_1,
       {{DSY_GPIOB, 8}, {DSY_GPIOB, 9}},
       I2CHandle::Config::Speed::I2C_1MHZ};

static LedDriverPca9685<2, true>::DmaBuffer DMA_BUFFER_MEM_SECTION
    field_led_dma_buffer_a,
    field_led_dma_buffer_b;

void DaisyField::Init(bool boost)
{
    seed.Configure();
    seed.Init(boost);
    seed.SetAudioBlockSize(48);

    // Switches
    uint8_t sw_pin[]  = {PIN_SW_1, PIN_SW_2};
    uint8_t adc_pin[] = {PIN_ADC_CV_1,
                         PIN_ADC_CV_2,
                         PIN_ADC_CV_3,
                         PIN_ADC_CV_4,
                         PIN_ADC_POT_MUX};

    for(size_t i = 0; i < SW_LAST; i++)
    {
        dsy_gpio_pin p = seed.GetPin(sw_pin[i]);
        sw[i].Init(p);
    }

    // ADCs
    AdcChannelConfig adc_cfg[CV_LAST + 1];
    for(size_t i = 0; i < CV_LAST; i++)
    {
        adc_cfg[i].InitSingle(seed.GetPin(adc_pin[i]));
    }
    // POT MUX
    adc_cfg[CV_LAST].InitMux(seed.GetPin(PIN_ADC_POT_MUX),
                             8,
                             seed.GetPin(PIN_MUX_SEL_0),
                             seed.GetPin(PIN_MUX_SEL_1),
                             seed.GetPin(PIN_MUX_SEL_2));
    seed.adc.Init(adc_cfg, 5);

    // Order of pots on the hardware connected to mux.
    size_t pot_order[KNOB_LAST] = {0, 3, 1, 4, 2, 5, 6, 7};
    for(size_t i = 0; i < KNOB_LAST; i++)
    {
        knob[i].Init(seed.adc.GetMuxPtr(4, pot_order[i]), AudioCallbackRate());
    }
    for(size_t i = 0; i < CV_LAST; i++)
    {
        cv[i].InitBipolarCv(seed.adc.GetPtr(i), AudioCallbackRate());
    }

    // Keyboard
    ShiftRegister4021<2>::Config keyboard_cfg;
    keyboard_cfg.clk     = seed.GetPin(PIN_CD4021_CLK);
    keyboard_cfg.latch   = seed.GetPin(PIN_CD4021_CS);
    keyboard_cfg.data[0] = seed.GetPin(PIN_CD4021_D1);
    keyboard_sr_.Init(keyboard_cfg);

    // OLED
    OledDisplay<SSD130x4WireSpi128x64Driver>::Config display_config;

    display_config.driver_config.transport_config.pin_config.dc
        = seed.GetPin(PIN_OLED_CMD);
    display_config.driver_config.transport_config.pin_config.reset
        = {DSY_GPIOX, 0}; // Not a real pin...

    display.Init(display_config);

    // LEDs
    // 2x PCA9685 addresses 0x00, and 0x02
    uint8_t   addr[2] = {0x00, 0x02};
    I2CHandle i2c;
    i2c.Init(field_led_i2c_config);
    led_driver.Init(i2c, addr, field_led_dma_buffer_a, field_led_dma_buffer_b);

    // Gate In
    dsy_gpio_pin gate_in_pin;
    gate_in_pin = seed.GetPin(PIN_GATE_IN);
    gate_in.Init(&gate_in_pin);
    // Gate Out
    gate_out.mode = DSY_GPIO_MODE_OUTPUT_PP;
    gate_out.pull = DSY_GPIO_NOPULL;
    gate_out.pin  = seed.GetPin(PIN_GATE_OUT);
    dsy_gpio_init(&gate_out);

    //midi
    MidiUartHandler::Config midi_config;
    midi.Init(midi_config);

    DacHandle::Config cfg;
    cfg.bitdepth   = DacHandle::BitDepth::BITS_12;
    cfg.buff_state = DacHandle::BufferState::ENABLED;
    cfg.mode       = DacHandle::Mode::POLLING;
    cfg.chn        = DacHandle::Channel::BOTH;
    seed.dac.Init(cfg);
}

void DaisyField::DelayMs(size_t del)
{
    seed.DelayMs(del);
}

void DaisyField::StartAudio(AudioHandle::InterleavingAudioCallback cb)
{
    seed.StartAudio(cb);
}

void DaisyField::StartAudio(AudioHandle::AudioCallback cb)
{
    seed.StartAudio(cb);
}

void DaisyField::StopAudio()
{
    seed.StopAudio();
}

void DaisyField::ChangeAudioCallback(AudioHandle::InterleavingAudioCallback cb)
{
    seed.ChangeAudioCallback(cb);
}

void DaisyField::ChangeAudioCallback(AudioHandle::AudioCallback cb)
{
    seed.ChangeAudioCallback(cb);
}

void DaisyField::SetHidUpdateRates()
{
    //set the hids to the new update rate
    for(size_t i = 0; i < KNOB_LAST; i++)
    {
        knob[i].SetSampleRate(AudioCallbackRate());
    }
    for(size_t i = 0; i < CV_LAST; i++)
    {
        cv[i].SetSampleRate(AudioCallbackRate());
    }
}

void DaisyField::SetAudioSampleRate(SaiHandle::Config::SampleRate samplerate)
{
    seed.SetAudioSampleRate(samplerate);
    SetHidUpdateRates();
}

float DaisyField::AudioSampleRate()
{
    return seed.AudioSampleRate();
}

void DaisyField::SetAudioBlockSize(size_t size)
{
    seed.SetAudioBlockSize(size);
    SetHidUpdateRates();
}

size_t DaisyField::AudioBlockSize()
{
    return seed.AudioBlockSize();
}

float DaisyField::AudioCallbackRate()
{
    return seed.AudioCallbackRate();
}


void DaisyField::StartAdc()
{
    seed.adc.Start();
}

void DaisyField::StopAdc()
{
    seed.adc.Stop();
}

/** Turns on the built-in 12-bit DAC on the Daisy Seed */
void DaisyField::StartDac()
{
    //dsy_dac_start(DSY_DAC_CHN_BOTH);
}

void DaisyField::ProcessAnalogControls()
{
    for(size_t i = 0; i < KNOB_LAST; i++)
        knob[i].Process();
    for(size_t i = 0; i < CV_LAST; i++)
        cv[i].Process();
}

void DaisyField::ProcessDigitalControls()
{
    // Switches
    for(size_t i = 0; i < SW_LAST; i++)
    {
        sw[i].Debounce();
        // Keyboard SM
    }
    //dsy_sr_4021_update(&keyboard_sr_);
    keyboard_sr_.Update();
    for(size_t i = 0; i < 16; i++)
    {
        uint8_t keyidx, keyoffset;
        keyoffset = i > 7 ? 8 : 0;
        keyidx    = (7 - (i % 8)) + keyoffset;
        keyboard_state_[keyidx]
            = keyboard_sr_.State(i) | (keyboard_state_[keyidx] << 1);
    }
    // Gate Input
    gate_in_trig_ = gate_in.Trig();
}

void DaisyField::SetCvOut1(uint16_t val)
{
    seed.dac.WriteValue(DacHandle::Channel::ONE, val);
}

void DaisyField::SetCvOut2(uint16_t val)
{
    seed.dac.WriteValue(DacHandle::Channel::TWO, val);
}

bool DaisyField::KeyboardState(size_t idx) const
{
    return keyboard_state_[idx] == 0x00;
}

bool DaisyField::KeyboardRisingEdge(size_t idx) const
{
    return keyboard_state_[idx] == 0x80;
}

bool DaisyField::KeyboardFallingEdge(size_t idx) const
{
    return keyboard_state_[idx] == 0x7F;
}

float DaisyField::GetKnobValue(size_t idx) const
{
    return knob[idx < KNOB_LAST ? idx : 0].Value();
}

float DaisyField::GetCvValue(size_t idx) const
{
    return cv[idx < CV_LAST ? idx : 0].Value();
}

Switch* DaisyField::GetSwitch(size_t idx)
{
    return &sw[idx < SW_LAST ? idx : 0];
}

AnalogControl* DaisyField::GetKnob(size_t idx)
{
    return &knob[idx < KNOB_LAST ? idx : 0];
}

AnalogControl* DaisyField::GetCv(size_t idx)
{
    return &cv[idx < CV_LAST ? idx : 0];
}


void DaisyField::VegasMode()
{
    uint32_t now;
    now = seed.system.GetNow();
    size_t idx;
    float  key_bright;
    // Cycle all 16 LEDs on keyboard SM in opposite pattern or something
    uint8_t led_grp_a[] = {LED_KEY_A8,
                           LED_KEY_A7,
                           LED_KEY_A6,
                           LED_KEY_A5,
                           LED_KEY_A4,
                           LED_KEY_A3,
                           LED_KEY_A2,
                           LED_KEY_A1};
    uint8_t led_grp_b[] = {LED_KEY_B8,
                           LED_KEY_B7,
                           LED_KEY_B6,
                           LED_KEY_B5,
                           LED_KEY_B4,
                           LED_KEY_B3,
                           LED_KEY_B2,
                           LED_KEY_B1};
    uint8_t led_grp_c[] = {LED_KNOB_1,
                           LED_KNOB_2,
                           LED_KNOB_3,
                           LED_KNOB_4,
                           LED_KNOB_5,
                           LED_KNOB_6,
                           LED_KNOB_7,
                           LED_KNOB_8};
    if(now - last_led_update_ > 10)
    {
        idx        = (now >> 10) % 8;
        key_bright = (float)(now & 1023) / 1023.0f;
        // Clear
        for(size_t i = 0; i < LED_LAST; i++)
        {
            led_driver.SetLed(i, 0.0f);
        }
        // Knob LEDs dance in order
        led_driver.SetLed(led_grp_a[idx], key_bright);
        led_driver.SetLed(led_grp_b[idx], 1.0f - key_bright);
        led_driver.SetLed(led_grp_c[idx], key_bright);
        // OLED moves a bar across the screen
        uint32_t bar_x = (now >> 4) % display.Width();
        display.Fill(false);
        for(size_t i = 0; i < display.Height(); i++)
        {
            display.DrawPixel(bar_x, i, true);
        }

        display.Update();
        led_driver.SwapBuffersAndTransmit();
    }
}