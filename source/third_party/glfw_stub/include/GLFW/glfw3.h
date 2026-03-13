#ifndef GLFW3_H
#define GLFW3_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#define GLFW_TRUE 1
#define GLFW_FALSE 0

#define GLFW_PRESS 1
#define GLFW_RELEASE 0

#define GLFW_CURSOR 0x00033001
#define GLFW_CURSOR_NORMAL 0x00034001
#define GLFW_CURSOR_DISABLED 0x00034003

#define GLFW_RAW_MOUSE_MOTION 0x00033005
#define GLFW_STICKY_KEYS 0x00033002

#define GLFW_CONTEXT_VERSION_MAJOR 0x00022002
#define GLFW_CONTEXT_VERSION_MINOR 0x00022003

typedef struct GLFWwindow GLFWwindow;

int glfwInit(void);
void glfwWindowHint(int hint, int value);
GLFWwindow* glfwCreateWindow(int width, int height, const char* title,
                             void* monitor, void* share);
void glfwMakeContextCurrent(GLFWwindow* window);
void glfwSetFramebufferSizeCallback(GLFWwindow* window,
                                    void (*cb)(GLFWwindow*, int, int));
int glfwRawMouseMotionSupported(void);
void glfwSetInputMode(GLFWwindow* window, int mode, int value);
int glfwGetInputMode(GLFWwindow* window, int mode);
void glfwSetCursorPos(GLFWwindow* window, double xpos, double ypos);
void glfwGetCursorPos(GLFWwindow* window, double* xpos, double* ypos);
int glfwGetKey(GLFWwindow* window, int key);
int glfwGetMouseButton(GLFWwindow* window, int button);
void glfwSwapBuffers(GLFWwindow* window);
void glfwPollEvents(void);

#ifdef __cplusplus
}
#endif

#endif
