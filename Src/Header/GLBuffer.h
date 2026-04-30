#ifndef GL_BUFFER_H
#define GL_BUFFER_H

#include <glad/glad.h>
#include <cstddef>

class GLBuffer {
public:
    GLuint ID;
    GLenum target; // GL_SHADER_STORAGE_BUFFER, GL_UNIFORM_BUFFER и т.д.

    GLBuffer() : ID(0), target(0) {}

    // Умный конструктор: создает, выделяет память и сразу биндит на нужный слот!
    GLBuffer(GLenum targetType, size_t size, const void* data, GLenum usage, GLuint bindingPoint) {
        target = targetType;
        glGenBuffers(1, &ID);
        glBindBuffer(target, ID);
        glBufferData(target, size, data, usage);

        // Сразу привязываем к слоту (например, 2 для SSBO материалов)
        glBindBufferBase(target, bindingPoint, ID);

        // Отвязываем, чтобы случайно не испортить
        glBindBuffer(target, 0);
    }

    // Быстрое обновление данных (например, когда очищаешь atomic counter)
    void UpdateData(size_t offset, size_t size, const void* data) {
        glBindBuffer(target, ID);
        glBufferSubData(target, offset, size, data);
        glBindBuffer(target, 0);
    }
    // Добавь это внутрь класса GLBuffer:
    void SetData(size_t size, const void* data, GLenum usage = GL_DYNAMIC_DRAW) {
        glBindBuffer(target, ID);
        glBufferData(target, size, data, usage);
        glBindBuffer(target, 0);
    }
    void Bind() { glBindBuffer(target, ID); }
    void BindAs(GLenum customTarget) {
        glBindBuffer(customTarget, ID);
    }
    void Unbind() { glBindBuffer(target, 0); }
    void Delete() { glDeleteBuffers(1, &ID); }
};

#endif