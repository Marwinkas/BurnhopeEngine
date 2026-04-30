#ifndef MESHRENDERER_CLASS_H
#define MESHRENDERER_CLASS_H
#include <vector>
#include "Mesh.h"
#include "Material.h"
struct SubMesh {
    Mesh* mesh;
    Material* material;
};
class MeshRenderer {
public:
    std::vector<SubMesh> subMeshes;
    MeshRenderer() {}
    void AddSubMesh(Mesh* mesh, Material* material) {
        SubMesh sub;
        sub.mesh = mesh;
        sub.material = material;
        subMeshes.push_back(sub);
    }
};
#endif