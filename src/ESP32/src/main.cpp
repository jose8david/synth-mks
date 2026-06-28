// SynthPatchServer — ESP32 C3 Mini
// Servidor web WiFi para cargar y guardar parches del DualSynth Teensy 4.1
//
// Funcionalidad:
//   - Levanta un Access Point WiFi (SynthPatch / synthpatch)
//   - Sirve una web con editor de parches y sliders
//   - Guarda parches en flash (LittleFS) como JSON
//   - Envía parches al Teensy por UART (GPIO20 RX / GPIO21 TX)
//   - Recibe confirmaciones del Teensy
//
// Conexiones:
//   ESP32 C3 Mini GPIO20 (RX) ← Teensy Pin 35 (TX8)
//   ESP32 C3 Mini GPIO21 (TX) → Teensy Pin 34 (RX8)
//   GND                       → GND (masa comun obligatoria)
//
// platformio.ini para ESP32 C3 Mini:
//   [env:esp32-c3-mini]
//   platform  = espressif32
//   board     = esp32-c3-devkitm-1
//   framework = arduino
//   lib_deps  =
//       bblanchon/ArduinoJson
//   board_build.filesystem = littlefs
//   monitor_speed = 115200

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// ─── Configuracion WiFi AP ─────────────────────────────────────────────────────
static const char* AP_SSID     = "SynthPatch";
static const char* AP_PASSWORD = "synthpatch";  // minimo 8 caracteres

// ─── UART hacia Teensy ────────────────────────────────────────────────────────
// HardwareSerial en ESP32 C3: Serial0=USB, Serial1=configurable
#define TEENSY_SERIAL Serial1
#define TEENSY_RX     20
#define TEENSY_TX     21
#define TEENSY_BAUD   115200

// ─── Servidor web ─────────────────────────────────────────────────────────────
WebServer server(80);

// ─── Estructura de un parche ──────────────────────────────────────────────────
struct Patch {
    char   name[32];
    char   sound[8];   // "MOOG" o "JUNO"
    // Filtro
    float  cutoff;
    float  resonance;
    float  filterEnvAmt;
    float  filterDecay;
    // Envolvente
    float  attack;
    float  decay;
    float  sustain;
    float  release;
    // Efectos
    float  reverbMix;
    float  reverbSize;
    float  delayTime;
    float  delayFeedback;
    // LFO
    float  lfoRate;
    float  lfoDepth;
    int    lfoDest;   // 0=filtro 1=pitch 2=volumen
    int    lfoShape;  // 0=sine 1=tri 2=saw
};

// ─── Parches predefinidos ─────────────────────────────────────────────────────
const Patch BUILTIN_PATCHES[] = {
    {
        "Minimoog Bass", "MOOG",
        450.0f, 0.35f, 2000.0f, 350.0f,
        4.0f, 80.0f, 0.85f, 300.0f,
        0.0f, 0.5f, 0.0f, 0.0f,
        0.5f, 0.0f, 0, 0
    },
    {
        "Juno Pad", "JUNO",
        2200.0f, 0.6f, 500.0f, 300.0f,
        18.0f, 150.0f, 0.75f, 800.0f,
        0.4f, 0.7f, 0.0f, 0.0f,
        3.0f, 0.3f, 0, 0
    },
    {
        "Moog Lead", "MOOG",
        1200.0f, 0.7f, 3000.0f, 200.0f,
        8.0f, 100.0f, 0.6f, 200.0f,
        0.1f, 0.5f, 0.0f, 0.0f,
        5.0f, 0.4f, 0, 0
    },
    {
        "Juno Strings", "JUNO",
        3500.0f, 0.4f, 200.0f, 400.0f,
        60.0f, 200.0f, 0.8f, 1200.0f,
        0.5f, 0.8f, 200.0f, 0.2f,
        1.5f, 0.2f, 0, 0
    },
    {
        "Acid Bass", "MOOG",
        600.0f, 0.9f, 4000.0f, 150.0f,
        2.0f, 60.0f, 0.3f, 150.0f,
        0.0f, 0.5f, 0.0f, 0.0f,
        0.5f, 0.0f, 0, 0
    },
    {
        "Ambient Pad", "JUNO",
        4000.0f, 0.3f, 100.0f, 600.0f,
        400.0f, 300.0f, 0.9f, 2000.0f,
        0.7f, 0.9f, 400.0f, 0.4f,
        2.0f, 0.5f, 0, 0
    },
};
static const int NUM_BUILTIN = sizeof(BUILTIN_PATCHES) / sizeof(BUILTIN_PATCHES[0]);

