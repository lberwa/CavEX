/*
	Copyright (c) 2022-2026 ByteBit/xtreme8000, lberwa

	This file is part of CavEX.

	CavEX is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	CavEX is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with CavEX.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "pc_input.h"
#include "../../game/game_state.h"
#include "../input.h"
#include "../../network/server_interface.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

extern GLFWwindow* window;

static int fullscreen = 0;
static int oldWidth = 1280;
static int oldHeight = 720;

void toggleFullscreen()
{
    fullscreen = !fullscreen;

    if (fullscreen)
    {
        GLFWmonitor *monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);

        glfwGetWindowSize(window, &oldWidth, &oldHeight);

        glfwSetWindowMonitor(
            window,
            monitor,
            0,
            0,
            mode->width,
            mode->height,
            mode->refreshRate
        );
    }
    else
    {
        glfwSetWindowMonitor(
            window,
            NULL,
            100,
            100,
            oldWidth,
            oldHeight,
            0
        );
    }
}

static int f11_was_pressed = 0;

static bool quit_requested = false;

void pc_update() {
    if (glfwWindowShouldClose(window)) { // quit X
        if (gstate.world_loaded) {
            /* Don't quit immediately: the server runs in its own thread and
             * works on `server` which lives on main()'s stack. If we set
             * gstate.quit now, main() returns and tears down that stack frame
             * while the server thread is still processing the (asynchronous)
             * unload -> it reads freed memory and crashes in
             * level_archive_write_player. Instead request the save/unload and
             * wait: once the server has saved & sent CRPC_WORLD_RESET the
             * client clears world_loaded, and we quit on the next frame. */
            if (!quit_requested) {
                quit_requested = true;
                gstate.paused = false;
                screen_set(&screen_select_world);
                svin_rpc_send(&(struct server_rpc) {
                    .type = SRPC_UNLOAD_WORLD,
                });
            }
            /* keep looping until the world is fully unloaded/saved */
        } else {
            gstate.quit = true;
        }
    }

    int f11_pressed = glfwGetKey(window, GLFW_KEY_F11) == GLFW_PRESS;// F11

    if (f11_pressed && !f11_was_pressed)
    {
        toggleFullscreen();
    }

    f11_was_pressed = f11_pressed;
}

void pc_init() {
    if (gstate.settings.start_fullscreen) {
        toggleFullscreen();
    }
}