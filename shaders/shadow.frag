#version 450

// Пустой фрагментный шейдер — только записываем глубину
// Vulkan сам пишет gl_FragDepth из gl_Position.z

void main() {
    // Ничего не делаем — depth пишется автоматически
}
