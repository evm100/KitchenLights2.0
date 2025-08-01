// Import required libraries
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <time.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h> // New library for JSON

// --- Configuration ---
// WiFi Credentials
const char* ssid = "Verastegui";
const char* password = "6162988135";

// LED & PWM Configuration (Scalable)
const int NUM_CHANNELS = 4;
const int ledPins[NUM_CHANNELS] = {16, 17, 18, 19}; // GPIO pins for each channel
const int PWM_FREQ = 5000;    // PWM frequency in Hz
const int PWM_RESOLUTION = 8; // PWM resolution (8-bit = 0-255)
const int MAX_DUTY_CYCLE = (1 << PWM_RESOLUTION) - 1; // 255 for 8-bit

// Current brightness values (0-100) for each channel
int brightness[NUM_CHANNELS] = {0, 0, 0, 0};

// NTP (Time) Configuration
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -25200; // PDT: UTC -7 hours
const int daylightOffset_sec = 3600;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// --- 24-Hour Schedule Data Structure ---
struct SchedulePoint {
  int hour;
  int minute;
  int brightness[NUM_CHANNELS];
};

const SchedulePoint schedule[] = {
  {0,  0,  {0, 0, 0, 5}},    {6,  0,  {0, 0, 0, 5}},
  {7,  0,  {50, 60, 50, 40}}, {9,  0,  {90, 100, 90, 80}},
  {17, 0,  {90, 100, 90, 80}},{18, 30, {100, 100, 100, 90}},
  {21, 0,  {40, 50, 40, 30}}, {22, 30, {10, 10, 10, 15}},
  {23, 59, {0, 0, 0, 5}}
};
const int numSchedulePoints = sizeof(schedule) / sizeof(schedule[0]);
bool scheduleActive = true;

