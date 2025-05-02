#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <ESP8266HTTPClient.h>
#include <EEPROM.h>

#define EEPROM_SIZE     512
#define ADDR_URL        0
#define ADDR_APIKEY     200
#define ADDR_GCODE      300
#define ADDR_TYPE       400

#define BUTTON_PIN      D1
#define LED_PIN         D2
#define LED_ON          LOW
#define LED_OFF         HIGH

#define DEBOUNCE_MS     50
#define RESET_HOLD_MS   3000

String baseURL, apiKey, gcode, serverType;
unsigned long lastDebounceTime = 0;
bool lastButtonState = HIGH;
bool buttonPressed = false;

void saveConfig(const String& url, const String& key, const String& code, const String& type) {
  EEPROM.begin(EEPROM_SIZE);
  url.getBytes((byte*)&EEPROM.get(ADDR_URL, url), 200);
  key.getBytes((byte*)&EEPROM.get(ADDR_APIKEY, key), 100);
  code.getBytes((byte*)&EEPROM.get(ADDR_GCODE, code), 100);
  type.getBytes((byte*)&EEPROM.get(ADDR_TYPE, type), 20);
  EEPROM.commit();
}

void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  char url[200], key[100], code[100], type[20];
  for (int i = 0; i < 200; ++i) url[i] = EEPROM.read(ADDR_URL + i);
  for (int i = 0; i < 100; ++i) key[i] = EEPROM.read(ADDR_APIKEY + i);
  for (int i = 0; i < 100; ++i) code[i] = EEPROM.read(ADDR_GCODE + i);
  for (int i = 0; i < 20; ++i)  type[i] = EEPROM.read(ADDR_TYPE + i);
  baseURL = String(url);
  apiKey = String(key);
  gcode = String(code);
  serverType = String(type);
}

void sendGCode() {
  digitalWrite(LED_PIN, LED_ON); delay(100); digitalWrite(LED_PIN, LED_OFF);
  if (WiFi.status() != WL_CONNECTED) return;
  if (baseURL.isEmpty() || apiKey.isEmpty() || gcode.isEmpty()) return;

  HTTPClient http;
  String url;
  String payload;
  String headerKey;
  String headerValue;

  if (serverType == "moon" || serverType == "moonraker") {
    url = baseURL + "/printer/gcode/script";
    payload = "{\"script\": \"" + gcode + "\"}";
    headerKey = "Authorization";
    headerValue = "Bearer " + apiKey;
  } else { // default to octoprint
    url = baseURL + "/api/printer/command";
    payload = "{\"command\": \"" + gcode + "\"}";
    headerKey = "X-Api-Key";
    headerValue = apiKey;
  }

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader(headerKey, headerValue);

  int code = http.POST(payload);
  Serial.printf("Sent GCODE '%s' to %s → HTTP %d\n", gcode.c_str(), serverType.c_str(), code);
  http.end();
}

void checkReset() {
  unsigned long holdStart = millis();
  while (digitalRead(BUTTON_PIN) == LOW) {
    unsigned long elapsed = millis() - holdStart;
    digitalWrite(LED_PIN, (elapsed / 100) % 2 == 0 ? LED_ON : LED_OFF);
    if (elapsed >= RESET_HOLD_MS) {
      Serial.println("Long press detected. Clearing EEPROM and rebooting...");
      EEPROM.begin(EEPROM_SIZE);
      for (int i = 0; i < EEPROM_SIZE; ++i) EEPROM.write(i, 0);
      EEPROM.commit();
      digitalWrite(LED_PIN, LED_OFF);
      delay(500);
      ESP.restart();
    }
    delay(10);
  }
  digitalWrite(LED_PIN, LED_OFF);
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_ON); delay(500); digitalWrite(LED_PIN, LED_OFF);

  checkReset();

  WiFiManager wm;
  WiFiManagerParameter param_url("octourl", "Base URL", "", 200);
  WiFiManagerParameter param_key("apikey", "API Key", "", 100);
  WiFiManagerParameter param_gcode("gcode", "GCODE Command", "M112", 100);
  WiFiManagerParameter param_type("type", "Server Type (octo/moon)", "octo", 20);

  wm.addParameter(&param_url);
  wm.addParameter(&param_key);
  wm.addParameter(&param_gcode);
  wm.addParameter(&param_type);

  if (!wm.autoConnect("EstopConfigAP")) {
    Serial.println("WiFiManager failed. Rebooting...");
    ESP.restart();
  }

  if (String(param_url.getValue()).length() > 0) {
    saveConfig(param_url.getValue(), param_key.getValue(), param_gcode.getValue(), param_type.getValue());
  }

  loadConfig();
}

void loop() {
  bool reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonState) lastDebounceTime = millis();

  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
    if (reading == LOW && !buttonPressed) {
      buttonPressed = true;
      sendGCode();
    } else if (reading == HIGH) {
      buttonPressed = false;
    }
  }

  lastButtonState = reading;
}
