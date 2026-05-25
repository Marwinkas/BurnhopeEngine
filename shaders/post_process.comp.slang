import common.Bindless;
import common.GlobalUbo;

struct PostProcessHeapPC {
    uint globalUbo;
    uint hdrOutput;
    uint hdrInput;
    uint depthInput;
    uint historyInput;
    uint historyOutput;
    uint exposureBuffer;
    uint dirtInput;
    uint normalInput;
    uint halftoneInput;
    uint paletteInput;
    uint ditherInput;
    uint defaultSampler;
};

[[vk::push_constant]] PostProcessHeapPC pc;



float3 ApplyCDL(float3 c, float4 lift, float4 gamma, float4 gain, float4 offset) {
    c = c * gain.rgb + offset.rgb;
    c = c + lift.rgb * clamp(float3(1.0) - c, 0.0, 1.0);
    c = pow(max(c, float3(0.0001)), gamma.rgb);
    return c;
}

float3 isolateBright(float3 c, float thresh) {
    float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
    float contribution = max(0.0, luma - thresh);
    contribution /= max(luma, 0.00001);
    return min(c * contribution, float3(5.0)); // Clamp maximum brightness
}

// RGB to HSV
float3 rgb2hsv(float3 c) {
    float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    float4 p = lerp(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
    float4 q = lerp(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

// HSV to RGB
float3 hsv2rgb(float3 c) {
    float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * lerp(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// Правильное физическое искажение линзы (Barrel Distortion)
float2 distCoords(float2 uv, float strength) {
    float2 d = uv - 0.5;
    float r2 = dot(d, d);
    return uv + d * (strength * r2);
}

float3 ReadRawColor(float2 uv) {
    return heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), uv, 0.0).rgb;
}

float3 ReadColor(float2 uv) {
    float3 c;
    if (loadGlobalUbo(pc.globalUbo).ppVignetteGrain.w > 0.0) { // Chromatic Aberration
        float ca = loadGlobalUbo(pc.globalUbo).ppVignetteGrain.w;
        float dist = length(uv - 0.5);
        float2 offset = (uv - 0.5) * ca * dist;
        c.r = ReadRawColor(uv + offset).r;
        c.g = ReadRawColor(uv).g;
        c.b = ReadRawColor(uv - offset).b;
    } else {
        c = ReadRawColor(uv);
    }
    return c;
}

// Эффект картины маслом (Kuwahara Filter)
float3 ApplyKuwahara(float2 uv, int radius) {
    float2 texel = 1.0 / loadGlobalUbo(pc.globalUbo).screenSize.xy;
    int offsetsX[4][2] = {{-radius, 0}, {0, radius}, {-radius, 0}, {0, radius}};
    int offsetsY[4][2] = {{-radius, 0}, {-radius, 0}, {0, radius}, {0, radius}};
    
    float minVar = half(1e6);
    float3 finalColor = float3(0.0);
    
    for(int q = 0; q < 4; q++) {
        float3 sum = float3(0.0);
        float3 sum2 = float3(0.0);
        float count = half(0.0);
        
        for(int x = offsetsX[q][0]; x <= offsetsX[q][1]; x++) {
            for(int y = offsetsY[q][0]; y <= offsetsY[q][1]; y++) {
                float3 c = float3(ReadColor(uv + float2(x, y) * texel));
                sum += c;
                sum2 += c * c;
                count += half(1.0);
            }
        }
        float3 mean = sum / count;
        float3 variance = abs((sum2 / count) - (mean * mean));
        float totalVar = variance.r + variance.g + variance.b;
        
        if(totalVar < minVar) {
            minVar = totalVar;
            finalColor = mean;
        }
    }
    return float3(finalColor);
}

float hatch(float2 uv, float angle, float scale, float thickness) {
    float s = sin(angle), c = cos(angle);
    float2 rotatedUV = mul(float2x2(c, -s, s, c), uv);
    return smoothstep(thickness - 0.1, thickness + 0.1, sin(rotatedUV.x * scale));
}

float3 getEdge(float2 pos, float w) {
    float2 texel = w / loadGlobalUbo(pc.globalUbo).screenSize.xy;
    float3 s00 = heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), clamp(pos + float2(-texel.x, -texel.y), 0.0, 1.0), 0.0).rgb;
    float3 s10 = heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), clamp(pos + float2(0.0, -texel.y), 0.0, 1.0), 0.0).rgb;
    float3 s20 = heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), clamp(pos + float2(texel.x, -texel.y), 0.0, 1.0), 0.0).rgb;
    float3 s01 = heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), clamp(pos + float2(-texel.x, 0.0), 0.0, 1.0), 0.0).rgb;
    float3 s21 = heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), clamp(pos + float2(texel.x, 0.0), 0.0, 1.0), 0.0).rgb;
    float3 s02 = heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), clamp(pos + float2(-texel.x, texel.y), 0.0, 1.0), 0.0).rgb;
    float3 s12 = heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), clamp(pos + float2(0.0, texel.y), 0.0, 1.0), 0.0).rgb;
    float3 s22 = heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), clamp(pos + float2(texel.x, texel.y), 0.0, 1.0), 0.0).rgb;
    float3 gx = -s00 - 2.0*s01 - s02 + s20 + 2.0*s21 + s22;
    float3 gy = -s00 - 2.0*s10 - s20 + s02 + 2.0*s12 + s22;
    return sqrt(gx*gx + gy*gy);
}

