#pragma once
#include "SecMath.h"   // ★ 敏感数学在 security 满档 TU
// ============================================================================
// AutoSkill.h —— 自动技能 + 自动飞轮
// ============================================================================
// 从 draw_Gui.cpp 外迁（2026-10-16）。包含：
//   · 配置（开关、触摸区、提前量、前摇表、优先级、连点）
//   · 逻辑（PyAttack 出刀回调 OnAttackDetected + 后台 worker 线程 AutoSkill_Worker）
//   · UI（"触摸"页的"自动技能"子页）
//   · 触摸红绿框的绘制（调整触摸区时可视）
//   · 监管前摇内置表（为自动技能的"提前量"服务）
//
// ⚠ 依赖（由 draw_Gui.cpp 提供，调用方需保证这些符号在链接时可见）：
//   · 全局 displayInfo / native_window_screen_x / native_window_screen_y
//   · 全局 g_diag_tap_x / y / ok / kind / count（诊断埋点，MCP 读）
//   · Touch::* / touch_init / touch_down / touch_up（TouchHelperA.h / KDA 驱动）
//   · PyAttack / PySelf / PyGenius（各功能模块的接口）
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <atomic>
#include <thread>
#include <unistd.h>
#include <sched.h>
#include <pthread.h>
#include <time.h>
#include <unordered_map>
#include <mutex>
#include <cmath>
#include <signal.h>   // sig_atomic_t / 退出时抬手用

#include "PyCore.h"
#include "PyRoot.h"
#include "PyAttack.h"
#include "PySelf.h"
#include "PyGenius.h"
#include "kerneldriver-paradise.hpp"
#include "TouchHelperA.h"
#include "imgui.h"
#include "theme.h"
#include "fang_ui.h"
#include "Security.h"    // ★ 授权闸门（AutoSkill 属于"自动类"，SOFT 阶段即关闭）

// ---- 宿主提供的全局（draw_Gui.cpp 中定义）----
extern android::ANativeWindowCreator::DisplayInfo displayInfo;
extern int native_window_screen_x;
extern int native_window_screen_y;
extern int g_diag_tap_x;
extern int g_diag_tap_y;
extern int g_diag_tap_ok;
extern int g_diag_tap_kind;
extern int g_diag_tap_count;

