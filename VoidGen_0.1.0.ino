//        INCLUDES

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32RotaryEncoder.h>
#include <Arduino.h>
#include <ESP32QRCodeReader.h>
#include <mbedtls/aes.h>
#include <mbedtls/gcm.h>
#include <vector>
#include <Base32-Decode.h>
#include "esp_camera.h"
#include "Preferences.h"
#include "RTClib.h"
#include "sha1.h"
#include "TOTP.h"
#include "LittleFS.h"


//        DEFINES

#define I2C_SDA 42
#define I2C_SCL 41

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32

#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C


//        CONSTS

const int8_t DI_ENCODER_SW = 19;
const uint8_t DI_ENCODER_B = 20;
const uint8_t DI_ENCODER_A = 21;


//        GLOBAL VARIABLES

struct Account{
  char issuer[32];
  char secret[128];
};

std::vector<Account> accounts;

enum MENU{
  ADD = -1,
  SEARCH = 1
};

enum MENU g_menu = SEARCH;

volatile bool g_knobPressed = false;
volatile bool g_knobTurned = false;
volatile int g_knobValue = 1;

volatile bool g_newQrDataAvailable = false;
bool g_lock = false;
String g_qrPayload = "";

bool g_loggedIn = false;
int g_password[4] = {0, 0, 0, 0};
bool g_initialCheckDone = false;

int g_index = 0;
bool g_selected = false;


//        DEFINITIONS

Preferences dataStorage;
Preferences metadataStorage;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
RTC_DS1307 rtc;
RotaryEncoder rotaryEncoder(DI_ENCODER_A, DI_ENCODER_B, DI_ENCODER_SW);
ESP32QRCodeReader reader(CAMERA_MODEL_AI_THINKER);

TaskHandle_t qrTaskHandle = NULL;


//        PROTOTYPES

void searchMenu();
void addMenu();
void cameraSetup();
void checkPassword();

Preferences preferences;


//        SETUP FUNCTIONS

void setup() {

  Serial.begin(115200);
  
  while(!Serial);
  Serial.println("Starting...");

  Wire.begin(I2C_SDA, I2C_SCL);

  //    Screen setup

  //  Delay to make sure screen works properly
  delay(2000);

  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)){
    Serial.println("ERROR: Screen initialization failed!");
    Serial.flush();
    while(1) delay(10);
  }

  Serial.println("Display initialized!");


  //    RTC setup

  if(!rtc.begin()){
    Serial.println("ERROR: RTC initialization failed!");
    Serial.flush();
    while(1) delay(10);
  }

  Serial.println("RTC initialized!");

  //    Rotary encoder setup

  rotaryEncoder.setEncoderType(EncoderType::FLOATING);
  rotaryEncoder.setBoundaries(0, 4, true);
  rotaryEncoder.onTurned(&knobCallback);
  rotaryEncoder.onPressed(&buttonCallback);
  rotaryEncoder.begin();

  Serial.println("Encoder initialized!");


  //    Call seperate function for camera setup

  cameraSetup();


  //    File system setup

  if(!LittleFS.begin(true)){
    Serial.println("LittleFS Mount failed! System halted.");
    Serial.flush();
    while(1) delay(10);
  }

  Serial.println("LittleFS mounted successfully.");

  //    Uncomment to DELETE ALL entries!
  //LittleFS.format();
  //Serial.println("LittleFS formatted!");
  //while(1);


  Serial.println("System setup successful!");
}

void cameraSetup(){

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = 11;
  config.pin_d1 = 9;
  config.pin_d2 = 8;
  config.pin_d3 = 10;
  config.pin_d4 = 12;
  config.pin_d5 = 18;
  config.pin_d6 = 17;
  config.pin_d7 = 16;
  config.pin_xclk = 15;
  config.pin_pclk = 13;
  config.pin_vsync = 6;
  config.pin_href = 7;
  config.pin_sccb_sda = 4;
  config.pin_sccb_scl = 5;
  config.pin_pwdn = -1;
  config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_GRAYSCALE; 
  config.frame_size = FRAMESIZE_QVGA;        
  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);

  if(err != ESP_OK){
    Serial.printf("ERROR: Camera initialization failed with error 0x%x", err);
    Serial.flush();
    while(1) delay(10);
  }

  Serial.println("Camera initialized successfully!");
}

//--------------------------------------------

bool compareAccounts(const Account& a, const Account& b){
  return strcasecmp(a.issuer, b.issuer) < 0;
}

