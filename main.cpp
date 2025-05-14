#include <functional>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <ESP8266HTTPClient.h>
#include <EEPROM.h>

#define EEPROM_SIZE     512
#define ADDR_URL        0
#define ADDR_APIKEY     200
#define ADDR_GCODE      300
#define ADDR_TYPE       400

#define BUTTON_PIN      2
#define LED_PIN         0
#define LED_ON          LOW
#define LED_OFF         HIGH

#define DEBOUNCE_MS     50
#define RESET_HOLD_MS   3000

String baseURL, apiKey, gcode, serverType;
unsigned long lastDebounceTime = 0;
bool lastButtonState = HIGH;
bool buttonPressed = false;

// Function declarations
bool sendRawKasaCommand(const String& ip, const String& json, bool infoOnly = false);
bool sendJsonAndGetResponse(WiFiClient& client, const String& ip, int port, const String& json, 
                          std::function<void(const String&)> responseProcessor);
bool sendKasaCommand(const String& command);
bool sendOctoPrintCommand(const String& gcode);
bool sendMoonrakerCommand(const String& gcode);
void sendCommand();
void checkReset();
void saveConfig(const String& url, const String& key, const String& code, const String& type);
void loadConfig();
void parseKasaCommand(const String& command, int& outletNum, bool& turnOn);
void dumpHex(const uint8_t* buffer, size_t len);
bool getKasaDeviceInfo(const String& ip, String& deviceId, String childIds[], int& numChildren);

// Save configuration to EEPROM
void saveConfig(const String& url, const String& key, const String& code, const String& type) {
  EEPROM.begin(EEPROM_SIZE);
  
  // Clear the EEPROM sections first
  for (int i = 0; i < 200; i++) EEPROM.write(ADDR_URL + i, 0);
  for (int i = 0; i < 100; i++) EEPROM.write(ADDR_APIKEY + i, 0);
  for (int i = 0; i < 100; i++) EEPROM.write(ADDR_GCODE + i, 0);
  for (int i = 0; i < 20; i++) EEPROM.write(ADDR_TYPE + i, 0);
  
  // Write the new values
  for (unsigned int i = 0; i < url.length(); i++) 
    EEPROM.write(ADDR_URL + i, url[i]);
  
  for (unsigned int i = 0; i < key.length(); i++) 
    EEPROM.write(ADDR_APIKEY + i, key[i]);
  
  for (unsigned int i = 0; i < code.length(); i++) 
    EEPROM.write(ADDR_GCODE + i, code[i]);
  
  for (unsigned int i = 0; i < type.length(); i++) 
    EEPROM.write(ADDR_TYPE + i, type[i]);
  
  EEPROM.commit();
  Serial.println("Config saved successfully");
}

// Load configuration from EEPROM
void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  char url[200] = {0};
  char key[100] = {0};
  char code[100] = {0};
  char type[20] = {0};
  
  for (int i = 0; i < 199; i++) {
    url[i] = EEPROM.read(ADDR_URL + i);
    if (url[i] == 0) break;
  }
  
  for (int i = 0; i < 99; i++) {
    key[i] = EEPROM.read(ADDR_APIKEY + i);
    if (key[i] == 0) break;
  }
  
  for (int i = 0; i < 99; i++) {
    code[i] = EEPROM.read(ADDR_GCODE + i);
    if (code[i] == 0) break;
  }
  
  for (int i = 0; i < 19; i++) {
    type[i] = EEPROM.read(ADDR_TYPE + i);
    if (type[i] == 0) break;
  }
  
  baseURL = String(url);
  apiKey = String(key);
  gcode = String(code);
  serverType = String(type);
  
  Serial.println("Loaded configuration:");
  Serial.println("URL: " + baseURL);
  Serial.print("API Key: ");
  Serial.println(apiKey.isEmpty() ? "[empty]" : "[set]");
  Serial.println("GCODE/Command: " + gcode);
  Serial.println("Server Type: " + serverType);
}

