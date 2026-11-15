// The native renderer's hooks in the game's Direct3D (XDroid360, only in a build with the renderer:
// XDROID360_NATIVE_RENDERER). The renderer draws from the GPU ring, and two things it can't read
// there come from the game thread (native-renderer/src/nfsmw_nativo_ganchos.h):
//   - which shader container each D3D shader object was made from (the microcode in the ring is
//     already patched by D3D, so it can't be looked up by content);
//   - which shaders each Draw* had bound, with its primitive type and count, so the ring can pair
//     the draw with its shaders.
// (ME1) Mass Effect's XDK Direct3D (2.0.4802, at 0x8221F000-0x8223FFFF), found by matching State of
// Decay's functions instruction by instruction (me1/scripts/match_d3d.py) and by their arguments in
// the generated code; the addresses agree with masseffect-nx's (RU edition, me_d3d_trace.cpp):
//   DrawVertices sub_82227F68 (dev, prim, start, count), DrawIndexedVertices sub_82228358 (dev, prim,
//     base, start, count): 0.57 and 0.53 against SoD's;
//   BeginVertices sub_82227550 (dev, prim, count, stride; DrawVerticesUP sub_822279E8 calls it and
//     copies the data) as the UP draw, BeginIndexedVertices sub_82227A30 (dev, prim, base, vertices,
//     index count, index format, stride) as the indexed UP draw: count in r5 and r7, as the
//     renderer reads them;
//   SetVertexShader sub_822197E0 (0.70) stores the shader at device +0x3080, SetPixelShader
//     sub_82219528 (0.72) at +0x307C.
// Unreal Engine 3 builds its shader objects in place from its shader cache, so they never pass
// through D3D's creation function (sub_82219418): the container is identified when the shader is
// set, from the object itself. A vertex shader keeps a copy of its container's virtual part at
// +0x368 and points to its microcode at [+0x20], a pixel shader at +0x28 and [+0x18] (as
// masseffect-nx found). D3D patches a vertex shader's microcode in place the first time it binds
// it to a vertex declaration (sub_8222F640, r3 the vertex shader, r7 the pixel shader), so that
// one is hooked too and identifies both before the original runs.
#include "stdafx.h"

#ifdef XDROID360_NATIVE_RENDERER

#include "kernel/function.h"
#include "kernel/game_build.h"
#include "kernel/memory.h"
#include "kernel/vfs.h"

#include "nfsmw_nativo_ganchos.h"

