#include <Homie.h>
#include <ArduinoJson.h>
#include <MFRC522.h>
#include <WiFiUDP.h>
#include <TimeLib.h>
#include <FS.h>
#include <NTPClient.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>
/*-------------------------------------------
 | Signal        | MFRC522       |  NodeMcu |         PINOUT RFID MFRC522
 |---------------|:-------------:| :------: |
 | SPI SDA       | SDA           | D8       |
 | SPI MOSI      | MOSI          | D7       |
 | SPI MISO      | MISO          | D6       |
 | SPI SCK       | SCK           | D5       |
 -------------------------------------------*/

// FastLed
// #define NUM_LEDS 1
// #define DATA_PIN 9
//
// uint led_ts;
// int fadeAmount = 5;
// int brightness = 0;
// CRGB led[NUM_LEDS];

//NeoPixel
Adafruit_NeoPixel statusLed = Adafruit_NeoPixel(1, D9, NEO_RGB);

// Flags
bool MQTT_DISC_FLAG = true;
bool findmeFlag = false;
bool START_NTP= false;


// Variáveis RFID
const int rfidss = 15;
const int rfidgain = 112;
String uid;

// Pinos no ESP
int RELAY_PIN = 4; // D2
int DOOR_PIN = 16; // 16

// Variáveis NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "br.pool.ntp.org", 0, 3600000);

// Create MFRC522 RFID instance
MFRC522 mfrc522 = MFRC522();

int lastDoorState;

String ledMode;
float ledCounter = 1;
int ledClock = 1;
float ledDirection = 0.2;


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
    if (ledCounter > 5.2 ){
      ledDirection = -0.2;
    }
    else if (ledCounter < -1){
      ledDirection = 0.2;
    }
    statusLed.setPixelColor(0, statusLed.Color((255 - exp(ledCounter)),(255 - exp(ledCounter)),(255 - exp(ledCounter))));
    statusLed.show();
    ledCounter += ledDirection;
    if (ledDirection > 0)
      ledClock++;
    else
      ledClock--;
  }

  if (ledMode == "pulse-blue"){
    if (ledCounter > 5.2 ){
      ledDirection = -0.2;
    }
    else if (ledCounter < -1){
      ledDirection = 0.2;
    }
    statusLed.setPixelColor(0, statusLed.Color((255 - exp(ledCounter)),0,0));
    statusLed.show();
    ledCounter += ledDirection;
    if (ledDirection > 0)
      ledClock++;
    else
      ledClock--;
  }

  if (ledMode == "blink-green"){
    if (ledClock < 30){
      statusLed.setPixelColor(0, statusLed.Color(255,0,0));
      statusLed.show();
      ledClock++;
    }
    else {
      if (!MQTT_DISC_FLAG){
        ledMode = "pulse-white";
        ledClock = 0;
      }
      else{
        ledMode = "pulse-blue";
        ledClock = 0;
      }
    }
  }
  if (ledMode == "blink-red"){
    if (ledClock < 30){
      statusLed.setPixelColor(0, statusLed.Color(0,255,0));
      statusLed.show();
      ledClock++;
    }
    else {
      if (!MQTT_DISC_FLAG){
        ledMode = "pulse-white";
        ledClock = 0;
      }
      else{
        ledMode = "pulse-blue";
        ledClock = 0;
      }
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
void fileReader(File f){
  String str;
  Homie.getLogger() << f.name() << " -> " << f.size() << endl;
  while (f.position()<f.size()){
    str = f.readStringUntil('\n');
    Homie.getLogger() << str << endl;
  }
  f.close();
  Homie.getLogger() << "----------------------------------------------" << endl;
}

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
void LogSend(){
  int i, j = 0;
  if (SPIFFS.exists("/access/log.csv")){
    Homie.getLogger() << "Log de sistema do modo OFFLINE encontrado" << endl;
    String str;
    File f = SPIFFS.open("/access/log.csv", "r");
    DynamicJsonBuffer jsonLogBuffer;
    JsonObject& logsend = jsonLogBuffer.createObject();
    logsend["data"] = f.readString();
    char sendmessage[512];
    logsend.prettyPrintTo(sendmessage, sizeof(sendmessage));
    Homie.getLogger() << "Log de acesso: " << sendmessage << endl;
    offAccessNode.setProperty("json").send(sendmessage);
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
  if (tagReader()){
    Homie.getLogger() << "Leitura UID: " << uid << endl;
    String NTPtime;
    File f = SPIFFS.open("/access/log.csv", "a");
    if(uidFinder(uid)){
      openLock();
    }
    ledMode = "blink-red";
    if(START_NTP)
      NTPtime = timeClient.getFormattedTime();
    else
      NTPtime = millis();
    f.println(uid + ";" + NTPtime);
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
  f = SPIFFS.open("/access/auth.csv", "r");
  fileReader(f);
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
}
// Checkar a função                                                             // !!!!!!!!!
bool fileHandler(const HomieRange& range, const String& value){
  if(value=="true"){
    Dir dir = SPIFFS.openDir("/access");
    while (dir.next()) {
      File f = dir.openFile("r");
      fileReader(f);
    }
  }
}

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
}

void setupHandler() {
  doorNode.setProperty("open");
  accessNode.setProperty("leitura");
}

void loopHandler() {
  if(findmeFlag){
    // ledBlink("findme");
  }
  else {
    // ledPulse("online");
    onlineMode();
    doorHandler();
    ledController();
  }
}

/*-----------------------------Homie events-----------------------------------*/
void onHomieEvent(const HomieEvent& event) {
  switch(event.type) {
    case HomieEventType::WIFI_CONNECTED:{
      timeClient.begin();
      START_NTP = true;
      }
      break;
    case HomieEventType::MQTT_READY:{
      MQTT_DISC_FLAG = false;
      ledMode = "pulse-white";
      LogSend();
    }
    break;
    case HomieEventType::MQTT_DISCONNECTED:{
      MQTT_DISC_FLAG = true;
      ledMode = "pulse-blue";
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

  // inicializa RFID
  setupRFID(rfidss, rfidgain);

  // FastLED.addLeds<WS2812B, DATA_PIN, RGB>(led, NUM_LEDS);
  // pinMode(DATA_PIN, OUTPUT);

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
  filecheckNode.advertise("read").settable(fileHandler);
  findNode.advertise("me").settable(findMeHandler);
  regNode.advertise("new").settable(regOnHandler);
  unlockNode.advertise("open").settable(unlockHandler);
  accessNode.advertise("attempt");
  doorNode.advertise("open");
  offAccessNode.advertise("json");

  // inicializa o modo Event
  Homie.onEvent(onHomieEvent);

  Homie.setup();
}

void loop(){
  Homie.loop();
  if(START_NTP)
    timeClient.update();
  if(MQTT_DISC_FLAG)
    offlineMode();
}
