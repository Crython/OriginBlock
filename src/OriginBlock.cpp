/*
 * ORIGINBLOCK.CPP
 * 
 * Main engine entry point and game loop implementation.
 * 
 * ARCHITECTURE:
 * - Fixed timestep game loop (60 updates/sec) with variable rendering (may vary during development)
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

#include "pch.h"
#include "OriginBlock.hpp"

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
    std::cout << "[LOG] Loading shaders and textures..." << std::endl;

    text = new TextRenderer(textShader);
    text->setScreenSize(WINDOW_WIDTH, WINDOW_HEIGHT);

	const std::string fontPaths[2] = {"assets/fonts/consola.ttf", "assets/fonts/arial.ttf"};
	bool fontLoaded = false;

	// Attempt to load fonts in order of preference
    for (const std::string path : fontPaths) {
        if (text->loadFont(path, 24)) {
            fontLoaded = true;
            break;
        }
    }
    if (!fontLoaded) {
	    std::cerr << "ERROR::ENGINE: Failed to load any font for TextRenderer" << std::endl;
		std::abort(); // Critical failure if no font can be loaded
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

    // Generate and allocate the buffer
    glGenBuffers(1, &lightingUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, lightingUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(WorldLightingData), nullptr, GL_DYNAMIC_DRAW); 
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    // Bind the buffer to binding point 0
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, lightingUBO); 

    // Link the shader (Crucial for Mac/OpenGL 4.1)
    GLuint blockIndex = glGetUniformBlockIndex(voxelShader.program, "WorldLighting");
    if (blockIndex != GL_INVALID_INDEX) {
        glUniformBlockBinding(voxelShader.program, blockIndex, 0);
    } else {
        std::cout << "WARNING: Could not find 'WorldLighting' in shader!" << std::endl;
    }


    playerCamera = {
		glm::mat4(1.0f), // View Matrix
		glm::mat4(1.0f), // Projec†tion Matrix
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
    playerCamera.position = glm::dvec3(0.5f, 0.0f, 0.5f);
    playerCamera.rotation = glm::vec3(0.0f, 0.0f, 0.0f); // looking toward positive X
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


// Wrapper function for all player input including movement, camera, block interaction, and chat.
void Engine::handleInputs(GLFWwindow* window, float dt) {

	// Handle chat input first, because the function also clears the input buffer
    // The chat input system can open the chat itself
    handleChatInput(dt);
    if (isChatOpen || wasChatClosedThisFrame) {
		wasChatClosedThisFrame = false; // Reset the flag for the next frame
		return; // Skip gameplay input while chat is open
    }
    else {
        handleGameplayInput(window, dt);
    }
}

// Handles chat input, including text entry, history navigation, and command execution.
void Engine::handleChatInput(float dt) {
	// Open chat with 'T' or '/' if not already open, though this should be handled in handleInputs() already
    if (!isChatOpen) {
        if (Input::keyPressed(GLFW_KEY_T)) {
            isChatOpen = true;
            Input::setMouseCaptured(false);
            Input::consumeCharBuffer(); // Drain stale key buffers
            historyIndex = -1;
            return;
        }
        if (Input::keyPressed(GLFW_KEY_SLASH)) {
            isChatOpen = true;
            Input::setMouseCaptured(false);
            Input::consumeCharBuffer();
            chatInputString = "/"; // Auto-insert slash when opening with '/'
            historyIndex = -1;
            return;
        }
        return;
    }

	// Unicode character input handling (consumes the character buffer)
    std::vector<uint32_t> chars = Input::consumeCharBuffer();
    for (uint32_t c : chars) {
        if (c >= 32 && c != 127) { // Printable character range
            chatInputString += static_cast<char>(c);

            // Reset history navigation if user edits manually after pressing Up
            if (historyIndex != -1) {
                historyIndex = -1;
            }
        }
    }

	// 3. Editing: Backspace key handling
    if (Input::keyPressed(GLFW_KEY_BACKSPACE) && !chatInputString.empty()) {
        chatInputString.pop_back();
        if (historyIndex != -1) {
            historyIndex = -1;
        }
    }

    // 4. History cycling (Up / Down Arrow Keys)
    if (!inputHistory.empty()) {
        // Up arrow: Move backward into history (older entries)
        if (Input::keyPressed(GLFW_KEY_UP)) {
            if (historyIndex == -1) {
                // Save whatever user was typing before navigating history
                savedDraft = chatInputString;
                historyIndex = static_cast<int>(inputHistory.size()) - 1;
            }
            else if (historyIndex > 0) {
                historyIndex--;
            }

            chatInputString = inputHistory[historyIndex];
        }

        // Down arrow: Move forward into history (newer entries)
        if (Input::keyPressed(GLFW_KEY_DOWN)) {
            if (historyIndex != -1) {
                historyIndex++;
                if (historyIndex >= static_cast<int>(inputHistory.size())) {
                    // Returned to current draft
                    historyIndex = -1;
                    chatInputString = savedDraft;
                }
                else {
                    chatInputString = inputHistory[historyIndex];
                }
            }
        }
    }

	// 5. Command execution or chat message sending (Enter key)
    if (Input::keyPressed(GLFW_KEY_ENTER) || Input::keyPressed(GLFW_KEY_KP_ENTER)) {
        if (!chatInputString.empty()) {
            // Push to user input history (avoiding duplicate back-to-back entries)
            if (inputHistory.empty() || inputHistory.back() != chatInputString) {
                inputHistory.push_back(chatInputString);
                if (inputHistory.size() > MAX_CHAT_HISTORY) {
                    inputHistory.erase(inputHistory.begin());
                }
            }

            // Differentiate between commands and plain chat
			if (chatInputString[0] == '/') { // Command detected
                command.executeCommand(chatInputString, *this);
            }
			else { // Plain chat message
                printToChat(chatInputString, false, true);
            }

            chatInputString.clear();
            savedDraft.clear();
        }

        historyIndex = -1;
        isChatOpen = false;
        Input::setMouseCaptured(true);
    }

	// 6. Cancel chat input (Escape key)
    if (Input::keyPressed(GLFW_KEY_ESCAPE)) {
        chatInputString.clear();
        savedDraft.clear();
        historyIndex = -1;
        isChatOpen = false;
		wasChatClosedThisFrame = true; // Mark that chat was closed this frame
        Input::setMouseCaptured(true);
    }
}

// Helper function to handle timing delay/rate for held keys cleanly
bool Engine::processKeyRepeat(int key, float dt) {
    auto it = keyRepeatTimer.find(key);
    if (it == keyRepeatTimer.end()) {
        keyRepeatTimer[key] = keyRepeatDelay;
        return true; // First frame trigger
    }

    it->second -= dt;
    if (it->second <= 0.0f) {
        it->second += keyRepeatRate;
        return true; // Repeat trigger
    }

    return false;
}

void Engine::handleGameplayInput(GLFWwindow* window, float dt) {
    // Utility Shortcuts
    if (Input::keyPressed(GLFW_KEY_ESCAPE)) glfwSetWindowShouldClose(window, true);
    if (Input::keyPressed(GLFW_KEY_M)) Input::setMouseCaptured(!Input::isMouseCaptured());
    if (Input::keyPressed(GLFW_KEY_R)) overworld.reloadAllChunks(true);

    handlePlayerMovement(dt);
    handleMouseLook();
    handleBlockInteraction();
    handleScrollInput();
}

void Engine::handlePlayerMovement(float dt) {
    glm::vec3 moveDir(0.0f);
    glm::dvec3 forward = playerCamera.getForwardVectorMovement();
    if (glm::length(forward) > 0.0001) forward = glm::normalize(forward);

    glm::vec3 right = glm::normalize(glm::cross(forward, glm::dvec3(0.0, 1.0, 0.0)));
    glm::vec3 up(0.0f, 1.0f, 0.0f);

    if (Input::keyDown(GLFW_KEY_W)) moveDir += forward;
    if (Input::keyDown(GLFW_KEY_S)) moveDir -= forward;
    if (Input::keyDown(GLFW_KEY_A)) moveDir -= right;
    if (Input::keyDown(GLFW_KEY_D)) moveDir += right;

    bool sprinting = Input::keyDown(GLFW_KEY_LEFT_SHIFT);

    if (playerCamera.isFlying) {
        if (Input::keyDown(GLFW_KEY_SPACE)) moveDir += up;
        if (Input::keyDown(GLFW_KEY_LEFT_CONTROL)) moveDir -= up;
    }
    else {
        // Check keyDown instead of keyPressed so holding Space continuously jumps
        if (Input::keyDown(GLFW_KEY_SPACE) && playerCamera.onGround) {
            playerCamera.velocity.y = JUMP_FORCE;
            playerCamera.onGround = false;
        }
    }

    if (glm::length(moveDir) > 0.0f) moveDir = glm::normalize(moveDir);

    bool diagonalMoving = (Input::keyDown(GLFW_KEY_A) || Input::keyDown(GLFW_KEY_D)) &&
        (Input::keyDown(GLFW_KEY_W) || Input::keyDown(GLFW_KEY_S));

    float speedMultiplier = sprinting ? SPRINT_MULTIPLIER : NORMAL_MULTIPLIER;
    if (playerCamera.isFlying) speedMultiplier *= 2.7f;
    else if (!playerCamera.onGround) speedMultiplier *= 0.75f;
    if (diagonalMoving) speedMultiplier *= 1.03f;

    // Movement Physics
    if (playerCamera.isFlying) {
        if (glm::length(moveDir) > 0.0f) {
            playerCamera.velocity += moveDir * (playerCamera.movementSpeed * dt * 5.0f);
            float maxSpeed = playerCamera.movementSpeed * speedMultiplier;
            if (glm::length(playerCamera.velocity) > maxSpeed)
                playerCamera.velocity = glm::normalize(playerCamera.velocity) * maxSpeed;
        }
        else {
            float speed = glm::length(playerCamera.velocity);
            if (speed > 0.0f) {
                float decelAmount = PLAYER_DECELERATION * dt * 2.0f;
                if (decelAmount > speed) playerCamera.velocity = glm::vec3(0.0f);
                else playerCamera.velocity -= glm::normalize(playerCamera.velocity) * decelAmount;
            }
        }
        playerCamera.position += playerCamera.velocity * dt;
    }
    else {
        glm::vec3 horizontalVel = moveDir * (playerCamera.movementSpeed * speedMultiplier);
        playerCamera.velocity.x = horizontalVel.x;
        playerCamera.velocity.z = horizontalVel.z;
        playerCamera.velocity.y += GRAVITY * dt;

        glm::vec3 frameVelocity = playerCamera.velocity * dt;
        glm::dvec3 bottomPos = playerCamera.position;
        bottomPos.y -= PLAYER_EYE_HEIGHT;

        overworld.resolveCollision(bottomPos, frameVelocity, playerCamera.dimensions, playerCamera.onGround);

        playerCamera.position = bottomPos;
        playerCamera.position.y += PLAYER_EYE_HEIGHT;
        playerCamera.velocity.y = frameVelocity.y / dt;
    }

    if (glm::length(playerCamera.velocity) > 0.0f || !playerCamera.onGround) {
        playerCamera.updateMatrices(WINDOW_WIDTH, WINDOW_HEIGHT);
    }
}

void Engine::handleMouseLook() {
    if (!Input::isMouseCaptured()) return;

    float dx = static_cast<float>(Input::consumeMouseDX());
    float dy = static_cast<float>(Input::consumeMouseDY());

    if (dx != 0.0f || dy != 0.0f) {
        cam.updateCameraRotation(playerCamera, dx, dy, playerCamera.sensitivity / 100.0f);
        viewDir = glm::normalize(playerCamera.getForwardVector());
        playerCamera.updateMatrices(WINDOW_WIDTH, WINDOW_HEIGHT);
    }
}

void Engine::handleBlockInteraction() {
    RaycastHit hitPos = { glm::ivec3(0), glm::ivec3(0) };

    // Place Block (Right Click)
    if (Input::mouseDown(GLFW_MOUSE_BUTTON_RIGHT) && playerCamera.blockPlaceCooldown <= 0.0f) {
        if (overworld.raycast(playerCamera.position, playerCamera.getForwardVector(), 10.0f, hitPos)) {
            glm::ivec3 placePos = hitPos.block + hitPos.normal;
            glm::dvec3 playerBottom = playerCamera.position;
            playerBottom.y -= PLAYER_EYE_HEIGHT;

            glm::ivec3 pMin = glm::floor(playerBottom - glm::dvec3(playerCamera.dimensions.x / 2.0, 0.0, playerCamera.dimensions.z / 2.0));
            glm::ivec3 pMax = glm::floor(playerBottom + glm::dvec3(playerCamera.dimensions.x / 2.0, playerCamera.dimensions.y, playerCamera.dimensions.z / 2.0));

            // Prevent placing blocks directly inside player bounding box
            if (placePos.x < pMin.x || placePos.x > pMax.x ||
                placePos.y < pMin.y || placePos.y > pMax.y ||
                placePos.z < pMin.z || placePos.z > pMax.z)
            {
                overworld.placeBlock(hitPos, BlockType::DIRECTION);
            }
        }
        playerCamera.blockPlaceCooldown = playerCamera.BLOCK_ACTION_DELAY;
    }

    // Break Block (Left Click)
    if (Input::mouseDown(GLFW_MOUSE_BUTTON_LEFT) && playerCamera.blockBreakCooldown <= 0.0f) {
        if (overworld.raycast(playerCamera.position, playerCamera.getForwardVector(), 10.0f, hitPos)) {
            overworld.breakBlock(hitPos);
        }
        playerCamera.blockBreakCooldown = playerCamera.BLOCK_ACTION_DELAY;
    }
}

void Engine::handleScrollInput() {
    float scrollY = static_cast<float>(Input::consumeScrollDY());
    if (scrollY == 0.0f) return;

    playerCamera.FOV -= scrollY;
    playerCamera.FOV = glm::clamp(playerCamera.FOV, 0.5f, 120.0f);

    playerCamera.sensitivity = 0.3f * (0.01111f * playerCamera.FOV);
    playerCamera.updateMatrices(WINDOW_WIDTH, WINDOW_HEIGHT);

    printToChat("FOV: " + std::to_string(playerCamera.FOV), false, true);
}

/**
 * Render the world and UI.
 * Uses rotation-only view matrix with relative positioning for precision at large distances.
 */
