#define THINGER_SERVER "172.100.100.22"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ThingerESP8266.h>
#include <Adafruit_NeoPixel.h>
#include <MFRC522.h>
#include <FS.h>
#include <time.h>
#include <ArduinoOTA.h>
#include <NtpClientLib.h>
#include <TimeLib.h>


// Credenciais do Thinger.IO
// #define USERNAME "paumito"
// #define DEVICE_ID "teste"
// #define DEVICE_CREDENTIAL "gAiu$u1wGJ9D"

#define USERNAME "SHLabs"
#define DEVICE_ID "lock205"         
#define DEVICE_CREDENTIAL "08B8p0r9v1$w"

// Informações do WiFi
#define SSID_STA "soholabs"
#define SSID_PASSWORD "s0h0l@b5"

// Pinos no ESP
#define VOLT_PIN  D2 // D0 16 (5v)

// Thinger.IO
ThingerESP8266 thing(USERNAME, DEVICE_ID, DEVICE_CREDENTIAL);

//NeoPixel
Adafruit_NeoPixel statusLed = Adafruit_NeoPixel(1, D9, NEO_RGB);

//NTPVARIAVIS
int8_t timeZone = -3;
int8_t minutesTimeZone = 0;

// Flags
bool Status;

// Variáveis RFID
const int rfidss = 15;
const int rfidgain = 112;
String uid;

// Create MFRC522 RFID instance
MFRC522 mfrc522 = MFRC522();

String ledMode;
int ledCounter = 0;
int ledLimit;
int ledTimer;
int ledDirection = 1;

uint doorTimer = 0;


/*------------------------------Rotinas LED----------------------------------*/
void ledController(){
 if (ledMode == "pulse-white"){
     if (ledCounter > 253)
       ledDirection = -2;
     if (ledCounter < 2)
       ledDirection = 2;
     statusLed.setPixelColor(0, statusLed.Color(ledCounter,ledCounter,ledCounter));
     statusLed.show();
     ledCounter += ledDirection;
   }

 if (ledMode == "pulse-blue"){
   if (ledCounter > 253)
     ledDirection = -2;
   if (ledCounter < 2)
     ledDirection = 2;
   statusLed.setPixelColor(0, statusLed.Color(0,0,ledCounter));
   statusLed.show();
   ledCounter += ledDirection;
 }

 if (ledMode == "white"){
   statusLed.setPixelColor(0, statusLed.Color(254,254,254));
   statusLed.show();
 }

 if (ledMode == "red"){
   statusLed.setPixelColor(0, statusLed.Color(0,254,0));
   statusLed.show();
 }

 if (ledMode == "blink-green"){
   if (ledCounter < 20){
     if (ledCounter % 5 == 0)
       statusLed.setPixelColor(0, statusLed.Color(0,0,0));
     else
       statusLed.setPixelColor(0, statusLed.Color(254,0,0));
     statusLed.show();
     ledCounter++;
   }
   else{
     if (Status == true)
       ledMode = "pulse-white";
     else
       ledMode = "pulse-blue";
   }
 }

   if (ledMode == "blink-red"){
     if (ledCounter < 20){
       if (ledCounter % 5 == 0)
         statusLed.setPixelColor(0, statusLed.Color(0,0,0));
       else
         statusLed.setPixelColor(0, statusLed.Color(0,254,0));
       statusLed.show();
       ledCounter++;
     }
     else{
       if (Status == true)
         ledMode = "pulse-white";
       else
         ledMode = "pulse-blue";
     }
   }
   if (ledMode == "blink-yellow"){
     if (ledCounter < 20){
       if (ledCounter % 5 == 0)
         statusLed.setPixelColor(0, statusLed.Color(0,0,0));
       else
         statusLed.setPixelColor(0, statusLed.Color(254,254,0));
       statusLed.show();
       ledCounter++;
     }
     else{
       if (Status == true)
         ledMode = "pulse-white";
       else
         ledMode = "pulse-blue";
     }
   }
 }

void toggleWiFiStatus(){
 ledCounter = 0;
 ledMode = "blink-yellow";
}
/*------------------------------NTP Client -----------------------------------*/
void processSyncEvent (NTPSyncEvent_t ntpEvent) {
   if (ntpEvent) {
       Serial.print ("Time Sync error: ");
       if (ntpEvent == noResponse)
           Serial.println ("NTP server not reachable");
       else if (ntpEvent == invalidAddress)
           Serial.println ("Invalid NTP server address");
   } else {
       Serial.print ("Got NTP time: ");
       Serial.println (NTP.getTimeDateString (NTP.getLastNTPSync ()));
   }
}

boolean syncEventTriggered = false; // True if a time even has been triggered
NTPSyncEvent_t ntpEvent; // Last triggered event
/*------------------------------Rotinas RFID----------------------------------*/

