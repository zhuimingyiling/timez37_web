#pragma once
// ============================================================================
// PyScene.h —— 游戏阶段(大厅 / 准备 / 局内)判定，单一真相源
// ============================================================================
// 判据实现在 PyCore::detect_phase()：读 g_ui_mgr.ui_dict 里挂着哪个 UI 对象
//   大厅 = UIHall   准备 = UIRoomWait   局内 = UIBattle(另带 UIControlCustom)
// 来历与跨进程实测见 idv-reverse-resume SKILL「区分三个阶段」。这是 007 的 domain 域选择模式。
//
// 独立线程 500ms 一轮(阶段切换不需要更勤)，原子发布，和 PyProgress/PyGenius 一个套路。
// 绘制侧用 phase()/in_battle()/in_prepare()/in_lobby() 读，便宜(只走几跳 + 扫一个 1~2 key 的小 dict)。
// 跟已有的 units_by_type 键 1/2(uses_battle_world)可互为交叉校验。

#include <atomic>
#include <thread>
#include <unistd.h>
#include <cstdio>
#include <cstdint>
#include "PyCore.h"

namespace PyScene {

static uintptr_t         g_libbase = 0;
static std::atomic<int>  g_phase{PyCore::PHASE_UNKNOWN};   // 见 PyProgress.h 同名变量注释(原子保证发布)
static char              g_status[96] = "未启动";

static void refresh_once()
{
    if (g_libbase == 0) return;
    if (!PyRoot::ensure()) {                    // CPython 根还没就绪(热更后重定位中/页换出)
        g_phase = PyCore::PHASE_UNKNOWN;
        snprintf(g_status, sizeof(g_status), "%s", PyRoot::status_text());
        // ★ 2026-10-22：这里原来静默 return，导致外面完全看不到卡在哪一步。
        //   PyRoot::ensure() 的失败原因写在 status_text() 里，只有打出来才知道是
        //   "ELF 没读到，旧值也不对"（.PyRuntime 段没认出 -> libclient 版本变了）
        //   还是 "等待解释器(ELF槽 +0x...)"（槽位置对但内容还没就绪）。
        //   每 10 轮(≈5 秒)打一次，不刷屏。
        static int s_root_n = 0;
        if ((s_root_n++ % 10) == 0) {
            printf("[阶段] PyRoot 未就绪: %s\n", g_status);
            fflush(stdout);
        }
        return;
    }
    const char *reason = "";
    PyCore::Phase ph = PyCore::detect_phase(reason);
    g_phase = (int)ph;
    static const char *NAME[] = {"未知", "大厅", "准备", "局内"};
    snprintf(g_status, sizeof(g_status), "%s (%s)", NAME[(int)ph & 3], reason);

    // 阶段变化时打一行（不刷屏）—— 这是确认"判对了没有"的唯一手段：
    //   PyScene 的 g_status 只存内存，不打印的话外面永远看不到它判的是哪个阶段。
    static int s_last_phase = -1;
    if ((int)ph != s_last_phase) {
        s_last_phase = (int)ph;
        printf("[阶段] %s\n", g_status);
        fflush(stdout);
    }
}

static const int REFRESH_INTERVAL_MS = 500;

static void thread_main()
{
    while (true) {
        refresh_once();
        usleep(REFRESH_INTERVAL_MS * 1000);
    }
}

static void start(uintptr_t libbase)
{
    if (g_libbase != 0) return;
    g_libbase = libbase;
    snprintf(g_status, sizeof(g_status), "启动中");
    std::thread(thread_main).detach();
    printf("[阶段] 已启动 libbase=0x%lx\n", (unsigned long)libbase);
    fflush(stdout);
}

// ---------------- 给绘制侧用的只读接口 ----------------
static inline PyCore::Phase phase()       { return (PyCore::Phase)g_phase.load(); }
static inline bool          in_battle()   { return g_phase.load() == PyCore::PHASE_BATTLE; }
static inline bool          in_prepare()  { return g_phase.load() == PyCore::PHASE_PREPARE; }
static inline bool          in_lobby()    { return g_phase.load() == PyCore::PHASE_LOBBY; }
static inline const char   *status_text() { return g_status; }

} // namespace PyScene
