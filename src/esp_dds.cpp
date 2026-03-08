#include "esp_dds.h"

static dds_context_t dds_ctx;
static bool dds_initialized = false;

// Thread messages processing
void dds_process_thread_messages(dds_thread_context_t* context) {
    while(xQueueReceive(context->queue, &context->message, 0) == pdTRUE) {
        context->message.callback(&context->message);
    }
}

void dds_take_mutex(dds_thread_context_t* context) {
    xSemaphoreTake(context->sync_mutex, portMAX_DELAY);
}

void dds_give_mutex(dds_thread_context_t* context) {
    xSemaphoreGive(context->sync_mutex);
}

// Internal helper functions
const char* dds_result_to_string(dds_result_t result) {
    switch (result) {
        #define X(name) case name: return #name;
        DDS_RESULT_LIST
        #undef X
        default: return "DDS_UNKNOWN_ERROR";
    }
}

static bool dds_validate_name(const char* name) {
    if (!name || name[0] == '\0') {
        return false; // Null or empty
    }
    if (strlen(name) >= DDS_MAX_NAME_LENGTH) {
        return false; // Too long
    }
    if (strlen(name) < DDS_MIN_NAME_LENGTH) {
        return false; // Too short
    }
    if (name[0] != '/') {
        return false; // Must start with slash (ROS2 convention)
    }
    return true;
}

static bool find_empty_slot(uint8_t* count, uint8_t max, uint8_t* index) {
    for (uint8_t i = 0; i < max; i++) {
        if (*count < max) {
            *index = i;
            (*count)++;
            return true;
        }
    }
    return false;
}

