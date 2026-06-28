// DualSynth Poly — Moog Bass + Juno, 4 voces + 16 potenciometros MIDI + ESP32 WebUI
// Teensy 4.1 · PCM5102A · USB MIDI + HW MIDI · OLED SH1106 · Encoder EC11
// Potenciometros: Filtro(21-24) Envolvente(25-28) Efectos(70,71,73,91,93) LFO(72,82,83)
// ESP32 C3 Mini: recepcion de parches JSON por Serial8 (Pin34 RX / Pin35 TX)

#include <Audio.h>
#include <MIDI.h>
#include <U8g2lib.h>
#include <Encoder.h>
#include <Wire.h>
#include <ArduinoJson.h>

// Serial8: comunicacion con ESP32 C3 Mini
// Pin 34 = RX8 <- ESP32 GPIO21 (TX)
// Pin 35 = TX8 -> ESP32 GPIO20 (RX)
#define ESP_SERIAL Serial8
#define ESP_BAUD   115200

// Forward declaration — debe ir despues de los includes para que byte este definido
void handleNoteOff(byte channel, byte note, byte velocity);

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI_HW);
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
Encoder enc(2, 3);

static constexpr int PIN_SW     = 4;
static constexpr int NUM_VOICES = 4;

// ─── CC MIDI de los 16 potenciometros ─────────────────────────────────────────
// Grupo FILTRO
static constexpr byte CC_FILTER_CUTOFF = 21;
static constexpr byte CC_FILTER_RESON  = 22;
static constexpr byte CC_FILTER_ENVAMT = 23;
static constexpr byte CC_FILTER_DECAY  = 24;
// Grupo ENVOLVENTE
static constexpr byte CC_ENV_ATTACK    = 25;
static constexpr byte CC_ENV_DECAY     = 26;
static constexpr byte CC_ENV_SUSTAIN   = 27;
static constexpr byte CC_ENV_RELEASE   = 28;
// Grupo EFECTOS
static constexpr byte CC_REV_MIX       = 91;
static constexpr byte CC_REV_SIZE      = 93;
static constexpr byte CC_DLY_TIME      = 73;  // TODO: verificar CC real si no responde
static constexpr byte CC_DLY_FEEDBACK  = 70;
// Grupo LFO
static constexpr byte CC_LFO_RATE      = 71;
static constexpr byte CC_LFO_DEPTH     = 72;
static constexpr byte CC_LFO_DEST      = 82;  // 0-42=filtro 43-84=pitch 85-127=vol
static constexpr byte CC_LFO_SHAPE     = 83;  // 0-42=sine  43-84=tri   85-127=saw

// ─── Audio objects ────────────────────────────────────────────────────────────
AudioSynthWaveform        osc1[NUM_VOICES];
AudioSynthWaveform        osc2[NUM_VOICES];
AudioEffectEnvelope       ampEnv[NUM_VOICES];
AudioMixer4               oscMix[NUM_VOICES];
AudioMixer4               voiceMix;
AudioFilterLadder         ladderFilter;
AudioFilterStateVariable  svfFilter;
AudioMixer4               filterMix;

AudioEffectFreeverbStereo reverb;
AudioMixer4               reverbMixL;
AudioMixer4               reverbMixR;

AudioEffectDelay          delay1;
AudioMixer4               delayMixL;
AudioMixer4               delayMixR;

AudioAmplifier            masterVol;
AudioOutputI2S            i2sOut;

// ─── Patch cords estaticas ────────────────────────────────────────────────────
// Voces → voiceMix
AudioConnection pv0e(ampEnv[0], 0, voiceMix, 0);
AudioConnection pv1e(ampEnv[1], 0, voiceMix, 1);
AudioConnection pv2e(ampEnv[2], 0, voiceMix, 2);
AudioConnection pv3e(ampEnv[3], 0, voiceMix, 3);

// voiceMix → filtros
AudioConnection pmla(voiceMix, 0, ladderFilter, 0);
AudioConnection pmsa(voiceMix, 0, svfFilter,    0);

