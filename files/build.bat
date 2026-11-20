@echo off
REM ---- LiteCity build script (Windows) ----
where g++ >nul 2>nul
if %errorlevel%==0 goto mingw
where cl >nul 2>nul
if %errorlevel%==0 goto msvc
echo.
echo No C++ compiler found. Install ONE of these, then run build.bat again:
echo   1) MSYS2 + MinGW-w64   https://www.msys2.org   (then: pacman -S mingw-w64-ucrt-x86_64-gcc)
echo   2) Visual Studio Build Tools (run from "x64 Native Tools Command Prompt")
pause
exit /b 1

:mingw
echo Building with MinGW g++ ...
g++ -std=c++17 -O2 main.cpp -o LiteCity.exe -mwindows -static -lopengl32 -lglu32 -lgdi32 -luser32
goto done

:msvc
echo Building with MSVC ...
cl /nologo /O2 /EHsc /std:c++17 main.cpp /Fe:LiteCity.exe /link opengl32.lib glu32.lib gdi32.lib user32.lib /SUBSYSTEM:WINDOWS
goto done

:done
if exist LiteCity.exe (echo. & echo SUCCESS: LiteCity.exe created. Double-click it to play.) else (echo. & echo Build failed.)
pause