// ─── Helpers JSON ─────────────────────────────────────────────────────────────
String patchToJson(const Patch& p)
{
    JsonDocument doc;
    doc["name"]          = p.name;
    doc["sound"]         = p.sound;
    doc["cutoff"]        = p.cutoff;
    doc["resonance"]     = p.resonance;
    doc["filterEnvAmt"]  = p.filterEnvAmt;
    doc["filterDecay"]   = p.filterDecay;
    doc["attack"]        = p.attack;
    doc["decay"]         = p.decay;
    doc["sustain"]       = p.sustain;
    doc["release"]       = p.release;
    doc["reverbMix"]     = p.reverbMix;
    doc["reverbSize"]    = p.reverbSize;
    doc["delayTime"]     = p.delayTime;
    doc["delayFeedback"] = p.delayFeedback;
    doc["lfoRate"]       = p.lfoRate;
    doc["lfoDepth"]      = p.lfoDepth;
    doc["lfoDest"]       = p.lfoDest;
    doc["lfoShape"]      = p.lfoShape;
    String out;
    serializeJson(doc, out);
    return out;
}

bool jsonToPatch(const String& json, Patch& p)
{
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return false;
    strlcpy(p.name,  doc["name"]  | "Sin nombre", sizeof(p.name));
    strlcpy(p.sound, doc["sound"] | "MOOG",        sizeof(p.sound));
    p.cutoff        = doc["cutoff"]        | 450.0f;
    p.resonance     = doc["resonance"]     | 0.35f;
    p.filterEnvAmt  = doc["filterEnvAmt"]  | 2000.0f;
    p.filterDecay   = doc["filterDecay"]   | 350.0f;
    p.attack        = doc["attack"]        | 4.0f;
    p.decay         = doc["decay"]         | 80.0f;
    p.sustain       = doc["sustain"]       | 0.85f;
    p.release       = doc["release"]       | 300.0f;
    p.reverbMix     = doc["reverbMix"]     | 0.0f;
    p.reverbSize    = doc["reverbSize"]    | 0.5f;
    p.delayTime     = doc["delayTime"]     | 0.0f;
    p.delayFeedback = doc["delayFeedback"] | 0.0f;
    p.lfoRate       = doc["lfoRate"]       | 2.0f;
    p.lfoDepth      = doc["lfoDepth"]      | 0.0f;
    p.lfoDest       = doc["lfoDest"]       | 0;
    p.lfoShape      = doc["lfoShape"]      | 0;
    return true;
}

// ─── LittleFS: guardar y cargar parches de usuario ────────────────────────────
String patchFilePath(const String& name)
{
    String safe = name;
    safe.replace(" ", "_");
    safe.replace("/", "-");
    return "/patches/" + safe + ".json";
}

bool savePatch(const Patch& p)
{
    if (!LittleFS.exists("/patches"))
        LittleFS.mkdir("/patches");
    File f = LittleFS.open(patchFilePath(p.name), "w");
    if (!f) return false;
    f.print(patchToJson(p));
    f.close();
    return true;
}

String listUserPatches()
{
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    if (LittleFS.exists("/patches")) {
        File dir = LittleFS.open("/patches");
        File entry = dir.openNextFile();
        while (entry) {
            if (!entry.isDirectory()) {
                String content = entry.readString();
                JsonDocument pd;
                if (deserializeJson(pd, content) == DeserializationError::Ok)
                    arr.add(pd["name"].as<String>());
            }
            entry = dir.openNextFile();
        }
    }
    String out;
    serializeJson(doc, out);
    return out;
}

// ─── Envio de parche al Teensy por UART ──────────────────────────────────────
void sendPatchToTeensy(const Patch& p)
{
    String json = patchToJson(p);
    TEENSY_SERIAL.println(json);
    Serial.print("[UART→Teensy] ");
    Serial.println(json);
}