namespace AutoSkill {

// ---- 日志：默认 no-op（与 draw_Gui.cpp 里 LOG 的行为一致）----
#ifndef AUTOSKILL_LOG
#define AUTOSKILL_LOG 0
#endif
#if AUTOSKILL_LOG
#define AS_LOG(...) do { printf(__VA_ARGS__); fflush(stdout); } while (0)
#else
#define AS_LOG(...) do { } while (0)
#endif

// ============================================================================
// 配置
// ============================================================================
inline bool  g_auto_skill_enabled  = false;
inline int   g_auto_skill_tap_count     = 1;
inline int   g_auto_skill_tap_delay_ms  = 100;

inline bool  g_auto_flywheel_enabled = false;
inline bool  g_priority_flywheel     = false;

//   2026-10-26：**自身身份**（draw_Gui 每帧填，见 UpdateSelfFlags）
//   自动技能的额外识别 + 记者按住时长 都靠这几个标志。
inline bool g_self_is_report = false;      // 自己是记者
inline bool g_self_is_prof   = false;      // 自己是教授
inline bool g_self_has_xiang = false;      // 身上带着忘忧之香
inline bool g_self_has_flywheel = false;   // 自己带了飞轮（天赋飞轮效应）
inline bool g_self_fly_probed  = false;   // 有没有真正读到自己的天赋数据（没读到就不拦）
inline int  g_self_item_cnt  = 0;          // 身上道具个数
inline int  g_report_hold_ms = 0;   // 2026-10-26：滑条删了，默认不额外延时         // 记者按住技能的时长（1~1000ms）
inline bool g_report_up_only = false;   // 记者：自己按，辅助只松手
inline std::atomic<uint64_t> g_up_only_at{0};
/*   2026-10-26：**记者技能区拦截代理**
 *   用户在「技能触摸区」里按下 -> 这一路触摸被屏蔽区吃掉（游戏看不到），
 *   我们自己在同一坐标 VirtualDown 一路；之后手指怎么动就 VirtualMove 跟过去
 *   （屏蔽区是"按下瞬间锁定到抬起"，滑出区外仍然归我们管），
 *   到点由我们 VirtualUp —— 游戏全程只看到我们这一路 contact，不会有第二个触点。
 */
inline bool     g_report_proxy   = true;      // 开关（默认开，记者专用）
inline std::atomic<uint64_t> g_proxy_release_at{0};   // 到点松手的时间
inline std::atomic<int> g_proxy_presses{0};   // 代理按下次数
inline std::atomic<int> g_proxy_moves{0};     // 转发的移动次数
inline std::atomic<int> g_proxy_releases{0};  // 代理松手次数
inline std::atomic<int> g_proxy_applies{0};   // ApplyReleaseNow 调用次数
inline std::atomic<int> g_proxy_hold_ms{0};   // 本次已按住多久
inline std::atomic<int> g_up_only_sched{0};   // 排过几次延时UP
inline std::atomic<int> g_up_only_sent{0};    // 真发了几次UP
inline std::atomic<int> g_up_only_ok{0};      // touch_up 返回 true 的次数
inline int  g_report_lead_ms = 200;        // 记者提前量（只对记者生效）
inline bool g_self_unknown   = true;       // 自身身份读不到 -> 不做限制（fail-open）
//   2026-10-26：**回调链路计数**（fn=dbg_atk 里看）
//   回调进来了却没点技能，就看是哪一道提前 return 挡的。
inline std::atomic<int> g_as_enter{0};      // 进 OnAttackDetected 的次数
inline std::atomic<int> g_as_disabled{0};   // 两个开关都关
inline std::atomic<int> g_as_noinit{0};     // 触摸没初始化
inline std::atomic<int> g_as_busy{0};       // 上一发还没抬起来
inline std::atomic<int> g_as_stale{0};      // atk_at 没被清（2s 内）
inline std::atomic<int> g_as_camp{0};       // 自己被判成监管(camp==1)
inline std::atomic<int> g_as_disp{0};       // displayInfo 还没好
inline std::atomic<int> g_as_action{0};     // worker 真正决定要按
inline std::atomic<int> g_as_noready{0};    // 技能/飞轮都不到就绪
inline std::atomic<int> g_as_gate{0};       // Lic::QuickGate 关掉了
//   2026-10-26：自己技能CD的最近一次读数（fn=dbg_atk 里看，判断 skill_ready 为什么 false）
inline std::atomic<int>   g_own_ok{0};        // 能读到 CD 结构
inline std::atomic<int>   g_own_has{0};       // sk.has
inline std::atomic<int>   g_own_remain{0};    // x100
inline std::atomic<int>   g_own_total{0};     // x100
inline std::atomic<int>   g_own_ccur{0};
inline std::atomic<int>   g_own_cmax{0};
inline std::atomic<int64_t> g_own_skillid{0};

inline bool  g_touch_adjust_enabled = false;       // 由触摸页"调整触摸区域"模式驱动
inline bool  g_touch_rect_show    = true;          // 技能框显示（调整页里的开关）
inline bool  g_flywheel_rect_show = true;          // 飞轮框显示
inline float g_touch_rect_x = 0.f, g_touch_rect_y = 0.f;
inline float g_touch_rect_w = 200.f, g_touch_rect_h = 200.f;
inline bool  g_touch_rect_inited   = false;
inline bool  g_touch_rect_dragging = false;
inline float g_touch_drag_dx = 0.f, g_touch_drag_dy = 0.f;

inline float g_flywheel_rect_x = 0.f, g_flywheel_rect_y = 0.f;
inline float g_flywheel_rect_w = 200.f, g_flywheel_rect_h = 200.f;
inline bool  g_flywheel_rect_inited   = false;
inline bool  g_flywheel_rect_dragging = false;
inline float g_flywheel_drag_dx = 0.f, g_flywheel_drag_dy = 0.f;

inline bool  g_touch_inited = false;

inline int   g_skill_lead_time_ms          = 200;
inline int   g_skill_windup_fallback_ms    = 400;
inline std::string g_skill_windup_warned_for;

static constexpr int TOUCH_SLOT    = 6;
static constexpr int FLYWHEEL_SLOT = 7;
static constexpr int TAP_SLOTS     = 8;

// ============================================================================
// 每个 slot 独立的点击状态
//
// ★★ 2026-10-13 修：自动技能 / 自动飞轮**抢触摸** ★★
//   原根因：三个变量是"一个点击序列"的状态，但自动技能和飞轮是**两个序列**：
//     ① 飞轮先 down（pending_up=1, last_slot=FLYWHEEL）
//     ② 技能紧接着 down（**覆盖**成 pending_up=1, last_slot=TOUCH）
//     ③ 到点只 up 了 TOUCH -> 飞轮的虚拟手指一直按着 ✗
//   → 修法：按 slot 独立的状态机，两个序列**物理隔离**。
// ★ Channel：本次 down 走的**哪条通道** —— 抬起必须走同一条 ✗✗
//   `Touch::VirtualUp(slot)` 只清虚拟手指，不碰 KMA；
//   而 `send_down_at` 在虚拟手指不可用时会走 KMA 的 `touch_down` ✓
//   → 走 KMA 路径按下时，手指永远抬不起来 ✗
// ============================================================================
struct TapState {
    bool     pending_up     = false;
    uint64_t down_ms        = 0;
    int      remaining_taps = 0;
    uint64_t next_tap_ms    = 0;
    enum Channel { CH_NONE = 0, CH_VIRTUAL, CH_KMA };
    int      channel        = CH_NONE;
};
inline TapState g_taps[TAP_SLOTS];

//   ★ 2026-10-23：被"正常杀死"（SIGTERM/SIGINT）时也要把手指抬起来。
//   背景：用户实测"整台手机触摸都不对"——进程被 kill 时如果正好有一根
//   虚拟手指按下（pending_up 还没执行），内核输入状态里 BTN_TOUCH 会一直是 1，
//   **进程死了，手指还按着**。信号处理器只置标志（async-signal-safe），
//   真正的抬起在 worker 循环里做（那里能安全调用 ioctl）。
inline volatile sig_atomic_t g_term_request = 0;

inline void TapUp(int slot);   // 前置声明：定义在下面（Release 里要用）

inline void ReleaseAllPending()
{
    for (int s = 0; s < TAP_SLOTS; s++) {
        if (g_taps[s].pending_up) TapUp(s);
        g_taps[s].pending_up     = false;
        g_taps[s].remaining_taps = 0;
    }
}

inline TapState& TapOf(int slot) {
    if (slot < 0 || slot >= TAP_SLOTS) slot = 0;
    return g_taps[slot];
}
// 按"**当次使用的通道**"抬起 ✓（不能无脑两个通道都抬：会多发一次 up）
inline void TapUp(int slot) {
    TapState& st = TapOf(slot);
    if (st.channel == TapState::CH_KMA) {
        touch_up(slot);
    } else {
        Touch::VirtualUp(slot);
    }
    st.channel    = TapState::CH_NONE;
    st.pending_up = false;
}

// ============================================================================
// 监管前摇内置表 + 名字缓存
// ============================================================================
static const int MAX_BOSSES = 64;
inline std::atomic<int> g_boss_windup_normal[MAX_BOSSES];
inline std::atomic<int> g_boss_windup_charge[MAX_BOSSES];
inline char             g_boss_names[MAX_BOSSES][24];
inline int              g_boss_n = 0;
inline std::unordered_map<std::string, int> g_boss_name_idx;
inline std::mutex       g_hunter_name_mtx;
inline std::string      g_cached_hunter_name;

struct BossWindupEntry { const char* name; int normal_ms; int charge_ms; };
inline const BossWindupEntry g_boss_windup_table[] = {
    { "约瑟夫",     420,  980 },
    { "红蝶",       430,  820 },
    { "小丑",       250,  990 },
    { "红夫人",     590, 1420 },
    { "鹿头",       300, 1700 },
    { "杰克",       470, 1270 },
    { "蜘蛛",       370,  780 },
    { "孽蜥",       450, 1620 },
    { "黄衣之主",   480, 1090 },
    { "疯眼",       470, 1110 },
    { "信徒",       400,  730 },
    { "梦之女巫",   400,  730 },
    { "爱哭鬼",     370, 1080 },
    { "26号守卫",   380, 1640 },
    { "噩梦",       360, 1480 },
    { "使徒",       570, 1410 },
    { "小提琴家",   410, 1210 },
    { "雕刻家",     550, 1620 },
    { "破轮",       430, 1540 },
    { "博士",       760, 1940 },
    { "渔女",       320, 1800 },
    { "蜡像师",     500, 1200 },
    { "记录员",     410, 1100 },
    { "守夜人",     370,  860 },
    { "隐士",       400, 1150 },
    { "歌剧演员",   310,  850 },
    { "愚人金",     390,  960 },
    { "跛脚羊",     380, 1010 },
    { "喧嚣",       430,  970 },
    { "杂货商",     350, 1000 },
    { "台球手",     350, 1100 },
    { "女王蜂",     380, 1190 },
    { "心兽",       650, 1600 },
    { "牙医",       370, 1100 },
    { "白无常",     700, 1290 },
    { "黑无常",     320,  880 },
    { "厂长",       350, 2060 },
    { "时空之影",   350, 1160 },
};

// 读场景对象的类名（跟 draw_Gui.cpp 里那 5 跳指针链一致）
inline std::string read_scene_class_name(uint64_t obj) {
    if (!obj) return "";
    uint64_t chain_head = getPtr64(obj + 0xf8);
    if (!chain_head) return "";
    uint64_t class_name_obj = getPtr64(getPtr64(getPtr64(getPtr64(chain_head) + 0x8) + 0x20) + 0x20);
    if (!class_name_obj) return "";
    int len = getDword(class_name_obj + 0x10);
    if (len <= 0 || len >= 256) return "";
    uint64_t str_ptr = getPtr64(class_name_obj + 0x8);
    if (!str_ptr) return "";
    std::string s;
    s.resize(len);
    if (!vm_readv(str_ptr, &s[0], len)) return "";
    return s;
}

// 当前局内监管的中文名（去方括号），"" = 未识别/不在局内
inline std::string current_hunter_name() {
    uint64_t hs = 0;
    if (!PySelf::hunter_body(hs)) return "";
    std::string cls = read_scene_class_name(hs);
    if (cls.empty()) return "";
    const char* zh = getboss(cls.c_str());
    if (!zh || !zh[0]) return "";
    if (strcmp(zh, cls.c_str()) == 0) return "";
    std::string s = zh;
    if (s.size() >= 2 && s.front() == '[' && s.back() == ']')
        s = s.substr(1, s.size() - 2);
    return s;
}

// 内置表初始化
inline void InitBossList() {
    if (g_boss_n > 0) return;
    auto add_name = [](const char* raw) {
        if (!raw || !raw[0]) return;
        std::string s = raw;
        if (s.size() >= 2 && s.front() == '[' && s.back() == ']')
            s = s.substr(1, s.size() - 2);
        if (g_boss_name_idx.count(s)) return;
        if (g_boss_n >= MAX_BOSSES) return;
        snprintf(g_boss_names[g_boss_n], sizeof(g_boss_names[0]), "%s", s.c_str());
        g_boss_name_idx[s] = g_boss_n;
        g_boss_windup_normal[g_boss_n].store(0);
        g_boss_windup_charge[g_boss_n].store(0);
        g_boss_n++;
    };
    for (size_t i = 0; i < sizeof(boss_table) / sizeof(boss_table[0]); i++)
        add_name(boss_table[i].display);
    add_name("[白无常]"); add_name("[黑无常]"); add_name("[厂长]");
    add_name("[伊斯人]"); add_name("[时空之影]");

    for (size_t i = 0; i < sizeof(g_boss_windup_table) / sizeof(g_boss_windup_table[0]); i++) {
        const auto &e = g_boss_windup_table[i];
        auto it = g_boss_name_idx.find(e.name);
        if (it == g_boss_name_idx.end()) continue;
        int idx = it->second;
        g_boss_windup_normal[idx].store(e.normal_ms);
        g_boss_windup_charge[idx].store(e.charge_ms);
    }
}

// ============================================================================
// 时间
// ============================================================================
inline uint64_t TouchNowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// ============================================================================
// 找主频最高的核（性能大核）+ 绑核
// ============================================================================
inline int find_big_core() {
    int best_cpu = -1;
    long best_freq = 0;
    for (int i = 0; i < 16; i++) {
        char path[128];
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
        FILE* f = fopen(path, "r");
        if (!f) continue;
        long freq = 0;
        if (fscanf(f, "%ld", &freq) != 1) freq = 0;
        fclose(f);
        if (freq > best_freq) { best_freq = freq; best_cpu = i; }
    }
    return best_cpu;
}
inline void BoostCurrentThread() {
    int cpu = find_big_core();
    if (cpu >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(cpu, &set);
        sched_setaffinity(0, sizeof(set), &set);
    }
    struct sched_param param;
    param.sched_priority = 1;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
}

// ============================================================================
// 读自己的飞轮剩余 CD（秒）。false = 完全读不到（无论带没带都放行，避免误封）
// ============================================================================
inline bool GetOwnFlywheelRemain(float &remain) {
    static uint64_t last_log = 0;
    uint64_t self_scene = 0;
    if (!PySelf::anchor(self_scene)) {
        if (TouchNowMs() - last_log > 2000) {
            last_log = TouchNowMs();
            AS_LOG("[飞轮] anchor 失败\n");
        }
        return false;
    }
    PyGenius::Info gi;
    if (!PyGenius::lookup(self_scene, 0, 0, gi)) {
        if (TouchNowMs() - last_log > 2000) {
            last_log = TouchNowMs();
            AS_LOG("[飞轮] lookup 失败 self_scene=0x%llX\n", (unsigned long long)self_scene);
        }
        return false;
    }
    remain = PyGenius::flywheel_remain_now(gi);
    if (TouchNowMs() - last_log > 2000) {
        last_log = TouchNowMs();
        AS_LOG("[飞轮] has=%d remain=%.2f\n", gi.has_flywheel ? 1 : 0, remain);
    }
    return true;
}

// ============================================================================
// 读自己当前技能的 CD
// ============================================================================
/*   2026-10-26：**记者技能区松手**（每个 worker tick 调一次）
 *   这里不再注入任何按下 —— 用户自己的手指本来就正常送到游戏，
 *   我们只在出刀时刻让 Upload 对这个 slot 输出 TRACKING_ID=-1，等于替他松手。
 *   → 游戏全程只看到"一根手指按下、然后抬起"，不会有第二个触点。
 */
inline void ReportReleaseTick()
{
    const bool on = g_auto_skill_enabled && g_report_proxy;   // 不依赖记者识别
    Touch::SetReleaseZone(g_touch_rect_x, g_touch_rect_y,
                          g_touch_rect_w, g_touch_rect_h, on);
}

inline bool GetOwnSkillReady() {
    /*   2026-10-26：**记者不看技能CD**
     *   实测记者的相机技能结构读不出 CD（dbg_atk: 自身技能CD 读到=0 has=0），
     *   于是 skill_ready 永远 false -> 自动技能一次都不按。
     *   记者直接当就绪（她按下去就是拍照，没有"冷却中不能用"这回事）。
     */
    if (g_self_is_report) return true;
    uint64_t self_scene = 0;
    if (!PySelf::anchor(self_scene)) { AS_LOG("[技能] 无法获取自己的 scene_obj\n"); return false; }
    if (!self_scene) { AS_LOG("[技能] 自己的 scene_obj = 0\n"); return false; }
    PyGenius::OwnSkillCD sk{};
    if (!PyGenius::get_own_skill_cd(self_scene, sk)) {
        AS_LOG("[技能] 无法读取自己的技能CD\n");
        return false;
    }
    /*   2026-10-26：**total=0 也算就绪**
     *   实测（记者 id=3017）：has=1 但 remain=1.00 / total=0.00 ——
     *   说明这条技能的 CD 结构根本没填过（不是在冷却）。
     *   只看 remain<=0.05 会让它永远不就绪；total<=0.01 就当没 CD。
     */
    bool ready = sk.has && (sk.remain <= 0.05f || sk.total <= 0.01f);
    g_own_ok = 1;
    g_own_has = sk.has ? 1 : 0;
    g_own_remain = (int)(sk.remain * 100.0f);
    g_own_total  = (int)(sk.total  * 100.0f);
    g_own_ccur   = sk.charge_cur;
    g_own_cmax   = sk.charge_max;
    g_own_skillid = sk.skill_id;
    AS_LOG("[技能] skill_id=%lld remain=%.2f total=%.2f charge=%d/%d ready=%d\n",
        (long long)sk.skill_id, sk.remain, sk.total,
        sk.charge_cur, sk.charge_max, ready ? 1 : 0);
    return ready;
}

// ============================================================================
// 出刀上升沿回调（在 PyAttack 线程里执行）
// ============================================================================
inline std::atomic<uint64_t> g_auto_skill_attack_at{0};
inline std::atomic<int>      g_auto_skill_attack_windup{0};

inline void OnAttackDetected()
{
    bool any_busy = false;
    for (int s = 0; s < TAP_SLOTS; s++)
        if (g_taps[s].pending_up || g_taps[s].remaining_taps > 0) { any_busy = true; break; }

    AS_LOG("[攻击] enter en=%d t=%d busy=%d atk_at=%llu camp=%d\n",
        g_auto_skill_enabled ? 1 : 0, g_touch_inited ? 1 : 0,
        any_busy ? 1 : 0,
        (unsigned long long)g_auto_skill_attack_at.load(), PySelf::camp());

    g_as_enter.fetch_add(1);
    if (!g_auto_skill_enabled && !g_auto_flywheel_enabled) { g_as_disabled.fetch_add(1); return; }
    if (!g_touch_inited) { g_as_noinit.fetch_add(1); return; }
    if (any_busy) { g_as_busy.fetch_add(1); return; }
    //   ⚠ 2026-10-24：原来是 `if (atk_at != 0) return;` ——
    //     只要有一次 worker 没把 atk_at 清掉（读卡住/降级分支/卡在某个 syscall），
    //     之后**所有出刀都会被这里挡掉**，表现就是"局内只有第一次生效"。
    //   改成：超过 2 秒没被处理就当作过期，允许新的出刀覆盖。
    {
        const uint64_t prev = g_auto_skill_attack_at.load();
        if (prev != 0 && (TouchNowMs() - prev) < 2000) { g_as_stale.fetch_add(1); return; }
    }
    if (PySelf::camp() == 1) { g_as_camp.fetch_add(1); return; }
    if (displayInfo.width <= 0) { g_as_disp.fetch_add(1); return; }

    int windup = 0;
    std::string hn;
    {
        std::lock_guard<std::mutex> lk(g_hunter_name_mtx);
        hn = g_cached_hunter_name;
    }
    if (!hn.empty()) {
        auto it = g_boss_name_idx.find(hn);
        if (it != g_boss_name_idx.end()) {
            if (PyAttack::attack_is_charge())
                windup = g_boss_windup_charge[it->second].load();
            else
                windup = g_boss_windup_normal[it->second].load();
        }
    }
    // 查不到前摇就用兜底，**绝不能留 0**（0 会让提前量失效）
    if (windup <= 0) {
        windup = g_skill_windup_fallback_ms;
        if (g_skill_windup_warned_for != hn) {
            g_skill_windup_warned_for = hn;
            AS_LOG("[前摇] 「%s」不在内置前摇表里，本次用兜底 %d ms\n",
                hn.empty() ? "(读不到名字)" : hn.c_str(), windup);
        }
    }
    g_auto_skill_attack_windup.store(windup);
    AS_LOG("[前摇] 监管=「%s」 前摇=%dms 提前量=%dms -> 出刀后 %dms 按\n",
        hn.empty() ? "(读不到)" : hn.c_str(), windup, g_skill_lead_time_ms,
        (windup - g_skill_lead_time_ms) > 0 ? (windup - g_skill_lead_time_ms) : 0);
    g_auto_skill_attack_at.store(TouchNowMs());
}

// ============================================================================
// 后台 worker 线程
// ============================================================================
inline void AutoSkill_Worker()
{
    BoostCurrentThread();
    while (displayInfo.width <= 0 || displayInfo.height <= 0)
        usleep(20 * 1000);

    if (!g_touch_inited) {
        int touch_w = native_window_screen_x;
        int touch_h = native_window_screen_y;
        AS_LOG("[触摸] 准备初始化: display=%dx%d orient=%d window=%dx%d\n",
            displayInfo.width, displayInfo.height, displayInfo.orientation, touch_w, touch_h);
        bool kma_ok = touch_init(touch_w, touch_h);
        AS_LOG("[触摸] touch_init(%d,%d) -> %s\n", touch_w, touch_h, kma_ok ? "OK" : "FAIL");
        bool uinput_ok = Touch::IsUinputReady();
        g_touch_inited = kma_ok || uinput_ok;
        AS_LOG("[触摸] 触摸可用性: KMA=%d uinput=%d combined=%d\n",
            kma_ok ? 1 : 0, uinput_ok ? 1 : 0, g_touch_inited ? 1 : 0);
    }

    // 通用发送：给某个 slot 在某个框的中心点发送 down
    auto send_down_at = [](int slot, float rx, float ry, float rw, float rh, uint64_t now_ms) {
        float cx = rx + rw * 0.5f;
        float cy = ry + rh * 0.5f;
        const int vslot = slot;
        bool vdown_ok = false;
        if (Touch::IsVirtualActive(vslot)) Touch::VirtualUp(vslot);
        vdown_ok = Touch::VirtualDown(vslot, cx, cy);

        ::Vector2 bp = Touch::ScreenToBase(cx, cy);
        int tx = (int)bp.x, ty = (int)bp.y;

        bool kma_ok = false;
        if (!vdown_ok) kma_ok = touch_down(slot, tx, ty);

        g_diag_tap_x    = (int)cx;
        g_diag_tap_y    = (int)cy;
        g_diag_tap_ok   = (vdown_ok || kma_ok) ? 1 : 0;
        g_diag_tap_kind = (slot == FLYWHEEL_SLOT) ? 2 : 1;
        g_diag_tap_count++;
        AS_LOG("[埋点] 自动%s slot=%d 屏幕=(%d,%d) 虚拟手指=%d 基坐标=(%d,%d) KMA=%d 第%d次\n",
            (slot == FLYWHEEL_SLOT) ? "飞轮" : "技能", slot, (int)cx, (int)cy,
            vdown_ok ? 1 : 0, tx, ty, kma_ok ? 1 : 0, g_diag_tap_count);

        if (vdown_ok || kma_ok) {
            TapState& st = TapOf(slot);
            st.down_ms        = now_ms;
            st.pending_up     = true;
            st.remaining_taps = g_auto_skill_tap_count - 1;
            st.channel        = vdown_ok ? TapState::CH_VIRTUAL : TapState::CH_KMA;
            return;
        }

        // 两条路都失败才用 uinput 兜底（传**屏幕坐标**，不是基坐标）
        bool uinput_ok = Touch::SimulateTapScreen((int)cx, (int)cy, SecMath::TapUinputHoldMs());
        printf("[AutoSkill] 虚拟手指+KMA 都失败, uinput=%s screen=(%d,%d)\n",
               uinput_ok ? "OK" : "FAIL", (int)cx, (int)cy);
        fflush(stdout);
        TapState& st = TapOf(slot);
        st.pending_up     = false;
        st.remaining_taps = g_auto_skill_tap_count - 1;
        st.down_ms        = now_ms;
        st.channel        = TapState::CH_NONE;
    };

    while (true) {
        uint64_t now_ms = TouchNowMs();
        ReportReleaseTick();   // 2026-10-26：维护记者松手区

        // ★ 2026-10-23：收到 SIGTERM/SIGINT（MT 管理器停止、kill、系统回收）
        //   先把所有手指抬起来再退出 —— 否则内核输入状态里会留一根按下的手指，
        //   用户体感就是"整台手机触摸都坏了"（这个事故已经真实发生过一次）。
        if (AutoSkill::g_term_request) {
            AutoSkill::ReleaseAllPending();
            _exit(0);
        }

        // ★ 2026-10-23 授权闸门（自动类）：降级阶段**不产生任何触摸**。
        //   为什么放在循环最前面：自动技能是"对外部有副作用"的功能，
        //   降级时必须先断它（比停绘制更紧急，误触会被游戏端当成脚本特征）。
        //
        //   ☠☠ 这里修过一个真实事故：上一版直接 `continue`，如果降级发生在
        //   "手指已按下、等着抬起"的窗口里，抬起就永远不会执行 →
        //   **屏幕上留着一根按下不动的虚拟手指**，用户表现为"整台手机触摸都不对"。
        //   现在先**把所有挂起的 tap 补一个 up**，再停。
        if (!Lic::auto_ok()) {
            ReleaseAllPending();                        // 补抬起，绝不留手指按着
            if (g_auto_skill_attack_at.load() != 0) g_auto_skill_attack_at.store(0);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        // ---- 出刀前摇计时 ----
        uint64_t atk_at = g_auto_skill_attack_at.load();
        if (atk_at != 0) {
            bool any_busy = false;
            for (int s = 0; s < TAP_SLOTS; s++)
                if (g_taps[s].pending_up || g_taps[s].remaining_taps > 0) { any_busy = true; break; }

            if (!g_auto_skill_enabled && !g_auto_flywheel_enabled) {
                g_auto_skill_attack_at.store(0);
            } else if (any_busy) {
                g_auto_skill_attack_at.store(0);
            } else {
                int windup = g_auto_skill_attack_windup.load();
                /*   2026-10-26：记者的提前量单独一个滑条（g_report_lead_ms） */
                const int use_lead = (g_self_is_report && !g_self_unknown)
                                   ? g_report_lead_ms : g_skill_lead_time_ms;
                int delta  = windup - use_lead;
                if (delta < 0) delta = 0;
                uint64_t elapsed = now_ms - atk_at;
                if (elapsed >= (uint64_t)delta) {
                    g_auto_skill_attack_at.store(0);
                    bool gate_ok = PyAttack::gate_pass_now();
                    if (gate_ok) {
                        float fly_remain = 999.f;
                        bool fly_known   = GetOwnFlywheelRemain(fly_remain);
                        bool fly_ready   = fly_known && (fly_remain <= 0.1f);
                        bool skill_ready = GetOwnSkillReady();

                        bool use_flywheel = false;
                        bool do_action    = false;
                        //   独立闸门：只改 mask() 不会让这里恢复
                        if (!Lic::QuickGate()) { skill_ready = false; fly_ready = false; g_as_gate.fetch_add(1); }

                        /*   2026-10-26：**各自的身份门**
                         *   技能：只有 带着忘忧之香 / 自己是教授 / (自己是记者 且 道具>=1) 才放；
                         *   飞轮：只认"自己带了飞轮"，不看技能那套条件。
                         */
                        /*   2026-10-26：**fail-open**
                         *   身份读不到（换局中/刚启动）时一律放行；否则一票否决会把
                         *   自动技能和飞轮一起关死（实测就是这样全哑的）。
                         */
                        const bool sk_ok = g_self_unknown
                || g_self_has_xiang || g_self_is_prof
                || g_self_is_report;              // 记者本身不需要道具
                        const bool fw_ok = !g_self_fly_probed || g_self_has_flywheel;
                        /*   2026-10-26：**飞轮优先**（改成固定顺序）
                         *   记者这条技能现在永远就绪（跳过 CD 判定），
                         *   如果还按技能优先，飞轮永远轮不上 -> 表现就是飞轮没效果。
                         *   所以：飞轮真就绪就先用飞轮，没就绪才用技能。
                         */
                        if (g_auto_flywheel_enabled && g_auto_skill_enabled) {
                            if (fw_ok && fly_ready)        { use_flywheel = true;  do_action = true; }
                            else if (sk_ok && skill_ready) { use_flywheel = false; do_action = true; }
                        } else if (g_auto_flywheel_enabled) {
                            if (fw_ok && fly_ready) { use_flywheel = true; do_action = true; }
                        } else if (g_auto_skill_enabled) {
                            if (sk_ok) { use_flywheel = false; do_action = true; }
                        }

                        AS_LOG("[技能] 决策 skill_en=%d fly_en=%d skill_ready=%d fly_ready=%d fly_remain=%.2f use_fly=%d do=%d\n",
                            g_auto_skill_enabled ? 1 : 0, g_auto_flywheel_enabled ? 1 : 0,
                            skill_ready ? 1 : 0, fly_ready ? 1 : 0, fly_remain,
                            use_flywheel ? 1 : 0, do_action ? 1 : 0);

                        /*   2026-10-26：只松手模式 —— 不 down，排一次延时 UP */
                        /*   2026-10-26：**只发 UP 游戏不认**（实测：6 次全 slot 抬手全成功、
                         *   touch_up 返回 true，但游戏毫无反应 —— 它只处理"自己看到按下过"的
                         *   那个指针的抬手）。所以这里不再跳过按下：照常补一次 down，
                         *   再额外排一发全 slot UP —— 你手指按着也好，没按也好，到点都会放出去。
                         */
                        /*   2026-10-26：记者 + 松手模式（唯一入口，不再重复）
                         *   不注入按下（用户自己按着），只让 Upload 立刻松掉技能区里那根手指。
                         *   注意：不依赖"自己是记者"的识别 —— 那个识别不稳，
                         *   一旦为 false 整个松手就不生效（实测就是这样全哑的）。
                         */
                        if (do_action && !use_flywheel && g_report_proxy) {
                            Touch::SetReleaseCondition(true);
                            Touch::ApplyReleaseNow();      // 手指不动也要主动推一次
                            Touch::SetReleaseCondition(false);
                            g_proxy_applies.fetch_add(1);
                            g_proxy_releases.fetch_add(1);
                            g_as_action.fetch_add(1);
                            do_action = false;
                        }
                        if (do_action) { g_as_action.fetch_add(1); }
                        if (!do_action) { g_as_noready.fetch_add(1); }
                        if (do_action) {
                            if (use_flywheel) {
                                send_down_at(FLYWHEEL_SLOT,
                                             g_flywheel_rect_x, g_flywheel_rect_y,
                                             g_flywheel_rect_w, g_flywheel_rect_h,
                                             now_ms);
                            } else {
                                send_down_at(TOUCH_SLOT,
                                             g_touch_rect_x, g_touch_rect_y,
                                             g_touch_rect_w, g_touch_rect_h,
                                             now_ms);
                            }
                        }
                    }
                }
            }
        }

        /*   2026-10-26：只松手模式的延时 UP（不 down，只发抬手） */
        {
            const uint64_t up_at = g_up_only_at.load();
            if (up_at != 0 && now_ms >= up_at) {
                g_up_only_at.store(0);
                /*   2026-10-26：**把 0~4 号 slot 全抬一遍**
                 *   实测：只抬 TOUCH_SLOT 时 touch_up 返回 true、事件也确实发了，
                 *   但游戏没反应 —— 因为你的物理手指不在那个 slot 上（它是 0~n）。
                 *   这里把 0~4 全发一次 UP，谁身上有 contact 谁就被抬起来。
                 */
                /*   2026-10-26：**把所有 slot 的虚拟手指全清掉**
                 *   症状：松手时技能位置留着一个触摸点，导致下一次释放失败。
                 *   原因：按下走的是虚拟手指（VirtualDown），只抬 TOUCH_SLOT 不够 ——
                 *   别的模块（自瞄/盖板/校准）也可能在 0~9 里占着 slot，
                 *   残留的 contact 会挡在技能键上。
                 */
                for (int _s5 = 0; _s5 < 10; ++_s5) {
                    Touch::VirtualUp(_s5);
                    if (touch_up(_s5)) g_up_only_ok.fetch_add(1);
                }
                Touch::VirtualUp(TOUCH_SLOT);
                if (touch_up(TOUCH_SLOT)) g_up_only_ok.fetch_add(1);
                Touch::VirtualUp(FLYWHEEL_SLOT);
                if (touch_up(FLYWHEEL_SLOT)) g_up_only_ok.fetch_add(1);
                g_up_only_sent.fetch_add(1);
            }
        }

        // ---- 抬起 / 连点（按 slot 独立处理）----
        for (int slot = 0; slot < TAP_SLOTS; slot++) {
            TapState& st = g_taps[slot];
            const bool is_fly = (slot == FLYWHEEL_SLOT);

            /*   2026-10-26：**记者按住时长**
             *   默认抬手走 SecMath::TapReleaseDue（很短的一下）；自己是记者时改成
             *   "按满 g_report_hold_ms 再松手"，其他角色完全不受影响。
             */
            /*   2026-10-26：**卡住的抬手兜底**
             *   症状：只有第一次出刀会触发自动技能/飞轮，后面全哑。
             *   根因：第一次 down 之后 pending_up 没被抬起来（抬手判据没命中/通道异常），
             *   于是 any_busy 永远为真，OnAttackDetected 每次都在 `if (any_busy) return;` 掉。
             *   兜底：超过 1.5 秒还挂着就强制抬起来并清掉连点，保证下一刀还能触发。
             */
            if (st.pending_up && (now_ms - st.down_ms) > 1500) {
                TapUp(slot);
                st.pending_up     = false;
                st.remaining_taps = 0;
            }
            const bool _hold_custom = (!is_fly) && g_self_is_report && g_report_hold_ms > 0;
            const bool _release_due = st.pending_up &&
                (_hold_custom ? ((now_ms - st.down_ms) >= (uint64_t)g_report_hold_ms)
                              : SecMath::TapReleaseDue(now_ms, st.down_ms));
            if (_release_due && g_up_only_at.load() == 0) {
                TapUp(slot);
                if (st.remaining_taps > 0)
                    st.next_tap_ms = now_ms + (uint64_t)SecMath::TapGapMs(g_auto_skill_tap_delay_ms);
            }

            const bool slot_enabled = is_fly ? g_auto_flywheel_enabled
                                             : g_auto_skill_enabled;
            if (!slot_enabled) { st.remaining_taps = 0; continue; }

            if (!st.pending_up && st.remaining_taps > 0
                && now_ms >= st.next_tap_ms) {
                if (is_fly) {
                    send_down_at(FLYWHEEL_SLOT,
                                 g_flywheel_rect_x, g_flywheel_rect_y,
                                 g_flywheel_rect_w, g_flywheel_rect_h,
                                 now_ms);
                } else {
                    send_down_at(TOUCH_SLOT,
                                 g_touch_rect_x, g_touch_rect_y,
                                 g_touch_rect_w, g_touch_rect_h,
                                 now_ms);
                }
                g_taps[slot].remaining_taps--;
            }
        }

        usleep(2000);   // 500Hz 轮询
    }
}

// ============================================================================
// 启动（幂等）
// ============================================================================
inline void Start()
{
    static bool started = false;
    if (started) return;
    started = true;

    InitBossList();
    {
        int filled = 0;
        for (int i = 0; i < g_boss_n; i++)
            if (g_boss_windup_normal[i].load() > 0) filled++;
        printf("[前摇] 内置表已加载：%d 位监管，其中 %d 位有前摇数据\n", g_boss_n, filled);
        fflush(stdout);
    }

    std::thread(AutoSkill_Worker).detach();
    PyAttack::set_attack_callback(OnAttackDetected);

    std::thread([](){
        while (true) {
            std::string hn = current_hunter_name();
            {
                std::lock_guard<std::mutex> lk(g_hunter_name_mtx);
                g_cached_hunter_name = hn;
            }
            usleep(500 * 1000);
        }
    }).detach();
}

// ============================================================================
// 触摸框绘制（调整触摸区时可视）
// ============================================================================
inline void DrawOneTouchRect(ImDrawList* Draw,
                              bool enabled,
                              float& rx, float& ry, float& rw, float& rh,
                              bool& inited, bool& dragging,
                              float& drag_dx, float& drag_dy,
                              ImU32 color, const char* label)
{
    if (!enabled) return;

    if (!inited && displayInfo.width > 0 && displayInfo.height > 0) {
        rw = 200.f; rh = 200.f;
        rx = displayInfo.width  * 0.7f;
        ry = displayInfo.height * 0.6f;
        inited = true;
    }

    ImVec2 p1(rx, ry);
    ImVec2 p2(rx + rw, ry + rh);
    ImVec2 center((p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f);

    Draw->AddRect(p1, p2, color, 0.0f, 0, 3.0f);
    Draw->AddLine(ImVec2(center.x - 12, center.y), ImVec2(center.x + 12, center.y), color, 2.0f);
    Draw->AddLine(ImVec2(center.x, center.y - 12), ImVec2(center.x, center.y + 12), color, 2.0f);
    char buf[96];
    snprintf(buf, sizeof(buf), "%s (%.0f, %.0f)", label, center.x, center.y);
    Draw->AddText(ImVec2(p1.x, p1.y > 24 ? p1.y - 20 : p1.y + 4), color, buf);

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse = io.MousePos;
    bool inside = mouse.x >= p1.x && mouse.x <= p2.x && mouse.y >= p1.y && mouse.y <= p2.y;

    if (io.MouseDown[0]) {
        if (!dragging && inside) {
            dragging = true;
            drag_dx = mouse.x - p1.x;
            drag_dy = mouse.y - p1.y;
        }
        if (dragging) {
            rx = mouse.x - drag_dx;
            ry = mouse.y - drag_dy;
            if (rx < 0.f) rx = 0.f;
            if (ry < 0.f) ry = 0.f;
            if (rx + rw > displayInfo.width)  rx = displayInfo.width  - rw;
            if (ry + rh > displayInfo.height) ry = displayInfo.height - rh;
        }
    } else {
        dragging = false;
    }
}

inline void TouchRect_Draw(ImDrawList* Draw) {
    DrawOneTouchRect(Draw, g_touch_adjust_enabled && g_touch_rect_show,
                     g_touch_rect_x, g_touch_rect_y, g_touch_rect_w, g_touch_rect_h,
                     g_touch_rect_inited, g_touch_rect_dragging,
                     g_touch_drag_dx, g_touch_drag_dy,
                     IM_COL32(255, 0, 0, 255), "技能");
    DrawOneTouchRect(Draw, g_touch_adjust_enabled && g_flywheel_rect_show && g_auto_flywheel_enabled,
                     g_flywheel_rect_x, g_flywheel_rect_y, g_flywheel_rect_w, g_flywheel_rect_h,
                     g_flywheel_rect_inited, g_flywheel_rect_dragging,
                     g_flywheel_drag_dx, g_flywheel_drag_dy,
                     IM_COL32(0, 220, 120, 255), "飞轮");
}

// ============================================================================
// UI —— "调整触摸区域"页（再点一次左侧「触摸」进入）
//   每个区域一个可展开项：开关 = 显示/隐藏那个框；展开 = 调 XY / 大小。
//   框显示时**手指在框里按住拖动**就能改位置（DrawOneTouchRect 已支持）。
// ============================================================================
inline void DrawAdjustUI(Theme::Palette& pal) {
    using namespace FangUI;

    // ---- 技能（红）----
    if (Fold("技能触摸区（红）", &g_touch_rect_show, pal)) {
        ImGui::Indent(20.0f);
        { float v = g_touch_rect_x;
          if (Slider("za_sk_x", &v, 0.0f, 3000.0f, "X %.0f", pal)) g_touch_rect_x = v; }
        { float v = g_touch_rect_y;
          if (Slider("za_sk_y", &v, 0.0f, 1600.0f, "Y %.0f", pal)) g_touch_rect_y = v; }
        { float v = g_touch_rect_w;
          if (Slider("za_sk_s", &v, 50.0f, 800.0f, "大小 %.0f", pal)) {
              g_touch_rect_w = v; g_touch_rect_h = v;
          } }
        ImGui::TextDisabled("红框里按住拖动也能改位置");
        ImGui::Unindent(20.0f);
        ImGui::Dummy(ImVec2(0, Space::S));
    }

    // ---- 飞轮（绿）----
    if (Fold("飞轮触摸区（绿）", &g_flywheel_rect_show, pal)) {
        ImGui::Indent(20.0f);
        { float v = g_flywheel_rect_x;
          if (Slider("za_fw_x", &v, 0.0f, 3000.0f, "X %.0f", pal)) g_flywheel_rect_x = v; }
        { float v = g_flywheel_rect_y;
          if (Slider("za_fw_y", &v, 0.0f, 1600.0f, "Y %.0f", pal)) g_flywheel_rect_y = v; }
        { float v = g_flywheel_rect_w;
          if (Slider("za_fw_s", &v, 50.0f, 800.0f, "大小 %.0f", pal)) {
              g_flywheel_rect_w = v; g_flywheel_rect_h = v;
          } }
        ImGui::TextDisabled("绿框里按住拖动也能改位置（关掉「自动飞轮」时不画）");
        ImGui::Unindent(20.0f);
        ImGui::Dummy(ImVec2(0, Space::S));
    }
}

// ============================================================================
// UI —— "触摸"页的"自动技能"子页
// ============================================================================
inline void DrawUI(Theme::Palette& pal) {
    using namespace FangUI;

    Head("自动技能", pal);
    RowSwitch("自动技能", &g_auto_skill_enabled, pal);
    /*   2026-10-26：用户要求 —— 「自动飞轮」开关挪到「自动技能」下面紧挨着，
     *   两个开关放在一起才好一眼看全；飞轮的具体参数还在下面单独一段。
     */
    RowSwitch("自动飞轮", &g_auto_flywheel_enabled, pal);
    //   2026-10-26："自己按+只松手"已移除 ——
    //   实测：技能键上出现第二个触点时游戏会判释放失败，只能在辅助独家按住时才能放出。
    //   记者的释放时机统一用下面的「记者松手快慢」控制。
    if (g_auto_skill_enabled) {
        //   触摸区的位置/大小统一搬到「调整触摸区域」页（再点一次左侧「触摸」进入）
        ImGui::TextDisabled("触摸区：再点一次左侧「触摸」进调整页");

        /*   2026-10-26：**记者松手快慢**（一个滑条搞定）
         *   数值 = 提前多少毫秒松手：越大越早松，越小越贴着出刀那一刻。
         *   （原来的"提前量/按住时长"两个滑条删掉了，按住时长固定 0。）
         */
        if (g_self_is_report) {
            ImGui::Dummy(ImVec2(0, 8));
            Head("记者松手快慢", pal, "Report release");
            float lv = (float)g_report_lead_ms;
            if (Slider("rp_rel", &lv, 0.0f, 600.0f, "%.0f ms", pal))
                g_report_lead_ms = (int)(lv + 0.5f);
            ImGui::TextDisabled("越大越早松手：当前提前 %d ms", g_report_lead_ms);
        }

        // ---- 触发距离 ----
        ImGui::Dummy(ImVec2(0, 8));
        Head("触发距离", pal);
        float md = PyAttack::max_distance();
        if (Slider("md", &md, 0.0f, 50.0f, "%.0f m", pal))
            PyAttack::set_max_distance(md);
        ImGui::TextDisabled("50 = 不限制距离");

        // ---- 提前量 ----
        ImGui::Dummy(ImVec2(0, 10));
        Head("提前量", pal);
        {
            float lead = (float)g_skill_lead_time_ms;
            if (Slider("lead", &lead, 0.0f, 300.0f, "%.0f ms", pal))
                g_skill_lead_time_ms = (int)(lead + 0.5f);
            ImGui::TextDisabled("前摇结束前 %.0f ms 按下", lead);
        }

        // ---- 兜底前摇 ----
        ImGui::Dummy(ImVec2(0, 10));
        Head("兜底前摇", pal, "表里没有的监管");
        {
            float fb = (float)g_skill_windup_fallback_ms;
            if (Slider("windup_fb", &fb, 100.0f, 1000.0f, "%.0f ms", pal))
                g_skill_windup_fallback_ms = (int)(fb + 0.5f);
            ImGui::TextDisabled("表里查不到时按 %.0f ms 当前摇（需 > 提前量）", fb);
            {
                std::string hn;
                { std::lock_guard<std::mutex> lk(g_hunter_name_mtx); hn = g_cached_hunter_name; }
                int w = 0; bool found = false;
                if (!hn.empty()) {
                    auto it = g_boss_name_idx.find(hn);
                    if (it != g_boss_name_idx.end()) {
                        w = g_boss_windup_normal[it->second].load();
                        found = (w > 0);
                    }
                }
                if (hn.empty())  ImGui::TextDisabled("当前监管：读不到名字");
                else if (found)  ImGui::TextDisabled("当前监管：%s   表里前摇 %d ms", hn.c_str(), w);
                else             ImGui::TextDisabled("当前监管：%s   表里没有 -> 用兜底 %d ms",
                                                     hn.c_str(), g_skill_windup_fallback_ms);
            }
        }
        ImGui::TextDisabled("前摇数据已内置，无需手动配置");

        // ---- 触发条件 ----
        bool facing = PyAttack::require_facing();
        if (RowSwitch("只在监管面朝自己时触发", &facing, pal))
            PyAttack::set_require_facing(facing);

        bool skipb = PyAttack::skip_if_board_between();
        if (RowSwitch("隔着板子不放技能", &skipb, pal))
            PyAttack::set_skip_if_board_between(skipb);

        if (facing) {
            float cos_th = PyAttack::facing_cos_threshold();
            if (Slider("cos", &cos_th, -0.5f, 0.9f, "%.2f", pal))
                PyAttack::set_facing_cos_threshold(cos_th);
            ImGui::TextDisabled("0.3 = 约 72°；越大越严格");
        }

        // ---- 连点 ----
        float tc = (float)g_auto_skill_tap_count;
        if (Slider("tc", &tc, 1.0f, 8.0f, "%.0f 次", pal))
            g_auto_skill_tap_count = (int)(tc + 0.5f);
        float td = (float)g_auto_skill_tap_delay_ms;
        if (Slider("td", &td, 0.0f, 1000.0f, "%.0f ms", pal))
            g_auto_skill_tap_delay_ms = (int)(td + 0.5f);
        ImGui::TextDisabled("延迟只影响第 2 次起的点击");
    }

    // ---- 自动飞轮（开关在上面，这里只放参数）----
    if (g_auto_flywheel_enabled) {
        ImGui::Dummy(ImVec2(0, 14));
        Head("自动飞轮参数", pal);
        ImGui::TextDisabled("飞轮触摸区：再点一次左侧「触摸」进调整页");
    }

    // ---- 优先级 ----
    if (g_auto_flywheel_enabled && g_auto_skill_enabled) {
        ImGui::Dummy(ImVec2(0, 10));
        Head("优先级", pal);
        RowSwitch("优先飞轮(出刀时)", &g_priority_flywheel, pal);
        ImGui::TextDisabled(g_priority_flywheel
            ? "飞轮就绪 → 飞轮；否则 → 技能"
            : "技能就绪 → 技能；否则 → 飞轮");
    }
}

} // namespace AutoSkill