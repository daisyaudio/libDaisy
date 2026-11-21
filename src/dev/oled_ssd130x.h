#pragma once
#ifndef SA_OLED_SSD130X_H
#define SA_OLED_SSD130X_H /**< & */

#include "per/i2c.h"
#include "per/spi.h"
#include "per/gpio.h"
#include "sys/system.h"
#include "stm32h7xx_hal.h"

namespace daisy
{
/**
 * I2C Transport for SSD1306 / SSD1309 OLED display devices
 */
class SSD130xI2CTransport
{
  public:
    struct Config
    {
        Config()
        {
            // Intialize using defaults
            Defaults();
        }
        I2CHandle::Config i2c_config;
        uint8_t           i2c_address;
        void              Defaults()
        {
            i2c_config.periph         = I2CHandle::Config::Peripheral::I2C_1;
            i2c_config.speed          = I2CHandle::Config::Speed::I2C_1MHZ;
            i2c_config.mode           = I2CHandle::Config::Mode::I2C_MASTER;
            i2c_config.pin_config.scl = Pin(PORTB, 8);
            i2c_config.pin_config.sda = Pin(PORTB, 9);
            i2c_address               = 0x3C;
        }
    };
    void Init(const Config& config)
    {
        i2c_address_ = config.i2c_address;
        i2c_.Init(config.i2c_config);
    };
    void SendCommand(uint8_t cmd)
    {
        uint8_t buf[2] = {0X00, cmd};
        i2c_.TransmitBlocking(i2c_address_, buf, 2, 1000);
    };

    void SendData(uint8_t* buff, size_t size)
    {
        // SSD1306 auto-increments its column pointer after one 0x40
        // prefix, so a page can go out in a single transaction.
        constexpr size_t kMaxBurst = 132; // widest driver width used here (128) + margin
        if(size <= kMaxBurst - 1)
        {
            uint8_t buf[kMaxBurst];
            buf[0] = 0X40;
            for(size_t i = 0; i < size; i++)
                buf[1 + i] = buff[i];
            i2c_.TransmitBlocking(
                i2c_address_, buf, static_cast<uint16_t>(size + 1), 1000);
            return;
        }
        for(size_t i = 0; i < size; i++)
        {
            uint8_t buf[2] = {0X40, buff[i]};
            i2c_.TransmitBlocking(i2c_address_, buf, 2, 1000);
        }
    };

  private:
    daisy::I2CHandle i2c_;
    uint8_t          i2c_address_;
};

/**
 * 4 Wire SPI Transport for SSD1306 / SSD1309 OLED display devices
 */
class SSD130x4WireSpiTransport
{
  public:
    struct Config
    {
        Config()
        {
            // Initialize using defaults
            Defaults();
        }
        SpiHandle::Config spi_config;
        struct
        {
            Pin dc;    /**< & */
            Pin reset; /**< & */
        } pin_config;
        bool useDma;
        void Defaults()
        {
            // SPI peripheral config
            spi_config.periph = SpiHandle::Config::Peripheral::SPI_1;
            spi_config.mode   = SpiHandle::Config::Mode::MASTER;
            spi_config.direction
                = SpiHandle::Config::Direction::TWO_LINES_TX_ONLY;
            spi_config.datasize       = 8;
            spi_config.clock_polarity = SpiHandle::Config::ClockPolarity::LOW;
            spi_config.clock_phase    = SpiHandle::Config::ClockPhase::ONE_EDGE;
            spi_config.nss            = SpiHandle::Config::NSS::HARD_OUTPUT;
            spi_config.baud_prescaler = SpiHandle::Config::BaudPrescaler::PS_8;
            // SPI pin config
            spi_config.pin_config.sclk = Pin(PORTG, 11);
            spi_config.pin_config.miso = Pin(PORTX, 0);
            spi_config.pin_config.mosi = Pin(PORTB, 5);
            spi_config.pin_config.nss  = Pin(PORTG, 10);
            // SSD130x control pin config
            pin_config.dc    = Pin(PORTB, 4);
            pin_config.reset = Pin(PORTB, 15);
            // Using DMA off by default
            useDma = false;
        }
    };
    void Init(const Config& config)
    {
        // Initialize both GPIO
        pin_dc_.Init(config.pin_config.dc, GPIO::Mode::OUTPUT);
        pin_reset_.Init(config.pin_config.reset, GPIO::Mode::OUTPUT);

        // Initialize SPI
        spi_.Init(config.spi_config);

        // Reset and Configure OLED.
        pin_reset_.Write(0);
        System::Delay(10);
        pin_reset_.Write(1);
        System::Delay(10);
    };

