
#ifndef SHADER_HPP
#define SHADER_HPP

class Shader {
public:
    GLuint program = 0;

    Shader() = default;
    Shader(const std::string& vertexPath, const std::string& fragmentPath) {
        load(vertexPath, fragmentPath);
    }

    void load(const std::string& vertexPath, const std::string& fragmentPath);

    void bind() const {
        glUseProgram(program);
    }

    void unbind() const {
        glUseProgram(0);
    }

    // Uniform helpers
    void setMat4(const std::string& name, const glm::mat4& m) const {
        glUniformMatrix4fv(glGetUniformLocation(program, name.c_str()),
            1, GL_FALSE, &m[0][0]);
    }
    void setVec3(const std::string& name, const glm::vec3& value) const;
    
	void setVec4(const std::string& name, const glm::vec4& value) const;

    void setInt(const std::string& name, int v) const {
        glUniform1i(glGetUniformLocation(program, name.c_str()), v);
    }
    void setFloat(const std::string& name, int v) const {
        glUniform1f(glGetUniformLocation(program, name.c_str()), v);
    }

	glm::mat4 getView() const;
	glm::mat4 getProjection() const;

private:
    GLuint compile(GLenum type, const std::string& source);
    std::string loadTextFile(const std::string& path);
};



#endif // SHADER_HPP