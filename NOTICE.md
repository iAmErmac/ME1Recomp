# Notices

ME1Recomp as a whole is licensed under the GNU General Public License, version 3 (`LICENSE`).

## Code it is built from

- **DAORecomp** (https://github.com/96dot/Dragon-Age-Origins-Recomp), through **SoDRecomp**
  (https://github.com/udarmolota/SoDRecomp): the runtime in `ME1Recomp/` (kernel, XAM, audio,
  GPU front end, settings, launcher), the scripts in `scripts/` and the instruction tests in
  `tests/`. That code was released under the MIT licence below, and its notice is kept.
- **XenonRecomp** (https://github.com/hedge-dev/XenonRecomp, MIT): a submodule, patched by
  `scripts/patch_xenonrecomp.py` (the XDroid360 games' shared patch) and
  `scripts/patch_xenonrecomp_me1.py`.
- **SDL 3** (https://github.com/libsdl-org/SDL, zlib licence): a submodule.
- **Xenia** (https://github.com/xenia-canary/xenia-canary, BSD-3-Clause): the PC build's GPU,
  used read-only from an XDroid360 checkout (`XDROID360_ROOT`).
- **masseffect-nx** (https://github.com/nebadasSwifty/masseffect-nx, GPL-3.0): the reference port
  of the same executable (Nintendo Switch). Its documentation was used for comparison. Code
  taken from it gets credited here and in the files it lands in.

## MIT licence of the DAORecomp / SoDRecomp code

MIT License

Copyright (c) 2026 the DAORecomp contributors
Copyright (c) 2026 udarmolota (the State of Decay port)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

Third-party code keeps its own licences: ME1Recomp/thirdparty/ (SDL: Zlib; AMD Anti-Lag 2
SDK: MIT), android/app/cpp/linkernsbypass (BSD-2-Clause), and the XenonRecomp submodule (MIT,
with GPL-2.0-or-later parts in its thirdparty/disasm).
