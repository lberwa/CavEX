/*
	Copyright (c) 2022 ByteBit/xtreme8000

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

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <assert.h>
#include "../../m-lib/m-string.h"
#include <malloc.h>
#include <math.h>
#include <string.h>

#include "../../game/game_state.h"
#include "../../graphics/texture_atlas.h"
#include "../../graphics/gfx_settings.h"
#include "../../lodepng/lodepng.h"
#include "../../util.h"
#include "../gfx.h"
#include "../input.h"
#include "../texture.h"

static void shader_error(GLuint shader) {
	GLint is_compiled = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &is_compiled);

	if(!is_compiled) {
		GLint length = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);

		char log[length];
		glGetShaderInfoLog(shader, length, &length, log);
		printf("%s\n", log);

		glDeleteShader(shader);
	}
}

static GLuint create_shader(const char* vertex, const char* fragment) {
	GLuint v = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(v, 1, (const GLchar* const*)&vertex, NULL);
	glCompileShader(v);
	shader_error(v);

	GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(f, 1, (const GLchar* const*)&fragment, NULL);
	glCompileShader(f);
	shader_error(f);

	GLuint program = glCreateProgram();
	glAttachShader(program, v);
	glAttachShader(program, f);

	glBindAttribLocation(program, 0, "a_pos");
	glBindAttribLocation(program, 1, "a_color");
	glBindAttribLocation(program, 2, "a_texcoord");
	glBindAttribLocation(program, 3, "a_light");

	glLinkProgram(program);
	return program;
}

static int window_width = GFX_PC_WINDOW_WIDTH;
static int window_height = GFX_PC_WINDOW_HEIGHT;
GLFWwindow* window;

/* 3D render target: an offscreen FBO that is kept at the *native* window
 * resolution so the 3D scene stays crisp. Blitted 1:1 to the screen. */
static GLuint fbo = 0;
static GLuint fbo_tex = 0;
static GLuint fbo_rbo = 0;
static int fb_width = GFX_PC_WINDOW_WIDTH;
static int fb_height = GFX_PC_WINDOW_HEIGHT;

/* 2D/GUI render target. The GUI uses a "Minecraft-style" integer-ish scaling:
 *
 *   s = min(window_w / BASE_W, window_h / BASE_H)
 *   gui_logical_w = window_w / s,  gui_logical_h = window_h / s
 *
 * The GUI is drawn into a buffer of (gui_logical_w x gui_logical_h) and scaled
 * by the *uniform* factor s onto the whole window. Consequences:
 *  - at the initial window size (= BASE) s == 1 -> GUI is 1:1, exactly as it
 *    looked when the program opened;
 *  - the pixels always stay square (same s on both axes) -> never distorted;
 *  - the more-stretched axis simply gets more logical pixels (more GUI room)
 *    instead of being stretched;
 *  - doubling the window doubles s, so one GUI pixel becomes 2x2 screen pixels.
 */
#define GUI_BASE_WIDTH GFX_PC_WINDOW_WIDTH
#define GUI_BASE_HEIGHT GFX_PC_WINDOW_HEIGHT
static GLuint fbo_gui = 0;
static GLuint fbo_gui_tex = 0;
static GLuint composite_prog = 0;
static int gui_logical_w = GUI_BASE_WIDTH;
static int gui_logical_h = GUI_BASE_HEIGHT;

/* true while we are rendering the 2D/GUI pass (gfx_mode_gui*) -> gfx_width()/
 * gfx_height() then report the logical GUI resolution so the GUI lays out
 * exactly like before. The 3D pass keeps the native resolution. */
static bool gui_pass = false;

/* last viewport requested via gfx_viewport(), in native window pixels, so it
 * can be re-applied when switching back to the 3D pass. */
static int last_vp_x = 0, last_vp_y = 0;
static int last_vp_w = GFX_PC_WINDOW_WIDTH, last_vp_h = GFX_PC_WINDOW_HEIGHT;

/* world clear color (kept so gfx_finish() can clear the 3D FBO correctly even
 * though the GUI FBO is cleared with a transparent color). */
static float clear_r = 1.0F, clear_g = 1.0F, clear_b = 1.0F;

/* deferred inverse-colour crosshair: it must invert the *final* image (3D+GUI),
 * so it cannot be baked into the separate, transparent GUI FBO. It is recorded
 * here (in logical GUI coords) and drawn onto the screen in gfx_finish(). */
