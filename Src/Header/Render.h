#ifndef RENDER_CLASS_H
#define RENDER_CLASS_H
#include "Mesh.h"
#include "LitShader.h"
#include "ShadowShader.h"
#include "PostProcessingShader.h"
#include "Window.h"
#include <unordered_map>
#include "SkyAtmosphere.h"
#include "VoxelShader.h"
#include "GLTexture.h"
#include "GLBuffer.h"
#include <entt/entt.hpp>
#include "Components.h"
#include <map> 
class DefferedShader;
class UI;
class CullingShader;
struct LightGrid {
	uint32_t offset;
	uint32_t count;
};
struct ClusterAABB {
	glm::vec4 minPoint; 	glm::vec4 maxPoint;
};
struct RenderInstance {
	entt::entity entity;
	Material* material;
};
struct RenderBatch {
	Mesh* mesh;
	int commandOffset; // Сдвиг в глобальном буфере
	int instanceCount;
};
// Components.h:
struct alignas(16) ObjectData {
	glm::mat4 modelMatrix;  // 64 байта
	GLuint materialID;      // 4 байта
	GLuint meshID;          // 4 байта
	GLuint padding;         // 4 байта
	GLuint padding2;        // 4 байта ← добавь это
	// итого ровно 80 байт
};
struct alignas(16) MeshLODInfo {
	GLuint countLOD0, firstIndexLOD0;
	GLuint countLOD1, firstIndexLOD1;
	GLuint countLOD2, firstIndexLOD2;
	GLuint pad1, pad2;
};
class Render {
public:
	Render();
public:
	GLBuffer materialBuffer;
	GLBuffer clusterBuffer;
	GLBuffer lightGridBuffer;
	GLBuffer globalLightIndexBuffer;
	GLBuffer atomicCounterBuffer;
	GLBuffer uboLights;
	GLBuffer meshInfoBuffer;
	GLuint hdrOutputTexture;
	SkyAtmosphere sky; // Твое небо
	glm::vec3 sunDir = glm::vec3(0.0f, -1.0f, 0.0f);
	GLuint gDepth;
	GLuint gBufferFBO;
	GLuint gNormalRoughness;
	GLuint gAlbedoMetallic;
	GLuint gHeightAO;
	GLuint rboDepth;
	GLuint hdrFBO;
	// Метод инициализации
	void InitGBuffer(int width, int height);

	// Главная функция стала гораздо короче
	void Draw(entt::registry& registry, LitShader& geometryShader, ShadowShader& shadowshader, PostProcessingShader& postprocessingshader, Window& window, Camera& camera, double crntTime, UI& ui, CullingShader& cullingshader, DefferedShader& defferedShader);

	void UpdateClusterGrid(Camera& camera, Window& window, CullingShader& cullingshader);
	glm::mat4 CalculateCalculateCSMMatrix(float nearP, float farP, Camera& camera, float shadowSize);


	std::vector<RenderBatch> mainDraws;
	std::vector<RenderBatch> staticShadowDraws;
	std::vector<RenderBatch> dynamicShadowDraws;

	int totalMainInstances = 0;
	int totalStaticShadowInstances = 0;
	int totalDynamicShadowInstances = 0;

	GLBuffer globalObjectBuffer;
	GLBuffer globalSphereBuffer;
	GLBuffer globalCommandBuffer;
	glm::vec3 cachedCamPos = glm::vec3(0.0f);
	bool isSceneDirty = true;
	std::vector<MaterialGPUData> cachedMaterialDataArray;
	std::map<Material*, int> cachedMaterialToIndex;
	std::map<Mesh*, std::vector<RenderInstance>> cachedMainBatches;
	std::map<Mesh*, std::vector<RenderInstance>> cachedShadowBatches;
	void CSMGPUCulling(int offset, int count, const glm::mat4& sunViewProj,
		const glm::vec3& camPos, const glm::mat4& camView,
		const glm::vec3& sunDir, float cascadeNear, float cascadeFar,
		CullingShader& cullingshader, Window& window);
	// Функция пересборки (вызывается только при изменениях)
	void RebuildBatches(entt::registry& registry);

private:
	

	void UpdateTransforms(entt::registry& registry);

	glm::vec3 SetupLightsAndUBO(entt::registry& registry, ShadowShader& shadowshader);

	// Вспомогательные функции тоже меняются (теперь они принимают std::vector<entt::entity> вместо std::vector<GameObject*>)
	void GlobalGPUCulling(int offset, int count, const glm::mat4& viewProj, const glm::vec3& camPos, bool isShadowPass, CullingShader& cullingshader, Window& window, GLuint hiZTex);

	std::vector<glm::mat4> RenderCSM(Camera& camera, std::map<Mesh*, std::vector<RenderInstance>>& shadowBatches, entt::registry& registry, ShadowShader& shadowshader, CullingShader& cullingshader, Window& window);

	void RenderAtlasShadows(Camera& camera, entt::registry& registry, ShadowShader& shadowshader, CullingShader& cullingshader, Window& window);

	void RenderMainPass(Camera& camera, Window& window, std::map<Mesh*, std::vector<RenderInstance>>& mainBatches, entt::registry& registry, std::vector<glm::mat4>& sunMatrices, LitShader& geometryShader, DefferedShader& deferredShader, ShadowShader& shadowshader, CullingShader& cullingshader, PostProcessingShader& ppShader);
};
#endif