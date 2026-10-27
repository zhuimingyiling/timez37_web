#pragma once
// ============================================================================
// AntiCowboy.h —— 防牛仔（牛仔用绳子抢人时自动放人）
// ============================================================================
//
// 用户需求（原话）：
//   「当我自身 state 状态是 11 时，如果自身 15 米内有"牛仔"面朝方向 90° 内有我，
//     并且牛仔正在使用技能 ID 3021，则自动点击放人键。
//     这个放人键让用户自行调，防牛仔的功能实现单独写个文件，
//     并且做个放人触摸区域显示」
//
//   「给触发防牛仔的自动放人的判定再额外加个限制，目标牛仔必须状态为 state=75 才生效」
//
// 生效条件（全部满足才触发）：
//   ① 自身是监管（PySelf::camp() == 1）
//   ② 自身 state == 11（举起求生者的状态）
//   ③ 15 米内有牛仔
//   ④ 牛仔面朝方向 90° 内包含我（牛仔在瞄我）
//   ⑤ 牛仔 skill_mgr.last_cast_skill_id == 3021（正甩绳）
//   ⑥ 牛仔自身 state == 75（甩绳动作进行中）
//
// 触发后：点击用户设置的"放人键"位置一次。
// 出技能后重置，不会连点。
// ============================================================================

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <time.h>
#include <unistd.h>

#include "PyCore.h"
#include "PyAttack.h"
#include "PySelf.h"
#include "kerneldriver-paradise.hpp"
#include "TouchHelperA.h"
#include "imgui.h"
#include "theme.h"
#include "fang_ui.h"
#include "AutoSkill.h"      // ★ 复用 DrawOneTouchRect（触摸框的拖动实现）

extern android::ANativeWindowCreator::DisplayInfo displayInfo;

