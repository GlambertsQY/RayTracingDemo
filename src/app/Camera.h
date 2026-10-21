#pragma once

#include <glm/glm.hpp>

class Camera
{
public:
    void reset(const glm::vec3& target, float radius);

    void orbit(float dx, float dy);
    void pan(float dx, float dy);
    void zoom(float amount);

    glm::vec3 position() const;
    glm::vec3 forward() const;
    glm::vec3 right() const;
    glm::vec3 up() const;

    glm::vec3 target{0.0f};
    float distance = 10.0f;
    float yaw = 0.0f;
    float pitch = 0.2f;
    float fovY = 0.9f;      // 弧度
    float aspect = 16.0f / 9.0f;
};