// ─── HTML de la interfaz web ──────────────────────────────────────────────────
// Se sirve desde PROGMEM para no consumir heap
static const char HTML_PAGE[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>SynthPatch</title>
<style>
  :root {
    --bg:       #0d0d0f;
    --surface:  #17171c;
    --border:   #2a2a35;
    --accent:   #e8622a;
    --accent2:  #4ecdc4;
    --text:     #e8e6e0;
    --muted:    #7a7880;
    --moog:     #e8622a;
    --juno:     #4ecdc4;
    --font-mono: 'JetBrains Mono', 'Fira Mono', monospace;
    --font-ui:   'Inter', system-ui, sans-serif;
  }

  * { box-sizing: border-box; margin: 0; padding: 0; }

  body {
    background: var(--bg);
    color: var(--text);
    font-family: var(--font-ui);
    min-height: 100vh;
    padding: 0 0 60px;
  }

  /* ── Header ── */
  header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 18px 24px;
    border-bottom: 1px solid var(--border);
    position: sticky;
    top: 0;
    background: var(--bg);
    z-index: 10;
  }

  .logo {
    font-family: var(--font-mono);
    font-size: 1.1rem;
    letter-spacing: 0.15em;
    color: var(--text);
  }

  .logo span { color: var(--accent); }

  #status-dot {
    width: 8px; height: 8px;
    border-radius: 50%;
    background: #444;
    display: inline-block;
    margin-right: 8px;
    transition: background 0.3s;
  }
  #status-dot.ok  { background: #4caf50; }
  #status-dot.err { background: #f44336; }

  #status-text {
    font-size: 0.78rem;
    color: var(--muted);
    font-family: var(--font-mono);
  }

  /* ── Layout ── */
  .main { max-width: 900px; margin: 0 auto; padding: 24px 16px; }

  /* ── Sound selector ── */
  .sound-toggle {
    display: flex;
    gap: 8px;
    margin-bottom: 24px;
  }

  .sound-btn {
    flex: 1;
    padding: 12px;
    border: 1px solid var(--border);
    background: var(--surface);
    color: var(--muted);
    border-radius: 6px;
    cursor: pointer;
    font-family: var(--font-mono);
    font-size: 0.9rem;
    letter-spacing: 0.1em;
    transition: all 0.15s;
  }

  .sound-btn.moog.active {
    border-color: var(--moog);
    color: var(--moog);
    background: rgba(232,98,42,0.08);
  }

  .sound-btn.juno.active {
    border-color: var(--juno);
    color: var(--juno);
    background: rgba(78,205,196,0.08);
  }

  /* ── Preset list ── */
  .section-title {
    font-family: var(--font-mono);
    font-size: 0.72rem;
    letter-spacing: 0.2em;
    color: var(--muted);
    text-transform: uppercase;
    margin-bottom: 10px;
    margin-top: 24px;
  }

  .preset-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(160px, 1fr));
    gap: 8px;
    margin-bottom: 24px;
  }

  .preset-btn {
    padding: 10px 14px;
    border: 1px solid var(--border);
    background: var(--surface);
    color: var(--text);
    border-radius: 6px;
    cursor: pointer;
    font-size: 0.85rem;
    text-align: left;
    transition: border-color 0.15s;
  }

  .preset-btn:hover { border-color: var(--accent); }
  .preset-btn.active { border-color: var(--accent); color: var(--accent); }
  .preset-btn .preset-type {
    font-size: 0.65rem;
    font-family: var(--font-mono);
    color: var(--muted);
    display: block;
    margin-top: 3px;
  }

  /* ── Param groups ── */
  .groups {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 16px;
  }

  @media (max-width: 600px) { .groups { grid-template-columns: 1fr; } }

  .group {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 16px;
  }

  .group-title {
    font-family: var(--font-mono);
    font-size: 0.68rem;
    letter-spacing: 0.2em;
    color: var(--muted);
    text-transform: uppercase;
    margin-bottom: 14px;
  }

  /* ── Sliders ── */
  .param {
    margin-bottom: 14px;
  }

  .param-header {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    margin-bottom: 5px;
  }

  .param-label {
    font-size: 0.8rem;
    color: var(--text);
  }

  .param-value {
    font-family: var(--font-mono);
    font-size: 0.78rem;
    color: var(--accent);
    min-width: 52px;
    text-align: right;
  }

  input[type=range] {
    -webkit-appearance: none;
    width: 100%;
    height: 3px;
    background: var(--border);
    border-radius: 2px;
    outline: none;
  }

  input[type=range]::-webkit-slider-thumb {
    -webkit-appearance: none;
    width: 14px;
    height: 14px;
    border-radius: 50%;
    background: var(--accent);
    cursor: pointer;
    transition: transform 0.1s;
  }

  input[type=range]::-webkit-slider-thumb:hover { transform: scale(1.3); }

  /* Juno accent para el segundo sonido */
  body.juno-active input[type=range]::-webkit-slider-thumb { background: var(--juno); }
  body.juno-active .param-value { color: var(--juno); }

  /* ── Select ── */
  select {
    width: 100%;
    background: var(--bg);
    border: 1px solid var(--border);
    color: var(--text);
    padding: 6px 8px;
    border-radius: 4px;
    font-family: var(--font-mono);
    font-size: 0.8rem;
    margin-top: 4px;
  }

  /* ── Botones de accion ── */
  .actions {
    display: flex;
    gap: 10px;
    margin-top: 20px;
    flex-wrap: wrap;
  }

  .btn {
    padding: 10px 20px;
    border-radius: 6px;
    border: none;
    cursor: pointer;
    font-family: var(--font-mono);
    font-size: 0.82rem;
    letter-spacing: 0.08em;
    transition: opacity 0.15s;
  }

  .btn:hover { opacity: 0.85; }
  .btn-send  { background: var(--accent); color: #fff; }
  .btn-save  { background: transparent; border: 1px solid var(--accent); color: var(--accent); }
  .btn-name  { display: flex; align-items: center; gap: 8px; }

  input[type=text] {
    background: var(--surface);
    border: 1px solid var(--border);
    color: var(--text);
    padding: 8px 12px;
    border-radius: 6px;
    font-size: 0.85rem;
    width: 200px;
  }

  /* ── Toast ── */
  #toast {
    position: fixed;
    bottom: 24px;
    left: 50%;
    transform: translateX(-50%) translateY(80px);
    background: var(--surface);
    border: 1px solid var(--border);
    color: var(--text);
    padding: 10px 20px;
    border-radius: 6px;
    font-size: 0.85rem;
    font-family: var(--font-mono);
    transition: transform 0.25s cubic-bezier(.34,1.56,.64,1);
    z-index: 100;
  }

  #toast.show { transform: translateX(-50%) translateY(0); }
  #toast.ok   { border-color: #4caf50; color: #4caf50; }
  #toast.err  { border-color: #f44336; color: #f44336; }