// filtros → filterMix
AudioConnection pflm(ladderFilter, 0, filterMix, 0);
AudioConnection pfsm(svfFilter,    0, filterMix, 1);

// filterMix → reverb + delay (paralelo)
AudioConnection pfmr(filterMix, 0, reverb, 0);
AudioConnection pfmd(filterMix, 0, delay1, 0);

// Dry + reverb → reverbMix
AudioConnection prs0(filterMix, 0, reverbMixL, 0);
AudioConnection prs1(filterMix, 0, reverbMixR, 0);
AudioConnection prrl(reverb,    0, reverbMixL, 1);
AudioConnection prrr(reverb,    1, reverbMixR, 1);

// reverbMix + delay → delayMix
AudioConnection pdml(reverbMixL, 0, delayMixL, 0);
AudioConnection pdmr(reverbMixR, 0, delayMixR, 0);
AudioConnection pdl( delay1,     0, delayMixL, 1);
AudioConnection pdr( delay1,     0, delayMixR, 1);

// delayMix → masterVol → i2s
AudioConnection pmvo(masterVol, 0, i2sOut,    0);
AudioConnection pmvl(delayMixL, 0, masterVol, 0);
AudioConnection pmvr(delayMixR, 0, i2sOut,    1);

// Patch cords dinamicas (osc → oscMix → ampEnv, se crean en setup)
AudioConnection* posc1[NUM_VOICES];
AudioConnection* posc2[NUM_VOICES];
AudioConnection* pmix[NUM_VOICES];

// ─── Gestion de voces ─────────────────────────────────────────────────────────
struct Voice {
    int           note      = -1;
    bool          active    = false;
    unsigned long startTime = 0;
};
Voice voices[NUM_VOICES];

int allocVoice(int note)
{
    for (int i = 0; i < NUM_VOICES; i++)
        if (voices[i].note == note) return i;
    for (int i = 0; i < NUM_VOICES; i++)
        if (!voices[i].active) return i;
    int oldest = 0;
    for (int i = 1; i < NUM_VOICES; i++)
        if (voices[i].startTime < voices[oldest].startTime) oldest = i;
    return oldest;
}

int findVoice(int note)
{
    for (int i = 0; i < NUM_VOICES; i++)
        if (voices[i].note == note && voices[i].active) return i;
    return -1;
}

// ─── Sonidos y parametros del encoder ────────────────────────────────────────
enum SoundId { SOUND_MOOG = 0, SOUND_JUNO = 1 };

struct Param { const char* name; float value, min, max, step; };

Param moogParams[] = {
    { "CUTOFF",  450.0f,  80.0f,  8000.0f, 50.0f  },
    { "RESON",   0.35f,   0.0f,   1.0f,    0.025f },
    { "RELEASE", 300.0f,  50.0f,  2000.0f, 50.0f  },
    { "ENV AMT", 2000.0f, 0.0f,   6000.0f, 100.0f },
};
Param junoParams[] = {
    { "CUTOFF",  2200.0f, 200.0f, 12000.0f, 100.0f },
    { "RESON",   0.6f,    0.0f,   5.0f,     0.1f   },
    { "DETUNE",  7.0f,    0.0f,   50.0f,    1.0f   },
    { "RELEASE", 500.0f,  50.0f,  2000.0f,  50.0f  },
};
static constexpr int NUM_PARAMS = 4;

SoundId activeSound = SOUND_MOOG;
int     activeParam = 0;

// ─── Estado de sintesis ───────────────────────────────────────────────────────
static constexpr float OSC1_GAIN       = 0.55f;
static constexpr float OSC2_GAIN       = 0.45f;
static constexpr float FILTER_DECAY_MS = 350.0f;
static constexpr float LOOP_PERIOD_MS  = 1.0f;
static constexpr float FILTER_DECAY_K  = 1.0f - expf(-LOOP_PERIOD_MS / FILTER_DECAY_MS);

float g_filterEnvPos  = 0.0f;
float g_currentCutoff = 450.0f;
float g_modCutoff     = 0.0f;

