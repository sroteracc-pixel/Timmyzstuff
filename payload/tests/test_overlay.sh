#!/bin/bash
# PC test of the overlay's frame-hook logic (NOT the Android surface part - that can only be tested on the headset).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
pass=0; failn=0
check() { if eval "$2"; then pass=$((pass+1)); echo "  PASS  $1"; else failn=$((failn+1)); echo "  FAIL  $1"; fi; }
cat > "$W/t.cpp" <<'C'
#include <cmath>
#include <cstdio>
#include <cstring>
#include "overlay.h"
static int g_lastCount = -1; static const void* const* g_lastLayers = nullptr; static int g_calls = 0; static int g_failWhenCount = -99;
static int fakeReal(int frame, const void* const* layers, int count, void* extra) {
    ++g_calls; g_lastCount = count; g_lastLayers = layers; (void)frame; (void)extra;
    return count == g_failWhenCount ? -5 : 0;
}
static float F(const unsigned char* b, int off) { float f; std::memcpy(&f, b + off, 4); return f; }
static int I(const unsigned char* b, int off) { int v; std::memcpy(&v, b + off, 4); return v; }
int main() {
    using namespace tzoverlay;
    setRealEndFrame4(reinterpret_cast<uint64_t>(&fakeReal));
    const void* game[2] = {(const void*)0x1111, (const void*)0x2222};
    // 1. menu closed: untouched
    int rc = hookEndFrame4(5, game, 2, nullptr);
    std::printf("T1 rc=%d count=%d same=%d\n", rc, g_lastCount, g_lastLayers == game);
    // 2. open, head at (0,1.7,0) looking straight ahead (-Z)
    testForceReady(7, 65);
    float head[7] = {0, 0, 0, 1, 0, 1.7f, 0};
    show(head);
    rc = hookEndFrame4(6, game, 2, nullptr);
    const unsigned char* sub = testSubmitBytes();
    std::printf("T2 rc=%d count=%d last=%d id=%d stage=%d vp=%d,%d,%d,%d\n", rc, g_lastCount, g_lastLayers[2] == sub, I(sub, 0), I(sub, 4), I(sub, 8), I(sub, 12), I(sub, 16), I(sub, 20));
    std::printf("T2 game layers kept=%d\n", g_lastLayers[0] == game[0] && g_lastLayers[1] == game[1]);
    std::printf("T2 pose q=(%.3f %.3f %.3f %.3f) p=(%.3f %.3f %.3f) size=(%.4f %.4f) color=%.1f\n", F(sub,40), F(sub,44), F(sub,48), F(sub,52), F(sub,56), F(sub,60), F(sub,64), F(sub,188), F(sub,192), F(sub,72));
    // 3. head turned 90 degrees left (looking along -X)
    float s45 = std::sin(3.14159265f / 4), c45 = std::cos(3.14159265f / 4);
    float head2[7] = {0, s45, 0, c45, 1.0f, 1.5f, 2.0f};
    show(head2);
    hookEndFrame4(7, game, 2, nullptr);
    sub = testSubmitBytes();
    std::printf("T3 pose q=(%.3f %.3f %.3f %.3f) p=(%.3f %.3f %.3f)\\n", F(sub,40), F(sub,44), F(sub,48), F(sub,52), F(sub,56), F(sub,60), F(sub,64));
    // 4. scale 1.5 changes the size
    state().scale = 1.5f; show(head);
    hookEndFrame4(8, game, 2, nullptr); sub = testSubmitBytes();
    std::printf("T4 size=(%.4f %.4f)\\n", F(sub,188), F(sub,192));
    // 5. close
    hide(); g_lastCount = -1; hookEndFrame4(9, game, 2, nullptr);
    std::printf("T5 count=%d\\n", g_lastCount);
    // 6. Meta refuses our layer: the game's frame must still go out, and the panel switches itself off
    show(head); g_failWhenCount = 3; g_calls = 0;
    rc = hookEndFrame4(10, game, 2, nullptr);
    std::printf("T6 rc=%d calls=%d lastCount=%d firstBad=%d broken=%d\\n", rc, g_calls, g_lastCount, stats().firstBadRc, stats().broken);
    g_calls = 0; rc = hookEndFrame4(11, game, 2, nullptr);
    std::printf("T6b rc=%d calls=%d lastCount=%d\\n", rc, g_calls, g_lastCount);
    // 7. older SDK (minor 40): size lives at byte 176
    return 0;
}
C
sed -i 's/\\\\n/\\n/g' "$W/t.cpp"
g++ -std=c++17 -DTZ_PC_TEST -I"$HERE/fake_jni" -I"$HERE/../src" "$W/t.cpp" "$HERE/../src/overlay.cpp" "$HERE/../src/panel.cpp" "$HERE/../src/panel_font.cpp" -ldl -pthread -o "$W/t" || exit 2
out=$("$W/t"); echo "$out"
check "closed menu: call passes through untouched" 'grep -q "^T1 rc=0 count=2 same=1" <<<"$out"'
check "open menu: one extra layer added at the END, game layers kept" 'grep -q "^T2 rc=0 count=3 last=1 id=7 stage=0 vp=0,0,1024,768" <<<"$out" && grep -q "T2 game layers kept=1" <<<"$out"'
check "panel sits 1.15 m ahead of a head looking at -Z, facing back" 'grep -q "T2 pose q=(0.000 0.000 0.000 1.000) p=(0.000 1.700 -1.150) size=(0.9500 0.7125) color=1.0" <<<"$out"'
check "head turned left: panel moves to -X and turns 90 degrees" 'grep -q "T3 pose q=(0.000 0.707 0.000 0.707) p=(-0.150 1.500 2.000)" <<<"$out"'
check "menu size 1.5x makes the panel 1.5x bigger" 'grep -q "T4 size=(1.4250 1.0687)" <<<"$out"'
check "closing the menu stops adding the layer" 'grep -q "^T5 count=2" <<<"$out"'
check "if Meta refuses the layer, the game frame is re-sent without it (rc 0, 2 calls)" 'grep -q "^T6 rc=0 calls=2 lastCount=2 firstBad=-5 broken=1" <<<"$out"'
check "after a refusal the panel stays off" 'grep -q "^T6b rc=0 calls=1 lastCount=2" <<<"$out"'
echo; echo "passed: $pass  failed: $failn"; [ "$failn" -eq 0 ]
