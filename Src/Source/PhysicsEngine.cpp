#include "../Header/PhysicsEngine.h"
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <iostream>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include "../Header/Components.h"

using namespace JPH;

// ===================================================================
// НАСТРОЙКА СЛОЕВ JOLT (Самая важная часть для скорости)
// ===================================================================
namespace Layers {
    static constexpr ObjectLayer NON_MOVING = 0; // Статика (земля, стены)
    static constexpr ObjectLayer MOVING = 1;     // Динамика (кубики, игрок)
    static constexpr ObjectLayer NUM_LAYERS = 2;
};

namespace BroadPhaseLayers {
    static constexpr BroadPhaseLayer NON_MOVING(0);
    static constexpr BroadPhaseLayer MOVING(1);
    static constexpr uint NUM_LAYERS(2);
};

// Фильтр BroadPhase: Говорит Jolt'у, что статика не сталкивается со статикой
class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
    }
    virtual uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
    virtual BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer inLayer) const override {
        return mObjectToBroadPhase[inLayer];
    }
private:
    BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl : public ObjectVsBroadPhaseLayerFilter {
public:
    virtual bool ShouldCollide(ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const override {
        switch (inLayer1) {
        case Layers::NON_MOVING: return inLayer2 == BroadPhaseLayers::MOVING;
        case Layers::MOVING:     return true; // Движущееся сталкивается со всем
        default:                 return false;
        }
    }
};

class ObjectLayerPairFilterImpl : public ObjectLayerPairFilter {
public:
    virtual bool ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const override {
        switch (inObject1) {
        case Layers::NON_MOVING: return inObject2 == Layers::MOVING;
        case Layers::MOVING:     return true;
        default:                 return false;
        }
    }
};

// ===================================================================
// ИНИЦИАЛИЗАЦИЯ И ОЧИСТКА
// ===================================================================

// --- АВТОМАТИЧЕСКОЕ УДАЛЕНИЕ ---
// Вызывается самим EnTT, когда ты нажимаешь "X" в инспекторе
void PhysicsEngine::OnPhysicsComponentDestroyed(entt::registry& registry, entt::entity entity) {
    auto& physComp = registry.get<PhysicsComponent>(entity);

    // Если тело было в Jolt, удаляем его навсегда
    if (!physComp.bodyID.IsInvalid()) {
        JPH::BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();
        bodyInterface.RemoveBody(physComp.bodyID);
        bodyInterface.DestroyBody(physComp.bodyID);
    }
}

// --- ПЕРЕСБОРКА И СОЗДАНИЕ ---
void PhysicsEngine::RebuildPhysicsEntities(entt::registry& registry) {
    JPH::BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();
    auto view = registry.view<TransformComponent, PhysicsComponent>();

    for (auto [entity, transformComp, physComp] : view.each()) {

        // 1. Если тело УЖЕ ЕСТЬ, но мы покрутили ползунки в UI -> уничтожаем старое тело
        if (physComp.rebuildPhysics && !physComp.bodyID.IsInvalid()) {
            bodyInterface.RemoveBody(physComp.bodyID);
            bodyInterface.DestroyBody(physComp.bodyID);
            physComp.bodyID = JPH::BodyID(); // Делаем ID недействительным
        }

        // 2. Если тела НЕТ (только что создали в UI или только что уничтожили абзацем выше) -> создаем!
        if (physComp.bodyID.IsInvalid()) {

            // --- ФОРМА ---
            JPH::ShapeRefC shape;
            if (physComp.colliderType == ColliderType::Box) {
                shape = new JPH::BoxShape(JPH::Vec3(physComp.extents.x, physComp.extents.y, physComp.extents.z));
            }
            else if (physComp.colliderType == ColliderType::Sphere) {
                shape = new JPH::SphereShape(physComp.radius);
            }
            else {
                shape = new JPH::BoxShape(JPH::Vec3(100.0f, 0.1f, 100.0f)); // Заглушка для Plane
            }

            // --- ТИП ДВИЖЕНИЯ И СЛОЙ ---
            JPH::EMotionType motionType = (physComp.bodyType == RigidBodyType::Dynamic) ?
                JPH::EMotionType::Dynamic : JPH::EMotionType::Static;
            JPH::ObjectLayer layer = (physComp.bodyType == RigidBodyType::Dynamic) ?
                Layers::MOVING : Layers::NON_MOVING;

            // --- КООРДИНАТЫ ---
            // --- КООРДИНАТЫ ---
            glm::vec3 p = transformComp.transform.position;

            // ВАЖНО: Переводим градусы из UI в радианы для Jolt!
            glm::vec3 r = glm::radians(transformComp.transform.rotation);

            JPH::Vec3 pos(p.x, p.y, p.z);
            JPH::Quat rot = JPH::Quat::sEulerAngles(JPH::Vec3(r.x, r.y, r.z));
            // --- СОЗДАНИЕ ---
            JPH::BodyCreationSettings settings(shape, pos, rot, motionType, layer);
            settings.mFriction = physComp.friction;
            settings.mRestitution = physComp.restitution;

            // ВАЖНО: Заставляем Jolt использовать нашу массу из UI, а не считать её по объему
            if (physComp.bodyType == RigidBodyType::Dynamic) {
                settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                settings.mMassPropertiesOverride.mMass = physComp.mass;
            }

            // Спавним тело и сохраняем его новый ID!
            physComp.bodyID = bodyInterface.CreateAndAddBody(settings, JPH::EActivation::Activate);

            // Сбрасываем флажок, объект полностью обновлен
            physComp.rebuildPhysics = false;
        }
    }
}
void PhysicsEngine::Init() {
    // 1. Инициализация памяти и типов Jolt
    RegisterDefaultAllocator();

    Trace = TraceImpl;
    JPH_IF_ENABLE_ASSERTS(AssertFailed = AssertFailedImpl;)

    Factory::sInstance = new Factory();
    RegisterTypes();

    // 2. Выделяем память для физики (TempAllocator нужен для быстрых расчетов на кадр)
    tempAllocator = new TempAllocatorImpl(10 * 1024 * 1024); // 10 МБ временной памяти
    jobSystem = new JobSystemThreadPool(cMaxPhysicsJobs, cMaxPhysicsBarriers, thread::hardware_concurrency() - 1);

    // 3. Создаем фильтры
    bpLayerInterface = new BPLayerInterfaceImpl();
    objectVsBPFilter = new ObjectVsBroadPhaseLayerFilterImpl();
    objectVsObjectFilter = new ObjectLayerPairFilterImpl();

    // 4. Создаем сам физический мир!
    const uint cMaxBodies = 10240;
    const uint cNumBodyMutexes = 0; // 0 = дефолт
    const uint cMaxBodyPairs = 10240;
    const uint cMaxContactConstraints = 10240;

    physicsSystem = new PhysicsSystem();
    physicsSystem->Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints,
        *bpLayerInterface, *objectVsBPFilter, *objectVsObjectFilter);

    std::cout << "[Physics] Jolt Physics успешно инициализирован!" << std::endl;
}

