#pragma once
#ifndef SA_LED_DRIVER_H
#define SA_LED_DRIVER_H /**< & */

#ifdef __cplusplus

#include <stdint.h>
#include <algorithm>
#include "per/i2c.h"
#include "per/gpio.h"

namespace daisy
{
/** LED driver for one or multiple PCA9685 12bit PWM chips connected to
 * a single I2C peripheral.
 * It includes gamma correction from 8bit brightness values but it 
 * can also be supplied with raw 12bit values.
 * This driver uses two buffers - one for drawing, one for transmitting.
 * Multiple LedDriverPca9685 instances can be used at the same time.
 * \param numDrivers    The number of PCA9685 driver attached to the I2C
 *                      peripheral.
 * \param persistentBufferContents If set to true, the current draw buffer 
 *                      contents will be copied to the next draw buffer 
 *                      during SwapBuffersAndTransmit(). Use this, if you plan
 *                      to write single leds at a time. 
 *                      If you will alway update all leds before calling 
 *                      SwapBuffersAndTransmit(), you can set this to false
 *                      and safe some cycles.
 * 
 *  @ingroup device
 */
template <int numDrivers, bool persistentBufferContents = true>
class LedDriverPca9685
{
  public:
    /** Buffer Type for a single PCA9685 driver chip. */
    struct __attribute__((packed)) PCA9685TransmitBuffer
    {
        /** register address */
        uint8_t registerAddr = PCA9685_LED0;
        struct __attribute__((packed))
        {
            /** cycle at which to switch on the led */
            uint16_t on;
            /** cycle at which to switch off the led */
            uint16_t off;
        } leds[16];
        /** full size in bytes */
        static constexpr uint16_t size = 16 * 4 + 1;
    };
    /** Buffer type for the entire DMA buffer. */
    using DmaBuffer = PCA9685TransmitBuffer[numDrivers];

    /** Initialises the driver. 
     * \param i2c           The I2C peripheral to use.
     * \param addresses     An array of addresses for each of the driver chips.
     * \param dma_buffer_a  The first buffer for the DMA. This must be placed in 
     *                      D2 memory by adding the DMA_BUFFER_MEM_SECTION attribute
     *                      like this: `LedDriverPca9685<2>::DmaBuffer DMA_BUFFER_MEM_SECTION bufferA;`
     * \param dma_buffer_b  The second buffer for the DMA. This must be placed in 
     *                      D2 memory by adding the DMA_BUFFER_MEM_SECTION attribute
     *                      like this: `LedDriverPca9685<2>::DmaBuffer DMA_BUFFER_MEM_SECTION bufferB;`
     * \param oe_pin        If the output enable pin is used, supply its configuration here.
     *                      It will automatically be pulled low by the driver.
    */
    void Init(I2CHandle i2c,
              const uint8_t (&addresses)[numDrivers],
              DmaBuffer dma_buffer_a,
              DmaBuffer dma_buffer_b,
              Pin       oe_pin = Pin(PORTX, 0))
    {
        i2c_             = i2c;
        draw_buffer_     = dma_buffer_a;
        transmit_buffer_ = dma_buffer_b;
        oe_pin_          = oe_pin;
        for(int d = 0; d < numDrivers; d++)
            addresses_[d] = addresses[d];
        current_driver_idx_ = -1;

        InitializeBuffers();
        InitializeDrivers();
    }

    /** Returns the number of leds available from this driver. */
    constexpr int GetNumLeds() const { return numDrivers * 16; }

    /** Sets all leds to a gamma corrected brightness between 0.0f and 1.0f. */
    void SetAllTo(float brightness)
    {
        const uint8_t intBrightness
            = (uint8_t)(std::clamp(brightness * 255.0f, 0.0f, 255.0f));
        SetAllTo(intBrightness);
    }

    /** Sets all leds to a gamma corrected brightness between 0 and 255. */
    void SetAllTo(uint8_t brightness)
    {
        const uint16_t cycles = gamma_table_[brightness];
        SetAllToRaw(cycles);
    }

    /** Sets all leds to a raw 12bit brightness between 0 and 4095. */
    void SetAllToRaw(uint16_t rawBrightness)
    {
        for(int led = 0; led < GetNumLeds(); led++)
            SetLedRaw(led, rawBrightness);
    }