#include <fstream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace
{
    namespace nativo = nfsmw::nativo;

    constexpr uint32_t kBoundVertexShader = 0x3080;
    constexpr uint32_t kBoundPixelShader = 0x307C;

    // Where a shader object keeps its container (see above).
    constexpr uint32_t kVsVirtualPart = 0x368, kVsMicrocode = 0x20;
    constexpr uint32_t kPsVirtualPart = 0x28, kPsMicrocode = 0x18;
    constexpr uint32_t kMaxContainer = 0x10000; // the renderer's own limit

    // The device offsets, set once when the library loads (before the game makes a device).
    struct ConfigureDevice
    {
        ConfigureDevice() { nativo::ConfigurarDispositivoD3D(kBoundVertexShader, kBoundPixelShader); }
    } s_configureDevice;

    std::mutex s_mutex;
    // Object -> the microcode it pointed to when it was identified: a new shader made at a freed
    // object's address points elsewhere, so it is identified again.
    std::unordered_map<uint32_t, uint32_t> s_identified[2];
    uint32_t s_scratch = 0; // guest buffer the container is put together in (under s_mutex)

    // A container the shader library doesn't have: the shaders Direct3D and Scaleform build while
    // the game runs (video, UI), which aren't on the disc. Each is written once to
    // userdata/runtime_shaders/{vs,ps}_<FNV-1a 64 of the container>.bin, the naming of
    // me1/scripts/ue3_shaders.py, so the next library build can take them in. Under s_mutex.
    std::unordered_set<uint64_t> s_saved;

    void SaveUnknown(const uint8_t* container, size_t size, bool vertex)
    {
        uint64_t hash = 1469598103934665603ull;
        for (size_t i = 0; i < size; i++)
            hash = (hash ^ container[i]) * 1099511628211ull;
        if (!s_saved.insert(hash).second)
            return;
        const auto folder = me1::kernel::VfsUserRoot() / "runtime_shaders";
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        const auto path = folder / fmt::format("{}_{:016x}.bin", vertex ? "vs" : "ps", hash);
        if (std::filesystem::exists(path, error))
            return;
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(container), std::streamsize(size));
        LOGI("Native renderer: saved the unknown {} shader {}", vertex ? "vertex" : "pixel", path.filename().string());
    }

    void Identify(uint8_t* base, uint32_t object, bool vertex)
    {
        if (!object)
            return;
        const uint32_t part = object + (vertex ? kVsVirtualPart : kPsVirtualPart);
        const uint32_t signature = PPC_LOAD_U32(part);
        if ((signature & ~1u) != 0x102A1100u || (signature & 1) != uint32_t(vertex))
            return;
        const uint32_t virtualSize = PPC_LOAD_U32(part + 4);
        const uint32_t physicalSize = PPC_LOAD_U32(part + 8);
        const uint32_t microcode = PPC_LOAD_U32(object + (vertex ? kVsMicrocode : kPsMicrocode));
        if (virtualSize < 24 || !microcode || uint64_t(virtualSize) + physicalSize > kMaxContainer)
            return;

        std::lock_guard lock(s_mutex);
        auto& seen = s_identified[vertex];
        if (const auto it = seen.find(object); it != seen.end() && it->second == microcode)
            return;
        if (!s_scratch)
            s_scratch = me1::kernel::KernelAlloc(kMaxContainer, 16);
        if (!s_scratch)
            return;
        memcpy(base + s_scratch, base + part, virtualSize);
        memcpy(base + s_scratch + virtualSize, base + microcode, physicalSize);
        const auto* entry = nativo::ganchos_detalle::IdentificarCreacion(base, s_scratch, vertex);
        nativo::ganchos_detalle::RecordarCreacion(object, entry, vertex);
        seen[object] = microcode;
        if (!entry)
            SaveUnknown(base + s_scratch, virtualSize + physicalSize, vertex);
    }

    // r3 the device, r4 the shader.
    template <bool kVertex>
    void SetShaderHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        Identify(base, ctx.r4.u32, kVertex);
        original(ctx, base);
    }

    // D3D's first bind of a vertex shader to a declaration: r3 the vertex shader, r7 the pixel shader.
    void FirstBindHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        Identify(base, ctx.r3.u32, true);
        Identify(base, ctx.r7.u32, false);
        original(ctx, base);
    }

    // r3 the device; r4..r7 as the call has them (the renderer reads type and count from them).
    template <nativo::FuncionDibujo kFunction>
    void DrawHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        nativo::AnotarDibujo(kFunction, base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32);
        original(ctx, base);
        // A record its FlushState didn't take goes to the queue.
        nativo::TerminarDibujo();
    }

    void SetVertexShaderHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        SetShaderHook<true>(ctx, base, original);
    }
    void SetPixelShaderHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        SetShaderHook<false>(ctx, base, original);
    }
    void DrawVerticesHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        DrawHook<nativo::FuncionDibujo::kVertices>(ctx, base, original);
    }
    void DrawIndexedVerticesHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        DrawHook<nativo::FuncionDibujo::kIndexados>(ctx, base, original);
    }
    void BeginVerticesHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        DrawHook<nativo::FuncionDibujo::kVerticesUP>(ctx, base, original);
    }
    void BeginIndexedVerticesHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        DrawHook<nativo::FuncionDibujo::kIndexadosUP>(ctx, base, original);
    }
}

namespace me1::gpu::native
{
    bool Active(); // android/runtime/native_host.cpp: the native renderer runs (not Xenia)
}

namespace
{
    // (ME1) Unreal Engine 3's switch for 2x MSAA with predicated tiling: a word in the data section
    // (1 on the disc; D3D's init sets it to 0 itself in SD video modes). At 1280x720 the game draws
    // its scene in two tiles (512 + 208 lines) with predicated packets, which the EDRAM-free renderer
    // doesn't put back together: the bottom strip came out misplaced and blown out on the S25. With
    // it 0 the scene is one tile, without the 2x MSAA edges. masseffect-nx does the same
    // (masseffect_game_msaa = 0, me_render_ring.cpp); Xenia's ME1 patch "Black Shading Fix" writes
    // the same word. Only with the native renderer: Xenia emulates the tiles.
    constexpr uint32_t kMsaaTiling = 0x82E5DCD4;

    // sub_82D3BDF0 runs in the game's static initializers (it sets up the render command buffer),
    // before the D3D init reads or writes the word.
    void RenderBufferInitHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        if (me1::gpu::native::Active())
        {
            PPC_STORE_U32(kMsaaTiling, 0);
            LOGI("Native renderer: the game's MSAA and predicated tiling off");
        }
        original(ctx, base);
    }
}

GAME_HOOK_ONE(sub_82D3BDF0, RenderBufferInitHook)
GAME_HOOK_ONE(sub_822197E0, SetVertexShaderHook)
GAME_HOOK_ONE(sub_82219528, SetPixelShaderHook)
GAME_HOOK_ONE(sub_8222F640, FirstBindHook)
GAME_HOOK_ONE(sub_82227F68, DrawVerticesHook)
GAME_HOOK_ONE(sub_82228358, DrawIndexedVerticesHook)
GAME_HOOK_ONE(sub_82227550, BeginVerticesHook)
GAME_HOOK_ONE(sub_82227A30, BeginIndexedVerticesHook)

#endif // XDROID360_NATIVE_RENDERER
