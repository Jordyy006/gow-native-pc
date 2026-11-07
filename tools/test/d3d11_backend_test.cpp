// Direct3D 11 GS renderer smoke test (patch 59), no game or disc needed: clear, alpha-blended triangle and a textured
// sprite are drawn on a headless D3D11 device and the presented frame is checked pixel by pixel.
// Build (llvm-mingw clang++, run from engine/runtime/PS2Recomp/ps2xRuntime, i.e. with the patch applied):
//   clang++ -std=gnu++20 -Iinclude -Isrc/lib/Kernel -I<xxhash-src> <this file>
//     src/lib/gs/gpu/gs_hw_d3d11.cpp src/lib/gs/gpu/gs_texture_store.cpp src/lib/gs/gs_swizzle.cpp
//     src/lib/gs/ps2_gs_memory.cpp -ld3d11 -o d3d11_backend_test.exe
// Exit code 0 = all checks passed. GOW_GS_D3D11_DEBUG=1 prints D3D11 debug-layer messages when that Windows feature is installed.
#include "runtime/gs/gs_hw_d3d11.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
static int g_fail = 0;
static void expect(const PresentationFrame &f, int x, int y, int r, int g, int b, int tol, const char *what)
{
    const uint8_t *p = &f.pixels[((size_t)y * f.width + x) * 4];
    const bool ok = std::abs(p[0] - r) <= tol && std::abs(p[1] - g) <= tol && std::abs(p[2] - b) <= tol;
    std::printf("%s %-22s (%4d,%4d) got %3d %3d %3d  want %3d %3d %3d\n", ok ? "PASS" : "FAIL", what, x, y, p[0], p[1], p[2], r, g, b);
    if (!ok) ++g_fail;
}
int main()
{
    if (!GSHwD3D11Backend::Probe())
    {
        // No usable D3D11 device (set GOW_GS_D3D11_WARP=1 to test on the software rasteriser of a GPU-less machine).
        std::printf("SKIP: no Direct3D 11 device\n");
        return std::getenv("GOW_GS_D3D11_REQUIRED") ? 1 : 0;
    }
    std::vector<uint8_t> vram(4u << 20);
    GSHwD3D11Backend b;
    b.Initialize(vram.data(), (uint32_t)vram.size());
    GSContext c{};
    c.frame.fbp = 0; c.frame.fbw = 10; c.frame.psm = 0;
    c.scissor.x1 = 639; c.scissor.y1 = 447;
    c.zbuf.zbp = 0x1180; c.zbuf.psm = 0x31;
    c.test = (1ull << 16) | (2ull << 17);
    b.ClearFramebuffer(c, 0xFF3070A0u); // r=a0 g=70 b=30

    // 1) alpha-blended red triangle, alpha 64/128 => average of red and the clear colour
    {
        GSPrimitiveBatch pb{};
        pb.vertexCount = 3; pb.state.context = c; pb.state.prim.type = GS_PRIM_TRIANGLE; pb.state.prim.abe = true;
        pb.state.context.alpha = 0x44; // (Cs - Cd) * As + Cd
        GSVertex v[3];
        v[0].x = 400; v[0].y = 300; v[1].x = 600; v[1].y = 300; v[2].x = 400; v[2].y = 420;
        for (auto &q : v) { q.z = 1000; q.r = 255; q.g = 0; q.b = 0; q.a = 64; }
        pb.vertices = {v[0], v[1], v[2]};
        b.Submit(pb);
    }
    // 2) textured sprite: 16x16 CT32 texture, left half green, right half blue
    {
        GSTransferCommand t{};
        t.bitbltbuf.dbp = 0x2000; t.bitbltbuf.dbw = 1; t.bitbltbuf.dpsm = 0;
        t.trxreg.rrw = 16; t.trxreg.rrh = 16; t.direction = 0;
        b.BeginTransfer(t);
        std::vector<uint8_t> tex(16 * 16 * 4);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
            {
                uint8_t *p = &tex[(y * 16 + x) * 4];
                p[0] = 0; p[1] = x < 8 ? 255 : 0; p[2] = x < 8 ? 0 : 255; p[3] = 128;
            }
        b.UploadImage(tex.data(), (uint32_t)tex.size());
        GSPrimitiveBatch pb{};
        pb.state.context = c;
        pb.state.context.tex0.tbp0 = 0x2000; pb.state.context.tex0.tbw = 1; pb.state.context.tex0.psm = 0;
        pb.state.context.tex0.tw = 4; pb.state.context.tex0.th = 4; pb.state.context.tex0.tcc = 1; pb.state.context.tex0.tfx = 1;
        pb.state.prim.type = GS_PRIM_SPRITE; pb.state.prim.tme = true; pb.state.prim.fst = true;
        pb.state.textureWidth = 16; pb.state.textureHeight = 16;
        pb.vertexCount = 2;
        GSVertex v[2];
        v[0].x = 100; v[0].y = 100; v[0].u = 0; v[0].v = 0;
        v[1].x = 132; v[1].y = 132; v[1].u = 16 * 16; v[1].v = 16 * 16;
        for (auto &q : v) { q.z = 1000; q.r = 128; q.g = 128; q.b = 128; q.a = 128; }
        pb.vertices[0] = v[0]; pb.vertices[1] = v[1];
        b.Submit(pb);
    }
    GSPresentationRequest r{};
    r.pmode = 1; r.dispfb1 = (uint64_t)10 << 9; r.display1 = ((uint64_t)639 << 32) | ((uint64_t)447 << 44);
    auto f = b.Present(r);
    if (!f) { std::printf("FAIL no frame\n"); return 1; }
    expect(f, 10, 10, 0xa0, 0x70, 0x30, 1, "clear colour");
    // blend: 255*0.5 + 160*0.5 = 207, 0 + 112*0.5 = 56, 0 + 48*0.5 = 24
    expect(f, 2 * 450, 2 * 320, 207, 56, 24, 3, "alpha blend");
    expect(f, 2 * 108, 2 * 116, 0, 255, 0, 2, "texture left half");
    expect(f, 2 * 124, 2 * 116, 0, 0, 255, 2, "texture right half");
    expect(f, 2 * 140, 2 * 116, 0xa0, 0x70, 0x30, 1, "outside sprite");
    std::printf("%s\n", g_fail ? "SOME FAILED" : "ALL PASSED");
    return g_fail ? 1 : 0;
}
