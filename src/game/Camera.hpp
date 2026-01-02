#pragma once

#include "engine/platform/Input.hpp"

#include <glm/glm.hpp>

#include <cmath>

class Camera {
   public:
    struct State {
        glm::vec3 pos{ 0.0f };
        float yaw = 0.0f;
        float pitch = 0.0f;
    };

    void setPosition(const glm::vec3& p) { pos = p; }
    void setYawPitch(float yRadians, float pRadians)
    {
        yaw = yRadians;
        pitch = pRadians;
    }

    const glm::vec3& position() const { return pos; }

    State state() const { return State{ .pos = pos, .yaw = yaw, .pitch = pitch }; }
    void setState(const State& s)
    {
        pos = s.pos;
        yaw = s.yaw;
        pitch = s.pitch;
    }

    static State lerp(const State& a, const State& b, float t)
    {
        State out;
        out.pos = glm::mix(a.pos, b.pos, t);

        const float twoPi = 6.283185307179586f;
        auto angleLerp = [&](float aRad, float bRad) {
            float delta = std::remainder(bRad - aRad, twoPi);
            return aRad + delta * t;
        };

        out.yaw = angleLerp(a.yaw, b.yaw);
        out.pitch = angleLerp(a.pitch, b.pitch);
        return out;
    }

    glm::mat4 viewMatrix() const;
    glm::mat4 projMatrix(float aspect) const;

    glm::vec3 forward() const;
    glm::vec3 right() const;
    glm::vec3 up() const;
    float fovRadians() const;

    void updateFPS(const Input& in, float dt);

   private:
    glm::vec3 pos{ 0.0f };
    float yaw = 0.0f;
    float pitch = 0.0f;
    float fovY = 70.0f;

    float moveSpeed = 5.0f;
    float mouseSens = 0.0025f;
};
