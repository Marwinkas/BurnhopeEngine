#pragma once

#include "ui/Canvas.hpp"

namespace burnhope {

// Пример приложения. Другое приложение подставляет свой UiBuilder и не трогает rhi/ui.
void shellBuild(Canvas& canvas, void* user);
void toolBuild(Canvas& canvas, void* user);
void colorBuild(Canvas& canvas, void* user);

} // namespace burnhope
