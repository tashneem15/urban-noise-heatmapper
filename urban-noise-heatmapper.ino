#include <WiFi.h>
#include <WebServer.h>
#include <driver/i2s.h>
#include <math.h>

// ============================================================================
// CONFIGURATION & HARDWARE PIN MAP
// ============================================================================
const char* ssid     = "FAB_LAB_IUB_2.4G";
const char* password = "fabbersxiub";

// INMP441 Pinout (Standard I2S -> Assigned to I2S_NUM_1)
#define INMP_SCK  12
#define INMP_WS   11
#define INMP_SD   10

// PDM MEMS Pinout (Hardware PDM -> MUST BE I2S_NUM_0 on ESP32-S3)
#define PDM_CLK   42
#define PDM_DAT   41

#define SAMPLE_RATE 16000
#define BUFFER_LEN  512

WebServer server(80);

// Global Telemetry Variables
float inmp_db = 30.0;
float pdm_db = 30.0;
int inmp_rms = 0;
int pdm_rms = 0;
String noise_category = "Initializing...";

// ============================================================================
// EMBEDDED WEB DASHBOARD (PROGMEM HTML/JS)
// ============================================================================
const char HTML_INDEX[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en" class="dark">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Urban Noise Node - Sensor Comparison</title>

  <!-- Suppress Tailwind CDN production warning -->
  <script>
    const origWarn = console.warn;
    console.warn = function(...args) {
      if (args[0] && typeof args[0] === 'string' && args[0].includes('cdn.tailwindcss.com')) return;
      origWarn.apply(console, args);
    };
  </script>
  <script src="https://cdn.tailwindcss.com"></script>

  <!-- Chart.js v3.9.1 (ES6 compatible, prevents SyntaxError crashes) -->
  <script src="https://cdn.jsdelivr.net/npm/chart.js@3.9.1/dist/chart.min.js"></script>

  <style>
    .glass-card {
      background: rgba(17, 24, 39, 0.85);
      backdrop-filter: blur(12px);
      border: 1px solid rgba(255, 255, 255, 0.08);
    }
  </style>
</head>
<body class="bg-slate-950 text-slate-100 min-h-screen font-sans p-4 sm:p-8">

  <!-- Header -->
  <header class="max-w-7xl mx-auto mb-8 flex flex-col md:flex-row justify-between items-start md:items-center gap-4 border-b border-slate-800 pb-5">
    <div>
      <div class="flex items-center space-x-3">
        <span class="inline-block w-3 h-3 rounded-full bg-emerald-500 animate-pulse"></span>
        <h1 class="text-2xl sm:text-3xl font-extrabold tracking-tight text-white">Urban Noise Heatmap Node</h1>
      </div>
      <p class="text-sm text-slate-400 mt-1">Dual Digital MEMS Acoustic Sampling (ESP32-S3 Core)</p>
    </div>
    <div class="flex items-center gap-3">
      <span id="noiseBadge" class="px-3 py-1.5 rounded-full text-xs font-semibold uppercase tracking-wider bg-emerald-500/10 text-emerald-400 border border-emerald-500/30">
        Sampling Active
      </span>
    </div>
  </header>

  <main class="max-w-7xl mx-auto space-y-8">
    
    <!-- Real-time Level Cards -->
    <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-6">
      
      <!-- INMP441 Card -->
      <div class="glass-card rounded-2xl p-6">
        <div class="flex justify-between items-center mb-4">
          <h2 class="text-lg font-bold text-sky-400">INMP441 (I2S_1)</h2>
          <span class="text-xs bg-sky-500/20 text-sky-300 px-2 py-0.5 rounded font-mono">Standard I2S</span>
        </div>
        <div class="flex items-baseline space-x-2">
          <span id="inmpVal" class="text-5xl font-black text-white">--</span>
          <span class="text-lg text-slate-400 font-medium">dBA</span>
        </div>
        <div class="w-full bg-slate-800 rounded-full h-2.5 mt-4 overflow-hidden">
          <div id="inmpBar" class="bg-gradient-to-r from-sky-500 to-indigo-500 h-2.5 rounded-full transition-all duration-300" style="width: 0%"></div>
        </div>
        <p id="inmpRms" class="text-xs text-slate-400 mt-2">RMS Raw: --</p>
      </div>

      <!-- PDM Mic Card -->
      <div class="glass-card rounded-2xl p-6">
        <div class="flex justify-between items-center mb-4">
          <h2 class="text-lg font-bold text-purple-400">PDM MEMS (I2S_0)</h2>
          <span class="text-xs bg-purple-500/20 text-purple-300 px-2 py-0.5 rounded font-mono">Hardware PDM</span>
        </div>
        <div class="flex items-baseline space-x-2">
          <span id="pdmVal" class="text-5xl font-black text-white">--</span>
          <span class="text-lg text-slate-400 font-medium">dBA</span>
        </div>
        <div class="w-full bg-slate-800 rounded-full h-2.5 mt-4 overflow-hidden">
          <div id="pdmBar" class="bg-gradient-to-r from-purple-500 to-pink-500 h-2.5 rounded-full transition-all duration-300" style="width: 0%"></div>
        </div>
        <p id="pdmRms" class="text-xs text-slate-400 mt-2">RMS Raw: --</p>
      </div>

      <!-- Delta Analysis -->
      <div class="glass-card rounded-2xl p-6 md:col-span-2 lg:col-span-1">
        <h2 class="text-lg font-bold text-emerald-400 mb-4">Sensor Variance Delta</h2>
        <div class="flex items-baseline space-x-2">
          <span id="deltaVal" class="text-5xl font-black text-white">0.0</span>
          <span class="text-lg text-slate-400 font-medium">dB Diff</span>
        </div>
        <p class="text-xs text-slate-400 mt-3">Variance between dedicated silicon decimation (I2S) vs. ESP32-S3 internal PDM demodulator.</p>
      </div>

    </div>

    <!-- Animated Sound Spectrum Visualizer -->
    <div class="glass-card rounded-2xl p-6">
      <div class="flex justify-between items-center mb-3">
        <h3 class="text-md font-bold text-slate-300">Live Frequency Spectrum Simulator</h3>
        <span class="text-xs text-slate-500">Real-time Audio Energy</span>
      </div>
      <div id="spectrumBars" class="flex items-end justify-between h-28 gap-1 pt-4"></div>
    </div>

    <!-- Live Chart Stream -->
    <div class="glass-card rounded-2xl p-6">
      <h3 class="text-lg font-bold text-slate-200 mb-4">Acoustic Comparison Stream (Chart.js)</h3>
      <div class="relative h-72 w-full">
        <canvas id="noiseChart"></canvas>
      </div>
    </div>

  </main>

  <script>
    // Build 32 Animated Spectrum Visualizer Bars
    const spectrumContainer = document.getElementById('spectrumBars');
    const BAR_COUNT = 32;
    const barElements = [];
    for (let i = 0; i < BAR_COUNT; i++) {
      const bar = document.createElement('div');
      bar.className = 'w-full bg-gradient-to-t from-sky-500 via-indigo-500 to-purple-500 rounded-t transition-all duration-150';
      bar.style.height = '8%';
      spectrumContainer.appendChild(bar);
      barElements.push(bar);
    }

    // Chart.js v3.9.1 Setup
    const ctx = document.getElementById('noiseChart').getContext('2d');
    const chart = new Chart(ctx, {
      type: 'line',
      data: {
        labels: [],
        datasets: [
          {
            label: 'INMP441 (I2S_1) dBA',
            borderColor: '#38bdf8',
            backgroundColor: 'rgba(56, 189, 248, 0.1)',
            data: [],
            fill: true,
            tension: 0.3
          },
          {
            label: 'PDM Mic (I2S_0) dBA',
            borderColor: '#c084fc',
            backgroundColor: 'rgba(192, 132, 252, 0.1)',
            data: [],
            fill: true,
            tension: 0.3
          }
        ]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        scales: {
          x: { grid: { color: 'rgba(255,255,255,0.05)' }, ticks: { color: '#94a3b8' } },
          y: { min: 30, max: 110, grid: { color: 'rgba(255,255,255,0.05)' }, ticks: { color: '#94a3b8' } }
        },
        plugins: { legend: { labels: { color: '#cbd5e1' } } }
      }
    });

    // Telemetry Polling Loop
    async function updateDashboard() {
      try {
        const res = await fetch('/api/data?t=' + Date.now(), { cache: 'no-store' });
        if (!res.ok) throw new Error('HTTP status ' + res.status);
        
        const data = await res.json();

        // Update Numerical Displays
        document.getElementById('inmpVal').innerText = Number(data.inmp441_db).toFixed(1);
        document.getElementById('pdmVal').innerText = Number(data.pdm_db).toFixed(1);
        document.getElementById('deltaVal').innerText = Math.abs(data.inmp441_db - data.pdm_db).toFixed(1);

        // Update Progress Bars
        document.getElementById('inmpBar').style.width = Math.min(100, Math.max(0, ((data.inmp441_db - 30) / 80) * 100)) + '%';
        document.getElementById('pdmBar').style.width = Math.min(100, Math.max(0, ((data.pdm_db - 30) / 80) * 100)) + '%';

        document.getElementById('inmpRms').innerText = 'RMS Raw: ' + data.inmp441_rms;
        document.getElementById('pdmRms').innerText = 'RMS Raw: ' + data.pdm_rms;
        document.getElementById('noiseBadge').innerText = data.noise_category;

        // Push Data Points to Chart
        const now = new Date().toLocaleTimeString();
        if (chart.data.labels.length > 20) {
          chart.data.labels.shift();
          chart.data.datasets[0].data.shift();
          chart.data.datasets[1].data.shift();
        }
        chart.data.labels.push(now);
        chart.data.datasets[0].data.push(data.inmp441_db);
        chart.data.datasets[1].data.push(data.pdm_db);
        chart.update('none'); // Optimized redraw

        // Animate Visualizer Bars using INMP441 Energy
        barElements.forEach((bar) => {
          const baseHeight = ((data.inmp441_db - 30) / 80) * 100;
          const randomNoise = (Math.random() - 0.5) * 20;
          const h = Math.max(5, Math.min(100, baseHeight + randomNoise));
          bar.style.height = h + '%';
        });

      } catch (e) {
        console.error("API Fetch Error:", e);
      }
    }

    // Poll every 300ms
    setInterval(updateDashboard, 300);
  </script>
</body>
</html>
)rawliteral";

