#pragma once

void nanoleafMenu();
bool nanoleafPowerOff();

#include <Arduino.h>
String nanoleafStatus();
bool nanoleafRestore(const String &data);
