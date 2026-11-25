#include "daisy_seed.h"

using namespace daisy;

constexpr GPIOPort SEED_LED_PORT = PORTC;
constexpr uint8_t  SEED_LED_PIN  = 7;

constexpr GPIOPort SEED_TEST_POINT_PORT = PORTG;
constexpr uint8_t  SEED_TEST_POINT_PIN  = 14;

#ifndef SEED_REV2
const Pin seedgpio[33] = {
    // GPIO 1-8
    //{PORTA, 8}, // removed on Rev4
    seed::D0,
    seed::D1,
    seed::D2,
    seed::D3,
    seed::D4,
    seed::D5,
    seed::D6,
    // GPIO 9-16
    seed::D7,
    seed::D8,
    seed::D9,
    seed::D10,
    seed::D11,
    seed::D12,
    seed::D13,
    seed::D14,
    // GPIO 17-24
    seed::D15,
    seed::D16,
    seed::D17,
    seed::D18,
    seed::D19,
    seed::D20,
    seed::D21,
    seed::D22,
    // GPIO 25-31
    seed::D23,
    seed::D24,
    seed::D25,
    seed::D26,
    seed::D27,
    seed::D28,
    seed::D29,
    seed::D30,

    // Seed2DFM exclusive pins
    seed::D31,
    seed::D32,
};
#else
const GPIOPort seed_ports[32] = {
    PORTA, PORTB, PORTC, PORTC, PORTC, PORTC, PORTD, PORTC, PORTG, PORTG, PORTB,
    PORTB, PORTB, PORTB, PORTB, PORTB, PORTC, PORTA, PORTA, PORTB, PORTA, PORTA,
    PORTC, PORTC, PORTA, PORTA, PORTA, PORTD, PORTG, PORTA, PORTB, PORTB,
};

const uint8_t seed_pins[32] = {
    8, 12, 11, 10, 9, 8, 7, 12, 10, 11, 4, 5,  8, 9, 6,  7,
    0, 1,  3,  1,  7, 6, 1, 5,  5,  4,  0, 11, 9, 2, 14, 15,
};

const Pin seedgpio[32] = {
    Pin(seed_ports[0], seed_pins[0]),   Pin(seed_ports[1], seed_pins[1]),
    Pin(seed_ports[2], seed_pins[2]),   Pin(seed_ports[3], seed_pins[3]),
    Pin(seed_ports[4], seed_pins[4]),   Pin(seed_ports[5], seed_pins[5]),
    Pin(seed_ports[6], seed_pins[6]),   Pin(seed_ports[7], seed_pins[7]),
    Pin(seed_ports[8], seed_pins[8]),   Pin(seed_ports[9], seed_pins[9]),
    Pin(seed_ports[10], seed_pins[10]), Pin(seed_ports[11], seed_pins[11]),
    Pin(seed_ports[12], seed_pins[12]), Pin(seed_ports[13], seed_pins[13]),
    Pin(seed_ports[14], seed_pins[14]), Pin(seed_ports[15], seed_pins[15]),
    Pin(seed_ports[16], seed_pins[16]), Pin(seed_ports[17], seed_pins[17]),
    Pin(seed_ports[18], seed_pins[18]), Pin(seed_ports[19], seed_pins[19]),
    Pin(seed_ports[20], seed_pins[20]), Pin(seed_ports[21], seed_pins[21]),
    Pin(seed_ports[22], seed_pins[22]), Pin(seed_ports[23], seed_pins[23]),
    Pin(seed_ports[24], seed_pins[24]), Pin(seed_ports[25], seed_pins[25]),
    Pin(seed_ports[26], seed_pins[26]), Pin(seed_ports[27], seed_pins[27]),
    Pin(seed_ports[28], seed_pins[28]), Pin(seed_ports[29], seed_pins[29]),
    Pin(seed_ports[30], seed_pins[30]), Pin(seed_ports[31], seed_pins[31]),
};
#endif

// Public Initialization

/** Vestigial function body for old function
 *  This is no longer in use.
 */
void DaisySeed::Configure() {}

