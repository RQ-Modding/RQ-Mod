/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 * Copyright (C) 2000- Fumihiko Murata <fmurata@p1.tcnet.ne.jp>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#include <SDL.h>
#include "gfx/gl.h"
#include <cglm/cglm.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "system4.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "system4/utfsjis.h"

#include "gfx/gfx.h"
#include "gfx/private.h"
#include "win32_ui.h"
#include "icon.h"
#include "modloader.h"
#include "profile.h"
#include "xsystem4.h"

struct sdl_private sdl;

static const GLchar glsl_preamble[] =
#ifdef USE_GLES
	"#version 300 es\n"
	"precision highp float;\n"
	"precision highp int;\n";
#else
	"#version 140\n";
#endif

/*
 * Transform from the window coordinate system -> clip-space.
 *
 * Window CS:  x= 0..w, y= 0..h (+y down)
 * clip-space: x=-1..1, y=-1..1 (+y up)
 *
 * +-                 -+
 * | 2/w   0    0   -1 |
 * |  0   -2/h  0    1 |
 * |  0    0    2   -1 |
 * |  0    0    0    1 |
 * +-                 -+
 */
static mat4 world_view_transform = MAT4(
	0, 0, 0, -1,
	0, 0, 0, -1,
	0, 0, 1, -1,
	0, 0, 0,  1);

static struct shader default_shader;
static struct shader present_bicubic;
static struct shader present_lanczos;
static struct shader present_xbrz;
static GLint loc_bicubic_tex_size = -1;
static GLint loc_lanczos_tex_size = -1;
static GLint loc_xbrz_tex_size = -1;
static int scaling_type = GFX_SCALE_STANDARD;

static const char *scaling_names[GFX_SCALE_COUNT] = {
	"standard", "bicubic", "nearest", "lanczos", "xbrz"
};

const char *gfx_scaling_name(int type)
{
	if (type < 0 || type >= GFX_SCALE_COUNT)
		return scaling_names[0];
	return scaling_names[type];
}

int gfx_scaling_from_name(const char *name)
{
	if (!name)
		return -1;
	for (int i = 0; i < GFX_SCALE_COUNT; i++) {
		if (!SDL_strcasecmp(scaling_names[i], name))
			return i;
	}
	if (!SDL_strcasecmp(name, "bilinear") || !SDL_strcasecmp(name, "normal"))
		return GFX_SCALE_STANDARD;
	if (!SDL_strcasecmp(name, "pixel"))
		return GFX_SCALE_NEAREST;
	return -1;
}

void gfx_set_scaling_type(int type)
{
	if (type < 0 || type >= GFX_SCALE_COUNT)
		type = GFX_SCALE_STANDARD;
	scaling_type = type;
}

int gfx_get_scaling_type(void)
{
	return scaling_type;
}

void gfx_get_window_pos_size(int *x, int *y, int *w, int *h)
{
	if (!sdl.window)
		return;
	if (x || y)
		SDL_GetWindowPosition(sdl.window, x, y);
	if (w || h)
		SDL_GetWindowSize(sdl.window, w, h);
}

bool gfx_handle_syscommand(possibly_unused unsigned cmd)
{
	return false;
}

static GLuint main_surface_fb;
static struct texture main_surface;
/* One FBO for offscreen blits. CopyUseAMapBorder/AddDA used to
 * glGen/Check/Delete a framebuffer per glyph (~40µs × hundreds). */
static GLuint blit_fbo;
static GLuint blit_attached_tex;
static struct texture *view = &main_surface;
static GLint max_texture_size;
static SDL_Color clear_color = { 0, 0, 0, 255 };
static float frame_rate;
static bool wait_vsync = false;

/*
 * RQ MOD (2.67.0): the 3D at the window's size.
 *
 * A 3D view draws into sharp.target at the window's scale (and copies itself, scaled down, into
 * its sprite). The frame then draws the scene twice into float pictures put on main_surface_fb
 * (gfx_reset_framebuffer binds that one): with the view's sprite black (layer[0], P) and white
 * (layer[1], P + T), every blend being affine in the colour underneath. gfx_layer_finish writes
 * main_surface from them and the sprite's own picture (the game's picture as before, for shots
 * and copies), and gfx_swap shows F(P) + (F(P + T) - F(P)) * X, F the filter chosen and X the
 * view at the window's pixels (shaders/present_layered.f.glsl).
 */
struct layered_shader {
	struct shader s;
	GLint tex_size, white_tex, hires_tex, hires_rect;
	bool tried;
};
static struct {
	bool on;                  // the setting
	bool broken;              // a shader or a picture failed: drawn as before
	struct texture target;    // the view at the window's scale
	void *drawn_key;          // the view that drew this frame
	int drawn;                // how many drew
	struct texture layer[2];  // the scene with the view black, white (RGBA16F)
	bool layer_ok;
	bool in_pass;
	bool armed;               // the next swap shows the view at the window's size
	bool shown;               // the last swap showed it
	float rect[4];            // the view's place in the game's picture (0 to 1)
	struct layered_shader plain, bicubic, lanczos, xbrz;
	float logged_scale;
	int logged_w, logged_h;
} sharp = { .on = true, .logged_scale = -1.f };

static GLchar *read_shader_file(const char *path)
{
	GLchar *source = SDL_LoadFile(path, NULL);
	if (!source) {
		char full_path[PATH_MAX];
		snprintf(full_path, PATH_MAX, XSYS4_DATA_DIR "/%s", path);
		source = SDL_LoadFile(full_path, NULL);
		if (!source)
			ERROR("Failed to load shader file %s", full_path, strerror(errno));
	}
	return source;
}

GLuint gfx_load_shader_file(const char *path, GLenum type, const char *defines)
{
	GLint shader_compiled;
	GLuint shader;
	const GLchar *source[3] = {
		glsl_preamble,
		defines ? defines : "",
		read_shader_file(path)
	};
	shader = glCreateShader(type);
	glShaderSource(shader, 3, source, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &shader_compiled);
	if (!shader_compiled) {
		GLint len;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
		char *infolog = xmalloc(len + 1);
		glGetShaderInfoLog(shader, len, NULL, infolog);
		ERROR("Failed to compile shader %s: %s", path, infolog);
	}
	SDL_free((char*)source[2]);
	return shader;
}

