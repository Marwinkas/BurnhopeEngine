#pragma once
#include <flecs.h>
#include "Components.hpp"

namespace burnhope
{
    struct EcsQueries
    {
        flecs::query<Position3, RotationEuler, Scale3, LocalMatrix, MeshComponent> meshTransforms;
        flecs::query<MeshComponent> meshes;
        flecs::query<Position3, RotationEuler, Scale3, LocalMatrix> transforms;
        flecs::query<LightComponent, Position3, RotationEuler, Scale3, LocalMatrix> lights;
        flecs::query<DecalComponent> decals;
        flecs::query<PortalComponent, Position3, RotationEuler, Scale3, LocalMatrix> portals;
        flecs::query<Position3, RotationEuler, Scale3, LocalMatrix, ReflectionProbeComponent> probes;

        explicit EcsQueries(flecs::world &world)
            : meshTransforms(world.query<Position3, RotationEuler, Scale3, LocalMatrix, MeshComponent>())
            , meshes(world.query<MeshComponent>())
            , transforms(world.query<Position3, RotationEuler, Scale3, LocalMatrix>())
            , lights(world.query<LightComponent, Position3, RotationEuler, Scale3, LocalMatrix>())
            , decals(world.query<DecalComponent>())
            , portals(world.query<PortalComponent, Position3, RotationEuler, Scale3, LocalMatrix>())
            , probes(world.query<Position3, RotationEuler, Scale3, LocalMatrix, ReflectionProbeComponent>())
        {
        }
    };
}
