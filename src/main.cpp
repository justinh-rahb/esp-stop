#include <functional>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>

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
#define CONFIG_HOLD_MS  1500

String baseURL, apiKey, gcode, serverType;
unsigned long lastDebounceTime = 0;
bool lastButtonState = HIGH;
bool buttonPressed = false;
unsigned long buttonPressStart = 0;

// Web server for persistent configuration access
ESP8266WebServer webServer(80);

// MQTT client for Bambu printers
WiFiClientSecure espSecureClient;
PubSubClient mqttClient(espSecureClient);

// Function declarations
bool sendRawKasaCommand(const String& ip, const String& json, bool infoOnly = false);
bool sendJsonAndGetResponse(WiFiClient& client, const String& ip, int port, const String& json,
                          std::function<void(const String&)> responseProcessor);
bool sendKasaCommand(const String& command);
bool sendOctoPrintCommand(const String& gcode);
bool sendMoonrakerCommand(const String& gcode);
bool sendBambuCommand(const String& serial);
void sendCommand();
void checkButtonHold();
void handleButtonRelease();
void saveConfig(const String& url, const String& key, const String& code, const String& type);
void loadConfig();
void parseKasaCommand(const String& command, int& outletNum, bool& turnOn);
void dumpHex(const uint8_t* buffer, size_t len);
bool getKasaDeviceInfo(const String& ip, String& deviceId, String childIds[], int& numChildren);
void setupWebServer();
void handleRoot();
void handleConfig();
void handleSave();
void handleReset();

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
      unsigned int index = childrenStart + 12; // Skip over "children":[
      int braceCount = 0;
      unsigned int childIndex = 0;

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
      Serial.println(" children (multi-outlet device)");
    } else {
      // No children array found - this is a single outlet device
      Serial.println("No children found - single outlet device");
      numChildren = 0;
    }
  }

  client.stop();
  return true; // Always return true if we got a response, even for single outlets
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

  // Query the device for its information
  if (!getKasaDeviceInfo(baseURL, deviceId, childIds, numChildren)) {
    Serial.println("Failed to get device info");
    return false;
  }

  // Handle single outlet devices (no children)
  if (numChildren == 0) {
    Serial.println("Single outlet device detected - using simple relay_state format");

    // For single outlets, ignore the outlet number and just control the main relay
    if (outletNum > 0) {
      Serial.print("Warning: Outlet ");
      Serial.print(outletNum);
      Serial.println(" requested but device is single outlet. Controlling main outlet.");
    }

    // Simple format for single outlet: no context, no child_ids
    String json = "{\"system\":{\"set_relay_state\":{\"state\":" +
                 String(turnOn ? 1 : 0) + "}}}";

    Serial.print("Sending command to single outlet: ");
    Serial.println(json);

    return sendRawKasaCommand(baseURL, json, false);
  }

  // Handle multi-outlet devices (with children)
  Serial.print("Multi-outlet device with ");
  Serial.print(numChildren);
  Serial.println(" outlets");

  // Check if requested outlet exists
  if (outletNum >= numChildren) {
    Serial.print("Error: Outlet ");
    Serial.print(outletNum);
    Serial.print(" requested but device only has ");
    Serial.print(numChildren);
    Serial.println(" outlets");

    // Try special handling for KP200 dual-outlet quirks
    if (outletNum == 1 && numChildren == 1) {
      Serial.println("KP200 dual-outlet quirk detected - trying special methods");

      // Method 1: Try derived child ID
      if (!childIds[0].isEmpty() && childIds[0].length() >= 2) {
        String secondOutletId = childIds[0].substring(0, childIds[0].length()-2) + "01";

        Serial.print("Method 1: Trying derived ID: ");
        Serial.println(secondOutletId);

        String json = "{\"context\":{\"child_ids\":[\"" + secondOutletId +
                     "\"]},\"system\":{\"set_relay_state\":{\"state\":" +
                     String(turnOn ? 1 : 0) + "}}}";

        if (sendRawKasaCommand(baseURL, json, false)) {
          return true;
        }
      }

      // Method 2: Try numeric index
      Serial.println("Method 2: Trying numeric index [1]");
      String json = "{\"context\":{\"child_ids\":[1]},\"system\":{\"set_relay_state\":{\"state\":" +
                   String(turnOn ? 1 : 0) + "}}}";

      if (sendRawKasaCommand(baseURL, json, false)) {
        return true;
      }

      // Method 3: Try outlet parameter
      Serial.println("Method 3: Trying outlet parameter");
      json = "{\"system\":{\"set_relay_state\":{\"state\":" + String(turnOn ? 1 : 0) +
             ",\"outlet\":1}}}";

      if (sendRawKasaCommand(baseURL, json, false)) {
        return true;
      }

      Serial.println("All KP200 methods failed");
    }

    return false;
  }

  // Send command to specific outlet using child_ids
  String json = "{\"context\":{\"child_ids\":[\"" + childIds[outletNum] +
               "\"]},\"system\":{\"set_relay_state\":{\"state\":" +
               String(turnOn ? 1 : 0) + "}}}";

  Serial.print("Sending command to outlet ");
  Serial.print(outletNum);
  Serial.print(" (ID: ");
  Serial.print(childIds[outletNum]);
  Serial.print("): ");
  Serial.println(json);

  return sendRawKasaCommand(baseURL, json, false);
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
  
  // Check if this is an emergency stop command
  String upperGcode = gcode;
  upperGcode.toUpperCase();
  upperGcode.trim();
  bool isEmergencyStop = (upperGcode == "M112" || upperGcode.startsWith("M112 "));
  
  if (isEmergencyStop) {
    // For emergency stop, disconnect first (works regardless of printer state)
    Serial.println("Emergency stop detected - disconnecting printer via connection API");
    String url = baseURL + "/api/connection";
    String payload = "{\"command\": \"disconnect\"}";
    
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Api-Key", apiKey);
    
    int httpCode = http.POST(payload);
    
    if (httpCode > 0) {
      Serial.printf("Disconnect HTTP response: %d\n", httpCode);
      if (httpCode == HTTP_CODE_NO_CONTENT || httpCode == HTTP_CODE_OK) {
        Serial.println("Printer disconnected successfully");
        success = true;
      }
    } else {
      Serial.printf("Disconnect HTTP error: %s\n", http.errorToString(httpCode).c_str());
    }
    
    http.end();
    
    // Also try sending M112 gcode as fallback (in case printer was operational)
    if (!success) {
      Serial.println("Disconnect failed, falling back to M112 gcode command...");
      String gcodeUrl = baseURL + "/api/printer/command";
      String gcodePayload = "{\"command\": \"M112\"}";
      
      http.begin(client, gcodeUrl);
      http.addHeader("Content-Type", "application/json");
      http.addHeader("X-Api-Key", apiKey);
      
      httpCode = http.POST(gcodePayload);
      if (httpCode > 0) {
        Serial.printf("M112 fallback HTTP response: %d\n", httpCode);
        if (httpCode == HTTP_CODE_NO_CONTENT || httpCode == HTTP_CODE_OK) {
          success = true;
        }
      } else {
        Serial.printf("M112 fallback HTTP error: %s\n", http.errorToString(httpCode).c_str());
      }
      
      http.end();
    }
  } else {
    // Regular gcode command
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
  }
  
  return success;
}