    /** Sets a single led to a gamma corrected brightness between 0.0f and 1.0f. */
    void SetLed(int ledIndex, float brightness)
    {
        const uint8_t intBrightness
            = (uint8_t)(std::clamp(brightness * 255.0f, 0.0f, 255.0f));
        SetLed(ledIndex, intBrightness);
    }

    /** Sets a single led to a gamma corrected brightness between 0 and 255. */
    void SetLed(int ledIndex, uint8_t brightness)
    {
        const uint16_t cycles = gamma_table_[brightness];
        SetLedRaw(ledIndex, cycles);
    }

    /** Sets a single led to a raw 12bit brightness between 0 and 4095. */
    void SetLedRaw(int ledIndex, uint16_t rawBrightness)
    {
        const auto d  = GetDriverForLed(ledIndex);
        const auto ch = GetDriverChannelForLed(ledIndex);
        // mask away the "full on" bit
        const auto on                = draw_buffer_[d].leds[ch].on & (0x0FFF);
        draw_buffer_[d].leds[ch].off = (on + rawBrightness) & (0x0FFF);
        // full on condition
        if(rawBrightness >= 0x0FFF)
            draw_buffer_[d].leds[ch].on = 0x1000 | on; // set "full on" bit
        else
            draw_buffer_[d].leds[ch].on = on; // clear "full on" bit
    }

    /** Swaps the current draw buffer and the current transmit buffer and
     *  starts transmitting the values to all chips.
     */
    void SwapBuffersAndTransmit()
    {
        // wait for current transmission to complete
        while(current_driver_idx_ >= 0) {};

        // swap buffers
        auto tmp         = transmit_buffer_;
        transmit_buffer_ = draw_buffer_;
        draw_buffer_     = tmp;

        // copy current transmit buffer contents to the new draw buffer
        // to keep the led settings (if required)
        if(persistentBufferContents)
        {
            for(int d = 0; d < numDrivers; d++)
                for(int ch = 0; ch < 16; ch++)
                    draw_buffer_[d].leds[ch].off
                        = transmit_buffer_[d].leds[ch].off;
        }

        // start transmission
        current_driver_idx_ = -1;
        ContinueTransmission();
    }

  private:
    void ContinueTransmission()
    {
        current_driver_idx_ = current_driver_idx_ + 1;
        if(current_driver_idx_ >= numDrivers)
        {
            current_driver_idx_ = -1;
            return;
        }

        const auto    d       = current_driver_idx_;
        const uint8_t address = PCA9685_I2C_BASE_ADDRESS | addresses_[d];
        const auto    status  = i2c_.TransmitDma(address,
                                             (uint8_t*)&transmit_buffer_[d],
                                             PCA9685TransmitBuffer::size,
                                             &TxCpltCallback,
                                             this);
        if(status != I2CHandle::Result::OK)
        {
            // TODO: fix this :-)
            // Reinit I2C (probably a flag to kill, but hey this works fairly well for now.)
            i2c_.Init(i2c_.GetConfig());
        }
    }
    uint16_t GetStartCycleForLed(int ledIndex) const
    {
        return (ledIndex << 2) & 0x0FFF; // shift each led by 4 cycles
    }

    uint8_t GetDriverForLed(int ledIndex) const { return ledIndex >> 4; }

    uint8_t GetDriverChannelForLed(int ledIndex) const
    {
        return ledIndex & 0x0F;
    }

    void InitializeBuffers()
    {
        for(int led = 0; led < GetNumLeds(); led++)
        {
            const auto d                     = GetDriverForLed(led);
            const auto ch                    = GetDriverChannelForLed(led);
            const auto startCycle            = GetStartCycleForLed(led);
            draw_buffer_[d].registerAddr     = PCA9685_LED0;
            draw_buffer_[d].leds[ch].on      = startCycle;
            draw_buffer_[d].leds[ch].off     = startCycle;
            transmit_buffer_[d].registerAddr = PCA9685_LED0;
            transmit_buffer_[d].leds[ch].on  = startCycle;
            transmit_buffer_[d].leds[ch].off = startCycle;
        }
    }

