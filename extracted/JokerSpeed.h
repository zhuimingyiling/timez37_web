#pragma once
// ============================================================================
// JokerSpeed.h —— 小丑拉锯加速
// ============================================================================
//
// 判据：skill_mgr.last_cast_skill_id == 1322
//      严格判定：不是 1322 立即停止加速（无保持窗口）
//
// 改的字段：MoveSpeedMgr._base_speed
//   进锯：_base_speed = 原值 × 倍率
//   出锯：_base_speed = 原值
// ============================================================================

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <time.h>

#include "PyCore.h"
#include "PyAttack.h"
#include "PySelf.h"

#include "imgui.h"
#include "theme.h"
#include "fang_ui.h"

namespace JokerSpeed {

// ============================================================================
// 配置
// ============================================================================
inline bool        g_on         = false;
inline float       g_scale      = 1.5f;
inline std::string g_status;

//   1312 = 一阶锯  1322 = 二阶锯
inline const int kDashSkillIds[] = { 1312, 1322 };
inline const int kDashSkillN = 2;

// ============================================================================
// 运行时状态
// ============================================================================
inline int64_t  g_last_skill_seen = -1;      // 界面显示用（当前 last_cast）

inline uint64_t g_msm       = 0;
inline double   g_base      = 0.0;
inline bool     g_have_base = false;

// ============================================================================
// 时间
// ============================================================================
inline uint64_t NowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// ============================================================================
// 状态判定
// ============================================================================
inline bool is_joker_now() {
    const uint64_t hu = PyAttack::hunter_unit_raw();
    if (!PyCore::is_obj(hu)) return false;
    return PyAttack::get_obj_field(hu, "_joker_current_faceid") != 0;
}

inline bool read_last_cast(int64_t& out) {
    if (!PyAttack::skillmgr_ready()) return false;
    out = PyAttack::last_cast_skill_id();
    return true;
}

//   ★★ 严格判定：last_cast == 1322 才算拉锯，!= 立即停
inline bool is_dashing_now() {
    if (!is_joker_now()) return false;
    int64_t sid = 0;
    if (!read_last_cast(sid)) return false;
    g_last_skill_seen = sid;
    for (int i = 0; i < kDashSkillN; i++)
        if (sid == kDashSkillIds[i]) return true;
    return false;
}

// ============================================================================
// 拿 MoveSpeedMgr._base_speed 的 PyFloat 对象
// ============================================================================
inline uint64_t msm_obj() {
    const uint64_t hu = PyAttack::hunter_unit_raw();
    if (!PyCore::is_obj(hu)) return 0;
    return PyAttack::get_obj_field(hu, "msm");
}

inline uint64_t msm_base_speed_obj() {
    const uint64_t msm = msm_obj();
    if (!PyCore::is_obj(msm)) return 0;
    return PyAttack::get_obj_field(msm, "_base_speed");
}

// ============================================================================
// PyFloat 读写
// ============================================================================
inline bool read_pyfloat(uint64_t obj, double& out) {
    if (!PyCore::is_obj(obj)) return false;
    if ((getPtr64(obj + 8) & 0x00FFFFFFFFFFFFFFULL) != PyRoot::g_float_type) return false;
    return vm_readv(obj + 0x10, &out, 8);
}
inline bool write_pyfloat(uint64_t obj, double v) {
    if (!PyCore::is_obj(obj)) return false;
    if ((getPtr64(obj + 8) & 0x00FFFFFFFFFFFFFFULL) != PyRoot::g_float_type) return false;
    return vm_writev(obj + 0x10, &v, 8);
}

inline double peek_base() {
    double v = 0.0;
    const uint64_t o = msm_base_speed_obj();
    if (!o || !read_pyfloat(o, v)) return -99999.0;
    return v;
}

// ============================================================================
// 每帧调用
// ============================================================================
inline void Tick() {
    if (!g_on) return;

    const uint64_t msm = msm_obj();

    // 换 msm 对象（换角色/换局）→ 清状态
    if (msm != g_msm) {
        g_msm       = msm;
        g_have_base = false;
    }

    const uint64_t vobj = msm_base_speed_obj();
    if (!vobj) { g_status = "拿不到 msm._base_speed"; return; }

    const bool dashing = is_dashing_now();

    // ---- 出锯：还原 ----
    if (!dashing) {
        if (g_have_base) {
            if (write_pyfloat(vobj, g_base)) {
                g_have_base = false;
                g_status = "已还原";
            }
        }
        return;
    }

    // ---- 拉锯：保证 _base_speed == 原值 × 倍率 ----
    double cur = 0.0;
    if (!read_pyfloat(vobj, cur)) { g_status = "_base_speed 不是 PyFloat"; return; }

    if (!g_have_base) {
        g_base = cur;
        g_have_base = true;
    }

    const double want = g_base * (double)g_scale;
    if (cur < want - 1e-4 || cur > want + 1e-4) {
        write_pyfloat(vobj, want);
    }
}

// ============================================================================
// 界面
// ============================================================================
inline void DrawUI(Theme::Palette& pal) {
    using namespace FangUI;

    Head("小丑加速", pal, "Joker Dash");
    RowSwitch("启用加速", &g_on, pal);

    if (!g_on) {
        ImGui::TextDisabled("关闭时零开销，不读写内存");
        return;
    }

    // ---- 倍率 ----
    ImGui::Dummy(ImVec2(0, Theme::Sp::M));
    Head("速度倍率", pal, "Scale");
    {
        float v = g_scale;
        if (Slider("jk_scale", &v, 1.0f, 50.0f, "%.2fx", pal))
            g_scale = v;
        ImGui::TextDisabled("1.0 原速");
    }
}

} // namespace JokerSpeed