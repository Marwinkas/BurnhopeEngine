#ifndef PHYSICSENGINE_CLASS_H
#define PHYSICSENGINE_CLASS_H

#include <vector>

// Основные заголовки Jolt (обычно подключаются после определения JPH_MATH_FAST)
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <entt/entt.hpp>
// Jolt требует реализации своих интерфейсов фильтрации коллизий.
// Чтобы не засорять заголовочный файл, сделаем опережающие объявления, 
// а саму логику спрячем в .cpp.
class BPLayerInterfaceImpl;
class ObjectVsBroadPhaseLayerFilterImpl;
class ObjectLayerPairFilterImpl;


#include <cstdarg>
#include <iostream>

using namespace JPH;

// Функция для вывода обычных сообщений от Jolt
static void TraceImpl(const char* inFMT, ...) {
    va_list list;
    va_start(list, inFMT);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), inFMT, list);
    va_end(list);
    std::cout << "[Jolt Physics] " << buffer << std::endl;
}

// Функция для перехвата критических ошибок (Assert)
#ifdef JPH_ENABLE_ASSERTS
static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, uint32_t inLine) {
    std::cout << "[Jolt CRITICAL] " << inFile << ":" << inLine << ": (" << inExpression << ") "
        << (inMessage != nullptr ? inMessage : "") << std::endl;
    return true; // Возвращаем true, чтобы Visual Studio остановилась на брейкпоинте
}
#endif

class PhysicsEngine {
public:
    void Init();
    void Update(float deltaTime);
    void Cleanup();

    void ApplyUIChanges(entt::registry& registry);
    void CreateTestScene();
    void RegisterEntities(entt::registry& registry);
    bool SyncTransforms(entt::registry& registry);
    void UpdatePhysicsFromTransforms(entt::registry& registry);
    // Главный класс физического мира (аналог PxPhysics + PxScene)
    JPH::PhysicsSystem* physicsSystem = nullptr;
    // Добавь эти две новые функции в public секцию PhysicsEngine:

    // Функция, которая будет ловить флаг rebuildPhysics
    void RebuildPhysicsEntities(entt::registry& registry);

    // Функция-коллбек для EnTT: вызывается автоматически, когда ты удаляешь PhysicsComponent
    void OnPhysicsComponentDestroyed(entt::registry& registry, entt::entity entity);
    // В Jolt нет указателей на тела (как PxRigidDynamic*). 
    // Мы храним только ID, а обращаемся к ним через BodyInterface.
    JPH::BodyID testBoxID;

private:
    // Системы управления памятью и потоками для Jolt
    JPH::TempAllocatorImpl* tempAllocator = nullptr;
    JPH::JobSystemThreadPool* jobSystem = nullptr;

    // Интерфейсы слоев (кто с кем может сталкиваться)
    BPLayerInterfaceImpl* bpLayerInterface = nullptr;
    ObjectVsBroadPhaseLayerFilterImpl* objectVsBPFilter = nullptr;
    ObjectLayerPairFilterImpl* objectVsObjectFilter = nullptr;
};

#endif