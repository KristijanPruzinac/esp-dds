#include <Arduino.h>
#include <esp_dds.h>

typedef struct {
    int target_count;
} count_goal_t;

typedef struct {
    int current_count;
} count_feedback_t;

typedef struct {
    int final_count;
} count_result_t;

typedef struct {
    int current_count;
    int target_count;
} count_context_t;

bool count_goal_callback(const void* goal, size_t size, void* context) {
    count_goal_t* g = (count_goal_t*)goal;
    count_context_t* ctx = (count_context_t*)context;
    
    ctx->current_count = 0;
    ctx->target_count = g->target_count;
    
    Serial.printf("Goal accepted: count to %d\n", g->target_count);
    return true;
}

esp_dds_action_state_t count_execute_callback(const void* goal, size_t goal_size, void* result, size_t* result_size, void* context) {
    count_context_t* ctx = (count_context_t*)context;
    
    if (ESP_DDS_IS_GOAL_CANCELED("/counter")) {
        Serial.println("Action cancelled!");
        count_result_t res = {ctx->current_count};
        memcpy(result, &res, sizeof(res));
        *result_size = sizeof(count_result_t);
        return ESP_DDS_ACTION_CANCELED;
    }
    
    if (ctx->current_count < ctx->target_count) {
        ctx->current_count++;
        
        count_feedback_t fb = {ctx->current_count};
        ESP_DDS_SEND_FEEDBACK("/counter", fb);
        
        Serial.printf("Counting: %d/%d\n", ctx->current_count, ctx->target_count);
        
        delay(500);
        
        if (ctx->current_count < ctx->target_count) {
            return ESP_DDS_ACTION_EXECUTING;
        }
    }
    
    count_result_t res = {ctx->target_count};
    memcpy(result, &res, sizeof(res));
    *result_size = sizeof(count_result_t);
    Serial.println("Counting completed!");
    return ESP_DDS_ACTION_SUCCEEDED;
}

void count_cancel_callback(void* context) {
    Serial.println("Cancel callback EXECUTED");
}

void action_feedback(const char* action, const void* feedback, size_t size, void* context) {
    count_feedback_t* fb = (count_feedback_t*)feedback;
    Serial.printf("Progress: %d\n", fb->current_count);
}

void action_result(const char* action, const void* result, size_t size, esp_dds_action_state_t state, void* context) {
    count_result_t* res = (count_result_t*)result;
    
    const char* status = "Unknown";
    switch (state) {
        case ESP_DDS_ACTION_ACCEPTED: status = "ACCEPTED"; break;
        case ESP_DDS_ACTION_EXECUTING: status = "EXECUTING"; break;
        case ESP_DDS_ACTION_SUCCEEDED: status = "SUCCEEDED"; break;
        case ESP_DDS_ACTION_CANCELED: status = "CANCELED"; break;
        case ESP_DDS_ACTION_ABORTED: status = "ABORTED"; break;
    }
    
    Serial.printf("Result: count=%d, status=%s\n", res->final_count, status);
}

void client_task(void* param) {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    count_goal_t goal = {10};
    ESP_DDS_SEND_GOAL("/counter", goal, action_feedback, action_result, NULL, 10000);
    
    Serial.println("Started counting action!");
    
    unsigned long start = millis();
    while (millis() - start < 3000) {
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
    
    Serial.println("Attempting to cancel...");
    bool cancel_result = ESP_DDS_CANCEL_GOAL("/counter", 1000);
    Serial.printf("Cancel request result: %s\n", cancel_result ? "SUCCESS" : "FAILED");
    
    vTaskDelete(NULL);
}

count_context_t action_ctx;

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    ESP_DDS_INIT();
    ESP_DDS_CREATE_ACTION("/counter", count_goal_callback, count_execute_callback, count_cancel_callback, &action_ctx);
    
    xTaskCreate(client_task, "client", 2048, NULL, 1, NULL);
    
    Serial.println("Actions Example Ready");
    Serial.println("Counting action started (will cancel after 3 seconds)...");
}

void loop() {
    ESP_DDS_PROCESS_ACTIONS();
    ESP_DDS_PROCESS_PENDING(10);
    delay(100);
}