float g_reverbMix     = 0.0f;
float g_delayTime     = 200.0f;
float g_delayFeedback = 0.0f;
float g_lfoRate       = 2.0f;
float g_lfoDepth      = 0.0f;
int   g_lfoDest       = 0;
int   g_lfoShape      = 0;

bool          g_displayDirty = true;
bool          g_showEffects  = false;

long          encLast = 0;
bool          swLast  = HIGH;
elapsedMillis swDebounce;
elapsedMillis filterEnvTimer;
elapsedMillis displayTimer;
elapsedMillis lfoTimer;

// ─── Helpers ──────────────────────────────────────────────────────────────────
const char* NOTE_NAMES[] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

float midiNoteToFreq(int note)
{
    return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}

float centsToRatio(float cents)
{
    return powf(2.0f, cents / 1200.0f);
}

float ccMap(byte val, float lo, float hi)
{
    return lo + (val / 127.0f) * (hi - lo);
}

Param* currentParams()
{
    return (activeSound == SOUND_MOOG) ? moogParams : junoParams;
}

// ─── LFO software ─────────────────────────────────────────────────────────────
void updateLFO()
{
    if (lfoTimer < 1) return;
    lfoTimer = 0;
    if (g_lfoDepth < 0.01f) return;

    static float lfoPhase = 0.0f;
    lfoPhase += g_lfoRate / 1000.0f;
    if (lfoPhase > 1.0f) lfoPhase -= 1.0f;

    float lfoVal = 0.0f;
    switch (g_lfoShape) {
        case 0: lfoVal = sinf(lfoPhase * TWO_PI);                        break;
        case 1: lfoVal = (lfoPhase < 0.5f)
                       ? (4.0f * lfoPhase - 1.0f)
                       : (3.0f - 4.0f * lfoPhase);                       break;
        case 2: lfoVal = 1.0f - 2.0f * lfoPhase;                         break;
    }

    float mod = g_lfoDepth * lfoVal;

    if (g_lfoDest == 0) {
        float cutoff = constrain(g_currentCutoff + mod * 2000.0f, 80.0f, 18000.0f);
        if (activeSound == SOUND_MOOG) ladderFilter.frequency(cutoff);
        else                           svfFilter.frequency(cutoff);
    } else if (g_lfoDest == 1) {
        for (int i = 0; i < NUM_VOICES; i++) {
            if (!voices[i].active || voices[i].note < 0) continue;
            float base  = midiNoteToFreq(voices[i].note);
            float ratio = centsToRatio(mod * 50.0f);
            osc1[i].frequency(base * ratio);
            osc2[i].frequency(activeSound == SOUND_MOOG
                ? base * 0.5f * ratio
                : base * centsToRatio(junoParams[2].value) * ratio);
        }
    } else {
        masterVol.gain(constrain(0.8f + mod * 0.3f, 0.0f, 1.0f));
    }
}

// ─── Filter cutoff dinamico ───────────────────────────────────────────────────
void updateFilterCutoff()
{
    if (filterEnvTimer >= (unsigned)LOOP_PERIOD_MS) {
        filterEnvTimer = 0;
        g_filterEnvPos *= (1.0f - FILTER_DECAY_K);
    }

    if (activeSound == SOUND_MOOG) {
        float cutoff = constrain(
            moogParams[0].value + moogParams[3].value * g_filterEnvPos + g_modCutoff,
            80.0f, 18000.0f);
        g_currentCutoff = cutoff;
        if (g_lfoDepth < 0.01f || g_lfoDest != 0)
            ladderFilter.frequency(cutoff);
    } else {
        float cutoff = constrain(junoParams[0].value + g_modCutoff, 200.0f, 18000.0f);
        g_currentCutoff = cutoff;
        if (g_lfoDepth < 0.01f || g_lfoDest != 0)
            svfFilter.frequency(cutoff);
    }
}

