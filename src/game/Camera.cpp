#include "Camera.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

glm::vec3 Camera::forward() const
{
    return glm::normalize(glm::vec3{ std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw) });
}

glm::vec3 Camera::right() const
{
    return glm::normalize(glm::cross(forward(), glm::vec3{ 0.0f, 1.0f, 0.0f }));
}

glm::vec3 Camera::up() const
{
    const glm::vec3 r = right();
    const glm::vec3 f = forward();
    return glm::normalize(glm::cross(r, f));
}

float Camera::fovRadians() const
{
    return glm::radians(fovY);
}

glm::mat4 Camera::viewMatrix() const
{
    return glm::lookAt(pos, pos + forward(), glm::vec3{ 0.0f, 1.0f, 0.0f });
}

glm::mat4 Camera::projMatrix(float aspect) const
{
    glm::mat4 p = glm::perspective(glm::radians(fovY), aspect, 0.1f, 2000.0f);
    p[1][1] *= -1.0f;
    return p;
}

void Camera::updateFPS(const Input& in, float dt)
{
    yaw += static_cast<float>(in.mouseDx()) * mouseSens;
    pitch -= static_cast<float>(in.mouseDy()) * mouseSens;
    pitch = std::clamp(pitch, -1.55f, 1.55f);

    glm::vec3 f{ std::cos(pitch) * std::cos(yaw), 0.0f, std::cos(pitch) * std::sin(yaw) };
    f = glm::normalize(f);
    const glm::vec3 r = glm::normalize(glm::cross(f, glm::vec3{ 0.0f, 1.0f, 0.0f }));

    float spd = moveSpeed;
    if (in.keyDown(GLFW_KEY_LEFT_SHIFT))
        spd *= 200.0f;

    if (in.keyDown(GLFW_KEY_W))
        pos += f * (spd * dt);
    if (in.keyDown(GLFW_KEY_S))
        pos -= f * (spd * dt);
    if (in.keyDown(GLFW_KEY_D))
        pos += r * (spd * dt);
    if (in.keyDown(GLFW_KEY_A))
        pos -= r * (spd * dt);

    const glm::vec3 upW{ 0.0f, 1.0f, 0.0f };
    if (in.keyDown(GLFW_KEY_UP) || in.keyDown(GLFW_KEY_SPACE))
        pos += upW * (spd * dt);
    if (in.keyDown(GLFW_KEY_DOWN) || in.keyDown(GLFW_KEY_LEFT_CONTROL))
        pos -= upW * (spd * dt);

    if (in.scrollDy() != 0.0)
        pos += upW * (float)(in.scrollDy() * (spd * 0.25f));
}
