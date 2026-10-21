#include "app/Camera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

void Camera::reset(const glm::vec3& newTarget, float radius)
{
    target = newTarget;
    distance = std::max(radius * 1.6f, 0.5f);
    yaw = 0.0f;
    pitch = 0.25f;
    fovY = 0.9f;
}

void Camera::orbit(float dx, float dy)
{
    yaw -= dx * 0.006f;
    pitch = std::clamp(pitch + dy * 0.006f, -1.45f, 1.45f);
}

void Camera::pan(float dx, float dy)
{
    target += right() * (-dx * distance * 0.0015f);
    target += up() * (dy * distance * 0.0015f);
}

void Camera::zoom(float amount)
{
    distance = std::clamp(distance * std::exp(-amount * 0.12f), 0.1f, 5000.0f);
}

glm::vec3 Camera::position() const
{
    const glm::vec3 dir(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
    return target + dir * distance;
}

glm::vec3 Camera::forward() const
{
    return glm::normalize(target - position());
}

glm::vec3 Camera::right() const
{
    return glm::normalize(glm::cross(forward(), glm::vec3(0.0f, 1.0f, 0.0f)));
}

glm::vec3 Camera::up() const
{
    return glm::normalize(glm::cross(right(), forward()));
}