void DaisySeed::Init(bool boost)
{
    //dsy_system_init();
    System::Config syscfg;
    boost ? syscfg.Boost() : syscfg.Defaults();
    syscfg.exact_audio_clock = exact_audio_clock;

    ConfigureQspi();
    // Configure the built-in GPIOs.
    GPIO::Config &led_config       = led.GetConfig();
    GPIO::Config &testpoint_config = testpoint.GetConfig();

    led_config.pin  = Pin(SEED_LED_PORT, SEED_LED_PIN);
    led_config.mode = GPIO::Mode::OUTPUT;

    testpoint_config.pin  = Pin(SEED_TEST_POINT_PORT, SEED_TEST_POINT_PIN);
    testpoint_config.mode = GPIO::Mode::OUTPUT;

    auto memory       = System::GetProgramMemoryRegion();
    auto boot_version = System::GetBootloaderVersion();

    if(boot_version == System::BootInfo::Version::LT_v6_0
       && memory != System::MemoryRegion::INTERNAL_FLASH)
    {
        syscfg.skip_clocks = true;
    }

    system.Init(syscfg);

    if(memory != System::MemoryRegion::QSPI)
        qspi.Init(qspi_config);

    if(boot_version != System::BootInfo::Version::LT_v6_0
       || (boot_version == System::BootInfo::Version::LT_v6_0
           && memory == System::MemoryRegion::INTERNAL_FLASH))
    {
        led.Init(led.GetConfig());
        testpoint.Init(testpoint.GetConfig());
        sdram_handle.Init();
    }

    ConfigureAudio();

    callback_rate_ = AudioSampleRate() / AudioBlockSize();
    // Due to the added 16kB+ of flash usage,
    // and the fact that certain breakouts use
    // both; USB won't be initialized by the
    // SEED file.
    //usb_handle.Init(UsbHandle::FS_INTERNAL);
}

void DaisySeed::DeInit()
{
    // This is intended to be used by the bootloader, but
    // we don't want to reinitialize pretty much anything in the
    // target application, so...

    system.DeInit();
}

Pin DaisySeed::GetPin(uint8_t pin_idx)
{
    Pin p;
    pin_idx = pin_idx < sizeof(seedgpio) / sizeof(seedgpio[0]) ? pin_idx : 0;

#ifndef SEED_REV2
    p = seedgpio[pin_idx];
#else
    p = {seed_ports[pin_idx], seed_pins[pin_idx]};
#endif

    return p;
}

void DaisySeed::DelayMs(size_t del)
{
    system.Delay(del);
}

void DaisySeed::StartAudio(AudioHandle::InterleavingAudioCallback cb)
{
    audio_handle.Start(cb);
}

void DaisySeed::StartAudio(AudioHandle::AudioCallback cb)
{
    audio_handle.Start(cb);
}

void DaisySeed::ChangeAudioCallback(AudioHandle::InterleavingAudioCallback cb)
{
    audio_handle.ChangeCallback(cb);
}

void DaisySeed::ChangeAudioCallback(AudioHandle::AudioCallback cb)
{
    audio_handle.ChangeCallback(cb);
}

void DaisySeed::StopAudio()
{
    audio_handle.Stop();
}

void DaisySeed::SetAudioSampleRate(SaiHandle::Config::SampleRate samplerate)
{
    audio_handle.SetSampleRate(samplerate);
    callback_rate_ = AudioSampleRate() / AudioBlockSize();
}

float DaisySeed::AudioSampleRate()
{
    return audio_handle.GetSampleRate();
}

void DaisySeed::SetAudioBlockSize(size_t blocksize)
{
    audio_handle.SetBlockSize(blocksize);
    callback_rate_ = AudioSampleRate() / AudioBlockSize();
}

size_t DaisySeed::AudioBlockSize()
{
    return audio_handle.GetConfig().blocksize;
}

float DaisySeed::AudioCallbackRate() const
{
    return callback_rate_;
}

void DaisySeed::SetLed(bool state)
{
    led.Write(state);
}

void DaisySeed::SetTestPoint(bool state)
{
    testpoint.Write(state);
}

const SaiHandle &DaisySeed::AudioSaiHandle() const
{
    return sai_1_handle_;
}

// Private Implementation

