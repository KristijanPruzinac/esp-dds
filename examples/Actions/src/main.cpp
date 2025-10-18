#include <Arduino.h>
#include <esp_dds.h>

// Simple action: counts to a number
typedef struct {
    int target_count;
} count_goal_t;

typedef struct {
    int current_count;
} count_feedback_t;

typedef struct {
    int final_count;
} count_result_t;

bool count_goal_callback(const void* goal, size_t size, void* context) {
    count_goal_t* g = (count_goal_t*)goal;
    Serial.printf("Goal accepted: count to %d\n", g->target_count);
    return true;
}

esp_dds_action_state_t count_execute_callback(const void* goal, size_t goal_size, void* result, size_t* result_size, void* context) {
    count_goal_t* g = (count_goal_t*)goal;
    
    for (int i = 1; i <= g->target_count; i++) {
        // Check if cancelled
        if (ESP_DDS_IS_GOAL_CANCELED("/counter")) {
            Serial.println("Action cancelled!");
            count_result_t res = {i};
            memcpy(result, &res, sizeof(res));
            *result_size = sizeof(count_result_t);
            return ESP_DDS_ACTION_CANCELED;
        }
        
        // Send feedback
        count_feedback_t fb = {i};
        ESP_DDS_SEND_FEEDBACK("/counter", fb);
        
        Serial.printf("Counting: %d/%d\n", i, g->target_count);
        vTaskDelay(500 / portTICK_PERIOD_MS); // Count slowly
    }
    
    count_result_t res = {g->target_count};
    memcpy(result, &res, sizeof(res));
    *result_size = sizeof(count_result_t);
    return ESP_DDS_ACTION_SUCCEEDED;
}

void count_cancel_callback(void* context) {
    Serial.println("Cancel requested");
}

// Action callbacks
void action_feedback(const char* action, const void* feedback, size_t size, void* context) {
    count_feedback_t* fb = (count_feedback_t*)feedback;
    Serial.printf("Progress: %d\n", fb->current_count);
}

void action_result(const char* action, const void* result, size_t size, esp_dds_action_state_t state, void* context) {
    count_result_t* res = (count_result_t*)result;
    
    const char* status = "Unknown";
    switch (state) {
        case ESP_DDS_ACTION_SUCCEEDED: status = "SUCCEEDED"; break;
        case ESP_DDS_ACTION_CANCELED: status = "CANCELED"; break;
        case ESP_DDS_ACTION_ABORTED: status = "ABORTED"; break;
    }
    
    Serial.printf("Result: count=%d, status=%s\n", res->final_count, status);
}

// Client task
void client_task(void* param) {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    // Start counting to 10
    count_goal_t goal = {10};
    ESP_DDS_SEND_GOAL("/counter", goal, action_feedback, action_result, NULL, 10000);
    
    Serial.println("Started counting action!");
    
    // Let it run for a bit then cancel (optional)
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    ESP_DDS_CANCEL_GOAL("/counter", 1000);
    
    vTaskDelete(NULL); // End task
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    // Initialize DDS
    ESP_DDS_INIT();
    
    // Create counter action
    ESP_DDS_CREATE_ACTION("/counter", count_goal_callback, count_execute_callback, count_cancel_callback, NULL);
    
    // Start client
    xTaskCreate(client_task, "client", 2048, NULL, 1, NULL);
    
    Serial.println("✅ Actions Example Ready");
    Serial.println("🔢 Counting action started (will cancel after 3 seconds)...");
}

void loop() {
    // Process actions and pending results
    ESP_DDS_PROCESS_ACTIONS();
    ESP_DDS_PROCESS_PENDING(10);
    delay(10);
}