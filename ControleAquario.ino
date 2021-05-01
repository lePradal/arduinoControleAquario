// Includes
//---------------------------------------------------------------
//#include <Pid.h>
//#include <OnOff.h>
#include <Aquarium.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Aquarium Param
//---------------------------------------------------------------
#define AQUARIUM_ID "1"
#define USER_ID "1"

// I/Os
//---------------------------------------------------------------
#define peltierPin 16   //d0
#define heaterPin 5     //d1
#define termometerPin 4 //d2

// PID Params
//---------------------------------------------------------------
//#define kP 1.3
//#define kI 0.5
//#define kD 0.1
//#define samplingRate 0.1

// DS18B20 Params
//---------------------------------------------------------------
uint8_t aquariumSensor[8] = { 0x28, 0x9E, 0x0B, 0x79, 0x97, 0x20, 0x03, 0x86 };
uint8_t wheaterSensor[8] = { 0x28, 0x2D, 0xEF, 0xE7, 0x26, 0x20, 0x01, 0x42 };

// EEPROM Params
//---------------------------------------------------------------
#define CONTROL_ACTIVE 0
#define SET_POINT_ADDRESS 1
#define LAST_WATER_TEMP 2
#define LAST_WEATHER_TEMP 3

// WiFi Params
//---------------------------------------------------------------
#define STASSID "Pradal 2.4G"
#define STAPSK  "pradal2180"
#define APP_HOST "api-aquario.herokuapp.com"

// Auth Params
//---------------------------------------------------------------
#define email "le.nodemcu@gmail.com"
#define password  "123456abc"

// Monving Average Params
//---------------------------------------------------------------
#define movingAveragePoints 25

// Objects
//---------------------------------------------------------------
//Pid peltier(peltierPin, kP, kI, kD, samplingRate);
//OnOff heater(heaterPin);
ESP8266WiFiMulti WiFiMulti;
Aquarium aquarium;
OneWire oneWire(termometerPin);
DallasTemperature sensors(&oneWire);

// Global Variables
//---------------------------------------------------------------
float temperature;
float weatherTemperature;
float lastTemperature;
float humidity;
bool stateChanged;
int delayMS;
float margin = 0.2;
bool isActive;
float setPoint;
long lastGetParams;
long lastLoopCicle;
const uint8_t fingerprint[20] = {0x94, 0xFC, 0xF6, 0x23, 0x6C, 0x37, 0xD5, 0xE7, 0x92, 0x78, 0x3C, 0x0B, 0x5F, 0xAD, 0x0C, 0xE4, 0x9E, 0xFD, 0x9E, 0xA8};
float waterTempHist[movingAveragePoints];
float weatherTempHist[movingAveragePoints];


// Setup
//---------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  EEPROM.begin(64);
  sensors.begin();

  pinMode(peltierPin, OUTPUT);
  pinMode(heaterPin, OUTPUT);
  pinMode(termometerPin, INPUT);

  Serial.println();
  for (uint8_t t = 4; t > 0; t--) {
    Serial.printf("[SETUP] WAIT %d...\n", t);
    Serial.flush();
    delay(1000);
  }

  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP(STASSID, STAPSK);

  aquarium = getAquarium();
  setPoint = EEPROM.read(SET_POINT_ADDRESS);
  isActive = EEPROM.read(CONTROL_ACTIVE);

  for (int i = 0; i < movingAveragePoints; i++) {
    waterTempHist[i] = EEPROM.read(LAST_WATER_TEMP);
    weatherTempHist[i] = EEPROM.read(LAST_WEATHER_TEMP);
  }

  //delayMS = samplingRate * 1000;
  delayMS = 1000;

  lastLoopCicle = millis();
  lastGetParams = millis();
};

// Loop
//---------------------------------------------------------------
void loop() {
  if (millis() - lastLoopCicle >= delayMS) {
  
    sensors.requestTemperatures();
    temperature = movingAverage(sensors.getTempC(aquariumSensor), waterTempHist);
    weatherTemperature = movingAverage(sensors.getTempC(wheaterSensor), weatherTempHist);

    Serial.print("setPoint: ");
    Serial.print(setPoint);
    Serial.print("  | temperature: ");
    Serial.print(temperature);
    Serial.print("  | wTemperature: ");
    Serial.print(weatherTemperature);
    Serial.print("  | peltionOn: ");
    Serial.print(temperature > setPoint);
    Serial.print("  | heaterOn: ");
    Serial.println(temperature <= setPoint - margin);
  
    EEPROM.write(LAST_WATER_TEMP, temperature);
    EEPROM.commit();
    EEPROM.write(LAST_WEATHER_TEMP, weatherTemperature);
    EEPROM.commit();
  
    if (millis() - lastGetParams >= 60000) {
  
      aquarium = getAquarium();
  
      setPoint = EEPROM.read(SET_POINT_ADDRESS);
      isActive = EEPROM.read(CONTROL_ACTIVE);
  
      if (temperature != lastTemperature) {
        stateChanged = true;
      }
  
      if (stateChanged) {
        bool updated = updateAquarium();
  
        bool registered = registerResult();
    
        if (updated && registered) {
          stateChanged = false;
          lastTemperature = temperature;
        }
      }
  
      lastGetParams = millis();
    }
  
    if (isActive) {
      digitalWrite(peltierPin, !(temperature > setPoint));
      digitalWrite(heaterPin, !(temperature <= setPoint - margin));
      
      //bool peltierSt = peltier.control(temperature, setPoint);
      //bool heaterSt = heater.control(temperature, margin, setPoint);
    }

    lastLoopCicle = millis();

  }
};

// Methods
//---------------------------------------------------------------

