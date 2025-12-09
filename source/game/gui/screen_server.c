#include "../game_state.h"
#include "screen.h"
#include "../../platform/input.h"
#include "../../platform/gfx.h"
#include "../../graphics/gui_util.h"
#include "../../network/server_comunication.h"
#include "../../platform/input.h"

#include <network.h>

#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include "../../parson/parson.h"

#define MAX_PARTS   100      // so viele Abschnitte erlaubst du
#define MAX_LENGTH  128     // maximale Länge pro Abschnitt
#define BUF_SIZE 1000

static int count;
static char liste[MAX_PARTS][MAX_LENGTH];

//-------------------------
// Input Mapping
//-------------------------
#define MAX_INPUTS 21
const char* inputs1[] = {
    "IB_FORWARD",
    "IB_BACKWARD",
    "IB_LEFT",
    "IB_RIGHT",
    "IB_ACTION1",
    "IB_ACTION2",
    "IB_JUMP",
    "IB_SNEAK",
    "IB_INVENTORY",
    "IB_HOME",
    "IB_SCROLL_LEFT",
    "IB_SCROLL_RIGHT",
    "IB_GUI_UP",
    "IB_GUI_DOWN",
    "IB_GUI_LEFT",
    "IB_GUI_RIGHT",
    "IB_GUI_CLICK",
    "IB_GUI_CLICK_ALT",
    "IB_SCREENSHOT",
    "IB_BACK",
    "IB_ANY"
};

enum input_button inputs2[] = {
    IB_FORWARD,
    IB_BACKWARD,
    IB_LEFT,
    IB_RIGHT,
    IB_ACTION1,
    IB_ACTION2,
    IB_JUMP,
    IB_SNEAK,
    IB_INVENTORY,
    IB_HOME,
    IB_SCROLL_LEFT,
    IB_SCROLL_RIGHT,
    IB_GUI_UP,
    IB_GUI_DOWN,
    IB_GUI_LEFT,
    IB_GUI_RIGHT,
    IB_GUI_CLICK,
    IB_GUI_CLICK_ALT,
    IB_SCREENSHOT,
    IB_BACK,
    IB_ANY
};

JSON_Object* inputs;
JSON_Value* inputs_val;
char* inputs_old;

//-------------------------
// Render Textures
//-------------------------
typedef struct {
    const char* name;
    struct tex_gfx* ptr;
} TextureMap;

TextureMap texture_map[] = {
    {"texture_fog", &texture_fog},
    {"texture_terrain", &texture_terrain},
    {"texture_particles", &texture_particles},
    {"texture_items", &texture_items},
    {"texture_mobs", &texture_mobs},
    {"texture_minecart", &texture_minecart},
    {"texture_creeper", &texture_creeper},
    {"texture_pig", &texture_pig},
    {"texture_font", &texture_font},
    {"texture_black_font", &texture_black_font},
    {"texture_anim", &texture_anim},
    {"texture_gui_inventory", &texture_gui_inventory},
    {"texture_gui_crafting", &texture_gui_crafting},
    {"texture_gui_furnace", &texture_gui_furnace},
    {"texture_gui_chest", &texture_gui_chest},
    {"texture_gui_iron_chest", &texture_gui_iron_chest},
    {"texture_gui2", &texture_gui2},
    {"texture_controls", &texture_controls},
    {"texture_pointer", &texture_pointer},
    {"texture_clouds", &texture_clouds},
    {"texture_sun", &texture_sun},
    {"texture_moon", &texture_moon},
    {"texture_shadow", &texture_shadow},
    {"texture_water", &texture_water},

    {"texture_mob_char", &texture_mob_char},

    {"texture_armor_chain1", &texture_armor_chain1},
    {"texture_armor_chain2", &texture_armor_chain2},
    {"texture_armor_cloth1", &texture_armor_cloth1},
    {"texture_armor_cloth2", &texture_armor_cloth2},
    {"texture_armor_gold1", &texture_armor_gold1},
    {"texture_armor_gold2", &texture_armor_gold2},
    {"texture_armor_iron1", &texture_armor_iron1},
    {"texture_armor_iron2", &texture_armor_iron2},
    {"texture_armor_diamond1", &texture_armor_diamond1},
    {"texture_armor_diamond2", &texture_armor_diamond2},

    {NULL, NULL} // Ende der Map
};

