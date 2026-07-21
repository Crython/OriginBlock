// OriginBlock.h : Include file for standard system include files,
// or project specific include files.

#ifndef VOXELENGINE_HPP
#define VOXELENGINE_HPP




// Custom headers
#include "filehandler.hpp"
#include "Camera.hpp"
#include "shader.hpp"
#include "Worlds.hpp"
#include "constants.hpp"
#include "inputs.hpp"
#include "textureManager.hpp"
#include "commands.hpp"
#include "TextRenderer.hpp"

class Engine
{
public:

    Engine(); // Constructor
    ~Engine(); // Destructor


    // Instances for the engine
    Shader voxelShader;
    Shader uiShader;
    Shader textShader;
    Shader colorShader;
    Cam cam; // camera utility instance
    World overworld; // The main world
    Terrain terrain;

    void initCrosshair();
    void drawRect(float x, float y, float w, float h, glm::vec4 color);
    GLuint uiVAO, uiVBO;
    GLuint rectVAO, rectVBO;

    glm::vec3 viewDir;
    glm::vec3 lightDir = glm::vec3(0.5f, 0.5f, 0.5f);
    // Block texture atlas
    TextureManager textures;
    Camera playerCamera;
    glm::dvec3 getCameraPosition() const { return playerCamera.position; }

    Commands command;
    TextRenderer* text;


    void handleInputs(GLFWwindow* window, float dt);
    void render();
    void update(); // Update logic

    void printToChat(const std::string& str, bool mergeWithLastMessage, bool newLine);
	void clearChat() { chatHistory.clear(); chatOpen = false; chatInput = ""; } // Completely clear chat history and close chat
    bool mouseLocked = false;

    // Debug stuff
    float currentFPS = 0.f;
    int verticesRendered = 0;
private:
    GLuint lightingUBO;

    // Chat thingy's
    bool chatOpen = false;
    std::string chatInput = "";
    std::vector<std::string> chatHistory;
    const size_t MAX_CHAT_HISTORY = 25;
    size_t historyIndex = 0; // Track current position in history


    // For key repeat in chat
    std::unordered_map<int, float> keyRepeatTimer;
    float keyRepeatDelay = 0.4f;    // Initial delay before repeat (seconds)
    float keyRepeatRate = 0.05f;    // How fast it repeats after delay

    float lastSensitivity = 0.f;
    bool debugStatistics = true; // Enables debug statistics
};

#endif // VOXELENGINE_HPP