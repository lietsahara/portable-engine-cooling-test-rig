#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LittleFS.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <HTTPClient.h>//used only by testRenderHTTPS() diagnostic below

// Diagnostic: set to 1 to test plain HTTPS to the cloud host on every boot.
// Useful for diagnosing TLS errors (-80, -1). Set back to 0 for normal use.
#define DEBUG_HTTPS_TEST 0

// Pin definitions
#define DHTPIN 14
#define SENSOR1 4
#define SENSOR2 5
#define SENSOR3 7
#define SD_CS_PIN 10          

// Sensor objects 
OneWire oneWire1(SENSOR1);
OneWire oneWire2(SENSOR2);
OneWire oneWire3(SENSOR3);
DallasTemperature sensor1(&oneWire1);
DallasTemperature sensor2(&oneWire2);
DallasTemperature sensor3(&oneWire3);

// Readings 
float radiatorInlet = 0;
float radiatorOutlet = 0;
float engineInlet = 0;
float coolingRate = 0;
float radiatorTempDiff = 0;
float ambientTemp = NAN;
float rpm = NAN;
float fanSpeed = NAN;
float flowRate = NAN;
float heatDissipation = NAN;
float tempRateOfChange = NAN;

// Recording state
bool recording = false;
bool paused = false;
bool ramRecording = false;
bool sdAvailable = false;
bool interrupted = false;

// Restart state
bool pendingRestart = false;
unsigned long restartAt = 0;

// RAM buffer for RAM-only recording
#define RAM_BUFFER_SIZE 200
String ramBuffer[RAM_BUFFER_SIZE];
int ramBufferIndex = 0;
int ramBufferCount = 0;

// Wi-Fi / mode 
const char* default_ap_ssid = "IoT Network";
const char* default_ap_password = "1_Mpaka_8";

String ap_ssid = default_ap_ssid;
String ap_password = default_ap_password;
String sta_ssid = "";
String sta_password = "";
bool use_sta = false;
bool sta_connected = false;

// Cloud WebSocket
const char* cloud_ws_host = "portable-engine-cooling-test-rig.onrender.com"; // tells the esp32 the backend server to connect to
const int cloud_ws_port = 443;
const char* cloud_ws_path = "/ws/esp32";
WebSocketsClient cloudWs;
bool cloud_ws_connected = false;

//Local server
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Preferences 
Preferences prefs;

// Admin auth 
String adminPassword = "admin";
String sessionToken = "";
unsigned long sessionExpiry = 0;
const unsigned long SESSION_TIMEOUT = 3600000; // 1 hour

// Forward declarations
void initWebSocket();
void initCloudWebSocket();
void sendSensorData();
void handleCommand(JsonObject doc);
void loadPreferences();
void saveNetworkConfig(const char* ap_ssid_in, const char* ap_password_in,
                       const char* sta_ssid_in, const char* sta_password_in);
void startAPMode();
void trySTAMode();
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);
void writeToSD(String line);
void writeToRAM(String line);
String getTimestamp();
bool checkAuth(AsyncWebServerRequest *request);
void handleLogin(AsyncWebServerRequest *request);
void handleStartRecording(AsyncWebServerRequest *request);
void handlePauseRecording(AsyncWebServerRequest *request);
void handleStopRecording(AsyncWebServerRequest *request);
void handleSwitchMode(AsyncWebServerRequest *request);
void handleNetworkConfig(AsyncWebServerRequest *request);
void handleDownload(AsyncWebServerRequest *request);
void handleEnableRamRecording(AsyncWebServerRequest *request);
void handleChangePassword(AsyncWebServerRequest *request);