</style>
</head>
<body>

<header>
  <div class="logo">SYNTH<span>PATCH</span></div>
  <div>
    <span id="status-dot"></span>
    <span id="status-text">desconectado</span>
  </div>
</header>

<div class="main">

  <!-- Sound selector -->
  <div class="sound-toggle">
    <button class="sound-btn moog active" onclick="setSound('MOOG')">MOOG BASS</button>
    <button class="sound-btn juno"        onclick="setSound('JUNO')">JUNO</button>
  </div>

  <!-- Presets predefinidos -->
  <div class="section-title">Parches predefinidos</div>
  <div class="preset-grid" id="builtin-presets"></div>

  <!-- Parches de usuario -->
  <div class="section-title">Mis parches</div>
  <div class="preset-grid" id="user-presets">
    <span style="color:var(--muted);font-size:0.8rem">No hay parches guardados</span>
  </div>

  <!-- Grupos de parametros -->
  <div class="section-title">Editor</div>
  <div class="groups">

    <div class="group">
      <div class="group-title">Filtro</div>
      <div class="param">
        <div class="param-header">
          <span class="param-label">Cutoff</span>
          <span class="param-value" id="v-cutoff">450 Hz</span>
        </div>
        <input type="range" id="cutoff" min="80" max="8000" step="10" value="450"
               oninput="updateSlider('cutoff','Hz')">
      </div>
      <div class="param">
        <div class="param-header">
          <span class="param-label">Resonancia</span>
          <span class="param-value" id="v-resonance">0.35</span>
        </div>
        <input type="range" id="resonance" min="0" max="1" step="0.01" value="0.35"
               oninput="updateSlider('resonance','')">
      </div>
      <div class="param">
        <div class="param-header">
          <span class="param-label">Env Amount</span>
          <span class="param-value" id="v-filterEnvAmt">2000 Hz</span>
        </div>
        <input type="range" id="filterEnvAmt" min="0" max="6000" step="50" value="2000"
               oninput="updateSlider('filterEnvAmt','Hz')">
      </div>
      <div class="param">
        <div class="param-header">
          <span class="param-label">Filter Decay</span>
          <span class="param-value" id="v-filterDecay">350 ms</span>
        </div>
        <input type="range" id="filterDecay" min="50" max="2000" step="10" value="350"
               oninput="updateSlider('filterDecay','ms')">
      </div>
    </div>

    <div class="group">
      <div class="group-title">Envolvente</div>
      <div class="param">
        <div class="param-header">
          <span class="param-label">Attack</span>
          <span class="param-value" id="v-attack">4 ms</span>
        </div>
        <input type="range" id="attack" min="1" max="2000" step="1" value="4"
               oninput="updateSlider('attack','ms')">
      </div>
      <div class="param">
        <div class="param-header">
          <span class="param-label">Decay</span>
          <span class="param-value" id="v-decay">80 ms</span>
        </div>
        <input type="range" id="decay" min="10" max="2000" step="5" value="80"
               oninput="updateSlider('decay','ms')">
      </div>
      <div class="param">
        <div class="param-header">
          <span class="param-label">Sustain</span>
          <span class="param-value" id="v-sustain">0.85</span>
        </div>
        <input type="range" id="sustain" min="0" max="1" step="0.01" value="0.85"
               oninput="updateSlider('sustain','')">
      </div>
      <div class="param">
        <div class="param-header">
          <span class="param-label">Release</span>
          <span class="param-value" id="v-release">300 ms</span>
        </div>
        <input type="range" id="release" min="20" max="4000" step="10" value="300"
               oninput="updateSlider('release','ms')">
      </div>
    </div>

    <div class="group">
      <div class="group-title">Efectos</div>
      <div class="param">
        <div class="param-header">
          <span class="param-label">Reverb Mix</span>
          <span class="param-value" id="v-reverbMix">0%</span>
        </div>
        <input type="range" id="reverbMix" min="0" max="1" step="0.01" value="0"
               oninput="updateSlider('reverbMix','%',100)">
      </div>
      <div class="param">
        <div class="param-header">
          <span class="param-label">Reverb Size</span>
          <span class="param-value" id="v-reverbSize">0.50</span>
        </div>
        <input type="range" id="reverbSize" min="0" max="1" step="0.01" value="0.5"
               oninput="updateSlider('reverbSize','')">
      </div>
      <div class="param">
        <div class="param-header">
          <span class="param-label">Delay Time</span>
          <span class="param-value" id="v-delayTime">0 ms</span>
        </div>
        <input type="range" id="delayTime" min="0" max="800" step="5" value="0"
               oninput="updateSlider('delayTime','ms')">
      </div>
      <div class="param">
        <div class="param-header">
          <span class="param-label">Delay Feedback</span>
          <span class="param-value" id="v-delayFeedback">0%</span>
        </div>
        <input type="range" id="delayFeedback" min="0" max="0.85" step="0.01" value="0"
               oninput="updateSlider('delayFeedback','%',100)">
      </div>
    </div>

    <div class="group">
      <div class="group-title">LFO</div>
      <div class="param">
        <div class="param-header">
          <span class="param-label">Rate</span>
          <span class="param-value" id="v-lfoRate">2.0 Hz</span>
        </div>
        <input type="range" id="lfoRate" min="0.1" max="20" step="0.1" value="2"
               oninput="updateSlider('lfoRate','Hz')">
      </div>
      <div class="param">
        <div class="param-header">
          <span class="param-label">Depth</span>
          <span class="param-value" id="v-lfoDepth">0%</span>
        </div>
        <input type="range" id="lfoDepth" min="0" max="1" step="0.01" value="0"
               oninput="updateSlider('lfoDepth','%',100)">
      </div>
      <div class="param">
        <span class="param-label">Destino</span>
        <select id="lfoDest" onchange="updateSelect()">
          <option value="0">Filtro</option>
          <option value="1">Pitch (vibrato)</option>
          <option value="2">Volumen (tremolo)</option>
        </select>
      </div>
      <div class="param" style="margin-top:12px">
        <span class="param-label">Forma</span>
        <select id="lfoShape" onchange="updateSelect()">
          <option value="0">Sine</option>
          <option value="1">Triangle</option>
          <option value="2">Sawtooth</option>
        </select>
      </div>
    </div>

  </div><!-- /groups -->

  <!-- Acciones -->
  <div class="actions">
    <button class="btn btn-send" onclick="sendPatch()">▶ Enviar al Teensy</button>
    <div class="btn-name">
      <input type="text" id="patch-name" placeholder="Nombre del parche" maxlength="31">
      <button class="btn btn-save" onclick="savePatch()">Guardar</button>
    </div>
  </div>