struct tex_gfx* get_texture_by_name(const char* name) {
    for (int i = 0; texture_map[i].name != NULL; i++) {
        if (strcmp(texture_map[i].name, name) == 0) {
            return texture_map[i].ptr;
        }
    }
    return NULL; // nicht gefunden
}

//-------------------------
// Parse char in functions
//-------------------------
int split_tilde(const char* input, char output[MAX_PARTS][MAX_LENGTH]) {
    int part = 0;
    int pos = 0;

    for(int i = 0; input[i] != '\0'; i++) {
        if(input[i] == '~') {
            output[part][pos] = '\0';  // Abschnitt beenden
            part++;
            pos = 0;

            if(part >= MAX_PARTS) break;
            continue;
        }

        if(pos < MAX_LENGTH - 1) {
            output[part][pos++] = input[i];
        }
    }

    output[part][pos] = '\0'; // letztes Teil
    part++;

    return part; // wie viele Teile gefunden wurden
}


// -------------------------
// Reset-Funktion
// -------------------------
static void screen_server_reset(struct screen* s, int width, int height) {
    char* inputs_old = NULL;

    char* mac_addr = server_get_mac_address();
    if (!mac_addr) {
        mac_addr = "failed";
    }

    //root
    inputs_val = json_value_init_object();
    inputs = json_value_get_object(inputs_val);

    //root.pressed
    JSON_Value* pressed_val = json_value_init_object();
    JSON_Object* pressed_obj = json_value_get_object(pressed_val);

    //root.released
    JSON_Value* released_val = json_value_init_object();
    JSON_Object* released_obj = json_value_get_object(released_val);

    //root.held
    JSON_Value* held_val = json_value_init_object();
    JSON_Object* held_obj = json_value_get_object(held_val);

    //root.other
    JSON_Value* other_val = json_value_init_object();
    JSON_Object* other_obj = json_value_get_object(other_val);


    for (int i = 0; i < 4; i++) {
        if (i < 3) {
            for (int i2 = 0; i2 < MAX_INPUTS; i2++) {
                if (i == 0) 
                    json_object_set_boolean(pressed_obj, inputs1[i2], 
                        input_pressed(inputs2[i2], 1));
                if (i == 1) 
                    json_object_set_boolean(released_obj, inputs1[i2], 
                        input_released(inputs2[i2], 1));
                if (i == 2) 
                    json_object_set_boolean(held_obj, inputs1[i2], 
                        input_held(inputs2[i2], 1)); 
            } 
        } else {
            json_object_set_string(other_obj, "mac_addr", mac_addr);
        }
    }
    
    json_object_set_value(inputs, "pressed", pressed_val);
    json_object_set_value(inputs, "released", released_val);
    json_object_set_value(inputs, "held", held_val);
    json_object_set_value(inputs, "other", other_val);

    inputs_old = json_serialize_to_string(inputs_val);


    server_init(192,168,15,152);
      
    /*u32 mac[6];
    net_get_mac_address(mac);
    server_send(mac, 6);
    */
}