void Engine::render()
{   
	// ----- Setup for rendering -----
    // 
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

    // Connect this buffer to binding point 0
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, lightingUBO);

    // Unbind
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

	// ----- Start rendering the world & UI-----
    // 
    // Draw the overworld
    // Pass rotationOnlyVP for frustum extraction and shader uniforms
    overworld.draw(voxelShader, lightDir, rotationOnlyVP, playerCamera.position, verticesRendered);

	// UI Rendering (Crosshair, Debug Stats, Chat)
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Draw Crosshair
    uiShader.bind();
    glBindVertexArray(uiVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    
	// ----- Start of Debug Stats and Chat Rendering -----
    // Draw Debug Stats (Top Left)
    
    std::stringstream posStream;
    posStream << std::fixed << std::setprecision(3) << "Pos: " 
        << playerCamera.position.x << " " 
        << playerCamera.position.y << " " 
        << playerCamera.position.z;
    std::string posText = posStream.str();

    text->renderText(posText, 10.0f, 40.0f, 1.0f, glm::vec3(1.0f), false);
    
    std::stringstream rotStream;
    float radToDeg = 180 / PI;
    rotStream << std::fixed << std::setprecision(3) << "Rot: "
        << playerCamera.rotation.x * radToDeg << " "
        << playerCamera.rotation.y * radToDeg << " "
        << playerCamera.rotation.z * radToDeg;
    std::string rotText = rotStream.str();

    text->renderText(rotText, 10.0f, 63.0f, 1.0f, glm::vec3(1.0f), false);


    std::string fpsText = "FPS: " + std::to_string((int)currentFPS);
    text->renderText(fpsText, 10.0f, 100.0f, 1.0f, glm::vec3(1.0f, 1.0f, 0.0f), false); // Yellow FPS

    if (debugStatistics)
    {
        // Reusable debug text
        std::string dbgText;

        dbgText = "Verticies: " + std::to_string(verticesRendered);
        text->renderText(dbgText, 10.0f, 130.0f, 1.0f, glm::vec3(1.0f, 0.25f, 0.2f), false); // Red debug information

        dbgText = "Chunks: " + std::to_string(Terrain::getTotalChunksGenerated());
        text->renderText(dbgText, 10.0f, 155.0f, 1.0f, glm::vec3(1.0f, 0.25f, 0.2f), false); // Red debug information

		// Sample climate values at the player's position
        HeightField::TerrainNoise tn(overworld.worldSeed);
		Biome::ClimateSample sample = Biome::sampleClimate(tn, (float)playerCamera.position.x, (float)playerCamera.position.z, overworld.worldSeed);
        dbgText = "c=" + std::to_string(sample.continentalness) + ", e=" + std::to_string(sample.erosion) + ", p=" + std::to_string(sample.continentalness) + "\n\rt=" + std::to_string(sample.continentalness) + ", h=" + std::to_string(sample.continentalness) + ", w=" + std::to_string(sample.weirdness);
        text->renderText(dbgText, 10.0f, 170.0f, 0.5f, glm::vec3(1.0f, 0.25f, 0.2f), false); // Red debug information
    }

    // Draw Chat
    float lineHeight = 25.0f;
    float typingBarHeight = lineHeight * 2.0f + 3.0f; // 

    // 1. Edge-to-edge background bar for active typing space (only 1 line high when chat is open)
    if (isChatOpen) {
        drawRect(0.0f, (float)WINDOW_HEIGHT - typingBarHeight, (float)WINDOW_WIDTH, typingBarHeight, glm::vec4(0.0f, 0.0f, 0.0f, 0.6f));
    }

    // 2. Chat history background (about 30 characters wide)
    int displayedHistoryCount = std::min((int)chatHistory.size(), 20);
    if (displayedHistoryCount > 0) {
        // Measure ~30 characters width + padding
        float historyBgWidth = text ? (text->getTextWidth("123456789012345678901234567890", 0.8f) + 20.0f) : 360.0f;
        float historyBgHeight = displayedHistoryCount * lineHeight + 20.0f;

        float historyBgY = isChatOpen 
            ? ((float)WINDOW_HEIGHT - typingBarHeight - historyBgHeight)
            : ((float)WINDOW_HEIGHT - historyBgHeight - 0.0f);

        

        drawRect(0.0f, historyBgY, historyBgWidth, historyBgHeight, glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));
    }

    // 3. Render text
    float chatY = (float)WINDOW_HEIGHT - 24.0f;

    if (isChatOpen) {
        text->renderText("> " + chatInputString + "_", 10.0f, chatY, 1.0f, glm::vec3(1.0f), false);
        chatY -= lineHeight;
    }

    int messagesDisplayed = 0;
    if (isChatOpen && !chatHistory.empty()) chatY -= lineHeight;
    for (int i = (int)chatHistory.size() - 1; i >= 0; --i) {
        if (messagesDisplayed >= displayedHistoryCount) break;
        messagesDisplayed++;

        text->renderText(chatHistory[i], 10.0f, chatY, 0.8f, glm::vec3(0.9f), true);
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
    overworld.rebuildDirtyChunks(lightDir, playerCamera.position);
}

