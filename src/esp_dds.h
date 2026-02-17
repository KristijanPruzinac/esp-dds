#ifndef ESP_DDS_H
#define ESP_DDS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <atomic>

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
#ifndef DDS_MAX_TOPICS
#define DDS_MAX_TOPICS 8
#endif

#ifndef DDS_MAX_SUBSCRIBERS_PER_TOPIC
#define DDS_MAX_SUBSCRIBERS_PER_TOPIC 4
#endif

#ifndef DDS_MAX_SYNC_SERVICES
#define DDS_MAX_SYNC_SERVICES 4
#endif

#ifndef DDS_MAX_ASYNC_SERVICES
#define DDS_MAX_ASYNC_SERVICES 4
#endif

#ifndef DDS_MAX_ACTIONS
#define DDS_MAX_ACTIONS 4
#endif

#ifndef DDS_MAX_NAME_LENGTH
#define DDS_MAX_NAME_LENGTH 48
#endif

#ifndef DDS_MIN_NAME_LENGTH
#define DDS_MIN_NAME_LENGTH 2
#endif


// ============================================================================
// ASYNC CALLBACK FUNCTIONALITY

#ifndef DDS_DATA_SIZE
#define DDS_DATA_SIZE 64
#endif

#ifndef DDS_TASK_QUEUE_SIZE
#define DDS_TASK_QUEUE_SIZE 32
#endif

#define DDS_TASK_QUEUE_TIMEOUT_MS 100

#define DDS_NOTIFY_BIT (1 << 1) // DDS System notifies with bit 1
#define THREAD_NOTIFY_BIT (1 << 0) // Timer notifies with bit 0

// ----- Error Codes -----

#define DDS_RESULT_LIST \
    X(DDS_SUCCESS) \
    X(DDS_ERROR_INVALID_NAME) \
    X(DDS_ERROR_MUTEX_TIMEOUT) \
    X(DDS_ERROR_NULL_PTR) \
    X(DDS_ERROR_QUEUE_FULL) \
    X(DDS_ERROR_DATA_TOO_LARGE) \
    X(DDS_ERROR_QUEUE_INVALID) \
    X(DDS_ERROR_SYNC_SERVICE_NOT_FOUND) \
    X(DDS_ERROR_ASYNC_SERVICE_NOT_FOUND) \
    X(DDS_ERROR_SYNC_SERVICE_TIMEOUT) \
    X(DDS_ERROR_MAX_TOPICS_REACHED) \
    X(DDS_ERROR_MAX_SUBSCRIBERS_REACHED) \
    X(DDS_ERROR_MAX_SYNC_SERVICES_REACHED) \
    X(DDS_ERROR_MAX_ASYNC_SERVICES_REACHED)

// Generate the enum
typedef enum {
    #define X(name) name,
    DDS_RESULT_LIST
    #undef X
} dds_result_t;

// Generate string conversion
const char* dds_result_to_string(dds_result_t result);

// ----- Message data (without callback) -----
typedef struct {
    int64_t timestamp; // Microsecond timestamp
    void* data[DDS_DATA_SIZE];
    size_t data_size;
} dds_message_data_t;

// Request structure
typedef struct {
    int64_t timestamp;
    void* data[DDS_DATA_SIZE];
    size_t data_size;
    uint32_t timeout_ms;
} dds_service_request_t;

// Response structure  
typedef struct {
    int64_t timestamp;
    void* data[DDS_DATA_SIZE];
    size_t data_size;
} dds_service_response_t;

struct dds_callback_context_s; // Forward declaration

// ----- Callback type -----
typedef void (*dds_callback_t)(struct dds_callback_context_s* context);
typedef dds_result_t (*dds_sync_callback_t)(dds_service_request_t* request, dds_service_response_t* response);