// Parse Kasa command to extract outlet number and action
void parseKasaCommand(const String& command, int& outletNum, bool& turnOn) {
  // Default values
  outletNum = 0;
  turnOn = true;
  
  // Convert to lowercase for consistent behavior
  String lowerCmd = command;
  lowerCmd.toLowerCase();
  
  // Look for format like "on0", "off1", etc.
  if (lowerCmd.startsWith("on")) {
    turnOn = true;
    if (lowerCmd.length() > 2) {
      outletNum = lowerCmd.substring(2).toInt();
    }
  } 
  else if (lowerCmd.startsWith("off")) {
    turnOn = false;
    if (lowerCmd.length() > 3) {
      outletNum = lowerCmd.substring(3).toInt();
    }
  }
  // If just a number is provided, assume it's the outlet number (turn on)
  else if (lowerCmd.toInt() || lowerCmd == "0") {
    outletNum = lowerCmd.toInt();
    turnOn = true;
  }
  
  Serial.print("Parsed Kasa command - Outlet: ");
  Serial.print(outletNum);
  Serial.print(", Action: ");
  Serial.println(turnOn ? "ON" : "OFF");
}

// Helper function to dump a buffer as hex bytes for debugging
void dumpHex(const uint8_t* buffer, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (buffer[i] < 16) Serial.print("0");
    Serial.print(buffer[i], HEX);
    Serial.print(" ");
    if ((i + 1) % 16 == 0) Serial.println();
  }
  Serial.println();
}

// Get information from the Kasa device including device ID and child IDs
bool getKasaDeviceInfo(const String& ip, String& deviceId, String childIds[], int& numChildren) {
  WiFiClient client;
  const int kasaPort = 9999;
  bool success = false;
  
  // Initialize return values
  deviceId = "";
  numChildren = 0;
  
  if (!client.connect(ip.c_str(), kasaPort)) {
    Serial.println("Failed to connect to Kasa device for info query");
    return false;
  }
  
  String infoJson = "{\"system\":{\"get_sysinfo\":{}}}";
  Serial.println("Getting device info...");
  
  // Encrypt and send the info query
  size_t infoJsonLength = infoJson.length();
  uint8_t* encrypted = new uint8_t[infoJsonLength];
  uint8_t key = 0xAB;
  
  for (size_t i = 0; i < infoJsonLength; i++) {
    encrypted[i] = infoJson.charAt(i) ^ key;
    key = encrypted[i];
  }
  
  uint8_t header[4] = {
    (uint8_t)((infoJsonLength >> 24) & 0xFF),
    (uint8_t)((infoJsonLength >> 16) & 0xFF),
    (uint8_t)((infoJsonLength >> 8) & 0xFF),
    (uint8_t)(infoJsonLength & 0xFF)
  };
  
  client.write(header, 4);
  client.write(encrypted, infoJsonLength);
  client.flush();
  delete[] encrypted;
  
  // Wait for response
  unsigned long timeout = millis() + 3000;
  while (client.available() == 0) {
    if (millis() > timeout) {
      Serial.println("Info query timeout");
      client.stop();
      return false;
    }
    delay(10);
  }
  
  // Process response
  if (client.available()) {
    // Skip length header
    for (int i = 0; i < 4 && client.available(); i++) {
      client.read();
    }
    
    // Decrypt response
    String response = "";
    uint8_t decryptKey = 0xAB;
    while (client.available()) {
      uint8_t c = client.read();
      uint8_t decrypted = c ^ decryptKey;
      decryptKey = c;
      response += (char)decrypted;
    }
    
    Serial.println("Device info response received");
    
    // Extract main device ID
    int deviceIdPos = response.indexOf("\"deviceId\":\"");
    if (deviceIdPos > 0) {
      deviceIdPos += 12; // Skip over "deviceId":"
      int deviceIdEnd = response.indexOf("\"", deviceIdPos);
      if (deviceIdEnd > deviceIdPos) {
        deviceId = response.substring(deviceIdPos, deviceIdEnd);
        Serial.print("Device ID: ");
        Serial.println(deviceId);
      }
    }
    
    // Look for the children array
    int childrenStart = response.indexOf("\"children\":[");
    if (childrenStart > 0) {
      // Navigate through the children array to extract each child's ID
      int index = childrenStart + 12; // Skip over "children":[ 
      int braceCount = 0;
      int childIndex = 0;
      
      // Process each child object
      while (index < response.length() && childIndex < 8) { // Maximum of 8 children
        if (response.charAt(index) == '{') {
          braceCount++;
          
          // Look for id within this child object
          int idPos = response.indexOf("\"id\":\"", index);
          if (idPos > 0 && braceCount == 1) {
            idPos += 6; // Skip over "id":"
            int idEnd = response.indexOf("\"", idPos);
            if (idEnd > idPos) {
              childIds[childIndex] = response.substring(idPos, idEnd);
              Serial.print("Child ");
              Serial.print(childIndex);
              Serial.print(" ID: ");
              Serial.println(childIds[childIndex]);
              childIndex++;
            }
          }
        } 
        else if (response.charAt(index) == '}') {
          braceCount--;
        }
        
        index++;
        
        // If we've completed a child object, check if we're at the end of the array
        if (braceCount == 0 && index < response.length()) {
          if (response.charAt(index) == ']') {
            break; // End of children array
          }
        }
      }
      
      numChildren = childIndex;
      Serial.print("Found ");
      Serial.print(numChildren);
      Serial.println(" children");
      success = (numChildren > 0);
    }
  }
  
  client.stop();
  return success;
}

