// shaders/vsm_sample.glsl
// Библиотека для выборки из Virtual Shadow Maps (VSM)

// Убедитесь, что set и binding соответствуют вашему vsmLayoutPtr (по умолчанию это set = 2)
layout(set = 2, binding = 0) uniform sampler2D vsmPhysicalAtlas;

layout(set = 2, binding = 1, std430) readonly buffer VSMPageTable {
    uint virtualPages[];
};

const uint VIRTUAL_PAGES_X = 4096;
const uint VIRTUAL_PAGES_Y = 4096;
const float PAGE_SIZE = 128.0;
const float PHYSICAL_ATLAS_SIZE = 4096.0; // 32 страницы по 128 пикселей (32 * 128 = 4096)

float SampleVirtualShadowMap(vec3 projCoords) {
    if (projCoords.z > 1.0 || projCoords.z < 0.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 || 
        projCoords.y < 0.0 || projCoords.y > 1.0) 
    {
        return 1.0; // Пиксель вне тени (за границей виртуальной карты)
    }

    uint vPageX = uint(projCoords.x * float(VIRTUAL_PAGES_X));
    uint vPageY = uint(projCoords.y * float(VIRTUAL_PAGES_Y));
    uint vPageIdx = vPageY * VIRTUAL_PAGES_X + vPageX;

    uint physPage = virtualPages[vPageIdx];
    
    // Если страница не выделена, геометрии там нет -> тени нет
    if (physPage == 0xFFFFFFFF || physPage == 0xFFFFFFFE) {
        return 1.0;
    }

    vec2 pageUV = fract(projCoords.xy * vec2(VIRTUAL_PAGES_X, VIRTUAL_PAGES_Y));
    
    uint physX = physPage % 32;
    uint physY = physPage / 32;
    
    vec2 physTexelCoord = vec2(physX * PAGE_SIZE, physY * PAGE_SIZE) + pageUV * PAGE_SIZE;
    vec2 atlasUV = physTexelCoord / PHYSICAL_ATLAS_SIZE;

    float shadowDepth = texture(vsmPhysicalAtlas, atlasUV).r;
    
    float bias = max(0.005 * (1.0 - projCoords.z), 0.0005);
    return (projCoords.z - bias > shadowDepth) ? 0.0 : 1.0;
}