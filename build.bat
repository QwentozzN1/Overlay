@echo off
setlocal
chcp 65001 > nul

echo [INFO] Компиляция проекта...

if not exist build mkdir build

:: Компилируем всё вместе, чтобы избежать любых проблем с линковкой внешних символов
cl.exe /EHsc /O2 /W3 /Fe:build\RustOverlay.exe main.cpp ^
    imgui\imgui.cpp ^
    imgui\imgui_draw.cpp ^
    imgui\imgui_tables.cpp ^
    imgui\imgui_widgets.cpp ^
    imgui\imgui_impl_win32.cpp ^
    imgui\imgui_impl_dx11.cpp ^
    /I "imgui" ^
    user32.lib gdi32.lib d3d11.lib dxgi.lib

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Ошибка при компиляции!
    pause
    exit /b %errorlevel%
)

echo.
echo [SUCCESS] Проект успешно собран! Готовый файл: build\RustOverlay.exe
pause
endlocal