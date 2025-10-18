#ifndef DDS_PLATFORM_H
#define DDS_PLATFORM_H

#ifdef ARDUINO
    #include <Arduino.h>
    
    // Arduino platform implementations
    #define DDS_DELAY(ms) delay(ms)
    #define DDS_MILLIS() millis()
    #define DDS_MICROS() micros()
    #define DDS_TASK_DELAY(ms) vTaskDelay(pdMS_TO_TICKS(ms))
    
    // Arduino debug output
    #define DDS_DEBUG_PRINT(...) Serial.printf(__VA_ARGS__)
    #define DDS_DEBUG_PRINTLN(msg) Serial.println(msg)
    
#else
    // Generic FreeRTOS implementations
    #include <freertos/FreeRTOS.h>
    #include <freertos/task.h>
    #include <cstdio>
    
    // FreeRTOS platform implementations
    #define DDS_DELAY(ms) vTaskDelay(pdMS_TO_TICKS(ms))
    #define DDS_MILLIS() (xTaskGetTickCount() * portTICK_PERIOD_MS)
    
    // High-resolution microsecond timing for FreeRTOS
    #ifdef ESP_PLATFORM
        #include "esp_timer.h"
        #define DDS_MICROS() (uint32_t)(esp_timer_get_time())
    #else
        // Fallback for generic FreeRTOS - less accurate but better than before
        #define DDS_MICROS() (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS * 1000ULL)
    #endif
    
    #define DDS_TASK_DELAY(ms) vTaskDelay(pdMS_TO_TICKS(ms))
    
    // Generic debug output
    #define DDS_DEBUG_PRINT(...) printf(__VA_ARGS__)
    #define DDS_DEBUG_PRINTLN(msg) printf("%s\n", msg)
    
#endif

// Common platform-independent macros
#define DDS_MAX(a, b) ((a) > (b) ? (a) : (b))
#define DDS_MIN(a, b) ((a) < (b) ? (a) : (b))
#define DDS_ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#endif // DDS_PLATFORM_H