#pragma once

#include "../Render/Camera.hpp"
#include "Descriptors.hpp"


#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

namespace burnhope {

	

    struct GlobalUbo {
        glm::mat4 projection{ 1.f };    
        glm::mat4 invViewProj{ 1.f };
        glm::mat4 view{ 1.f };
        alignas(16) glm::vec3 camPos;
        float zNear{ 0.1f };
        alignas(16) glm::vec3 sunDir{ 0.0f, -1.0f, 0.0f };
        float zFar{ 1000.0f };
        glm::vec4 screenSize;
        glm::mat4 sunLightSpaceMatrices[4];
        glm::vec4 cascadeSplits;
        uint32_t gridDimX{ 16 };
        uint32_t gridDimY{ 9 };
        uint32_t gridDimZ{ 24 };
        float lightSize{ 20.0f };
    };

	
	struct FrameInfo {
		int frameIndex;
		float frameTime;
		VkCommandBuffer commandBuffer;
		Camera& camera;
		VkDescriptorSet globalDescriptorSet;
		BurnhopeDescriptorPool& frameDescriptorPool; 
	};

}  