void setupRFID(int rfidss, int rfidgain) {
 SPI.begin();                                                                  // Inicializa o protocolo SPI para o MFRC522
 mfrc522.PCD_Init(rfidss, UINT8_MAX);                                          // Inicializa MFRC522 Hardware
 mfrc522.PCD_SetAntennaGain(rfidgain);                                         // Seta o ganho da antena
}

bool tagReader(){
 if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
   mfrc522.PICC_HaltA();

   // There are Mifare PICCs which have 4 byte or 7 byte UID
   // Get PICC's UID and store on a variable
   uid = "";
   for (int i = 0; i < mfrc522.uid.size; ++i) {
           uid += String(mfrc522.uid.uidByte[i], HEX);
   }
   return true;
 }else{
   mfrc522.PICC_HaltA();
   return false;
 }
}

/*----------------------------Modos de operação-------------------------------*/
void onlineMode(){
  if (tagReader())
    thing.stream(thing["tag"]);
}

void openDoor(){
 doorTimer = millis()+300;
 digitalWrite(VOLT_PIN, LOW);
}

void closeDoor(){
 if (doorTimer != 0)
   if (millis() > doorTimer){
     digitalWrite(VOLT_PIN, HIGH);
     doorTimer = 0;
   }
}

void setup() {
 Serial.begin(115200);
 statusLed.begin();

 NTP.begin ("a.st1.ntp.br", timeZone, false, minutesTimeZone);
 NTP.setInterval (3600);

 NTP.onNTPSyncEvent ([](NTPSyncEvent_t event) {
       ntpEvent = event;
       syncEventTriggered = true;
   });
 // inicializa RFID
 setupRFID(rfidss, rfidgain);

 // Inicializa SPIFFS
 SPIFFS.begin();
 File INIT;                                                                      // Arquivo de verificação na inicialização.
 if(!SPIFFS.exists("/data.csv")){                                              // Verifica se o arquivo já existe.
   INIT = SPIFFS.open("/data.csv", "w");                                       // Caso não exista, cria-se o arquivo.
   if(!INIT)
     Serial.println("Error 1");                                                // ERROR 1 = Falha ao abrir arquivo.
   INIT.close();
 }

 // seta pinos
 pinMode(VOLT_PIN, OUTPUT);
 digitalWrite(VOLT_PIN, HIGH);

 thing.add_wifi(SSID_STA, SSID_PASSWORD);

 thing["unlock"] << [](pson& in){
   int number = in;
   if (number == 0){
     ledCounter = 0;
     ledMode = "blink-red";
   }
   else if (number == 1){
     openDoor();
     ledCounter = 0;
     ledMode = "blink-green";
   }
   else if (number == 2){
     ledCounter = 0;
     ledMode = "blink-yellow";
   }
 };

 thing["tag"] >> [](pson& out){
   out = uid;
 };

 // NTP EVENTS
 NTP.onNTPSyncEvent ([](NTPSyncEvent_t event) {
       ntpEvent = event;
       syncEventTriggered = true;
   });

 // set OTA hostname
 const char *hostname = "lock";
 ArduinoOTA.setHostname(hostname);

 // set OTA port
 ArduinoOTA.setPort(8266);

 // set a basic OTA password
 ArduinoOTA.setPassword("s0h0l@b5");

 ArduinoOTA.onStart([]() {
 String type;
 if (ArduinoOTA.getCommand() == U_FLASH) {
   type = "sketch";
 } else { // U_SPIFFS
   type = "filesystem";
 }

 // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
 Serial.println("Start updating " + type);
});

 // show progress while upgrading flash
 ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
     Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
 });

 ArduinoOTA.onEnd([]() {
   Serial.println("\nEnd");
 });

 ArduinoOTA.onError([](ota_error_t error) {
   Serial.printf("Error[%u]: ", error);
   if (error == OTA_AUTH_ERROR) {
     Serial.println("Auth Failed");
   } else if (error == OTA_BEGIN_ERROR) {
     Serial.println("Begin Failed");
   } else if (error == OTA_CONNECT_ERROR) {
     Serial.println("Connect Failed");
   } else if (error == OTA_RECEIVE_ERROR) {
     Serial.println("Receive Failed");
   } else if (error == OTA_END_ERROR) {
     Serial.println("End Failed");
   }
 });
 // init arduino OTA
 ArduinoOTA.begin();
}

void loop() {
 thing.handle();
 ArduinoOTA.handle();
 onlineMode();
 ledController();
 closeDoor();
 if ((WiFi.status() == WL_CONNECTED) && !(WiFi.localIP() == INADDR_NONE)){
   Status = true;
 }
 else Status = false;
 if (syncEventTriggered) {
       processSyncEvent (ntpEvent);
       syncEventTriggered = false;
 }
 if (uid != ""){
   pson data;
   data["lockID"] = DEVICE_ID;
   data["rfid_UID"] = uid;
   thing.call_endpoint("device_post", data);
   Serial.println("Endpoint chamado");
   uid = "";
 }

}
