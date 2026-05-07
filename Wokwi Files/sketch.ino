// ====================================================================
// Name: Tarun Rashmikant Kandare
// Matriculation Number - 17246381
// Task 3 - Task 3 – MQTT Communication with JSON
// ====================================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include "SimpleJson.h"

// WiFi
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

// MQTT broker
const char* MQTT_BROKER = "141.69.95.10";  
const int   MQTT_PORT   = 1883;
const char* TOPIC_DATA  = "iem/task3/pico/data";
const char* TOPIC_CMD   = "iem/task3/pico/cmd";

const char*         SHARED_TOKEN     = "iem2026";
const char*         EXPECTED_SOURCE  = "nano";
const unsigned long WATCHDOG_TIMEOUT = 10000UL;
const int           INTERVAL_MIN     = 50;
const int           INTERVAL_MAX     = 2000;

// ── Hardware pin numbers ─────────────────────────────────────────────
const int LED_PIN    = 28;
const int BUTTON_PIN = 2;
const int POT_PIN    = 26;

bool          blinkEnabled     = true;
bool          ledState         = false;
int           overrideInterval = -1;
bool          inSafeState      = false;
unsigned long lastValidCmd     = 0;
int           expectedSeqIn    = -1;
unsigned long seqOutgoing      = 0;

unsigned long msgAccepted      = 0;
unsigned long msgRejectedJson  = 0;
unsigned long msgRejectedAuth  = 0;
unsigned long msgSeqGaps       = 0;

// ── Timing ───────────────────────────────────────────────────────────
unsigned long lastBlinkTime    = 0;
unsigned long lastPublishTime  = 0;

static char g_rawPayload[256];

// ── Library objects ──────────────────────────────────────────────────
WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);
SimpleJson   jsonOut, jsonIn;

//  WiFi setup

void setupWiFi() {
  Serial.print("[WiFi] Connecting to "); Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n[WiFi] FAILED – is the Wokwi IoT Gateway running?");
  }
}


//  Watchdog helpers

void enterSafeState() {
  if (!inSafeState) {
    inSafeState      = true;
    overrideInterval = -1;
    Serial.println("[WDG] No cmds for 10s → SAFE STATE (local poti control)");
  }
}

void leaveSafeState() {
  if (inSafeState) {
    inSafeState = false;
    Serial.println("[WDG] Valid command received → left safe state");
  }
}


bool validateMessage(const SimpleJson& msg) {
  // ✅ Fixed: strcmp for correct string content comparison
  if (strcmp(msg.getString("token"), SHARED_TOKEN) != 0) {
    msgRejectedAuth++;
    Serial.println("[AUTH] Wrong/missing token → rejected");
    return false;
  }
  if (strcmp(msg.getString("source"), EXPECTED_SOURCE) != 0) {
    msgRejectedAuth++;
    Serial.print("[AUTH] Wrong source: ");
    Serial.println(msg.getString("source"));
    return false;
  }
  return true;
}

bool checkSequence(const SimpleJson& msg) {
  int seq = msg.getInt("seq");

  if (seq == 0 && expectedSeqIn > 0) {
    Serial.println("[SEQ] Nano restarted (seq=0) – resyncing");
    expectedSeqIn = 0;
  }

  if (expectedSeqIn == -1) {
    expectedSeqIn = seq;
  }

  if (seq < expectedSeqIn) {
    Serial.print("[SEQ] Replay! got="); Serial.print(seq);
    Serial.print(" expected>="); Serial.print(expectedSeqIn);
    Serial.println(" → discarded");
    return false;
  }

  if (seq > expectedSeqIn) {
    msgSeqGaps++;
    Serial.print("[SEQ] Gap: expected="); Serial.print(expectedSeqIn);
    Serial.print(", got="); Serial.println(seq);
  }

  expectedSeqIn = seq + 1;
  return true;
}

void processCommand(const SimpleJson& cmd) {

  if (strstr(g_rawPayload, "\"blinkEnabled\"")) {
    blinkEnabled = cmd.getBool("blinkEnabled");
    Serial.print("[CMD] blinkEnabled = ");
    Serial.println(blinkEnabled ? "true" : "false");
  }

  if (strstr(g_rawPayload, "\"ledOn\"")) {
    bool on = cmd.getBool("ledOn");
    if (!blinkEnabled) {
      ledState = on;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    }
    Serial.print("[CMD] ledOn = ");
    Serial.println(on ? "true" : "false");
  }

  if (strstr(g_rawPayload, "\"interval\"")) {
    int iv = cmd.getInt("interval");
    if (iv >= INTERVAL_MIN && iv <= INTERVAL_MAX) {
      overrideInterval = iv;
      Serial.print("[CMD] Interval → "); Serial.print(iv); Serial.println(" ms");
    } else {
      Serial.print("[CMD] Interval "); Serial.print(iv);
      Serial.println(" ms out of range [50..2000] → rejected");
    }
  }

  if (strstr(g_rawPayload, "\"useLocalPot\"") && cmd.getBool("useLocalPot")) {
    overrideInterval = -1;
    Serial.println("[CMD] useLocalPot → poti speed restored");
  }
}