static bool xhair_show;
static struct tex_gfx* xhair_tex;
static int xhair_x, xhair_y, xhair_tx, xhair_ty, xhair_sx, xhair_sy, xhair_w,
	xhair_h;

int gfx_width() {
	/* logical GUI width during the 2D pass, native width during the 3D pass */
	return gui_pass ? gui_logical_w : fb_width;
}

int gfx_height() {
	return gui_pass ? gui_logical_h : fb_height;
}

int gfx_gui_width(void) {
	return gui_logical_w;
}

int gfx_gui_height(void) {
	return gui_logical_h;
}

void gfx_pointer_to_gui(float* x, float* y) {
	/* window pixels -> logical GUI coordinates (the GUI is rendered at
	 * gui_logical_* and scaled onto the window) */
	if(x)
		*x = (*x) * (float)gui_logical_w / (float)(window_width > 0 ? window_width : 1);
	if(y)
		*y = (*y) * (float)gui_logical_h / (float)(window_height > 0 ? window_height : 1);
}

/* (re)allocate the offscreen render target for a given size */
static void gfx_resize_fbo(int width, int height) {
	if(width < 1)
		width = 1;
	if(height < 1)
		height = 1;

	fb_width = width;
	fb_height = height;

	/* default 3D viewport covers the whole (native) target */
	last_vp_x = 0;
	last_vp_y = 0;
	last_vp_w = fb_width;
	last_vp_h = fb_height;

	if(!fbo)
		return;

	glBindTexture(GL_TEXTURE_2D, fbo_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fb_width, fb_height, 0, GL_RGBA,
				 GL_UNSIGNED_BYTE, NULL);
	glBindTexture(GL_TEXTURE_2D, 0);

	glBindRenderbuffer(GL_RENDERBUFFER, fbo_rbo);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, fb_width,
						  fb_height);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(0, 0, fb_width, fb_height);
}

/* recompute the GUI logical resolution for a given window size (see the long
 * comment at GUI_BASE_WIDTH) and (re)allocate its texture. */