void onQrCodeTask(void *pvParameters){
  
  struct QRCodeData qrCodeData;

  while(true){
    if(reader.receiveQrCode(&qrCodeData, 100)){
      Serial.println("Found QR code!");
      
      if(qrCodeData.valid){
        Serial.println("QR code successfully scanned!");

        g_qrPayload = String((const char*)qrCodeData.payload);
        g_newQrDataAvailable = true;

        reader.end();
        Serial.println("QR reader hardware stopped.");
        
        qrTaskHandle = NULL;
        vTaskDelete(NULL);

        return;
      }else{
        Serial.println("ERROR: Invalid QR code detected.");
      }
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void knobCallback(long value){
  g_knobValue = value;
  g_knobTurned = true;

  if(g_selected && g_loggedIn == false){
    if(g_index < 4){
      g_password[g_index] = value; 
    }
  }else{
    g_index = value;
  } 

  //Serial.printf("Knob - Index: %d - %d\n", g_knobValue, g_index);
}

void buttonCallback(unsigned long duration){

  Serial.printf("Boop! Button was down for %u ms\n", duration);

  if(g_loggedIn == false){

    if(g_selected == false && g_index == 4){
      Serial.println("Submitting PIN...");
      checkPassword();
      
      return;
    }

    g_selected = !g_selected;

    if(g_selected){
      if(g_index < 4){
        rotaryEncoder.setBoundaries(0, 9, true);
        rotaryEncoder.setEncoderValue(g_password[g_index]);
        Serial.println("Selected!");
      }else{
        g_selected = false;
      }
    }else{
      rotaryEncoder.setBoundaries(0, 4, true);
      rotaryEncoder.setEncoderValue(g_index);
      Serial.println("Unselected!");
    }

    return;
  }

  //  If long press -> switch menu
  if(duration >= 350){
    g_menu = (g_menu == SEARCH) ? ADD : SEARCH;
    Serial.printf("Switched to : %s menu\n", (g_menu == 1) ? "SEARCH" : "ADD");
    return;
  }

  g_knobPressed = true;

}

String encrypt(const std::vector<Account>& plainData){

  if(plainData.empty()) return "";

  size_t elementSize = sizeof(Account);
  size_t plainLen = plainData.size() * elementSize;

  std::vector<unsigned char> outputBuffer(plainLen + 16);

  unsigned char iv[12];
  unsigned char tag[16];

  for(int i = 0; i < 12; i += 4){
    uint32_t randomVal = esp_random();
    memcpy(&iv[i], &randomVal, 4);
  }

  preferences.begin("crypto", true);
  String masterKey = preferences.getString("masterKey", "");

  if(masterKey == ""){
    Serial.println("ERROR: Master key not found!");
    Serial.flush();
    while(1) delay(100);
  }

  const unsigned char* keyBytes = (const unsigned char*) masterKey.c_str();

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, keyBytes, 256);
  mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, plainLen, iv, 12, nullptr, 0, (unsigned char*)plainData.data(), outputBuffer.data(),  16, tag);
  mbedtls_gcm_free(&gcm);

  String finalHex = "";

  for(int i = 0; i < 12; i++){
    if(iv[i] < 0x10){
      finalHex += "0";
    }
    finalHex += String(iv[i], HEX);
  }

  for(int i = 0; i < plainLen; i++){
    if(outputBuffer[i] < 0x10){
      finalHex += "0";
    }
    finalHex += String(outputBuffer[i], HEX);
  }

  for(int i = 0; i < 16; i++){
    if(tag[i] < 0x10){
      finalHex += "0";
    }
    finalHex += String(tag[i], HEX);
  }

  return finalHex;
}

bool decrypt(const String& hexData, std::vector<Account>& output){
  
  size_t expectedLen = hexData.length() / 2;

  if(expectedLen < 12 + 16) return false;
  
  size_t cipherLen = expectedLen - 12 - 16;
  
  std::vector<unsigned char> buffer(expectedLen);
  
  for(int i = 0; i < expectedLen; i++){
    sscanf(hexData.c_str() + i*2, "%2hhx", &buffer[i]);
  }
  
  unsigned char iv[12];
  unsigned char tag[16];

  memcpy(iv, buffer.data(), 12);
  memcpy(tag, buffer.data() + 12 + cipherLen, 16);
  
  std::vector<unsigned char> decrypted(cipherLen);

  preferences.begin("crypto", true);
  String masterKey = preferences.getString("masterKey", "");

  if(masterKey == ""){
    Serial.println("ERROR: Master key not found!");
    Serial.flush();
    while(1) delay(100);
  }

  const unsigned char* keyBytes = (const unsigned char*) masterKey.c_str();
  
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, keyBytes, 256);
  
  int ret = mbedtls_gcm_auth_decrypt(&gcm, cipherLen, iv, 12, nullptr, 0, tag, 16, buffer.data() + 12, decrypted.data());
  
  mbedtls_gcm_free(&gcm);
  
  if(ret != 0) return false;
  
  output.clear();

  size_t numAccounts = cipherLen / sizeof(Account);
  
  output.resize(numAccounts);
  memcpy(output.data(), decrypted.data(), cipherLen);

  for(size_t i = 0; i < numAccounts; i++){
    output[i].issuer[31] = '\0';
    output[i].secret[127] = '\0';
  }
  
  return true;
}