void gfx_load_shader(struct shader *dst, const char *vertex_shader_path, const char *fragment_shader_path)
{
	GLuint program = glCreateProgram();
	GLuint vertex_shader = gfx_load_shader_file(vertex_shader_path, GL_VERTEX_SHADER, NULL);
	GLuint fragment_shader = gfx_load_shader_file(fragment_shader_path, GL_FRAGMENT_SHADER, NULL);

	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);
	glLinkProgram(program);

	GLint link_success;
	glGetProgramiv(program, GL_LINK_STATUS, &link_success);
	if (!link_success)
		ERROR("Failed to link shader: %s, %s", vertex_shader_path, fragment_shader_path);

	dst->program = program;
	dst->world_transform = glGetUniformLocation(program, "world_transform");
	dst->view_transform = glGetUniformLocation(program, "view_transform");
	dst->texture = glGetUniformLocation(program, "tex");
	dst->vertex_pos = glGetAttribLocation(program, "vertex_pos");
	dst->vertex_uv = glGetAttribLocation(program, "vertex_uv");
	dst->prepare = NULL;
}

static void present_prepare_bicubic(struct gfx_render_job *job, void *data)
{
	Texture *t = data;
	glUniform2f(loc_bicubic_tex_size, (float)t->w, (float)t->h);
}

static void present_prepare_lanczos(struct gfx_render_job *job, void *data)
{
	Texture *t = data;
	glUniform2f(loc_lanczos_tex_size, (float)t->w, (float)t->h);
}

static void present_prepare_xbrz(struct gfx_render_job *job, void *data)
{
	Texture *t = data;
	glUniform2f(loc_xbrz_tex_size, (float)t->w, (float)t->h);
}

static int gl_initialize(void)
{
	gfx_load_shader(&default_shader, "shaders/render.v.glsl", "shaders/render.f.glsl");
	gfx_load_shader(&present_bicubic, "shaders/render.v.glsl", "shaders/present_bicubic.f.glsl");
	gfx_load_shader(&present_lanczos, "shaders/render.v.glsl", "shaders/present_lanczos.f.glsl");
	gfx_load_shader(&present_xbrz, "shaders/render.v.glsl", "shaders/present_xbrz.f.glsl");
	loc_bicubic_tex_size = glGetUniformLocation(present_bicubic.program, "tex_size");
	loc_lanczos_tex_size = glGetUniformLocation(present_lanczos.program, "tex_size");
	loc_xbrz_tex_size = glGetUniformLocation(present_xbrz.program, "tex_size");
	present_bicubic.prepare = present_prepare_bicubic;
	present_lanczos.prepare = present_prepare_lanczos;
	present_xbrz.prepare = present_prepare_xbrz;

	const struct gfx_vertex vertex_data[] = {
		//  x,   y,   z,   w,   u,   v
		{ 0.f, 0.f, 0.f, 1.f, 0.f, 0.f },
		{ 1.f, 0.f, 0.f, 1.f, 1.f, 0.f },
		{ 0.f, 1.f, 0.f, 1.f, 0.f, 1.f },
		{ 1.f, 1.f, 0.f, 1.f, 1.f, 1.f }
	};

	const GLuint rect_index_data[] = { 0, 1, 3, 2 };
	const GLuint line_index_data[] = { 0, 3 };

	glGenVertexArrays(1, &sdl.gl.vao);
	glBindVertexArray(sdl.gl.vao);

	glGenBuffers(1, &sdl.gl.vbo);
	glBindBuffer(GL_ARRAY_BUFFER, sdl.gl.vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_data), vertex_data, GL_STATIC_DRAW);
	glGenBuffers(1, &sdl.gl.quad_vbo);

	glGenBuffers(1, &sdl.gl.rect_ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sdl.gl.rect_ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, 4 * sizeof(GLuint), rect_index_data, GL_STATIC_DRAW);

	glGenBuffers(1, &sdl.gl.line_ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sdl.gl.line_ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, 2 * sizeof(GLuint), line_index_data, GL_STATIC_DRAW);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glBindVertexArray(0);

	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);

	return 0;
}

static void set_window_title(void)
{
	char title[1024] = { [1023] = 0 };
	char *game_name = sjis2utf(config.game_name, 0);
	rq_window_title(title, sizeof title, game_name);
	free(game_name);

	SDL_SetWindowTitle(sdl.window, title);
}

static bool gfx_initialized = false;

/*
 * Reduce window size if the display resolution is smaller than the game
 * resolution.
 */
static void init_window_size(void)
{
#ifndef __ANDROID__
	int display_id = SDL_GetWindowDisplayIndex(sdl.window);
	if (display_id < 0)
		return;

	SDL_Rect bounds;
	if (SDL_GetDisplayUsableBounds(display_id, &bounds) < 0)
		return;

	int w, h;
	SDL_GetWindowSize(sdl.window, &w, &h);
	if (bounds.w > w && bounds.h > h)
		return;

	int border_top, border_bot;
	if (SDL_GetWindowBordersSize(sdl.window, &border_top, NULL, &border_bot, NULL) < 0)
		return;

	int scaled_h = bounds.h - (border_top + border_bot);
	int scaled_w = ((float)scaled_h / h) * w;
	int scaled_x = bounds.w / 2 - scaled_w / 2;

	SDL_SetWindowSize(sdl.window, scaled_w, scaled_h);
	SDL_SetWindowPosition(sdl.window, bounds.x + scaled_x, bounds.y + border_top);
#endif
}

