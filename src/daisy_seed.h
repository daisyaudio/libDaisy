#pragma once
#ifndef DSY_SEED_H
#define DSY_SEED_H

#include "daisy.h"

//Uncomment this out if you're still using the rev2 hardware
//#define SEED_REV2

namespace daisy
{
/**
   @brief This is the higher-level interface for the Daisy board. \n
    All basic peripheral configuration/initialization is setup here. \n

   @ingroup boards
*/
class DaisySeed
{
  public:
    DaisySeed() {}
    ~DaisySeed() {}

    /** This function used to provide a pre-initialization configuraiton
     *  it has since been deprecated, and does nothing.
     */
    void Configure();

    /**
    Initializes the Daisy Seed and the following peripherals:
    SDRAM, QSPI, 24-bit 48kHz Audio via AK4556, Internal USB,
    as well as the built-in LED and Testpoint.

    ADCs, DACs, and other special peripherals (such as I2C, SPI, etc.)
    can be initialized using their specific initializers within libdaisy
    for a specific application.
    */
    void Init(bool boost = false);

    /**
    Deinitializes all peripherals automatically handled by `Init`.
    */
    void DeInit();

    /**
    Wait some ms before going on.
    \param del Delay time in ms.
    */
    void DelayMs(size_t del);

    /**
    Returns the gpio_pin corresponding to the index 0-31.
    For the given GPIO on the Daisy Seed (labeled 1-32 in docs).
    */
    static Pin GetPin(uint8_t pin_idx);

    /** Begins the audio for the seeds builtin audio.
    the specified callback will get called whenever
    new data is ready to be prepared.
    */
    void StartAudio(AudioHandle::InterleavingAudioCallback cb);

    /** Begins the audio for the seeds builtin audio.
    the specified callback will get called whenever
    new data is ready to be prepared.
    This will use the newer non-interleaved callback.
    */
    void StartAudio(AudioHandle::AudioCallback cb);

    /** Changes to a new interleaved callback
     */
    void ChangeAudioCallback(AudioHandle::InterleavingAudioCallback cb);

    /** Changes to a new multichannel callback
     */
    void ChangeAudioCallback(AudioHandle::AudioCallback cb);

    /** Stops the audio if it is running. */
    void StopAudio();

    /** Updates the Audio Sample Rate, and reinitializes.
     ** Audio must be stopped for this to work.
     */
    void SetAudioSampleRate(SaiHandle::Config::SampleRate samplerate);

    /** Returns the audio sample rate in Hz as a floating point number.
     */
    float AudioSampleRate();

    /** Sets the number of samples processed per channel by the audio callback.
     */
    void SetAudioBlockSize(size_t blocksize);

    /** Returns the number of samples per channel in a block of audio. */
    size_t AudioBlockSize();

    /** Returns the rate in Hz that the Audio callback is called */
    float AudioCallbackRate() const;

    /** Returns the SAI Handle for the Daisy Seed
     *  This can be useful when adding a secondary codec,
     *  the result of this function can be passed to the audio reinit
     *  with an SAI2 configuration
     */
    const SaiHandle& AudioSaiHandle() const;

    /** Sets the state of the built in LED
     */
    void SetLed(bool state);

    /** Sets the state of the test point near pin 10
     */
    void SetTestPoint(bool state);

    /** Print formatted debug log message
     */
    template <typename... VA>
    static void Print(const char* format, VA... va)
    {
        Log::Print(format, va...);
    }

    /** Print formatted debug log message with automatic line termination
    */
    template <typename... VA>
    static void PrintLine(const char* format, VA... va)
    {
        Log::PrintLine(format, va...);
    }

    /** Start the logging session. Optionally wait for terminal connection before proceeding.
    */
    static void StartLog(bool wait_for_pc = false)
    {
        Log::StartLog(wait_for_pc);
    }


    // While the library is still in heavy development, most of the
    // configuration handles will remain public.
    QSPIHandle         qspi;
    QSPIHandle::Config qspi_config;
    SdramHandle        sdram_handle; /**< & */
    AudioHandle        audio_handle; /**< & */
    AdcHandle          adc;          /**< & */
    DacHandle          dac;
    UsbHandle          usb_handle; /**< & */
    GPIO               led, testpoint;
    System             system;
    Ak4556             codec;

    /** Set to true before Init() to use the exact PLL3 audio clock
     ** (see System::Config::exact_audio_clock): 8 kHz to 96 kHz run at
     ** their nominal rates, and SAI_192KHZ is not available.
     */
    bool exact_audio_clock = false;