String getToken() {
  if ((WiFiMulti.run() == WL_CONNECTED)) {
    std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
    client->setFingerprint(fingerprint);
    HTTPClient https;
    Serial.print("[HTTPS] Auth - POST... begin authentication\n");
    if (https.begin(*client, "https://" APP_HOST "/auth")) {
      https.addHeader("Content-Type", "application/json");
      String request = "{\"email\":\"" email "\",\"password\":\"" password "\"}";
      int httpCode = https.POST(request);
      if (httpCode > 0) {
        Serial.printf("[HTTPS] Auth - POST... code: %d\n", httpCode);
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
          String payload = https.getString();          
          return split(payload.substring(10), '"', 0);
        }

        return "";
      } else {
        Serial.printf("[HTTPS] Auth - POST... failed, error: %s\n", https.errorToString(httpCode).c_str());
        return "";
      }
      https.end();
    } else {
      Serial.printf("[HTTPS] Auth - Unable to connect\n");
      return "";
    }
  } else {
    return "";
  }
};

Aquarium getAquarium() {

  String token = getToken();
  
  if ((WiFiMulti.run() == WL_CONNECTED)) {
    std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
    client->setFingerprint(fingerprint);
    HTTPClient https;
    Serial.print("[HTTPS] Aquarium - GET... getting aquarium\n");
    if (https.begin(*client, "https://" APP_HOST "/aquariums/" AQUARIUM_ID)) {
      https.addHeader("Authorization", "Bearer " + token);
      int httpCode = https.GET();
      if (httpCode > 0) {
        Serial.printf("[HTTPS] Aquarium - GET... code: %d\n", httpCode);
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
          String payload = https.getString();

          Serial.print("[HTTPS] Aquarium - GET... payload: ");
          Serial.println(payload);

          Aquarium aq(payload);

          EEPROM.write(CONTROL_ACTIVE, aq.getControlActive());
          EEPROM.commit();
          EEPROM.write(SET_POINT_ADDRESS, aq.getSetPointTemp());
          EEPROM.commit();

          return aq;
        }
      } else {
        Serial.printf("[HTTPS] Aquarium - GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
      }
      https.end();
    } else {
      Serial.printf("[HTTPS] Aquarium - Unable to connect\n");
    }
  }
};

bool updateAquarium() {

  String token = getToken();
  
  if ((WiFiMulti.run() == WL_CONNECTED)) {
    std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
    client->setFingerprint(fingerprint);
    HTTPClient https;
    Serial.println("[HTTPS] Aquarium - PUT... updating aquarium");
    if (https.begin(*client, "https://" APP_HOST "/aquariums/" AQUARIUM_ID)) {
      https.addHeader("Authorization", "Bearer " + token);
      https.addHeader("Content-Type", "application/json");

      aquarium.setStatus("\"ONLINE\"");
      aquarium.setTemperature(temperature);

      String request = aquarium.tJson();

      int httpCode = https.PUT(request);

      if (httpCode > 0) {
        Serial.printf("[HTTPS] Aquarium - PUT... code: %d\n", httpCode);
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
          Serial.println("[HTTPS] Aquarium - PUT... aquarium updated");

          return true;
        }
      } else {
        Serial.printf("[HTTPS] Aquarium - PUT... failed, error: %s\n", https.errorToString(httpCode).c_str());
      }
      https.end();
    } else {
      Serial.printf("[HTTPS] Aquarium - Unable to connect\n");
    }
  }

  return false;
};

bool registerResult() {

  String token = getToken();
  
  if ((WiFiMulti.run() == WL_CONNECTED)) {
    std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
    client->setFingerprint(fingerprint);
    HTTPClient https;
    Serial.println("[HTTPS] Aquarium - POST... registering result");
    if (https.begin(*client, "https://" APP_HOST "/results")) {
      https.addHeader("Authorization", "Bearer " + token);
      https.addHeader("Content-Type", "application/json");

      String wTemp = String(weatherTemperature);

      String request = "{\"userId\":\"" USER_ID "\",\"aquariumId\":\"" AQUARIUM_ID "\",\"weatherTemperature\":\"" + wTemp + "\"}";

      int httpCode = https.POST(request);

      if (httpCode > 0) {
        Serial.printf("[HTTPS] Aquarium - POST... code: %d\n", httpCode);
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
          Serial.println("[HTTPS] Aquarium - POST... result saved");

          return true;
        }
      } else {
        Serial.printf("[HTTPS] Aquarium - POST... failed, error: %s\n", https.errorToString(httpCode).c_str());
      }
      https.end();
    } else {
      Serial.printf("[HTTPS] Aquarium - Unable to connect\n");
    }
  }

  return false;
};

String split(String data, char separator, int index) {
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length()-1;

  for(int i=0; i<=maxIndex && found<=index; i++){
    if(data.charAt(i)==separator || i==maxIndex){
        found++;
        strIndex[0] = strIndex[1]+1;
        strIndex[1] = (i == maxIndex) ? i+1 : i;
    }
  }

  return found>index ? data.substring(strIndex[0], strIndex[1]) : "";
};

bool toBoolean(String value) {
  if (value == "true" || value == "TRUE" || value == "True") {
    return true;
  } else if (value == "false" || value == "FALSE" || value == "False") {
    return false;
  }
};

float movingAverage(float value, float historicValue[]) {
  for (int i = movingAveragePoints - 1; i > 0; i--) {
    historicValue[i] = historicValue[i - 1];
  }

  historicValue[0] = value;

  float sum = 0;
  for (int i = 0; i < movingAveragePoints; i++) {
    sum += historicValue[i];
  }

  return sum/movingAveragePoints;
};