// HTML page
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <script src="/chart.js"></script>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: sans-serif; margin: 10px; }
    .chart-container { width: 100%; height: 300px; background: #f0f0f0; margin-bottom: 15px; }
    .status { font-weight: bold; }
    .admin-panel { border: 1px solid #ccc; padding: 10px; margin-top: 20px; display: none; }
    .admin-panel input, .admin-panel button { margin: 5px; }
  </style>
</head>
<body>
<h1>Engine Cooling Monitor</h1>
<p class="status">Status: <span id='apStatus'>--</span> | <span id='staStatus'>--</span> | <span id='cloudStatus'>--</span> | <span id='sdStatus'>--</span> | <span id='recStatus'>--</span></p>

<p>Radiator Inlet: <span id='radiatorInlet'>__</span> C</p>
<p>Radiator Outlet: <span id='radiatorOutlet'>__</span> C</p>
<p>Engine Inlet: <span id='engineInlet'>__</span> C</p>
<p>Cooling Rate: <span id='coolingRate'>__</span> C</p>
<p>Heat Dissipation Q: <span id='heatDissipation'>__</span> W</p>
<p>Radiator ΔT: <span id='radiatorTempDiff'>__</span> C</p>
<p>Temp Rate of Change: <span id='tempRateOfChange'>__</span> C/s</p>

<!-- Charts -->
<h2>Temperature vs Time</h2>
<div class="chart-container"><canvas id="tempVsTime"></canvas></div>
<h2>Heat Dissipation vs Time</h2>
<div class="chart-container"><canvas id="qVsTime"></canvas></div>
<h2>Heat Dissipation vs RPM</h2>
<div class="chart-container"><canvas id="qVsRpm"></canvas></div>
<h2>Radiator ΔT vs Time</h2>
<div class="chart-container"><canvas id="radiatorDeltaTVsTime"></canvas></div>
<h2>Cooling Rate vs Time</h2>
<div class="chart-container"><canvas id="coolingRateVsTime"></canvas></div>
<h2>Temp Rate of Change vs Time</h2>
<div class="chart-container"><canvas id="dTdtVsTime"></canvas></div>

<!-- Admin login -->
<div id="loginSection">
  <h3>Admin Login</h3>
  <input type="password" id="adminPassword" placeholder="Password">
  <button onclick="login()">Login</button>
  <p id="loginError" style="color:red;"></p>
</div>

<!-- Admin panel (hidden until login) -->
<div id="adminPanel" class="admin-panel">
  <h3>Admin Controls</h3>
  <button onclick="startRecording()">Start Recording</button>
  <button onclick="pauseRecording()">Pause</button>
  <button onclick="stopRecording()">Stop</button>
  <button onclick="switchMode('sta')">Switch to STA</button>
  <button onclick="switchMode('ap')">Switch to AP</button>
  <button onclick="enableRamRecording()">Enable RAM-only Recording</button>
  <button onclick="downloadData()">Download CSV</button>

  <h4>Change Admin Password</h4>
  <input type="password" id="oldPwd" placeholder="Current password">
  <input type="password" id="newPwd" placeholder="New password">
  <button onclick="changePassword()">Change Password</button>

  <h4>Network Configuration</h4>
  <input type="text" id="apSsid" placeholder="AP SSID (own hotspot)">
  <input type="password" id="apPwd" placeholder="AP password">
  <input type="text" id="staSsid" placeholder="STA SSID (router)">
  <input type="password" id="staPwd" placeholder="STA password">
  <button onclick="saveNetwork()">Save Network & Restart</button>

  <p id="adminMessage"></p>
</div>

<script>
let websocket = new WebSocket('ws://' + location.host + '/ws');

function createChart(id, yLabel, datasets) {
  const ctx = document.getElementById(id).getContext('2d');
  return new Chart(ctx, {
    type: 'line',
    data: {
      labels: [],
      datasets: datasets.map(ds => ({
        label: ds.label,
        data: [],
        borderWidth: 2,
        pointRadius: 1,
        tension: 0.2,
        borderColor: ds.color
      }))
    },
    options: {
      responsive: true,
      animation: false,
      scales: {
        x: { title: { display: true, text: 'Time / Reading' } },
        y: { title: { display: true, text: yLabel } }
      }
    }
  });
}

const tempVsTime = createChart('tempVsTime', '°C', [
  { label: 'Radiator Inlet', color: 'red' },
  { label: 'Radiator Outlet', color: 'blue' },
  { label: 'Engine Inlet', color: 'green' }
]);
const qVsTime = createChart('qVsTime', 'W', [{ label: 'Q', color: 'purple' }]);
const qVsRpm = createChart('qVsRpm', 'W', [{ label: 'Q', color: 'purple' }]);
const radiatorDeltaTVsTime = createChart('radiatorDeltaTVsTime', '°C', [{ label: 'ΔT', color: 'teal' }]);
const coolingRateVsTime = createChart('coolingRateVsTime', '°C', [{ label: 'Cooling Rate', color: 'orange' }]);
const dTdtVsTime = createChart('dTdtVsTime', '°C/s', [{ label: 'dT/dt', color: 'brown' }]);

function addPoint(chart, label, values) {
  chart.data.labels.push(label);
  values.forEach((v, i) => chart.data.datasets[i].data.push(v));
  if (chart.data.labels.length > 30) {
    chart.data.labels.shift();
    chart.data.datasets.forEach(ds => ds.data.shift());
  }
  chart.update();
}

websocket.onmessage = function(event) {
  let data = JSON.parse(event.data);

  document.getElementById('apStatus').textContent = 'AP: ' + (data.ap_active ? 'on' : 'off');
  document.getElementById('staStatus').textContent = 'STA: ' + (data.sta_connected ? 'connected' : 'off');
  document.getElementById('cloudStatus').textContent = 'Cloud: ' + (data.cloud_connected ? 'connected' : 'off');
  document.getElementById('sdStatus').textContent = 'SD: ' + (data.sd_available ? 'ok' : 'missing');
  document.getElementById('recStatus').textContent = 'Rec: ' + (data.recording ? (data.paused ? 'paused' : 'recording') : 'idle');

  document.getElementById('radiatorInlet').textContent = data.radiatorInlet;
  document.getElementById('radiatorOutlet').textContent = data.radiatorOutlet;
  document.getElementById('engineInlet').textContent = data.engineInlet;
  document.getElementById('coolingRate').textContent = data.coolingRate;
  document.getElementById('heatDissipation').textContent = data.heatDissipation;
  document.getElementById('radiatorTempDiff').textContent = data.radiatorTempDiff;
  document.getElementById('tempRateOfChange').textContent = data.tempRateOfChange;

  const label = new Date().toLocaleTimeString();
  addPoint(tempVsTime, label, [data.radiatorInlet, data.radiatorOutlet, data.engineInlet]);
  if (data.heatDissipation !== null) addPoint(qVsTime, label, [data.heatDissipation]);
  if (data.heatDissipation !== null && data.rpm !== null) addPoint(qVsRpm, data.rpm, [data.heatDissipation]);
  if (data.radiatorTempDiff !== null) addPoint(radiatorDeltaTVsTime, label, [data.radiatorTempDiff]);
  if (data.coolingRate !== null) addPoint(coolingRateVsTime, label, [data.coolingRate]);
  if (data.tempRateOfChange !== null) addPoint(dTdtVsTime, label, [data.tempRateOfChange]);
};

// ##### Admin JS #####

function getToken() { return localStorage.getItem('admin_token'); }
function setToken(t) { localStorage.setItem('admin_token', t); }
function clearToken() { localStorage.removeItem('admin_token'); }

function login() {
  const pwd = document.getElementById('adminPassword').value;
  fetch('/login', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: 'password=' + encodeURIComponent(pwd)
  }).then(res => res.json()).then(data => {
    if (data.token) {
      setToken(data.token);
      document.getElementById('loginSection').style.display = 'none';
      document.getElementById('adminPanel').style.display = 'block';
    } else {
      document.getElementById('loginError').textContent = 'Invalid password';
    }
  }).catch(() => document.getElementById('loginError').textContent = 'Login failed');
}

function adminRequest(url, params = {}) {
  const token = getToken();
  if (!token) return alert('Not logged in');
  const body = new URLSearchParams(params).toString();
  fetch(url, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/x-www-form-urlencoded',
      'Authorization': 'Bearer ' + token
    },
    body: body
  }).then(res => res.json()).then(data => {
    document.getElementById('adminMessage').textContent = JSON.stringify(data);
  }).catch(err => document.getElementById('adminMessage').textContent = 'Error: ' + err);
}

