//微验网络验证//
//如果是AIDE编译jni，请将原main.cpp删除，将此注入好的文件改成main.cpp

#include <stdio.h>

// 日志总开关（Android.mk 里 -DENABLE_LOG=0 可关）。0 = stdout/stderr 全丢 /dev/null
#ifndef ENABLE_LOG
#define ENABLE_LOG 1
#endif
#include <unistd.h>
#include <sched.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>   // ★ mkdir/chmod（日志目录收紧权限用）
#include <dirent.h>
#include <pthread.h>
#include <fstream>
#include <string>
#include <string.h>
#include <time.h>
#include <malloc.h>
#include <iostream>
#include <sys/prctl.h>
#include <thread>
#include <chrono>
#include <errno.h>
#include <signal.h>

// ★★ 日志开关：ENABLE_LOG=0 时 MLOG 展开为空，连 printf 都不调
//   注意用 #if 而不是 #ifdef —— -DENABLE_LOG=0 也是"已定义"
#if ENABLE_LOG
  #define MLOG(...) do { printf(__VA_ARGS__); fflush(stdout); } while (0)
#else
  #define MLOG(...) do {} while (0)
#endif


#include <linux/input.h>
#include <sys/ioctl.h>

using namespace std;

#include "Android_draw/WenxinVerify.h"
#include "logdump.h"
#include "Android_draw/kerneldriver-paradise.hpp"
// ★ 反逆向 / 反调试 / 完整性自校验（2026-10-13）
#include "Android_draw/guard.h"
// ★ 运行时授权心跳（2026-10-13）—— 见 heartbeat.h 的设计说明
#include "Android_draw/heartbeat.h"
// ★ 2026-10-23 授权核心（租约/降级/会话密钥/看门狗）—— 见 Security.h
#include "Android_draw/Security.h"

#include "draw.h"
#include "AndroidImgui.h"
#include "GraphicsManager.h"

// 由 my_ui.cpp 维护：UI 完全隐藏且静止（供主循环决定是否整帧跳过）
namespace MyUI { extern bool g_ui_hidden_idle; }
#include "Android_draw/timer.h"
#include "build_entropy.h"

// ★ 触摸方式全局：1 = paradise/KMA 内核触摸（优先），2 = TouchHelperA
//   由启动时询问决定，运行期不变。auto_shock.h 里用 extern 引用。
int g_touch_mode = 1;
#include "McpBridge/mcp_bridge.h"   // ImGui-MCP 桥


extern int native_window_screen_x;
extern int native_window_screen_y;


timer DrawFPS;
float fps = 60;

long int value1;
long int value2;
long int value3;


// ============================================================
// 编译熵
// ============================================================

static volatile unsigned g_entropy_sink = 0;

static void touch_build_entropy() {

    for (unsigned char b : g_build_entropy) {
        g_entropy_sink += b;
    }

    g_entropy_sink +=
        static_cast<unsigned>(g_build_tag[0]);
}


// ============================================================
// 进程名
// ============================================================

//   ★ 2026-10-23：daemonize() 里拿不到 argc/argv，所以在 main() 开头先存一份。
//     只改 comm（prctl）不够 —— `ps` / `top` 默认看的是 argv[0]（cmdline），
//     不改的话后台列表里就是 "you.sh"（用户实测到的现象）。
static int    g_argv_saved_argc = 0;
static char** g_argv_saved      = nullptr;

static void spoof_process_name() {

    static const char *kDisguiseNames[] = {

        "kworker/u8:3",
        "kworker/0:2",
        "logd.auditd",
        "mdnsd",
        "vndservicemgr",
        "hwservicemanager",
        "wifi_forward",
        "statsd",

    };

    srand(
        static_cast<unsigned>(time(nullptr)) ^
        static_cast<unsigned>(getpid())
    );

    const char *name =
        kDisguiseNames[
            rand() %
            (sizeof(kDisguiseNames) /
             sizeof(kDisguiseNames[0]))
        ];

    prctl(
        PR_SET_NAME,
        name
    );

    // ★ 原地覆写 argv[0]（长度不超过原字符串，最安全：不动 argv 数组本身）
    //   效果：`ps -A -o ARGS` / `top` / `dumpsys activity` 里看到的是伪装名，
    //   而不是 /data/adb/1/.../you.sh。原路径只留在 /proc/pid/exe（root 才看得到）。
    if (g_argv_saved_argc > 0 && g_argv_saved && g_argv_saved[0]) {
        size_t cap = strlen(g_argv_saved[0]);
        if (cap > 1) {
            memset(g_argv_saved[0], 0, cap);
            size_t k = strlen(name);
            if (k > cap - 1) k = cap - 1;
            memcpy(g_argv_saved[0], name, k);
        }
    }

    // ★★ 让 Scudo 立即归还空闲内存（2026-10-10 内存优化）
    //
    //   实测：RSS 249MB 里有 **184MB 是单个 [anon:scudo:primary] 区域**，
    //   而程序自己的容器(ImGuiStorage/名字缓存/实体数组)全都稳定不涨 ——
    //   也就是说这些页**已经不是活对象了，只是分配器没还**。
    //   Scudo 的 primary 区有个特性：只要区里还剩**一个**活 chunk，
    //   **整块区域都不会归还**（所以单单调 M_PURGE 没用，它只释放全空区域）。
    //   真正管这个的是 **M_DECAY_TIME**：默认值会延迟/保留，
    //   设成 0 就是"一旦空闲立刻归还给系统"。
    //   代价是分配路径上多一点点工作，换来 RSS 不随峰值长期膨胀。
    //   （M_PURGE 的定期清理保留 —— 两者互补：DECAY_TIME 管"及时还"，
    //     PURGE 管"把已经攒下的也清掉"。）
#ifdef M_DECAY_TIME
    mallopt(M_DECAY_TIME, 0);
#endif
}


// ============================================================
// 调试日志
// ============================================================