</div><!-- /main -->

<div id="toast"></div>

<script>
// ── Estado ────────────────────────────────────────────────────────────────────
let currentSound = 'MOOG';

const BUILTIN = [
  { name:'Minimoog Bass',  sound:'MOOG', cutoff:450,  resonance:0.35, filterEnvAmt:2000, filterDecay:350,  attack:4,   decay:80,  sustain:0.85, release:300,  reverbMix:0,   reverbSize:0.5, delayTime:0,   delayFeedback:0,    lfoRate:0.5, lfoDepth:0,   lfoDest:0, lfoShape:0 },
  { name:'Juno Pad',       sound:'JUNO', cutoff:2200, resonance:0.6,  filterEnvAmt:500,  filterDecay:300,  attack:18,  decay:150, sustain:0.75, release:800,  reverbMix:0.4, reverbSize:0.7, delayTime:0,   delayFeedback:0,    lfoRate:3,   lfoDepth:0.3, lfoDest:0, lfoShape:0 },
  { name:'Moog Lead',      sound:'MOOG', cutoff:1200, resonance:0.7,  filterEnvAmt:3000, filterDecay:200,  attack:8,   decay:100, sustain:0.6,  release:200,  reverbMix:0.1, reverbSize:0.5, delayTime:0,   delayFeedback:0,    lfoRate:5,   lfoDepth:0.4, lfoDest:0, lfoShape:0 },
  { name:'Juno Strings',   sound:'JUNO', cutoff:3500, resonance:0.4,  filterEnvAmt:200,  filterDecay:400,  attack:60,  decay:200, sustain:0.8,  release:1200, reverbMix:0.5, reverbSize:0.8, delayTime:200, delayFeedback:0.2,  lfoRate:1.5, lfoDepth:0.2, lfoDest:0, lfoShape:0 },
  { name:'Acid Bass',      sound:'MOOG', cutoff:600,  resonance:0.9,  filterEnvAmt:4000, filterDecay:150,  attack:2,   decay:60,  sustain:0.3,  release:150,  reverbMix:0,   reverbSize:0.5, delayTime:0,   delayFeedback:0,    lfoRate:0.5, lfoDepth:0,   lfoDest:0, lfoShape:0 },
  { name:'Ambient Pad',    sound:'JUNO', cutoff:4000, resonance:0.3,  filterEnvAmt:100,  filterDecay:600,  attack:400, decay:300, sustain:0.9,  release:2000, reverbMix:0.7, reverbSize:0.9, delayTime:400, delayFeedback:0.4,  lfoRate:2,   lfoDepth:0.5, lfoDest:0, lfoShape:0 },
];