static void gfx_resize_gui_fbo(int win_w, int win_h) {
	if(win_w < 1)
		win_w = 1;
	if(win_h < 1)
		win_h = 1;

	float sx = (float)win_w / (float)GUI_BASE_WIDTH;
	float sy = (float)win_h / (float)GUI_BASE_HEIGHT;
	float s = (sx < sy) ? sx : sy; /* scale of the less-stretched axis */
	if(s < 0.01F)
		s = 0.01F;

	gui_logical_w = (int)roundf((float)win_w / s);
	gui_logical_h = (int)roundf((float)win_h / s);
	if(gui_logical_w < 1)
		gui_logical_w = 1;
	if(gui_logical_h < 1)
		gui_logical_h = 1;
	/* guard against absurd texture sizes at extreme aspect ratios */
	if(gui_logical_w > 8192)
		gui_logical_w = 8192;
	if(gui_logical_h > 8192)
		gui_logical_h = 8192;

	if(!fbo_gui)
		return;

	glBindTexture(GL_TEXTURE_2D, fbo_gui_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gui_logical_w, gui_logical_h, 0,
				 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glBindTexture(GL_TEXTURE_2D, 0);
}

static void framebuffer_size_callback(GLFWwindow* window, int width,
									  int height) {
	window_width = width;
	window_height = height;
	/* 3D renders at the native window resolution (crisp). */
	gfx_resize_fbo(width, height);
	/* GUI uses uniform scaling: recompute its logical resolution. */
	gfx_resize_gui_fbo(width, height);
}

static void scroll_callback(GLFWwindow* window, double xoffset,
							double yoffset) {
	// TODO: buttons are not released
	if(glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED) {
		if(yoffset > 0) {
			// input_set_status(IB_SCROLL_LEFT, GLFW_PRESS);
		} else if(yoffset < 0) {
			// input_set_status(IB_SCROLL_RIGHT, GLFW_PRESS);
		}
	}
}

static GLuint shader_prog;
static float current_tex_scale_x = 1.0F / 256.0F;
static float current_tex_scale_y = 1.0F / 256.0F;

static void gfx_set_tex_scale(float sx, float sy) {
	current_tex_scale_x = sx;
	current_tex_scale_y = sy;
	glUniform2f(glGetUniformLocation(shader_prog, "tex_scale"), sx, sy);
}

void gfx_setup() {
	glfwInit();

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	window
		= glfwCreateWindow(window_width, window_height, GAME_NAME, NULL, NULL);
	glfwMakeContextCurrent(window);

	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	// glfwSetScrollCallback(window, scroll_callback);

	if(glfwRawMouseMotionSupported())
		glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

	glfwSetInputMode(window, GLFW_STICKY_KEYS, GLFW_TRUE);

	if(glewInit())
		printf("Could not load extended OpenGL functions!\n");

	printf("Vendor: %s\n", glGetString(GL_VENDOR));
	printf("Renderer: %s\n", glGetString(GL_RENDERER));
	printf("Version: %s\n", glGetString(GL_VERSION));

	string_t shader_file;
	string_init(shader_file);

	string_printf(
		shader_file, "%s/vertex.shader",
		config_read_string(&gstate.config_user, "paths.texturepack", "assets"));
	void* vertex = file_read(string_get_cstr(shader_file));
	assert(vertex);

	string_printf(
		shader_file, "%s/fragment.shader",
		config_read_string(&gstate.config_user, "paths.texturepack", "assets"));
	void* fragment = file_read(string_get_cstr(shader_file));
	assert(fragment);

	string_clear(shader_file);

	shader_prog = create_shader(vertex, fragment);
	free(vertex);
	free(fragment);
	glUseProgram(shader_prog);

	gfx_clear_buffers(255, 255, 255);
	gfx_texture(true);
	gfx_alpha_test(true);

	glCullFace(GL_BACK);
	glFrontFace(GL_CW);
	#ifdef GFX_WIREFRAME
	glPolygonMode(GL_FRONT, GL_LINE);
	glPolygonMode(GL_BACK, GL_LINE);
	#endif
	gfx_cull_func(MODE_BACK);

	gfx_depth_func(MODE_LEQUAL);

	glViewport(0, 0, gfx_width(), gfx_height());

	/* Create an offscreen FBO at the fixed logical resolution. Render into
	 * this FBO each frame, then upscale with nearest filtering to the
	 * window size in gfx_finish(). */
	glGenTextures(1, &fbo_tex);
	glBindTexture(GL_TEXTURE_2D, fbo_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fb_width, fb_height, 0, GL_RGBA,
				 GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenRenderbuffers(1, &fbo_rbo);
	glBindRenderbuffer(GL_RENDERBUFFER, fbo_rbo);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, fb_width,
						  fb_height);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
						   GL_TEXTURE_2D, fbo_tex, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, fbo_rbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
							  GL_RENDERBUFFER, fbo_rbo);

	if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		printf("FBO incomplete\n");

	/* Create the fixed-resolution GUI FBO (color only, GL_NEAREST so it stays
	 * pixelated when upscaled). Cleared transparent so the 3D shows through. */
	glGenTextures(1, &fbo_gui_tex);
	glBindTexture(GL_TEXTURE_2D, fbo_gui_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gui_logical_w, gui_logical_h, 0,
				 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	/* MAG nearest -> pixelated when upscaled; MIN linear -> no shimmer when the
	 * window is smaller than the logical buffer (minification). */
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenFramebuffers(1, &fbo_gui);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo_gui);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
						   fbo_gui_tex, 0);
	if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		printf("GUI FBO incomplete\n");
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);

	/* Minimal passthrough shader to composite the GUI FBO onto the screen. */
	composite_prog = create_shader(
		"#version 110\n"
		"void main() {\n"
		"  gl_Position = gl_Vertex;\n"
		"  gl_TexCoord[0] = gl_MultiTexCoord0;\n"
		"}\n",
		"#version 110\n"
		"uniform sampler2D tex;\n"
		"void main() {\n"
		"  gl_FragColor = texture2D(tex, gl_TexCoord[0].st);\n"
		"}\n");

	glUseProgram(shader_prog);

	/* Keep rendering bound to the FBO by default. */
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(0, 0, fb_width, fb_height);

	/* Match the FBO to the actual framebuffer size right away (HiDPI / WM may
	 * differ from the requested window size, and the resize callback is not
	 * guaranteed to fire on creation). */
	{
		int fbw, fbh;
		glfwGetFramebufferSize(window, &fbw, &fbh);
		window_width = fbw;
		window_height = fbh;
		gfx_resize_fbo(fbw, fbh);
		gfx_resize_gui_fbo(fbw, fbh);
	}

	tex_init();
	gfx_bind_texture(&texture_terrain);

	glUniform1i(glGetUniformLocation(shader_prog, "tex"), 0);
	gfx_set_tex_scale(1.0F / 256.0F, 1.0F / 256.0F);
}

