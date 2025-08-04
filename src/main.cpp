#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ⭐ UPDATE YOUR WIFI CREDENTIALS
const char* ssid = "Verastegui";
const char* password = "6162988135";

// --- Configuration ---
const int NUM_CHANNELS = 3;
// ⭐ UPDATE PINS FOR YOUR LED CHANNELS
uint8_t ledPins[NUM_CHANNELS] = {12, 25, 34}; 
int brightness[NUM_CHANNELS] = {0, 0, 0};

// PWM Properties
const int PWM_FREQ = 5000;
const int PWM_RESOLUTION = 8; // 8-bit resolution (0-255)
const int PWM_MAX_VALUE = 255;

// --- Sunset Schedule Parameters ---
int rampUpStartTimeMinutes = 0;
int peakBrightTimeMinutes = 0;
int lightsOffTimeMinutes = 0;

// --- Manual Control State ---
bool manualOverride = false;
bool manualLock = false;
unsigned long manualOverrideStartTime = 0;
const unsigned long MANUAL_OVERRIDE_TIMEOUT = 3600000; // 1 hour in milliseconds

bool noonRebootFlag = false;

// --- Network & Web Server ---
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -25200); // PDT is UTC-7

// --- Web Page HTML/CSS/JS (stored in PROGMEM) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Luces de Mama 🐣</title>
    <style>
        :root {
            --bg-grad-1: #000428;
            --bg-grad-2: #004e92;
            --text-color: #f0f0f0;
            --slider-bg: #455a64; /* A more visible dark grey for the track */
            --thumb-color: #6dd5ed;
            --active-track-1: #2193b0;
            --active-track-2: #6dd5ed;
            --lock-color: #f0f0f0;
        }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            background: linear-gradient(-45deg, var(--bg-grad-1), var(--bg-grad-2), #00305a, #005a8d);
            background-size: 400%% 400%%;
            animation: gradientBG 15s ease infinite;
            color: var(--text-color);
            margin: 0;
            padding: 20px;
            text-align: center;
        }
        @keyframes gradientBG {
            0%% { background-position: 0%% 50%%; }
            50%% { background-position: 100%% 50%%; }
            100%% { background-position: 0%% 50%%; }
        }
        h1, h2 {
            text-shadow: 2px 2px 4px rgba(0,0,0,0.5);
        }
        .container {
            max-width: 800px;
            margin: auto;
            background: rgba(0,0,0,0.3);
            padding: 20px;
            border-radius: 15px;
            box-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.37);
        }
        .sliders, .schedule {
            margin-bottom: 20px;
        }
        .slider-container {
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 15px;
        }
        .slider-group {
            flex-grow: 1;
        }
        label {
            display: block;
            margin-bottom: 10px;
            font-size: 1.2em;
        }
        
        /* --- NEW SLIDER STYLES --- */
        input[type="range"] {
            -webkit-appearance: none;
            appearance: none;
            width: 100%;
            height: 20px; /* This is the track height, thumb is larger */
            background: transparent; /* Input element is transparent, track is styled below */
            outline: none;
            cursor: pointer;
        }

        /* --- SLIDER THUMB --- */
        input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none;
            appearance: none;
            width: 40px; /* Larger thumb */
            height: 40px; /* Larger thumb */
            background: var(--thumb-color);
            border-radius: 50%;
            border: 3px solid #fff;
            box-shadow: 0 0 8px rgba(109, 213, 237, 0.7);
            /* Vertically center the thumb on the track */
            margin-top: -10px; /* (track height - thumb height) / 2 */
        }
        input[type="range"]::-moz-range-thumb {
            width: 35px;
            height: 35px;
            background: var(--thumb-color);
            border-radius: 50%;
            border: 3px solid #fff;
            border: none; /* FF adds a border, remove it */
        }

        /* --- SLIDER TRACK (RAILING) --- */
        input[type="range"]::-webkit-slider-runnable-track {
            width: 100%;
            height: 20px; /* Thicker track */
            background: var(--slider-bg); /* Solid background color */
            border-radius: 10px;
            border: 1px solid rgba(0,0,0,0.2);
        }
        input[type="range"]::-moz-range-track {
            width: 100%;
            height: 20px;
            background: var(--slider-bg);
            border-radius: 10px;
            border: 1px solid rgba(0,0,0,0.2);
        }
        /* --- END NEW SLIDER STYLES --- */

        .sky-gradient-circle {
            width: 150px;
            height: 150px;
            margin: 20px auto;
            border-radius: 50%;
            background: radial-gradient(circle at 50% 70%, #f7b733, #fc4a1a, #4a1a3a, #141e30);
            box-shadow: 0 0 20px rgba(252, 74, 26, 0.5);
            border: 3px solid rgba(255,255,255,0.3);
            cursor: pointer;
            transition: transform 0.2s ease;
        }
        .sky-gradient-circle:hover {
            transform: scale(1.05);
        }
        #lockButton {
            font-size: 2.5em;
            cursor: pointer;
            padding: 10px;
            color: var(--lock-color);
            transition: transform 0.2s ease, color 0.2s;
        }
        #lockButton:hover {
            transform: scale(1.1);
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>💫Cocina de Mamá🍴</h1>
        <div class="sliders">
            <h2>Master Control</h2>
            <div class="slider-container">
                <div class="slider-group">
                    <input type="range" min="0" max="100" value="0" class="slider" id="master">
                </div>
                <div id="lockButton">🔓</div>
            </div>
        </div>
        <div class="schedule">
            <h2>Sky Clock</h2>
            <div class="sky-gradient-circle" id="skyClock"></div>
        </div>
    </div>

    <script>
        let gateway = `ws://${window.location.hostname}/ws`;
        let websocket;

        function initWebSocket() {
            console.log('Trying to open a WebSocket connection...');
            websocket = new WebSocket(gateway);
            websocket.onopen = onOpen;
            websocket.onclose = onClose;
            websocket.onmessage = onMessage;
        }

        function onOpen(event) {
            console.log('Connection opened');
            websocket.send("getState");
        }

        function onClose(event) {
            console.log('Connection closed');
            setTimeout(initWebSocket, 2000);
        }

        function onMessage(event) {
            console.log("Received: " + event.data);
            let data;
            try {
                data = JSON.parse(event.data);
            } catch (e) {
                console.error("Failed to parse JSON:", e);
                return;
            }

            if (data.avg_brightness !== undefined) {
                let masterSlider = document.getElementById('master');
                if (masterSlider) {
                    masterSlider.value = data.avg_brightness;
                    updateSliderLook(masterSlider);
                }
            }
            
            if (data.locked !== undefined) {
                let lockButton = document.getElementById('lockButton');
                lockButton.innerHTML = data.locked ? '🔒' : '🔓';
                lockButton.style.color = data.locked ? 'var(--thumb-color)' : 'var(--lock-color)';
            }
        }

        function updateSliderLook(slider) {
            // This function applies a gradient to the INPUT element. In WebKit,
            // this gradient is drawn ON TOP of the track's solid background color.
            let percentage = (slider.value - slider.min) / (slider.max - slider.min) * 100;
            slider.style.background = `linear-gradient(to right, var(--active-track-1), var(--active-track-2) ${percentage}%%, var(--slider-bg) ${percentage}%%)`;
        }

        function sendSliderValue(id, value) {
            let msg = `${id}:${value}`;
            console.log(`Sending: ${msg}`);
            websocket.send(msg);
        }
        
        window.addEventListener('load', (event) => {
            initWebSocket();
            
            const master = document.getElementById('master');
            const skyClock = document.getElementById('skyClock');
            const lockButton = document.getElementById('lockButton');

            // Set the initial look of the slider when the page loads
            updateSliderLook(master); 

            master.addEventListener('input', function() {
                updateSliderLook(this);
                sendSliderValue('m', this.value);
            });

            skyClock.addEventListener('click', () => {
                console.log('Sky Clock clicked, resetting to auto.');
                websocket.send('reset');
            });

            lockButton.addEventListener('click', () => {
                console.log('Lock button clicked.');
                websocket.send('toggleLock');
            });
        });
    </script>
