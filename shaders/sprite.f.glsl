/* Copyright (C) 2023 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

uniform sampler2D tex;
uniform float blend_rate;
uniform vec3 multiply_color;
// RQ MOD (2.67.0): 1 or 2 draws the texture's colour as black or white, its alpha kept, for the
// 3D at the window's size (the scene drawn with the 3D sprite black, then white); 0, the default
// of every program that does not set it, is off.
uniform int layer_mask;

in vec2 tex_coord;
out vec4 frag_color;

void main() {
	frag_color = texture(tex, tex_coord);
	if (layer_mask != 0)
		frag_color.rgb = vec3(float(layer_mask - 1));
	frag_color.rgb *= multiply_color;
	frag_color.a *= blend_rate;
}
