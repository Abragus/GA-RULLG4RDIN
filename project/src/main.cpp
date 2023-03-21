#ifndef MAIN
#define MAIN

/**********

 * Some code copied from: https://lastminuteengineers.com/creating-esp32-web-server-arduino-ide/#configuring-the-esp32-web-server-in-wifi-station-sta-mode 
 * Also used this: https://randomnerdtutorials.com/esp32-vs-code-platformio-spiffs/ (and some other pages on the same website)
 * 
 * 
 * Current state: Can run a web server and allow connected clients to control connected motor position and speed

**********/

// #include "interface.h"

#include <stdlib.h>
#include <Arduino.h>

#include <ArduinoJson.h>

#include <multiLog.h>
/* Network credentials are stored in network_credentials.h, enter them there */
//-- Network related --//
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
//Local mDNS
#include <ESPmDNS.h>
// HTTP post for rullgardin.duckdns.org
#include <HTTPClient.h>
/* Network credentials are stored in network_credentials.h, enter them there */
#include "network_credentials.h"
// Wireless log output
#include <WebSerial.h>

// OTA updates
#include <AsyncElegantOTA.h>
#define OTA_USERNAME ""
#define OTA_PASSWORD ""

// File system
#include "SPIFFS.h"

#include "rullgardin.h"

#include <map>

#define ONBOARD_LED  2

// Remote server, where to upload internal IP
String serverURL = "http://rullgardin.duckdns.org/update-ip";


const char* ssid[] = WIFI_SSID;
const char* password[] = WIFI_PASSWORD;
const char* id[] = WIFI_ID;

/*** Config ***/
const uint8_t wifi_connect_seconds_timeout_per_network = 4; // Seconds
const uint8_t wifi_scan_tries = 2; // How many scans it should do before giving up
const uint8_t wifi_scan_delay_seconds = 5; // How long to wait between scans



#define DEBUG 0

AsyncWebServer  server(80);
AsyncWebSocket ws("/ws");

// Pre-declare functions to allow mentioning them before they are defined
bool setup_wifi_success();
bool connect_wifi_network(String ssid, String password, String id);
void send_ip_to_remote_server();

void sendWebSocket(DynamicJsonDocument config, int32_t client_id = -1);
void sendFullConfigWebSocket(int32_t client_id = -1);
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len);
void initWebSocket();

void handle_OnConnect();
void handle_auto();
void handle_up();
void handle_down();
void handle_speed(String url);
void handle_NotFound();
void handle_position(String url);
void recvMsg(uint8_t *data, size_t len);

String SendHTML();

Rullgardin rullgardin = Rullgardin();

void flash_led(uint8_t flashes, uint16_t on_time, uint16_t off_time);

void setup() {
  Serial.begin(115200);

  pinMode(ONBOARD_LED,OUTPUT);
  
  MultiLog.println("");

  if (!setup_wifi_success())
    MultiLog.println("Network connection failed, continuing in offline mode (which is the exact same thing as online mode).");

  sendFullConfigWebSocket();
}

void loop() {
  static long last_websocket_cleanup = millis();
  // static long last_position_log = millis();
  static long last_position_update = millis();
  static int position_when_last_checked;
  static int current_position;
  
  if (rullgardin.run()) {
    digitalWrite(ONBOARD_LED, HIGH);
    return;
  } // else
  
  digitalWrite(ONBOARD_LED, LOW);

  if (millis() - last_websocket_cleanup > 5*1000) {
    // MultiLog.println("Cleaned up WebSocket clients.");
    ws.cleanupClients();
    last_websocket_cleanup = millis();
  }  

  // rullgardin.run();

  current_position = rullgardin.get_position();
  if (current_position != position_when_last_checked) {
    StaticJsonDocument<128> config;
    config["position"] = current_position;
    sendWebSocket(config);    
    position_when_last_checked = current_position;
    last_position_update = millis();
  }
  
    // if (millis() - last_position_log > 1000) {
    //   MultiLog.println("Position: " + String(rullgardin.get_position()) + ", last position: " + String(position_when_last_checked)); 
    //   last_position_log = millis();
    // }
}

