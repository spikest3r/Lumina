#pragma once
#include "engine_types.h"

class ICharacterController : public GameObject {
public:
    virtual ~ICharacterController() {}
    virtual void Move(Vector3 direction, float speed, float dt) = 0;
    virtual void Jump(float force) = 0;
    virtual Vector3 getPosition() const = 0;
    virtual void setPosition(Vector3 position) = 0;
    virtual float getVerticalVelocity() = 0;
};