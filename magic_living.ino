#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <PersWiFiManager.h>
#include <ESP8266SSDP.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>
#include "Bounce2.h"
#include "DHT.h"
#include <AsyncDelay.h>
#include <ESP8266HTTPClient.h>
#include <EEPROM.h>
 
ESP8266WebServer server(80);
DNSServer dnsServer;
PersWiFiManager persWM(server, dnsServer);

const char *metaRefreshStr = "<script>window.location='/'</script><a href='/'>redirecting...</a>";

//WIFI
String command;
StaticJsonBuffer<200> jsonBuffer;
JsonObject& jsonCommand = jsonBuffer.createObject();

//SLEEP
char toChar[2500];
AsyncDelay wakeUpWifi;
unsigned long timeToSleep;
int wifiStatus;


//RELAY
#define LIGHT_LIVING_ROOM_PIN  D7
#define LIGHT_DINNING_ROOM_PIN  D8
#define BUTTON_PIN_RELAY_1  D2
#define BUTTON_PIN_RELAY_2  D3

int state_relay1 = LOW;       // estado actual del pin de salida 
int state_relay2 = LOW;       // estado actual del pin de sali

Bounce debouncer_relay1 = Bounce(); 
Bounce debouncer_relay2 = Bounce();

//PHOTOCELL
#define PHOTOCELL A0
int lastLightReading  = -1;
AsyncDelay readPhotocell;


//DHT
#define DHTPIN D1 
DHT dht(DHTPIN, DHT22);

//TURN OFF
#define BUTTON_TURN_OFF  D4
Bounce debouncer_turn_off = Bounce();

//OPENHAB REST
HTTPClient httpClient;
String openhabServer = "openhab:8080";

//code from fsbrowser example, consolidated.
bool handleFileRead(String path) {
  DEBUG_PRINT("handlefileread" + path);
  if (path.endsWith("/")) path += "index.htm";
  String contentType;
  if (path.endsWith(".htm") || path.endsWith(".html")) contentType = "text/html";
  else if (path.endsWith(".css")) contentType = "text/css";
  else if (path.endsWith(".js")) contentType = "application/javascript";
  else if (path.endsWith(".png")) contentType = "image/png";
  else if (path.endsWith(".gif")) contentType = "image/gif";
  else if (path.endsWith(".jpg")) contentType = "image/jpeg";
  else if (path.endsWith(".ico")) contentType = "image/x-icon";
  else if (path.endsWith(".xml")) contentType = "text/xml";
  else if (path.endsWith(".pdf")) contentType = "application/x-pdf";
  else if (path.endsWith(".zip")) contentType = "application/x-zip";
  else if (path.endsWith(".gz")) contentType = "application/x-gzip";
  else if (path.endsWith(".json")) contentType = "application/json";
  else contentType = "text/plain";

  //split filepath and extension
  String prefix = path, ext = "";
  int lastPeriod = path.lastIndexOf('.');
  if (lastPeriod >= 0) {
    prefix = path.substring(0, lastPeriod);
    ext = path.substring(lastPeriod);
  }

  //look for smaller versions of file
  //minified file, good (myscript.min.js)
  if (SPIFFS.exists(prefix + ".min" + ext)) path = prefix + ".min" + ext;
  //gzipped file, better (myscript.js.gz)
  if (SPIFFS.exists(prefix + ext + ".gz")) path = prefix + ext + ".gz";
  //min and gzipped file, best (myscript.min.js.gz)
  if (SPIFFS.exists(prefix + ".min" + ext + ".gz")) path = prefix + ".min" + ext + ".gz";

  if (SPIFFS.exists(path)) {
    DEBUG_PRINT("sending file " + path);
    File file = SPIFFS.open(path, "r");
    if (server.hasArg("download"))
      server.sendHeader("Content-Disposition", " attachment;");
    if (server.uri().indexOf("nocache") < 0)
      server.sendHeader("Cache-Control", " max-age=172800");

    //optional alt arg (encoded url), server sends redirect to file on the web
    if (WiFi.status() == WL_CONNECTED && server.hasArg("alt")) {
      server.sendHeader("Location", server.arg("alt"), true);
      server.send ( 302, "text/plain", "");
    } else {
      //server sends file
      size_t sent = server.streamFile(file, contentType);
    }
    file.close();
    return true;
  } //if SPIFFS.exists
  return false;
} //bool handleFileRead