bool setup_wifi_success() {
  // If only one network is configured, skip WiFi scan and connect immediately 
  if (sizeof(ssid) / sizeof(ssid[0]) == 1)
      return connect_wifi_network(ssid[0], password[0], id[0]);    

  // Do up to wifi_scan_tries scans
  for (uint8_t s = 0; s < wifi_scan_tries; s++) {
    MultiLog.println("Scanning for available WiFi networks... ");
    flash_led(2, 100, 100);

    uint8_t networks_found = WiFi.scanNetworks();

    if (networks_found == 0) 
      goto noNetworksFound;

    delay(100); // Probably not needed
    
    // Print out network names
    MultiLog.print(networks_found + " networks found: ");
    for (uint8_t j = 0; j < min((int)networks_found, 10); j++)
      MultiLog.println(": " + WiFi.SSID(j));
    if (networks_found > 10)
      MultiLog.print("...and " + String(networks_found - 10) + " more");
    MultiLog.print("\n");

    // Check networks found in scan for ones provided in network_credentials.h
    for (uint8_t i = 0; i < sizeof(ssid) / sizeof(ssid[0]); i++) {  // Loop through network_credentials.h entries...
      for (uint8_t j = 0; j < networks_found; j++) {  // Loop through scan results...
        // When matching entry found, try connecting
        if (WiFi.SSID(j) == ssid[i] && connect_wifi_network(ssid[i], password[i], id[i])) 
          goto wifiConnected;
      }
    }
    // If no networks were found, or none were connected to
  noNetworksFound: 
    MultiLog.print("No networks found. Scanning again in " + String(wifi_scan_delay_seconds) + " seconds.");
    delay(wifi_scan_delay_seconds * 1000);
  }
  wifiConnected:
  MultiLog.println("\n\nWiFi connected!");
  MultiLog.print(": IP address: ");  
  MultiLog.println(WiFi.localIP().toString());
  flash_led(3, 100, 100);
  
  if(!MDNS.begin("rullgardin")) 
    MultiLog.println(": Error starting mDNS. ");
  else 
    MultiLog.println(": Also available at: rullgardin.local");
  
  send_ip_to_remote_server();

  // Setup Server //
  // Handle button presses
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){  // Redirect mDNS connections to internal IP due to encountering delays when using mDNS
    // request->redirect("http://" + WiFi.localIP().toString() + "/home");
    request->redirect("/home");
  });
  server.on("/home", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", SendHTML());
  });
  server.on("/auto", [](AsyncWebServerRequest *request){
    handle_auto();
    request->send(200);
  });
  server.on("/up", [](AsyncWebServerRequest *request){
    handle_up();
    request->send(200);
  });
  server.on("/down", [](AsyncWebServerRequest *request){
    handle_down();
    request->send(200);
  });
  server.on("/speed", [](AsyncWebServerRequest *request){
    handle_speed(request->url().c_str());
    request->send(200);
  });
  server.on("/position", [](AsyncWebServerRequest *request){
    handle_position(request->url().c_str());
    request->send(200);
  });

  initWebSocket();

  AsyncElegantOTA.begin(&server, OTA_USERNAME, OTA_PASSWORD);    // Start ElegantOTA
  
  WebSerial.begin(&server);
  WebSerial.msgCallback(recvMsg);
  MultiLog.set_web_serial_enabled(true);

  server.begin();
  MultiLog.println(": HTTP server started. ");

  return true;
}

void sendWebSocket(DynamicJsonDocument config, int32_t client_id) {
  String message;
  serializeJson(config, message);
  
  if (client_id == -1) {
    ws.textAll(message);
    MultiLog.println("Send WebSocket message: " + message);
  } else {
    ws.text(client_id, message);
    MultiLog.println("Send WebSocket message to client #" + String(client_id) + ": " + message);
  }
}

