#ifndef IDV_KERNEL_DRIVER_KMA_HPP
#define IDV_KERNEL_DRIVER_KMA_HPP

#include "paradise_api.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mutex>

// ===== 全局唯一 driver / pid =====
inline paradise_driver* driver = nullptr;
inline pid_t            pid    = -1;

inline bool KmaReady() {
    return driver != nullptr;
}

// 驱动是否真的可用（能通一次 ioctl）。未刷入/版本不符时返回 false。
inline bool KmaAlive() {
    if (!driver) return false;
    int alive = 0;
    return driver->is_process_alive(getpid(), &alive) ? true : false;
}

inline void InitKmaDriver() {
    if (driver) return;
    driver = new paradise_driver();
    /*   2026-10-24：这两条删掉了。
     *   连接结果由驱动库自己打印（install_driver_fd / 识别到Paradise Driver），
     *   失败时它也会打 "Paradise: 无法连接驱动"，不需要我们再重复一遍。 */
    int alive = 0;
    (void)driver->is_process_alive(getpid(), &alive);
}

inline std::mutex g_kma_mtx;

inline bool touch_acquire(int /*who*/, int /*timeout_ms*/ = 300) { return true; }
inline void touch_release(int /*who*/) {}

// ---------- 进程 / 模块 ----------
inline int getPID(char* package_name) {
    if (!KmaReady()) return -1;
    if (!package_name || !*package_name) return -1;
    std::lock_guard<std::mutex> lk(g_kma_mtx);
    pid_t p = driver->get_pid(package_name);
    if (p > 0) {
        pid = p;
        driver->initialize(p);
    }
    return p;
}
inline int get_name_pid(char* package_name) { return getPID(package_name); }

inline bool PidExamIne() {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d", pid);
    return pid > 0 && access(path, F_OK) == 0;
}

inline long getModuleBase(char* module_name) {
    if (!KmaReady() || pid <= 0) return 0;
    std::lock_guard<std::mutex> lk(g_kma_mtx);
    return (long)driver->get_module_base(module_name);
}

// ---------- 内存读写 ----------
inline bool vm_readv(unsigned long address, void* buffer, size_t size) {
    if (!KmaReady()) return false;
    std::lock_guard<std::mutex> lk(g_kma_mtx);
    return driver->read(address, buffer, size);
}
inline bool vm_writev(unsigned long address, const void* buffer, size_t size) {
    if (!KmaReady()) return false;
    std::lock_guard<std::mutex> lk(g_kma_mtx);
    return driver->write(address, const_cast<void*>(buffer), size);
}

inline float getfloat(unsigned long address) {
    if (!KmaReady()) return 0.0f;
    std::lock_guard<std::mutex> lk(g_kma_mtx);
    return driver->read<float>(address);
}
inline float getFloat(unsigned long address) { return getfloat(address); }

inline int getdword(unsigned long address) {
    if (!KmaReady()) return 0;
    std::lock_guard<std::mutex> lk(g_kma_mtx);
    return driver->read<int>(address);
}
inline int getDword(unsigned long address) { return getdword(address); }

inline unsigned int getPtr32(unsigned int address) {
    if (!KmaReady()) return 0u;
    std::lock_guard<std::mutex> lk(g_kma_mtx);
    return driver->read<unsigned int>(address);
}
inline unsigned long getPtr64(unsigned long address) {
    if (!KmaReady()) return 0ul;
    std::lock_guard<std::mutex> lk(g_kma_mtx);
    return driver->read<unsigned long>(address) & 0x00FFFFFFFFFFFFFFUL;
}

inline void writedword(unsigned long address, int data) {
    if (!KmaReady()) return;
    std::lock_guard<std::mutex> lk(g_kma_mtx);
    driver->write<int>(address, data);
}
inline void writefloat(unsigned long address, float data) {
    if (!KmaReady()) return;
    std::lock_guard<std::mutex> lk(g_kma_mtx);
    driver->write<float>(address, data);
}

// ============================================================
// ---------- KMA 触摸 ----------
//
// ★ 重要安全变更：
//   - 移除 touch_raw_tap / touch_event（会在部分设备上触发内核崩溃）
//   - 增加 g_kma_touch_ok 守卫：只有 touch_init 成功后才允许 touch_down/up/move
//   - touch_init 失败时所有 KMA 触摸 API 直接返回 false，绝不触碰 driver
// ============================================================
static int  g_kma_touch_w      = 0;
static int  g_kma_touch_h      = 0;
static bool g_kma_touch_inited = false;   // 内部：init 已调用
static bool g_kma_touch_ok     = false;   // ★ 新增：init 成功后才为 true

inline bool touch_init(int abs_screen_x, int abs_screen_y) {
    if (!KmaReady()) return false;

    // 尺寸没变且之前成功过，直接返回
    if (g_kma_touch_inited &&
        g_kma_touch_w == abs_screen_x &&
        g_kma_touch_h == abs_screen_y) {
        return g_kma_touch_ok;
    }

    g_kma_touch_w = abs_screen_x;
    g_kma_touch_h = abs_screen_y;
    g_kma_touch_inited = true;
    g_kma_touch_ok = driver->touch_init(abs_screen_x, abs_screen_y);

    printf("[触摸] touch_init(%d,%d) -> %s\n",
           abs_screen_x, abs_screen_y, g_kma_touch_ok ? "OK" : "FAIL");
    fflush(stdout);

    return g_kma_touch_ok;
}

// ★ 查询 KMA 触摸是否可用（外部可根据这个决定是否走 uinput 兜底）
inline bool touch_kma_ok() { return g_kma_touch_ok; }

inline bool touch_down(int slot, int x, int y) {
    if (!KmaReady())     return false;
    if (!g_kma_touch_ok) return false;   // ★ 关键守卫：init 失败就不碰 driver
    return driver->touch_down(slot, x, y);
}
inline bool touch_up(int slot) {
    if (!KmaReady())     return false;
    if (!g_kma_touch_ok) return false;
    return driver->touch_up(slot);
}
inline bool touch_move(int slot, int x, int y) {
    if (!KmaReady())     return false;
    if (!g_kma_touch_ok) return false;
    return driver->touch_move(slot, x, y);
}

#endif