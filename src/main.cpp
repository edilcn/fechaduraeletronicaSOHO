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
#define DEVICE_ID "lock0"
#define DEVICE_CREDENTIAL "lXRkg%odXWp@"

// Informações do WiFi
#define SSID_STA "soholabs"
#define SSID_PASSWORD "s0h0l@b5"

// Pinos no ESP
#define VOLT_PIN  D0 // D0 16 (15v)
#define RINT_PIN  D1 // D1 5 (Radar Interno)
#define REXT_PIN  D2 // D2 4 (Radar Externo)

/*----------------------------------------------------------------------------------------|
 | OPERATION_MODE   | D0 (15v)  | D1 (Radar Interno)  | D2 (Radar Externo)  | RFID        | PINOUT BORNES CONTROLE
 |:----------------:|:---------:|:-------------------:|:-------------------:|:-----------:|
 | normal           | low       | high                | high                | off         |
 | night            | low       | high                | low                 | on          |
 | open             | high      | low                 | low                 | off         |
 | close            | low       | low                 | low                 | off         |
 ----------------------------------------------------------------------------------------*/
// Thinger.IO
ThingerESP8266 thing(USERNAME, DEVICE_ID, DEVICE_CREDENTIAL);

//NeoPixel
Adafruit_NeoPixel statusLed = Adafruit_NeoPixel(8, D9, NEO_RGB);

//NTPVARIAVIS
int8_t timeZone = -3;
int8_t minutesTimeZone = 0;

// Flags
bool Status;
bool REGISTER;

String OPERATION_MODE = "night";
String NEXT_OPERATION_MODE = "night";


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
int i=0;

uint doorTimer = 0;

String begin_date;                                                              // Dado proveniente do parse do arquivo.
String begin_time;                                                              // Dado proveniente do parse do arquivo.
String end_date;                                                                // Dado proveniente do parse do arquivo.
String end_time;                                                                // Dado proveniente do parse do arquivo.
String recurr;                                                                  // Dado proveniente do parse do arquivo.
String user_uid;                                                                // Dado proveniente do parse do arquivo.
String weekdays;                                                                // Dado proveniente do parse do arquivo.
//
String r_begin_date;                                                             // Dado proveniente do parse do Servidor.
String r_begin_time;                                                             // Dado proveniente do parse do Servidor.
String r_end_date;                                                               // Dado proveniente do parse do Servidor.
String r_end_time;                                                               // Dado proveniente do parse do Servidor.
String r_recurr;                                                                 // Dado proveniente do parse do Servidor.
String r_user_uid;                                                               // Dado proveniente do parse do Servidor.
String r_weekdays;                                                               // Dado proveniente do parse do Servidor.

int value;


