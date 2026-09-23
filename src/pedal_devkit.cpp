#include "pedal_devkit.h"

namespace daisy::pedal_devkit
{
void Hardware::Init()
{
    seed.Init(true);
    sw1.Init(seed::D7, seed::D8); //< wired opposite from expected..
    sw2.Init(seed::D9);
    sw3.Init(seed::D22);
    sw4.Init(seed::D0);
    sw5.Init(seed::D28);
    fsw_1.Init(seed::D10);
    fsw_2.Init(seed::D23);
    sd_detect.Init(seed::D11);

    AdcChannelConfig adc_cfg[kNumPots + 1];
    auto             conversion_speed
        = AdcChannelConfig::ConversionSpeed::SPEED_810CYCLES_5;
    // Pots
    adc_cfg[0].InitSingle(seed::D15, conversion_speed);
    adc_cfg[1].InitSingle(seed::D16, conversion_speed);
    adc_cfg[2].InitSingle(seed::D17, conversion_speed);
    adc_cfg[3].InitSingle(seed::D18, conversion_speed);
    adc_cfg[4].InitSingle(seed::D19, conversion_speed);
    adc_cfg[5].InitSingle(seed::D20, conversion_speed);
    // Expression
    adc_cfg[6].InitSingle(seed::D21, conversion_speed);
    seed.adc.Init(adc_cfg, kNumPots + 1);

    for(size_t i = 0; i < kNumPots; i++)
        pot[i].Init(seed.adc.GetPtr(i), 1000.f);
    expression.Init(seed.adc.GetPtr(kNumPots), 1000.f);

    led1.Init(seed::D24, false);
    led2.Init(seed::D25, seed::D27, seed::D26, false);

    MidiUartHandler::Config midi_config;
    midi_config.transport_config.periph
        = UartHandler::Config::Peripheral::USART_1;
    midi_config.transport_config.rx = seed::D14;
    midi_config.transport_config.tx = seed::D13;
    midi.Init(midi_config);

    seed.adc.Start();
}

void Hardware::UpdateAllControls()
{
    for(auto &p : pot)
        p.Process();
    expression.Process();

    sw2.Debounce();
    sw3.Debounce();
    sw4.Debounce();
    sw5.Debounce();
    fsw_1.Debounce();
    fsw_2.Debounce();
    sd_detect.Debounce();
}

void Hardware::PrintAllControls()
{
    using Log           = Logger<LOGGER_INTERNAL>;
    const char *sw1_str = sw1.Read() == Switch3::POS_LEFT
                              ? "L"
                              : sw1.Read() == Switch3::POS_RIGHT ? "R" : "C";

    Log::PrintLine("\033[2J\033[H]"); //< Clear the terminal
    Log::PrintLine("Pedal DevKit Controls:");
    Log::PrintLine("----Switches----");
    Log::PrintLine("\tSW1: %s\tSW2: %s\tSW3: %s",
                   sw1_str,
                   sw2.Pressed() ? "R" : "L",
                   sw3.Pressed() ? "R" : "L");
    Log::PrintLine("\tSW4: %s\tSW5: %s",
                   sw4.Pressed() ? "X" : "O",
                   sw5.Pressed() ? "X" : "O");
    Log::PrintLine("\tFSW1: %s\tFSW2: %s",
                   fsw_1.Pressed() ? "X" : "O",
                   fsw_2.Pressed() ? "X" : "O");
    Log::PrintLine("\tSD Detect: %s",
                   sd_detect.Pressed() ? "SD Card Detected" : "No SD Card");
    Log::PrintLine("------ADCs------");
    Log::PrintLine("\tVR1: " FLT_FMT3 "\tVR2: " FLT_FMT3 "\tVR3: " FLT_FMT3,
                   FLT_VAR3(pot[0].Value()),
                   FLT_VAR3(pot[1].Value()),
                   FLT_VAR3(pot[2].Value()));
    Log::PrintLine("\tVR4: " FLT_FMT3 "\tVR5: " FLT_FMT3 "\tVR6: " FLT_FMT3,
                   FLT_VAR3(pot[3].Value()),
                   FLT_VAR3(pot[4].Value()),
                   FLT_VAR3(pot[5].Value()));
    Log::PrintLine("\tExpression: " FLT_FMT3, FLT_VAR3(expression.Value()));
}

} // namespace daisy::pedal_devkit