void DaisySeed::ConfigureQspi()
{
    qspi_config.device = QSPIHandle::Config::Device::IS25LP064A;
    qspi_config.mode   = QSPIHandle::Config::Mode::MEMORY_MAPPED;

    qspi_config.pin_config.io0 = Pin(PORTF, 8);
    qspi_config.pin_config.io1 = Pin(PORTF, 9);
    qspi_config.pin_config.io2 = Pin(PORTF, 7);
    qspi_config.pin_config.io3 = Pin(PORTF, 6);
    qspi_config.pin_config.clk = Pin(PORTF, 10);
    qspi_config.pin_config.ncs = Pin(PORTG, 6);
}
void DaisySeed::ConfigureAudio()
{
    // SAI1 -- Peripheral
    // Configure
    SaiHandle::Config sai_config;
    sai_config.periph          = SaiHandle::Config::Peripheral::SAI_1;
    sai_config.sr              = SaiHandle::Config::SampleRate::SAI_48KHZ;
    sai_config.bit_depth       = SaiHandle::Config::BitDepth::SAI_24BIT;
    sai_config.a_sync          = SaiHandle::Config::Sync::MASTER;
    sai_config.b_sync          = SaiHandle::Config::Sync::SLAVE;
    sai_config.pin_config.fs   = Pin(PORTE, 4);
    sai_config.pin_config.mclk = Pin(PORTE, 2);
    sai_config.pin_config.sck  = Pin(PORTE, 5);

    // Device-based Init
    switch(CheckBoardVersion())
    {
        case BoardVersion::DAISY_SEED_1_1:
        {
            // Data Line Directions
            sai_config.a_dir         = SaiHandle::Config::Direction::RECEIVE;
            sai_config.pin_config.sa = Pin(PORTE, 6);
            sai_config.b_dir         = SaiHandle::Config::Direction::TRANSMIT;
            sai_config.pin_config.sb = Pin(PORTE, 3);
            I2CHandle::Config i2c_config;
            i2c_config.mode           = I2CHandle::Config::Mode::I2C_MASTER;
            i2c_config.periph         = I2CHandle::Config::Peripheral::I2C_2;
            i2c_config.speed          = I2CHandle::Config::Speed::I2C_400KHZ;
            i2c_config.pin_config.scl = Pin(PORTH, 4);
            i2c_config.pin_config.sda = Pin(PORTB, 11);
            I2CHandle i2c_handle;
            i2c_handle.Init(i2c_config);
            Wm8731::Config codec_cfg;
            codec_cfg.Defaults();
            Wm8731 codec;
            codec.Init(codec_cfg, i2c_handle);
        }
        break;
        case BoardVersion::DAISY_SEED_2_DFM:
        {
            // Data Line Directions
            sai_config.a_dir         = SaiHandle::Config::Direction::TRANSMIT;
            sai_config.pin_config.sa = Pin(PORTE, 6);
            sai_config.b_dir         = SaiHandle::Config::Direction::RECEIVE;
            sai_config.pin_config.sb = Pin(PORTE, 3);
            /** PCM3060 disable deemphasis pin */
            GPIO deemp;
            deemp.Init(Pin(PORTB, 11), GPIO::Mode::OUTPUT);
            deemp.Write(0);
        }
        break;
        case BoardVersion::DAISY_SEED:
        default:
        {
            // Same configuration used for Seed Rev4, Seed Rev7 (1.2), and Seed3
            // Data Line Directions
            sai_config.a_dir         = SaiHandle::Config::Direction::TRANSMIT;
            sai_config.pin_config.sa = Pin(PORTE, 6);
            sai_config.b_dir         = SaiHandle::Config::Direction::RECEIVE;
            sai_config.pin_config.sb = Pin(PORTE, 3);

            constexpr Pin codec_reset_pin = Pin(PORTB, 11);
            codec.Init(codec_reset_pin);
        }
        break;
    }

    // Then Initialize
    sai_1_handle_.Init(sai_config);

    // Audio
    AudioHandle::Config audio_config;
    audio_config.blocksize  = 48;
    audio_config.samplerate = SaiHandle::Config::SampleRate::SAI_48KHZ;
    audio_config.postgain   = 1.f;
    audio_handle.Init(audio_config, sai_1_handle_);
}
void DaisySeed::ConfigureDac()
{
    // This would be the equivalent initialization as previously existed.
    // However, not all platforms have the DAC, and many use those pins
    // for other things.
    //    DacHandle::Config cfg;
    //    cfg.bitdepth   = DacHandle::Config::BitDepth::BITS_12;
    //    cfg.buff_state = DacHandle::Config::BufferState::ENABLED;
    //    cfg.mode       = DacHandle::Config::Mode::POLLING;
    //    cfg.chn        = DacHandle::Config::Channel::BOTH;
    //    dac.Init(cfg);
}

DaisySeed::BoardVersion DaisySeed::CheckBoardVersion()
{
    /** Version Checks:
     *  * Fall through is Daisy Seed v1 (aka Daisy Seed rev4)
     *  * PD3 tied to GND is Daisy Seed 1.1 (aka Daisy Seed rev5)
     *  * PD4 tied to GND is Daisy Seed2 DFM
     *  * PD5 tied to GND is Daisy Seed 1.2 (aka Daisy Seed rev7)
     *  * PH6 tied to GND is Daisy Seed 3
     */

    /** Initialize GPIO */
    GPIO s2dfm_gpio, seed_1_1_gpio, seed_1_2_gpio, seed_3_gpio;
    Pin  seed_1_1_pin(PORTD, 3);
    Pin  s2dfm_pin(PORTD, 4);
    Pin  seed_1_2_pin(PORTD, 5);
    Pin  seed_3_pin(PORTH, 6);
    seed_1_1_gpio.Init(seed_1_1_pin, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    s2dfm_gpio.Init(s2dfm_pin, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    seed_1_2_gpio.Init(seed_1_2_pin, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    seed_3_gpio.Init(seed_3_pin, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);

    if(!seed_1_1_gpio.Read())
        return BoardVersion::DAISY_SEED_1_1;
    else if(!s2dfm_gpio.Read())
        return BoardVersion::DAISY_SEED_2_DFM;
    else if(!seed_1_2_gpio.Read())
        return BoardVersion::DAISY_SEED_1_2;
    else if(!seed_3_gpio.Read())
        return BoardVersion::DAISY_SEED_3;
    else
        return BoardVersion::DAISY_SEED;
}
