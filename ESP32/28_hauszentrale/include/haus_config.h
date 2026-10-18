/**
 * @file haus_config.h
 * @brief Small persisted application config (S24).
 *
 * Stored in the template's /config.ini via TplSpiffsConfig.app_flags, so it
 * is version-checked and saved with the rest of the configuration.
 */

#ifndef HAUS_CONFIG_H
#define HAUS_CONFIG_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void haus_config_init(void);

bool haus_config_can_enabled(void);
void haus_config_set_can_enabled(bool enabled);

#ifdef __cplusplus
}
#endif

#endif