// Быстрый шум для Voronoi
float3 voronoi(float2 uv, float scale) {
    float2 g = floor(uv * scale);
    float2 f = frac(uv * scale);
    float2 res = float2(8.0);
    float hash = 0.0;
    for(int y=-1; y<=1; y++) {
        for(int x=-1; x<=1; x++) {
            float2 lattice = float2(x,y);
            // Вариант 1: Создаем вектор, используя разные смещения для X и Y
            float noiseX = frac(sin(dot(g + lattice, float2(12.9898, 78.233))) * 43758.5453);
            float noiseY = frac(sin(dot(g + lattice, float2(39.346, 11.135))) * 43758.5453);
            float2 r = float2(noiseX, noiseY);
            float2 d = lattice + r - f;
            float dist = dot(d,d);
            if(dist < res.x) { res.y = res.x; res.x = dist; hash = noiseX; }
            else if(dist < res.y) { res.y = dist; }
        }
    }
    return float3(res.x, res.y, hash);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);
    int2 pixel = int2(id.xy);
    if (pixel.x >= int(loadGlobalUbo(pc.globalUbo).screenSize.x) || pixel.y >= int(loadGlobalUbo(pc.globalUbo).screenSize.y)) return;
    float2 uv = (float2(pixel) + 0.5) / loadGlobalUbo(pc.globalUbo).screenSize.xy;
    float rawD = heapTex2D_r(pc.depthInput).SampleLevel(heapSampler(pc.defaultSampler), uv, 0.0).r;

    // --- WORLD CURVATURE (Inception) ---
    if (loadGlobalUbo(pc.globalUbo).ppCanvasInk.y > 0.5) {
        float curve = loadGlobalUbo(pc.globalUbo).ppCanvasInk.z;
        uv.y -= (uv.x - 0.5) * (uv.x - 0.5) * curve; 
    }

    // --- ENVIRONMENT BREATHING ---
    if (loadGlobalUbo(pc.globalUbo).ppCausticsBreath.y > 0.5) {
        float amp = loadGlobalUbo(pc.globalUbo).ppCausticsBreath.z;
        float speed = loadGlobalUbo(pc.globalUbo).ppCausticsBreath.w;
        float t = loadGlobalUbo(pc.globalUbo).ppMotionBlur.z * speed;
        float breath = sin(t) * amp * 0.05;
        uv = (uv - 0.5) * (1.0 + breath) + 0.5;
    }
    
    // --- DROSTE EFFECT (Рекурсивный экран) ---
    if (loadGlobalUbo(pc.globalUbo).ppReactDroste.w > 0.0) {
        float scale = loadGlobalUbo(pc.globalUbo).ppReactDroste.w;
        float2 dUV = uv - 0.5;
        if (length(dUV) > 0.0) uv = 0.5 + normalize(dUV) * exp(fmod(log(length(dUV)), scale));
    }
    
    // --- SLIT SCAN ---
    if (loadGlobalUbo(pc.globalUbo).ppPurkinjeSlit.w > 0.5) {
        float delay = uv.y * loadGlobalUbo(pc.globalUbo).ppReactDroste.x;
        // Требуется массив истории или сильная зависимость от Motion Vectors. 
        // Эмуляция через волну:
        uv.x += sin(uv.y * 10.0 + loadGlobalUbo(pc.globalUbo).ppMotionBlur.z * delay) * 0.05;
    }
    
    // --- ROLLING SHUTTER ---
    if (loadGlobalUbo(pc.globalUbo).ppEctoRolling.w > 0.5) {
        float4 ndc = float4(uv * 2.0 - 1.0, rawD, 1.0);
        float4 worldPos = mul(loadGlobalUbo(pc.globalUbo).invViewProj, ndc); worldPos /= worldPos.w;
        float4 prevNdc = mul(loadGlobalUbo(pc.globalUbo).prevViewProj, worldPos); prevNdc /= prevNdc.w;
        float2 velocity = uv - (prevNdc.xy * 0.5 + 0.5);
        uv -= velocity * uv.y * loadGlobalUbo(pc.globalUbo).ppPurkinjeSlit.x * 50.0;
    }
    
    // --- GRAVITATIONAL LENSING ---
    if (loadGlobalUbo(pc.globalUbo).ppPhosASCII.z > 0.5) {
        float2 center = float2(0.5);
        float2 dir = uv - center;
        float dist = length(dir);
        if (dist > 0.01) {
            float bend = loadGlobalUbo(pc.globalUbo).ppPhosASCII.w / dist;
            uv -= normalize(dir) * bend * 0.01;
        }
    }
    
    // --- SCREEN TEAR / GLITCH SLICE ---
    if (loadGlobalUbo(pc.globalUbo).ppGlitch4.y > 0.5) {
        float t = loadGlobalUbo(pc.globalUbo).ppMotionBlur.z * loadGlobalUbo(pc.globalUbo).ppGlitch4.z; // Frequency
        float slice = step(0.95, frac(sin(floor(uv.y * 20.0 + t)) * 43758.5453));
        uv.x += slice * loadGlobalUbo(pc.globalUbo).ppGlitch4.w * (frac(sin(t * 100.0)) - 0.5);
        uv = clamp(uv, 0.0, 1.0);
    }
    
    // --- ANALOG VIDEO NOISE (Horizontal Sync Loss) ---
    if (loadGlobalUbo(pc.globalUbo).ppHatchAnalog.y > 0.5) {
        float syncLoss = loadGlobalUbo(pc.globalUbo).ppHatchAnalog.z;
        float t = loadGlobalUbo(pc.globalUbo).ppMotionBlur.z * 10.0;
        float wave = sin(uv.y * 5.0 + t) * sin(uv.y * 12.0 - t * 0.5);
        if (wave > 0.8) uv.x += (wave - 0.8) * syncLoss;
        uv.x = frac(uv.x); // Wrapping
    }
    
    // --- Fluid Dynamics (Navier-Stokes Advection Approximation) ---
    if (loadGlobalUbo(pc.globalUbo).ppAfterBleed.w > 0.5 || loadGlobalUbo(pc.globalUbo).ppPsych4.y > 0.0) {
        float vis = max(loadGlobalUbo(pc.globalUbo).ppPsych4.y, loadGlobalUbo(pc.globalUbo).ppFluidCMYK.x);
        float4 ndc = float4(uv * 2.0 - 1.0, rawD, 1.0);
        float4 worldPos = mul(loadGlobalUbo(pc.globalUbo).invViewProj, ndc); worldPos /= worldPos.w;
        float4 prevNdc = mul(loadGlobalUbo(pc.globalUbo).prevViewProj, worldPos); prevNdc /= prevNdc.w;
        float2 velocity = (uv - (prevNdc.xy * 0.5 + 0.5));
        
        float t = loadGlobalUbo(pc.globalUbo).ppMotionBlur.z * 0.5;
        float2 curl = float2(sin(uv.y * 15.0 + t), cos(uv.x * 15.0 - t));
        
        uv -= (velocity * vis + curl * 0.002 * vis) * 5.0;
    }

    // --- LENS BREATHING ---
    if (loadGlobalUbo(pc.globalUbo).ppLens3.w > 0.5) {
        float focus = loadGlobalUbo(pc.globalUbo).ppDoFParams.y;
        float breath = loadGlobalUbo(pc.globalUbo).ppLens4.x;
        float scale = 1.0 - (1.0 / max(focus, 0.1)) * breath * 0.01;
        uv = (uv - 0.5) * scale + 0.5;
    }
        // --- VECTOR FIELD FLOW ---
    if (loadGlobalUbo(pc.globalUbo).ppGravVector.x > 0.5) {
        int texIdx = int(loadGlobalUbo(pc.globalUbo).ppTexIndices.x); 
        if (texIdx >= 0) {
            float scale = loadGlobalUbo(pc.globalUbo).ppPsych8.y;
            // Тайлинг текстуры + анимация
            float2 flow = heapBindlessTex2D(texIdx).SampleLevel(heapSampler(pc.defaultSampler), uv * scale + loadGlobalUbo(pc.globalUbo).ppMotionBlur.z * 0.1, 0.0).xy * 2.0 - 1.0;
            uv += flow * loadGlobalUbo(pc.globalUbo).ppGravVector.y;
        }
    }
    float pSize = 1.0;
    // --- PIXELATION ---
    if (loadGlobalUbo(pc.globalUbo).ppRetroParams.x > 0.5 && loadGlobalUbo(pc.globalUbo).ppRetroParams2.x > 1.1) {
        pSize = floor(loadGlobalUbo(pc.globalUbo).ppRetroParams2.x);
        float2 centerPixel = floor(float2(pixel) / pSize) * pSize + (pSize * 0.5);
        uv = centerPixel / loadGlobalUbo(pc.globalUbo).screenSize.xy;
    }
    
    // Distort UVs early (Barrel Distortion)
    if (loadGlobalUbo(pc.globalUbo).ppDistortionDirt.x > 0.5) {
        float2 d = uv - 0.5;
        float r2 = dot(d, d);
        uv = 0.5 + d * (1.0 + r2 * loadGlobalUbo(pc.globalUbo).ppDistortionDirt.y);
        
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
            heapRWTex2D(pc.hdrOutput)[pixel] = float4(0.0, 0.0, 0.0, 1.0);
            heapRWTex2D(pc.historyOutput)[pixel] = float4(0.0);
            return;
        }
    }
    
    if (loadGlobalUbo(pc.globalUbo).ppWarp.x > 0.5) {
        float time = loadGlobalUbo(pc.globalUbo).ppMotionBlur.z * loadGlobalUbo(pc.globalUbo).ppWarp.z;
        float noise = frac(sin(dot(uv * 10.0 + time, float2(12.9898, 78.233))) * 43758.5453);
        uv += (noise - 0.5) * loadGlobalUbo(pc.globalUbo).ppWarp.y;
    }
    
    // Screen Space Refraction (Heat Haze / Water)
    if (loadGlobalUbo(pc.globalUbo).ppDitherAniso.z > 0.5) {
        float time = loadGlobalUbo(pc.globalUbo).ppVignetteGrain.z; // time * speed
        float2 refr;
        refr.x = sin(uv.y * 30.0 + time) * cos(uv.x * 20.0 + time);
        refr.y = cos(uv.x * 25.0 + time) * sin(uv.y * 35.0 + time);
        uv += refr * loadGlobalUbo(pc.globalUbo).ppDitherAniso.w;
        uv = clamp(uv, 0.0, 1.0);
    }

    // Retro Glitch UV displacement
    if (loadGlobalUbo(pc.globalUbo).ppRetroParams.x > 0.5 && loadGlobalUbo(pc.globalUbo).ppRetroParams.z > 0.0) {
        float t = loadGlobalUbo(pc.globalUbo).ppMotionBlur.z * 10.0; // time
        float glitchBand = step(0.95, frac(sin(floor(uv.y * 20.0) * 12.9898 + t) * 43758.5453));
        float shift = (frac(sin(t * 123.456) * 789.123) - 0.5) * 0.1 * loadGlobalUbo(pc.globalUbo).ppRetroParams.z;
        uv.x += glitchBand * shift;
        uv = clamp(uv, 0.0, 1.0);
    }
    
    // Voronoi Stained Glass UV Pixelation
    if (loadGlobalUbo(pc.globalUbo).ppStylizedParams.w > 0.0) {
        float scale = loadGlobalUbo(pc.globalUbo).ppStylizedParams.w;
        float3 v = voronoi(uv, scale);
        uv += (v.z - 0.5) * 0.1; // Физическое искажение витража (Shattered Glass)
    }
    
    // --- SCREEN FROST ---
    if (loadGlobalUbo(pc.globalUbo).ppColorSplash.w > 0.5) {
        float2 noiseUV = uv * 3.0;
        float frostNoise = frac(sin(dot(noiseUV, float2(12.9898, 78.233))) * 43758.5453);
        float dist = length(uv - 0.5) * 2.0;
        float frostMask = smoothstep(0.6, 1.0, dist) * loadGlobalUbo(pc.globalUbo).ppHeatFrost.x;
        uv += (frostNoise - 0.5) * frostMask * 0.05;
    }
    
    // --- SCANLINE MOIRE ---
    if (loadGlobalUbo(pc.globalUbo).ppHatchAnalog.w > 0.5) {
        float scale = loadGlobalUbo(pc.globalUbo).ppMoireTunnel.x;
        float moire = sin(uv.x * scale) * cos(uv.y * scale * 0.9);
        uv += (moire - 0.5) * 0.005; // Легкое геометрическое искажение
    }
    
    // --- HEAT SHIMMER (MIRAGE) ---
    if (loadGlobalUbo(pc.globalUbo).ppColorSplash.y > 0.5) {
        float t = loadGlobalUbo(pc.globalUbo).ppMotionBlur.z * 5.0;
        float noise = frac(sin(dot(uv * 15.0 + float2(t, t * 0.5), float2(12.9898, 78.233))) * 43758.5453);
        uv.x += (noise - 0.5) * loadGlobalUbo(pc.globalUbo).ppColorSplash.z;
        uv = clamp(uv, 0.0, 1.0);
    }
    
    // --- LENS CONDENSATION / HUMIDITY ---
    if (loadGlobalUbo(pc.globalUbo).ppFluidCMYK.w > 0.5) {
        float fogMask = smoothstep(0.3, 1.0, length(uv - 0.5) * 2.0) * loadGlobalUbo(pc.globalUbo).ppCondenDust.x;
        uv += (frac(sin(dot(uv*50.0, float2(12.9, 78.2)))*43758.5) - 0.5) * fogMask * 0.02;
        uv = clamp(uv, 0.0, 1.0);
    }

    // --- LENS WEATHER & WATER DROPS (Raindrops with Physics & Shapes) ---
    if (loadGlobalUbo(pc.globalUbo).ppWeatherSSR.x > 0.5 || loadGlobalUbo(pc.globalUbo).ppHeatFrost.y > 0.5) {
        float speed = loadGlobalUbo(pc.globalUbo).ppWeatherParams.x;
        float size = loadGlobalUbo(pc.globalUbo).ppWeatherParams.y;
        float densityThreshold = loadGlobalUbo(pc.globalUbo).ppWeatherParams.z;
        float distortion = loadGlobalUbo(pc.globalUbo).ppWeatherParams.w;

        // Если включены отдельные "Капли Воды", применяем их настройку преломления
        if (loadGlobalUbo(pc.globalUbo).ppHeatFrost.y > 0.5) {
            distortion = max(distortion, loadGlobalUbo(pc.globalUbo).ppHeatFrost.z); // dropRefraction
            if (loadGlobalUbo(pc.globalUbo).ppWeatherSSR.x < 0.5) { // Если погода выключена, задаем дефолт
                speed = 1.0; size = 20.0; densityThreshold = 0.8;
            }
        }

        float time = loadGlobalUbo(pc.globalUbo).ppMotionBlur.z * speed;
        
        // Физическая симуляция (Инерция камеры + Завихрения ветра Навье-Стокса)
        float4 ndc = float4(uv * 2.0 - 1.0, rawD, 1.0);
        float4 worldPos = mul(loadGlobalUbo(pc.globalUbo).invViewProj, ndc); worldPos /= worldPos.w;
        float4 prevNdc = mul(loadGlobalUbo(pc.globalUbo).prevViewProj, worldPos); prevNdc /= prevNdc.w;
        float2 velocity = (uv - (prevNdc.xy * 0.5 + 0.5)); 
        
        float windTime = loadGlobalUbo(pc.globalUbo).ppMotionBlur.z * 0.5;
        float2 windCurl = float2(
            sin(uv.y * 5.0 + windTime) * cos(uv.x * 3.0 - windTime),
            cos(uv.x * 4.0 - windTime) * sin(uv.y * 6.0 + windTime)
        ) * 0.02;
        
        // Смещаем UV сетки капель в противоход движению камеры, создавая инерцию
        float2 dropUV = uv - (velocity * 2.0) - windCurl;
        
        float2 grid = dropUV * float2(size, size * 0.5);
        
        // Уникальный рандом для каждой вертикальной колонки
        float idX = floor(grid.x);
        float rndX = frac(sin(idX * 123.456) * 789.123);
        
        // Капли рандомно виляют по X при падении
        grid.x += sin(grid.y * 3.0 + time * 2.0 + rndX * 10.0) * 0.15 * rndX;
        
        // Рандомная скорость падения каждой колонки (от 0.5 до 2.5)
        float colSpeed = 0.5 + rndX * 2.0; 
        
        // ВАЖНО: Знак минус (-=) заставляет капли падать ВНИЗ по экрану
        grid.y -= time * colSpeed * 2.0;
        
        // Смещаем начальную позицию по Y, чтобы колонки падали не синхронно
        grid.y += rndX * 50.0;
        
        float2 id = floor(grid);
        float2 f = frac(grid) - 0.5;
        float rnd = frac(sin(dot(id, float2(12.9898, 78.233))) * 43758.5453);
        
        if (rnd > (1.0 - densityThreshold)) {
            // Жизненный цикл капли (чтобы они рандомно появлялись и исчезали, а не висели вечно)
            float dropLife = frac(time * 0.5 + rnd);
            float dropFade = smoothstep(0.0, 0.1, dropLife) * smoothstep(1.0, 0.8, dropLife);
            
            // Выбор формы на основе ID ячейки
            float shapeRnd = frac(rnd * 43.123);
            if (shapeRnd > 0.6) {
                // Идеальная капля - сужаем верхнюю часть (f.y < 0.0)
                f.x *= 1.0 + max(-f.y * 4.0, 0.0);
            } else if (shapeRnd > 0.3) {
                // Прямоугольная / длинная струйка
                f.x *= 2.5; 
                f.y *= 0.6; 
            } else {
                // Бесформенная лужица (splat)
                f.x *= 1.0 + sin(f.y * 15.0) * 0.4;
                f.y *= 1.0 + cos(f.x * 15.0) * 0.4;
            }

            float dist = length(f);
            float edgeFade = smoothstep(0.2, 0.05, dist); 
            
            if (dist < 0.2) { 
                // Преломление работает как линза
                uv += f * distortion * 8.0 * dropFade * edgeFade; 
                uv = clamp(uv, 0.0, 1.0);
            }
        }
    }

    float3 color = float3(0.0);
    
    // --- WATERCOLOR FILTER ---
    if (loadGlobalUbo(pc.globalUbo).ppStylized3.w > 0.5) {
        float rad = loadGlobalUbo(pc.globalUbo).ppStylized4.x;
        float2 noiseUV = uv * 10.0;
        float2 wobble = float2(frac(sin(dot(noiseUV, float2(12.9898, 78.233))) * 43758.5453), 
                           frac(sin(dot(noiseUV, float2(39.346, 11.135))) * 43758.5453)) - 0.5;
        color = ApplyKuwahara(uv + wobble * 0.005 * rad, int(rad));
    } 
    // --- POINTILLISM ---
    else if (loadGlobalUbo(pc.globalUbo).ppStylized4.y > 0.5) {
        float pSize = max(loadGlobalUbo(pc.globalUbo).ppStylized4.z, 1.0);
        float pDens = loadGlobalUbo(pc.globalUbo).ppStylized4.w;
        float2 pUV = floor(uv * loadGlobalUbo(pc.globalUbo).screenSize.xy / pSize) * pSize / loadGlobalUbo(pc.globalUbo).screenSize.xy;
        float2 localUV = frac(uv * loadGlobalUbo(pc.globalUbo).screenSize.xy / pSize) - 0.5;
        float noise = frac(sin(dot(pUV, float2(12.9898, 78.233))) * 43758.5453);
        float2 randOffset = float2(cos(noise * 6.28), sin(noise * 6.28)) * pSize * 0.5 / loadGlobalUbo(pc.globalUbo).screenSize.xy;
        float3 pColor = ReadColor(clamp(pUV + randOffset, 0.0, 1.0));
        float pMask = smoothstep(0.5 * pDens, 0.4 * pDens, length(localUV));
        color = lerp(float3(0.0), pColor, pMask);
    } else if (loadGlobalUbo(pc.globalUbo).ppStylizedParams.y > 0.0) {
        color = ApplyKuwahara(uv, int(loadGlobalUbo(pc.globalUbo).ppStylizedParams.y));
    } else {
        color = ReadColor(uv);
    }
    
    // --- PERIPHERAL TUNNEL VISION ---
    if (loadGlobalUbo(pc.globalUbo).ppMoireTunnel.y > 0.5) {
        float intensity = loadGlobalUbo(pc.globalUbo).ppMoireTunnel.z;
        float dist = distance(uv, float2(0.5));
        float tunnel = smoothstep(0.3, 0.8, dist) * intensity;
        color = lerp(color, float3(dot(color, float3(0.333)) * 0.2), tunnel);
    }
    
    // --- CMYK OFFSET ---
    if (loadGlobalUbo(pc.globalUbo).ppFluidCMYK.y > 0.5) {
        float off = loadGlobalUbo(pc.globalUbo).ppFluidCMYK.z;
        float3 cC = ReadColor(uv + float2(off, off));
        float3 cM = ReadColor(uv + float2(-off, off));
        float3 cY = ReadColor(uv + float2(0.0, -off));
        // CMYK Extract
        float r = cC.r; float g = cM.g; float b = cY.b;
        color = float3(r, g, b);
    }
    
    // --- PURKINJE SHIFT (Ночное зрение) ---
    if (loadGlobalUbo(pc.globalUbo).ppPurkinjeSlit.y > 0.5) {
        float luma = dot(color, float3(0.299, 0.587, 0.114));
        float3 nightVision = float3(0.05, 0.15, 0.35) * luma * 2.0; // Синеватый фильтр
        float isDark = 1.0 - smoothstep(0.0, 0.5, luma);
        color = lerp(color, nightVision, isDark * loadGlobalUbo(pc.globalUbo).ppPurkinjeSlit.z);
    }
    
    // --- INK BLEED ---
    if (loadGlobalUbo(pc.globalUbo).ppDropsEcho.w > 0.5) {
        float radius = loadGlobalUbo(pc.globalUbo).ppCanvasInk.x;
        float2 texel = 1.0 / loadGlobalUbo(pc.globalUbo).screenSize.xy;
        float3 minColor = color;
        for (int x = -1; x <= 1; x++) {
            for (int y = -1; y <= 1; y++) {
                float3 c = ReadColor(clamp(uv + float2(x, y) * texel * radius, 0.0, 1.0));
                minColor = min(minColor, c);
            }
        }
        color = minColor;
    }
    
    // --- DUST MOTES (Screen Space Particles) ---
    if (loadGlobalUbo(pc.globalUbo).ppCondenDust.y > 0.5) {
        float t = loadGlobalUbo(pc.globalUbo).ppMotionBlur.z * 0.2;
        float dust = frac(sin(dot(floor(uv * 100.0 + float2(t, t * 0.5)), float2(12.9898, 78.233))) * 43758.5453);
        if (dust > 0.995) {
            color += float3(1.0) * dot(color, float3(0.333)) * loadGlobalUbo(pc.globalUbo).ppCondenDust.z; 
        }
    }
    
    // --- PURKINJE TREE (Blood Vessels on Retina) ---
    if (loadGlobalUbo(pc.globalUbo).ppPsych3.w > 0.0) {
        float pScale = loadGlobalUbo(pc.globalUbo).ppPsych3.w * 50.0;
        float t = loadGlobalUbo(pc.globalUbo).ppMotionBlur.z * loadGlobalUbo(pc.globalUbo).ppPsych8.x; // Speed
        float thickness = loadGlobalUbo(pc.globalUbo).ppPsych7.x;
        float3 pColor = loadGlobalUbo(pc.globalUbo).ppPsych7.yzw; // Цвет кровеносных сосудов
        float veins = sin(uv.x * pScale + t + sin(uv.y * pScale * 0.8 - t)) * cos(uv.y * pScale * 1.2 + t);
        float vignette = smoothstep(0.4, 1.0, distance(uv, float2(0.5)));
        color = lerp(color, pColor, smoothstep(1.0 - thickness, 1.0, veins) * vignette * 0.8);
    }

    // --- JPEG / MPEG ARTIFACTS ---
    if (loadGlobalUbo(pc.globalUbo).ppGlitch3.z > 0.5) {
        float bSize = max(loadGlobalUbo(pc.globalUbo).ppGlitch3.w, 1.0);
        float q = max(loadGlobalUbo(pc.globalUbo).ppGlitch4.x, 1.0);
        float2 blockUV = floor(uv * loadGlobalUbo(pc.globalUbo).screenSize.xy / bSize) * bSize / loadGlobalUbo(pc.globalUbo).screenSize.xy;
        color = floor(ReadColor(blockUV) * q + 0.5) / q;
    }
    
    // --- OPTICAL SOUP / COLOR BLEEDING ---
    if (loadGlobalUbo(pc.globalUbo).ppBleedMosh.x > 0.0) {
        float radius = loadGlobalUbo(pc.globalUbo).ppBleedMosh.y;
        float2 texel = 1.0 / loadGlobalUbo(pc.globalUbo).screenSize.xy;
        float3 edgeH = ReadColor(uv + float2(texel.x, 0.0)) - ReadColor(uv - float2(texel.x, 0.0));
        float3 edgeV = ReadColor(uv + float2(0.0, texel.y)) - ReadColor(uv - float2(0.0, texel.y));
        float2 bleedDir = normalize(float2(length(edgeH), length(edgeV)) + 0.001);
        color += ReadColor(clamp(uv + bleedDir * radius * texel, 0.0, 1.0)) * 0.5;
        color *= 0.666;
    }
    
    // --- PIXEL SORTING ---
    if (loadGlobalUbo(pc.globalUbo).ppAsciiSort.z > 0.0) {
        float thresh = loadGlobalUbo(pc.globalUbo).ppAsciiSort.w;
        float angle = loadGlobalUbo(pc.globalUbo).ppPixelSort.x * 3.14159 / 180.0;
        float sortLen = loadGlobalUbo(pc.globalUbo).ppPixelSort.y;
        float time = loadGlobalUbo(pc.globalUbo).ppMotionBlur.z * loadGlobalUbo(pc.globalUbo).ppPixelSort.z;
        
        float2 dir = float2(cos(angle), sin(angle));
        float3 maxColor = color;
        float maxLuma = dot(color, float3(0.299, 0.587, 0.114));
        
        int steps = max(1, int(sortLen * 50.0));
        for(int i = 1; i < steps; i++) {
            float2 offset = dir * (float(i) * 0.005 + sin(time)*0.01);
            float3 sampleC = ReadColor(clamp(uv - offset, 0.0, 1.0));
            float luma = dot(sampleC, float3(0.299, 0.587, 0.114));
            if(luma > thresh && luma > maxLuma) {
                maxColor = sampleC;
                maxLuma = luma;
            }
        }
        color = maxColor;
    }
    
    // --- BLURS ---
    if (loadGlobalUbo(pc.globalUbo).ppBlurs.x > 0.0) {
        int mode = int(loadGlobalUbo(pc.globalUbo).ppBlurs.x + 0.5);
        float strength = loadGlobalUbo(pc.globalUbo).ppBlurs.y;
        float radius = loadGlobalUbo(pc.globalUbo).ppBlurs.z;
        float3 blurColor = float3(0.0);
        
        if (mode == 1) { 
            float totW = half(0.0); int r = int(min(radius, 15.0)); // CLAMPED
            float3 blurColor16 = float3(0.0);
            for(int x = -r; x <= r; x++) {
                for(int y = -r; y <= r; y++) {
                    blurColor16 += float3(ReadColor(uv + float2(x,y) / loadGlobalUbo(pc.globalUbo).screenSize.xy));
                    totW += half(1.0);
                }
            }
            color = lerp(color, float3(blurColor16 / max(totW, half(1.0))), strength);
        } else if (mode == 2) { 
            float totW = half(0.0); int r = int(min(radius, 15.0)); // CLAMPED
            float3 blurColor16 = float3(0.0);
            for(int x = -r; x <= r; x++) {
                for(int y = -r; y <= r; y++) {
                    float w = half(exp(-(float(x*x + y*y)) / (2.0 * max(radius, 0.1))));
                    blurColor16 += float3(ReadColor(uv + float2(x,y) / loadGlobalUbo(pc.globalUbo).screenSize.xy)) * w;
                    totW += w;
                }
            }
            color = lerp(color, float3(blurColor16 / max(totW, half(1.0))), strength);
        } else if (mode == 3) { 
            float2 center = loadGlobalUbo(pc.globalUbo).ppBlurCenter.xy;
            float2 dir = center - uv;
            int samples = int(radius * 2.0);
            for(int i = 0; i < samples; i++) {
                blurColor += ReadColor(uv + dir * (float(i) / float(samples)) * 0.1);
            }
            color = lerp(color, blurColor / float(max(samples, 1)), strength);
        } else if (mode == 4) { 
            float2 cPixel = floor(uv * loadGlobalUbo(pc.globalUbo).screenSize.xy / radius) * radius + radius * 0.5;
            blurColor = ReadColor(cPixel / loadGlobalUbo(pc.globalUbo).screenSize.xy);
            color = lerp(color, blurColor, strength);
        }
    }
    
    // --- TILT SHIFT ---
    if (loadGlobalUbo(pc.globalUbo).ppLens3.x > 0.5) {
        float amount = loadGlobalUbo(pc.globalUbo).ppLens3.y;
        float falloff = loadGlobalUbo(pc.globalUbo).ppLens3.z;
        float blurAmount = smoothstep(falloff, 1.0, abs(uv.y - 0.5) * 2.0) * amount;
        if (blurAmount > 0.0) {
            float3 blurC = float3(0.0);
            int samples = int(blurAmount * 5.0);
            for(int i=-samples; i<=samples; i++) blurC += ReadColor(clamp(uv + float2(0.0, i) / loadGlobalUbo(pc.globalUbo).screenSize.y, 0.0, 1.0));
            color = lerp(color, blurC / float(samples * 2 + 1), clamp(blurAmount, 0.0, 1.0));
        }
        // Усиление цветов в центре фокуса
        float centerDist = smoothstep(falloff + 0.2, 0.0, abs(uv.y - 0.5) * 2.0);
        color = lerp(color, color * 1.3, centerDist);
    }

    // --- SPEED LINES (ANIME) ---
    if (loadGlobalUbo(pc.globalUbo).ppSpeedLines.x > 0.5) {
        float time = loadGlobalUbo(pc.globalUbo).ppMotionBlur.z * loadGlobalUbo(pc.globalUbo).ppPsych8.w * 10.0;
        float2 center = float2(0.5);
        float2 dir = uv - center;
        float angle = atan2(dir.y, dir.x);
        float noise = frac(sin(floor(angle * loadGlobalUbo(pc.globalUbo).ppPsych8.z) * 12.9898 + time) * 43758.5453); // z: Lines Count
        float dist = length(dir) * 2.0;
        // Растягиваем шум вдоль радиуса
        float lines = step(0.9, noise) * smoothstep(1.0 - loadGlobalUbo(pc.globalUbo).ppPsych8.w, 1.0, dist); // w: Length
        color += float3(lines); // Белые штрихи по краям
    }
    
    // --- SCREEN-SPACE CANVAS ---
    if (loadGlobalUbo(pc.globalUbo).ppDropsEcho.y > 0.5) {
        float2 canvasUV = uv * 200.0;
        float noise = frac(sin(dot(canvasUV, float2(12.9898, 78.233))) * 43758.5453);
        color *= lerp(1.0, noise, 0.1 * loadGlobalUbo(pc.globalUbo).ppDropsEcho.z);
    }

    // --- EYE FLOATERS ---
    if (loadGlobalUbo(pc.globalUbo).ppPsych1.y > 0.0) {
        float t = loadGlobalUbo(pc.globalUbo).ppMotionBlur.z * 0.2;
        float4 ndc = float4(uv * 2.0 - 1.0, rawD, 1.0);
        float4 worldPos = mul(loadGlobalUbo(pc.globalUbo).invViewProj, ndc); worldPos /= worldPos.w;
        float4 prevNdc = mul(loadGlobalUbo(pc.globalUbo).prevViewProj, worldPos); prevNdc /= prevNdc.w;
        float2 vel = uv - (prevNdc.xy * 0.5 + 0.5);
        float2 floatUV = uv * 10.0 + vel * 50.0; // Floaters lag behind camera
        float f1 = sin(floatUV.x * 2.0 + t) * cos(floatUV.y * 3.0 - t);
        float f2 = sin(floatUV.x * 5.0 - t * 2.0) * cos(floatUV.y * 4.0 + t);
        float floaterShape = smoothstep(0.5, 1.0, f1 * f2);
        color = lerp(color, float3(0.0), floaterShape * loadGlobalUbo(pc.globalUbo).ppPsych1.y * 0.8);
    }

    if (any(isnan(color)) || any(isinf(color))) color = float3(0.0);
    
    // --- SCREEN SPACE SUBSURFACE SCATTERING (SSSS) ---
    if (loadGlobalUbo(pc.globalUbo).ppSSSS.x > 0.5) {
        float roughness = heapTex2D(pc.normalInput).SampleLevel(heapSampler(pc.defaultSampler), uv, 0.0).a;
        float cDepth = heapTex2D_r(pc.depthInput).SampleLevel(heapSampler(pc.defaultSampler), uv, 0.0).r;
        if (cDepth < 1.0 && roughness > 0.3 && roughness < 0.8) { 
            float3 sssColor = float3(0.0); 
            float3 totW = float3(0.0);
            float2 texel = 1.0 / loadGlobalUbo(pc.globalUbo).screenSize.xy;
            float3 scatterProfile[5] = float3[](
                float3(0.1, 0.03, 0.01), float3(0.3, 0.1, 0.05), float3(1.0, 0.8, 0.6), float3(0.3, 0.1, 0.05), float3(0.1, 0.03, 0.01)
            );
            for(int x=-2; x<=2; x++) {
                for(int y=-2; y<=2; y++) {
                    float2 off = float2(x,y) * texel * loadGlobalUbo(pc.globalUbo).ppSSSS.y * 100.0;
                    float2 sampleUV = clamp(uv + off, 0.0, 1.0);
                    float sDepth = heapTex2D_r(pc.depthInput).SampleLevel(heapSampler(pc.defaultSampler), sampleUV, 0.0).r;
                    float w = exp(-abs(cDepth - sDepth) * 50.0);
                    float3 profileW = scatterProfile[x+2] * scatterProfile[y+2] * w;
                    sssColor += heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), sampleUV, 0.0).rgb * profileW; 
                    totW += profileW;
                }
            }
            color = lerp(color, sssColor / max(totW, float3(0.001)), 0.8 * loadGlobalUbo(pc.globalUbo).ppSSSS.x);
        }
    }

    // --- STOCHASTIC SCREEN SPACE REFLECTIONS (SSSR) ---
    if (loadGlobalUbo(pc.globalUbo).ppWeatherSSR.z > 0.5) {
        float roughness = heapTex2D(pc.normalInput).SampleLevel(heapSampler(pc.defaultSampler), uv, 0.0).a;
        if (roughness < 0.6 && roughness > 0.01) { 
            float3 N = heapTex2D(pc.normalInput).SampleLevel(heapSampler(pc.defaultSampler), uv, 0.0).rgb;
            if (length(N) > 0.1) {
                N = normalize(N);
                float d = heapTex2D_r(pc.depthInput).SampleLevel(heapSampler(pc.defaultSampler), uv, 0.0).r;
                if (d < 1.0) {
                    float4 worldPosRaw = mul(loadGlobalUbo(pc.globalUbo).invViewProj, float4(uv * 2.0 - 1.0, d, 1.0));
                    float3 worldPos = worldPosRaw.xyz / worldPosRaw.w;
                    float3 V = normalize(worldPos - loadGlobalUbo(pc.globalUbo).camPos);
                    
                    float3 reflColor = float3(0.0); float hit = 0.0;
                    
                    {
                        float3 R = reflect(V, N);
                        float2 noise = frac(sin(dot(uv, float2(12.9898, 78.233)) + loadGlobalUbo(pc.globalUbo).ppMotionBlur.z) * float2(43758.5453, 28001.8384));
                        R = normalize(R + (float3(noise, frac(noise.x + noise.y)) - 0.5) * roughness * 0.4);
                        float stepSize = 0.1; float steps = loadGlobalUbo(pc.globalUbo).ppWeatherSSR.w; float thickness = max(loadGlobalUbo(pc.globalUbo).ppSSSS.z, 0.1);
                        float3 rayPos = worldPos + N * 0.1;
                        for (int i = 0; i < int(steps); i++) {
                            rayPos += R * stepSize;
                            float4 rClip = mul(loadGlobalUbo(pc.globalUbo).projection, mul(loadGlobalUbo(pc.globalUbo).view, float4(rayPos, 1.0)));
                            if (rClip.w <= 0.0) break;
                            float3 rNdc = rClip.xyz / rClip.w; float2 rUv = rNdc.xy * 0.5 + 0.5;
                            if (rUv.x < 0.0 || rUv.x > 1.0 || rUv.y < 0.0 || rUv.y > 1.0) break;
                            float sDepth = heapTex2D_r(pc.depthInput).SampleLevel(heapSampler(pc.defaultSampler), rUv, 0.0).r;
                            if (sDepth < rNdc.z) {
                                float4 hitWorldRaw = mul(loadGlobalUbo(pc.globalUbo).invViewProj, float4(rUv * 2.0 - 1.0, sDepth, 1.0));
                                if (distance(hitWorldRaw.xyz / hitWorldRaw.w, rayPos) < thickness) {
                                    reflColor = heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), rUv, 0.0).rgb; hit = 1.0; break;
                                }
                            }
                            stepSize *= 1.05; 
                        }
                    }
                    
                    // Мгновенно и аппаратно раздаем результаты вычислений соседям
                    if (hit > 0.5) {
                        color = lerp(color, reflColor, lerp(0.1, 1.0, pow(1.0 - max(dot(N, -V), 0.0), 5.0)) * (1.0 - roughness));
                    }
                }
            }
        }
    }
    
    // --- DATAMOSH ---
    if (loadGlobalUbo(pc.globalUbo).ppBleedMosh.z > 0.0) {
        float d = heapTex2D_r(pc.depthInput).SampleLevel(heapSampler(pc.defaultSampler), uv, 0.0).r;
        float4 ndc = float4(uv * 2.0 - 1.0, d, 1.0);
        float4 worldPos = mul(loadGlobalUbo(pc.globalUbo).invViewProj, ndc); 
        worldPos /= worldPos.w; float4 prevNdc = mul(loadGlobalUbo(pc.globalUbo).prevViewProj, worldPos); prevNdc /= prevNdc.w;
        float2 velocity = uv - (prevNdc.xy * 0.5 + 0.5);
        
        if (length(velocity) > 0.0001 && length(velocity) < loadGlobalUbo(pc.globalUbo).ppBleedMosh.w) {
            color = heapTex2D(pc.historyInput).SampleLevel(heapSampler(pc.defaultSampler), uv - velocity, 0.0).rgb; // Настоящий Datamosh
        }
    }

    // --- ECTOPLASM / SLIME TRAIL ---
    if (loadGlobalUbo(pc.globalUbo).ppCondenDust.w > 0.5) {
        float3 ectoColor = loadGlobalUbo(pc.globalUbo).ppEctoRolling.xyz;
        float2 vel = float2(0.0);
        float4 ndc = float4(uv * 2.0 - 1.0, rawD, 1.0);
        float4 worldPos = mul(loadGlobalUbo(pc.globalUbo).invViewProj, ndc); worldPos /= worldPos.w;
        float4 prevNdc = mul(loadGlobalUbo(pc.globalUbo).prevViewProj, worldPos); prevNdc /= prevNdc.w;
        vel = uv - (prevNdc.xy * 0.5 + 0.5); 
        
        if (length(vel) > 0.005) {
            float3 hist = heapTex2D(pc.historyInput).SampleLevel(heapSampler(pc.defaultSampler), clamp(uv - vel * 2.0, 0.0, 1.0), 0.0).rgb;
            color = lerp(color, ectoColor * hist, 0.5);
        }
    }

    // --- TAA / MOTION TRAILS ---
    if (loadGlobalUbo(pc.globalUbo).ppTAA_CAS.x > 0.5 || loadGlobalUbo(pc.globalUbo).ppMotionBlur.w > 0.0) {
        float trails = loadGlobalUbo(pc.globalUbo).ppMotionBlur.w;
        float d = heapTex2D_r(pc.depthInput).SampleLevel(heapSampler(pc.defaultSampler), uv, 0.0).r;
        float2 velocity = float2(0.0);
        float4 ndc = float4(uv * 2.0 - 1.0, d, 1.0);
        float4 worldPos = mul(loadGlobalUbo(pc.globalUbo).invViewProj, ndc); 
        if (abs(worldPos.w) > 0.00001) {
            worldPos /= worldPos.w;
            float4 prevNdc = mul(loadGlobalUbo(pc.globalUbo).prevViewProj, worldPos); 
            if (abs(prevNdc.w) > 0.00001) {
                prevNdc /= prevNdc.w;
                float2 prevUv = prevNdc.xy * 0.5 + 0.5;
                velocity = (uv - prevUv); 
            }
        }
        
        float3 historyColor = heapTex2D(pc.historyInput).SampleLevel(heapSampler(pc.defaultSampler), uv - velocity, 0.0).rgb;
                if (loadGlobalUbo(pc.globalUbo).ppAstigDolly.z > 0.5) {
            if (length(velocity) > loadGlobalUbo(pc.globalUbo).ppAstigDolly.w) {
                color = lerp(color, float3(dot(color, float3(0.333))), 0.8); // Обесцвечиваем и смазываем
            }
        }
        
        // --- TEMPORAL ECHO (PSYCHEDELIC) ---
        if (loadGlobalUbo(pc.globalUbo).ppHeatFrost.w > 0.5 || loadGlobalUbo(pc.globalUbo).ppSaccBurn.z > 0.5 || loadGlobalUbo(pc.globalUbo).ppAfterBleed.y > 0.5 || loadGlobalUbo(pc.globalUbo).ppPsych1.z > 0.0) {
            float3 staticHist = heapTex2D(pc.historyInput).SampleLevel(heapSampler(pc.defaultSampler), uv, 0.0).rgb; // Игнорируем камеру!
            
            // Phosphor Trails (Только зеленое свечение ярких пикселей)
            if (loadGlobalUbo(pc.globalUbo).ppSaccBurn.z > 0.5) {
                float3 phos = max(float3(0.0), staticHist - loadGlobalUbo(pc.globalUbo).ppSaccBurn.w) * float3(0.2, 1.0, 0.2);
                color = max(color, phos);
            }
            // Temporal Color Bleeding
            else if (loadGlobalUbo(pc.globalUbo).ppAfterBleed.y > 0.5) {
                color.r = lerp(color.r, staticHist.r, loadGlobalUbo(pc.globalUbo).ppAfterBleed.z);
                color.b = lerp(color.b, staticHist.b, loadGlobalUbo(pc.globalUbo).ppAfterBleed.z * 0.5);
            }
            // Time Stutter
            else if (loadGlobalUbo(pc.globalUbo).ppPsych1.z > 0.0) {
                float severity = loadGlobalUbo(pc.globalUbo).ppPsych1.z;
                float speed = loadGlobalUbo(pc.globalUbo).ppPsych4.z;
                float t = loadGlobalUbo(pc.globalUbo).ppMotionBlur.z * speed * 5.0;
                if (frac(t) < severity) {
                    color = staticHist;
                }
            }
            // Стандартное эхо
            else {
                // Плавное смешивание вместо жесткого max
                color = lerp(color, max(color, staticHist), loadGlobalUbo(pc.globalUbo).ppDropsEcho.x);
            }
        }
        
        // --- FRAME BUFFER HAUNTING ---
        if (loadGlobalUbo(pc.globalUbo).ppPsych3.y > 0.0) {
            float luma = dot(color, float3(0.333));
            float noise = frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
            if (luma < 0.5 && noise > (1.0 - loadGlobalUbo(pc.globalUbo).ppPsych3.y)) {
                color = lerp(color, heapTex2D(pc.historyInput).SampleLevel(heapSampler(pc.defaultSampler), uv, 0.0).rgb, 0.8); 
            }
        }

        // --- INSIDE SCREEN SMUDGES ---
        if (loadGlobalUbo(pc.globalUbo).ppPsych3.x > 0.0) {
            float s = sin(uv.x*30.0 + sin(uv.y*20.0)) * cos(uv.y*25.0 + sin(uv.x*15.0));
            float smudgeNoise = smoothstep(0.5, 1.0, s);
            float invLuma = 1.0 - dot(color, float3(0.333));
            color += float3(0.5) * smudgeNoise * invLuma * loadGlobalUbo(pc.globalUbo).ppPsych3.x; 
        }

        if (trails > 0.0) {
            float blend = 1.0 - trails;
            color = lerp(historyColor, color, blend);
        } else {
            float3 cMin = color; float3 cMax = color;
            float2 texel = 1.0 / loadGlobalUbo(pc.globalUbo).screenSize.xy;
            float3 s1 = ReadRawColor(clamp(uv + float2( texel.x, 0.0), 0.0, 1.0));
            float3 s2 = ReadRawColor(clamp(uv + float2(-texel.x, 0.0), 0.0, 1.0));
            float3 s3 = ReadRawColor(clamp(uv + float2(0.0,  texel.y), 0.0, 1.0));
            float3 s4 = ReadRawColor(clamp(uv + float2(0.0, -texel.y), 0.0, 1.0));
            cMin = min(cMin, min(s1, min(s2, min(s3, s4))));
            cMax = max(cMax, max(s1, max(s2, max(s3, s4))));
            historyColor = clamp(historyColor, cMin, cMax);
            float blend = clamp(loadGlobalUbo(pc.globalUbo).ppTAA_CAS.y, 0.01, 0.99);
            color = lerp(historyColor, color, blend);
        }
    }

    // --- REACTION-DIFFUSION ---
    if (loadGlobalUbo(pc.globalUbo).ppReactDroste.y > 0.5) {
        float speed = loadGlobalUbo(pc.globalUbo).ppReactDroste.z;
        float t = loadGlobalUbo(pc.globalUbo).ppMotionBlur.z * speed;
        float2 noiseUV = uv * 5.0;
        float rd = sin(noiseUV.x * 10.0 + t) * cos(noiseUV.y * 10.0 - t) +
                   sin(noiseUV.y * 15.0 - t * 1.5) * cos(noiseUV.x * 15.0 + t * 1.5);
        rd = smoothstep(0.0, 0.5, rd);
        color = lerp(color, float3(0.1, 0.9, 0.5), rd * 0.5);
    }

    // --- SMEAR / RIBBON TRAILS ---
    if (loadGlobalUbo(pc.globalUbo).ppTrails.x > 0.0) {
        float3 hist = heapTex2D(pc.historyInput).SampleLevel(heapSampler(pc.defaultSampler), uv, 0.0).rgb;
        float luma = dot(color, float3(0.333));
        if (luma > loadGlobalUbo(pc.globalUbo).ppTrails.z) color = lerp(color, hist, loadGlobalUbo(pc.globalUbo).ppTrails.y);
    }
    
    // Записываем чистый TAA результат в историю следующего кадра (БЕЗ Bloom, DoF, ToneMap!)
    heapRWTex2D(pc.historyOutput)[pixel] = float4(color, 1.0);

    // Motion Blur (Camera Velocity)
    if (loadGlobalUbo(pc.globalUbo).ppMotionBlur.x > 0.5) {
        float d = heapTex2D_r(pc.depthInput).SampleLevel(heapSampler(pc.defaultSampler), uv, 0.0).r;
        float4 ndc = float4(uv * 2.0 - 1.0, d, 1.0);
        float4 worldPos = mul(loadGlobalUbo(pc.globalUbo).invViewProj, ndc); 
        if (abs(worldPos.w) > 0.00001) {
            worldPos /= worldPos.w;
            float4 prevNdc = mul(loadGlobalUbo(pc.globalUbo).prevViewProj, worldPos); 
            if (abs(prevNdc.w) > 0.00001) {
                prevNdc /= prevNdc.w;
                float2 prevUv = prevNdc.xy * 0.5 + 0.5;
                
                float2 velocity = (uv - prevUv); 
                velocity *= loadGlobalUbo(pc.globalUbo).ppMotionBlur.y; // Strength applies before clamp
                float maxVel = 0.1; // Ограничение размытия 10% экрана
                if (length(velocity) > maxVel) velocity = normalize(velocity) * maxVel;
                
                if (length(velocity) > 0.0005) {
                    float3 mbColor = color; float totalWeight = 1.0;
                    int samples = 16;
                    for (int i = 1; i < samples; ++i) {
                        float t = (float(i) / float(samples - 1)) - 0.5;
                        float2 offsetUV = clamp(uv + velocity * t, 0.0, 1.0);
                        float sampleD = heapTex2D_r(pc.depthInput).SampleLevel(heapSampler(pc.defaultSampler), offsetUV, 0.0).r;
                        float weight = (sampleD < d - 0.01) ? 0.0 : 1.0; 
                        mbColor += ReadColor(offsetUV) * weight;
                        totalWeight += weight;
                    }
                    color = mbColor / max(totalWeight, 0.001);
                }
            }
        }
    }

    // Depth of Field
    if (loadGlobalUbo(pc.globalUbo).ppDoFParams.x > 0.5) {
        float d = heapTex2D_r(pc.depthInput).SampleLevel(heapSampler(pc.defaultSampler), uv, 0.0).r;
        float zN = loadGlobalUbo(pc.globalUbo).zNear; float zF = loadGlobalUbo(pc.globalUbo).zFar;
        float linDepth = (zN * zF) / (zF + d * (zN - zF));
        
        float focusDist = loadGlobalUbo(pc.globalUbo).ppDoFParams.y;
        if (loadGlobalUbo(pc.globalUbo).ppLensAdvanced.z > 0.5) { // Auto-Focus
            float centerD = heapTex2D_r(pc.depthInput).SampleLevel(heapSampler(pc.defaultSampler), float2(0.5, 0.5), 0.0).r;
            focusDist = (zN * zF) / (zF + centerD * (zN - zF));
        }
        
        float coc = clamp(abs(linDepth - focusDist) / max(loadGlobalUbo(pc.globalUbo).ppDoFParams.z, 0.001), 0.0, 1.0);
        if (coc > 0.05) {
            float3 dofColor = float3(0.0); float totW = 0.0;
            float rad = loadGlobalUbo(pc.globalUbo).ppDoFParams.w * coc * 0.01;
            
            // Оптимизация (Имитация полу-разрешения): Разреженный Bokeh-семплинг (Golden Ratio)
            int samples = int(lerp(4.0, 24.0, coc));
            const float GOLDEN_ANGLE = 2.39996323;
            
            // Custom Bokeh Figure
            int shape = int(loadGlobalUbo(pc.globalUbo).ppRetroParams2.z + 0.5);
            float rotAngle = loadGlobalUbo(pc.globalUbo).ppRetroParams2.w * 3.14159 / 180.0;
            float blades = 6.0; 
            if (shape == 1) blades = 6.0;
            else if (shape == 2) blades = 8.0;
            else if (shape == 3) blades = 3.0;
            
            for (int i = 0; i < samples; ++i) {
                float r = sqrt(float(i) + 0.5) / sqrt(float(samples));
                float theta = float(i) * GOLDEN_ANGLE;
                float2 off = float2(cos(theta), sin(theta)) * (rad * r);
                
                // Применяем фигуру к боке
                if (shape > 0) {
                    float angle = atan2(off.y, off.x) + rotAngle;
                    float rShape = cos(3.14159/blades) / cos(angle - (2.0*3.14159/blades)*floor((angle + 3.14159/blades)/(2.0*3.14159/blades)));
                    off *= rShape;
                }
                
                float2 sampUV = uv + off;
                float sampD = heapTex2D_r(pc.depthInput).SampleLevel(heapSampler(pc.defaultSampler), sampUV, 0.0).r;
                float sampLin = (zN * zF) / (zF + sampD * (zN - zF));
                float sampCoc = clamp(abs(sampLin - loadGlobalUbo(pc.globalUbo).ppDoFParams.y) / loadGlobalUbo(pc.globalUbo).ppDoFParams.z, 0.0, 1.0);
                
                float weight = (sampLin < linDepth) ? 1.0 : clamp(sampCoc / max(coc, 0.001), 0.0, 1.0);
                dofColor += heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), sampUV, 0.0).rgb * weight; 
                totW += weight;
            }
            color = dofColor / max(totW, 0.001);
        }
    }

    // Bloom & Lens Flare
    float3 bloom = float3(0.0); float3 flare = float3(0.0);
    float2 texel = 1.0 / loadGlobalUbo(pc.globalUbo).screenSize.xy; float thresh = loadGlobalUbo(pc.globalUbo).ppBloomParams.y;
    if (loadGlobalUbo(pc.globalUbo).ppBloomParams.x > 0.5) {
        float totalWeight = 0.0;
        for(int x = -2; x <= 2; x+=2) {
            for(int y = -2; y <= 2; y+=2) {
                float3 s = heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), uv + float2(x, y) * texel * 2.0, 0.0).rgb;
                bloom += isolateBright(s, thresh);
                totalWeight += 1.0;
            }
        }
        float3 totalBloom = (bloom / max(totalWeight, 1.0)) * loadGlobalUbo(pc.globalUbo).ppBloomParams.z;
        if (loadGlobalUbo(pc.globalUbo).ppDistortionDirt.z > 0.5) {
            float3 dirt = heapTex2D(pc.dirtInput).SampleLevel(heapSampler(pc.defaultSampler), uv, 0.0).rgb;
            float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
            float3 dirtIllumination = totalBloom + float3(luma * 0.2); // Also illuminated by general bright areas
            color += dirt * dirtIllumination * loadGlobalUbo(pc.globalUbo).ppDistortionDirt.w;
        }
        color += totalBloom;
    }
    if (loadGlobalUbo(pc.globalUbo).ppLensFlare.x > 0.5) {
        // Anamorphic Flares (Горизонтальные синие блики)
        if (loadGlobalUbo(pc.globalUbo).ppBlurCenter.z > 0.5) {
            float3 streak = float3(0.0);
            for (int i = -10; i <= 10; i++) {
                streak += isolateBright(heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), clamp(uv + float2(float(i) * 3.0 * texel.x, 0.0), 0.0, 1.0), 0.0).rgb, thresh) * (1.0 - abs(float(i))/10.0);
            }
            flare += streak * float3(0.1, 0.3, 1.0) * 1.5;
        }
        
        float2 ghostVec = (float2(0.5) - uv) * loadGlobalUbo(pc.globalUbo).ppLensFlare.z; int ghosts = int(loadGlobalUbo(pc.globalUbo).ppLensFlare.w);
        
        // Хроматическая аберрация на бликах
        float2 dir = normalize(ghostVec) * loadGlobalUbo(pc.globalUbo).ppLensAdvanced.y; 
        for(int i = 0; i < ghosts; i++) {
            float2 gUV = frac(uv + ghostVec * float(i));
            float weight = pow(1.0 - float(i)/float(ghosts), 2.0);
            float3 s;
            s.r = heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), gUV - dir, 0.0).r;
            s.g = heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), gUV, 0.0).g;
            s.b = heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), gUV + dir, 0.0).b;
            flare += isolateBright(s, thresh) * weight;
        }
        
        // Halo (Ореол)
        if (loadGlobalUbo(pc.globalUbo).ppLensAdvanced.x > 0.001) {
            float2 haloVec = normalize(ghostVec) * loadGlobalUbo(pc.globalUbo).ppLensAdvanced.x;
            float2 haloUV = frac(uv + haloVec);
            float haloWeight = length(float2(0.5) - haloUV) / length(float2(0.5));
            haloWeight = pow(1.0 - clamp(haloWeight, 0.0, 1.0), 5.0);
            flare += isolateBright(heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), haloUV, 0.0).rgb, thresh) * haloWeight;
        }
        
        // --- STAR FILTER / CROSS SCREEN ---
        if (loadGlobalUbo(pc.globalUbo).ppLens4.y > 0.5) {
            float starThresh = loadGlobalUbo(pc.globalUbo).ppLens4.z;
            float starLen = loadGlobalUbo(pc.globalUbo).ppLens4.w;
            
            // Фикс: Читаем строго по кресту X и Y с широким шагом, а не по диагонали
            for(int i = -15; i <= 15; i++) {
                if(i == 0) continue;
                float w = 1.0 - abs(float(i)) / 15.0;
                // Горизонтальный луч
                flare += isolateBright(heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), clamp(uv + float2(float(i) * starLen * texel.x, 0.0), 0.0, 1.0), 0.0).rgb, starThresh) * w * 2.0;
                // Вертикальный луч
                flare += isolateBright(heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), clamp(uv + float2(0.0, float(i) * starLen * texel.y), 0.0, 1.0), 0.0).rgb, starThresh) * w * 2.0;
            }
        }
        
        // --- ASTIGMATISM (Лучи света от астигматизма) ---
        if (loadGlobalUbo(pc.globalUbo).ppTransAnime.w > 0.5) {
            float astLen = loadGlobalUbo(pc.globalUbo).ppAstigDolly.x;
            float angle = loadGlobalUbo(pc.globalUbo).ppAstigDolly.y * 3.14159 / 180.0;
            float2 dir = float2(cos(angle), sin(angle));
            for(int i = -10; i <= 10; i++) {
                if (i==0) continue;
                flare += isolateBright(heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), clamp(uv + dir * float(i) * astLen * texel, 0.0, 1.0), 0.0).rgb, thresh) * (1.0 - abs(float(i))/10.0);
            }
        }
        
        color += flare * loadGlobalUbo(pc.globalUbo).ppLensFlare.y;
    }
    // --- BURNING FILM ---
    if (loadGlobalUbo(pc.globalUbo).ppSaccBurn.x > 0.5) {
        float time = loadGlobalUbo(pc.globalUbo).ppSaccBurn.y; // Читаем правильный параметр времени горения!
        float dist = distance(uv, float2(0.5));
        float noise = frac(sin(dot(uv*20.0, float2(12.9, 78.2)))*43758.5);
        float burnEdge = time - dist + noise*0.1;
        
        if (burnEdge > 0.05) color = float3(0.0); // Сгорело
        else if (burnEdge > 0.0) color = lerp(color, float3(1.0, 0.2, 0.0) * 5.0, burnEdge / 0.05);
    }
    // --- CAS (Contrast Adaptive Sharpening) ДО Tonemapping ---
    if (loadGlobalUbo(pc.globalUbo).ppTAA_CAS.z > 0.5) {
        float2 texel = 1.0 / loadGlobalUbo(pc.globalUbo).screenSize.xy;
        float3 a = ReadRawColor(clamp(uv + float2(0, -texel.y), 0.0, 1.0));
        float3 b = ReadRawColor(clamp(uv + float2(-texel.x, 0), 0.0, 1.0));
        float3 c = ReadRawColor(clamp(uv + float2(texel.x, 0), 0.0, 1.0));
        float3 d = ReadRawColor(clamp(uv + float2(0, texel.y), 0.0, 1.0));
        
        float w = -1.0 / max(8.0, loadGlobalUbo(pc.globalUbo).ppTAA_CAS.w * 10.0 + 8.0);
        float3 sharpened = (color + (a + b + c + d) * w) / (1.0 + 4.0 * w);
        
        // Защита от отрицательных значений HDR
        color = lerp(color, max(sharpened, float3(0.0)), loadGlobalUbo(pc.globalUbo).ppTAA_CAS.w);
    }

    // Color Grade & Tone Mapping
    
    // ЗАЩИТА: Если C++ не передал настройки (например, буфер состоит из нулей), gamma будет 0.
    // В этом случае мы используем дефолтные значения, чтобы экран не становился черным или серым.

    
    float currentAdapted = max((*DescriptorHandle<RWStructuredBuffer<float>>(pc.exposureBuffer))[0], 0.0001);
    float exposure = 1.0;
    if (loadGlobalUbo(pc.globalUbo).ppExposureParams.x > 0.5) {
        exposure = (0.18 / currentAdapted) * loadGlobalUbo(pc.globalUbo).ppExposureParams.y; // UE5 style adaptation + comp.
    } else {
        exposure = max(loadGlobalUbo(pc.globalUbo).ppExposureParams.y, 0.01);
    }
    color *= exposure;
    
    float temp = max(loadGlobalUbo(pc.globalUbo).ppColorBalance.x, 1000.0); 
    color *= float3(temp > 6500.0 ? 6500.0 / temp : 1.0, 1.0, temp < 6500.0 ? temp / 6500.0 : 1.0);
    
    // --- 3-Way Color Grading ---
    float luma_cg = dot(color, float3(0.2126, 0.7152, 0.0722));
    float shadowFactor = clamp(1.0 - (luma_cg * 2.5), 0.0, 1.0);
    float highlightFactor = clamp((luma_cg - 0.5) * 2.0, 0.0, 1.0);
    float midtoneFactor = 1.0 - shadowFactor - highlightFactor;
    
    float3 shadowColor = ApplyCDL(color, loadGlobalUbo(pc.globalUbo).cgShadowsLift, loadGlobalUbo(pc.globalUbo).cgShadowsGamma, loadGlobalUbo(pc.globalUbo).cgShadowsGain, loadGlobalUbo(pc.globalUbo).cgShadowsOffset);
    float3 midtoneColor = ApplyCDL(color, loadGlobalUbo(pc.globalUbo).cgMidtonesLift, loadGlobalUbo(pc.globalUbo).cgMidtonesGamma, loadGlobalUbo(pc.globalUbo).cgMidtonesGain, loadGlobalUbo(pc.globalUbo).cgMidtonesOffset);
    float3 highlightColor = ApplyCDL(color, loadGlobalUbo(pc.globalUbo).cgHighlightsLift, loadGlobalUbo(pc.globalUbo).cgHighlightsGamma, loadGlobalUbo(pc.globalUbo).cgHighlightsGain, loadGlobalUbo(pc.globalUbo).cgHighlightsOffset);
                
    color = shadowColor * shadowFactor + midtoneColor * midtoneFactor + highlightColor * highlightFactor;
    color = ApplyCDL(color, loadGlobalUbo(pc.globalUbo).cgGlobalLift, loadGlobalUbo(pc.globalUbo).cgGlobalGamma, loadGlobalUbo(pc.globalUbo).cgGlobalGain, loadGlobalUbo(pc.globalUbo).cgGlobalOffset);

    float3 cgColor = loadGlobalUbo(pc.globalUbo).cgShadows.rgb * shadowFactor + loadGlobalUbo(pc.globalUbo).cgMidtones.rgb * midtoneFactor + loadGlobalUbo(pc.globalUbo).cgHighlights.rgb * highlightFactor;
    if (length(loadGlobalUbo(pc.globalUbo).cgShadows.rgb) + length(loadGlobalUbo(pc.globalUbo).cgMidtones.rgb) + length(loadGlobalUbo(pc.globalUbo).cgHighlights.rgb) > 0.01) {
        color *= cgColor;
    }
    float3 mixed;
    mixed.r = dot(color, loadGlobalUbo(pc.globalUbo).cgRgbMixerRed.rgb);
    mixed.g = dot(color, loadGlobalUbo(pc.globalUbo).cgRgbMixerGreen.rgb);
    mixed.b = dot(color, loadGlobalUbo(pc.globalUbo).cgRgbMixerBlue.rgb);
    color = mixed;

    // --- Tone Mapping ---
    int tonemapper = int(loadGlobalUbo(pc.globalUbo).ppLensAdvanced.w + 0.5);
    if (tonemapper == 1) {
        color = color / (color + float3(1.0)); // Reinhard
    } else if (tonemapper == 2) {
        // ACES Film
        float a = 2.51f; float b = 0.03f; float c = 2.43f; float d = 0.59f; float e = 0.14f;
        color = clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
    }
    
    float sat = max(loadGlobalUbo(pc.globalUbo).ppColorBalance.z, 0.0);
    color = lerp(float3(dot(color, float3(0.2126, 0.7152, 0.0722))), color, sat);
    
    float contrast = max(loadGlobalUbo(pc.globalUbo).ppColorBalance.y, 0.0);
    color = clamp((color - 0.5) * contrast + 0.5, 0.0, 1.0);
    
    if (loadGlobalUbo(pc.globalUbo).ppVignetteGrain.x > 0.0) color *= smoothstep(0.8, 0.8 - loadGlobalUbo(pc.globalUbo).ppVignetteGrain.x, distance(uv, float2(0.5)) * (0.5 + loadGlobalUbo(pc.globalUbo).ppVignetteGrain.x));
    if (loadGlobalUbo(pc.globalUbo).ppVignetteGrain.y > 0.0) color += (frac(sin(dot(uv, float2(12.9898, 78.233)) + loadGlobalUbo(pc.globalUbo).ppMotionBlur.z) * 43758.5453) - 0.5) * loadGlobalUbo(pc.globalUbo).ppVignetteGrain.y;
    
    float gamma = max(loadGlobalUbo(pc.globalUbo).ppColorBalance.w, 0.1);
    color = pow(clamp(color, 0.0, 1.0), float3(1.0 / gamma));

    // --- OUTLINE / EDGE DETECTION ---
    if (loadGlobalUbo(pc.globalUbo).ppOutlineParams.x > 0.5) {
        float2 outUV = uv;
        int outlineMode = int(loadGlobalUbo(pc.globalUbo).ppOutlineColor.w + 0.5);
        
        // Outline Wobble/Jitter
        if (loadGlobalUbo(pc.globalUbo).ppOutlineJitter.x > 0.5) {
            float t = loadGlobalUbo(pc.globalUbo).ppMotionBlur.z * loadGlobalUbo(pc.globalUbo).ppOutlineJitter.y;
            float t_discrete = floor(t); // Stop-motion effect (FPS)
            float2 jitter = float2(
                frac(sin(dot(uv + t_discrete, float2(12.9898, 78.233))) * 43758.5453),
                frac(cos(dot(uv - t_discrete, float2(39.346, 11.135))) * 43758.5453)
            ) - 0.5;
            outUV += jitter * (loadGlobalUbo(pc.globalUbo).ppOutlineJitter.z / loadGlobalUbo(pc.globalUbo).screenSize.xy);
        }

        float thickness = loadGlobalUbo(pc.globalUbo).ppOutlineParams.y;
        float2 texel = 1.0 / loadGlobalUbo(pc.globalUbo).screenSize.xy;
        float2 offsets[4] = float2[]( float2(thickness, 0.0)*texel, float2(-thickness, 0.0)*texel, float2(0.0, thickness)*texel, float2(0.0, -thickness)*texel );

        float d0 = heapTex2D_r(pc.depthInput).SampleLevel(heapSampler(pc.defaultSampler), outUV, 0.0).r;
        float3 n0 = heapTex2D(pc.normalInput).SampleLevel(heapSampler(pc.defaultSampler), outUV, 0.0).rgb;
        if (length(n0) > 0.1) n0 = normalize(n0);

        float zN = loadGlobalUbo(pc.globalUbo).zNear; float zF = loadGlobalUbo(pc.globalUbo).zFar;
        float linD0 = (zN * zF) / (zF + d0 * (zN - zF));
        float edgeDepth = 0.0, edgeNormal = 0.0;

        for (int i = 0; i < 4; i++) {
            float2 sampUV = clamp(outUV + offsets[i], 0.0, 1.0);
            float d1 = heapTex2D_r(pc.depthInput).SampleLevel(heapSampler(pc.defaultSampler), sampUV, 0.0).r;
            
            if (outlineMode == 0 || outlineMode == 1) {
                if (d0 >= 1.0 && d1 >= 1.0) continue; 
                if ((d0 >= 1.0 && d1 < 1.0) || (d0 < 1.0 && d1 >= 1.0)) { edgeDepth = 1.0; continue; }
                float linD1 = (zN * zF) / (zF + d1 * (zN - zF));
                float depthDiff = abs(linD0 - linD1) / max(min(linD0, linD1), 0.0001);
                if (depthDiff > loadGlobalUbo(pc.globalUbo).ppOutlineParams.z) edgeDepth = 1.0;
            }
            
            if (outlineMode == 0 || outlineMode == 2) {
                if (d0 >= 1.0 || d1 >= 1.0) continue; // Don't draw internal lines on the sky
                float3 n1 = heapTex2D(pc.normalInput).SampleLevel(heapSampler(pc.defaultSampler), sampUV, 0.0).rgb;
                if (length(n1) > 0.1 && 1.0 - dot(n0, normalize(n1)) > loadGlobalUbo(pc.globalUbo).ppOutlineParams.w) edgeNormal = 1.0;
            }
        }
        float edge = max(edgeDepth, edgeNormal);
        color = lerp(color, loadGlobalUbo(pc.globalUbo).ppOutlineColor.rgb, edge);
    }

    // Adaptive Posterization
    if (loadGlobalUbo(pc.globalUbo).ppStylizedParams.x > 0.0) {
        float levels = loadGlobalUbo(pc.globalUbo).ppStylizedParams.x;
        color = floor(color * levels + 0.5) / levels;
    }

    // Dithering to prevent color banding
    if (loadGlobalUbo(pc.globalUbo).ppDitherAniso.x > 0.5) {
        int dMode = int(loadGlobalUbo(pc.globalUbo).ppDitherData.x + 0.5);
        float dScale = max(1.0, loadGlobalUbo(pc.globalUbo).ppDitherData.y);
        float2 pixelScale = floor(float2(pixel) / dScale);
        if (loadGlobalUbo(pc.globalUbo).ppHatchAnalog.x > 0.5) {
            float4 ndc = float4(uv * 2.0 - 1.0, rawD, 1.0);
            float4 worldPos = mul(loadGlobalUbo(pc.globalUbo).invViewProj, ndc); worldPos /= worldPos.w;
            pixelScale = floor(worldPos.xz * 100.0 / dScale);
        }
        const int bayer4[16] = int[](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
        int bx = int(pixelScale.x) % 4;
        int by = int(pixelScale.y) % 4;
        float ditherPattern = (float(bayer4[by * 4 + bx]) / 15.0) - 0.5;

        if (dMode == 0) {
            float ditherStrength = max(loadGlobalUbo(pc.globalUbo).ppDitherAniso.y, 1.0);
            if (ditherStrength > 1.1) {
                float levels = max(256.0 / (ditherStrength * 2.0), 2.0); 
                color = color + (ditherPattern / levels) * 2.0;
                color = floor(color * levels + 0.5) / levels;
            } else {
                float randomDither = frac(sin(dot(uv, float2(12.9898, 78.233)) + loadGlobalUbo(pc.globalUbo).ppMotionBlur.z) * 43758.5453);
                color += (randomDither - 0.5) / 255.0;
            }
        } else if (dMode == 1) {
            float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
            luma += ditherPattern * 0.5; 
            if (luma < 0.33) color = loadGlobalUbo(pc.globalUbo).ditherShadow.rgb;
            else if (luma < 0.66) color = loadGlobalUbo(pc.globalUbo).ditherMid.rgb;
            else color = loadGlobalUbo(pc.globalUbo).ditherHighlight.rgb;
        } else if (dMode == 2) {
            // Custom Bayer/Pattern Texture
            float customPat = heapTex2D(pc.ditherInput).SampleLevel(heapSampler(pc.defaultSampler), frac(uv * dScale), 0.0).r;
            color += (customPat - 0.5) * (loadGlobalUbo(pc.globalUbo).ppDitherAniso.y / 10.0);
        }
    }

    // --- EYE BLINKING ---
    if (loadGlobalUbo(pc.globalUbo).ppPsych1.x > 0.0) {
        float t = loadGlobalUbo(pc.globalUbo).ppMotionBlur.z * loadGlobalUbo(pc.globalUbo).ppPsych1.x; // Freq
        float blink = smoothstep(0.8, 1.0, abs(sin(t * 3.14159))); 
        color *= (1.0 - blink); // Экран полностью гаснет
    }

    // --- Color Compression & Palette ---
    if (loadGlobalUbo(pc.globalUbo).ppColorComp.x > 0.5) {
        float levels = loadGlobalUbo(pc.globalUbo).ppColorComp.y;
        color = floor(color * levels) / levels;
    }
    if (loadGlobalUbo(pc.globalUbo).ppColorComp.w > 0.5) { // LUT (256x16 standard format)
        float3 c = clamp(color, 0.0, 1.0);
        float b = c.b * 15.0;
        float b_floor = floor(b);
        float b_fract = frac(b);
        float2 uv1 = float2((b_floor + c.r) / 16.0, c.g);
        float2 uv2 = float2((b_floor + 1.0 + c.r) / 16.0, c.g);
        color = lerp(heapTex2D(pc.paletteInput).SampleLevel(heapSampler(pc.defaultSampler), uv1, 0.0).rgb, heapTex2D(pc.paletteInput).SampleLevel(heapSampler(pc.defaultSampler), uv2, 0.0).rgb, b_fract);
    }
    
    // --- ASCII ART ---
    if (loadGlobalUbo(pc.globalUbo).ppAsciiSort.x > 0.0) {
        float scale = max(2.0, loadGlobalUbo(pc.globalUbo).ppAsciiSort.y);
        int mode = int(loadGlobalUbo(pc.globalUbo).ppPixelSort.w + 0.5);
        float2 cell = floor(uv * loadGlobalUbo(pc.globalUbo).screenSize.xy / scale) * scale;
        float2 localUV = frac(uv * loadGlobalUbo(pc.globalUbo).screenSize.xy / scale);
        float3 cellColor = ReadColor(cell / loadGlobalUbo(pc.globalUbo).screenSize.xy);
        float luma = dot(cellColor, float3(0.2126, 0.7152, 0.0722));
        float shape = 0.0;
        
        if (mode == 0) {
            int charIndex = int(luma * 8.0); 
            if (charIndex == 0) shape = 0.0; 
            else if (charIndex == 1) shape = step(0.4, localUV.x) * step(localUV.x, 0.6) * step(0.4, localUV.y); 
            else if (charIndex == 2) shape = step(0.4, localUV.y) * step(localUV.y, 0.6); 
            else if (charIndex == 3) shape = step(0.2, localUV.y) * step(localUV.y, 0.8) * step(0.4, localUV.x) * step(localUV.x, 0.6); 
            else if (charIndex == 4) shape = step(length(localUV - 0.5), 0.3); 
            else if (charIndex == 5) shape = step(length(localUV - 0.5), 0.4) - step(length(localUV - 0.5), 0.2); 
            else if (charIndex == 6) shape = step(abs(localUV.x + localUV.y - 1.0), 0.1) + step(abs(localUV.x - localUV.y), 0.1); 
            else shape = 1.0; 
        } else if (mode == 1) {
            shape = luma > (localUV.x * localUV.y) ? 1.0 : 0.0;
        } else if (mode == 2) {
            float bin = frac(sin(dot(cell, float2(12.9898, 78.233))) * 43758.5453) > 0.5 ? 1.0 : 0.0;
            shape = (luma > 0.2) ? (bin > 0.5 ? step(0.3, localUV.x)*step(localUV.x, 0.7) : step(0.3, localUV.y)*step(localUV.y, 0.7)) : 0.0;
        }
        color = cellColor * shape;
    }
    
    // --- WORLD-SPACE ASCII ---
    if (loadGlobalUbo(pc.globalUbo).ppPhosASCII.x > 0.5) {
        float scale = loadGlobalUbo(pc.globalUbo).ppPhosASCII.y;
        float4 worldRaw = mul(loadGlobalUbo(pc.globalUbo).invViewProj, float4(uv * 2.0 - 1.0, rawD, 1.0));
        float3 worldPos = worldRaw.xyz / worldRaw.w;
        float3 N = heapTex2D(pc.normalInput).SampleLevel(heapSampler(pc.defaultSampler), uv, 0.0).rgb;
        float pattern = frac(sin(dot(floor(worldPos * scale), float3(12.9, 78.2, 45.1)))*43758.5);
        float luma = dot(color, float3(0.333));
        color *= step(0.5, pattern + luma); // Накладываем матричный дождь на мир
    }
    
    // --- SELECTIVE COLOR (COLOR SPLASH) ---
    if (loadGlobalUbo(pc.globalUbo).ppSpeedLines.z > 0.5) {
        float3 hsv = rgb2hsv(color);
        float targetHue = loadGlobalUbo(pc.globalUbo).ppSpeedLines.w;
        float hueRange = loadGlobalUbo(pc.globalUbo).ppColorSplash.x;
        float hueDist = min(abs(hsv.x - targetHue), 1.0 - abs(hsv.x - targetHue));
        
        if (hueDist > hueRange) {
            float luma = dot(color, float3(0.299, 0.587, 0.114));
            color = float3(luma); // Обесцвечиваем всё, кроме нужного цвета
        }
    }

    // CRT Scanlines & VHS Noise
    if (loadGlobalUbo(pc.globalUbo).ppRetroParams.x > 0.5) {
        if (loadGlobalUbo(pc.globalUbo).ppRetroParams.y > 0.0) {
            float scanlineY = (float(pixel.y) + 0.5) / pSize;
            float scanline = sin(frac(scanlineY) * 3.14159265);
            color *= lerp(1.0, scanline, 0.5 * loadGlobalUbo(pc.globalUbo).ppRetroParams.y);
            
            float2 crtUV = uv * 2.0 - 1.0;
            color *= lerp(1.0, smoothstep(2.0, 0.8, dot(crtUV, crtUV)), 0.25 * loadGlobalUbo(pc.globalUbo).ppRetroParams.y);
        }
        if (loadGlobalUbo(pc.globalUbo).ppRetroParams.w > 0.0) {
            float vhsNoise = frac(sin(dot(uv + frac(loadGlobalUbo(pc.globalUbo).ppMotionBlur.z * 10.0), float2(12.9898, 78.233))) * 43758.5453);
            float tapeBand = step(0.98, frac(uv.y * 3.0 - loadGlobalUbo(pc.globalUbo).ppMotionBlur.z * 1.5));
            color += (vhsNoise - 0.5) * 0.15 * loadGlobalUbo(pc.globalUbo).ppRetroParams.w;
            color += tapeBand * (vhsNoise - 0.5) * 0.3 * loadGlobalUbo(pc.globalUbo).ppRetroParams.w; 
        }
    }

    // --- FILM DAMAGE ---
    if (loadGlobalUbo(pc.globalUbo).ppColorFX.w > 0.5) {
        float intensity = loadGlobalUbo(pc.globalUbo).ppFilmDamage.x;
        float scratches = loadGlobalUbo(pc.globalUbo).ppFilmDamage.y;
        color *= lerp(float3(1.0), float3(1.0, 0.9, 0.7), intensity); // Yellowing
        float t = loadGlobalUbo(pc.globalUbo).ppMotionBlur.z;
        float scratch = frac(sin(dot(float2(uv.x, floor(t * 10.0)), float2(12.9898, 78.233))) * 43758.5453);
        if (scratch > 0.99) {
            float lineIntensity = frac(sin(uv.y * 10.0 + t) * 100.0);
            color *= (1.0 - lineIntensity * scratches);
        }
        float dust = frac(sin(dot(uv + floor(t * 15.0), float2(12.9898, 78.233))) * 43758.5453);
        if (dust > 0.998) color *= 0.5;
    }
    
    // --- LIGHT LEAKS ---
    if (loadGlobalUbo(pc.globalUbo).ppGlitch3.x > 0.5) {
        float t = loadGlobalUbo(pc.globalUbo).ppMotionBlur.z * 0.5;
        float leak = sin(uv.x * 5.0 + t) * sin(uv.y * 3.0 - t * 1.2);
        leak = smoothstep(0.5, 1.0, leak) * smoothstep(0.2, 0.8, abs(uv.x - 0.5) * 2.0);
        color += float3(1.0, 0.3, 0.0) * leak * loadGlobalUbo(pc.globalUbo).ppGlitch3.y;
    }
    
    // --- EMBOSS ---
    if (loadGlobalUbo(pc.globalUbo).ppEmboss.x > 0.5) {
        float strength = loadGlobalUbo(pc.globalUbo).ppEmboss.y;
        float angle = loadGlobalUbo(pc.globalUbo).ppEmboss.z * 3.14159 / 180.0;
        int style = int(loadGlobalUbo(pc.globalUbo).ppEmboss.w + 0.5);
        float2 dir = float2(cos(angle), sin(angle)) / loadGlobalUbo(pc.globalUbo).screenSize.xy;
        float3 c1_e = heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), clamp(uv - dir, 0.0, 1.0), 0.0).rgb;
        float3 c2_e = heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), clamp(uv + dir, 0.0, 1.0), 0.0).rgb;
        float3 diff = (c2_e - c1_e) * strength;
        if (style == 0) {
            float lumaDiff = dot(diff, float3(0.333));
            color = float3(0.5) + float3(lumaDiff);
        } else {
            color = color + diff;
        }
    }

    // --- PENCIL SKETCH ---
    if (loadGlobalUbo(pc.globalUbo).ppSketch.x > 0.5) {
        float strokeStrength = loadGlobalUbo(pc.globalUbo).ppSketch.y;
        float strokeLen = max(loadGlobalUbo(pc.globalUbo).ppSketch.z, 0.1);
        float threshold = loadGlobalUbo(pc.globalUbo).ppSketch.w;
        float shadowLevel = loadGlobalUbo(pc.globalUbo).ppSketch2.x;
        float wS = loadGlobalUbo(pc.globalUbo).ppSketch2.y; float wM = loadGlobalUbo(pc.globalUbo).ppSketch2.z; float wH = loadGlobalUbo(pc.globalUbo).ppSketch2.w;
        float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
        float hatchScale = loadGlobalUbo(pc.globalUbo).screenSize.y / strokeLen;
        float h1 = hatch(uv, 0.785, hatchScale, threshold) * wM;  
        float h2 = hatch(uv, -0.785, hatchScale, threshold) * wS; 
        float h3 = hatch(uv, 0.0, hatchScale, threshold) * wS;    
        float h4 = hatch(uv, 1.57, hatchScale, threshold) * wH;   
        float sketch = 1.0;
        if (luma < shadowLevel * 0.33) sketch = h1 * h2 * h3;
        else if (luma < shadowLevel * 0.66) sketch = h1 * h2;
        else if (luma < shadowLevel) sketch = h1;
        else sketch = lerp(1.0, h4, 1.0 - clamp((luma - shadowLevel)/(1.0 - shadowLevel), 0.0, 1.0));
        sketch = lerp(1.0, clamp(sketch, 0.0, 1.0), strokeStrength);
        color = color * float3(sketch);
    }

    // --- EDGE DETECT ---
    if (loadGlobalUbo(pc.globalUbo).ppEdgeDetect.x > 0.5) {
        float width = loadGlobalUbo(pc.globalUbo).ppEdgeDetect.y;
        float brightness = loadGlobalUbo(pc.globalUbo).ppEdgeDetect.z;
        float gamma = loadGlobalUbo(pc.globalUbo).ppEdgeDetect.w;
        float blur = loadGlobalUbo(pc.globalUbo).ppEdgeDetect2.x;
        int mode = int(loadGlobalUbo(pc.globalUbo).ppEdgeDetect2.y + 0.5);
        float3 edgeColor = getEdge(uv, width);
        if (blur > 0.0) {
            float2 bTexel = blur / loadGlobalUbo(pc.globalUbo).screenSize.xy;
            edgeColor += getEdge(uv + float2(bTexel.x, 0.0), width);
            edgeColor += getEdge(uv + float2(-bTexel.x, 0.0), width);
            edgeColor += getEdge(uv + float2(0.0, bTexel.y), width);
            edgeColor += getEdge(uv + float2(0.0, -bTexel.y), width);
            edgeColor /= 5.0;
        }
        if (mode == 1) edgeColor = loadGlobalUbo(pc.globalUbo).ppEdgeColor.rgb * dot(edgeColor, float3(0.2126, 0.7152, 0.0722));
        edgeColor = pow(max(edgeColor * brightness, float3(0.0001)), float3(gamma));
        color = edgeColor;
    }

    // --- HALFTONE / CROSS-HATCHING ---
    if (loadGlobalUbo(pc.globalUbo).ppHalftone.x > 0.5) {
        float hScale = loadGlobalUbo(pc.globalUbo).ppHalftone.y;
        float hContrast = loadGlobalUbo(pc.globalUbo).ppHalftone.z;
        float2 hUv = uv * float2(loadGlobalUbo(pc.globalUbo).screenSize.x / loadGlobalUbo(pc.globalUbo).screenSize.y, 1.0) * hScale;
        float hTex = heapTex2D(pc.halftoneInput).SampleLevel(heapSampler(pc.defaultSampler), hUv, 0.0).r;
        
        // Procedural fallback (Процедурная штриховка, если текстура белая/отсутствует)
        if (loadGlobalUbo(pc.globalUbo).ppHalftone.w < 0.5) {
            hTex = clamp(sin(hUv.x * 2.0)*sin(hUv.y * 2.0)*2.0, 0.0, 1.0);
        }
        
        float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
        luma = clamp((luma - 0.5) * hContrast + 0.5, 0.0, 1.0);
        float hatchVal = smoothstep(0.45, 0.55, hTex + luma - 0.5); 
        
        color = lerp(float3(0.0), color, hatchVal);
    }
    
    // --- K-MEANS PALETTE (Постеризация с умным сдвигом) ---
    if (loadGlobalUbo(pc.globalUbo).ppGravVector.z > 0.5) {
        float k = max(loadGlobalUbo(pc.globalUbo).ppGravVector.w, 1.0);
        float3 hsv = rgb2hsv(color);
        hsv.x = floor(hsv.x * k + 0.5) / k;
        hsv.z = floor(hsv.z * k + 0.5) / k;
        color = hsv2rgb(hsv);
    }
    
    // --- RECURSIVE FEEDBACK LOOPS ---
    if (loadGlobalUbo(pc.globalUbo).ppKMeansFeed.x > 0.5) {
        float zoom = loadGlobalUbo(pc.globalUbo).ppKMeansFeed.y;
        float angle = loadGlobalUbo(pc.globalUbo).ppKMeansFeed.z * 3.14159 / 180.0;
        float2 d = uv - 0.5;
        float s = sin(angle), c = cos(angle);
        float2 feedUV = 0.5 + mul(float2x2(c, -s, s, c), d) * zoom;
        float3 feedColor = heapTex2D(pc.historyInput).SampleLevel(heapSampler(pc.defaultSampler), feedUV, 0.0).rgb;
        color = max(color, feedColor * 0.95); 
    }
     if (loadGlobalUbo(pc.globalUbo).gbColor1.w > 0.5) {
        float luma = dot(color, float3(0.299, 0.587, 0.114));
        float2 pixelScale = floor(float2(pixel) / 2.0); // Dither Grid
        const int bayer[16] = int[](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
        float dither = (float(bayer[(int(pixelScale.y)%4)*4 + (int(pixelScale.x)%4)]) / 15.0) - 0.5;
        luma = clamp(luma + dither * 0.3, 0.0, 1.0); // Dithering mix
        
        if (luma < 0.25) color = loadGlobalUbo(pc.globalUbo).gbColor1.rgb;
        else if (luma < 0.5) color = loadGlobalUbo(pc.globalUbo).gbColor2.rgb;
        else if (luma < 0.75) color = loadGlobalUbo(pc.globalUbo).gbColor3.rgb;
        else color = loadGlobalUbo(pc.globalUbo).gbColor4.rgb;
    }
    // --- MANGA SCREENTONES ---
    if (loadGlobalUbo(pc.globalUbo).ppStylized3.x > 0.5) {
        float sSize = max(loadGlobalUbo(pc.globalUbo).ppStylized3.y, 1.0);
        float darkness = loadGlobalUbo(pc.globalUbo).ppStylized3.z;
        float2 scrUV = uv * loadGlobalUbo(pc.globalUbo).screenSize.xy / sSize;
        float2 localUV = frac(scrUV) - 0.5;
        float luma = dot(color, float3(0.299, 0.587, 0.114));
        float radius = clamp(1.0 - luma * darkness, 0.0, 1.0) * 0.707;
        float dotMask = step(length(localUV), radius);
        color = lerp(color, float3(0.0), dotMask); // Черные точки в тенях
    }

    // --- CMAA (Conservative Morphological Anti-Aliasing) ---
    if (loadGlobalUbo(pc.globalUbo).ppColorComp.z > 0.5) {
        float2 texel = 1.0 / loadGlobalUbo(pc.globalUbo).screenSize.xy;
        float luma = dot(color, float3(0.299, 0.587, 0.114));
        float lumaU = dot(ReadColor(uv + float2(0, texel.y)), float3(0.299, 0.587, 0.114));
        float lumaD = dot(ReadColor(uv - float2(0, texel.y)), float3(0.299, 0.587, 0.114));
        float lumaL = dot(ReadColor(uv - float2(texel.x, 0)), float3(0.299, 0.587, 0.114));
        float lumaR = dot(ReadColor(uv + float2(texel.x, 0)), float3(0.299, 0.587, 0.114));

        float dU = abs(luma - lumaU);
        float dD = abs(luma - lumaD);
        float dL = abs(luma - lumaL);
        float dR = abs(luma - lumaR);

        float maxH = max(dL, dR);
        float maxV = max(dU, dD);
        
        float2 dir = (maxH > maxV) ? float2(texel.x, 0) : float2(0, texel.y);
        float maxDiff = max(maxH, maxV);
        
        if (maxDiff > 0.05) { // Edge threshold
            color = (color * 2.0 + ReadColor(uv - dir) + ReadColor(uv + dir)) * 0.25;
        }
    }

    // --- COLOR FX & DEBUG ---
    if (loadGlobalUbo(pc.globalUbo).ppColorFX.x > 0.5) {
        color = float3(1.0) - color;
    }
    if (loadGlobalUbo(pc.globalUbo).ppColorFX.y > 0.5) {
        float l = dot(color, float3(0.2126, 0.7152, 0.0722));
        if (l < 0.25) color = lerp(float3(0,0,1), float3(0,1,1), l*4.0);
        else if (l < 0.5) color = lerp(float3(0,1,1), float3(0,1,0), (l-0.25)*4.0);
        else if (l < 0.75) color = lerp(float3(0,1,0), float3(1,1,0), (l-0.5)*4.0);
        else color = lerp(float3(1,1,0), float3(1,0,0), (l-0.75)*4.0);
    }
    if (loadGlobalUbo(pc.globalUbo).ppColorFX.z > 0.5) {
        float d = heapTex2D_r(pc.depthInput).SampleLevel(heapSampler(pc.defaultSampler), uv, 0.0).r;
        float zN = loadGlobalUbo(pc.globalUbo).zNear; float zF = loadGlobalUbo(pc.globalUbo).zFar;
        float linD = (zN * zF) / (zF + d * (zN - zF));
        color = float3(linD / 50.0);
    }

    if (any(isnan(color)) || any(isinf(color))) color = float3(0.0);
    

    heapRWTex2D(pc.hdrOutput)[pixel] = float4(color, 1.0);

    // --- Auto Exposure Adaptation (Выполняется в 1 потоке на весь кадр) ---
    // SUBGROUP OPTIMIZATION: 256 threads of the first workgroup do this in parallel!
    if (id.x == 0 && id.y == 0 && loadGlobalUbo(pc.globalUbo).ppExposureParams.x > 0.5) {
        float2 suv = (float2(id.xy) + 0.5) / 16.0;
        float3 s = max(heapTex2D(pc.hdrInput).SampleLevel(heapSampler(pc.defaultSampler), suv, 0.0).rgb, float3(0.0));
        float luma = dot(s, float3(0.2126, 0.7152, 0.0722));
        float logLuma = log2(clamp(luma, 0.0001, 10000.0));
        
        // Аппаратная редукция (сложение) внутри subgroup без shared memory!
        float sumLuma = WaveActiveSum(logLuma);
        
        if (WaveGetLaneIndex() == 0) {
            // Только 1 поток (лидер) выполняет финальный расчет
            float avgLuma = exp2(sumLuma / 256.0);
            float targetLuma = clamp(avgLuma, loadGlobalUbo(pc.globalUbo).ppExposureParams.z, loadGlobalUbo(pc.globalUbo).ppExposureParams.w);
            (*DescriptorHandle<RWStructuredBuffer<float>>(pc.exposureBuffer))[0] = lerp((*DescriptorHandle<RWStructuredBuffer<float>>(pc.exposureBuffer))[0], targetLuma, 0.02);
        }
    }
}