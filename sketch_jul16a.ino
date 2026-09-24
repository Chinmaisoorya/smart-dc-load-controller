#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>

// ---------- Pins ----------
#define OLED_SDA    21
#define OLED_SCL    22
#define RELAY1_PIN  32
#define RELAY2_PIN  33
#define RELAY3_PIN  25
#define RELAY4_PIN  26
#define CURRENT_PIN 34
//OLED
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
//ACS712 DC
const float SENSITIVITY_V_PER_A = 0.185;  // ACS712-5A datasheet value
const float ADC_VREF = 3.3;
const int   ADC_RESOLUTION = 4095;
float g_zeroVoltage = 1.65;  // auto-calibrated in setup()

//Protection thresholds
float g_tripCurrent = 3.0;
const int   TRIP_CONFIRM_COUNT = 3;
const unsigned long COOLDOWN_MS = 5000;
const int   MAX_AUTO_RETRIES    = 3;

enum LoadState { NORMAL, TRIPPED, COOLDOWN, LOCKOUT };
LoadState state = NORMAL;
int overThresholdCount = 0;
int autoRetryCount = 0;
unsigned long cooldownStart = 0;
unsigned long totalTripCount = 0;

//Relay state
bool relayState[4] = {true, false, false, false};
const int relayPins[4] = {RELAY1_PIN, RELAY2_PIN, RELAY3_PIN, RELAY4_PIN};

//Current smoothing
const int SMOOTH_SAMPLES = 10;
float iReadings[SMOOTH_SAMPLES] = {0};
int smoothIndex = 0;
float g_current = 0;

//WiFi with dashboard
const char* WIFI_SSID     = "WIFI_NAME";
const char* WIFI_PASSWORD = "PASSKEY";
WebServer server(80);

String serialBuffer = "";

void setRelay(int index, bool on) {
  relayState[index] = on;
  digitalWrite(relayPins[index], on ? LOW : HIGH);
}

float average(float arr[], int n) {
  float s = 0;
  for (int i = 0; i < n; i++) s += arr[i];
  return s / n;
}

float readRawDCCurrent() {
  const int samples = 200;
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(CURRENT_PIN);
    delayMicroseconds(100);
  }
  float avgADC = sum / (float)samples;
  float voltage = avgADC * (ADC_VREF / ADC_RESOLUTION);
  float current = (voltage - g_zeroVoltage) / SENSITIVITY_V_PER_A;
  return current;
}

void calibrateZero() {
  const int samples = 500;
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(CURRENT_PIN);
    delayMicroseconds(100);
  }
  float avgADC = sum / (float)samples;
  g_zeroVoltage = avgADC * (ADC_VREF / ADC_RESOLUTION);
  Serial.printf("Zero-current baseline set to %.3f V\n", g_zeroVoltage);
}

const char* stateName() {
  switch (state) {
    case NORMAL:   return "NORMAL";
    case TRIPPED:  return "TRIPPED";
    case COOLDOWN: return "COOLDOWN";
    case LOCKOUT:  return "LOCKOUT (manual reset needed)";
  }
  return "";
}

