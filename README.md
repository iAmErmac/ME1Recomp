# ME1Recomp

A static recompilation of **Mass Effect** (Xbox 360, title 4D5307E8) for Android (ARM64,
through the [XDroid360](https://github.com/udarmolota/XDroid360) launcher) and the PC. The
game's PowerPC code is translated to C++ with XenonRecomp and compiled for the host. The
runtime (kernel, XAM, audio, GPU front end) comes from
[SoDRecomp](https://github.com/udarmolota/SoDRecomp), which is based on
[DAORecomp](https://github.com/96dot/Dragon-Age-Origins-Recomp).

**This repository holds no game code or data.** The game is the player's own copy: the disc's
`default.xex` is recompiled on the player's machine, and the code XenonRecomp generates from it
is never committed. One exception: `android/profile/runtime_shaders` holds the 57 shaders the
game's Direct3D and Scaleform build at run time (64 KB, as the game's hook saved them on a phone).
No file on the disc holds them, and without them the launcher's first build of the shader library
leaves the menus without text until a first run and "Update game".

## Status

Early work.
- The PC build (Xenia's Direct3D 12 GPU) starts the game and reaches the title screen, the main
  menu and character creation.
- Android, with XDroid360's native (EDRAM-free) renderer, in a development build: the intro videos,
  the menus with their text and character creation render on a Galaxy S25 at the game's 30 fps.
  The game hooks are in `ME1Recomp/ME1Recomp/gpu/native_hooks.cpp`: the Direct3D draws and shader
  objects; the shaders Direct3D and Scaleform build at run time, saved to
  `userdata/runtime_shaders` for the shader library; 2x MSAA with predicated tiling turned off.
  The launcher's game profile (`android/profile`) is not in XDroid360 yet.
- The supported program is `default.xex` XEX 0.0.0.5 (media 572BA75D),
  SHA-256 `4beb582540010b25032e3a51e4ce84a6fb1a9f381adddd8b2def0dc5d6bf715d`. Another edition's
  `default.xex` is a different program and needs its own configuration.

## What is here

| Path | What |
|---|---|
| `ME1Recomp/` | the runtime: kernel, XAM, audio, the GPU front end, settings, the PC launcher |
| `android/` | the Android runtime and front end, and the game profile's recipe template (not adapted to Mass Effect yet) |
| `config/` | XenonRecomp's configuration: `ME1.toml` (helpers, setjmp/longjmp, function bounds, data in code) and `ME1_switch_tables.toml` |
| `scripts/` | `patch_xenonrecomp.py` (the XDroid360 games' shared recompiler patch), `patch_xenonrecomp_me1.py` (Mass Effect's extra instructions), the function and jump-table finders (`find_helpers.py`, `find_switches.py`, `fix_functions.py`, `ppc.py`), `run_game.py` |
| `tests/insn/` | unit checks for the XenonRecomp additions |
| `XenonRecomp/` | submodule: XenonRecomp at ddd128b |
| `ME1Recomp/thirdparty/SDL` | submodule: SDL 3.4.18 |

## Building on the PC

Layout next to this checkout: the extracted disc in `../game` (with `default.xex` at its root),
the generated code in `../ppc`.

1. Patch XenonRecomp: `python scripts/patch_xenonrecomp.py`, then
   `python scripts/patch_xenonrecomp_me1.py`, and build it.
2. Generate the code: `XenonRecomp config/ME1.toml XenonRecomp/XenonUtils/ppc_context.h`.
3. Configure `ME1Recomp/` with CMake and Ninja, using `ME1Recomp/cmake/clang-cl-xwin.cmake`
   (`ME1_LLVM_BIN`, `ME1_WINSYSROOT`). `XDROID360_ROOT` names an XDroid360 checkout with its
   `recomp` submodule (Xenia and its third-party sources). `ME1_PPC_DIR` names the generated code.
4. Run `ME1Recomp.exe <game folder> --play`.

## Licence

GPL-3.0 (`LICENSE`). The runtime code from DAORecomp and SoDRecomp keeps its MIT notice, and
third-party parts keep their own licences: see `NOTICE.md`.
