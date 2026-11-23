/* Copyright (C) 2026 RQ_modding
 *
 * The plain present (Standard, bilinear, and Nearest, by the texture's filter) as a function, for
 * the 3D at the window's size (present_layered.f.glsl), which filters two pictures the same way.
 */

uniform sampler2D tex;

in vec2 tex_coord;
out vec4 frag_color;

vec4 present_sample(sampler2D src, vec2 coord)
{
	return texture(src, coord);
}
