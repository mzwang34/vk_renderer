#include "vk_camera.h"
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <SDL.h>

void Camera::processSDLEvent(SDL_Event& e)
{
    if (e.type == SDL_MOUSEMOTION) {
        if (e.motion.state & SDL_BUTTON_MMASK) {
            yaw -= (float)e.motion.xrel / 200.f;
            pitch -= (float)e.motion.yrel / 200.f;
        }
    }
}

void Camera::processInput()
{
    const Uint8* keystate = SDL_GetKeyboardState(nullptr);
    velocity = glm::vec3(0.f);
    if (keystate[SDL_SCANCODE_W]) { velocity.z -= 1.0f; }
    if (keystate[SDL_SCANCODE_S]) { velocity.z += 1.0f; }
    if (keystate[SDL_SCANCODE_A]) { velocity.x -= 1.0f; }
    if (keystate[SDL_SCANCODE_D]) { velocity.x += 1.0f; }
}

void Camera::update(float dt)
{
    glm::mat4 cameraRotation = getRotationMatrix();
    glm::vec3 translation = glm::vec3(cameraRotation * glm::vec4(velocity, 0.f));
    position += translation * dt * 5.0f; 
}

glm::mat4 Camera::getViewMatrix()
{
    glm::mat4 cameraTranslation = glm::translate(glm::mat4(1.f), position);
    glm::mat4 cameraRotation = getRotationMatrix();
    return glm::inverse(cameraTranslation * cameraRotation);
}

glm::mat4 Camera::getRotationMatrix()
{
    glm::quat pitchRotation = glm::angleAxis(pitch, glm::vec3(1.f, 0.f, 0.f));
    glm::quat yawRotation = glm::angleAxis(yaw, glm::vec3(0.f, 1.f, 0.f));

    return glm::toMat4(yawRotation) * glm::toMat4(pitchRotation);
}

glm::mat4 Camera::getProjectionMatrix(float aspectRatio)
{
    glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(70.f), aspectRatio, 0.1f, 10000.0f); // depth 0-1
    proj[1][1] *= -1;

    return proj;
}