void PhysicsEngine::Cleanup() {
    delete physicsSystem;
    delete objectVsObjectFilter;
    delete objectVsBPFilter;
    delete bpLayerInterface;
    delete jobSystem;
    delete tempAllocator;
    delete Factory::sInstance;
    Factory::sInstance = nullptr;
}

// ===================================================================
// РАБОТА СО СЦЕНОЙ
// ===================================================================

void PhysicsEngine::CreateTestScene() {
    BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();

    // Создаем статичный пол (размер 100x1x100)
    BoxShapeSettings floorShapeSettings(Vec3(100.0f, 1.0f, 100.0f));
    ShapeSettings::ShapeResult floorShapeResult = floorShapeSettings.Create();

    BodyCreationSettings floorSettings(floorShapeResult.Get(), Vec3(0.0f, -1.0f, 0.0f), Quat::sIdentity(), EMotionType::Static, Layers::NON_MOVING);
    BodyID floorID = bodyInterface.CreateAndAddBody(floorSettings, EActivation::DontActivate);
}

// Берем твои GameObject и создаем для них физические тела
void PhysicsEngine::RegisterEntities(entt::registry& registry) {
    JPH::BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();

    // EnTT View: Берем все сущности, у которых есть оба компонента
    auto view = registry.view<TransformComponent, PhysicsComponent>();

    // Идиоматичный цикл EnTT
    for (auto [entity, transformComp, physComp] : view.each()) {

        // 1. Создаем форму (Shape)
        JPH::ShapeRefC shape;
        if (physComp.colliderType == ColliderType::Box) {
            shape = new JPH::BoxShape(JPH::Vec3(physComp.extents.x, physComp.extents.y, physComp.extents.z));
        }
    
        // 2. Настраиваем слой и тип движения
        JPH::EMotionType motionType = (physComp.bodyType == RigidBodyType::Dynamic) ?
            JPH::EMotionType::Dynamic : JPH::EMotionType::Static;

        JPH::ObjectLayer layer = (physComp.bodyType == RigidBodyType::Dynamic) ?
            Layers::MOVING : Layers::NON_MOVING;

        // 3. Берем координаты из TransformComponent
        JPH::Vec3 pos(transformComp.transform.position.x, transformComp.transform.position.y, transformComp.transform.position.z);
        JPH::Quat rot = JPH::Quat::sEulerAngles(JPH::Vec3(transformComp.transform.rotation.x, transformComp.transform.rotation.y, transformComp.transform.rotation.z));

        // 4. Создаем тело
        JPH::BodyCreationSettings settings(shape, pos, rot, motionType, layer);
        settings.mFriction = physComp.friction;
        settings.mRestitution = physComp.restitution;

        // 5. Записываем ID обратно в компонент!
        physComp.bodyID = bodyInterface.CreateAndAddBody(settings, JPH::EActivation::Activate);
    }
}

