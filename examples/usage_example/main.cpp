#include <Arduino.h>
#include "esp_dds.h"
#include "esp_timer.h"

bool debug = true; // Affects time measurement prints, disable for accurate latency results

void test_topic(dds_callback_context_t* context) {
    if (debug) Serial.printf("\n\nTopic callback executed in task %p\n", xTaskGetCurrentTaskHandle());
    Serial.printf("Topic latency is %d us\n", esp_timer_get_time() - context->message_data.timestamp);
    if (debug) Serial.printf("%s\n", context->message_data.data);
}

void test_async_service_client(dds_callback_context_t* context){
    if (debug) Serial.printf("Async service RETURN callback executed in task %p\n", xTaskGetCurrentTaskHandle());

    if (debug) Serial.print("Received async response data: ");
    if (debug) Serial.printf("%s\n", context->message_data.data);
}

void test_async_service_server(dds_callback_context_t* context){
    if (debug) Serial.printf("\nAsync service callback executed in task %p\n", xTaskGetCurrentTaskHandle());
    Serial.printf("Async service latency is %d us\n", esp_timer_get_time() - context->message_data.timestamp);

    if (context->client_callback && context->client_queue && context->client_task) {
        DDS_SEND_ASYNC_MESSAGE(
            context->client_callback,
            context->client_queue,
            context->client_task,
            "Response from async service"
        );
    }
}

dds_result_t test_sync_service(dds_service_request_t* request, dds_service_response_t* response) {
    if (debug) Serial.printf("\nSync service callback executed in task %p\n", xTaskGetCurrentTaskHandle());
    Serial.printf("Sync service latency is %d us\n", esp_timer_get_time() - request->timestamp);

    if (debug) Serial.print("Received sync request data: ");
    if (debug) Serial.printf("%s\n", request->data);

    // Prepare response
    const char* resp_str = "Response from sync service";
    strcpy((char*)response->data, resp_str);
    response->data_size = strlen(resp_str) + 1;

    return DDS_SUCCESS;

}

static dds_thread_context_t thread_context;
void thread_timer_callback(void* arg) { xTaskNotify(thread_context.task, THREAD_NOTIFY_BIT, eSetBits); }
void thread_task(void* parameter) {
    Serial.printf("Thread task started with handle %p\n", xTaskGetCurrentTaskHandle());

    thread_context.task = xTaskGetCurrentTaskHandle();
    thread_context.queue = xQueueCreate(20, sizeof(dds_callback_context_t));
    thread_context.sync_mutex = xSemaphoreCreateMutex();
    
    esp_timer_create_args_t timer_args = {
        .callback = &thread_timer_callback,
        .arg = NULL,
    };
    esp_timer_create(&timer_args, &(thread_context.timer));
    esp_timer_start_periodic(thread_context.timer, 10000); // 10 ms

    // ------- THREAD SETUP CODE START -------

    dds_result_t result;
    result = DDS_SUBSCRIBE("/sensor/temperature", test_topic, &thread_context);
    if (result != DDS_SUCCESS) {
        Serial.printf("Topic subscribe failed: %s\n", DDS_RESULT_TO_STRING(result));
    }

    result = DDS_CREATE_SERVICE_SYNC("/test/sync", test_sync_service, &thread_context);
    if (result != DDS_SUCCESS) {
        Serial.printf("Sync service creation failed: %s\n", DDS_RESULT_TO_STRING(result));
    }

    result = DDS_CREATE_SERVICE_ASYNC("/test/async", test_async_service_server, &thread_context);
    if (result != DDS_SUCCESS) {
        Serial.printf("Async service creation failed: %s\n", DDS_RESULT_TO_STRING(result));
    }

    // ------- THREAD SETUP CODE END -------

    vTaskDelay(500);
    
    while(1) {
        // Wait for any notification (message or timer)
        uint32_t notification_value;
        xTaskNotifyWait(0x00, 0xFF, &notification_value, portMAX_DELAY);
        
        if (notification_value & DDS_NOTIFY_BIT) { // DDS message notification
            DDS_TAKE_MUTEX(&thread_context);
            DDS_PROCESS_THREAD_MESSAGES(&thread_context);
            DDS_GIVE_MUTEX(&thread_context);
        }
        if (notification_value & THREAD_NOTIFY_BIT) { // Timer tick notification
            DDS_TAKE_MUTEX(&thread_context);

            // ------- THREAD LOOP CODE START -------

            // ------- THREAD LOOP CODE END -------

            DDS_PROCESS_THREAD_MESSAGES(&thread_context);
            DDS_GIVE_MUTEX(&thread_context);
        }
    }
}

