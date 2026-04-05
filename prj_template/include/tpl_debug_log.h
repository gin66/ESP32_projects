#include <Arduino.h>

void tpl_debug_log_setup();
void tpl_debug_log_register_endpoints();

#ifdef USE_TPL_DEBUG_LOG

#ifndef DEBUG_LOG_LINES
#define DEBUG_LOG_LINES 100
#endif
#ifndef DEBUG_LOG_LINE_SIZE
#define DEBUG_LOG_LINE_SIZE 160
#endif

#endif
