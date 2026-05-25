import common.Bindless;
import common.GlobalUbo;
import common.GbufferDebug;
import common.GfxHeapPC;
import common.MaterialGpu;
import common.SceneAddresses;

[[vk::push_constant]] GfxHeapPC pc;

struct VSOut {
    float3 inCrntPos : TEXCOORD0;
    float2 inTexCoord : TEXCOORD1;
    float3 inTBN0 : TEXCOORD2;
    float3 inTBN1 : TEXCOORD3;
    float3 inTBN2 : TEXCOORD4;
    nointerpolation uint inMatID : TEXCOORD5;
    float4 inColor : TEXCOORD6;
    float2 inUV2 : TEXCOORD7;
    float inThickness : TEXCOORD8;
};


struct MaterialBlock {
    MaterialData materials[];
};

struct FragOut {
    float4 gNormalRoughness : SV_Target0;
    float4 gAlbedoMetallic : SV_Target1;
    float4 gHeightAO : SV_Target2;
    float4 gEmissive : SV_Target3;
    uint o_PortalID : SV_Target4;
};








float2 matSampleUv(float2 uv, int repeat) {
    return repeat == 1 ? frac(uv) : clamp(uv, 0.0, 1.0);
}

float3 sampleMatTexRgb(SceneAddresses scene, int heapSlot, float2 uv, int repeat) {
    int texIdx = sceneTextureHeapIndex(scene, heapSlot);
    if (texIdx < 0) return float3(1.0, 0.0, 1.0);
    SamplerState sam = heapSampler(0u);
    float2 sampleUv = matSampleUv(uv, repeat);
    return heapBindlessTex2DNonUniform(texIdx).SampleLevel(sam, sampleUv, 0.0).rgb;
}

float4 sampleMatTex(SceneAddresses scene, int heapSlot, float2 uv, float2 dx, float2 dy, int useTriplanar, float3 pos, float3 posDdx, float3 posDdy, float3 blend, float scale, int repeat) {
    int texIdx = sceneTextureHeapIndex(scene, heapSlot);
    if (texIdx < 0) return float4(1.0, 1.0, 1.0, 1.0);
    SamplerState sam = heapSampler(0u);
    if (useTriplanar == 1) {
        float2 uvX = pos.zy * scale; float2 uvY = pos.xz * scale; float2 uvZ = pos.xy * scale;
        uvX = matSampleUv(uvX, repeat); uvY = matSampleUv(uvY, repeat); uvZ = matSampleUv(uvZ, repeat);
        float4 tx = heapBindlessTex2DNonUniform(texIdx).SampleLevel(sam, uvX, 0.0);
        float4 ty = heapBindlessTex2DNonUniform(texIdx).SampleLevel(sam, uvY, 0.0);
        float4 tz = heapBindlessTex2DNonUniform(texIdx).SampleLevel(sam, uvZ, 0.0);
        return tx * blend.x + ty * blend.y + tz * blend.z;
    }
    float3 rgb = sampleMatTexRgb(scene, heapSlot, uv, repeat);
    return float4(rgb, 1.0);
}



