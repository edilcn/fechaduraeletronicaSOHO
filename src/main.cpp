#include <Homie.h>
#include <ArduinoJson.h>
#include <MFRC522.h>
#include <WiFiUDP.h>
#include <TimeLib.h>
#include <FS.h>
#include <Adafruit_NeoPixel.h>
#include <ESP8266WiFi.h>

WiFiEventHandler gotIpEventHandler, disconnectedEventHandler;

/*-------------------------------------------
 | Signal        | MFRC522       |  NodeMcu |         PINOUT RFID MFRC522
 |---------------|:-------------:| :------: |
 | SPI SDA       | SDA           | D8       |
 | SPI MOSI      | MOSI          | D7       |
 | SPI MISO      | MISO          | D6       |
 | SPI SCK       | SCK           | D5       |
 -------------------------------------------*/

//NeoPixel
Adafruit_NeoPixel statusLed = Adafruit_NeoPixel(1, D9, NEO_RGB);

// Flags
bool MQTT_DISC_FLAG = true;
bool findmeFlag = false;

// Variáveis RFID
const int rfidss = 15;
const int rfidgain = 112;
String uid;

// Pinos no ESP
int RELAY_PIN = 4; // D2
int DOOR_PIN = 16; // 16

// Create MFRC522 RFID instance
MFRC522 mfrc522 = MFRC522();

int lastDoorState;

String ledMode;
int ledCounter = 80;
int ledLimit;
int ledTimer;
int ledDirection = 1;
int ledUnlock = 0;


/*----------------------------Nodes para o HOMIE------------------------------*/
HomieNode accessNode("access", "jSON");                                         // publica todos as tentativas de acesso
HomieNode offAccessNode("offlineaccess", "jSON");                               // publica todas as tentativas de acesso enquanto offline
HomieNode unlockNode("unlock", "Relay");                                        // destrava a porta
HomieNode doorNode("door", "Binary");                                           // publica se a porta está aberta ou fechada
HomieNode regNode("registry", "jSON");                                          // recebe cadastros de usuários
HomieNode findNode("find", "Binary");                                           // Identificação da fechadura (sinalização luminosa)
HomieNode filecheckNode("file", "File");                                        // Leitura dos Arquivos p/ conferencia   // !!!!!!!!!

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
    if (ledCounter > 254)
      ledDirection = -1;
    if (ledCounter == 0)
      ledDirection = 1;
    statusLed.setPixelColor(0, statusLed.Color(0,0,ledCounter));
    statusLed.show();
    ledCounter = ledCounter + ledDirection;
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
      if (MQTT_DISC_FLAG == false)
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
      if (MQTT_DISC_FLAG == false)
        ledMode = "pulse-white";
      else
        ledMode = "pulse-blue";
    }
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
  return true;
}

/*-----------------------------Funções do Sistema-----------------------------*/

bool uidFinder(String uid){
  File f = SPIFFS.open("/access/auth.csv", "r");
  while (true){
    if (f.position()<f.size()){
      if (f.readStringUntil(';') == uid){
        Homie.getLogger() << uid <<" Encontrado"  << endl;
        f.close();
        return true;
      }
      else f.readStringUntil('\n');
    }
    else {
      f.close();
      return false;
    }
  }
}

void openLock(){
  uint ts = millis();
  digitalWrite(RELAY_PIN, LOW);
  ledMode = "blink-green";
  while (millis() < ts+300){}
  digitalWrite(RELAY_PIN, HIGH);
}
// Checkar a Função                                                             // !!!!!!!!!


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
    accessNode.setProperty("attempt").setRetained(false).send(uid);
  }
}

void offlineMode(){
  if (tagReader()){
    Homie.getLogger() << "Leitura UID: " << uid << endl;
    File f = SPIFFS.open("/access/log.csv", "a");
    if(uidFinder(uid)){
      openLock();
    }
    ledMode = "blink-red";
    f.println(uid);
    f.close();
  }
}

/*----------------------------Homie Callback Handlers----------------------------------*/
bool findMeHandler(const HomieRange& range, const String& value){
  if(value == "true"){
    findmeFlag = true;
  }
  else findmeFlag = false;
  return true;
}

