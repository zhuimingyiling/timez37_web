// ============================================================================
// CalibProbe.h —— 「自动校准」的**数据侦测**阶段（先只观测，不点击）★
//
// ── 需求（用户原话）──────────────────────────────────────────────────────
//   「玩求生修密码机时密码机会时不时弹出校准条，必须在**进度条黄色时**点下
//     校准键。校准键是固定的，**校准出现时间和校准进度条黄色区是随机的**。
//     让辅助每次都在自身修机时出现校准条时，自动在黄条区域时点下校准键。」
//   补充确认（很重要）：
//     · 校准条**随机出现**，摸一下不一定出，也可能直接出 ✓
//     · 校准键是**屏幕上的固定按钮** ✓
//     · **误点有代价 —— 会让密码机暂停** ✗✗
//   => 所以：**"没检测到就绝不点"**，误判率必须压到接近零 ✓
//
// ── 为什么不能只靠已有的 fix_process ────────────────────────────────────
//   `PyProgress` 已有的 `fix_process` 是**修机总进度(0~100)**，
//   而校准条是**独立的一次性 QTE**，跟总进度无关 ✗
//   自动校准需要两个**不同**的量：
//     ① 黄区位置（随机、一次性）    -> 决定"该在什么时候点"
//     ② 游标当前位置（每帧变）      -> 决定"现在是否落在黄区里"
//   这两个都还不知道在哪 -> 本文件就是**把它们找出来** ✓
//
// ── 方案（分级，优先零风险）─────────────────────────────────────────────
//   第 0 级：**在已有的 Python 对象里找**（只读内存，不碰反作弊敏感面）★
//     本项目读游戏数据一直靠"CPython 对象图遍历"，
//     校准条很可能就是某个对象上的字段，只是还没被发现 ✓
//     （和当初从对象图里挖出 support_trait 是同一个套路）
//   第 1 级：**后台常驻记录**（本文件的 Recorder）★
//     用户说了："校准出现时间不确定，你后台持续跑，我修一会出现校准，
//     我自己校准一遍完告诉你" ✓
//     => 所以不能做成"按一下抓一帧"，必须**常驻记录最近 N 秒的历史** ✓
//   第 2 级：硬件断点（用户已授权，但**只在前两级失败时才动用**）✗
//     项目注释明确写过"这游戏挂了 5 个网易安全模块，
//     硬件断点是反作弊重点监控的手段" ✗ 所以尽量不碰 ✓
// ============================================================================
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(__ANDROID__)
#include "PyCore.h"

//   需要 PySelf::g_module_dict（game_kernel 的字典）—— 由 draw_Gui.cpp 提供 ✓
//   ⚠ 不直接 include PySelf.h：那个头文件很大，而且会和 draw_Gui.cpp 里的
//     同名 static 变量冲突风险 ✗ 所以用"外部提供"的方式 ✓
namespace CalibProbe { bool HostModuleDict(uint64_t* out); }
#endif

namespace CalibProbe {

static constexpr int HIST       = 1200;   // 60Hz x 20 秒 ✓
static constexpr int MAX_FIELDS = 96;
static constexpr int MAX_GEN    = 16;
static constexpr int NAME_CAP   = 40;

// ---------------------------------------------------------------------------
// 一个被跟踪的字段
// ---------------------------------------------------------------------------
struct FieldTrack {
    char     owner[24];         // 哪台机器（obj 地址后几位）
    char     name[NAME_CAP];    // Python 属性名
    uint64_t slot;              // 属性的 value_slot 地址
    //   ⚠ **缓存 slot 安全，缓存"值指针"不安全** ✗
    //     属性每次被赋值都换一个新的 float 对象 -> slot 里的指针会变，
    //     但 **slot 本身（dict entry 里的那个位置）是稳定的** ✓
    //     所以每帧重新解引用 slot ✓（PyProgress 就是这么做的）
    float    hist[HIST];
    short    n_samp;            // 已采样个数（环形，满了就是 HIST）
    short    head;              // 环形写指针
};

struct Recorder {
    FieldTrack f[MAX_FIELDS];
    int        n = 0;

    uint64_t   gen_obj[MAX_GEN] = {0};   // 找到的密码机对象
    int        gen_n = 0;

    bool       active   = false;
    int        ticks    = 0;             // 总采样次数
    int        disc_cnt = 0;             // 登记过几次
    //   ★ 自动重试发现用的计数器 ✓
    //     用户按"开始记录"时可能还没解析出 game_kernel（**实测就遇到了**）✗
    //     所以每 30 帧重试一次，成功就自动接上 ✓
    //     好处：用户不用再按一次，也不打断他修机 ✓
    int        retry_tick = 0;
    char       status[160] = "未开始";

