#pragma once

#include "vk_types.h" 
#include <SDL_events.h>

class Camera {
public:
    glm::vec3 position;
    glm::vec3 velocity;

    float pitch = 0;
    float yaw = 0;

    void update(float dt);
    void processSDLEvent(SDL_Event& e);
    void processInput();

    glm::mat4 getViewMatrix();
    glm::mat4 getRotationMatrix();
    glm::mat4 getProjectionMatrix(float aspectRatio);
};