// Frag debug mode: pc.gfxDebug low 4 bits (see Render/RenderDebug.hpp kGBufferDebugFragMode).
[shader("fragment")]
FragOut main(VSOut v, float4 fragPos : SV_Position) {
    uint dbgMode = gbufferFragDebugMode(pc.gfxDebug);
    // Debug paths first — do not touch bindless SSBOs (bad heap = black frame).
    if (dbgMode == 1u) {
        FragOut dbg;
        dbg.gNormalRoughness = float4(0.0, 1.0, 0.0, 0.5);
        dbg.gAlbedoMetallic = float4(0.0, 1.0, 0.0, 0.0);
        dbg.gHeightAO = float4(0.0, 1.0, 0.0, 1.0);
        dbg.gEmissive = float4(0.0, 1.0, 0.0, 1.0);
        dbg.o_PortalID = 0u;
        return dbg;
    }
    if (dbgMode == 2u) {
        FragOut dbg;
        dbg.gAlbedoMetallic = float4(1.0, 0.0, 1.0, 1.0);
        dbg.gNormalRoughness = float4(1.0, 0.0, 1.0, 0.5);
        dbg.gHeightAO = float4(1.0, 0.0, 1.0, 1.0);
        dbg.gEmissive = float4(1.0, 0.0, 1.0, 1.0);
        dbg.o_PortalID = 0u;
        return dbg;
    }
    if (dbgMode == 6u && pc.sceneAddressesBda != 0u) {
        SceneAddresses scene = loadSceneAddresses(pc.sceneAddressesBda);
        float3 finalTestColor = float3(1.0, 0.0, 1.0); // magenta = no sub-test branch matched
        // Sub-test: RenderDebug.hpp kGBufferDebugFragSubTest → gfxDebug bits 4..6.
        uint subTest = (pc.gfxDebug >> 4u) & 7u;
        uint matIdx = min(v.inMatID, pc.meshMaterialId);
        MaterialData mat = loadMaterialScene(scene, matIdx);
        float2 uv = v.inTexCoord * mat.uvScale;
        if (subTest == 0u) {
            // UV checker — not a texture test (was black: finalTestColor stayed 0).
            finalTestColor = float3(
                frac(uv.x * 8.0) > 0.5 ? 1.0 : 0.0,
                frac(uv.y * 8.0) > 0.5 ? 1.0 : 0.0,
                0.0);
        } else if (subTest == 1u && scene.globalUboBda != 0u) {
            GlobalUbo* ubo = sceneGlobalUboPtr(scene);
            finalTestColor = ubo->sunColor.rgb;
        } else if (subTest == 2u) {
            finalTestColor = mat.albedoColor.rgb;
        } else if (subTest == 3u || subTest == 5u || subTest == 6u) {
            finalTestColor = sampleMatTexRgb(scene, mat.albedoAlphaIdx, uv, mat.repeatTexture);
        } else if (subTest == 4u) {
            int heapIdx = sceneTextureHeapIndex(scene, mat.albedoAlphaIdx);
            finalTestColor = float3(
                mat.albedoAlphaIdx < 0 ? 0.0 : 1.0,
                heapIdx < 0 ? 0.0 : 1.0,
                scene.textureTableCount > 0u ? 1.0 : 0.0);
        } else if (subTest == 7u) {
            finalTestColor = sampleMatTexRgb(scene, int(scene.textureTableBase), uv, mat.repeatTexture);
        }

        FragOut dbg;
        dbg.gAlbedoMetallic = float4(finalTestColor, 0.0);
        dbg.gNormalRoughness = float4(normalize(v.inTBN2) * 0.5 + 0.5, 0.5);
        dbg.gHeightAO = float4(1.0, 1.0, 1.0, 1.0);
        dbg.gEmissive = float4(0.0, 0.0, 0.0, 1.0);
        dbg.o_PortalID = 0u;
        return dbg;
    }
    if (dbgMode == 3u) {
        uint id = max(v.inMatID, 1u);
        float3 idCol = float3(
            float((id * 37u) % 255u) / 255.0,
            float((id * 17u) % 255u) / 255.0,
            float((id * 97u) % 255u) / 255.0);
        FragOut dbg;
        dbg.gAlbedoMetallic = float4(idCol, 0.0);
        dbg.gNormalRoughness = float4(idCol, 0.5);
        dbg.gHeightAO = float4(1.0, 1.0, 1.0, 1.0);
        dbg.gEmissive = float4(0.0, 0.0, 0.0, 1.0);
        dbg.o_PortalID = 0u;
        return dbg;
    }

    // Minimal mode 0: no heap UBO / textures (frag mode 2 OK; full mode 0 = DEVICE_LOST on NV).
    if (gbufferMinimalFragNoHeap(pc.gfxDebug)) {
        float3 N = normalize(v.inTBN2);
        FragOut o;
        o.gNormalRoughness = float4(N * 0.5 + 0.5, 0.5);
        o.gAlbedoMetallic = float4(v.inColor.rgb, 0.0);
        o.gHeightAO = float4(0.0, 1.0, 0.5, 1.0);
        o.gEmissive = float4(0.0, 0.0, 0.0, 1.0);
        o.o_PortalID = 0u;
        return o;
    }

    if (pc.sceneAddressesBda == 0u) {
        float3 N = normalize(v.inTBN2);
        FragOut o;
        o.gNormalRoughness = float4(N * 0.5 + 0.5, 0.5);
        o.gAlbedoMetallic = float4(v.inColor.rgb, 0.0);
        o.gHeightAO = float4(0.0, 1.0, 0.5, 1.0);
        o.gEmissive = float4(0.0, 0.0, 0.0, 1.0);
        o.o_PortalID = 0u;
        return o;
    }

    SceneAddresses scene = loadSceneAddresses(pc.sceneAddressesBda);
    if (scene.materialStorageBda == 0u || scene.globalUboBda == 0u) {
        float3 N = normalize(v.inTBN2);
        FragOut o;
        o.gNormalRoughness = float4(N * 0.5 + 0.5, 0.5);
        o.gAlbedoMetallic = float4(v.inColor.rgb, 0.0);
        o.gHeightAO = float4(0.0, 1.0, 0.5, 1.0);
        o.gEmissive = float4(0.0, 0.0, 0.0, 1.0);
        o.o_PortalID = 0u;
        return o;
    }

    uint matIdx = min(v.inMatID, pc.meshMaterialId);
    MaterialData mat = loadMaterialScene(scene, matIdx);

    if (gbufferMinimalFragMaterialOnly(pc.gfxDebug)) {
        float3 N = normalize(v.inTBN2);
        float3 albedo = mat.albedoColor.rgb * v.inColor.rgb;
        FragOut o;
        o.gNormalRoughness = float4(N * 0.5 + 0.5, 0.5);
        o.gAlbedoMetallic = float4(albedo, mat.metallicStrength);
        o.gHeightAO = float4(0.0, mat.aoStrength, mat.roughnessStrength, 1.0);
        o.gEmissive = float4(mat.emissiveColor.rgb * mat.emissiveIntensity, 1.0);
        o.o_PortalID = 0u;
        return o;
    }

    GlobalUbo* ubo = sceneGlobalUboPtr(scene);

    if (gbufferMinimalFragUboOnly(pc.gfxDebug)) {
        float3 N = normalize(v.inTBN2);
        float3 albedo = mat.albedoColor.rgb * v.inColor.rgb * ubo->sunColor.rgb; 
        FragOut o;
        o.gNormalRoughness = float4(N * 0.5 + 0.5, 0.5);
        o.gAlbedoMetallic = float4(albedo, mat.metallicStrength);
        o.gHeightAO = float4(0.0, mat.aoStrength, mat.roughnessStrength, 1.0);
        o.gEmissive = float4(mat.emissiveColor.rgb * mat.emissiveIntensity, 1.0);
        o.o_PortalID = 0u;
        return o;
    }
    float2 pixelPos = fragPos.xy;

    float2 scaledUV = v.inTexCoord * mat.uvScale;
    float2 dx = ddx(scaledUV);
    float2 dy = ddy(scaledUV);
    
    // --- Melting / Creeping Walls ---
    // Везде ниже по тексту меняем ubo-> на ubo->
    if (ubo->ppPsych2.x > 0.0) {
        float2 screenUV = pixelPos.xy / ubo->screenSize.xy;
        float distFromCenter = distance(screenUV, float2(0.5)) * 2.0; 
        float threshold = ubo->ppPsych5.x;
        float meltFactor = 1.0;
        if (threshold < 0.99) {
            meltFactor = smoothstep(1.0 - threshold, 1.0 - threshold + 0.2, distFromCenter);
        }

        if (meltFactor > 0.001) {
            float time = ubo->ppMotionBlur.z;
            float speed = ubo->ppPsych2.x;
            float noiseScale = max(ubo->ppPsych5.y, 0.1);
            
            float s = sin(v.inTexCoord.x * noiseScale * 15.0) * cos(v.inTexCoord.x * noiseScale * 5.0 + time * 0.5);
            float stripSpeed = lerp(0.2, 1.0, smoothstep(-1.0, 1.0, s)); 
            
            scaledUV.y -= speed * time * stripSpeed * meltFactor; 
            scaledUV.x += sin(v.inTexCoord.y * noiseScale * 10.0 + time) * 0.01 * meltFactor * stripSpeed;
        }
    }

    float3 posDdx = ddx(v.inCrntPos);
    float3 posDdy = ddy(v.inCrntPos);
    float2 finalUV = mat.repeatTexture == 1 ? scaledUV : clamp(scaledUV, 0.0, 1.0);
    
    float3 N = normalize(v.inTBN2);
    float3 T = normalize(v.inTBN0);
    float3 dp = T - dot(T, N) * N;
    if (dot(dp, dp) > 0.0001) T = normalize(dp);
    else T = normalize(cross(N, abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0)));
    
    float signT = dot(cross(N, T), normalize(v.inTBN1)) < 0.0 ? -1.0 : 1.0;
    float3 B = cross(N, T) * signT;
    float3x3 finalTBN = float3x3(T, B, N);

    float3 blendWeights = abs(N);
    blendWeights = max(blendWeights - 0.2, 0.0);
    blendWeights /= max(dot(blendWeights, float3(1.0)), 0.00001); 

    float height    = 0.0;
    float3  albedo    = mat.albedoColor.rgb * v.inColor.rgb; 
    float alpha       = mat.albedoColor.a * v.inColor.a;     
    float3  emissive  = mat.emissiveColor.rgb * mat.emissiveIntensity;
    float metallic  = mat.metallicStrength;
    float roughness = mat.roughnessStrength; 
    float ao        = mat.aoStrength;        
    float3  worldNormal = N;
    
    // --- Hollow-Face Illusion ---
    if (ubo->ppPsych1.w > 0.0) {
        worldNormal = -worldNormal;
        finalTBN[2] = -finalTBN[2];
        mat.heightStrength = -mat.heightStrength * ubo->ppPsych5.z; 
    }

    // ORMX MAP
    if (mat.ormxIdx >= 0) {
        float4 ormx = sampleMatTex(scene, mat.ormxIdx, finalUV, dx, dy, mat.useTriplanar, v.inCrntPos, posDdx, posDdy, blendWeights, mat.triplanarScale, mat.repeatTexture);
        ao         *= ormx.r; 
        roughness *= ormx.g;
        metallic  *= ormx.b;
        height     = ormx.a;
        
        if (mat.useTriplanar == 0 && mat.heightStrength > 0.0) {
            float3 viewDirWorld   = normalize(ubo->camPos - v.inCrntPos);
            float3 viewDirTangent = normalize(mul(transpose(finalTBN), viewDirWorld));
            float2 p = viewDirTangent.xy / max(viewDirTangent.z, 0.1);
            finalUV = scaledUV - p * (height * 0.02 * mat.heightStrength);
            if (mat.repeatTexture == 0) finalUV = clamp(finalUV, 0.0, 1.0);
            ormx = sampleMatTex(scene, mat.ormxIdx, finalUV, dx, dy, 0, v.inCrntPos, posDdx, posDdy, float3(1,0,0), 1.0, mat.repeatTexture);
            ao *= ormx.r; roughness *= ormx.g; metallic *= ormx.b; height = ormx.a;
        }
    }
    
    // Albedo
    if (mat.albedoAlphaIdx >= 0) {
        float4 texColor = sampleMatTex(scene, mat.albedoAlphaIdx, finalUV, dx, dy, mat.useTriplanar, v.inCrntPos, posDdx, posDdy, blendWeights, mat.triplanarScale, mat.repeatTexture);
        albedo *= texColor.rgb;
        alpha *= texColor.a;
    }

    // Screen-Door Dithering
    if (mat.isTransparent == 1) {
        if (alpha <= 0.01) discard;; 
        int x = int(pixelPos.x) % 4;
        int y = int(pixelPos.y) % 4;
        const float bayer[16] = float[](
            0.0625, 0.5625, 0.1875, 0.6875, 0.8125, 0.3125, 0.9375, 0.4375,
            0.2500, 0.7500, 0.1250, 0.6250, 1.0000, 0.5000, 0.8750, 0.3750
        );
        if (alpha < bayer[y * 4 + x]) discard;;
    } else if (gbufferFragDebugMode(pc.gfxDebug) != 0u) {
        if (alpha < 0.5) discard;;
    }
    
    // Emissive
    if (mat.emissiveIdx >= 0) {
        emissive = sampleMatTex(scene, mat.emissiveIdx, finalUV, dx, dy, mat.useTriplanar, v.inCrntPos, posDdx, posDdy, blendWeights, mat.triplanarScale, mat.repeatTexture).rgb * mat.emissiveColor.rgb * mat.emissiveIntensity;
    }
    
    // Normal Map
    if (mat.normalIdx >= 0) {
        if (mat.useTriplanar == 1) {
            float2 uvX = v.inCrntPos.zy * mat.triplanarScale; float2 uvY = v.inCrntPos.xz * mat.triplanarScale; float2 uvZ = v.inCrntPos.xy * mat.triplanarScale;
            float2 dxX = posDdx.zy * mat.triplanarScale; float2 dyX = posDdy.zy * mat.triplanarScale;
            float2 dxY = posDdx.xz * mat.triplanarScale; float2 dyY = mat.triplanarScale;
            float2 dxZ = posDdx.xy * mat.triplanarScale; float2 dyZ = posDdy.xy * mat.triplanarScale;
            
            if (mat.repeatTexture == 0) { uvX = clamp(uvX, 0.0, 1.0); uvY = clamp(uvY, 0.0, 1.0); uvZ = clamp(uvZ, 0.0, 1.0); }
            
            float3 rgbX = sampleMatTex(scene, mat.normalIdx, uvX, dxX, dyX, 0, v.inCrntPos, posDdx, posDdy, float3(1,0,0), 1.0, mat.repeatTexture).rgb;
            float3 rgbY = sampleMatTex(scene, mat.normalIdx, uvY, dxY, dyY, 0, v.inCrntPos, posDdx, posDdy, float3(1,0,0), 1.0, mat.repeatTexture).rgb;
            float3 rgbZ = sampleMatTex(scene, mat.normalIdx, uvZ, dxZ, dyZ, 0, v.inCrntPos, posDdx, posDdy, float3(1,0,0), 1.0, mat.repeatTexture).rgb;
            
            float2 tX_xy = rgbX.xy * 2.0 - 1.0; tX_xy *= mat.normalStrength;
            float3 tX = float3(tX_xy, sqrt(max(1.0 - dot(tX_xy, tX_xy), 0.0)));
            float2 tY_xy = rgbY.xy * 2.0 - 1.0; tY_xy *= mat.normalStrength;
            float3 tY = float3(tY_xy, sqrt(max(1.0 - dot(tY_xy, tY_xy), 0.0)));
            float2 tZ_xy = rgbZ.xy * 2.0 - 1.0; tZ_xy *= mat.normalStrength;
            float3 tZ = float3(tZ_xy, sqrt(max(1.0 - dot(tZ_xy, tZ_xy), 0.0)));
            
            float3 nX = float3(tX.z * sign(N.x), tX.y, -tX.x);
            float3 nY = float3(tY.x, tY.z * sign(N.y), -tY.y);
            float3 nZ = float3(tZ.x, tZ.y, tZ.z * sign(N.z));

            worldNormal = normalize(nX * blendWeights.x + nY * blendWeights.y + nZ * blendWeights.z);
        } else {
            float3 rgb = sampleMatTex(scene, mat.normalIdx, finalUV, dx, dy, 0, v.inCrntPos, posDdx, posDdy, float3(1,0,0), 1.0, mat.repeatTexture).rgb;
            float2 xy = rgb.xy * 2.0 - 1.0; xy *= mat.normalStrength;
            float3 tangentNormal = float3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)));
            worldNormal = normalize(mul(finalTBN, tangentNormal));
        }
    }
    
    roughness = clamp(roughness, 0.04, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);
    
    float lightMask = 0.5; float rimMask = 1.0; 
    
    // --- Micro-trypophobia ---
    if (ubo->ppPsych2.z > 0.0) {
        float scale = ubo->ppPsych2.z;
        float2 cell = frac(finalUV * scale) - 0.5;
        float dist = length(cell);
        if (dist < 0.3) {
            height -= 0.5 * mat.heightStrength; 
            float3 holeNormal = normalize(float3(-cell * 5.0, 1.0));
            worldNormal = normalize(mul(finalTBN, holeNormal));
            albedo *= 0.2; 
        }
    }

    // --- Depth Parallax Anomalies ---
    if (ubo->ppPsych2.w > 0.0) {
        float3 viewDirWorld   = normalize(ubo->camPos - v.inCrntPos);
        float3 viewDirTangent = normalize(mul(transpose(finalTBN), viewDirWorld));
        float2 p = viewDirTangent.xy / max(viewDirTangent.z, 0.1);
        float2 deepUV = finalUV - p * 0.1; 
        
        float eyeShape = length(frac(deepUV * 5.0) - 0.5);
        if (eyeShape < 0.15) {
            albedo = lerp(albedo, float3(0.9, 0.1, 0.1), 0.8); 
            emissive += float3(0.8, 0.0, 0.0);
        }
    }

    FragOut o;
    o.gNormalRoughness = float4(worldNormal * 0.5 + 0.5, roughness);
    o.gAlbedoMetallic  = float4(albedo, metallic);
    o.gHeightAO        = float4(height, ao, lightMask, rimMask);
    // Emissive.a = window depth [0,1] for lighting (NV: do not sample D32/D24 depth heap).
    o.gEmissive        = float4(emissive, saturate(fragPos.z));
    o.o_PortalID = ubo->portalID;
    return o;
}