    void Reset() {
        n = 0; gen_n = 0; ticks = 0; disc_cnt = 0; active = false;
        retry_tick = 0;
        for (int i = 0; i < MAX_FIELDS; i++) { f[i].n_samp = 0; f[i].head = 0; }
        snprintf(status, sizeof(status), "已重置");
    }

    FieldTrack* Add(const char* owner, const char* name, uint64_t slot) {
        if (n >= MAX_FIELDS) return nullptr;
        for (int i = 0; i < n; i++)                 // 已有同 slot 的复用 ✓
            if (f[i].slot == slot) return &f[i];
        FieldTrack& t = f[n++];
        memset(&t, 0, sizeof(t));
        snprintf(t.owner, sizeof(t.owner), "%s", owner);
        snprintf(t.name,  sizeof(t.name),  "%.*s", NAME_CAP - 1, name);
        t.slot = slot;
        return &t;
    }
};

inline Recorder& R() { static Recorder r; return r; }

// ===========================================================================
// ★★ 读一个字符串对象的短名字（**不用 PyCore::read_str_any**）★★
//   ☠☠ 为什么不用现成的 read_str_any ✗✗
//     实测矛盾（同一批地址、同一个进程）：
//       · 硬编码 `vm_readv(addr + 0x30, buf, len)`  -> **读得出** "__name__" ✓
//       · 循环里 `PyCore::read_str_any(getPtr64(kp+16), buf, sizeof(buf))`
//         -> **一个都读不出**（同一次 dump 里两种写法并存，结果不同）✗
//     我没有深究它为什么（可能是编译期数组尺寸相关的重载/内联差异）✗
//     但它**卡住了整个功能** -> 换一条已验证可行的路 ✓
//   这条路就是 ImGui 诊断里验证过的那条：
//     读 len(+0x10) -> 读 state(+0x20) -> 判 kind/compact -> 从 +0x30 读内容 ✓
//   ⚠ 只处理 **compact** 字符串（Python 里几乎全部是）✗
//     非 compact 的直接放弃 —— 我们只需要读属性名，compact 足够 ✓
//   ⚠ 只接受**可打印 ASCII** -> 天然过滤掉把二进制垃圾当名字的情况 ✓
// ===========================================================================
inline bool ReadName(uint64_t s, char* out, size_t cap)
{
#if defined(__ANDROID__)
    out[0] = 0;
    if (!PyCore::is_obj(s) || cap < 2) return false;
    int64_t len = 0;
    uint32_t state = 0;
    if (!vm_readv(s + 0x10, &len, 8)) return false;
    if (!vm_readv(s + 0x20, &state, 4)) return false;
    if (len <= 0) return false;
    const int kind    = (state >> 2) & 7;
    const int compact = (state >> 6) & 1;
    if (kind > 2 || !compact) return false;
    if ((size_t)len >= cap) len = (int64_t)cap - 1;
    if (!vm_readv(s + 0x30, out, (size_t)len)) { out[0] = 0; return false; }
    out[len] = 0;
    for (int64_t t = 0; t < len; t++) {
        const unsigned char c = (unsigned char)out[t];
        if (c < 0x20 || c > 0x7E) { out[0] = 0; return false; }
    }
    return true;
#else
    (void)s; (void)out; (void)cap; return false;
#endif
}

// ===========================================================================
// 找出所有 GeneratorUnit，并把它们的**全部数值字段**登记进跟踪表
//   ⚠ 只在 active 且**还没登记过**时做一次 ✗
//     对象地址一局内稳定，每帧重新遍历整张表太贵 ✓
//   ⚠ 与 PyProgress 用**同一个布局 / 同一个 type 号(3)** ✓
//     但这里只读、不插手它的缓存变量 ✓（各读各的，互不影响）
// ===========================================================================
inline bool Discover(char* err, size_t errn)
{
#if defined(__ANDROID__)
    Recorder& r = R();

    uint64_t ud = 0;
    if (!HostModuleDict(&ud) || !PyCore::is_obj(ud)) {
        snprintf(err, errn, "拿不到 game_kernel 字典（等下一帧）");
        return false;
    }
    //   ★★☠☠ 关键修正：`units_by_type` **不在 game_kernel 里** ✗✗
    //     实测把 game_kernel 的 136 个键**全部列出来**之后才看到：
    //       [25] g_avatar   [27] g_unit   [31] unit_mgr   [45] g_cam_ctrl …
    //     **没有 units_by_type** ✗
    //     它在 **unit_mgr**（下标 31）里面 —— 是**两级**：
    //       game_kernel["unit_mgr"] -> 那个对象 / 它的 dict -> ["units_by_type"]
    //     PySelf.h 里就是这么拿的：
    //       `g_i_unit_mgr = find_key(g_module_dict, "unit_mgr", …)`
    //       `uint64_t um = get_attr(g_module_dict, g_i_unit_mgr)`
    //     => 我第一版只查了一级，所以**永远找不到** ✗
    //        我为此白折腾了好几轮（还去怀疑掩码、字符串读取）✗
    //   ⚠ 要不要走 unit_mgr 的**实例字典**：unit_mgr 是个对象，
    //     units_by_type 可能在它的 dict 里（`inst - 0x18`）✓
    //     所以两级都试：对象自身的 dict，以及对象本身当 dict 读 ✓
    uint64_t ubt = 0;
    {
        const int64_t i_um = PyCore::find_key(ud, "unit_mgr", -1);
        const uint64_t um  = (i_um >= 0) ? PyCore::get_attr(ud, i_um) : 0;
        if (PyCore::is_obj(um)) {
            //  ① 把 um 当 dict 读（unit_mgr 可能是模块/类字典）✓
            const int64_t a = PyCore::find_key(um, "units_by_type", -1);
            if (a >= 0) ubt = PyCore::get_attr(um, a);
            //  ② 读它的实例字典（对象 - 0x18）✓
            if (!PyCore::is_obj(ubt)) {
                const uint64_t umd = getPtr64(um - 0x18) & 0x00FFFFFFFFFFFFFFULL;
                if (PyCore::is_obj(umd)) {
                    const int64_t b = PyCore::find_key(umd, "units_by_type", -1);
                    if (b >= 0) ubt = PyCore::get_attr(umd, b);
                }
            }
        }
    }
    if (!PyCore::is_obj(ubt)) {
        snprintf(err, errn,
                 "找不到 units_by_type（i_um 走 unit_mgr 两级都没拿到；"
                 "um=0x%llx）",
                 (unsigned long long)0);
        return false;
    }

    PyCore::Dict k;
    if (!PyCore::read_dict(ubt, k)) { snprintf(err, errn, "units_by_type 结构异常"); return false; }

    uint64_t lst = 0;
    for (int64_t i = 0; i < k.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(k, i));
        kp &= 0x00FFFFFFFFFFFFFFULL;          // ★ 掩码不能漏 ✗（见上面注释）
        if (!PyCore::is_obj(kp) || getPtr64(kp + 8) != PyRoot::g_int_type) continue;
        int64_t sz = 0; uint32_t dg = 0;
        vm_readv(kp + 16, &sz, 8);
        vm_readv(kp + 24, &dg, 4);
        if (sz == 1 && (int)dg == 3) {              // 3 = GeneratorUnit ✓
            lst = getPtr64(PyCore::value_slot(k, i));
            lst &= 0x00FFFFFFFFFFFFFFULL;           // ★ 值指针同样要掩 ✗
            break;
        }
    }
    if (!PyCore::is_obj(lst)) { snprintf(err, errn, "没有密码机列表（不在局内？）"); return false; }

