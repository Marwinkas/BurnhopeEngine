#version 330 core
layout (location = 0) out vec4 FragColor;
in vec3 TexCoords;
uniform vec3 sunDir;
vec3 skyColorTop = vec3(0.15, 0.45, 0.85);
vec3 skyColorBottom = vec3(0.8, 0.9, 1.0);
vec3 sunsetColor = vec3(1.0, 0.5, 0.2);
vec3 sunColor = vec3(1.0, 0.9, 0.8);
vec3 nightColorTop = vec3(0.01, 0.02, 0.05);
vec3 nightColorBottom = vec3(0.0, 0.0, 0.02);
vec3 moonColor = vec3(0.9, 0.95, 1.0);
float rand(vec3 co) {
    return fract(sin(dot(co, vec3(12.9898, 78.233, 54.53))) * 43758.5453);
}
void main() {
    vec3 viewDir = normalize(TexCoords);
    vec3 L = normalize(-sunDir);     float sunHeight = L.y; 
    float dayIntensity = smoothstep(-0.35, 0.1, sunHeight);
    float sunsetFactor = smoothstep(0.18, 0.0, sunHeight);
    float nightFade = smoothstep(-0.4, -0.1, sunHeight);
    float sunsetIntensity = sunsetFactor * nightFade;
    vec3 currentTop = mix(nightColorTop, skyColorTop, dayIntensity);
    vec3 currentBottom = mix(nightColorBottom, skyColorBottom, dayIntensity);
    float height = max(viewDir.y, 0.0);
    vec3 currentHorizon = mix(currentBottom, sunsetColor, sunsetIntensity);
    vec3 skyGradient = mix(currentHorizon, currentTop, height);
        float sunDot = dot(viewDir, L);
    float sunGlow = max(sunDot, 0.0);
    vec3 corona = sunColor * pow(sunGlow, 32.0) * 0.5 * dayIntensity;
    float disk = smoothstep(0.998, 0.999, sunGlow);
    vec3 sunDisk = sunColor * disk * 50.0 * dayIntensity;
            vec3 moonDir = normalize(-L); 
        float moonDot = dot(viewDir, moonDir);
        float moonDiskIntensity = smoothstep(0.9994, 0.9996, moonDot);
            vec3 moonFinal = moonColor * moonDiskIntensity * (1.0 - dayIntensity);
        float starVal = rand(viewDir * 200.0); 
    float starIntensity = step(0.998, starVal); 
    vec3 stars = vec3(starIntensity) * (1.0 * (1.0 - dayIntensity));
        FragColor = vec4(skyGradient + corona + sunDisk + moonFinal + stars, 1.0);
}