static dds_thread_context_t thread2_context;
void thread2_timer_callback(void* arg) {
    // Send notification for timer tick
    xTaskNotify(thread2_context.task, THREAD_NOTIFY_BIT, eSetBits);
}
void thread_task2(void* parameter) {
    Serial.printf("Thread 2 task started with handle %p\n", xTaskGetCurrentTaskHandle());
    thread2_context.task = xTaskGetCurrentTaskHandle();
    thread2_context.queue = xQueueCreate(20, sizeof(dds_callback_context_t));
    thread2_context.sync_mutex = xSemaphoreCreateMutex();
    
    esp_timer_create_args_t timer_args = {
        .callback = &thread2_timer_callback,
        .arg = NULL,
    };
    esp_timer_create(&timer_args, &thread2_context.timer);
    esp_timer_start_periodic(thread2_context.timer, 100000); // 100 ms

    // ------- THREAD SETUP CODE START -------

    // ------- THREAD SETUP CODE END -------

    vTaskDelay(500);
    
    while(1) {
        // Wait for any notification (message or timer)
        uint32_t notification_value;
        xTaskNotifyWait(0x00, 0xFF, &notification_value, portMAX_DELAY);
        
        if (notification_value & DDS_NOTIFY_BIT) { // DDS message notification
            DDS_TAKE_MUTEX(&thread2_context);
            DDS_PROCESS_THREAD_MESSAGES(&thread2_context);
            DDS_GIVE_MUTEX(&thread2_context);
        }
        if (notification_value & THREAD_NOTIFY_BIT) { // Timer tick notification
            DDS_TAKE_MUTEX(&thread2_context);

            // ------- THREAD LOOP CODE START -------
            dds_result_t result;

            result = DDS_PUBLISH("/sensor/temperature", "Hello topic");
            if (result != DDS_SUCCESS) {
                Serial.printf("Topic publish failed: %s\n", DDS_RESULT_TO_STRING(result));
            }

            dds_service_response_t response;
            result = DDS_CALL_SERVICE_SYNC("/test/sync", "Hello sync service", &response, 10);
            if (result != DDS_SUCCESS) {
                Serial.printf("Sync service call failed: %s\n", DDS_RESULT_TO_STRING(result));
            }
            else {
                if (debug) Serial.print("Received sync response data: ");
                if (debug) Serial.printf("%s\n", response.data);
            }

            result = DDS_CALL_SERVICE_ASYNC("/test/async", test_async_service_client, &thread2_context, "Hello async service");
            if (result != DDS_SUCCESS) {
                Serial.printf("Async service call failed: %s\n", DDS_RESULT_TO_STRING(result));
            }

            // ------- THREAD LOOP CODE END -------

            DDS_PROCESS_THREAD_MESSAGES(&thread2_context);
            DDS_GIVE_MUTEX(&thread2_context);
        }
    }
}


void setup() {
    Serial.begin(921600);
    vTaskDelay(2000);
    Serial.println("\n\n=== ESP-DDS System Starting ===\n");
    
    // Initialize DDS system
    DDS_INIT();

    Serial.printf("Main task started with handle %p\n", xTaskGetCurrentTaskHandle());

    //Create new task
    xTaskCreate(
        thread_task,          // Task function
        NULL,                 // Name of task
        16384,                // Stack size in words
        NULL,                 // Task input parameter
        1,                    // Priority of the task
        NULL);
    
        //Create new task
    xTaskCreate(
        thread_task2,         // Task function
        NULL,                 // Name of task
        16384,                // Stack size in words
        NULL,                 // Task input parameter
        1,                    // Priority of the task
        NULL);
    
    Serial.println("✅ System started");
}

void loop() {
    vTaskDelay(1000);                  
}
