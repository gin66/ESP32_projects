#ifndef POWER_CHART_H
#define POWER_CHART_H

#include <lvgl.h>
#include <stdint.h>

// Buffer size for 120 seconds of data at ~1s updates
#define POWER_BUFFER_SIZE 150  // 120s + margin

typedef struct {
    float power_W;
    uint32_t timestamp_ms;
} power_data_t;

// Chart management functions
void power_chart_init(void);
void power_chart_add_value(float power_W);
void power_chart_update(void);
void power_chart_set_chart_obj(lv_obj_t *chart);

// Power statistics functions
float power_chart_get_current(void);
float power_chart_get_min(void);
float power_chart_get_max(void);
void power_chart_reset_stats(void);

#endif // POWER_CHART_H