// Send command to the specific outlet of a TP-Link Kasa device
bool sendKasaCommand(const String& command) {
  // Parse the command to determine outlet number and action
  int outletNum;
  bool turnOn;
  parseKasaCommand(command, outletNum, turnOn);
  
  // Get device info including child IDs
  String deviceId;
  String childIds[8]; // Support up to 8 outlets
  int numChildren;
  bool isKP200 = false;
  String modelName = "";
  
  // Query the device for its information
  if (getKasaDeviceInfo(baseURL, deviceId, childIds, numChildren)) {
    // If we have outlet 1 requested but only one child found, it might be a KP200
    // even if we can't confirm from the model name
    if (outletNum == 1 && numChildren <= 1) {
      // First, try to get model info
      WiFiClient infoClient;
      String infoJson = "{\"system\":{\"get_sysinfo\":{}}}";
      
      if (sendJsonAndGetResponse(infoClient, baseURL, 9999, infoJson, [&](const String& response) {
        // Extract model name from response
        int modelPos = response.indexOf("\"model\":\"");
        if (modelPos > 0) {
          modelPos += 9; // Skip "model":"
          int modelEnd = response.indexOf("\"", modelPos);
          if (modelEnd > modelPos) {
            modelName = response.substring(modelPos, modelEnd);
            Serial.print("Device model: ");
            Serial.println(modelName);
            
            // Check if it's a KP200 model
            if (modelName.indexOf("KP200") >= 0) {
              isKP200 = true;
              Serial.println("Detected KP200 model - enabling special dual-outlet handling");
            }
          }
        }
      })) {
        // Successfully got device info
      }
      
      // If model detection failed but we have outlet 1 requested with only 1 child,
      // assume it might be a KP200 and try special handling
      if (!isKP200 && outletNum == 1 && numChildren <= 1) {
        Serial.println("Outlet 1 requested but only 1 child found - trying special handling");
        isKP200 = true;
      }
      
      // For KP200, try the special methods for second outlet
      if (isKP200 && outletNum == 1) {
        Serial.println("Using special handling for KP200 second outlet");
        
        // Method 1: Try derived child ID
        if (!childIds[0].isEmpty() && childIds[0].length() >= 2) {
          String secondOutletId = childIds[0].substring(0, childIds[0].length()-2) + "01";
          
          Serial.print("Trying second outlet with derived ID: ");
          Serial.println(secondOutletId);
          
          String json = "{\"context\":{\"child_ids\":[\"" + secondOutletId + 
                       "\"]},\"system\":{\"set_relay_state\":{\"state\":" + 
                       String(turnOn ? 1 : 0) + "}}}";
          
          if (sendRawKasaCommand(baseURL, json, false)) {
            return true;
          }
        }
        
        // Method 2: Try numeric index
        Serial.println("Trying second outlet with numeric index");
        String json = "{\"context\":{\"child_ids\":[1]},\"system\":{\"set_relay_state\":{\"state\":" + 
                     String(turnOn ? 1 : 0) + "}}}";
        
        if (sendRawKasaCommand(baseURL, json, false)) {
          return true;
        }
        
        // Method 3: Try outlet parameter
        Serial.println("Trying second outlet with outlet parameter");
        json = "{\"system\":{\"set_relay_state\":{\"state\":" + String(turnOn ? 1 : 0) + 
               ",\"outlet\":1}}}";
        
        if (sendRawKasaCommand(baseURL, json, false)) {
          return true;
        }
        
        Serial.println("All methods failed for second outlet");
        return false;
      }
    }
    
    // Normal handling for non-KP200 devices or outlet 0 of KP200
    if (outletNum >= numChildren) {
      Serial.print("Error: Outlet ");
      Serial.print(outletNum);
      Serial.print(" requested but device only has ");
      Serial.print(numChildren);
      Serial.println(" outlets");
      return false;
    }
    
    // Send command to regular outlet
    String json = "{\"context\":{\"child_ids\":[\"" + childIds[outletNum] + 
                 "\"]},\"system\":{\"set_relay_state\":{\"state\":" + 
                 String(turnOn ? 1 : 0) + "}}}";
    
    Serial.print("Sending command to outlet ");
    Serial.print(outletNum);
    Serial.print(": ");
    Serial.println(json);
    
    return sendRawKasaCommand(baseURL, json, false);
  } else {
    Serial.println("Failed to get device info");
    return false;
  }
}

