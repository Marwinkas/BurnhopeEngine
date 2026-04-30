#ifndef GL_TEXTURE_H
#define GL_TEXTURE_H

#include <glad/glad.h>
#include <cmath>
#include <algorithm>

class GLTexture {
public:
    GLuint ID;
    GLenum target; // GL_TEXTURE_2D, GL_TEXTURE_3D, GL_TEXTURE_2D_ARRAY

    GLTexture() : ID(0), target(0) {}

    // Метод для создания 3D текстуры (для твоих вокселей)
    void Create3D(int width, int height, int depth, GLenum internalFormat, bool generateMipmaps = false) {
        target = GL_TEXTURE_3D;
        glGenTextures(1, &ID);
        glBindTexture(target, ID);

        int mipLevels = 1;
        if (generateMipmaps) {
            mipLevels = 1 + (int)std::floor(std::log2(std::max({ width, height, depth })));
        }

        // glTexStorage делает текстуру "неизменяемой" в размерах — это работает быстрее
        glTexStorage3D(target, mipLevels, internalFormat, width, height, depth);
    }

    // Удобная настройка фильтрации
    void SetFilter(GLenum minFilter, GLenum magFilter) {
        glBindTexture(target, ID);
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, minFilter);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, magFilter);
    }

    // Удобная настройка границ (Clamp, Repeat и т.д.)
    void SetWrap(GLenum wrapMode) {
        glBindTexture(target, ID);
        glTexParameteri(target, GL_TEXTURE_WRAP_S, wrapMode);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, wrapMode);
        if (target == GL_TEXTURE_3D || target == GL_TEXTURE_CUBE_MAP || target == GL_TEXTURE_2D_ARRAY) {
            glTexParameteri(target, GL_TEXTURE_WRAP_R, wrapMode);
        }
    }

    // Установка цвета границы
    void SetBorderColor(float r, float g, float b, float a) {
        glBindTexture(target, ID);
        float color[] = { r, g, b, a };
        glTexParameterfv(target, GL_TEXTURE_BORDER_COLOR, color);
    }

    void Bind(GLuint slot) {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(target, ID);
    }

    void Delete() { glDeleteTextures(1, &ID); }
};

#endif