int gfx_init(void)
{
	if (gfx_initialized)
		return true;

	uint32_t flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO;
	if (config.joypad)
		flags |= SDL_INIT_GAMECONTROLLER;
#ifdef _WIN32
	// On a monitor scaled above 100% in Windows, Windows stretched the whole window of a program
	// that does not know the scale, which blurred everything. In DPI-scaled points the window keeps
	// its size on the screen while the game is drawn into the screen's own pixels; the mouse is in
	// points (mouse_get_pos_raw in input.c turns it into pixels).
	SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
#endif
	if (SDL_Init(flags) < 0)
		ERROR("SDL_Init failed: %s", SDL_GetError());

#ifdef USE_GLES
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif

	sdl.format = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA32);
	sdl.window =  SDL_CreateWindow("XSystem4",
				       SDL_WINDOWPOS_UNDEFINED,
				       SDL_WINDOWPOS_UNDEFINED,
				       config.view_width,
				       config.view_height,
				       SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
	if (!sdl.window)
		ERROR("SDL_CreateWindow failed: %s", SDL_GetError());

	set_window_title();
	win32_ui_init(sdl.window);

	sdl.gl.context = SDL_GL_CreateContext(sdl.window);
	if (!sdl.gl.context)
		ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());

#ifndef USE_GLES
	glewExperimental = GL_TRUE;
	GLenum err = glewInit();
	if (err != GLEW_OK && err != GLEW_ERROR_NO_GLX_DISPLAY)
		ERROR("glewInit failed");
#endif

	SDL_GL_SetSwapInterval(wait_vsync ? 1 : 0);
	gl_initialize();
	gfx_draw_init();
	gfx_set_window_logical_size(config.view_width, config.view_height);
	init_window_size();
	atexit(gfx_fini);
	gfx_clear();
	icon_init();
	gfx_initialized = true;
	rq_prof_init();
	return 0;
}

void gfx_fini(void)
{
	if (blit_fbo) {
		glDeleteFramebuffers(1, &blit_fbo);
		blit_fbo = 0;
		blit_attached_tex = 0;
	}
	glDeleteProgram(default_shader.program);
	SDL_DestroyWindow(sdl.window);
	SDL_FreeFormat(sdl.format);
	SDL_Quit();
}

Texture *gfx_main_surface(void)
{
	return &main_surface;
}

static void main_surface_init(int w, int h)
{
	gfx_delete_texture(&main_surface);

	glGenTextures(1, &main_surface.handle);
	glBindTexture(GL_TEXTURE_2D, main_surface.handle);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);

	main_surface.w = w;
	main_surface.h = h;

	glGenFramebuffers(1, &main_surface_fb);
	glBindFramebuffer(GL_FRAMEBUFFER, main_surface_fb);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, main_surface.handle, 0);
}

void gfx_set_window_logical_size(int w, int h)
{
	sdl.w = w;
	sdl.h = h;
	world_view_transform[0][0] = 2.0 / w;
	world_view_transform[1][1] = 2.0 / h;
	gfx_update_screen_scale();
	main_surface_init(w, h);
}

bool gfx_set_fullscreen(bool enable)
{
	uint32_t flags = enable ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
	if (SDL_SetWindowFullscreen(sdl.window, flags) < 0) {
		WARNING("Failed to set fullscreen mode: %s", SDL_GetError());
		return false;
	}
	return true;
}