// ── Helpers ───────────────────────────────────────────────────────────────────
function updateSlider(id, unit, mult) {
  const el  = document.getElementById(id);
  const val = parseFloat(el.value);
  const disp = mult ? Math.round(val * mult) + unit : val + (unit ? ' ' + unit : '');
  document.getElementById('v-' + id).textContent = disp;
}

function updateSelect() { /* valores recogidos al enviar */ }

function getParams() {
  return {
    sound:         currentSound,
    cutoff:        parseFloat(document.getElementById('cutoff').value),
    resonance:     parseFloat(document.getElementById('resonance').value),
    filterEnvAmt:  parseFloat(document.getElementById('filterEnvAmt').value),
    filterDecay:   parseFloat(document.getElementById('filterDecay').value),
    attack:        parseFloat(document.getElementById('attack').value),
    decay:         parseFloat(document.getElementById('decay').value),
    sustain:       parseFloat(document.getElementById('sustain').value),
    release:       parseFloat(document.getElementById('release').value),
    reverbMix:     parseFloat(document.getElementById('reverbMix').value),
    reverbSize:    parseFloat(document.getElementById('reverbSize').value),
    delayTime:     parseFloat(document.getElementById('delayTime').value),
    delayFeedback: parseFloat(document.getElementById('delayFeedback').value),
    lfoRate:       parseFloat(document.getElementById('lfoRate').value),
    lfoDepth:      parseFloat(document.getElementById('lfoDepth').value),
    lfoDest:       parseInt(document.getElementById('lfoDest').value),
    lfoShape:      parseInt(document.getElementById('lfoShape').value),
  };
}

