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
#include <cstdlib>
#include <unistd.h>
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
    // 7. pointing + clicking through the real overlay code (no Android window on a PC, so nothing is posted)
    hookEndFrame4(12, game, 2, nullptr);
    char sp[] = "/tmp/tz_settings_XXXXXX"; int fd = mkstemp(sp); close(fd); remove(sp);
    setSettingsPath(sp, nullptr);
    state() = tzpanel::PanelState();
    show(head);                                   // panel centre (0,1.7,-1.15), 0.95 x 0.7125 m, 1x
    // where a picture pixel is in the room, for the panel as it is RIGHT NOW (size and distance can change)
    auto world = [&](float px, float py, float out[3]) {
        const unsigned char* sb = testSubmitBytes();
        out[0] = F(sb, 56) + (px / 1024.0f - 0.5f) * F(sb, 188); out[1] = F(sb, 60) + (0.5f - py / 768.0f) * F(sb, 192); out[2] = F(sb, 64);
    };
    auto sample = [&](float px, float py, float trig, bool right = true) {
        PointerSample s; std::memset(&s, 0, sizeof s);
        float w[3]; world(px, py, w);
        const int h = right ? 1 : 0;
        s.valid[h] = true; s.pose[h][3] = 1; s.pose[h][4] = w[0]; s.pose[h][5] = w[1]; s.pose[h][6] = -0.3f;   // pointing straight at the spot
        s.trigger[h] = trig; s.trigger[1 - h] = 0;
        return s;
    };
    PointerSample both; std::memset(&both, 0, sizeof both); both.trigger[0] = both.trigger[1] = 0.95f;
    pointer(1.0, both, nullptr);                                 // opening gesture still held
    PointerResult pr = pointer(1.02, sample(155, 213, 0.9f), nullptr);   // pull the trigger on "Favorites" - too early
    std::printf("T7a tab=%d (still held, must stay 0)\n", state().tab);
    pointer(1.04, sample(155, 213, 0.0f), nullptr);              // let go: armed
    pr = pointer(1.06, sample(155, 213, 0.9f), nullptr);         // now click Favorites
    std::printf("T7b tab=%d clicked=%d hover=%d\n", state().tab, pr.clickedId, state().hover);
    pointer(1.08, sample(155, 213, 0.0f), nullptr);
    pointer(1.10, sample(644, 240, 0.0f), nullptr);              // go back to Settings first
    pointer(1.12, sample(155, 136 + 23, 0.9f), nullptr); pointer(1.14, sample(155, 136 + 23, 0.0f), nullptr);
    pr = pointer(1.16, sample(644, 240, 0.9f), nullptr);         // Sound effects
    pointer(1.18, sample(644, 240, 0.0f), nullptr);
    std::printf("T7c tab=%d sound=%d\n", state().tab, state().sound ? 1 : 0);
    FILE* sf = fopen(sp, "r"); char sb[128] = {0}; if (sf) { size_t n = fread(sb, 1, sizeof sb - 1, sf); sb[n] = 0; fclose(sf); }
    std::printf("T7d saved=[%s]\n", sb);
    pointer(1.20, sample(952, 70, 0.0f), nullptr);
    pr = pointer(1.22, sample(952, 70, 0.9f), nullptr);
    std::printf("T7e close=%d\n", pr.close ? 1 : 0);
    pointer(1.24, sample(952, 70, 0.0f), nullptr);
    // colour list: click "Red"
    pointer(1.26, sample(518, 312, 0.0f), nullptr);
    pointer(1.28, sample(518, 312, 0.9f), nullptr);
    pointer(1.29, sample(518, 312, 0.0f), nullptr);
    std::printf("T7c2 colour=%d (%s)\n", state().colorIndex, tzpanel::colorChoice(state().colorIndex).name);
    // size slider: drag to the far right, nothing changes size until let go
    pointer(1.30, sample(700, 408, 0.0f), nullptr);
    pointer(1.32, sample(700, 408, 0.9f), nullptr);
    pointer(1.34, sample(900, 408, 0.9f), nullptr);
    hookEndFrame4(13, game, 2, nullptr); sub = testSubmitBytes();
    std::printf("T7f during drag scale=%.2f panel width=%.4f\n", state().scale, F(sub, 188));
    pointer(1.36, sample(900, 408, 0.0f), nullptr);
    hookEndFrame4(14, game, 2, nullptr); sub = testSubmitBytes();
    std::printf("T7g after let go scale=%.2f panel width=%.4f\n", state().scale, F(sub, 188));
    // transparency slider: far right = 0.25, the panel does not move
    pointer(1.40, sample(700, 468, 0.0f), nullptr);
    pointer(1.42, sample(700, 468, 0.9f), nullptr);
    pointer(1.44, sample(900, 468, 0.9f), nullptr);
    pointer(1.46, sample(900, 468, 0.0f), nullptr);
    std::printf("T7i transparency=%.2f\n", state().transparency);
    // distance slider: far right = 2.0 m; the panel moves only when let go
    pointer(1.50, sample(700, 528, 0.0f), nullptr);
    pointer(1.52, sample(700, 528, 0.9f), nullptr);
    pointer(1.54, sample(900, 528, 0.9f), nullptr);
    hookEndFrame4(15, game, 2, nullptr); sub = testSubmitBytes();
    std::printf("T7j during drag distance=%.2f panel z=%.3f\n", state().distance, F(sub, 64));
    pointer(1.56, sample(900, 528, 0.0f), nullptr);
    hookEndFrame4(16, game, 2, nullptr); sub = testSubmitBytes();
    std::printf("T7k after let go distance=%.2f panel z=%.3f\n", state().distance, F(sub, 64));
    // stage D7: the Basketball page, through the real pointer path: tab, switch, slider, scan button; read back the way the payload does
    {
        std::printf("T7m default aimAsk on=%d cap=%.0f shotScan=%d\n", aimAsk().on ? 1 : 0, aimAsk().capM, takeShotScanRequest() ? 1 : 0);
        tzpanel::PanelState tmp; tmp.tab = 4; std::vector<tzpanel::HitRect> hh; { tzpanel::Canvas cv(1024, 768); tzpanel::drawPanel(cv, tmp, &hh); }
        auto at = [&](int id, float* x, float* y) { for (auto& r : hh) if (r.id == id) { *x = r.x + r.w / 2; *y = r.y + r.h / 2; return true; } return false; };
        auto edge = [&](int id, bool left, float* x, float* y) { for (auto& r : hh) if (r.id == id) { *x = left ? r.x + 16 : r.x + r.w - 16; *y = r.y + r.h / 2; return true; } return false; };
        float x = 0, y = 0; double t = 2.0;
        auto click = [&](int id) { if (!at(id, &x, &y)) return false; pointer(t, sample(x, y, 0.0f), nullptr); t += 0.02; pointer(t, sample(x, y, 0.9f), nullptr); t += 0.02; pointer(t, sample(x, y, 0.0f), nullptr); t += 0.02; return true; };
        click(tzpanel::HIT_TAB0 + 4);
        std::printf("T7n tab=%d\n", state().tab);
        click(tzpanel::HIT_TOGGLE_AIM);
        std::printf("T7o aimAsk on=%d cap=%.0f\n", aimAsk().on ? 1 : 0, aimAsk().capM);
        float lx, ly, rx, ry;                                      // drag the slider to the far left, let go
        edge(tzpanel::HIT_SLIDER_AIMCAP, true, &lx, &ly); edge(tzpanel::HIT_SLIDER_AIMCAP, false, &rx, &ry);
        pointer(t, sample(rx, ry, 0.0f), nullptr); t += 0.02; pointer(t, sample(rx, ry, 0.9f), nullptr); t += 0.02;
        pointer(t, sample(lx, ly, 0.9f), nullptr); t += 0.02; pointer(t, sample(lx, ly, 0.0f), nullptr); t += 0.02;
        std::printf("T7p aimAsk on=%d cap=%.0f\n", aimAsk().on ? 1 : 0, aimAsk().capM);
        pointer(t, sample(lx, ly, 0.0f), nullptr); t += 0.02; pointer(t, sample(lx, ly, 0.9f), nullptr); t += 0.02;
        pointer(t, sample(rx, ry, 0.9f), nullptr); t += 0.02; pointer(t, sample(rx, ry, 0.0f), nullptr); t += 0.02;
        std::printf("T7q aimAsk on=%d cap=%.0f\n", aimAsk().on ? 1 : 0, aimAsk().capM);
        click(tzpanel::HIT_SCAN_SHOT);
        const bool a = takeShotScanRequest(), b = takeShotScanRequest(), c = takeScanRequest();
        std::printf("T7r shotScan first=%d second=%d movementScan=%d scanState=%d\n", a ? 1 : 0, b ? 1 : 0, c ? 1 : 0, state().scanState);
        click(tzpanel::HIT_TOGGLE_AIM);
        std::printf("T7s aimAsk on=%d\n", aimAsk().on ? 1 : 0);
        state().tab = 0; state().aimOn = false; state().aimCapM = 50; state().scanState = 0; markDirty();
    }
    // the dot is not shown when pointing away; sample with invalid hands is harmless
    PointerSample none; std::memset(&none, 0, sizeof none);
    pr = pointer(1.4, none, nullptr);
    std::printf("T7h onMenu=%d\n", pr.onMenu ? 1 : 0);
    // 8. hiding the game's button presses while the menu is open
    unsigned char st[128]; std::memset(st, 0xAB, sizeof st);
    filterControllerState(0, st);
    std::printf("T8a open: buttons=%02x%02x triggers=%02x sticks=%02x connected=%02x tail=%02x\n", st[4], st[5], st[16], st[40], st[0], st[100]);
    hide();
    std::memset(st, 0xAB, sizeof st); filterControllerState(0, st);
    std::printf("T8b just closed (grace): buttons=%02x\n", st[4]);
    usleep(600 * 1000);
    std::memset(st, 0xAB, sizeof st); filterControllerState(0, st);
    std::printf("T8c later: buttons=%02x\n", st[4]);
    std::memset(st, 0xAB, sizeof st); filterControllerState(-3, st);
    std::printf("T8d failed call is left alone: buttons=%02x\n", st[4]);
    // 9. settings file is read back
    state() = tzpanel::PanelState(); setSettingsPath(sp, nullptr);
    std::printf("T9 reloaded sound=%d size=%.2f colour=%d transparency=%.2f distance=%.2f\n", state().sound ? 1 : 0, state().scale, state().colorIndex, state().transparency, state().distance);
    remove(sp);
    return 0;
}
C
sed -i 's/\\\\n/\\n/g' "$W/t.cpp"
g++ -std=c++17 -DTZ_PC_TEST -I"$HERE/fake_jni" -I"$HERE/../src" "$W/t.cpp" "$HERE/../src/overlay.cpp" "$HERE/../src/pointer.cpp" "$HERE/../src/movement.cpp" "$HERE/../src/il2cpp_scan.cpp" "$HERE/../src/panel.cpp" "$HERE/../src/panel_font.cpp" -ldl -pthread -o "$W/t" || exit 2
out=$("$W/t"); echo "$out"
check "closed menu: call passes through untouched" 'grep -q "^T1 rc=0 count=2 same=1" <<<"$out"'
check "open menu: one extra layer added at the END, game layers kept" 'grep -q "^T2 rc=0 count=3 last=1 id=7 stage=0 vp=0,0,1024,768" <<<"$out" && grep -q "T2 game layers kept=1" <<<"$out"'
check "panel sits 1.15 m ahead of a head looking at -Z, facing back" 'grep -q "T2 pose q=(0.000 0.000 0.000 1.000) p=(0.000 1.700 -1.150) size=(0.9500 0.7125) color=1.0" <<<"$out"'
check "head turned left: panel moves to -X and turns 90 degrees" 'grep -q "T3 pose q=(0.000 0.707 0.000 0.707) p=(-0.150 1.500 2.000)" <<<"$out"'
check "menu size 1.5x makes the panel 1.5x bigger" 'grep -q "T4 size=(1.4250 1.0687)" <<<"$out"'
check "closing the menu stops adding the layer" 'grep -q "^T5 count=2" <<<"$out"'
check "if Meta refuses the layer, the game frame is re-sent without it (rc 0, 2 calls)" 'grep -q "^T6 rc=0 calls=2 lastCount=2 firstBad=-5 broken=1" <<<"$out"'
check "pull on a button while the opening triggers are still held does nothing" 'grep -q "^T7a tab=0" <<<"$out"'
check "pointing the real controller line at 'Favorites' and pulling clicks it" 'grep -q "^T7b tab=1 clicked=11 hover=11" <<<"$out"'
check "sound toggle works and the setting is written to the file" 'grep -q "^T7c tab=0 sound=0" <<<"$out" && grep -q "T7d saved=\[sound=0" <<<"$out"'
check "X button asks for the menu to close" 'grep -q "^T7e close=1" <<<"$out"'
check "clicking Red in the colour list selects it" 'grep -q "^T7c2 colour=1 (Red)" <<<"$out"'
check "while dragging the size slider the panel keeps its size" 'grep -q "^T7f during drag scale=1.50 panel width=0.9500" <<<"$out"'
check "letting go of the slider resizes the panel (1.5x)" 'grep -q "^T7g after let go scale=1.50 panel width=1.4250" <<<"$out"'
check "transparency slider reaches exactly 0.25" 'grep -q "^T7i transparency=0.25" <<<"$out"'
check "distance slider: panel stays put while dragging" 'grep -q "^T7j during drag distance=2.00 panel z=-1.150" <<<"$out"'
check "distance slider: panel moves to 2 m when let go" 'grep -q "^T7k after let go distance=2.00 panel z=-2.000" <<<"$out"'
check "Basketball page: the Aimbot starts off at 50 (Unlimited) and no scan is waiting" 'grep -q "^T7m default aimAsk on=0 cap=50 shotScan=0" <<<"$out"'
check "Basketball page: the sidebar entry opens it" 'grep -q "^T7n tab=4" <<<"$out"'
check "Basketball page: the switch turns the aimbot on (read back through aimAsk)" 'grep -q "^T7o aimAsk on=1 cap=50" <<<"$out"'
check "Basketball page: dragging the slider to the far left gives 5 m" 'grep -q "^T7p aimAsk on=1 cap=5$" <<<"$out"'
check "Basketball page: dragging to the far right gives 50 m (Unlimited)" 'grep -q "^T7q aimAsk on=1 cap=50$" <<<"$out"'
check "Basketball page: the scan button asks for ONE ball scan and no movement scan" 'grep -q "^T7r shotScan first=1 second=0 movementScan=0 scanState=1" <<<"$out"'
check "Basketball page: the switch turns off again" 'grep -q "^T7s aimAsk on=0" <<<"$out"'
check "no hand data: no dot" 'grep -q "^T7h onMenu=0" <<<"$out"'
check "menu open: the game sees blanked buttons/triggers/sticks, connection byte kept" 'grep -q "^T8a open: buttons=0000 triggers=00 sticks=00 connected=ab tail=ab" <<<"$out"'
check "just after closing: still blanked for a moment" 'grep -q "^T8b just closed (grace): buttons=00" <<<"$out"'
check "later: the game sees real buttons again" 'grep -q "^T8c later: buttons=ab" <<<"$out"'
check "a failed call is not touched" 'grep -q "^T8d failed call is left alone: buttons=ab" <<<"$out"'
check "saved settings are read back after a restart" 'grep -q "^T9 reloaded sound=0 size=1.50 colour=1 transparency=0.25 distance=2.00" <<<"$out"'
check "after a refusal the panel stays off" 'grep -q "^T6b rc=0 calls=1 lastCount=2" <<<"$out"'
echo; echo "passed: $pass  failed: $failn"; [ "$failn" -eq 0 ]