void Engine::printToChat(const std::string& str, bool mergeWithLastMessage, bool newLine)
{
    if (str.empty()) return;

    std::stringstream ss(str);
    std::string line;
    bool first = true;

    while (std::getline(ss, line, '\n')) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (ss.eof() && line.empty()) {
            break;
        }

        if (first && mergeWithLastMessage) {
            if (chatHistory.empty()) {
                chatHistory.push_back(line);
            } else {
                chatHistory.back() += line;
            }
        } else {
            chatHistory.push_back(line);
        }
        first = false;

        if (chatHistory.size() > MAX_CHAT_HISTORY) {
            chatHistory.erase(chatHistory.begin());
        }
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
    std::cout << "[LOG] Starting game..." << std::endl;

    // Init GLFW
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);

    const char* ProjectName = "OriginBlock";
	const std::string Version = "v0.2b";

    std::cout << "[LOG] Attempting to create window..." << std::endl;
    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, ProjectName, nullptr, nullptr);
    if (!window) return -1;

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0); // 0 = V-Sync OFF (uncapped FPS)

    glfwSetWindowFocusCallback(window, focus_callback);
	glfwSetCursorPos(window, WINDOW_WIDTH / 2.0, WINDOW_HEIGHT / 2.0); // Center mouse initially

    std::cout << "[LOG] Window created. Loading OpenGL pointers..." << std::endl;
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

    engine.overworld.updateLoadedChunks(engine.playerCamera.position, engine.viewDir, engine.overworld.worldSeed);


	// Make sure the player is above the terrain
    // Take the floor of the modulo of the player position with 16 to get the chunk coordinates
	int chunkX = static_cast<int>(std::floor(engine.playerCamera.position.x / 16.0f));
	int chunkZ = static_cast<int>(std::floor(engine.playerCamera.position.z / 16.0f));
	// Take the floor of the AND operator of the player position and 15 to get the local coordinates
	int localX = static_cast<int>(std::floor(engine.playerCamera.position.x)) & 15;
	int localZ = static_cast<int>(std::floor(engine.playerCamera.position.z)) & 15;

	int terrainHeightBelowPlayer = engine.terrain.getOrGenerateColumn(chunkX, chunkZ, engine.overworld.worldSeed)->heightMap[localX][localZ];
	//engine.playerCamera.isFlying = false; // Disable flying so we would collide with the terrain
	while (engine.playerCamera.position.y < terrainHeightBelowPlayer + 5.0f && !engine.overworld.checkCollision(engine.playerCamera.position, engine.playerCamera.dimensions)) {
		engine.playerCamera.position.y += 1.0f; // Move player up until they are above the terrain
	}

    DebugExport debugExport;
    std::string debugFilenameStr = "heightmaps/heightmap" + std::to_string(engine.overworld.worldSeed) + ".png";
    const char* debugFilename = debugFilenameStr.c_str();
    debugExport.writeChunkHeightmapPNG(-80, -80, 160, 160, 16, engine.overworld.worldSeed, debugFilename);

    // File will go to (root or executable directory)/BiomeParamaters/<seed>
	// Will write 6 .png images for each biome parameter (Continentalness, Erosion, Peaks, Temperature, Humidity, Weirdness) with it's respective parameter type as the filename
    debugExport.writeClimateParametersPNG(-500, -500, 1000, 1000, engine.overworld.worldSeed);
    /**/

        
    // Timing
    const float FIXED_DT = 1.0f / 60.0f; // 60 updates/sec
    double previousTime = glfwGetTime();
    double accumulator = 0.0;
    double fpsTimer = 0.0;
    int frameCount = 0;

    // ----- Main loop -----
	std::cout << "[LOG] Entering main game loop!" << std::endl;
    while (!glfwWindowShouldClose(window))
    {
        double currentTime = glfwGetTime();
        double frameTime = currentTime - previousTime;
        previousTime = currentTime;

        // 1. Clamp frameTime to prevent the "Spiral of Death" during lag spikes/pauses
        if (frameTime > 0.25)
        {
            frameTime = 0.25;
        }

        accumulator += frameTime;
        fpsTimer += frameTime;

        // 2. Fetch OS events & prepare input state ONCE per frame
        glfwPollEvents();
        Input::newFrame();

        // 3. Process inputs ONCE per frame using frameTime
        if (mainWindowFocused)
        {
            engine.handleInputs(window, static_cast<float>(frameTime));
        }

        // 4. Fixed-rate physics & simulation updates (runs 0, 1, or multiple times)
        while (accumulator >= FIXED_DT)
        {
            float dt = FIXED_DT;

            // Block interaction cooldowns (tick at fixed rate)
            if (engine.playerCamera.blockPlaceCooldown > 0.0f) engine.playerCamera.blockPlaceCooldown -= dt;
            if (engine.playerCamera.blockBreakCooldown > 0.0f) engine.playerCamera.blockBreakCooldown -= dt;

            // Step physics & world state
            engine.update();

            accumulator -= FIXED_DT;
        }

        // 5. Render frame
        glm::vec3 fc = engine.overworld.lightData.fogColor;
        glClearColor(fc.r, fc.g, fc.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        engine.voxelShader.bind();
        engine.render();

        glfwSwapBuffers(window);
        frameCount++;

        // 6. Smooth FPS calculation (updates twice per second)
        if (fpsTimer >= 0.5)
        {
            engine.currentFPS = frameCount / static_cast<float>(fpsTimer);
            frameCount = 0;
            fpsTimer = 0.0;
        }
    }

    glfwTerminate();
    return 0;
}

