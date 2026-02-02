#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>

#include "ConfigurationManager.h"
#include "ApplicationManager.h"
#include "PrinterManager.h"
#include "NFCManager.h"

// LCD I2C pins
#define LCD_SDA 23
#define LCD_SCL 22

// LCD at I2C address 0x27 (common for 1602), 16 chars, 2 lines
LiquidCrystal_I2C lcd(0x27, 16, 2);

void initWiFi() {
  auto& config = ConfigurationManager::getInstance();

  Serial.print("Connecting to WiFi: ");
  Serial.println(config.getWiFiSSID());

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");

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

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi OK");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    delay(2000);
  } else {
    Serial.println();
    Serial.println("WiFi connection failed!");

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi FAILED");
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
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");
  Serial.println("LCD initialized");

  // Initialize ApplicationManager (message queue)
  if (!ApplicationManager::getInstance().begin()) {
    Serial.println("ApplicationManager init failed - halting");
    lcd.clear();
    lcd.print("AppMgr FAILED");
    while (1) { delay(1000); }
  }

  // Connect to WiFi
  initWiFi();

  // Initialize NFCManager
  if (!NFCManager::getInstance().begin()) {
    Serial.println("NFCManager init failed - halting");
    lcd.clear();
    lcd.print("NFC FAILED");
    while (1) { delay(1000); }
  }

  // Initialize and start PrinterManager
  PrinterManager::getInstance().begin();
  PrinterManager::getInstance().startPollingTask();

  // Start NFC scan task
  NFCManager::getInstance().startScanTask();

  // Ready
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ready");
  Serial.println("=== Setup complete ===");
}

void loop() {
  // Process any pending messages
  ApplicationManager::getInstance().processMessages();

  // NFC scanning is now handled by NFCManager in its own task
  delay(100);
}
