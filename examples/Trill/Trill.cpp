/**
 * Read Trill capacitive sensor
 */

#include "daisy_seed.h"
#include "dev/trill/Trill.h"

using namespace daisy;
using namespace daisy::seed;

DaisySeed hw;

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    //
}

int main(void)
{
    // Initialize the Daisy Seed
    hw.Init();

    // Start the Serial Logger
    hw.StartLog();

    /** Start the Audio engine, and call the "AudioCallback" function to fill new data */
    hw.StartAudio(AudioCallback);

    // Create a Trill object
    Trill trill;

    // Initialize the Trill object
    int i2cBus = 1; // only 1 and 4 are properly mapped to pins on the Seed
    int ret    = trill.setup(i2cBus, Trill::BAR);
    if(ret)
        hw.Print("trill.setup() returned %d\n", ret);

    // loop forever
    while(1)
    {
        trill.readI2C();
        if(trill.getNumTouches())
        {
            for(size_t n = 0; n < trill.getNumTouches(); ++n)
                hw.Print(
                    "%.3f (%.2f) ", trill.touchLocation(n), trill.touchSize(n));
            hw.Print("\n");
        }
    }
}