String getCode(const Account& account, int &secondsLeft){
  
  unsigned char rawSecret[80];
  memset(rawSecret, 0, sizeof(rawSecret));

  int decodedLen = base32decode(account.secret, rawSecret, sizeof(rawSecret));

  if(decodedLen <= 0){
    secondsLeft = 0;
    return "ERR:B32";
  }

  TOTP totp(rawSecret, decodedLen);

  DateTime now = rtc.now();
  long currentTimestamp = now.unixtime();

  int secondsPassed = currentTimestamp % 30;
  secondsLeft = 30 - secondsPassed;

  char* dynamicCode = totp.getCode(currentTimestamp);
  
  return String(dynamicCode);
}

void searchMenu(){

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.setTextColor(WHITE, BLACK);

  if(accounts.empty()){
    display.println("No accounts!");
    display.println("LONG press to switch menu");
  }else{

    if(g_index >= (int)accounts.size()){
      g_index = accounts.size() - 1;
    }

    if(g_index < 0){
      g_index = 0;
    }

    int secondsLeft = 0;
    String liveCode = getCode(accounts[g_index], secondsLeft);

    display.printf("[%d/%d] %s\n", g_index + 1, accounts.size(), accounts[g_index].issuer);
    
    display.setTextSize(2);
    display.setCursor(0, 12);
    display.print(liveCode);

    int barWidth = map(secondsLeft, 0, 30, 0, 127);

    display.drawFastHLine(0, 31, barWidth, WHITE);
  }

  display.display();
}

void addMenu(){

  noInterrupts();
  bool pressed = g_knobPressed;
  if(pressed) g_knobPressed = false;
  interrupts();

  if(g_newQrDataAvailable == true){

    Serial.println("Processing scanned QR payload...");

    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println("Adding key...");
    display.display();

    Serial.println(g_qrPayload);

    g_lock = true;
    saveEntry(g_qrPayload);
    g_lock = false;

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Key added!");
    display.display();

    g_newQrDataAvailable = false;
    g_menu = SEARCH;

    return;
  }

  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println("Scan QR");
  display.display();

  if(qrTaskHandle == NULL){
    reader.beginOnCore(1);
    Serial.println("Begin QR reader on core 1");
    xTaskCreate(onQrCodeTask, "onQrCode", 4 * 1024, NULL, 4, &qrTaskHandle);
  }

  if(pressed && qrTaskHandle != NULL){

    Serial.println("Cancelling adding key!");

    vTaskDelete(qrTaskHandle);

    qrTaskHandle = NULL;
    g_menu = SEARCH;
    return;
  }
}

void saveData(){

  String encryptedHex = encrypt(accounts);

  File file = LittleFS.open("/accounts.bin", FILE_WRITE);

  if(!file){
    Serial.println(" ERROR: Failed to open file for writing!");
    return;
  }

  file.write((uint8_t*)encryptedHex.c_str(), encryptedHex.length());
  file.close();

  Serial.printf("Database saved safely! Wrote %d bytes (%d accounts).\n", encryptedHex.length(), accounts.size());
}

void loadData(){

  String hexData = "";

  File file = LittleFS.open("/accounts.bin", FILE_READ);

  if(!file){
    Serial.println("ERROR: Failed to open file for reading!");
    return;
  }

  size_t fileSize = file.size();

  if(fileSize == 0){
    Serial.println("ERROR: Database file is empty.");
    file.close();
    return;
  }

  hexData.reserve(fileSize);

  while(file.available()){
    hexData += (char)file.read();
  }

  file.close();

  if(!decrypt(hexData, accounts)){
    Serial.println("ERROR: Decryption failed!");
    accounts.clear();
  }else{
    Serial.printf("Successfully loaded %d accounts from file!\n", accounts.size());
  }    
}

