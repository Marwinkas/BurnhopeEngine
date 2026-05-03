@echo off
echo Starting the build process...

:: Создаем папку для сборки, если её нет
if not exist build mkdir build
cd build

:: Конфигурируем проект (выбираем компилятор и настройки)
:: Флаг -A x64 важен для 64-битных систем
cmake .. -A x64

:: Сама сборка. Параметр --config Release создаст быстрый exe без лишнего веса
cmake --build . --config Release

echo.
echo Done! Your engine is in build/Release/
pause