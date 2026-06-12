#include "Arduino.h"
#include "esp_task_wdt.h"

#include "config.h"

#include "hardware/bluetoothctl.h"
#include "hardware/improv.h"
#include "hardware/settings.h"
#include "hardware/webctl.h"
#include "hardware/wifictl.h"

#include "input/base.h"
#include "output/base.h"

/**
 * setup functionality
*/
void setup() {
  Serial.begin(115200);
  esp_task_wdt_init(30, true);
  esp_task_wdt_add(NULL);

  Settings::setup();
  BluetoothHandler::setup();
  WifiHandler::setup();
  WebHandler::setup();
  BaseInput::setup();

  esp_task_wdt_reset();
  delay(10);
}

/**
 * loop functionality
*/
void loop() {
  BluetoothHandler::loop();
  WifiHandler::loop();
  WebHandler::loop();
  ImprovHandler::loop();
  BaseInput::loop();

  esp_task_wdt_reset();
  delay(10);
}
