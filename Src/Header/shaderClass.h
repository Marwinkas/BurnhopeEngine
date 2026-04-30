#ifndef SHADER_CLASS_H
#define SHADER_CLASS_H
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#define NOMINMAX
#include <windows.h>
#include <string>
#include <filesystem>
#include <string>
#include <vector>
#include <iostream> 
#include <fstream>

std::string get_file_contents(const char* filename);

class Shader
{
public:
    GLuint ID;
    Shader() : ID(0) {}
    Shader(const char* computeFile);
    Shader(const char* vertexFile, const char* fragmentFile);
    Shader(const char* vertexFile, const char* fragmentFile, const char* geometryFile);

    void Activate();
    void Delete();

    virtual void Init() {}

    void Set(GLint loc, float val) { glUniform1f(loc, val); }
    void Set(GLint loc, const std::vector<float>& values) {
        glUniform1fv(loc, (GLsizei)values.size(), values.data());
    }

    // Если ты используешь обычный массив C++ (как float lodDist[2])
    void Set(GLint loc, int count, const float* values) {
        glUniform1fv(loc, count, values);
    }
    void Set(GLint loc, int val) { glUniform1i(loc, val); }
    void Set(GLint loc, const glm::vec3& val) { glUniform3fv(loc, 1, &val[0]); }
    void Set(GLint loc, float v0, float v1, float v2) {
        glUniform3f(loc, v0, v1, v2);
    }

    // Аналог glUniform2f (два числа)
    void Set(GLint loc, float v0, float v1) {
        glUniform2f(loc, v0, v1);
    }

    // Аналог glUniform4f (четыре числа, например для цвета RGBA)
    void Set(GLint loc, float v0, float v1, float v2, float v3) {
        glUniform4f(loc, v0, v1, v2, v3);
    }

    void Set(GLint loc, GLuint textureID, int slot, GLenum target = GL_TEXTURE_2D) {
        // Активируем нужный "карман" (слот) на видеокарте
        glActiveTexture(GL_TEXTURE0 + slot);
        // Кладем в него текстуру
        glBindTexture(target, textureID);
        // Говорим шейдеру: "Смотри в карман номер N"
        glUniform1i(loc, slot);
    }

    void Set(GLint loc, const glm::vec4& val) { glUniform4fv(loc, 1, &val[0]); }
    void Set(GLint loc, const glm::mat4& val) { glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(val)); }
    void Set(GLint loc, unsigned int val) { glUniform1ui(loc, val); }

    void Set(GLint loc, const std::vector<glm::mat4>& matrices) {
        glUniformMatrix4fv(loc, matrices.size(), GL_FALSE, glm::value_ptr(matrices[0]));
    }
    void compileErrors(unsigned int shader, const char* type);
protected:
    // Помогатор для поиска локаций внутри Init()
    GLint GetLoc(const char* name) { return glGetUniformLocation(ID, name); }
};

#endif