// ─── Activacion de sonido ─────────────────────────────────────────────────────
void activateSound(SoundId id)
{
    activeSound = id;
    activeParam = 0;

    for (int i = 0; i < NUM_VOICES; i++) {
        ampEnv[i].noteOff();
        voices[i].active = false;
        voices[i].note   = -1;
    }

    if (id == SOUND_MOOG) {
        filterMix.gain(0, 1.0f);
        filterMix.gain(1, 0.0f);
        for (int i = 0; i < NUM_VOICES; i++) {
            osc1[i].begin(0.0f, 440.0f, WAVEFORM_BANDLIMIT_SAWTOOTH);
            osc2[i].begin(0.0f, 220.0f, WAVEFORM_BANDLIMIT_SAWTOOTH);
            oscMix[i].gain(0, OSC1_GAIN);
            oscMix[i].gain(1, OSC2_GAIN);
            ampEnv[i].attack(4);
            ampEnv[i].decay(80);
            ampEnv[i].sustain(0.85f);
            ampEnv[i].release(moogParams[2].value);
        }
        ladderFilter.frequency(moogParams[0].value);
        ladderFilter.resonance(moogParams[1].value);
        ladderFilter.octaveControl(3.5f);
    } else {
        filterMix.gain(0, 0.0f);
        filterMix.gain(1, 1.0f);
        for (int i = 0; i < NUM_VOICES; i++) {
            osc1[i].begin(0.0f, 440.0f, WAVEFORM_BANDLIMIT_SAWTOOTH);
            osc2[i].begin(0.0f, 440.0f, WAVEFORM_BANDLIMIT_SQUARE);
            oscMix[i].gain(0, 0.5f);
            oscMix[i].gain(1, 0.5f);
            ampEnv[i].attack(18);
            ampEnv[i].decay(150);
            ampEnv[i].sustain(0.75f);
            ampEnv[i].release(junoParams[3].value);
        }
        svfFilter.frequency(junoParams[0].value);
        svfFilter.resonance(constrain(junoParams[1].value, 0.7f, 5.0f));
    }
    g_displayDirty = true;
}

