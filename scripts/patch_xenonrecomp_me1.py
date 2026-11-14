"""Adds the VMX instructions Mass Effect needs that the XDroid360 games' shared recompiler patch
(scripts/patch_xenonrecomp.py) does not have yet. Run it after that script. Meant to move into
the shared patch script (DAORecomp fork, scripts/patch_recompiler.py) once checked.

ME1's default.xex has 202 sites: vandc 157, vnor 27, vpkuhus128 6, vpkuwus128 6, vctuxs 6.
Idempotent (looks for its marker).

usage: python scripts/patch_xenonrecomp_me1.py [<XenonRecomp checkout>]   (default: ./XenonRecomp)
"""
import sys
from pathlib import Path

MARKER = '    // ---- ME1 additions ----\n'
CASES = MARKER + r'''    case PPC_INST_VANDC: // as VANDC128: vD = vA & ~vB
        println("\tsimde_mm_store_si128((simde__m128i*){}.u8, simde_mm_andnot_si128(simde_mm_load_si128((simde__m128i*){}.u8), simde_mm_load_si128((simde__m128i*){}.u8)));", v(insn.operands[0]), v(insn.operands[2]), v(insn.operands[1]));
        break;

    case PPC_INST_VNOR: // as VNOR128
        println("\tsimde_mm_store_si128((simde__m128i*){}.u8, simde_mm_xor_si128(simde_mm_or_si128(simde_mm_load_si128((simde__m128i*){}.u8), simde_mm_load_si128((simde__m128i*){}.u8)), simde_mm_set1_epi32(-1)));", v(insn.operands[0]), v(insn.operands[1]), v(insn.operands[2]));
        break;

    case PPC_INST_VPKUHUS128: // u16 -> u8 with unsigned saturation; vectors are stored reversed, so vA's half goes second
        println("\tsimde_mm_store_si128((simde__m128i*){}.u8, simde_mm_packus_epi16(simde_mm_min_epu16(simde_mm_load_si128((simde__m128i*){}.u16), simde_mm_set1_epi16(255)), simde_mm_min_epu16(simde_mm_load_si128((simde__m128i*){}.u16), simde_mm_set1_epi16(255))));", v(insn.operands[0]), v(insn.operands[2]), v(insn.operands[1]));
        break;

    case PPC_INST_VPKUWUS128: // u32 -> u16 with unsigned saturation
        println("\tsimde_mm_store_si128((simde__m128i*){}.u16, simde_mm_packus_epi32(simde_mm_min_epu32(simde_mm_load_si128((simde__m128i*){}.u32), simde_mm_set1_epi32(65535)), simde_mm_min_epu32(simde_mm_load_si128((simde__m128i*){}.u32), simde_mm_set1_epi32(65535))));", v(insn.operands[0]), v(insn.operands[2]), v(insn.operands[1]));
        break;

    case PPC_INST_VCTUXS: // float -> u32 with unsigned saturation, scaled by 2^uimm; NaN -> 0
        println("\t{{ PPCVRegister t; for (int i = 0; i < 4; i++) {{ float x = {}.f32[i] * {}.0f; t.u32[i] = (x != x || x <= 0.0f) ? 0u : (x >= 4294967296.0f ? 0xFFFFFFFFu : uint32_t(x)); }} {} = t; }}", v(insn.operands[1]), 1u << insn.operands[2], v(insn.operands[0]));
        break;

'''
ANCHOR = '    case PPC_INST_VAND:\n'


def main():
    root = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / 'XenonRecomp')
    path = root / 'XenonRecomp' / 'recompiler.cpp'
    text = path.read_text(encoding='utf-8')
    if MARKER in text:
        print(f'already patched: {path}')
        return
    if text.count(ANCHOR) != 1:
        sys.exit(f'{path}: anchor "case PPC_INST_VAND:" not found exactly once')
    path.write_text(text.replace(ANCHOR, CASES + ANCHOR), encoding='utf-8')
    print(f'patched {path} (ME1 additions)')


if __name__ == '__main__':
    main()