bool regOnHandler(const HomieRange& range, const String& value){
  String str = "";
  String Copy = "";
  if(value==("0")) return false;
  DynamicJsonBuffer incomejsonBuffer;
  JsonObject& income = incomejsonBuffer.parseObject(value);
  String uid_reg = income["uid"];
  String begin_date = income["begin_date"]; // "mock"
  String begin_time = income["begin_time"]; // "mock"
  String end_time = income["end_time"]; // "mock"
  String recurrence = income["recurrence"]; // "mock"
  String week_days = income["week_days"]; // "mock"
  String end_date = income["end_date"]; // "mock"
  File f;
  File temp = SPIFFS.open("/access/temp.csv","w");

  if (!uidFinder(uid_reg)){
    Homie.getLogger() << "debug" << endl;
    f = SPIFFS.open("/access/auth.csv", "a+");
    f.println(uid_reg +';'+begin_date+';'+begin_time+';'+end_time+';'+recurrence+';'+week_days+';'+end_date);
    f.close();
  }
  else {
    f = SPIFFS.open("/access/auth.csv", "r");
    while (f.position()<f.size()){
      str = f.readStringUntil(';');
      if (str == uid_reg){
        f.readStringUntil('\n');
      }
      else {
        temp.print(str + ';');
        Copy = f.readStringUntil('\n');
        temp.println(Copy);
      }
    }
    temp.println(uid_reg +';'+begin_date+';'+begin_time+';'+end_time+';'+recurrence+';'+week_days+';'+end_date);
    f.close();
    temp.close();
    SPIFFS.remove("/access/auth.csv");
    SPIFFS.rename("/access/temp.csv","/access/auth.csv");
  }
  f.close();
  return true;
}

bool unlockHandler(const HomieRange& range, const String& value) {
  if (value == "true"){
    openLock();
    Homie.getLogger() << "Fechadura desbloqueada" << endl;
    ledMode = "blink-green";
    return true;
  }
  if(value == "false") {
    ledMode = "blink-red";
    return true;
  }
  return true;
}
// Checkar a função                                                             // !!!!!!!!!
// bool fileHandler(const HomieRange& range, const String& value){
//   if(value=="true"){
//     Dir dir = SPIFFS.openDir("/access");
//     while (dir.next()) {
//       File f = dir.openFile("r");
//       fileReader(f);
//     }
//   }
//   return true;
// }

bool doorHandler(){
  if (digitalRead(DOOR_PIN) != lastDoorState)
  {
    if (digitalRead(DOOR_PIN)){
      Homie.getLogger() << "Status: Porta Aberta" << endl;
      doorNode.setProperty("open").send("false");
    }

    else {
      Homie.getLogger() << "Status: Porta Fechada" << endl;
      doorNode.setProperty("open").send("true");
    }
    lastDoorState = digitalRead(DOOR_PIN);
  }
  return true;
}

void setupHandler() {
  doorNode.setProperty("open");
}

void loopHandler() {
  if(findmeFlag){
  }
  else {
    onlineMode();
    doorHandler();
    ledController();

  }
}

/*-----------------------------Homie events-----------------------------------*/
void onHomieEvent(const HomieEvent& event) {
  switch(event.type) {
    case HomieEventType::WIFI_DISCONNECTED:
      // Do whatever you want when Wi-Fi is disconnected in normal mode

      Serial << "Wi-Fi disconnected, reason: " << (int8_t)event.wifiReason << endl;
      break;
    case HomieEventType::WIFI_CONNECTED:{
      // timeClient.begin();
      // START_NTP = true;
      }
      break;
    case HomieEventType::MQTT_READY:{
      MQTT_DISC_FLAG = false;
      ledMode = "pulse-white";
      // LogSend();
    }
    break;
    case HomieEventType::MQTT_DISCONNECTED:{
      MQTT_DISC_FLAG = true;
      ledMode = "pulse-blue";
      Serial << "MQTT disconnected, reason: " << (int8_t)event.mqttReason << endl;
    }

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

  // Inicializa o LED (neopixel)
  statusLed.begin();

  // FastLED.addLeds<WS2812B, DATA_PIN, RGB>(led, NUM_LEDS);
  // pinMode(DATA_PIN, OUTPUT);

  // inicializa RFID
  setupRFID(rfidss, rfidgain);
  // seta pinos
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(DOOR_PIN, INPUT);
  digitalWrite(RELAY_PIN, HIGH);
  lastDoorState = digitalRead(DOOR_PIN);

  // parametros Homie
  Homie_setFirmware("SOHO MQTT Lock", "0.0.1");
  Homie_setBrand("SOHO-Lock");
  Homie.setSetupFunction(setupHandler).setLoopFunction(loopHandler);

  // inicializa os nodes
  // filecheckNode.advertise("read").settable(fileHandler);
  findNode.advertise("me").settable(findMeHandler);
  regNode.advertise("new").settable(regOnHandler);
  unlockNode.advertise("open").settable(unlockHandler);
  accessNode.advertise("attempt");
  doorNode.advertise("open");
  offAccessNode.advertise("json");

  // inicializa o modo Event
  Homie.onEvent(onHomieEvent);
  Homie.getMqttClient().setKeepAlive(2);
  Homie.setup();
}

void loop(){
  Homie.loop();
  if (ledUnlock == 1000){

  }
  if(MQTT_DISC_FLAG)
    offlineMode();
}