// ─── Handler CC — 16 potenciometros ──────────────────────────────────────────
void handleControlChange(byte channel, byte cc, byte value)
{
    // Descomenta para depurar que CCs llegan:
    // Serial.print("CC="); Serial.print(cc); Serial.print(" val="); Serial.println(value);

    switch (cc) {

        // ── FILTRO ────────────────────────────────────────────────────────────
        case CC_FILTER_CUTOFF:
            moogParams[0].value = ccMap(value, 80.0f, 8000.0f);
            junoParams[0].value = ccMap(value, 200.0f, 12000.0f);
            g_displayDirty = true;
            break;

        case CC_FILTER_RESON:
            moogParams[1].value = ccMap(value, 0.0f, 1.0f);
            junoParams[1].value = ccMap(value, 0.7f, 5.0f);
            ladderFilter.resonance(moogParams[1].value);
            svfFilter.resonance(junoParams[1].value);
            g_displayDirty = true;
            break;

        case CC_FILTER_ENVAMT:
            moogParams[3].value = ccMap(value, 0.0f, 6000.0f);
            g_displayDirty = true;
            break;

        case CC_FILTER_DECAY:
            // Reescala el coeficiente de decay del filter env
            // Valor bajo = decay lento, valor alto = decay rapido
            // Lo guardamos como tiempo en ms y lo usamos en updateFilterCutoff
            {
                float decayMs = ccMap(value, 50.0f, 2000.0f);
                // Actualiza dinamicamente el coeficiente de decay
                // (no es constexpr, se aplica en el proximo ciclo de updateFilterCutoff)
                const_cast<float&>(FILTER_DECAY_K) =
                    1.0f - expf(-LOOP_PERIOD_MS / decayMs);
            }
            g_displayDirty = true;
            break;

        // ── ENVOLVENTE ────────────────────────────────────────────────────────
        case CC_ENV_ATTACK: {
            float att = ccMap(value, 1.0f, 2000.0f);
            for (int i = 0; i < NUM_VOICES; i++) ampEnv[i].attack(att);
            g_displayDirty = true;
            break;
        }
        case CC_ENV_DECAY: {
            float dec = ccMap(value, 10.0f, 2000.0f);
            for (int i = 0; i < NUM_VOICES; i++) ampEnv[i].decay(dec);
            g_displayDirty = true;
            break;
        }
        case CC_ENV_SUSTAIN: {
            float sus = ccMap(value, 0.0f, 1.0f);
            for (int i = 0; i < NUM_VOICES; i++) ampEnv[i].sustain(sus);
            g_displayDirty = true;
            break;
        }
        case CC_ENV_RELEASE: {
            float rel = ccMap(value, 20.0f, 4000.0f);
            for (int i = 0; i < NUM_VOICES; i++) ampEnv[i].release(rel);
            moogParams[2].value = rel;
            junoParams[3].value = rel;
            g_displayDirty = true;
            break;
        }

        // ── EFECTOS ───────────────────────────────────────────────────────────
        case CC_REV_MIX: {
            g_reverbMix = ccMap(value, 0.0f, 1.0f);
            float dry   = 1.0f - g_reverbMix * 0.8f;
            reverbMixL.gain(0, dry);  reverbMixL.gain(1, g_reverbMix);
            reverbMixR.gain(0, dry);  reverbMixR.gain(1, g_reverbMix);
            g_displayDirty = true;
            break;
        }
        case CC_REV_SIZE:
            reverb.roomsize(ccMap(value, 0.0f, 1.0f));
            reverb.damping(1.0f - ccMap(value, 0.0f, 0.8f));
            g_displayDirty = true;
            break;

        case CC_DLY_TIME:
            g_delayTime = ccMap(value, 10.0f, 800.0f);
            delay1.delay(0, g_delayTime);
            g_displayDirty = true;
            break;

        case CC_DLY_FEEDBACK:
            g_delayFeedback = ccMap(value, 0.0f, 0.85f);
            delayMixL.gain(1, g_delayFeedback);
            delayMixR.gain(1, g_delayFeedback);
            g_displayDirty = true;
            break;

        // ── LFO ───────────────────────────────────────────────────────────────
        case CC_LFO_RATE:
            g_lfoRate = ccMap(value, 0.1f, 20.0f);
            g_displayDirty = true;
            break;

        case CC_LFO_DEPTH:
            g_lfoDepth = ccMap(value, 0.0f, 1.0f);
            g_displayDirty = true;
            break;

        case CC_LFO_DEST:
            g_lfoDest = (value < 43) ? 0 : (value < 85) ? 1 : 2;
            g_displayDirty = true;
            break;

        case CC_LFO_SHAPE:
            g_lfoShape = (value < 43) ? 0 : (value < 85) ? 1 : 2;
            g_displayDirty = true;
            break;

        // ── MOD WHEEL ─────────────────────────────────────────────────────────
        case 1:
            g_modCutoff    = 4000.0f * (value / 127.0f);
            g_displayDirty = true;
            break;
    }
}