// ----- Full message (with callback, for queue) -----
typedef struct dds_callback_context_s {
    dds_callback_t callback;
    QueueHandle_t* queue;
    TaskHandle_t* task;

    dds_callback_t client_callback;
    QueueHandle_t* client_queue;
    TaskHandle_t* client_task;

    dds_message_data_t message_data;
} dds_callback_context_t;

typedef struct {
    dds_sync_callback_t callback;
    SemaphoreHandle_t* sync_mutex;
} dds_sync_callback_context_t;

typedef struct {
    TaskHandle_t task;

    QueueHandle_t queue;
    dds_callback_context_t message;

    SemaphoreHandle_t sync_mutex;

    esp_timer_handle_t timer;
} dds_thread_context_t;

// ----- Thread messages processing -----
void dds_process_thread_messages(dds_thread_context_t* context);
void dds_take_mutex(dds_thread_context_t* context);
void dds_give_mutex(dds_thread_context_t* context);

// ----- Function declarations -----
dds_result_t dds_send_async_message(dds_callback_t callback,
                                    QueueHandle_t* queue,
                                    TaskHandle_t* task,
                                    const void* data, 
                                    size_t data_size,
                                    dds_callback_t client_callback = NULL,
                                    QueueHandle_t* client_queue = NULL,
                                    TaskHandle_t* client_task = NULL);

dds_result_t dds_send_async_message_no_data(dds_callback_t callback,
                                           QueueHandle_t* queue,
                                           TaskHandle_t* task);

// ============================================================================

// Communication visibility
typedef enum {
    DDS_LOCAL_ONLY,
    DDS_NETWORK_VISIBLE
} dds_visibility_t;

// Action states (like ROS2)
typedef enum {
    DDS_ACTION_ACCEPTED,
    DDS_ACTION_EXECUTING,
    DDS_ACTION_SUCCEEDED,
    DDS_ACTION_CANCELED,
    DDS_ACTION_ABORTED
} dds_action_state_t;

// Core structures
typedef struct {
    char name[DDS_MAX_NAME_LENGTH];
    dds_callback_context_t callback_contexts[DDS_MAX_SUBSCRIBERS_PER_TOPIC];
    uint8_t subscriber_count;
    dds_visibility_t visibility;
} dds_topic_t;

typedef struct {
    char name[DDS_MAX_NAME_LENGTH];
    dds_sync_callback_context_t callback_context;
    dds_visibility_t visibility;
} dds_sync_service_t;

typedef struct {
    char name[DDS_MAX_NAME_LENGTH];
    dds_callback_context_t callback_context;
    dds_visibility_t visibility;
} dds_async_service_t;

typedef struct {
    char name[DDS_MAX_NAME_LENGTH];
    dds_callback_context_t goal_callback_context;
    dds_callback_context_t execute_callback_context;
    dds_callback_context_t cancel_callback_context;
    dds_action_state_t state;
    bool active;
    bool cancel_requested;
    dds_visibility_t visibility;
} dds_action_t;

// Main DDS context
typedef struct {
    dds_topic_t topics[DDS_MAX_TOPICS];
    dds_sync_service_t sync_services[DDS_MAX_SYNC_SERVICES];
    dds_async_service_t async_services[DDS_MAX_ASYNC_SERVICES];
    dds_action_t actions[DDS_MAX_ACTIONS];
    
    uint8_t topic_count;
    uint8_t sync_service_count;
    uint8_t async_service_count;
    uint8_t action_count;
    
    SemaphoreHandle_t mutex;
    TaskHandle_t processor_task;
    bool running;
} dds_context_t;

// ============================================================================
// PUBLIC API - ALL FUNCTIONS HAVE MACRO WRAPPERS FOR CONSISTENCY
// ============================================================================

// Core System API
void dds_init(void);
void dds_reset(void);

#define DDS_INIT() dds_init()
#define DDS_RESET() dds_reset()

#define DDS_RESULT_TO_STRING(result) \
    dds_result_to_string(result)

