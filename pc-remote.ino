#include <WiFi.h>
#include <WebServer.h>

/* ========= WIFI ========= */
const char* ssid     = "SSID";
const char* password = "PASSWORD";

/* ========= STATIC IP ========= */
IPAddress localIP(192, 168, 1, 2);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(192, 168, 1, 1);

/* ========= STATUS ========= */
const char ON  = '+';
const char OFF = '-';

char currentStatus = OFF;

/* =========================================================
ESP32-C3 SuperMini PINS

For motherboard LED:
  Anode    → '+' pin on motherboard LED header
  Cathode  → '-' pin on motherboard LED header
  Emitter  → GPIO10
  Collector→ GND

For power switch:
  Anode    → 180Ω resistor → GPIO4
  Cathode  → GND
  Emitter  → motherboard power header pin
  Collector→ motherboard power header pin

For reset switch:
  Anode    → 180Ω resistor → GPIO3
  Cathode  → GND
  Emitter  → motherboard reset header pin
  Collector→ motherboard reset header pin
========================================================= */
const int pinPowerButton   = 4;   // power switch
const int pinResetButton   = 3;   // reset switch
const int pinPowerLightOut = 5;   // case LED
const int pinPowerLightIn  = 10;  // motherboard LED

const int buttonPressTime  = 250;

/* ===================================================== */

unsigned long powerButtonReleaseTime = 0;
bool forceOffActive = false;

volatile bool statusChangePending = false;
volatile unsigned long lastInterruptTime = 0;

WebServer server(80);

String getStatusText()
{
  return (currentStatus == ON) ? "ON" : "OFF";
}

void IRAM_ATTR statusChange()
{
  unsigned long now = micros();
  if (now - lastInterruptTime < 50000) return; // 50ms debounce
  lastInterruptTime = now;
  statusChangePending = true;
}

void handleStatusChange()
{
  if (!statusChangePending) return;
  statusChangePending = false;

  // PC817 active-low: LOW = LED on = PC is on
  bool pcOn = !digitalRead(pinPowerLightIn);

  if (pcOn)
  {
    currentStatus = ON;
  }
  else
  {
    currentStatus = OFF;
    forceOffActive = false;
    digitalWrite(pinPowerButton, LOW);
    powerButtonReleaseTime = 0;
  }

  digitalWrite(pinPowerLightOut, pcOn);
}

void pressPowerButton(int duration)
{
  digitalWrite(pinPowerButton, HIGH);
  powerButtonReleaseTime = millis() + duration;
}

void checkPowerRelease()
{
  if (powerButtonReleaseTime &&
      (long)(millis() - powerButtonReleaseTime) >= 0)
  {
    digitalWrite(pinPowerButton, LOW);
    powerButtonReleaseTime = 0;
    forceOffActive = false;
  }
}

void handleRoot()
{
  String statusColor    = (currentStatus == ON) ? "#4caf50" : "#d64545";
  String forceOffBanner = forceOffActive
    ? "<div class='warning'>&#9888; Force off in progress...</div>"
    : "";
  String disabledAttr   = forceOffActive ? "disabled onclick='return false'" : "";

  String page =
  "<!DOCTYPE html><html><head>"
  "<meta name='viewport' content='width=device-width, initial-scale=1'>"
  "<meta http-equiv='refresh' content='3'>"

  "<style>"
  "body{background:#0f1117;color:#e6e6e6;"
  "font-family:Arial;text-align:center;padding:20px;}"
  ".card{background:#1a1d26;padding:20px;"
  "border-radius:14px;max-width:420px;margin:auto;"
  "box-shadow:0 0 20px rgba(0,0,0,0.4);}"
  "button{width:100%;padding:16px;margin:8px 0;"
  "font-size:18px;border:none;border-radius:10px;"
  "background:#2b6cff;color:white;cursor:pointer;}"
  "button:disabled{background:#444;color:#888;cursor:not-allowed;}"
  ".warn{background:#e6a23c;}"
  ".danger{background:#d64545;}"
  "a{text-decoration:none;}"
  ".status{font-size:22px;margin-bottom:15px;}"
  ".dot{display:inline-block;width:12px;height:12px;"
  "border-radius:50%;margin-right:6px;"
  "background:" + statusColor + ";}"
  ".warning{background:#7a3c00;border-radius:8px;"
  "padding:10px;margin-bottom:12px;font-size:15px;}"
  "</style></head>"

  "<body><div class='card'>"
  "<h2>PC Power Controller</h2>"
  "<div class='status'><span class='dot'></span>Status: <b>" + getStatusText() + "</b></div>"
  + forceOffBanner +

  "<a href='/on'><button "              + disabledAttr + ">Power ON</button></a>"
  "<a href='/off'><button class='warn' " + disabledAttr + ">Power OFF</button></a>"
  "<a href='/reset'><button "            + disabledAttr + ">Reset</button></a>"
  "<a href='/forceoff'><button class='danger' onclick=\"return confirm('Force shutdown the PC?')\">Force OFF</button></a>"

  "</div></body></html>";

  server.send(200, "text/html", page);
}

void handleOn()
{
  if (!forceOffActive && currentStatus != ON)
    pressPowerButton(buttonPressTime);

  server.sendHeader("Location", "/");
  server.send(303);
}

void handleOff()
{
  if (!forceOffActive && currentStatus == ON)
    pressPowerButton(buttonPressTime);

  server.sendHeader("Location", "/");
  server.send(303);
}

void handleReset()
{
  if (!forceOffActive)
  {
    digitalWrite(pinResetButton, HIGH);
    delay(500);
    digitalWrite(pinResetButton, LOW);
  }

  server.sendHeader("Location", "/");
  server.send(303);
}

void handleForceOff()
{
  if (forceOffActive) {
    server.sendHeader("Location", "/");
    server.send(303);
    return;
  }

  forceOffActive = true;
  pressPowerButton(10000);

  server.sendHeader("Location", "/");
  server.send(303);
}

void setup()
{
  Serial.begin(115200);

  pinMode(pinPowerLightIn,  INPUT_PULLUP);
  pinMode(pinPowerLightOut, OUTPUT);
  pinMode(pinPowerButton,   OUTPUT);
  pinMode(pinResetButton,   OUTPUT);

  digitalWrite(pinPowerButton,   LOW);
  digitalWrite(pinResetButton,   LOW);
  digitalWrite(pinPowerLightOut, LOW);

  currentStatus = !digitalRead(pinPowerLightIn) ? ON : OFF;
  digitalWrite(pinPowerLightOut, currentStatus == ON);

  attachInterrupt(
    digitalPinToInterrupt(pinPowerLightIn),
    statusChange,
    CHANGE
  );

  if (!WiFi.config(localIP, gateway, subnet, dns))
    Serial.println("Static IP config failed");

  WiFi.begin(ssid, password);

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startAttemptTime < 20000)
  {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected:");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi Failed — continuing offline");
  }

  server.on("/",         handleRoot);
  server.on("/on",       handleOn);
  server.on("/off",      handleOff);
  server.on("/reset",    handleReset);
  server.on("/forceoff", handleForceOff);

  server.begin();
}

void loop()
{
  handleStatusChange();
  server.handleClient();
  checkPowerRelease();
}
