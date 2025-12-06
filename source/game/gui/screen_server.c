#include <stdbool.h>
#include "../game_state.h"
#include "screen.h"
#include "../../platform/input.h"
#include "../../graphics/gui_util.h"

// -------------------------
// Reset-Funktion
// -------------------------
static void screen_server_reset(struct screen* s, int width, int height) {

}

// -------------------------
// Update-Funktion
// -------------------------
static void screen_server_update(struct screen* s, float dt) {

    if (input_pressed(IB_HOME, 1)) {
        screen_back();
    }

}

// -------------------------
// Render2D-Funktion
// -------------------------
static void screen_server_render2D(struct screen* s, int width, int height) {
    gutil_bg();
    
    char buf[128];
    //snprintf(buf, sizeof(buf), "Server Screen - Network is %s", gstate.network);
    snprintf(buf, sizeof(buf), "Server Screen - Network is %s", gstate.network ? "ON" : "OFF");


    gutil_text(100, 100, buf, 16, false);

}

struct screen screen_server = {
    .reset = screen_server_reset,
    .update = screen_server_update,
    .render2D = screen_server_render2D,
    .render3D = NULL,
    .render_world = false,
};