bool gfx_is_fullscreen(void)
{
	return SDL_GetWindowFlags(sdl.window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
}

void gfx_toggle_fullscreen(void)
{
	gfx_set_fullscreen(!gfx_is_fullscreen());
}

// The drawable's size when the view was last fitted to it.
static int drawable_w, drawable_h;

void gfx_update_screen_scale(void)
{
	int display_w, display_h;
	SDL_GL_GetDrawableSize(sdl.window, &display_w, &display_h);
	drawable_w = display_w;
	drawable_h = display_h;

	if (display_w * sdl.h == display_h * sdl.w) {
		// The aspect ratios are the same, just scale appropriately.
		sdl.viewport.x = 0;
		sdl.viewport.y = 0;
		sdl.viewport.w = display_w;
		sdl.viewport.h = display_h;
		return;
	}

	double logical_aspect = (double)sdl.w / sdl.h;
	double display_aspect = (double)display_w / display_h;
	if (logical_aspect > display_aspect) {
		// letterbox
		sdl.viewport.w = display_w;
		sdl.viewport.h = sdl.h * display_w / sdl.w;
		sdl.viewport.x = 0;
		sdl.viewport.y = (display_h - sdl.viewport.h) / 2;
	} else {
		// pillarbox (side bars)
		sdl.viewport.w = sdl.w * display_h / sdl.h;
		sdl.viewport.h = display_h;
		sdl.viewport.x = (display_w - sdl.viewport.w) / 2;
		sdl.viewport.y = 0;
	}
}

void gfx_set_wait_vsync(bool wait)
{
	wait_vsync = wait;
	if (gfx_initialized)
		SDL_GL_SetSwapInterval(wait ? 1 : 0);
}

void gfx_set_clear_color(int r, int g, int b, int a)
{
	clear_color.r = max(0, min(255, r));
	clear_color.g = max(0, min(255, g));
	clear_color.b = max(0, min(255, b));
	clear_color.a = max(0, min(255, a));
}

void gfx_clear(void)
{
	glClearColor(clear_color.r / 255.f, clear_color.g / 255.f, clear_color.b / 255.f, clear_color.a / 255.f);
	glClear(GL_COLOR_BUFFER_BIT);
}

static mat4 mw_transform = GLM_MAT4_IDENTITY_INIT;

void gfx_set_view_offset(int x, int y)
{
	glm_mat4_identity(mw_transform);
	if (!x && !y)
		return;
	vec3 off = { (float)x / config.view_width, (float)y / config.view_height, 0 };
	glm_translate(mw_transform, off);
}

float gfx_get_frame_rate(void)
{
	return frame_rate;
}

static void gfx_update_frame_rate_counter(void)
{
	static uint64_t timestamp;
	static int frame_count;

	frame_count++;
	uint64_t current_time = SDL_GetTicks64();
	if (current_time > timestamp + 1000) {
		frame_rate = frame_count * 1000.f / (current_time - timestamp);
		timestamp = current_time;
		frame_count = 0;
	}
}

/* RQ MOD (2.67.0): the 3D at the window's size; see `sharp` at the top. */

void gfx_set_sharp_3d(bool on)
{
	if (sharp.on != on)
		rq_log("3D at the window's size: %s", on ? "on" : "off");
	sharp.on = on;
	if (!on)
		sharp.armed = false;
	sharp.logged_scale = -1.f;
}

bool gfx_get_sharp_3d(void)
{
	return sharp.on;
}

float gfx_sharp_3d_scale(void)
{
	if (!sharp.on || sharp.broken || view != &main_surface || sdl.w <= 0)
		return 1.f;
	float s = (float)sdl.viewport.w / sdl.w;
	// No larger than the game's picture: nothing to gain.
	return s < 1.01f ? 1.f : s;
}

static bool make_texture(struct texture *t, int w, int h, GLint internal, GLenum type)
{
	while (glGetError() != GL_NO_ERROR)
		;
	if (!t->handle)
		glGenTextures(1, &t->handle);
	glBindTexture(GL_TEXTURE_2D, t->handle);
	glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, GL_RGBA, type, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
	t->w = w;
	t->h = h;
	return glGetError() == GL_NO_ERROR;
}

static void sharp_fail(const char *what)
{
	WARNING("3D at the window's size: %s; the 3D is drawn at the game's size", what);
	rq_log("3D at the window's size: %s; the 3D is drawn at the game's size", what);
	sharp.broken = true;
	sharp.armed = false;
}

struct texture *gfx_sharp_3d_target(int w, int h)
{
	if (sharp.broken || w <= 0 || h <= 0 || w > max_texture_size || h > max_texture_size)
		return NULL;
	if (sharp.target.handle && sharp.target.w == w && sharp.target.h == h)
		return &sharp.target;
	if (!make_texture(&sharp.target, w, h, GL_RGBA8, GL_UNSIGNED_BYTE)) {
		sharp_fail("no picture of that size");
		return NULL;
	}
	return &sharp.target;
}

void gfx_sharp_3d_drawn(void *key)
{
	sharp.drawn_key = key;
	sharp.drawn++;
}

void *gfx_sharp_3d_take(void)
{
	void *key = sharp.drawn == 1 ? sharp.drawn_key : NULL;
	sharp.drawn = 0;
	sharp.drawn_key = NULL;
	return key;
}

// A shader's source read from the game's folder or the data folder, or NULL.
static GLchar *try_read_shader(const char *path)
{
	GLchar *source = SDL_LoadFile(path, NULL);
	if (!source) {
		char full_path[PATH_MAX];
		snprintf(full_path, PATH_MAX, XSYS4_DATA_DIR "/%s", path);
		source = SDL_LoadFile(full_path, NULL);
	}
	return source;
}

// The layered present over a filter: the filter's file with PRESENT_LAYERED, then
// present_layered.f.glsl. A failure is logged and leaves the shader empty.
static bool load_layered(struct layered_shader *ls, const char *filter_path)
{
	GLchar *filter = try_read_shader(filter_path);
	GLchar *layered = try_read_shader("shaders/present_layered.f.glsl");
	if (!filter || !layered) {
		WARNING("3D at the window's size: %s or present_layered.f.glsl not found", filter_path);
		SDL_free(filter);
		SDL_free(layered);
		return false;
	}
	const GLchar *source[4] = { glsl_preamble, "#define PRESENT_LAYERED\n", filter, layered };
	GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fs, 4, source, NULL);
	glCompileShader(fs);
	SDL_free(filter);
	SDL_free(layered);
	GLint ok = 0;
	glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024] = "";
		glGetShaderInfoLog(fs, sizeof log, NULL, log);
		WARNING("3D at the window's size: %s: %s", filter_path, log);
		glDeleteShader(fs);
		return false;
	}
	GLuint vs = gfx_load_shader_file("shaders/render.v.glsl", GL_VERTEX_SHADER, NULL);
	GLuint program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	glDeleteShader(vs);
	glDeleteShader(fs);
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if (!ok) {
		WARNING("3D at the window's size: %s did not link", filter_path);
		glDeleteProgram(program);
		return false;
	}
	ls->s.program = program;
	ls->s.world_transform = glGetUniformLocation(program, "world_transform");
	ls->s.view_transform = glGetUniformLocation(program, "view_transform");
	ls->s.texture = glGetUniformLocation(program, "tex");
	ls->s.vertex_pos = glGetAttribLocation(program, "vertex_pos");
	ls->s.vertex_uv = glGetAttribLocation(program, "vertex_uv");
	ls->s.prepare = NULL;
	ls->tex_size = glGetUniformLocation(program, "tex_size");
	ls->white_tex = glGetUniformLocation(program, "white_tex");
	ls->hires_tex = glGetUniformLocation(program, "hires_tex");
	ls->hires_rect = glGetUniformLocation(program, "hires_rect");
	return true;
}

static struct layered_shader *layered_for(int type)
{
	struct layered_shader *ls = &sharp.plain;
	const char *path = "shaders/present_plain.f.glsl";
	switch (type) {
	case GFX_SCALE_BICUBIC:
		ls = &sharp.bicubic;
		path = "shaders/present_bicubic.f.glsl";
		break;
	case GFX_SCALE_LANCZOS:
		ls = &sharp.lanczos;
		path = "shaders/present_lanczos.f.glsl";
		break;
	case GFX_SCALE_XBRZ:
		ls = &sharp.xbrz;
		path = "shaders/present_xbrz.f.glsl";
		break;
	default:
		break;
	}
	if (!ls->s.program && !ls->tried) {
		ls->tried = true;
		load_layered(ls, path);
	}
	return ls->s.program ? ls : NULL;
}

struct layered_draw {
	struct layered_shader *ls;
	GLuint hires;
};

static void prepare_layered(possibly_unused struct gfx_render_job *job, void *data)
{
	struct layered_draw *d = data;
	glUniform2f(d->ls->tex_size, (float)sharp.layer[0].w, (float)sharp.layer[0].h);
	glUniform4fv(d->ls->hires_rect, 1, sharp.rect);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, sharp.layer[1].handle);
	glUniform1i(d->ls->white_tex, 1);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, d->hires);
	glUniform1i(d->ls->hires_tex, 2);
	glActiveTexture(GL_TEXTURE0);
}

