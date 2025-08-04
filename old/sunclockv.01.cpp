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
// These values are calculated in setup() after fetching sunset time
int rampUpStartTimeMinutes = 0;
int peakBrightTimeMinutes = 0;
int lightsOffTimeMinutes = 0;

bool manualOverride = false;
bool noonRebootFlag = false;

// --- Network & Web Server ---
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
WiFiUDP ntpUDP;
// Pacific Time is UTC-8, but with daylight saving it's UTC-7.
// PDT is UTC-7 (-25200 seconds). PST is UTC-8 (-28800).
// NTPClient handles DST automatically if the timezone rule is set correctly on the server side.
// For simplicity, we'll use a fixed offset. Adjust if needed.
NTPClient timeClient(ntpUDP, "pool.ntp.org", -25200); 

// --- Web Page HTML/CSS/JS (stored in PROGMEM to save RAM) -
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
            --slider-bg: #2a3d45;
            --thumb-color: #6dd5ed;
            --active-track-1: #2193b0;
            --active-track-2: #6dd5ed;
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
        .slider-group {
            margin-bottom: 15px;
        }
        label {
            display: block;
            margin-bottom: 10px;
            font-size: 1.2em;
        }
        input[type="range"] {
            -webkit-appearance: none;
            width: 80%%;
            height: 30px;
            background: var(--slider-bg);
            border-radius: 15px;
            outline: none;
            padding: 0;
            margin: 0;
            cursor: pointer;
        }
        input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none;
            appearance: none;
            width: 45px;
            height: 45px;
            background: var(--thumb-color);
            border-radius: 50%;
            border: 3px solid #fff;
            box-shadow: 0 0 8px rgba(109, 213, 237, 0.7);
        }
        input[type="range"]::-moz-range-thumb {
            width: 40px;
            height: 40px;
            background: var(--thumb-color);
            border-radius: 50%;
            border: 3px solid #fff;
        }
        .sky-gradient-circle {
            width: 150px;
            height: 150px;
            margin: 20px auto;
            border-radius: 50%;
            background: radial-gradient(circle at 50% 70%, #f7b733, #fc4a1a, #4a1a3a, #141e30);
            box-shadow: 0 0 20px rgba(252, 74, 26, 0.5);
            border: 3px solid rgba(255,255,255,0.3);
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>💡💫Cocina🍴💡</h1>
        <div class="sliders">
            <h2>Master Control</h2>
            <div class="slider-group">
                <label for="master">All Channels</label>
                <input type="range" min="0" max="100" value="0" class="slider" id="master">
            </div>
        </div>
        <div class="schedule">
            <h2>Sky Clock</h2>
            <div class="sky-gradient-circle"></div>
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
        }

        function updateSliderLook(slider) {
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
            updateSliderLook(master); 

            master.addEventListener('input', function() {
                updateSliderLook(this);
                sendSliderValue('m', this.value);
            });
        });
    </script>
</body>
</html>
)rawliteral";

// --- Helper Functions ---

// Function to set a channel's brightness
void setBrightness(int channel, int value) { // value is 0-100
  if (channel < 0 || channel >= NUM_CHANNELS) return;
  brightness[channel] = constrain(value, 0, 100);
  int pwmValue = map(brightness[channel], 0, 100, 0, PWM_MAX_VALUE);
  ledcWrite(channel, pwmValue);
}

// Send current brightness states to all connected web clients
void notifyClients() {
  int total_brightness = 0;
  for(int i=0; i < NUM_CHANNELS; i++) {
    total_brightness += brightness[i];
  }
  int avg = (NUM_CHANNELS > 0) ? (total_brightness / NUM_CHANNELS) : 0;
  String json = "{\"avg_brightness\":" + String(avg) + "}";
  ws.textAll(json);
}

// Handle incoming WebSocket messages
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    String message = (char*)data;
    
    if (message.equalsIgnoreCase("getState")) {
        notifyClients();
        return;
    }
    
    char id_char = message.charAt(0);
    int value = message.substring(message.indexOf(':') + 1).toInt();
    
    manualOverride = true; // Any slider move triggers manual override

    if (id_char == 'm') { // Master slider
      for (int i = 0; i < NUM_CHANNELS; i++) {
        setBrightness(i, value);
      }
    } 
    
    notifyClients();
  }
}

// WebSocket event handler
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
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

// --- New Sunset Schedule Functions ---

