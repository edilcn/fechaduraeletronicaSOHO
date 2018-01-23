#include <Homie.h>
#include "FastLED.h"
#include <ArduinoJson.h>
#include <NtpClientLib.h>
#include <MFRC522.h>
#include <WiFiUDP.h>
#include <TimeLib.h>
#include <FS.h>

// FastLed
#define NUM_LEDS 1
#define DATA_PIN 9

CRGB led[NUM_LEDS];
int fadeAmount = 5;
int brightness = 0;
uint led_ts;

// Flags
bool MQTT_DISC_FLAG = true;

// Variáveis RFID
const int rfidss = 15;
const int rfidgain = 112;
String uid;

// Pinos no ESP
int RELAY_PIN = 4; // D2
int DOOR_PIN = D0;

// Variáveis NTP
const char * ntpserver = "br.pool.ntp.org";
int ntpinter = 30;
WiFiUDP ntpUDP;
int16_t timeZone = -3;
uint32_t currentMillis = 0;
uint32_t previousMillis = 0;

// Create MFRC522 RFID instance
MFRC522 mfrc522 = MFRC522();

int lastDoorState;

/*----------------------------Nodes para o HOMIE------------------------------*/
HomieNode accessNode("access", "jSON");                                         // publica todos as tentativas de acesso
HomieNode offAccessNode("offlineAccess", "jSON");                               // publica todas as tentativas de acesso enquanto offline
HomieNode unlockNode("unlock", "Relay");                                        // destrava a porta
HomieNode doorNode("door", "Binary");                                           // publica se a porta está aberta ou fechada
HomieNode regNode("registry", "jSON");                                          // recebe cadastros de usuários

/*------------------------Implementação da Iluminação-------------------------*/
void ledPulse(String state){
  if (state == "online"){
    led[0].setRGB(255,255,255);
    led[0].fadeLightBy(brightness);
    FastLED.show();
    brightness = brightness + fadeAmount;
    if(brightness == 0 || brightness == 255)
    fadeAmount = -fadeAmount;
  }
  if (state == "offline"){
    led[0].setRGB(0,0,255);
    led[0].fadeLightBy(brightness);
    FastLED.show();
    brightness = brightness + fadeAmount;
    if(brightness == 0 || brightness == 255)
    fadeAmount = -fadeAmount;
  }
}

void ledBlink(String color){
    led_ts = millis();
    if(color == "green"){
      // Turn the LED on, then pause
      led[0].setRGB(255,0,0);
      FastLED.show();
      while (millis() < led_ts+1000){}
    }
    if(color =="red"){
      led[0].setRGB(0,255,0);
      FastLED.show();
      while (millis() < led_ts+1000){}
    }
}

/*------------------------------Rotinas RFID----------------------------------*/
void setupRFID(int rfidss, int rfidgain) {
  SPI.begin();                                                                  // Inicializa o protocolo SPI para o MFRC522
  mfrc522.PCD_Init(rfidss, UINT8_MAX);                                          // Inicializa MFRC522 Hardware
  mfrc522.PCD_SetAntennaGain(rfidgain);                                         // Seta o ganho da antena
}