// Send command to Moonraker/Klipper server
bool sendMoonrakerCommand(const String& gcode) {
  WiFiClient client;
  HTTPClient http;
  bool success = false;

  Serial.print("Sending to Moonraker: ");
  Serial.println(gcode);

  // Check if this is an emergency stop command (M112)
  String upperGcode = gcode;
  upperGcode.toUpperCase();
  upperGcode.trim();

  String url;
  String payload;

  if (upperGcode == "M112" || upperGcode.startsWith("M112 ")) {
    // Use emergency_stop endpoint for immediate shutdown (not queued)
    Serial.println("Emergency stop detected - using /printer/emergency_stop endpoint");
    url = baseURL + "/printer/emergency_stop";
    payload = ""; // Emergency stop doesn't need a payload

    http.begin(client, url);

    // Moonraker uses Bearer token authentication
    if (!apiKey.isEmpty()) {
      http.addHeader("Authorization", "Bearer " + apiKey);
    }

    int httpCode = http.POST(payload);

    if (httpCode > 0) {
      String response = http.getString();
      Serial.printf("Emergency stop HTTP response: %d\n", httpCode);
      Serial.println("Response: " + response);
      if (httpCode == HTTP_CODE_OK) {
        success = true;
      } else if (httpCode == 404) {
        // Klipper may not be connected - endpoint not registered
        // Fall back to gcode/script endpoint
        Serial.println("Emergency stop endpoint not found (Klipper disconnected?). Falling back to gcode/script...");
        http.end();
        
        url = baseURL + "/printer/gcode/script";
        payload = "{\"script\": \"M112\"}";
        http.begin(client, url);
        http.addHeader("Content-Type", "application/json");
        if (!apiKey.isEmpty()) {
          http.addHeader("Authorization", "Bearer " + apiKey);
        }
        httpCode = http.POST(payload);
        if (httpCode > 0) {
          response = http.getString();
          Serial.printf("Fallback gcode/script HTTP response: %d\n", httpCode);
          Serial.println("Response: " + response);
          if (httpCode == HTTP_CODE_OK) {
            success = true;
          }
        } else {
          Serial.printf("Fallback HTTP error: %s\n", http.errorToString(httpCode).c_str());
        }
      }
    } else {
      Serial.printf("Emergency stop HTTP error: %s\n", http.errorToString(httpCode).c_str());
    }
  } else {
    // Use gcode/script endpoint for regular commands (queued)
    url = baseURL + "/printer/gcode/script";
    payload = "{\"script\": \"" + gcode + "\"}";

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
  }

  http.end();
  return success;
}

