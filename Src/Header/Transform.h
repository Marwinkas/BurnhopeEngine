#ifndef TRANSFORM_CLASS_H
#define TRANSFORM_CLASS_H
#include<glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include<glad/glad.h>
#include<vector>
class Transform {
public:
	glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f);
	glm::vec3 rotation = glm::vec3(0.0f, 0.0f, 0.0f);
	glm::vec3 scale = glm::vec3(1.0f, 1.0f, 1.0f);
	glm::mat4 matrix = glm::mat4(1.0f);
	bool updatematrix = true;
};
#endif