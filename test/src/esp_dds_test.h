#ifndef ESP_DDS_TEST_H
#define ESP_DDS_TEST_H

#include "esp_dds.h"
#include "dds_platform.h"
#include <stdio.h>

// Test configurations
#define TEST_TOTAL_CYCLES 100
#define TEST_STRESS_ITERATIONS 3
#define TEST_TIMING_SAMPLES 5

// Test result structure
typedef struct {
    const char* test_name;
    bool passed;
    uint32_t min_time_us;
    uint32_t max_time_us;
    uint32_t avg_time_us;
    uint32_t failures;
} test_result_t;

// Timing macros - use the platform-specific implementation
#define TEST_GET_MICROS() DDS_MICROS()
#define TEST_GET_MILLIS() DDS_MILLIS()

// Debug configuration
#define TEST_DEBUG 1

#if TEST_DEBUG
#define TEST_PRINT(...) printf(__VA_ARGS__)
#define TEST_PRINTLN(msg) printf("%s\n", msg)
#else
#define TEST_PRINT(...) 
#define TEST_PRINTLN(msg)
#endif

// Test message types
typedef struct {
    int32_t data;
    uint32_t timestamp;
} test_message_t;

typedef struct {
    int32_t a;
    int32_t b;
} math_request_t;

typedef struct {
    int32_t result;
} math_response_t;

typedef struct {
    int32_t target_position;
    int32_t speed;
} navigation_goal_t;

typedef struct {
    int32_t progress_percent;
} navigation_feedback_t;

typedef struct {
    int32_t final_position;
    uint32_t total_time_ms;
} navigation_result_t;

typedef struct {
    int progress;
    navigation_goal_t goal;
} navigation_context_t;

// Test state
extern uint32_t test_cycle;
extern uint32_t total_failures;
extern bool test_in_progress;
extern test_result_t test_results[];
extern const int NUM_TESTS;

// Test counters
extern volatile uint32_t pub_sub_count;
extern volatile uint32_t service_call_count;
extern volatile uint32_t action_goal_count;
extern volatile uint32_t action_feedback_count;
extern volatile uint32_t action_result_count;

// Test functions
void esp_dds_run_comprehensive_test(void);
void test_basic_pub_sub(void);
void test_service_modes(void);
void test_concurrent_operations(void);
void test_stress_conditions(void);
void test_edge_cases(void);
void test_resource_limits(void);
void test_real_actions(void);
void test_async_calling_thread(void);
void test_concurrent_actions(void);
void test_action_cancellation(void);
void test_deadlock_scenarios(void);
void test_callback_context(void);

// Test callbacks
void test_topic_callback(const char* topic, const void* data, size_t size, void* context);
bool test_service_callback(const void* request, size_t req_size, void* response, size_t* resp_size, void* context);
void test_async_callback(const char* service, const void* response, size_t size, void* context);

// Action callbacks
bool navigation_goal_callback(const void* goal, size_t size, void* context);
esp_dds_action_state_t navigation_execute_callback(const void* goal, size_t goal_size, void* result, size_t* result_size, void* context);
void navigation_cancel_callback(void* context);
void navigation_feedback_callback(const char* action, const void* feedback, size_t size, void* context);
void navigation_result_callback(const char* action, const void* result, size_t size, esp_dds_action_state_t state, void* context);

// Utility functions
void esp_dds_test_cleanup(void);
void esp_dds_register_test_services(void);

#endif // ESP_DDS_TEST_H