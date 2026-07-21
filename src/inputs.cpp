#include "pch.h"

#include "inputs.hpp"

GLFWwindow* Input::window = nullptr;

std::unordered_set<uint32_t> Input::chars = {};
bool Input::keys[512] = {};
bool Input::prevKeys[512] = {};
bool Input::pressed[512] = {};
bool Input::usedKeys[512] = {};

bool Input::mouseButtons[16] = {};
bool Input::prevMouseButtons[16] = {};
bool Input::mousePressedArr[16] = {};

double Input::mouseX = 0.0;
double Input::mouseY = 0.0;
double Input::prevMouseX = 0.0;
double Input::prevMouseY = 0.0;
double Input::deltaX = 0.0;
double Input::deltaY = 0.0;
double Input::scrollDX = 0.0;
double Input::scrollDY = 0.0;

bool Input::mouseCaptured = false;
bool Input::hasMouse = false;

void Input::init(GLFWwindow* win) {
    window = win;

    // Set the callbacks
    glfwSetCharCallback(window, characterCallback);
    glfwSetKeyCallback(win, keyCallback);
    glfwSetMouseButtonCallback(win, mouseButtonCallback);
    glfwSetScrollCallback(win, scrollCallback);
    glfwSetCursorPosCallback(win, cursorPosCallback);
    glfwSetWindowFocusCallback(win, focusCallback);
}

void Input::newFrame()
{
    for (int i = 0; i < 512; i++)
    {
        pressed[i] = keys[i] && !prevKeys[i];
        prevKeys[i] = keys[i];
    }

    if (mouseCaptured && hasMouse)
    {
        deltaX += mouseX - prevMouseX;
        deltaY += mouseY - prevMouseY;
    }

    prevMouseX = mouseX;
    prevMouseY = mouseY;
}

// Key accessors
bool Input::keyDown(int k) { return keys[k]; }
bool Input::charDown(uint32_t codepoint) {
    if (chars.find(codepoint) != chars.end()) {
        chars.erase(codepoint);
        return true;
	}
    return false;
}
bool Input::keyPressed(int key) {
    if (key < 0 || key >= 512) return false;
    return pressed[key];  // This is already computed correctly in newFrame()
}

// Mouse accessors
bool Input::mouseDown(int button) {
    return button >= 0 && button < 16 && mouseButtons[button];
}
bool Input::mousePressed(int button) {
    return button >= 0 && button < 16 && mousePressedArr[button];
}

// Consume mouse delta (resets after each call)
double Input::consumeMouseDX() { double dx = deltaX; deltaX = 0.0; return dx; }
double Input::consumeMouseDY() { double dy = deltaY; deltaY = 0.0; return dy; }

double Input::consumeScrollDX() { double dx = scrollDX; scrollDX = 0.0; return dx; }
double Input::consumeScrollDY() { double dy = scrollDY; scrollDY = 0.0; return dy; }

void Input::setMouseCaptured(bool captured) {
    mouseCaptured = captured;
    glfwSetInputMode(window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

    // Reset previous mouse positions to avoid jump
    glfwGetCursorPos(window, &prevMouseX, &prevMouseY);
    mouseX = prevMouseX;
    mouseY = prevMouseY;

    deltaX = deltaY = 0.0;
}

bool Input::isMouseCaptured() { return mouseCaptured; }

// ---------------- Callbacks ----------------

void Input::characterCallback(GLFWwindow* window, unsigned int codepoint) {
	chars.insert(codepoint); // Avoids duplicates by nature
}

void Input::keyCallback(GLFWwindow*, int key, int, int action, int) {
    if (key >= 0 && key < 512) keys[key] = (action != GLFW_RELEASE);
}

void Input::mouseButtonCallback(GLFWwindow*, int button, int action, int) {
    if (button >= 0 && button < 16)
        mouseButtons[button] = (action != GLFW_RELEASE);
}

void Input::scrollCallback(GLFWwindow*, double xoffset, double yoffset) {
    scrollDX += xoffset;
    scrollDY += yoffset;
}

void Input::cursorPosCallback(GLFWwindow*, double x, double y) {
    mouseX = x;
    mouseY = y;
    hasMouse = true;
}

void Input::focusCallback(GLFWwindow*, int focused) {
    if (!focused) {
        setMouseCaptured(false);
        hasMouse = false;
    }
}
