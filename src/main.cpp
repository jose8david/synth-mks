/*
 * SynthOscillator
 *
 * Teensy 4.1 with PCM5102A I2S DAC + USB MIDI input.
 *
 * Signal chain:
 *   osc1 (bandlimited saw) + osc2 (bandlimited saw, +7 cents)
 *   → mixer → resonant low-pass filter → amp envelope → I2S out
 *
 * Detuning osc2 by a few cents causes the two saws to beat against each other,
 * giving a natural chorus/thickness effect without any extra processing.
 * AudioFilterStateVariable is a 2-pole SVF; LP output has a gentle rolloff
 * and the resonance parameter lets it bloom toward self-oscillation.
 *
 * PCM5102A wiring to Teensy 4.1:
 *   SCK   →  GND   (PCM5102A uses internal PLL, no master clock needed)
 *   BCK   → Pin 21 (BCLK)
 *   DIN   → Pin 7  (DOUT)
 *   LRCK  → Pin 20 (LRCLK)
 *   GND   → GND
 *   VIN   → 3.3V
 *   XSMT  → 3.3V   (soft mute, active-low — tie high to unmute)
 *   FMT   → GND    (I2S format) (Optional?)
 */

#include <Audio.h>

// --- Audio objects ---
AudioSynthWaveform osc1; // fundamental, bandlimited sawtooth
AudioSynthWaveform osc2; // +7 cents detuned — beating adds thickness
AudioMixer4 oscMix;
AudioFilterStateVariable filter; // 2-pole SVF; LP output fed to envelope
AudioEffectEnvelope ampEnv;
AudioOutputI2S i2sOut;

// --- Patch cords ---
AudioConnection c1(osc1, 0, oscMix, 0);
AudioConnection c2(osc2, 0, oscMix, 1);
AudioConnection c3(oscMix, 0, filter, 0);
AudioConnection c4(filter, 0, ampEnv, 0); // port 0 = low-pass output
AudioConnection c5(ampEnv, 0, i2sOut, 0); // left
AudioConnection c6(ampEnv, 0, i2sOut, 1); // right

// +7 cents: 2^(7/1200)
static constexpr float DETUNE_RATIO = 1.00412f;

float midiNoteToFreq(int note)
{
    return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}

void handleNoteOn(byte channel, byte note, byte velocity)
{
    float freq = midiNoteToFreq(note);
    float amp = velocity / 127.0f;

    osc1.frequency(freq);
    osc1.amplitude(amp * 0.5f);

    osc2.frequency(freq * DETUNE_RATIO);
    osc2.amplitude(amp * 0.5f);

    ampEnv.noteOn();
}

void handleNoteOff(byte channel, byte note, byte velocity)
{
    ampEnv.noteOff();
}

void setup()
{
    AudioMemory(20);

    // Bandlimited sawtooth avoids aliasing at high notes
    osc1.begin(0.0f, 440.0f, WAVEFORM_BANDLIMIT_SAWTOOTH);
    osc2.begin(0.0f, 440.0f * DETUNE_RATIO, WAVEFORM_BANDLIMIT_SAWTOOTH);

    // Equal blend of both oscillators
    oscMix.gain(0, 0.6f);
    oscMix.gain(1, 0.6f);

    // Low-pass filter: cutoff ~3 kHz, resonance range is 0.7 (flat) to 5.0 (self-oscillate)
    filter.frequency(3000.0f);
    filter.resonance(1.8f);

    // Amp envelope
    ampEnv.attack(8);
    ampEnv.decay(120);
    ampEnv.sustain(0.7f);
    ampEnv.release(400);

    usbMIDI.setHandleNoteOn(handleNoteOn);
    usbMIDI.setHandleNoteOff(handleNoteOff);
}

void loop()
{
    usbMIDI.read();
}