function applyPatch(p) {
  setSound(p.sound);
  const fields = ['cutoff','resonance','filterEnvAmt','filterDecay',
                  'attack','decay','sustain','release',
                  'reverbMix','reverbSize','delayTime','delayFeedback',
                  'lfoRate','lfoDepth'];
  fields.forEach(f => {
    const el = document.getElementById(f);
    if (el) { el.value = p[f]; el.dispatchEvent(new Event('input')); }
  });
  document.getElementById('lfoDest').value  = p.lfoDest;
  document.getElementById('lfoShape').value = p.lfoShape;
  document.getElementById('patch-name').value = p.name || '';
}

function setSound(s) {
  currentSound = s;
  document.querySelectorAll('.sound-btn').forEach(b => b.classList.remove('active'));
  document.querySelector('.sound-btn.' + s.toLowerCase()).classList.add('active');
  document.body.classList.toggle('juno-active', s === 'JUNO');
}

function showToast(msg, type) {
  const t = document.getElementById('toast');
  t.textContent = msg;
  t.className = 'show ' + (type || '');
  setTimeout(() => { t.className = ''; }, 2500);
}

function setStatus(ok, text) {
  document.getElementById('status-dot').className = ok ? 'ok' : 'err';
  document.getElementById('status-text').textContent = text;
}

// ── API calls ─────────────────────────────────────────────────────────────────
async function sendPatch() {
  const p = getParams();
  p.name = document.getElementById('patch-name').value || 'sin nombre';
  try {
    const r = await fetch('/send', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(p)
    });
    if (r.ok) {
      showToast('Enviado al Teensy', 'ok');
      setStatus(true, 'conectado');
      highlightActive(p.name);
    } else {
      showToast('Error al enviar', 'err');
    }
  } catch(e) {
    showToast('Sin conexion con el ESP32', 'err');
    setStatus(false, 'sin respuesta');
  }
}

async function savePatch() {
  const p = getParams();
  p.name = document.getElementById('patch-name').value.trim();
  if (!p.name) { showToast('Pon un nombre al parche', 'err'); return; }
  try {
    const r = await fetch('/save', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(p)
    });
    if (r.ok) {
      showToast('Parche guardado', 'ok');
      loadUserPatches();
    } else {
      showToast('Error al guardar', 'err');
    }
  } catch(e) {
    showToast('Error de red', 'err');
  }
}

async function loadUserPatches() {
  try {
    const r   = await fetch('/patches');
    const arr = await r.json();
    const el  = document.getElementById('user-presets');
    if (!arr.length) {
      el.innerHTML = '<span style="color:var(--muted);font-size:0.8rem">No hay parches guardados</span>';
      return;
    }
    el.innerHTML = '';
    arr.forEach(name => {
      const btn = document.createElement('button');
      btn.className = 'preset-btn';
      btn.textContent = name;
      btn.onclick = () => loadUserPatch(name);
      el.appendChild(btn);
    });
  } catch(e) { /* sin LittleFS aun */ }
}

async function loadUserPatch(name) {
  try {
    const r = await fetch('/patch?name=' + encodeURIComponent(name));
    const p = await r.json();
    applyPatch(p);
    showToast('Parche cargado', 'ok');
  } catch(e) {
    showToast('Error al cargar', 'err');
  }
}