static float colors[256];

void gfx_update_light(float daytime, const float* light_lookup) {
	assert(daytime > -GLM_FLT_EPSILON && daytime < 1.0F + GLM_FLT_EPSILON
		   && light_lookup);

	for(int sky = 0; sky < 16; sky++) {
		for(int torch = 0; torch < 16; torch++) {
			colors[torch * 16 + sky]
				= fmaxf(light_lookup[torch], light_lookup[sky] * daytime);
		}
	}

	glUniform1fv(glGetUniformLocation(shader_prog, "lighting"), 256, colors);
}

float gfx_lookup_light(uint8_t light) {
	return colors[light];
}

void gfx_clear_buffers(uint8_t r, uint8_t g, uint8_t b) {
	clear_r = r / 255.0F;
	clear_g = g / 255.0F;
	clear_b = b / 255.0F;
	glClearColor(clear_r, clear_g, clear_b, 1.0F);
}

void gfx_crosshair(struct tex_gfx* tex, int x, int y, int tx, int ty, int sx,
				   int sy, int width, int height) {
	/* recorded in logical GUI coords; actually drawn in gfx_finish() on top of
	 * the composited image so the colour inversion sees the 3D scene */
	xhair_tex = tex;
	xhair_x = x;
	xhair_y = y;
	xhair_tx = tx;
	xhair_ty = ty;
	xhair_sx = sx;
	xhair_sy = sy;
	xhair_w = width;
	xhair_h = height;
	xhair_show = true;
}

/* Composite the 3D FBO + GUI FBO onto the default framebuffer (the screen).
 * Split out of gfx_finish so a screenshot can trigger the same composite and
 * then read the finished image back from framebuffer 0 -- otherwise glReadPixels
 * would only see the still-bound GUI FBO (no world). Does NOT swap buffers and
 * does NOT clear xhair_show, so the caller controls those. */
static void gfx_composite_to_default(void) {
	if(!fbo)
		return;

	glDisable(GL_SCISSOR_TEST);

		/* 1) native 3D image -> screen */
		glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		glBlitFramebuffer(0, 0, fb_width, fb_height, 0, 0, window_width,
						  window_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

		/* 2) GUI image -> screen. The logical buffer already has the window's
		 * aspect ratio (gui_logical_w/h = window/s), so a fullscreen quad scales
		 * it by the uniform factor s -> fills the window, square pixels, no
		 * distortion. MAG nearest -> crisp pixel doubling. */
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, window_width, window_height);
		glUseProgram(composite_prog);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, fbo_gui_tex);
		glUniform1i(glGetUniformLocation(composite_prog, "tex"), 0);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		/* FBO origin is bottom-left, so tex (0,0) maps to NDC (-1,-1). */
		glBegin(GL_QUADS);
		glMultiTexCoord2f(GL_TEXTURE0, 0.0F, 0.0F);
		glVertex2f(-1.0F, -1.0F);
		glMultiTexCoord2f(GL_TEXTURE0, 1.0F, 0.0F);
		glVertex2f(1.0F, -1.0F);
		glMultiTexCoord2f(GL_TEXTURE0, 1.0F, 1.0F);
		glVertex2f(1.0F, 1.0F);
		glMultiTexCoord2f(GL_TEXTURE0, 0.0F, 1.0F);
		glVertex2f(-1.0F, 1.0F);
		glEnd();

		glUseProgram(shader_prog);
		gfx_cull_func(MODE_BACK);

		/* inverse-colour crosshair on top of the final image (3D + GUI) */
		if(xhair_show && xhair_tex) {
			glViewport(0, 0, window_width, window_height);
			mat4 proj;
			glm_ortho(0, gui_logical_w, gui_logical_h, 0, -256, 256, proj);
			gfx_matrix_projection(proj, false);
			gfx_matrix_modelview(GLM_MAT4_IDENTITY);
			gfx_fog(false);
			gfx_lighting(false);
			gfx_texture(true);
			gfx_alpha_test(true);
			gfx_write_buffers(true, false, false);
			gfx_bind_texture(xhair_tex);
			gfx_blending(MODE_INVERT);
			gfx_draw_quads(
				4,
				(int16_t[]) {xhair_x, xhair_y, -2, xhair_x + xhair_w, xhair_y,
							 -2, xhair_x + xhair_w, xhair_y + xhair_h, -2,
							 xhair_x, xhair_y + xhair_h, -2},
				(uint8_t[]) {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
							 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
				(uint16_t[]) {xhair_tx, xhair_ty, xhair_tx + xhair_sx, xhair_ty,
							  xhair_tx + xhair_sx, xhair_ty + xhair_sy, xhair_tx,
							  xhair_ty + xhair_sy});
			gfx_blending(MODE_OFF);
		}
}

