#pragma once

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include "per/i2c.h"
typedef unsigned char i2c_char_t;

class I2c
{
  protected:
    ssize_t readBytes(void *buf, size_t count);
    ssize_t writeBytes(const void *buf, size_t count);
    enum
    {
        kTimeout = 10
    };
    int i2cAddress;

  public:
    I2c(){};
    I2c(I2c &&) = delete;
    int initI2C_RW(int bus, int address, int dummy);
    int closeI2C();

    virtual ~I2c();
    daisy::I2CHandle i2cHandle;
};

inline int I2c::initI2C_RW(int bus, int address, int dummy)
{
    using namespace daisy;
    I2CHandle::Config cfg;
    cfg.mode   = I2CHandle::Config::Mode::I2C_MASTER;
    cfg.speed  = I2CHandle::Config::Speed::I2C_400KHZ;
    cfg.periph = (I2CHandle::Config::Peripheral)bus;
    // TODO: default pins should be handled by I2CHandle
    switch(bus)
    {
        default:
            // can't easily find what the default pins for the other busses are
            // meant to be, so we use the default.
            // TODO: how do we log from here?
        case 1:
            cfg.periph         = I2CHandle::Config::Peripheral::I2C_1;
            cfg.pin_config.scl = {PORTB, 8};
            cfg.pin_config.sda = {PORTB, 9};
            break;
        case 4:
            cfg.periph         = I2CHandle::Config::Peripheral::I2C_4;
            cfg.pin_config.scl = {PORTB, 6};
            cfg.pin_config.sda = {PORTB, 7};
            break;
    }
    cfg.address = i2cAddress; // this seems unused anyhow
    i2cAddress  = address;
    i2cHandle.Init(cfg);
    return 0;
}

inline int I2c::closeI2C()
{
    return 0;
}

inline ssize_t I2c::readBytes(void *buf, size_t count)
{
    if(daisy::I2CHandle::Result::OK
       == i2cHandle.ReceiveBlocking(
           i2cAddress, (uint8_t *)buf, count, kTimeout))
        return count;
    else
        return -1;
}

inline ssize_t I2c::writeBytes(const void *buf, size_t count)
{
    if(daisy::I2CHandle::Result::OK
       == i2cHandle.TransmitBlocking(
           i2cAddress, (uint8_t *)buf, count, kTimeout))
        return count;
    else
        return -1;
}

inline I2c::~I2c() {}

#include "sys/system.h"
inline int usleep(useconds_t us)
{
    daisy::System::Delay((us + 999) / 1000);
    return 0;
}