// ─── Display ──────────────────────────────────────────────────────────────────
void drawDisplay()
{
    display.clearBuffer();

    // Nombre del sonido
    display.setFont(u8g2_font_9x15B_tf);
    const char* sname = (activeSound == SOUND_MOOG) ? "MOOG BASS" : "JUNO";
    int nameW = display.getStrWidth(sname);
    display.drawStr((128 - nameW) / 2, 13, sname);

    // Notas activas
    display.setFont(u8g2_font_6x10_tf);
    char notesBuf[32] = "";
    bool anyActive = false;
    for (int i = 0; i < NUM_VOICES; i++) {
        if (voices[i].active && voices[i].note >= 0) {
            char tmp[6];
            int oct = (voices[i].note / 12) - 1;
            snprintf(tmp, sizeof(tmp), "%s%d ",
                     NOTE_NAMES[voices[i].note % 12], oct);
            strncat(notesBuf, tmp, sizeof(notesBuf) - strlen(notesBuf) - 1);
            anyActive = true;
        }
    }
    display.drawStr(0, 26, anyActive ? notesBuf : "- - -");

    display.drawHLine(0, 30, 128);

    if (g_showEffects) {
        const char* lfoDestNames[]  = { "flt", "pch", "vol" };
        const char* lfoShapeNames[] = { "sin", "tri", "saw" };
        char l1[22], l2[22];
        snprintf(l1, sizeof(l1), "Rev:%.0f%% Dly:%.0fms",
                 g_reverbMix * 100.0f, g_delayTime);
        snprintf(l2, sizeof(l2), "LFO:%.1fHz>%s %s",
                 g_lfoRate, lfoDestNames[g_lfoDest], lfoShapeNames[g_lfoShape]);
        display.setFont(u8g2_font_6x10_tf);
        display.drawStr(0, 42, l1);
        display.drawStr(0, 54, l2);
    } else {
        Param* p = &currentParams()[activeParam];
        char line[24];
        if (activeParam == 1 || (activeParam == 2 && activeSound == SOUND_MOOG))
            snprintf(line, sizeof(line), ">%s %.2f", p->name, p->value);
        else
            snprintf(line, sizeof(line), ">%s %.0f", p->name, p->value);
        display.setFont(u8g2_font_6x10_tf);
        display.drawStr(0, 44, line);
        display.drawFrame(0, 48, 128, 8);
        int barW = (int)(((p->value - p->min) / (p->max - p->min)) * 124.0f);
        display.drawBox(2, 50, constrain(barW, 0, 124), 4);
    }

    // Indicadores de parametro (encoder)
    for (int i = 0; i < NUM_PARAMS; i++) {
        int dotX = 54 + i * 10;
        if (i == activeParam) display.drawBox(dotX, 60, 5, 4);
        else                  display.drawFrame(dotX, 60, 5, 4);
    }

    // Indicadores de voces activas
    for (int i = 0; i < NUM_VOICES; i++) {
        int vx = 104 + i * 7;
        if (voices[i].active) display.drawBox(vx, 60, 5, 4);
        else                  display.drawFrame(vx, 60, 5, 4);
    }

    display.sendBuffer();
}

// ─── Encoder ──────────────────────────────────────────────────────────────────
void handleEncoder()
{
    long encNow = enc.read();
    long diff   = encNow - encLast;

    if (abs(diff) >= 4) {
        int steps = diff / 4;
        encLast   = encNow - (diff % 4);
        Param& p  = currentParams()[activeParam];
        p.value   = constrain(p.value + steps * p.step, p.min, p.max);

        if (activeParam == 1) {
            ladderFilter.resonance(moogParams[1].value);
            svfFilter.resonance(constrain(junoParams[1].value, 0.7f, 5.0f));
        } else if (activeParam == 2 && activeSound == SOUND_MOOG) {
            for (int i = 0; i < NUM_VOICES; i++)
                ampEnv[i].release(moogParams[2].value);
        } else if (activeParam == 3 && activeSound == SOUND_JUNO) {
            for (int i = 0; i < NUM_VOICES; i++)
                ampEnv[i].release(junoParams[3].value);
        }

        g_showEffects  = false;
        g_displayDirty = true;
    }

    bool swNow = digitalRead(PIN_SW);
    if (swNow == LOW && swLast == HIGH && swDebounce > 30) {
        swDebounce = 0;
        activateSound(activeSound == SOUND_MOOG ? SOUND_JUNO : SOUND_MOOG);
    }
    swLast = swNow;
}

// ─── Handlers MIDI ────────────────────────────────────────────────────────────
void handleNoteOn(byte channel, byte note, byte velocity)
{
    if (velocity == 0) { handleNoteOff(channel, note, 0); return; }

    int   v    = allocVoice(note);
    float freq = midiNoteToFreq(note);
    float amp  = velocity / 127.0f;

    osc1[v].frequency(freq);
    osc1[v].amplitude(amp * OSC1_GAIN);

    if (activeSound == SOUND_MOOG) {
        osc2[v].frequency(freq * 0.5f);
        osc2[v].amplitude(amp * OSC2_GAIN);
    } else {
        osc2[v].frequency(freq * centsToRatio(junoParams[2].value));
        osc2[v].amplitude(amp * OSC2_GAIN);
    }

    ampEnv[v].noteOn();
    voices[v].note      = note;
    voices[v].active    = true;
    voices[v].startTime = millis();
    g_filterEnvPos      = 1.0f;
    g_displayDirty      = true;
}

