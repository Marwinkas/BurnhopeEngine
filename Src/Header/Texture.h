#ifndef TEXTURE_CLASS_H
#define TEXTURE_CLASS_H
#include<glad/glad.h>
#include"stb_image.h"
#include"shaderClass.h"
class Texture
{
public:
	GLuint ID;
	GLuint64 handle = 0;
	const char* type;
	GLuint unit;
	GLuint FBO = 0;
	GLuint RBO = 0;
	Texture(int width, int height, const char* texType, GLuint slot);
	Texture();
	Texture(const char* image, const char* texType, GLuint slot, int typemap);
	void makeResident();
	void texUnit(Shader& shader, const char* uniform, GLuint unit);
	void Bind();
	void Unbind();
	void Delete();
};
#endif