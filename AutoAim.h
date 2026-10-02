#ifndef IDV_AUTO_AIM_H
#define IDV_AUTO_AIM_H

#include <atomic>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <thread>
#include <unistd.h>
#include <ctime>

#include "imgui.h"
#include "PyAttack.h"
#include "PySelf.h"
#include "kerneldriver-kma.hpp"

namespace AutoAim {

// ---------------- 配置 ----------------
static std::atomic<bool>  enabled{false};          // UI 里"自动瞄准"开关
static std::atomic<int>   lock_policy{1};          // 0=距离最近 1=屏幕最近
static std::atomic<float> predict_m{0.0f};
static std::atomic<bool>  show_circle{true};
static std::atomic<float> circle_r{300.f};
static std::atomic<float> circle_thick{2.0f};
static std::atomic<uint32_t> circle_color{IM_COL32(255,220,0,180)};
static std::atomic<float> max_dist{50.f};
static std::atomic<int>   aim_rate{1500};          // 视野转动速度(像素/秒)
static std::atomic<float> aim_radius{400.f};       // slot 5 从中心偏移多少算满方向

// 视野手指落点：屏幕中心 + 朝目标方向偏移
// 技能键不碰（用户自己按真人手指）
static const int SLOT_VIEW = 5;

// 屏幕尺寸
static int g_screen_w = 0, g_screen_h = 0;

// 内部状态
static std::atomic<bool> g_active{false};
static float g_view_x = 0.f, g_view_y = 0.f;
static float g_home_x = 0.f, g_home_y = 0.f;

// 兼容字段（draw_Gui.cpp 里可能引用）
static std::atomic<int>   touch_mode{0};
static std::atomic<float> skill_x{0.f}, skill_y{0.f};
static std::atomic<float> skill_w{180.f}, skill_h{180.f};
static bool skill_inited = false;
static std::atomic<float> aim_x{0.f}, aim_y{0.f};
static std::atomic<float> aim_w{300.f}, aim_h{300.f};
static bool aim_inited = false;
static bool draw_trigger = false;
static bool draw_ranges  = false;

// ---------------- 目标 ----------------
struct TargetInfo {
    uint64_t obj;
    float wx, wy, wz;
    float sx, sy;
    float psx, psy;
    float fx, fy;
    float dist_m;
    int   camp;
};

static const int MAX_TARGETS = 64;
static TargetInfo g_targets[2][MAX_TARGETS];
static int g_target_n[2] = {0, 0};
static std::atomic<int> g_target_active{0};

static void publish_targets(const TargetInfo* list, int n) {
    if (n > MAX_TARGETS) n = MAX_TARGETS;
    int w = 1 - g_target_active.load();
    memcpy(g_targets[w], list, sizeof(TargetInfo) * n);
    g_target_n[w] = n;
    g_target_active.store(w);
}

// ---------------- 坐标映射 ----------------
static inline void screen_to_touch(int sx, int sy, int& tx, int& ty) {
    if (g_screen_w > g_screen_h) {
        tx = g_screen_h - sy;
        ty = sx;
    } else {
        tx = sx;
        ty = sy;
    }
}

// ---------------- 选目标 ----------------
static const TargetInfo* pick_target(int my_camp) {
    int b = g_target_active.load();
    int n = g_target_n[b];
    const float px = g_screen_w * 0.5f;
    const float py = g_screen_h * 0.5f;
    const float cr = circle_r.load();
    const float r2 = cr * cr;
    const float md = max_dist.load();
    const int pol = lock_policy.load();

    const TargetInfo* best = nullptr;
    float best_key = 1e30f;

    for (int i = 0; i < n; i++) {
        const TargetInfo& t = g_targets[b][i];
        if (t.camp == my_camp) continue;
        if (t.camp != 1 && t.camp != 2) continue;
        if (t.dist_m > md) continue;

        float ddx = t.sx - px;
        float ddy = t.sy - py;
        float d2 = ddx*ddx + ddy*ddy;
        if (d2 > r2) continue;

        float key = (pol == 0) ? t.dist_m : d2;
        if (key < best_key) { best_key = key; best = &t; }
    }
    return best;
}

// ---------------- 主线程 ----------------
static void worker_thread() {
    printf("[自瞄] 线程启动\n"); fflush(stdout);

    while (true) {
        usleep(8000);   // 125Hz

        if (g_screen_w <= 0 || g_screen_h <= 0) continue;

        const bool want = enabled.load();
        const bool active = g_active.load();

        // ---- 启动：slot 5 按屏幕中心 ----
        if (want && !active) {
            g_home_x = g_screen_w * 0.5f;
            g_home_y = g_screen_h * 0.5f;
            g_view_x = g_home_x;
            g_view_y = g_home_y;
            int tx, ty;
            screen_to_touch((int)g_home_x, (int)g_home_y, tx, ty);
            touch_down(SLOT_VIEW, tx, ty);
            g_active.store(true);
            printf("[自瞄] 开启 slot5 down (%d,%d)\n", tx, ty);
            fflush(stdout);
            continue;
        }

        // ---- 停止：抬起 ----
        if (!want && active) {
            touch_up(SLOT_VIEW);
            g_active.store(false);
            printf("[自瞄] 关闭\n"); fflush(stdout);
            continue;
        }

        if (!g_active.load()) continue;

        // ---- 目标方向 = 从屏幕中心指向目标 ----
        const int my_camp = PySelf::camp();
        const TargetInfo* tgt = pick_target(my_camp);

        float fx = g_home_x;
        float fy = g_home_y;
        if (tgt) {
            float dx = tgt->psx - g_home_x;
            float dy = tgt->psy - g_home_y;
            float dlen = sqrtf(dx*dx + dy*dy);
            float radius = (float)aim_radius.load();
            if (dlen > 1.0f) {
                fx = g_home_x + dx / dlen * radius;
                fy = g_home_y + dy / dlen * radius;
            }
        }

        // 平滑逼近
        const float move_dx = fx - g_view_x;
        const float move_dy = fy - g_view_y;
        const float dist = sqrtf(move_dx*move_dx + move_dy*move_dy);
        const float rate = (float)aim_rate.load();
        const float max_step = rate * 0.008f;
        if (dist <= max_step || dist < 0.5f) {
            g_view_x = fx; g_view_y = fy;
        } else {
            g_view_x += move_dx / dist * max_step;
            g_view_y += move_dy / dist * max_step;
        }

        int tx, ty;
        screen_to_touch((int)g_view_x, (int)g_view_y, tx, ty);
        touch_move(SLOT_VIEW, tx, ty);
    }
}

static void start() {
    static bool s = false;
    if (s) return;
    s = true;
    std::thread(worker_thread).detach();
}

// ---------------- 屏幕尺寸 ----------------
static void ensure_inited(int sw, int sh) {
    g_screen_w = sw;
    g_screen_h = sh;
}

// ---------------- 绘制 ----------------
static void DrawOverlay(ImDrawList* dl, int sw, int sh) {
    ensure_inited(sw, sh);
    const float px = sw * 0.5f;
    const float py = sh * 0.5f;

    if (show_circle.load()) {
        ImU32 col = (ImU32)circle_color.load();
        dl->AddCircle(ImVec2(px, py), circle_r.load(), col, 64, circle_thick.load());
    }

    if (draw_ranges && g_active.load()) {
        // 视野手指位置（青色圆点）
        dl->AddCircleFilled(ImVec2(g_view_x, g_view_y), 12, IM_COL32(0,255,255,200));
        // 屏幕中心（白色十字）
        dl->AddLine({px-15,py},{px+15,py}, IM_COL32(255,255,255,150), 2.0f);
        dl->AddLine({px,py-15},{px,py+15}, IM_COL32(255,255,255,150), 2.0f);
    }
}

static const char* status_text() {
    static char buf[64];
    if (!enabled.load())   snprintf(buf, sizeof(buf), "未开启");
    else if (!g_active.load()) snprintf(buf, sizeof(buf), "启动中");
    else                   snprintf(buf, sizeof(buf), "自瞄中");
    return buf;
}

} // namespace AutoAim

#endif