#include <Homie.h>
#include <ArduinoJson.h>
#include <MFRC522.h>
#include <FS.h>
#include <Adafruit_NeoPixel.h>

// Pinos no ESP
#define VOLT_PIN  D0 // D0 16 (15v)
#define RINT_PIN  D1 // D1 5 (Radar Interno)
#define REXT_PIN  D2 // D2 4 (Radar Externo)

//NeoPixel
Adafruit_NeoPixel statusLed = Adafruit_NeoPixel(1, D9, NEO_RGB);

// Flags
bool MQTT_DISC_FLAG = true;
String OPERATION_MODE = "night";
String NEXT_OPERATION_MODE = "night";
/*----------------------------------------------------------------------------------------|
 | OPERATION_MODE   | D0 (15v)  | D1 (Radar Interno)  | D2 (Radar Externo)  | RFID        | PINOUT BORNES CONTROLE
 |:----------------:|:---------:|:-------------------:|:-------------------:|:-----------:|
 | normal           | low       | high                | high                | off         |
 | night            | low       | high                | low                 | on          |
 | open             | high      | low                 | low                 | off         |
 | close            | low       | low                 | low                 | off         |
 ----------------------------------------------------------------------------------------*/


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



/*----------------------------Nodes para o HOMIE------------------------------*/
HomieNode accessNode("access", "jSON");                                         // publica todos as tentativas de acesso
HomieNode offAccessNode("offlineaccess", "jSON");                               // publica todas as tentativas de acesso enquanto offline
HomieNode unlockNode("unlock", "Relay");                                        // destrava a porta
HomieNode regNode("registry", "jSON");                                          // recebe cadastros de usuários
HomieNode filecheckNode("file", "File");                                        // Leitura dos Arquivos p/ conferencia   // !!!!!!!!!
HomieNode operationModeNode("operationmode", "String");                              // modo de operação da porta

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

void openDoor(){
  doorTimer = millis()+6000;
  digitalWrite(VOLT_PIN, LOW);
}

void closeDoor(){
  if (doorTimer != 0)
    if (millis() > doorTimer){
      digitalWrite(VOLT_PIN, HIGH);
      doorTimer = 0;
      Homie.getLogger() << "Fechando a porta" <<
       endl;
    }
}

// Checkar a Função                                                             // !!!!!!!!!
void LogSend(){
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
  if (NEXT_OPERATION_MODE != OPERATION_MODE){
    if (NEXT_OPERATION_MODE == "normal"){
      digitalWrite(VOLT_PIN, HIGH);
      digitalWrite(RINT_PIN, LOW);
      digitalWrite(REXT_PIN, LOW);
      OPERATION_MODE = NEXT_OPERATION_MODE;
    }
    if (NEXT_OPERATION_MODE == "night"){
      digitalWrite(VOLT_PIN, HIGH);
      digitalWrite(RINT_PIN, HIGH);
      digitalWrite(REXT_PIN, LOW);
      OPERATION_MODE = NEXT_OPERATION_MODE;
    }
    if (NEXT_OPERATION_MODE == "open"){
      digitalWrite(VOLT_PIN, LOW);
      digitalWrite(RINT_PIN, LOW);
      digitalWrite(REXT_PIN, LOW);
      OPERATION_MODE = NEXT_OPERATION_MODE;
    }
    if (NEXT_OPERATION_MODE == "close"){
      digitalWrite(VOLT_PIN, HIGH);
      digitalWrite(RINT_PIN, HIGH);
      digitalWrite(REXT_PIN, HIGH);
      OPERATION_MODE = NEXT_OPERATION_MODE;
    }
    if (NEXT_OPERATION_MODE == "locked"){
      digitalWrite(VOLT_PIN, HIGH);
      digitalWrite(RINT_PIN, HIGH);
      digitalWrite(REXT_PIN, HIGH);
      OPERATION_MODE = NEXT_OPERATION_MODE;
    }
  }

  if ((OPERATION_MODE == "night") || (OPERATION_MODE == "normal") || (OPERATION_MODE == "close")){
    if (tagReader()){
      Homie.getLogger() << "Leitura da TAG: " << uid << endl;
      accessNode.setProperty("attempt").send(uid);
    }
  }
  else if (OPERATION_MODE == "open"){

  }
}

void offlineMode(){
  // caso entre em modo offline a porta passa a operar no modo night
  digitalWrite(VOLT_PIN, HIGH);
  digitalWrite(RINT_PIN, HIGH);
  digitalWrite(REXT_PIN, LOW);

  if (tagReader()){
    Homie.getLogger() << "Leitura da TAG: " << uid << endl;
    File f = SPIFFS.open("/access/log.csv", "a");
    if(uidFinder(uid)){
      openDoor();
    }
    ledCounter = 0;
    ledMode = "blink-red";
    f.println(uid);
    f.close();
  }
}

/*----------------------------Homie Callback Handlers----------------------------------*/
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