// ============================================================================
// 2026-10-24：**把启动输出也落盘**（发布版同样生效）
//
//   问题：daemonize() 之后 stdout 被改到 /dev/null（发布版），
//         于是"验证成功/验证失败/卡密无效"这些**用户最需要看的信息全丢了**，
//         用户只会说"打不开"，我们手上没有证据。
//
//   做法：在 daemonize 之前 dup 一份终端 fd，之后用一个 tee 线程把
//         stdout/stderr 同时写回**终端**和 `/data/adb/.d/start.log`。
//         这样用户既能看见，也能把这个文件发给我们。
// ============================================================================
static int g_term_fd = -1;          // daemonize 之前的终端 stdout

static void SaveTerminalFd() {
    if (g_term_fd < 0) g_term_fd = dup(STDOUT_FILENO);
}

static void StartOutputTee() {
    static bool started = false;
    if (started) return;
    started = true;

    int p[2];
    if (pipe(p) != 0) return;

    std::string dir = GUARD_STR("/data/adb/.d");
    mkdir(dir.c_str(), 0700);
    int f = open((dir + GUARD_STR("/o.log")).c_str(),
                 O_WRONLY | O_CREAT | O_APPEND, 0600);

    if (f >= 0) {
        char head[128];
        int n = snprintf(head, sizeof(head),
                         "\n===== 启动 %lld =====\n", (long long)time(nullptr));
        if (n > 0) { ssize_t w = write(f, head, (size_t)n); (void)w; }
    }

    dup2(p[1], STDOUT_FILENO);
    dup2(p[1], STDERR_FILENO);
    close(p[1]);

    const int rfd = p[0];
    const int term = g_term_fd;
    std::thread([rfd, f, term]() {
        char buf[4096];
        ssize_t n;
        while ((n = read(rfd, buf, sizeof(buf))) > 0) {
            if (f >= 0)    { ssize_t w = write(f, buf, (size_t)n); (void)w; }
            if (term >= 0) { ssize_t w = write(term, buf, (size_t)n); (void)w; }
        }
        if (f >= 0) close(f);
    }).detach();
}

static void debug_log(const char *msg) {
    // ★ 2026-10-22：不再自己 fopen /data/local/tmp/hack_stdout.log
    //   那个路径在 /data/local/tmp（目录 771），任何拿到 shell 的 uid
    //   都能把整个日志读走 —— 日志里有特性名、偏移来源、扫描细节。
    //   现在统一走 stdout：daemonize() 会把它 dup2 到
    //     /data/adb/.d/o.log（目录 0700 + 文件 0600，只有 root 能读）
    //   代价是 daemonize() 之前那几句只出现在终端里，不再落盘。
#if !ENABLE_LOG
    (void)msg;
    return;
#else
    printf("%s\n", msg);
    fflush(stdout);
#endif
}


// ============================================================
// daemonize
//
// 调试版：
// stdout / stderr 都保存到日志。
// ============================================================

void daemonize() {

    debug_log(
        "[MAIN] daemonize() 开始"
    );


    pid_t pid = fork();


    if (pid < 0) {

        debug_log(
            "[MAIN] fork() 失败"
        );

        exit(1);
    }


    if (pid > 0) {

        debug_log(
            "[MAIN] 父进程退出，子进程继续"
        );

        exit(0);
    }


    debug_log(
        "[MAIN] 已进入子进程"
    );


    if (setsid() < 0) {

        debug_log(
            "[MAIN] setsid() 失败"
        );

        exit(1);
    }


    debug_log(
        "[MAIN] setsid() 成功"
    );


    spoof_process_name();


    if (chdir("/") < 0) {

        debug_log(
            "[MAIN] chdir(/) 失败"
        );

        exit(1);
    }


    // ========================================================
    // 不再把日志丢到 /dev/null
    // ========================================================

    // ⚠ 用 ENABLE_LOG 控制日志去向。
    //   光靠 draw_Gui.cpp 里的 LOG() 宏关日志是不够的 ——
    //   代码里还有大量裸 printf 绕过了那个宏，它们直接写 stdout。
    //   这里把 stdout/stderr 整个改道，才能做到"真的一点日志都不留"。
    //
    //   ☠☠ 2026-10-13 修：日志原来写在 /data/local/tmp/ 且用 **0666** 创建 ✗
    //     /data/local/tmp 是 771（人人可进），于是任何拿到 shell 的用户
    //     （uid 2000，**不需要 root**）都能读走整个日志 ✗
    //     日志里有：特性名、偏移来源、内部剖析统计、扫描细节 ——
    //     等于**把实现白皮书交给攻击者** ✗✗ 比逆向二进制省事一万倍 ✗
    //   改到 /data/adb/.d/（700 root）并用 0600 创建 ✓
    //   ⚠ 权限从 0666 改成 0600 是关键：
    //     0666 意味着"谁都能读、谁都能写" —— 攻击者还能**篡改**日志 ✗
// 2026-10-22: 恢复受 ENABLE_LOG 控制 —— 发布版必须走 #else
//   ENABLE_LOG=1 -> /data/adb/.d/{o,e}.log（erro.bin 依赖 o.log 尾部）
//   ENABLE_LOG=0 -> /dev/null，daemonize 之后所有裸 printf 自动消失
#if ENABLE_LOG
    const std::string kLogDir = GUARD_STR("/data/adb/.d");
    mkdir(kLogDir.c_str(), 0700);
    chmod(kLogDir.c_str(), 0700);
    const std::string kOutS = GUARD_STR("/data/adb/.d/o.log");
    const std::string kErrS = GUARD_STR("/data/adb/.d/e.log");
    const char* kOutPath = kOutS.c_str();
    const char* kErrPath = kErrS.c_str();
#else
    const char* kOutPath = "/dev/null";
    const char* kErrPath = "/dev/null";
#endif

    int fd1 =
        open(
            kOutPath,
            O_WRONLY |
            O_CREAT |
            O_APPEND,
            0600            // ★ 只有 root 能读写（原来是 0666 ✗）
        );


    int fd2 =
        open(
            kErrPath,
            O_WRONLY |
            O_CREAT |
            O_APPEND,
            0600            // ★ 只有 root 能读写（原来是 0666 ✗）
        );


    if (fd1 >= 0) {

        dup2(
            fd1,
            STDOUT_FILENO
        );

        if (fd1 != STDOUT_FILENO) {
            close(fd1);
        }
    }


    if (fd2 >= 0) {

        dup2(
            fd2,
            STDERR_FILENO
        );

        if (fd2 != STDERR_FILENO) {
            close(fd2);
        }
    }


    // stdin 仍然关闭
    int stdin_fd =
        open(
            "/dev/null",
            O_RDONLY
        );


    if (stdin_fd >= 0) {

        dup2(
            stdin_fd,
            STDIN_FILENO
        );

        if (stdin_fd != STDIN_FILENO) {
            close(stdin_fd);
        }
    }


    printf("[MAIN] daemonize() 完成 pid=%d\n", getpid());
    fflush(stdout);

    // ❌ 不在这里 CloseTerminal（2026-10-20 修正）
    //   后面还有 [驱动] 状态、[FATAL] 等需要显示到终端的诊断，
    //   提前关闭会让它们静默消失 ✗
    //   正确时机：init_My_drawdata() 之后、主循环之前 —— 见 main() ⑯ 段
}