void gfx_finish(bool vsync) {
	gfx_composite_to_default();
	xhair_show = false;

	glfwSwapBuffers(window);

	/* clear the GUI FBO transparent for the next frame */
	glBindFramebuffer(GL_FRAMEBUFFER, fbo_gui);
	glViewport(0, 0, gui_logical_w, gui_logical_h);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);

	/* clear the 3D FBO for the next frame */
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(0, 0, fb_width, fb_height);
	gui_pass = false;
	glClearColor(clear_r, clear_g, clear_b, 1.0F);
	gfx_write_buffers(true, true, true);
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
	glfwPollEvents();
}

void gfx_flip_buffers(float* gpu_wait, float* vsync_wait) {
	*gpu_wait = 0;
	*vsync_wait = 0;
}

void gfx_bind_texture(struct tex_gfx* tex) {
	tex_gfx_bind(tex, 0);
	gfx_set_tex_scale(1.0F / (float)(tex ? tex->width : 256),
					  1.0F / (float)(tex ? tex->height : 256));
}

void gfx_set_block_atlas_size(size_t atlas_size) {
	(void)atlas_size;
}

void gfx_bind_texture_virtual(struct tex_gfx* tex) {
	tex_gfx_bind(tex, 0);
	gfx_set_tex_scale(1.0F / 256.0F, 1.0F / 256.0F);
}

void gfx_bind_texture_pixels(struct tex_gfx* tex) {
	gfx_bind_texture(tex);
}

void gfx_copy_framebuffer(uint8_t* dest, size_t* width, size_t* height) {
	assert(width && height);

	/* The final image lives in the default framebuffer only AFTER the 3D + GUI
	 * FBOs are composited (normally in gfx_finish). A screenshot is taken before
	 * that, so composite now and read back the window-sized result -- otherwise
	 * we would only capture the GUI FBO (no world). */
	*width = window_width;
	*height = window_height;

	if(!dest)
		return;

	gfx_composite_to_default();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	void* tmp = malloc(*width * 4);

	if(!tmp)
		return;

	glReadPixels(0, 0, *width, *height, GL_RGBA, GL_UNSIGNED_BYTE, dest);

	// flip image
	for(size_t y = 0; y < *height / 2; y++) {
		memcpy(tmp, dest + y * (*width) * 4, *width * 4);
		memcpy(dest + y * (*width) * 4, dest + (*height - 1 - y) * (*width) * 4,
			   *width * 4);
		memcpy(dest + (*height - 1 - y) * (*width) * 4, tmp, *width * 4);
	}

	free(tmp);
}

void gfx_mode_world() {
	/* render the 3D scene into the native-resolution FBO */
	gui_pass = false;
	if(fbo)
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	/* re-apply the stored (top-left) 3D viewport, flipped to OpenGL bottom-left */
	glViewport(last_vp_x, fb_height - last_vp_y - last_vp_h, last_vp_w,
			   last_vp_h);

	gfx_write_buffers(true, true, true);
	gfx_matrix_texture(false, NULL);
}

void gfx_mode_gui() {
	/* render the 2D/GUI into the fixed-resolution GUI FBO */
	gui_pass = true;
	if(fbo_gui)
		glBindFramebuffer(GL_FRAMEBUFFER, fbo_gui);
	glViewport(0, 0, gui_logical_w, gui_logical_h);

	gfx_fog(false);

	mat4 proj;
	glm_ortho(0, gfx_width(), gfx_height(), 0, -256, 256, proj);
	gfx_matrix_projection(proj, false);
	gfx_matrix_modelview(GLM_MAT4_IDENTITY);

	gfx_lighting(false);
	gfx_blending(MODE_BLEND);
	gfx_alpha_test(true);
	gfx_write_buffers(true, false, false);
}

