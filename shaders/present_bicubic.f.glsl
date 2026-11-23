/* Copyright (C) 2026 RQ_modding
 *
 * Catmull-Rom bicubic present filter (vanilla "Bicubic" / バイキュービック).
 */

uniform sampler2D tex;
uniform vec2 tex_size;

in vec2 tex_coord;
out vec4 frag_color;

float cubic(float x)
{
	float ax = abs(x);
	if (ax <= 1.0)
		return 1.5 * ax * ax * ax - 2.5 * ax * ax + 1.0;
	if (ax < 2.0)
		return -0.5 * ax * ax * ax + 2.5 * ax * ax - 4.0 * ax + 2.0;
	return 0.0;
}

// The filtered colour of src at coord (RQ MOD 2.67.0: a function, so the 3D at the window's size
// filters two pictures the same way; see present_layered.f.glsl).
vec4 present_sample(sampler2D src, vec2 coord)
{
	vec2 pixel = coord * tex_size - vec2(0.5);
	vec2 f = fract(pixel);
	vec2 base = floor(pixel) - vec2(1.0);
	vec2 texel = 1.0 / tex_size;
	vec4 color = vec4(0.0);
	for (int j = 0; j < 4; j++) {
		float wy = cubic(float(j) - 1.0 - f.y);
		for (int i = 0; i < 4; i++) {
			float wx = cubic(float(i) - 1.0 - f.x);
			vec2 uv = (base + vec2(float(i), float(j)) + vec2(0.5)) * texel;
			color += texture(src, uv) * wx * wy;
		}
	}
	return color;
}

#ifndef PRESENT_LAYERED
void main()
{
	frag_color = present_sample(tex, tex_coord);
}
#endif
