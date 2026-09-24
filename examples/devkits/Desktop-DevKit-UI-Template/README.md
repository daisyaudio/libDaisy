# Desktop Devkit UI Template

This example is a bit more complex than the other examples in the repo.

All inputs for the Desktop DevKit are read, and printed when changed to Serial over the external USB port, also used to power the DevKit.

The 16 buttons, 8 pots, and LEDs are all managed using libDaisy's UI class from `main_page.h`.

As usual inputs are read in the AudioCallback. UI Events are then created for the relevant pieces of the interface.
Inside the Low-priority Callback, the events are handled, and the LED UI is redrawn.
The Low-priority Callback is used so that things like SD Card Disk I/O, QSPI saving, etc. that may block the main while() loop do not cause the UI to become unresponsive, or appear stuck.

## Flashing to the Daisy

This is designed to use the external USB version of the bootloader.

This version of the bootloader can be flashed to the daisy via the built-in USB port by running `make program-boot` from this directory, or by selecting the "v6.x (external)" variant from the [web programmer](https://flash.daisy.audio/).

Once the bootloader has been flashed to the Daisy, you can power connect the USB-C cable from your computer to the Desktop Devkit's USB-C port (located above the Seed3).

With the device connected, press RESET and then BOOT on the Seed.
You should see the USR LED on the Daisy blink quickly and then start slowly pulsing.

While in this state you can run: `make program-dfu`, or upload the `Desktop-DevKit-UI-Template.bin` file to the [web programmer](https://flash.daisy.audio/) to program the example to the Seed.

Once bootloader is programmed onto the Seed it does not need to be reprogrammed unless you need to install a different variant of the bootloader, or program an app to the Seed's internal flash.

## Connecting to the Program

Once the program has been flashed to the Daisy, you'll see that the Seed's USR LED is blinking at a steady rate.
You can push each of the tactile switches to see the corresponding LED illuminate.

The CV outputs will be generating slow ramp and saw waveforms respectively, MIDI note messages to the MIDI Input will emit from the MIDI output, and audio sent to the audio inputs will be copied to the outputs.

You can connect to the program using a serial monitor, and see print statements that occur whenever any input is changed.

Within the source code you'll see that many of these print statements are set within on change callbacks in the `main_page.h`.
Other inputs are checked in the Audio Callback, and messages are generated to be sent from the main `while()` loop.
