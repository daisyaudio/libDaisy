# libDaisy Changelog

## Unreleased

### Breaking Changes

### Features

### Bug fixes

* patch_sm: corrected the order of the gate out pins
* sai: fixed occasional output audio channel swap (#268)

### Other
* Switch: Use `System::GetNow()` rather than the update rate to calculate `TimeHeldMs()`.  

### Migrating
* Backwards compatability will be maintained until the next breaking change, at which point the `update_rate` argument will be removed from `Switch::Init`.

## v2.0.1

### Breaking Changes

### Features

### Bug fixes

* qspi: fixed bug with GetData() that wouldn't return correct data when passed actual address instead of normalized offset (i.e. >= 0x90000000)
* sdram: fixed occasional hard fault caused by `RPDelay`, which is now set to 16 (same as in v1.0.0 and earlier)
* patch_sm: fixed integer overflow error with `VoltageToCode` method

### Other

## v2.0.0

### Breaking Changes

* qspi: updated from C to C++, and fixed up the API to be cleaner, and simpler to use
* sdram: updated from C to C++
  * Create SdramHandle class. Gets rid of configurable pin and hardcodes it instead.

### Features

* adc: added initialization mappings for pins ADC1_INP12 (PC2) and ADC1_INP13 (PC3) (Not accessible on Daisy Seed)
* board: added support files for upcoming Daisy Patch SM hardware
* rng: added new Random module that provides access to the hardware True Random Number Generator
* spi: added DMA Transactions (same type of queue system as I2C) to the SPI Handle class.
* util: added new PersistentStorage class for storing/recalling data between power cycles
* util: added new VoctCalibration helper class for calibrating ADC inputs
* seed: added support for Daisy Seed 1.1 (aka Daisy Seed rev5) hardware. Pin-compatible, with same form factor. WM8731 Codec instead of AK4556.
* bootloader: added `program-app` make target to upload code to the daisy bootloader

### Bug fixes

* adc: fixed bug with ADC where designs with more than 8 channels AND a mux would halt the ADC.
* sdram: changed SdramTiming.RPDelay to 1 to avoid underflow bug.

### Other

* util: added deinits and memory-aware inits for bootloader compatibility

### Migrating

#### QSPI

```c++
DaisySeed hw;
// ...

// Old
hw.qspi_handle.mode = DSY_QSPI_MODE_INDIRECT_POLLING;
dsy_qspi_init(&hw.qspi_handle);
dsy_qspi_erase(address, address + sector_size);
dsy_qspi_write(address, size, (uint8_t*)&some_data);

// New -- qspi mode is automatically handled
hw.qspi.Erase(address, address + sector_size);
hw.qspi.Write(address, size, (uint8_t*)&some_data);
```

#### Sdram

This is only applicable if you had a seed-level board that needed to initialize external SDRAM.

```cpp
//Init
//Old:
dsy_gpio_pin *pin_group;
dsy_sdram_handle sdram_handle;
sdram_handle.state             = DSY_SDRAM_STATE_ENABLE;
pin_group                      = sdram_handle.pin_config;
pin_group[DSY_SDRAM_PIN_SDNWE] = dsy_pin(DSY_GPIOH, 5);
dsy_sdram_init(&sdram_handle);

//New:
SdramHandle sdram;
sdram.Init();
```

## v1.0.0

### Breaking Changes

* move MidiEvent and related data to `daisy` namespace.

### Features

* string: allow more integer types in `FixedCapStr::AppendInt()`
* fifo/stack: Add new methods for searching elements
* fifo/stack: Reduce binary size
* RgbLed: Added individual setters for each color

### Bug fixes

* midi: SysEx messages that overflow stop reading data until rx sysexstop. Previously overflowed sysex would cause junk messages.
* midi: NoteOns of velocity 0 cause NoteOffs.
* usb-serial: fix RX callback function being overwritten

### Other

* test: add unit testing for midi parser.  
* tests: add tests for `FIFO`
* docs: Update TODO comment in `uart.h` to reflect most recent uart update.
* ci: add filters to the workflows
* ci: add workflow that builds libDaisy with CMake
* build: small fixes in CMakeLists.txt
* build: report detailed memory usage when linking

### Migrating

With `using namespace daisy` (as typical in example programs) no changes necessary. Otherwise:

```c++
// Old
MidiEvent event;

// New
daisy::MidiEvent event;
```

## v0.1.0

Initial Release
