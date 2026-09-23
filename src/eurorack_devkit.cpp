#include "eurorack_devkit.h"

namespace daisy::eurorack_devkit {

void Hardware::Init() {
  seed.Init(true);
  sw1.Init(seed::D0);
  sw2.Init(seed::D20);
  sw3.Init(seed::D7, seed::D27); //< wired opposite from expected..
  sw4.Init(seed::D12);

  AdcChannelConfig adc_config[5];
  auto conversion_speed = AdcChannelConfig::ConversionSpeed::SPEED_810CYCLES_5;
  adc_config[0].InitMux(seed::D15, 8, seed::D24, seed::D25, seed::D26,
                        conversion_speed);
  adc_config[1].InitSingle(seed::D18, conversion_speed);
  adc_config[2].InitSingle(seed::D17, conversion_speed);
  adc_config[3].InitSingle(seed::D16, conversion_speed);
  adc_config[4].InitSingle(seed::D19, conversion_speed);
  seed.adc.Init(adc_config, 5);
  for (size_t i = 0; i < kNumPots; i++) {
    pot[i].Init(seed.adc.GetMuxPtr(0, i), 1000.f);
  }
  for (size_t i = 0; i < kNumCVs; i++) {
    cv[i].InitBipolarCv(seed.adc.GetPtr(i + 1), 1000.f);
  }

  led1.Init(seed::D9, seed::D28, seed::D8, false);
  led2.Init(seed::D11, false);

  gate_in1.Init(seed::D10);
  gate_in2.Init(seed::D21);

  // DAC init
  // Maybe this becomes separate and/or optional to make it
  // easier to set up these outputs as GPIO if the user wants?
  DacHandle::Config dac_config;
  dac_config.bitdepth = DacHandle::BitDepth::BITS_12;
  dac_config.buff_state = DacHandle::BufferState::DISABLED;
  dac_config.mode = DacHandle::Mode::POLLING;
  dac_config.chn = DacHandle::Channel::BOTH;
  seed.dac.Init(dac_config);

  MidiUartHandler::Config midi_config;
  midi_config.transport_config.periph =
      UartHandler::Config::Peripheral::USART_1;
  midi_config.transport_config.rx = seed::D14;
  midi_config.transport_config.tx = seed::D13;
  midi.Init(midi_config);

  seed.adc.Start();
}

void Hardware::UpdateAllControls() {
  for (auto &p : pot)
    p.Process();
  for (auto &c : cv)
    c.Process();
  sw1.Debounce();
  sw2.Debounce();
  sw4.Debounce();
}

void Hardware::SetDacOut(DacHandle::Channel channel, uint16_t code) {
  seed.dac.WriteValue(channel, code);
}

void Hardware::PrintAllControls() {
  // TODO: consider updating this to write everything to one buffer, and then
  // transmit to avoid the screen flicker of everything writing in separate
  // transactions.
  using Log = Logger<LOGGER_INTERNAL>;
  const char *sw3_str = sw3.Read() == Switch3::POS_LEFT
                            ? "L"
                            : sw3.Read() == Switch3::POS_RIGHT ? "R" : "C";

  Log::PrintLine("\033[2J\033[H"); //< Clear the terminal
  Log::PrintLine("Pedal DevKit Controls:");
  Log::PrintLine("----Switches----");
  Log::PrintLine("\tSW1: %s\tSW2: %s\tSW3: %s", sw1.Pressed() ? "X" : "O",
                 sw2.Pressed() ? "X" : "O", sw3_str, sw4.Pressed() ? "R" : "L");
  Log::PrintLine("----Gate Ins----");
  Log::PrintLine("\tGate In 1: %s\tGate In 2: %s", gate_in1.State() ? "X" : "O",
                 gate_in2.State() ? "X" : "O");
  Log::PrintLine("------ADCs------");
  Log::PrintLine("\tVR1: " FLT_FMT3 "\tVR2: " FLT_FMT3 "\tVR3: " FLT_FMT3
                 "\tVR4: " FLT_FMT3,
                 FLT_VAR3(pot[0].Value()), FLT_VAR3(pot[1].Value()),
                 FLT_VAR3(pot[2].Value()), FLT_VAR3(pot[3].Value()));
  Log::PrintLine("\tVR5: " FLT_FMT3 "\tVR6: " FLT_FMT3 "\tVR7: " FLT_FMT3
                 "\tVR8: " FLT_FMT3,
                 FLT_VAR3(pot[4].Value()), FLT_VAR3(pot[5].Value()),
                 FLT_VAR3(pot[6].Value()), FLT_VAR3(pot[7].Value()));
  Log::PrintLine("\tCV 1: " FLT_FMT3 "\tCV 2: " FLT_FMT3 "\tCV 3: " FLT_FMT3
                 "\tCV 4: " FLT_FMT3,
                 FLT_VAR3(cv[0].Value()), FLT_VAR3(cv[1].Value()),
                 FLT_VAR3(cv[2].Value()), FLT_VAR3(cv[3].Value()));
}

} // namespace daisy::eurorack_devkit