</body>
</html>
)rawliteral";

// --- Helper Functions ---

void setBrightness(int channel, int value) {
  if (channel < 0 || channel >= NUM_CHANNELS) return;
  brightness[channel] = constrain(value, 0, 100);
  int pwmValue = map(brightness[channel], 0, 100, 0, PWM_MAX_VALUE);
  ledcWrite(channel, pwmValue);
}

void notifyClients() {
  int total_brightness = 0;
  for(int i=0; i < NUM_CHANNELS; i++) {
    total_brightness += brightness[i];
  }
  int avg = (NUM_CHANNELS > 0) ? (total_brightness / NUM_CHANNELS) : 0;
  
  String json = "{\"avg_brightness\":" + String(avg) + ", \"locked\":" + (manualLock ? "true" : "false") + "}";
  ws.textAll(json);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    String message = (char*)data;
    
    if (message.equalsIgnoreCase("getState")) {
        notifyClients();
        return;
    }

    if (message.equalsIgnoreCase("reset")) {
        Serial.println("Received reset command from client.");
        manualOverride = false;
        manualLock = false;
        notifyClients();
        return;
    }

    if (message.equalsIgnoreCase("toggleLock")) {
        manualLock = !manualLock;
        Serial.printf("Manual lock toggled to: %s\n", manualLock ? "ON" : "OFF");
        if (manualLock) {
            manualOverride = true;
        } else {
            manualOverride = true; 
            manualOverrideStartTime = millis();
        }
        notifyClients();
        return;
    }
    
    char id_char = message.charAt(0);
    int value = message.substring(message.indexOf(':') + 1).toInt();
    
    if (id_char == 'm') {
      manualOverride = true;
      if (!manualLock) {
        manualOverrideStartTime = millis();
        Serial.println("Manual override activated. Timeout timer started.");
      } else {
        Serial.println("Manual override (LOCKED).");
      }
      for (int i = 0; i < NUM_CHANNELS; i++) {
        setBrightness(i, value);
      }
    } 
    
    notifyClients();
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      notifyClients();
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG: case WS_EVT_ERROR: break;
  }
}

