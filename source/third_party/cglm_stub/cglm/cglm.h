#ifndef CGLM_STUB_H
#define CGLM_STUB_H

#include <math.h>
#include <string.h>

#define GLM_PI 3.14159265358979323846f
#define GLM_PIf 3.14159265358979323846f
#define GLM_FLT_EPSILON 1.19209290e-07F

typedef float vec2[2];
typedef float vec3[3];
typedef float vec4[4];
typedef float mat4[4][4];
typedef int ivec2[2];
typedef int ivec3[3];

#define GLM_MAT4_IDENTITY_INIT \
	{ {1.0f, 0.0f, 0.0f, 0.0f}, \
	  {0.0f, 1.0f, 0.0f, 0.0f}, \
	  {0.0f, 0.0f, 1.0f, 0.0f}, \
	  {0.0f, 0.0f, 0.0f, 1.0f} }

static mat4 GLM_MAT4_IDENTITY = GLM_MAT4_IDENTITY_INIT;

static inline float glm_rad(float deg) { return deg * (GLM_PI / 180.0f); }
static inline float glm_deg(float rad) { return rad * (180.0f / GLM_PI); }
static inline float glm_clamp(float v, float min, float max) {
	return v < min ? min : (v > max ? max : v);
}
static inline float glm_pow2(float v) { return v * v; }

static inline void glm_vec2_zero(vec2 v) { v[0] = v[1] = 0.0f; }
static inline void glm_vec3_zero(vec3 v) { v[0] = v[1] = v[2] = 0.0f; }
static inline void glm_vec2_copy(const vec2 v, vec2 dst) { dst[0] = v[0]; dst[1] = v[1]; }
static inline void glm_vec3_copy(const vec3 v, vec3 dst) { dst[0] = v[0]; dst[1] = v[1]; dst[2] = v[2]; }
static inline void glm_vec3_add(const vec3 a, const vec3 b, vec3 dst) {
	dst[0] = a[0] + b[0]; dst[1] = a[1] + b[1]; dst[2] = a[2] + b[2];
}
static inline void glm_vec3_sub(const vec3 a, const vec3 b, vec3 dst) {
	dst[0] = a[0] - b[0]; dst[1] = a[1] - b[1]; dst[2] = a[2] - b[2];
}
static inline void glm_vec3_scale(const vec3 v, float s, vec3 dst) {
	dst[0] = v[0] * s; dst[1] = v[1] * s; dst[2] = v[2] * s;
}
static inline void glm_vec3_muladd(const vec3 a, const vec3 b, vec3 dst) {
	dst[0] += a[0] * b[0]; dst[1] += a[1] * b[1]; dst[2] += a[2] * b[2];
}
static inline void glm_vec3_crossn(const vec3 a, const vec3 b, vec3 dst) {
	dst[0] = a[1] * b[2] - a[2] * b[1];
	dst[1] = a[2] * b[0] - a[0] * b[2];
	dst[2] = a[0] * b[1] - a[1] * b[0];
}
static inline void glm_vec3_normalize(vec3 v) { (void)v; }
static inline float glm_vec3_dot(const vec3 a, const vec3 b) {
	return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}
static inline void glm_vec3_lerp(const vec3 a, const vec3 b, float t, vec3 dst) {
	dst[0] = a[0] + (b[0] - a[0]) * t;
	dst[1] = a[1] + (b[1] - a[1]) * t;
	dst[2] = a[2] + (b[2] - a[2]) * t;
}
static inline float glm_vec3_distance2(const vec3 a, const vec3 b) {
	float dx = a[0]-b[0], dy = a[1]-b[1], dz = a[2]-b[2];
	return dx*dx + dy*dy + dz*dz;
}
static inline float glm_vec2_distance2(const vec2 a, const vec2 b) {
	float dx = a[0]-b[0], dy = a[1]-b[1];
	return dx*dx + dy*dy;
}

static inline void glm_mat4_identity(mat4 m) {
	memset(m, 0, sizeof(mat4));
	m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
}

static inline void glm_translate_make(mat4 m, const vec3 v) {
	glm_mat4_identity(m);
	m[3][0] = v[0]; m[3][1] = v[1]; m[3][2] = v[2];
}
static inline void glm_translate(mat4 m, const vec3 v) {
	m[3][0] += v[0]; m[3][1] += v[1]; m[3][2] += v[2];
}
static inline void glm_translate_y(mat4 m, float y) { m[3][1] += y; }
static inline void glm_translate_to(mat4 m, const vec3 v, mat4 dst) {
	glm_mat4_identity(dst);
	dst[3][0] = v[0]; dst[3][1] = v[1]; dst[3][2] = v[2];
}
static inline void glm_rotate_x(mat4 m, float r, mat4 dst) { (void)m; (void)r; (void)dst; }
static inline void glm_rotate_y(mat4 m, float r, mat4 dst) { (void)m; (void)r; (void)dst; }
static inline void glm_rotate_z(mat4 m, float r, mat4 dst) { (void)m; (void)r; (void)dst; }
static inline void glm_scale(mat4 m, const vec3 v) { (void)m; (void)v; }
static inline void glm_scale_uni(mat4 m, float s) { (void)m; (void)s; }

static inline void glm_mat4_mul(const mat4 a, const mat4 b, mat4 dst) { (void)a; (void)b; glm_mat4_identity(dst); }
static inline void glm_mat4_transpose(mat4 m) { (void)m; }
static inline void glm_mat4_transpose_to(const mat4 m, mat4 dst) { (void)m; glm_mat4_identity(dst); }

static inline void glm_perspective(float fovy, float aspect, float near, float far, mat4 dst) {
	(void)fovy; (void)aspect; (void)near; (void)far; glm_mat4_identity(dst);
}
static inline void glm_ortho(float left, float right, float bottom, float top, float near, float far, mat4 dst) {
	(void)left; (void)right; (void)bottom; (void)top; (void)near; (void)far; glm_mat4_identity(dst);
}
static inline void glm_lookat(vec3 eye, vec3 center, vec3 up, mat4 dst) {
	(void)eye; (void)center; (void)up; glm_mat4_identity(dst);
}

static inline void glm_frustum_planes(const mat4 m, vec4 dest[6]) { (void)m; (void)dest; }
static inline int glm_aabb_frustum(vec3 box[2], vec4 frustum[6]) {
	(void)box; (void)frustum; return 1;
}

#endif