namespace AntiCowboy {

// ============================================================================
// 配置
// ============================================================================
inline bool  g_on        = false;
inline float g_dist_m    = 15.0f;    // 检测牛仔的水平距离
inline float g_fov_deg   = 90.0f;    // 牛仔面朝方向多宽的扇区包含我（整幅角，半角=值/2）
inline float g_tap_x     = 1000.0f;     // 放人键屏幕坐标
inline float g_tap_y     = 500.0f;
inline int   g_hold_ms   = 60;
inline bool  g_show_rect = false;    // 显示触摸区

// 牛仔使用技能的 skill_id（用户实测给出）
inline const int kCowboySkillId  = 3021;
// 自身举起求生者的 state（用户实测给出）
inline const int kSelfCarryState = 11;
// ★ 牛仔甩绳时自身 state 必须是 75（用户实测给出）
//   为什么要加这条：skill_mgr.last_cast_skill_id == 3021 只是"上一次甩的绳"，
//   技能刚结束还没被下一次覆盖时它仍然是 3021，会误触发；
//   再加上 state=75 就把"真的在甩绳的过程中"钉死了 ✓
inline const int kCowboyRopeState = 75;

// 触摸 slot：避开
//   AutoSkill: 6/7  AutoPallet: 8  AutoCrash: 5  探针: 9
//   → 防牛仔用 4 ✓
inline const int kSlot = 4;

// 触发冷却：防止同一次甩绳重复点
inline const int64_t kCooldownMs = 800;

// ============================================================================
// 运行时状态
// ============================================================================
inline bool     g_triggered  = false;   // 本次 state==11 期间是否已触发
inline int64_t  g_last_tap_ms = 0;
inline char     g_status[160] = "未开启";

inline uint64_t NowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// ============================================================================
// 从场景对象拿世界坐标（x=世界X, y=高度, z=水平Z）
// ============================================================================
inline bool ScenePos(uint64_t scene, float& x, float& y, float& z) {
    if (!PyCore::is_obj(scene)) return false;
    uint64_t coor = getPtr64(scene + 0x28);
    if (coor < 0x5000000000ULL || coor >= 0x8000000000ULL) return false;
    float p[3] = {0, 0, 0};
    if (!vm_readv(coor + 0xa0, p, 12)) return false;
    x = p[0]; y = p[1]; z = p[2];
    return !(x == 0.f && z == 0.f);   // 空槽
}

// ============================================================================
// 从 Python unit 拿它的场景对象（unit.model + 0x20）
// ============================================================================
inline uint64_t UnitScene(uint64_t unit) {
    if (!PyCore::is_obj(unit)) return 0;
    uint64_t d = PyCore::get_inst_dict(unit);
    if (!d) return 0;
    PyCore::Dict dk;
    if (!PyCore::read_dict(d, dk)) return 0;
    int64_t im = PyCore::resolve_index(d, dk, -1, "model");
    if (im < 0) return 0;
    uint64_t model = getPtr64(PyCore::value_slot(dk, im));
    if (!PyCore::is_obj(model)) return 0;
    uint64_t scene = getPtr64(model + 0x20) & 0x00FFFFFFFFFFFFULL;
    return PyCore::is_obj(scene) ? scene : 0;
}

// ============================================================================
// 读 unit.skill_mgr.last_cast_skill_id
// ============================================================================
inline int64_t UnitLastSkill(uint64_t unit) {
    if (!PyCore::is_obj(unit)) return 0;
    uint64_t d = PyCore::get_inst_dict(unit);
    if (!d) return 0;
    PyCore::Dict dk;
    if (!PyCore::read_dict(d, dk)) return 0;
    int64_t ism = PyCore::resolve_index(d, dk, -1, "skill_mgr");
    if (ism < 0) return 0;
    uint64_t sm = getPtr64(PyCore::value_slot(dk, ism));
    if (!PyCore::is_obj(sm)) return 0;
    uint64_t smd = PyCore::get_inst_dict(sm);
    if (!smd) return 0;
    PyCore::Dict smk;
    if (!PyCore::read_dict(smd, smk)) return 0;
    int64_t ilc = PyCore::resolve_index(smd, smk, -1, "last_cast_skill_id");
    if (ilc < 0) return 0;
    int64_t v = 0;
    if (!PyCore::read_int(getPtr64(PyCore::value_slot(smk, ilc)), v)) return 0;
    return v;
}

// ============================================================================
// 读 unit.direction 的水平朝向（fx, fz）
//   布局与 PyAttack::get_hunter_forward 方法 2 一致 ✓
// ============================================================================
inline bool UnitForward(uint64_t unit, float& fx, float& fz) {
    fx = fz = 0;
    uint64_t d = PyCore::get_inst_dict(unit);
    if (!d) return false;
    PyCore::Dict dk;
    if (!PyCore::read_dict(d, dk)) return false;
    int64_t idir = PyCore::resolve_index(d, dk, -1, "direction");
    if (idir < 0) return false;
    uint64_t dobj = getPtr64(PyCore::value_slot(dk, idir));
    if (!PyCore::is_obj(dobj)) return false;
    float vx = 0, vy = 0, vz = 0;
    if (!vm_readv(dobj + 0x10, &vx, 4)) return false;
    if (!vm_readv(dobj + 0x14, &vy, 4)) return false;
    if (!vm_readv(dobj + 0x18, &vz, 4)) return false;
    fx = vx; fz = vz;
    return (fabsf(fx) + fabsf(fz) >= 0.01f);
}

// ============================================================================
// ★ 读 unit.state_machine._state 列表里是否包含指定 state id
//
//   与 PySelf 读自身 state 走同一条链（PySelf.h: try_refresh() 里
//   `state_machine._state` -> list -> 取 best 那段），所以自身 state=11
//   和牛仔 state=75 是同一个语义来源 ✓
//
//   为什么用"列表包含"而不是"取一个 best 值再比较"：
//     _state 是**状态机当前叠加的多个状态**的列表（站立/战斗姿态/技能态可能同时在列
//     表里），PySelf 那边取 best 是为了显示"最像技能的那个" —— 那是为了面板好看。
//     判定"牛仔是否真的在甩绳"要求**存在 75 就算**，宁缺毋滥、不能被别的状态挤掉 ✓
// ============================================================================
inline bool UnitHasState(uint64_t unit, int want) {
    if (!PyCore::is_obj(unit)) return false;
    uint64_t d = PyCore::get_inst_dict(unit);
    if (!d) return false;
    PyCore::Dict dk;
    if (!PyCore::read_dict(d, dk)) return false;
    int64_t ism = PyCore::resolve_index(d, dk, -1, "state_machine");
    if (ism < 0) return false;
    uint64_t sm = getPtr64(PyCore::value_slot(dk, ism));
    if (!PyCore::is_obj(sm)) return false;
    uint64_t smd = PyCore::get_inst_dict(sm);
    if (!smd) return false;
    PyCore::Dict smk;
    if (!PyCore::read_dict(smd, smk)) return false;
    int64_t ist = PyCore::resolve_index(smd, smk, -1, "_state");
    if (ist < 0) return false;
    uint64_t lst = getPtr64(PyCore::value_slot(smk, ist));
    int64_t n = 0; uint64_t items = 0;
    if (!PyCore::read_list(lst, n, items)) return false;
    for (int64_t i = 0; i < n && i < 16; i++) {
        int64_t v = 0;
        if (PyCore::read_int(getPtr64(items + i * 8), v) && v == want) return true;
    }
    return false;
}

// ============================================================================
// 找"牛仔"单位：在 units_by_type[2] 里按 model_path 匹配 "niuzai"
//   Name.h 里牛仔的映射关键词就是 "m_niuzai" ✓
// ============================================================================
inline uint64_t FindCowboyUnit() {
    uint64_t units[32] = {};
    //   get_all_units_of_type 返回 >0 = 读到 n 个 / 0 = 确实没有 / <0 = 读失败 ✓
    const int n = PyAttack::get_all_units_of_type(2, units, 32);
    if (n <= 0) return 0;
    for (int i = 0; i < n; i++) {
        if (!PyCore::is_obj(units[i])) continue;
        uint64_t d = PyCore::get_inst_dict(units[i]);
        if (!d) continue;
        PyCore::Dict dk;
        if (!PyCore::read_dict(d, dk)) continue;
        int64_t im = PyCore::resolve_index(d, dk, -1, "model_path");
        if (im < 0) continue;
        uint64_t mp = getPtr64(PyCore::value_slot(dk, im));
        if (!PyCore::is_obj(mp)) continue;
        char path[192] = {0};
        PyCore::read_str_any(mp, path, sizeof(path));
        if (strstr(path, "niuzai") != nullptr) return units[i];
    }
    return 0;
}

// ============================================================================
// 触摸：点击放人键
// ============================================================================
inline bool DoTap() {
    if (g_tap_x <= 0.f || g_tap_y <= 0.f) return false;
    const float cx = g_tap_x, cy = g_tap_y;

    // ① 虚拟手指优先（与自动技能同一条路，平板/新内核上已验证可用）
    if (Touch::IsVirtualActive(kSlot)) Touch::VirtualUp(kSlot);
    if (Touch::VirtualDown(kSlot, cx, cy)) {
        usleep((useconds_t)g_hold_ms * 1000);
        Touch::VirtualUp(kSlot);
        return true;
    }

    // ② KMA 兜底：**必须先转基坐标**（否则超范围被静默钳位）
    ::Vector2 bp = Touch::ScreenToBase(cx, cy);
    bool ok1 = touch_down(kSlot, (int)bp.x, (int)bp.y);
    usleep((useconds_t)g_hold_ms * 1000);
    bool ok2 = touch_up(kSlot);
    if (ok1 && ok2) return true;

    // ③ uinput 最后兜底
    return Touch::SimulateTapScreen((int)cx, (int)cy, g_hold_ms);
}

// ============================================================================
// 每帧调用（挂在 Draw_Main 里，和 AutoCrash::Tick() 同一处）
// ============================================================================
inline void Tick() {
    if (!g_on) { g_triggered = false; snprintf(g_status, sizeof(g_status), "未开启"); return; }

    // ① 只在自身是监管时生效
    if (PySelf::camp() != 1) {
        g_triggered = false;
        snprintf(g_status, sizeof(g_status), "仅监管生效");
        return;
    }

    uint64_t self_unit = PyAttack::hunter_unit_raw();
    if (!PyCore::is_obj(self_unit)) {
        g_triggered = false;
        snprintf(g_status, sizeof(g_status), "自身 unit 未就绪");
        return;
    }

    // ② 自身 state == 11（举起求生者）
    const int self_state = PyAttack::cur_state();
    if (self_state != kSelfCarryState) {
        g_triggered = false;   // 退出 state=11 -> 允许下次再触发
        char b[64];
        snprintf(b, sizeof(b), "state=%d（非%d）", self_state, kSelfCarryState);
        snprintf(g_status, sizeof(g_status), "%s", b);
        return;
    }

    // ③ 找牛仔
    const uint64_t cb_unit = FindCowboyUnit();
    if (!cb_unit) { snprintf(g_status, sizeof(g_status), "state=11，但没找到牛仔"); return; }

    // 拿坐标
    uint64_t self_scene = UnitScene(self_unit);
    uint64_t cb_scene   = UnitScene(cb_unit);
    float sx = 0, sy = 0, sz = 0, cxx = 0, cyy = 0, czz = 0;
    if (!ScenePos(self_scene, sx, sy, sz) || !ScenePos(cb_scene, cxx, cyy, czz)) {
        snprintf(g_status, sizeof(g_status), "state=11，坐标读不到");
        return;
    }

    // ③续：15 米内（水平距离，与玩家直觉一致）
    const float dx = sx - cxx;
    const float dz = sz - czz;
    const float hdist = sqrtf(dx * dx + dz * dz) / 11.886f;
    if (hdist > g_dist_m) {
        char b[96];
        snprintf(b, sizeof(b), "state=11，牛仔 %.1fm（>%.0f）", hdist, g_dist_m);
        snprintf(g_status, sizeof(g_status), "%s", b);
        return;
    }

    // ④ 牛仔面朝方向 90° 内有我
    float fx = 0, fz = 0;
    if (!UnitForward(cb_unit, fx, fz)) {
        snprintf(g_status, sizeof(g_status), "牛仔朝向读不到");
        return;
    }
    float flen = sqrtf(fx * fx + fz * fz);
    if (flen < 0.01f) { snprintf(g_status, sizeof(g_status), "牛仔朝向为零"); return; }
    fx /= flen; fz /= flen;

    float tx = sx - cxx, tz = sz - czz;
    float tlen = sqrtf(tx * tx + tz * tz);
    if (tlen < 0.01f) { tx = 0; tz = 0; }    // 贴身：任何朝向都算命中
    else { tx /= tlen; tz /= tlen; }

    const float dot = fx * tx + fz * tz;
    const float half_rad = (g_fov_deg * 0.5f) * 3.14159265f / 180.0f;
    const float cos_half = cosf(half_rad);
    if (dot < cos_half) {
        char b[96];
        snprintf(b, sizeof(b), "state=11，牛仔 %.1fm 但朝向不对(cos=%.2f)", hdist, dot);
        snprintf(g_status, sizeof(g_status), "%s", b);
        return;
    }

    // ⑤ 牛仔正在使用技能 3021
    const int64_t cb_skill = UnitLastSkill(cb_unit);
    if (cb_skill != kCowboySkillId) {
        char b[112];
        snprintf(b, sizeof(b), "state=11，牛仔 %.1fm 朝向OK，但技能=%lld",
                 hdist, (long long)cb_skill);
        snprintf(g_status, sizeof(g_status), "%s", b);
        return;
    }

    // ⑥ ★ 牛仔自身 state 必须 == 75（甩绳动作进行中）
    //   skill_mgr.last_cast_skill_id 会一直停在 3021 直到下一次甩绳，
    //   加上这条就把"真的在甩绳"和"甩完了但字段还没被覆盖"区分开 ✓
    if (!UnitHasState(cb_unit, kCowboyRopeState)) {
        char b[128];
        snprintf(b, sizeof(b), "牛仔 %.1fm 技能=%d 但 state≠%d（不在甩绳动作中）",
                 hdist, (int)cb_skill, kCowboyRopeState);
        snprintf(g_status, sizeof(g_status), "%s", b);
        return;
    }

    // ---- 全部命中 ----
    if (g_triggered) { snprintf(g_status, sizeof(g_status), "已触发，等待技能结束"); return; }
    const uint64_t now = NowMs();
    if (g_last_tap_ms != 0 && (int64_t)now - g_last_tap_ms < kCooldownMs) {
        snprintf(g_status, sizeof(g_status), "冷却中");
        return;
    }

    if (DoTap()) {
        g_triggered   = true;
        g_last_tap_ms = (int64_t)now;
        snprintf(g_status, sizeof(g_status), "★ 已放人（牛仔 %.1fm，state=%d）",
                 hdist, kCowboyRopeState);
    } else {
        snprintf(g_status, sizeof(g_status), "点击失败（放人键坐标？）");
    }
}

// ============================================================================
// 触摸区绘制（复用 AutoSkill 的 DrawOneTouchRect）
// ============================================================================
inline void TouchRect_Draw(ImDrawList* Draw) {
    // 只在"防牛仔开 + 显示触摸区开"时画，避免干扰别功能 ✓
    if (!g_on || !g_show_rect) return;

    static bool  inited   = false;
    static bool  dragging = false;
    static float drag_dx  = 0.f;
    static float drag_dy  = 0.f;
    //   ⚠ 100x100 是"放人键这个按钮"的视觉提示 —— 不需要 200x200 那么大 ✓
    //     但 DrawOneTouchRect 收的是 `float&`，得用局部变量 ✓
    float rw = 100.f, rh = 100.f;

    AutoSkill::DrawOneTouchRect(Draw, true,
                                g_tap_x, g_tap_y, rw, rh,
                                inited, dragging, drag_dx, drag_dy,
                                IM_COL32(255, 160, 0, 255), "放人键");
}

// ============================================================================
// UI
// ============================================================================
inline void DrawUI(Theme::Palette& pal) {
    using namespace FangUI;

    Head("防牛仔", pal, "Anti Cowboy");
    RowSwitch("启用防牛仔", &g_on, pal);

    if (!g_on) {
        ImGui::TextDisabled("关闭时零开销");
        return;
    }

    ImGui::Dummy(ImVec2(0, Theme::Sp::M));
    Head("检测参数", pal);
    {
        float v = g_dist_m;
        if (Slider("acw_dist", &v, 5.0f, 30.0f, "距离 %.0f m", pal)) g_dist_m = v;
        ImGui::TextDisabled("与牛仔水平距离小于此值时检测");
    }
    {
        float v = g_fov_deg;
        if (Slider("acw_fov", &v, 30.0f, 180.0f, "扇区 %.0f°", pal)) g_fov_deg = v;
        ImGui::TextDisabled("牛仔面朝方向多少度内包含我（半角 = 值的一半）");
    }

    ImGui::Dummy(ImVec2(0, Theme::Sp::M));
    Head("放人键位置", pal, "Tap Pos");
    {
        float v = g_tap_x;
        if (Slider("acw_tx", &v, 0.f, 3000.f, "X %.0f", pal)) g_tap_x = v;
    }
    {
        float v = g_tap_y;
        if (Slider("acw_ty", &v, 0.f, 3000.f, "Y %.0f", pal)) g_tap_y = v;
    }
    {
        float v = (float)g_hold_ms;
        if (Slider("acw_hm", &v, 10.f, 200.f, "按住 %.0f ms", pal))
            g_hold_ms = (int)(v + 0.5f);
    }
    //   2026-10-26：搬到「调整触摸区域」页
    ImGui::TextDisabled("触摸区：再点一次左侧「触摸」进调整页");
    ImGui::TextDisabled("开启后可在屏幕上拖动橙色框调整位置");

    #if SHOW_DUMP_PAGE
    // ★★ 2026-10-17：状态显示只在 Debug 版显示 ★★
    //   受 Android.mk 的 SHOW_DUMP_PAGE 控制 —— 发布版（=0）这段**根本
    //   不编进去**（不是运行期隐藏），不占体积也不留痕迹 ✓
    //   与「设置页 → 监管 / 技能CD」那两段的处理方式完全一致 ✓
    ImGui::Dummy(ImVec2(0, Theme::Sp::M));
    Head("状态", pal, "Status");
    ImGui::TextWrapped("%s", g_status);
    ImGui::TextDisabled("触发条件：自身 state=%d + %.0fm 内有牛仔 + 牛仔朝我"
                        " + 牛仔技能=%d + 牛仔 state=%d",
                        kSelfCarryState, g_dist_m, kCowboySkillId, kCowboyRopeState);
#endif
}

} // namespace AntiCowboy