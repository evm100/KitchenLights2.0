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
	    %MASTER%
            <hr>
	    <!--
            <h2>Individual Channels</h2>
            %SLIDERS% 
	    -->
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
        }

        function onClose(event) {
            console.log('Connection closed');
            setTimeout(initWebSocket, 2000);
        }

        function onMessage(event) {
            console.log(event.data);
            let data = JSON.parse(event.data);
            // Update individual sliders based on server state
            for (let i = 0; i < %NUM_CHANNELS%; i++) {
                let sliderId = `s${i}`;
                let slider = document.getElementById(sliderId);
                if (slider) {
                    slider.value = data.brightness[i];
                    updateSliderLook(slider);
                }
           	 let sliderId = 'm';
	    	let slider = document.getElementById(sliderId);
	    	if slider(slider) {
			    slider.value = averageBrightness();
		   	 updateSliderLook(slider);
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
            
            // Master Slider
            let slider = document.getElementById('m');
            slider.addEventListener('input', function() {
                updateSliderLook(this);
                sendSliderValue('m', this.value);
            });
            
            // Individual Sliders
            for(let i=0; i<%NUM_CHANNELS%; i++) {
                let slider = document.getElementById(`s${i}`);
                slider.addEventListener('input', function() {
                    updateSliderLook(this);
                    sendSliderValue(`s${i}`, this.value);
                });
            }
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
  int avg = 0;
	String json = "{\"brightness\":[";
  for(int i=0; i < NUM_CHANNELS; i++) {
    json += String(brightness[i]);
    //if (i < NUM_CHANNELS - 1) 
    json += ",";
    avg += brightness[i];
  }
  avg = avg/NUM_CHANNELS;
  json += String(avg);
  json += "]}";
  ws.textAll(json);
}

// Handle incoming WebSocket messages
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    String message = (char*)data;
    
    char id_char = message.charAt(0);
    int value = message.substring(message.indexOf(':') + 1).toInt();
    
    manualOverride = true; // Any slider move triggers manual override

    if (id_char == 'm') { // Master slider
      for (int i = 0; i < NUM_CHANNELS; i++) {
        setBrightness(i, value);
      }
    } else if (id_char == 's') { // Individual slider
      int channel = message.substring(1, message.indexOf(':')).toInt();
      setBrightness(channel, value);
    }
    
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
  Serial.println("Processor called for: " + var);
  if(var == "MASTER"){
	  int avg = 0;
	  for(int i=0; i<NUM_CHANNELS; i++){
		  avg += brightness[i];
	  }
	  avg = avg/NUM_CHANNELS;
	  String s = "";
	s += "<div class='slider-group'>";
        s += "<input type='range' min='0' max='100' value='" + String(avg) + "' class='slider' id='m'>";
	s += "</div>";
  	Serial.println("Generated Master Slider HTML.");
	return s;
  }

  if(var == "SLIDERS"){
	  //String s = "";
    //for(int i=0; i<NUM_CHANNELS; i++){
    //  s += "<div class='slider-group'>";
    //  s += "<label for='s" + String(i) + "'>Channel " + String(i+1) + "</label>";
    //  s += "<input type='range' min='0' max='100' value='" + String(brightness[i]) + "' class='slider' id='s" + String(i) + "'>";
    //  s += "</div>";
    //}
    //Serial.println("Generated SLIDERS HTML.");
    //return s;
  }
  if(var == "SCHEDULE"){
    // --- CORRECTED TABLE GENERATION ---
    String table = "<table><thead><tr><th>Time</th>"; // Added <thead> for structure
    table += "<th>Brightness</th>";
    table += "</tr></thead><tbody>"; // Closed <thead>, opened <tbody>

    for(int i=0; i<numSchedulePoints; i++){
      char timeStr[6];
      sprintf(timeStr, "%02d:%02d", schedule[i].hour, schedule[i].minute);
      table += "<tr><td>" + String(timeStr) + "</td>";
     int avg = 0;
      for (int j=0; j<NUM_CHANNELS; j++){
	avg += schedule[i].brightness[j];
      }
      avg = avg/NUM_CHANNELS;
      table += "<td>" + String(avg) +"</td></tr>";
    }
    table += "</tbody></table>"; // Closed </tbody>
    return table;
  }
  if(var == "NUM_CHANNELS"){
      return String(NUM_CHANNELS);
  }
  return String();
}

// Check schedule and update lights if not in manual override mode
void checkSchedule() {
  if (manualOverride) return;

  timeClient.update();
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();
  
  // Find the most recent schedule point that has passed
  int lastScheduleIndex = -1;
  for (int i = 0; i < numSchedulePoints; i++) {
    if (currentHour > schedule[i].hour || (currentHour == schedule[i].hour && currentMinute >= schedule[i].minute)) {
      lastScheduleIndex = i;
    }
  }

  // Handle midnight wrap-around
  if (lastScheduleIndex == -1) {
    // If it's before the first schedule point of the day, use the last one from the previous day.
    lastScheduleIndex = numSchedulePoints -1;
  }
  
  // Apply the brightness from that schedule point
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

  // Configure PWM channels
  for (int i = 0; i < NUM_CHANNELS; i++) {
    ledcSetup(i, PWM_FREQ, PWM_RESOLUTION);
    ledcAttachPin(ledPins[i], i);
    setBrightness(i, 0); // Start with lights off
  }

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP Address: ");
  Serial.println(WiFi.localIP());

  // Initialize Web Sockets
  ws.onEvent(onEvent);
  server.addHandler(&ws);

  // Serve the main web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", index_html, processor);
  });


  // Start server
  server.begin();

  // Initialize time client
  timeClient.begin();

  // Initialize OTA
  ArduinoOTA.setHostname("esp32-light-controller");
  ArduinoOTA.onStart([]() { Serial.println("OTA Start"); });
  ArduinoOTA.onEnd([]() { Serial.println("\nOTA End"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); });
  ArduinoOTA.onError([](ota_error_t error) { Serial.printf("Error[%u]: ", error); });
  ArduinoOTA.begin();
}

void loop() {
  // Handle OTA updates
  ArduinoOTA.handle();

  // Clean up disconnected WebSocket clients
  ws.cleanupClients();

  // Check schedule every minute
  static unsigned long lastScheduleCheck = 0;
  if (millis() - lastScheduleCheck > 60000) {
    lastScheduleCheck = millis();
    checkSchedule();
  }
  
  // Daily Reboot at Noon
  timeClient.update();
  if (timeClient.getHours() == 12 && timeClient.getMinutes() == 0 && timeClient.getSeconds() == 0) {
      if(!noonRebootFlag) {
        Serial.println("Noon reboot triggered!");
        ESP.restart();
      }
  } else {
    noonRebootFlag = false; // Reset flag after noon has passed
  }
}
