#include "ui/actions.h"
#include "ui/screens.h"

void action_button_pressed(lv_event_t *e) {
    // TODO: Implement action button_pressed here
    lv_arc_set_value(objects.value, lv_arc_get_value(objects.value)+10);
}