// ============================================================
// main / you_main
//
//   2026-10-24：双形态构建
//     · PAYLOAD_AS_SO 未定义 → 普通可执行文件（老的 exec 方式）
//     · PAYLOAD_AS_SO 定义   → 共享库，导出 you_main()，
//       由壳用 android_dlopen_ext(USE_LIBRARY_FD) 从 memfd 直接加载
//       —— 全程不落盘，TLS/重定位/静态初始化交给系统 linker
// ============================================================

// ============================================================================
// 2026-10-24：把进程绑到**大核**
//
//   为什么要：这程序的主要开销是"驱动读 + 每帧绘制"，被调度到小核会明显掉帧。
//
//   做法：
//     1) 遍历 /sys/devices/system/cpu/cpuN/cpufreq/cpuinfo_max_freq
//     2) 跳过 offline 的核（hotplug 时绑到离线核会把线程饿死）
//     3) 取最高频那一档（>= 最高频的 92%）作为"大核簇"
//     4) sched_setaffinity(0) —— **必须在创建线程之前调用**，
//        之后 fork/新建的线程都会继承这个亲和性
//
//   有任何一步失败就**保持原样**（不设亲和性），绝不把进程绑死。
// ============================================================================
static void BindBigCores()
{
    struct CpuFreq { int id; long khz; };
    CpuFreq cpus[16];
    int n = 0;

    for (int i = 0; i < 16 && n < 16; ++i) {
        char path[128];
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
        FILE* f = fopen(path, "r");
        if (!f) continue;                    // 没有 cpufreq 节点 = 这个核不存在
        long khz = 0;
        if (fscanf(f, "%ld", &khz) != 1 || khz <= 0) { fclose(f); continue; }
        fclose(f);

        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/online", i);
        f = fopen(path, "r");
        int online = 1;                      // cpu0 通常没有 online 节点，默认在线
        if (f) { int v = 1; if (fscanf(f, "%d", &v) == 1) online = v; fclose(f); }
        if (!online) continue;

        cpus[n].id = i;
        cpus[n].khz = khz;
        ++n;
    }
    if (n == 0) return;

    /*   ★ 2026-10-24 改：绑**中大核**，不再只绑大核。
     *
     *   老做法：只留"最高频 92% 以上"的核 —— 在 1+3+4 的机器上只剩那颗 Prime，
     *   单核跑满时容易和 UI 抢，反而抖。
     *
     *   新做法：**按频率分簇，丢掉最低的那一簇（小核）**，中核+大核全留。
     *     1) 按频率升序排；
     *     2) 相邻频率相差 > 18% 就算换簇（同一簇内各核频率相同或几乎相同）；
     *     3) 簇数 >= 2 时，把最低簇（小核）剔掉。
     *   这样 4+4 / 1+3+4 / 2+2+4 这些布局都能自动适配，不用写死核号。
     */
    // 插入排序（n <= 16，够用，避免引入 <algorithm>）
    for (int i = 1; i < n; ++i) {
        CpuFreq key = cpus[i];
        int j = i - 1;
        while (j >= 0 && cpus[j].khz > key.khz) { cpus[j + 1] = cpus[j]; --j; }
        cpus[j + 1] = key;
    }

    int cluster_start = 0;                   // 最低簇的起始下标
    int clusters = 1;
    for (int i = 1; i < n; ++i) {
        if (cpus[i].khz * 100 > cpus[i - 1].khz * 118) {   // 涨了 18% 以上 = 新簇
            if (clusters == 1) cluster_start = i;          // 第一簇 = 小核，记下它的结束位置
            ++clusters;
        }
    }
    if (clusters < 2) cluster_start = 0;     // 只有一簇（如 8 个同频核）→ 全留

    cpu_set_t set;
    CPU_ZERO(&set);
    int cnt = 0;
    for (int i = cluster_start; i < n; ++i) { CPU_SET(cpus[i].id, &set); ++cnt; }
    if (cnt == 0) return;

    if (sched_setaffinity(0, sizeof(set), &set) == 0) {
#if ENABLE_LOG
        char buf[128]; int off = 0;
        off += snprintf(buf + off, sizeof(buf) - off, "[中/大核] 绑定 CPU:");
        for (int i = cluster_start; i < n && off < (int)sizeof(buf) - 8; ++i)
            off += snprintf(buf + off, sizeof(buf) - off, " %d", cpus[i].id);
        printf("%s （%d 簇，去掉最低簇 %d 核，共绑定 %d 核）\n",
               buf, clusters, cluster_start, cnt);
        fflush(stdout);
#endif
    }
}