    void SendCommand(uint8_t cmd)
    {
        pin_dc_.Write(0);
        spi_.BlockingTransmit(&cmd, 1);
    };

    void SendCommands(uint8_t* buff, size_t size)
    {
        pin_dc_.Write(0);
        spi_.BlockingTransmit(buff, size);
    };

    void SendData(uint8_t* buff, size_t size)
    {
        pin_dc_.Write(1);
        spi_.BlockingTransmit(buff, size);
    };

    void SendDataDma(uint8_t*                          buff,
                     size_t                            size,
                     SpiHandle::EndCallbackFunctionPtr end_callback,
                     void*                             context)
    {
        SCB_CleanInvalidateDCache_by_Addr(buff, size);
        pin_dc_.Write(1);
        spi_.DmaTransmit(buff, size, NULL, end_callback, context);
    };

  private:
    SpiHandle spi_;
    GPIO      pin_reset_;
    GPIO      pin_dc_;
};

/**
 * Soft SPI Transport for SSD1306 / SSD1309 OLED display devices
 */
class SSD130x4WireSoftSpiTransport
{
  public:
    struct Config
    {
        Config()
        {
            // Initialize using defaults
            Defaults();
        }
        struct
        {
            uint32_t sclk_delay;
            Pin      sclk;
            Pin      mosi;
            Pin      dc;
            Pin      reset;
        } pin_config;
        void Defaults()
        {
            pin_config.sclk_delay = 0; // fast as possible?!
            // SPI peripheral config
            pin_config.sclk = Pin(PORTD, 3); /**< D10 - SPI2 SCK  */
            pin_config.mosi = Pin(PORTC, 3); /**< D9  - SPI2 MOSI */
            // SSD130x control pin config
            pin_config.dc    = Pin(PORTC, 11); //D2
            pin_config.reset = Pin(PORTC, 10); //D3
        }
    };
    void Init(const Config& config)
    {
        // Initialize both GPIO
        pin_sclk_.Init(config.pin_config.sclk, GPIO::Mode::OUTPUT);
        pin_sclk_.Write(1); //ClockPolarity::LOW
        clk_delay = config.pin_config.sclk_delay;
        pin_mosi_.Init(config.pin_config.mosi, GPIO::Mode::OUTPUT);
        pin_mosi_.Write(0);

        pin_dc_.Init(config.pin_config.dc, GPIO::Mode::OUTPUT);
        pin_reset_.Init(config.pin_config.reset, GPIO::Mode::OUTPUT);

        // Reset and Configure OLED.
        pin_reset_.Write(0);
        System::Delay(10);
        pin_reset_.Write(1);
        System::Delay(10);
    };
    void SendCommand(uint8_t cmd)
    {
        pin_dc_.Write(0);
        SoftSpiTransmit(cmd);
    };

    void SendData(uint8_t* buff, size_t size)
    {
        pin_dc_.Write(1);
        for(size_t i = 0; i < size; i++)
            SoftSpiTransmit(buff[i]);
    };

  private:
    void SoftSpiTransmit(uint8_t val)
    {
        // bit flip
        val = ((val & 0x01) << 7) | ((val & 0x02) << 5) | ((val & 0x04) << 3)
              | ((val & 0x08) << 1) | ((val & 0x10) >> 1) | ((val & 0x20) >> 3)
              | ((val & 0x40) >> 5) | ((val & 0x80) >> 7);

        for(uint8_t bit = 0u; bit < 8u; bit++)
        {
            pin_mosi_.Write((val & (1 << bit)) ? 1 : 0);

            System::DelayTicks(clk_delay);

            pin_sclk_.Toggle();

            System::DelayTicks(clk_delay);

            pin_sclk_.Toggle();
        }
    }

    uint32_t clk_delay;
    GPIO     pin_sclk_;
    GPIO     pin_mosi_;
    GPIO     pin_reset_;
    GPIO     pin_dc_;
};


/**
 * A driver implementation for the SSD1306/SSD1309
 */
template <size_t width, size_t height, typename Transport>
class SSD130xDriver
{
  public:
    struct Config
    {
        typename Transport::Config transport_config;

