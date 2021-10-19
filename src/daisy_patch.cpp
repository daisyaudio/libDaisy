#include "daisy_patch.h"
#include "dev/codec_ak4556.h"

using namespace daisy;

// Hardware Definitions
#define PIN_ENC_CLICK 0
#define PIN_ENC_B 11
#define PIN_ENC_A 12
#define PIN_OLED_DC 9
#define PIN_OLED_RESET 30
#define PIN_MIDI_OUT 13
#define PIN_MIDI_IN 14
#define PIN_GATE_OUT 17
#define PIN_GATE_IN_1 20
#define PIN_GATE_IN_2 19
#define PIN_SAI_SCK_A 28
#define PIN_SAI2_FS_A 27
#define PIN_SAI2_SD_A 26
#define PIN_SAI2_SD_B 25
#define PIN_SAI2_MCLK 24

#define PIN_AK4556_RESET 29

#define PIN_CTRL_1 15
#define PIN_CTRL_2 16
#define PIN_CTRL_3 21
#define PIN_CTRL_4 18

void DaisyPatch::Init(bool boost)
{
    // Configure Seed first
    seed.Configure();
    seed.Init(boost);
    InitAudio();
    InitDisplay();
    InitCvOutputs();
    InitEncoder();
    InitGates();
    InitDisplay();
    InitMidi();
    InitControls();
    // Set Screen update vars
    screen_update_period_ = 17; // roughly 60Hz
    screen_update_last_   = seed.system.GetNow();
}

void DaisyPatch::DelayMs(size_t del)
{
    seed.DelayMs(del);
}

void DaisyPatch::SetHidUpdateRates()
{
    for(size_t i = 0; i < CTRL_LAST; i++)
    {
        controls[i].SetSampleRate(AudioCallbackRate());
    }
}

void DaisyPatch::StartAudio(AudioHandle::AudioCallback cb)
{
    seed.StartAudio(cb);
}

void DaisyPatch::ChangeAudioCallback(AudioHandle::AudioCallback cb)
{
    seed.ChangeAudioCallback(cb);
}

void DaisyPatch::StopAudio()
{
    seed.StopAudio();
}

void DaisyPatch::SetAudioSampleRate(SaiHandle::Config::SampleRate samplerate)
{
    seed.SetAudioSampleRate(samplerate);
    SetHidUpdateRates();
}

float DaisyPatch::AudioSampleRate()
{
    return seed.AudioSampleRate();
}

void DaisyPatch::SetAudioBlockSize(size_t size)
{
    seed.SetAudioBlockSize(size);
    SetHidUpdateRates();
}

size_t DaisyPatch::AudioBlockSize()
{
    return seed.AudioBlockSize();
}

float DaisyPatch::AudioCallbackRate()
{
    return seed.AudioCallbackRate();
}

void DaisyPatch::StartAdc()
{
    seed.adc.Start();
}

/** Stops Transfering data from the ADC */
void DaisyPatch::StopAdc()
{
    seed.adc.Stop();
}


void DaisyPatch::ProcessAnalogControls()
{
    for(size_t i = 0; i < CTRL_LAST; i++)
    {
        controls[i].Process();
    }
}
float DaisyPatch::GetKnobValue(Ctrl k)
{
    return (controls[k].Value());
}

void DaisyPatch::ProcessDigitalControls()
{
    encoder.Debounce();
}

// This will render the display with the controls as vertical bars
void DaisyPatch::DisplayControls(bool invert)
{
    bool on, off;
    on  = invert ? false : true;
    off = invert ? true : false;
    if(seed.system.GetNow() - screen_update_last_ > screen_update_period_)
    {
        // Graph Knobs
        size_t barwidth, barspacing;
        size_t curx, cury;
        screen_update_last_ = seed.system.GetNow();
        barwidth            = 15;
        barspacing          = 20;
        display.Fill(off);
        // Bars for all four knobs.
        for(size_t i = 0; i < DaisyPatch::CTRL_LAST; i++)
        {
            float  v;
            size_t dest;
            curx = (barspacing * i + 1) + (barwidth * i);
            cury = display.Height();
            v    = GetKnobValue(static_cast<DaisyPatch::Ctrl>(i));
            dest = (v * display.Height());
            for(size_t j = dest; j > 0; j--)
            {
                for(size_t k = 0; k < barwidth; k++)
                {
                    display.DrawPixel(curx + k, cury - j, on);
                }
            }
        }
        display.Update();
    }
}