    /** Internal indices for DaisySeed-equivalent devices
     *  This shouldn't have any effect on user-facing code,
     *  and only needs to be checked to properly initialize
     *  the onboard-circuits.
    */
    enum class BoardVersion
    {
        /** Daisy Seed Rev4
         *  This is the original Daisy Seed */
        DAISY_SEED,
        /** Daisy Seed 1.1 (aka Daisy Seed Rev5)
         *  This is a pin-compatible version of the Daisy Seed
         *  that uses the WM8731 codec instead of the AK4430 */
        DAISY_SEED_1_1,
        /** Daisy Seed 2 DFM is a software compatible version of the
         *  original Daisy Seed that has improvements for manufacturing,
         *  as well as an improved audio codec (PCM3060)
         */
        DAISY_SEED_2_DFM,
        /** Daisy Seed 1.2 (aka Daisy Seed Rev7)
         *  Pin-compatible version of the Daisy Seed that uses
         *  the PCM3060 audio codec
         */
        DAISY_SEED_1_2,
        /** Daisy Seed3
         *  Pin-compatible version of the Daisy Seed that uses
         *  the TAC5242, has a USB-C connector, and replaced
         *  the SMT Debug connector with pads for installing a
         *  through-hole connector.
         */
        DAISY_SEED_3,
    };

    /** Returns the BoardVersion detected during intiialization */
    BoardVersion CheckBoardVersion();


  private:
    /** Local shorthand for debug log destination
    */
    using Log = Logger<LOGGER_INTERNAL>;

    void ConfigureQspi();
    void ConfigureAudio();
    void ConfigureAdc();
    void ConfigureDac();
    //void     ConfigureI2c();
    float callback_rate_;

    SaiHandle sai_1_handle_;
};

/** seed namespace contains pinout constants for addressing
 * the pins on the Daisy Seed SOM.
 */
namespace seed
{
    /** Constant Pinout consts */
    constexpr Pin D0  = Pin(PORTB, 12);
    constexpr Pin D1  = Pin(PORTC, 11);
    constexpr Pin D2  = Pin(PORTC, 10);
    constexpr Pin D3  = Pin(PORTC, 9);
    constexpr Pin D4  = Pin(PORTC, 8);
    constexpr Pin D5  = Pin(PORTD, 2);
    constexpr Pin D6  = Pin(PORTC, 12);
    constexpr Pin D7  = Pin(PORTG, 10);
    constexpr Pin D8  = Pin(PORTG, 11);
    constexpr Pin D9  = Pin(PORTB, 4);
    constexpr Pin D10 = Pin(PORTB, 5);
    constexpr Pin D11 = Pin(PORTB, 8);
    constexpr Pin D12 = Pin(PORTB, 9);
    constexpr Pin D13 = Pin(PORTB, 6);
    constexpr Pin D14 = Pin(PORTB, 7);
    constexpr Pin D15 = Pin(PORTC, 0);
    constexpr Pin D16 = Pin(PORTA, 3);
    constexpr Pin D17 = Pin(PORTB, 1);
    constexpr Pin D18 = Pin(PORTA, 7);
    constexpr Pin D19 = Pin(PORTA, 6);
    constexpr Pin D20 = Pin(PORTC, 1);
    constexpr Pin D21 = Pin(PORTC, 4);
    constexpr Pin D22 = Pin(PORTA, 5);
    constexpr Pin D23 = Pin(PORTA, 4);
    constexpr Pin D24 = Pin(PORTA, 1);
    constexpr Pin D25 = Pin(PORTA, 0);
    constexpr Pin D26 = Pin(PORTD, 11);
    constexpr Pin D27 = Pin(PORTG, 9);
    constexpr Pin D28 = Pin(PORTA, 2);
    constexpr Pin D29 = Pin(PORTB, 14);
    constexpr Pin D30 = Pin(PORTB, 15);

    /** Analog pins share same pins as digital pins */
    constexpr Pin A0  = D15;
    constexpr Pin A1  = D16;
    constexpr Pin A2  = D17;
    constexpr Pin A3  = D18;
    constexpr Pin A4  = D19;
    constexpr Pin A5  = D20;
    constexpr Pin A6  = D21;
    constexpr Pin A7  = D22;
    constexpr Pin A8  = D23;
    constexpr Pin A9  = D24;
    constexpr Pin A10 = D25;
    constexpr Pin A11 = D28;

    /** Pins unique to Daisy Seed 2 DFM */
    constexpr Pin D31 = Pin(PORTC, 2);
    constexpr Pin D32 = Pin(PORTC, 3);

    /** Analog Pin alias */
    constexpr Pin A12 = D31;
    constexpr Pin A13 = D32;
} // namespace seed

} // namespace daisy

#endif