function startRecording() { adminRequest('/api/recording/start'); }
function pauseRecording() { adminRequest('/api/recording/pause'); }
function stopRecording() { adminRequest('/api/recording/stop'); }
function switchMode(mode) { adminRequest('/api/mode', {mode}); }
function enableRamRecording() { adminRequest('/api/recording/ram'); }

function downloadData() {
  const token = getToken();
  if (!token) return alert('Not logged in');
  window.location.href = '/download?token=' + token;
}

function changePassword() {
  const old_password = document.getElementById('oldPwd').value;
  const new_password = document.getElementById('newPwd').value;
  const token = getToken();
  if (!token) return alert('Not logged in');
  const body = new URLSearchParams({old_password, new_password}).toString();
  fetch('/api/change-password', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/x-www-form-urlencoded',
      'Authorization': 'Bearer ' + token
    },
    body: body
  }).then(res => res.json()).then(data => {
    document.getElementById('adminMessage').textContent = JSON.stringify(data);
    if (data.status && data.status.includes('changed')) {
      clearToken();
      setTimeout(() => location.reload(), 1500);
    }
  }).catch(err => {
    document.getElementById('adminMessage').textContent = 'Error: ' + err;
  });
}

function saveNetwork() {
  const ap_ssid = document.getElementById('apSsid').value;
  const ap_password = document.getElementById('apPwd').value;
  const sta_ssid = document.getElementById('staSsid').value;
  const sta_password = document.getElementById('staPwd').value;
  const token = getToken();
  if (!token) return alert('Not logged in');

  const body = new URLSearchParams({ap_ssid, ap_password, sta_ssid, sta_password}).toString();
  fetch('/api/network', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/x-www-form-urlencoded',
      'Authorization': 'Bearer ' + token
    },
    body: body
  }).then(res => res.json()).then(data => {
    document.getElementById('adminMessage').textContent = JSON.stringify(data);
    clearToken(); // ESP is about to reboot; token will be invalid anyway
    setTimeout(() => location.reload(), 4000);
  }).catch(err => {
    document.getElementById('adminMessage').textContent = 'Error: ' + err;
  });
}

