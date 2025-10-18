#include <Arduino.h>
#include <esp_dds.h>

// Simple message counter
volatile int received_count = 0;

// Callback when message arrives
void message_received(const char* topic, const void* data, size_t size, void* context) {
    int number = *(int*)data;
    Serial.printf("Received: %d\n", number);
    received_count++;
}

// Publisher task
void publisher_task(void* param) {
    int counter = 0;
    
    while (1) {
        // Publish a number every second
        ESP_DDS_PUBLISH("/numbers", counter);
        Serial.printf("Published: %d\n", counter);
        counter++;
        
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    // Initialize DDS
    ESP_DDS_INIT();
    
    // Subscribe to numbers topic
    ESP_DDS_SUBSCRIBE("/numbers", message_received, NULL);
    
    // Start publisher task
    xTaskCreate(publisher_task, "publisher", 2048, NULL, 1, NULL);
    
    Serial.println("✅ PubSub Example Ready");
    Serial.println("📤 Publishing numbers every second...");
}

void loop() {
    // Process DDS system
    ESP_DDS_PROCESS_PENDING(10);
    delay(10);
}