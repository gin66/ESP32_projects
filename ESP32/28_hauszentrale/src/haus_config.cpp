/**
 * @file haus_config.cpp
 * @brief Persisted CAN enable intent (S24.04, S23.03).
 */

#include "haus_config.h"

#include "haus_gui.h"
#include "tpl_debug_log.h"
#include "tpl_system.h"

#define HAUS_CFG_CAN_ENABLED 0x00000001u

void haus_config_init(void) {
  TPL_LOGI("config", "loaded: CAN %s",
           (tpl_spiffs_config.app_flags & HAUS_CFG_CAN_ENABLED) ? "on" : "off");
}

bool haus_config_can_enabled(void) {
  return (tpl_spiffs_config.app_flags & HAUS_CFG_CAN_ENABLED) != 0;
}

void haus_config_set_can_enabled(bool enabled) {
  if (enabled)
    tpl_spiffs_config.app_flags |= HAUS_CFG_CAN_ENABLED;
  else
    tpl_spiffs_config.app_flags &= ~HAUS_CFG_CAN_ENABLED;
  const bool ok = tpl_write_config();
  /* The config write disables the cache, so the framebuffer update made at
   * the same moment can be lost; ask for a full repaint. */
  gui_request_redraw();
  TPL_LOGI("config", "CAN %s %s", enabled ? "on" : "off",
           ok ? "persisted" : "NOT persisted");
}