void handleNoteOff(byte channel, byte note, byte velocity)
{
    int v = findVoice(note);
    if (v < 0) return;
    ampEnv[v].noteOff();
    voices[v].active = false;
    voices[v].note   = -1;
    g_displayDirty   = true;
}

// ─── Recepcion de parches desde ESP32 por UART ───────────────────────────────
void applyPatchFromJson(const String& json)
{
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) {
        Serial.println("[ESP32] JSON invalido");
        ESP_SERIAL.println("{\"error\":\"json invalido\"}");
        return;
    }

    // Sonido
    const char* sound = doc["sound"] | "MOOG";
    SoundId newSound  = (strcmp(sound, "JUNO") == 0) ? SOUND_JUNO : SOUND_MOOG;
    if (newSound != activeSound) activateSound(newSound);

    // Filtro
    if (doc["cutoff"].is<float>()) {
        moogParams[0].value = doc["cutoff"];
        junoParams[0].value = doc["cutoff"];
    }
    if (doc["resonance"].is<float>()) {
        moogParams[1].value = doc["resonance"];
        junoParams[1].value = doc["resonance"];
        ladderFilter.resonance(moogParams[1].value);
        svfFilter.resonance(constrain(junoParams[1].value, 0.7f, 5.0f));
    }
    if (doc["filterEnvAmt"].is<float>())
        moogParams[3].value = doc["filterEnvAmt"];

    // Envolvente
    float att = doc["attack"]  | (activeSound == SOUND_MOOG ? 4.0f  : 18.0f);
    float dec = doc["decay"]   | (activeSound == SOUND_MOOG ? 80.0f : 150.0f);
    float sus = doc["sustain"] | (activeSound == SOUND_MOOG ? 0.85f : 0.75f);
    float rel = doc["release"] | (activeSound == SOUND_MOOG ? 300.0f: 500.0f);
    for (int i = 0; i < NUM_VOICES; i++) {
        ampEnv[i].attack(att);
        ampEnv[i].decay(dec);
        ampEnv[i].sustain(sus);
        ampEnv[i].release(rel);
    }
    moogParams[2].value = rel;
    junoParams[3].value = rel;

    // Efectos
    if (doc["reverbMix"].is<float>()) {
        g_reverbMix = doc["reverbMix"];
        float dry   = 1.0f - g_reverbMix * 0.8f;
        reverbMixL.gain(0, dry);  reverbMixL.gain(1, g_reverbMix);
        reverbMixR.gain(0, dry);  reverbMixR.gain(1, g_reverbMix);
    }
    if (doc["reverbSize"].is<float>()) {
        reverb.roomsize(doc["reverbSize"].as<float>());
        reverb.damping(1.0f - doc["reverbSize"].as<float>() * 0.8f);
    }
    if (doc["delayTime"].is<float>()) {
        g_delayTime = doc["delayTime"];
        delay1.delay(0, g_delayTime);
    }
    if (doc["delayFeedback"].is<float>()) {
        g_delayFeedback = doc["delayFeedback"];
        delayMixL.gain(1, g_delayFeedback);
        delayMixR.gain(1, g_delayFeedback);
    }

    // LFO
    if (doc["lfoRate"].is<float>())  g_lfoRate  = doc["lfoRate"];
    if (doc["lfoDepth"].is<float>()) g_lfoDepth = doc["lfoDepth"];
    if (doc["lfoDest"].is<int>())    g_lfoDest  = doc["lfoDest"];
    if (doc["lfoShape"].is<int>())   g_lfoShape = doc["lfoShape"];

    g_displayDirty = true;

    // Muestra el nombre del parche en pantalla brevemente
    const char* pname = doc["name"] | "";
    if (strlen(pname) > 0) {
        display.clearBuffer();
        display.setFont(u8g2_font_9x15B_tf);
        display.drawStr(0, 24, "Parche cargado:");
        display.setFont(u8g2_font_6x10_tf);
        display.drawStr(0, 42, pname);
        display.sendBuffer();
        delay(1000);
        g_displayDirty = true;
    }

    // Confirma al ESP32
    ESP_SERIAL.print("{\"ok\":true,\"patch\":\"");
    ESP_SERIAL.print(pname);
    ESP_SERIAL.println("\"}");

    Serial.print("[ESP32] Parche aplicado: "); Serial.println(pname);
}

