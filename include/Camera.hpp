#ifndef CAMERA_HPP
#define CAMERA_HPP

#include "constants.hpp"
#include "types.hpp"

struct Camera {
    // 1. View matrix (camera -> world)
    glm::mat4 viewMatrix = glm::mat4(1.0f);

    // 2. Projection matrix (perspective)
    glm::mat4 projectionMatrix = glm::mat4(1.0f);

    // 3. High precision position of the camera in world space
    glm::dvec3 position = glm::vec3(0.0);

    // 4. Rotation of the camera in floats to avoid errors // Stored in radians. Multiply by 180/PI to get degrees
	glm::vec3 rotation = glm::vec3(0.0f); // yaw(x), pitch(y), roll(z)

    glm::dvec3 getForwardVectorMovement() const {
        float cp = std::cos(rotation.y);
        return glm::dvec3{
            cp * sin(rotation.x),
            0,  
            -cp * cos(rotation.x)   // NEGATE Z
        };
	}
    glm::dvec3 getForwardVector() const {
        float cp = std::cos(rotation.y);
        return glm::dvec3{
            cp * sin(rotation.x),
            sin(rotation.y),
            -cp * cos(rotation.x)   // NEGATE Z
        };
    }


    // 5. Clipping planes
    float nearClip = 0.1f;
    float farClip = 1000.0f;

    // 6. Field of view (vertical, in degrees)
    float FOV = 90.0f;  // used by projectToScreen(...)

	float sensitivity = 0.05f; // mouse sensitivity multiplier

	// 7. Movement speed
	float movementSpeed = 5.0f; // can move ... units per second on its own
	glm::vec3 velocity = glm::vec3(0.0f, 0.0f, 0.0f); // current velocity vector (stores outside forces that move it)
	// May be used for acceleration, gravity, etc.

	float BLOCK_ACTION_DELAY = 0.2f; // seconds between block actions
	float blockBreakCooldown = 0.0f; // current breaking  cooldown timer
	float blockPlaceCooldown = 0.0f; // current placing cooldown timer


	uint8_t blockPlaceDistanceX2 = 50; // max distance to place blocks
	// Translates to 5.0 units (blockPlaceDistanceX2 >> 1)

    bool onGround = false;
    bool isFlying = true; // Start in flying mode by default
    glm::vec3 dimensions = glm::vec3(PLAYER_WIDTH, PLAYER_HEIGHT, PLAYER_WIDTH);


	// Update VIEW and PROJECTION matrices
    void updateMatrices(int windowWidth, int windowHeight)
    {
        // 1. Rebuild VIEW from yaw, pitch, position
        float cp = std::cos(rotation.y);
        glm::dvec3 forward{
            cp * sin(rotation.x),
            sin(rotation.y),
            -cp * cos(rotation.x)   // NEGATE Z     
        };

        glm::dvec3 worldUp{ 0.0, 1.0, 0.0 };  // Explicit floating-point literals

        glm::dvec3 right = glm::normalize(glm::cross(forward, worldUp));
        glm::dvec3 up = glm::normalize(glm::cross(right, forward));

        viewMatrix = glm::lookAt(
            position,
            position + forward,
            up
        );

        // 2. Rebuild PROJECTION (only if FOV or window size changes)
        float aspect = static_cast<float>(windowWidth) / windowHeight;
        projectionMatrix = glm::perspective(
            glm::radians(FOV),
            aspect,
            nearClip,
            farClip
        );
    }

};

class Cam {
public: 

    glm::vec3 rotatePoint(const glm::vec3& p, const glm::vec3& rot);
    glm::vec3 worldToCamera(const glm::vec3& p, const glm::vec3& camPos, const glm::mat3& rotMatrix);
    glm::vec2 projectToScreen(const glm::vec3& p, int width, int height, float fovDeg);
    void updateCameraRotation(Camera& cam, float mouse_dx, float mouse_dy, float sensitivity = 0.002f);
    glm::mat4 cameraRotationMatrix(const Camera& cam);
};  

inline glm::vec3 Cam::rotatePoint(const glm::vec3& p, const glm::vec3& rot) {
    glm::vec3 r = p;

    // Yaw (around Y axis)
    float cosy = cos(rot.y), siny = sin(rot.y);
    r = { cosy * r.x + siny * r.z, r.y, -siny * r.x + cosy * r.z };

    // Pitch (around X axis)
    float cosp = cos(rot.x), sinp = sin(rot.x);
    r = { r.x, cosp * r.y - sinp * r.z, sinp * r.y + cosp * r.z };

    // Roll (around Z axis)
    float cosr = cos(rot.z), sinr = sin(rot.z);
    r = { cosr * r.x - sinr * r.y, sinr * r.x + cosr * r.y, r.z };

    return r;
}

inline glm::vec3 Cam::worldToCamera(const glm::vec3& p, const glm::vec3& camPos, const glm::mat3& rotMatrix) {
    glm::vec3 translated = p - camPos;
    glm::vec3 cameraSpace = rotMatrix * -translated;
    return cameraSpace;
}

inline glm::vec2 Cam::projectToScreen(const glm::vec3& point, int width, int height, float fov) {
    float aspect = static_cast<float>(width) / height;
    float fovRad = fov * 3.1415926535f / 180.0f;
    float tanHalfFov = std::tan(fovRad / 2.0f);

    // Perspective projection
    float xProj = (point.x / point.z) / (tanHalfFov * aspect);
    float yProj = (point.y / point.z) / tanHalfFov;

    // Map to screen space: x as is, y inverted (flip to match screen coordinates)
    float xScreen = (xProj + 1.0f) * 0.5f * width;
    float yScreen = (1.0f - yProj) * 0.5f * height; // Inverted y

    return { xScreen, yScreen };
}

inline void Cam::updateCameraRotation(Camera& cam, float dx, float dy, float sens)
{
    cam.rotation.x += dx * sens;
    cam.rotation.y -= dy * sens;

    cam.rotation.y = glm::clamp(cam.rotation.y, -glm::half_pi<float>() + 0.01f, glm::half_pi<float>() - 0.01f);

    
    cam.updateMatrices(WINDOW_WIDTH, WINDOW_HEIGHT);  // <- ONLY HERE
}

inline glm::mat4 Cam::cameraRotationMatrix(const Camera& cam)
{           
    glm::mat4 R = glm::mat4(1.0f);
	R = glm::rotate(R, cam.rotation.z, glm::vec3(0, 0, 1));   // roll  – local Z
    R = glm::rotate(R, cam.rotation.x, glm::vec3(0, 1, 0));   // yaw   – world X
    R = glm::rotate(R, cam.rotation.y, glm::vec3(1, 0, 0));   // pitch – local Y
    return R;
}



#endif // CAMERA_HPP
