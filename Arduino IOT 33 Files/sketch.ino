// ====================================================================
// Name: Tarun Rashmikant Kandare
// Matriculation Number - 17246381
// Task 3 - Task 3 – MQTT Communication with JSON
// ====================================================================

#include <WiFiNINA.h>
#include <PubSubClient.h>
#include "SimpleJson.h"

const char* WIFI_SSID = "WLAN-Pi-1";       
const char* WIFI_PASS = "raspberry";       

// ── MQTT broker
const char* MQTT_BROKER = "141.69.95.10";
const int   MQTT_PORT   = 1883;
const char* TOPIC_DATA  = "iem/task3/pico/data";
const char* TOPIC_CMD   = "iem/task3/pico/cmd";   

const char*         SHARED_TOKEN     = "iem2026";
const char*         EXPECTED_SOURCE  = "pico";
const unsigned long WATCHDOG_TIMEOUT = 10000UL;

#define MIRROR_LED_PIN LED_BUILTIN

int  remotePotValue  = 0;
int  remoteInterval  = 0;
int  remoteUptime    = 0;
bool remoteBlink     = false;
bool remoteLedState  = false;
bool remoteSafeState = false;

unsigned long lastValidData  = 0;
bool          picoTimeout    = false;
int           expectedSeqIn  = -1;
unsigned long seqOutgoing    = 0;

unsigned long msgAccepted     = 0;
unsigned long msgRejectedJson = 0;
unsigned long msgRejectedAuth = 0;
unsigned long msgSeqGaps      = 0;

static char g_rawPayload[512];

// ── Library objects
WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);
SimpleJson   jsonOut, jsonIn;
String       inputBuffer = "";

void setupWiFi() {
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("[WiFi] ERROR: WiFi module not found!");
    while (true) delay(1000);
  }

  Serial.print("[WiFi] Connecting to '");
  Serial.print(WIFI_SSID);
  Serial.println("'...");

  int attempts = 0;
  while (WiFi.begin(WIFI_SSID, WIFI_PASS) != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
    if (++attempts >= 30) {
      Serial.println("\n[WiFi] FAILED – check SSID and password!");
      return;
    }
  }
  Serial.println("\n[WiFi] Connected!");
  Serial.print("[WiFi] IP: "); Serial.println(WiFi.localIP());
  Serial.print("[WiFi] Signal: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
}