// ============================================================================
// HARDWARE DRIVER INITIALIZATION
// ============================================================================

// PDM Mic Hardware Initialization (MUST BE ON I2S_NUM_0)
bool initPDMMic() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = BUFFER_LEN,
    .use_apll = false
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = PDM_CLK,
    .ws_io_num = I2S_PIN_NO_CHANGE,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = PDM_DAT
  };

  esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  if (err != ESP_OK) return false;
  err = i2s_set_pin(I2S_NUM_0, &pin_config);
  return (err == ESP_OK);
}

// INMP441 Hardware Initialization (Assigned to I2S_NUM_1)
bool initINMP441() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = BUFFER_LEN,
    .use_apll = false
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = INMP_SCK,
    .ws_io_num = INMP_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = INMP_SD
  };

  esp_err_t err = i2s_driver_install(I2S_NUM_1, &i2s_config, 0, NULL);
  if (err != ESP_OK) return false;
  err = i2s_set_pin(I2S_NUM_1, &pin_config);
  return (err == ESP_OK);
}

// ============================================================================
// ACOUSTIC SAMPLING & RMS CALCULATIONS
// ============================================================================

void sampleMicrophones() {
  int16_t buffer[BUFFER_LEN];
  size_t bytes_read = 0;
  TickType_t timeout = pdMS_TO_TICKS(5); // 5ms non-blocking timeout

  // 1. Read PDM Mic from I2S_NUM_0
  if (i2s_read(I2S_NUM_0, &buffer, sizeof(buffer), &bytes_read, timeout) == ESP_OK && bytes_read > 0) {
    int samples = bytes_read / sizeof(int16_t);
    double sum_sq = 0;
    for (int i = 0; i < samples; i++) {
      sum_sq += (double)(buffer[i] * buffer[i]);
    }
    double rms = sqrt(sum_sq / samples);
    if (isnan(rms) || isinf(rms)) rms = 0;
    
    pdm_rms = (int)rms;
    double calculated_db = (20.0 * log10(rms > 1.0 ? rms : 1.0)) + 33.0;
    if (isnan(calculated_db) || isinf(calculated_db)) calculated_db = 30.0;
    pdm_db = constrain((float)calculated_db, 30.0f, 115.0f);
  }

  // 2. Read INMP441 Mic from I2S_NUM_1
  if (i2s_read(I2S_NUM_1, &buffer, sizeof(buffer), &bytes_read, timeout) == ESP_OK && bytes_read > 0) {
    int samples = bytes_read / sizeof(int16_t);
    double sum_sq = 0;
    for (int i = 0; i < samples; i++) {
      sum_sq += (double)(buffer[i] * buffer[i]);
    }
    double rms = sqrt(sum_sq / samples);
    if (isnan(rms) || isinf(rms)) rms = 0;

    inmp_rms = (int)rms;
    double calculated_db = (20.0 * log10(rms > 1.0 ? rms : 1.0)) + 35.0;
    if (isnan(calculated_db) || isinf(calculated_db)) calculated_db = 30.0;
    inmp_db = constrain((float)calculated_db, 30.0f, 115.0f);
  }

  // Environmental Noise Category
  float avg_db = (inmp_db + pdm_db) / 2.0f;
  if (avg_db < 45.0) noise_category = "Quiet (Residential)";
  else if (avg_db < 65.0) noise_category = "Moderate (Traffic)";
  else if (avg_db < 85.0) noise_category = "High Noise (Main Road)";
  else noise_category = "Hazardous Level";
}

