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
#include "TouchHelperA.h"

namespace AutoAim {

// ============================================================
// 配置
// ============================================================

static std::atomic<bool> enabled{false};

// 0 = 世界距离最近
// 1 = 屏幕中心最近
static std::atomic<int> lock_policy{1};

static std::atomic<float> predict_m{0.0f};

static std::atomic<bool> show_circle{true};

static std::atomic<float> circle_r{300.0f};

static std::atomic<float> circle_thick{2.0f};

static std::atomic<uint32_t>
circle_color{IM_COL32(255, 220, 0, 180)};

static std::atomic<float> max_dist{50.0f};

// 像素/秒
static std::atomic<int> aim_rate{1500};

// 技能方向距离屏幕中心的偏移量
static std::atomic<float> aim_radius{400.0f};


// ============================================================
// 屏幕
// ============================================================

static int g_screen_w = 0;
static int g_screen_h = 0;


// ============================================================
// AutoAim 状态
// ============================================================

// true = 当前正在执行一次技能 AutoAim
static std::atomic<bool> g_active{false};


// 当前自动控制的屏幕位置
static float g_view_x = 0.0f;
static float g_view_y = 0.0f;


// 技能开始时 aim 手指的位置
static float g_home_x = 0.0f;
static float g_home_y = 0.0f;


// ============================================================
// 兼容 draw_Gui.cpp 可能使用的字段
// ============================================================

static std::atomic<int> touch_mode{0};

static std::atomic<float>
skill_x{0.0f},
skill_y{0.0f};

static std::atomic<float>
skill_w{180.0f},
skill_h{180.0f};

static bool skill_inited = false;

static std::atomic<float>
aim_x{0.0f},
aim_y{0.0f};

static std::atomic<float>
aim_w{300.0f},
aim_h{300.0f};

static bool aim_inited = false;

static bool draw_trigger = false;

static bool draw_ranges = false;


// ============================================================
// 本次技能锁定的真实手指
// ============================================================

struct LockedFinger {

    int device = -1;

    int slot = -1;

    int id = -1;

    uint64_t downSerial = 0;

    bool valid = false;
};


// 自瞄触发区域
static std::atomic<int> g_trigger_zone_x1{0};
static std::atomic<int> g_trigger_zone_y1{0};
static std::atomic<int> g_trigger_zone_x2{0};
static std::atomic<int> g_trigger_zone_y2{0};

// 当前是否处于触发区域
static std::atomic<bool> g_aim_trigger{false};
static std::atomic<int> g_trigger_mapping{0};

// 技能按钮手指
static LockedFinger g_skill_finger;


// 实际控制技能方向的手指
static LockedFinger g_aim_finger;


// ============================================================
// 目标
// ============================================================

struct TargetInfo {

    uint64_t obj;

    float wx;
    float wy;
    float wz;

    float sx;
    float sy;

    float psx;
    float psy;

    float fx;
    float fy;

    float dist_m;