// --- Web Page HTML/CSS/JS (New Version) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <title>ESP32 Light Controller</title>
  <meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
  <style>
    @keyframes gradient {
      0% { background-position: 0% 50%; }
      50% { background-position: 100% 50%; }
      100% { background-position: 0% 50%; }
    }
    body { 
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif, "Apple Color Emoji", "Segoe UI Emoji", "Segoe UI Symbol";
      background: linear-gradient(-45deg, #0f2027, #203a43, #2c5364, #3498db);
      background-size: 400% 400%;
      animation: gradient 15s ease infinite;
      color: #ecf0f1; text-align: center; margin: 0; padding: 15px;
    }
    h2 { color: #fff; font-weight: 500; }
    .container { max-width: 800px; margin: 0 auto; }
    .slider-container { background: rgba(0,0,0,0.25); padding: 20px; border-radius: 12px; margin-bottom: 20px; box-shadow: 0 4px 15px rgba(0,0,0,0.2); }
    .slider {
      -webkit-appearance: none; width: 100%; height: 20px; border-radius: 10px;
      outline: none; transition: opacity .2s; cursor: pointer;
    }
    .slider::-webkit-slider-thumb { -webkit-appearance: none; appearance: none; width: 40px; height: 40px; border-radius: 50%; background: #fff; border: 4px solid #3498db; cursor: pointer; }
    .slider::-moz-range-thumb { width: 40px; height: 40px; border-radius: 50%; background: #fff; border: 4px solid #3498db; cursor: pointer; }
    label { display: block; margin-bottom: 15px; font-size: 1.4em; font-weight: 300; }
    span { color: #f1c40f; font-weight: 600; }
    #master-container { border: 2px solid #3498db; }
    #schedule-container {
      margin-top: 30px; background: rgba(0,0,0,0.25); padding: 15px; border-radius: 12px;
    }
    #schedule-table { border-collapse: collapse; width: 100%; }
    #schedule-table th, #schedule-table td { text-align: center; padding: 10px; border-bottom: 1px solid rgba(255,255,255,0.1); }
    #schedule-table th { color: #3498db; font-size: 1.1em; }
  </style>
</head>
<body>
  <div class="container">
    <h2>💡 Light Controller</h2>
    <div id="master-container" class="slider-container">
      <label for="masterSlider">Master Control: <span id="masterValue">0</span>%</label>
      <input type="range" min="0" max="100" value="0" class="slider" id="masterSlider">
    </div>
    <div id="channel-sliders"></div>
    <div id="schedule-container">
      <h3>📅 Schedule</h3>
      <table id="schedule-table"></table>
    </div>
  </div>

<script>
  const NUM_CHANNELS = 4;

  function updateLed(channel, value) {
    fetch(`/update?channel=${channel}&value=${value}`);
  }

  function updateSliderVisual(slider, value) {
    const percentage = (value - slider.min) / (slider.max - slider.min) * 100;
    slider.style.background = `linear-gradient(to right, #f1c40f 0%, #3498db ${percentage}%, #566573 ${percentage}%, #566573 100%)`;
  }

  // Generate individual channel sliders
  const channelContainer = document.getElementById('channel-sliders');
  for (let i = 1; i <= NUM_CHANNELS; i++) {
    const sliderDiv = document.createElement('div');
    sliderDiv.className = 'slider-container';
    sliderDiv.innerHTML = `
      <label for="ch${i}">Channel ${i}: <span id="ch${i}Value">0</span>%</label>
      <input type="range" min="0" max="100" value="0" class="slider" id="ch${i}">
    `;
    channelContainer.appendChild(sliderDiv);
  }

  // Attach event listeners
  for (let i = 1; i <= NUM_CHANNELS; i++) {
    const slider = document.getElementById(`ch${i}`);
    const sliderValue = document.getElementById(`ch${i}Value`);
    slider.addEventListener('input', (event) => {
      const value = event.target.value;
      sliderValue.textContent = value;
      updateSliderVisual(event.target, value);
    });
    slider.addEventListener('change', (event) => updateLed(i, event.target.value));
  }
  
  const masterSlider = document.getElementById('masterSlider');
  const masterValue = document.getElementById('masterValue');
  masterSlider.addEventListener('input', () => {
    const value = masterSlider.value;
    masterValue.textContent = value;
    updateSliderVisual(masterSlider, value);
    for (let i = 1; i <= NUM_CHANNELS; i++) {
      const slider = document.getElementById(`ch${i}`);
      slider.value = value;
      document.getElementById(`ch${i}Value`).textContent = value;
      updateSliderVisual(slider, value);
    }
  });
  masterSlider.addEventListener('change', () => {
     for (let i = 1; i <= NUM_CHANNELS; i++) {
       updateLed(i, masterSlider.value);
     }
  });

  // --- Functions to load data on page start ---
  async function populateStatus() {
    const response = await fetch('/status');
    const status = await response.json();
    for (let i = 1; i <= NUM_CHANNELS; i++) {
      const slider = document.getElementById(`ch${i}`);
      const value = status[`ch${i}`] || 0;
      slider.value = value;
      document.getElementById(`ch${i}Value`).textContent = value;
      updateSliderVisual(slider, value);
    }
  }

  async function populateSchedule() {
    const response = await fetch('/schedule');
    const schedule = await response.json();
    const table = document.getElementById('schedule-table');
    
    let headerHtml = '<thead><tr><th>Time</th>';
    for(let i=1; i<=NUM_CHANNELS; i++) headerHtml += `<th>Ch ${i}</th>`;
    headerHtml += '</tr></thead>';

    let bodyHtml = '<tbody>';
    schedule.forEach(point => {
        const time = String(point.hour).padStart(2, '0') + ':' + String(point.minute).padStart(2, '0');
        bodyHtml += `<tr><td>${time}</td>`;
        for(let i=0; i<NUM_CHANNELS; i++) {
            bodyHtml += `<td>${point.brightness[i]}%</td>`;
        }
        bodyHtml += '</tr>';
    });
    bodyHtml += '</tbody>';

    table.innerHTML = headerHtml + bodyHtml;
  }

  // Run on page load
  window.addEventListener('load', () => {
    populateStatus();
    populateSchedule();
  });
</script>
</body>
</html>
)rawliteral";


// --- Functions ---

void setLedBrightness(int channel, int newBrightness) {
  if (channel < 0 || channel >= NUM_CHANNELS) return;
  newBrightness = max(0, min(100, newBrightness));
  brightness[channel] = newBrightness;
  uint32_t duty = map(newBrightness, 0, 100, 0, MAX_DUTY_CYCLE);
  ledcWrite(channel, duty);
  Serial.printf("Channel %d set to %d%% (Duty: %d)\n", channel, newBrightness, duty);
}

void checkSchedule() {
  if (!scheduleActive) return;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  if (timeinfo.tm_hour == 12 && timeinfo.tm_min == 0 && timeinfo.tm_sec == 0) {
    Serial.println("Noon reboot initiated.");
    ESP.restart();
  }
  float currentTimeInMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  int prevPoint = 0;
  int nextPoint = 0;
  for (int i = 0; i < numSchedulePoints - 1; i++) {
    prevPoint = i;
    nextPoint = i + 1;
    float scheduleTime = schedule[nextPoint].hour * 60 + schedule[nextPoint].minute;
    if (currentTimeInMinutes < scheduleTime) break;
  }
  float prevTime = schedule[prevPoint].hour * 60 + schedule[prevPoint].minute;
  float nextTime = schedule[nextPoint].hour * 60 + schedule[nextPoint].minute;
  float factor = (nextTime > prevTime) ? (currentTimeInMinutes - prevTime) / (nextTime - prevTime) : 1.0;
  for (int i = 0; i < NUM_CHANNELS; i++) {
    int prevBrightness = schedule[prevPoint].brightness[i];
    int nextBrightness = schedule[nextPoint].brightness[i];
    int currentBrightness = prevBrightness + factor * (nextBrightness - prevBrightness);
    setLedBrightness(i, currentBrightness);
  }
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < NUM_CHANNELS; i++) {
    ledcSetup(i, PWM_FREQ, PWM_RESOLUTION);
    ledcAttachPin(ledPins[i], i);
  }

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi connected.");
  Serial.print("IP: "); Serial.println(WiFi.localIP());

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  ArduinoOTA.setHostname("esp32-lighting-controller");
  ArduinoOTA.begin();

  // --- Web Server Handlers (REWRITTEN using ArduinoJson) ---
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  server.on("/update", HTTP_GET, [] (AsyncWebServerRequest *request) {
    scheduleActive = false;
    if (request->hasParam("channel") && request->hasParam("value")) {
      int channel = request->getParam("channel")->value().toInt();
      int value = request->getParam("value")->value().toInt();
      setLedBrightness(channel - 1, value);
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  // REWRITTEN: Send current LED status as JSON
  server.on("/status", HTTP_GET, [=](AsyncWebServerRequest *request){
    JsonDocument doc; // Use JsonDocument instead of JSONVar
    for (int i = 0; i < NUM_CHANNELS; i++) {
      doc[String("ch") + (i + 1)] = brightness[i];
    }
    String jsonResponse;
    serializeJson(doc, jsonResponse);
    request->send(200, "application/json", jsonResponse);
  });

  // REWRITTEN: Send schedule as JSON
  server.on("/schedule", HTTP_GET, [=](AsyncWebServerRequest *request){
    JsonDocument doc;
    JsonArray scheduleArray = doc.to<JsonArray>();
    for (int i = 0; i < numSchedulePoints; i++) {
      JsonObject point = scheduleArray.createNestedObject();
      point["hour"] = schedule[i].hour;
      point["minute"] = schedule[i].minute;
      JsonArray bArray = point.createNestedArray("brightness");
      for (int j = 0; j < NUM_CHANNELS; j++) {
        bArray.add(schedule[i].brightness[j]);
      }
    }
    String jsonResponse;
    serializeJson(doc, jsonResponse);
    request->send(200, "application/json", jsonResponse);
  });

  server.begin();
}