// Parses time string "H:MM:SS AM/PM" and returns minutes from midnight
int parseTime(String timeStr) {
  if (timeStr.length() < 8) return -1; // Invalid format

  int hour = timeStr.substring(0, timeStr.indexOf(":")).toInt();
  int minute = timeStr.substring(timeStr.indexOf(":") + 1, timeStr.lastIndexOf(":")).toInt();
  
  if (timeStr.indexOf("PM") > 0 && hour != 12) {
    hour += 12;
  }
  if (timeStr.indexOf("AM") > 0 && hour == 12) { // Midnight case (12 AM is 0 hour)
    hour = 0;
  }
  
  return hour * 60 + minute;
}

// Fetches sunset time and calculates the daily schedule
void getAndCalculateSchedule() {
  String sunsetTimeStr = "";
  HTTPClient http;
  
  // Your coordinates
  String url = "http://api.sunrisesunset.io/json?lat=33.020875887379724&lng=-117.13424892282217&timezone=PST&date=today";
  
  Serial.print("Fetching sunset time from: ");
  Serial.println(url);

  http.begin(url);
  int httpCode = http.GET();

  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      JsonDocument doc;
      deserializeJson(doc, payload);
      sunsetTimeStr = doc["results"]["sunset"].as<String>();
    }
  } else {
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();

  if (sunsetTimeStr != "") {
    Serial.print("Successfully fetched sunset time: ");
    Serial.println(sunsetTimeStr);
    peakBrightTimeMinutes = parseTime(sunsetTimeStr);
    
    // --- Parameters for the light curve ---
    rampUpStartTimeMinutes = peakBrightTimeMinutes - 90; // Start ramp 90 minutes before sunset
    lightsOffTimeMinutes = 22 * 60 + 30; // Lights off at 10:30 PM
    
    Serial.println("--- Daily Schedule Calculated ---");
    Serial.printf("Ramp Up Start: %02d:%02d\n", rampUpStartTimeMinutes / 60, rampUpStartTimeMinutes % 60);
    Serial.printf("Peak Brightness (Sunset): %02d:%02d\n", peakBrightTimeMinutes / 60, peakBrightTimeMinutes % 60);
    Serial.printf("Lights Off: %02d:%02d\n", lightsOffTimeMinutes / 60, lightsOffTimeMinutes % 60);
    Serial.println("-------------------------------");

  } else {
    Serial.println("Failed to fetch sunset time. Using fallback values.");
    // Fallback schedule if API fails
    peakBrightTimeMinutes = 19 * 60; // 7:00 PM
    rampUpStartTimeMinutes = peakBrightTimeMinutes - 90;
    lightsOffTimeMinutes = 22 * 60 + 30;
  }
}

// The brightness curve function.
// Takes progress (0.0 to 1.0) and returns brightness multiplier (0.0 to 1.0).
float curveFunction(float progress) {
  // Currently a linear curve.
  // Replace "return progress;" with a power function for an exponential curve, e.g.:
  // return pow(progress, 2.0); // '2.0' is the exponent, tweak for desired curve
  return progress;
}

// Checks current time and updates light brightness based on the curve
void updateLightsFromCurve() {
  if (manualOverride) return;

  timeClient.update();
  int currentMinutes = timeClient.getHours() * 60 + timeClient.getMinutes();

  int targetBrightness = 0;

  // Ramp up period
  if (currentMinutes >= rampUpStartTimeMinutes && currentMinutes < peakBrightTimeMinutes) {
    float progress = (float)(currentMinutes - rampUpStartTimeMinutes) / (peakBrightTimeMinutes - rampUpStartTimeMinutes);
    targetBrightness = curveFunction(progress) * 100.0;
  }
  // Ramp down period
  else if (currentMinutes >= peakBrightTimeMinutes && currentMinutes < lightsOffTimeMinutes) {
    float progress = (float)(currentMinutes - peakBrightTimeMinutes) / (lightsOffTimeMinutes - peakBrightTimeMinutes);
    targetBrightness = (1.0 - curveFunction(progress)) * 100.0; // Ramp down from 100 to 0
  }
  // Outside the active window
  else {
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
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP Address: ");
  Serial.println(WiFi.localIP());

  // Fetch sunset time and calculate schedule
  getAndCalculateSchedule();

  ws.onEvent(onEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
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
  // Update lights based on curve every 5 seconds
  if (millis() - lastUpdate > 5000) { 
    lastUpdate = millis();
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