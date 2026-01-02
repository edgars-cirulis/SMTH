#pragma once
#include <array>
#include <glm/glm.hpp>

struct FrustumPlanes {
    std::array<glm::vec4, 6> p{};
};

inline FrustumPlanes makeFrustumPlanes(const glm::mat4& viewProj)
{
    FrustumPlanes f{};
    const glm::mat4& m = viewProj;

    f.p[0] = glm::vec4(m[0][3] + m[0][0], m[1][3] + m[1][0], m[2][3] + m[2][0], m[3][3] + m[3][0]);  // left
    f.p[1] = glm::vec4(m[0][3] - m[0][0], m[1][3] - m[1][0], m[2][3] - m[2][0], m[3][3] - m[3][0]);  // right
    f.p[2] = glm::vec4(m[0][3] + m[0][1], m[1][3] + m[1][1], m[2][3] + m[2][1], m[3][3] + m[3][1]);  // bottom
    f.p[3] = glm::vec4(m[0][3] - m[0][1], m[1][3] - m[1][1], m[2][3] - m[2][1], m[3][3] - m[3][1]);  // top
    f.p[4] = glm::vec4(m[0][3] + m[0][2], m[1][3] + m[1][2], m[2][3] + m[2][2], m[3][3] + m[3][2]);  // near
    f.p[5] = glm::vec4(m[0][3] - m[0][2], m[1][3] - m[1][2], m[2][3] - m[2][2], m[3][3] - m[3][2]);  // far

    for (auto& pl : f.p) {
        const glm::vec3 n{ pl.x, pl.y, pl.z };
        const float len = glm::length(n);
        if (len > 0.0f)
            pl /= len;
    }
    return f;
}

inline bool frustumIntersectsAABB(const FrustumPlanes& f, const glm::vec3& bmin, const glm::vec3& bmax)
{
    for (const auto& pl : f.p) {
        const glm::vec3 n{ pl.x, pl.y, pl.z };
        glm::vec3 p = bmin;
        if (n.x >= 0)
            p.x = bmax.x;
        if (n.y >= 0)
            p.y = bmax.y;
        if (n.z >= 0)
            p.z = bmax.z;
        if (glm::dot(n, p) + pl.w < 0.0f)
            return false;
    }
    return true;
}

inline void transformAABB(const glm::mat4& m, const glm::vec3& inMin, const glm::vec3& inMax, glm::vec3& outMin, glm::vec3& outMax)
{
    glm::vec3 corners[8] = {
        { inMin.x, inMin.y, inMin.z }, { inMax.x, inMin.y, inMin.z }, { inMin.x, inMax.y, inMin.z }, { inMax.x, inMax.y, inMin.z },
        { inMin.x, inMin.y, inMax.z }, { inMax.x, inMin.y, inMax.z }, { inMin.x, inMax.y, inMax.z }, { inMax.x, inMax.y, inMax.z },
    };
    outMin = glm::vec3(m * glm::vec4(corners[0], 1.0f));
    outMax = outMin;
    for (int i = 1; i < 8; i++) {
        glm::vec3 p = glm::vec3(m * glm::vec4(corners[i], 1.0f));
        outMin = glm::min(outMin, p);
        outMax = glm::max(outMax, p);
    }
}