static bool take_mutex(uint32_t timeout_ms) {
#ifdef ESP_PLATFORM
    if (!dds_initialized || dds_ctx.mutex == NULL) {
        return false;
    }

    return xSemaphoreTake(dds_ctx.mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
#else
    return true; // Stub for non-ESP platforms
#endif
}

static void give_mutex(void) {
#ifdef ESP_PLATFORM
    if (dds_ctx.mutex != NULL) {
        xSemaphoreGive(dds_ctx.mutex);
    }
#endif
}

static dds_topic_t* find_topic(const char* name) {
    for (uint8_t i = 0; i < dds_ctx.topic_count; i++) {
        if (strcmp(dds_ctx.topics[i].name, name) == 0) {
            return &dds_ctx.topics[i];
        }
    }
    return NULL;
}

static dds_sync_service_t* find_sync_service(const char* name) {
    for (uint8_t i = 0; i < dds_ctx.sync_service_count; i++) {
        if (strcmp(dds_ctx.sync_services[i].name, name) == 0) {
            return &dds_ctx.sync_services[i];
        }
    }
    return NULL;
}

static dds_async_service_t* find_async_service(const char* name) {
    for (uint8_t i = 0; i < dds_ctx.async_service_count; i++) {
        if (strcmp(dds_ctx.async_services[i].name, name) == 0) {
            return &dds_ctx.async_services[i];
        }
    }
    return NULL;
}

static dds_action_t* find_action(const char* name) {
    for (uint8_t i = 0; i < dds_ctx.action_count; i++) {
        if (strcmp(dds_ctx.actions[i].name, name) == 0) {
            return &dds_ctx.actions[i];
        }
    }
    return NULL;
}

// Public API implementation
void dds_init(void) {
    if (dds_initialized) return;

    memset(&dds_ctx, 0, sizeof(dds_ctx));
    
#ifdef ESP_PLATFORM
    dds_ctx.mutex = xSemaphoreCreateMutex();
#endif
    
    dds_ctx.running = true;
    dds_initialized = true;
}

void dds_reset(void) {
    if (!take_mutex(1000)) return;
    
    dds_ctx.running = false;
    
    // Clear all state
    memset(dds_ctx.topics, 0, sizeof(dds_ctx.topics));
    memset(dds_ctx.sync_services, 0, sizeof(dds_ctx.sync_services));
    memset(dds_ctx.async_services, 0, sizeof(dds_ctx.async_services));
    memset(dds_ctx.actions, 0, sizeof(dds_ctx.actions));
    
    dds_ctx.topic_count = 0;
    dds_ctx.sync_service_count = 0;
    dds_ctx.async_service_count = 0;
    dds_ctx.action_count = 0;
    
    dds_ctx.running = true;
    
    give_mutex();
}

//Async message implementation
dds_result_t dds_send_async_message(dds_callback_t callback,
                                    QueueHandle_t* queue,
                                    TaskHandle_t* task,
                                    const void* data,
                                    size_t data_size,
                                    dds_callback_t client_callback,
                                    QueueHandle_t* client_queue,
                                    TaskHandle_t* client_task) {
    if (!dds_initialized) {
        return DDS_ERROR_NOT_INITIALIZED;
    }

    if(!callback || !queue || !task) return DDS_ERROR_NULL_PTR;
    if(*queue == NULL) return DDS_ERROR_QUEUE_INVALID;
    if(data && data_size > DDS_DATA_SIZE) return DDS_ERROR_DATA_TOO_LARGE;

    dds_callback_context_t context;
    context.callback = callback;
    context.queue = queue;
    context.task = task;

    context.message_data.timestamp = esp_timer_get_time();
    memset(context.message_data.data, 0, DDS_DATA_SIZE);
    context.message_data.data_size = data_size;

    //Return callback info
    if(client_callback && client_queue && client_task) {
        context.client_callback = client_callback;
        context.client_queue = client_queue;
        context.client_task = client_task;
    }

    if(data && data_size > 0) {
        memcpy(context.message_data.data, data, data_size);
    }
    
    if(xQueueSend(*queue, &context, 0) != pdTRUE) {
        return DDS_ERROR_QUEUE_FULL;
    }

    xTaskNotify(*task, DDS_NOTIFY_BIT, eSetBits);
    
    return DDS_SUCCESS;
}

dds_result_t dds_send_async_message_no_data(dds_callback_t callback,
                                            QueueHandle_t* queue,
                                           TaskHandle_t* task) {
    return dds_send_async_message(callback, queue, task, NULL, 0);
}

// Topic implementation
dds_result_t dds_publish(const char* topic, const void* data, size_t size) {
    if (!dds_initialized) {
        return DDS_ERROR_NOT_INITIALIZED;
    }

    if (!dds_validate_name(topic)) return DDS_ERROR_INVALID_NAME;
    if (!topic || !data) return DDS_ERROR_NULL_PTR;
    if (size > DDS_DATA_SIZE) return DDS_ERROR_DATA_TOO_LARGE;
    if (!take_mutex(100)) return DDS_ERROR_MUTEX_TIMEOUT;
    
    dds_topic_t* t = find_topic(topic);
    if (!t) {
        // Auto-create topic on first publish
        if (dds_ctx.topic_count >= DDS_MAX_TOPICS) {
            give_mutex();
            return DDS_ERROR_MAX_TOPICS_REACHED;
        }
        t = &dds_ctx.topics[dds_ctx.topic_count];
        strncpy(t->name, topic, DDS_MAX_NAME_LENGTH - 1);
        t->subscriber_count = 0;
        dds_ctx.topic_count++;
    }

    dds_result_t result = DDS_SUCCESS;
    // Deliver to subscribers in subscriber thread
    for (uint8_t i = 0; i < t->subscriber_count; i++) {
        if (t->callback_contexts[i].callback){
            dds_result_t new_result;
            new_result = dds_send_async_message(
                t->callback_contexts[i].callback,
                t->callback_contexts[i].queue,
                t->callback_contexts[i].task,
                data,
                size
            );

            if (new_result != DDS_SUCCESS)
                result = new_result;
        }
    }
    
    give_mutex();
    return result;
}

dds_result_t dds_subscribe(const char* topic, dds_callback_t callback, dds_thread_context_t* thread_context) {
    if (!dds_initialized) {
        return DDS_ERROR_NOT_INITIALIZED;
    }

    if (!dds_validate_name(topic)) return DDS_ERROR_INVALID_NAME;
    if (!callback || !(&thread_context->queue) || !(&thread_context->task)) return DDS_ERROR_NULL_PTR;
    if (!take_mutex(100)) return DDS_ERROR_MUTEX_TIMEOUT;
    
    dds_topic_t* t = find_topic(topic);
    if (!t) {
        // Create topic if it doesn't exist
        if (dds_ctx.topic_count >= DDS_MAX_TOPICS) {
            give_mutex();
            return DDS_ERROR_MAX_TOPICS_REACHED;
        }
        t = &dds_ctx.topics[dds_ctx.topic_count];
        strncpy(t->name, topic, DDS_MAX_NAME_LENGTH - 1);
        dds_ctx.topic_count++;
    }
    
    if (t->subscriber_count >= DDS_MAX_SUBSCRIBERS_PER_TOPIC) {
        give_mutex();
        return DDS_ERROR_MAX_SUBSCRIBERS_REACHED;
    }
    
    t->callback_contexts[t->subscriber_count].callback = callback;
    t->callback_contexts[t->subscriber_count].queue = &thread_context->queue;
    t->callback_contexts[t->subscriber_count].task = &thread_context->task;
    t->subscriber_count++;
    
    give_mutex();
    return DDS_SUCCESS;
}

dds_result_t dds_unsubscribe(const char* topic, dds_callback_t callback) {
    if (!dds_initialized) {
        return DDS_ERROR_NOT_INITIALIZED;
    }

    if (!topic || !callback) return DDS_ERROR_NULL_PTR;
    if (!take_mutex(100)) return DDS_ERROR_MUTEX_TIMEOUT;
    
    dds_topic_t* t = find_topic(topic);
    if (t) {
        for (uint8_t i = 0; i < t->subscriber_count; i++) {
            if (t->callback_contexts[i].callback == callback) {
                for (uint8_t j = i; j < t->subscriber_count - 1; j++) {
                    t->callback_contexts[j] = t->callback_contexts[j + 1];
                }
                memset(&t->callback_contexts[t->subscriber_count - 1], 0, sizeof(dds_callback_context_t));
                t->subscriber_count--;
                break;
            }
        }
    }
    give_mutex();
    return DDS_SUCCESS;
}

// Service implementation
dds_result_t dds_create_service_sync(const char* service, dds_sync_callback_t callback, dds_thread_context_t* thread_context) {
    if (!dds_initialized) {
        return DDS_ERROR_NOT_INITIALIZED;
    }

    if (!dds_validate_name(service)) return DDS_ERROR_INVALID_NAME;
    if (!service || !callback) return DDS_ERROR_NULL_PTR;
    if (!take_mutex(100)) return DDS_ERROR_MUTEX_TIMEOUT;
    
    if (find_sync_service(service)) {
        give_mutex();
        return DDS_ERROR_SYNC_SERVICE_NOT_FOUND;
    }
    if (dds_ctx.sync_service_count >= DDS_MAX_SYNC_SERVICES){
        give_mutex();
        return DDS_ERROR_MAX_SYNC_SERVICES_REACHED;
    }
    
    dds_sync_service_t* s = &dds_ctx.sync_services[dds_ctx.sync_service_count];
    strncpy(s->name, service, DDS_MAX_NAME_LENGTH - 1);
    s->callback_context.callback = callback;
    s->callback_context.sync_mutex = &thread_context->sync_mutex;
    dds_ctx.sync_service_count++;
    
    give_mutex();
    return DDS_SUCCESS;
}

dds_result_t dds_create_service_async(const char* service, dds_callback_t callback, dds_thread_context_t* thread_context) {
    if (!dds_initialized) {
        return DDS_ERROR_NOT_INITIALIZED;
    }

    if (!dds_validate_name(service)) return DDS_ERROR_INVALID_NAME;
    if (!service || !callback) return DDS_ERROR_NULL_PTR;
    if (!take_mutex(100)) return DDS_ERROR_MUTEX_TIMEOUT;
    
    if (find_async_service(service)) {
        give_mutex();
        return DDS_ERROR_ASYNC_SERVICE_NOT_FOUND;
    }
    if (dds_ctx.async_service_count >= DDS_MAX_ASYNC_SERVICES){
        give_mutex();
        return DDS_ERROR_MAX_ASYNC_SERVICES_REACHED;
    }
    
    dds_async_service_t* s = &dds_ctx.async_services[dds_ctx.async_service_count];
    strncpy(s->name, service, DDS_MAX_NAME_LENGTH - 1);
    s->callback_context.callback = callback;
    s->callback_context.queue = &thread_context->queue;
    s->callback_context.task = &thread_context->task;
    dds_ctx.async_service_count++;
    
    give_mutex();
    return DDS_SUCCESS;
}

dds_result_t dds_call_service_sync(const char* service, const void* request_data, size_t size,
                              dds_service_response_t* response, uint32_t timeout_ms) {
    if (!dds_initialized) {
        return DDS_ERROR_NOT_INITIALIZED;
    }

    if (!take_mutex(100)) return DDS_ERROR_MUTEX_TIMEOUT;
    
    dds_sync_service_t* s = find_sync_service(service);
    dds_result_t result;

    dds_service_request_t request = {
        .timestamp = esp_timer_get_time(),
        .data_size = size,
        .timeout_ms = timeout_ms
    };
    memcpy(request.data, request_data, size);
    
    if (s) {
        if (s->callback_context.callback && s->callback_context.sync_mutex) {
            // Take service mutex if provided
            if (xSemaphoreTake(*(s->callback_context.sync_mutex), pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
                give_mutex();
                return DDS_ERROR_SYNC_SERVICE_TIMEOUT;
            }

            // Call service directly while mutex is held
            result = s->callback_context.callback(&request, response);

            // Release service mutex after callback
            xSemaphoreGive(*(s->callback_context.sync_mutex));
        }
        else {
            give_mutex();
            return DDS_ERROR_NULL_PTR;
        }
    }
    else {
        give_mutex();
        return DDS_ERROR_SYNC_SERVICE_NOT_FOUND;
    }
    
    give_mutex();
    return result;
}

dds_result_t dds_call_service_async(const char* service,
                                        dds_callback_t client_callback,
                                        dds_thread_context_t* client_thread_context,
                                        const void* data,
                                        size_t size) {
    if (!dds_initialized) {
        return DDS_ERROR_NOT_INITIALIZED;
    }

    if (!service || !client_callback || !(&client_thread_context->queue) || !(&client_thread_context->task)) return DDS_ERROR_NULL_PTR;
    if (!take_mutex(100)) return DDS_ERROR_MUTEX_TIMEOUT;
    
    dds_async_service_t* s = find_async_service(service);
    if (!s || !s->callback_context.queue || !s->callback_context.task) {
        give_mutex();
        return DDS_ERROR_QUEUE_INVALID;
    }

    dds_result_t result = dds_send_async_message(
        s->callback_context.callback,
        s->callback_context.queue,
        s->callback_context.task,
        data,
        size,
        client_callback,
        &client_thread_context->queue,
        &client_thread_context->task
    );
    
    give_mutex();
    return result;
}

// Action implementation
//TODO: IMPLEMENT ACTIONS
/*
bool dds_create_action(const char* action, dds_callback_t goal_cb,
                          dds_callback_t execute_cb, dds_callback_t cancel_cb,
                          void* context) {
    if (!dds_validate_name(action)) return false;
    if (!action || !goal_cb || !execute_cb) return false;
    if (!take_mutex(100)) return false;
    
    if (find_action(action) || dds_ctx.action_count >= DDS_MAX_ACTIONS) {
        give_mutex();
        return false;
    }
    
    dds_action_t* a = &dds_ctx.actions[dds_ctx.action_count];
    strncpy(a->name, action, DDS_MAX_NAME_LENGTH - 1);
    a->goal_callback_context.callback = goal_cb;
    a->execute_callback_context.callback = execute_cb;
    a->cancel_callback_context.callback = cancel_cb;
    a->goal_callback_context.context = context;
    a->execute_callback_context.context = context;
    a->cancel_callback_context.context = context;
    a->state = DDS_ACTION_ACCEPTED;
    a->active = false;
    a->cancel_requested = false;
    dds_ctx.action_count++;
    
    give_mutex();
    return true;
}

bool dds_send_goal(const char* action, const void* goal, size_t goal_size,
                      dds_callback_t feedback_cb, dds_callback_t result_cb) {
    if (!action || !goal || goal_size > DDS_DATA_SIZE) return false;
    if (!take_mutex(100)) return false;
    
    dds_action_t* a = find_action(action);
    if (!a || a->active || !a->goal_callback_context.callback) {
        give_mutex();
        return false;
    }
    
    // Create goal message
    dds_message_data_t goal_msg;
    goal_msg.timestamp = xTaskGetTickCount();
    memcpy(goal_msg.data, goal, goal_size);
    goal_msg.data_size = goal_size;
    
    // Check if goal is accepted via async callback
    dds_message_data_t response;
    bool accepted = false;
    
    // Execute goal callback to check acceptance
    if (a->goal_callback_context.callback) {
        a->goal_callback_context.callback(&goal_msg, a->goal_callback_context.context);
        // For simplicity, assume goal is accepted if callback exists
        // You might want to modify the callback to return bool
        accepted = true;
    }
    
    if (!accepted) {
        give_mutex();
        return false;
    }
    
    // Store goal info and activate action
    memcpy(&a->goal_callback_context.message_data, &goal_msg, sizeof(dds_message_data_t));
    a->active = true;
    a->state = DDS_ACTION_ACCEPTED;
    a->cancel_requested = false;
    
    // Store client callbacks for feedback/result
    a->goal_callback_context.callback = feedback_cb;  // Reuse goal context for feedback
    a->goal_callback_context.context = context;
    a->execute_callback_context.callback = result_cb; // Reuse execute context for result
    a->execute_callback_context.context = context;
    
    give_mutex();
    return true;
}

bool dds_cancel_goal(const char* action) {
    if (!take_mutex(100)) return false;
    
    dds_action_t* a = find_action(action);
    if (!a || !a->active) {
        give_mutex();
        return false;
    }
    
    a->cancel_requested = true;
    if (a->cancel_callback_context.callback) {
        // Send cancel notification via async callback
        dds_message_data_t cancel_msg;
        cancel_msg.timestamp = xTaskGetTickCount();
        cancel_msg.data_size = 0; // No data, just notification
        
        a->cancel_callback_context.callback(&cancel_msg, a->cancel_callback_context.context);
    }
    
    give_mutex();
    return true;
}

bool dds_send_feedback(const char* action, const void* feedback, size_t size) {
    if (!action || !feedback || size > DDS_DATA_SIZE) return false;
    if (!take_mutex(100)) return false;
    
    dds_action_t* a = find_action(action);
    if (!a || !a->active || !a->goal_callback_context.callback) {
        give_mutex();
        return false;
    }
    
    // Create feedback message
    dds_message_data_t feedback_msg;
    feedback_msg.timestamp = xTaskGetTickCount();
    memcpy(feedback_msg.data, feedback, size);
    feedback_msg.data_size = size;
    
    // Send feedback via async callback
    if (a->goal_callback_context.queue && a->goal_callback_context.task_handle) {
        dds_send_async_message(
            a->goal_callback_context.task_handle,
            a->goal_callback_context.callback,
            a->goal_callback_context.queue,
            &feedback_msg,
            sizeof(feedback_msg),
            a->goal_callback_context.context
        );
    } else {
        // Direct call if no async info available
        a->goal_callback_context.callback(&feedback_msg, a->goal_callback_context.context);
    }
    
    give_mutex();
    return true;
}

bool dds_is_goal_canceled(const char* action) {
    if (!take_mutex(10)) return false;
    
    dds_action_t* a = find_action(action);
    bool canceled = a ? a->cancel_requested : false;
    
    give_mutex();
    return canceled;
}
    */