/*------------------------------Rotinas LED----------------------------------*/
void ledController(){
  for (i=0; i<8; i++){
    if (ledMode == "pulse-white"){
      if (ledCounter > 253)
        ledDirection = -2;
      if (ledCounter < 2)
        ledDirection = 2;
      statusLed.setPixelColor(i, statusLed.Color(ledCounter,ledCounter,ledCounter));
      statusLed.show();
      ledCounter += ledDirection;
    }

  if (ledMode == "pulse-blue"){
    if (ledCounter > 253)
      ledDirection = -2;
    if (ledCounter < 2)
      ledDirection = 2;
    statusLed.setPixelColor(i, statusLed.Color(0,0,ledCounter));
    statusLed.show();
    ledCounter += ledDirection;
  }

  if (ledMode == "white"){
    statusLed.setPixelColor(i, statusLed.Color(254,254,254));
    statusLed.show();
  }

  if (ledMode == "red"){
    statusLed.setPixelColor(i, statusLed.Color(0,254,0));
    statusLed.show();
  }

  if (ledMode == "blink-green"){
    if (ledCounter < 100){
      if (ledCounter % 5 == 0)
        statusLed.setPixelColor(i, statusLed.Color(0,0,0));
      else
        statusLed.setPixelColor(i, statusLed.Color(254,0,0));
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
      if (ledCounter < 100){
        if (ledCounter % 5 == 0)
          statusLed.setPixelColor(i, statusLed.Color(0,0,0));
        else
          statusLed.setPixelColor(i, statusLed.Color(0,254,0));
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
    if (ledMode == "blink-blue"){
      if (ledCounter < 100){
        if (ledCounter % 5 == 0)
          statusLed.setPixelColor(i, statusLed.Color(0,0,0));
        else
          statusLed.setPixelColor(i, statusLed.Color(0,254,0));
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
  
}

void toggleWiFiStatus(){
  ledCounter = 0;
  ledMode = "blink-blue";
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

/*--------------------------- Filesystem -------------------------------*/
bool fileReader() {
  String line;
  File TEMP = SPIFFS.open("/data.csv", "r");
  while(TEMP.position()<TEMP.size()){
      line = TEMP.readStringUntil(0x5C);
      Serial.print(line);
    }
  Serial.print("Fechando Arquivo!");
  return false;
}

bool userFinder(String uid){
  File DATA_R = SPIFFS.open("/data.csv", "r");
  while (true){
    if (DATA_R.position()<DATA_R.size()){
      if (DATA_R.readStringUntil(';') == uid){
        DATA_R.close();
        Serial.print("Encontrado!");
        return true;
      }
      else DATA_R.readStringUntil('\n');
    }
    else {
      DATA_R.close();
      return false;
    }
  }
}

bool userInfo(String uid){
  File DATA_R = SPIFFS.open("/data.csv", "r");                                                                  // Arquivo para leitura de registros dos usuários.
  while (true){
    if (DATA_R.position()<DATA_R.size()){
      if (DATA_R.readStringUntil(';') == uid){
        user_uid = uid;
        begin_date = DATA_R.readStringUntil(';');
        begin_time = DATA_R.readStringUntil(';');
        end_time = DATA_R.readStringUntil(';');
        end_date = DATA_R.readStringUntil(';');
        recurr = DATA_R.readStringUntil(';');
        weekdays = DATA_R.readStringUntil('\n');
        Serial.println(user_uid +';'+begin_date+';'+begin_time+';'+end_time+';'+end_date+';'+recurr+';'+weekdays);
        DATA_R.close();
        return true;
      }
      else DATA_R.readStringUntil('\n');
    }
    else {
      DATA_R.close();
      return false;
    }
  }
}

bool userReg(){
  String str = "";
  String Copy = "";

  user_uid = "";
  user_uid += r_user_uid.c_str();

  begin_date = "";
  begin_date += r_begin_date.c_str();

  begin_time = "";
  begin_time += r_begin_time;

  end_time = "";
  end_time = r_end_time.c_str();

  end_date = "";
  end_date = r_end_date.c_str();

  recurr = "";
  recurr += r_recurr.c_str();

  weekdays = "";
  weekdays += r_weekdays.c_str();

  Serial.println("Strings convertidas: " + user_uid + ';' + begin_date + ';' + begin_time + ';' + end_time + ';' + end_date + ';' + recurr + ';' + weekdays);

  File f;                                                                      // Arquivo temporário
  File TEMP = SPIFFS.open("/temp.csv", "w");                                         // Abre um arquivo temp.txt no modo escrita

  Serial.println("DEBUG_0 : COMEÇO DE REGISTRO");

  if (!userFinder(user_uid)){
    f = SPIFFS.open("/data.csv", "a+");
    f.println(user_uid +';'+begin_date+';'+begin_time+';'+end_time+';'+end_date+';'+recurr+';'+weekdays);
    f.close();
  }
  else {
    f = SPIFFS.open("/data.csv", "r");
    while (f.position()<f.size()){
      str = f.readStringUntil(';');
      if (str == user_uid){
        f.readStringUntil('\n');
      }
      else {
        TEMP.print(str + ';');
        Copy = f.readStringUntil('\n');
        TEMP.println(Copy);
      }
    }
    TEMP.println(user_uid +';'+begin_date+';'+begin_time+';'+end_time+';'+end_date+';'+recurr+';'+weekdays);
    f.close();
    TEMP.close();
    SPIFFS.remove("/data.csv");
    SPIFFS.rename("/temp.csv","/data.csv");
  }
  f.close();
  return true;
  }

void printData(){
  Dir dir = SPIFFS.openDir("/");
  while (dir.next()) {
      Serial.println(dir.fileName());
      File f = dir.openFile("r");
      Serial.print(f.size());
      Serial.println();
      f.close();
  }
  Serial.print("FIM DO REGISTRO");
}

bool infoCheck(){
  return true;
}

bool userCheck(String uid){
  userInfo(uid);
  return true;
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
  if ((OPERATION_MODE == "night") || (OPERATION_MODE == "normal") || (OPERATION_MODE == "close"))
    if (tagReader())
      thing.stream(thing["tag"]);

  if (REGISTER == true){
    userReg();
    printData();
    REGISTER = false;
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
  pinMode(RINT_PIN, OUTPUT);
  pinMode(REXT_PIN, OUTPUT);
  digitalWrite(VOLT_PIN, HIGH);
  digitalWrite(RINT_PIN, HIGH);
  digitalWrite(REXT_PIN, LOW);


  thing.add_wifi(SSID_STA, SSID_PASSWORD);

  thing["mode"] = [](pson& in, pson& out){
      if(in.is_empty()){
        in = (int) value;
        out = (int) value;
      }
      else {
        value = in;
      }

      Serial.println("Valor recebido : " + value);
      
      if (value == 0)
        NEXT_OPERATION_MODE = "night";
      else if (value == 1)
        NEXT_OPERATION_MODE = "normal";
      else if (value == 2)
        NEXT_OPERATION_MODE = "open";
      else if (value == 3)
        NEXT_OPERATION_MODE = "close";
      else if (value == 4)
        NEXT_OPERATION_MODE = "locked";
      Serial.println("Modo de operação: " + NEXT_OPERATION_MODE);
      };
  
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
  };

  thing["botao"] = [](pson& in, pson& out){
    if(in.is_empty()){
        in = (bool) digitalRead(VOLT_PIN);
        out = (bool) digitalRead(VOLT_PIN);
    }
    else{
        digitalWrite(VOLT_PIN, in ? HIGH : LOW);
    }
  };

  thing["tag"] >> [](pson& out){
    out = uid;
  };

  thing["registry"] << [](pson& in){
      r_user_uid = (const char*)in["user_uid"];
      r_begin_date = (const char*)in["begin_date"];
      r_begin_time = (const char*)in["begin_time"];
      r_end_time = (const char*)in["end_time"];
      r_end_date = (const char*)in["end_date"];
      r_recurr = (const char*)in["recurr"];
      r_weekdays = (const char*)in["weekdays"];
      Serial.println("Registro a ser salvo: " + r_user_uid + ';' + r_begin_date + ';' + r_begin_time + ';' + r_end_time + ';' + r_end_date + ';' + r_recurr + ';' + r_weekdays);
      REGISTER = true;
  };
  thing["delete"] << [](pson& in){
    int deleete = in;
    if (deleete == 1){
      Serial.println("Arquivo removido!");
      SPIFFS.remove("/data.csv");
      ledCounter = 0;
      ledMode = "blink-red";
      deleete = 0;
    }
  };

  thing["readfile"] << [](pson& in){
    int read = in;
    if (read == 1){
      Serial.println("Começando leitura de arquivo");
      fileReader();
      ledCounter = 0;
      ledMode = "blink-green";
      read = 0;
    }
  };

  thing["check"] << [](pson& in){
    String checkuid = in;
    userInfo(checkuid);
  };

  // NTP EVENTS
  NTP.onNTPSyncEvent ([](NTPSyncEvent_t event) {
        ntpEvent = event;
        syncEventTriggered = true;
    });

  // set OTA hostname
  const char *hostname = "entrada";
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
