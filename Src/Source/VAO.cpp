#include"../Header/VAO.h"
VAO::VAO()
{
	glGenVertexArrays(1, &ID);
}
void VAO::LinkAttrib(VBO& VBO, GLuint layout, GLuint numComponents, GLenum type, GLsizeiptr stride, void* offset,bool value)
{
	VBO.Bind();
	glVertexAttribPointer(layout, numComponents, type, value, stride, offset);
	glEnableVertexAttribArray(layout);
	VBO.Unbind();
}
void VAO::Bind()
{
	glBindVertexArray(ID);
}
void VAO::Unbind()
{
	glBindVertexArray(0);
}
void VAO::Delete()
{
	glDeleteVertexArrays(1, &ID);
}