void handleRoot() {
  String html = "<html><head><meta http-equiv='refresh' content='4'>"
                "<style>body{font-family:sans-serif;background:#111;color:#eee;padding:20px}"
                "h1{color:#4fc3f7}.card{background:#1e1e1e;padding:14px;border-radius:8px;margin-bottom:10px}"
                "button{padding:10px 16px;margin:4px;border-radius:6px;border:none;font-size:14px}"
                "input{padding:8px;border-radius:6px;border:none;width:80px}"
                ".on{background:#4caf50;color:#fff}.off{background:#555;color:#fff}"
                ".warn{background:#b00020}</style></head><body>";
  html += "<h1>Smart DC Load Controller</h1>";
  html += "<div class='card'>Channel 1 current: " + String(g_current, 2) + " A</div>";
  html += "<div class='card'>State: " + String(stateName()) + "</div>";
  html += "<div class='card'>Total trips: " + String(totalTripCount) + "</div>";

  html += "<div class='card'>Trip threshold: <b>" + String(g_tripCurrent, 2) + " A</b><br><br>";
  html += "<form action='/settrip' method='GET'>";
  html += "<input type='number' step='0.1' name='value' placeholder='e.g. 2.5'> ";
  html += "<button class='on' type='submit'>Set</button></form></div>";

  html += "<div class='card'>Zero-current calibration: " + String(g_zeroVoltage, 3) + " V<br><br>";
  html += "<a href='/zero'><button class='off'>Re-zero now (no load!)</button></a></div>";

  if (state == LOCKOUT) {
    html += "<div class='card warn'>LOCKED OUT after repeated trips.</div>";
    html += "<a href='/reset'><button class='on'>Manual Reset</button></a>";
  }

  for (int i = 0; i < 4; i++) {
    html += "<div class='card'>Relay " + String(i + 1) + ": ";
    html += "<a href='/relay?ch=" + String(i) + "&state=1'><button class='on'>ON</button></a>";
    html += "<a href='/relay?ch=" + String(i) + "&state=0'><button class='off'>OFF</button></a>";
    html += relayState[i] ? " (ON)" : " (OFF)";
    html += "</div>";
  }
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleRelay() {
  if (server.hasArg("ch") && server.hasArg("state")) {
    int ch = server.arg("ch").toInt();
    bool st = server.arg("state").toInt() == 1;
    if (ch == 0 && state == LOCKOUT && st) {
      server.sendHeader("Location", "/");
      server.send(303);
      return;
    }
    if (ch >= 0 && ch < 4) setRelay(ch, st);
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSetTrip() {
  if (server.hasArg("value")) {
    float v = server.arg("value").toFloat();
    if (v > 0 && v < 5.0) {
      g_tripCurrent = v;
      Serial.printf("Trip current updated to %.2f A (via web)\n", g_tripCurrent);
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleZero() {
  calibrateZero();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleReset() {
  state = NORMAL;
  autoRetryCount = 0;
  overThresholdCount = 0;
  setRelay(0, true);
  Serial.println("Manual reset — channel 1 restored, retry counter cleared");
  server.sendHeader("Location", "/");
  server.send(303);
}

void checkSerialCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      serialBuffer.trim();
      if (serialBuffer.length() > 0) {
        if (serialBuffer == "reset") {
          state = NORMAL;
          autoRetryCount = 0;
          overThresholdCount = 0;
          setRelay(0, true);
          Serial.println("Manual reset via Serial");
        } else if (serialBuffer == "zero") {
          calibrateZero();
        } else {
          float v = serialBuffer.toFloat();
          if (v > 0 && v < 5.0) {
            g_tripCurrent = v;
            Serial.printf("Trip current updated to %.2f A (via Serial)\n", g_tripCurrent);
          } else {
            Serial.println("Commands: a number (set trip A), 'zero' (recalibrate), 'reset' (clear lockout)");
          }
        }
      }
      serialBuffer = "";
    } else {
      serialBuffer += c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  analogSetAttenuation(ADC_11db);

  for (int i = 0; i < 4; i++) {
    digitalWrite(relayPins[i], HIGH);
    pinMode(relayPins[i], OUTPUT);
  }
  setRelay(0, true);

  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Smart DC Load");
  display.println("Calibrating zero...");
  display.println("(keep load OFF)");
  display.display();

  delay(1500);
  calibrateZero();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(300);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected. IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi failed - continuing offline");
  }

  Serial.println("Commands: number=set trip A, 'zero'=recalibrate, 'reset'=clear lockout");

  server.on("/", handleRoot);
  server.on("/relay", handleRelay);
  server.on("/settrip", handleSetTrip);
  server.on("/zero", handleZero);
  server.on("/reset", handleReset);
  server.begin();
}

void loop() {
  server.handleClient();
  checkSerialCommands();

  float raw = readRawDCCurrent();
  iReadings[smoothIndex] = raw;
  smoothIndex = (smoothIndex + 1) % SMOOTH_SAMPLES;
  g_current = average(iReadings, SMOOTH_SAMPLES);
  if (g_current < 0) g_current = 0;

  switch (state) {

    case NORMAL:
      if (g_current > g_tripCurrent) {
        overThresholdCount++;
        if (overThresholdCount >= TRIP_CONFIRM_COUNT) {
          setRelay(0, false);
          state = TRIPPED;
          totalTripCount++;
          Serial.println("TRIPPED - overcurrent confirmed, relay 1 OFF");
        }
      } else {
        overThresholdCount = 0;
      }
      break;

    case TRIPPED:
      autoRetryCount++;
      cooldownStart = millis();
      state = COOLDOWN;
      Serial.printf("Entering cooldown (%lus)...\n", COOLDOWN_MS / 1000);
      break;

    case COOLDOWN:
      if (millis() - cooldownStart >= COOLDOWN_MS) {
        if (autoRetryCount > MAX_AUTO_RETRIES) {
          state = LOCKOUT;
          Serial.println("LOCKOUT - too many trips, manual reset required");
        } else {
          setRelay(0, true);
          overThresholdCount = 0;
          state = NORMAL;
          Serial.println("Cooldown done - relay 1 re-enabled, watching for fault");
        }
      }
      break;

    case LOCKOUT:
      break;
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("I1: "); display.print(g_current, 2); display.println(" A");
  display.print("Trip@: "); display.println(g_tripCurrent, 2);
  display.print("State: "); display.println(stateName());
  display.print("Trips: "); display.println(totalTripCount);
  display.display();

  delay(150);
}