        /** Higher column start address sent before each page write.
         *  64x32 panels are wired to the middle 64 columns of the
         *  controller's 128-column RAM and so need a 32-column offset
         *  (0x10 | 32 >> 4 == 0x12); every other geometry starts at
         *  column 0 (0x10). Override for panels that map differently:
         *  the offset is (controller RAM columns - panel width) / 2,
         *  where the SSD1306/SSD1309 have 128 columns and the SH1106 132. */
        uint8_t high_column_addr = (width == 64 && height == 32) ? 0x12 : 0x10;
    };

    void Init(Config config)
    {
        transport_.Init(config.transport_config);
        high_column_addr_ = config.high_column_addr;

        // Init routine...

        // Display Off
        transport_.SendCommand(0xaE);
        // Dimension dependent commands...
        switch(height)
        {
            case 16:
                // Display Clock Divide Ratio
                transport_.SendCommand(0xD5);
                transport_.SendCommand(0x60);
                // Multiplex Ratio
                transport_.SendCommand(0xA8);
                transport_.SendCommand(0x0F);
                // COM Pins
                transport_.SendCommand(0xDA);
                transport_.SendCommand(0x02);
                break;
            case 32:
                // Display Clock Divide Ratio
                transport_.SendCommand(0xD5);
                transport_.SendCommand(0x80);
                // Multiplex Ratio
                transport_.SendCommand(0xA8);
                transport_.SendCommand(0x1F);
                // COM Pins
                transport_.SendCommand(0xDA);
                if(width == 64)
                {
                    transport_.SendCommand(0x12);
                }
                else
                {
                    transport_.SendCommand(0x02);
                }

                break;
            case 48:
                // Display Clock Divide Ratio
                transport_.SendCommand(0xD5);
                transport_.SendCommand(0x80);
                // Multiplex Ratio
                transport_.SendCommand(0xA8);
                transport_.SendCommand(0x2F);
                // COM Pins
                transport_.SendCommand(0xDA);
                transport_.SendCommand(0x12);
                break;
            default: // 128
                // Display Clock Divide Ratio
                transport_.SendCommand(0xD5);
                transport_.SendCommand(0x80);
                // Multiplex Ratio
                transport_.SendCommand(0xA8);
                transport_.SendCommand(0x3F);
                // COM Pins
                transport_.SendCommand(0xDA);
                transport_.SendCommand(0x12);
                break;
        }

        // Display Offset
        transport_.SendCommand(0xD3);
        transport_.SendCommand(0x00);
        // Start Line Address
        transport_.SendCommand(0x40);
        // Normal Display
        transport_.SendCommand(0xA6);
        // All On Resume
        transport_.SendCommand(0xA4);
        // Charge Pump
        transport_.SendCommand(0x8D);
        transport_.SendCommand(0x14);
        // Set Segment Remap
        transport_.SendCommand(0xA1);
        // COM Output Scan Direction
        transport_.SendCommand(0xC8);
        // Contrast Control
        transport_.SendCommand(0x81);
        transport_.SendCommand(0x8F);
        // Pre Charge
        transport_.SendCommand(0xD9);
        transport_.SendCommand(0x25);
        // VCOM Detect
        transport_.SendCommand(0xDB);
        transport_.SendCommand(0x34);


        // Display On
        transport_.SendCommand(0xAF); //--turn on oled panel
    };

    size_t Width() const { return width; };
    size_t Height() const { return height; };

    void DrawPixel(uint_fast8_t x, uint_fast8_t y, bool on)
    {
        if(x >= width || y >= height)
            return;
        if(on)
            buffer_[x + (y / 8) * width] |= (1 << (y % 8));
        else
            buffer_[x + (y / 8) * width] &= ~(1 << (y % 8));
    }

    void Fill(bool on)
    {
        for(size_t i = 0; i < sizeof(buffer_); i++)
        {
            buffer_[i] = on ? 0xff : 0x00;
        }
    };

    /**
     * Update the display
    */
    void Update()
    {
        uint8_t i;
        for(i = 0; i < (height / 8); i++)
        {
            transport_.SendCommand(0xB0 + i);
            transport_.SendCommand(0x00);
            transport_.SendCommand(high_column_addr_);
            transport_.SendData(&buffer_[width * i], width);
        }
    };

    /**
     * Has update finished
    */
    bool UpdateFinished() { return true; }