    int camp;
};


static const int MAX_TARGETS = 64;

static TargetInfo
g_targets[2][MAX_TARGETS];

static int
g_target_n[2] = {0, 0};

static std::atomic<int>
g_target_active{0};


static void publish_targets(
    const TargetInfo *list,
    int n
) {

    if (!list) {
        return;
    }

    if (n < 0) {
        n = 0;
    }

    if (n > MAX_TARGETS) {
        n = MAX_TARGETS;
    }

    int w =
        1 - g_target_active.load();

    if (n > 0) {

        memcpy(
            g_targets[w],
            list,
            sizeof(TargetInfo) * n
        );
    }

    g_target_n[w] = n;

    g_target_active.store(w);
}


// ============================================================
// 获取当前目标
// ============================================================

static const TargetInfo *pick_target(
    int my_camp
) {

    int b =
        g_target_active.load();

    int n =
        g_target_n[b];

    const float px =
        g_screen_w * 0.5f;

    const float py =
        g_screen_h * 0.5f;

    const float cr =
        circle_r.load();

    const float r2 =
        cr * cr;

    const float md =
        max_dist.load();

    const int pol =
        lock_policy.load();

    const TargetInfo *best =
        nullptr;

    float best_key =
        1e30f;

    for (int i = 0; i < n; ++i) {

        const TargetInfo &t =
            g_targets[b][i];

        if (t.camp == my_camp)
            continue;

        if (t.camp != 1 &&
            t.camp != 2)
            continue;

        if (t.dist_m > md)
            continue;

        float ddx =
            t.sx - px;

        float ddy =
            t.sy - py;

        float d2 =
            ddx * ddx +
            ddy * ddy;

        if (d2 > r2)
            continue;

        float key =
            (pol == 0)
                ? t.dist_m
                : d2;

        if (key < best_key) {

            best_key = key;

            best = &t;
        }
    }

    return best;
}


// ============================================================
// 清除本次技能锁定
// ============================================================

static void clear_lock(
    const char *reason
) {

    if (g_active.load()) {

        printf(
            "[自瞄] 技能结束：%s\n",
            reason ? reason : "unknown"
        );

        fflush(stdout);
    }

    g_skill_finger =
        LockedFinger{};

    g_aim_finger =
        LockedFinger{};

    g_active.store(false);
}


// ============================================================
// 获取当前真实手指
// ============================================================

static int get_fingers(
    Touch::FingerInfo *out,
    int maxCount
) {

    return Touch::GetActiveFingers(
        out,
        maxCount
    );
}


// ============================================================
// 找到本次技能的手指
//
// 规则：
//
// 1. State == 47 时，downSerial 最大的手指
//    = 最近按下的技能手指。
//
// 2. 如果还有在它之前已经存在的手指：
//       最近的那个 = aimFinger
//
// 3. 如果没有其他手指：
//       skillFinger 自己 = aimFinger
//
// 4. 后面新出现的第三根手指不会重新选择。
// ============================================================

static bool lock_skill_fingers() {

    Touch::FingerInfo fingers[10]{};

    int count =
        get_fingers(
            fingers,
            10
        );

    if (count <= 0) {

        printf(
            "[自瞄] State47，但没有检测到真实手指\n"
        );

        fflush(stdout);

        return false;
    }


    // --------------------------------------------------------
    // 找 downSerial 最大的：
    // 本次技能手指
    // --------------------------------------------------------

    int skillIndex = -1;

    uint64_t newestSerial = 0;

    for (int i = 0; i < count; ++i) {

        if (!fingers[i].isDown)
            continue;

        if (skillIndex < 0 ||
            fingers[i].downSerial > newestSerial) {

            skillIndex = i;

            newestSerial =
                fingers[i].downSerial;
        }
    }

    if (skillIndex < 0) {
        return false;
    }


    const Touch::FingerInfo &skill =
        fingers[skillIndex];


    g_skill_finger.device =
        skill.device;

    g_skill_finger.slot =
        skill.slot;

    g_skill_finger.id =
        skill.id;

    g_skill_finger.downSerial =
        skill.downSerial;

    g_skill_finger.valid =
        true;


    // --------------------------------------------------------
    // 找技能手指之前已经存在的手指
    //
    // 如果存在：
    //   最新的旧手指 = aimFinger
    //
    // 如果不存在：
    //   技能手指自己控制方向
    // --------------------------------------------------------

    int aimIndex = -1;

    uint64_t bestOldSerial = 0;

    for (int i = 0; i < count; ++i) {

        if (i == skillIndex)
            continue;

        if (!fingers[i].isDown)
            continue;

        // 必须是技能手指之前已经存在的
        if (fingers[i].downSerial >=
            skill.downSerial) {

            continue;
        }

        if (aimIndex < 0 ||
            fingers[i].downSerial >
            bestOldSerial) {

            aimIndex = i;

            bestOldSerial =
                fingers[i].downSerial;
        }
    }


    if (aimIndex >= 0) {

        const Touch::FingerInfo &aim =
            fingers[aimIndex];

        g_aim_finger.device =
            aim.device;

        g_aim_finger.slot =
            aim.slot;

        g_aim_finger.id =
            aim.id;

        g_aim_finger.downSerial =
            aim.downSerial;

        g_aim_finger.valid =
            true;

        // 已经存在视角手指
        g_home_x = aim.x;
        g_home_y = aim.y;

        printf(
            "[自瞄] State47：已有视角手指 "
            "device=%d slot=%d，技能手指 "
            "device=%d slot=%d\n",
            aim.device,
            aim.slot,
            skill.device,
            skill.slot
        );

    } else {

        // 没有提前存在的手指
        // 技能手指自己承担方向控制
        g_aim_finger =
            g_skill_finger;

        g_home_x =
            skill.x;

        g_home_y =
            skill.y;

        printf(
            "[自瞄] State47：无已有视角手指，"
            "技能手指自己控制方向 "
            "device=%d slot=%d\n",
            skill.device,
            skill.slot
        );
    }

    fflush(stdout);


    g_view_x =
        g_home_x;

    g_view_y =
        g_home_y;


    g_active.store(true);

    return true;
}


// ============================================================
// 检查已经锁定的手指是否还存在
// ============================================================

static bool locked_finger_alive(
    const LockedFinger &finger
) {

    if (!finger.valid) {
        return false;
    }

    if (!Touch::IsFingerDown(
            finger.device,
            finger.slot)) {

        return false;
    }

    return true;
}


// ============================================================
// 自动方向
// ============================================================

static void update_aim() {

    if (!g_active.load())
        return;

    if (!g_aim_finger.valid)
        return;


    // --------------------------------------------------------
    // 技能手指已经抬起
    // --------------------------------------------------------

    if (!locked_finger_alive(
            g_skill_finger)) {

        clear_lock(
            "技能手指 UP"
        );

        return;
    }


    // --------------------------------------------------------
    // 方向手指已经不存在
    //
    // 正常情况下，如果 aimFinger == skillFinger，
    // 上面已经一起判断了。
    // 如果是提前存在的视角手指，
    // 它中途抬起以后就不能继续写。
    // --------------------------------------------------------

    if (!locked_finger_alive(
            g_aim_finger)) {

        clear_lock(
            "方向手指 UP"
        );

        return;
    }


    const int my_camp =
        PySelf::camp();

    const TargetInfo *tgt =
        pick_target(my_camp);


    // 没目标：
    // 保持技能开始时方向，不继续乱动
    float fx =
        g_home_x;

    float fy =
        g_home_y;


    if (tgt) {

        float dx =
            tgt->psx - g_home_x;

        float dy =
            tgt->psy - g_home_y;

        float dlen =
            sqrtf(
                dx * dx +
                dy * dy
            );

        float radius =
            aim_radius.load();


        if (dlen > 1.0f) {

            fx =
                g_home_x +
                dx / dlen * radius;

            fy =
                g_home_y +
                dy / dlen * radius;
        }
    }


    // --------------------------------------------------------
    // 平滑移动
    // --------------------------------------------------------

    const float move_dx =
        fx - g_view_x;

    const float move_dy =
        fy - g_view_y;

    const float dist =
        sqrtf(
            move_dx * move_dx +
            move_dy * move_dy
        );


    const float rate =
        (float)aim_rate.load();


    // worker 约 2ms
    const float max_step =
        rate * 0.002f;


    if (dist <= max_step ||
        dist < 0.5f) {

        g_view_x = fx;
        g_view_y = fy;

    } else {

        g_view_x +=
            move_dx / dist *
            max_step;

        g_view_y +=
            move_dy / dist *
            max_step;
    }


    // --------------------------------------------------------
    // 修改已经存在的真实手指
    //
    // 不创建 DOWN
    // 不创建 UP
    // 不创建虚拟 slot
    // 不碰其他手指
    // --------------------------------------------------------

    if (!Touch::MoveExistingScreen(
            g_aim_finger.device,
            g_aim_finger.slot,
            g_view_x,
            g_view_y)) {

        clear_lock(
            "MoveExistingScreen失败"
        );
    }
}


// ============================================================
// Worker
// ============================================================

static void worker_thread() {

    printf(
        "[自瞄] 新版触摸跟随线程启动\n"
    );

    fflush(stdout);


    while (true) {

        // 2ms
        usleep(2000);


        if (g_screen_w <= 0 ||
            g_screen_h <= 0) {

            continue;
        }


        const bool want =
            enabled.load();


        // ----------------------------------------------------
        // AutoAim UI 关闭
        // ----------------------------------------------------

        if (!want) {

            if (g_active.load()) {

                clear_lock(
                    "AutoAim关闭"
                );
            }

            continue;
        }


        // ----------------------------------------------------
        // State != 47
        //
        // 这是技能生命周期的硬条件。
        // ----------------------------------------------------

        const int state =
            PyAttack::cur_state();


        if (state != 47) {

            if (g_active.load()) {

                clear_lock(
                    "State != 47"
                );
            }

            continue;
        }


        // ----------------------------------------------------
        // State == 47
        // ----------------------------------------------------

        if (!g_active.load()) {

            if (!lock_skill_fingers()) {

                // 极短暂同步窗口。
                // 下一轮继续尝试。
                continue;
            }

            continue;
        }


        // ----------------------------------------------------
        // 已经锁定
        // ----------------------------------------------------

        update_aim();
    }
}


// ============================================================
// start
// ============================================================

static void start() {

    static bool started = false;

    if (started)
        return;

    started = true;

    std::thread(
        worker_thread
    ).detach();
}


// ============================================================
// 屏幕尺寸
// ============================================================

static void ensure_inited(
    int sw,
    int sh
) {

    g_screen_w = sw;
    g_screen_h = sh;
}


// ============================================================
// Overlay
// ============================================================

static void DrawOverlay(
    ImDrawList *dl,
    int sw,
    int sh
) {

    if (!dl)
        return;


    ensure_inited(
        sw,
        sh
    );


    const float px =
        sw * 0.5f;

    const float py =
        sh * 0.5f;


    if (show_circle.load()) {

        ImU32 col =
            (ImU32)circle_color.load();

        dl->AddCircle(
            ImVec2(px, py),
            circle_r.load(),
            col,
            64,
            circle_thick.load()
        );
    }


    if (draw_ranges &&
        g_active.load()) {

        // 当前自动控制的真实手指位置
        dl->AddCircleFilled(
            ImVec2(
                g_view_x,
                g_view_y
            ),
            12.0f,
            IM_COL32(
                0,
                255,
                255,
                200
            )
        );


        // 屏幕中心
        dl->AddLine(
            {
                px - 15,
                py
            },
            {
                px + 15,
                py
            },
            IM_COL32(
                255,
                255,
                255,
                150
            ),
            2.0f
        );

        dl->AddLine(
            {
                px,
                py - 15
            },
            {
                px,
                py + 15
            },
            IM_COL32(
                255,
                255,
                255,
                150
            ),
            2.0f
        );
    }
}


// ============================================================
// 状态文字
// ============================================================

static const char *status_text() {

    static char buf[64];


    if (!enabled.load()) {

        snprintf(
            buf,
            sizeof(buf),
            "未开启"
        );

    } else if (
        PyAttack::cur_state() != 47
    ) {

        snprintf(
            buf,
            sizeof(buf),
            "等待技能"
        );

    } else if (
        !g_active.load()
    ) {

        snprintf(
            buf,
            sizeof(buf),
            "锁定手指中"
        );

    } else {

        snprintf(
            buf,
            sizeof(buf),
            "自瞄中"
        );
    }


    return buf;
}

} // namespace AutoAim

#endif
