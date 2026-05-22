# synth-mks

Teensy 4.1 music synthesizer oscillator module with USB MIDI input and I2S audio output via a PCM5102A DAC.

## Build

This project uses [PlatformIO](https://platformio.org/). Always build after any code edit to verify it compiles:

```bash
pio run
```

Never try to upload the code to the module, you won't have access to it.

## Hardware constraints

**Before editing anything that could affect hardware behavior, stop and ask the user for confirmation.** This includes:

- USB mode or build flags (currently `USB_MIDI_AUDIO_SERIAL`)
- Pin assignments (I2S, MIDI, or any other peripheral pins)
- Clock or sample-rate configuration
- I2S or PCM5102A initialization settings
- Any change to `platformio.ini` that affects the board or framework

The target is a physical device. Wrong pin or USB config can require a reflash or damage hardware, so verify intent before editing.