// Validate any stored token against the server on page load
(async function init() {
  const token = getToken();
  if (!token) return;

  try {
    const res = await fetch('/api/recording/stop', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded',
        'Authorization': 'Bearer ' + token
      },
      body: ''
    });
    if (res.status === 401) {
      clearToken();
      return; // leave login form visible
    }
    document.getElementById('loginSection').style.display = 'none';
    document.getElementById('adminPanel').style.display = 'block';
  } catch (e) {
    clearToken();
  }
})();
</script>
</body>
</html>
)rawliteral";

// Local WebSocket event
//uses the EspAsyncWebServer library for the esp32 server (local browser connected to the esp32)
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
             AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.println("Local browser connected");
      break;
    case WS_EVT_DISCONNECT:
      Serial.println("Local browser disconnected");
      break;
    case WS_EVT_DATA:
      Serial.println("Data from local browser");
      break;
    case WS_EVT_ERROR:
      Serial.printf("WS error: %s\n", (char*)data);  // data is the error message here
      break;
    default:
      break;
  }
}

  //Testing HTTPS connection to the cloud server since i got a -80 error
  //##########
#if DEBUG_HTTPS_TEST
void testRenderHTTPS() {
  WiFiClientSecure client;
  client.setInsecure();  // diagnostic only not for use with real data

  HTTPClient https;
  https.begin(client, "https://portable-engine-cooling-test-rig.onrender.com/");
  int code = https.GET();
  Serial.printf("[HTTPS test] code=%d\n", code);
  https.end();

  // Optional: comparing with a known good host to tell "network blocks TLS"
  // from "network blocks Render specifically"
  https.begin(client, "https://www.google.com/");
  code = https.GET();
  Serial.printf("[HTTPS google.com] code=%d\n", code);
  https.end();
}
#endif
//############

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

// Cloud WebSocket event
//uses arduinoWebsockets library for the esp32 client connected to cloud server
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      cloud_ws_connected = false;
      Serial.println("[CloudWS] Disconnected");//CloudWs is a log type prefix to distinguish between local and cloud websocket logs
      break;
    case WStype_CONNECTED:
      cloud_ws_connected = true;
      Serial.println("[CloudWS] Connected");
      break;
    case WStype_TEXT: {
      Serial.printf("[CloudWS] Received: %.*s\n", (int)length, (const char*)payload);
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, payload, length);
      if (err) {
        Serial.print("[CloudWS] JSON parse failed: ");
        Serial.println(err.c_str());
        return;
      }
      if (!doc.is<JsonObject>()) {
        Serial.println("[CloudWS] Expected object,JSON is not an object");
        return;
      }
      JsonObject obj = doc.as<JsonObject>();
      handleCommand(obj);
      break;
    }
    case WStype_ERROR:
      Serial.println("[CloudWS] Error");
      break;
    case WStype_BIN://not using, will be ignored safely
      Serial.println("[CloudWS] Binary data received (not handled)");
      break;
    default:
      break;
  }
}

