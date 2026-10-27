#pragma once
// ============================================================================
// PyCore.h —— 读游戏内嵌 CPython 对象图的公共工具，PyProgress / PyGenius / PySelf 共用
// ============================================================================
//
// 2026-09-23 之前这些函数在三个模块里各抄了一份(每份约 150 行)，差别只有值域上下界。
// 代价是实测过的：给 find_key 加"整块读 + 驻留键指针"那次优化，要往三个文件里贴三遍同样的代码，
// 改 bug 同理。现在统一放这里，优化和修复一次生效。
//
// 解释器版本是 CPython 3.11，结构布局说明见 PyProgress.h 文件头，这里只放实现：
//   PyASCIIObject: 长度 +0x10，紧凑 ASCII 数据 +0x30
//   PyDictObject : ma_keys +0x20；keys 头 +0x09=dk_log2_index_bytes +0x0A=dk_kind +0x18=dk_nentries
//                  entries = keys + 32 + (1<<log2_index_bytes)，步长 16(全 unicode) / 24(带 hash)
//   managed-dict 实例: dict 指针在对象前 0x18
//   PyLongObject : +0x10 ob_size, +0x18 第一个 digit
//   list         : +0x10 ob_size, +0x18 ob_item
//
// 类型对象和 True/False 一律取自 PyRoot(运行时找出来的)，这里不再各存一份。
//
// ！！线程安全 ！！
// 三个模块各跑一个线程(16 / 100 / 500ms)，会同时调用这里的函数。所以带状态的两样东西
// —— 整块读缓冲 g_entry_block 和驻留键指针表 —— 必须是 thread_local，否则三个线程互相踩。
// 合并前它们分属三个命名空间、天然隔离，合并时如果照搬成普通 static 就会引入真实的数据竞争。
// 其余函数都是纯函数(只读目标进程内存)，无状态。

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <sys/prctl.h>   // DisguiseThreadName 用
#include "PyRoot.h"

