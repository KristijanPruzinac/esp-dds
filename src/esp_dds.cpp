#include "esp_dds.h"

static esp_dds_context_t dds_ctx;

// Internal helper functions
static bool esp_dds_validate_name(const char* name) {
    if (!name || name[0] == '\0') {
        return false; // Null or empty
    }
    if (strlen(name) >= ESP_DDS_MAX_NAME_LENGTH) {
        return false; // Too long
    }
    if (strlen(name) < ESP_DDS_MIN_NAME_LENGTH) {
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
    return xSemaphoreTake(dds_ctx.mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
#else
    return true; // Stub for non-ESP platforms
#endif
}

static void give_mutex(void) {
#ifdef ESP_PLATFORM
    xSemaphoreGive(dds_ctx.mutex);
#endif
}

static esp_dds_topic_t* find_topic(const char* name) {
    for (uint8_t i = 0; i < dds_ctx.topic_count; i++) {
        if (strcmp(dds_ctx.topics[i].name, name) == 0) {
            return &dds_ctx.topics[i];
        }
    }
    return NULL;
}

static esp_dds_service_t* find_service(const char* name) {
    for (uint8_t i = 0; i < dds_ctx.service_count; i++) {
        if (strcmp(dds_ctx.services[i].name, name) == 0) {
            return &dds_ctx.services[i];
        }
    }
    return NULL;
}

static esp_dds_action_t* find_action(const char* name) {
    for (uint8_t i = 0; i < dds_ctx.action_count; i++) {
        if (strcmp(dds_ctx.actions[i].name, name) == 0) {
            return &dds_ctx.actions[i];
        }
    }
    return NULL;
}

// Public API implementation
void esp_dds_init(void) {
    memset(&dds_ctx, 0, sizeof(dds_ctx));
    
#ifdef ESP_PLATFORM
    dds_ctx.mutex = xSemaphoreCreateMutex();
#endif
    
    dds_ctx.running = true;
}

void esp_dds_reset(void) {
    if (!take_mutex(1000)) return;
    
    dds_ctx.running = false;
    
    // Clear all state
    memset(dds_ctx.topics, 0, sizeof(dds_ctx.topics));
    memset(dds_ctx.services, 0, sizeof(dds_ctx.services));
    memset(dds_ctx.actions, 0, sizeof(dds_ctx.actions));
    memset(dds_ctx.pending, 0, sizeof(dds_ctx.pending));
    
    dds_ctx.topic_count = 0;
    dds_ctx.service_count = 0;
    dds_ctx.action_count = 0;
    dds_ctx.pending_count = 0;
    
    dds_ctx.running = true;
    
    give_mutex();
}

// Topic implementation
bool esp_dds_publish(const char* topic, const void* data, size_t size) {
    if (!esp_dds_validate_name(topic)) return false;
    if (!topic || !data || size > ESP_DDS_MAX_MESSAGE_SIZE) return false;
    if (!take_mutex(100)) return false;
    
    esp_dds_topic_t* t = find_topic(topic);
    if (!t) {
        // Auto-create topic on first publish
        if (dds_ctx.topic_count >= ESP_DDS_MAX_TOPICS) {
            give_mutex();
            return false;
        }
        t = &dds_ctx.topics[dds_ctx.topic_count];
        strncpy(t->name, topic, ESP_DDS_MAX_NAME_LENGTH - 1);
        t->subscriber_count = 0;
        dds_ctx.topic_count++;
    }
    
    // Deliver to subscribers immediately (in publisher's thread)
    for (uint8_t i = 0; i < t->subscriber_count; i++) {
        if (t->callbacks[i]) {
            t->callbacks[i](topic, data, size, t->contexts[i]);
        }
    }
    
    give_mutex();
    return true;
}

bool esp_dds_subscribe(const char* topic, esp_dds_topic_cb_t callback, void* context) {
    if (!esp_dds_validate_name(topic)) return false;
    if (!topic || !callback) return false;
    if (!take_mutex(100)) return false;
    
    esp_dds_topic_t* t = find_topic(topic);
    if (!t) {
        // Create topic if it doesn't exist
        if (dds_ctx.topic_count >= ESP_DDS_MAX_TOPICS) {
            give_mutex();
            return false;
        }
        t = &dds_ctx.topics[dds_ctx.topic_count];
        strncpy(t->name, topic, ESP_DDS_MAX_NAME_LENGTH - 1);
        dds_ctx.topic_count++;
    }
    
    if (t->subscriber_count >= ESP_DDS_MAX_SUBSCRIBERS_PER_TOPIC) {
        give_mutex();
        return false;
    }
    
    t->callbacks[t->subscriber_count] = callback;
    t->contexts[t->subscriber_count] = context;
    t->subscriber_count++;
    
    give_mutex();
    return true;
}

void esp_dds_unsubscribe(const char* topic, esp_dds_topic_cb_t callback) {
    if (!take_mutex(100)) return;
    
    esp_dds_topic_t* t = find_topic(topic);
    if (t) {
        for (uint8_t i = 0; i < t->subscriber_count; i++) {
            if (t->callbacks[i] == callback) {
                // Shift remaining subscribers
                for (uint8_t j = i; j < t->subscriber_count - 1; j++) {
                    t->callbacks[j] = t->callbacks[j + 1];
                    t->contexts[j] = t->contexts[j + 1];
                }
                t->subscriber_count--;
                break;
            }
        }
    }
    
    give_mutex();
}

// Service implementation
bool esp_dds_create_service(const char* service, esp_dds_service_cb_t callback,
                           esp_dds_service_mode_t mode, void* context) {
    if (!esp_dds_validate_name(service)) return false;
    if (!service || !callback) return false;
    if (!take_mutex(100)) return false;
    
    if (find_service(service) || dds_ctx.service_count >= ESP_DDS_MAX_SERVICES) {
        give_mutex();
        return false;
    }
    
    esp_dds_service_t* s = &dds_ctx.services[dds_ctx.service_count];
    strncpy(s->name, service, ESP_DDS_MAX_NAME_LENGTH - 1);
    s->callback = callback;
    s->mode = mode;
    s->context = context;
    dds_ctx.service_count++;
    
    give_mutex();
    return true;
}

bool esp_dds_call_service_sync(const char* service, const void* request, size_t req_size,
                              void* response, size_t* resp_size, uint32_t timeout_ms) {
    if (!service || !request || !response || !resp_size || req_size > ESP_DDS_MAX_MESSAGE_SIZE) {
        return false;
    }
    
    if (!take_mutex(100)) {
        return false;
    }
    
    esp_dds_service_t* s = find_service(service);
    
    if (!s || !s->callback) {
        give_mutex();
        return false;
    }
    
    // Cache the callback and context while we have the mutex
    esp_dds_service_cb_t callback = s->callback;
    void* context = s->context;
    
    // Release mutex before calling callback (callback might take time)
    give_mutex();
    
    // Verify callback is still valid (extra safety check)
    if (!callback) {
        return false;
    }
    
    // Execute callback in caller's thread
    bool result = callback(request, req_size, response, resp_size, context);
    
    return result;
}

bool esp_dds_call_service_async(const char* service, const void* request, size_t req_size,
                               esp_dds_async_cb_t callback, void* context, uint32_t timeout_ms) {
    if (!service || !request || !callback || req_size > ESP_DDS_MAX_MESSAGE_SIZE) return false;
    if (!take_mutex(100)) return false;
    
    esp_dds_service_t* s = find_service(service);
    if (!s || !s->callback || dds_ctx.pending_count >= ESP_DDS_MAX_ACTIONS) {
        give_mutex();
        return false;
    }
    
    // Store pending request
    esp_dds_pending_t* pending = &dds_ctx.pending[dds_ctx.pending_count];
    strncpy(pending->target_name, service, ESP_DDS_MAX_NAME_LENGTH - 1);
    pending->caller_task = xTaskGetCurrentTaskHandle();
    pending->callback.async_cb = callback;
    pending->context = context;
    pending->is_action = false;
    pending->response_ready = false;
    
    // Execute service immediately (in current thread)
    uint8_t temp_response[ESP_DDS_MAX_MESSAGE_SIZE];
    size_t temp_size = sizeof(temp_response);
    bool success = s->callback(request, req_size, temp_response, &temp_size, s->context);
    
    if (success) {
        memcpy(pending->response_data, temp_response, temp_size);
        pending->response_size = temp_size;
        pending->response_ready = true;
        dds_ctx.pending_count++;
    }
    
    give_mutex();
    return success;
}

// Action implementation
bool esp_dds_create_action(const char* action, esp_dds_goal_cb_t goal_cb,
                          esp_dds_execute_cb_t execute_cb, esp_dds_cancel_cb_t cancel_cb,
                          void* context) {
    if (!esp_dds_validate_name(action)) return false;
    if (!action || !goal_cb || !execute_cb) return false;
    if (!take_mutex(100)) return false;
    
    if (find_action(action) || dds_ctx.action_count >= ESP_DDS_MAX_ACTIONS) {
        give_mutex();
        return false;
    }
    
    esp_dds_action_t* a = &dds_ctx.actions[dds_ctx.action_count];
    strncpy(a->name, action, ESP_DDS_MAX_NAME_LENGTH - 1);
    a->goal_callback = goal_cb;
    a->execute_callback = execute_cb;
    a->cancel_callback = cancel_cb;
    a->context = context;
    a->state = ESP_DDS_ACTION_ACCEPTED;
    a->active = false;
    a->cancel_requested = false;
    dds_ctx.action_count++;
    
    give_mutex();
    return true;
}

bool esp_dds_send_goal(const char* action, const void* goal, size_t goal_size,
                      esp_dds_feedback_cb_t feedback_cb, esp_dds_result_cb_t result_cb,
                      void* context, uint32_t timeout_ms) {
    if (!action || !goal || goal_size > ESP_DDS_MAX_MESSAGE_SIZE) return false;
    if (!take_mutex(100)) return false;
    
    esp_dds_action_t* a = find_action(action);
    if (!a || a->active || !a->goal_callback) {
        give_mutex();
        return false;
    }
    
    // Check if goal is accepted
    if (!a->goal_callback(goal, goal_size, a->context)) {
        give_mutex();
        return false;
    }
    
    // Store goal and client info
    memcpy(a->goal_data, goal, goal_size);
    a->goal_size = goal_size;
    a->active = true;
    a->state = ESP_DDS_ACTION_ACCEPTED;
    a->cancel_requested = false;
    
    // Create pending result tracker
    if (dds_ctx.pending_count < ESP_DDS_MAX_ACTIONS) {
        esp_dds_pending_t* pending = &dds_ctx.pending[dds_ctx.pending_count];
        strncpy(pending->target_name, action, ESP_DDS_MAX_NAME_LENGTH - 1);
        pending->caller_task = xTaskGetCurrentTaskHandle();
        pending->callback.result_cb = result_cb;
        pending->context = context;
        pending->is_action = true;
        pending->response_ready = false;
        dds_ctx.pending_count++;
    }
    
    give_mutex();
    return true;
}

bool esp_dds_cancel_goal(const char* action, uint32_t timeout_ms) {
    if (!take_mutex(100)) return false;
    
    esp_dds_action_t* a = find_action(action);
    if (!a || !a->active) {
        give_mutex();
        return false;
    }
    
    a->cancel_requested = true;
    if (a->cancel_callback) {
        a->cancel_callback(a->context);
    }
    
    give_mutex();
    return true;
}

bool esp_dds_send_feedback(const char* action, const void* feedback, size_t size) {
    if (!action || !feedback || size > ESP_DDS_MAX_MESSAGE_SIZE) return false;
    if (!take_mutex(100)) return false;
    
    // Find pending action and deliver feedback
    for (uint8_t i = 0; i < dds_ctx.pending_count; i++) {
        esp_dds_pending_t* p = &dds_ctx.pending[i];
        if (p->is_action && strcmp(p->target_name, action) == 0 && p->callback.feedback_cb) {
            // Execute feedback callback in caller's thread context
            p->callback.feedback_cb(action, feedback, size, p->context);
            break;
        }
    }
    
    give_mutex();
    return true;
}

// Processing functions
void esp_dds_process_services(void) {
    // Services are processed immediately in caller's thread
    // No background processing needed for this simple design
}

void esp_dds_process_actions(void) {
    if (!take_mutex(10)) return;
    
    for (uint8_t i = 0; i < dds_ctx.action_count; i++) {
        esp_dds_action_t* a = &dds_ctx.actions[i];
        
        if (a->active && (a->state == ESP_DDS_ACTION_ACCEPTED || a->state == ESP_DDS_ACTION_EXECUTING)) {
            // Execute action (in processor thread)
            uint8_t result[ESP_DDS_MAX_MESSAGE_SIZE];
            size_t result_size = sizeof(result);
            
            esp_dds_action_state_t state = a->execute_callback(
                a->goal_data, a->goal_size, result, &result_size, a->context);
            
            a->state = state;
            
            // Only mark as inactive if it reached a final state
            if (state != ESP_DDS_ACTION_EXECUTING) {
                a->active = false;
                
                // Store result for delivery to client
                for (uint8_t j = 0; j < dds_ctx.pending_count; j++) {
                    esp_dds_pending_t* p = &dds_ctx.pending[j];
                    if (p->is_action && strcmp(p->target_name, a->name) == 0) {
                        memcpy(p->response_data, result, result_size);
                        p->response_size = result_size;
                        p->action_state = state;
                        p->response_ready = true;
                        break;
                    }
                }
            }
        }
    }
    
    give_mutex();
}

void esp_dds_process_pending(uint32_t timeout_ms) {
    if (!take_mutex(10)) return;
    
    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    
    for (uint8_t i = 0; i < dds_ctx.pending_count; i++) {
        esp_dds_pending_t* p = &dds_ctx.pending[i];
        
        if (p->response_ready && p->caller_task == current_task) {
            // Execute callback in caller's thread context
            if (p->is_action && p->callback.result_cb) {
                p->callback.result_cb(p->target_name, p->response_data, 
                                    p->response_size, p->action_state, p->context);
            } else if (!p->is_action && p->callback.async_cb) {
                p->callback.async_cb(p->target_name, p->response_data,
                                   p->response_size, p->context);
            }
            
            // Remove from pending list
            for (uint8_t j = i; j < dds_ctx.pending_count - 1; j++) {
                dds_ctx.pending[j] = dds_ctx.pending[j + 1];
            }
            dds_ctx.pending_count--;
            i--; // Recheck current position
        }
    }
    
    give_mutex();
}

bool esp_dds_is_goal_canceled(const char* action) {
    if (!take_mutex(10)) return false;
    
    esp_dds_action_t* a = find_action(action);
    bool canceled = a ? a->cancel_requested : false;
    
    give_mutex();
    return canceled;
}