// Helper function to send JSON and process response
bool sendJsonAndGetResponse(WiFiClient& client, const String& ip, int port, const String& json, 
                          std::function<void(const String&)> responseProcessor) {
  if (!client.connect(ip.c_str(), port)) {
    Serial.println("Failed to connect to device");
    return false;
  }
  
  // Encrypt and send
  size_t jsonLength = json.length();
  uint8_t* encrypted = new uint8_t[jsonLength];
  uint8_t key = 0xAB;
  
  for (size_t i = 0; i < jsonLength; i++) {
    encrypted[i] = json.charAt(i) ^ key;
    key = encrypted[i];
  }
  
  uint8_t header[4] = {
    (uint8_t)((jsonLength >> 24) & 0xFF),
    (uint8_t)((jsonLength >> 16) & 0xFF),
    (uint8_t)((jsonLength >> 8) & 0xFF),
    (uint8_t)(jsonLength & 0xFF)
  };
  
  client.write(header, 4);
  client.write(encrypted, jsonLength);
  client.flush();
  delete[] encrypted;
  
  // Wait for response
  unsigned long timeout = millis() + 3000;
  bool dataReceived = false;
  
  while (millis() < timeout) {
    if (client.available()) {
      dataReceived = true;
      break;
    }
    delay(10);
  }
  
  if (!dataReceived) {
    Serial.println("Command timeout");
    client.stop();
    return false;
  }
  
  // Process response
  if (client.available()) {
    // Skip length header
    for (int i = 0; i < 4 && client.available(); i++) {
      client.read();
    }
    
    // Decrypt response
    String response = "";
    uint8_t decryptKey = 0xAB;
    
    while (client.available()) {
      uint8_t c = client.read();
      uint8_t decrypted = c ^ decryptKey;
      decryptKey = c;
      response += (char)decrypted;
    }
    
    responseProcessor(response);
    
    client.stop();
    return true;
  }
  
  client.stop();
  return false;
}

