#ifndef GLEW_H
#define GLEW_H

#include <GL/gl.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GLEW_OK 0

static inline GLenum glewInit(void) {
	return GLEW_OK;
}

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH 0x8B84
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER 0x8D41
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT 0x8D00
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif

#ifndef GLsizeiptr
typedef ptrdiff_t GLsizeiptr;
#endif
#ifndef GLintptr
typedef ptrdiff_t GLintptr;
#endif

static inline void glGenBuffers(GLsizei n, GLuint* buffers) {
	(void)n; (void)buffers;
}
static inline void glDeleteBuffers(GLsizei n, const GLuint* buffers) {
	(void)n; (void)buffers;
}
static inline void glBindBuffer(GLenum target, GLuint buffer) {
	(void)target; (void)buffer;
}
static inline void glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
	(void)target; (void)size; (void)data; (void)usage;
}
static inline void glEnableVertexAttribArray(GLuint index) { (void)index; }
static inline void glDisableVertexAttribArray(GLuint index) { (void)index; }
static inline void glVertexAttribPointer(GLuint index, GLint size, GLenum type,
										 GLboolean normalized, GLsizei stride,
										 const void* pointer) {
	(void)index; (void)size; (void)type; (void)normalized; (void)stride; (void)pointer;
}

static inline void glGetShaderiv(GLuint shader, GLenum pname, GLint* params) {
	(void)shader; (void)pname; if(params) *params = 1;
}
static inline void glGetShaderInfoLog(GLuint shader, GLsizei maxLength, GLsizei* length, GLchar* infoLog) {
	(void)shader; if(length) *length = 0; if(infoLog && maxLength > 0) infoLog[0] = 0;
}
static inline void glDeleteShader(GLuint shader) { (void)shader; }
static inline GLuint glCreateShader(GLenum type) { (void)type; return 1; }
static inline void glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length) {
	(void)shader; (void)count; (void)string; (void)length;
}
static inline void glCompileShader(GLuint shader) { (void)shader; }
static inline GLuint glCreateProgram(void) { return 1; }
static inline void glAttachShader(GLuint program, GLuint shader) { (void)program; (void)shader; }
static inline void glBindAttribLocation(GLuint program, GLuint index, const GLchar* name) {
	(void)program; (void)index; (void)name;
}
static inline void glLinkProgram(GLuint program) { (void)program; }
static inline void glUseProgram(GLuint program) { (void)program; }
static inline GLint glGetUniformLocation(GLuint program, const GLchar* name) { (void)program; (void)name; return 0; }
static inline void glUniform1i(GLint location, GLint v0) { (void)location; (void)v0; }
static inline void glUniform1f(GLint location, GLfloat v0) { (void)location; (void)v0; }
static inline void glUniform2f(GLint location, GLfloat v0, GLfloat v1) { (void)location; (void)v0; (void)v1; }
static inline void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) { (void)location; (void)v0; (void)v1; (void)v2; }
static inline void glUniform1fv(GLint location, GLsizei count, const GLfloat* value) { (void)location; (void)count; (void)value; }
static inline void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) {
	(void)location; (void)count; (void)transpose; (void)value;
}
static inline void glGenRenderbuffers(GLsizei n, GLuint* renderbuffers) { (void)n; (void)renderbuffers; }
static inline void glBindRenderbuffer(GLenum target, GLuint renderbuffer) { (void)target; (void)renderbuffer; }
static inline void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) {
	(void)target; (void)internalformat; (void)width; (void)height;
}
static inline void glGenFramebuffers(GLsizei n, GLuint* framebuffers) { (void)n; (void)framebuffers; }
static inline void glBindFramebuffer(GLenum target, GLuint framebuffer) { (void)target; (void)framebuffer; }
static inline void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
	(void)target; (void)attachment; (void)textarget; (void)texture; (void)level;
}
static inline void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) {
	(void)target; (void)attachment; (void)renderbuffertarget; (void)renderbuffer;
}
static inline GLenum glCheckFramebufferStatus(GLenum target) { (void)target; return GL_FRAMEBUFFER_COMPLETE; }
static inline void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
									 GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
									 GLbitfield mask, GLenum filter) {
	(void)srcX0; (void)srcY0; (void)srcX1; (void)srcY1;
	(void)dstX0; (void)dstY0; (void)dstX1; (void)dstY1;
	(void)mask; (void)filter;
}

#ifdef __cplusplus
}
#endif

#endif