//  MQTT callback
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  unsigned int len = min(length, (unsigned int)(sizeof(g_rawPayload) - 1));
  memcpy(g_rawPayload, payload, len);
  g_rawPayload[len] = '\0';

  Serial.print("[MQTT<] "); Serial.println(g_rawPayload);

  if (!jsonIn.parse(g_rawPayload)) {
    msgRejectedJson++;
    Serial.println("[REJECT] JSON parse failed");
    return;
  }

  if (!validateMessage(jsonIn)) return;
  if (!checkSequence(jsonIn)) return;

  lastValidCmd = millis();
  leaveSafeState();
  processCommand(jsonIn);
  msgAccepted++;
}

//  MQTT reconnect
void mqttReconnect() {
  while (!mqtt.connected()) {
    Serial.print("[MQTT] Connecting to broker...");
    char clientId[32];
    snprintf(clientId, sizeof(clientId), "PicoW-%04X", (unsigned)random(0xFFFF));

    if (mqtt.connect(clientId)) {
      Serial.println(" connected!");
      mqtt.subscribe(TOPIC_CMD);
      Serial.print("[MQTT] Subscribed: "); Serial.println(TOPIC_CMD);
    } else {
      Serial.print(" failed (state="); Serial.print(mqtt.state());
      Serial.println(") → retry in 2s");
      delay(2000);
    }
  }
}

//  Publish sensor data
void publishSensorData(int potValue, int currentInterval) {
  char buf[256];
  jsonOut.clear();
  jsonOut.setString("token",        SHARED_TOKEN);
  jsonOut.setString("source",       "pico");
  jsonOut.setInt   ("seq",          (int)seqOutgoing++);
  jsonOut.setInt   ("potValue",     potValue);
  jsonOut.setBool  ("blinkEnabled", blinkEnabled);
  jsonOut.setInt   ("interval",     currentInterval);
  jsonOut.setBool  ("ledState",     ledState);
  jsonOut.setBool  ("safeState",    inSafeState);
  jsonOut.setInt   ("uptime",       (int)(millis() / 1000));
  jsonOut.toCharArray(buf, sizeof(buf));
  mqtt.publish(TOPIC_DATA, buf);
}

//  setup()
void setup() {
  pinMode(LED_PIN,    OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.begin(115200);
  delay(1000);

  Serial.println("====================================");
  Serial.println(" Pico W – Task 3: MQTT + JSON");
  Serial.println("====================================");

  setupWiFi();

  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(512);

  lastValidCmd = millis();
  Serial.println("[INFO] Setup complete – waiting for MQTT connection...");
}

bool          lastBtnState    = HIGH;
unsigned long lastBtnDebounce = 0;

void loop() {

  if (!mqtt.connected()) mqttReconnect();
  mqtt.loop();

  if (millis() - lastValidCmd > WATCHDOG_TIMEOUT) {
    enterSafeState();
  }

  bool btnNow = digitalRead(BUTTON_PIN);
  if (btnNow != lastBtnState) {
    lastBtnDebounce = millis();
  }
  if ((millis() - lastBtnDebounce) > 50 && btnNow == LOW) {
    blinkEnabled    = !blinkEnabled;
    lastBtnDebounce = millis() + 300;
    Serial.print("[BTN] blinkEnabled toggled → ");
    Serial.println(blinkEnabled ? "ON" : "OFF");
  }
  lastBtnState = btnNow;

  int potValue = analogRead(POT_PIN);
  int currentInterval = (overrideInterval == -1)
    ? (int)map(potValue, 0, 4095, INTERVAL_MAX, INTERVAL_MIN)
    : overrideInterval;

  if (blinkEnabled) {
    if (millis() - lastBlinkTime >= (unsigned long)currentInterval) {
      lastBlinkTime = millis();
      ledState      = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    }
  } else {
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
  }

  if (millis() - lastPublishTime >= 500) {
    lastPublishTime = millis();
    publishSensorData(potValue, currentInterval);
  }
}
