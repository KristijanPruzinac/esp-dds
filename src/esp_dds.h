#ifndef ESP_DDS_H
#define ESP_DDS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#else
// Minimal FreeRTOS stubs for platform independence
typedef void* TaskHandle_t;
typedef void* SemaphoreHandle_t;
typedef void* QueueHandle_t;
#define portMAX_DELAY 0xFFFFFFFF
#endif

// Configuration - completely static allocation
#define ESP_DDS_MAX_TOPICS 32
#define ESP_DDS_MAX_SERVICES 24  
#define ESP_DDS_MAX_ACTIONS 16
#define ESP_DDS_MAX_SUBSCRIBERS_PER_TOPIC 8
#define ESP_DDS_MAX_MESSAGE_SIZE 256
#define ESP_DDS_MAX_NAME_LENGTH 48
#define ESP_DDS_MIN_NAME_LENGTH 2

// Communication visibility
typedef enum {
    ESP_DDS_LOCAL_ONLY,
    ESP_DDS_NETWORK_VISIBLE
} esp_dds_visibility_t;

// Service modes
typedef enum {
    ESP_DDS_SYNC,      // Execute in CLIENT's thread (blocking)
    ESP_DDS_ASYNC,     // Execute in PROCESSOR thread (non-blocking)  
} esp_dds_service_mode_t;

// Action states (like ROS2)
typedef enum {
    ESP_DDS_ACTION_ACCEPTED,
    ESP_DDS_ACTION_EXECUTING,
    ESP_DDS_ACTION_SUCCEEDED,
    ESP_DDS_ACTION_CANCELED,
    ESP_DDS_ACTION_ABORTED
} esp_dds_action_state_t;

// Callback types
typedef void (*esp_dds_topic_cb_t)(const char* topic, const void* data, size_t size, void* context);
typedef bool (*esp_dds_service_cb_t)(const void* request, size_t req_size, void* response, size_t* resp_size, void* context);
typedef void (*esp_dds_async_cb_t)(const char* service, const void* response, size_t size, void* context);
typedef bool (*esp_dds_goal_cb_t)(const void* goal, size_t size, void* context);
typedef esp_dds_action_state_t (*esp_dds_execute_cb_t)(const void* goal, size_t goal_size, void* result, size_t* result_size, void* context);
typedef void (*esp_dds_cancel_cb_t)(void* context);
typedef void (*esp_dds_feedback_cb_t)(const char* action, const void* feedback, size_t size, void* context);
typedef void (*esp_dds_result_cb_t)(const char* action, const void* result, size_t size, esp_dds_action_state_t state, void* context);

// Core structures
typedef struct {
    char name[ESP_DDS_MAX_NAME_LENGTH];
    esp_dds_topic_cb_t callbacks[ESP_DDS_MAX_SUBSCRIBERS_PER_TOPIC];
    void* contexts[ESP_DDS_MAX_SUBSCRIBERS_PER_TOPIC];
    uint8_t subscriber_count;
    esp_dds_visibility_t visibility;
} esp_dds_topic_t;

typedef struct {
    char name[ESP_DDS_MAX_NAME_LENGTH];
    esp_dds_service_cb_t callback;
    esp_dds_service_mode_t mode;
    void* context;
    esp_dds_visibility_t visibility;
} esp_dds_service_t;

typedef struct {
    char name[ESP_DDS_MAX_NAME_LENGTH];
    esp_dds_goal_cb_t goal_callback;
    esp_dds_execute_cb_t execute_callback;
    esp_dds_cancel_cb_t cancel_callback;
    void* context;
    esp_dds_action_state_t state;
    bool active;
    bool cancel_requested;
    uint8_t goal_data[ESP_DDS_MAX_MESSAGE_SIZE];
    size_t goal_size;
    esp_dds_visibility_t visibility;
} esp_dds_action_t;

// Pending requests for async operations
typedef struct {
    char target_name[ESP_DDS_MAX_NAME_LENGTH];
    TaskHandle_t caller_task;
    union {
        esp_dds_async_cb_t async_cb;
        esp_dds_feedback_cb_t feedback_cb;
        esp_dds_result_cb_t result_cb;
    } callback;
    void* context;
    uint8_t response_data[ESP_DDS_MAX_MESSAGE_SIZE];
    size_t response_size;
    esp_dds_action_state_t action_state;
    bool response_ready;
    bool is_action;
} esp_dds_pending_t;

