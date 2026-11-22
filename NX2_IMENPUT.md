# imenput-emu notes

Fork of Lichii 031c8a8 (see `IMENPUT_FORK.md`). Work log for this fork.

## Build (Windows, MSVC)
See `NX2_NOTES.md` "Setup". Short form, from a VS "x64 Native Tools" prompt in a SHORT path (e.g. `C:\dev\imenput-emu`):

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INTERMEDIATE_DIR_STRATEGY=SHORT -DENABLE_QT=OFF -DLICHII_CMD=ON -DENABLE_WEB_SERVICE=OFF -DLICHII_ROOM=OFF -DLICHII_TESTS=OFF -DENABLE_WERROR=OFF -DENABLE_UPDATE_CHECKER=OFF
    cmake --build build --target lichii-cmd

## Run with debug captures (keys/firmware/games stay on your own PC, never in the repo)

    set EDEN_NX2_AUTO_STUB=1
    set EDEN_NX2_FRAME_CAPTURE=C:\dev\frames
    set EDEN_NX2_SHADER_DUMP=C:\dev\shaders
    build\bin\eden-cli.exe -g "<game folder>\exefs\main"

## Log
- 2026-11-22: imported Lichii; first target Donkey Kong Bananza (black screen).
- 2026-11-22: `Core::ResolveGamePath` (core.cpp): a folder given on the command line is searched (3 levels) for an ExeFS (`main`), `exefs/main`, then the largest .nsp/.xci/.nsz/.xcz/.nca. Files are used as they are.
