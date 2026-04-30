#version 430 core
layout(rgba8, binding = 0) writeonly uniform image3D voxelTexture;
in vec3 gWorldPos;
in vec3 gNormal;
in vec2 gTexCoords;
uniform sampler2D albedoMap;
uniform vec3 gridMin;
uniform vec3 gridMax;
uniform int gridSize;
uniform vec3 sunDir; 
void main() {
    vec3 uvw = (gWorldPos - gridMin) / (gridMax - gridMin);
    if(any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0)))) return;
    ivec3 voxelCoord = ivec3(uvw * float(gridSize));
    vec4 albedo = texture(albedoMap, gTexCoords);
    if(albedo.a < 0.5) discard;
        vec3 n = normalize(gNormal);
    vec3 l = normalize(sunDir);
    float diff = max(dot(n, l), 0.0);
            vec3 ambient = albedo.rgb * 0.01; 
    vec3 radiance = albedo.rgb * diff + ambient;
    imageStore(voxelTexture, voxelCoord, vec4(radiance, 1.0));
}