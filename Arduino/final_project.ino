#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <FirebaseESP32.h>
#include <Preferences.h>
#include <DNSServer.h> 

#define ZCROSS_PIN  4
#define TRIAC_PIN   5
#define LED_STATUS  2

#define FIREBASE_HOST "https://kyaryu-191a2-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "yhBFwq9LdVcu6PZSptvv6Zt78Pt9qlb48AXqQ4Hb"
FirebaseData firebaseData;
FirebaseAuth auth;
FirebaseConfig config;

Preferences preferences;
String savedSSID = "";
String savedPASS = "";

WebServer server(80);
WebSocketsServer webSocket(81);

hw_timer_t *timer = NULL;
volatile int pwm_delay = 9300;
volatile bool is_on = true;
volatile int last_pwm = 6580;
bool inAPMode = false;

const byte DNS_PORT = 53;
DNSServer dnsServer;

const char* htmlContent = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Dimmer Control</title>
    <style>
        body { background: black; color: white; font-family: Arial; text-align: center; }
        .slider { width: 80%; margin-top: 20px; }
        .button { font-size: 18px; padding: 10px; margin-top: 10px; cursor: pointer; }
        input[type="number"], input[type="password"] { width: 80px; font-size: 16px; }
        select { font-size: 16px; padding: 5px; }
    </style>
    <script>
        let ws;
        window.onload = () => {
            ws = new WebSocket("ws://" + location.hostname + ":81/");
            ws.onmessage = (event) => {
                if (event.data.startsWith("WIFI_LIST:")) {
                    let list = event.data.substring(10).split("|");
                    let wifiSelect = document.getElementById("wifiSelect");
                    wifiSelect.innerHTML = "";
                    list.forEach((item) => {
                        let opt = document.createElement("option");
                        opt.value = item;
                        opt.innerText = item;
                        wifiSelect.appendChild(opt);
                    });
                    document.getElementById("wifiSection").style.display = "block";
                } else if (event.data === "ONLINE") {
                    document.getElementById("checkBtn").style.display = "none";
                } else {
                    document.getElementById("powerStatus").innerText = event.data;
                }
            };
        };
        function updatePWM(val) {
            let v = parseInt(val);
            v = Math.max(0, Math.min(v, 100));
            document.getElementById("brightnessInput").value = v;
            document.getElementById("pwmValue").innerText ="";
            if (ws) ws.send("PWM:" + v);
        }
        function validateInput() {
            let val = parseInt(document.getElementById("brightnessInput").value);
            val = Math.max(0, Math.min(val, 100));
            document.getElementById("brightnessInput").value = val;
            document.getElementById("brightnessSlider").value = val;
            updatePWM(val);
        }
        function togglePower() {
            if (ws) ws.send("TOGGLE");
        }
        function checkWiFi() {
            if (ws) ws.send("SCAN_WIFI");
        }
        function connectWiFi() {
            const ssid = document.getElementById("wifiSelect").value;
            const pass = document.getElementById("wifiPass").value;
            if (ws) ws.send("CONNECT_WIFI:" + ssid + "|" + pass);
        }
    </script>
</head>
<body>
    <h1>Dimmer Control</h1>
    <input type="range" id="brightnessSlider" min="0" max="100" value="50" class="slider" oninput="updatePWM(this.value)">
    <p>Brightness: 
        <input type="number" id="brightnessInput" value="50" min="0" max="100" oninput="updatePWM(this.value)" onblur="validateInput()"> 
        % <span id="pwmValue"></span>
    </p>
    <button class="button" onclick="togglePower()">ON/OFF</button>
    <p>Status: <span id="powerStatus">ON</span></p>
    <button class="button" id="checkBtn" onclick="checkWiFi()">CHECK WIFI</button>

    <div id="wifiSection" style="display:none; margin-top:20px;">
        <h3>WiFi Networks</h3>
        <select id="wifiSelect"></select><br><br>
        <input type="password" id="wifiPass" placeholder="Password"><br><br>
        <button class="button" onclick="connectWiFi()">CONNECT</button>
    </div>
</body>
</html>
)rawliteral";