bool tagReader(){
  //If a new PICC placed to RFID reader continue
  if ( !mfrc522.PICC_IsNewCardPresent()) {
          delay(50);
          return false;
  }
  //Since a PICC placed get Serial (UID) and continue
  if ( !mfrc522.PICC_ReadCardSerial()) {
          delay(50);
          return false;
  }
  // We got UID tell PICC to stop responding
  mfrc522.PICC_HaltA();

  // There are Mifare PICCs which have 4 byte or 7 byte UID
  // Get PICC's UID and store on a variable
  uid = "";
  for (int i = 0; i < mfrc522.uid.size; ++i) {
          uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  Homie.getLogger()<< uid << endl;
  return true;
}

/*-----------------------------Funções do Sistema-----------------------------*/
void openLock(){
  uint ts = millis();
  digitalWrite(RELAY_PIN, LOW);
  while (millis() < ts+300){}
  digitalWrite(RELAY_PIN, HIGH);
}

void LogCallback(){
  if (SPIFFS.exists("/access/log.csv")){
    Homie.getLogger() << "Log de sistema do modo OFFLINE encontrado" << endl;
    int i, j = 0;
    String str[]="";
    File f = SPIFFS.open("/access/log.csv", "r");
    while(f.position()<f.size()){
      str[i] = f.readStringUntil('\n');
      i++;
    }
    DynamicJsonBuffer jsonLogBuffer;
    JsonObject& logsend = jsonLogBuffer.createObject();

    for (j;j==i;j++){
        String Userlog;
        Userlog += "log";
        Userlog += j;
        logsend[Userlog] = str[j];
    }
    char JsonmessageBuffer[512];
    logsend.printTo(JsonmessageBuffer, sizeof(JsonmessageBuffer));
    Homie.getLogger() << "Log de acesso: " << JsonmessageBuffer << endl;
    offAccessNode.setProperty("offlineAccess").send(JsonmessageBuffer);
    f.close();
    SPIFFS.remove("/access/log.csv");
  }
}

/*----------------------------Modos de operação-------------------------------*/
void onlineMode(){
  if (tagReader()){
    // Prepara o envio do JSON para o Servidor
    //DynamicJsonBuffer JSONbuffer;;
    //JsonObject& JSONencoder = JSONbuffer.createObject();
    //JSONencoder["tag"] = uid;
    //JSONencoder["time"] = NTP.getTimeStr(timestamp);
    //char JSONmessageBuffer[300];
    //JSONencoder.prettyPrintTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));
    Homie.getLogger() << "Leitura da TAG: " << uid << endl;
    accessNode.setProperty("attempt").send(uid);
  }
}

void offlineMode(){
  bool found = false;
  String str;
  if (tagReader()){
    // UID , BEGIN(hh:mm) , END(hh:mm) , days (1,2,3,4,5,6,7) , expiry (dd:mm:yyyy hh:mm)
    if (SPIFFS.exists("/access/auth.csv")){
      File f = SPIFFS.open("/access/auth.csv", "r");
      while (!found){
        if (f.position()<f.size()){
          str = f.readStringUntil(',');
          if (str == uid){
            //implementar codigo para verificar se está dentro do período de acesso
            openLock();
            found = true;
            Homie.getLogger() << "Acesso offline CONCEDIDO. TAG: " << uid << endl;
          }
          else str = f.readStringUntil('\n');
        }
      }
      Homie.getLogger() << "Acesso offline NEGADO. TAG: " << uid << endl;
      f.close();
      f = SPIFFS.open("/access/log.csv", "a");
      f.println(uid+", "+String(millis()));
      f.close();
    }
  }
}

/*----------------------------HOMIE Handlers----------------------------------*/
bool regOnHandler(const HomieRange& range, const String& value){
  if(value==("0")) return false;
  // Parseia JSON de cadastro vindo do Servidor
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(value);
  String UID = root["uid"];                                                     // Tag do usuário
  String start = root["start"];                                                 // Horário inicial
  String stop = root["stop"];                                                   // Horário final
  String days = root["days"];                                                   // Dias de recorrência do evento
  String revoke = root["revoke"];                                               // Data e hora de expiração de acesso.
        /*-----Debug----*/
        // Serial.print("Cadastro: "); Serial.print(value); Serial.println();
        // Serial.print("UID recebido: "); Serial.print(UID); Serial.println();
        // Serial.print("Start recebido: "); Serial.print(start); Serial.println();
        // Serial.print("Stop recebido: "); Serial.print(stop); Serial.println();
        // Serial.print("Days recebido: "); Serial.print(days); Serial.println();
        // Serial.print("Revoke recebido: "); Serial.print(revoke); Serial.println();

  // Monta o caminho e o arquivo a ser criado
  String path_userID = "/U/";
  path_userID += UID;
  path_userID += ".json";

  // Aloca os valores parseados num buffer para ser serializado dentro do arquivo UID.json
  DynamicJsonBuffer jsonBuffer1;
  JsonObject& user = jsonBuffer1.createObject();
  user["uid"] = UID;
  user["start"] = start;
  user["stop"] = stop;
  user["days"] = days;
  user["revoke"] = revoke;
        /*-----Debug----*/
        // char bufferprint[512];
        // user.prettyPrintTo(bufferprint,sizeof(bufferprint));
        // Homie.getLogger() << "Dados do Usuário: " << bufferprint << endl;

  // Salva o arquivo no endereço indicado
  File f = SPIFFS.open(path_userID, "w");
  user.prettyPrintTo(f);
  Homie.getLogger() << "Usuário Registrado com Sucesso!" << endl;
  f.close();

  return true;
}

bool unlockHandler(const HomieRange& range, const String& value) {
  if (value == "true"){
    openLock();
    Homie.getLogger() << "Fechadura desbloqueada" << endl;
    ledBlink("green");
    return true;
  }
  if(value == "false") {
    ledBlink("red");
    return true;
  }
}

bool doorHandler(){
  if (digitalRead(DOOR_PIN) != lastDoorState)
  {
    if (digitalRead(DOOR_PIN))

      doorNode.setProperty("open").send("true");
    else
      doorNode.setProperty("open").send("false");
    lastDoorState = digitalRead(DOOR_PIN);
  }
}

void setupHandler() {
  doorNode.setProperty("status");
  NTP.begin(ntpserver, timeZone);
  NTP.setInterval(ntpinter * 60);
  accessNode.setProperty("leitura");
  time_t networkTime = NTP.getTime();
  time_t startTime = millis();
  time_t systemTime;
  lastDoorState = digitalRead(DOOR_PIN);
}

void loopHandler() {
    ledPulse("online");
    LogCallback();
    onlineMode();
    doorHandler();
}

/*-----------------------------Homie events-----------------------------------*/
void onHomieEvent(const HomieEvent& event) {
  switch(event.type) {
    case HomieEventType::WIFI_CONNECTED:
      MQTT_DISC_FLAG = true;
    break;
    case HomieEventType::MQTT_READY:
      MQTT_DISC_FLAG = false;
    break;
    case HomieEventType::MQTT_DISCONNECTED:
      MQTT_DISC_FLAG = true;
    break;
  }
}

/*---------------------------Inicialização Geral------------------------------*/
void setup() {
  // inicializa serial
  Serial.begin(115200);
  Serial << endl << endl;

  // inicializa FS
  SPIFFS.begin();

  // inicializa RFID
  setupRFID(rfidss, rfidgain);

  FastLED.addLeds<WS2812B, DATA_PIN, RGB>(led, NUM_LEDS);
  pinMode(DATA_PIN, OUTPUT);

  // seta pinos
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);

  // parametros Homie
  Homie_setFirmware("SOHO MQTT Lock", "0.0.1");
  Homie_setBrand("SOHO-Lock");
  Homie.setSetupFunction(setupHandler).setLoopFunction(loopHandler);

  // inicializa os nodes
  regNode.advertise("new").settable(regOnHandler);
  unlockNode.advertise("open").settable(unlockHandler);
  accessNode.advertise("attempt");
  doorNode.advertise("open");
  offAccessNode.advertise("file");

  // inicializa o modo Event
  Homie.onEvent(onHomieEvent);

  Homie.setup();
}

void loop(){
  Homie.loop();
  if(MQTT_DISC_FLAG){
    ledPulse("offline");
    offlineMode();
  }
}
