/**
 * Read Trill craft capacitive sensor
 */

#include "daisy_seed.h"
#include "dev/trill/Trill.h"

using namespace daisy;

DaisySeed hw;

int main(void)
{
    // Initialize the Daisy Seed
    hw.Init();

    // Start the Serial Logger
    hw.StartLog();

    // Create a Trill object
    Trill trill;

    // Initialize the Trill object
    int i2cBus = 1; // only 1 and 4 are properly mapped to pins on the Seed
    int ret    = trill.setup(i2cBus, Trill::CRAFT);
    if(ret)
        hw.PrintLine("trill.setup() returned %d", ret);

    // loop forever
    while(1)
    {
        hw.DelayMs(100);
        trill.readI2C();
        for(auto &x : trill.rawData)
        {
            hw.Print("%d ", int(x * 100000.f));
        }
        hw.PrintLine("");
    }
    return 0;
}