// Send emergency stop command to Bambu printer via MQTT
bool sendBambuCommand(const String& serial) {
  bool success = false;

  if (serial.isEmpty()) {
    Serial.println("Bambu printer serial number not configured");
    return false;
  }

  if (apiKey.isEmpty()) {
    Serial.println("Bambu access code not configured");
    return false;
  }

  Serial.println("Attempting Bambu emergency stop via MQTT...");
  Serial.printf("Printer IP: %s\n", baseURL.c_str());
  Serial.printf("Serial: %s\n", serial.c_str());

  // Configure TLS (disable certificate verification for Bambu self-signed cert)
  espSecureClient.setInsecure();

  // Set MQTT server and credentials
  mqttClient.setServer(baseURL.c_str(), 8883);
  mqttClient.setClient(espSecureClient);

  // Connect to MQTT broker
  String clientId = "esp-stop-" + String(ESP.getChipId());

  if (mqttClient.connect(clientId.c_str(), "bblp", apiKey.c_str())) {
    Serial.println("Connected to Bambu MQTT broker");

    // Prepare stop command
    String topic = "device/" + serial + "/request";
    String payload = "{\"print\":{\"command\":\"stop\",\"param\":\"\"}}";

    Serial.printf("Publishing to topic: %s\n", topic.c_str());
    Serial.printf("Payload: %s\n", payload.c_str());

    // Publish emergency stop command
    if (mqttClient.publish(topic.c_str(), payload.c_str())) {
      Serial.println("Emergency stop command sent successfully");
      success = true;
    } else {
      Serial.println("Failed to publish MQTT message");
    }

    mqttClient.disconnect();
  } else {
    Serial.printf("MQTT connection failed, state: %d\n", mqttClient.state());
  }

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
  else if (serverType.equalsIgnoreCase("bambu")) {
    success = sendBambuCommand(gcode);
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

// Check for button hold to trigger config portal or factory reset
void checkButtonHold() {
  if (buttonPressStart == 0) return;
  
  unsigned long elapsed = millis() - buttonPressStart;

  // Fast blink while holding
  digitalWrite(LED_PIN, (elapsed / 100) % 2 == 0 ? LED_ON : LED_OFF);

  // 1.5 seconds: Visual indicator - medium blink
  if (elapsed >= CONFIG_HOLD_MS && elapsed < RESET_HOLD_MS) {
    digitalWrite(LED_PIN, (elapsed / 200) % 2 == 0 ? LED_ON : LED_OFF);
  }

  // 3 seconds: Factory reset (immediate, don't wait for release)
  if (elapsed >= RESET_HOLD_MS) {
    Serial.println("Factory reset initiated (3s hold). Clearing EEPROM and rebooting...");

    // Rapid blink to indicate reset
    for (int i = 0; i < 10; i++) {
      digitalWrite(LED_PIN, LED_ON);
      delay(50);
      digitalWrite(LED_PIN, LED_OFF);
      delay(50);
    }

    EEPROM.begin(EEPROM_SIZE);
    for (int i = 0; i < EEPROM_SIZE; ++i) EEPROM.write(i, 0);
    EEPROM.commit();

    delay(500);
    ESP.restart();
  }
}

// Called from loop() when button is released
void handleButtonRelease() {
  unsigned long elapsed = millis() - buttonPressStart;
  digitalWrite(LED_PIN, LED_OFF);

  if (elapsed < CONFIG_HOLD_MS) {
    // Short press: send command
    Serial.println("Short press - sending command");
    sendCommand();
  } else if (elapsed < RESET_HOLD_MS) {
    // Medium hold (1.5-3s): config portal
    Serial.println("Config portal triggered (1.5s hold). Starting WiFiManager...");

    // Three slow blinks to indicate config mode
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_PIN, LED_ON);
      delay(300);
      digitalWrite(LED_PIN, LED_OFF);
      delay(300);
    }

    // Start WiFiManager config portal
    WiFiManager wm;
    WiFiManagerParameter param_url("octourl", "Base URL or Kasa IP", baseURL.c_str(), 200);
    WiFiManagerParameter param_key("apikey", "API Key (or unused for Kasa)", apiKey.c_str(), 100);
    WiFiManagerParameter param_gcode("gcode", "GCODE or Kasa Action (on/off/on0/off1)", gcode.c_str(), 100);
    WiFiManagerParameter param_type("type", "Server Type (octo/moon/kasa)", serverType.c_str(), 20);

    wm.addParameter(&param_url);
    wm.addParameter(&param_key);
    wm.addParameter(&param_gcode);
    wm.addParameter(&param_type);

    wm.setSaveParamsCallback([&]() {
      Serial.println("WiFiManager params saved");
      saveConfig(
        param_url.getValue(),
        param_key.getValue(),
        param_gcode.getValue(),
        param_type.getValue()
      );
    });

    // Start config portal (blocks until done)
    wm.startConfigPortal("EstopConfigAP");

    // Reload configuration after portal closes
    loadConfig();

    Serial.println("Config portal closed, resuming normal operation");
  }
  // If >= RESET_HOLD_MS, factory reset already fired while held

  buttonPressed = false;
  buttonPressStart = 0;
}

