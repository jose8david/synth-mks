# synth-mks — DualSynth Poly

Teensy 4.1 polyphonic music synthesizer with USB/hardware MIDI input, I2S audio output via PCM5102A, and an ESP32 C3 Mini serving a WiFi web patch editor.

## Architecture

The project contains three firmware targets:

| Target | Board | Description |
|--------|-------|-------------|
| `src/main.cpp` | Teensy 4.1 | Original monophonic synth (2 detuned saws → SVF filter → envelope → I2S) |
| `src/MIDI/src/main.cpp` | Teensy 4.1 | Full polysynth — 4 voices, Moog Bass + Juno engines, effects, OLED UI |
| `src/ESP32/src/main.cpp` | ESP32 C3 Mini | WiFi Access Point with web-based patch editor and UART bridge to Teensy |

## Build

```bash
# Main Teensy firmware
pio run

# MIDI polysynth firmware
pio run -d src/MIDI

# ESP32 patch server
pio run -d src/ESP32
```

## Pin Connections

### PCM5102A I2S DAC → Teensy 4.1

| PCM5102A | Teensy 4.1 |
|----------|-------------|
| SCK      | GND (internal PLL, no master clock) |
| BCK      | Pin 21 (BCLK) |
| DIN      | Pin 7  (DOUT) |
| LRCK     | Pin 20 (LRCLK) |
| GND      | GND |
| VIN      | 3.3V |
| XSMT     | 3.3V (unmute) |
| FMT      | GND (I2S format) |

### EC11 Rotary Encoder → Teensy 4.1

| Encoder | Teensy 4.1 |
|---------|-------------|
| CLK     | Pin 2 |
| DT      | Pin 3 |
| SW (push) | Pin 4 (INPUT_PULLUP) |

### SH1106 OLED 128×64 (I2C) → Teensy 4.1

| OLED | Teensy 4.1 |
|------|-------------|
| SDA   | Pin 18 (Wire) |
| SCL   | Pin 19 (Wire) |
| VCC   | 3.3V |
| GND   | GND |

### Hardware MIDI (DIN-5) → Teensy 4.1

Uses `Serial1` with an external MIDI input circuit (optoisolator 6N138):

| MIDI DIN-5 | Teensy 4.1 |
|------------|-------------|
| Pin 4 (5V via 220Ω) | Pin 0 (RX1) via 6N138 |
| Pin 2 (GND) | GND |

See [`docs/MIDI.png`](docs/MIDI.png) for the circuit schematic.

### ESP32 C3 Mini ↔ Teensy 4.1 (UART)

Serial communication at 115200 baud for patch data exchange (JSON lines).

| ESP32 C3 Mini | Teensy 4.1 |
|---------------|-------------|
| GPIO20 (UART RX) | Pin 35 (TX8) |
| GPIO21 (UART TX) | Pin 34 (RX8) |
| GND | GND (common ground required) |

### Power

- USB-A female connector provides 5V input
- MP1584EN buck converter steps 5V down to 3.3V for the Teensy, ESP32, and peripherals

## MIDI CC Map (16 Potentiometers)

The polysynth (`src/MIDI`) maps the following MIDI CCs for external potentiometer control:

| Group | CC | Parameter | Range |
|-------|----|-----------|-------|
| **Filter** | 21 | Cutoff | 80–8000 Hz |
| | 22 | Resonance | 0.0–1.0 (Moog) / 0.7–5.0 (Juno) |
| | 23 | Envelope Amount | 0–6000 Hz |
| | 24 | Filter Decay | 50–2000 ms |
| **Envelope** | 25 | Attack | 1–2000 ms |
| | 26 | Decay | 10–2000 ms |
| | 27 | Sustain | 0.0–1.0 |
| | 28 | Release | 20–4000 ms |
| **Effects** | 91 | Reverb Mix | 0–100% |
| | 93 | Reverb Size | 0.0–1.0 |
| | 73 | Delay Time | 10–800 ms |
| | 70 | Delay Feedback | 0–85% |
| **LFO** | 71 | LFO Rate | 0.1–20 Hz |
| | 72 | LFO Depth | 0–100% |
| | 82 | LFO Destination | 0=filter, 1=pitch, 2=volume |
| | 83 | LFO Shape | 0=sine, 1=triangle, 2=sawtooth |
| **Mod Wheel** | 1 | Modulation Cutoff | 0–4000 Hz offset |

## ESP32 WiFi Patch Server

The ESP32 C3 Mini creates a WiFi Access Point:

- **SSID**: `SynthPatch`
- **Password**: `synthpatch`
- **Web UI**: `http://192.168.4.1`

Features:
- Edit all synth parameters via sliders
- 6 built-in presets (Minimoog Bass, Juno Pad, Moog Lead, Juno Strings, Acid Bass, Ambient Pad)
- Save/load user patches to LittleFS flash storage
- Send patches to the Teensy in real time over UART
- Periodic connection status ping (every 5 seconds)

## Upload

Compiled firmware is at:
- `.pio/build/teensy41/firmware.hex` (main/MIDI)
- `src/ESP32/.pio/build/esp32-c3-mini/firmware.bin` (ESP32)

Use [`teensy_loader_cli`](https://github.com/PaulStoffregen/teensy_loader_cli) for the Teensy:

```bash
teensy_loader_cli --mcu=TEENSY41 -w -v .pio/build/teensy41/firmware.hex
```

Use `pio run -d src/ESP32 --target upload` for the ESP32.

## PlatformIO Config Notes

- Main project uses `build_flags = -DUSB_MIDI_AUDIO_SERIAL` for USB MIDI + audio + serial
- MIDI subproject uses `-D USB_MIDI_SERIAL` for USB MIDI + serial
- ESP32 uses `board_build.filesystem = littlefs` for patch storage