// The two layers through a layered shader, each sampled with filter (GL_LINEAR or GL_NEAREST).
static void draw_layered(struct layered_shader *ls, GLuint hires, GLint filter, GLfloat *world, GLfloat *viewt)
{
	for (int i = 0; i < 2; i++) {
		glBindTexture(GL_TEXTURE_2D, sharp.layer[i].handle);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	struct layered_draw d = { ls, hires };
	ls->s.prepare = prepare_layered;
	struct gfx_render_job job = {
		.shader = &ls->s,
		.shape = GFX_RECTANGLE,
		.texture = sharp.layer[0].handle,
		.world_transform = world,
		.view_transform = viewt,
		.data = &d
	};
	glDisable(GL_BLEND);
	gfx_render(&job);
	glEnable(GL_BLEND);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
}

static void attach_main_surface(void)
{
	glBindFramebuffer(GL_FRAMEBUFFER, main_surface_fb);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, main_surface.handle, 0);
	glViewport(0, 0, sdl.w, sdl.h);
	sharp.in_pass = false;
}

bool gfx_layer_pass(int pass)
{
	if (sharp.broken || pass < 0 || pass > 1)
		return false;
	if (pass == 0) {
		// The shaders this frame needs, before anything is drawn.
		if (!layered_for(GFX_SCALE_STANDARD) || !layered_for(scaling_type)) {
			sharp_fail("its shaders did not load");
			return false;
		}
	}
	if (!sharp.layer_ok || sharp.layer[0].w != sdl.w || sharp.layer[0].h != sdl.h) {
		sharp.layer_ok = false;
		for (int i = 0; i < 2; i++) {
			if (!make_texture(&sharp.layer[i], sdl.w, sdl.h, GL_RGBA16F, GL_FLOAT)) {
				sharp_fail("no float picture");
				return false;
			}
		}
		glBindFramebuffer(GL_FRAMEBUFFER, main_surface_fb);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sharp.layer[0].handle, 0);
		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		attach_main_surface();
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			sharp_fail("a float picture cannot be drawn into");
			return false;
		}
		sharp.layer_ok = true;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, main_surface_fb);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sharp.layer[pass].handle, 0);
	glViewport(0, 0, sdl.w, sdl.h);
	sharp.in_pass = true;
	return true;
}

void gfx_layer_cancel(void)
{
	attach_main_surface();
	sharp.armed = false;
}

void gfx_layer_scene_drawn(void)
{
	if (!sharp.in_pass)
		sharp.armed = false;
}

void gfx_layer_finish(struct texture *x, Rectangle *rect)
{
	attach_main_surface();
	struct layered_shader *ls = layered_for(GFX_SCALE_STANDARD);
	if (!ls || !x || !x->handle || !sharp.target.handle) {
		sharp.armed = false;
		return;
	}
	sharp.rect[0] = (float)rect->x / sdl.w;
	sharp.rect[1] = (float)rect->y / sdl.h;
	sharp.rect[2] = (float)(rect->x + rect->w) / sdl.w;
	sharp.rect[3] = (float)(rect->y + rect->h) / sdl.h;

	// The game's picture: black + (white - black) * the view at the game's size, one pixel each.
	static mat4 identity = GLM_MAT4_IDENTITY_INIT;
	static mat4 fbo_transform = MAT4(
		2, 0, 0, -1,
		0, 2, 0, -1,
		0, 0, 1,  0,
		0, 0, 0,  1);
	draw_layered(ls, x->handle, GL_NEAREST, identity[0], fbo_transform[0]);
	sharp.armed = true;

	// Once per size of the 3D's picture (a resize draws one frame at the old size first).
	if (sharp.target.w != sharp.logged_w || sharp.target.h != sharp.logged_h || sharp.logged_scale < 0.f) {
		float s = (float)sharp.target.w / x->w;
		rq_log("3D at the window's size: %dx%d (scale %.2f)", sharp.target.w, sharp.target.h, s);
		sharp.logged_scale = s;
		sharp.logged_w = sharp.target.w;
		sharp.logged_h = sharp.target.h;
	}
}

void gfx_window_info(struct gfx_window_info *info)
{
	memset(info, 0, sizeof *info);
	if (!sdl.window)
		return;
	SDL_GetWindowSize(sdl.window, &info->w, &info->h);
	SDL_GL_GetDrawableSize(sdl.window, &info->px_w, &info->px_h);
	info->view_x = sdl.viewport.x;
	info->view_y = sdl.viewport.y;
	info->view_w = sdl.viewport.w;
	info->view_h = sdl.viewport.h;
	info->fullscreen = gfx_is_fullscreen();
	info->sharp_3d = sharp.on && !sharp.broken;
	info->sharp_3d_shown = sharp.shown;
	if (sharp.shown) {
		info->sharp_3d_w = sharp.target.w;
		info->sharp_3d_h = sharp.target.h;
	}
}

// The window's size in points, out of full screen; the view is fitted again at the size event.
bool gfx_set_window_size(int w, int h)
{
	if (!sdl.window || w < 64 || h < 48 || w > 16384 || h > 16384)
		return false;
	if (gfx_is_fullscreen())
		gfx_set_fullscreen(false);
	SDL_RestoreWindow(sdl.window);
	SDL_SetWindowSize(sdl.window, w, h);
	gfx_update_screen_scale();
	return true;
}

// The viewport of the window's back buffer, as drawn, saved as a PNG (rows top first).
int gfx_save_window(const char *path)
{
	int w = sdl.viewport.w, h = sdl.viewport.h;
	if (w <= 0 || h <= 0)
		return 0;
	uint8_t *pixels = xmalloc((size_t)w * h * 4);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(sdl.viewport.x, sdl.viewport.y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, main_surface_fb);
	// The window's alpha is not the picture's: opaque, as shown (the writer writes RGBA).
	for (size_t i = 3; i < (size_t)w * h * 4; i += 4)
		pixels[i] = 255;
	uint8_t *row = xmalloc((size_t)w * 4);
	for (int y = 0; y < h / 2; y++) {
		uint8_t *a = pixels + (size_t)y * w * 4, *b = pixels + (size_t)(h - 1 - y) * w * 4;
		memcpy(row, a, (size_t)w * 4);
		memcpy(a, b, (size_t)w * 4);
		memcpy(b, row, (size_t)w * 4);
	}
	free(row);
	struct cg cg = {
		.type = ALCG_UNKNOWN,
		.metrics = {
			.w = w,
			.h = h,
			.bpp = 24,
			.has_pixel = true,
			.has_alpha = false,
			.pixel_pitch = w * 3,
			.alpha_pitch = 1
		},
		.pixels = pixels
	};
	FILE *fp = file_open_utf8(path, "wb");
	if (!fp) {
		WARNING("Failed to open %s: %s", display_utf0(path), strerror(errno));
		free(pixels);
		return 0;
	}
	int r = cg_write(&cg, ALCG_PNG, fp);
	fclose(fp);
	free(pixels);
	return r;
}

