#include "App.hpp"
#include "../engine/render/RenderScene.hpp"

#include <algorithm>
#include <chrono>

void App::run()
{
    vk.initWindow(1280, 720, "Cotton Strike: Offensive Sox");
    input.attach(vk.window());
    vk.initVulkan();

    renderer.init(vk);

    simCamera.setPosition({ 0.0f, 1.7f, 5.0f });
    simCamera.setYawPitch(3.14159f, 0.0f);
    renderCamera = simCamera;
    prevCam = simCamera.state();
    currCam = prevCam;

    constexpr float kFixedDt = 1.0f / 128.0f;
    constexpr int kMaxSimStepsPerFrame = 8;
    float accumulator = 0.0f;

    double pendingMouseDx = 0.0;
    double pendingMouseDy = 0.0;
    double pendingScrollDy = 0.0;

    auto t0 = std::chrono::high_resolution_clock::now();
    while (!vk.shouldClose()) {
        vk.pollEvents();

        auto t1 = std::chrono::high_resolution_clock::now();
        float frameDt = std::chrono::duration<float>(t1 - t0).count();
        t0 = t1;
        frameDt = std::min(frameDt, 0.25f);

        input.update();
        pendingMouseDx += input.mouseDx();
        pendingMouseDy += input.mouseDy();
        pendingScrollDy += input.scrollDy();

        accumulator += frameDt;
        int steps = static_cast<int>(accumulator / kFixedDt);
        steps = std::min(steps, kMaxSimStepsPerFrame);

        if (steps > 0) {
            for (int i = 0; i < steps; ++i) {
                if (i == 0) {
                    input.setMouseDelta(pendingMouseDx, pendingMouseDy);
                    input.setScrollDelta(pendingScrollDy);
                    pendingMouseDx = 0.0;
                    pendingMouseDy = 0.0;
                    pendingScrollDy = 0.0;
                } else {
                    input.setMouseDelta(0.0, 0.0);
                    input.setScrollDelta(0.0);
                }

                prevCam = currCam;
                simulateFixed(kFixedDt);
                currCam = simCamera.state();

                accumulator -= kFixedDt;
            }
        }

        input.setMouseDelta(0.0, 0.0);
        input.setScrollDelta(0.0);
        input.endFrame();

        const float alpha = std::clamp(accumulator / kFixedDt, 0.0f, 1.0f);
        render(alpha);
    }

    vk.deviceWaitIdle();
    renderer.shutdown(vk);
    vk.shutdown();
}

void App::simulateFixed(float dt)
{
    simCamera.updateFPS(input, dt);
}

void App::render(float alpha)
{
    if (vk.swapchainRebuildRequested()) {
        vk.recreateSwapchain();
    }

    renderCamera.setState(Camera::lerp(prevCam, currCam, alpha));

    VkExtent2D ext = vk.swapchainExtent();
    const float aspect = (ext.height > 0) ? ((float)ext.width / (float)ext.height) : 1.0f;

    RenderScene scene{};
    scene.camera.view = renderCamera.viewMatrix();
    scene.camera.proj = renderCamera.projMatrix(aspect);
    scene.camera.position = renderCamera.position();
    scene.camera.forward = renderCamera.forward();
    scene.camera.right = renderCamera.right();
    scene.camera.up = renderCamera.up();
    scene.camera.fovRadians = renderCamera.fovRadians();
    scene.camera.aspect = aspect;

    scene.sun.direction = glm::normalize(glm::vec3(0.35f, 0.85f, 0.15f));
    scene.sun.intensity = 6.0f;
    scene.sun.color = glm::vec3(1.0f, 0.98f, 0.92f);

    scene.exposure = 1.0f;
    scene.timeSeconds = (float)glfwGetTime();

    scene.transforms.push_back(glm::mat4(1.0f));
    DrawItem d{};
    d.meshId = 0;
    d.materialId = 0;
    d.transformIndex = 0;
    d.baseColorFactor = glm::vec4(1.0f);
    d.metallicRoughnessFactor = glm::vec2(1.0f, 1.0f);
    scene.draws.push_back(d);

    renderer.drawFrame(vk, scene);
}
