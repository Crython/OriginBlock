/*
 * VOXELENGINE.CPP
 * 
 * Main engine entry point and game loop implementation.
 * 
 * ARCHITECTURE:
 * - Fixed timestep game loop (60 updates/sec) with variable rendering
 * - Separate input, update, and render phases
 * - Camera system with smooth acceleration/deceleration
 * - Block interaction system (raycast-based placement/breaking)
 * - Command/chat system for runtime configuration
 * 
 * RENDERING PIPELINE:
 * 1. Set up camera matrices (view with rotation only, not translation)
 * 2. Update lighting uniform buffer
 * 3. Draw world with relative positioning (for precision)
 * 4. Draw UI elements (crosshair, HUD)
 */

#include "VoxelEngine.hpp"

// ===========================
// PHYSICS & MOVEMENT CONSTANTS
// ===========================

// Player movement physics
constexpr float PLAYER_DECELERATION = 10.0f;    // Deceleration rate when no input (blocks per second^2)
constexpr float SPRINT_MULTIPLIER = 1.35f;        // Sprint speed boost
constexpr float NORMAL_MULTIPLIER = 1.0f;        // Normal movement speed

// Input handling
constexpr float KEY_REPEAT_DELAY = 0.3f;         // Initial delay before key repeat (seconds)
constexpr float KEY_REPEAT_RATE = 0.05f;         // Time between repeats (seconds)


bool mainWindowFocused = true;

// ===========================
// ENGINE INITIALIZATION
// ===========================

/**
 * Engine constructor - Sets up shaders, camera, and initial world state.
 */
Engine::Engine() : 
    voxelShader("res/vertex.glsl", "res/fragment.glsl"), 
    uiShader("res/ui_vertex.glsl", "res/ui_fragment.glsl"),
    textShader("res/text_vertex.glsl", "res/text_fragment.glsl"),
    colorShader("res/color_vertex.glsl", "res/color_fragment.glsl")
{
    text = new TextRenderer(textShader);
    text->setScreenSize(WINDOW_WIDTH, WINDOW_HEIGHT);
    if (!text->loadFont("C:/Windows/Fonts/consola.ttf", 24)) {
        // Fallback to another common font if Consolas is missing
        if (!text->loadFont("C:/Windows/Fonts/arial.ttf", 24)) {
			std::cerr << "ERROR::ENGINE: Failed to load any font for TextRenderer" << std::endl;
			std::abort(); // Critical failure if no font can be loaded
        }
    }

    initCrosshair();

    // Initialize Rect drawing data
    glGenVertexArrays(1, &rectVAO);
    glGenBuffers(1, &rectVBO);
    glBindVertexArray(rectVAO);
    glBindBuffer(GL_ARRAY_BUFFER, rectVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 2, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    glGenBuffers(1, &lightingUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, lightingUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(WorldLightingData), nullptr, GL_DYNAMIC_DRAW); // Allocate storage
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    // Bind to the same binding point as in the shader
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, lightingUBO); // binding = 1

    playerCamera = {
		glm::mat4(1.0f), // View Matrix
		glm::mat4(1.0f), // Projection Matrix
		glm::dvec3(0.0), // Position
		glm::vec3(0.0f), // Rotation
		0.05f,           // Near Clip
		5000.0f,         // Far Clip
		90.0f,           // FOV
		0.3f,            // Mouse Sensitivity
		7.8f,            // Movement Speed (blocks per second)
		glm::vec3(0.0f), // Velocity
		0.025f           // Block Action Delay
	};
    playerCamera.position = glm::dvec3(0, 80.0f, 0);
    playerCamera.rotation = glm::vec3(90.0f, 0.0f, 0.0f); // looking toward negative Z
    playerCamera.updateMatrices(WINDOW_WIDTH, WINDOW_HEIGHT);

    textures.loadBlockTextures();

    // Initialize viewDir AFTER playerCamera is constructed
    viewDir = glm::normalize(playerCamera.getForwardVector());

}

Engine::~Engine()
{
    delete text;
    glDeleteVertexArrays(1, &rectVAO);
    glDeleteBuffers(1, &rectVBO);
}

// ===========================
// INPUT HANDLING
// ===========================