// Function to send raw Kasa json command
bool sendRawKasaCommand(const String& ip, const String& json, bool infoOnly) {
  WiFiClient client;
  const int kasaPort = 9999;
  bool success = false;
  
  Serial.print("Sending raw command to Kasa device: ");
  Serial.println(json);
  
  if (!client.connect(ip.c_str(), kasaPort)) {
    Serial.println("Failed to connect to Kasa device");
    return false;
  }
  
  // Encrypt the payload (TP-Link XOR encryption)
  size_t jsonLength = json.length();
  uint8_t* encrypted = new uint8_t[jsonLength];
  uint8_t key = 0xAB;
  
  for (size_t i = 0; i < jsonLength; i++) {
    encrypted[i] = json.charAt(i) ^ key;
    key = encrypted[i];
  }
  
  // Prepare the 4-byte header (big-endian length)
  uint8_t header[4] = {
    (uint8_t)((jsonLength >> 24) & 0xFF),
    (uint8_t)((jsonLength >> 16) & 0xFF),
    (uint8_t)((jsonLength >> 8) & 0xFF),
    (uint8_t)(jsonLength & 0xFF)
  };
  
  client.write(header, 4);
  client.write(encrypted, jsonLength);
  client.flush();
  delete[] encrypted;
  
  // Wait for and read response
  unsigned long timeout = millis() + 3000;
  bool dataReceived = false;
  
  while (millis() < timeout) {
    if (client.available()) {
      dataReceived = true;
      break;
    }
    delay(10);
  }
  
  if (!dataReceived) {
    Serial.println("Raw command timeout");
    client.stop();
    return false;
  }
  
  // Read and process response
  if (client.available()) {
    // Skip the header (first 4 bytes)
    for (int i = 0; i < 4 && client.available(); i++) {
      client.read();
    }
    
    // Decrypt response
    String response = "";
    uint8_t decryptKey = 0xAB;
    while (client.available()) {
      uint8_t c = client.read();
      uint8_t decrypted = c ^ decryptKey;
      decryptKey = c;
      response += (char)decrypted;
    }
    
    Serial.print("Raw command response: ");
    Serial.println(response);
    
    if (response.indexOf("\"err_code\":0") > 0) {
      success = true;
      Serial.println("Raw command successful");
    } else {
      Serial.println("Raw command failed or returned error");
    }
  }
  
  client.stop();
  return success;
}

// Send command to OctoPrint server
bool sendOctoPrintCommand(const String& gcode) {
  WiFiClient client;
  HTTPClient http;
  bool success = false;
  
  Serial.print("Sending to OctoPrint: ");
  Serial.println(gcode);
  
  String url = baseURL + "/api/printer/command";
  String payload = "{\"command\": \"" + gcode + "\"}";
  
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Api-Key", apiKey);
  
  int httpCode = http.POST(payload);
  
  if (httpCode > 0) {
    Serial.printf("OctoPrint HTTP response: %d\n", httpCode);
    if (httpCode == HTTP_CODE_NO_CONTENT || httpCode == HTTP_CODE_OK) {
      success = true;
    }
  } else {
    Serial.printf("OctoPrint HTTP error: %s\n", http.errorToString(httpCode).c_str());
  }
  
  http.end();
  return success;
}

// Send command to Moonraker/Klipper server
bool sendMoonrakerCommand(const String& gcode) {
  WiFiClient client;
  HTTPClient http;
  bool success = false;
  
  Serial.print("Sending to Moonraker: ");
  Serial.println(gcode);
  
  String url = baseURL + "/printer/gcode/script";
  String payload = "{\"script\": \"" + gcode + "\"}";
  
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  
  // Moonraker uses Bearer token authentication
  if (!apiKey.isEmpty()) {
    http.addHeader("Authorization", "Bearer " + apiKey);
  }
  
  int httpCode = http.POST(payload);
  
  if (httpCode > 0) {
    Serial.printf("Moonraker HTTP response: %d\n", httpCode);
    if (httpCode == HTTP_CODE_OK) {
      String response = http.getString();
      Serial.println("Response: " + response);
      success = true;
    }
  } else {
    Serial.printf("Moonraker HTTP error: %s\n", http.errorToString(httpCode).c_str());
  }
  
  http.end();
  return success;
}

