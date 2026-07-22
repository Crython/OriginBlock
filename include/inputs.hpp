#ifndef INPUTS_HPP
#define INPUTS_HPP

class Input {
public:
    Input() = delete;

    static void init(GLFWwindow* window);
    static void newFrame(); // Call once per frame AFTER glfwPollEvents

    // Keyboard
    static bool keyDown(int key);
    static bool keyPressed(int key);

    // Character buffer (Consumes and clears characters when read)
    static std::vector<uint32_t> consumeCharBuffer();

    // Mouse
    static bool mouseDown(int button);
    static bool mousePressed(int button);

    static double consumeMouseDX();
    static double consumeMouseDY();
    static double consumeScrollDX();
    static double consumeScrollDY();

    static void setMouseCaptured(bool captured);
    static bool isMouseCaptured();

private:
    static void characterCallback(GLFWwindow* window, unsigned int codepoint);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void cursorPosCallback(GLFWwindow* window, double x, double y);
    static void focusCallback(GLFWwindow* window, int focused);

private:
    static GLFWwindow* window;

    static std::vector<uint32_t> charBuffer;

    static bool keys[512];
    static bool prevKeys[512];
    static bool pressed[512];

    static bool mouseButtons[16];
    static bool prevMouseButtons[16];
    static bool mousePressedArr[16];

    static double mouseX, mouseY;
    static double prevMouseX, prevMouseY;
    static double deltaX, deltaY;
    static double scrollDX, scrollDY;

    static bool mouseCaptured;
    static bool hasMouse;
};

#endif