/**
 * Process all player input including movement, camera, block interaction, and chat.
 * Handles both immediate and repeat key presses for text entry.
 */
void Engine::handleInputs(GLFWwindow* window, float dt)
{
    bool sprinting = false;

    RaycastHit hitPos = { glm::ivec3(0), glm::ivec3(0) };

    
    // ===== CHAT/COMMAND INPUT =====
    if (Input::keyDown(GLFW_KEY_T) && !chatOpen) {
        chatOpen = true;
        chatInput = "";
        keyRepeatTimer[GLFW_KEY_T] = keyRepeatDelay;
        Input::setMouseCaptured(false);  // Unlock mouse to type
    }

    if (chatOpen) {
        // Lambda to handle adding a character with instant + repeat
        auto handleKey = [&](int glfwKey, char normalChar, char shiftChar = 0) {
            char c = normalChar;
            if (shiftChar != 0) {
                bool shift = Input::keyDown(GLFW_KEY_LEFT_SHIFT) || Input::keyDown(GLFW_KEY_RIGHT_SHIFT);
                c = shift ? shiftChar : normalChar;
            }

            bool wasDown = keyRepeatTimer.count(glfwKey) > 0;
            bool isDown = Input::keyDown(glfwKey);

            if (isDown && !wasDown) {
                // First frame the key is down -> instant add
                chatInput += c;                

                keyRepeatTimer[glfwKey] = keyRepeatDelay;  // Start delay before repeat
            }
            else if (isDown && wasDown) {
                // Key held -> repeat logic
                float& timer = keyRepeatTimer[glfwKey];
                timer -= dt;
                if (timer <= 0.0f) {
                    chatInput += c;
                    timer += keyRepeatRate;
                }
            }
            else if (!isDown) {
                // Key released -> clean up
                keyRepeatTimer.erase(glfwKey);
            }
            };
        auto handleChar = [&](int keyCode, char normalChar, char shiftChar = 0) {
           
            char c = normalChar;
            if (shiftChar != 0) {
                bool shift = Input::keyDown(GLFW_KEY_LEFT_SHIFT) || Input::keyDown(GLFW_KEY_RIGHT_SHIFT);
                c = shift ? shiftChar : normalChar;
            }

            bool wasDown = keyRepeatTimer.count(keyCode) > 0;
            bool isDown = Input::charDown(keyCode);

            if (isDown && !wasDown) {
                // First frame the key is down -> instant add
                chatInput += c;
                
                keyRepeatTimer[keyCode] = keyRepeatDelay;  // Start delay before repeat
            }
            else if (isDown && wasDown) {
                // Key held -> repeat logic
                float& timer = keyRepeatTimer[keyCode];
                timer -= dt;
                if (timer <= 0.0f) {
                    chatInput += c;
                    timer += keyRepeatRate;
                }
            }
            else if (!isDown) {
                // Key released -> clean up
                keyRepeatTimer.erase(keyCode);
            }
            };

        // Letters A-Z
        for (int k = GLFW_KEY_A; k <= GLFW_KEY_Z; ++k) {
            handleKey(k, 'a' + (k - GLFW_KEY_A), 'A' + (k - GLFW_KEY_A));
        }

        // Numbers 0-9 (top row — shift gives symbols if you want later)
        for (int k = GLFW_KEY_0; k <= GLFW_KEY_9; ++k) {
            if (k == GLFW_KEY_7) {
                handleKey(k, '7', '/'); // Special case for '/'
				continue;
            }
            handleKey(k, '0' + (k - GLFW_KEY_0));
        }

        // Common command keys
        {
            handleKey(GLFW_KEY_SPACE, ' ');
            handleKey(GLFW_KEY_SLASH, '-', '_');
            handleKey(GLFW_KEY_MINUS, '+', '-');
		    handleKey(GLFW_KEY_EQUAL, '=');
		    handleKey(GLFW_KEY_PERIOD, '.', ':');
		    handleKey(GLFW_KEY_COMMA, ',', ';');

            handleChar(126, '~', 'h');
        }

        // Backspace — special delete behavior
        {
            int key = GLFW_KEY_BACKSPACE;
            bool isDown = Input::keyDown(key);
            bool wasDown = keyRepeatTimer.count(key) > 0;

            if (isDown && !wasDown && !chatInput.empty()) {
                chatInput.pop_back();
                keyRepeatTimer[key] = keyRepeatDelay;
            }
            else if (isDown && wasDown && !chatInput.empty()) {
                float& timer = keyRepeatTimer[key];
                timer -= dt;
                if (timer <= 0.0f) {
                    chatInput.pop_back();
                    timer += keyRepeatRate;
                }
            }
            else if (!isDown) {
                keyRepeatTimer.erase(key);
            }
        }

        // Up/Down arrows to cycle chat history
        {
            int keyUp = GLFW_KEY_UP;
            int keyDown = GLFW_KEY_DOWN;
            bool isDownUp = Input::keyDown(keyUp);
            bool isDownDown = Input::keyDown(keyDown);
            bool wasDownUp = keyRepeatTimer.count(keyUp) > 0;
            bool wasDownDown = keyRepeatTimer.count(keyDown) > 0;

            if ((isDownUp && !wasDownUp) || (isDownDown && !wasDownDown)) {
                if (isDownUp) historyIndex++;
                else if (historyIndex > 0) historyIndex--;

                // Filter chatHistory for user inputs (starting with "< ")
                std::vector<std::string> inputs;
                for (auto const& s : chatHistory) {
                    if (s.rfind("< ", 0) == 0) {
                        inputs.push_back(s.substr(2));
                    }
                }

                if (inputs.empty()) {
                    historyIndex = 0;
                } else {
                    if (historyIndex > inputs.size()) historyIndex = inputs.size();
                    if (historyIndex == 0) chatInput = "";
                    else chatInput = inputs[inputs.size() - historyIndex];
                }

                keyRepeatTimer[isDownUp ? keyUp : keyDown] = keyRepeatDelay;
            }
            else if (isDownUp || isDownDown) {
                int key = isDownUp ? keyUp : keyDown;
                float& timer = keyRepeatTimer[key];
                timer -= dt;
                if (timer <= 0.0f) {
                    if (isDownUp) historyIndex++;
                    else if (historyIndex > 0) historyIndex--;

                    std::vector<std::string> inputs;
                    for (auto const& s : chatHistory) {
                        if (s.rfind("< ", 0) == 0) {
                            inputs.push_back(s.substr(2));
                        }
                    }

                    if (!inputs.empty()) {
                        if (historyIndex > inputs.size()) historyIndex = inputs.size();
                        if (historyIndex == 0) chatInput = "";
                        else chatInput = inputs[inputs.size() - historyIndex];
                    } else historyIndex = 0;

                    timer += keyRepeatRate;
                }
            }
            else {
                keyRepeatTimer.erase(keyUp);
                keyRepeatTimer.erase(keyDown);
            }
        }
        // Submit command
        if (Input::keyDown(GLFW_KEY_ENTER)) {
            if (!chatInput.empty()) {
				printToChat("< " + chatInput, false, true);

                std::vector<std::string> args;
                size_t cmdID = command.getCommandID(chatInput, args);
                if (cmdID > 0) {
                    command.executeCommand(cmdID, args, chatInput, *this);
                }
                else if (!chatInput.empty() && chatInput[0] == '/') {
                    printToChat("Unknown command", false, true);
                }
            }
            chatOpen = false;
            chatInput.clear();
            historyIndex = 0;
            Input::setMouseCaptured(true);
        }

        // Cancel
        if (Input::keyDown(GLFW_KEY_ESCAPE)) {
            chatOpen = false;
            chatInput.clear();
            historyIndex = 0;
            Input::setMouseCaptured(true);
        }
    }
    else { // Regular gameplay input (movement and interaction)


        // ===== ESCAPE & UTILITY KEYS =====
        if (Input::keyDown(GLFW_KEY_ESCAPE))
        {
            glfwSetWindowShouldClose(window, true);
        }
        if (Input::keyDown(GLFW_KEY_M))
        {
            Input::setMouseCaptured(!Input::isMouseCaptured());
        }
        if (Input::keyDown(GLFW_KEY_R))
        {
            overworld.reloadAllChunks(true);
        }

        // ===== PLAYER MOVEMENT =====
        glm::vec3 moveDir(0.0f);
        glm::dvec3 forward = playerCamera.getForwardVectorMovement();
        if (glm::length(forward) > 0.0001) forward = glm::normalize(forward);
        
        glm::vec3 right = glm::normalize(glm::cross(forward, glm::dvec3(0.0, 1.0, 0.0)));
        glm::vec3 up = glm::vec3(0, 1, 0); // Y-up world

        if (Input::keyDown(GLFW_KEY_W)) moveDir += forward;
        if (Input::keyDown(GLFW_KEY_S)) moveDir -= forward;
        if (Input::keyDown(GLFW_KEY_A)) moveDir -= right;
        if (Input::keyDown(GLFW_KEY_D)) moveDir += right;

        if (playerCamera.isFlying) {
            if (Input::keyDown(GLFW_KEY_SPACE)) moveDir += up;
            if (Input::keyDown(GLFW_KEY_LEFT_CONTROL)) moveDir -= up;
            if (Input::keyDown(GLFW_KEY_LEFT_SHIFT)) sprinting = true;
        } else {
            if (Input::keyDown(GLFW_KEY_LEFT_SHIFT)) sprinting = true;
            if (Input::keyDown(GLFW_KEY_SPACE) && playerCamera.onGround) {
                playerCamera.velocity.y = JUMP_FORCE;
                playerCamera.onGround = false;
            }
        }

        if (glm::length(moveDir) > 0.0f) moveDir = glm::normalize(moveDir);
		bool diagonalMoving = (Input::keyDown(GLFW_KEY_A) || Input::keyDown(GLFW_KEY_D)) && (Input::keyDown(GLFW_KEY_W) || Input::keyDown(GLFW_KEY_S));

        float speedMultiplier = sprinting ? SPRINT_MULTIPLIER : NORMAL_MULTIPLIER;
		speedMultiplier = playerCamera.isFlying ? speedMultiplier * 2.7f : speedMultiplier; // Faster flying
		speedMultiplier = playerCamera.onGround ? speedMultiplier : speedMultiplier * 0.75f; // Slower in air when walking
		speedMultiplier = diagonalMoving ? speedMultiplier * 1.03f : speedMultiplier; // Slight boost for diagonal movement to feel natural

        if (playerCamera.isFlying) {
            // Flight physics: smooth acceleration/deceleration
            if (glm::length(moveDir) > 0.0f) {
                playerCamera.velocity += moveDir * (playerCamera.movementSpeed * dt * 5.0f); 
                if (glm::length(playerCamera.velocity) > playerCamera.movementSpeed * speedMultiplier)
                    playerCamera.velocity = glm::normalize(playerCamera.velocity) * (playerCamera.movementSpeed * speedMultiplier);
            }
            else {
                float speed = glm::length(playerCamera.velocity);
                if (speed > 0.0f) {
                    float decelAmount = PLAYER_DECELERATION * dt * 2.0f;
                    if (decelAmount > speed)
                        playerCamera.velocity = glm::vec3(0.0f);
                    else
                        playerCamera.velocity -= glm::normalize(playerCamera.velocity) * decelAmount;
                }
            }
            playerCamera.position += playerCamera.velocity * dt;
        } else {
            // Walking physics
            glm::vec3 horizontalVel = moveDir * (playerCamera.movementSpeed * speedMultiplier);
            playerCamera.velocity.x = horizontalVel.x;
            playerCamera.velocity.z = horizontalVel.z;
            
            // Apply gravity
            playerCamera.velocity.y += GRAVITY * dt;
            
            // Resolve collisions
            glm::vec3 frameVelocity = playerCamera.velocity * dt;
            glm::dvec3 bottomPos = playerCamera.position;
            bottomPos.y -= PLAYER_EYE_HEIGHT;

            overworld.resolveCollision(bottomPos, frameVelocity, playerCamera.dimensions, playerCamera.onGround);
            
            playerCamera.position = bottomPos;
            playerCamera.position.y += PLAYER_EYE_HEIGHT;

            // Update velocity from resolved frame velocity (if we hit a wall/floor)
            playerCamera.velocity.y = frameVelocity.y / dt;
        }

        if (glm::length(playerCamera.velocity) > 0.0f || !playerCamera.onGround) playerCamera.updateMatrices(WINDOW_WIDTH, WINDOW_HEIGHT);


        // ===== MOUSE LOOK =====
        if (Input::isMouseCaptured())
        {
            float dx = Input::consumeMouseDX();
            float dy = Input::consumeMouseDY();
            if (dx != 0.0f || dy != 0.0f) {
                cam.updateCameraRotation(playerCamera, dx, dy, playerCamera.sensitivity / 100);
                viewDir = glm::normalize(playerCamera.getForwardVector());
                playerCamera.updateMatrices(WINDOW_WIDTH, WINDOW_HEIGHT);
            }
        }

        // ===== BLOCK INTERACTION =====
        // Place block
        if (Input::mouseDown(GLFW_MOUSE_BUTTON_RIGHT) && playerCamera.blockPlaceCooldown <= 0.0f) {
            if (overworld.raycast(playerCamera.position, playerCamera.getForwardVector(), 10.0f, hitPos)) {

                // Avoid placing blocks inside the player
                glm::ivec3 placePos = hitPos.block + hitPos.normal;
                glm::dvec3 playerBottom = playerCamera.position;
                playerBottom.y -= PLAYER_EYE_HEIGHT;

                glm::ivec3 pMin = glm::floor(playerBottom - glm::dvec3(playerCamera.dimensions.x / 2.0, 0.0, playerCamera.dimensions.z / 2.0));
                glm::ivec3 pMax = glm::floor(playerBottom + glm::dvec3(playerCamera.dimensions.x / 2.0, playerCamera.dimensions.y, playerCamera.dimensions.z / 2.0));

                if (placePos.x < pMin.x || placePos.x > pMax.x ||
                    placePos.y < pMin.y || placePos.y > pMax.y ||
                    placePos.z < pMin.z || placePos.z > pMax.z) 
                {
                    overworld.placeBlock(hitPos, BlockType::DIRT);
                }

            }
            playerCamera.blockPlaceCooldown = playerCamera.BLOCK_ACTION_DELAY;
        }

        // Break block
        if (Input::mouseDown(GLFW_MOUSE_BUTTON_LEFT) && playerCamera.blockBreakCooldown <= 0.0f) {
            if (overworld.raycast(playerCamera.position, playerCamera.getForwardVector(), 10.0f, hitPos)) {
                overworld.breakBlock(hitPos);
            }
            playerCamera.blockBreakCooldown = playerCamera.BLOCK_ACTION_DELAY;
        }

        // Adjust fog density with scroll
        float scrollY = (float)Input::consumeScrollDY();
        if (scrollY != 0.0f) {
            overworld.lightData.fogDensity += scrollY * 0.0005f; // Adjust sensitivity as needed
            if (overworld.lightData.fogDensity < 0.0f) overworld.lightData.fogDensity = 0.0f;
            if (overworld.lightData.fogDensity > 1.0f) overworld.lightData.fogDensity = 1.0f;
            printToChat("Fog Density: " + std::to_string(overworld.lightData.fogDensity), false, true);
        }
    }
}