// -------------------------
// Update-Funktion
// -------------------------
static void screen_server_update(struct screen* s, float dt) {

    if (input_pressed(IB_HOME, 1)) {
        server_close();
        screen_back();
        return;
    }

    inputs_old = json_serialize_to_string(inputs_val);

    /*JSON_Object* pressed = json_object_get_object(inputs, "pressed");
    JSON_Object* released = json_object_get_object(inputs, "released");
    JSON_Object* held = json_object_get_object(inputs, "held");
    */

    JSON_Value* pressed_val = json_value_init_object();
    JSON_Object* pressed = json_value_get_object(pressed_val);

    JSON_Value* released_val = json_value_init_object();
    JSON_Object* released = json_value_get_object(released_val);

    JSON_Value* held_val = json_value_init_object();
    JSON_Object* held = json_value_get_object(held_val);


    for (int i = 0; i < 4; i++) {
        for (int i2 = 0; i2 < MAX_INPUTS; i2++) {
            if (i == 0) 
                json_object_set_boolean(pressed, inputs1[i2], 
                    input_pressed(inputs2[i2], 1));
            if (i == 1) 
                json_object_set_boolean(released, inputs1[i2], 
                    input_released(inputs2[i2], 1));
            if (i == 2) 
                json_object_set_boolean(held, inputs1[i2], 
                    input_held(inputs2[i2], 1)); 
        }
    }
    
    json_object_set_value(inputs, "pressed", pressed_val);
    json_object_set_value(inputs, "released", released_val);
    json_object_set_value(inputs, "held", held_val);

    char* new_json = json_serialize_to_string(inputs_val);

    //if (inputs_old != json_serialize_to_string(inputs_val)) {
    if (inputs_old == NULL || strcmp(inputs_old, new_json) != 0) {

        char* json_text = json_serialize_to_string(inputs_val);
        server_send(json_text, strlen(json_text));

        u8* data = NULL;
        int len;

        time_t t = time(NULL);
        struct tm* tm_info = localtime(&t);
        int tm1 = tm_info->tm_sec;

        bool f = false;

        while (data == NULL && f == false) {    
            data = server_receive(&len);

            tm_info = localtime(&t);
            if (tm1 + 1 <= tm_info->tm_sec) {
                server_send("no", 2);
                f = true;
            }

            if (input_pressed(IB_HOME, 1)) {
                server_close();
                screen_back();
                f = true;
                return;
            }
            input_poll();
        }

        if (!f)
            server_send("yes", 3);
    
        if (len < BUF_SIZE) {
            ((char*)data)[len] = '\0';
        }

        count = split_tilde((char*)data, liste);
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

    gutil_text(100, 120, liste[0], 12, false);

    

    for (int i = 0; i < count; i++) {
        //--------------------------------------------------------
        // gutil_text(x, y, str, size, shadow)
        //--------------------------------------------------------
        if (strcmp(liste[i], "gutil_text") == 0) {
            // Prüfen, dass genug Argumente da sind
            if (i + 5 < count) {
                int x = atoi(liste[i + 1]);
                int y = atoi(liste[i + 2]);
                const char* text = liste[i + 3];
                int size = atoi(liste[i + 4]);
                bool flag = (strcmp(liste[i + 5], "true") == 0) ? true : false;

                gutil_text(x, y, text, size, flag);

                i += 5; // Argumente überspringen
            }
        } else 

        //--------------------------------------------------------
        // gfx_bind_texture(tex)
        //--------------------------------------------------------
        if (strcmp(liste[i], "gfx_bind_texture") == 0) {
            // Prüfen, dass das Argument da ist
            if (i + 1 < count) {
                struct tex_gfx* tex = get_texture_by_name(liste[i + 1]);
                    if (tex != NULL) {
                    gfx_bind_texture(tex);  // Pointer auf die Struktur übergeben
                }
                i += 1; // Argument überspringen
            }
        } else

        //--------------------------------------------------------
        // gutil_bg_panorama()
        //--------------------------------------------------------
        if (strcmp(liste[i], "gutil_bg_panorama") == 0) {
            gutil_bg_panorama();
        } else

        //--------------------------------------------------------
        // gutil_window(x, y, width, height, title)
        //--------------------------------------------------------
        if (strcmp(liste[i], "gutil_window") == 0) {
            if (i + 5 < count) {
                int x = atoi(liste[i + 1]);
                int y = atoi(liste[i + 2]);
                int width = atoi(liste[i + 3]);
                int height = atoi(liste[i + 4]);
                char* title = liste[i + 5];

                gutil_window(x, y, width, height, title);
                i += 5;
            }
        } else

        //--------------------------------------------------------
        // gutil_text_col(col)
        //--------------------------------------------------------
        if (strcmp(liste[i], "gutil_text_col") == 0) {
            if (i + 1 < count) {
                int col = atoi(liste[i + 1]);
                gutil_text_col(col);
                i += 1;
            }
        } else

        //--------------------------------------------------------
        // gutil_texquad(x, y, tx, ty, sx, sy, width, height)
        //--------------------------------------------------------
        if (strcmp(liste[i], "gutil_texquad") == 0) {
            if (i + 8 < count) {
                int x      = atoi(liste[i + 1]);
                int y      = atoi(liste[i + 2]);
                int tx     = atoi(liste[i + 3]);
                int ty     = atoi(liste[i + 4]);
                int sx     = atoi(liste[i + 5]);
                int sy     = atoi(liste[i + 6]);
                int width  = atoi(liste[i + 7]);
                int height = atoi(liste[i + 8]);

                gutil_texquad(x, y, tx, ty, sx, sy, width, height);
                i += 8;
            }
        }
    }
}

struct screen screen_server = {
    .reset = screen_server_reset,
    .update = screen_server_update,
    .render2D = screen_server_render2D,
    .render3D = NULL,
    .render_world = false,
};