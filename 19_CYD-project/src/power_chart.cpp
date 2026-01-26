#include "power_chart.h"
#include <Arduino.h>
#include <string.h>
#include <math.h>

// Circular buffer for power data
static power_data_t power_buffer[POWER_BUFFER_SIZE];
static int buffer_head = 0;
static int buffer_count = 0;

// Chart object reference
static lv_obj_t *chart_obj = NULL;
static lv_chart_series_t *chart_series = NULL;

// Current power value (most recent)
static float current_power = 0.0f;

// Calculated chart range values
static float calculated_y_min = 0.0f;
static float calculated_y_max = 100.0f;

// Chart configuration
static const int CHART_POINTS = 120;  // 120 seconds of data
static const lv_color_t CHART_COLOR = lv_palette_main(LV_PALETTE_BLUE);
static const lv_chart_update_mode_t CHART_UPDATE_MODE = LV_CHART_UPDATE_MODE_SHIFT;

// Initialize the chart system
void power_chart_init(void) {
    // Clear buffer
    buffer_head = 0;
    buffer_count = 0;
    
    // Reset current power
    current_power = 0.0f;
    
    // Reset calculated range values
    calculated_y_min = 0.0f;
    calculated_y_max = 100.0f;
    
    // Chart will be configured when set_chart_obj is called
}

