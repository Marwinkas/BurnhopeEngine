#pragma once
#include <flecs.h>
#include "Components.hpp"

namespace burnhope
{
    inline void registerBurnhopeComponents(flecs::world &world)
    {
        world.component<Position3>()
            .member<float>("x")
            .member<float>("y")
            .member<float>("z");

        world.component<RotationEuler>()
            .member<float>("x")
            .member<float>("y")
            .member<float>("z");

        world.component<Scale3>()
            .member<float>("x")
            .member<float>("y")
            .member<float>("z");

        world.component<LocalMatrix>();
        world.component<TransformHistory>();

        world.component<IDComponent>();
        world.component<TagComponent>();
        world.component<MeshComponent>();
        world.component<LightComponent>();
        world.component<HierarchyComponent>();
        world.component<ReflectionProbeComponent>();
        world.component<DecalComponent>();
        world.component<PortalComponent>();

        world.component<Static>();
        world.component<Active>();
        world.component<Visible>();
        world.component<TransformChanged>();
    }
}
