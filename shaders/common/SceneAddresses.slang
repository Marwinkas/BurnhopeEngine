// Scene BDA table (mesh/task safe — no descriptor heap import).
import common.GlobalUbo;
import common.MaterialGpu;
import common.SceneGpu;

export struct SceneAddresses {
    uint globalUboHeap;
    uint textureTableBase;
    uint64_t objectStorageBda;
    uint64_t materialStorageBda;
    uint64_t boneStorageBda;
    uint32_t defaultSampler;
    uint32_t textureTableCount;
    uint64_t globalUboBda;
};

export SceneAddresses loadSceneAddresses(uint64_t sceneBda) {
    return *((SceneAddresses*)(sceneBda));
}

export MaterialData loadMaterialScene(SceneAddresses scene, uint matIdx) {
    return loadMaterialBda(scene.materialStorageBda, matIdx);
}

/** Material tex fields store absolute resource-heap slots (from CPU heapIndex after refresh). */
export int sceneTextureHeapIndex(SceneAddresses scene, int heapSlot) {
    if (heapSlot < 0) return -1;
    return heapSlot;
}

/** G-Buffer frag: UBO via BDA (NV: heap ConstantBuffer<GlobalUbo> can DEVICE_LOST). */
export GlobalUbo* sceneGlobalUboPtr(SceneAddresses scene) {
    return (GlobalUbo*)scene.globalUboBda;
}

export ObjectData loadObjectBda(uint64_t objectBda, uint objectId) {
    return *((ObjectData*)(objectBda + uint64_t(objectId) * 192u));
}

export float4x4 loadBoneMatrixBda(uint64_t boneBda, uint boneIdx) {
    return *((float4x4*)(boneBda + uint64_t(boneIdx) * 64u));
}
