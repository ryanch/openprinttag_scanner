#include <Wire.h>
#include <WiFi.h>

#include "ConfigurationManager.h"
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

  // Initialize LCD
  lcdManager.begin();
  lcdManager.updateScreen("Initializing...", "");
  Serial.println("LCD initialized");

  // Initialize ApplicationManager (message queue)
  if (!ApplicationManager::getInstance().begin()) {
    Serial.println("ApplicationManager init failed - halting");
    lcdManager.updateScreen("AppMgr FAILED", "");
    while (1) { delay(1000); }
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
  static StubPrinterLinkStrategy stubStrategy;
  PrinterManager::getInstance().setStrategy(&stubStrategy);
  PrinterManager::getInstance().begin();
  PrinterManager::getInstance().startPollingTask();

  // Start NFC scan task
  NFCManager::getInstance().startScanTask();

  // Ready
  lcdManager.updateScreen("Ready", "");
  Serial.println("=== Setup complete ===");
}

void loop() {
  // Process any pending messages for the application
  ApplicationManager::getInstance().processMessages();

  // Process any pending messages for the LCD
  lcdManager.processQueue();

  // NFC scanning is now handled by NFCManager in its own task
  delay(100);
}