bool validateMessage(const SimpleJson& msg) {
  if (strcmp(msg.getString("token").c_str(), SHARED_TOKEN) != 0) {
    msgRejectedAuth++;
    Serial.println("[AUTH] Wrong/missing token → rejected");
    return false;
  }
  if (strcmp(msg.getString("source").c_str(), EXPECTED_SOURCE) != 0) {
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
    Serial.println("[SEQ] Pico restarted (seq=0) – resyncing");
    expectedSeqIn = 0;
  }

  if (expectedSeqIn == -1) {
    expectedSeqIn = seq;
  }

  if (seq < expectedSeqIn) {
    Serial.print("[SEQ] Replay! got="); Serial.print(seq);
    Serial.print(", expected>="); Serial.print(expectedSeqIn);
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


void mqttCallback(char* topic, byte* payload, unsigned int length) {
  unsigned int len = min(length, (unsigned int)(sizeof(g_rawPayload) - 1));
  memcpy(g_rawPayload, payload, len);
  g_rawPayload[len] = '\0';

  if (!jsonIn.parse(g_rawPayload)) {
    msgRejectedJson++;
    Serial.println("[REJECT] JSON parse failed");
    return;
  }

  if (!validateMessage(jsonIn)) return;
  if (!checkSequence(jsonIn))   return;

  remotePotValue  = jsonIn.getInt ("potValue");
  remoteInterval  = jsonIn.getInt ("interval");
  remoteUptime    = jsonIn.getInt ("uptime");
  remoteBlink     = jsonIn.getBool("blinkEnabled");
  remoteLedState  = jsonIn.getBool("ledState");
  remoteSafeState = jsonIn.getBool("safeState");

  digitalWrite(MIRROR_LED_PIN, remoteLedState ? HIGH : LOW);
  Serial.print("[LED] Mirroring Pico LED → ");
  Serial.println(remoteLedState ? "ON" : "OFF");


  lastValidData = millis();
  if (picoTimeout) {
    picoTimeout = false;
    Serial.println("[WDG] Pico is back online");
  }

  msgAccepted++;
}

// ====================================================================
//  MQTT reconnect
// ====================================================================
void mqttReconnect() {
  while (!mqtt.connected()) {
    Serial.print("[MQTT] Connecting to broker...");
    char clientId[32];
    snprintf(clientId, sizeof(clientId), "NanoIoT-%04X", (unsigned)random(0xFFFF));

    if (mqtt.connect(clientId)) {
      Serial.println(" connected!");
      mqtt.subscribe(TOPIC_DATA);
      Serial.print("[MQTT] Subscribed: "); Serial.println(TOPIC_DATA);
    } else {
      Serial.print(" failed (state="); Serial.print(mqtt.state());
      Serial.println(") → retry in 2s");
      delay(2000);
    }
  }
}

// ====================================================================
//  Send command to Pico
// ====================================================================
void sendCommand(SimpleJson& cmd) {
  cmd.setString("token",  SHARED_TOKEN);
  cmd.setString("source", "nano");
  cmd.setInt   ("seq",    (int)seqOutgoing++);
  char buf[256];
  cmd.toCharArray(buf, sizeof(buf));
  mqtt.publish(TOPIC_CMD, buf);
  Serial.print("[MQTT>] "); Serial.println(buf);
}

void printStatus() {
  Serial.println("──── Remote Pico Status ────");
  Serial.print("  LED     : "); Serial.println(remoteLedState  ? "ON"  : "OFF");
  Serial.print("  Blink   : "); Serial.println(remoteBlink     ? "ON"  : "OFF");
  Serial.print("  Interval: "); Serial.print(remoteInterval);  Serial.println(" ms");
  Serial.print("  PotValue: "); Serial.println(remotePotValue);
  Serial.print("  Uptime  : "); Serial.print(remoteUptime);    Serial.println(" s");
  Serial.print("  SafeMode: "); Serial.println(remoteSafeState ? "YES" : "no");
  Serial.println("────────────────────────────");
}

void printStats() {
  Serial.println("──── Message Statistics ────");
  Serial.print("  Accepted       : "); Serial.println(msgAccepted);
  Serial.print("  Rejected (JSON): "); Serial.println(msgRejectedJson);
  Serial.print("  Rejected (Auth): "); Serial.println(msgRejectedAuth);
  Serial.print("  Seq gaps       : "); Serial.println(msgSeqGaps);
  Serial.println("────────────────────────────");
}

void printHelp() {
  Serial.println("──── Available Commands ────");
  Serial.println("  ON              – LED on  at Pico");
  Serial.println("  OFF             – LED off at Pico");
  Serial.println("  BLINK           – Blink on");
  Serial.println("  NOBLINK         – Blink off");
  Serial.println("  INTERVAL <ms>   – Set blink speed (50..2000 ms)");
  Serial.println("  POT             – Re-enable local poti control");
  Serial.println("  STATUS          – Show remote Pico state");
  Serial.println("  STATS           – Show message statistics");
  Serial.println("  HELP            – Show this help");
  Serial.println("────────────────────────────");
}

void processSerialInput() {
  while (Serial.available()) {
    char c = (char)Serial.read();

    if (c == '\n' || c == '\r') {
      if (inputBuffer.length() == 0) return;
      inputBuffer.trim();
      String cmd = inputBuffer;
      inputBuffer = "";
      cmd.toUpperCase();

      if (cmd == "ON") {
        jsonOut.clear();
        jsonOut.setBool("blinkEnabled", false);
        jsonOut.setBool("ledOn",        true);
        sendCommand(jsonOut);
        Serial.println("  → LED ON (at Pico)");

      } else if (cmd == "OFF") {
        jsonOut.clear();
        jsonOut.setBool("blinkEnabled", false);
        jsonOut.setBool("ledOn",        false);
        sendCommand(jsonOut);
        Serial.println("  → LED OFF (at Pico)");

      } else if (cmd == "BLINK") {
        jsonOut.clear();
        jsonOut.setBool("blinkEnabled", true);
        sendCommand(jsonOut);
        Serial.println("  → Blink ON");

      } else if (cmd == "NOBLINK") {
        jsonOut.clear();
        jsonOut.setBool("blinkEnabled", false);
        sendCommand(jsonOut);
        Serial.println("  → Blink OFF");

      } else if (cmd.startsWith("INTERVAL ")) {
        int ms = cmd.substring(9).toInt();
        if (ms >= 50 && ms <= 2000) {
          jsonOut.clear();
          jsonOut.setInt("interval", ms);
          sendCommand(jsonOut);
          Serial.print("  → Interval set to "); Serial.print(ms); Serial.println(" ms");
        } else {
          Serial.println("  ERROR: Interval must be 50..2000 ms");
        }

      } else if (cmd == "POT") {
        jsonOut.clear();
        jsonOut.setBool("useLocalPot", true);
        sendCommand(jsonOut);
        Serial.println("  → Poti control restored at Pico");

      } else if (cmd == "STATUS") {
        printStatus();
      } else if (cmd == "STATS") {
        printStats();
      } else if (cmd == "HELP") {
        printHelp();
      } else {
        Serial.print("  Unknown command: '"); Serial.print(cmd); Serial.println("'");
        Serial.println("  Type HELP to see available commands.");
      }

    } else {
      inputBuffer += c;
    }
  }
}

void setup() {
  pinMode(MIRROR_LED_PIN, OUTPUT);
  digitalWrite(MIRROR_LED_PIN, LOW);   // start with LED off

  Serial.begin(115200);
  while (!Serial) delay(100);

  Serial.println("============================================");
  Serial.println(" Arduino Nano 33 IoT – Task 3: MQTT + JSON");
  Serial.println("============================================");
  printHelp();

  setupWiFi();

  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(512);

  lastValidData = millis();
  Serial.println("[INFO] Setup complete – connecting to MQTT...");
}

void loop() {
  if (!mqtt.connected()) mqttReconnect();
  mqtt.loop();

  // ── Watchdog: if no data from Pico for 10s → turn off mirror LED ─
  if (!picoTimeout && millis() - lastValidData > WATCHDOG_TIMEOUT) {
    picoTimeout = true;
    digitalWrite(MIRROR_LED_PIN, LOW);   // safe state: LED off
    Serial.println("[WDG] WARNING: No data from Pico for 10s!");
    Serial.println("      Is the Wokwi simulation running?");
  }

  processSerialInput();
}