void initCloudWebSocket() {
  cloudWs.beginSSL(cloud_ws_host, cloud_ws_port, cloud_ws_path);
  cloudWs.onEvent(webSocketEvent);
  cloudWs.setReconnectInterval(5000);
  cloudWs.enableHeartbeat(60000, 3000, 2);//wait 60 seconds before sending a ping, wait 3 seconds for a pong, and retry 2 times before disconnecting i.e to detect silent deaths
}

// Cloud command handling
void handleCommand(JsonObject doc) {
  const char* cmd = doc["command"];//pointer to doc address
  if (!cmd) return;

  Serial.print("[CloudWS] Command: ");
  Serial.println(cmd);

  if (strcmp(cmd, "recording_start") == 0) {
    recording = true;
    paused = false;
    prefs.putBool("was_recording", true);
    Serial.println("Recording started");
  } else if (strcmp(cmd, "recording_pause") == 0) {
    paused = true;
    Serial.println("Recording paused");
  } else if (strcmp(cmd, "recording_stop") == 0) {
    recording = false;
    paused = false;
    prefs.putBool("was_recording", false);
    Serial.println("Recording stopped");
  } else if (strcmp(cmd, "new_project") == 0) {
    const char* name = doc["name"] | "unnamed";
    bool save_sd = doc["save_sd"] | true;
    bool save_db = doc["save_db"] | true;
    Serial.printf("New project: %s (SD=%d, DB=%d)\n", name, save_sd, save_db);
  } else if (strcmp(cmd, "restart") == 0) {
    Serial.println("Restarting...");
    pendingRestart = true;
    restartAt = millis() + 1000;
  } else if (strcmp(cmd, "switch_mode") == 0) {
    const char* mode = doc["mode"];
    prefs.putBool("use_sta", mode && strcmp(mode, "sta") == 0);
    Serial.println("Switching mode and restarting...");
    pendingRestart = true;
    restartAt = millis() + 1000;
  } else if (strcmp(cmd, "network_config") == 0) {
    const char* ap_ssid_in = doc["ap_ssid"] | "";
    const char* ap_password_in = doc["ap_password"] | "";
    const char* sta_ssid_in = doc["sta_ssid"] | "";
    const char* sta_password_in = doc["sta_password"] | "";
    saveNetworkConfig(ap_ssid_in, ap_password_in, sta_ssid_in, sta_password_in);
    Serial.println("Network config saved. Restarting...");
    pendingRestart = true;
    restartAt = millis() + 1000;
  }
}

// Preferences
void loadPreferences() {
  ap_ssid = prefs.getString("ap_ssid", default_ap_ssid);
  ap_password = prefs.getString("ap_password", default_ap_password);
  sta_ssid = prefs.getString("sta_ssid", "");
  sta_password = prefs.getString("sta_password", "");
  use_sta = prefs.getBool("use_sta", false);
  adminPassword = prefs.getString("admin_pwd", "can'tCatchMe");
  interrupted = prefs.getBool("was_recording", false);
}

void saveNetworkConfig(const char* ap_ssid_in, const char* ap_password_in,
                       const char* sta_ssid_in, const char* sta_password_in) {
  if (strlen(ap_ssid_in) > 0) prefs.putString("ap_ssid", ap_ssid_in);
  if (strlen(ap_password_in) > 0) prefs.putString("ap_password", ap_password_in);
  if (strlen(sta_ssid_in) > 0) prefs.putString("sta_ssid", sta_ssid_in);
  if (strlen(sta_password_in) > 0) prefs.putString("sta_password", sta_password_in);
}

// Wi-Fi modes
void startAPMode() {
  WiFi.softAP(ap_ssid.c_str(), ap_password.c_str());
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("AP SSID: ");
  Serial.println(ap_ssid);
}

