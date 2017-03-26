#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>
#include "Bounce2.h"
#include "DHT.h"

ESP8266WebServer server(80);


//WIFI
String command;
StaticJsonBuffer<200> jsonBuffer;
JsonObject& jsonCommand = jsonBuffer.createObject();
char toChar[2500];

//RELAY
#define LIGHT_LIVING_ROOM_PIN  13
#define LIGHT_DINNING_ROOM_PIN  12

int state_relay1 = LOW;       // estado actual del pin de salida 
int state_relay2 = LOW;       // estado actual del pin de sali

Bounce debouncer_relay1 = Bounce(); 
Bounce debouncer_relay2 = Bounce();


//DHT
#define DHTPIN D1 
//#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
DHT dht(DHTPIN, DHT22);

void handleLight() {
  digitalWrite(LED_BUILTIN, LOW);
  

  int number = server.arg("number").toInt();
  boolean state  = server.arg("state").toInt();

  switch(number){
    case 1:{
      digitalWrite(LIGHT_LIVING_ROOM_PIN,state);
    }
    break;
    case 2:{
      digitalWrite(LIGHT_DINNING_ROOM_PIN,state);
    }
  }

  server.send(200, "text/html", state?"ON":"OFF");
  delay(500);                    
  digitalWrite(LED_BUILTIN, HIGH);
}

void handleSensors() {
  digitalWrite(LED_BUILTIN, LOW);
 
 jsonCommand["temperature"] = dht.readTemperature();
 jsonCommand["humidity"] = dht.readHumidity();
 jsonCommand["lightLivingRoom"] = digitalRead(LIGHT_LIVING_ROOM_PIN);
 jsonCommand["lightDinningRoom"] = digitalRead(LIGHT_DINNING_ROOM_PIN);
 jsonCommand.printTo(command);
 server.send(200, "text/html", command);
 delay(500);                    
 digitalWrite(LED_BUILTIN, HIGH);
}

void setLightValue(int pin,boolean turn){
  digitalWrite(pin,turn);
}

void handleCommand() {
  digitalWrite(LED_BUILTIN, LOW); 

  //receive message
  command = server.arg("command");
  
  unsigned int rawbuf[250];
  int k = 0;
  //decode message
  while(true){
    rawbuf[k++] = command.toInt();

    if(command.indexOf(' ') == -1){
      break;
    }
    command = command.substring(command.indexOf(' ')+1);
  }


  //response message
  jsonCommand["code"] = "OK";
  command = "";
  jsonCommand.printTo(command);
  server.send(200, "text/html", command);
  delay(500);                    
  digitalWrite(LED_BUILTIN, HIGH);

}

 
void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //exit after config instead of connecting
  wifiManager.setBreakAfterConfig(true);

  pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output

  //reset settings - for testing
 // wifiManager.resetSettings();


  //tries to connect to last known settings
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP" with password "password"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("LIVING")) {
    Serial.println("failed to connect, we should reset as see if it connects");
    delay(3000);
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected!");


  Serial.println("local ip");
  Serial.println(WiFi.localIP());

  server.on("/light", handleLight);
  server.on("/sensors", handleSensors);


  server.begin();

  //RELAY
  pinMode(LIGHT_LIVING_ROOM_PIN,  OUTPUT) ;
  pinMode(LIGHT_DINNING_ROOM_PIN,  OUTPUT) ;

  //DHT
  dht.begin();

  digitalWrite(LED_BUILTIN, HIGH);


}


void loop() {
  server.handleClient();
}