void gfx_swap(void)
{
	// A move to a monitor of another scale changes the drawable's pixels while the window keeps
	// its size in points, which may send no size event.
	int dw, dh;
	SDL_GL_GetDrawableSize(sdl.window, &dw, &dh);
	if (dw != drawable_w || dh != drawable_h)
		gfx_update_screen_scale();

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(sdl.viewport.x, sdl.viewport.y, sdl.viewport.w, sdl.viewport.h);
	gfx_clear();

	static mat4 wv_transform = MAT4(
		2,  0, 0, -1,
		0, -2, 0,  1,
		0,  0, 1,  0,
		0,  0, 0,  1);
	struct shader *present = &default_shader;
	GLint mag = GL_LINEAR, minf = GL_LINEAR;
	switch (scaling_type) {
	case GFX_SCALE_NEAREST:
		mag = GL_NEAREST;
		minf = GL_NEAREST;
		break;
	case GFX_SCALE_BICUBIC:
		present = &present_bicubic;
		break;
	case GFX_SCALE_LANCZOS:
		present = &present_lanczos;
		break;
	case GFX_SCALE_XBRZ:
		present = &present_xbrz;
		mag = GL_NEAREST;
		minf = GL_NEAREST;
		break;
	default:
		break;
	}
	glBindTexture(GL_TEXTURE_2D, view->handle);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minf);
	// The 3D at the window's size (2.67.0): the frame's two layers and the view's own picture.
	struct layered_shader *ls = sharp.armed && sharp.on && view == &main_surface
		? layered_for(scaling_type) : NULL;
	sharp.shown = ls != NULL;
	if (ls) {
		draw_layered(ls, sharp.target.handle, mag, mw_transform[0], wv_transform[0]);
	} else {
		struct gfx_render_job job = {
			.shader = present,
			.shape = GFX_RECTANGLE,
			.texture = view->handle,
			.world_transform = mw_transform[0],
			.view_transform = wv_transform[0],
			.data = view
		};
		gfx_render(&job);
	}
#ifdef RQ_MODLOADER
	rq_ui_before_swap();
#endif

	rq_prof_begin(RQ_PROF_SWAP);
	SDL_GL_SwapWindow(sdl.window);
	rq_prof_end(RQ_PROF_SWAP);
	glBindFramebuffer(GL_FRAMEBUFFER, main_surface_fb);
	glViewport(0, 0, sdl.w, sdl.h);

	gfx_update_frame_rate_counter();
	rq_prof_frame();
#ifdef RQ_MODLOADER
	rq_debug_on_present();
	rq_ui_on_present();
	rq_discord_tick();
#endif
}

void gfx_set_view(struct texture *t)
{
	view = t;
}

void gfx_reset_view(void)
{
	view = &main_surface;
}

/*
 * Set up mandatory shader arguments.
 */
void gfx_prepare_job(struct gfx_render_job *job)
{
	glUseProgram(job->shader->program);

	glUniformMatrix4fv(job->shader->world_transform, 1, GL_FALSE, job->world_transform);
	glUniformMatrix4fv(job->shader->view_transform, 1, GL_FALSE, job->view_transform);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, job->texture);
	glUniform1i(job->shader->texture, 0);

	if (job->shader->prepare)
		job->shader->prepare(job, job->data);
}

/*
 * Execute a render job set up previously with gfx_prepare_job.
 */
void gfx_run_job(struct gfx_render_job *job)
{
	glBindVertexArray(sdl.gl.vao);
	glEnableVertexAttribArray(job->shader->vertex_pos);
	glEnableVertexAttribArray(job->shader->vertex_uv);

	glBindBuffer(GL_ARRAY_BUFFER, job->shape == GFX_QUADRILATERAL ? sdl.gl.quad_vbo : sdl.gl.vbo);
	glVertexAttribPointer(job->shader->vertex_pos, 4, GL_FLOAT, GL_FALSE, sizeof(struct gfx_vertex), NULL);
	glVertexAttribPointer(job->shader->vertex_uv, 2, GL_FLOAT, GL_FALSE, sizeof(struct gfx_vertex), (void*)offsetof(struct gfx_vertex, u));

	switch (job->shape) {
	case GFX_RECTANGLE:
	case GFX_QUADRILATERAL:
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sdl.gl.rect_ibo);
		glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_INT, NULL);
#ifdef RQ_MODLOADER
		rq_debug_add_verts(4);
#endif
		break;
	case GFX_LINE:
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sdl.gl.line_ibo);
		glDrawElements(GL_LINES, 2, GL_UNSIGNED_INT, NULL);
#ifdef RQ_MODLOADER
		rq_debug_add_verts(2);
#endif
		break;
	}

	glDisableVertexAttribArray(job->shader->vertex_pos);
	glDisableVertexAttribArray(job->shader->vertex_uv);
	glBindVertexArray(0);
	glUseProgram(0);
}

void gfx_render(struct gfx_render_job *job)
{
	if (!job->shader) {
		job->shader = &default_shader;
		gfx_prepare_job(job);
		gfx_run_job(job);
		job->shader = NULL;
	} else {
		gfx_prepare_job(job);
		gfx_run_job(job);
	}
}

void _gfx_render_texture(struct shader *s, struct texture *t, Rectangle *r, void *data)
{
	if (!t->handle) {
		WARNING("Attempted to render uninitialized texture");
		return;
	}

	mat4 world_transform = WORLD_TRANSFORM(t->w, t->h, r ? r->x : 0, r ? r->y : 0);

	struct gfx_render_job job = {
		.shader = s,
		.shape = GFX_RECTANGLE,
		.texture = t->handle,
		.world_transform = world_transform[0],
		.view_transform = world_view_transform[0],
		.data = data
	};
	gfx_render(&job);
}

