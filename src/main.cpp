#include <Homie.h>
#include <ArduinoJson.h>
#include <NtpClientLib.h>
#include <MFRC522.h>
#include <WiFiUDP.h>
#include <TimeLib.h>
#include <FS.h>

// Flags
bool MQTT_DISC_FLAG = true;

// Variáveis RFID
const int rfidss = 15;
const int rfidgain = 112;
int PIN_RELAY = 4;
unsigned long cooldown = 0;
String tag;
// Variáveis NTP
const char * ntpserver = "br.pool.ntp.org";
int ntpinter = 30;
WiFiUDP ntpUDP;
int16_t timeZone = -3;
uint32_t currentMillis = 0;
uint32_t previousMillis = 0;
time_t timestamp;
// Create MFRC522 RFID instance
MFRC522 mfrc522 = MFRC522();

/*------------------------------Rotinas RFID----------------------------------*/
void setupRFID(int rfidss, int rfidgain) {
  SPI.begin();                                                                  // Inicializa o protocolo SPI para o MFRC522
  mfrc522.PCD_Init(rfidss, UINT8_MAX);                                          // Inicializa MFRC522 Hardware
  mfrc522.PCD_SetAntennaGain(rfidgain);                                         // Seta o ganho da antena

  // Serial.printf("[ INFO ] RFID SS_PIN: %u and Gain Factor: %u", rfidss, rfidgain);
  // Serial.println("");
}
void rfidloop(){
  String uid = "";
  //If a new PICC placed to RFID reader continue
  if ( !mfrc522.PICC_IsNewCardPresent()) {
          delay(10);
          return;
  }
  //Since a PICC placed get Serial (UID) and continue
  if ( !mfrc522.PICC_ReadCardSerial()) {
          delay(10);
          return;
  }
  // We got UID tell PICC to stop responding
  mfrc522.PICC_HaltA();
  cooldown = millis() + 500;

  // There are Mifare PICCs which have 4 byte or 7 byte UID
  // Get PICC's UID and store on a variable
  Homie.getLogger() << "[ INFO ] PICC's UID: " << endl;
  for (int i = 0; i < mfrc522.uid.size; ++i) {
          uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  Serial.print(uid);
  // Get PICC type
  MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
  String type = mfrc522.PICC_GetTypeName(piccType);

}
/*--------------------Funções e declarações para o HOMIE----------------------*/
HomieNode rfidNode("auth", "Rfid");
HomieNode lockNode("fechadura", "Relay");
HomieNode regNode("cadastro", "File");
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////

/*----------------------------Modos de operação-------------------------------*/
void online_mode(time_t timestamp){
  Homie.getLogger() << "Leitura da Tag - Modo Online" << endl;
  if (currentMillis >= cooldown) {
      rfidloop();
  }
  // Prepara o envio do JSON para o Servidor
  DynamicJsonBuffer JSONbuffer;;
  JsonObject& JSONencoder = JSONbuffer.createObject();
  JSONencoder["tag"] = tag;
  JSONencoder["time"] = NTP.getTimeStr(timestamp);
  char JSONmessageBuffer[300];
  JSONencoder.prettyPrintTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));
  Homie.getLogger() << "Leitura da TAG: " << JSONmessageBuffer << endl;
  rfidNode.setProperty("read").send(tag);
}
void offline_mode(time_t timestamp){
  Homie.getLogger() << "Leitura da Tag - Modo Offline" << endl;
  if (currentMillis >= cooldown) {
      rfidloop();
  }

  String off_tag = "/U/";
  off_tag += tag;
  off_tag += ".json";
  if (SPIFFS.exists(off_tag)) {
    File off = SPIFFS.open(off_tag, "r");
    size_t size = off.size();
    // Aloca o buffer para determinar o tamanho do arquivo.
    std::unique_ptr<char[]> buf(new char[size]);
    off.readBytes(buf.get(), size);
    DynamicJsonBuffer jsonBuffer0;
    JsonObject& json = jsonBuffer0.parseObject(buf.get());
    String demo = json["tag"];
    Serial.print(demo);Serial.println();
    if(demo == tag){
      DynamicJsonBuffer jsonBuffer1;
      JsonObject& mqttlog = jsonBuffer1.createObject();
      time_t timestamp = NTP.getTime();
      mqttlog["tag"] = tag;
      mqttlog["time"] = NTP.getTimeStr(timestamp);
      File log = SPIFFS.open("/log/Mqtt_Disconnected.json", "a");
      mqttlog.prettyPrintTo(log);
      log.close();
    }
  }
}
////////////////////////////////////////////////////////////////////////////////