void readEspSerial()
{
    static String espBuf = "";
    while (ESP_SERIAL.available()) {
        char c = ESP_SERIAL.read();
        Serial.print(c); 
        if (c == '\n') {
            espBuf.trim();
            if (espBuf.length() > 0) applyPatchFromJson(espBuf);
            espBuf = "";
        } else {
            espBuf += c;
        }
    }
}

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);
    pinMode(PIN_SW, INPUT_PULLUP);

    Wire.begin();
    Wire.setClock(400000);
    display.begin();
    display.setContrast(200);

    display.clearBuffer();
    display.setFont(u8g2_font_9x15B_tf);
    display.drawStr(16, 28, "DualSynth Poly");
    display.setFont(u8g2_font_6x10_tf);
    display.drawStr(22, 46, "4 voces + FX");
    display.sendBuffer();
    delay(900);

    // Conexiones dinamicas osc → oscMix → ampEnv
    for (int i = 0; i < NUM_VOICES; i++) {
        posc1[i] = new AudioConnection(osc1[i],   0, oscMix[i], 0);
        posc2[i] = new AudioConnection(osc2[i],   0, oscMix[i], 1);
        pmix[i]  = new AudioConnection(oscMix[i], 0, ampEnv[i], 0);
    }

    AudioMemory(80);

    for (int i = 0; i < NUM_VOICES; i++)
        voiceMix.gain(i, 0.25f);

    // Reverb: empieza seco
    reverbMixL.gain(0, 1.0f);  reverbMixL.gain(1, 0.0f);
    reverbMixR.gain(0, 1.0f);  reverbMixR.gain(1, 0.0f);
    reverb.roomsize(0.5f);
    reverb.damping(0.5f);

    // Delay: empieza sin feedback
    delay1.delay(0, 200.0f);
    delayMixL.gain(0, 1.0f);  delayMixL.gain(1, 0.0f);
    delayMixR.gain(0, 1.0f);  delayMixR.gain(1, 0.0f);

    masterVol.gain(0.8f);

    activateSound(SOUND_MOOG);

    usbMIDI.setHandleNoteOn(handleNoteOn);
    usbMIDI.setHandleNoteOff(handleNoteOff);
    usbMIDI.setHandleControlChange(handleControlChange);

    MIDI_HW.begin(MIDI_CHANNEL_OMNI);
    MIDI_HW.setHandleNoteOn(handleNoteOn);
    MIDI_HW.setHandleNoteOff(handleNoteOff);
    MIDI_HW.setHandleControlChange(handleControlChange);

    Serial.println("Listo. 16 pots activos.");

    // Comunicacion con ESP32 C3 Mini
    ESP_SERIAL.begin(ESP_BAUD);
    Serial.println("[ESP32] Serial8 listo — Pin34(RX) Pin35(TX)");
}

// ─── Loop ─────────────────────────────────────────────────────────────────────
void loop()
{
    usbMIDI.read();
    MIDI_HW.read();
    readEspSerial();      // recibe parches del ESP32
    updateFilterCutoff();
    updateLFO();
    handleEncoder();

    if (g_displayDirty && displayTimer >= 50) {
        drawDisplay();
        g_displayDirty = false;
        displayTimer   = 0;
    }
}