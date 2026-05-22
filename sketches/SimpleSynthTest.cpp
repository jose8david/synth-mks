/*
 * SimpleSynthTest
 *
 * Minimal Teensy 4.1 sketch:
 *   USB MIDI In → Osc1 + Osc2 (one octave up) → Mixer → Amp Envelope → Reverb → USB Audio Out
 *
 * Board settings (Arduino IDE):
 *   Board:    Teensy 4.1
 *   USB Type: Audio + MIDI
 */

#include <Audio.h>

// --- Audio objects ---
AudioSynthWaveform osc1;
AudioSynthWaveform osc2; // one octave above osc1
AudioMixer4 oscMix;
AudioEffectEnvelope ampEnv;
AudioEffectReverb reverb;
AudioOutputUSB usbOut;

// --- Patch cords ---
AudioConnection c1(osc1, 0, oscMix, 0);
AudioConnection c2(osc2, 0, oscMix, 1);
AudioConnection c3(oscMix, 0, ampEnv, 0);
AudioConnection c4(ampEnv, 0, reverb, 0);
AudioConnection c5(reverb, 0, usbOut, 0); // left
AudioConnection c6(reverb, 0, usbOut, 1); // right

// --- MIDI note → frequency ---
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

    osc2.frequency(freq * 2.0f); // one octave up
    osc2.amplitude(amp * 0.4f);

    ampEnv.noteOn();
}

void handleNoteOff(byte channel, byte note, byte velocity)
{
    ampEnv.noteOff();
}

void setup()
{
    AudioMemory(16);

    // Oscillators — bandlimited sawtooth
    osc1.begin(0.0f, 440.0f, WAVEFORM_BANDLIMIT_SAWTOOTH);
    osc2.begin(0.0f, 880.0f, WAVEFORM_BANDLIMIT_SAWTOOTH);

    // Mixer — equal blend of both oscillators
    oscMix.gain(0, 0.6f);
    oscMix.gain(1, 0.6f);

    // Amp envelope — fast attack, moderate release
    ampEnv.attack(5);
    ampEnv.decay(100);
    ampEnv.sustain(0.8f);
    ampEnv.release(300);

    // Reverb — 50% wet
    reverb.reverbTime(1.5f);

    // USB MIDI callbacks
    usbMIDI.setHandleNoteOn(handleNoteOn);
    usbMIDI.setHandleNoteOff(handleNoteOff);
}

void loop()
{
    usbMIDI.read();
}
