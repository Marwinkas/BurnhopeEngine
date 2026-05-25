import common.Bindless;

struct MatPreviewPC {
    float4 albedoColor;
    float4 emissiveColor;
    float4 matParams;
    float4 uvScale_triSc;
    float4 camPos_time;
    int4 flags;
    uint outImage;
    uint texAlbedo;
    uint texORM;
    uint texNormal;
    uint texHDR;
    uint defaultSampler;
};

[[vk::push_constant]] MatPreviewPC pc;




static const float MAX_DIST = 50.0;
static const float SURF_DIST = 0.001;
static const int MAX_STEPS = 100;

float sdSphere(float3 p, float s) { return length(p) - s; }
float sdBox(float3 p, float3 b) { float3 q = abs(p) - b; return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0); }

float getDist(float3 p) {
    if (pc.flags.x == 0) return sdSphere(p, 1.2);
    if (pc.flags.x == 1) return sdBox(p, float3(0.9));
    return sdBox(p, float3(1.5, 0.05, 1.5));
}

float3 getNormal(float3 p) {
    float d = getDist(p); float2 e = float2(0.001, 0);
    float3 n = d - float3(getDist(p-e.xyy), getDist(p-e.yxy), getDist(p-e.yyx));
    return normalize(n);
}

float2 getUV(float3 p, float3 n) {
    float3 localN = normalize(n);
    float2 scale = pc.uvScale_triSc.xy;
    
    if (pc.flags.x == 0) return float2(atan2(localN.z, localN.x)/(2.0*3.14159) + 0.5, asin(localN.y)/3.14159 + 0.5) * scale;
    if (pc.flags.x == 1) {
        float3 a = abs(localN);
        if (a.x > a.y && a.x > a.z) return p.yz * scale * 0.5 + 0.5;
        if (a.y > a.x && a.y > a.z) return p.xz * scale * 0.5 + 0.5;
        return p.xy * scale * 0.5 + 0.5;
    }
    return p.xz * scale * 0.5 + 0.5;
}

float3 fresnelSchlick(float cosT, float3 F0) { return F0 + (1.0 - F0) * pow(clamp(1.0 - cosT, 0.0, 1.0), 5.0); }
float DistributionGGX(float3 N, float3 H, float r) { float a=r*r; float a2=a*a; float NdH=max(dot(N,H),0.0); return a2/(3.14159*pow(NdH*NdH*(a2-1.0)+1.0,2.0)); }
float GeometrySchlickGGX(float NdV, float r) { float k=pow(r+1.0,2.0)/8.0; return NdV/(NdV*(1.0-k)+k); }
float GeometrySmith(float3 N, float3 V, float3 L, float r) { return GeometrySchlickGGX(max(dot(N,V),0.0),r) * GeometrySchlickGGX(max(dot(N,L),0.0),r); }

float4 sampleTex(Texture2D<float4> tex, SamplerState sam, float3 p, float3 n, float2 uv, bool useTri, bool repTex) {
    if(useTri) {
        float3 bf = abs(n);
        bf = max(bf - 0.2, 0.0);
        bf /= dot(bf, float3(1.0));
        float s = pc.uvScale_triSc.z;
        float2 uvX = p.zy * s; float2 uvY = p.xz * s; float2 uvZ = p.xy * s;
        if (!repTex) {
            uvX = clamp(uvX, 0.0, 1.0);
            uvY = clamp(uvY, 0.0, 1.0);
            uvZ = clamp(uvZ, 0.0, 1.0);
        }
        float4 tx = tex.Sample(sam, uvX);
        float4 ty = tex.Sample(sam, uvY);
        float4 tz = tex.Sample(sam, uvZ);
        return tx*bf.x + ty*bf.y + tz*bf.z;
    }
    float2 finalUV = repTex ? uv : clamp(uv, 0.0, 1.0);
    return tex.Sample(sam, finalUV);
}