/*----------------------------HOMIE Handlers----------------------------------*/
bool regOnHandler(const HomieRange& range, const String& value){
  if(value=="0") return false;
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
        // Serial.print("UID: ");Serial.print(UID);Serial.println();
        // Serial.print("start: ");Serial.print(start);Serial.println();
        // Serial.print("stop: ");Serial.print(stop);Serial.println();
        // Serial.print("days: ");Serial.print(days);Serial.println();
        // Serial.print("revoke: ");Serial.print(revoke);Serial.println();
        // Serial.print("Arquivo: "); Serial.print(f);Serial.println();
        // Serial.print("Path: ");Serial.print(path_userID);

  // Salva o arquivo no endereço indicado
  File f = SPIFFS.open(path_userID, "w");
  user.prettyPrintTo(f);
  f.close();

  return true;
}

bool lockOnHandler(const HomieRange& range, const String& value) {
  if (value != "true" && value != "false") return false;

  bool on = (value == "true");
  digitalWrite(PIN_RELAY, !on ? HIGH : LOW);
  rfidNode.setProperty("open").send(value);
  Homie.getLogger() << "Autorização: " << (on ? "true" : "false") << endl;

  return true;
}

void setupHandler() {
  rfidNode.setProperty("leitura").send("c");
}

void loopHandler() {
  if(!MQTT_DISC_FLAG) {
      online_mode(timestamp);                                     // Define o tópico soho/deviceID/lockreader/read como publish
    }
    else {
      offline_mode(timestamp);
  }
}
////////////////////////////////////////////////////////////////////////////////
/*-----------------------------Homie events-----------------------------------*/
void onHomieEvent(const HomieEvent& event) {
  switch(event.type) {
    case HomieEventType::MQTT_READY:
      MQTT_DISC_FLAG = false;
    break;
    case HomieEventType::MQTT_DISCONNECTED:
      MQTT_DISC_FLAG = true;
    break;
  }
}
void setup() {
    pinMode(D1, INPUT);
    Serial.begin(115200);
    Serial << endl << endl;
    SPIFFS.begin();
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, HIGH);
    Homie_setFirmware("RFID Reader", "0.0.1");
    setupRFID(rfidss, rfidgain);
    NTP.begin(ntpserver, timeZone);
    NTP.setInterval(ntpinter * 60);
    timestamp = NTP.getTime();
    Homie.setSetupFunction(setupHandler).setLoopFunction(loopHandler);
    regNode.advertise("novo").settable(regOnHandler);
    lockNode.advertise("open").settable(lockOnHandler);
    rfidNode.advertise("read");
    Homie.onEvent(onHomieEvent);
    Homie.setup();
}

void loop() {
    Homie.loop();
    if (digitalRead(D1) == HIGH){
      Serial.print(MQTT_DISC_FLAG);Serial.println();
      // Rotina para printar os tamanhos dos arquivos salvos na memória do ESP
      Dir dir = SPIFFS.openDir("/U");
      while (dir.next()) {
          Serial.print(dir.fileName());
          File f = dir.openFile("r");
          Serial.print(" Tamanho do arquivo: "); Serial.print(f.size());Serial.println();
      }
    }
}
