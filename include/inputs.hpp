#ifndef INPUTS_HPP
#define INPUTS_HPP

class Input {
public:
    static void init(GLFWwindow* window);
    static void newFrame(); // call once per frame AFTER glfwPollEvents

    static bool keyDown(int key);    // held
	static bool charDown(uint32_t codepoint); // unicode character input
    static bool keyPressed(int key); // pressed this frame (can be queried multiple times safely)

    static bool mouseDown(int button);    // held
    static bool mousePressed(int button); // pressed this frame

    static double consumeMouseDX();
    static double consumeMouseDY();

    static double consumeScrollDX();
    static double consumeScrollDY();

    static void setMouseCaptured(bool captured);
    static bool isMouseCaptured();


private:
	static void characterCallback(GLFWwindow*, unsigned int codepoint);
    static void keyCallback(GLFWwindow*, int key, int, int action, int);
    static void mouseButtonCallback(GLFWwindow*, int button, int action, int);
    static void scrollCallback(GLFWwindow*, double xoffset, double yoffset);
    static void cursorPosCallback(GLFWwindow*, double x, double y);
    static void focusCallback(GLFWwindow*, int focused);

private:
    static GLFWwindow* window;

	static std::unordered_set<uint32_t> chars; // Used for unicode character input
    static bool keys[512];
    static bool prevKeys[512];
    static bool pressed[512];
    static bool usedKeys[512];

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