// Web server handlers
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:Arial;margin:20px;background:#f0f0f0}";
  html += ".container{max-width:600px;margin:0 auto;background:white;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}";
  html += "h1{color:#333;border-bottom:2px solid #007bff;padding-bottom:10px}";
  html += ".info{background:#e7f3ff;padding:15px;border-radius:4px;margin:10px 0}";
  html += ".label{font-weight:bold;color:#555}";
  html += ".value{color:#007bff;margin-left:10px}";
  html += "a{display:inline-block;margin:10px 5px;padding:10px 20px;background:#007bff;color:white;text-decoration:none;border-radius:4px}";
  html += "a:hover{background:#0056b3}.warn{background:#fff3cd;border-left:4px solid #ffc107;padding:10px;margin:10px 0}";
  html += "</style></head><body><div class='container'>";
  html += "<h1>ESP E-Stop Control</h1>";

  html += "<div class='info'>";
  html += "<div><span class='label'>WiFi:</span><span class='value'>Connected</span></div>";
  html += "<div><span class='label'>IP Address:</span><span class='value'>" + WiFi.localIP().toString() + "</span></div>";
  html += "<div><span class='label'>Signal Strength:</span><span class='value'>" + String(WiFi.RSSI()) + " dBm</span></div>";
  html += "</div>";

  html += "<div class='info'>";
  html += "<div><span class='label'>Server Type:</span><span class='value'>" + serverType + "</span></div>";
  html += "<div><span class='label'>Base URL:</span><span class='value'>" + baseURL + "</span></div>";
  html += "<div><span class='label'>Command:</span><span class='value'>" + gcode + "</span></div>";
  html += "<div><span class='label'>API Key:</span><span class='value'>" + String(apiKey.isEmpty() ? "[not set]" : "[configured]") + "</span></div>";
  html += "</div>";

  html += "<div class='warn'>";
  html += "<strong>Button Controls:</strong><br>";
  html += "&bull; Short press: Send command<br>";
  html += "&bull; Hold 1.5s: Open config portal<br>";
  html += "&bull; Hold 3s: Factory reset";
  html += "</div>";

  html += "<a href='/config'>Configure Settings</a>";
  html += "<a href='/reset' onclick='return confirm(\"Factory reset?\")'>Factory Reset</a>";
  html += "</div></body></html>";

  webServer.send(200, "text/html", html);
}