// ===========================
// RENDERING
// ===========================

/**
 * Render the world and UI.
 * Uses rotation-only view matrix with relative positioning for precision at large distances.
 */
void Engine::render()
{   
    
    // Bind textures to the GPU
    textures.bindBlockTextures();
    voxelShader.setInt("uBlockTextures", 0);

    // Rendering code here
    voxelShader.bind();

    // Remove translation from View Matrix because uModel handles relative positioning
    glm::mat4 viewNoTranslation = playerCamera.viewMatrix;
    viewNoTranslation[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // Reset translation column
    
    // Create VP matrix with rotation-only View
    glm::mat4 rotationOnlyVP = playerCamera.projectionMatrix * viewNoTranslation;

    voxelShader.setMat4("uViewProj", rotationOnlyVP); 

    // Update lighting UBO with current values
    glBindBuffer(GL_UNIFORM_BUFFER, lightingUBO);
    WorldLightingData& light = overworld.lightData;
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(WorldLightingData), &light);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    
    // Draw the overworld
    // Pass rotationOnlyVP for frustum extraction and shader uniforms
    overworld.draw(voxelShader, lightDir, rotationOnlyVP, playerCamera.position, totalChunksRenderedPercent);

    // ======= UI & HUD RENDERING =======
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 1. Draw Crosshair
    uiShader.bind();
    glBindVertexArray(uiVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    
    // 2. Draw Debug Stats (Top Left)
    
    std::stringstream posStream;
    posStream << std::fixed << std::setprecision(2) << "Pos: " 
        << playerCamera.position.x << " " 
        << playerCamera.position.y << " " 
        << playerCamera.position.z;
    std::string posText = posStream.str();

    text->renderText(posText, 10.0f, 30.0f, 1.0f, glm::vec3(1.0f));
    

    std::string fpsText = "FPS: " + std::to_string((int)currentFPS);
    text->renderText(fpsText, 10.0f, 60.0f, 1.0f, glm::vec3(1.0f, 1.0f, 0.0f)); // Yellow FPS

    // 3. Draw Chat (Bottom Left)
    float chatY = (float)WINDOW_HEIGHT - 27.5f;
    float lineHeight = 25.0f;

    // Background for chat
    if (chatOpen || !chatHistory.empty()) {
        float bgWidth = 400.0f;
        float bgHeight = (chatHistory.size() + (chatOpen ? 1 : 0)) * lineHeight + 10.0f;
        drawRect(5.0f, (float)WINDOW_HEIGHT - bgHeight - 15.0f, bgWidth, bgHeight, glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));
    }

    if (chatOpen) {
        text->renderText("> " + chatInput + "_", 10.0f, chatY, 1.0f, glm::vec3(1.0f));
        chatY -= lineHeight;
    }

    for (int i = chatHistory.size() - 1; i >= 0; --i) {
        text->renderText(chatHistory[i], 10.0f, chatY, 0.8f, glm::vec3(0.9f));
        chatY -= lineHeight;
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}   


/*
 * Update game state - processes chunk generation/meshing and world updates.
 */
void Engine::update()
{
    // Update logic here   
    
	// Process threaded chunk work (transfer from workers to main thread)
    overworld.processThreadedWork();
    
    // Load/Unload chunks, Rebuild dirty chunks
    overworld.updateLoadedChunks(playerCamera.position, viewDir, overworld.worldSeed);
    overworld.rebuildDirtyChunks(lightDir);
}

void Engine::printToChat(const std::string& str, bool mergeWithLastMessage, bool newLine)
{
    if (!mergeWithLastMessage) chatHistory.push_back(str);
    else {
        if (chatHistory.empty()) chatHistory.push_back(str);
        else chatHistory.back() += str;
	}
    if (chatHistory.size() > MAX_CHAT_HISTORY) {
        chatHistory.erase(chatHistory.begin());
    }
}

void Engine::drawRect(float x, float y, float w, float h, glm::vec4 color) {
    colorShader.bind();
    colorShader.setVec4("uColor", color);
    glm::mat4 projection = glm::ortho(0.0f, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT, 0.0f);
    colorShader.setMat4("uProjection", projection);

    float vertices[] = {
        x,     y,
        x,     y + h,
        x + w, y + h,

        x,     y,
        x + w, y + h,
        x + w, y
    };

    glDisable(GL_CULL_FACE);
    glBindVertexArray(rectVAO);
    glBindBuffer(GL_ARRAY_BUFFER, rectVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
}

void Engine::initCrosshair()
{
    float size = 0.005f;
    float aspect = (float)WINDOW_WIDTH / (float)WINDOW_HEIGHT;
    
    float vertices[] = {
        // positions                    // texCoords
        -size / aspect,  size,          0.0f, 1.0f,
        -size / aspect, -size,          0.0f, 0.0f,
         size / aspect, -size,          1.0f, 0.0f,

        -size / aspect,  size,          0.0f, 1.0f,
         size / aspect, -size,          1.0f, 0.0f,
         size / aspect,  size,          1.0f, 1.0f
    };

    glGenVertexArrays(1, &uiVAO);
    glGenBuffers(1, &uiVBO);

    glBindVertexArray(uiVAO);
    glBindBuffer(GL_ARRAY_BUFFER, uiVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Position attribute
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // TexCoords attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}


void focus_callback(GLFWwindow* window, int focused) {
    if (focused) {
        // The window has gained focus
        std::cout << "Window gained focus\n";
        // Centre the mouse cursor to prevent sudden jumps when refocusing
        glfwSetCursorPos(window, WINDOW_WIDTH / 2.0, WINDOW_HEIGHT / 2.0);

        mainWindowFocused = true;
    }
    else {
        // The window has lost focus
        std::cout << "Window lost focus\n";
        mainWindowFocused = false;
    }
}


int main()
{
    // Init GLFW
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);

    const char* ProjectName = "OriginBlock";
	const std::string Version = "v0.1a";


    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, ProjectName, nullptr, nullptr);
    if (!window) return -1;

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0); // 0 = V-Sync OFF (uncapped FPS)

    glfwSetWindowFocusCallback(window, focus_callback);
	glfwSetCursorPos(window, WINDOW_WIDTH / 2.0, WINDOW_HEIGHT / 2.0); // Center mouse initially

    // Load OpenGL
    if (!gladLoadGL()) return -1;

    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    //glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); // Wireframe for debugging

    Engine engine;
    Input::init(window);
    Input::setMouseCaptured(true);

    engine.overworld.setRandSeed();
    std::cout << "Seed: " << engine.overworld.worldSeed << "\n";

	engine.terrain.initVoronoi(engine.overworld.worldSeed, 10'000, 10000, -5000.0f, -5000.0f);

    std::string debugFilenameStr = "heightmaps/heightmap" + std::to_string(engine.overworld.worldSeed) + ".png";
    const char* debugFilename = debugFilenameStr.c_str();
    //engine.terrain.writeChunkHeightmapPNG(-500, -500, 1000, 1000, 16, engine.overworld.worldSeed, debugFilename);

    debugFilenameStr = "biomemaps/biomemap" + std::to_string(engine.overworld.worldSeed) + ".png";
    debugFilename = debugFilenameStr.c_str();
    //engine.terrain.writeChunkBiomemapPNG(-500, -500, 1000, 1000, 16, engine.overworld.worldSeed, debugFilename);

    engine.overworld.updateLoadedChunks(engine.playerCamera.position, engine.viewDir, engine.overworld.worldSeed);


    // Timing
    const float FIXED_DT = 1.0f / 60.0f; // 60 updates/sec
    double previousTime = glfwGetTime();
    double accumulator = 0.0;
    double fpsTimer = 0.0;
    int frameCount = 0;

    // ----- Main loop -----
	std::cout << "Starting main loop...\n";
    while (!glfwWindowShouldClose(window))
    {
        double currentTime = glfwGetTime();
        double frameTime = currentTime - previousTime;
        previousTime = currentTime;

        accumulator += frameTime;
        fpsTimer += frameTime;



        // Poll input every frame
        glfwPollEvents();
        Input::newFrame();

        while (accumulator >= FIXED_DT)
        {
            float dt = FIXED_DT;

            if (mainWindowFocused)
                engine.handleInputs(window, dt); // use Input class directly

            // cooldowns
            if (engine.playerCamera.blockPlaceCooldown > 0.0f) engine.playerCamera.blockPlaceCooldown -= dt;
            if (engine.playerCamera.blockBreakCooldown > 0.0f) engine.playerCamera.blockBreakCooldown -= dt;

            engine.update();

            accumulator -= FIXED_DT;
        }



        // Render (interpolation can be added if desired)
        glm::vec3 fc = engine.overworld.lightData.fogColor;
        glClearColor(fc.r, fc.g, fc.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        engine.voxelShader.bind();
        engine.render();

        glfwSwapBuffers(window);
        frameCount++;

        // FPS calculation (updates once per second)
        if (fpsTimer >= 0.1)
        {
            engine.currentFPS = frameCount / static_cast<float>(fpsTimer);
            frameCount = 0;
            fpsTimer = 0.0;
        }
    }

    glfwTerminate();
    return 0;
}