// Main DDS context
typedef struct {
    esp_dds_topic_t topics[ESP_DDS_MAX_TOPICS];
    esp_dds_service_t services[ESP_DDS_MAX_SERVICES];
    esp_dds_action_t actions[ESP_DDS_MAX_ACTIONS];
    esp_dds_pending_t pending[ESP_DDS_MAX_ACTIONS]; // Reuse for both services and actions
    
    uint8_t topic_count;
    uint8_t service_count;
    uint8_t action_count;
    uint8_t pending_count;
    
    SemaphoreHandle_t mutex;
    TaskHandle_t processor_task;
    bool running;
} esp_dds_context_t;

// ============================================================================
// PUBLIC API - ALL FUNCTIONS HAVE MACRO WRAPPERS FOR CONSISTENCY
// ============================================================================

// Core System API
void esp_dds_init(void);
void esp_dds_reset(void);

#define ESP_DDS_INIT() esp_dds_init()
#define ESP_DDS_RESET() esp_dds_reset()

// Topic API
bool esp_dds_publish(const char* topic, const void* data, size_t size);
bool esp_dds_subscribe(const char* topic, esp_dds_topic_cb_t callback, void* context);
void esp_dds_unsubscribe(const char* topic, esp_dds_topic_cb_t callback);

#define ESP_DDS_PUBLISH(topic, data) \
    esp_dds_publish(topic, &(data), sizeof(data))

#define ESP_DDS_SUBSCRIBE(topic, callback, context) \
    esp_dds_subscribe(topic, callback, context)

#define ESP_DDS_UNSUBSCRIBE(topic, callback) \
    esp_dds_unsubscribe(topic, callback)

// Service API  
bool esp_dds_create_service(const char* service, esp_dds_service_cb_t callback, 
                           esp_dds_service_mode_t mode, void* context);
bool esp_dds_call_service_sync(const char* service, const void* request, size_t req_size,
                              void* response, size_t* resp_size, uint32_t timeout_ms);
bool esp_dds_call_service_async(const char* service, const void* request, size_t req_size,
                               esp_dds_async_cb_t callback, void* context, uint32_t timeout_ms);

#define ESP_DDS_CREATE_SERVICE(service, callback, mode, context) \
    esp_dds_create_service(service, callback, mode, context)

#define ESP_DDS_CALL_SERVICE_SYNC(service, request, response, timeout) \
    do { \
        size_t resp_size_val = sizeof(response); \
        esp_dds_call_service_sync(service, &(request), sizeof(request), \
                                 (void*)&(response), &resp_size_val, timeout); \
    } while(0)

#define ESP_DDS_CALL_SERVICE_ASYNC(service, request, callback, context, timeout) \
    esp_dds_call_service_async(service, &(request), sizeof(request), callback, context, timeout)

// Action API
bool esp_dds_create_action(const char* action, esp_dds_goal_cb_t goal_cb,
                          esp_dds_execute_cb_t execute_cb, esp_dds_cancel_cb_t cancel_cb,
                          void* context);
bool esp_dds_send_goal(const char* action, const void* goal, size_t goal_size,
                      esp_dds_feedback_cb_t feedback_cb, esp_dds_result_cb_t result_cb,
                      void* context, uint32_t timeout_ms);
bool esp_dds_cancel_goal(const char* action, uint32_t timeout_ms);
bool esp_dds_send_feedback(const char* action, const void* feedback, size_t size);

#define ESP_DDS_CREATE_ACTION(action, goal_cb, execute_cb, cancel_cb, context) \
    esp_dds_create_action(action, goal_cb, execute_cb, cancel_cb, context)

#define ESP_DDS_SEND_GOAL(action, goal, feedback_cb, result_cb, context, timeout) \
    esp_dds_send_goal(action, &(goal), sizeof(goal), feedback_cb, result_cb, context, timeout)

#define ESP_DDS_CANCEL_GOAL(action, timeout) \
    esp_dds_cancel_goal(action, timeout)

#define ESP_DDS_SEND_FEEDBACK(action, feedback) \
    esp_dds_send_feedback(action, &(feedback), sizeof(feedback))

// Processing API (call this periodically from main loop)
void esp_dds_process_services(void);
void esp_dds_process_actions(void);
void esp_dds_process_pending(uint32_t timeout_ms);

#define ESP_DDS_PROCESS_SERVICES() esp_dds_process_services()
#define ESP_DDS_PROCESS_ACTIONS() esp_dds_process_actions()
#define ESP_DDS_PROCESS_PENDING(timeout) esp_dds_process_pending(timeout)

// Utility
bool esp_dds_is_goal_canceled(const char* action);

#define ESP_DDS_IS_GOAL_CANCELED(action) esp_dds_is_goal_canceled(action)

#endif // ESP_DDS_H