#include "DebugLogBuffer.h"
#include <Wire.h>
#include <WiFi.h>
#include <time.h>

#include "ConfigurationManager.h"
#include "BluetoothManager.h"
#include "ApplicationManager.h"
#include "PrinterManager.h"
#include "NFCManager.h"
#include "PrusaLinkAPIStrategy.h"
#include "SpoolmanManager.h"
#include "HomeAssistantManager.h"
#include "StubPrinterLinkStrategy.h"
#include "LCDManager.h"

// Global HTTP mutex for serializing WiFi HTTP requests
SemaphoreHandle_t g_httpMutex = nullptr;

// LCD I2C pins
#define LCD_SDA 23
#define LCD_SCL 22

// LCD Manager
LCDManager lcdManager(0x27, 16, 2);

void initWiFi() {
  auto& config = ConfigurationManager::getInstance();

  if (strlen(config.getWiFiSSID()) == 0) {
    DBG_LOGLN("WiFi SSID not configured - skipping WiFi");
    lcdManager.updateScreen("WiFi: no SSID", "Configure via BLE");
    delay(2000);
    return;
  }

  DBG_LOG("Connecting to WiFi: ");
  DBG_LOGLN(config.getWiFiSSID());

  lcdManager.updateScreen("Connecting WiFi", "");

  WiFi.begin(config.getWiFiSSID(), config.getWiFiPassword());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    DBG_LOG(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    DBG_LOGLN("");
    DBG_LOG("WiFi connected! IP: ");
    DBG_LOGLN(WiFi.localIP());

    lcdManager.updateScreen("WiFi OK", WiFi.localIP().toString().c_str());

    DBG_LOGLN("Setting up NTP...");
    configTime(0, 0, "pool.ntp.org");
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
      DBG_LOGLN("Failed to obtain time");
      lcdManager.updateScreen("NTP FAILED", "");
    } else {
      DBG_LOGLN("Time obtained");
      //lcdManager.updateScreen("NTP OK", "");
    }

    delay(2000);
  } else {
    DBG_LOGLN("");
    DBG_LOGLN("WiFi connection failed!");

    lcdManager.updateScreen("WiFi FAILED", "");
    delay(2000);
  }
}

void setup() {
  delay(1000);
  Serial.begin(9600);
  delay(1000);
  DBG_LOGLN("=== Starting setup ===");

  // Initialize I2C with custom pins for LCD
  Wire.begin(LCD_SDA, LCD_SCL);
  DBG_LOGLN("I2C initialized");

  // Initialize LCD and start its task on core 0
  lcdManager.begin();
  lcdManager.startTask();
  lcdManager.updateScreen("Initializing...", "");
  DBG_LOGLN("LCD initialized");

  // Initialize ConfigurationManager FIRST (loads NVS)
  if (!ConfigurationManager::getInstance().begin()) {
    DBG_LOGLN("ConfigurationManager init failed - halting");
    lcdManager.updateScreen("Config FAILED", "");
    while (1) { delay(1000); }
  }
  lcdManager.setScreenTimeoutMs(ConfigurationManager::getInstance().getLcdTimeoutMs());

  // Initialize ApplicationManager (message queue) with LCD reference
  if (!ApplicationManager::getInstance().begin(&lcdManager)) {
    DBG_LOGLN("ApplicationManager init failed - halting");
    lcdManager.updateScreen("AppMgr FAILED", "");
    while (1) { delay(1000); }
  }


  // Initialize BluetoothManager BEFORE WiFi (they share the radio)
  lcdManager.updateScreen("Starting BLE...", "");
  if (!BluetoothManager::getInstance().begin()) {
    DBG_LOGLN("BluetoothManager init failed - continuing without BLE");
  }

  // Connect to WiFi
  initWiFi();

  // Create global HTTP mutex for serializing HTTP requests
  g_httpMutex = xSemaphoreCreateMutex();
  if (g_httpMutex == nullptr) {
    DBG_LOGLN("Failed to create HTTP mutex - halting");
    lcdManager.updateScreen("Mutex FAILED", "");
    while (1) { delay(1000); }
  }

  // Initialize SpoolmanManager
  if (!SpoolmanManager::getInstance().begin(g_httpMutex)) {
    DBG_LOGLN("SpoolmanManager init failed - continuing without Spoolman");
  }

  // Initialize HomeAssistantManager
  if (!HomeAssistantManager::getInstance().begin()) {
    DBG_LOGLN("HomeAssistantManager init failed - continuing without HA");
  }

  // Load automation mode from config
  {
    uint8_t mode = ConfigurationManager::getInstance().getAutomationMode();
    ApplicationManager::getInstance().setAutomationMode(static_cast<AutomationMode>(mode));
    DBG_LOGF("Automation mode: %s\n",
                  mode == 0 ? "SELF_DIRECTED" : "CONTROLLED_BY_HA");
  }

  // Initialize NFCManager
  if (!NFCManager::getInstance().begin()) {
    DBG_LOGLN("NFCManager init failed - halting");
    lcdManager.updateScreen("NFC FAILED", "");
    while (1) { delay(1000); }
  }

  // Initialize and start PrinterManager with stub strategy for testing
  // To use real PrusaLink API, replace StubPrinterLinkStrategy with PrusaLinkAPIStrategy
  //static StubPrinterLinkStrategy printerStrategy;
  static PrusaLinkAPIStrategy printerStrategy;
  printerStrategy.setHttpMutex(g_httpMutex);
  PrinterManager::getInstance().setStrategy(&printerStrategy);
  PrinterManager::getInstance().begin();

  // One synchronous check before polling task starts (no race condition)
  printerStrategy.update();

  PrinterManager::getInstance().startPollingTask();

  // Start NFC scan task
  NFCManager::getInstance().startScanTask();

  // Start SpoolmanManager task
  SpoolmanManager::getInstance().startTask();

  // Build status + HA startup from same config snapshot
  auto& config = ConfigurationManager::getInstance();

  // Start HomeAssistantManager task
  DBG_LOGF("Setup: HA config before startTask: enabled=%s host='%s' host_len=%u port=%u user_set=%s\n",
                config.getHAEnabled() ? "true" : "false",
                config.getHAMqttHost(),
                static_cast<unsigned>(strlen(config.getHAMqttHost())),
                static_cast<unsigned>(config.getHAMqttPort()),
                strlen(config.getHAMqttUser()) > 0 ? "true" : "false");
  HomeAssistantManager::getInstance().startTask();

  ApplicationManager::getInstance().showStatusOnLCD();

  DBG_LOGLN("=== Setup complete ===");
}

void loop() {
  DebugLogBuffer::getInstance().drainPending(64);

  // Process any pending messages for the application
  ApplicationManager::getInstance().processMessages();

  // LCD and NFC scanning are handled by their own tasks
  delay(100);
}