    int64_t cnt = 0;
    vm_readv(lst + 0x10, &cnt, 8);                  // ob_size
    uint64_t items = getPtr64(lst + 0x18);          // ob_item
    if (cnt <= 0 || cnt > 64 || !PyCore::is_obj(items)) { snprintf(err, errn, "密码机列表异常"); return false; }
    if (cnt > MAX_GEN) cnt = MAX_GEN;

    r.gen_n = 0;
    r.n     = 0;
    for (int64_t i = 0; i < cnt; i++) {
        uint64_t inst = getPtr64(items + i * 8);
        if (!PyCore::is_obj(inst)) continue;
        uint64_t d = getPtr64(inst - 0x18);
        PyCore::Dict dk;
        if (!PyCore::read_dict(d, dk)) continue;

        r.gen_obj[r.gen_n++] = inst;

        char owner[24];
        snprintf(owner, sizeof(owner), "gen%llx", (unsigned long long)(inst & 0xFFFFF));

        //   ★ 登记**全部**字段，不猜名字 ✗
        //     我们还不知道校准数据叫什么，猜名字 = 自断线索 ✓
        for (int64_t j = 0; j < dk.n_entries && r.n < MAX_FIELDS; j++) {
            uint64_t kp = getPtr64(PyCore::key_slot(dk, j));
            kp &= 0x00FFFFFFFFFFFFFFULL;      // ★ 掩码不能漏 ✗
            if (!PyCore::is_obj(kp)) continue;
            //   ⚠ 不校验"是不是字符串类型"✗
            //     PyRoot 里**没有 g_str_type**（只登记了 dict/int/float/true/false）✗
            //     靠 read_str_any 自己判 —— 它内部有 kind 校验 ✓
            char nm[NAME_CAP] = {0};
            //   ☠ 这里**必须用我们自己的 ReadName**，不能用 PyCore::read_str_any ✗
            //     实测后者在循环里一个都读不出（同一个地址、同一次 dump）✗
            //     直接导致"找到 7 台密码机，登记 0 个字段" ✗
            if (!ReadName(getPtr64(kp + 16), nm, sizeof(nm))) continue;
            if (!nm[0]) continue;
            uint64_t vs = PyCore::value_slot(dk, j);
            r.Add(owner, nm, vs);
        }
    }
    r.disc_cnt++;
    snprintf(err, errn, "找到 %d 台密码机，登记 %d 个字段", r.gen_n, r.n);
    return r.n > 0;
#else
    (void)err; (void)errn; return false;
#endif
}