// ============================================================================
// WEB SERVER HANDLERS
// ============================================================================

void handleRoot() { 
  server.send(200, "text/html", HTML_INDEX); 
}

void handleDataAPI() {
  // Prevent browser response caching
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.sendHeader("Access-Control-Allow-Origin", "*");

  // Build sanitized, guaranteed valid JSON
  String json = "{";
  json += "\"inmp441_db\":" + String(isnan(inmp_db) ? 30.0 : inmp_db, 1) + ",";
  json += "\"pdm_db\":" + String(isnan(pdm_db) ? 30.0 : pdm_db, 1) + ",";
  json += "\"inmp441_rms\":" + String(inmp_rms) + ",";
  json += "\"pdm_rms\":" + String(pdm_rms) + ",";
  json += "\"noise_category\":\"" + noise_category + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

// ============================================================================
// MAIN SETUP & LOOP
// ============================================================================

void setup() {
  Serial.begin(115200);

  // USB CDC Serial Wait Block (For ESP32-S3 Native USB)
  unsigned long startWait = millis();
  while (!Serial && (millis() - startWait < 3000)) {
    delay(10);
  }
  delay(500);

  Serial.println("\n--- ESP32-S3 BOOT SEQUENCE ---");

  // Hardware Drivers
  Serial.print("[1/4] Initializing PDM Mic (I2S_0)... ");
  if (initPDMMic()) Serial.println("OK");
  else Serial.println("FAILED");

  Serial.print("[2/4] Initializing INMP441 (I2S_1)... ");
  if (initINMP441()) Serial.println("OK");
  else Serial.println("FAILED");

  // WiFi Connection
  Serial.print("[3/4] Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // Prevents power-save brownout loops
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected!");
    Serial.print("[WiFi] Dashboard Address: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WiFi] Timed out! Operating in standalone mode.");
  }

  // Web Server Configuration
  Serial.println("[4/4] Starting Web Server...");
  server.on("/", handleRoot);
  server.on("/api/data", handleDataAPI);
  server.on("/favicon.ico", []() { server.send(204); }); // Suppress 404 favicon error
  server.begin();

  Serial.println("--- SETUP COMPLETE ---");
}

void loop() {
  server.handleClient();

  static unsigned long last_sample = 0;
  if (millis() - last_sample > 100) {
    sampleMicrophones();
    last_sample = millis();
  }

  delay(1); // Keep CPU task watchdog happy
}