void gfx_mode_gui_viewport(uint32_t width, uint32_t height) {
	/* render the 2D/GUI into the fixed-resolution GUI FBO (split-screen: the
	 * caller sets the per-player viewport via gfx_viewport() afterwards). */
	gui_pass = true;
	if(fbo_gui)
		glBindFramebuffer(GL_FRAMEBUFFER, fbo_gui);

	gfx_fog(false);

	mat4 proj;
	glm_ortho(0, (float)width, (float)height, 0, -256, 256, proj);
	gfx_matrix_projection(proj, false);
	gfx_matrix_modelview(GLM_MAT4_IDENTITY);

	gfx_lighting(false);
	gfx_blending(MODE_BLEND);
	gfx_alpha_test(true);
	gfx_write_buffers(true, false, false);
}

/* apply a top-left viewport rect to the currently bound target, flipping Y to
 * OpenGL's bottom-left convention. Target height = native for the 3D pass,
 * logical for the GUI pass. */
static void gfx_apply_viewport(int x, int y, int w, int h) {
	int th = gui_pass ? gui_logical_h : fb_height;
	glViewport(x, th - y - h, w, h);
}

void gfx_viewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
	/* coordinates are top-left, in the space of the currently bound target
	 * (native for the 3D pass, logical for the GUI pass), because the caller
	 * derives them from gfx_width()/gfx_height(). */
	if(!gui_pass) {
		last_vp_x = x;
		last_vp_y = y;
		last_vp_w = width;
		last_vp_h = height;
	}
	gfx_apply_viewport(x, y, width, height);
}

void gfx_viewport_reset(void) {
	/* remember the full native rect for the next 3D pass */
	last_vp_x = 0;
	last_vp_y = 0;
	last_vp_w = fb_width;
	last_vp_h = fb_height;
	/* but apply the viewport that matches the currently bound target: if we are
	 * in the GUI pass, the GUI FBO (gui_logical_w/h) is bound, not the native
	 * 3D FBO -- using the native size here would draw the GUI oversized and
	 * off-center. */
	if(gui_pass)
		glViewport(0, 0, gui_logical_w, gui_logical_h);
	else
		glViewport(0, 0, fb_width, fb_height);
}

void gfx_matrix_projection(mat4 proj, bool is_perspective) {
	assert(proj);
	glUniformMatrix4fv(glGetUniformLocation(shader_prog, "proj"), 1, GL_FALSE,
					   (float*)proj);
}

void gfx_matrix_modelview(mat4 mv) {
	assert(mv);
	glUniformMatrix4fv(glGetUniformLocation(shader_prog, "mv"), 1, GL_FALSE,
					   (float*)mv);
}

void gfx_matrix_texture(bool enable, mat4 tex) {
	if(enable) {
		assert(tex);
		glUniformMatrix4fv(glGetUniformLocation(shader_prog, "texm"), 1,
						   GL_FALSE, (float*)tex);
	} else {
		glUniformMatrix4fv(glGetUniformLocation(shader_prog, "texm"), 1,
						   GL_FALSE, (float*)GLM_MAT4_IDENTITY);
	}
}

void gfx_fog_color(uint8_t r, uint8_t g, uint8_t b) {
	glUniform3f(glGetUniformLocation(shader_prog, "fog_color"), r / 255.0F,
				g / 255.0F, b / 255.0F);
}

void gfx_fog_pos(float dx, float dz, float distance) {
	assert(distance > 0);

	glUniform2f(glGetUniformLocation(shader_prog, "fog_delta"), dx, dz);
	glUniform1f(glGetUniformLocation(shader_prog, "fog_distance"), distance);
}

void gfx_fog(bool enable) {
	glUniform1i(glGetUniformLocation(shader_prog, "enable_fog"), enable);
}

