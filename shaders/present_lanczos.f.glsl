/* Copyright (C) 2026 RQ_modding
 *
 * Lanczos-3 present filter (extra mode, labeled "lanczos").
 */

uniform sampler2D tex;
uniform vec2 tex_size;

in vec2 tex_coord;
out vec4 frag_color;

const float PI = 3.14159265358979323846;

float sinc(float x)
{
	if (abs(x) < 0.0001)
		return 1.0;
	float px = PI * x;
	return sin(px) / px;
}

float lanczos(float x)
{
	if (abs(x) >= 3.0)
		return 0.0;
	return sinc(x) * sinc(x / 3.0);
}

// The filtered colour of src at coord (RQ MOD 2.67.0: a function, so the 3D at the window's size
// filters two pictures the same way; see present_layered.f.glsl).
vec4 present_sample(sampler2D src, vec2 coord)
{
	vec2 pixel = coord * tex_size - vec2(0.5);
	vec2 f = fract(pixel);
	vec2 base = floor(pixel) - vec2(2.0);
	vec2 texel = 1.0 / tex_size;
	vec4 color = vec4(0.0);
	float wsum = 0.0;
	for (int j = 0; j < 6; j++) {
		float wy = lanczos(float(j) - 2.0 - f.y);
		for (int i = 0; i < 6; i++) {
			float w = lanczos(float(i) - 2.0 - f.x) * wy;
			vec2 uv = (base + vec2(float(i), float(j)) + vec2(0.5)) * texel;
			color += texture(src, uv) * w;
			wsum += w;
		}
	}
	return color / max(wsum, 0.0001);
}

#ifndef PRESENT_LAYERED
void main()
{
	frag_color = present_sample(tex, tex_coord);
}
#endif