void handleConfig() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:Arial;margin:20px;background:#f0f0f0}";
  html += ".container{max-width:600px;margin:0 auto;background:white;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}";
  html += "h1{color:#333;border-bottom:2px solid #007bff;padding-bottom:10px}";
  html += "form{margin:20px 0}label{display:block;margin:15px 0 5px;font-weight:bold;color:#555}";
  html += "input,select{width:100%;padding:8px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}";
  html += "input[type=submit]{background:#28a745;color:white;border:none;padding:12px;margin-top:20px;cursor:pointer;font-size:16px}";
  html += "input[type=submit]:hover{background:#218838}.hint{font-size:12px;color:#666;margin-top:2px}";
  html += "</style></head><body><div class='container'>";
  html += "<h1>Configure E-Stop</h1>";
  html += "<form action='/save' method='POST'>";

  html += "<label>Server Type:</label>";
  html += "<select name='type'>";
  html += "<option value='octo'" + String(serverType.equalsIgnoreCase("octo") ? " selected" : "") + ">OctoPrint</option>";
  html += "<option value='moon'" + String(serverType.equalsIgnoreCase("moon") || serverType.equalsIgnoreCase("moonraker") ? " selected" : "") + ">Moonraker/Klipper</option>";
  html += "<option value='kasa'" + String(serverType.equalsIgnoreCase("kasa") ? " selected" : "") + ">TP-Link Kasa</option>";
  html += "<option value='bambu'" + String(serverType.equalsIgnoreCase("bambu") ? " selected" : "") + ">Bambu Lab</option>";
  html += "</select>";

  html += "<label>Base URL or IP Address:</label>";
  html += "<input type='text' name='url' value='" + baseURL + "' placeholder='http://192.168.1.100:7125 or 192.168.1.50'>";
  html += "<div class='hint'>Kasa/Bambu: IP only (e.g., 192.168.1.50). OctoPrint/Moonraker: full URL</div>";

  html += "<label>API Key / Access Code:</label>";
  html += "<input type='text' name='key' value='" + apiKey + "' placeholder='(unused for Kasa)'>";
  html += "<div class='hint'>OctoPrint: X-Api-Key, Moonraker: Bearer token, Bambu: Access Code (from printer settings)</div>";

  html += "<label>G-code / Command / Serial:</label>";
  html += "<input type='text' name='gcode' value='" + gcode + "' placeholder='M112 or on/off/on0/off1'>";
  html += "<div class='hint'>OctoPrint/Moonraker: M112 (triggers emergency_stop). Kasa: on/off/on0/off1. Bambu: Printer Serial Number</div>";

  html += "<input type='submit' value='Save Configuration'>";
  html += "</form>";
  html += "<a href='/' style='display:inline-block;margin:10px 0;color:#007bff'>&larr; Back</a>";
  html += "</div></body></html>";

  webServer.send(200, "text/html", html);
}

