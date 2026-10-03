#pragma once

#include "ui/Canvas.hpp"

namespace burnhope {

// Пример приложения. Другое приложение передаёт свой UiBuilder в hostOpen и не трогает окно, rhi и present.
void shellBuild(Canvas& canvas, void* user);
void toolBuild(Canvas& canvas, void* user);
void colorBuild(Canvas& canvas, void* user);

} // namespace burnhope