function highlightActive(name) {
  document.querySelectorAll('.preset-btn').forEach(b => {
    b.classList.toggle('active', b.textContent.trim() === name);
  });
}

// ── Init ──────────────────────────────────────────────────────────────────────
function buildBuiltinPresets() {
  const el = document.getElementById('builtin-presets');
  BUILTIN.forEach(p => {
    const btn = document.createElement('button');
    btn.className = 'preset-btn';
    btn.innerHTML = p.name + '<span class="preset-type">' + p.sound + '</span>';
    btn.onclick = () => { applyPatch(p); highlightActive(p.name); };
    el.appendChild(btn);
  });
}

buildBuiltinPresets();
loadUserPatches();

// Ping periodico para mostrar estado de conexion
setInterval(async () => {
  try {
    const r = await fetch('/ping');
    if (r.ok) setStatus(true, 'conectado');
  } catch(e) { setStatus(false, 'sin respuesta'); }
}, 5000);
</script>
</body>
</html>
)rawhtml";

// ─── Rutas del servidor web ───────────────────────────────────────────────────
void handleRoot()
{
    server.send_P(200, "text/html", HTML_PAGE);
}

void handlePing()
{
    server.send(200, "application/json", "{\"ok\":true}");
}

void handleSend()
{
    Serial.println("[handleSend] llamado");  // <-- añade esto
    Serial.println(server.arg("plain"));     // <-- y esto para ver el JSON
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }
    Patch p;
    if (!jsonToPatch(server.arg("plain"), p)) {
        server.send(400, "application/json", "{\"error\":\"json invalido\"}");
        return;
    }
    sendPatchToTeensy(p);
    server.send(200, "application/json", "{\"ok\":true}");
}

void handleSave()
{
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }
    Patch p;
    if (!jsonToPatch(server.arg("plain"), p)) {
        server.send(400, "application/json", "{\"error\":\"json invalido\"}");
        return;
    }
    if (savePatch(p)) {
        server.send(200, "application/json", "{\"ok\":true}");
    } else {
        server.send(500, "application/json", "{\"error\":\"error al guardar\"}");
    }
}

void handleListPatches()
{
    server.send(200, "application/json", listUserPatches());
}

void handleGetPatch()
{
    if (!server.hasArg("name")) {
        server.send(400, "application/json", "{\"error\":\"falta name\"}");
        return;
    }
    String path = patchFilePath(server.arg("name"));
    if (!LittleFS.exists(path)) {
        server.send(404, "application/json", "{\"error\":\"no encontrado\"}");
        return;
    }
    File f = LittleFS.open(path, "r");
    String content = f.readString();
    f.close();
    server.send(200, "application/json", content);
}

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);
    Serial.println("\n=== SynthPatch Server ===");

    // LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("[ERROR] LittleFS no arranca");
    } else {
        Serial.println("[LittleFS] OK");
    }

    // UART hacia Teensy
    TEENSY_SERIAL.begin(TEENSY_BAUD, SERIAL_8N1, TEENSY_RX, TEENSY_TX);
    Serial.println("[UART] Serial1 iniciado en GPIO20/21");

    // WiFi Access Point
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.print("[WiFi] AP: "); Serial.println(AP_SSID);
    Serial.print("[WiFi] IP: "); Serial.println(WiFi.softAPIP());

    // Rutas
    server.on("/",        HTTP_GET,  handleRoot);
    server.on("/ping",    HTTP_GET,  handlePing);
    server.on("/send",    HTTP_POST, handleSend);
    server.on("/save",    HTTP_POST, handleSave);
    server.on("/patches", HTTP_GET,  handleListPatches);
    server.on("/patch",   HTTP_GET,  handleGetPatch);

    server.begin();
    Serial.println("[HTTP] Servidor en http://192.168.4.1");
}

// ─── Loop ─────────────────────────────────────────────────────────────────────
void loop()
{
    server.handleClient();

    // Recibe confirmaciones del Teensy
    while (TEENSY_SERIAL.available()) {
        String line = TEENSY_SERIAL.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            Serial.print("[Teensy→ESP] "); Serial.println(line);
        }
    }
}