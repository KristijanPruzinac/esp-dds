void setup() {
    Serial.begin(921600);

    vTaskDelay(500);
    
    // Initialize DDS system
    DDS_INIT();

    //Create new task
    xTaskCreate(
        thread_task,          // Task function
        NULL,                 // Name of task
        16384,                // Stack size in words
        NULL,                 // Task input parameter
        1,                    // Priority of the task
        NULL);
}

void loop() {
    vTaskDelay(1000);
}