bool operationHandler(const HomieRange& range, const String& value) {
  if (value=="normal"){
    NEXT_OPERATION_MODE = "normal";
    Homie.getLogger() << "Modo de operação normal" << endl;
    operationModeNode.setProperty("operation").send("normal");
    ledMode = "pulse-white";
    return true;
  }
  if (value=="night"){
    NEXT_OPERATION_MODE = "night";
    Homie.getLogger() << "Modo de operação noturno" << endl;
    operationModeNode.setProperty("operation").send("night");
    ledMode = "pulse-white";
    return true;
  }
  if (value=="open"){
    NEXT_OPERATION_MODE = "open";
    Homie.getLogger() << "Modo porta aberta" << endl;
    operationModeNode.setProperty("operation").send("open");
    ledMode = "white";
    return true;
  }
  if (value=="close"){
    NEXT_OPERATION_MODE = "close";
    Homie.getLogger() << "Modo porta fechada" << endl;
    operationModeNode.setProperty("operation").send("close");
    ledMode = "pulse-white";
    return true;
  }
  if (value=="locked"){
    NEXT_OPERATION_MODE = "locked";
    Homie.getLogger() << "Modo porta trancada" << endl;
    operationModeNode.setProperty("operation").send("locked");
    ledMode = "red";
    return true;
  }
  return false;
}

bool unlockHandler(const HomieRange& range, const String& value) {
  if (value == "true"){
    openDoor();
    Homie.getLogger() << "Abrindo a porta" << endl;
    ledCounter = 0;
    ledMode = "blink-green";
    return true;
  }
  if(value == "false") {
    Homie.getLogger() << "Acesso negado" << endl;
    ledCounter = 0;
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
  return true;
}

void setupHandler() {
  accessNode.setProperty("leitura");
}

void loopHandler() {
  onlineMode();
  ledController();
  closeDoor();
}

/*-----------------------------Homie events-----------------------------------*/
void onHomieEvent(const HomieEvent& event) {
  switch(event.type) {
    case HomieEventType::STANDALONE_MODE:
      // Do whatever you want when standalone mode is started
      break;
    case HomieEventType::CONFIGURATION_MODE:
      // Do whatever you want when configuration mode is started
      break;
    case HomieEventType::NORMAL_MODE:
      // Do whatever you want when normal mode is started
      break;
    case HomieEventType::OTA_STARTED:
      // Do whatever you want when OTA is started
      break;
    case HomieEventType::OTA_PROGRESS:
      // Do whatever you want when OTA is in progress

      // You can use event.sizeDone and event.sizeTotal
      break;
    case HomieEventType::OTA_FAILED:
      // Do whatever you want when OTA is failed
      break;
    case HomieEventType::OTA_SUCCESSFUL:
      // Do whatever you want when OTA is successful
      break;
    case HomieEventType::ABOUT_TO_RESET:
      // Do whatever you want when the device is about to reset
      break;
    case HomieEventType::WIFI_CONNECTED:
      // Do whatever you want when Wi-Fi is connected in normal mode

      // You can use event.ip, event.gateway, event.mask
      break;
    case HomieEventType::WIFI_DISCONNECTED:
      // Do whatever you want when Wi-Fi is disconnected in normal mode

      // You can use event.wifiReason
      break;
    case HomieEventType::MQTT_PACKET_ACKNOWLEDGED:
      // Do whatever you want when an MQTT packet with QoS > 0 is acknowledged by the broker

      // You can use event.packetId
      break;
    case HomieEventType::READY_TO_SLEEP:
      // After you've called `prepareToSleep()`, the event is triggered when MQTT is disconnected
      break;
    case HomieEventType::MQTT_READY:{
      MQTT_DISC_FLAG = false;
      ledMode == "pulse-white";
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

  // seta pinos
  pinMode(VOLT_PIN, OUTPUT);
  pinMode(RINT_PIN, OUTPUT);
  pinMode(REXT_PIN, OUTPUT);
  digitalWrite(VOLT_PIN, LOW);
  digitalWrite(RINT_PIN, HIGH);
  digitalWrite(REXT_PIN, LOW);

  // parametros Homie
  Homie_setFirmware("SOHO MQTT Automatic Door", "0.0.2");
  Homie_setBrand("SOHO Labs");
  Homie.setSetupFunction(setupHandler).setLoopFunction(loopHandler);

  // inicializa os nodes
  filecheckNode.advertise("read").settable(fileHandler);
  regNode.advertise("new").settable(regOnHandler);
  unlockNode.advertise("open").settable(unlockHandler);
  operationModeNode.advertise("mode").settable(operationHandler);
  accessNode.advertise("attempt");
  offAccessNode.advertise("json");
  // inicializa o modo Event
  Homie.onEvent(onHomieEvent);
  Homie.disableLedFeedback();
  Homie.setup();
}

void loop(){
  Homie.loop();
  if(MQTT_DISC_FLAG)
    offlineMode();
}
