#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

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

// --- Schedule ---
struct SchedulePoint {
  int hour;
  int minute;
  int brightness[NUM_CHANNELS]; // Brightness 0-100
};

SchedulePoint schedule[] = {
  { 6,  45, {5,  5,  5}},
  { 7,  0, {0, 0, 0}},
  { 16,  0, {50, 50, 50}},
  {18,  0, {100, 100, 100}},
  {20,  30, {30, 30, 30}},
  {21,  30, {5, 5, 5}},
  {22,  0, {0,  0,  0}},
  { 0,  0, {0,  0,  0}}
};
const int numSchedulePoints = sizeof(schedule) / sizeof(SchedulePoint);
bool manualOverride = false;
bool noonRebootFlag = false;

// --- Network & Web Server ---
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -25200); // PDT is UTC-7 (-25200 seconds)

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
        table {
            width: 100%%;
            border-collapse: collapse;
            margin-top: 15px;
            background: rgba(255,255,255,0.1);
            border-radius: 8px;
        }
        th, td {
            padding: 12px;
            border: 1px solid rgba(255,255,255,0.2);
        }
        th {
            background-color: rgba(0,0,0,0.2);
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
            <!-- Individual sliders are commented out as intended -->
        </div>
        <div class="schedule">
            <h2>24h Schedule</h2>
            %SCHEDULE%
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
            // Request initial state from server
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
                return; // Exit if JSON is invalid
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
            // Initialize its look based on its starting value
            updateSliderLook(master); 

            master.addEventListener('input', function() {
                updateSliderLook(this);
                sendSliderValue('m', this.value);
            });
            
            // The event listener for individual sliders is no longer needed
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
  // Scale 0-100 to PWM resolution (0-255)
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

  // Construct a valid JSON object
  String json = "{\"avg_brightness\":" + String(avg) + "}";
  
  ws.textAll(json);
}

// Handle incoming WebSocket messages
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    String message = (char*)data;
    
    // Check for a state request message
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
    // No 's' case needed as they are commented out
    
    notifyClients(); // Send updated state back to all clients
  }
}

// WebSocket event handler
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      // Send current state to the newly connected client
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

// Replace placeholders in the HTML
String processor(const String& var){
  if(var == "SCHEDULE"){
    String table = "<table><thead><tr><th>Time</th><th>Brightness</th></tr></thead><tbody>";

    for(int i=0; i<numSchedulePoints; i++){
      char timeStr[6];
      sprintf(timeStr, "%02d:%02d", schedule[i].hour, schedule[i].minute);
      table += "<tr><td>" + String(timeStr) + "</td>";
      int avg = 0;
      for (int j=0; j<NUM_CHANNELS; j++){
	      avg += schedule[i].brightness[j];
      }
      avg = (NUM_CHANNELS > 0) ? (avg / NUM_CHANNELS) : 0;
      table += "<td>" + String(avg) +"%</td></tr>";
    }
    table += "</tbody></table>";
    return table;
  }
  return String();
}

// Check schedule and update lights if not in manual override mode
void checkSchedule() {
  if (manualOverride) return;

  timeClient.update();
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();
  
  int lastScheduleIndex = -1;
  for (int i = 0; i < numSchedulePoints; i++) {
    if (currentHour > schedule[i].hour || (currentHour == schedule[i].hour && currentMinute >= schedule[i].minute)) {
      lastScheduleIndex = i;
    }
  }

  if (lastScheduleIndex == -1) {
    lastScheduleIndex = numSchedulePoints -1;
  }
  
  bool stateChanged = false;
  for (int i = 0; i < NUM_CHANNELS; i++) {
    if (brightness[i] != schedule[lastScheduleIndex].brightness[i]) {
      setBrightness(i, schedule[lastScheduleIndex].brightness[i]);
      stateChanged = true;
    }
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

  ws.onEvent(onEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", index_html, processor);
  });

  server.begin();
  timeClient.begin();
  ArduinoOTA.setHostname("esp32-light-controller");
  ArduinoOTA.begin();
}

void loop() {
  ArduinoOTA.handle();
  ws.cleanupClients();

  static unsigned long lastScheduleCheck = 0;
  if (millis() - lastScheduleCheck > 60000) {
    lastScheduleCheck = millis();
    checkSchedule();
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