void gfx_render_texture(struct texture *t, Rectangle *r)
{
	_gfx_render_texture(&default_shader, t, r, t);
}

void gfx_render_quadrilateral(struct texture *t, struct gfx_vertex vertices[4])
{
	gfx_render_quadrilateral_with(&default_shader, t, vertices, t);
}

void gfx_render_quadrilateral_with(struct shader *s, struct texture *t, struct gfx_vertex vertices[4], void *data)
{
	if (!t->handle) {
		WARNING("Attempted to render uninitialized texture");
		return;
	}

	// Normalize the texture coordinates to [0, 1]
	struct gfx_vertex buf[4];
	memcpy(buf, vertices, sizeof(struct gfx_vertex) * 4);
	for (int i = 0; i < 4; i++) {
		buf[i].u /= t->w;
		buf[i].v /= t->h;
	}

	glBindBuffer(GL_ARRAY_BUFFER, sdl.gl.quad_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(buf), buf, GL_DYNAMIC_DRAW);

	mat4 world_transform = GLM_MAT4_IDENTITY_INIT;
	struct gfx_render_job job = {
		.shader = s,
		.shape = GFX_QUADRILATERAL,
		.texture = t->handle,
		.world_transform = world_transform[0],
		.view_transform = world_view_transform[0],
		.data = data
	};
	gfx_render(&job);
}

/*
 * Texture recycling.
 *
 * Creating a GL texture costs ~0.25 ms of driver time whatever its size, and
 * the UI (Party table, shops, quest list) creates and deletes hundreds of
 * small sprite textures in one frame. Deleted RGBA textures of up to
 * TEX_POOL_MAX_SIDE are kept here and handed back to a later request for the
 * same size, which only needs a glTexSubImage2D. Set XSYS4_NO_TEXPOOL to turn
 * it off.
 *
 * tex_flags is indexed by GL handle: TEXF_RGBA marks textures that are safe
 * to recycle (RGBA, default parameters), TEXF_FB_OK marks textures already
 * validated as a framebuffer attachment.
 */
#define TEX_POOL_MAX 2048
#define TEX_POOL_MAX_PX 12000000.0
#define TEX_POOL_MAX_SIDE 512
#define TEXF_RGBA 1
#define TEXF_FB_OK 2

static struct {
	GLuint handle;
	int w, h;
} tex_pool[TEX_POOL_MAX];
static int tex_pool_n;
static double tex_pool_px;
static uint8_t *tex_flags;
static size_t tex_flags_n;
static int tex_pool_on = -1;

static bool tex_pool_enabled(void)
{
	if (tex_pool_on < 0)
		tex_pool_on = getenv("XSYS4_NO_TEXPOOL") == NULL;
	return tex_pool_on;
}

static void tex_flags_set(GLuint handle, uint8_t flags)
{
	if (handle >= tex_flags_n) {
		size_t n = (size_t)handle + 1024;
		tex_flags = xrealloc(tex_flags, n);
		memset(tex_flags + tex_flags_n, 0, n - tex_flags_n);
		tex_flags_n = n;
	}
	tex_flags[handle] = flags;
}

static uint8_t tex_flags_get(GLuint handle)
{
	return handle < tex_flags_n ? tex_flags[handle] : 0;
}

static void tex_destroy(GLuint handle)
{
	if (blit_attached_tex == handle)
		blit_attached_tex = 0;
	tex_flags_set(handle, 0);
	glDeleteTextures(1, &handle);
}

// Take a texture of exactly w*h from the pool. Returns 0 if there is none.
static GLuint tex_pool_take(int w, int h)
{
	for (int i = tex_pool_n - 1; i >= 0; i--) {
		if (tex_pool[i].w != w || tex_pool[i].h != h)
			continue;
		GLuint handle = tex_pool[i].handle;
		memmove(&tex_pool[i], &tex_pool[i + 1], sizeof(tex_pool[0]) * (tex_pool_n - i - 1));
		tex_pool_n--;
		tex_pool_px -= (double)w * h;
		return handle;
	}
	return 0;
}

// Keep a texture for reuse. Returns false if it is not eligible (caller deletes it).
static bool tex_pool_put(struct texture *t)
{
	double px = (double)t->w * t->h;
	if (!tex_pool_enabled() || !(tex_flags_get(t->handle) & TEXF_RGBA))
		return false;
	if (t->w <= 0 || t->h <= 0 || t->w > TEX_POOL_MAX_SIDE || t->h > TEX_POOL_MAX_SIDE)
		return false;
	// drop the oldest entries to make room
	int drop = 0;
	double px_after = tex_pool_px + px;
	while ((tex_pool_n - drop >= TEX_POOL_MAX || px_after > TEX_POOL_MAX_PX) && drop < tex_pool_n) {
		px_after -= (double)tex_pool[drop].w * tex_pool[drop].h;
		tex_destroy(tex_pool[drop].handle);
		drop++;
	}
	if (drop) {
		memmove(&tex_pool[0], &tex_pool[drop], sizeof(tex_pool[0]) * (tex_pool_n - drop));
		tex_pool_n -= drop;
		tex_pool_px = px_after - px;
	}
	tex_pool[tex_pool_n].handle = t->handle;
	tex_pool[tex_pool_n].w = t->w;
	tex_pool[tex_pool_n].h = t->h;
	tex_pool_n++;
	tex_pool_px += px;
	return true;
}

static void init_texture(struct texture *t, int w, int h)
{
	glGenTextures(1, &t->handle);
	tex_flags_set(t->handle, 0);
	glBindTexture(GL_TEXTURE_2D, t->handle);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	t->w = w;
	t->h = h;
}