  protected:
    Transport transport_;
    uint8_t   buffer_[width * height / 8];
    uint8_t   high_column_addr_;
};

/**
 * A driver for the SSD1306/SSD1309 128x64 OLED displays connected via 4 wire SPI
 */
using SSD130x4WireSpi128x64Driver
    = daisy::SSD130xDriver<128, 64, SSD130x4WireSpiTransport>;

/**
 * A driver for the SSD1306/SSD1309 128x32 OLED displays connected via 4 wire SPI
 */
using SSD130x4WireSpi128x32Driver
    = daisy::SSD130xDriver<128, 32, SSD130x4WireSpiTransport>;

/**
 * A driver for the SSD1306/SSD1309 98x16 OLED displays connected via 4 wire SPI
 */
using SSD130x4WireSpi98x16Driver
    = daisy::SSD130xDriver<98, 16, SSD130x4WireSpiTransport>;

/**
 * A driver for the SSD1306/SSD1309 64x48 OLED displays connected via 4 wire SPI
 */
using SSD130x4WireSpi64x48Driver
    = daisy::SSD130xDriver<64, 48, SSD130x4WireSpiTransport>;

/**
 * A driver for the SSD1306/SSD1309 64x32 OLED displays connected via 4 wire SPI
 */
using SSD130x4WireSpi64x32Driver
    = daisy::SSD130xDriver<64, 32, SSD130x4WireSpiTransport>;

/**
 * A driver for the SSD1306/SSD1309 128x64 OLED displays connected via I2C
 */
using SSD130xI2c128x64Driver
    = daisy::SSD130xDriver<128, 64, SSD130xI2CTransport>;

/**
 * A driver for the SSD1306/SSD1309 128x32 OLED displays connected via I2C
 */
using SSD130xI2c128x32Driver
    = daisy::SSD130xDriver<128, 32, SSD130xI2CTransport>;

/**
 * A driver for the SSD1306/SSD1309 98x16 OLED displays connected via I2C
 */
using SSD130xI2c98x16Driver = daisy::SSD130xDriver<98, 16, SSD130xI2CTransport>;

/**
 * A driver for the SSD1306/SSD1309 64x48 OLED displays connected via I2C
 */
using SSD130xI2c64x48Driver = daisy::SSD130xDriver<64, 48, SSD130xI2CTransport>;

/**
 * A driver for the SSD1306/SSD1309 64x32 OLED displays connected via I2C
 */
using SSD130xI2c64x32Driver = daisy::SSD130xDriver<64, 32, SSD130xI2CTransport>;

/**
 * A driver for the SSD1306/SSD1309 128x64 OLED displays connected via 4 wire SPI
 */
using SSD130x4WireSoftSpi128x64Driver
    = daisy::SSD130xDriver<128, 64, SSD130x4WireSoftSpiTransport>;


/**
 * A driver implementation for the SSD1307
 */
template <size_t width, size_t height, typename Transport>
class SSD1307Driver
{
  public:
    struct Config
    {
        typename Transport::Config transport_config;

        /** Higher column start address sent before each page write.
         *  See SSD130xDriver::Config::high_column_addr — no SSD1307 alias
         *  is 64x32 today, but the member is kept in step so both drivers
         *  are configured the same way. */
        uint8_t high_column_addr = (width == 64 && height == 32) ? 0x12 : 0x10;
    };

    void Init(Config config)
    {
        transport_.Init(config.transport_config);

        high_column_addr_ = config.high_column_addr;
        useDma_           = config.transport_config.useDma;

        // Init routine...
        uint8_t uDispayOffset;
        uint8_t uMultiplex;
        switch(height)
        {
            case 64:
                uDispayOffset = 0x60;
                uMultiplex    = 0x7F;
                break;

            case 80:
                uDispayOffset = 0x68;
                uMultiplex    = 0x4F;
                break;

            case 128:
            default:
                uDispayOffset = 0x00;
                uMultiplex    = 0x7F;
                break;
        }

        // Display Off
        transport_.SendCommand(0xaE);

        // Memory Mode
        transport_.SendCommand(0x20);

        // Normal Display
        transport_.SendCommand(0xA6);

        // Multiplex Ratio
        transport_.SendCommand(0xA8);
        transport_.SendCommand(uMultiplex);

        // All On Resume
        transport_.SendCommand(0xA4);

        // Display Offset
        transport_.SendCommand(0xD3);
        transport_.SendCommand(uDispayOffset);

        // Display Clock Divide Ratio
        transport_.SendCommand(0xD5);
        transport_.SendCommand(0x80);

        // Pre Charge
        transport_.SendCommand(0xD9);
        transport_.SendCommand(0x22);

        // Com Pins
        transport_.SendCommand(0xDA);
        transport_.SendCommand(0x12);

        // VCOM Detect
        transport_.SendCommand(0xDB);
        transport_.SendCommand(0x35);

        // Contrast Control
        transport_.SendCommand(0x81);
        transport_.SendCommand(0x80);

        // Display On
        transport_.SendCommand(0xAF);
    };

