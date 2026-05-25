// Frame GlobalUbo via descriptor heap (std140). Fragment/vertex only — not mesh/task.
import common.Bindless;
import common.GlobalUbo;
import common.SceneAddresses;

export GlobalUbo loadGlobalUboScene(SceneAddresses scene) {
    return loadGlobalUbo(scene.globalUboHeap);
}
