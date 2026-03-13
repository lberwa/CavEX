#include "GLFW/glfw3.h"

struct GLFWwindow {
	int unused;
};

int glfwInit(void) { return GLFW_TRUE; }
void glfwWindowHint(int hint, int value) { (void)hint; (void)value; }
GLFWwindow* glfwCreateWindow(int width, int height, const char* title,
                             void* monitor, void* share) {
	(void)width; (void)height; (void)title; (void)monitor; (void)share;
	return (GLFWwindow*)0x1; // non-NULL dummy
}
void glfwMakeContextCurrent(GLFWwindow* window) { (void)window; }
void glfwSetFramebufferSizeCallback(GLFWwindow* window,
                                    void (*cb)(GLFWwindow*, int, int)) {
	(void)window; (void)cb;
}
int glfwRawMouseMotionSupported(void) { return GLFW_FALSE; }
void glfwSetInputMode(GLFWwindow* window, int mode, int value) {
	(void)window; (void)mode; (void)value;
}
int glfwGetInputMode(GLFWwindow* window, int mode) {
	(void)window; (void)mode; return GLFW_CURSOR_NORMAL;
}
void glfwSetCursorPos(GLFWwindow* window, double xpos, double ypos) {
	(void)window; (void)xpos; (void)ypos;
}
void glfwGetCursorPos(GLFWwindow* window, double* xpos, double* ypos) {
	if(xpos) *xpos = 0.0;
	if(ypos) *ypos = 0.0;
}
int glfwGetKey(GLFWwindow* window, int key) { (void)window; (void)key; return GLFW_RELEASE; }
int glfwGetMouseButton(GLFWwindow* window, int button) { (void)window; (void)button; return GLFW_RELEASE; }
void glfwSwapBuffers(GLFWwindow* window) { (void)window; }
void glfwPollEvents(void) {}