/**
 * http invoke to handle the light
 * example 
 * curl --header "Content-Type: application/json"   --request POST   --data '{light1:{"state":"ON"},light2:{"state":"OFF"}}'   http://living/light
 * number can be 1 or 2
 * state is 0 to OFF and 1 to ON
 */
void handleLight() {
  digitalWrite(LED_BUILTIN, LOW);
  
  StaticJsonBuffer<200> newBuffer;
  JsonObject& request = newBuffer.parseObject(server.arg("plain"));
  String stateLight1  = request["light1"]["state"];

  digitalWrite(LIGHT_LIVING_ROOM_PIN,stateLight1.equals("ON")?1:0);

  delay(200);
  String stateLight2  = request["light2"]["state"];
  digitalWrite(LIGHT_DINNING_ROOM_PIN,stateLight2.equals("ON")?1:0);

  server.send(200, "application/json", "{success:true}");
  delay(200);                    
  digitalWrite(LED_BUILTIN, HIGH);
}

/**
 * get in json format all the sensors values
 * example http://192.168.1.4/sensors
 */
void handleSensors() {
  digitalWrite(LED_BUILTIN, LOW);

 command = "";
 jsonCommand["temperature"] = dht.readTemperature();
 jsonCommand["humidity"] = dht.readHumidity();
 jsonCommand["light1"] = digitalRead(LIGHT_LIVING_ROOM_PIN)?"ON":"OFF";
 jsonCommand["light2"] = digitalRead(LIGHT_DINNING_ROOM_PIN)?"ON":"OFF";
 jsonCommand["lightLevel"] = map(analogRead(PHOTOCELL), 0, 1023, 0, 100);

 jsonCommand.printTo(command);
 server.send(200, "text/html", command);
 delay(200);                    
 digitalWrite(LED_BUILTIN, HIGH);
}

/**
 * store the config data separated by semicolon
 * 1 - openhab location i.e. openhab:8080
 */
void handleConfig(){
  openhabServer = server.arg("openhabServer");
  char arrayToStore[20];                    // Must be greater than the length of string.
  openhabServer.toCharArray(arrayToStore, openhabServer.length()+1);  // Convert string to array.
  EEPROM.put(0, arrayToStore);                 // To store data
  for (int i = 0; i < openhabServer.length(); ++i){
        EEPROM.write(i,  (uint8_t) openhabServer[i]);
  }
  EEPROM.write(openhabServer.length(),  (uint8_t) ';');
  EEPROM.commit();

  server.send(200, "text/html", "OK");
  
}

/* get time to sleep the wifi
 *  example curl --header "Content-Type: application/json"   --request POST   --data '{"minutes":1,"seconds":10}'   http://living/sleep
 * 
 */
void handleSleep(){
    StaticJsonBuffer<200> newBuffer;
    JsonObject& request = newBuffer.parseObject(server.arg("plain"));
    int seconds = request["seconds"];
    int minutes = request["minutes"];
    int hours = request["hours"];
 
    timeToSleep = seconds * 1000;
    timeToSleep += minutes * 60 * 1000;
    timeToSleep += hours * 60 * 60 * 1000;
    server.send ( 200, "text/json", "{success:true}" );
  
    wakeUpWifi.start(timeToSleep, AsyncDelay::MILLIS);  
    delay(500);
    WiFi.mode(WIFI_OFF);
   
}