// --- Sunset Schedule Functions ---

int parseTime(String timeStr) {
  if (timeStr.length() < 8) return -1;
  int hour = timeStr.substring(0, timeStr.indexOf(":")).toInt();
  int minute = timeStr.substring(timeStr.indexOf(":") + 1, timeStr.lastIndexOf(":")).toInt();
  if (timeStr.indexOf("PM") > 0 && hour != 12) hour += 12;
  if (timeStr.indexOf("AM") > 0 && hour == 12) hour = 0;
  return hour * 60 + minute;
}

void getAndCalculateSchedule() {
  HTTPClient http;
  String url = "http://api.sunrisesunset.io/json?lat=33.020875887379724&lng=-117.13424892282217&timezone=PST&date=today";
  Serial.print("Fetching sunset time from: "); Serial.println(url);
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    JsonDocument doc;
    deserializeJson(doc, payload);
    String sunsetTimeStr = doc["results"]["sunset"].as<String>();
    Serial.print("Successfully fetched sunset time: "); Serial.println(sunsetTimeStr);
    peakBrightTimeMinutes = parseTime(sunsetTimeStr);
    rampUpStartTimeMinutes = peakBrightTimeMinutes - 90;
    lightsOffTimeMinutes = 22 * 60 + 30; // 10:30 PM
    Serial.println("--- Daily Schedule Calculated ---");
    Serial.printf("Ramp Up Start: %02d:%02d\n", rampUpStartTimeMinutes / 60, rampUpStartTimeMinutes % 60);
    Serial.printf("Peak Brightness (Sunset): %02d:%02d\n", peakBrightTimeMinutes / 60, peakBrightTimeMinutes % 60);
    Serial.printf("Lights Off: %02d:%02d\n", lightsOffTimeMinutes / 60, lightsOffTimeMinutes % 60);
    Serial.println("-------------------------------");
  } else {
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    Serial.println("Using fallback values.");
    peakBrightTimeMinutes = 19 * 60;
    rampUpStartTimeMinutes = peakBrightTimeMinutes - 90;
    lightsOffTimeMinutes = 22 * 60 + 30;
  }
  http.end();
}

float curveFunction(float progress) {
  return progress; // Linear curve
}

void updateLightsFromCurve() {
  if (manualOverride) return;
  timeClient.update();
  int currentMinutes = timeClient.getHours() * 60 + timeClient.getMinutes();
  int targetBrightness = 0;
  if (currentMinutes >= rampUpStartTimeMinutes && currentMinutes < peakBrightTimeMinutes) {
    float progress = (float)(currentMinutes - rampUpStartTimeMinutes) / (peakBrightTimeMinutes - rampUpStartTimeMinutes);
    targetBrightness = curveFunction(progress) * 100.0;
  } else if (currentMinutes >= peakBrightTimeMinutes && currentMinutes < lightsOffTimeMinutes) {
    float progress = (float)(currentMinutes - peakBrightTimeMinutes) / (lightsOffTimeMinutes - peakBrightTimeMinutes);
    targetBrightness = (1.0 - curveFunction(progress)) * 100.0;
  } else {
    targetBrightness = 0;
  }
  bool stateChanged = false;
  if (brightness[0] != targetBrightness) {
    stateChanged = true;
  }
  for (int i = 0; i < NUM_CHANNELS; i++) {
    setBrightness(i, targetBrightness);
  }
  if (stateChanged) {
    notifyClients();
  }
}

void checkManualOverrideTimeout() {
  if (manualOverride && !manualLock) {
    if (millis() - manualOverrideStartTime > MANUAL_OVERRIDE_TIMEOUT) {
      Serial.println("Manual override timed out. Reverting to auto schedule.");
      manualOverride = false;
      notifyClients();
    }
  }
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < NUM_CHANNELS; i++) {
    ledcSetup(i, PWM_FREQ, PWM_RESOLUTION);
    ledcAttachPin(ledPins[i], i);
    setBrightness(i, 0);
  }
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nConnected! IP Address: "); Serial.println(WiFi.localIP());
  
  getAndCalculateSchedule();
  
  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", index_html);
  });
  server.begin();
  timeClient.begin();
  ArduinoOTA.setHostname("esp32-light-controller");
  ArduinoOTA.begin();
}

void loop() {
  ArduinoOTA.handle();
  ws.cleanupClients();

  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 5000) { 
    lastUpdate = millis();
    checkManualOverrideTimeout();
    updateLightsFromCurve();
  }
  
  timeClient.update();
  if (timeClient.getHours() == 12 && timeClient.getMinutes() == 0 && timeClient.getSeconds() == 0) {
      if(!noonRebootFlag) {
        Serial.println("Noon reboot triggered!");
        ESP.restart();
      }
  } else {
    noonRebootFlag = false;
  }
}
