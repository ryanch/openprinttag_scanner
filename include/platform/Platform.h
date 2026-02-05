#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef ARDUINO
  #include <Arduino.h>
  #include <freertos/FreeRTOS.h>
  #include <freertos/queue.h>
  #include <freertos/semphr.h>
#else
  #include "NativePlatform.h"
#endif

#endif // PLATFORM_H