void gfx_blending(enum gfx_blend mode) {
	switch(mode) {
		case MODE_BLEND:
			glDisable(GL_COLOR_LOGIC_OP);
			glEnable(GL_BLEND);
			/* RGB: normal alpha blend. ALPHA: accumulate coverage so the GUI
			 * FBO stays fully opaque where it covers (e.g. the dirt menu bg);
			 * otherwise the separate GUI buffer would let the 3D clear colour
			 * bleed through semi-transparent overlays when composited. */
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
								GL_ONE_MINUS_SRC_ALPHA);
			break;
		case MODE_BLEND2:
			glDisable(GL_COLOR_LOGIC_OP);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			break;
		case MODE_BLEND3:
			glDisable(GL_COLOR_LOGIC_OP);
			glEnable(GL_BLEND);
			glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
			break;
		case MODE_INVERT:
			glDisable(GL_BLEND);
			glEnable(GL_COLOR_LOGIC_OP);
			glLogicOp(GL_INVERT);
			break;
		case MODE_OFF:
			glDisable(GL_COLOR_LOGIC_OP);
			glDisable(GL_BLEND);
			break;
	}
}

void gfx_alpha_test(bool enable) {
	glUniform1i(glGetUniformLocation(shader_prog, "enable_alpha"), enable);
}

void gfx_write_buffers(bool color, bool depth, bool depth_test) {
	glColorMask(color, color, color, color);
	glDepthMask(depth);

	if(depth_test) {
		glEnable(GL_DEPTH_TEST);
	} else {
		glDisable(GL_DEPTH_TEST);
	}
}

void gfx_depth_range(float near, float far) {
	glDepthRange(near, far);
}

void gfx_depth_func(enum depth_func func) {
	switch(func) {
		case MODE_LEQUAL: glDepthFunc(GL_LEQUAL); break;
		case MODE_EQUAL: glDepthFunc(GL_EQUAL); break;
	}
}

void gfx_texture(bool enable) {
	glUniform1i(glGetUniformLocation(shader_prog, "enable_texture"), enable);
}

void gfx_lighting(bool enable) {
	glUniform1i(glGetUniformLocation(shader_prog, "enable_lighting"), enable);
}

void gfx_cull_func(enum cull_func func) {
	if(func != MODE_NONE) {
		glEnable(GL_CULL_FACE);
	} else {
		glDisable(GL_CULL_FACE);
	}

	switch(func) {
		case MODE_FRONT: glCullFace(GL_FRONT); break;
		case MODE_BACK: glCullFace(GL_BACK); break;
		default:
	}
}

void gfx_scissor(bool enable, uint32_t x, uint32_t y, uint32_t width,
				 uint32_t height) {
	if(enable) {
		/* input is top-left (GUI/Wii convention); flip Y to OpenGL's bottom-left
		 * using the currently bound target height (logical in the GUI pass,
		 * native in the 3D pass) so it scales correctly with the window. */
		int th = gui_pass ? gui_logical_h : fb_height;
		glEnable(GL_SCISSOR_TEST);
		glScissor((int)x, th - (int)y - (int)height, (int)width, (int)height);
	} else {
		glDisable(GL_SCISSOR_TEST);
	}
}

void gfx_draw_lines(size_t vertex_count, const int16_t* vertices,
					const uint8_t* colors) {
	assert(vertices && colors);
	glLineWidth(2.0F);

	assert(vertex_count < 256);

	float tmp[vertex_count * 3];
	for(size_t k = 0; k < vertex_count * 3; k++)
		tmp[k] = vertices[k] / 256.0F;

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, tmp);
	glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, colors);

	glDrawArrays(GL_LINES, 0, vertex_count);

	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
}

void gfx_draw_quads(size_t vertex_count, const int16_t* vertices,
					const uint8_t* colors, const uint16_t* texcoords) {
	assert(vertices && colors && texcoords);

	assert(vertex_count < 256);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);

	glVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, 0, vertices);
	glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, colors);
	glVertexAttribPointer(2, 2, GL_UNSIGNED_SHORT, GL_FALSE, 0, texcoords);

	glDrawArrays(GL_QUADS, 0, vertex_count);

	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
	glDisableVertexAttribArray(2);
}

void gfx_draw_quads_flt(size_t vertex_count, const float* vertices,
						const uint8_t* colors, const float* texcoords) {
	assert(vertices && colors && texcoords);

	const float prev_scale_x = current_tex_scale_x;
	const float prev_scale_y = current_tex_scale_y;
	gfx_set_tex_scale(1.0F, 1.0F);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vertices);
	glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, colors);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, texcoords);

	glDrawArrays(GL_QUADS, 0, vertex_count);

	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
	glDisableVertexAttribArray(2);

	gfx_set_tex_scale(prev_scale_x, prev_scale_y);
}
