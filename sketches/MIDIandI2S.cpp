/*
 * SimpleTeensyOscillator
 *
 * Teensy 4.1 with PCM5102A I2S DAC + USB MIDI input.
 * USB MIDI note on/off drives a square wave oscillator.
 *
 * Board settings (Arduino IDE):
 *   Board:    Teensy 4.1
 *   USB Type: MIDI + Audio  (or just MIDI)
 *
 * PCM5102A wiring to Teensy 4.1:
 *   SCK        →  GND     (PCM5102A uses internal PLL, no master clock needed)
 *   BCK        →  Pin 21  (BCLK)
 *   DIN        →  Pin 7   (DOUT)
 *   LRCK       →  Pin 20  (LRCLK)
 *   GND        →  GND
 *   VIN        →  3.3V
 *   XSMT       →  3.3V    (soft mute, active low — tie high to unmute)
 *   FMT        →  GND     (I2S format) (Optional?)
 */

#include <Audio.h>

// --- Audio objects ---
AudioSynthWaveform osc;
AudioEffectEnvelope ampEnv;
AudioOutputI2S i2sOut;

// --- Patch cords ---
AudioConnection c1(osc, 0, ampEnv, 0);
AudioConnection c2(ampEnv, 0, i2sOut, 0); // left
AudioConnection c3(ampEnv, 0, i2sOut, 1); // right

float midiNoteToFreq(int note)
{
    return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}

void handleNoteOn(byte channel, byte note, byte velocity)
{
    osc.frequency(midiNoteToFreq(note));
    osc.amplitude(velocity / 127.0f * 0.4f);
    ampEnv.noteOn();
}

void handleNoteOff(byte channel, byte note, byte velocity)
{
    ampEnv.noteOff();
}

void setup()
{
    AudioMemory(16);

    osc.begin(0.0f, 440.0f, WAVEFORM_SQUARE);

    ampEnv.attack(10);
    ampEnv.decay(50);
    ampEnv.sustain(0.7f);
    ampEnv.release(200);

    usbMIDI.setHandleNoteOn(handleNoteOn);
    usbMIDI.setHandleNoteOff(handleNoteOff);
}

void loop()
{
    usbMIDI.read();
}