// Thread messages processing
#define DDS_PROCESS_THREAD_MESSAGES(context) \
    dds_process_thread_messages(context)

#define DDS_TAKE_MUTEX(context) \
    dds_take_mutex(context)

#define DDS_GIVE_MUTEX(context) \
    dds_give_mutex(context)

// Topic API
dds_result_t dds_publish(const char* topic, const void* data, size_t size);
dds_result_t dds_subscribe(const char* topic, dds_callback_t callback, dds_thread_context_t* thread_context);
dds_result_t dds_unsubscribe(const char* topic, dds_callback_t callback);

#define DDS_PUBLISH(topic, data) \
    dds_publish(topic, &(data), sizeof(data))

#define DDS_SUBSCRIBE(topic, callback, thread_context) \
    dds_subscribe(topic, callback, thread_context)

#define DDS_UNSUBSCRIBE(topic, callback) \
    dds_unsubscribe(topic, callback)

// Service API  
dds_result_t dds_create_service_sync(const char* service, 
                           dds_sync_callback_t callback, dds_thread_context_t* thread_context);

dds_result_t dds_create_service_async(const char* service, 
                           dds_callback_t callback, dds_thread_context_t* thread_context);

dds_result_t dds_call_service_sync(const char* service, const void* request_data, size_t size,
                            dds_service_response_t* response, uint32_t timeout_ms);

dds_result_t dds_call_service_async(const char* service,
                                        dds_callback_t client_callback,
                                        dds_thread_context_t* client_thread_context,
                                        const void* data,
                                        size_t size);

#define DDS_CREATE_SERVICE_SYNC(service, callback, thread_context) \
    dds_create_service_sync(service, callback, thread_context)

#define DDS_CREATE_SERVICE_ASYNC(service, callback, thread_context) \
    dds_create_service_async(service, callback, thread_context)

#define DDS_CALL_SERVICE_SYNC(service, request_data, response, timeout_ms) \
    dds_call_service_sync(service, &(request_data), sizeof(request_data), response, timeout_ms)

#define DDS_CALL_SERVICE_ASYNC(service, client_callback, client_thread_context, data) \
    dds_call_service_async(service, client_callback, client_thread_context, &(data), sizeof(data))

// Async message API
#define DDS_SEND_ASYNC_MESSAGE(callback, queue, task, data) \
    dds_send_async_message(callback, queue, task, &(data), sizeof(data))

#define DDS_SEND_ASYNC_MESSAGE_WITH_RETURN(callback, queue, task, data, client_callback, client_queue, client_task) \
    dds_send_async_message(callback, queue, task, &(data), sizeof(data), client_callback, client_queue, client_task)

// Action API
/*
bool dds_create_action(const char* action, dds_callback_t goal_cb,
                          dds_callback_t execute_cb, dds_callback_t cancel_cb,
                          void* context);
bool dds_send_goal(const char* action, const void* goal, size_t goal_size,
                      dds_callback_t feedback_cb, dds_callback_t result_cb,
                      void* context);
bool dds_cancel_goal(const char* action);
bool dds_send_feedback(const char* action, const void* feedback, size_t size);

#define DDS_CREATE_ACTION(action, goal_cb, execute_cb, cancel_cb, context) \
    dds_create_action(action, goal_cb, execute_cb, cancel_cb, context)

#define DDS_SEND_GOAL(action, goal, feedback_cb, result_cb, context) \
    dds_send_goal(action, &(goal), sizeof(goal), feedback_cb, result_cb, context)

#define DDS_CANCEL_GOAL(action) \
    dds_cancel_goal(action)

#define DDS_SEND_FEEDBACK(action, feedback) \
    dds_send_feedback(action, &(feedback), sizeof(feedback))

// Utility
bool dds_is_goal_canceled(const char* action);

#define DDS_IS_GOAL_CANCELED(action) dds_is_goal_canceled(action)
*/

#endif
