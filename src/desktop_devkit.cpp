#include "desktop_devkit.h"

namespace daisy::desktop_devkit
{
static constexpr I2CHandle::Config led_i2c_config
    = {I2CHandle::Config::Peripheral::I2C_1,
       {seed::D11, seed::D12},
       I2CHandle::Config::Speed::I2C_1MHZ};

static Hardware::LedDriver::DmaBuffer DMA_BUFFER_MEM_SECTION led_dma_buffer_a,
    led_dma_buffer_b;

void Hardware::Init()
{
    // Initialize stuff
    seed.Init(true);

    /** Switches */
    tog_sw17.Init(daisy::seed::D19);
    tog_sw18.Init(daisy::seed::D28, daisy::seed::D27);

    sd_detect.Init(daisy::seed::D7);

    // Initialize jack input detection with opposite polarity since the
    // switching connection of the jack is held to GND while it is unplugged.
    left_detect.Init(daisy::seed::D16,
                     0.f,
                     Switch::Type::TYPE_MOMENTARY,
                     Switch::Polarity::POLARITY_NORMAL);
    right_detect.Init(daisy::seed::D17,
                      0.f,
                      Switch::Type::TYPE_MOMENTARY,
                      Switch::Polarity::POLARITY_NORMAL);

    ButtonSr::Config sr_cfg;
    sr_cfg.clk     = daisy::seed::D8;
    sr_cfg.latch   = daisy::seed::D9;
    sr_cfg.data[0] = daisy::seed::D10;
    button_sr.Init(sr_cfg);
    for(auto &b : button)
        b.Init(true);

    /** UART MIDI I/O */
    daisy::MidiUartHandler::Config midi_config;
    midi_config.transport_config.periph
        = daisy::UartHandler::Config::Peripheral::USART_1;
    midi_config.transport_config.rx = daisy::seed::D14;
    midi_config.transport_config.tx = daisy::seed::D13;
    midi.Init(midi_config);

    /** ADC Inputs */
    daisy::AdcChannelConfig adc_cfg[3];
    auto                    conversion_speed
        = daisy::AdcChannelConfig::ConversionSpeed::SPEED_810CYCLES_5;
    adc_cfg[0].InitMux(daisy::seed::D15,
                       8,
                       daisy::seed::D24,
                       daisy::seed::D25,
                       daisy::seed::D26,
                       conversion_speed);
    adc_cfg[1].InitSingle(daisy::seed::D20, conversion_speed);
    adc_cfg[2].InitSingle(daisy::seed::D21, conversion_speed);
    seed.adc.Init(adc_cfg, 3);

    for(int i = 0; i < 8; i++)
        pot[i].Init(seed.adc.GetMuxPtr(0, i), 1000.f);
    for(int i = 0; i < 2; i++)
        cv[i].InitBipolarCv(seed.adc.GetPtr(i + 1), 1000.f);

    /** Leds */
    daisy::I2CHandle i2c;
    i2c.Init(led_i2c_config);
    led_driver.Init(i2c, {0x00}, led_dma_buffer_a, led_dma_buffer_b);
    ClearLeds();
    UpdateLeds();

    /** Gate Ins */
    gate_in[0].Init(daisy::seed::D0);
    gate_in[1].Init(daisy::seed::D18);

    /** DAC/Gate Output */
    daisy::DacHandle::Config dac_cfg;
    dac_cfg.bitdepth   = daisy::DacHandle::BitDepth::BITS_12;
    dac_cfg.buff_state = daisy::DacHandle::BufferState::ENABLED;
    dac_cfg.chn        = daisy::DacHandle::Channel::BOTH;
    dac_cfg.mode       = daisy::DacHandle::Mode::POLLING;
    seed.dac.Init(dac_cfg);
    // Alternatively: DMA mode
    // dac_cfg.mode = daisy::DacHandle::Mode::DMA;
    // dac_cfg.target_samplerate = 12000;

    seed.adc.Start();
}

void Hardware::UpdateAllControls()
{
    button_sr.Update();
    for(int i = 0; i < 16; i++)
    {
        int sr_idx = 15 - i;
        button[i].Debounce(button_sr.State(sr_idx));
    };
    for(auto &p : pot)
        p.Process();
    for(auto &c : cv)
        c.Process();
    tog_sw17.Debounce();
    sd_detect.Debounce();
    left_detect.Debounce();
    right_detect.Debounce();
}

void Hardware::SetDacOut(DacHandle::Channel channel, uint16_t code)
{
    seed.dac.WriteValue(channel, code);
}

void Hardware::ClearLeds()
{
    led_driver.SetAllTo(1.f);
}

void Hardware::UpdateLeds()
{
    led_driver.SwapBuffersAndTransmit();
}

void Hardware::SetLed(int idx, float brightness)
{
    led_driver.SetLed(idx, 1.f - brightness);
}

void Hardware::StartLowPriorityCallback(TimerHandle::PeriodElapsedCallback cb,
                                        uint32_t target_freq,
                                        void *   data)
{
    TimerHandle::Config timcfg;
    timcfg.periph        = daisy::TimerHandle::Config::Peripheral::TIM_5;
    timcfg.dir           = daisy::TimerHandle::Config::CounterDir::UP;
    auto tim_base_freq   = daisy::System::GetPClk2Freq();
    auto tim_target_freq = target_freq;
    auto tim_period      = tim_base_freq / tim_target_freq;
    timcfg.period        = tim_period;
    timcfg.enable_irq    = true;
    tim5_handle.Init(timcfg);
    tim5_handle.SetCallback(cb, data);
    tim5_handle.Start();
}

/** Stops an ongoing low-priority callback */
void Hardware::StopLowPriorityCallback()
{
    tim5_handle.Stop();
}

} // namespace daisy::desktop_devkit