void setup() {
  Serial.begin(115200);
  // read persisted parameters parameter 
  EEPROM.begin(512);
  char arrayToStore[20];  
  openhabServer = "";
  for(int i =0;i<100;i++){
    char c = (char)EEPROM.read(i);
    Serial.println(c);
    if(c != ';'){
      openhabServer += c;
    }
    else{
      break;
    }
  }
  Serial.println(openhabServer);
  
  SPIFFS.begin();
  persWM.begin();

  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output

  //if you get here you have connected to the WiFi
  Serial.println("connected!");


  Serial.println("local ip");
  Serial.println(WiFi.localIP());

  server.on("/light", handleLight);
  server.on("/sleep", handleSleep);
  server.on("/sensors", handleSensors);
  server.on("/config", handleConfig);

  //RELAY
  pinMode(LIGHT_LIVING_ROOM_PIN,  OUTPUT) ;
  pinMode(LIGHT_DINNING_ROOM_PIN,  OUTPUT) ;

  pinMode(BUTTON_PIN_RELAY_1, INPUT_PULLUP);
  debouncer_relay1.attach(BUTTON_PIN_RELAY_1);
  debouncer_relay1.interval(5);

  pinMode(BUTTON_PIN_RELAY_2, INPUT_PULLUP);
  debouncer_relay2.attach(BUTTON_PIN_RELAY_2);
  debouncer_relay2.interval(5);

  //DHT
  dht.begin();

  //PHOTOCELL
  readPhotocell.start(1000, AsyncDelay::MILLIS);
  
  //TURN OFF
  pinMode(BUTTON_TURN_OFF, INPUT_PULLUP);
  debouncer_turn_off.interval(5);
  debouncer_turn_off.attach(BUTTON_TURN_OFF);

  digitalWrite(LED_BUILTIN, HIGH);


  //serve files from SPIFFS
  server.onNotFound([]() {
    if (!handleFileRead(server.uri())) {
      server.sendHeader("Cache-Control", " max-age=172800");
      server.send(302, "text/html", metaRefreshStr);
    }
  });
   //SSDP makes device visible on windows network
  server.on("/description.xml", HTTP_GET, []() {
    SSDP.schema(server.client());
  });
  SSDP.setSchemaURL("description.xml");
  SSDP.setHTTPPort(80);
  SSDP.setName("chupamela");
  SSDP.setURL("/");
  SSDP.begin();
  SSDP.setDeviceType("upnp:rootdevice");

  
  server.begin();


}



/**
 * check the if the buttons were pressed to
 * change the relays status or send command to openhab
 * 
 */
void checkButtons(){


  debouncer_relay1.update();
  
  // Get the update value
  int value = debouncer_relay1.read();
  if (value != state_relay1 && value==0) {
     digitalWrite(LED_BUILTIN, LOW);
     digitalWrite(LIGHT_LIVING_ROOM_PIN , !digitalRead(LIGHT_LIVING_ROOM_PIN));  
     if(wifiStatus == WL_CONNECTED ){
      httpClient.begin("http://" + openhabServer + "/rest/items/LightLivingRoom/state");
      httpClient.PUT(digitalRead(LIGHT_LIVING_ROOM_PIN)==HIGH?"ON":"OFF");
      httpClient.end();
     }
     digitalWrite(LED_BUILTIN, HIGH);
  }
  state_relay1 = value;
  
  debouncer_relay2.update();
  // Get the update value
  value = debouncer_relay2.read();
  if (value != state_relay2 && value==0) {
     digitalWrite(LED_BUILTIN, LOW);
     digitalWrite(LIGHT_DINNING_ROOM_PIN, !digitalRead(LIGHT_DINNING_ROOM_PIN));   
     if(wifiStatus == WL_CONNECTED ){
      httpClient.begin("http://" + openhabServer + "/rest/items/LightDinningRoom/state");
      httpClient.PUT(digitalRead(LIGHT_DINNING_ROOM_PIN)==HIGH?"ON":"OFF");
      httpClient.end();
     }
     digitalWrite(LED_BUILTIN, HIGH);
  }

  state_relay2 = value;

  debouncer_turn_off.update();
  // Get the update value
  value = debouncer_turn_off.read();
  if (value==0) {
     digitalWrite(LED_BUILTIN, LOW);
     if(wifiStatus == WL_CONNECTED){ 
      httpClient.begin("http://" + openhabServer + "/rest/items/Living_Mode/state");
      httpClient.PUT("TURN_OFF");
      httpClient.end();
     }
     digitalWrite(LED_BUILTIN, HIGH);

  }
  
}



/* evaluate light status
 *  if there is any change send to openhab
 *  the light status
 * 
 */

void evaluateLight(){
    int photocellReading  = map(analogRead(PHOTOCELL), 0, 1023, 0, 100);
     if(abs(lastLightReading - photocellReading) > 2){
        lastLightReading = photocellReading;
        String str = String(lastLightReading);
        char  output[5];
        str.toCharArray(output,5);
        httpClient.begin("http://" + openhabServer + "/rest/items/Living_Light/state");
        httpClient.PUT(output);
        httpClient.end();
      }
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  
  checkButtons();
 
  wifiStatus = WiFi.status();

  if (wifiStatus != WL_CONNECTED && readPhotocell.isExpired()  ) {
    evaluateLight();
    readPhotocell.repeat();
  }

  if (wifiStatus != WL_CONNECTED && wakeUpWifi.isExpired()  ) {
     WiFi.forceSleepBegin(); wdt_reset(); ESP.restart(); while(1)wdt_reset();
  }

   
}
