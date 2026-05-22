# synth-mks

Teensy 4.1 music synthesizer oscillator module with USB MIDI input and I2S audio output via a PCM5102A DAC.

## Setup

Open in VS Code and when prompted, click **Reopen in Container**. The devcontainer installs PlatformIO and all dependencies automatically.

To build:

```bash
pio run
```

## Upload to Teensy

The compiled firmware lives at `.pio/build/teensy41/firmware.hex`. Upload it from your **host machine** (not the container) using [`teensy_loader_cli`](https://github.com/PaulStoffregen/teensy_loader_cli).

**macOS**
```bash
brew install teensy_loader_cli
teensy_loader_cli --mcu=TEENSY41 -w -v .pio/build/teensy41/firmware.hex
```

**Linux**
```bash
# Debian/Ubuntu
sudo apt install teensy-loader-cli

teensy_loader_cli --mcu=TEENSY41 -w -v .pio/build/teensy41/firmware.hex
```

**Windows**

Download the prebuilt binary from the [PJRC download page](https://www.pjrc.com/teensy/loader_cli.html) and run it from a command prompt:

```bat
teensy_loader_cli.exe --mcu=TEENSY41 -w -v .pio\build\teensy41\firmware.hex
```

The `-w` flag waits for the Teensy to enter bootloader mode. Press the physical button on the board if it doesn't reboot automatically.
