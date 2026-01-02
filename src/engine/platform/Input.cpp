#include "Input.hpp"
#include <GLFW/glfw3.h>

void Input::attach(GLFWwindow* win)
{
    window = win;
    glfwSetWindowUserPointer(window, this);
    glfwSetScrollCallback(window, &Input::onScroll);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
}

void Input::update()
{
    if (!window)
        return;

    double x, y;
    glfwGetCursorPos(window, &x, &y);

    if (firstMouse) {
        lastX = x;
        lastY = y;
        firstMouse = false;
    }

    mdx = x - lastX;
    mdy = y - lastY;
    lastX = x;
    lastY = y;
}

bool Input::keyDown(int glfwKey) const
{
    return window && glfwGetKey(window, glfwKey) == GLFW_PRESS;
}

bool Input::mouseDown(int glfwButton) const
{
    return window && glfwGetMouseButton(window, glfwButton) == GLFW_PRESS;
}

void Input::endFrame() noexcept
{
    sdy = 0.0;
}

void Input::onScroll(GLFWwindow* win, double, double yoff)
{
    auto* self = static_cast<Input*>(glfwGetWindowUserPointer(win));
    if (!self)
        return;

    self->sdy += yoff;
}