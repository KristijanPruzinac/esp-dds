#include <Arduino.h>
#include "esp_dds.h"
#include "esp_dds_test.h"

void test_runner_task(void* param) {
    Serial.println("🧪 ESP-DDS Test Runner Started");
    
    // Run comprehensive test
    esp_dds_run_comprehensive_test();
    
    Serial.println("✅ Test cycle completed");
    vTaskDelete(NULL);
}

void setup() {
    Serial.begin(115200);
    DDS_DELAY(2000);
    Serial.println("\n\n=== ESP-DDS Test Starting ===\n");
    
    // Initialize DDS system
    ESP_DDS_INIT();
    
    // Start test runner
    xTaskCreate(test_runner_task, "TestRunner", 16384, NULL, 1, NULL);
    
    Serial.println("✅ System started - tests running in background");
}

void loop() {
    // Main loop - process DDS system
    ESP_DDS_PROCESS_ACTIONS();
    ESP_DDS_PROCESS_PENDING(10);
    DDS_DELAY(10);
}