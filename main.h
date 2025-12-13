#pragma once

#ifdef ESP32
#define INT_HANDLER IRAM_ATTR
#endif
#ifdef ESP8266
#define INT_HANDLER ICACHE_RAM_ATTR
#endif 
 