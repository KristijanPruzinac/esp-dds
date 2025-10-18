#include <Arduino.h>
#include <esp_dds.h>

// Service: doubles a number
bool double_service(const void* request, size_t req_size, void* response, size_t* resp_size, void* context) {
    int input = *(int*)request;
    int output = input * 2;
    
    *(int*)response = output;
    *resp_size = sizeof(int);
    
    Serial.printf("Service: %d -> %d\n", input, output);
    return true;
}

// Async callback
void service_result(const char* service, const void* response, size_t size, void* context) {
    int result = *(int*)response;
    Serial.printf("Async result: %d\n", result);
}

// Client task
void client_task(void* param) {
    int counter = 1;
    
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Wait for server
    
    while (1) {
        // Sync call
        int sync_response;
        ESP_DDS_CALL_SERVICE_SYNC("/double", counter, sync_response, 1000);
        Serial.printf("Sync call: %d -> %d\n", counter, sync_response);
        
        // Async call  
        ESP_DDS_CALL_SERVICE_ASYNC("/double", counter, service_result, NULL, 1000);
        
        counter++;
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    // Initialize DDS
    ESP_DDS_INIT();
    
    // Create service
    ESP_DDS_CREATE_SERVICE("/double", double_service, ESP_DDS_SYNC, NULL);
    
    // Start client
    xTaskCreate(client_task, "client", 2048, NULL, 1, NULL);
    
    Serial.println("✅ Services Example Ready");
    Serial.println("🔄 Calling service every 2 seconds...");
}

void loop() {
    // Process async responses
    ESP_DDS_PROCESS_PENDING(10);
    delay(10);
}