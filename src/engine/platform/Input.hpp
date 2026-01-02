#pragma once

struct GLFWwindow;

class Input {
   public:
    void attach(GLFWwindow* win);
    void update();
    void endFrame() noexcept;

    bool keyDown(int glfwKey) const;
    bool mouseDown(int glfwButton) const;

    double mouseDx() const { return mdx; }
    double mouseDy() const { return mdy; }

    double scrollDy() const noexcept { return sdy; }

    void setMouseDelta(double dx, double dy) noexcept
    {
        mdx = dx;
        mdy = dy;
    }

    void setScrollDelta(double dy) noexcept { sdy = dy; }

   private:
    GLFWwindow* window = nullptr;

    double lastX = 0.0, lastY = 0.0;
    double mdx = 0.0, mdy = 0.0;
    double sdy = 0.0;
    bool firstMouse = true;

    static void onScroll(GLFWwindow* win, double xoff, double yoff);
};