// Set the chart object (called from main after UI initialization)
void power_chart_set_chart_obj(lv_obj_t *chart) {
    Serial.println("[PowerChart] Setting chart object");
    
    if (chart == NULL) {
        Serial.println("[PowerChart] ERROR: Chart object is NULL!");
        return;
    }
    
    chart_obj = chart;
    Serial.printf("[PowerChart] Chart object set: %p\n", chart_obj);
    
    // Configure chart
    lv_chart_set_type(chart_obj, LV_CHART_TYPE_LINE);
    Serial.println("[PowerChart] Chart type set to LINE");
    
    lv_chart_set_point_count(chart_obj, CHART_POINTS);
    Serial.printf("[PowerChart] Chart point count set to %d\n", CHART_POINTS);
    
    // Use SHIFT mode for automatic rolling effect
    lv_chart_set_update_mode(chart_obj, LV_CHART_UPDATE_MODE_SHIFT);
    Serial.println("[PowerChart] Chart update mode set to SHIFT");
    
    // Add series
    chart_series = lv_chart_add_series(chart_obj, CHART_COLOR, LV_CHART_AXIS_PRIMARY_Y);
    if (chart_series == NULL) {
        Serial.println("[PowerChart] ERROR: Failed to add chart series!");
    } else {
        Serial.println("[PowerChart] Chart series added successfully");
    }
    
    // Set range with initial values (will be auto-scaled)
    lv_chart_set_axis_range(chart_obj, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    Serial.println("[PowerChart] Initial chart range set to 0-100");
    
    // Remove grid lines by setting divider line count to 0
    lv_chart_set_div_line_count(chart_obj, 0, 0);
    
    // Ensure background color from eezStudio is preserved
    lv_obj_set_style_bg_opa(chart_obj, LV_OPA_COVER, LV_PART_MAIN);
    
    // Initialize chart data with zeros
    // With LV_CHART_UPDATE_MODE_SHIFT, new values will automatically shift old ones
    lv_chart_set_all_values(chart_obj, chart_series, 0);
    lv_chart_refresh(chart_obj);
    Serial.println("[PowerChart] Chart initialized with zeros");
}

// Add a new power value to the buffer
void power_chart_add_value(float power_W) {
    Serial.printf("[PowerChart] Adding power value: %.1f W\n", power_W);
    
    // Update current power
    current_power = power_W;
    
    if (chart_obj == NULL || chart_series == NULL) {
        Serial.println("[PowerChart] ERROR: Chart not initialized!");
        return;
    }
    
    if (buffer_count >= POWER_BUFFER_SIZE) {
        // Buffer full, overwrite oldest
        buffer_head = (buffer_head + 1) % POWER_BUFFER_SIZE;
        buffer_count--;
    }
    
    // Add new value
    int index = (buffer_head + buffer_count) % POWER_BUFFER_SIZE;
    power_buffer[index].power_W = power_W;
    power_buffer[index].timestamp_ms = millis();
    buffer_count++;
    
    Serial.printf("[PowerChart] Buffer count: %d, head: %d\n", buffer_count, buffer_head);
    
    // Remove values older than 120 seconds
    unsigned long current_time = millis();
    int removed_count = 0;
    while (buffer_count > 0) {
        int oldest_index = buffer_head;
        if (current_time - power_buffer[oldest_index].timestamp_ms <= 120000) {
            break;  // Oldest value is within 120s
        }
        
        // Remove oldest value
        buffer_head = (buffer_head + 1) % POWER_BUFFER_SIZE;
        buffer_count--;
        removed_count++;
    }
    
    if (removed_count > 0) {
        Serial.printf("[PowerChart] Removed %d old values\n", removed_count);
    }
    
    // Update chart
    power_chart_update();
}

// Update chart from buffer data
void power_chart_update(void) {
    Serial.println("[PowerChart] Updating chart...");
    
    if (chart_obj == NULL) {
        Serial.println("[PowerChart] ERROR: Chart object is NULL!");
        return;
    }
    
    if (chart_series == NULL) {
        Serial.println("[PowerChart] ERROR: Chart series is NULL!");
        return;
    }
    
    if (buffer_count == 0) {
        Serial.println("[PowerChart] No data in buffer");
        return;
    }
    
    Serial.printf("[PowerChart] Updating with %d data points\n", buffer_count);
    
    // Calculate min and max values for auto-scaling
    float min_power = power_buffer[buffer_head].power_W;
    float max_power = power_buffer[buffer_head].power_W;
    
    for (int i = 0; i < buffer_count; i++) {
        int index = (buffer_head + i) % POWER_BUFFER_SIZE;
        float power = power_buffer[index].power_W;
        
        if (power < min_power) min_power = power;
        if (power > max_power) max_power = power;
    }
    
    // Calculate y_min and y_max as multiples of 50W with at least 50W range
    // y_min should be the lower multiple of 50W, y_max the higher multiple of 50W
    // This ensures y_max - y_min >= 50W
    
    // Calculate y_min as floor(min_power / 50) * 50
    calculated_y_min = floor(min_power / 50.0f) * 50.0f;
    
    // Calculate y_max as ceil(max_power / 50) * 50
    calculated_y_max = ceil(max_power / 50.0f) * 50.0f;
    
    // Ensure minimum range of 50W
    if (calculated_y_max - calculated_y_min < 50.0f) {
        // If range is less than 50W, increase y_max to meet the requirement
        calculated_y_max = calculated_y_min + 50.0f;
    }
    
    // Update chart range
    lv_chart_set_axis_range(chart_obj, LV_CHART_AXIS_PRIMARY_Y, (int32_t)calculated_y_min, (int32_t)calculated_y_max);
    
    // Get the most recent value (last in buffer)
    int latest_index = (buffer_head + buffer_count - 1) % POWER_BUFFER_SIZE;
    float latest_power = power_buffer[latest_index].power_W;
    
    // Use lv_chart_set_next_value for automatic rolling with SHIFT mode
    lv_chart_set_next_value(chart_obj, chart_series, (int32_t)latest_power);
    
    // Refresh chart
    lv_chart_refresh(chart_obj);
    
    Serial.printf("[PowerChart] Chart updated. Range: %d - %d, Added value: %.1f\n", 
                  (int32_t)calculated_y_min, (int32_t)calculated_y_max, latest_power);
}

// Get current power value
float power_chart_get_current(void) {
    return current_power;
}

// Get minimum power value (calculated chart range)
float power_chart_get_min(void) {
    return calculated_y_min;
}

// Get maximum power value (calculated chart range)
float power_chart_get_max(void) {
    return calculated_y_max;
}

// Reset power statistics (not needed for buffer-based approach)
void power_chart_reset_stats(void) {
    // Nothing to reset since we calculate from buffer
}