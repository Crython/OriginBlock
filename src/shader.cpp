#include "pch.h"
#include "Shader.hpp"


std::string Shader::loadTextFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cout << "Failed to open shader file: " << path << "\n";
        return "";
	}
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

GLuint Shader::compile(GLenum type, const std::string& source) {
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    // Error handling
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        std::cout << "Shader compile error:\n" << log << "\n";
    }

    return shader;
}

void Shader::load(const std::string& vertexPath, const std::string& fragmentPath) {
    std::string vertexCode = loadTextFile(vertexPath);
    std::string fragmentCode = loadTextFile(fragmentPath);
    
    GLuint vs = compile(GL_VERTEX_SHADER, vertexCode);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragmentCode);

    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    // Error handling
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        std::cout << "Shader link error:\n" << log << "\n";
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
}

void Shader::setVec3(const std::string& name, const glm::vec3& value) const
{
    GLint loc = glGetUniformLocation(program, name.c_str());
    if (loc == -1) {
        // Optional but recommended during development
        std::cout << "Uniform not found: " << name << "\n";
        return;
    }
    glUniform3fv(loc, 1, &value[0]);
}

void Shader::setVec4(const std::string& name, const glm::vec4& value) const
{
    GLint loc = glGetUniformLocation(program, name.c_str());
    if (loc == -1) {
        // Optional but recommended during development
        std::cout << "Uniform not found: " << name << "\n";
        return;
    }
    glUniform4fv(loc, 1, &value[0]);
}

glm::mat4 Shader::getView() const {
    GLint loc = glGetUniformLocation(program, "uView");
    glm::mat4 view(1.0f);
    if (loc != -1) {
        glGetUniformfv(program, loc, &view[0][0]);
    }
    return view;
}

glm::mat4 Shader::getProjection() const {
    GLint loc = glGetUniformLocation(program, "uProjection");
    glm::mat4 proj(1.0f);
    if (loc != -1) {
        glGetUniformfv(program, loc, &proj[0][0]);
    }
    return proj;
}