float2 getSphericalUV(float3 v) {
    float2 uv = float2(atan2(v.z, v.x), asin(v.y));
    uv *= float2(0.1591549, 0.3183098);
    return uv + 0.5;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    SamplerState sam = heapSampler(pc.defaultSampler);
    RWTexture2D<float4> outImage = heapRWTex2D(pc.outImage);

    int2 pixel = int2(id.xy); int2 size = int2(512, 512) /* preview size */;
    if (pixel.x >= size.x || pixel.y >= size.y) return;

    float2 uv = (float2(pixel) / float2(size)) * 2.0 - 1.0;
    uv.y = -uv.y; // Flip Y for Vulkan
    
    float3 ro = pc.camPos_time.xyz;
    float3 fwd = normalize(-ro);
    float3 right = normalize(cross(float3(0,1,0), fwd));
    if(length(right) < 0.01) right = float3(1,0,0);
    float3 up = cross(fwd, right);
    float3 rd = normalize(fwd + uv.x * right * 0.414 + uv.y * up * 0.414);

    float dO = 0.0;
    for(int i=0; i<MAX_STEPS; i++) { float3 p = ro + rd*dO; float dS = getDist(p); dO += dS; if(dO>MAX_DIST || dS<SURF_DIST) break; }

    float3 color = float3(0.06, 0.06, 0.08); // Dark UI background

    if(dO < MAX_DIST) {
        float3 p = ro + rd * dO; float3 n = getNormal(p); float2 texUV = getUV(p, n);
        
        bool hasAlb = (pc.flags.y & 1) != 0;
        bool hasORM = (pc.flags.y & 2) != 0;
        bool hasNrm = (pc.flags.y & 4) != 0;
        bool isTrn  = (pc.flags.y & 8) != 0;
        bool useTri = (pc.flags.y & 16) != 0;
        bool repTex = (pc.flags.y & 32) != 0;

        float3 albedo = pc.albedoColor.rgb; float alpha = pc.albedoColor.a;
        if(hasAlb) {
            float4 aa = sampleTex(heapTex2D(pc.texAlbedo), sam, p, n, texUV, useTri, repTex);
            albedo *= aa.rgb; alpha *= aa.a;
        }
        
        float metallic = pc.matParams.x; float roughness = pc.matParams.y; float ao = 1.0;
        if (hasORM) {
            float3 orm = sampleTex(heapTex2D(pc.texORM), sam, p, n, texUV, useTri, repTex).rgb;
            ao *= orm.r; roughness *= orm.g; metallic *= orm.b;
        }
        
        float3 emissive = pc.emissiveColor.rgb * pc.emissiveColor.w;

        if(hasNrm) {
            float3 rgb = sampleTex(heapTex2D(pc.texNormal), sam, p, n, texUV, useTri, repTex).rgb;
            float2 xy = rgb.xy * 2.0 - 1.0;
            xy *= pc.matParams.z;
            float3 tNormal = float3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)));
            float3 up = abs(n.y)<0.999 ? float3(0,1,0) : float3(1,0,0); float3 t = normalize(cross(up, n)); float3 b = cross(n, t);
            n = normalize(mul(float3x3(t, b, n), tNormal));
        }

        float3 V = normalize(-rd);
        
        // Image-Based Lighting Ambient (Используем Lod для устранения шума от HDRI)
        float3 envDiff = heapTex2D(pc.texHDR).SampleLevel(sam, getSphericalUV(n), 8.0).rgb;
        float3 envSpec = heapTex2D(pc.texHDR).SampleLevel(sam, getSphericalUV(reflect(-V, n)), roughness * 8.0).rgb;

        float3 F0 = lerp(float3(0.04), albedo, metallic);
        float3 kD = (float3(1.0) - fresnelSchlick(max(dot(n, V), 0.0), F0)) * (1.0 - metallic);
        
        float3 ambient = (kD * albedo * envDiff + envSpec * F0) * ao;

        // Sun Light
        float3 sunDir = normalize(float3(sin(pc.camPos_time.w), 0.5, cos(pc.camPos_time.w)));
        float3 Ls = sunDir; float3 Hs = normalize(V + Ls);
        float3 radS = float3(5.0); // Sun intensity
        
        float NdotL = max(dot(n, Ls), 0.0);
        float3 spec = (DistributionGGX(n, Hs, roughness) * GeometrySmith(n, V, Ls, roughness) * fresnelSchlick(max(dot(Hs, V), 0.0), F0)) / (4.0 * max(dot(n, V), 0.0) * NdotL + 0.0001);
        float3 Lo = (kD * albedo / 3.14159 + spec) * radS * NdotL;
        
        float3 finalColor = ambient + Lo + emissive;
        
        if (isTrn) {
            // Настоящее полупрозрачное смешивание (alpha blending)
            color = lerp(color, finalColor, alpha);
        } else {
            if (alpha >= 0.5) {
                color = finalColor;
            }
        }
    }
    
    color = color / (color + float3(1.0)); // Simple ACES/Reinhard
    color = pow(color, float3(1.0/2.2));   // Gamma
    outImage[pixel] = float4(color, 1.0);
}