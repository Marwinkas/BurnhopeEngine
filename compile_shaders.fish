#!/usr/bin/fish

# Создаем папку для скомпилированных шейдеров, если её нет
mkdir -p Shaders/compiled

for shader in Shaders/*.{vert,frag,comp}
    set filename (basename $shader)
    echo "Compiling $filename..."
    glslc $shader -o Shaders/$filename.spv
end

echo "Done! All shaders compiled to .spv"