void sendFullConfigWebSocket(int32_t client_id) {
  StaticJsonDocument<128> config;
  config["speed"] = rullgardin.get_speed();
  config["position"] = rullgardin.get_position();
  config["maxSteps"] = rullgardin.get_max_steps();
  sendWebSocket(config, client_id);
}
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    if (strcmp((char*)data, "toggle") == 0) {
      // sendWebSocket();
      return;
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      sendFullConfigWebSocket(client->id());
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

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

void recvMsg(uint8_t *data, size_t len){
  MultiLog.println("Received Data...");
  String d = "";
  for(int i=0; i < len; i++){
    d += char(data[i]);
  }
  MultiLog.println(d);
}

bool connect_wifi_network(String ssid, String password, String id="") {
  if (id != "") { // Skolans nät (som kräver användarnamn (id) + lösen)
    MultiLog.print("Connecting to WPA2 network: "); 
    WiFi.begin(ssid.c_str(), WPA2_AUTH_PEAP, "", id.c_str(), password.c_str());
  } else {
    // Connect to provided WiFi network
    MultiLog.print("Connecting to: ");
    WiFi.begin(ssid.c_str(), password.c_str());
  }
  MultiLog.println(ssid.c_str());

  // Wait until connected
  MultiLog.print("Connecting...");
  for (uint8_t m = 1; m <= wifi_connect_seconds_timeout_per_network; m++) { 
    // Blink light (takes 1 second)
    flash_led(1, 500, 500);

    if (WiFi.status() == WL_CONNECTED)
      return true;

    // Connection timeout
    else if (m == wifi_connect_seconds_timeout_per_network) {
      MultiLog.println("");
      MultiLog.print("WiFi connection timeout. \n");
      WiFi.disconnect();
      delay(1000);
      return false;
    } else  // Keep waiting
      MultiLog.print(".");
  }
  return false;
}

void send_ip_to_remote_server() {
  HTTPClient http;

  String serverPath = serverURL + "?newip=" + WiFi.localIP().toString();
  
  // Prepare http request
  http.begin(serverPath.c_str());
  
  MultiLog.println(": Sending http GET request: " + serverPath);
  int httpResponseCode = http.GET();
  if (httpResponseCode)
    MultiLog.println(": IP uploaded to remote server.");
  else
    MultiLog.println(": Failed to upload IP to remote server.");
}

void handle_auto() {
  rullgardin.stop();
  MultiLog.println("Stopping.");
}

void handle_up() {
  StaticJsonDocument<128> config;
  config["movingTo"] = 0;
  sendWebSocket(config);
  rullgardin.open();
  MultiLog.println("Moving up");
}

void handle_down() {
  StaticJsonDocument<128> config;
  config["movingTo"] = 100;
  sendWebSocket(config);
  rullgardin.close();
  MultiLog.println("Moving down");
}

void handle_speed(String url) {
  uint16_t slider_position = url.substring(-1, 7).toInt(); // Remove '/speed/' from url
  
  if (rullgardin.set_speed(slider_position)) {
  #if DEBUG
    multiLog.println("Setting speed: " + String(slider_position));
  } else {
    multiLog.println("Failed setting speed: " + String(slider_position));
  #else
    return;
  #endif
  }
}

void handle_position(String url) {
  uint16_t slider_position = url.substring(-1, 10).toInt(); // Remove '/position/' from url
  
  StaticJsonDocument<128> config;
  config["movingTo"] = slider_position;
  sendWebSocket(config);

  rullgardin.move_to_position(slider_position);
}

// Prepares HTML code to send to client, from PCInterface.html
String SendHTML(){

  // Mount HTML file
  if(!SPIFFS.begin(true)){
    MultiLog.println("An Error has occurred while mounting SPIFFS");
    return "";
  }

  File html_file = SPIFFS.open("/PCInterface.html");
  String html_string; // String for HTML file to return to client

  // Read HTML file to string
  if(!html_file){
    MultiLog.println("Failed to open file for reading");
  } else {
    while(html_file.available()){
      html_string += (char)html_file.read();
    }
  }
  html_file.close();

  // If HTML file successfully loaded, return it 
  if (html_string != "")
    html_string.replace("___serverip___", WiFi.localIP().toString());
    return html_string;
  
  // Default HTML page, "relic"-ish, useful for when file loading failed
  MultiLog.println("Failed loading HTML file. Returning default page. ");
  html_string = "<style>html { font-family: Helvetica; } </style>\n";
  html_string +="<h1>ESP32 Web Server</h3>\n";
  html_string +="<h3>Error: No HTML file was loaded</h3>\n";

  return html_string;
}

void flash_led(uint8_t flashes, uint16_t on_time, uint16_t off_time) {
  for (uint8_t i = 0; i < flashes; i++) {
      digitalWrite(ONBOARD_LED, HIGH);
      delay(on_time);
      digitalWrite(ONBOARD_LED, LOW);
      delay(off_time);
  }
}

#endif