void handleSave() {
  if (webServer.hasArg("url") && webServer.hasArg("key") && webServer.hasArg("gcode") && webServer.hasArg("type")) {
    String url = webServer.arg("url");
    String key = webServer.arg("key");
    String cmd = webServer.arg("gcode");
    String type = webServer.arg("type");

    saveConfig(url, key, cmd, type);
    loadConfig();

    String html = "<!DOCTYPE html><html><head><meta http-equiv='refresh' content='3;url=/'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial;margin:20px;background:#f0f0f0;text-align:center}";
    html += ".container{max-width:400px;margin:50px auto;background:white;padding:30px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}";
    html += "h1{color:#28a745}</style></head><body><div class='container'>";
    html += "<h1>&#10003; Saved!</h1>";
    html += "<p>Configuration saved successfully.</p>";
    html += "<p>Redirecting...</p>";
    html += "</div></body></html>";

    webServer.send(200, "text/html", html);
  } else {
    webServer.send(400, "text/plain", "Missing parameters");
  }
}

void handleReset() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:Arial;margin:20px;background:#f0f0f0;text-align:center}";
  html += ".container{max-width:400px;margin:50px auto;background:white;padding:30px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}";
  html += "h1{color:#dc3545}</style></head><body><div class='container'>";
  html += "<h1>Resetting...</h1>";
  html += "<p>Factory reset in progress.</p>";
  html += "<p>Device will restart shortly.</p>";
  html += "</div></body></html>";

  webServer.send(200, "text/html", html);

  delay(1000);

  Serial.println("Factory reset via web interface. Clearing EEPROM...");
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < EEPROM_SIZE; ++i) EEPROM.write(i, 0);
  EEPROM.commit();

  delay(500);
  ESP.restart();
}

void setupWebServer() {
  webServer.on("/", handleRoot);
  webServer.on("/config", handleConfig);
  webServer.on("/save", HTTP_POST, handleSave);
  webServer.on("/reset", handleReset);

  webServer.begin();
  Serial.println("Web server started on port 80");
  Serial.print("Access at: http://");
  Serial.println(WiFi.localIP());
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
  Serial.println("2.0.0");

  // Configure WiFi using WiFiManager
  WiFiManager wm;
  WiFiManagerParameter param_url("octourl", "Base URL or IP Address", "", 200);
  WiFiManagerParameter param_key("apikey", "API Key / Access Code", "", 100);
  WiFiManagerParameter param_gcode("gcode", "GCODE / Command / Serial", "M112", 100);
  WiFiManagerParameter param_type("type", "Server Type (octo/moon/kasa/bambu)", "octo", 20);
  
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

  // Start persistent web server for configuration
  setupWebServer();

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

  Serial.println("\n=== E-Stop Ready ===");
  Serial.println("Button controls:");
  Serial.println("  Short press: Send command");
  Serial.println("  Hold 1.5s: Config portal");
  Serial.println("  Hold 3s: Factory reset");
  Serial.print("Web interface: http://");
  Serial.println(WiFi.localIP());
}

void loop() {
  // Handle web server requests
  webServer.handleClient();

  // Read button state with debounce
  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
    if (reading == LOW && !buttonPressed) {
      // Button just pressed
      buttonPressed = true;
      buttonPressStart = millis();
      Serial.println("Button pressed");
    } else if (reading == LOW && buttonPressed) {
      // Button held - check for config/reset triggers
      checkButtonHold();
    } else if (reading == HIGH && buttonPressed) {
      // Button released - handle all release actions in one place
      handleButtonRelease();
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