    void InitializeDrivers()
    {
        // init OE pin and pull low to enable outputs
        if(oe_pin_.port != PORTX)
        {
            oe_pin_gpio_.Init(oe_pin_, GPIO::Mode::OUTPUT);
            oe_pin_gpio_.Write(0);
        }

        // init the individual drivers
        for(int d = 0; d < numDrivers; d++)
        {
            const uint8_t address = PCA9685_I2C_BASE_ADDRESS | addresses_[d];
            uint8_t       buffer[2];
            buffer[0] = PCA9685_MODE1;
            buffer[1] = 0x00;
            i2c_.TransmitBlocking(address, buffer, 2, 1);
            System::Delay(20);
            buffer[0] = PCA9685_MODE1;
            buffer[1] = 0x00;
            i2c_.TransmitBlocking(address, buffer, 2, 1);
            System::Delay(20);
            buffer[0] = PCA9685_MODE1;
            // auto increment on
            buffer[1] = 0b00100000;
            i2c_.TransmitBlocking(address, buffer, 2, 1);
            System::Delay(20);
            buffer[0] = PCA9685_MODE2;
            // OE-high = high Impedance
            // Push-Pull outputs
            // outputs change on STOP
            // outputs inverted
            buffer[1] = 0b000110110;
            i2c_.TransmitBlocking(address, buffer, 2, 5);
        }
    }

    // an internal function to handle i2c callbacks
    // called when an I2C transmission completes and the next driver must be updated
    static void TxCpltCallback(void* context, I2CHandle::Result result)
    {
        auto drv_ptr = reinterpret_cast<
            LedDriverPca9685<numDrivers, persistentBufferContents>*>(context);
        drv_ptr->ContinueTransmission();
    }

    I2CHandle              i2c_;
    PCA9685TransmitBuffer* draw_buffer_;
    PCA9685TransmitBuffer* transmit_buffer_;
    uint8_t                addresses_[numDrivers];
    Pin                    oe_pin_;
    GPIO                   oe_pin_gpio_;
    // index of the dirver that is currently updated.
    volatile int8_t current_driver_idx_;
    const uint16_t  gamma_table_[256] = {
        0,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,
        2,    2,    2,    2,    2,    2,    2,    3,    3,    4,    4,    5,
        5,    6,    7,    8,    8,    9,    10,   11,   12,   13,   15,   16,
        17,   18,   20,   21,   23,   25,   26,   28,   30,   32,   34,   36,
        38,   40,   43,   45,   48,   50,   53,   56,   59,   62,   65,   68,
        71,   75,   78,   82,   85,   89,   93,   97,   101,  105,  110,  114,
        119,  123,  128,  133,  138,  143,  149,  154,  159,  165,  171,  177,
        183,  189,  195,  202,  208,  215,  222,  229,  236,  243,  250,  258,
        266,  273,  281,  290,  298,  306,  315,  324,  332,  341,  351,  360,
        369,  379,  389,  399,  409,  419,  430,  440,  451,  462,  473,  485,
        496,  508,  520,  532,  544,  556,  569,  582,  594,  608,  621,  634,
        648,  662,  676,  690,  704,  719,  734,  749,  764,  779,  795,  811,
        827,  843,  859,  876,  893,  910,  927,  944,  962,  980,  998,  1016,
        1034, 1053, 1072, 1091, 1110, 1130, 1150, 1170, 1190, 1210, 1231, 1252,
        1273, 1294, 1316, 1338, 1360, 1382, 1404, 1427, 1450, 1473, 1497, 1520,
        1544, 1568, 1593, 1617, 1642, 1667, 1693, 1718, 1744, 1770, 1797, 1823,
        1850, 1877, 1905, 1932, 1960, 1988, 2017, 2045, 2074, 2103, 2133, 2162,
        2192, 2223, 2253, 2284, 2315, 2346, 2378, 2410, 2442, 2474, 2507, 2540,
        2573, 2606, 2640, 2674, 2708, 2743, 2778, 2813, 2849, 2884, 2920, 2957,
        2993, 3030, 3067, 3105, 3143, 3181, 3219, 3258, 3297, 3336, 3376, 3416,
        3456, 3496, 3537, 3578, 3619, 3661, 3703, 3745, 3788, 3831, 3874, 3918,
        3962, 4006, 4050, 4095};

    static constexpr uint8_t PCA9685_I2C_BASE_ADDRESS = 0b01000000;
    static constexpr uint8_t PCA9685_MODE1
        = 0x00; // location for Mode1 register address
    static constexpr uint8_t PCA9685_MODE2
        = 0x01; // location for Mode2 reigster address
    static constexpr uint8_t PCA9685_LED0
        = 0x06; // location for start of LED0 registers
    static constexpr uint8_t PRE_SCALE_MODE
        = 0xFE; //location for setting prescale (clock speed)
};

} // namespace daisy

#endif
#endif
