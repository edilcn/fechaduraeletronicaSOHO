#include <Homie.h>
#include <ArduinoJson.h>
#include <NtpClientLib.h>
#include <MFRC522.h>
#include <WiFiUDP.h>
#include <TimeLib.h>
#include <FS.h>

// Variáveis RFID
const int rfidss = 15;
const int rfidgain = 112;
int PIN_RELAY = 4;
unsigned long cooldown = 0;

// Variáveis NTP
const char * ntpserver = "br.pool.ntp.org";
int ntpinter = 30;
WiFiUDP ntpUDP;
int16_t timeZone = -3;
uint32_t currentMillis = 0;
uint32_t previousMillis = 0;
// Create MFRC522 RFID instance
MFRC522 mfrc522 = MFRC522();

// Funções RFID
void setupRFID(int rfidss, int rfidgain);
/*------------------------------Rotinas RFID----------------------------------*/
bool regOnHandler(const HomieRange& range, const String& value){
  if(value=="0") return false;
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(value);
  char* UID = root["UID"];
  char* start_dateTime = root["start"]["dateTime"]; // "2015-09-15T06:00:00+02:00"
  char* start_timeZone = root["start"]["timeZone"]; // "Europe/Zurich"
  char* end_dateTime = root["end"]["dateTime"]; // "2015-09-15T07:00:00+02:00"
  char* end_timeZone = root["end"]["timeZone"]; // "Europe/Zurich"
  char* recurrence0 = root["recurrence"][0]; // "RRULE:FREQ=WEEKLY;COUNT=5;BYDAY=TU,FR"
  char* userID = UID;
  String path_userID = "/U/";
  path_userID += userID;
  path_userID += ".json";

  DynamicJsonBuffer jsonBuffer1;
  JsonObject& user = jsonBuffer1.createObject();
  user["UID"] = UID;
  user["StartdateTime"] = start_dateTime;
  user["timeZone"] = start_timeZone;
  user["EnddateTime"] = end_dateTime;
  user["timeZone"] = end_timeZone;
  JsonArray& recurrence = user.createNestedArray("recurrence");
  recurrence.add(recurrence0);
  File f = SPIFFS.open(path_userID.c_str(), "w");
  root.prettyPrintTo(f);
  f.close();
  return true;
}
void ShowReaderDetails() {
  // Get the MFRC522 software version
  byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  Serial.print(F("[ INFO ] MFRC522 Version: 0x"));
  Serial.print(v, HEX);
  if (v == 0x91)
    Serial.print(F(" = v1.0"));
  else if (v == 0x92)
    Serial.print(F(" = v2.0"));
  else if (v == 0x88)
    Serial.print(F(" = clone"));
  else
    Serial.print(F(" (unknown)"));
  Serial.println("");
  // When 0x00 or 0xFF is returned, communication probably failed
  if ((v == 0x00) || (v == 0xFF)) {
    Serial.println(F("[ WARN ] Communication failure, check if MFRC522 properly connected"));
  }
}
void setupRFID(int rfidss, int rfidgain) {
  SPI.begin();           // MFRC522 Hardware uses SPI protocol
  mfrc522.PCD_Init(rfidss, UINT8_MAX);    // Initialize MFRC522 Hardware
  // Set RFID Hardware Antenna Gain
  // This may not work with some boards
  mfrc522.PCD_SetAntennaGain(rfidgain);
  Serial.printf("[ INFO ] RFID SS_PIN: %u and Gain Factor: %u", rfidss, rfidgain);
  Serial.println("");
  ShowReaderDetails(); // Show details of PCD - MFRC522 Card Reader details
}

/*--------------------Funções e declarações para o HOMIE----------------------*/
HomieNode rfidNode("leitura", "Rfid");
HomieNode lockNode("fechadura", "Relay");
HomieNode regNode("cadastro", "File");

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
  //If a new PICC placed to RFID reader continue
  if ( ! mfrc522.PICC_IsNewCardPresent()) {
    delay(50);
    return;
  }
  //Since a PICC placed get Serial (UID) and continue
  if ( ! mfrc522.PICC_ReadCardSerial()) {
    delay(50);
    return;
  }
  // We got UID tell PICC to stop responding
  mfrc522.PICC_HaltA();
  cooldown = millis() + 2000;

  // There are Mifare PICCs which have 4 byte or 7 byte UID
  // Get PICC's UID and store on a variable
  Serial.print(F("[ INFO ] PICC's UID: "));
  String uid = "";
  for (int i = 0; i < mfrc522.uid.size; ++i) {
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  // Get PICC type
  MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
  String type = mfrc522.PICC_GetTypeName(piccType);

  // Prepara o envio do JSON para o Servidor
  StaticJsonBuffer<300> JSONbuffer;
  JsonObject& JSONencoder = JSONbuffer.createObject();
  JSONencoder["tag"] = uid;
  JSONencoder["time"] = NTP.getTime();
  char JSONmessageBuffer[300];
  JSONencoder.prettyPrintTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));
  Homie.getLogger() << "Leitura da TAG: " << JSONmessageBuffer << endl;
  rfidNode.setProperty("leitura").send(JSONmessageBuffer); // Define o tópico soho/deviceID/lockreader/read como publish
}
////////////////////////////////////////////////////////////////////////////////
//1533133620        1533130020

void setup() {
    Serial.begin(115200);
    Serial << endl << endl;
    SPIFFS.begin();
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, HIGH);
    Homie_setFirmware("RFID Reader", "0.0.1");
    setupRFID(rfidss, rfidgain);
    NTP.begin(ntpserver, timeZone);
    NTP.setInterval(ntpinter * 60);
    Homie.setSetupFunction(setupHandler).setLoopFunction(loopHandler);
    regNode.advertise("novo").settable(regOnHandler);
    lockNode.advertise("open").settable(lockOnHandler);
    rfidNode.advertise("leitura");
    Homie.setup();
}

void loop() {
    Homie.loop();
}