void IRAM_ATTR zeroCrossingInterrupt() {
  if (is_on) {
    timerWrite(timer, 0);
    timerAlarm(timer, pwm_delay, false, 0);
  }
}
void IRAM_ATTR triggerTriac() {
  if (is_on) {
    digitalWrite(TRIAC_PIN, HIGH);
    delayMicroseconds(100);
    digitalWrite(TRIAC_PIN, LOW);
  }
}
void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  String msg = String((char *)payload);
  switch (type) {
    case WStype_CONNECTED:
      webSocket.sendTXT(num, is_on ? "ON" : "OFF");
      break;
    case WStype_TEXT:
      if (msg.startsWith("PWM:")) {
        int val = constrain(msg.substring(4).toInt(), 0, 100);
        pwm_delay = map(val, 0, 100, 9300, 1750);
        if (is_on) last_pwm = pwm_delay;
        Firebase.setInt(firebaseData, "/ESP32/brightness", val);
      } else if (msg == "TOGGLE") {
        is_on = !is_on;
        pwm_delay = is_on ? last_pwm : 10000;
        Firebase.setString(firebaseData, "/ESP32/status", is_on ? "ON" : "OFF");
        webSocket.broadcastTXT(is_on ? "ON" : "OFF");
      } else if (msg == "SCAN_WIFI" && inAPMode) {
        int n = WiFi.scanNetworks();
        String list = "WIFI_LIST:";
        for (int i = 0; i < n; i++) {
          list += WiFi.SSID(i);
          if (i < n - 1) list += "|";
        }
        webSocket.sendTXT(num, list);
      } else if (msg.startsWith("CONNECT_WIFI:")) {
        String data = msg.substring(13);
        int sep = data.indexOf("|");
        String ssid = data.substring(0, sep);
        String pass = data.substring(sep + 1);
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), pass.c_str());
        unsigned long t = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t < 8000) delay(200);

        if (WiFi.status() == WL_CONNECTED) {
          preferences.begin("wifi", false);
          preferences.putString("ssid", ssid);
          preferences.putString("pass", pass);
          preferences.end();
          inAPMode = false;
          digitalWrite(LED_STATUS, LOW);
          config.host = FIREBASE_HOST;
          config.signer.tokens.legacy_token = FIREBASE_AUTH;
          Firebase.begin(&config, &auth);
          Firebase.reconnectWiFi(true);
          webSocket.sendTXT(num, "ONLINE");
        }
      }
      break;
    default: break;
  }
}

void handleRoot() {
  server.send(200, "text/html", htmlContent);
}
void handleNotFound() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void updateFromFirebase() {
  if (Firebase.getInt(firebaseData, "/ESP32/brightness"))
    pwm_delay = map(firebaseData.intData(), 0, 100, 9300, 1750);
  if (Firebase.getString(firebaseData, "/ESP32/status"))
    is_on = (firebaseData.stringData() == "ON");
}

void setup() {
  Serial.begin(115200);
  pinMode(ZCROSS_PIN, INPUT);
  pinMode(TRIAC_PIN, OUTPUT);
  pinMode(LED_STATUS, OUTPUT);

  attachInterrupt(digitalPinToInterrupt(ZCROSS_PIN), zeroCrossingInterrupt, RISING);
  timer = timerBegin(1000000);
  timerAttachInterrupt(timer, &triggerTriac);

  preferences.begin("wifi", true);
  savedSSID = preferences.getString("ssid", "");
  savedPASS = preferences.getString("pass", "");
  preferences.end();

  WiFi.mode(WIFI_STA);
  if (savedSSID != "") WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 5000) delay(100);

  if (WiFi.status() == WL_CONNECTED) {
    inAPMode = false;
    digitalWrite(LED_STATUS, LOW);
  } else {
    inAPMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32 192.168.4.1", "12345678");
    digitalWrite(LED_STATUS, HIGH);

    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  }

  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  server.on("/", handleRoot);
  server.onNotFound(handleNotFound); 
  server.begin();

  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);
}

unsigned long lastUpdate = 0;
void loop() {
  server.handleClient();
  webSocket.loop();
  if (inAPMode) dnsServer.processNextRequest();

  if (millis() - lastUpdate > 50) {
    updateFromFirebase();
    lastUpdate = millis();
  }

  static unsigned long wifiLostTime = 0;
  if (!inAPMode && WiFi.status() != WL_CONNECTED) {
    if (wifiLostTime == 0) wifiLostTime = millis();
    if (millis() - wifiLostTime > 6000) {
      inAPMode = true;
      WiFi.disconnect();
      WiFi.mode(WIFI_AP);
      WiFi.softAP("ESP32 192.168.4.1", "12345678");
      digitalWrite(LED_STATUS, HIGH);
      dnsServer.start(DNS_PORT, "*", WiFi.softAPIP()); //  KHỞI ĐỘNG LẠI DNS
    }
  } else {
    wifiLostTime = 0;
  }
}
