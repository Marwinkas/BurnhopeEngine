#version 330 core
out vec4 FragColor;
in vec2 texCoords;
uniform sampler2D screenTexture;   uniform sampler2D positionTexture; uniform sampler2D normalTexture;   uniform sampler2D albedoTexture;   uniform mat4 camMatrix;            uniform vec3 camPos;
uniform float maxDistance;
uniform int steps;
uniform float resolution;
uniform float thickness;
uniform float ssrStrength;
vec2 binarySearch(vec3 dir, inout vec3 hitPos) {
    for(int i = 0; i < 5; i++) {
        vec4 projected = camMatrix * vec4(hitPos, 1.0);
        projected.xy /= projected.w;
        projected.xy = projected.xy * 0.5 + 0.5;
        float sceneDepth = texture(positionTexture, projected.xy).z;
        float depthDiff = hitPos.z - sceneDepth;
        if(depthDiff <= 0.0) hitPos -= dir * (resolution * 0.5);
        else hitPos += dir * (resolution * 0.5);
    }
    vec4 finalProjected = camMatrix * vec4(hitPos, 1.0);
    return (finalProjected.xy / finalProjected.w) * 0.5 + 0.5;
}
void main() {
        vec4 normData = texture(normalTexture, texCoords);
    vec3 normal = normalize(normData.rgb);
    float roughness = normData.a; 
    vec4 aData = texture(albedoTexture, texCoords);
    float metallic = aData.a;     vec3 baseAlbedo = aData.rgb;
        if(length(normal) < 0.1 || roughness > 0.8) {
        FragColor = vec4(0.0);
        return;
    }
    vec3 pos = texture(positionTexture, texCoords).rgb;
    vec3 viewDir = normalize(pos - camPos);
    vec3 reflectDir = normalize(reflect(viewDir, normal));
        float F0 = mix(0.04, 1.0, metallic);
    float fresnel = F0 + (1.0 - F0) * pow(1.0 - max(dot(normal, -viewDir), 0.0), 5.0);
        vec3 hitPos = pos;
    vec2 hitCoords = vec2(-1.0);
    vec3 step = reflectDir * resolution;
    for(int i = 0; i < steps; i++) {
        hitPos += step;
        if(distance(pos, hitPos) > maxDistance) break;
        vec4 projected = camMatrix * vec4(hitPos, 1.0);
        projected.xy /= projected.w;
        projected.xy = projected.xy * 0.5 + 0.5;
        if(projected.x < 0.0 || projected.x > 1.0 || projected.y < 0.0 || projected.y > 1.0) break;
        float sceneDepth = texture(positionTexture, projected.xy).z;
        float depthDiff = hitPos.z - sceneDepth;
        if(depthDiff <= 0.0 && depthDiff > -thickness) {
            hitCoords = binarySearch(reflectDir, hitPos);
            break;
        }
    }
        if(hitCoords != vec2(-1.0)) {
        vec2 edgeFactor = smoothstep(0.0, 0.2, hitCoords) * (1.0 - smoothstep(0.8, 1.0, hitCoords));
        float screenFade = edgeFactor.x * edgeFactor.y;
        vec3 color = texture(screenTexture, hitCoords).rgb;
                if(metallic > 0.5) {
            color *= baseAlbedo; 
        }
        float visibility = screenFade * fresnel * (1.0 - roughness) * ssrStrength;
        FragColor = vec4(color, clamp(visibility, 0.0, 1.0));
    } else {
        FragColor = vec4(0.0);
    }
}