void trySTAMode() {
  if (!use_sta || sta_ssid.length() == 0) {
    Serial.println("STA not requested. Running as AP-only server.");
    sta_connected = false;
    return;
  }

  Serial.print("Connecting to STA");
  WiFi.begin(sta_ssid.c_str(), sta_password.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    sta_connected = true;
    Serial.println("\nSTA connected. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    sta_connected = false;
    Serial.println("\nSTA failed. Staying AP-only.");
  }
}

// SD helpers
String getTimestamp() {
  return String(millis());
}

void writeToSD(String line) {
  if (!sdAvailable) return;
  File file = SD.open("/experiment.csv", FILE_APPEND);
  if (file) {
    file.println(line);
    file.close();
  } else {
    Serial.println("Failed to open SD file for append");
  }
}

void writeToRAM(String line) {
  ramBuffer[ramBufferIndex] = line;
  ramBufferIndex = (ramBufferIndex + 1) % RAM_BUFFER_SIZE;
  if (ramBufferCount < RAM_BUFFER_SIZE) ramBufferCount++;
}

// Admin auth
bool checkAuth(AsyncWebServerRequest *request) {
  if (sessionToken == "") return false;
  if (millis() > sessionExpiry) {
    sessionToken = "";
    return false;
  }
  String token = "";
  if (request->hasHeader("Authorization")) {
    token = request->getHeader("Authorization")->value();
    if (token.startsWith("Bearer ")) token = token.substring(7);
  } else if (request->hasParam("token")) {
    token = request->getParam("token")->value();
  }
  return token == sessionToken;
}

void handleLogin(AsyncWebServerRequest *request) {
  if (!request->hasParam("password", true)) {
    request->send(400, "application/json", "{\"error\":\"missing password\"}");
    return;
  }
  String pwd = request->getParam("password", true)->value();
  if (pwd == adminPassword) {
    sessionToken = String(random(0x7FFFFFFF), HEX);
    sessionExpiry = millis() + SESSION_TIMEOUT;
    request->send(200, "application/json", "{\"token\":\"" + sessionToken + "\"}");
  } else {
    request->send(401, "application/json", "{\"error\":\"invalid password\"}");
  }
}

void handleStartRecording(AsyncWebServerRequest *request) {
  if (!checkAuth(request)) {
    request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return;
  }
  if (!sdAvailable && !ramRecording) {
    request->send(400, "application/json", "{\"error\":\"no SD card and RAM recording not enabled\"}");
    return;
  }
  recording = true;
  paused = false;
  prefs.putBool("was_recording", true);
  request->send(200, "application/json", "{\"status\":\"recording started\"}");
}

void handlePauseRecording(AsyncWebServerRequest *request) {
  if (!checkAuth(request)) {
    request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return;
  }
  paused = true;
  request->send(200, "application/json", "{\"status\":\"paused\"}");
}

void handleStopRecording(AsyncWebServerRequest *request) {
  if (!checkAuth(request)) {
    request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return;
  }
  recording = false;
  paused = false;
  prefs.putBool("was_recording", false);
  request->send(200, "application/json", "{\"status\":\"stopped\"}");
}

void handleSwitchMode(AsyncWebServerRequest *request) {
  if (!checkAuth(request)) {
    request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return;
  }
  if (!request->hasParam("mode", true)) {
    request->send(400, "application/json", "{\"error\":\"missing mode\"}");
    return;
  }
  String mode = request->getParam("mode", true)->value();
  prefs.putBool("use_sta", mode == "sta");
  request->send(200, "application/json", "{\"status\":\"switching mode\"}");
   // Don't restart inside the handler - let loop() do it
   //having delay (200) ESP.restart() inside the handler blocks the server for 200ms then reboots mid-handler
  pendingRestart = true;
  restartAt = millis() + 1000;
}

void handleNetworkConfig(AsyncWebServerRequest *request) {
  if (!checkAuth(request)) {
    request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return;
  }

  prefs.begin("engine", false);

  bool staProvided = false;

  if (request->hasParam("ap_ssid", true)) {
    String v = request->getParam("ap_ssid", true)->value();
    if (v.length() > 0) prefs.putString("ap_ssid", v);
  }
  if (request->hasParam("ap_password", true)) {
    String v = request->getParam("ap_password", true)->value();
    if (v.length() > 0) prefs.putString("ap_password", v);
  }
  if (request->hasParam("sta_ssid", true)) {
    String v = request->getParam("sta_ssid", true)->value();
    if (v.length() > 0) {
      prefs.putString("sta_ssid", v);
      staProvided = true;
    }
  }
  if (request->hasParam("sta_password", true)) {
    String v = request->getParam("sta_password", true)->value();
    if (v.length() > 0) prefs.putString("sta_password", v);
  }

  // If the admin gives an STA SSID, enable STA mode
  if (staProvided) {
    prefs.putBool("use_sta", true);
  }

  // Debug: print what got saved
  Serial.printf("Saved: use_sta=%d ap_ssid=%s sta_ssid=%s\n",
                prefs.getBool("use_sta", false),
                prefs.getString("ap_ssid", "").c_str(),
                prefs.getString("sta_ssid", "").c_str());


  request->send(200, "application/json", "{\"status\":\"network config saved, restarting\"}");

  // Don't restart inside the handler - let loop() do it
  pendingRestart = true;
  restartAt = millis() + 1000;
}

void handleEnableRamRecording(AsyncWebServerRequest *request) {
  if (!checkAuth(request)) {
    request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return;
  }
  ramRecording = true;
  request->send(200, "application/json", "{\"status\":\"RAM recording enabled\"}");
}

void handleDownload(AsyncWebServerRequest *request) {
  if (!checkAuth(request)) {
    request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return;
  }
  if (!sdAvailable) {
    request->send(400, "application/json", "{\"error\":\"no SD card\"}");
    return;
  }
  if (!SD.exists("/experiment.csv")) { // check instead of open
    request->send(404, "application/json", "{\"error\":\"file not found\"}");
    return;
  }
  request->send(SD, "/experiment.csv", "text/csv", true); // server opens/closes it
}

void handleChangePassword(AsyncWebServerRequest *request) {
  if (!checkAuth(request)) {
    request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return;
  }
  if (!request->hasParam("old_password", true) || !request->hasParam("new_password", true)) {
    request->send(400, "application/json", "{\"error\":\"missing old_password or new_password\"}");
    return;
  }
  String oldPwd = request->getParam("old_password", true)->value();
  String newPwd = request->getParam("new_password", true)->value();

  if (oldPwd != adminPassword) {
    request->send(401, "application/json", "{\"error\":\"old password incorrect\"}");
    return;
  }
  if (newPwd.length() < 4) {
    request->send(400, "application/json", "{\"error\":\"new password too short\"}");
    return;
  }

  adminPassword = newPwd;
  prefs.begin("engine", false);
  prefs.putString("admin_pwd", adminPassword);
 
  sessionToken = "";  // force re-login

  request->send(200, "application/json", "{\"status\":\"password changed, please log in again\"}");
}

// Sensor JSON
void sendSensorData() {
  JsonDocument doc;
  //store the sensor readings and other relevant data in the JSON document
  //1. sensor readings and calculated values
  doc["radiatorInlet"] = radiatorInlet;   //store the radiatorInlet value in the JSON document with key "radiatorInlet"
  doc["radiatorOutlet"] = radiatorOutlet;
  doc["engineInlet"] = engineInlet;
  doc["ambientTemp"] = ambientTemp;
  doc["rpm"] = rpm;
  doc["fanSpeed"] = fanSpeed;
  doc["flowRate"] = flowRate;
  doc["coolingRate"] = coolingRate;
  doc["heatDissipation"] = heatDissipation;
  doc["tempRateOfChange"] = tempRateOfChange;
  doc["radiatorTempDiff"] = radiatorTempDiff;

  String baseJson;
  // serialize the JSON document into a string for sending to the cloud server
  serializeJson(doc, baseJson); //#snapshot 1 SENSOR-ONLY

  //2. status flags
  doc["ap_active"] = true;
  doc["sta_connected"] = sta_connected;
  doc["cloud_connected"] = cloud_ws_connected;
  doc["sd_available"] = sdAvailable;
  doc["recording"] = recording;
  doc["paused"] = paused;
  String localJson;
  serializeJson(doc, localJson);//#snapshot 2 SENSOR+STATUS
  //send the JSON string to all connected local web clients via the local websocket
  ws.textAll(localJson);

  if (cloud_ws_connected) {
    cloudWs.sendTXT(baseJson);
  }
}

// Setup
void setup() {
  randomSeed(esp_random());//to ensure every boot has a different random seed for session tokens and other random values
  Serial.begin(115200);

  sensor1.begin();
  sensor2.begin();
  sensor3.begin();

  if (!LittleFS.begin(true)) { //true here automatically formats the filesystem if it fails to mount, which is useful for first-time setup or after a corruption
    Serial.println("LittleFS failed!");
  }

  if (SD.begin(SD_CS_PIN)) {
    sdAvailable = true;
    Serial.println("SD card initialized");
  } else {
    sdAvailable = false;
    Serial.println("SD card failed or not present");
  }

  prefs.begin("engine", false);//initialize preferences with namespace "engine"
  //added to ensure that preferences are loaded before any network or recording operations
  //removed redundant prefs.begin from loadPreferences and handleNetworkConfig
  loadPreferences();

  if (interrupted) {
    Serial.println("WARNING: Previous recording was interrupted. Data may be recoverable.");
  }

  WiFi.mode(WIFI_AP_STA);
  startAPMode();
  trySTAMode();


  initWebSocket();

  //server.on redisters HTTP GET and POST handlers for various endpoints
  // it also serves the main HTML page and the chart.js file from LittleFS
  //and handles API requests for recording control, mode switching, network configuration, RAM recording, password change, and data download
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", index_html);
  });
  server.on("/chart.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/chart.js", "application/javascript");
  });
  server.on("/login", HTTP_POST, handleLogin);//1. route/endpoint 2. HTTP method 3. handler function
  server.on("/api/recording/start", HTTP_POST, handleStartRecording);
  server.on("/api/recording/pause", HTTP_POST, handlePauseRecording);
  server.on("/api/recording/stop", HTTP_POST, handleStopRecording);
  server.on("/api/mode", HTTP_POST, handleSwitchMode);
  server.on("/api/network", HTTP_POST, handleNetworkConfig);
  server.on("/api/recording/ram", HTTP_POST, handleEnableRamRecording);
  server.on("/api/change-password", HTTP_POST, handleChangePassword);
  server.on("/download", HTTP_GET, handleDownload);

  server.begin();//start the local webserver
  Serial.println("Local webserver started");

  if (sta_connected) {//if in sta mode and connected to the cloud, initialize the cloud websocket

    #if DEBUG_HTTPS_TEST
    testRenderHTTPS(); 
    #endif      // diagnostic

    initCloudWebSocket();
  }
}