// Забираем координаты из Jolt и кладем их в EnTT для рендера
bool PhysicsEngine::SyncTransforms(entt::registry& registry) {
    JPH::BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();
    auto view = registry.view<TransformComponent, PhysicsComponent>();

    bool sceneChanged = false;

    for (auto [entity, transformComp, physComp] : view.each()) {
        // Пропускаем статику и объекты без физики
        if (physComp.bodyID.IsInvalid() || physComp.bodyType == RigidBodyType::Static) continue;

        // ВАЖНО: Если кубик "уснул" (лежит на полу и не двигается) - не тратим ресурсы!
        if (!bodyInterface.IsActive(physComp.bodyID)) continue;

        // Читаем новые данные из Jolt
        JPH::Vec3 pos = bodyInterface.GetCenterOfMassPosition(physComp.bodyID);
        JPH::Quat rot = bodyInterface.GetRotation(physComp.bodyID);

        // Позицию отдаем как есть
        transformComp.transform.position = glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ());

        // ВАЖНО: Переводим радианы в градусы для твоего UI!
        JPH::Vec3 euler = rot.GetEulerAngles();
        transformComp.transform.rotation = glm::degrees(glm::vec3(euler.GetX(), euler.GetY(), euler.GetZ()));

        transformComp.transform.updatematrix = true;

        sceneChanged = true;
    }

    return sceneChanged;
}
// Принудительно телепортируем физику туда, куда мы сдвинули объект в UI/Гизмо
void PhysicsEngine::UpdatePhysicsFromTransforms(entt::registry& registry) {
    JPH::BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();

    auto view = registry.view<TransformComponent, PhysicsComponent>();

    for (auto [entity, transformComp, physComp] : view.each()) {
        // Если флажок не поднят или у объекта нет физики — пропускаем
        if (!physComp.updatePhysicsTransform || physComp.bodyID.IsInvalid()) {
            continue;
        }

        // Берем новые координаты из GLM
        glm::vec3 p = transformComp.transform.position;

        // ВАЖНО: Добавляем glm::radians, чтобы Jolt не сошел с ума от градусов!
        glm::vec3 r = glm::radians(transformComp.transform.rotation);

        JPH::Vec3 newPos(p.x, p.y, p.z);
        JPH::Quat newRot = JPH::Quat::sEulerAngles(JPH::Vec3(r.x, r.y, r.z));

        // Телепортируем тело в Jolt! EActivation::Activate разбудит тело, если оно "уснуло"
        bodyInterface.SetPositionAndRotation(physComp.bodyID, newPos, newRot, JPH::EActivation::Activate);

        // ВАЖНО: Сбрасываем скорость! Иначе, если ты поймал падающий куб, 
        // он сохранит скорость падения и улетит вниз как пуля, когда ты отпустишь мышку.
        bodyInterface.SetLinearAndAngularVelocity(physComp.bodyID, JPH::Vec3::sZero(), JPH::Vec3::sZero());

        // Опускаем флажок, мы всё сделали
        physComp.updatePhysicsTransform = false;
    }
}
// ===================================================================
// ЛУП: ОБНОВЛЕНИЕ И СИНХРОНИЗАЦИЯ
// ===================================================================

void PhysicsEngine::Update(float deltaTime) {
    // Шаг физики (обычно делают фиксированный шаг, но пока сойдет deltaTime)
    int collisionSteps = 1;
    physicsSystem->Update(deltaTime, collisionSteps, tempAllocator, jobSystem);
}