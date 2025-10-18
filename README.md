# ESP-DDS

A lightweight DDS-like communication framework for ESP32 with FreeRTOS support, providing ROS2-style communication patterns (Topics, Services, Actions) for embedded systems.

## Features

- **Topics**: Publish/Subscribe pattern with multiple subscribers
- **Services**: Request/Response pattern with sync/async modes  
- **Actions**: Long-running operations with feedback and cancellation
- **Thread-Safe**: Built-in mutex protection for concurrent access
- **Static Allocation**: No dynamic memory allocation
- **Platform Independent**: Works with Arduino & ESP-IDF frameworks

## Installation

### PlatformIO
Add to your `platformio.ini`:
```ini
lib_deps = 
    KristijanPruzinac/ESP-DDS@^1.0.0
```

## Quick start
```cpp
#include <Arduino.h>
#include <esp_dds.h>

void message_received(const char* topic, const void* data, size_t size, void* context) {
    int number = *(int*)data;
    Serial.printf("Received: %d\n", number);
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    ESP_DDS_INIT();
    ESP_DDS_SUBSCRIBE("/numbers", message_received, NULL);
    
    int data = 42;
    ESP_DDS_PUBLISH("/numbers", data);
}

void loop() {
    ESP_DDS_PROCESS_ACTIONS();
    ESP_DDS_PROCESS_PENDING(10);
    delay(10);
}
```

## Examples
### Run Basic Pub/Sub
```bash
cd examples/Basic_PubSub
pio run -t upload && pio device monitor
```

### Run Services
```bash
cd examples/Services
pio run -t upload && pio device monitor
```

### Run Actions
```bash
cd examples/Actions
pio run -t upload && pio device monitor
```

### Running Tests
```bash
cd test
pio run -t upload && pio device monitor
```

## License
MIT License - see LICENSE file for details.