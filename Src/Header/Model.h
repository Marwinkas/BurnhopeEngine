#ifndef MODEL_CLASS_H
#define MODEL_CLASS_H
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include "Mesh.h"
#include <string>
#include <vector>
#include <iostream>
#include <map>
#include <unordered_map> 
#include <cmath> 
#include <algorithm>
#include <meshoptimizer.h>
class Model
{ 
public: 
    std::vector<Mesh> meshes;
    std::vector<std::string> loadedMaterialPaths;
    static std::map<std::string, std::vector<Mesh>> globalMeshCache;
    static std::map<std::string, std::vector<std::string>> globalMaterialCache;
    std::string directory;
    std::string projectRoot;
    Model(std::string const& path, std::string const& root);
private:
    void loadModel(std::string const& path);

    // НОВОЕ: Функция вытаскивания текстур из Assimp
    std::string loadMaterialTexture(aiMaterial* mat, aiTextureType type, std::string typeName);
};
#endif