#ifdef PAYLOAD_AS_SO
extern "C" __attribute__((visibility("default")))
int you_main(
    int argc,
    char *argv[],
    unsigned long long sec_blob       /* 壳交接的匿名页指针（已异或） */
) {
    Lic::SetupSecret(sec_blob);
#else
int main(
    int argc,
    char *argv[]
) {
#endif

    // ★ 2026-10-23：关掉 stdout 的块缓冲。
    //   原来重定向到文件时是全缓冲，任何 _exit() 都会把已经 printf 的内容**丢掉** ——
    //   排查"启动秒退、日志空白"时被这个坑了一小时（表现：日志里只有 "[MAIN] 进入 main()"）。
    //   发布版代价可忽略（日志本来就少），换来的可观测性值这个钱。
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    // ★ 2026-10-23：spoof_process_name() 在 daemonize() 里，拿不到 argv，
    //   这里先存一份（改写 argv[0] 用）。
    g_argv_saved_argc = argc;
    g_argv_saved      = argv;

    // ========================================================
    // ⓪ 反逆向 / 反调试 / 完整性自校验（2026-10-13）
    //
    //   为什么放**最前面**：
    //     必须在任何业务逻辑之前建立基线 —— 记录自身代码段摘要、
    //     探测环境（ptrace/Frida/注入）。晚一步做，攻击者就已经
    //     把补丁打好了 ✗
    //   ⚠ Init() 只会跑一次，且**不打印任何可被搜索的字符串** ✓
    // ========================================================
    // ---- 崩溃转储 / 终端输出基础设施（必须在最前面）----
    LogDump::SaveTerminal();          // 先占住终端 fd，daemonize 之后还能打印
    LogDump::TruncateLogs();          // o.log / e.log 只保留本次运行
    LogDump::InstallSignalHandlers(); // 闪退时自动写 erro.bin
    Guard::SetAbortHook(&LogDump::Fatal);

    Guard::Init();

    // ========================================================
    // ① 卡密验证
    // ========================================================

    // ★ 启动即检测：干净 -> 不打印；命中 -> Trip() 打印原因并退出
    Guard::Tick();

// 改成
    MLOG("[MAIN] 进入 main()\n");

    Wenxin::RunVerifyOrExit();

    // ★ 校验点 2：验证返回后立刻确认代码段没被改过 ✓
    //   （单点跳过 RunVerifyOrExit 的攻击者，会在这里被抓住）
    GUARD_CHECK("post-verify");

    // ★★ 启动运行时授权心跳（2026-10-13）★★
    //   为什么必须有：在这之前**验证只发生一次**，攻击者只要在启动时
    //   用 Frida hook 一次返回值就能永久使用 ✗
    //   心跳让授权变成"持续成立"的东西 ✓
    //   ⚠ 而且它的结果**必须真的影响功能**（见主循环里的 Authorized()）✗
    //     只打日志/只弹窗等于没做 ✗
    //
    //   ☠☠ 位置很关键：**必须在 daemonize() 之后** ✗✗
    //     我第一版放在这里（daemonize 之前），结果：
    //       - 线程是在**父进程**里创建的
    //       - daemonize() 之后**父进程退出**，线程跟着死 ✗
    //       - 子进程里根本没有这个线程，心跳一次都没发过 ✗
    //     证据：日志里只有 "[HB] 线程启动，等待首轮"，
    //           之后再无任何 [HB] 输出，计数器永远是 0 ✗
    //     教训：**fork 之前不要创建线程** —— fork 只复制调用线程 ✗


    // ★ 2026-10-13 反逆向：日志串也加密 —— 原来这条 "[MAIN] 卡密验证成功"
    //   是 `strings` 里最好的锚点（直接指向验证流程）✗ 用 GUARD_STR 包起来 ✓
    MLOG("%s", GUARD_STR("[MAIN] OK\n"));


    // ========================================================
    // ② 选择触摸设备
    //
    // 必须在 daemonize() 之前：
    //   当检测到多个 Type-B 触摸设备时，需要读 stdin
    //   让用户从终端选择。daemonize() 后 stdin 被重定向到
    //   /dev/null，就选不了了。
    //
    // 注意：SelectTouchDevice() 只读 /dev/input/eventX，
    //       不碰 binder，不影响 fork 安全性。
    // ========================================================

    // 改成
    MLOG("[MAIN] 开始 SelectTouchDevice()\n");
    if (!Touch::SelectTouchDevice()) {
        MLOG("[MAIN] SelectTouchDevice() 失败\n");
        return 1;
    }
    MLOG("[MAIN] SelectTouchDevice() 成功\n");

    // ★★ 让用户选触摸方式（1 paradise 内核 / 2 TouchHelperA）★★
    //   必须在这里问：daemonize() 之后 stdin 会被重定向到 /dev/null，就选不了了。
    //   ⚠ 用 `setsid nohup ./you.sh < /dev/null &` 启动时 stdin 是 /dev/null，
    //     fgets 立刻返回 NULL —— 必须兜住默认值，绝不能卡住启动。
    //   选择结果存进全局 g_touch_mode，自动震慑的点击/滑动全程按它走
    //   （不再做自动兜底）。自瞄(AutoAim)保留自己的兜底逻辑，不受影响。
    {
        printf("\n");
        printf("  选择触摸 1paradise（优先）                    2 Touch（有问题再使用这个）\n");
        printf("  输入 1 或 2 后回车: ");
        fflush(stdout);

        char _tb[32] = {0};
        bool _got = false;
        if (fgets(_tb, sizeof(_tb), stdin)) {
            for (int _i = 0; _tb[_i]; _i++) {
                if (_tb[_i] == 0x31) { g_touch_mode = 1; _got = true; break; }
                if (_tb[_i] == 0x32) { g_touch_mode = 2; _got = true; break; }
            }
        }
        printf("  -> 使用 %s%s\n\n",
               g_touch_mode == 2 ? "2 Touch (TouchHelperA)" : "1 paradise (内核触摸)",
               _got ? "" : "  [未读到输入，用默认]");
        fflush(stdout);
    }


    // ========================================================
    // ③ daemonize
    //
    // 必须先于 screen_config()：
    //   screen_config() 内部会通过 ANativeWindowCreator 触发
    //   binder 首次初始化（打开 /dev/binder、创建 ProcessState
    //   单例）。这个单例不是 fork-safe 的——如果父进程先初始化
    //   它，fork 出来的子进程里 binder 状态就坏了，createSurface
    //   会返回 EBADF 甚至直接 SIGSEGV。
    //
    //   所以：父进程绝对不碰 binder，一切 binder 操作都在
    //   子进程里从零开始。
    // ========================================================

    // 改成
    MLOG("[MAIN] 准备 daemonize()\n");


    SaveTerminalFd();   // 必须在 daemonize 之前：留一份终端 fd


    daemonize();

    // ★ 校验点 3：daemonize 之后（fork 过，镜像地址变了要重新确认）✓
    GUARD_CHECK("post-daemon");

    // ★★ 在这里（fork **之后**）才启动心跳线程 ★★
    //   fork 只复制调用线程，父进程里的线程到了子进程就没了 ✗
    //   所以线程必须在本进程（daemon 子进程）里创建 ✓
    // ★ 2026-10-22 重新启用（此前因误判被注释掉；误判根因已修：
    //   现在只有 BC_DEAD 才计数退出，BC_WARN 一律放行）
    // ★★ 2026-10-23：授权核心**必须先于**心跳启动（顺序不能反）★★
    //   理由：如果"进入计时状态"只发生在 HeartbeatStart 内部，
    //   攻击者把 HeartbeatStart 打成空函数，授权核心就永远不启动，
    //   于是所有闸门都会认为"心跳没启用 = 正常"——整套设计失效。
    //   现在 Start 在 main 里先跑，HeartbeatStart 怎么被打掉都不影响计时。
    Lic::Start();
    Wenxin::HeartbeatStart();

    // ★ 2026-10-23：授权看门狗（必须在本进程里起 —— 同上，fork 之后）
    //   它负责三件旧实现没有的事：
    //     ① 帧计数停了（渲染钩子被摘） → poison
    //     ② 心跳请求停了（线程被摘）   → poison
    //     ③ 关键函数序言被改（inline patch） → poison（随机延时后退出）
    Lic::WatchdogStart();


#if ENABLE_LOG
    StartOutputTee();        // 只有调试版才把 stdout 同时写回终端和 o.log
#endif

    BindBigCores();          // ★ 必须在起线程之前：之后 fork/新建的线程都继承

    printf(
        "[MAIN] daemonize() 返回\n"
    );

    fflush(stdout);


    // ========================================================
    // ④ 获取屏幕信息
    //
    // 现在在子进程里，binder 是干净的，第一次调用会正常
    // 初始化 ProcessState。
    // ========================================================

    printf(
        "[MAIN] 准备 screen_config()\n"
    );

    fflush(stdout);


    ::screen_config();


    printf(
        "[MAIN] screen_config() 完成\n"
    );

    fflush(stdout);


    // ========================================================
    // ⑤ 屏幕尺寸
    // ========================================================

    {

        int a =
            ::displayInfo.width;

        int b =
            ::displayInfo.height;


        int short_side =
            a < b ? a : b;


        int long_side =
            a < b ? b : a;


        bool is_portrait =
            ((::displayInfo.orientation % 2) == 0);


        ::native_window_screen_x =
            is_portrait
                ? short_side
                : long_side;


        ::native_window_screen_y =
            is_portrait
                ? long_side
                : short_side;


        ::abs_ScreenX =
            ::native_window_screen_x;


        ::abs_ScreenY =
            ::native_window_screen_y;


        printf(
            "[MAIN] display=%dx%d orientation=%d\n",
            ::displayInfo.width,
            ::displayInfo.height,
            ::displayInfo.orientation
        );


        printf(
            "[MAIN] native_window=%dx%d\n",
            ::native_window_screen_x,
            ::native_window_screen_y
        );


        fflush(stdout);
    }


    // ========================================================
    // ⑥ Paradise Driver
    // ========================================================

    printf(
        "[MAIN] 准备 InitKmaDriver()\n"
    );

    fflush(stdout);


    /*   2026-10-24：驱动库自己会打印
     *     install_driver_fd: fd=N installed successfully / fd not installed, ret=-1, errno=22
     *     识别到Paradise Driver | fd N
     *     Paradise: 无法连接驱动（未刷入或不可用）…
     *   发布版 stdout 已重定向到 /dev/null，所以这里**临时**把它指回终端，
     *   让这几行留在屏幕上；打完立即还原，其余日志仍然不输出。
     */
    {
        const int tfd = LogDump::TerminalFd();
        int saved = -1;
        if (tfd >= 0) {
            saved = dup(STDOUT_FILENO);
            if (saved >= 0) dup2(tfd, STDOUT_FILENO);
        }
        InitKmaDriver();
        fflush(stdout);
        if (saved >= 0) { dup2(saved, STDOUT_FILENO); close(saved); }
    }

    printf("[MAIN] InitKmaDriver() 返回\n");

    // ★★ 驱动状态（2026-10-20 改）★★
    //   需求：终端**始终**显示，o.log 跟随 ENABLE_LOG
    //   → 终端用 TermPrintOnly（只写 g_term_fd，不写 o.log）
    //   → 文件用 printf（STDOUT 已被 daemonize 重定向到 o.log）
    /*   2026-10-24：这两条也删了（连接状态由驱动库自己打印，见上）。 */

    fflush(stdout);


    // ========================================================
    // ⑦ 编译熵
    // ========================================================

    touch_build_entropy();


    printf(
        "[MAIN] touch_build_entropy() 完成\n"
    );

    fflush(stdout);


    // ========================================================
    // ⑧ 参数
    // ========================================================

    value1 = 970061201;
    value2 = 16384;
    value3 = 257;


    // ========================================================
    // ⑨ 创建 Graphics
    // ========================================================

    printf(
        "[MAIN] 准备 GraphicsManager::getGraphicsInterface(VULKAN)\n"
    );

    fflush(stdout);


    ::graphics =
        GraphicsManager::getGraphicsInterface(
            GraphicsManager::VULKAN
        );


    printf(
    "[MAIN] GraphicsManager 返回 graphics=%p\n",
    ::graphics.get()
);

    fflush(stdout);


    if (!::graphics) {

        printf(
            "[FATAL] graphics == nullptr\n"
        );

        fflush(stdout);

        return 1;
    }


    // ========================================================
    // ⑩ 创建 ANativeWindow
    // ========================================================

    printf(
        "[MAIN] 准备 Create ANativeWindow %dx%d\n",
        native_window_screen_x,
        native_window_screen_y
    );

    fflush(stdout);


    printf("[MAIN] >>> 进入 ANativeWindowCreator::Create\n");
fflush(stdout);

::window =
    android::ANativeWindowCreator::Create(
        "new_edition",
        native_window_screen_x,
        native_window_screen_y,
        permeate_record
    );

printf("[MAIN] <<< ANativeWindowCreator::Create 已返回\n");
fflush(stdout);

printf(
    "[MAIN] window=%p\n",
    (void *)::window
);
fflush(stdout);

// ★★ 主动向系统声明"本 surface 只跑 60fps"（2026-10-10 性能，实测有效）
//
//   背景（实测数据）：同一份 overlay，屏幕在 **120Hz** 时整机 CPU 100%，
//   把屏幕切到 **60Hz** 后降到 **70%** —— 那 30 个点是"系统合成链路跑在双倍频率"
//   的代价。而 overlay 自己只提交 60fps，多出来的合成周期纯属浪费。
//
//   项目里此前**从未**调用过 setFrameRate，所以系统一直按默认(120Hz)合成。
//   这个调用告诉 SurfaceFlinger "我们内容是固定 60fps"，SF 会据此选择匹配的
//   显示模式 —— **不必手动把整机锁到 60Hz**。
//
//   compatibility 用 FIXED_SOURCE：明确表示内容就是固定帧率，SF 可放心切模式。
//   ⚠ 需要 API 30+；低版本没有这个符号，用宏挡掉。
#if defined(__ANDROID_API__) && __ANDROID_API__ >= 30
    if (::window) {
        const int fr = ANativeWindow_setFrameRate(
            ::window, 60.0f, ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
        printf("[MAIN] setFrameRate(60) -> %d\n", fr);
        fflush(stdout);
    }
#endif


    printf(
        "[MAIN] ANativeWindowCreator::Create 返回 window=%p\n",
        (void *)::window
    );

    fflush(stdout);


    if (!::window) {

        printf(
            "[FATAL] window == nullptr\n"
        );

        fflush(stdout);

        return 1;
    }


    // ========================================================
    // ⑪ 初始化 Vulkan / Graphics
    // ========================================================

    printf(
        "[MAIN] 准备 graphics->Init_Render()\n"
    );

    fflush(stdout);


    graphics->Init_Render(
        ::window,
        native_window_screen_x,
        native_window_screen_y
    );


    printf(
        "[MAIN] graphics->Init_Render() 返回\n"
    );

    fflush(stdout);


    // ========================================================
    // ⑫ 初始化 Touch
    // ========================================================

    printf(
        "[MAIN] 准备 Touch::Init()\n"
    );

    fflush(stdout);


    Touch::Init(
        {
            (float)::abs_ScreenX,
            (float)::abs_ScreenY
        },
        false
    );


    printf(
        "[MAIN] Touch::Init() 返回\n"
    );

    fflush(stdout);


    // ========================================================
    // ⑬ 设置方向
    // ========================================================

    printf(
        "[MAIN] 准备 Touch::setOrientation(%d)\n",
        displayInfo.orientation
    );

    fflush(stdout);


    Touch::setOrientation(
        displayInfo.orientation
    );


    printf(
        "[MAIN] Touch::setOrientation() 完成\n"
    );

    fflush(stdout);


    // ========================================================
    // ⑭ 启动读取线程
    // ========================================================

    printf(
        "[MAIN] 准备启动 read_thread\n"
    );

    fflush(stdout);


    std::thread(
        read_thread,
        value1,
        value2,
        value3
    ).detach();


    printf(
        "[MAIN] read_thread 已启动\n"
    );

    fflush(stdout);


    // ========================================================
    // ⑭.5 启动 ImGui-MCP 桥
    //
    //   让 PC 上的 AI 能连进来读数据 / 改数据 / 截图 / 搭 UI 原型。
    //   端口与监听范围可以用环境变量覆盖（在 shell 里起进程时最方便）：
    //     DWRG_MCP_PORT=19790     监听端口
    //     DWRG_MCP_LAN=1          监听 0.0.0.0（同一 WiFi 直连）；
    //                             默认只监听 127.0.0.1，配合
    //                             `adb forward tcp:19790 tcp:19790`
    //     DWRG_MCP_RO=1           只读模式，禁止 AI 改设备数据
    //     DWRG_MCP_MAXW=900       截图长边上限
    //
    //   ★ 编译期开关 SHOW_MCP_PAGE（Android.mk 里 -DSHOW_MCP_PAGE=0/1）：
    //       0 = UI 不显示「MCP」页，并且这里**完全不启动** MCP 桥
    //           （不建监听线程、不开端口，等于这段代码不存在）
    //       1 = 正常启动（默认）
    // ========================================================

#if SHOW_MCP_PAGE
    {
        int  mcp_port = 19790;
        bool mcp_lan  = false;

        if (const char *p = getenv("DWRG_MCP_PORT")) {
            int v = atoi(p);
            if (v > 0 && v < 65536) mcp_port = v;
        }
        if (const char *p = getenv("DWRG_MCP_LAN")) {
            mcp_lan = (atoi(p) != 0);
        }
        if (const char *p = getenv("DWRG_MCP_RO")) {
            McpBridge::SetAllowWrite(atoi(p) == 0);
        }
        if (const char *p = getenv("DWRG_MCP_MAXW")) {
            int v = atoi(p);
            if (v >= 0) McpBridge::SetDefaultMaxWidth(v);
        }

        McpBridge::Start(mcp_port, mcp_lan);

        printf(
            "[MAIN] ImGui-MCP bridge 启动 port=%d lan=%d\n",
            mcp_port,
            (int)mcp_lan
        );

        fflush(stdout);
    }
#else
    printf("[MAIN] SHOW_MCP_PAGE=0 —— 本次构建不加载 ImGui-MCP\n");
    fflush(stdout);
#endif


    // ========================================================
    // ⑮ FPS
    // ========================================================

    DrawFPS.SetFps(
        fps
    );


    DrawFPS.AotuFPS_init();


    DrawFPS.setAffinity();


    printf(
        "[MAIN] FPS 初始化完成\n"
    );

    fflush(stdout);


    // ========================================================
    // ⑯ ImGui 绘制数据
    // ========================================================

    printf(
        "[MAIN] 准备 init_My_drawdata()\n"
    );

    fflush(stdout);


    ::init_My_drawdata();

printf(
    "[MAIN] init_My_drawdata() 完成\n"
);

fflush(stdout);

// ★★ 所有启动期诊断已经打完，关闭终端 fd（2026-10-20）★★
//   此刻已经输出的内容：
//     · [MAIN] xxx    → printf → o.log（或 /dev/null）✓
//     · [驱动] 状态   → TermPrintOnly → 终端 ✓
//     · [FATAL] 错误  → TermPrintOnly + printf（双通道）✓
//   关闭后：
//     · pty slave 释放，终端 app 收到 EOF
//       → 显示"[进程已结束 - 按回车关闭]" ✓
//     · 主循环内的 printf 仍写 o.log（ENABLE_LOG=1）
//       或 /dev/null（ENABLE_LOG=0）—— 都不影响终端 ✓
LogDump::CloseTerminal();
fflush(stdout);


    // ========================================================
    // ⑰ 主循环
    // ========================================================

    printf(
        "[MAIN] ================================\n"
    );

    printf(
        "[MAIN] 即将进入 ImGui 主循环\n"
    );

    printf(
        "[MAIN] ================================\n"
    );

    fflush(stdout);


    static bool flag = true;


    static double debug_last = 0.0;
static uint64_t debug_frames = 0;

// ★★ 主循环分段计时（2026-10-10）
//   渲染/主线程实测占单核 42.8%（60fps 下 = 每帧约 7ms），但不知道花在哪。
//   这次直接量：给每个阶段打 CPU 时间戳（CLOCK_THREAD_CPUTIME_ID，只算本线程），
//   每秒汇总一次。CPU 占用就是这么一分一分加起来的，量出来就不用猜了。
struct TimespecNs {
    static long long cpu() {
        struct timespec ts;
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
        return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    }
};
static long long s_acc_begin = 0, s_acc_new = 0, s_acc_ui = 0, s_acc_end = 0, s_acc_mcp = 0;
static long long s_tick_last = 0;
static int       s_tick_n = 0;

// ⚠ 这里曾经做过"完全隐藏时整帧跳过"的优化（2026-10-10），**已回退**。
//   原因：ESP 框是画在我们自己的 surface 上、靠 present 呈现的 ——
//   跳过整帧/降低 present 频率，等于把**绘制刷新**也一起降频了，
//   用户实测"滑动视角后对方的框要一秒才跟过来"。
//   而 drawBegin() 很可能就是抓取游戏画面的调用，跳过它会让镜像停在旧帧。
//   结论：**帧不能跳**。省 CPU 必须从别处下手，不能动这条链路。
static int s_skip_n = 0; (void)s_skip_n;

while (flag) {

    // ★★ 授权心跳门禁（2026-10-13）★★
    //   ★ 这是整套防护里**唯一真正让破解失去意义**的一环：
    //     心跳失效 -> **什么都不画**（不是弹提示让人 NOP 掉）✗
    //   ⚠ 必须放在 drawBegin() **之前**：
    //     放在帧中间 continue 会让 Begin/End 不配对，渲染直接坏掉 ✗
    //   ⚠ 循环继续跑（不退出进程）：一是便于网络恢复后自动恢复，
    //     二是"进程活着但没画面"比"进程秒退"更难判断哪一步被拦了 ✓
    // ★ 授权门禁（2026-10-22 恢复）：只有"服务端明确说账号失效"或
    //   "会话内从未验证成功"时才拦。拦的方式是跳过本帧绘制、不退出进程 ——
    //   网络恢复后能自动回来。BC_WARN（网络/超多开/顶号）一律放行。
    if (!Wenxin::Authorized()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
    }
    // ★★ 2026-10-23：上面这段现在由 Lic 阶梯接管（见 security.h）★★
    //   ① OnRenderFrame() **每帧都要调**：看门狗靠帧计数判断"渲染钩子被摘"
    //   ② 只有重度降级（ST_HARD/ST_LOCK）才跳过绘制；
    //      ST_SOFT 继续画 ESP/小地图，自动类由 mask 关掉
    //      —— 这样"拔网线"只掉一半功能，不会让正版用户瞬间全白
    Lic::OnRenderFrame();
    if (Lic::stage() >= Lic::ST_HARD) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
    }

    long long _t0 = TimespecNs::cpu();
    const bool frame_ready = drawBegin();
    long long _t1 = TimespecNs::cpu();

    // drawBegin() may have shut down the old Vulkan backend while rebuilding
    // the surface. Never start an ImGui/Vulkan frame unless recreation succeeded.
    if (!frame_ready || !graphics) {
        static unsigned int s_frame_not_ready = 0;
        ++s_frame_not_ready;
        if ((s_frame_not_ready % 30) == 1) {
            printf("[MAIN] drawBegin 未就绪，跳过本帧（累计=%u, graphics=%p, window=%p）\n",
                   s_frame_not_ready, (void*)graphics.get(), (void*)window);
            fflush(stdout);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
    }

    graphics->NewFrame();
    long long _t2 = TimespecNs::cpu();
    Layout_tick_UI(&flag);
    long long _t3 = TimespecNs::cpu();
    printf("[Frame] EndFrame 开始\n"); fflush(stdout);
    graphics->EndFrame();
    printf("[Frame] EndFrame 完成\n"); fflush(stdout);
    long long _t4 = TimespecNs::cpu();
    s_acc_begin += _t1 - _t0; s_acc_new += _t2 - _t1;
    s_acc_ui    += _t3 - _t2; s_acc_end += _t4 - _t3;
    s_tick_n++;

    // ★ ImGui-MCP：执行 AI 下发的命令。
    //   必须放在 ImGui 帧之外（EndFrame 之后、下一帧 NewFrame 之前），
    //   因为部分命令会操作 ImGui 窗口状态。
    McpBridge::Tick();
    { long long _t5 = TimespecNs::cpu(); s_acc_mcp += _t5 - _t4; }

    // ★★ 校验点 4：主循环里持续复验（2026-10-13）★★
    //   这是**最重要的一处** —— 攻击者常见的做法是"让程序跑起来，
    //   用 Frida 在运行时把验证函数的返回值改掉"。静态一次性校验
    //   挡不住这种 ✗ 主循环里持续自校验才能发现"内存被打了补丁" ✓
    //   ⚠ 每 64 帧才查一次：自校验要哈希整个代码段，别放每帧做 ✗
    //   （Guard::Check 内部也会节流环境探测，见 guard.cpp）
    if ((s_tick_n & 0x3F) == 0) GUARD_CHECK("loop");

    // ========================================================
    // ImGui 1.92 起字体图集改为「动态纹理」：
    //   - ImFontAtlas 不再暴露 TexPixelsAlpha8 / TexPixelsRGBA32；
    //   - 纹理的创建/更新/销毁由后端负责
    //     （本项目的 imgui_impl_vulkan 会置 ImGuiBackendFlags_RendererHasTextures
    //      并在每帧处理 ImGuiPlatformIO::Textures[]）；
    //   - 因此这里不再需要（也不能）手动 ClearTexData()。
    //     旧代码那一段在 1.92 里连编译都过不了。
    // 如果之后确实要回收 CPU 侧字形缓存，用的是：
    //   ImGui::GetIO().Fonts->CompactCache();
    // 而且只能在帧外调用。
    // ========================================================

    DrawFPS.SetFps((unsigned int)fps);

    float frame_ms = DrawFPS.AotuFPS();

    debug_frames++;

    double now = ImGui::GetTime();

        if (now - debug_last >= 1.0) {
#if ENABLE_LOG
        if (s_tick_n > 0) {
            const double n = (double)s_tick_n;
            printf("[主循环] 每帧 CPU: drawBegin=%.2fms NewFrame=%.2fms UI=%.2fms EndFrame=%.2fms Mcp=%.2fms | 合计=%.2fms (%d 帧)\n",
                   s_acc_begin / n / 1e6, s_acc_new / n / 1e6, s_acc_ui / n / 1e6,
                   s_acc_end / n / 1e6, s_acc_mcp / n / 1e6,
                   (s_acc_begin + s_acc_new + s_acc_ui + s_acc_end + s_acc_mcp) / n / 1e6, s_tick_n);
            fflush(stdout);
            s_acc_begin = s_acc_new = s_acc_ui = s_acc_end = s_acc_mcp = 0;
            s_tick_n = 0;
        }
        printf(
            "[FPS DEBUG] target=%.1f timer=%.1f imgui=%.1f frames=%llu\n",
            fps,
            frame_ms > 0.0f ? 1000.0f / frame_ms : 0.0f,
            ImGui::GetIO().Framerate,
            (unsigned long long)debug_frames
        );
        fflush(stdout);
#endif
        debug_frames = 0;
        debug_last = now;
    }
}


    // ========================================================
    // ⑱ 正常退出
    // ========================================================

    // ★ 退出前先停心跳并注销（type=15 用户退出）✓
    //   为什么要做：服务端按"在线名额"算多开数量，
    //   不注销的话这个名额会一直被占着 ✗
    //   一卡多用场景下名额被僵尸会话占满，别人就进不来了 ✗✗
    //   ⚠ 顺序很重要：**先 Stop 再 Logout** ——
    //     反过来的话心跳线程可能在我们注销后又发一次 type=8，
    //     等于刚退出又登录，名额又占回去了 ✗
    Wenxin::HeartbeatStop();
    Wenxin::HeartbeatLogout();

    printf(
        "[MAIN] 主循环退出\n"
    );

    fflush(stdout);


    graphics->Shutdown();


    printf(
        "[MAIN] graphics->Shutdown() 完成\n"
    );

    fflush(stdout);


    android::ANativeWindowCreator::Destroy(
        ::window
    );

    printf(
        "[MAIN] window Destroy 完成\n"
    );

    fflush(stdout);

    // ★ 强制结束进程（2026-10-20）
    //   read_thread 是 detach 的、McpBridge 有监听线程、
    //   HeartbeatStop 可能卡在网络 —— 主线程 return 后进程会挂着 ✗
    //   _exit(0) 直接走内核 exit，不等任何线程、不跑析构 ✓
    _exit(0);
}
