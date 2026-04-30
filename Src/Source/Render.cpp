	#include "../Header/Render.h"
	#include "../Header/UI.h" 
	#include "../Header/CullingShader.h"
	#include "../Header/DefferedShader.h"
	#include "Tracy/tracy/Tracy.hpp"

void Render::RebuildBatches(entt::registry& registry) {
    mainDraws.clear();
    staticShadowDraws.clear();
    dynamicShadowDraws.clear();
    cachedMaterialDataArray.clear();
    cachedMaterialToIndex.clear();

    std::vector<ObjectData> objData;
    std::vector<BoundingSphere> sphereData;
    std::vector<DrawCommand> cmdData;
    std::vector<MeshLODInfo> meshLODDataArray;
    std::map<Mesh*, int> meshToIndex;

    auto renderView = registry.view<MeshComponent, TransformComponent>();

    // Умная лямбда, чтобы не дублировать код!
    auto processGroup = [&](std::vector<RenderBatch>& drawList, bool isShadow, bool reqStatic, bool reqDynamic) {
        for (auto entity : renderView) {
            auto& meshComp = renderView.get<MeshComponent>(entity);
            if (!meshComp.isVisible) continue;
            if (isShadow && !meshComp.castShadow) continue;
            if (reqStatic && !meshComp.isStatic) continue;
            if (reqDynamic && meshComp.isStatic) continue;

            auto& transform = registry.get<TransformComponent>(entity).transform;
            float maxScale = std::max(transform.scale.x, std::max(transform.scale.y, transform.scale.z));
            glm::vec3 worldPos = glm::vec3(transform.matrix[3]);

            for (auto& subMesh : meshComp.renderer.subMeshes) {
                // 1. ЗАПОЛНЯЕМ ИНФО О МЕШЕ (ЛОДЫ)
                if (meshToIndex.find(subMesh.mesh) == meshToIndex.end()) {
                    meshToIndex[subMesh.mesh] = meshLODDataArray.size();

                    MeshLODInfo lodInfo = {};
                    // Безопасно берем ЛОДы (если у меша их нет, дублируем нулевой)
                    lodInfo.countLOD0 = subMesh.mesh->lods.size() > 0 ? subMesh.mesh->lods[0].count : subMesh.mesh->indices.size();
                    lodInfo.firstIndexLOD0 = subMesh.mesh->lods.size() > 0 ? subMesh.mesh->lods[0].firstIndex : 0;

                    lodInfo.countLOD1 = subMesh.mesh->lods.size() > 1 ? subMesh.mesh->lods[1].count : lodInfo.countLOD0;
                    lodInfo.firstIndexLOD1 = subMesh.mesh->lods.size() > 1 ? subMesh.mesh->lods[1].firstIndex : lodInfo.firstIndexLOD0;

                    lodInfo.countLOD2 = subMesh.mesh->lods.size() > 2 ? subMesh.mesh->lods[2].count : lodInfo.countLOD1;
                    lodInfo.firstIndexLOD2 = subMesh.mesh->lods.size() > 2 ? subMesh.mesh->lods[2].firstIndex : lodInfo.firstIndexLOD1;

                    meshLODDataArray.push_back(lodInfo);
                }
                if (cachedMaterialToIndex.find(subMesh.material) == cachedMaterialToIndex.end()) {
                    cachedMaterialToIndex[subMesh.material] = cachedMaterialDataArray.size();
                    cachedMaterialDataArray.push_back(subMesh.material->getGPUData());
                }
                else {
                    // Обновляем данные материала — вдруг текстуры догрузились
                    int idx = cachedMaterialToIndex[subMesh.material];
                    cachedMaterialDataArray[idx] = subMesh.material->getGPUData();
                }
                // 2. ОБНОВЛЯЕМ ОБЪЕКТ
                int globalIdx = objData.size();

                ObjectData obj;
                obj.modelMatrix = transform.matrix;
                obj.materialID = isShadow ? 0 : cachedMaterialToIndex[subMesh.material];
                obj.meshID = meshToIndex[subMesh.mesh]; // Привязываем меш!
                objData.push_back(obj);

                sphereData.push_back({ glm::vec4(worldPos, subMesh.mesh->boundingRadius * maxScale) });

                DrawCommand cmd = {};
                cmd.count = subMesh.mesh->indices.size();
                cmd.instanceCount = 1;
                cmd.firstIndex = 0;
                cmd.baseVertex = 0;
                cmd.baseInstance = globalIdx; // ТОЧНАЯ ССЫЛКА НА ГЛОБАЛЬНЫЙ МАССИВ!
                cmdData.push_back(cmd);

                drawList.push_back({ subMesh.mesh, globalIdx, 1 });
            }
        }
        };

    processGroup(mainDraws, false, false, false);
    totalMainInstances = mainDraws.size();

    processGroup(staticShadowDraws, true, true, false);
    totalStaticShadowInstances = staticShadowDraws.size();

    processGroup(dynamicShadowDraws, true, false, true);
    totalDynamicShadowInstances = dynamicShadowDraws.size();

    // ЗАГРУЖАЕМ В ВИДЕОКАРТУ 1 РАЗ!
    if (!objData.empty()) {
        globalObjectBuffer.SetData(objData.size() * sizeof(ObjectData), objData.data());
        globalSphereBuffer.SetData(sphereData.size() * sizeof(BoundingSphere), sphereData.data());
        globalCommandBuffer.SetData(cmdData.size() * sizeof(DrawCommand), cmdData.data());
    }
    if (!cachedMaterialDataArray.empty()) {
        // ДЕБАГ
        for (int i = 0; i < cachedMaterialDataArray.size(); i++) {
            std::cout << "[GPU MAT " << i << "] hasNormal="
                << cachedMaterialDataArray[i].hasNormal
                << " normalHandle=" << cachedMaterialDataArray[i].normalHandle
                << std::endl;
        }
        materialBuffer.SetData(cachedMaterialDataArray.size() * sizeof(MaterialGPUData),
            cachedMaterialDataArray.data());
    }
    if (!meshLODDataArray.empty()) {
        meshInfoBuffer.SetData(meshLODDataArray.size() * sizeof(MeshLODInfo), meshLODDataArray.data());
    }
    isSceneDirty = false;
    std::cout << "[AZDO] Глобальные буферы созданы. Объектов: " << objData.size() << std::endl;
}
	void UpdateMatrices(entt::registry& registry, entt::entity entity, glm::mat4 parentMatrix) {
		auto& transformComp = registry.get<TransformComponent>(entity);
		glm::mat4 localMatrix = glm::mat4(1.0f);

		ImGuizmo::RecomposeMatrixFromComponents(
			glm::value_ptr(transformComp.transform.position),
			glm::value_ptr(transformComp.transform.rotation),
			glm::value_ptr(transformComp.transform.scale),
			glm::value_ptr(localMatrix)
		);

		transformComp.transform.matrix = parentMatrix * localMatrix;

		// Если у сущности есть дети, обновляем их тоже
		if (auto* hierarchy = registry.try_get<HierarchyComponent>(entity)) {
			for (entt::entity child : hierarchy->children) {
				UpdateMatrices(registry, child, transformComp.transform.matrix);
			}
		}
	}
	Render::Render() {
		materialBuffer = GLBuffer(GL_SHADER_STORAGE_BUFFER, 100 * sizeof(MaterialGPUData), nullptr, GL_DYNAMIC_DRAW, 2);
		clusterBuffer = GLBuffer(GL_SHADER_STORAGE_BUFFER, 16 * 9 * 24 * sizeof(ClusterAABB), nullptr, GL_STATIC_COPY, 3);
		lightGridBuffer = GLBuffer(GL_SHADER_STORAGE_BUFFER, 16 * 9 * 24 * sizeof(LightGrid), nullptr, GL_DYNAMIC_DRAW, 4);
		globalLightIndexBuffer = GLBuffer(GL_SHADER_STORAGE_BUFFER, 16 * 9 * 24 * 100 * sizeof(unsigned int), nullptr, GL_DYNAMIC_DRAW, 5);
		atomicCounterBuffer = GLBuffer(GL_SHADER_STORAGE_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW, 6);
        meshInfoBuffer = GLBuffer(GL_SHADER_STORAGE_BUFFER, 1000 * sizeof(MeshLODInfo), nullptr, GL_DYNAMIC_DRAW, 7);
		uboLights = GLBuffer(GL_UNIFORM_BUFFER, sizeof(LightUBOBlock), NULL, GL_DYNAMIC_DRAW, 0);
        globalObjectBuffer = GLBuffer(GL_SHADER_STORAGE_BUFFER, 10000 * sizeof(ObjectData), nullptr, GL_DYNAMIC_DRAW, 10);
        globalSphereBuffer = GLBuffer(GL_SHADER_STORAGE_BUFFER, 10000 * sizeof(BoundingSphere), nullptr, GL_DYNAMIC_DRAW, 0); // Слот куллинга 0
        globalCommandBuffer = GLBuffer(GL_SHADER_STORAGE_BUFFER, 10000 * sizeof(DrawCommand), nullptr, GL_DYNAMIC_DRAW, 1); // Слот куллинга 1
	};
	void Render::InitGBuffer(int width, int height) {
		glGenFramebuffers(1, &gBufferFBO);
		glBindFramebuffer(GL_FRAMEBUFFER, gBufferFBO);

		// 2. Нормаль (RGB) + Roughness (A) - 16-битный float для точных векторов
        glGenTextures(1, &gNormalRoughness);
        glBindTexture(GL_TEXTURE_2D, gNormalRoughness);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gNormalRoughness, 0);

		// 3. Цвет (RGB) + AO (A) - Обычный 8-битный формат, тут float не нужен
		glGenTextures(1, &gAlbedoMetallic);
		glBindTexture(GL_TEXTURE_2D, gAlbedoMetallic);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gAlbedoMetallic, 0);

        glGenTextures(1, &gHeightAO);
        glBindTexture(GL_TEXTURE_2D, gHeightAO);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gHeightAO, 0);

		// Указываем OpenGL, что рисуем в 3 текстуры одновременно
		GLuint attachments[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
		glDrawBuffers(3, attachments);

		// Буфер глубины (Z-Buffer)
        glGenTextures(1, &gDepth); // Не забудь добавить GLuint gDepth; в Render.h!
        glBindTexture(GL_TEXTURE_2D, gDepth);
        // Используем 32-битный float для идеальной точности
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gDepth, 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cout << "G-Buffer FBO Error!" << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        glGenTextures(1, &hdrOutputTexture);
        glBindTexture(GL_TEXTURE_2D, hdrOutputTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenFramebuffers(1, &hdrFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gDepth, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hdrOutputTexture, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
	void Render::Draw(entt::registry& registry, LitShader& geometryShader, ShadowShader& shadowshader, PostProcessingShader& postprocessingshader, Window& window, Camera& camera, double crntTime, UI& ui, CullingShader& cullingshader, DefferedShader& defferedShader) {
		ZoneScoped;
        cachedCamPos = camera.Position;
		glEnable(GL_CULL_FACE);
		UpdateTransforms(registry);
		if (isSceneDirty) {
			RebuildBatches(registry);
		}

		GLuint zero = 0;
		atomicCounterBuffer.UpdateData(0, sizeof(GLuint), &zero);
		sunDir = SetupLightsAndUBO(registry, shadowshader);
		cullingshader.lightCullingShader.Activate();
		glUniformMatrix4fv(glGetUniformLocation(cullingshader.lightCullingShader.ID, "viewMatrix"), 1, GL_FALSE, glm::value_ptr(camera.GetViewMatrix()));
        glUniformMatrix4fv(glGetUniformLocation(cullingshader.lightCullingShader.ID, "projMatrix"),
            1, GL_FALSE, glm::value_ptr(camera.GetProjectionMatrix(45.0f, 0.1f, 1000.0f)));
        glUniform2f(glGetUniformLocation(cullingshader.lightCullingShader.ID, "screenSize"),
            (float)window.settings.width, (float)window.settings.height);
        glUniform1i(glGetUniformLocation(cullingshader.lightCullingShader.ID, "useOcclusionCull"), 1);

        // Биндим Hi-Z текстуру (она из прошлого кадра — это нормально)
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, postprocessingshader.hiZTexture);
        glUniform1i(glGetUniformLocation(cullingshader.lightCullingShader.ID, "hiZTexture"), 0);

		// Быстрый подсчет источников света (Entt делает это мгновенно)
		auto lightView = registry.view<LightComponent>();
		int lightCount = 0;
		for (auto entity : lightView) {
			if (lightView.get<LightComponent>(entity).light.enable) lightCount++;
		}

		glUniform1ui(glGetUniformLocation(cullingshader.lightCullingShader.ID, "totalLights"), lightCount);
		glDispatchCompute(1, 1, 24);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
        glEnable(GL_DEPTH_CLAMP);
		std::vector<glm::mat4> sunMatrices = RenderCSM(camera, cachedShadowBatches, registry, shadowshader, cullingshader, window);
		RenderAtlasShadows(camera, registry, shadowshader, cullingshader, window);
        glDisable(GL_DEPTH_CLAMP);

		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
		glViewport(0, 0, window.settings.width, window.settings.height);

		RenderMainPass(camera, window, cachedMainBatches, registry, sunMatrices, geometryShader, defferedShader, shadowshader, cullingshader, postprocessingshader);

		glDisable(GL_CULL_FACE);
		
		sky.Draw(camera, sunDir);
		postprocessingshader.Update(window, crntTime, camera, ui, sunDir, uboLights.ID, shadowshader, hdrOutputTexture,gDepth,gNormalRoughness, gAlbedoMetallic);
		FrameMark;

		
	}
	void Render::UpdateTransforms(entt::registry& registry) {
		// Ищем все объекты, у которых ЕСТЬ Transform, но НЕТ HierarchyComponent (или он не является чьим-то ребенком)
		// Для простоты пока просто переберем все Transform, у которых parent == entt::null
		auto view = registry.view<TransformComponent>();

		for (auto entity : view) {
			bool isRoot = true;
			if (auto* hierarchy = registry.try_get<HierarchyComponent>(entity)) {
				if (hierarchy->parent != entt::null) {
					isRoot = false; // Это ребенок, его обновит родитель
				}
			}

			if (isRoot) {
				UpdateMatrices(registry, entity, glm::mat4(1.0f));
			}
		}
	}
    glm::vec3 Render::SetupLightsAndUBO(entt::registry& registry, ShadowShader& shadowshader) {
        LightUBOBlock lightBlock;
        lightBlock.activeLightsCount = 0;

        static glm::vec3 lastSunDir = glm::vec3(0.0f);
        glm::vec3 currentSunDir = glm::vec3(0.0f, -1.0f, 0.0f);

        // Размер атласа и максимальный тайл
        const int atlasRes = shadowshader.atlasResolution; // 4096
        const int maxTile = shadowshader.tileSize;        // 512
        const int gridCount = atlasRes / maxTile;           // 8x8 = 64 слота по 512

        // --- СЧИТАЕМ СЛОТЫ С УЧЁТОМ ДИСТАНЦИИ ---
        // Атлас делим на тайлы 128x128 — минимальный размер
        // 512 = 4x4 тайла по 128, 256 = 2x2, 128 = 1x1
        // Используем простой линейный аллокатор в единицах 128px тайлов
        const int minTile = 128;
        const int atlasInUnits = atlasRes / minTile; // 32 единицы по горизонтали
        // Общий пул слотов в единицах minTile^2
        // Для простоты храним текущую занятую строку и колонку
        int allocX = 0; // текущая позиция в минимальных тайлах
        int allocY = 0;

        auto lightViewAll = registry.view<LightComponent, TransformComponent>();
        for (auto entity : lightViewAll) {
            auto& light = lightViewAll.get<LightComponent>(entity).light;
            auto& transform = lightViewAll.get<TransformComponent>(entity).transform;

            if (!light.enable || !light.castShadows || light.type == LightType::Directional) continue;
            if (light.mobility == LightMobility::Static && light.hasBakedShadows) continue;

            // Определяем размер тайла по дистанции до камеры
            float dist = glm::length(transform.position - cachedCamPos); // см ниже
            int tileSize;
            if (dist < 20.0f)  tileSize = 512;
            else if (dist < 60.0f)  tileSize = 256;
            else                    tileSize = 128;

            light.shadowTileSize = tileSize;

            // Размер в единицах minTile
            int unitsPerTile = tileSize / minTile; // 4, 2 или 1
            int facesCount = (light.type == LightType::Point) ? 6 : 1;

            // Выравниваем аллокатор по размеру тайла
            if (allocX + unitsPerTile * facesCount > atlasInUnits) {
                allocX = 0;
                allocY += unitsPerTile; // следующая строка
            }
            if (allocY + unitsPerTile > atlasInUnits) {
                // Атлас заполнен — отключаем тени для этой лампочки
                light.shadowSlot = -1;
                continue;
            }

            // Слот = индекс тайла maxTile в атласе
            // Конвертируем позицию минимальных тайлов в пиксельные координаты
            // и кодируем как "расширенный слот"
            // Храним пиксельные координаты напрямую в shadowSlot как упакованное int
            // Упаковка: slot = (pixelY / minTile) * atlasInUnits + (pixelX / minTile)
            light.shadowSlot = allocY * atlasInUnits + allocX;
            allocX += unitsPerTile * facesCount;
        }

        // --- Заполняем UBO как раньше, но добавляем tileSize ---
        auto view = registry.view<LightComponent, TransformComponent>();
        for (auto entity : view) {
            auto& lightComp = view.get<LightComponent>(entity);
            auto& transformComp = view.get<TransformComponent>(entity);
            Light& l = lightComp.light;
            Transform& t = transformComp.transform;

            if (!l.enable || l.type == LightType::None) continue;

            if (l.type == LightType::Directional) {
                glm::quat qRot = glm::quat(glm::radians(t.rotation));
                currentSunDir = glm::normalize(qRot * glm::vec3(0.0f, -1.0f, 0.0f));
                if (glm::distance(currentSunDir, lastSunDir) > 0.0001f) {
                    shadowshader.staticShadowsDirty = true;
                    lastSunDir = currentSunDir;
                }
            }

            int i = lightBlock.activeLightsCount;

            lightBlock.lights[i].posType = glm::vec4(t.position, (float)l.type);
            lightBlock.lights[i].colorInt = glm::vec4(l.color, l.intensity);

            glm::quat qRot = glm::quat(glm::radians(t.rotation));
            glm::vec3 direction = glm::normalize(qRot * glm::vec3(0.0f, -1.0f, 0.0f));
            lightBlock.lights[i].dirRadius = glm::vec4(direction, l.radius);

            if (l.type == LightType::Spot) {
                glm::mat4 lightProj = glm::perspective(glm::radians(l.outerCone * 2.0f), 1.0f, 0.1f, 1000.0f);
                glm::mat4 lightView = glm::lookAt(t.position, t.position + direction, glm::vec3(0.0f, 1.0f, 0.0f));
                l.lightSpaceMatrix = lightProj * lightView;
            }

            float inner = glm::cos(glm::radians(l.innerCone));
            float outer = glm::cos(glm::radians(l.outerCone));
            float castsShadows = l.castShadows ? 1.0f : 0.0f;

            // Кодируем shadowSlot и tileSize вместе:
            // shadowParams.w = shadowSlot (как раньше)
            // shadowParams.z = castShadows
            // НОВОЕ: tileSize передаём отдельно через свободный канал
            // Упакуем tileSize в знак shadowSlot: положительный = 512, *-1 = 256, *-2 = 128
            // Проще — добавим отдельный vec4 в структуру. Но структура фиксирована...
            // Кодируем tileSize прямо в shadowSlot: slot * 10000 + tileSize
            float encodedSlot = (float)(l.shadowSlot * 10000 + l.shadowTileSize);

            lightBlock.lights[i].shadowParams = glm::vec4(inner, outer, castsShadows, encodedSlot);
            lightBlock.lights[i].lightSpaceMatrix = l.lightSpaceMatrix;

            lightBlock.activeLightsCount++;
            if (lightBlock.activeLightsCount >= 100) break;
        }

        uboLights.UpdateData(0, sizeof(LightUBOBlock), &lightBlock);
        return currentSunDir;
    }
	void Render::UpdateClusterGrid(Camera& camera, Window& window, CullingShader& cullingshader) {
		cullingshader.clusterGridShader.Activate();
		cullingshader.clusterGridShader.Set(cullingshader.clusterGridShader.loc.zNear, 0.1f);
		cullingshader.clusterGridShader.Set(cullingshader.clusterGridShader.loc.zFar, 1000.0f);
		cullingshader.clusterGridShader.Set(cullingshader.clusterGridShader.loc.gridDimX, 16);
		cullingshader.clusterGridShader.Set(cullingshader.clusterGridShader.loc.gridDimY, 9);
		cullingshader.clusterGridShader.Set(cullingshader.clusterGridShader.loc.gridDimZ, 24);
		cullingshader.clusterGridShader.Set(cullingshader.clusterGridShader.loc.projection, camera.GetProjectionMatrix(45.0f, 0.1f, 1000.0f));
		glDispatchCompute(16, 9, 24);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	}
	
    glm::mat4 Render::CalculateCalculateCSMMatrix(float nearP, float farP, Camera& camera, float shadowSize) {
        glm::mat4 proj = camera.GetProjectionMatrix(45.0f, nearP, farP);
        glm::mat4 invCam = glm::inverse(proj * camera.GetViewMatrix());

        std::vector<glm::vec4> frustumCorners;
        for (int x = 0; x < 2; ++x) {
            for (int y = 0; y < 2; ++y) {
                for (int z = 0; z < 2; ++z) {
                    glm::vec4 pt = invCam * glm::vec4(2.0f * x - 1.0f, 2.0f * y - 1.0f, 2.0f * z - 1.0f, 1.0f);
                    frustumCorners.push_back(pt / pt.w);
                }
            }
        }

        glm::vec3 center = glm::vec3(0);
        for (const auto& v : frustumCorners) center += glm::vec3(v);
        center /= 8.0f;

        // Магия сферы: находим идеальный радиус
        float radius = 0.0f;
        for (const auto& v : frustumCorners) {
            float distance = glm::length(glm::vec3(v) - center);
            radius = std::max(radius, distance);
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;

        // =========================================================
        // ФИКС КОРОТКИХ ТЕНЕЙ: Расширяем поле зрения солнца!
        // =========================================================
        radius = std::min(radius, 300.0f);

        float padding = glm::clamp(radius * 0.05f, 2.0f, 15.0f);
        radius += padding;


        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        if (std::abs(sunDir.y) > 0.999f) up = glm::vec3(0.0f, 0.0f, 1.0f);

        glm::mat4 lightView = glm::lookAt(center - sunDir * 1000.0f, center, up);

        // Стабилизация мерцания пикселей при движении
        float worldUnitsPerTexel = (radius * 2.0f) / shadowSize;
        glm::vec3 centerLightSpace = glm::vec3(lightView * glm::vec4(center, 1.0f));
        centerLightSpace.x = std::floor(centerLightSpace.x / worldUnitsPerTexel) * worldUnitsPerTexel;
        centerLightSpace.y = std::floor(centerLightSpace.y / worldUnitsPerTexel) * worldUnitsPerTexel;

        center = glm::vec3(glm::inverse(lightView) * glm::vec4(centerLightSpace, 1.0f));
        lightView = glm::lookAt(center - sunDir * 1000.0f, center, up);
   
        // ТЕПЕРЬ Z-RANGE НОРМАЛЬНЫЙ: от 0.0 до 2000.0
        // Твой шейдер куллинга больше не будет отсекать тени!
        return glm::ortho(-radius, radius, -radius, radius, -500.0f, 2000.0f) * lightView;
    }
    void Render::GlobalGPUCulling(int offset, int count, const glm::mat4& viewProj, const glm::vec3& camPos, bool isShadowPass, CullingShader& cullingshader, Window& window, GLuint hiZTex) {
        if (count == 0) return;
        // --- ПРИВЯЗЫВАЕМ БУФЕРЫ ДЛЯ КУЛЛИНГА ---
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, globalSphereBuffer.ID); // Сферы (binding = 0)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, globalCommandBuffer.ID); // Команды (binding = 1)
        // ----------------------------------------
        cullingshader.Activate();
        cullingshader.Set(cullingshader.loc.camPos, camPos);
        cullingshader.Set(cullingshader.loc.isShadowPass, isShadowPass ? 1 : 0);

        auto planes = cullingshader.ExtractFrustumPlanes(viewProj);
        for (int i = 0; i < 6; i++) {
            cullingshader.Set(cullingshader.loc.frustumPlanes[i], planes[i].normal.x, planes[i].normal.y, planes[i].normal.z, planes[i].distance);
        }

        // Передаем сдвиг, чтобы шейдер куллил только нужную часть (Тени или Main)
        cullingshader.Set(glGetUniformLocation(cullingshader.ID, "bufferOffset"), (unsigned int)offset);
        cullingshader.Set(cullingshader.loc.objectCount, (unsigned int)count);
        cullingshader.Set(cullingshader.loc.viewProjection, viewProj);

        if (!isShadowPass) {
            cullingshader.Set(cullingshader.loc.screenSize, (float)window.settings.width, (float)window.settings.height);
            cullingshader.Set(cullingshader.loc.hiZTexture, hiZTex, 0);
        }

        GLuint workGroups = (count + 255) / 256;
        glDispatchCompute(workGroups, 1, 1);
        glMemoryBarrier(GL_COMMAND_BARRIER_BIT);
    }


    std::vector<glm::mat4> Render::RenderCSM(Camera& camera, std::map<Mesh*, std::vector<RenderInstance>>& shadowBatches, entt::registry& registry, ShadowShader& shadowshader, CullingShader& cullingshader, Window& window) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 3.0f); // ИДЕАЛЬНЫЕ ЗНАЧЕНИЯ! Не 24!
        // Если рябит - увеличивай второе число!
        shadowshader.Activate();
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, globalObjectBuffer.ID);
        glEnable(GL_DEPTH_CLAMP);
        static int frameCounter = 0; frameCounter++;
        static std::vector<glm::mat4> sunMatrices(4, glm::mat4(1.0f));
        ;
        float lastSplit = 0.1f;
        float cascadeNears[4] = { 0.1f, shadowshader.cascadeSplits[0],
                           shadowshader.cascadeSplits[1], shadowshader.cascadeSplits[2] };
        for (int i = 0; i < 4; i++) {
            bool shouldUpdate = true;
            if (i == 1 && frameCounter % 2 != 0) shouldUpdate = false;
            if (i == 2 && frameCounter % 3 != 0) shouldUpdate = false;
            if (i == 3 && frameCounter % 4 != 0) shouldUpdate = false;
            if (shouldUpdate) sunMatrices[i] = CalculateCalculateCSMMatrix(lastSplit, shadowshader.cascadeSplits[i], camera, (float)shadowshader.sunShadowSize);
            lastSplit = shadowshader.cascadeSplits[i];
        }

        glBindFramebuffer(GL_FRAMEBUFFER, shadowshader.sunFBO);
        glViewport(0, 0, shadowshader.sunShadowSize, shadowshader.sunShadowSize);

        for (int i = 0; i < 4; i++) {
            bool matrixUpdated = (i == 0) || (i == 1 && frameCounter % 2 == 0) || (i == 2 && frameCounter % 3 == 0) || (i == 3 && frameCounter % 4 == 0);
            if (!matrixUpdated) continue;

            // СТАТИЧНЫЕ ТЕНИ
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadowshader.staticSunShadowArray, 0, i);
            glClear(GL_DEPTH_BUFFER_BIT);

            if (totalStaticShadowInstances > 0) {
                int staticOffset = totalMainInstances; // Сдвиг для статики
                CSMGPUCulling(staticOffset, totalStaticShadowInstances,
                    sunMatrices[i], camera.Position, camera.GetViewMatrix(),
                    sunDir, cascadeNears[i], shadowshader.cascadeSplits[i],
                    cullingshader, window);
                shadowshader.Activate();
                shadowshader.Set(shadowshader.loc.lightProjection, sunMatrices[i]);
                for (auto& draw : staticShadowDraws) {
                    draw.mesh->VAO.Bind();
                    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, globalCommandBuffer.ID);
                    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)(draw.commandOffset * sizeof(DrawCommand)), draw.instanceCount, sizeof(DrawCommand));
                }
            }

            glCopyImageSubData(shadowshader.staticSunShadowArray, GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, shadowshader.sunShadowArray, GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, shadowshader.sunShadowSize, shadowshader.sunShadowSize, 1);

            // ДИНАМИЧЕСКИЕ ТЕНИ
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadowshader.sunShadowArray, 0, i);
            if (totalDynamicShadowInstances > 0) {
                int dynamicOffset = totalMainInstances + totalStaticShadowInstances;
                CSMGPUCulling(dynamicOffset, totalDynamicShadowInstances,
                    sunMatrices[i], camera.Position, camera.GetViewMatrix(),
                    sunDir, cascadeNears[i], shadowshader.cascadeSplits[i],
                    cullingshader, window);
                shadowshader.Activate();
                shadowshader.Set(shadowshader.loc.lightProjection, sunMatrices[i]);
                for (auto& draw : dynamicShadowDraws) {
                    draw.mesh->VAO.Bind();
                    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, globalCommandBuffer.ID);
                    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)(draw.commandOffset * sizeof(DrawCommand)), draw.instanceCount, sizeof(DrawCommand));
                }
            }
        }
        shadowshader.staticShadowsDirty = false;
        glDisable(GL_DEPTH_CLAMP);
        glDisable(GL_POLYGON_OFFSET_FILL); 
        return sunMatrices;
    }
    void Render::CSMGPUCulling(int offset, int count, const glm::mat4& sunViewProj,
        const glm::vec3& camPos, const glm::mat4& camView,
        const glm::vec3& sunDir, float cascadeNear, float cascadeFar,
        CullingShader& cullingshader, Window& window)
    {
        if (count == 0) return;

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, globalSphereBuffer.ID);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, globalCommandBuffer.ID);

        cullingshader.csmCullingShader.Activate();

        // Frustum плоскости из матрицы солнца
        auto planes = cullingshader.ExtractFrustumPlanes(sunViewProj);
        for (int i = 0; i < 6; i++) {
            glUniform4f(
                glGetUniformLocation(cullingshader.csmCullingShader.ID,
                    ("frustumPlanes[" + std::to_string(i) + "]").c_str()),
                planes[i].normal.x, planes[i].normal.y,
                planes[i].normal.z, planes[i].distance
            );
        }

        glUniformMatrix4fv(glGetUniformLocation(cullingshader.csmCullingShader.ID, "viewProjection"),
            1, GL_FALSE, glm::value_ptr(sunViewProj));
        glUniformMatrix4fv(glGetUniformLocation(cullingshader.csmCullingShader.ID, "camView"),
            1, GL_FALSE, glm::value_ptr(camView));
        glUniform3f(glGetUniformLocation(cullingshader.csmCullingShader.ID, "camPos"),
            camPos.x, camPos.y, camPos.z);
        glUniform3f(glGetUniformLocation(cullingshader.csmCullingShader.ID, "sunDir"),
            sunDir.x, sunDir.y, sunDir.z);
        glUniform1f(glGetUniformLocation(cullingshader.csmCullingShader.ID, "cascadeNear"),
            cascadeNear);
        glUniform1f(glGetUniformLocation(cullingshader.csmCullingShader.ID, "cascadeFar"),
            cascadeFar);
        glUniform1ui(glGetUniformLocation(cullingshader.csmCullingShader.ID, "bufferOffset"),
            (unsigned int)offset);
        glUniform1ui(glGetUniformLocation(cullingshader.csmCullingShader.ID, "objectCount"),
            (unsigned int)count);

        GLuint workGroups = (count + 255) / 256;
        glDispatchCompute(workGroups, 1, 1);
        glMemoryBarrier(GL_COMMAND_BARRIER_BIT);
    }

    void Render::RenderAtlasShadows(Camera& camera, entt::registry& registry,
        ShadowShader& shadowshader, CullingShader& cullingshader, Window& window)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, shadowshader.atlasFBO);
        glEnable(GL_SCISSOR_TEST);

        const int minTile = 128;
        const int atlasRes = shadowshader.atlasResolution; // 4096
        const int atlasInUnits = atlasRes / minTile;           // 32

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, globalObjectBuffer.ID);

        auto lightView = registry.view<LightComponent, TransformComponent>();
        for (auto entity : lightView) {
            auto& light = lightView.get<LightComponent>(entity).light;
            auto& transform = lightView.get<TransformComponent>(entity).transform;

            if (!light.enable || !light.castShadows || light.type == LightType::Directional) continue;
            if (light.mobility == LightMobility::Static && light.hasBakedShadows) continue;
            if (light.shadowSlot < 0) continue; // атлас заполнен

            int tileSize = light.shadowTileSize; // 512, 256 или 128
            int unitsPerTile = tileSize / minTile;

            // Декодируем пиксельные координаты из слота
            int slotX = (light.shadowSlot % atlasInUnits) * minTile;
            int slotY = (light.shadowSlot / atlasInUnits) * minTile;

            auto renderFace = [&](int faceSlotX, int faceSlotY, glm::mat4 lightMatrix) {
                glViewport(faceSlotX, faceSlotY, tileSize, tileSize);
                glScissor(faceSlotX, faceSlotY, tileSize, tileSize);
                glClear(GL_DEPTH_BUFFER_BIT);

                if (totalStaticShadowInstances > 0) {
                    GlobalGPUCulling(totalMainInstances, totalStaticShadowInstances,
                        lightMatrix, camera.Position, true, cullingshader, window, 0);
                    shadowshader.Activate();
                    shadowshader.Set(shadowshader.loc.lightProjection, lightMatrix);
                    for (auto& draw : staticShadowDraws) {
                        draw.mesh->VAO.Bind();
                        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, globalCommandBuffer.ID);
                        glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
                            (void*)(draw.commandOffset * sizeof(DrawCommand)), draw.instanceCount, sizeof(DrawCommand));
                    }
                }
                if (totalDynamicShadowInstances > 0) {
                    GlobalGPUCulling(totalMainInstances + totalStaticShadowInstances,
                        totalDynamicShadowInstances, lightMatrix, camera.Position, true, cullingshader, window, 0);
                    shadowshader.Activate();
                    shadowshader.Set(shadowshader.loc.lightProjection, lightMatrix);
                    for (auto& draw : dynamicShadowDraws) {
                        draw.mesh->VAO.Bind();
                        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, globalCommandBuffer.ID);
                        glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
                            (void*)(draw.commandOffset * sizeof(DrawCommand)), draw.instanceCount, sizeof(DrawCommand));
                    }
                }
                };

            if (light.type == LightType::Spot) {
                renderFace(slotX, slotY, light.lightSpaceMatrix);
                if (light.mobility == LightMobility::Static) light.hasBakedShadows = true;
            }
            else if (light.type == LightType::Point) {
                glm::vec3 pos = transform.position;
                glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, light.radius);
                glm::mat4 shadowTransforms[6] = {
                    shadowProj * glm::lookAt(pos, pos + glm::vec3(1, 0, 0), glm::vec3(0,-1, 0)),
                    shadowProj * glm::lookAt(pos, pos + glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0)),
                    shadowProj * glm::lookAt(pos, pos + glm::vec3(0, 1, 0), glm::vec3(0, 0, 1)),
                    shadowProj * glm::lookAt(pos, pos + glm::vec3(0,-1, 0), glm::vec3(0, 0,-1)),
                    shadowProj * glm::lookAt(pos, pos + glm::vec3(0, 0, 1), glm::vec3(0,-1, 0)),
                    shadowProj * glm::lookAt(pos, pos + glm::vec3(0, 0,-1), glm::vec3(0,-1, 0)),
                };
                for (int face = 0; face < 6; ++face) {
                    // Каждая грань идёт правее предыдущей
                    int faceSlotX = slotX + face * tileSize;
                    int faceSlotY = slotY;
                    // Если вышли за ширину атласа — переносим на новую строку
                    if (faceSlotX + tileSize > atlasRes) {
                        faceSlotX = (faceSlotX) % atlasRes;
                        faceSlotY += tileSize;
                    }
                    renderFace(faceSlotX, faceSlotY, shadowTransforms[face]);
                }
                if (light.mobility == LightMobility::Static) light.hasBakedShadows = true;
            }
        }
        glDisable(GL_SCISSOR_TEST);
    }


    void Render::RenderMainPass(Camera& camera, Window& window, std::map<Mesh*, std::vector<RenderInstance>>& mainBatches, entt::registry& registry, std::vector<glm::mat4>& sunMatrices, LitShader& geometryShader, DefferedShader& deferredComputeShader, ShadowShader& shadowshader, CullingShader& cullingshader, PostProcessingShader& ppShader) {
        glDisable(GL_POLYGON_OFFSET_FILL);
        GlobalGPUCulling(0, totalMainInstances, camera.GetViewProjectionMatrix(), camera.Position, false, cullingshader, window, ppShader.hiZTexture);

        glBindFramebuffer(GL_FRAMEBUFFER, gBufferFBO);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, globalObjectBuffer.ID);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, materialBuffer.ID);

        geometryShader.Activate();
        geometryShader.Set(geometryShader.loc.camPos, camera.Position);
        geometryShader.Set(geometryShader.loc.camMatrix, camera.GetViewProjectionMatrix());
        geometryShader.Set(geometryShader.loc.view, camera.GetViewMatrix());

        for (auto& draw : mainDraws) {
            draw.mesh->VAO.Bind();
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, globalCommandBuffer.ID);
            glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)(draw.commandOffset * sizeof(DrawCommand)), draw.instanceCount, sizeof(DrawCommand));
        }

        // ФАЗА 1.5: КОПИРОВАНИЕ ГЛУБИНЫ
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gBufferFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ppShader.FBO);
        glBlitFramebuffer(0, 0, window.settings.width, window.settings.height, 0, 0, window.settings.width, window.settings.height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        glDisable(GL_DEPTH_TEST);

        // ФАЗА 2: COMPUTE DEFERRED LIGHTING
        deferredComputeShader.Activate();

        deferredComputeShader.Set(deferredComputeShader.loc.camPos, camera.Position);
        deferredComputeShader.Set(deferredComputeShader.loc.view, camera.GetViewMatrix());

        // ПЕРЕДАЕМ РАЗМЕР ЭКРАНА ОБЯЗАТЕЛЬНО (иначе UV = NaN)

        glUniform3fv(glGetUniformLocation(deferredComputeShader.ID, "sunDir"), 1, glm::value_ptr(sunDir));
        glUniform2f(glGetUniformLocation(deferredComputeShader.ID, "screenSize"), (float)window.settings.width, (float)window.settings.height);

        deferredComputeShader.Set(deferredComputeShader.loc.sunLightSpaceMatrices, sunMatrices);
        deferredComputeShader.Set(deferredComputeShader.loc.cascadeSplits, shadowshader.cascadeSplits);

        deferredComputeShader.Set(deferredComputeShader.loc.shadowAtlas, shadowshader.shadowAtlas, 12);
        deferredComputeShader.Set(deferredComputeShader.loc.sunShadowMap, shadowshader.sunShadowArray, 13, GL_TEXTURE_2D_ARRAY);
        deferredComputeShader.Set(deferredComputeShader.loc.noiseTexture, deferredComputeShader.blueNoiseTexture, 0);

        glm::mat4 invViewProj = glm::inverse(camera.GetViewProjectionMatrix());
        glUniformMatrix4fv(glGetUniformLocation(deferredComputeShader.ID, "invViewProj"), 1, GL_FALSE, glm::value_ptr(invViewProj));

        // ВНИМАНИЕ: gPositionMetallic БОЛЬШЕ НЕТ! 
        // Я закомментировал этот кусок, он тебе больше не нужен:
        // glActiveTexture(GL_TEXTURE14);
        // glBindTexture(GL_TEXTURE_2D, gPositionMetallic);
        // deferredComputeShader.Set(deferredComputeShader.loc.gPositionMetallic, 14);

        glActiveTexture(GL_TEXTURE15);
        glBindTexture(GL_TEXTURE_2D, gNormalRoughness);
        deferredComputeShader.Set(deferredComputeShader.loc.gNormalRoughness, 15);

        glActiveTexture(GL_TEXTURE16);
        glBindTexture(GL_TEXTURE_2D, gAlbedoMetallic);
        deferredComputeShader.Set(deferredComputeShader.loc.gAlbedoMetallic, 16);

        glActiveTexture(GL_TEXTURE17);
        glBindTexture(GL_TEXTURE_2D, gDepth);
        glUniform1i(glGetUniformLocation(deferredComputeShader.ID, "gDepth"), 17);

        glActiveTexture(GL_TEXTURE18);
        glBindTexture(GL_TEXTURE_2D, gHeightAO);
        deferredComputeShader.Set(deferredComputeShader.loc.gHeightAO, 18);

        // ПРИВЯЗЫВАЕМ ТЕКСТУРУ ДЛЯ ЗАПИСИ
        glBindImageTexture(0, hdrOutputTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);

        GLuint numGroupsX = (window.settings.width + 15) / 16;
        GLuint numGroupsY = (window.settings.height + 15) / 16;
        glDispatchCompute(numGroupsX, numGroupsY, 1);

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);


        glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);
        glEnable(GL_DEPTH_TEST);
    
    }