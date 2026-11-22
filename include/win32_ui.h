#ifndef RQ_WIN32_UI_H
#define RQ_WIN32_UI_H

struct SDL_Window;

void win32_ui_init(struct SDL_Window *window);
/* The title bar's menu marks this filter (0 to 4, as gfx_scaling_name) as the one in use. */
void win32_ui_mark_scaling(int type);

#endif