namespace PyCore {

// 引擎/CPython 指针高位常年是 0xB4(Android TBI)，getPtr64 内部已经掩过，所以这里比较的都是掩码后的值。
// 下限必须跟场景层(draw_Gui.cpp is_user_ptr)一致用 0x50：本机指针虽然都在 0x77xx~0x7Cxx，
// 但 so/堆落在哪取决于地址随机化和进程映射总量，别的设备/渠道包(多带一堆 SDK)会落到 0x6x 段。
// 原来卡 0x70 时，2026-09-24 渠道服反馈整个 Python 层(自身/进度/天赋)全灭、场景层正常。
// 挡垃圾不靠这个范围：每一步都有类型指针/结构校验
inline bool is_obj(uint64_t p) { return p > 0x5000000000ULL && p < 0x8000000000ULL; }

inline bool str_equals(uint64_t s, const char *want)
{
    if (!is_obj(s)) return false;
    int64_t len = 0;
    if (!vm_readv(s + 0x10, &len, 8)) return false;
    size_t n = strlen(want);
    // 上限原来写的是 64 —— 但本工程里有大量超长键名/模块名
    // （如 common_cs.combat_core.systems.systems_goldrush.maze.MazeBaseTypes 有 67 字符），
    // 卡 64 会让这些名字**永远匹配不上**，表现为「明明存在却报不存在」。
    if (len != (int64_t)n || n >= 256) return false;
    char buf[256];
    if (!vm_readv(s + 0x30, buf, n)) return false;
    return memcmp(buf, want, n) == 0;
}

// 读一个紧凑 ASCII 的 PyUnicode(账号 uid 这类纯数字串就是)。
// +0x10 长度，+0x20 的低位字节是 state 位域：compact ascii 时数据紧跟在 +0x30。
// 非 ASCII(中文名字是 UCS2/4)一律返回 false —— 要显示中文得另做解码，这里不猜
//   ★ 2026-10-24 实现搬进 SecPyCore.cpp（security 库，A 档混淆）
//      这里只剩声明：字段偏移在二进制里看不到了
bool read_str(uint64_t s, char *out, size_t cap);


// 解码任意 Python str，包含**中文（UCS2/UCS4）**，统一转成 UTF-8。
//
// read_str 只处理 compact ASCII，遇到中文直接返回 false ——
// 结果就是「明明是个字符串，却显示成乱码/问号」。
// 实测 pydata.goldrush_minimap_configs 里就有两个中文/非 ASCII 键名读不出来。
//
// PyASCIIObject 布局（compact 时数据紧跟头部）：
//   +0x10 length(8)  +0x20 state(4)
//   state: bit2-4 = kind(0=1字节 1=2字节 2=4字节)
//          bit6   = compact        bit7 = ascii
//   数据：compact 时在 +0x30；否则 +0x28 是数据指针
//   ★ 2026-10-24 实现搬进 SecPyCore.cpp（security 库，A 档混淆）
//      这里只剩声明：字段偏移在二进制里看不到了
bool read_str_any(uint64_t s, char *out, size_t cap);


struct Dict { uint64_t entries; int stride; int64_t n_entries; };

// ⚠ entries 起始必须逐实例算：同一个类的不同实例 dict 大小可以不一样，
// 把一个实例算出的偏移套到别的实例上会读到无关属性
//   ★ 2026-10-24 实现搬进 SecPyCore.cpp（security 库，A 档混淆）
//      这里只剩声明：字段偏移在二进制里看不到了
bool read_dict(uint64_t d, Dict &out);


inline uint64_t key_slot(const Dict &k, int64_t i)   { return k.entries + i * k.stride + (k.stride == 24 ? 8 : 0); }
inline uint64_t value_slot(const Dict &k, int64_t i) { return k.entries + i * k.stride + (k.stride == 24 ? 16 : 8); }

// ---- 字典查找的两项优化(2026-09-23) ----
// 原来每比一个键要 3 次驱动读(取键指针 + 读长度 + 读内容)，全扫上千条的实例字典就是几千次读。
//   a) 整块读条目：entries 是连续内存，一次 vm_readv 读完(1300 条 * 24 字节 = 31KB)
//   b) 驻留键指针：CPython 会驻留标识符形式的属性名，同一个属性名在进程里就是同一个字符串对象。
//      第一次按名字找到后记下键对象地址，以后只比指针，连字符串都不用读。
//      假设不成立时自动退回按名字比较，只是慢一点，不会读错。
// 效果：命中缓存的校验从 3 次读降到 1 次；全扫从几千次降到 1 次(整块) + 本地比较。
// 两者都是 thread_local，见文件头「线程安全」。
inline thread_local uint8_t g_entry_block[48 * 1024];
// ⚠ 踩过的坑(2026-10-09)：name 曾经直接存**指针**，注释写着"调用方传的都是字符串常量"。
//   这个假设一旦被打破就是静默错读 —— 例如带点号路径导航(path_raw)传的是栈上 buf，
//   第二次调用时那块栈已经变成新名字，于是 strcmp(旧指针, 新名) 两边指向同一块内存、
//   永远相等 → 返回上一条名字的陈旧 key → **不同的键名全查出同一个值**。
//   所以这里必须**拷贝**名字，不能存指针。
// ★★ 名字缓冲区 64 -> 160（2026-10-10 性能修复，**纯尺寸改动**）
//
//   按 CPU 时间测出的分解（logcat: grep 剖析）：
//     [剖析] PySelf: CPU 11.73 ms/次 -> 占单核 73.3%   <- 整机大头就是它
//     [剖析] 绘制:   CPU  2.22 ms/帧 -> 占单核 13.3%
//     [剖析] 字典:   每次刷新 调用 29.3，其中【全扫 21.7】(74%)，否定缓存 0
//   "否定缓存 0" 说明这些名字**最终找得到**，只是每次都走慢路径。原因在这里：
//     remember_interned() 第一行就是 strlen(name) >= 64 就 return，
//   而本工程**确实有超过 64 字符的键名**（本文件上面的注释里就举了一例：
//   common_cs.combat_core.systems.systems_goldrush.maze.MazeBaseTypes，67 字符）。
//   这类名字永远存不进驻留表 -> find_key 第一遍(只比指针、几乎不花读取)被跳过
//   -> 每帧对 700 条逐条 str_equals，每条 2 次驱动读取。
//   21.7 次全扫 × 700 × 2 ≈ **每次刷新 3 万次系统调用** —— 那 73% 就是它。
//   放大到 160，长名字也能驻留，第一遍指针比较即可命中。
//   ⚠ 只改容量，查找/写入逻辑一行未动，对判定结果没有任何影响。
struct InternedName { char name[160]; uint64_t key_obj; };
// ★★ 驻留键表：32 -> 256（2026-10-10 性能修复，**纯尺寸改动**）
//
//   实测日志（局内，每次刷新的平均字典操作数）：
//     [剖析] 字典: 调用 27.2   其中 hint命中 4.5 / 全扫 19.7 / 逐条 2.9
//     [剖析] PySelf: 平均 14.8 ms/次   <- 刷新间隔就是 16ms，这线程自己吃满一个核
//   也就是 **73% 的字典查找退化成全扫**。而全扫的代价全看这里 want 知不知道：
//     · want 已知 -> 第一遍只比指针(kp == want)，整块已在内存，**0 次额外读取**
//     · want 未知 -> 跳过第一遍，第二遍对 700 条逐条 str_equals，**每条 2 次读取**
//   表只有 32 个槽位，而这个工程的键名/属性名远多于 32（本文件上面就写过
//   "有大量超长键名/模块名"）—— **表一满，后面的名字永远存不进去**，于是每次全扫
//   都走那条 700×2 次读取的慢路径：19.7 × ~1400 ≈ **每次刷新 2.7 万次读取**。
//   放大到 256 足以装下实际用到的名字（内存代价可忽略）。
//   ⚠ 这里**只改容量**，查找/写入逻辑一行没动：满了仍然是"存不下就算了"，
//     行为与之前完全一致，只是不再那么容易满 —— 所以不影响任何功能。
inline thread_local InternedName g_interned[256];
inline thread_local int g_interned_n = 0;

inline uint64_t interned_of(const char *name)
{
    for (int i = 0; i < g_interned_n; i++)
        if (strcmp(g_interned[i].name, name) == 0) return g_interned[i].key_obj;
    return 0;
}

inline void remember_interned(const char *name, uint64_t kp)
{
    if (!name || strlen(name) >= sizeof(g_interned[0].name)) return;   // 太长就不缓存
    for (int i = 0; i < g_interned_n; i++)
        if (strcmp(g_interned[i].name, name) == 0) { g_interned[i].key_obj = kp; return; }
    if (g_interned_n < (int)(sizeof(g_interned) / sizeof(g_interned[0]))) {
        snprintf(g_interned[g_interned_n].name, sizeof(g_interned[0].name), "%s", name);
        g_interned[g_interned_n].key_obj = kp;
        g_interned_n++;
    }
}

// ★★ 字典扫描计数器（性能剖析用）
//   起因：局内实测 CPU 69%(单核)，stime:utime = 3.7:1 —— 79% 在内核态，即驱动读取的
//   ioctl。按本文件注释里"每次驱动读约 3µs"折算：
//     13.49 秒 stime / 3µs ≈ 450 万次 / 25 秒 ≈ **每帧 3000 次读取**
//   而绘制循环"每个实体读一次位置"只有 99 次/帧，仅占 3%。剩下的只能来自**字典扫描**
//   （扫一遍 700 条的字典 ≈ 1400 次读取）。所以先量清楚，别再猜。
static std::atomic<uint64_t> g_dbg_find_calls{0};      // find_key 调用次数
static std::atomic<uint64_t> g_dbg_find_fullscan{0};   // 其中退化成"整块读 + 双遍全扫"的
static std::atomic<uint64_t> g_dbg_find_hint{0};       // 其中靠 hint 一次命中的(最省)
static std::atomic<uint64_t> g_dbg_find_slow{0};       // 整块读失败、退回逐条读的(最贵)
static std::atomic<uint64_t> g_dbg_find_neg{0};        // 命中"否定缓存"直接返回的(省掉整轮扫描)

// ★★ 否定缓存（性能修复，2026-10-10）
//
//   实测：PySelf 每次刷新 17.6 ms，而刷新间隔只有 16 ms —— 这个线程自己就占满一个核。
//   剖析日志：每次刷新 23.4 次字典查找，其中 **16 次退化成全扫**。
//   算一下就对上了：全扫若拿不到"驻留键指针"，就要对 700 条逐条 str_equals，
//   每条 2 次驱动读取 —— 16 × 700 × 2 = **22,400 次系统调用/刷新**，
//   按每次约 0.8µs 计 ≈ **18 ms**，与实测 17.6 ms 吻合。
//
//   根因：find_key 只在**找到**的时候才 remember_interned()，**查不到的名字永远记不住**
//   —— 于是同一个不存在的名字，每帧都要把 700 条重新比一遍字符串，永远重复。
//   修法：把"在这个字典里查不到"也记下来，下次直接返回 -1，不再扫。
//
//   安全性：只对**同一个字典对象**(d)生效，字典地址一变缓存立刻作废；
//   命中否定缓存的直接返回 -1，与"真扫一遍也找不到"的结果**完全一致** —— 不改变任何判定。
struct NegKey { uint64_t d; char name[160]; };

// ★★ 2026-10-14 修复：否定缓存**从来没有命中过**（实测 [剖析] 一直是 "否定缓存 0"）★★
//
//   原因：原实现用**单个** g_neg_dict 记录"当前缓存针对哪个字典"，
//     只要下一个查询换了字典就把整表清空 ✗
//     而 PySelf 每次刷新要在 **30 个不同的字典**上查（"model" / "state_machine"
//     这类名字分散在每个 unit 各自的 __dict__ 上），字典一切换缓存就归零 ——
//     这张表永远是空的，2026-10-10 想省掉的"每次 16 次全扫"一次也没省掉 ✗
//     实测数据：调用 63002 / hint 命中 11393 / 全扫 41582 / 否定缓存 **0**
//     （全扫占 66%，每次刷新 19.8 次全扫 —— 全扫是最贵的路径）
//
//   修法：NegKey 里本来就有 d 字段，直接按 (d, name) 精确匹配，
//     不再搞"换字典就整体作废" ✓ 容量提到 192（一次刷新约 30 次查询）✓
//
//   ⚠ 唯一的语义约束：字典**内容**可能被游戏改写（新增键），
//     而否定缓存记的是"上次扫完确实没有这个键" —— 那种情况会漏掉新键 ✗
//     所以由调用方**定期**整体清一次（见 neg_reset）：
//       PySelf 每 30 轮（@33ms 约 1 秒）清一次 ——
//       跨轮复用的收益基本全拿到，内容变化最多只影响 1 秒 ✓
//     （不每轮都清，是因为作者原意就是跨轮复用：
//       "同一个不存在的名字每帧都要把 700 条重新比一遍"才是要解决的问题）
static const int kNegCap = 192;
inline thread_local NegKey g_neg[kNegCap];
inline thread_local int    g_neg_n = 0;

inline void neg_reset() { g_neg_n = 0; }

inline bool neg_hit(uint64_t d, const char *name)
{
    for (int i = 0; i < g_neg_n; i++)
        if (g_neg[i].d == d && strcmp(g_neg[i].name, name) == 0) return true;
    return false;
}

inline void neg_add(uint64_t d, const char *name)
{
    if (!name || strlen(name) >= sizeof(g_neg[0].name)) return;
    for (int i = 0; i < g_neg_n; i++)
        if (g_neg[i].d == d && strcmp(g_neg[i].name, name) == 0) return;
    if (g_neg_n >= kNegCap) g_neg_n = 0;          // 满了整体重来，不会无限增长
    g_neg[g_neg_n].d = d;
    snprintf(g_neg[g_neg_n].name, sizeof(g_neg[0].name), "%s", name);
    g_neg_n++;
}

inline int64_t find_key(uint64_t d, const char *name, int64_t hint)
{
    // 上次就查不到 -> 直接返回，省掉整轮 700 条逐条比较(约 1400 次系统调用)
    if (neg_hit(d, name)) { g_dbg_find_neg.fetch_add(1, std::memory_order_relaxed); return -1; }
    g_dbg_find_calls.fetch_add(1, std::memory_order_relaxed);
    Dict k;
    if (!read_dict(d, k)) return -1;
    const uint64_t want = interned_of(name);
    if (hint >= 0 && hint < k.n_entries) {
        uint64_t kp = getPtr64(key_slot(k, hint));
        if (want && kp == want) { g_dbg_find_hint.fetch_add(1, std::memory_order_relaxed); return hint; }
        if (str_equals(kp, name)) { remember_interned(name, kp); g_dbg_find_hint.fetch_add(1, std::memory_order_relaxed); return hint; }
    }
    size_t bytes = (size_t)k.n_entries * k.stride;
    int key_off = (k.stride == 24) ? 8 : 0;
    if (bytes > 0 && bytes <= sizeof(g_entry_block) && vm_readv(k.entries, g_entry_block, bytes)) {
        g_dbg_find_fullscan.fetch_add(1, std::memory_order_relaxed);
        for (int pass = 0; pass < 2; pass++) {
            if (pass == 0 && !want) continue;                       // 还不知道键地址，直接进第二遍
            for (int64_t i = 0; i < k.n_entries; i++) {
                uint64_t kp = 0;
                memcpy(&kp, g_entry_block + (size_t)i * k.stride + key_off, 8);
                kp &= 0x00FFFFFFFFFFFFFFULL;
                if (pass == 0) { if (kp == want) return i; }
                else if (str_equals(kp, name)) { remember_interned(name, kp); return i; }
            }
        }
        neg_add(d, name);          // 整块扫完也没有 -> 记下来，下次别再扫
        return -1;
    }
    // 整块读失败(条目跨到换出的页等)：退回逐条读
    g_dbg_find_slow.fetch_add(1, std::memory_order_relaxed);
    for (int64_t i = 0; i < k.n_entries; i++)
        if (str_equals(getPtr64(key_slot(k, i)), name)) return i;
    neg_add(d, name);
    return -1;
}

inline uint64_t get_attr(uint64_t d, int64_t idx)
{
    Dict k;
    if (!read_dict(d, k) || idx < 0 || idx >= k.n_entries) return 0;
    return getPtr64(value_slot(k, idx));
}

// 某个属性的序号：先用缓存的并校验，不对就全扫重找。序号随类属性插入顺序变，热更后可能位移，
// 所以从不直接信任缓存 —— 最坏就是多扫一遍，不会读错属性
inline int64_t resolve_index(uint64_t d, const Dict &dk, int64_t cached, const char *name)
{
    if (cached >= 0 && cached < dk.n_entries) {
        uint64_t kp = getPtr64(key_slot(dk, cached));
        uint64_t want = interned_of(name);
        if (want && kp == want) return cached;                      // 驻留键指针：校验只要 1 次读
        if (str_equals(kp, name)) { remember_interned(name, kp); return cached; }
    }
    return find_key(d, name, -1);
}

// managed-dict 实例：dict 指针在对象前 0x18。多校验一次 ob_type 是 dict，避免把别的东西当实例
inline uint64_t get_inst_dict(uint64_t obj)
{
    if (!is_obj(obj)) return 0;
    uint64_t d = getPtr64(obj - 0x18);
    if (!is_obj(d) || getPtr64(d + 8) != PyRoot::g_dict_type) return 0;
    return d;
}

// 读一个 PyLong。带防竞态校验：解引用后重读指针确认没变，再看 refcount 和类型
inline bool read_int(uint64_t vp, int64_t &out)
{
    if (!is_obj(vp)) return false;
    uint8_t b[32];
    if (!vm_readv(vp, b, 32)) return false;
    int64_t rc = 0; memcpy(&rc, b, 8);
    uint64_t tp = 0; memcpy(&tp, b + 8, 8); tp &= 0x00FFFFFFFFFFFFFFULL;
    if (rc <= 0 || tp != PyRoot::g_int_type) return false;
    int64_t sz = 0; memcpy(&sz, b + 16, 8);
    uint32_t dg = 0; memcpy(&dg, b + 24, 4);
    out = (sz == 0) ? 0 : (int64_t)dg;
    if (sz < 0) out = -out;
    return true;
}

// 读一个 int/float 属性到 float。lo/hi 是值域闸：进度是 0~100，冷却是 0~600，**不能混用**。
// 三次重试 + 指针重读是必须的：属性每帧被赋成新的 float 对象，旧的进 freelist 被复用
inline bool read_number(uint64_t slot, float &out, double lo, double hi)
{
    for (int t = 0; t < 3; t++) {
        uint64_t vp = getPtr64(slot);
        if (!is_obj(vp)) continue;
        uint8_t b[32];
        if (!vm_readv(vp, b, 32)) continue;
        if (getPtr64(slot) != vp) continue;       // 解引用期间指针变了 -> 对象可能已被释放复用
        int64_t rc = 0; memcpy(&rc, b, 8);
        uint64_t tp = 0; memcpy(&tp, b + 8, 8); tp &= 0x00FFFFFFFFFFFFFFULL;
        if (rc <= 0) continue;                    // refcount 0 = 已释放
        double v;
        if (tp == PyRoot::g_float_type) {
            memcpy(&v, b + 16, 8);
        } else if (tp == PyRoot::g_int_type) {    // 0 和 100 常是缓存的小整数对象，必须两种都认
            int64_t sz = 0; memcpy(&sz, b + 16, 8);
            uint32_t dg = 0; memcpy(&dg, b + 24, 4);
            v = (sz == 0) ? 0.0 : (double)dg;
            if (sz < 0) v = -v;
        } else {
            continue;
        }
        if (!(v >= lo && v <= hi)) continue;      // 值域闸：垃圾值里出现过 0.16 这种"看着合理"的
        out = (float)v;
        return true;
    }
    return false;
}

// 读一个 bool。先比 PyRoot 找出来的两个单例(最稳)，比不上再按 PyLong 布局退化解
inline bool read_bool(uint64_t slot, bool &out)
{
    uint64_t vp = getPtr64(slot);
    if (!is_obj(vp)) return false;
    if (vp == PyRoot::g_true)  { out = true;  return true; }
    if (vp == PyRoot::g_false) { out = false; return true; }
    uint8_t b[32];
    if (!vm_readv(vp, b, 32)) return false;
    int64_t rc = 0; memcpy(&rc, b, 8);
    if (rc <= 0) return false;
    int64_t sz = 0; memcpy(&sz, b + 16, 8);
    uint32_t dg = 0; memcpy(&dg, b + 24, 4);
    out = (sz != 0 && dg != 0);
    return true;
}

// list: +0x10 元素数, +0x18 元素数组。
// 三点语义要守住(合并前三个模块都是这样，改了会连累天赋记忆和本体集合)：
//   1) n > 256 视为读到垃圾 -> 失败。上限不能放宽：调用方会按 n 逐个读元素，
//      n 是垃圾大数时会让 16ms 那个线程空转几千次读
//   2) **空列表是成功**(n=0, items=0)，不是失败 —— "读不到"和"真的是空"必须分开，
//      这条是天赋那边踩过的坑(见 PyGenius.h 文件头第 3 条)
//   3) 非空时 items 必须是合法堆指针
inline bool read_list(uint64_t L, int64_t &n, uint64_t &items)
{
    if (!is_obj(L)) return false;
    if (!vm_readv(L + 0x10, &n, 8)) return false;
    if (n < 0 || n > 256) return false;
    if (n == 0) { items = 0; return true; }
    items = getPtr64(L + 0x18);
    return is_obj(items);
}

// units_by_type 是 dict(int -> list)，按 int 键取出那条 list
inline uint64_t get_type_list(uint64_t ubt, int type)
{
    Dict k;
    if (!read_dict(ubt, k)) return 0;
    for (int64_t i = 0; i < k.n_entries; i++) {
        uint64_t kp = getPtr64(key_slot(k, i));
        if (!is_obj(kp) || getPtr64(kp + 8) != PyRoot::g_int_type) continue;
        int64_t sz = 0; uint32_t dg = 0;
        vm_readv(kp + 16, &sz, 8);
        vm_readv(kp + 24, &dg, 4);
        if (sz == 1 && (int)dg == type) return getPtr64(value_slot(k, i));
    }
    return 0;
}

// sys.modules -> "game_kernel" -> md_dict(进程内稳定，不随对局变)。
// 失败原因写进 why，调用方直接拿去显示在面板上
inline uint64_t game_kernel_dict(const char *&why)
{
    uint64_t sm = getPtr64(PyRoot::g_modules_slot);
    if (!is_obj(sm))                          { why = "sys.modules 取不到"; return 0; }
    if (getPtr64(sm + 8) != PyRoot::g_dict_type) { why = "sys.modules 不是dict(偏移已失效)"; return 0; }
    int64_t i = find_key(sm, "game_kernel", -1);
    if (i < 0)                                { why = "找不到 game_kernel 模块"; return 0; }
    uint64_t mod = get_attr(sm, i);
    if (!is_obj(mod))                         { why = "game_kernel 模块对象无效"; return 0; }
    uint64_t md = getPtr64(mod + 0x10);
    if (!is_obj(md))                          { why = "md_dict 无效"; return 0; }
    return md;
}

// unit.model -> +0x20 = 场景对象指针(引擎自己持有的身份，用来跟绘制循环里的 obj 配对)
inline uint64_t get_scene_obj(uint64_t model_obj)
{
    if (!is_obj(model_obj)) return 0;
    uint64_t sp = getPtr64(model_obj + 0x20);
    return is_obj(sp) ? sp : 0;
}

// ---- 游戏阶段(大厅/准备/局内)判定 ----
// 判据 = g_ui_mgr.ui_dict 里挂着哪个 UI 对象，纯对象存在性，不用 world_type
//   (用户已否决 world_type：多个大厅/地图下它各不同)。
// 2026-10-15 跨进程实测并与注入样本一致：
//   大厅 = {UIHall}   准备 = {UIRoomWait}   局内 = {UIBattle, UIControlCustom}
// 三态 key 完全不重叠。局内有 2 个 key，所以按 局内 -> 准备 -> 大厅 顺序查。
// 与 units_by_type 键 1/2(uses_battle_world) 互为交叉校验：UIBattle 在 <=> 有键 1/2。
enum Phase { PHASE_UNKNOWN = 0, PHASE_LOBBY = 1, PHASE_PREPARE = 2, PHASE_BATTLE = 3 };

inline Phase detect_phase(const char *&reason)
{
    // g_ui_mgr 在模块 dict、ui_dict 在 UIManager 实例 dict，两个条目序号进程内稳定、跨对局不变。
    // thread_local hint 配合 PyScene 单线程调用；find_key 自带校验，缓存错了会重扫。
    static thread_local int64_t hint_uimgr = -1, hint_uidict = -1;
    const char *why = "";
    uint64_t md = game_kernel_dict(why);
    if (!md) { reason = why; return PHASE_UNKNOWN; }

    int64_t iu = find_key(md, "g_ui_mgr", hint_uimgr);
    if (iu < 0) { reason = "没有 g_ui_mgr"; return PHASE_UNKNOWN; }
    hint_uimgr = iu;
    uint64_t ud = get_inst_dict(get_attr(md, iu));
    if (!ud) { reason = "g_ui_mgr 实例 dict 无效"; return PHASE_UNKNOWN; }

    int64_t id = find_key(ud, "ui_dict", hint_uidict);
    if (id < 0) { reason = "没有 ui_dict"; return PHASE_UNKNOWN; }
    hint_uidict = id;
    uint64_t uidict = get_attr(ud, id);

    Dict k;
    if (!read_dict(uidict, k)) { reason = "ui_dict 读取失败"; return PHASE_UNKNOWN; }
    bool has_room = false, has_hall = false;
    for (int64_t i = 0; i < k.n_entries; i++) {
        uint64_t kp = getPtr64(key_slot(k, i));
        if (!is_obj(kp)) continue;
        if (str_equals(kp, "UIBattle")) { reason = "UIBattle"; return PHASE_BATTLE; }
        else if (str_equals(kp, "UIRoomWait")) has_room = true;
        else if (str_equals(kp, "UIHall")) has_hall = true;
    }
    if (has_room) { reason = "UIRoomWait"; return PHASE_PREPARE; }
    if (has_hall) { reason = "UIHall"; return PHASE_LOBBY; }
    reason = "无已知 UI key";
    return PHASE_UNKNOWN;
}

} // namespace PyCore

// ===========================================================================
// 诊断用：给当前线程挂一个**可区分、但仍是伪装风格**的名字。
//
// 为什么需要：main.cpp 启动时随机选一个伪装名 prctl 给主线程，其余线程**继承**它 ——
// 于是 /proc 里所有工作线程都叫同一个名字，剖析时分不清谁是谁
// （实测两个线程各占 ~40% 单核，却无法判断是哪个模块）。
// 这里用 kworker 风格的名字，既保持"看起来像系统线程"的伪装，
// 又能让 `cat /proc/PID/task/*/comm` 区分出每个模块的开销。
// idx 只是一个序号，随便给，只要不与别人重复。
// ===========================================================================
inline void DisguiseThreadName(int idx) {
    char nm[16];
    snprintf(nm, sizeof(nm), "kworker/u8:%d", idx);
    prctl(PR_SET_NAME, nm);
}