//    Currently ignores any parameter after the secret!
void saveEntry(String payload){

  int totpMarker = payload.indexOf("/totp/");
  int colonMarker = payload.indexOf(":", totpMarker + 6);
  int questionMarker = payload.indexOf("?");
  int secretMarker = payload.indexOf("secret=");

  if(totpMarker == -1 || secretMarker == -1){
    Serial.println("Error: Critical TOTP markers missing.");
    return;
  }

  String serviceName = "";

  if(colonMarker != -1 && colonMarker < questionMarker){
    serviceName = payload.substring(totpMarker + 6, colonMarker);
  }else{
    serviceName = payload.substring(totpMarker + 6, questionMarker);
  }

  int endOfSecretMarker = payload.indexOf("&", secretMarker);
  String secretKey = "";

  if(endOfSecretMarker == -1){
    secretKey = payload.substring(secretMarker + 7);
  }else{
    secretKey = payload.substring(secretMarker + 7, endOfSecretMarker);
  }

  serviceName.trim();
  secretKey.trim();

  Serial.println("--- PARSING SUCCESSFUL ---");
  Serial.printf("Service Name: %s\n", serviceName.c_str());
  Serial.printf("Secret Key:  %s\n", secretKey.c_str());

  Account newAccount;
  memset(&newAccount, 0, sizeof(Account));

  strncpy(newAccount.issuer, serviceName.c_str(), 31);
  newAccount.issuer[31] = '\0';
  strncpy(newAccount.secret, secretKey.c_str(), 127);
  newAccount.secret[127] = '\0';

  accounts.push_back(newAccount);
  std::sort(accounts.begin(), accounts.end(), compareAccounts);
  saveData();

  rotaryEncoder.setBoundaries(0, accounts.size() - 1, true);
  rotaryEncoder.setEncoderValue(0);

  Serial.println("Saved encrypted key to vector and file!");
}

void deleteEntry(){

  if(g_index < accounts.size()){

    accounts.erase(accounts.begin() + g_index);
    saveData();

    g_index = 0;

    if(!accounts.empty()){
      rotaryEncoder.setBoundaries(0, accounts.size() - 1, true);
      rotaryEncoder.setEncoderValue(0);
    }else{
      rotaryEncoder.setBoundaries(0, 0, false);
      rotaryEncoder.setEncoderValue(0);
    }
  }
}

void login(){

  display.clearDisplay();
  display.setTextSize(2);

  int xPos = 0;

  for(int i = 0; i < 4; i++){
    
    xPos = 15 + (i * 18);
    display.setCursor(xPos, 4);

    if(g_selected && i == g_index){
      display.setTextColor(BLACK, WHITE);
      display.print(g_password[i]);
    }else{
      display.setTextColor(WHITE, BLACK);
      display.print(g_password[i]);
    }
  }

  xPos = 15 + (4 * 18) + 6;
  display.setCursor(xPos, 4);

  if(g_selected == false && g_index == 4){
    display.setTextColor(BLACK, WHITE);
    display.print("OK");
  }else{
    display.setTextColor(WHITE, BLACK);
    display.print("OK");
  }

  if(g_selected == false){
    if(g_index < 4){
      xPos = 15 + (g_index * 18);
      display.drawFastHLine(xPos, 20, 9, WHITE);
    }else{
      display.drawFastHLine(xPos, 20, 12, WHITE);
    }
  }

  display.display();
}

void checkPassword(){

  preferences.begin("crypto", true);

  int loginPin[4];

  size_t bytesRead = preferences.getBytes("pin", loginPin, sizeof(loginPin));

  if(bytesRead > 0){
    Serial.printf("Successfully read %d bytes from flash!\n", bytesRead);
  }else{
    Serial.println("ERROR: Failed to get login PIN!");
    Serial.flush();
    while(1) delay(100);
  }

  preferences.end();

  if(memcmp(g_password, loginPin, sizeof(loginPin)) == 0){
    Serial.println("Password valid! Logging in!");
    g_loggedIn = true;
  }else{
    Serial.println("Password invalid!");

    g_index = 0;
    for(int i = 0; i < 4; i++){
      g_password[i] = 0;
    }
    rotaryEncoder.setEncoderValue(0);
  }

}

void loop() {

  if(g_loggedIn == false){
    login();
    return;
  }

  if(g_initialCheckDone == false){
    loadData();

    g_index = 0;

    if(!accounts.empty()){
      rotaryEncoder.setBoundaries(0, accounts.size() - 1, true);
      rotaryEncoder.setEncoderValue(0);
    }else{
      rotaryEncoder.setBoundaries(0, 0, false);
      rotaryEncoder.setEncoderValue(0);
    }

    g_menu = SEARCH;
    g_initialCheckDone = true;
  }

  if(g_lock) return;

  switch(g_menu){
    case SEARCH: searchMenu(); break;
    case ADD: addMenu(); break;
  }
}