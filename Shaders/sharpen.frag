#version 430 core
out vec4 FragColor;

// ВАЖНО: Это имя ДОЛЖНО совпадать с "out" переменной в framebuffer.vert!
in vec2 texCoords; 

uniform sampler2D screenTexture;
uniform float sharpenAmount;
uniform vec2 texelSize; 

#define FXAA_REDUCE_MIN   (1.0/128.0)
#define FXAA_REDUCE_MUL   (1.0/8.0)
#define FXAA_SPAN_MAX     8.0

void main() {
    // 1. БЕРЕМ СОСЕДНИЕ ПИКСЕЛИ
    vec3 rgbNW = texture(screenTexture, texCoords + vec2(-1.0, -1.0) * texelSize).xyz;
    vec3 rgbNE = texture(screenTexture, texCoords + vec2(1.0, -1.0) * texelSize).xyz;
    vec3 rgbSW = texture(screenTexture, texCoords + vec2(-1.0, 1.0) * texelSize).xyz;
    vec3 rgbSE = texture(screenTexture, texCoords + vec2(1.0, 1.0) * texelSize).xyz;
    vec3 rgbM  = texture(screenTexture, texCoords).xyz;

    // 2. КОНВЕРТИРУЕМ В ЯРКОСТЬ (Luma)
    vec3 lumaVector = vec3(0.299, 0.587, 0.114);
    float lumaNW = dot(rgbNW, lumaVector);
    float lumaNE = dot(rgbNE, lumaVector);
    float lumaSW = dot(rgbSW, lumaVector);
    float lumaSE = dot(rgbSE, lumaVector);
    float lumaM  = dot(rgbM,  lumaVector);

    // 3. ОПРЕДЕЛЯЕМ ГРАНИЦЫ
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);

    // 4. ВЫЧИСЛЯЕМ НАПРАВЛЕНИЕ СГЛАЖИВАНИЯ
    dir = min(vec2(FXAA_SPAN_MAX, FXAA_SPAN_MAX), max(vec2(-FXAA_SPAN_MAX, -FXAA_SPAN_MAX), dir * rcpDirMin)) * texelSize;

    // 5. ДЕЛАЕМ ВЫБОРКИ ПОД УГЛОМ (Смягчаем край)
    vec3 rgbA = 0.5 * (
        texture(screenTexture, texCoords + dir * (1.0/3.0 - 0.5)).xyz +
        texture(screenTexture, texCoords + dir * (2.0/3.0 - 0.5)).xyz);

    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        texture(screenTexture, texCoords + dir * (0.0/3.0 - 0.5)).xyz +
        texture(screenTexture, texCoords + dir * (3.0/3.0 - 0.5)).xyz);

    float lumaB = dot(rgbB, lumaVector);

    // Выбираем лучший вариант сглаживания
    vec3 finalColor = ((lumaB < lumaMin) || (lumaB > lumaMax)) ? rgbA : rgbB;

    // 6. РЕЗКОСТЬ (Sharpen)
    if (sharpenAmount > 0.0) {
        vec3 blur = (rgbNW + rgbNE + rgbSW + rgbSE) * 0.25;
        finalColor = finalColor + (finalColor - blur) * sharpenAmount; 
    }

    FragColor = vec4(finalColor, 1.0);
}