#pragma once
// ============================================================================
// AutoCrash.h —— 自动坠机（孽蜥飞天技能自动下坠）
// ============================================================================
//
// 原理：孽蜥飞天时，如果与最近求生水平距离 <= 阈值，自动点下坠键。
//   技能ID：一阶 2214 / 2203，二阶 2201 / 2203
//   触发：last_cast_skill_id 匹配 + 距离满足 → 点一次
//   每次飞天只触发一次（出技能后重置）
//
// 触摸：优先 paradise (KMA)，失败用 touchhelpA (虚拟手指)
//
// ⚠ 依赖：Touch::* / touch_down / touch_up / ::Vector2 需调用方先 include
// ============================================================================

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <time.h>
#include <unistd.h>

#include "PyCore.h"
#include "PyAttack.h"
#include "PySelf.h"
#include "imgui.h"
#include "theme.h"
#include "fang_ui.h"
#include "kerneldriver-paradise.hpp"   // touch_down / touch_up / ::Vector2
#include "TouchHelperA.h"              // Touch::VirtualDown / VirtualUp / ScreenToBase

// 屏幕尺寸（拖动边界用）
extern android::ANativeWindowCreator::DisplayInfo displayInfo;

namespace AutoCrash {

// ============================================================================
// 配置
// ============================================================================
inline bool  g_on       = false;
inline float g_dist_m   = 2.0f;
inline float g_tap_x    = 0.0f;
inline float g_tap_y    = 0.0f;
inline int   g_hold_ms  = 60;

//   孽蜥飞天技能ID：一阶 2214 / 2203，二阶 2201 / 2203
inline const int kFlySkillIds[] = { 2214, 2203, 2201 };
inline const int kFlySkillN = 3;

//   触摸slot（避开自动技能6/7、盖板8、探针9）
inline const int kSlot = 5;

// ============================================================================
// ★★ 2026-10-17 新增：触摸区显示（用户要求）★★
//   ⚠ 语义说明：g_tap_x/g_tap_y 是**点击中心点**（配置项 ac.tap_x/ac.tap_y
//     保持原语义不变 —— 老用户的坐标不会错位）✓
//     触摸区矩形以它为**中心**，宽高由 g_rect_w/g_rect_h 决定 ✓
// ============================================================================
inline bool  g_show_rect    = false;    // 是否显示触摸区
inline float g_rect_w       = 100.f;    // 框宽（像素）
inline float g_rect_h       = 100.f;    // 框高（像素）
inline bool  g_rect_inited  = false;
inline bool  g_rect_dragging = false;
inline float g_drag_dx      = 0.f;
inline float g_drag_dy      = 0.f;

// ============================================================================
// 运行时状态
// ============================================================================
inline bool g_triggered = false;

// ============================================================================
// 拿场景对象和世界坐标
// ============================================================================
inline bool get_scene_pos(uint64_t unit, float& x, float& y, float& z) {
    if (!PyCore::is_obj(unit)) return false;
    const uint64_t model = PyAttack::get_obj_field(unit, "model");
    if (!PyCore::is_obj(model)) return false;
    const uint64_t scene = PyCore::get_scene_obj(model);
    if (!PyCore::is_obj(scene)) return false;
    uint64_t cr = getPtr64(scene + 0x28);
    if ((cr >> 48) == 0xB400) cr &= 0x0000FFFFFFFFFFFFULL;
    if (!PyCore::is_obj(cr)) return false;
    float p[3] = {0, 0, 0};
    if (!vm_readv(cr + 0xa0, p, 12)) return false;
    x = p[0]; y = p[1]; z = p[2];
    return true;
}

//   到最近求生的**水平**距离（米）—— 飞天时高差太大，只算水平
inline float nearest_surv_hdist() {
    uint64_t units[64] = {};
    const int n = PyAttack::get_all_units_of_type(2, units, 64);
    if (n <= 0) return -1.0f;

    const uint64_t hu = PyAttack::hunter_unit_raw();
    float sx = 0, sy = 0, sz = 0;
    if (!get_scene_pos(hu, sx, sy, sz)) return -1.0f;

    float best = 1e9f;
    for (int i = 0; i < n; i++) {
        if (!PyCore::is_obj(units[i])) continue;
        float x, y, z;
        if (!get_scene_pos(units[i], x, y, z)) continue;
        const float dx = x - sx;
        const float dz = z - sz;
        const float d = sqrtf(dx * dx + dz * dz) / 11.886f;
        if (d < best) best = d;
    }
    return best < 1e8f ? best : -1.0f;
}

// ============================================================================
// 触发点击：优先 paradise (KMA)，失败用 touchhelpA (虚拟手指)
// ============================================================================
inline bool do_crash_tap() {
    if (g_tap_x <= 0.f || g_tap_y <= 0.f) return false;

    const float cx = g_tap_x;
    const float cy = g_tap_y;

    // ★★ 2026-10-17 改：与自动砸板/AutoSkill 完全一致 —— **虚拟手指优先** ★★
    //   原版是"KMA 优先，虚拟手指兜底"，在平板/新内核上会
    //   "UI 显示 OK 但实际点不到"（KMA 静默钳位，见 AutoPallet.h 的注释）✗
    //   现在按已验证可用的顺序：
    //     ① 虚拟手指（VirtualDown + 屏幕坐标）
    //     ② KMA     （必须先把屏幕坐标转基坐标）
    //     ③ uinput  （最后兜底）

    // ---- ① 虚拟手指优先 ----
    if (Touch::IsVirtualActive(kSlot)) Touch::VirtualUp(kSlot);
    if (Touch::VirtualDown(kSlot, cx, cy)) {
        usleep((useconds_t)g_hold_ms * 1000);
        Touch::VirtualUp(kSlot);
        return true;
    }

    // ---- ② KMA 兜底（先转基坐标）----
    ::Vector2 bp = Touch::ScreenToBase(cx, cy);
    bool ok1 = touch_down(kSlot, (int)bp.x, (int)bp.y);
    usleep((useconds_t)g_hold_ms * 1000);
    bool ok2 = touch_up(kSlot);
    if (ok1 && ok2) return true;

    // ---- ③ uinput 最后兜底 ----
    extern bool SimulateTapScreen(int, int, int);
    return Touch::SimulateTapScreen((int)cx, (int)cy, g_hold_ms);
}

// ============================================================================
// 每帧调用
// ============================================================================
inline void Tick() {
    if (!g_on) { g_triggered = false; return; }
    if (!PyAttack::skillmgr_ready()) return;

    const int sid = PyAttack::last_cast_skill_id();

    bool flying = false;
    for (int i = 0; i < kFlySkillN; i++)
        if (sid == kFlySkillIds[i]) { flying = true; break; }

    if (!flying) { g_triggered = false; return; }
    if (g_triggered) return;

    const float d = nearest_surv_hdist();
    if (d < 0.f || d > g_dist_m) return;

    if (do_crash_tap()) g_triggered = true;
}

// ============================================================================
// 触摸区绘制 + 拖动
//   与 AutoSkill / AutoPallet 的触摸框行为一致：点住框内拖动改位置 ✓
//   ⚠ 不用 include AutoSkill.h 复用 DrawOneTouchRect ✗
//     那样 AutoCrash 会变成 AutoSkill 的依赖（编译慢、耦合高）✓
//     这里是几行独立实现，复制而非引用 —— 保持模块独立性 ✓
// ============================================================================
inline void TouchRect_Draw(ImDrawList* Draw) {
    if (!g_show_rect) return;

    //   框以 (g_tap_x, g_tap_y) 为中心，向外展开 g_rect_w x g_rect_h
    float rx = g_tap_x - g_rect_w * 0.5f;
    float ry = g_tap_y - g_rect_h * 0.5f;

    ImVec2 p1(rx, ry);
    ImVec2 p2(rx + g_rect_w, ry + g_rect_h);
    ImVec2 center(g_tap_x, g_tap_y);

    const ImU32 col = IM_COL32(0, 180, 255, 255);   // 蓝色（跟红/绿/橙区分）
    Draw->AddRect(p1, p2, col, 0.0f, 0, 3.0f);
    Draw->AddLine(ImVec2(center.x - 12, center.y), ImVec2(center.x + 12, center.y), col, 2.0f);
    Draw->AddLine(ImVec2(center.x, center.y - 12), ImVec2(center.x, center.y + 12), col, 2.0f);
    char buf[96];
    snprintf(buf, sizeof(buf), "自动坠机 (%.0f,%.0f)", center.x, center.y);
    Draw->AddText(ImVec2(p1.x, p1.y > 24 ? p1.y - 20 : p1.y + 4), col, buf);

    // 拖动
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 m = io.MousePos;
    bool inside = m.x >= p1.x && m.x <= p2.x && m.y >= p1.y && m.y <= p2.y;

    if (io.MouseDown[0]) {
        if (!g_rect_dragging && inside) {
            g_rect_dragging = true;
            g_drag_dx = m.x - p1.x;
            g_drag_dy = m.y - p1.y;
        }
        if (g_rect_dragging) {
            //   反推中心点：中心 = 新的左上角 + 半宽/半高
            float new_rx = m.x - g_drag_dx;
            float new_ry = m.y - g_drag_dy;
            g_tap_x = new_rx + g_rect_w * 0.5f;
            g_tap_y = new_ry + g_rect_h * 0.5f;
            //   屏幕边界限制（防止推到屏幕外找不到）
            if (g_tap_x < g_rect_w * 0.5f) g_tap_x = g_rect_w * 0.5f;
            if (g_tap_y < g_rect_h * 0.5f) g_tap_y = g_rect_h * 0.5f;
            if (g_tap_x > displayInfo.width  - g_rect_w * 0.5f)
                g_tap_x = displayInfo.width  - g_rect_w * 0.5f;
            if (g_tap_y > displayInfo.height - g_rect_h * 0.5f)
                g_tap_y = displayInfo.height - g_rect_h * 0.5f;
        }
    } else {
        g_rect_dragging = false;
    }
}

// ============================================================================
// 界面
// ============================================================================
inline void DrawUI(Theme::Palette& pal) {
    using namespace FangUI;

    Head("自动坠机", pal, "Auto Crash");
    RowSwitch("启用", &g_on, pal);

    if (!g_on) {
        ImGui::TextDisabled("关闭时零开销");
        return;
    }

    ImGui::Dummy(ImVec2(0, Theme::Sp::M));
    Head("触发距离", pal, "Distance");
    {
        float v = g_dist_m;
        if (Slider("ac_dist", &v, 0.5f, 10.0f, "%.1f m", pal))
            g_dist_m = v;
        ImGui::TextDisabled("与最近求生水平距离小于此值时触发");
    }

    ImGui::Dummy(ImVec2(0, Theme::Sp::M));
Head("点击位置", pal, "Tap Pos");
{
    float v = g_tap_x;
    if (Slider("ac_tx", &v, 0.f, 3000.f, "X %.0f", pal)) g_tap_x = v;
}
{
    float v = g_tap_y;
    if (Slider("ac_ty", &v, 0.f, 3000.f, "Y %.0f", pal)) g_tap_y = v;
}
// ★★ 2026-10-17 新增：显示触摸区（用户要求）★★
//   勾上后屏幕上出现蓝色方框，可以直接拖动改位置 ✓
RowSwitch("显示触摸区", &g_show_rect, pal);
if (g_show_rect) {
    {
        float v = g_rect_w;
        if (Slider("ac_rw", &v, 30.f, 300.f, "框宽 %.0f", pal)) g_rect_w = v;
    }
    {
        float v = g_rect_h;
        if (Slider("ac_rh", &v, 30.f, 300.f, "框高 %.0f", pal)) g_rect_h = v;
    }
    ImGui::TextDisabled("开启后可在屏幕上拖动蓝色框调整位置");
}
ImGui::TextDisabled("下坠技能键在屏幕上的位置（框中心点）");
}

} // namespace AutoCrash