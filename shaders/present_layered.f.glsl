/* Copyright (C) 2026 RQ_modding
 *
 * The 3D at the window's size: the game's picture with its 3D view drawn into the window's own
 * pixels. Compiled after one of the present filters (present_plain, present_bicubic,
 * present_lanczos, present_xbrz) with PRESENT_LAYERED defined, which gives present_sample.
 *
 * Every blend the game draws with is affine in the colour underneath, so with the 3D view's colour
 * X a pixel of the picture is P + T * X. tex holds the picture drawn with the 3D view black (P),
 * white_tex with it white (P + T), both at the game's size; hires_tex holds the 3D view at the
 * window's size, hires_rect its place in the picture (x0, y0, x1, y1 in texture coordinates).
 */

uniform sampler2D white_tex;
uniform sampler2D hires_tex;
uniform vec4 hires_rect;

void main()
{
	vec3 black = present_sample(tex, tex_coord).rgb;
	vec3 white = present_sample(white_tex, tex_coord).rgb;
	vec2 at = clamp((tex_coord - hires_rect.xy) / (hires_rect.zw - hires_rect.xy), 0.0, 1.0);
	vec3 x = texture(hires_tex, at).rgb;
	frag_color = vec4(black + (white - black) * x, 1.0);
}
