#include <Wire.h>
#include <WiFi.h>
#include <time.h>

#include "ConfigurationManager.h"
#include "BluetoothManager.h"
#include "ApplicationManager.h"
#include "PrinterManager.h"
#include "NFCManager.h"
#include "PrusaLinkAPIStrategy.h"
#include "StubPrinterLinkStrategy.h"
#include "LCDManager.h"

// LCD I2C pins
#define LCD_SDA 23
#define LCD_SCL 22

// LCD Manager
LCDManager lcdManager(0x27, 16, 2);

void initWiFi() {
  auto& config = ConfigurationManager::getInstance();

  Serial.print("Connecting to WiFi: ");
  Serial.println(config.getWiFiSSID());

  lcdManager.updateScreen("Connecting WiFi", "");

  WiFi.begin(config.getWiFiSSID(), config.getWiFiPassword());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("WiFi connected! IP: ");
    Serial.println(WiFi.localIP());

    lcdManager.updateScreen("WiFi OK", WiFi.localIP().toString().c_str());

    Serial.println("Setting up NTP...");
    configTime(0, 0, "pool.ntp.org");
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
      Serial.println("Failed to obtain time");
      lcdManager.updateScreen("NTP FAILED", "");
    } else {
      Serial.println("Time obtained");
      //lcdManager.updateScreen("NTP OK", "");
    }

    delay(2000);
  } else {
    Serial.println();
    Serial.println("WiFi connection failed!");

    lcdManager.updateScreen("WiFi FAILED", "");
    delay(2000);
  }
}

void setup() {
  delay(1000);
  Serial.begin(9600);
  delay(1000);
  Serial.println("=== Starting setup ===");

  // Initialize I2C with custom pins for LCD
  Wire.begin(LCD_SDA, LCD_SCL);
  Serial.println("I2C initialized");

  // Initialize LCD and start its task on core 0
  lcdManager.begin();
  lcdManager.startTask();
  lcdManager.updateScreen("Initializing...", "");
  Serial.println("LCD initialized");

  // Initialize ConfigurationManager FIRST (loads NVS)
  if (!ConfigurationManager::getInstance().begin()) {
    Serial.println("ConfigurationManager init failed - halting");
    lcdManager.updateScreen("Config FAILED", "");
    while (1) { delay(1000); }
  }

  // Initialize ApplicationManager (message queue) with LCD reference
  if (!ApplicationManager::getInstance().begin(&lcdManager)) {
    Serial.println("ApplicationManager init failed - halting");
    lcdManager.updateScreen("AppMgr FAILED", "");
    while (1) { delay(1000); }
  }


  // Initialize BluetoothManager BEFORE WiFi (they share the radio)
  lcdManager.updateScreen("Starting BLE...", "");
  if (!BluetoothManager::getInstance().begin()) {
    Serial.println("BluetoothManager init failed - continuing without BLE");
  }

  // Connect to WiFi
  initWiFi();

  // Initialize NFCManager
  if (!NFCManager::getInstance().begin()) {
    Serial.println("NFCManager init failed - halting");
    lcdManager.updateScreen("NFC FAILED", "");
    while (1) { delay(1000); }
  }

  // Initialize and start PrinterManager with stub strategy for testing
  // To use real PrusaLink API, replace StubPrinterLinkStrategy with PrusaLinkAPIStrategy
  //static StubPrinterLinkStrategy printerStrategy;
  static PrusaLinkAPIStrategy printerStrategy;
  PrinterManager::getInstance().setStrategy(&printerStrategy);
  PrinterManager::getInstance().begin();

  // One synchronous check before polling task starts (no race condition)
  printerStrategy.update();

  PrinterManager::getInstance().startPollingTask();

  // Start NFC scan task
  NFCManager::getInstance().startScanTask();

  // Build status screen
  auto& config = ConfigurationManager::getInstance();

  char bleInd = BluetoothManager::getInstance().isAdvertising() ? '+' : '!';

  char wifiInd;
  if (strlen(config.getWiFiSSID()) == 0) wifiInd = '?';
  else wifiInd = (WiFi.status() == WL_CONNECTED) ? '+' : '!';

  char prusaInd;
  if (strlen(config.getPrusaLinkURL()) == 0) prusaInd = '?';
  else prusaInd = printerStrategy.isConnected() ? '+' : '!';

  char line1[17], line2[17];
  snprintf(line1, sizeof(line1), "Status NFC+ BLE%c", bleInd);
  snprintf(line2, sizeof(line2), "PrusaLink%c Wifi%c", prusaInd, wifiInd);
  lcdManager.updateScreen(line1, line2);

  Serial.println("=== Setup complete ===");
}

void loop() {
  // Process any pending messages for the application
  ApplicationManager::getInstance().processMessages();

  // LCD and NFC scanning are handled by their own tasks
  delay(100);
}