void gfx_init_texture_with_pixels(struct texture *t, int w, int h, void *pixels)
{
	GLuint recycled = tex_pool_enabled() ? tex_pool_take(w, h) : 0;
	if (recycled) {
		t->handle = recycled;
		t->w = w;
		t->h = h;
		glBindTexture(GL_TEXTURE_2D, recycled);
		if (pixels) {
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		} else {
			// a new blank texture must not show the previous owner's pixels
			GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, t, 0, 0, w, h);
			glClearColor(0, 0, 0, 0);
			glClear(GL_COLOR_BUFFER_BIT);
			gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
		}
		return;
	}
	init_texture(t, w, h);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	tex_flags_set(t->handle, TEXF_RGBA);
}

void gfx_update_texture_with_pixels(struct texture *t, void *pixels)
{
	glBindTexture(GL_TEXTURE_2D, t->handle);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->w, t->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

void gfx_init_texture_with_cg(struct texture *t, struct cg *cg)
{
	gfx_init_texture_with_pixels(t, cg->metrics.w, cg->metrics.h, cg->pixels);
}

void gfx_init_texture_rgba(struct texture *t, int w, int h, SDL_Color color)
{
	if (w > max_texture_size) {
		WARNING("Texture width %d exceeds maximum texture size %d", w, max_texture_size);
		w = max_texture_size;
	}
	if (h > max_texture_size) {
		WARNING("Texture height %d exceeds maximum texture size %d", h, max_texture_size);
		h = max_texture_size;
	}
	gfx_init_texture_blank(t, w, h);
	if (w <= 0 || h <= 0)
		return;
	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, t, 0, 0, w, h);
	glClearColor(color.r / 255.f, color.g / 255.f, color.b / 255.f, color.a / 255.f);
	glClear(GL_COLOR_BUFFER_BIT);
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
}

void gfx_init_texture_rgb(struct texture *t, int w, int h, SDL_Color color)
{
	if (w > max_texture_size) {
		WARNING("Texture width %d exceeds maximum texture size %d", w, max_texture_size);
		w = max_texture_size;
	}
	if (h > max_texture_size) {
		WARNING("Texture height %d exceeds maximum texture size %d", h, max_texture_size);
		h = max_texture_size;
	}
	init_texture(t, w, h);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	if (w <= 0 || h <= 0)
		return;
	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, t, 0, 0, w, h);
	glClearColor(color.r / 255.f, color.g / 255.f, color.b / 255.f, 255.f);
	glClear(GL_COLOR_BUFFER_BIT);
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
}

void gfx_init_texture_amap(struct texture *t, int w, int h, uint8_t *amap, SDL_Color color)
{
	uint32_t *pixels = xmalloc(sizeof(uint32_t) * w * h);
	for (int i = 0; i < w*h; i++) {
		uint32_t c = SDL_MapRGBA(sdl.format, color.r, color.g, color.b, amap[i]);
		pixels[i] = c;
	}

	gfx_init_texture_with_pixels(t, w, h, pixels);
	free(pixels);
}

void gfx_init_texture_rmap(struct texture *t, int w, int h, uint8_t *rmap)
{
	init_texture(t, w, h);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, rmap);
}

void gfx_init_texture_blank(struct texture *t, int w, int h)
{
	gfx_init_texture_with_pixels(t, w, h, NULL);
}

void gfx_copy_main_surface(struct texture *dst)
{
	init_texture(dst, main_surface.w, main_surface.h);
	glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, main_surface.w, main_surface.h, 0);
}

void gfx_delete_texture(struct texture *t)
{
	if (t->handle && !tex_pool_put(t))
		tex_destroy(t->handle);
	t->handle = 0;
}

GLuint gfx_set_framebuffer(GLenum target, Texture *t, int x, int y, int w, int h)
{
	if (!blit_fbo)
		glGenFramebuffers(1, &blit_fbo);

	glBindFramebuffer(target, blit_fbo);
	if (blit_attached_tex != t->handle) {
		glFramebufferTexture2D(target, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->handle, 0);
		// completeness is a property of the texture, so check it once
		uint8_t flags = tex_flags_get(t->handle);
		if (!(flags & TEXF_FB_OK)) {
			if (glCheckFramebufferStatus(target) != GL_FRAMEBUFFER_COMPLETE)
				ERROR("Incomplete framebuffer");
			tex_flags_set(t->handle, flags | TEXF_FB_OK);
		}
		blit_attached_tex = t->handle;
	}
	glViewport(x, y, w, h);
	return blit_fbo;
}

void gfx_reset_framebuffer(GLenum target, possibly_unused GLuint fbo)
{
	glBindFramebuffer(target, main_surface_fb);
	glViewport(0, 0, sdl.w, sdl.h);
}

SDL_Color gfx_get_pixel(Texture *t, int x, int y)
{
	GLuint fbo = gfx_set_framebuffer(GL_READ_FRAMEBUFFER, t, 0, 0, t->w, t->h);

	uint8_t pixel[4];
	glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);

	gfx_reset_framebuffer(GL_READ_FRAMEBUFFER, fbo);

	return (SDL_Color) {
		.r = pixel[0],
		.g = pixel[1],
		.b = pixel[2],
		.a = pixel[3],
	};
}

void *gfx_get_pixels(Texture *t)
{
	GLuint fbo = gfx_set_framebuffer(GL_READ_FRAMEBUFFER, t, 0, 0, t->w, t->h);
	void *pixels = xmalloc(t->w * t->h * 4);
	glReadPixels(0, 0, t->w, t->h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	gfx_reset_framebuffer(GL_READ_FRAMEBUFFER, fbo);
	return pixels;
}

int gfx_save_texture(Texture *t, const char *path, enum cg_type format)
{
	void *pixels = gfx_get_pixels(t);
	struct cg cg = {
		.type = ALCG_UNKNOWN,
		.metrics = {
			.w = t->w,
			.h = t->h,
			.bpp = 24,
			.has_pixel = true,
			.has_alpha = true,
			.pixel_pitch = t->w * 3,
			.alpha_pitch = 1
		},
		.pixels = pixels
	};
	FILE *fp = file_open_utf8(path, "wb");
	if (!fp) {
		WARNING("Failed to open %s: %s", display_utf0(path), strerror(errno));
		free(pixels);
		return 0;
	}
	int r = cg_write(&cg, format, fp);
	fclose(fp);
	free(pixels);
	return r;
}