// Send a command based on the configured server type
void sendCommand() {
  digitalWrite(LED_PIN, LED_ON);
  bool success = false;
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected - cannot send command");
    digitalWrite(LED_PIN, LED_OFF);
    return;
  }
  
  if (baseURL.isEmpty()) {
    Serial.println("Base URL not configured");
    digitalWrite(LED_PIN, LED_OFF);
    return;
  }
  
  if (gcode.isEmpty()) {
    Serial.println("Command/GCODE not configured");
    digitalWrite(LED_PIN, LED_OFF);
    return;
  }
  
  Serial.print("Server type: ");
  Serial.println(serverType);
  
  if (serverType.equalsIgnoreCase("kasa")) {
    success = sendKasaCommand(gcode);
  } 
  else if (serverType.equalsIgnoreCase("moon") || serverType.equalsIgnoreCase("moonraker")) {
    success = sendMoonrakerCommand(gcode);
  }
  else {
    // Default to OctoPrint
    success = sendOctoPrintCommand(gcode);
  }
  
  // Blink status
  if (success) {
    // Success - quick blink
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_PIN, LED_ON);
      delay(100);
      digitalWrite(LED_PIN, LED_OFF);
      delay(100);
    }
  } else {
    // Error - slow blink
    for (int i = 0; i < 2; i++) {
      digitalWrite(LED_PIN, LED_ON);
      delay(500);
      digitalWrite(LED_PIN, LED_OFF);
      delay(500);
    }
  }
}

// Check for reset button hold
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
  
  // Startup blink
  digitalWrite(LED_PIN, LED_ON);
  delay(500);
  digitalWrite(LED_PIN, LED_OFF);
  
  Serial.println("\n\nESP8266 E-Stop Button Starting");
  Serial.print("Firmware version: ");
  Serial.println("1.0.0");
  
  // Check for reset button press during boot
  checkReset();
  
  // Configure WiFi using WiFiManager
  WiFiManager wm;
  WiFiManagerParameter param_url("octourl", "Base URL or Kasa IP", "", 200);
  WiFiManagerParameter param_key("apikey", "API Key (or unused for Kasa)", "", 100);
  WiFiManagerParameter param_gcode("gcode", "GCODE or Kasa Action (on/off/on0/off1)", "M112", 100);
  WiFiManagerParameter param_type("type", "Server Type (octo/moon/kasa)", "octo", 20);
  
  wm.addParameter(&param_url);
  wm.addParameter(&param_key);
  wm.addParameter(&param_gcode);
  wm.addParameter(&param_type);
  
  // Load saved parameters
  loadConfig();
  
  // Set parameter defaults from loaded config if available
  if (!baseURL.isEmpty()) {
    param_url.setValue(baseURL.c_str(), 200);
  }
  if (!apiKey.isEmpty()) {
    param_key.setValue(apiKey.c_str(), 100);
  }
  if (!gcode.isEmpty()) {
    param_gcode.setValue(gcode.c_str(), 100);
  }
  if (!serverType.isEmpty()) {
    param_type.setValue(serverType.c_str(), 20);
  }
  
  // Save parameters callback
  wm.setSaveParamsCallback([&]() {
    Serial.println("WiFiManager params saved");
    saveConfig(
      param_url.getValue(),
      param_key.getValue(),
      param_gcode.getValue(),
      param_type.getValue()
    );
  });
  
  // Start WiFi configuration portal if needed
  if (!wm.autoConnect("EstopConfigAP")) {
    Serial.println("WiFiManager failed. Rebooting...");
    delay(3000);
    ESP.restart();
  }
  
  // Save parameters if they were updated during autoConnect
  if (String(param_url.getValue()).length() > 0) {
    saveConfig(
      param_url.getValue(),
      param_key.getValue(),
      param_gcode.getValue(),
      param_type.getValue()
    );
    
    // Reload the configuration
    loadConfig();
  }
  
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Quick blink to indicate ready state
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, LED_ON);
    delay(50);
    digitalWrite(LED_PIN, LED_OFF);
    delay(50);
  }
  
  // If we're in Kasa mode, query the device info once at startup
  if (serverType.equalsIgnoreCase("kasa") && !baseURL.isEmpty()) {
    String deviceId;
    String childIds[8];
    int numChildren;
    getKasaDeviceInfo(baseURL, deviceId, childIds, numChildren);
  }
}

void loop() {
  // Read button state with debounce
  bool reading = digitalRead(BUTTON_PIN);
  
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  
  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
    if (reading == LOW && !buttonPressed) {
      buttonPressed = true;
      Serial.println("Button pressed - sending command");
      sendCommand();
    } else if (reading == HIGH) {
      buttonPressed = false;
    }
  }
  
  lastButtonState = reading;
  
  // Handle WiFi reconnection if needed
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection lost. Reconnecting...");
    WiFi.reconnect();
    delay(5000);
  }
}