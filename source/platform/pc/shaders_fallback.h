/*
	Eingebettete GLSL-Shader als Fallback fuer den PC-Build.

	Der PC-Renderer laedt zur Laufzeit <texturepack>/vertex.shader und
	<texturepack>/fragment.shader. Fehlen diese Dateien, werden sie aus dem hier
	eingebetteten Inhalt erzeugt (auf Platte geschrieben) und verwendet, statt
	dass das Programm mit einem Assert abbricht.

	Inhalt muss mit assets/vertex.shader bzw. assets/fragment.shader
	uebereinstimmen. Bei Aenderungen an den Shader-Dateien hier nachziehen.
*/

#ifndef SHADERS_FALLBACK_H
#define SHADERS_FALLBACK_H

static const char* FALLBACK_VERTEX_SHADER =
	"#version 110\n"
	"\n"
	"uniform mat4 mv;\n"
	"uniform mat4 proj;\n"
	"uniform mat4 texm;\n"
	"uniform vec2 tex_scale;\n"
	"\n"
	"uniform bool enable_lighting;\n"
	"uniform float lighting[256];\n"
	"\n"
	"attribute vec3 a_pos;\n"
	"attribute vec4 a_color;\n"
	"attribute vec2 a_texcoord;\n"
	"attribute vec2 a_light;\n"
	"\n"
	"varying vec3 v_pos;\n"
	"varying vec4 v_color;\n"
	"varying vec2 v_texcoord;\n"
	"\n"
	"void main() {\n"
	"	if(enable_lighting) {\n"
	"		v_color = vec4(vec3(lighting[int(a_light.x) + int(a_light.y) * 16]), 1.0);\n"
	"	} else {\n"
	"		v_color = a_color;\n"
	"	}\n"
	"\n"
	"	v_pos = a_pos;\n"
	"	v_texcoord = (texm * vec4(a_texcoord * tex_scale, 0.0, 1.0)).xy;\n"
	"	gl_Position = proj * mv  * vec4(a_pos, 1.0);\n"
	"}\n";

static const char* FALLBACK_FRAGMENT_SHADER =
	"#version 110\n"
	"\n"
	"uniform sampler2D tex;\n"
	"uniform bool enable_texture;\n"
	"uniform bool enable_alpha;\n"
	"\n"
	"uniform bool enable_fog;\n"
	"uniform vec2 fog_delta;\n"
	"uniform float fog_distance;\n"
	"uniform vec3 fog_color;\n"
	"\n"
	"varying vec3 v_pos;\n"
	"varying vec4 v_color;\n"
	"varying vec2 v_texcoord;\n"
	"\n"
	"void main() {\n"
	"	vec4 tex_color = vec4(1.0);\n"
	"\n"
	"	if(enable_texture)\n"
	"		tex_color = texture2D(tex, v_texcoord);\n"
	"\n"
	"	float v_fog = 0.0;\n"
	"\n"
	"	if(enable_fog)\n"
	"		v_fog = clamp((length(fog_delta + v_pos.xz) - (fog_distance - 9.0)) / 8.0, 0.0, 1.0);\n"
	"\n"
	"	vec4 frag = v_color * tex_color;\n"
	"	gl_FragColor = vec4(mix(frag.rgb, fog_color, v_fog), frag.a);\n"
	"\n"
	"	if(enable_alpha && gl_FragColor.a < 0.0625)\n"
	"		discard;\n"
	"}\n";

#endif
