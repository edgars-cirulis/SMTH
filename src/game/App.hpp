#pragma once
#include "../engine/gfx/Renderer.hpp"
#include "../engine/gfx/VulkanContext.hpp"
#include "../engine/platform/Input.hpp"
#include "Camera.hpp"

class App {
   public:
    void run();

   private:
    void simulateFixed(float dt);
    void render(float alpha);

    VulkanContext vk;
    Renderer renderer;

    Input input;

    Camera simCamera;
    Camera renderCamera;
    Camera::State prevCam{};
    Camera::State currCam{};
};