    size_t Width() const { return width; };
    size_t Height() const { return height; };

    void DrawPixel(uint_fast8_t x, uint_fast8_t y, bool on)
    {
        if(x >= width || y >= height)
            return;
        if(on)
            buffer_[x + (y / 8) * width] |= (1 << (y % 8));
        else
            buffer_[x + (y / 8) * width] &= ~(1 << (y % 8));
    }

    void Fill(bool on)
    {
        for(size_t i = 0; i < sizeof(buffer_); i++)
        {
            buffer_[i] = on ? 0xff : 0x00;
        }
    };

    /**
     * Update the display
    */
    void Update()
    {
        if(useDma_)
        {
            transferPagesCount_ = (height / 8);
            if(transferPagesCount_)
            {
                updateing_ = true;
                TransferPageDma(0);
            }
        }
        else
        {
            uint8_t i;
            for(i = 0; i < (height / 8); i++)
            {
                transport_.SendCommand(0xB0 + i);
                transport_.SendCommand(0x00);
                transport_.SendCommand(high_column_addr_);
                transport_.SendData(&buffer_[width * i], width);
            }
            updateing_ = false;
        }
    };

    /**
     * Has update finished
    */
    bool UpdateFinished() { return !updateing_; }

  private:
    Transport transport_;
    uint8_t   buffer_[width * height / 8];
    bool      updateing_;
    uint8_t   transferPagesCount_;
    uint8_t   transferingPage_;
    bool      useDma_;
    uint8_t   high_column_addr_;

    void TransferPageDma(uint8_t page)
    {
        transferingPage_ = page;

        uint8_t commands[] = {static_cast<uint8_t>(0xB0 + transferingPage_),
                              0x00,
                              high_column_addr_};
        transport_.SendCommands(commands, 3);
        // transport_.SendCommand(0xB0 + transferingPage_);
        // transport_.SendCommand(0x00);
        // transport_.SendCommand(high_column_addr);
        transport_.SendDataDma(&buffer_[width * transferingPage_],
                               width,
                               SpiPageCompleteCallback,
                               this);
        //        transport_.SendDataDma(&buffer_[width * 16], width, SpiPageCompleteCallback, this);
    }

    void PageTransfered(void)
    {
        if(transferingPage_ < transferPagesCount_ - 1)
        {
            TransferPageDma(transferingPage_ + 1);
        }
        else
            updateing_ = false;
    }

    static void SpiPageCompleteCallback(void*                    context,
                                        daisy::SpiHandle::Result result)
    {
        static_cast<SSD1307Driver*>(context)->PageTransfered();
    }
};

/**
 * A driver for the SSD1307 128x64 OLED displays connected via 4 wire SPI
 */
using SSD13074WireSpi128x64Driver
    = daisy::SSD1307Driver<128, 64, SSD130x4WireSpiTransport>;

/**
 * A driver for the SSD1307 128x80 OLED displays connected via 4 wire SPI
 */
using SSD13074WireSpi128x80Driver
    = daisy::SSD1307Driver<128, 80, SSD130x4WireSpiTransport>;

/**
 * A driver for the SSD1307 128x128 OLED displays connected via 4 wire SPI
 */
using SSD13074WireSpi128x128Driver
    = daisy::SSD1307Driver<128, 128, SSD130x4WireSpiTransport>;

/**
 * A driver for the SSD1307 128x64 OLED displays connected via I2C
 */
using SSD1307I2c128x64Driver
    = daisy::SSD130xDriver<128, 64, SSD130xI2CTransport>;

/**
 * A driver for the SSD1307 128x80 OLED displays connected via I2C
 */
using SSD1307I2c128x80Driver
    = daisy::SSD1307Driver<128, 80, SSD130xI2CTransport>;

/**
 * A driver for the SSD1307 128x128 OLED displays connected via I2C
 */
using SSD1307I2c128x128Driver
    = daisy::SSD130xDriver<128, 128, SSD130xI2CTransport>;


}; // namespace daisy


#endif