// ===========================================================================
// 一次采样：把当前所有被跟踪字段的值压进历史
//   ⚠ 只在 active 时做；否则零开销 ✓（默认关，用户开了才跑）
//   ⚠ 值域闸用**宽**范围（±1e6）✗
//     因为还不知道校准值是什么量纲（0~1？0~100？像素？）✓
//     宁可能记到垃圾，也别把真信号滤掉 ✗
// ===========================================================================
inline void Sample()
{
#if defined(__ANDROID__)
    Recorder& r = R();
    if (!r.active) return;
    for (int i = 0; i < r.n; i++) {
        FieldTrack& t = r.f[i];
        float v = 0.f;
        if (!PyCore::read_number(t.slot, v, -1.0e6, 1.0e6)) v = 0.f;
        t.hist[t.head] = v;
        t.head = (short)((t.head + 1) % HIST);
        if (t.n_samp < HIST) t.n_samp++;
    }
    r.ticks++;
#endif
}

// ===========================================================================
// 分析：找"像游标"的字段
//   判据（四条，缺一不可）✓ —— 这条判据是给**离线分析**用的，
//     真正点击时的判据要更严（见 CalibAuto.h）
//     ① 历史窗口内有**大幅变化**（极差 > 0.15，相对自身量纲）
//     ② 变化是**单调**的（相邻差同号的占比 > 0.7）—— 游标是匀速扫过 ✓
//     ③ 变化跨**多个采样点**（> 5 个）—— 排除单帧毛刺 ✓
//     ④ 与"总进度 fix_process"**不同**（总进度也在涨，但它不是游标）✓
//   out_lo/out_hi = 该字段在窗口内的最小/最大值；out_len = 单调段长度
//   返回命中的字段下标，-1 = 没找到 ✓
// ===========================================================================
inline int FindCursor(float* out_lo, float* out_hi, int* out_len)
{
    Recorder& r = R();
    int best = -1; float best_amp = 0.f; int best_len = 0;
    float best_lo = 0.f, best_hi = 0.f;

    for (int i = 0; i < r.n; i++) {
        FieldTrack& t = r.f[i];
        const int ns = t.n_samp;
        if (ns < 30) continue;
        //   按时间顺序展开环形缓冲 ✓
        float v[HIST];
        const int start = (ns < HIST) ? 0 : t.head;
        for (int j = 0; j < ns; j++) v[j] = t.hist[(start + j) % HIST];

        //   ④ 排除总进度：它的名字就是 fix_process ✓（也排除 *progress*）
        if (strstr(t.name, "progress") || strstr(t.name, "fix_process")) continue;

        float lo = v[0], hi = v[0];
        int same = 0, diff = 0, moves = 0;
        for (int j = 1; j < ns; j++) {
            const float d = v[j] - v[j - 1];
            if (v[j] < lo) lo = v[j];
            if (v[j] > hi) hi = v[j];
            if (d > 1e-6f || d < -1e-6f) moves++;
            if (j >= 2) {
                const float d0 = v[j - 1] - v[j - 2];
                if ((d > 0 && d0 > 0) || (d < 0 && d0 < 0)) same++; else diff++;
            }
        }
        const float amp = hi - lo;
        if (amp < 0.15f) continue;                 // ① 幅度
        if (moves < 6)   continue;                 // ③ 跨多个采样点
        if (same + diff > 4 && (float)same / (float)(same + diff) < 0.70f) continue;  // ② 单调

        if (amp > best_amp) {
            best_amp = amp; best = i; best_len = moves;
            best_lo = lo; best_hi = hi;
        }
    }
    if (out_lo)  *out_lo  = best_lo;
    if (out_hi)  *out_hi  = best_hi;
    if (out_len) *out_len = best_len;
    return best;
}

} // namespace CalibProbe