// Loop
void loop() {
  if (pendingRestart && millis() > restartAt) {
  ESP.restart();
}
  static unsigned long lastTime = 0;
  static float lastEngineInlet = NAN;
  static unsigned long lastEngineTime = 0;

  if (sta_connected) {      //check wether the esp32 is connected to the cloud server in STA mode
    cloudWs.loop();         //keep the cloud websocket alive and handle incoming messages
  }

  if (millis() - lastTime >= 1000) {//check if 1 second has passed since the last sensor reading
    lastTime = millis();

    sensor1.requestTemperatures();//request temperature readings from the sensors
    sensor2.requestTemperatures();
    sensor3.requestTemperatures();

    // will be uncommented when real sensors are connected:
    // radiatorInlet = sensor1.getTempCByIndex(0);
    // radiatorOutlet = sensor2.getTempCByIndex(0);
    // engineInlet = sensor3.getTempCByIndex(0);

    // dummies for now
    radiatorInlet = random(40, 60);
    radiatorOutlet = random(40, 50);
    engineInlet = random(50, 70);

    coolingRate = radiatorInlet - radiatorOutlet;
    radiatorTempDiff = radiatorInlet - radiatorOutlet;

    if (!isnan(lastEngineInlet)) {//check if lastEngineInlet is a valid number before calculating the rate of change
      unsigned long now = millis();
      float dt = (now - lastEngineTime) / 1000.0;//convert milliseconds to seconds
      if (dt > 0) {
        tempRateOfChange = (engineInlet - lastEngineInlet) / dt;
      }
    }
    lastEngineInlet = engineInlet;//store the current engine inlet temperature for the next iteration
    lastEngineTime = millis();

    if (!isnan(flowRate) && flowRate > 0) {
      const float Cp = 4180.0;//specific heat capacity of water in J/(kg·°C)
      heatDissipation = flowRate * Cp * (radiatorInlet - radiatorOutlet);
    } else {
      heatDissipation = NAN;
    }

    Serial.print("Radiator Inlet: "); Serial.print(radiatorInlet);
    Serial.print(" Radiator Outlet: "); Serial.print(radiatorOutlet);
    Serial.print(" Engine Inlet: "); Serial.println(engineInlet);

    if (recording && !paused) {
      String line = getTimestamp() + "," +
                    String(radiatorInlet) + "," +
                    String(radiatorOutlet) + "," +
                    String(engineInlet) + "," +
                    String(coolingRate) + "," +
                    String(heatDissipation) + "," +
                    String(radiatorTempDiff) + "," +
                    String(tempRateOfChange);
      if (sdAvailable) {
        writeToSD(line);
      } else if (ramRecording) {
        writeToRAM(line);
      }
    }

    sendSensorData();
  }
}