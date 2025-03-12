set CURRENT_DIR=%CD%
mkdir build > nul
set PICO_SDK_PATH=%~dp0/pico-sdk
cmake -B build -G Ninja -S .