// Private Function Implementations
// set SAI2 stuff -- run this between seed configure and init
void DaisyPatch::InitAudio()
{
    // Handle Seed Audio as-is and then
    SaiHandle::Config sai_config[2];
    // Internal Codec
    sai_config[0].periph          = SaiHandle::Config::Peripheral::SAI_1;
    sai_config[0].sr              = SaiHandle::Config::SampleRate::SAI_48KHZ;
    sai_config[0].bit_depth       = SaiHandle::Config::BitDepth::SAI_24BIT;
    sai_config[0].a_sync          = SaiHandle::Config::Sync::MASTER;
    sai_config[0].b_sync          = SaiHandle::Config::Sync::SLAVE;
    sai_config[0].a_dir           = SaiHandle::Config::Direction::TRANSMIT;
    sai_config[0].b_dir           = SaiHandle::Config::Direction::RECEIVE;
    sai_config[0].pin_config.fs   = {DSY_GPIOE, 4};
    sai_config[0].pin_config.mclk = {DSY_GPIOE, 2};
    sai_config[0].pin_config.sck  = {DSY_GPIOE, 5};
    sai_config[0].pin_config.sa   = {DSY_GPIOE, 6};
    sai_config[0].pin_config.sb   = {DSY_GPIOE, 3};

    // External Codec
    sai_config[1].periph          = SaiHandle::Config::Peripheral::SAI_2;
    sai_config[1].sr              = SaiHandle::Config::SampleRate::SAI_48KHZ;
    sai_config[1].bit_depth       = SaiHandle::Config::BitDepth::SAI_24BIT;
    sai_config[1].a_sync          = SaiHandle::Config::Sync::SLAVE;
    sai_config[1].b_sync          = SaiHandle::Config::Sync::MASTER;
    sai_config[1].a_dir           = SaiHandle::Config::Direction::TRANSMIT;
    sai_config[1].b_dir           = SaiHandle::Config::Direction::RECEIVE;
    sai_config[1].pin_config.fs   = seed.GetPin(27);
    sai_config[1].pin_config.mclk = seed.GetPin(24);
    sai_config[1].pin_config.sck  = seed.GetPin(28);
    sai_config[1].pin_config.sb   = seed.GetPin(25);
    sai_config[1].pin_config.sa   = seed.GetPin(26);

    SaiHandle sai_handle[2];
    sai_handle[0].Init(sai_config[0]);
    sai_handle[1].Init(sai_config[1]);

    // Reset Pin for AK4556
    // Built-in AK4556 was reset during Seed Init
    dsy_gpio_pin codec_reset_pin = seed.GetPin(PIN_AK4556_RESET);
    Ak4556::Init(codec_reset_pin);

    // Reinit Audio for _both_ codecs...
    AudioHandle::Config cfg;
    cfg.blocksize  = 48;
    cfg.samplerate = SaiHandle::Config::SampleRate::SAI_48KHZ;
    cfg.postgain   = 0.5f;
    seed.audio_handle.Init(cfg, sai_handle[0], sai_handle[1]);
}

void DaisyPatch::InitControls()
{
    AdcChannelConfig cfg[CTRL_LAST];

    // Init ADC channels with Pins
    cfg[CTRL_1].InitSingle(seed.GetPin(PIN_CTRL_1));
    cfg[CTRL_2].InitSingle(seed.GetPin(PIN_CTRL_2));
    cfg[CTRL_3].InitSingle(seed.GetPin(PIN_CTRL_3));
    cfg[CTRL_4].InitSingle(seed.GetPin(PIN_CTRL_4));

    // Initialize ADC
    seed.adc.Init(cfg, CTRL_LAST);

    // Initialize AnalogControls, with flip set to true
    for(size_t i = 0; i < CTRL_LAST; i++)
    {
        controls[i].Init(seed.adc.GetPtr(i), AudioCallbackRate(), true);
    }
}

void DaisyPatch::InitDisplay()
{
    OledDisplay<SSD130x4WireSpi128x64Driver>::Config display_config;

    display_config.driver_config.transport_config.pin_config.dc
        = seed.GetPin(PIN_OLED_DC);
    display_config.driver_config.transport_config.pin_config.reset
        = seed.GetPin(PIN_OLED_RESET);

    display.Init(display_config);
}

void DaisyPatch::InitMidi()
{
    MidiUartHandler::Config midi_config;
    midi.Init(midi_config);
}

void DaisyPatch::InitCvOutputs()
{
    //    dsy_dac_init(&seed.dac_handle, DSY_DAC_CHN_BOTH);
    //    dsy_dac_write(DSY_DAC_CHN1, 0);
    //    dsy_dac_write(DSY_DAC_CHN2, 0);
    DacHandle::Config cfg;
    cfg.bitdepth   = DacHandle::BitDepth::BITS_12;
    cfg.buff_state = DacHandle::BufferState::ENABLED;
    cfg.mode       = DacHandle::Mode::POLLING;
    cfg.chn        = DacHandle::Channel::BOTH;
    seed.dac.Init(cfg);
    seed.dac.WriteValue(DacHandle::Channel::BOTH, 0);
}

void DaisyPatch::InitEncoder()
{
    encoder.Init(seed.GetPin(PIN_ENC_A),
                 seed.GetPin(PIN_ENC_B),
                 seed.GetPin(PIN_ENC_CLICK));
}

void DaisyPatch::InitGates()
{
    // Gate Output
    gate_output.pin  = seed.GetPin(PIN_GATE_OUT);
    gate_output.mode = DSY_GPIO_MODE_OUTPUT_PP;
    gate_output.pull = DSY_GPIO_NOPULL;
    dsy_gpio_init(&gate_output);

    // Gate Inputs
    dsy_gpio_pin pin;
    pin = seed.GetPin(PIN_GATE_IN_1);
    gate_input[GATE_IN_1].Init(&pin);
    pin = seed.GetPin(PIN_GATE_IN_2);
    gate_input[GATE_IN_2].Init(&pin);
}
