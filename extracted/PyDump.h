#pragma once
// ============================================================================
// PyDump.h —— 调试模式：交互式 dump CPython 对象图，找字段名
// ============================================================================
//
// 用途：
//   1. dump game_kernel 模块字典的所有键名（找顶层入口）
//   2. dump units_by_type 里每个 type 的数量
//   3. dump 指定 unit 实例的完整 __dict__（所有属性名 + 值）
//   4. dump 任意 dict 对象的所有键（通过指针地址）
//   5. 按关键词过滤属性名
//
// 使用方式：
//   - 在 ImGui 面板里点 "Dump" 按钮
//   - 结果写入 g_buf，面板里逐行显示
//   - 线程安全：dump 在渲染线程里执行（每帧最多调一次），用 thread_local 缓冲
//
// 依赖：PyCore.h（所有读取工具）、PyRoot.h（类型指针）

#include <cstdint>
#include "memhwbp.h"
#include "guard.h"        // ★ GUARD_STR：发布要求"零明文锚点" ✓
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cctype>
#include "PyCore.h"
#include "PyAttack.h"
#include "rescue_log.h"
// （2026-10-13：auto_shock.h / autozhenshe.h 的 include 已随「自动震慑」删除 ✓
//   原来这里靠 autozhenshe.h 顺带拿到 get_all_units_of_type / g_id2name 等，
//   那些其实来自 PyAttack.h，本文件本来就 include 了 ✓）

namespace PyDump {

// is_obj / getPtr64 / vm_readv 是全局工具，PyCore/PyRoot 里也有各自的副本。
// PyDump 用全局的（draw_Gui.cpp 里 PyProgress 等也是裸调）
using PyCore::is_obj;

// 输出缓冲：128KB 够用（一个 unit dict 最多几百条 * 60 字节）
static char g_buf[524288];
static int  g_buf_len = 0;
static int  g_buf_cap = (int)sizeof(g_buf);

// 过滤关键词（空 = 不过滤）
// 过滤词 / 名字参数。
// ⚠ 原来只有 64 字节，而本工程里模块全名动辄 60~100 字符
//   （common_cs.combat_core.systems.systems_goldrush.maze.MazeMapGenerationSystem 有 76 字符），
//   传进来会被**静默截断**，然后报「模块不存在」。
static char g_filter[256] = "";

// 状态
static char g_status[128] = "未启动";

// ---- 基础写入工具 ----
// ★ 2026-10-10 崩溃修复。**核心：vsnprintf 的返回值是"本来想写多少"，不是"实际写进去多少"。**
//   缓冲快满时它照样返回完整的期望长度，而只写进去剩下那点。原来的 `g_buf_len += n`
//   于是能把 g_buf_len 推过 g_buf_cap：实测 g_buf_len 到过 524298，而 g_buf 只有 524288。
//   接着 save_buf 里 fwrite(g_buf, 1, 524298) 被 bionic 的 FORTIFY 拦下 ——
//   "FORTIFY: fwrite: prevented 524298-byte read from 524288-byte buffer" -> SIGABRT，
//   整个 overlay 进程直接死(tombstone_01)。用户存的那批 dump 全都停在
//   524287~524306 字节，也是同一个账目错误造成的"看起来保存成功、其实早写满了"。
//
//   现在统一走 vw()：只按**实际进缓冲的字节数**前进，g_buf_len 恒 <= g_buf_cap-1。
static void vw(const char *fmt, va_list ap)
{
    if (g_buf_len < 0) g_buf_len = 0;                       // 防御：负数会让下面的 size 变成天文数字
    if (g_buf_len >= g_buf_cap - 1) { g_buf_len = g_buf_cap - 1; return; }
    int n = vsnprintf(g_buf + g_buf_len, (size_t)(g_buf_cap - g_buf_len), fmt, ap);
    if (n < 0) return;                                      // 编码错误：什么都没写
    const int room = g_buf_cap - 1 - g_buf_len;             // 还能放几个字符(不含结尾 NUL)
    g_buf_len += (n < room) ? n : room;                     // 截断时也只前进到缓冲末尾
}

static void w(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vw(fmt, ap);
    va_end(ap);
}

static void wline(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vw(fmt, ap);
    va_end(ap);
    w("\n");
}

static void reset_buf() { g_buf[0] = 0; g_buf_len = 0; }

// 追加模式：被别的 dump 内部调用时不要清空已写好的头部
// （之前 dump_typeinfo 先写了 tp_basicsize，再调 dump_dict_raw，
//   结果被对方的 reset_buf() 整段冲掉了）
static bool g_append_mode = false;
static void maybe_reset() { if (!g_append_mode) reset_buf(); }

static bool passes_filter(const char *name)
{
    if (!g_filter[0]) return true;
    return strstr(name, g_filter) != nullptr;
}

// ---- 读一个值并格式化到 buf ----
static void dump_value(uint64_t vp, int indent, int depth = 0)
{
    if (!vp) { w("(null)"); return; }
    if (!is_obj(vp)) { w("0x%llx (非法指针)", (unsigned long long)vp); return; }

    // 读 32 字节头部：refcount(8) + type(8) + 数据(16)
    uint8_t b[32];
    if (!vm_readv(vp, b, 32)) { w("(读取失败)"); return; }
    uint64_t tp = 0; memcpy(&tp, b + 8, 8); tp &= 0x00FFFFFFFFFFFFFFULL;

    if (tp == PyRoot::g_int_type) {
        int64_t sz = 0; uint32_t dg = 0;
        memcpy(&sz, b + 16, 8);
        memcpy(&dg, b + 24, 4);
        int64_t v = (sz == 0) ? 0 : (int64_t)dg;
        if (sz < 0) v = -v;
        w("%lld", v);
    }
    else if (tp == PyRoot::g_float_type) {
        double v; memcpy(&v, b + 16, 8);
        w("%.4f", v);
    }
    else if (tp == PyRoot::g_true)  w("True");
    else if (tp == PyRoot::g_false) w("False");
    else {
        // 判断类型名
        uint64_t np = getPtr64(tp + 0x18);
        if (!is_obj(np)) { w("<type?>"); return; }
        char tn[32] = {0};
        vm_readv(np, tn, sizeof(tn) - 1);
        tn[sizeof(tn) - 1] = 0;

        if (strcmp(tn, "str") == 0) {
            char sb[128];
            if (PyCore::read_str(vp, sb, sizeof(sb))) w("'%s'", sb);
            else w("<str len>");
        }
        else if (strcmp(tn, "list") == 0) {
            int64_t n = 0; uint64_t items = 0;
            if (PyCore::read_list(vp, n, items)) {
                w("<list len=%lld>", n);
                if (n > 0 && n <= 8) {
                    w(" [");
                    for (int64_t i = 0; i < n; i++) {
                        if (i) w(", ");
                        uint64_t elem = getPtr64(items + i * 8);
                        if (elem) {
                            uint8_t eb[16];
                            if (vm_readv(elem, eb, 16)) {
                                uint64_t etp = 0; memcpy(&etp, eb + 8, 8); etp &= 0x00FFFFFFFFFFFFFFULL;
                                if (etp == PyRoot::g_int_type) {
                                    int64_t sz2=0; uint32_t dg2=0;
                                    vm_readv(elem + 16, &sz2, 8); vm_readv(elem + 24, &dg2, 4);
                                    int64_t iv = (sz2==0)?0:(int64_t)dg2; if (sz2<0) iv=-iv;
                                    w("%lld", iv);
                                }
                                else if (etp == PyRoot::g_float_type) {
                                    double dv; vm_readv(elem+16, &dv, 8); w("%.2f", dv);
                                }
                                else w("?");
                            }
                        } else w("nil");
                    }
                    w("]");
                }
            }
            else w("<list 读取失败>");
        }
        else if (strcmp(tn, "dict") == 0) {
    PyCore::Dict dk;
    //   ★★ 2026-10-14：dict 也要打**地址** ★★
    //     用户要按路径一路深挖（cam_ctrl -> cam_list -> ...），
    //     而原来只打 `<dict N keys>` **不打地址** ——
    //     拿不到地址就没法用「Dump 指定地址」继续挖下一层 ✗
    //     加地址之后：dump 上层就能看到下层的地址，把地址填进
    //     「Dump 指定地址」框就能接着展开 ✓
    if (PyCore::read_dict(vp, dk)) w("<dict @ 0x%llX %lld keys>",
                                     (unsigned long long)vp, dk.n_entries);
    else w("<dict 读取失败>");
}
        else {
            // 自定义类实例：尝试读 __dict__ 并递归展开
            if (depth < 3) {
                uint64_t inst_dict = PyCore::get_inst_dict(vp);
                PyCore::Dict idk;
                if (is_obj(inst_dict) && PyCore::read_dict(inst_dict, idk) && idk.n_entries > 0) {
                    w("<%s @ 0x%llx (%lld keys)> {\n", tn, (unsigned long long)vp, idk.n_entries);
                    for (int64_t j = 0; j < idk.n_entries; j++) {
                        uint64_t kp2 = getPtr64(PyCore::key_slot(idk, j));
                        char kn2[64] = {0};
                        if (!PyCore::read_str(kp2, kn2, sizeof(kn2))) continue;
                        if (!passes_filter(kn2)) continue;
                        for (int k = 0; k < indent + 4; k++) w(" ");
                        w("%s = ", kn2);
                        uint64_t vp2 = getPtr64(PyCore::value_slot(idk, j));
                        dump_value(vp2, indent + 4, depth + 1);
                        w("\n");
                    }
                    for (int k = 0; k < indent; k++) w(" ");
                    w("}");
                } else {
                    w("<%s @ 0x%llx>", tn, (unsigned long long)vp);
                }
            } else {
                w("<%s @ 0x%llx>", tn, (unsigned long long)vp);
            }
        }
    }
}

// ---- Dump 1: game_kernel 模块字典所有键 ----

static void dump_game_kernel()
{
    reset_buf();
    const char *why = nullptr;
    uint64_t md = PyCore::game_kernel_dict(why);
    if (!md) {
        wline("game_kernel 不可用: %s", why ? why : "未知");
        snprintf((char*)g_status, 128, "失败: %s", why ? why : "未知");
        return;
    }

    PyCore::Dict dk;
    if (!PyCore::read_dict(md, dk)) {
        wline("md_dict 读取失败");
        snprintf((char*)g_status, 128, "md_dict 读取失败");
        return;
    }

    wline("===== game_kernel.md_dict (%lld 个键) =====", dk.n_entries);
    wline("过滤: %s", g_filter[0] ? g_filter : "(无)");
    wline("%-4s  %-36s  值", "#", "键名", "");
    wline("%-4s  %-36s  %s", "----", "----------------------------------------", "----");

    int shown = 0;
    for (int64_t i = 0; i < dk.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(dk, i));
        char kname[64] = {0};
        if (!PyCore::read_str(kp, kname, sizeof(kname))) {
            // 非 ASCII 或读取失败
            uint64_t tp = getPtr64(kp + 8);
            char tn[16]={0};
            uint64_t np = getPtr64(tp + 0x18);
            if (is_obj(np)) vm_readv(np, tn, 15);
            wline("%-4lld  <%s key 0x%llx>", i, tn[0]?tn:"?", (unsigned long long)kp);
            continue;
        }
        if (!passes_filter(kname)) continue;

        uint64_t vp = getPtr64(PyCore::value_slot(dk, i));
        w("%-4lld  %-36s  ", i, kname);
        dump_value(vp, 0);
        w("\n");
        shown++;
    }
    wline("===== 共显示 %d / %lld 条 =====", shown, dk.n_entries);
    snprintf((char*)g_status, 128, "OK: game_kernel %ld 键, 显示 %d", dk.n_entries, shown);
}

// ---- Dump 2: units_by_type 概览 ----
static void dump_units_by_type()
{
    reset_buf();
    const char *why = nullptr;
    uint64_t md = PyCore::game_kernel_dict(why);
    if (!md) { wline("game_kernel 不可用: %s", why?why:"?"); return; }

    // game_kernel 实例的 __dict__
    uint64_t gk_obj = 0;
    PyCore::Dict dk;
    if (!PyCore::read_dict(md, dk)) { wline("md_dict 读取失败"); return; }
    for (int64_t i = 0; i < dk.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(dk, i));
        char kname[64];
        if (PyCore::read_str(kp, kname, sizeof(kname)) && strcmp(kname, "unit_mgr") == 0) {
            gk_obj = getPtr64(PyCore::value_slot(dk, i));
            break;
        }
    }
    if (!gk_obj) { wline("找不到 unit_mgr"); return; }

    uint64_t umg_dict = PyCore::get_inst_dict(gk_obj);
    if (!umg_dict) { wline("unit_mgr.__dict__ 读取失败"); return; }

    PyCore::Dict umd;
    if (!PyCore::read_dict(umg_dict, umd)) { wline("unit_mgr dict 读取失败"); return; }

    uint64_t ubt = 0;
    for (int64_t i = 0; i < umd.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(umd, i));
        char kname[64];
        if (PyCore::read_str(kp, kname, sizeof(kname)) && strcmp(kname, "units_by_type") == 0) {
            ubt = getPtr64(PyCore::value_slot(umd, i));
            break;
        }
    }
    if (!ubt) { wline("找不到 units_by_type"); return; }

    PyCore::Dict ubtd;
    if (!PyCore::read_dict(ubt, ubtd)) { wline("units_by_type dict 读取失败"); return; }

    wline("===== units_by_type (%lld 个类型) =====", ubtd.n_entries);
    wline("%-8s  %-12s  %s", "type", "数量", "首元素地址");

    for (int64_t i = 0; i < ubtd.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(ubtd, i));
        if (!is_obj(kp) || getPtr64(kp + 8) != PyRoot::g_int_type) continue;
        int64_t sz = 0; uint32_t dg = 0;
        vm_readv(kp + 16, &sz, 8);
        vm_readv(kp + 24, &dg, 4);
        int64_t type_val = (sz == 0) ? 0 : (int64_t)dg;
        if (sz < 0) type_val = -type_val;

        uint64_t lst = getPtr64(PyCore::value_slot(ubtd, i));
        int64_t cnt = 0; uint64_t items = 0;
        if (PyCore::read_list(lst, cnt, items)) {
            uint64_t first = (cnt > 0) ? getPtr64(items) : 0;
            wline("%-8lld  %-12lld  0x%llx", type_val, cnt, (unsigned long long)first);
        }
        else {
            wline("%-8lld  %-12s  (读取失败)", type_val, "?");
        }
    }
    snprintf((char*)g_status, 128, "OK: units_by_type %ld 类型", ubtd.n_entries);
}

// ---- 遍历 units_by_type 里**所有键、所有 unit** ----
// 给特殊模式排查用：一次性看到所有类型、所有单位的类名/uid/坐标/scene_obj
static void dump_all_units()
{
    reset_buf();
    const char *why = nullptr;
    uint64_t md = PyCore::game_kernel_dict(why);
    if (!md) { wline("game_kernel 不可用: %s", why?why:"?"); return; }

    // 找 unit_mgr
    uint64_t gk_obj = 0;
    PyCore::Dict dk;
    if (!PyCore::read_dict(md, dk)) return;
    for (int64_t i = 0; i < dk.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(dk, i));
        char kname[64];
        if (PyCore::read_str(kp, kname, sizeof(kname)) && strcmp(kname, "unit_mgr") == 0) {
            gk_obj = getPtr64(PyCore::value_slot(dk, i));
            break;
        }
    }
    if (!gk_obj) { wline("找不到 unit_mgr"); return; }

    uint64_t umg_dict = PyCore::get_inst_dict(gk_obj);
    if (!umg_dict) { wline("unit_mgr.__dict__ 失败"); return; }

    PyCore::Dict umd;
    if (!PyCore::read_dict(umg_dict, umd)) return;

    uint64_t ubt = 0;
    for (int64_t i = 0; i < umd.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(umd, i));
        char kname[64];
        if (PyCore::read_str(kp, kname, sizeof(kname)) && strcmp(kname, "units_by_type") == 0) {
            ubt = getPtr64(PyCore::value_slot(umd, i));
            break;
        }
    }
    if (!ubt) { wline("找不到 units_by_type"); return; }

    PyCore::Dict ubtd;
    if (!PyCore::read_dict(ubt, ubtd)) { wline("units_by_type dict 读取失败"); return; }

    wline("===== 所有单位类型 (%lld) =====", (long long)ubtd.n_entries);
    wline("");

    for (int64_t i = 0; i < ubtd.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(ubtd, i));
        int64_t type_val = 0;
        if (!PyCore::read_int(kp, type_val)) continue;

        uint64_t lst = getPtr64(PyCore::value_slot(ubtd, i));
        int64_t n = 0; uint64_t items = 0;
        if (!PyCore::read_list(lst, n, items)) {
            wline("type=%lld  list读取失败", (long long)type_val);
            continue;
        }

        wline("--- type=%lld  数量=%lld ---", (long long)type_val, (long long)n);

        for (int64_t j = 0; j < n && j < 16; j++) {
            uint64_t unit = getPtr64(items + j * 8);
            if (!PyCore::is_obj(unit)) continue;

            uint64_t ud = PyCore::get_inst_dict(unit);
            PyCore::Dict udk;
            if (!ud || !PyCore::read_dict(ud, udk)) continue;

            // 类名
            uint64_t tp = getPtr64(unit + 8);
            char cname[64] = {0};
            uint64_t np = getPtr64(tp + 0x18);
            if (PyCore::is_obj(np)) vm_readv(np, cname, 63);

            // 找 uid / position / model
            uint64_t uid_obj = 0;
            float pos[3] = {0,0,0};
            uint64_t scene_obj = 0;
            int64_t unit_type = 0;

            for (int64_t k = 0; k < udk.n_entries; k++) {
                uint64_t kkp = getPtr64(PyCore::key_slot(udk, k));
                char kname[64] = {0};
                if (!PyCore::read_str(kkp, kname, sizeof(kname))) continue;

                if (strcmp(kname, "uid") == 0) {
                    uid_obj = getPtr64(PyCore::value_slot(udk, k));
                } else if (strcmp(kname, "position") == 0) {
                    uint64_t po = getPtr64(PyCore::value_slot(udk, k));
                    if (PyCore::is_obj(po)) vm_readv(po + 0x10, pos, 12);
                } else if (strcmp(kname, "model") == 0) {
                    uint64_t mo = getPtr64(PyCore::value_slot(udk, k));
                    if (PyCore::is_obj(mo))
                        scene_obj = getPtr64(mo + 0x20);
                } else if (strcmp(kname, "unit_type") == 0) {
                    uint64_t ut = getPtr64(PyCore::value_slot(udk, k));
                    if (PyCore::is_obj(ut)) PyCore::read_int(ut, unit_type);
                }
            }

            int64_t uid_val = 0;
            if (uid_obj) PyCore::read_int(uid_obj, uid_val);

            wline("  [%lld] %s", (long long)j, cname);
            wline("       unit_type=%lld  uid=%lld",
                  (long long)unit_type, (long long)uid_val);
            wline("       pos=(%.1f, %.1f, %.1f)",
                  pos[0], pos[1], pos[2]);
            wline("       unit=0x%llX  scene(model+0x20)=0x%llX",
                  (unsigned long long)unit,
                  (unsigned long long)scene_obj);
        }
        wline("");
    }

    snprintf((char*)g_status, 128, "OK: dump all units %ld types", ubtd.n_entries);
}

// ---- Dump 3: 指定 unit 实例的完整 __dict__ ----
// note 可选：在表头上方额外显示一行（例如反查来源）
static void dump_unit_dict(uint64_t unit_obj, const char *note = nullptr)
{
    reset_buf();
    if (!unit_obj) { wline("unit 地址为 0"); return; }
    if (note && note[0]) wline("来源: %s", note);

    // 尝试 managed dict
    uint64_t d = PyCore::get_inst_dict(unit_obj);
    if (!d) {
        // 也可能 unit_obj 本身就是一个 dict
        PyCore::Dict test;
        if (PyCore::read_dict(unit_obj, test) && getPtr64(unit_obj + 8) == PyRoot::g_dict_type) {
            d = unit_obj;
        } else {
            wline("无法读取 __dict__ (obj=0x%llx)", (unsigned long long)unit_obj);
            // 显示类型名
            uint64_t tp = getPtr64(unit_obj + 8);
            char tn[64]={0};
            uint64_t np = getPtr64(tp + 0x18);
            if (is_obj(np)) vm_readv(np, tn, 63);
            wline("类型: %s", tn[0] ? tn : "(未知)");
            return;
        }
    }

    // 读类型名
    uint64_t tp = getPtr64(unit_obj + 8);
    char tname[64] = {0};
    uint64_t np = getPtr64(tp + 0x18);
    if (is_obj(np)) vm_readv(np, tname, 63);

    PyCore::Dict dk;
    if (!PyCore::read_dict(d, dk)) { wline("dict 读取失败"); return; }

    wline("===== unit 0x%llx 类型=%s (%lld 个属性) =====",
          (unsigned long long)unit_obj, tname[0]?tname:"?", dk.n_entries);
    wline("过滤: %s", g_filter[0] ? g_filter : "(无)");

    int shown = 0;
    for (int64_t i = 0; i < dk.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(dk, i));
        char kname[64] = {0};
        if (!PyCore::read_str(kp, kname, sizeof(kname))) {
            wline("  [? key 0x%llx]", (unsigned long long)kp);
            continue;
        }
        if (!passes_filter(kname)) continue;

        uint64_t vp = getPtr64(PyCore::value_slot(dk, i));
        w("  %-40s = ", kname);
        dump_value(vp, 2);
        w("\n");
        shown++;
    }
    wline("===== 共显示 %d / %lld 条 =====", shown, dk.n_entries);
    snprintf((char*)g_status, 128, "OK: unit %s %d/%ld 属性", tname[0]?tname:"?", shown, dk.n_entries);
}

static void     dump_expand(uint64_t addr, int64_t depth);

// ---- Dump 4: 任意 dict 地址 ----
static void dump_dict_at(uint64_t addr)
{
    reset_buf();
    if (!is_obj(addr)) { wline("地址 0x%llx 不是合法对象指针", (unsigned long long)addr); return; }

    // 检查是不是 dict
    uint64_t tp = getPtr64(addr + 8);
    char tn[32] = {0};
    uint64_t np = getPtr64(tp + 0x18);
    if (is_obj(np)) vm_readv(np, tn, 31);

    if (strcmp(tn, "dict") != 0) {
    // ★★ 2026-10-17：非 dict 转发给 dump_expand ★★
    //   原来这里只试 PyCore::get_inst_dict（固定偏移），对 C 扩展类
    //   （ChatVoiceMgr / GameUnit / ……）读不到 __dict__ 就**什么都不输出** ✗
    //   dump_expand 里的 attr_dict_of 会扫一圈偏移找 __dict__，
    //   找不到就打原始内存（至少能看到字段布局）✓
    //   g_append_mode 包一层：dump_expand 内部有自己的 reset 逻辑，
    //   不包的话会把上面已经写好的"类型名"行冲掉 ✗
    g_append_mode = true;
    dump_expand(addr, 3);
    g_append_mode = false;
    return;
}

    PyCore::Dict dk;
    if (!PyCore::read_dict(addr, dk)) { wline("dict 读取失败"); return; }

    wline("===== dict 0x%llx (%lld 个键) =====", (unsigned long long)addr, dk.n_entries);
    wline("过滤: %s", g_filter[0] ? g_filter : "(无)");

    int shown = 0;
    for (int64_t i = 0; i < dk.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(dk, i));
        char kname[64] = {0};
        if (!PyCore::read_str(kp, kname, sizeof(kname))) {
            // int key?
            uint64_t ktp = getPtr64(kp + 8);
            if (ktp == PyRoot::g_int_type) {
                int64_t sz=0; uint32_t dg=0;
                vm_readv(kp+16, &sz, 8); vm_readv(kp+24, &dg, 4);
                int64_t iv = (sz==0)?0:(int64_t)dg; if (sz<0) iv=-iv;
                uint64_t vp = getPtr64(PyCore::value_slot(dk, i));
                w("  [%lld] = ", iv);
                dump_value(vp, 2);
                w("\n");
                shown++;
                continue;
            }
            wline("  [? key 0x%llx]", (unsigned long long)kp);
            continue;
        }
        if (!passes_filter(kname)) continue;

        uint64_t vp = getPtr64(PyCore::value_slot(dk, i));
        w("  %-40s = ", kname);
        dump_value(vp, 2);
        w("\n");
        shown++;
    }
    wline("===== 共显示 %d / %lld 条 =====", shown, dk.n_entries);
    snprintf((char*)g_status, 128, "OK: dict %d/%ld", shown, dk.n_entries);
}

// ---- Dump 5: units_by_type[N] 里第 idx 个 unit ----
static void dump_unit_by_type(int type, int idx)
{
    reset_buf();
    const char *why = nullptr;
    uint64_t md = PyCore::game_kernel_dict(why);
    if (!md) { wline("game_kernel 不可用: %s", why?why:"?"); return; }

    uint64_t gk_obj = 0;
    PyCore::Dict dk;
    if (!PyCore::read_dict(md, dk)) return;
    for (int64_t i = 0; i < dk.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(dk, i));
        char kname[64];
        if (PyCore::read_str(kp, kname, sizeof(kname)) && strcmp(kname, "unit_mgr") == 0) {
            gk_obj = getPtr64(PyCore::value_slot(dk, i));
            break;
        }
    }
    if (!gk_obj) { wline("unit_mgr 找不到"); return; }

    uint64_t umg_dict = PyCore::get_inst_dict(gk_obj);
    if (!umg_dict) { wline("unit_mgr.__dict__ 失败"); return; }

    PyCore::Dict umd;
    if (!PyCore::read_dict(umg_dict, umd)) return;

    uint64_t ubt = 0;
    for (int64_t i = 0; i < umd.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(umd, i));
        char kname[64];
        if (PyCore::read_str(kp, kname, sizeof(kname)) && strcmp(kname, "units_by_type") == 0) {
            ubt = getPtr64(PyCore::value_slot(umd, i));
            break;
        }
    }
    if (!ubt) { wline("units_by_type 找不到"); return; }

    uint64_t lst = PyCore::get_type_list(ubt, type);
    if (!lst) { wline("type %d 不存在", type); return; }

    int64_t n = 0; uint64_t items = 0;
    if (!PyCore::read_list(lst, n, items)) { wline("list 读取失败"); return; }
    if (idx < 0 || idx >= n) { wline("idx %d 超出范围 (0~%lld)", idx, n - 1); return; }

    uint64_t unit = getPtr64(items + idx * 8);
    dump_unit_dict(unit);
}

// ---- 按 unit_type 把所有 unit 的完整 __dict__ 都 dump 出来 ----
// 特殊模式（加页手记/摸金）排查用：看 GoldRushPlayerUnit 之类的字段结构
static void dump_type_units(int64_t type)
{
    reset_buf();
    const char *why = nullptr;
    uint64_t md = PyCore::game_kernel_dict(why);
    if (!md) { wline("game_kernel 不可用: %s", why?why:"?"); return; }

    uint64_t gk_obj = 0;
    PyCore::Dict dk;
    if (!PyCore::read_dict(md, dk)) return;
    for (int64_t i = 0; i < dk.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(dk, i));
        char kname[64];
        if (PyCore::read_str(kp, kname, sizeof(kname)) && strcmp(kname, "unit_mgr") == 0) {
            gk_obj = getPtr64(PyCore::value_slot(dk, i));
            break;
        }
    }
    if (!gk_obj) { wline("unit_mgr 找不到"); return; }

    uint64_t umg_dict = PyCore::get_inst_dict(gk_obj);
    if (!umg_dict) { wline("unit_mgr.__dict__ 失败"); return; }

    PyCore::Dict umd;
    if (!PyCore::read_dict(umg_dict, umd)) return;

    uint64_t ubt = 0;
    for (int64_t i = 0; i < umd.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(umd, i));
        char kname[64];
        if (PyCore::read_str(kp, kname, sizeof(kname)) && strcmp(kname, "units_by_type") == 0) {
            ubt = getPtr64(PyCore::value_slot(umd, i));
            break;
        }
    }
    if (!ubt) { wline("units_by_type 找不到"); return; }

    uint64_t lst = PyCore::get_type_list(ubt, (int)type);
    if (!lst) { wline("type=%lld 不存在", (long long)type); return; }

    int64_t n = 0; uint64_t items = 0;
    if (!PyCore::read_list(lst, n, items)) { wline("list 读取失败"); return; }

    wline("===== type=%lld 共 %lld 个单位 =====", (long long)type, (long long)n);

    for (int64_t i = 0; i < n; i++) {
        uint64_t unit = getPtr64(items + i * 8);
        if (!PyCore::is_obj(unit)) continue;

        wline("");
        wline("----- unit[%lld] @ 0x%llX -----", (long long)i, (unsigned long long)unit);

        uint64_t ud = PyCore::get_inst_dict(unit);
        PyCore::Dict udk;
        if (!ud || !PyCore::read_dict(ud, udk)) {
            wline("  dict 读取失败");
            continue;
        }

        for (int64_t k = 0; k < udk.n_entries; k++) {
            uint64_t kkp = getPtr64(PyCore::key_slot(udk, k));
            char kname[64] = {0};
            if (!PyCore::read_str(kkp, kname, sizeof(kname))) continue;

            uint64_t vp = getPtr64(PyCore::value_slot(udk, k));

            w("  %-32s = ", kname);

            if (!PyCore::is_obj(vp)) {
                w("(null)");
                w("\n");
                continue;
            }

            // 先试 int
            int64_t iv = 0;
            if (PyCore::read_int(vp, iv)) {
                w("%lld  obj=0x%llX", (long long)iv, (unsigned long long)vp);
                w("\n");
                continue;
            }

            // 再试 str
            char sv[64];
            if (PyCore::read_str(vp, sv, sizeof(sv))) {
                w("'%s'  obj=0x%llX", sv, (unsigned long long)vp);
                w("\n");
                continue;
            }

            // 最后按 float3 猜
            float f3[3] = {0};
            if (vm_readv(vp + 0x10, f3, 12)) {
                // 判断是不是合理的 float：有限且绝对值合理
                if (isfinite(f3[0]) && isfinite(f3[1]) && isfinite(f3[2]) &&
                    fabsf(f3[0]) < 1e6f && fabsf(f3[1]) < 1e6f && fabsf(f3[2]) < 1e6f) {
                    w("f3=(%.2f, %.2f, %.2f)  obj=0x%llX",
                      f3[0], f3[1], f3[2], (unsigned long long)vp);
                    w("\n");
                    continue;
                }
            }

            // 其他：至少打印指针
            uint64_t vtp = getPtr64(vp + 8);
            char tn[32] = {0};
            uint64_t np = getPtr64((vtp & 0x00FFFFFFFFFFFFFFULL) + 0x18);
            if (PyCore::is_obj(np)) vm_readv(np, tn, 31);
            w("<%s>  obj=0x%llX", tn[0]?tn:"?", (unsigned long long)vp);
            w("\n");
        }
    }

    snprintf((char*)g_status, 128, "OK: type=%lld %lld 单位", (long long)type, (long long)n);
}

// ---- Dump 6: 场景对象地址 -> 反查 CPython unit（供 "Dump 当前操控单位" 用）----
// PySelf::anchor 给的是 unit.model+0x20 的场景对象。反查：遍历 units_by_type 的
// [1]/[2]/[236]/[1065]，对每个 unit 读 model->+0x20 比较。命中就 dump 该 unit 的 __dict__
static void dump_unit_by_scene(uint64_t scene_obj)
{
    reset_buf();
    if (!is_obj(scene_obj)) { wline("场景对象地址非法"); return; }

    const char *why = nullptr;
    uint64_t md = PyCore::game_kernel_dict(why);
    if (!md) { wline("game_kernel 不可用: %s", why?why:"?"); return; }

    uint64_t gk_obj = 0;
    PyCore::Dict dk;
    if (!PyCore::read_dict(md, dk)) return;
    for (int64_t i = 0; i < dk.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(dk, i));
        char kname[64];
        if (PyCore::read_str(kp, kname, sizeof(kname)) && strcmp(kname, "unit_mgr") == 0) {
            gk_obj = getPtr64(PyCore::value_slot(dk, i));
            break;
        }
    }
    if (!gk_obj) { wline("unit_mgr 找不到"); return; }

    uint64_t umg_dict = PyCore::get_inst_dict(gk_obj);
    if (!umg_dict) { wline("unit_mgr.__dict__ 失败"); return; }

    PyCore::Dict umd;
    if (!PyCore::read_dict(umg_dict, umd)) return;

    uint64_t ubt = 0;
    for (int64_t i = 0; i < umd.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(umd, i));
        char kname[64];
        if (PyCore::read_str(kp, kname, sizeof(kname)) && strcmp(kname, "units_by_type") == 0) {
            ubt = getPtr64(PyCore::value_slot(umd, i));
            break;
        }
    }
    if (!ubt) { wline("units_by_type 找不到"); return; }

    static const int TYPES[] = {1, 2, 236, 1065};
    for (int t = 0; t < 4; t++) {
        uint64_t lst = PyCore::get_type_list(ubt, TYPES[t]);
        if (!lst) continue;
        int64_t n = 0; uint64_t items = 0;
        if (!PyCore::read_list(lst, n, items) || n <= 0) continue;
        for (int64_t i = 0; i < n; i++) {
            uint64_t unit = getPtr64(items + i * 8);
            if (!is_obj(unit)) continue;
            uint64_t ud = PyCore::get_inst_dict(unit);
            if (!ud) continue;
            PyCore::Dict udk;
            if (!PyCore::read_dict(ud, udk)) continue;
            for (int64_t j = 0; j < udk.n_entries; j++) {
                uint64_t kp = getPtr64(PyCore::key_slot(udk, j));
                char kname[64];
                if (!PyCore::read_str(kp, kname, sizeof(kname)) || strcmp(kname, "model") != 0) continue;
                uint64_t model = getPtr64(PyCore::value_slot(udk, j));
                if (PyCore::get_scene_obj(model) == scene_obj) {
                    char note[96];
                    snprintf(note, sizeof(note), "type=%d idx=%d unit=0x%llx", TYPES[t], (int)i, (unsigned long long)unit);
                    dump_unit_dict(unit, note);
                    return;
                }
            }
        }
    }
    wline("没在 [1]/[2]/[236]/[1065] 里找到 scene=0x%llx", (unsigned long long)scene_obj);
    wline("(可能正在切操控对象, 或本体集合还没刷新)");
    snprintf((char*)g_status, 128, "反查失败");
}

// ---- Dump 7: 技能字段专用 ----
static void print_skill_instance(uint64_t vp)
{
    if (!is_obj(vp)) { w("(null)"); return; }
    uint64_t tp = getPtr64(vp + 8);
    char tn[32] = {0};
    uint64_t np = getPtr64(tp + 0x18);
    if (is_obj(np)) vm_readv(np, tn, 31);

    if (strcmp(tn, "int") == 0) {
        int64_t iv = 0;
        if (PyCore::read_int(vp, iv)) w("%lld", (long long)iv);
        else w("<int?>");
        return;
    }
    if (strcmp(tn, "float") == 0) {
        double dv = 0; vm_readv(vp + 16, &dv, 8);
        w("%.2f", dv);
        return;
    }
    if (strcmp(tn, "bool") == 0) { w("%s", (vp == PyRoot::g_true) ? "True" : "False"); return; }
    if (strcmp(tn, "str") == 0) {
        char sb[64];
        if (PyCore::read_str(vp, sb, sizeof(sb))) w("'%s'", sb);
        else w("<str>");
        return;
    }
    uint64_t d = PyCore::get_inst_dict(vp);
    if (!d) { w("<%s @ 0x%llx>", tn, (unsigned long long)vp); return; }
    PyCore::Dict dk;
    if (!PyCore::read_dict(d, dk)) { w("<%s dict读取失败>", tn); return; }
    w("<%s", tn);
    static const char* WANT[] = {"_cd_delta", "cd_time", "power_num", "_cur_power_num"};
    for (int f = 0; f < 4; f++) {
        for (int64_t i = 0; i < dk.n_entries; i++) {
            uint64_t kp = getPtr64(PyCore::key_slot(dk, i));
            char kn[64] = {0};
            if (!PyCore::read_str(kp, kn, sizeof(kn)) || strcmp(kn, WANT[f]) != 0) continue;
            uint64_t val = getPtr64(PyCore::value_slot(dk, i));
            double dv = 0; int64_t iv = 0;
            uint64_t vtp = is_obj(val) ? (getPtr64(val + 8) & 0x00FFFFFFFFFFFFFFULL) : 0;
            if (vtp == PyRoot::g_float_type) { vm_readv(val + 16, &dv, 8); w(" %s=%.3f", kn, dv); }
            else if (vtp == PyRoot::g_int_type && PyCore::read_int(val, iv)) w(" %s=%lld", kn, (long long)iv);
            break;
        }
    }
    w(">");
}

static void dump_skill_fields(uint64_t unit_obj)
{
    reset_buf();
    if (!is_obj(unit_obj)) { wline("unit 地址非法"); return; }

    uint64_t ud = PyCore::get_inst_dict(unit_obj);
    PyCore::Dict udk;
    if (!ud || !PyCore::read_dict(ud, udk)) { wline("unit dict 读取失败"); return; }

    uint64_t skill_mgr = 0;
    for (int64_t i = 0; i < udk.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(udk, i));
        char kn[64] = {0};
        if (PyCore::read_str(kp, kn, sizeof(kn)) && strcmp(kn, "skill_mgr") == 0) {
            skill_mgr = getPtr64(PyCore::value_slot(udk, i));
            break;
        }
    }
    if (!is_obj(skill_mgr)) { wline("skill_mgr 拿不到"); return; }

    uint64_t smd = PyCore::get_inst_dict(skill_mgr);
    PyCore::Dict smk;
    if (!smd || !PyCore::read_dict(smd, smk)) { wline("skill_mgr dict 失败"); return; }
    wline("===== skill_mgr = 0x%llx =====", (unsigned long long)skill_mgr);

    static const char* FIELDS[] = {"skill_dict", "skill_bind_btn", "skill_btn_casted", "skill_lst", "last_cast_skill_id"};
    static const bool  IS_LIST[] = {false, false, false, true, false};
    for (int f = 0; f < 5; f++) {
        uint64_t vp = 0;
        for (int64_t i = 0; i < smk.n_entries; i++) {
            uint64_t kp = getPtr64(PyCore::key_slot(smk, i));
            char kn[64] = {0};
            if (PyCore::read_str(kp, kn, sizeof(kn)) && strcmp(kn, FIELDS[f]) == 0) {
                vp = getPtr64(PyCore::value_slot(smk, i));
                break;
            }
        }
        wline("");
        wline("---- %s = 0x%llx ----", FIELDS[f], (unsigned long long)vp);
        if (!is_obj(vp)) { wline("  (空)"); continue; }

        if (IS_LIST[f]) {
            int64_t n = 0; uint64_t items = 0;
            if (!PyCore::read_list(vp, n, items)) { wline("  list 读取失败"); continue; }
            wline("  list len=%lld", (long long)n);
            for (int64_t i = 0; i < n && i < 32; i++) {
                w("   [%lld] = ", (long long)i);
                print_skill_instance(getPtr64(items + i * 8));
                w("\n");
            }
        } else {
            PyCore::Dict dk;
            if (!PyCore::read_dict(vp, dk)) {
                w("  (非 dict) ");
                print_skill_instance(vp);
                w("\n");
                continue;
            }
            wline("  dict %lld keys", (long long)dk.n_entries);
            for (int64_t i = 0; i < dk.n_entries && i < 64; i++) {
                uint64_t kp = getPtr64(PyCore::key_slot(dk, i));
                w("   ");
                int64_t iv = 0;
                char kn[64] = {0};
                if (PyCore::read_int(kp, iv))     w("[%lld] = ", (long long)iv);
                else if (PyCore::read_str(kp, kn, sizeof(kn))) w("[%s] = ", kn);
                else w("[?] = ");
                print_skill_instance(getPtr64(PyCore::value_slot(dk, i)));
                w("\n");
            }
        }
    }
    snprintf((char*)g_status, 128, "OK: skill 字段 dump");
}

// ---- Dump 8: 地图候选字段（无需输入过滤词，自动多关键词扫描） ----
// 键盘调不起来时的替代方案：一次扫描所有跟地图/场景/关卡沾边的键
static void dump_map_candidates()
{
    reset_buf();
    const char *why = nullptr;
    uint64_t md = PyCore::game_kernel_dict(why);
    if (!md) { wline("game_kernel 不可用: %s", why ? why : "?"); return; }

    PyCore::Dict dk;
    if (!PyCore::read_dict(md, dk)) { wline("md_dict 读取失败"); return; }

    // 命中任意一个就输出。前缀/后缀同时列，覆盖 snake_case 和 camelCase
    static const char* kws[] = {
        "map", "Map", "MAP",
        "scene", "Scene", "SCENE",
        "level", "Level", "LEVEL",
        "room", "Room", "ROOM",
        "dungeon", "Dungeon",
        "area", "Area", "floor", "Floor",
        "region", "Region", "zone", "Zone",
        "world", "World", "terrain", "Terrain",
        nullptr
    };

    wline("===== game_kernel 地图候选 (共 %lld 键) =====", (long long)dk.n_entries);
    wline("%-4s  %-42s  %s", "#", "键名", "值");
    wline("%-4s  %-42s  %s", "----", "------------------------------------------", "----");

    int shown = 0;
    for (int64_t i = 0; i < dk.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(dk, i));
        char kname[96] = {0};
        if (!PyCore::read_str(kp, kname, sizeof(kname))) continue;

        bool hit = false;
        for (int k = 0; kws[k]; k++) {
            if (strstr(kname, kws[k])) { hit = true; break; }
        }
        if (!hit) continue;

        uint64_t vp = getPtr64(PyCore::value_slot(dk, i));
        w("%-4lld  %-42s  = ", (long long)i, kname);
        dump_value(vp, 0);
        w("\n");
        shown++;
    }
    wline("===== 命中 %d 条 =====", shown);
    snprintf((char*)g_status, 128, "地图候选: 命中 %d 条", shown);
}

// ---- Dump 9: 当前地图元数据（scene_data / info_dict） ----
static void dump_current_scene()
{
    reset_buf();
    const char *why = nullptr;
    uint64_t md = PyCore::game_kernel_dict(why);
    if (!md) { wline("game_kernel 不可用: %s", why ? why : "?"); return; }

    // g_world_mgr
    int64_t i_wm = PyCore::find_key(md, "g_world_mgr", -1);
    if (i_wm < 0) { wline("找不到 g_world_mgr"); return; }
    uint64_t wm = PyCore::get_attr(md, i_wm);
    uint64_t wmd = PyCore::get_inst_dict(wm);
    if (!wmd) { wline("g_world_mgr 不是 managed-dict"); return; }
    PyCore::Dict wmk;
    if (!PyCore::read_dict(wmd, wmk)) { wline("g_world_mgr dict 读取失败"); return; }

    // cur_world
    int64_t i_cw = PyCore::find_key(wmd, "cur_world", -1);
    if (i_cw < 0) { wline("找不到 cur_world"); return; }
    uint64_t cw = PyCore::get_attr(wmd, i_cw);
    uint64_t cwd = PyCore::get_inst_dict(cw);
    if (!cwd) { wline("cur_world 不是 managed-dict"); return; }
    PyCore::Dict cwk;
    if (!PyCore::read_dict(cwd, cwk)) { wline("cur_world dict 读取失败"); return; }

    // 关键字段先单独打印
    wline("===== cur_world 关键字段 =====");
    const char *keys[] = {
        "scene_id", "scene_path", "scene_model_group",
        "world_type", "game_type", "match_type", "room_type",
        "x_size", "z_size",
        "layer_id", "battle_id", "guuid",
        nullptr
    };
    for (int k = 0; keys[k]; k++) {
        int64_t i = PyCore::find_key(cwd, keys[k], -1);
        if (i < 0) { wline("  %-20s = <无>", keys[k]); continue; }
        uint64_t vp = getPtr64(PyCore::value_slot(cwk, i));
        w("  %-20s = ", keys[k]);
        dump_value(vp, 0);
        w("\n");
    }

    // scene_data 展开
    wline("");
    wline("===== scene_data =====");
    int64_t i_sd = PyCore::find_key(cwd, "scene_data", -1);
    if (i_sd >= 0) {
        uint64_t sd = getPtr64(PyCore::value_slot(cwk, i_sd));
        PyCore::Dict sdk;
        if (PyCore::read_dict(sd, sdk)) {
            wline("(%lld keys)", (long long)sdk.n_entries);
            for (int64_t i = 0; i < sdk.n_entries; i++) {
                uint64_t kp = getPtr64(PyCore::key_slot(sdk, i));
                char kn[96] = {0};
                if (!PyCore::read_str(kp, kn, sizeof(kn))) continue;
                uint64_t vp = getPtr64(PyCore::value_slot(sdk, i));
                w("  %-32s = ", kn);
                dump_value(vp, 0);
                w("\n");
            }
        } else {
            wline("scene_data 不是 dict 或读失败");
        }
    }

    // info_dict 展开
    wline("");
    wline("===== info_dict =====");
    int64_t i_id = PyCore::find_key(cwd, "info_dict", -1);
    if (i_id >= 0) {
        uint64_t id = getPtr64(PyCore::value_slot(cwk, i_id));
        PyCore::Dict idk;
        if (PyCore::read_dict(id, idk)) {
            wline("(%lld keys)", (long long)idk.n_entries);
            for (int64_t i = 0; i < idk.n_entries; i++) {
                uint64_t kp = getPtr64(PyCore::key_slot(idk, i));
                char kn[96] = {0};
                if (!PyCore::read_str(kp, kn, sizeof(kn))) continue;
                uint64_t vp = getPtr64(PyCore::value_slot(idk, i));
                w("  %-32s = ", kn);
                dump_value(vp, 0);
                w("\n");
            }
        } else {
            wline("info_dict 不是 dict 或读失败");
        }
    }

    snprintf((char*)g_status, 128, "scene dump 完成");
}

// ---- Dump 10: cur_world.scene_childs（找房间/走廊） ----
static void dump_scene_childs()
{
    reset_buf();
    const char *why = nullptr;
    uint64_t md = PyCore::game_kernel_dict(why);
    if (!md) { wline("game_kernel 不可用: %s", why ? why : "?"); return; }

    int64_t i_wm = PyCore::find_key(md, "g_world_mgr", -1);
    if (i_wm < 0) { wline("找不到 g_world_mgr"); return; }
    uint64_t wm = PyCore::get_attr(md, i_wm);
    uint64_t wmd = PyCore::get_inst_dict(wm);
    PyCore::Dict wmk;
    if (!wmd || !PyCore::read_dict(wmd, wmk)) { wline("g_world_mgr dict 失败"); return; }

    int64_t i_cw = PyCore::find_key(wmd, "cur_world", -1);
    if (i_cw < 0) { wline("找不到 cur_world"); return; }
    uint64_t cw = PyCore::get_attr(wmd, i_cw);
    uint64_t cwd = PyCore::get_inst_dict(cw);
    PyCore::Dict cwk;
    if (!cwd || !PyCore::read_dict(cwd, cwk)) { wline("cur_world dict 失败"); return; }

    // scene_childs
    int64_t i_sc = PyCore::find_key(cwd, "scene_childs", -1);
    if (i_sc < 0) { wline("找不到 scene_childs"); return; }
    uint64_t sc = getPtr64(PyCore::value_slot(cwk, i_sc));
    PyCore::Dict sck;
    if (!PyCore::read_dict(sc, sck)) { wline("scene_childs 不是 dict"); return; }

    wline("===== scene_childs (%lld keys) =====", (long long)sck.n_entries);
    wline("");

    for (int64_t i = 0; i < sck.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(sck, i));
        char kn[96] = {0};
        if (!PyCore::read_str(kp, kn, sizeof(kn))) {
            // int 键
            int64_t kv = 0;
            if (PyCore::read_int(kp, kv)) snprintf(kn, sizeof(kn), "[%lld]", (long long)kv);
            else snprintf(kn, sizeof(kn), "[?]");
        }
        uint64_t vp = getPtr64(PyCore::value_slot(sck, i));

        wline("--- %s @ 0x%llX ---", kn, (unsigned long long)vp);
        dump_value(vp, 2);
        wline("");
    }

    snprintf((char*)g_status, 128, "scene_childs dump 完成");
}

// ---- Dump 11: 找房间数据 ---- 
// 一次性扫几个可能藏房间的地方
static void dump_room_hunt()
{
    reset_buf();
    const char *why = nullptr;
    uint64_t md = PyCore::game_kernel_dict(why);
    if (!md) { wline("game_kernel 不可用"); return; }

    // 1) g_world_mgr.WORLD_FAC
    int64_t i_wm = PyCore::find_key(md, "g_world_mgr", -1);
    if (i_wm >= 0) {
        uint64_t wm = PyCore::get_attr(md, i_wm);
        uint64_t wmd = PyCore::get_inst_dict(wm);
        PyCore::Dict wmk;
        if (wmd && PyCore::read_dict(wmd, wmk)) {
            int64_t i_fac = PyCore::find_key(wmd, "WORLD_FAC", -1);
            if (i_fac >= 0) {
                uint64_t fac = getPtr64(PyCore::value_slot(wmk, i_fac));
                PyCore::Dict fk;
                if (PyCore::read_dict(fac, fk)) {
                    wline("===== WORLD_FAC (%lld) =====", (long long)fk.n_entries);
                    for (int64_t i = 0; i < fk.n_entries; i++) {
                        uint64_t kp = getPtr64(PyCore::key_slot(fk, i));
                        char kn[96] = {0};
                        if (!PyCore::read_str(kp, kn, sizeof(kn))) continue;
                        uint64_t vp = getPtr64(PyCore::value_slot(fk, i));
                        w("  %-32s = ", kn);
                        dump_value(vp, 0);
                        w("\n");
                    }
                }
            }
        }
    }

    // 2) cur_world.scene 对象
    if (i_wm >= 0) {
        uint64_t wm = PyCore::get_attr(md, i_wm);
        uint64_t wmd = PyCore::get_inst_dict(wm);
        PyCore::Dict wmk;
        if (wmd && PyCore::read_dict(wmd, wmk)) {
            int64_t i_cw = PyCore::find_key(wmd, "cur_world", -1);
            if (i_cw >= 0) {
                uint64_t cw = PyCore::get_attr(wmd, i_cw);
                uint64_t cwd = PyCore::get_inst_dict(cw);
                PyCore::Dict cwk;
                if (cwd && PyCore::read_dict(cwd, cwk)) {
                    int64_t i_sc = PyCore::find_key(cwd, "scene", -1);
                    if (i_sc >= 0) {
                        uint64_t scene = getPtr64(PyCore::value_slot(cwk, i_sc));
                        wline("");
                        wline("===== cur_world.scene @ 0x%llX =====", (unsigned long long)scene);
                        dump_value(scene, 0);
                        wline("");
                    }
                }
            }
        }
    }

    // 3) game_kernel 里所有含 room/region/cell/corridor/tile 的键
    wline("");
    wline("===== game_kernel 里候选键 =====");
    PyCore::Dict dk;
    if (PyCore::read_dict(md, dk)) {
        static const char* kws[] = {
            "room", "Room", "region", "Region", "cell", "Cell",
            "corridor", "Corridor", "tile", "Tile", "nav", "Nav",
            "floor", "Floor", "wall", "Wall", "door", "Door",
            "grid", "Grid", "layout", "Layout",
            nullptr
        };
        for (int64_t i = 0; i < dk.n_entries; i++) {
            uint64_t kp = getPtr64(PyCore::key_slot(dk, i));
            char kn[96] = {0};
            if (!PyCore::read_str(kp, kn, sizeof(kn))) continue;
            for (int k = 0; kws[k]; k++) {
                if (strstr(kn, kws[k])) {
                    uint64_t vp = getPtr64(PyCore::value_slot(dk, i));
                    w("  %-42s = ", kn);
                    dump_value(vp, 0);
                    w("\n");
                    break;
                }
            }
        }
    }

    snprintf((char*)g_status, 128, "room hunt 完成");
}

// ---- Dump 12: 随机地图布局（区块 / 房间 / 走廊）----------------------------
// h55_random 这类地图，每局到底启用了哪些区块是随机的（走廊/房间的组合）。
// 已经用 dump_room_hunt / dump_map_candidates 排除掉的入口（本局实测）：
//     cur_world.nav_mgr / region_mgr / collision_mgr   = None
//     scene_region_basement_data / _cave_data / _coaster_light_data = 空 dict
//     region_name_data                                 = 空 list
// 剩下的强候选：
//     cur_world.world_visible_block_list               = set   ← 逐元素展开
//     cur_world.softwall_wood_uids                     = set
// 另外把所有「名字像区块、且非空」的容器键也列出来，避免漏。
//
// CPython set 对象布局（64 位）：
//   +0 refcnt  +8 type  +16 fill  +24 used  +32 mask  +40 table  +48 hash  +56 finger
//   +64 smalltable[]
//   table == NULL 时元素就在 smalltable 里；setentry = { key(8), hash(8) }
static bool set_header(uint64_t sp, int64_t &used, int64_t &mask, uint64_t &table)
{
    if (!is_obj(sp)) return false;
    uint8_t b[64];
    if (!vm_readv(sp, b, sizeof(b))) return false;
    memcpy(&used,  b + 24, 8);
    memcpy(&mask,  b + 32, 8);
    memcpy(&table, b + 40, 8);
    if (!is_obj(table)) table = sp + 64;      // 内联 smalltable
    if (used < 0 || used > 100000 || mask < 0 || mask > 100000) return false;
    return true;
}

static void dump_set(uint64_t sp, const char *note)
{
    int64_t used = 0, mask = 0; uint64_t table = 0;
    if (!set_header(sp, used, mask, table)) { w("(不是可读的 set)"); return; }
    w("<set used=%lld mask=%lld table=0x%llX> [", (long long)used, (long long)mask,
      (unsigned long long)table);
    int shown = 0;
    for (int64_t i = 0; i <= mask && shown < used; i++) {
        uint64_t kp = 0;
        if (!vm_readv(table + (uint64_t)i * 16, &kp, 8)) continue;
        if (!is_obj(kp)) continue;            // NULL / dummy 槽
        if (shown) w(", ");
        dump_value(kp, 0);
        shown++;
    }
    w("]");
    (void)note;
}

// 结构上像不像 set：
//   PySetObject  +16 fill  +24 used  +32 mask(一定是 2^n-1)  +40 table
//   PyDictObject +16 ma_used  +24 version_tag(大数)  +32 keys 指针(大数)
//   所以「mask 是 2^n-1 且 used<=mask+1」能把 dict 排掉。再显式排除 g_dict_type。
static bool looks_like_set(uint64_t vp)
{
    if (!is_obj(vp)) return false;
    if (getPtr64(vp + 8) == PyRoot::g_dict_type) return false;
    int64_t used = 0, mask = 0; uint64_t table = 0;
    if (!set_header(vp, used, mask, table)) return false;
    if (mask < 7) return false;
    if (((mask + 1) & mask) != 0) return false;      // 必须是 2^n - 1
    if (used > mask + 1) return false;
    if (!is_obj(table)) return false;
    return true;
}

// 扫一个 dict，把「名字像区块/房间/走廊」的键打出来；值是 set 的就展开
static void sweep_layout_dict(const char *label, uint64_t dobj, int indent)
{
    PyCore::Dict k;
    if (!is_obj(dobj) || !PyCore::read_dict(dobj, k)) {
        wline("%*s%s: 读不到 dict", indent, "", label);
        return;
    }
    wline("%*s===== %s (%lld keys) =====", indent, "", label, (long long)k.n_entries);

    static const char* kws[] = {
        "block", "Block", "room", "Room", "area", "Area", "region", "Region",
        "section", "Section", "floor", "Floor", "grid", "Grid", "tile", "Tile",
        "nav", "Nav", "door", "Door", "wall", "Wall", "cave", "Cave",
        "layer", "Layer", "born", "Born", "pos_id", "PosId", "group", "Group",
        "scene_child", "preload", "world_dct", "layout",
        nullptr
    };

    for (int64_t i = 0; i < k.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(k, i));
        char kn[96] = {0};
        if (!PyCore::read_str(kp, kn, sizeof(kn))) continue;

        bool hit = false;
        for (int q = 0; kws[q]; q++) if (strstr(kn, kws[q])) { hit = true; break; }
        if (!hit) continue;

        uint64_t vp = getPtr64(PyCore::value_slot(k, i));
        if (!vp || !is_obj(vp)) continue;

        w("%*s  %-40s = ", indent, "", kn);
        if (looks_like_set(vp)) dump_set(vp, kn);
        else                    dump_value(vp, 2);
        w("\n");
    }
    wline("");
}

static void dump_layout_blocks()
{
    reset_buf();
    const char *why = nullptr;
    uint64_t md = PyCore::game_kernel_dict(why);
    if (!md) { wline("game_kernel 不可用: %s", why ? why : "?"); return; }

    int64_t i_wm = PyCore::find_key(md, "g_world_mgr", -1);
    if (i_wm < 0) { wline("找不到 g_world_mgr"); return; }
    uint64_t wm  = PyCore::get_attr(md, i_wm);
    uint64_t wmd = PyCore::get_inst_dict(wm);
    if (!is_obj(wmd)) { wline("g_world_mgr 实例字典读不到"); return; }

    // ★ 重点：world_visible_block_list 属于 g_world_mgr，不在 cur_world 里
    //   （上一版找错对象了，所以报「不存在」）
    sweep_layout_dict("g_world_mgr", wmd, 0);

    int64_t i_cw = PyCore::find_key(wmd, "cur_world", -1);
    if (i_cw >= 0) {
        uint64_t cw  = PyCore::get_attr(wmd, i_cw);
        uint64_t cwd = PyCore::get_inst_dict(cw);
        sweep_layout_dict("cur_world", cwd, 0);
    } else {
        wline("找不到 cur_world");
    }

    // 已知的强候选，无条件展开一遍
    static const struct { const char* dictname; const char* key; } kForce[] = {
        { "g_world_mgr", "world_visible_block_list" },
        { "g_world_mgr", "preload_world_dct"        },
        { "g_world_mgr", "extra_battle_scene_world_dct" },
        { "g_world_mgr", "extra_hall_scene_world_dct"   },
        { "cur_world",   "softwall_wood_uids"       },
        { "cur_world",   "dynamic_showing_groups"   },
    };
    wline("===== 已知强候选（强制展开）=====");
    for (auto &f : kForce) {
        uint64_t dobj = (!strcmp(f.dictname, "g_world_mgr")) ? wmd : 0;
        if (!dobj) {
            int64_t ic = PyCore::find_key(wmd, "cur_world", -1);
            if (ic < 0) continue;
            dobj = PyCore::get_inst_dict(PyCore::get_attr(wmd, ic));
        }
        PyCore::Dict k;
        if (!PyCore::read_dict(dobj, k)) continue;
        int64_t idx = PyCore::find_key(dobj, f.key, -1);
        w("  %-14s %-32s = ", f.dictname, f.key);
        if (idx < 0) { w("(不存在)\n"); continue; }
        uint64_t vp = getPtr64(PyCore::value_slot(k, idx));
        if (looks_like_set(vp)) dump_set(vp, f.key);
        else                    dump_value(vp, 0);
        w("\n");
    }

    snprintf((char*)g_status, 128, "layout blocks dump 完成");
}

// ---- Dump 13: 原始字典探针（带类型名）--------------------------------------
// 之前 dump_scene_childs 里有一批键读出来是**空字符串**，一直没搞清是什么。
// 这里绕过 read_str，直接读对象头拿**类型名**（PyTypeObject.tp_name 在 type+24），
// 再打原始字节，就能确定键/值到底是不是 str、bytes、int 还是别的。
static bool type_name_of(uint64_t obj, char *out, size_t cap)
{
    out[0] = 0;
    if (!is_obj(obj) || cap < 2) return false;
    uint64_t tp = getPtr64(obj + 8);
    if (!is_obj(tp)) return false;
    uint64_t namep = getPtr64(tp + 24);        // PyTypeObject.tp_name
    if (!is_obj(namep)) return false;
    if (!vm_readv(namep, out, cap - 1)) return false;
    out[cap - 1] = 0;
    return true;
}

// 尽量把对象转成可读文本：str 读内容、int 读数值、其它给类型名 + 原始字节
static void describe_obj(uint64_t obj, char *out, size_t cap)
{
    out[0] = 0;
    if (!obj) { snprintf(out, cap, "(null)"); return; }
    if (!is_obj(obj)) { snprintf(out, cap, "(非指针 0x%llx)", (unsigned long long)obj); return; }

    char tn[64] = {0};
    type_name_of(obj, tn, sizeof(tn));

    char s[128] = {0};
    if (strcmp(tn, "str") == 0) {
        // 先用快路径（compact ASCII），不行再走完整解码（中文 UCS2/UCS4）
        if (!PyCore::read_str(obj, s, sizeof(s)))
            PyCore::read_str_any(obj, s, sizeof(s));
        if (s[0]) { snprintf(out, cap, "str \"%s\"", s); return; }
    }
    int64_t iv = 0;
    if (strcmp(tn, "int") == 0 && PyCore::read_int(obj, iv)) {
        snprintf(out, cap, "int %lld", (long long)iv);
        return;
    }
    if (strcmp(tn, "bytes") == 0 && PyCore::read_str(obj, s, sizeof(s))) {
        snprintf(out, cap, "bytes \"%s\"", s);
        return;
    }

    uint8_t raw[24] = {0};
    vm_readv(obj, raw, sizeof(raw));
    char hex[80] = {0};
    for (int i = 0; i < 24; i++) snprintf(hex + i * 3, 4, "%02x ", raw[i]);
    snprintf(out, cap, "%s  raw[%s]", tn[0] ? tn : "?", hex);
}

static void dump_dict_raw(uint64_t d)
{
    maybe_reset();
    PyCore::Dict k;
    if (!is_obj(d) || !PyCore::read_dict(d, k)) {
        wline("0x%llX 不是可读的 dict", (unsigned long long)d);
        return;
    }
    wline("===== dict @ 0x%llX  (%lld 条) —— 带类型名 + 地址 =====",
          (unsigned long long)d, (long long)k.n_entries);
    wline("");
    for (int64_t i = 0; i < k.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(k, i)) & 0x00FFFFFFFFFFFFFFULL;
        uint64_t vp = getPtr64(PyCore::value_slot(k, i)) & 0x00FFFFFFFFFFFFFFULL;
        char kd[256], vd[256];
        describe_obj(kp, kd, sizeof(kd));
        describe_obj(vp, vd, sizeof(vd));
        // 地址要打出来 —— 后面要靠它去 dict_at / memdump 继续深挖
        wline("[%2lld] key 0x%012llX %-34s | val 0x%012llX %s",
              (long long)i,
              (unsigned long long)kp, kd,
              (unsigned long long)vp, vd);
    }
    snprintf((char*)g_status, 128, "dict_raw dump 完成");
}

// ---- Dump 19: 类型信息（找字段偏移的关键）----------------------------------
// 前置声明：这两个函数定义在文件后面
static uint64_t attr_dict_of(uint64_t obj);
static void     dump_memdump(uint64_t addr, int64_t len);


// 想知道 ClientRoomSurface 的字段在哪，最直接的是看它这个 **type 对象**：
//   tp_basicsize (type+32) = 实例占多少字节  → 决定能放几个字段
//   tp_itemsize  (type+40)
//   再把它自己的 dict dump 出来看 __slots__ / 方法名
static void dump_typeinfo(uint64_t tpaddr)
{
    reset_buf();
    if (!is_obj(tpaddr)) { wline("0x%llX 不是合法地址", (unsigned long long)tpaddr); return; }

    char tn[64] = {0};
    type_name_of(tpaddr, tn, sizeof(tn));
    wline("对象 0x%llX 的类型是 \"%s\"", (unsigned long long)tpaddr, tn);

    // 直接把它当 type 对象读
    char selfname[128] = {0};
    uint64_t namep = getPtr64(tpaddr + 24) & 0x00FFFFFFFFFFFFFFULL;
    if (is_obj(namep)) vm_readv(namep, selfname, sizeof(selfname) - 1);

    uint64_t basicsize = getPtr64(tpaddr + 32);
    uint64_t itemsize  = getPtr64(tpaddr + 40);

    wline("");
    wline("===== 当作 type 对象解读 =====");
    wline("  tp_name      = \"%s\"", selfname);
    wline("  tp_basicsize = %llu   (0x%llX)", (unsigned long long)basicsize,
          (unsigned long long)basicsize);
    wline("  tp_itemsize  = %llu", (unsigned long long)itemsize);
    if (basicsize >= 16 && basicsize <= 4096) {
        wline("  → 实例大小 %llu 字节 = 头部 16 + 数据 %llu 字节，"
              "即 %llu 个 8 字节字段",
              (unsigned long long)basicsize, (unsigned long long)(basicsize - 16),
              (unsigned long long)((basicsize - 16) / 8));
    }
    wline("");

    uint64_t d = attr_dict_of(tpaddr);
    if (!is_obj(d)) {
        // ★ type 对象的 __dict__ 是 PyTypeObject.tp_dict，在 **+0x108** ——
        //   attr_dict_of 只扫 +8..+48（那是给普通实例用的），够不到。
        //   扫一圈 0xD0..0x140 把真正的 tp_dict 找出来，才能看到 __slots__ / 方法名。
        for (int off = 0xD0; off <= 0x140; off += 8) {
            uint64_t p = getPtr64(tpaddr + off) & 0x00FFFFFFFFFFFFFFULL;
            if (!is_obj(p)) continue;
            char tn2[32] = {0};
            if (!type_name_of(p, tn2, sizeof(tn2))) continue;
            if (strcmp(tn2, "dict") == 0) {
                wline("  (tp_dict 在 type+0x%X)", off);
                d = p;
                break;
            }
        }
    }
    // 子函数内部也会写缓冲，这里用追加模式，别把上面的 tp_basicsize 冲掉
    g_append_mode = true;
    if (is_obj(d)) {
        wline("===== 该 type 自己的 dict（__slots__ / 方法名在这里）=====");
        wline("");
        dump_dict_raw(d);
    } else {
        wline("type 对象没有可读的 dict，改打内存：");
        dump_memdump(tpaddr, 384);
    }
    g_append_mode = false;
    snprintf((char*)g_status, 128, "typeinfo dump 完成");
}

// ---- Dump 14: 枚举 sys.modules --------------------------------------------
static void dump_modules()
{
    reset_buf();
    const char *why = nullptr;
    uint64_t md = PyCore::game_kernel_dict(why);
    if (!md) { wline("game_kernel 不可用: %s", why ? why : "?"); return; }

    uint64_t sm_slot = PyRoot::g_modules_slot;
    uint64_t sm = sm_slot ? getPtr64(sm_slot) : 0;
    if (!is_obj(sm)) { wline("sys.modules 取不到 (slot=0x%llX)", (unsigned long long)sm_slot); return; }

    PyCore::Dict k;
    if (!PyCore::read_dict(sm, k)) { wline("sys.modules 不是 dict"); return; }

    wline("===== sys.modules (%lld 个) =====", (long long)k.n_entries);
    wline("");
    for (int64_t i = 0; i < k.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(k, i));
        char kn[160] = {0};
        if (!PyCore::read_str(kp, kn, sizeof(kn))) continue;
        uint64_t vp = getPtr64(PyCore::value_slot(k, i));
        wline("  %-58s @ 0x%llX", kn, (unsigned long long)vp);
    }
    snprintf((char*)g_status, 128, "modules dump 完成");
}

// ---- Dump 15: 取 game_kernel 里某个名字，打印 + 探它 ------------------------
static void dump_named_raw(const char *name)
{
    reset_buf();
    if (!name || !*name) { wline("需要名字（用 Dump 页的过滤词框填）"); return; }
    const char *why = nullptr;
    uint64_t md = PyCore::game_kernel_dict(why);
    if (!md) { wline("game_kernel 不可用"); return; }

    int64_t idx = PyCore::find_key(md, name, -1);
    if (idx < 0) { wline("game_kernel 里没有 '%s'", name); return; }
    uint64_t vp = PyCore::get_attr(md, idx);

    char vd[256];
    describe_obj(vp, vd, sizeof(vd));
    wline("game_kernel['%s'] = %s", name, vd);
    wline("");
    if (is_obj(vp)) dump_dict_raw(vp);
    snprintf((char*)g_status, 128, "named raw dump 完成");
}

// 稳健版「找到这个对象里的 dict」：
//   PyCore::get_inst_dict 只看 +16 一个偏移，对「模块对象」就不适用
//   （而且实测这里的指针高位带 tag，比如 0xb4000072d5a4d9b0，必须先掩掉）。
//   这里把 +8..+48 每个偏移都试一遍，用**类型名 == "dict"** 来判定，最可靠。
static uint64_t find_any_dict(uint64_t obj)
{
    if (!is_obj(obj)) return 0;
    for (int off = 8; off <= 48; off += 8) {
        uint64_t p = getPtr64(obj + off) & 0x00FFFFFFFFFFFFFFULL;
        if (!is_obj(p)) continue;
        char tn[32] = {0};
        if (!type_name_of(p, tn, sizeof(tn))) continue;
        if (strcmp(tn, "dict") == 0) return p;
    }
    return 0;
}

// 取对象的属性字典：先按常规实例布局（+16），不行再用按类型名探测的兜底
// （模块对象的字典不在 +16，必须走兜底）
static uint64_t attr_dict_of(uint64_t obj)
{
    uint64_t d = PyCore::get_inst_dict(obj);
    if (is_obj(d)) return d;
    return find_any_dict(obj);
}
// sys.modules 里有 common_cs.RoomSurfaceMgr / pydata.scene_config 这类模块，
// 很可能就是地图布局/房间数据的所在。这里直接按名字取到模块的 __dict__ 再探。
static void dump_module_raw(const char *modname)
{
    reset_buf();
    if (!modname || !*modname) { wline("需要模块名（用 name= 传）"); return; }

    uint64_t sm_slot = PyRoot::g_modules_slot;
    uint64_t sm = sm_slot ? getPtr64(sm_slot) : 0;
    if (!is_obj(sm)) { wline("sys.modules 取不到"); return; }

    int64_t idx = PyCore::find_key(sm, modname, -1);
    if (idx < 0) { wline("sys.modules 里没有 '%s'", modname); return; }
    uint64_t mod = PyCore::get_attr(sm, idx) & 0x00FFFFFFFFFFFFFFULL;

    char md[256];
    describe_obj(mod, md, sizeof(md));
    wline("sys.modules['%s'] = %s", modname, md);
    wline("");

    uint64_t d = find_any_dict(mod);
    if (!is_obj(d)) {
        wline("没有找到 dict —— 把对象前 64 字节打出来看：");
        uint8_t raw[64] = {0};
        vm_readv(mod, raw, sizeof(raw));
        for (int i = 0; i < 64; i += 16) {
            w("  +%02d  ", i);
            for (int k = 0; k < 16; k++) w("%02x ", raw[i + k]);
            w("\n");
        }
        return;
    }
    dump_dict_raw(d);
    snprintf((char*)g_status, 128, "module_raw dump 完成");
}

// 前置声明：memdump 定义在后面，path_raw 要用
static void dump_memdump(uint64_t addr, int64_t len);

// ---- Dump 17: 按点号路径探 game_kernel -------------------------------------
// 例如 "g_world_mgr.cur_world.scene_childs" —— scene_childs 是 cur_world 的属性，
// 不在 game_kernel 顶层，所以需要能一路点下去。
static void dump_path_raw(const char *path)
{
    reset_buf();
    if (!path || !*path) { wline("需要路径（用 name= 传）"); return; }

    // 支持 "mod:" 前缀：从 sys.modules 起步（模块名本身带点，取最长匹配前缀）
    //   例：mod:common_cs.RoomSurfaceMgr.ClientRoomSurface
    const char *p = path;
    uint64_t cur = 0;
    bool from_mod = false;
    if (strncmp(p, "mod:", 4) == 0) {
        p += 4;
        from_mod = true;
        uint64_t sm_slot = PyRoot::g_modules_slot;
        uint64_t sm = sm_slot ? getPtr64(sm_slot) : 0;
        if (!is_obj(sm)) { wline("sys.modules 取不到"); return; }

        // 从最长的前缀开始试，找到 sys.modules 里的那个模块
        char tmp[256];
        snprintf(tmp, sizeof(tmp), "%s", p);
        int best = -1;
        for (int cut = (int)strlen(tmp); cut > 0; --cut) {
            if (tmp[cut] != '.' && tmp[cut] != '\0') continue;
            char save = tmp[cut];
            tmp[cut] = '\0';
            int64_t idx = PyCore::find_key(sm, tmp, -1);
            tmp[cut] = save;
            if (idx >= 0) { best = cut; break; }
        }
        if (best < 0) { wline("sys.modules 里找不到 '%s' 的任何前缀", p); return; }

        char modname[256];
        snprintf(modname, sizeof(modname), "%.*s", best, p);
        int64_t idx = PyCore::find_key(sm, modname, -1);
        cur = PyCore::get_attr(sm, idx) & 0x00FFFFFFFFFFFFFFULL;
        char vd[256];
        describe_obj(cur, vd, sizeof(vd));
        wline("sys.modules['%s'] = 0x%llX %s", modname, (unsigned long long)cur, vd);
        p += best;
        if (*p == '.') ++p;
        if (!*p) { wline(""); dump_dict_raw(attr_dict_of(cur)); return; }
    } else {
        const char *why = nullptr;
        cur = PyCore::game_kernel_dict(why);
        if (!cur) { wline("game_kernel 不可用"); return; }
        wline("game_kernel");
    }
    (void)from_mod;

    char buf[256];
    snprintf(buf, sizeof(buf), "%s", p);
    char *save = nullptr;
    for (char *tok = strtok_r(buf, ".", &save); tok; tok = strtok_r(nullptr, ".", &save)) {
        uint64_t d = attr_dict_of(cur);
        if (!is_obj(d)) d = cur;
        int64_t idx = PyCore::find_key(d, tok, -1);
        if (idx < 0) {
            wline("  .%s  <-- 找不到（当前层不是 dict / 没这个键）", tok);
            return;
        }
        uint64_t vp = PyCore::get_attr(d, idx) & 0x00FFFFFFFFFFFFFFULL;
        char vd[256];
        describe_obj(vp, vd, sizeof(vd));
        wline("  .%s = 0x%llX %s", tok, (unsigned long long)vp, vd);
        cur = vp;
    }

    char vd[256];
    describe_obj(cur, vd, sizeof(vd));
    wline("末级对象: %s  @ 0x%llX", vd, (unsigned long long)cur);
    wline("");

    // 末级本身就是 dict 就直接展开
    char tn[32] = {0};
    type_name_of(cur, tn, sizeof(tn));
    if (strcmp(tn, "dict") == 0) {
        dump_dict_raw(cur);
    } else if (strcmp(tn, "list") == 0 || strcmp(tn, "tuple") == 0 || strcmp(tn, "set") == 0) {
        wline("末级是 %s，逐元素打印：", tn);
        int64_t n = 0; uint64_t items = 0;
        if (PyCore::read_list(cur, n, items)) {
            for (int64_t i = 0; i < n && i < 400; i++) {
                uint64_t ep = getPtr64(items + (uint64_t)i * 8) & 0x00FFFFFFFFFFFFFFULL;
                char ed[256];
                describe_obj(ep, ed, sizeof(ed));
                wline("  [%2lld] %s", (long long)i, ed);
                uint64_t sub = attr_dict_of(ep);
                if (is_obj(sub) && i < 8) {
                    char sub_tn[32] = {0};
                    type_name_of(ep, sub_tn, sizeof(sub_tn));
                    wline("       ^ 这是 %s，其字段：", sub_tn);
                    dump_dict_raw(sub);
                }
            }
        }
    } else {
        uint64_t d = attr_dict_of(cur);
        if (is_obj(d)) {
            dump_dict_raw(d);
        } else {
            // 没有 __dict__（__slots__ / C 扩展类型）——直接把内存打出来看字段布局
            wline("末级对象没有 __dict__，改打它的原始内存（找字段偏移）：");
            wline("");
            dump_memdump(cur, 512);
        }
    }
    snprintf((char*)g_status, 128, "path_raw dump 完成");
}

// ---- Dump 18: 原始内存 dump（找 __slots__ / C 扩展对象的字段布局）----------
// ClientRoomSurface 的字段不在 +16 的 __dict__ 里，只能直接把对象内存打出来，
// 看哪些偏移上放着合法指针（指向 list/dict），从而定位房间列表。
static void dump_memdump(uint64_t addr, int64_t len)
{
    maybe_reset();
    if (!is_obj(addr)) { wline("0x%llX 不是合法对象地址", (unsigned long long)addr); return; }
    if (len <= 0 || len > 2048) len = 512;

    uint8_t buf[2048];
    if (!vm_readv(addr, buf, (size_t)len)) { wline("读取失败"); return; }

    wline("===== 内存 0x%llX  共 %lld 字节 =====", (unsigned long long)addr, (long long)len);
    wline("");
    for (int64_t i = 0; i < len; i += 16) {
        w("  +%03llX  ", (unsigned long long)i);
        for (int k = 0; k < 16; k++) w("%02x ", buf[i + k]);
        w(" | ");
        for (int k = 0; k < 16; k++) {
            uint8_t c = buf[i + k];
            w("%c", (c >= 32 && c < 127) ? (char)c : '.');
        }
        // 把这一行里每个 8 字节当指针解读，标注它指向什么类型
        w(" |");
        for (int k = 0; k + 8 <= 16; k += 8) {
            uint64_t p = 0;
            memcpy(&p, buf + i + k, 8);
            p &= 0x00FFFFFFFFFFFFFFULL;
            if (!is_obj(p)) continue;
            char tn[32] = {0};
            if (!type_name_of(p, tn, sizeof(tn)) || !tn[0]) continue;
            w(" +%lld:%s", (long long)(i + k), tn);
        }
        w("\n");
    }
    snprintf((char*)g_status, 128, "memdump 完成");
}

// ---- Dump 20: 在对象图里按名字找键，打印路径 --------------------------------
// 之前只能按**已知路径**走（path_raw），不知道路径就束手无策。
// 这个从 game_kernel 出发做有界 DFS，把「某个键名到底挂在谁身上」找出来 ——
// 找 maze_mgr / voxel_mgr / MazeVoxelData 这类只在特定状态下才存在的对象必备。
//
// 边界：深度 ≤ 7，访问节点 ≤ 1024，避免卡住渲染线程。
// 边界：深度 ≤ 12，访问节点 ≤ 8192。
//   ● 原来只有 1024/深度 7 —— 够不到 Cocos 的 UI 节点树（小地图节点就挂在那里）。
//     OmniSight 那种工具是用 gc.get_objects() 扫全部 40 万对象，
//     我们只读内存没法调用 gc，只能靠把预算放大来覆盖到。
//   ● 同时也按**类型名**匹配：MiniMap / MiniMapBigVer 这类节点是靠类名认的，
//     键名里根本没有它。
struct FpNode { uint64_t obj; uint8_t depth; uint16_t plen; char path[168]; };

#define FP_MAXNODE 8192
#define FP_MAXDEPTH 12

static void dump_find_path(const char *want)
{
    reset_buf();
    if (!want || !*want) { wline("需要一个键名（用 name= 传；以 * 开头表示子串匹配，如 *maze*）"); return; }

    // "*xxx*" = 子串匹配（不区分大小写）；不带 * = 精确键名。
    // 两种模式都会**同时**比对「键名」和「值的类型名」。
    bool substr = (want[0] == '*');
    char needle[128] = {0};
    if (substr) {
        snprintf(needle, sizeof(needle), "%s", want + 1);
        size_t n = strlen(needle);
        if (n && needle[n - 1] == '*') needle[n - 1] = 0;
    } else {
        snprintf(needle, sizeof(needle), "%s", want);
    }
    for (size_t i = 0; needle[i]; i++)
        if (needle[i] >= 'A' && needle[i] <= 'Z') needle[i] = (char)(needle[i] - 'A' + 'a');

    const char *why = nullptr;
    uint64_t root = PyCore::game_kernel_dict(why);
    if (!root) { wline("game_kernel 不可用"); return; }

    static FpNode st[FP_MAXNODE];
    int sp = 0;
    st[sp].obj = root; st[sp].depth = 0; st[sp].plen = 0; st[sp].path[0] = 0;
    sp++;

    int visited = 0, hits = 0;
    wline("===== 从 game_kernel 找 %s\"%s\"（键名 + 类型名）=====", substr ? "(子串) " : "", want);
    wline("");

    while (sp > 0 && visited < FP_MAXNODE) {
        FpNode cur = st[--sp];
        if (cur.depth > FP_MAXDEPTH) continue;

        PyCore::Dict k;
        if (!is_obj(cur.obj) || !PyCore::read_dict(cur.obj, k)) continue;
        visited++;

        for (int64_t i = 0; i < k.n_entries; i++) {
            uint64_t kp = getPtr64(PyCore::key_slot(k, i));
            char kn[128] = {0};
            if (!PyCore::read_str(kp, kn, sizeof(kn))) continue;
            uint64_t vp = getPtr64(PyCore::value_slot(k, i)) & 0x00FFFFFFFFFFFFFFULL;

            auto match = [&](const char *s) -> bool {
                if (!s || !*s) return false;
                char low[128] = {0};
                snprintf(low, sizeof(low), "%s", s);
                for (size_t q = 0; low[q]; q++)
                    if (low[q] >= 'A' && low[q] <= 'Z') low[q] = (char)(low[q] - 'A' + 'a');
                return substr ? (strstr(low, needle) != nullptr) : (strcmp(low, needle) == 0);
            };

            char vtn[64] = {0};
            type_name_of(vp, vtn, sizeof(vtn));

            bool key_hit = match(kn);
            bool type_hit = match(vtn);

            if (key_hit || type_hit) {
                char vd[200];
                describe_obj(vp, vd, sizeof(vd));
                wline("★ %s%s%s%s", cur.path, cur.plen ? "." : "", kn,
                      type_hit && !key_hit ? "   <-- 类型名命中" : "");
                wline("     = %s", vd);
                hits++;
                if (hits >= 150) { wline("(命中过多，提前结束)"); sp = 0; break; }
            }

            // 继续往下：值本身是 dict，或者是有 __dict__ 的对象
            uint64_t sub = 0;
            char tn[32] = {0};
            type_name_of(vp, tn, sizeof(tn));
            if (strcmp(tn, "dict") == 0) sub = vp;
            else if (strcmp(tn, "list") == 0 || strcmp(tn, "tuple") == 0) {
                // list/tuple 里也可能是对象，取前几个看看
                int64_t n = 0; uint64_t items = 0;
                if (PyCore::read_list(vp, n, items)) {
                    for (int64_t e = 0; e < n && e < 8; e++) {
                        uint64_t ep = getPtr64(items + (uint64_t)e * 8) & 0x00FFFFFFFFFFFFFFULL;
                        uint64_t ed = attr_dict_of(ep);
                        if (is_obj(ed) && sp < 1000) {
                            st[sp].obj = ed;
                            st[sp].depth = (uint8_t)(cur.depth + 1);
                            st[sp].plen = (uint16_t)snprintf(st[sp].path, sizeof(st[sp].path),
                                                            "%s%s%s[%lld]",
                                                            cur.path, cur.plen ? "." : "",
                                                            kn, (long long)e);
                            sp++;
                        }
                    }
                }
                continue;
            } else {
                sub = attr_dict_of(vp);
            }
            if (!is_obj(sub) || sp >= 1000) continue;
            st[sp].obj = sub;
            st[sp].depth = (uint8_t)(cur.depth + 1);
            st[sp].plen = (uint16_t)snprintf(st[sp].path, sizeof(st[sp].path),
                                            "%s%s%s", cur.path, cur.plen ? "." : "", kn);
            sp++;
        }
    }

    wline("");
    wline("访问了 %d 个 dict，命中 %d 处", visited, hits);
    if (!hits) wline("(没找到 —— 可能该对象在当前状态下尚未创建)");
    snprintf((char*)g_status, 128, "find_path 完成");
}

// ---- Dump 21: 内存模式搜索 ------------------------------------------------
// 为什么需要：想找 MiniMap / MiniMapBigVer 这类**类名**在不在，
// find_path 只能顺着 dict 走（Cocos 节点的树不在 Python dict 里），
// 而枚举全堆又要调 gc.get_objects()（我们只读内存，调不了）。
// 所以退一步：直接在进程内存里按字节搜字符串 —— 类加载了，它的
// tp_name 就一定在内存里。
//
// 用法: fn=memscan  name=<要搜的字符串>  arg=起始地址  arg2=长度
// 分块扫描，命中就记地址并把周围一小段按字符串打出来。
static void dump_memscan(const char *pat, uint64_t start, uint64_t len)
{
    reset_buf();
    if (!pat || !*pat) { wline("需要要搜的字符串（用 name= 传）"); return; }
    if (!start) { wline("需要起始地址（arg=）"); return; }

    const size_t plen = strlen(pat);
    // 上限 256MB，避免一次扫太久把渲染线程卡死
    const uint64_t kMaxScan = 256ULL * 1024 * 1024;
    if (len == 0 || len > kMaxScan) len = kMaxScan;

    const size_t kChunk = 64 * 1024;
    static uint8_t buf[64 * 1024 + 256];

    wline("===== 在 0x%llX .. 0x%llX (%llu MB) 里搜 \"%s\" =====",
          (unsigned long long)start, (unsigned long long)(start + len),
          (unsigned long long)(len / 1048576), pat);
    wline("");

    int hits = 0;
    uint64_t off = 0;
    while (off < len && hits < 40) {
        size_t want = (size_t)((len - off) < kChunk ? (len - off) : kChunk);
        if (!vm_readv(start + off, buf, want)) { off += kChunk; continue; }

        for (size_t i = 0; i + plen <= want; i++) {
            if (buf[i] != (uint8_t)pat[0]) continue;
            if (memcmp(buf + i, pat, plen) != 0) continue;

            uint64_t addr = start + off + i;
            wline("★ 0x%llX", (unsigned long long)addr);
            // 把命中点前后各 48 字节里能打印的部分打出来
            size_t s = (i > 48) ? i - 48 : 0;
            size_t e = (i + plen + 48 < want) ? i + plen + 48 : want;
            char txt[160]; int n = 0;
            for (size_t k = s; k < e && n < 150; k++) {
                uint8_t c = buf[k];
                txt[n++] = (c >= 32 && c < 127) ? (char)c : ' ';
            }
            txt[n] = 0;
            wline("    上下文: %s", txt);
            if (++hits >= 40) { wline("(命中过多，提前结束)"); break; }
        }
        off += want;
    }

    wline("");
    wline("命中 %d 处", hits);
    if (!hits) wline("(没有 —— 说明这个字符串/类名不在内存里，即尚未加载)");
    snprintf((char*)g_status, 128, "memscan 完成");
}

// ---- Dump 21b: 数值模式搜索 ------------------------------------------------
// 为什么需要：memscan 只能搜**字符串**，但帧率上限是个**数值**。
//   想找 NeoX 引擎的帧率配置（config_fps_lag(highFps, poorFps, midFpsInterval)
//   会把 60 / 30 存在相邻内存），就必须能搜数值。
//
// 用法: fn=memfind  name=<模式>  arg=起始地址  arg2=长度
//
// 模式语法：
//   f:60                搜 float 60.0
//   i:60                搜 int32 60
//   f:60,30@512         搜 float 60，且其后 512 字节内还有一个 float 30
//   i:60,30,8@64        int 60、30、8 三个都在 64 字节内
//
// 多个值必须**按顺序、在 @ 指定的窗口内**出现 —— 这是用来把
// 「一堆孤立的 60」收敛成「一处真正的帧率配置」的关键。
static void dump_memfind(const char *pat, uint64_t start, uint64_t len)
{
    reset_buf();
    if (!pat || !*pat) { wline("需要搜索模式（name=，例如 f:60,30@512）"); return; }
    if (!start) { wline("需要起始地址（arg=）"); return; }

    // ---- 解析模式： 类型:值1,值2,值3@窗口 ----
    char typ = 0;
    const char* q = pat;
    if ((q[0]=='f'||q[0]=='i'||q[0]=='d') && q[1]==':') { typ = q[0]; q += 2; }
    if (!typ) { wline("模式要以 f: 或 i: 或 d: 开头（float / int32 / double）"); return; }

    double vals[8]; int nvals = 0;
    uint64_t window = 256;
    {
        char tmp[256]; snprintf(tmp, sizeof(tmp), "%s", q);
        char* at = strchr(tmp, '@');
        if (at) { *at = 0; window = strtoull(at + 1, nullptr, 0); if (!window) window = 256; }
        char* tok = strtok(tmp, ",");
        while (tok && nvals < 8) { vals[nvals++] = atof(tok); tok = strtok(nullptr, ","); }
    }
    if (!nvals) { wline("没解析出要搜的值"); return; }
    if (window > 65536) window = 65536;
    if (window < 8) window = 8;

    const size_t esz = (typ == 'f') ? 4 : (typ == 'd' ? 8 : 4);
    const uint64_t kMaxScan = 512ULL * 1024 * 1024;
    if (len == 0 || len > kMaxScan) len = kMaxScan;

    wline("===== memfind  模式=%s  类型=%s  值=%d 个  窗口=%llu 字节 =====",
          pat, typ=='f'?"float":(typ=='d'?"double":"int32"),
          nvals, (unsigned long long)window);
    wline("      范围 0x%llX .. 0x%llX  (%llu MB)",
          (unsigned long long)start, (unsigned long long)(start + len),
          (unsigned long long)(len / 1048576));
    wline("");

    // ⚠ 块大小必须小。
    //   实测 vm_readv 读 256KB 会直接失败，而循环里失败是 `off += kChunk; continue;`
    //   —— 于是整个扫描 0 秒就"完成"，一个命中都没有，看起来像"没找到"，
    //   实际是根本没读进来。工程里验证过能用的最大读是 0x1000(4KB)，就用它。
    const size_t kChunk = 4096;
    // 多留 window 字节，保证跨块的组合不漏
    static uint8_t buf[4096 + 65536 + 64];
    int readFail = 0, readOk = 0;

    auto matchAt = [&](const uint8_t* b, size_t i, double v) -> bool {
        if (typ == 'f') { float x; memcpy(&x, b + i, 4); return x == (float)v; }
        if (typ == 'd') { double x; memcpy(&x, b + i, 8); return x == v; }
        int32_t x; memcpy(&x, b + i, 4); return x == (int32_t)v;
    };

    int hits = 0;
    uint64_t off = 0;
    while (off < len && hits < 30) {
        size_t want = (size_t)((len - off) < kChunk ? (len - off) : kChunk);
        size_t want2 = want + (size_t)window;                 // 多读一段
        if (off + want2 > len) want2 = (size_t)(len - off);
        if (!vm_readv(start + off, buf, want2)) { readFail++; off += kChunk; continue; }
        readOk++;

        for (size_t i = 0; i + esz <= want2; i++) {
            if (!matchAt(buf, i, vals[0])) continue;

            // 后续值必须在窗口内按顺序出现
            bool all = true;
            size_t cursor = i + esz;
            for (int v = 1; v < nvals && all; v++) {
                bool found = false;
                size_t lim = i + (size_t)window;
                if (lim > want2) lim = want2;
                for (size_t k = cursor; k + esz <= lim; k += 1) {
                    if (matchAt(buf, k, vals[v])) { cursor = k + esz; found = true; break; }
                }
                if (!found) all = false;
            }
            if (!all) continue;

            uint64_t addr = start + off + i;
            wline("★ 0x%llX   [%.6g%s]", (unsigned long long)addr, vals[0],
                  typ=='f'?"f":(typ=='d'?"d":"i"));
            // 把附近 64 字节按「float x4 / int x8」两种解释都打出来，便于人眼判断
            size_t s = (i >= 32) ? i - 32 : 0;
            if (s + 96 > want2) s = (want2 > 96) ? want2 - 96 : 0;
            char line[400]; int n = 0;
            n += snprintf(line + n, sizeof(line) - n, "    附近 float: ");
            for (int k = 0; k < 8 && s + k*4 + 4 <= want2 && n < 300; k++) {
                float fv; memcpy(&fv, buf + s + k*4, 4);
                if (fv > -1e6f && fv < 1e6f) n += snprintf(line + n, sizeof(line) - n, "%.4g ", fv);
                else                         n += snprintf(line + n, sizeof(line) - n, "?? ");
            }
            wline("%s", line);
            n = 0;
            n += snprintf(line + n, sizeof(line) - n, "    附近 int  : ");
            for (int k = 0; k < 10 && s + k*4 + 4 <= want2 && n < 300; k++) {
                int32_t iv2; memcpy(&iv2, buf + s + k*4, 4);
                n += snprintf(line + n, sizeof(line) - n, "%d ", iv2);
            }
            wline("%s", line);

            if (++hits >= 30) { wline("(命中过多，提前结束 —— 请缩小范围或加更多值约束)"); break; }
        }
        off += want;
    }

    wline("");
    wline("读取: 成功 %d 块 / 失败 %d 块（块大小 %zu 字节）", readOk, readFail, kChunk);
    if (readOk == 0) wline("⚠ 一块都没读成功 —— 地址范围不可读，换个范围（本工具不会静默跳过）");
    wline("命中 %d 处", hits);
    if (!hits && readOk) wline("(没找到 —— 换类型 f:/i:/d:，加大窗口 @，或换扫描范围)");
    snprintf((char*)g_status, 128, "memfind 完成");
}

// ---- Dump 21c: 单点内存写入 ------------------------------------------------
// 用法: fn=memwrite  name=<类型 f|i|d>  arg=<地址>  arg2=<值>
//
// ⚠ 这是**唯一会改动游戏内存**的工具，所以每一步都留痕：
//   写之前先读旧值打出来，写完立刻读回校验。
//   想还原就把打出来的旧值再写一次。
//
// 批量：arg 可以是逗号分隔的多个地址，name 是逗号分隔的对应类型，
//       全部写同一个 arg2 的值。方便一次试多个候选。
static void dump_memwrite(const char *spec, uint64_t addrCSV, int64_t valBits)
{
    reset_buf();
    if (!addrCSV) { wline("需要地址（arg=）"); return; }
    if (!spec || !*spec) spec = "f";

    // 解析地址列表
    uint64_t addrs[16]; char types[16]; int n = 0;
    {
        char tmp[512]; snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)addrCSV);
        // arg 传进来的可能是十进制；也允许 name 里带十六进制地址
        char* tok = strtok(tmp, ",");
        while (tok && n < 16) { addrs[n] = strtoull(tok, nullptr, 0); types[n] = 'f'; n++; tok = strtok(nullptr, ","); }
    }
    // name 形如 "f" 或 "f,i,f"
    {
        char tmp[128]; snprintf(tmp, sizeof(tmp), "%s", spec);
        int i = 0;
        char* tok = strtok(tmp, ",");
        while (tok && i < n) {
            const char* c = tok;
            while (*c == ' ') c++;
            if (*c=='f'||*c=='i'||*c=='d'||*c=='F') types[i] = *c;
            i++; tok = strtok(nullptr, ",");
        }
    }

    wline("===== memwrite =====");
    wline("  要写的值: %lld (0x%llX)  [类型 %s]", (long long)valBits, (unsigned long long)valBits, spec);

    int okN = 0;
    for (int i = 0; i < n; i++) {
        uint64_t a = addrs[i];
        char t = types[i];
        if (t == 'F') {
            // F = arg2 是 float 的**原始 32 位模式**（IEEE754）。
            //   为什么需要：帧间隔这种值没法用整数表达 ——
            //   1/120 = 0.0083333338，而写入通道的 arg2 是 int64，
            //   走 'f' 会被当成 "把整数 0 转成 float"，写进去就是 0.0。
            //   所以直接传位模式最稳。
            uint32_t bits = (uint32_t)valBits;
            float nv; memcpy(&nv, &bits, 4);
            float ov = 0; bool rok = vm_readv(a, &ov, 4);
            bool wok = vm_writev(a, &nv, 4);
            float back = 0; bool bok = vm_readv(a, &back, 4);
            uint32_t bbits = 0; memcpy(&bbits, &back, 4);
            wline("  [%d] 0x%llX  float(bits 0x%08X)  旧=%.9g -> 新=%.9g   写=%s  回读=%.9g  %s",
                  i, (unsigned long long)a, bits, rok?ov:0.0f, nv,
                  wok?"成功":"失败", bok?back:0.0f,
                  (bok && bbits == bits) ? "✔ 校验通过" : "✘ 校验不符");
            if (wok && bok && bbits == bits) okN++;
        } else if (t == 'f') {
            float nv = (float)valBits;
            float ov = 0; bool rok = vm_readv(a, &ov, 4);
            bool wok = vm_writev(a, &nv, 4);
            float back = 0; bool bok = vm_readv(a, &back, 4);
            wline("  [%d] 0x%llX  float  旧=%.6g -> 新=%.6g   写=%s  回读=%.6g  %s",
                  i, (unsigned long long)a, rok?ov:0.0f, nv,
                  wok?"成功":"失败", bok?back:0.0f,
                  (bok && back == nv) ? "✔ 校验通过" : "✘ 校验不符");
            if (wok && bok && back == nv) okN++;
        } else if (t == 'd') {
            double nv = (double)valBits;
            double ov = 0; bool rok = vm_readv(a, &ov, 8);
            bool wok = vm_writev(a, &nv, 8);
            double back = 0; bool bok = vm_readv(a, &back, 8);
            wline("  [%d] 0x%llX  double 旧=%.6g -> 新=%.6g   写=%s  回读=%.6g  %s",
                  i, (unsigned long long)a, rok?ov:0.0, nv,
                  wok?"成功":"失败", bok?back:0.0,
                  (bok && back == nv) ? "✔ 校验通过" : "✘ 校验不符");
            if (wok && bok && back == nv) okN++;
        } else {
            int32_t nv = (int32_t)valBits;
            int32_t ov = 0; bool rok = vm_readv(a, &ov, 4);
            bool wok = vm_writev(a, &nv, 4);
            int32_t back = 0; bool bok = vm_readv(a, &back, 4);
            wline("  [%d] 0x%llX  int32  旧=%d -> 新=%d   写=%s  回读=%d  %s",
                  i, (unsigned long long)a, rok?ov:0, nv,
                  wok?"成功":"失败", bok?back:0,
                  (bok && back == nv) ? "✔ 校验通过" : "✘ 校验不符");
            if (wok && bok && back == nv) okN++;
        }
    }
    wline("");
    wline("成功 %d / %d 处", okN, n);
    snprintf((char*)g_status, 128, "memwrite 完成");
}

// ---- Dump 21d: 硬件断点 ------------------------------------------------
// 用法: fn=memhwbp  name=<op>  arg=<地址>  arg2=<类型或长度>
//   op = exec | read | write | rw | clear | info
//
// 命中后会打印完整的寄存器快照 —— 重点是 pc / lr / x0..x3，
// 有了它们就能从"某个 API 被调用"倒推回游戏自己的代码地址。
static void dump_memhwbp(const char *op, uint64_t addr, int64_t extra)
{
    reset_buf();
    if (!op || !*op) op = "info";
    char err[128] = {0};

    if (!strcmp(op, "clear")) {
        bool ok = MemHwbp::Clear(err, sizeof(err));
        wline("清除全部断点: %s %s", ok?"成功":"失败", err);
        return;
    }

    if (strcmp(op, "info") != 0) {
        uint32_t type = 4, len = 8;
        if      (!strcmp(op, "exec"))  { type = 4; len = extra ? (uint32_t)extra : 4; }
        else if (!strcmp(op, "read"))  { type = 1; len = extra ? (uint32_t)extra : 8; }
        else if (!strcmp(op, "write")) { type = 2; len = extra ? (uint32_t)extra : 8; }
        else if (!strcmp(op, "rw"))    { type = 3; len = extra ? (uint32_t)extra : 8; }
        else { wline("op 只能是 exec/read/write/rw/clear/info"); return; }

        if (!addr) { wline("需要地址（arg=）"); return; }
        bool ok = MemHwbp::Set(addr, type, len, err, sizeof(err));
        wline("下断点 0x%llX 类型=%s 长度=%u -> %s %s",
              (unsigned long long)addr, MemHwbp::TypeName(type), len,
              ok?"成功":"失败", err);
        if (!ok) return;
        wline("(断点已生效，等几秒让游戏跑到，然后再用 op=info 读记录)");
        return;
    }

    // ---- info: 显示配置 + 每个断点的命中记录 ----
#if defined(__ANDROID__)
    std::lock_guard<std::mutex> lk(g_kma_mtx);
    paradise_hwbp_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    if (!driver->hwbp_get_config(&cfg)) { wline("hwbp_get_config 失败"); return; }
    wline("===== 硬件断点配置 =====");
    wline("  tgid=%d  point_count=%u  BRP=%llu  WRP=%llu",
          cfg.tgid, cfg.point_count,
          (unsigned long long)cfg.num_brps, (unsigned long long)cfg.num_wrps);
    for (uint32_t i = 0; i < cfg.point_count && i < 16; i++) {
        wline("  [%u] 0x%llX  %s  len=%u  scope=%u", i,
              (unsigned long long)cfg.points[i].address,
              MemHwbp::TypeName(cfg.points[i].type),
              cfg.points[i].length, cfg.points[i].scope);
    }
    wline("");

    for (uint32_t i = 0; i < cfg.point_count && i < 16; i++) {
        static paradise_hwbp_record recs[32];
        uint32_t got = 0;
        memset(recs, 0, sizeof(recs));
        if (!driver->hwbp_get_records(i, 0, recs, 32, &got)) {
            wline("  [%u] 读记录失败", i);
            continue;
        }
        wline("  ===== 断点 [%u] 0x%llX  记录 %u 条 =====",
              i, (unsigned long long)cfg.points[i].address, got);
        for (uint32_t k = 0; k < got && k < 12; k++) {
            wline("    #%u  命中次数=%llu", k, (unsigned long long)recs[k].hit_count);
            wline("        pc = 0x%llX   lr = 0x%llX   sp = 0x%llX",
                  (unsigned long long)recs[k].pc,
                  (unsigned long long)recs[k].lr,
                  (unsigned long long)recs[k].sp);
            wline("        x0 = 0x%llX  x1 = 0x%llX  x2 = 0x%llX  x3 = 0x%llX",
                  (unsigned long long)recs[k].x0, (unsigned long long)recs[k].x1,
                  (unsigned long long)recs[k].x2, (unsigned long long)recs[k].x3);
            wline("        x0(int)=%lld  x1(int)=%lld",
                  (long long)recs[k].x0, (long long)recs[k].x1);
        }
    }
#else
    wline("非 Android");
#endif
    snprintf((char*)g_status, 128, "memhwbp 完成");
}
// (fpsunlock / Dump 21e 已删除 —— 该功能废弃：它把游戏的 eglSwapInterval 改成 1，
//  使游戏从 60fps 解锁到面板满帧，导致系统合成链路工作量翻倍、整机 CPU 从 ~50% 升到 ~100%。)


// ---------------------------------------------------------------------------
// 绘制逐门统计：排查"模仿者局里人突然不画"。
//   绘制循环里有一串 continue，每个都是一道门。只看画面分不清是哪道门吃的人，
//   所以每帧记一次各门的淘汰数，通过 MCP 的 draw_stats 读出来。
//   定义放这里(而不是 draw_Gui.cpp)是因为 dump_draw_stats 也要用它。
// ---------------------------------------------------------------------------
struct DrawStats {
    int total, skip_cls, is_self, goldrush, lobby, cc_skip, body_skip,
        xy0, dz, dist, camp_self, drawn;
    int cc_nobook;      // 是本体但身份簿里没有(照画，只是没身份标注)
    // ★ 本体集合的"命中/落空"计数，用来判断这个集合到底还配不配当前场景。
    //   2026-10-10 假面任务实测：假面一开，模型被换掉，身份簿(cc_live)和本体集合
    //   (is_body)**同时**失效，于是每个 player 类实体都走 "簿子没有且非本体" 被丢掉 ——
    //   计数器上就是 cc_skip 从 30 涨到 41、实际绘制从 11 掉到 1、跳过自身从 1 变 0。
    //   只靠 is_body 自己判断不出"集合已经跟场景无关了"，必须有这两个数：
    //     命中 > 0  -> 集合还在正常工作，落空的就是真该丢的(镜像/LOD/分身)
    //     命中 = 0 且落空 >= 3 -> 集合跟当前场景**毫无交集**，是换模型/换图了，不该再用它过滤
    int body_hit;       // player 类实体里**在本体集合中**的个数
    int body_miss;      // 因"不是本体"被丢掉的个数(只算这一道门，不含 cc_dead_unrendered)
    int masq;           // 本帧按「扮演者N」认领回来的假面替身个数
    int masq_dup;       // 因为该号已有替身在画而让位的本人对象个数
    int pos_claim;      // 本帧按**世界坐标**认领回座位的实体个数(假面的正解)
    // 坐标认领的诊断三分量：为什么没认领成
    int pc_noobj;       // objcoor==0：位置指针本身是空的
    int pc_zero;        // 指针在，但 +0xa0 读出来全 0
    int pc_have;        // 坐标可用，真的参与了比对
    int pos_live;       // 本帧有多少个框的位置改用 unit.position(靠 body_old 认出来的那些)
    int pos_fail;       // 本帧有多少个座位的 unit.position 读失败(框会沿用旧位置，不会跳)
    int pair_exact;     // 假面配对 A 级：位置精确重合(≤0.5 米)配上的座位数
    int pair_loose;     // 假面配对 B 级：已停用(会把错误配对锁死)，保留字段便于对照
    int pair_skip;      // 假面期间"配对在工作、但这个实体不在配对表里"而被跳过不画的对象数
    int anchor_missing; // 本帧有多少个座位的当前锚点不在实体列表里(>=3 判定为假面，见 s_masq_now)
    int pos_reject;     // 位置改写被合理性闸门挡下的次数(坏坐标，保持原位置不跳)
    int skip_self_cc;   // 按身份簿的号跳过的"自己"
    int dup_old;        // 靠 body_old 认出来、但该号 body 已在别处命中而被让位掉的对象数(防重复框)
    int pos_track;      // 当帧坐标读失败、改用持久跟踪值的框数(它们不会消失，只停在最后一次可信位置)
    int seatbox;        // 按座位补画的框数(假面期间没有实体可挂的那些号)
    int seatbox_nopos;  // 想补画但坐标不可用的次数
    // ★★ 2026-10-13 新增：**天赋绘制**逐门计数 ★★
    //   用户反馈"局内实时绘制在绘制框上的天赋不见了" ✗
    //   天赋那段有三道门，只靠画面分不清是哪道吃掉的 ✗
    //     tn_enter : 进入 show_draw_Genius 分支的实体数
    //     tn_mirror: 被 "camp2 且 is_mirror_unit" 判为镜像而整块跳过的数
    //     tn_lookup: lookup 失败(拿不到天赋数据)的数
    //     tn_empty : 三段文字全空(没东西可画)的数
    //     tn_drawn : 真的画了天赋的实体数
    int tn_enter, tn_mirror, tn_lookup, tn_empty, tn_drawn;
};
static DrawStats g_ds;
static DrawStats g_ds_pub;      // 给 MCP 读的快照

// ===========================================================================
// Dump 21h: 救援/动作状态取样 —— 为「自动震慑」定位"救人"这个动作的 state id
//
// 需求背景（用户描述）：
//   求生救人有一个 1~2 秒的**动作过程**，监管在这期间打中就是「震慑」。
//   但求生可以**自行打断**（取消救人）—— 于是变成博弈：
//     打早了 -> 对方取消 -> 监管挥空亏刀
//     打晚了 -> 人救下来了
//   我们要做的是：在「取消窗口关闭之后、动画结束之前」出刀，且提前量等于监管前摇。
//
// 所以第一步不是写逻辑，而是**先把状态找出来**：
//   救人期间，求生单位的 state_machine._state 最后一个 int 会变成某个特定值。
//   这个 dump 就是把附近每个求生/监管单位的当前 state id + 状态名打出来。
//
// 用法：在「救人前 / 救人中 / 救人后」各跑一次，对比输出 ——
//       只在「救人中」出现的那一行就是目标状态。
//       （Dump 页过滤词框可留空）
// ===========================================================================
static void dump_rescue_state()
{
    reset_buf();
    wline("===== 救援/动作状态取样 =====");
    wline("救人前 / 救人中 / 救人后 各跑一次，对比找只在救人期间出现的 state");
    wline("");

    // 约定（沿用 PySelf/PyAttack）：type 1 = 监管，type 2 = 求生
    const int   types[2] = { 2, 1 };
    const char *tnm[2]   = { "求生", "监管" };

    for (int ti = 0; ti < 2; ti++) {
        uint64_t units[32] = {};
        const int n = PyAttack::get_all_units_of_type(types[ti], units, 32);
        wline("---- %s (type=%d) 单位数=%d ----", tnm[ti], types[ti], n);

        for (int i = 0; i < n; i++) {
            const uint64_t u = units[i];
            if (!PyCore::is_obj(u)) {
                wline("  [%d] %llx  (不是 Python 对象)", i, (unsigned long long)u);
                continue;
            }

            // ---- 动作状态：把 state_machine._state **整个列表**打出来 ----
            //
            // ⚠ 第一版只取"最后一个 int"。结果：两局能看到某个 1 秒级状态，
            //   而另一局**明明救了 4 次却完全看不到** —— 说明救人不在这一层。
            //   _state 是个**栈**：末尾是当前层，前面的元素是下层。
            //   破译压在底层(所以能被末位反映)，而救人/交互很可能叠在另一层，
            //   或者根本在别的字段里。所以要**整列打出来**，不能再只看末位。
            const uint64_t sm = PyAttack::get_obj_field(u, "state_machine");
            char stackbuf[192] = "-";
            int  cur = -1;
            int  depth = 0;
            if (sm) {
                const uint64_t sl = PyAttack::get_obj_field(sm, "_state");
                if (PyCore::is_obj(sl)) {
                    int64_t  n = 0;
                    uint64_t items = 0;
                    if (PyCore::read_list(sl, n, items) && n > 0) {
                        if (n > 12) n = 12;          // 栈不会很深，截断防坏数据
                        depth = (int)n;
                        int off = 0;
                        for (int64_t k = 0; k < n; k++) {
                            int v = 0;
                            if (PyAttack::read_pylong(getPtr64(items + k * 8), v)) {
                                off += snprintf(stackbuf + off, sizeof(stackbuf) - off,
                                                (k ? ",%d" : "%d"), v);
                                if (k == n - 1) cur = v;
                            }
                            if (off >= (int)sizeof(stackbuf) - 10) break;
                        }
                    }
                }
            }

            // ---- 状态名（state_id_2_class_name 映射，PyAttack 启动时已加载）----
            const char *nm = "(未在映射表里)";
            auto it = PyAttack::g_id2name.find(cur);
            if (it != PyAttack::g_id2name.end()) nm = it->second.c_str();

            // ---- model / 身份：给一个能认人的标识 ----
            const uint64_t model = PyAttack::get_obj_field(u, "model");
            int uidv = 0;
            {   // uid 字段名在不同单位上不一定有，读不到就留 0
                PyCore::Dict dk;
                const uint64_t d = PyCore::get_inst_dict(u);
                if (d && PyCore::read_dict(d, dk)) {
                    const int64_t iu = PyCore::resolve_index(d, dk, -1, "uid");
                    if (iu >= 0) PyAttack::read_pylong(getPtr64(PyCore::value_slot(dk, iu)), uidv);
                }
            }

            wline("  [%d] uid=%llu 栈深=%d  state栈=[%s]  末位=%d %s",
                  i, (unsigned long long)(uint32_t)uidv, depth, stackbuf, cur, nm);
        }
        wline("");
    }

    wline("提示：把三次输出贴出来，我据此确定救人状态 id，再设计时机判定。");
    snprintf((char*)g_status, 128, "rescue_state 完成");
}
// （2026-10-13：dump_auto_shock 已随「自动震慑」功能一起删除 ✓）
// ---- Dump 21j: cam_dump lives in draw_Gui.cpp ----
//   It needs libbase / MatrixOffset / Matrix / is_user_ptr, which are
//   globals defined in draw_Gui.cpp; this header is included before them.
// ---- Dump 21k: 密码机列表原样输出（排查约瑟夫拍照/镜像世界）----------------
// 用法: fn=gen_dump
//   把 PyProgress 那一份双缓冲快照**原样**打出来：每台的进度、对象指针、坐标，
//   外加"已破译/在修/未动"的计数和 last_generator 的判定结果。
//   用法：**拍照前 / 拍照中各跑一次**，对比两次输出就能看出：
//     · 台数 (n) 有没有翻倍            -> 镜像副本是否混进来了
//     · 已破译计数 done 有没有被误算     -> "最后一台"误显示的根因
//     · 同一个 scene_obj 是否出现两次
static void dump_gen_dump()
{
    reset_buf();
    wline("===== 密码机列表（PyProgress 快照）=====");
    wline("available=%d", PyProgress::available() ? 1 : 0);

    PyProgress::GenListEntry e[16];
    const int n = PyProgress::get_list(e, 16);
    wline("台数 n=%d", n);
    wline("");
    int done = 0, doing = 0, zero = 0;
    for (int i = 0; i < n; i++) {
        if (e[i].progress >= 99.95f)      done++;
        else if (e[i].progress > 0.05f)   doing++;
        else                              zero++;
        wline("  [%2d] 进度=%6.2f%%  obj=%llx  pos=(%.1f, %.1f)",
              i, e[i].progress, (unsigned long long)e[i].scene_obj, e[i].x, e[i].y);
    }
    wline("");
    wline("已破译=%d  在修=%d  未动=%d", done, doing, zero);

    float lp = 0.0f;
    const bool show = PyProgress::last_generator(lp);
    wline("last_generator 判定: %s%s", show ? "显示" : "不显示",
          show ? "" : "  (done<4 或 有进度的机器不足)");
    if (show) wline("  最后一台进度 = %.2f%%", lp);
    wline("");
    wline("排查提示：拍照前后各跑一次，对比「台数」和「已破译」有没有异常变化");
    snprintf((char*)g_status, 128, "gen_dump 完成");
}

// ---- Dump 21m: units_by_type[键] 实例字段探测 ------------------------------
// 用法: fn=item_probe  arg=键号（默认 209）
//
// 背景（2026-10-11 实测结论）：
//   小丑的地面拾取物（推进器/风翼/钻头）**不在 read_thread 遍历的 curArray 里** ——
//   用户站在零件旁实测 data[] 只有 camp 1/2/3，四个记录器也都没抓到它。
//   最后靠 `fn=unit_types` 全键扫描 + **用户捡起两个零件** 定位到：
//     units_by_type[209] 的数量 31 -> 29，正好少 2 个。**键 209 就是零件**。
//
//   这个 dump 用来搞清楚 209 的每个实例有哪些字段 —— 特别是**坐标字段叫什么**。
//   密码机（键 3）用的是 "position"，零件不一定同名，所以先把键名+数值列出来看。
static void dump_item_probe(uint64_t type_key)
{
    if (type_key == 0) type_key = 209;
    reset_buf();
    wline("===== units_by_type[%llu] 实例字段探测 =====", (unsigned long long)type_key);

    const char *why = "";
    uint64_t gmd = PyCore::game_kernel_dict(why);
    if (!gmd) { wline("game_kernel 读不到: %s", why); return; }

    const int64_t i_um = PyCore::find_key(gmd, "unit_mgr", -1);
    const uint64_t um = (i_um >= 0) ? PyCore::get_attr(gmd, i_um) : 0;
    if (!PyCore::is_obj(um)) { wline("unit_mgr 无效（不在对局中？）"); return; }

    const uint64_t ud = getPtr64(um - 0x18);
    const int64_t i_ubt = PyCore::find_key(ud, "units_by_type", -1);
    const uint64_t ubt = (i_ubt >= 0) ? PyCore::get_attr(ud, i_ubt) : 0;
    if (!PyCore::is_obj(ubt)) { wline("units_by_type 无效"); return; }

    const uint64_t lst = PyCore::get_type_list(ubt, (int)type_key);
    if (!PyCore::is_obj(lst)) { wline("该键没有对应列表"); return; }

    int64_t n = 0;
    vm_readv(lst + 0x10, &n, 8);            // ob_size
    const uint64_t items = getPtr64(lst + 0x18);
    wline("数量 = %lld", (long long)n);
    if (n <= 0 || !PyCore::is_obj(items)) { wline("列表为空"); return; }
    wline("");

    // 只探前 3 个实例 —— 同类对象字段名一样，看一个就够，多看几个是防坏数据
    // ★ 从 3 提到 8（2026-10-12）：查相中世界判据时发现
    //   **本人不在这前 3 个实例里** —— 三个实例的 is_self 全是 0。
    //   探测数量太少就永远看不到自己，而 in_photo_world 很可能
    //   只对本人有效（5 次采样都因此读了个寂寞）。
    //   代价是输出变大（8 × 1304 行 ≈ 1 万行），MCP 往返慢一点，可以接受。
    const int probe = (n < 8) ? (int)n : 8;
    for (int i = 0; i < probe; i++) {
        const uint64_t inst = getPtr64(items + (uint64_t)i * 8);
        if (!PyCore::is_obj(inst)) continue;
        const uint64_t d = getPtr64(inst - 0x18);       // managed dict 预头
        PyCore::Dict dk;
        if (!PyCore::read_dict(d, dk)) { wline("--- 实例 %d: 读不到 dict ---", i); continue; }

        wline("--- 实例 %d  addr=%llx  dict=%llx  字段数 %lld ---",
              i, (unsigned long long)inst, (unsigned long long)d, (long long)dk.n_entries);
        // ⚠ 如果字段名是乱码 / 字段数大得离谱（比如 214 个），
        //   说明**这类对象不是 managed-dict 结构**（`inst-0x18` 那套只对密码机有效）。
        //   这时用上面打出来的 addr 去跑 `fn=typeinfo`（看 tp_basicsize/__slots__）
        //   或 `fn=expand`（万能展开），比在这里猜偏移靠谱。
        for (int64_t k = 0; k < dk.n_entries; k++) {
            const uint64_t kp = getPtr64(PyCore::key_slot(dk, k));
            char nm[80] = {0};
            // ⚠ 必须用 read_str，**不能用 read_str_any** ——
            //   实测 read_str_any 读出来的名字是 UTF-16 字节交错的
            //   （"org_submesh_visible_dict" 变成 "ro_gusmbse_hivisilb_eidtc"），
            //   因为游戏里的属性名是 UCS-2 存储的，两个字节一个字符。
            //   read_str 会正确处理这种情况 —— typeinfo 就是用它的，名字一直是好的。
            if (PyCore::is_obj(kp)) PyCore::read_str(kp, nm, sizeof(nm) - 1);

            // 值：能当数字读就把数值一起打出来（坐标一眼就能认出来）
            //   ⚠ read_number 的签名是 (slot, out, lo, hi) —— 四个参数，
            //     少了 lo/hi 会编译报 "too few arguments"（实测踩过）。
            //     这里范围放宽到 ±1e7，好把坐标（几百~几千）和进度（0~100）都包住。
            const uint64_t vp = getPtr64(PyCore::value_slot(dk, k));
            // ★ 同时打出"值对象指针 + 对象内 +0x18 的原始 8 字节"（2026-10-12 改）
            //   用途：约瑟夫相中世界的判据要靠**字段翻转**来找 ——
            //   只打数值的话，bool/None/对象引用这些变化**完全看不见** ✗。
            //   打原始字节后，把两次输出 diff 一下就能立刻定位是哪个字段变了 ✓。
            uint64_t raw18 = 0;
            if (PyCore::is_obj(vp)) vm_readv(vp + 0x18, &raw18, 8);

            float fv = 0.0f;
            const bool is_num = PyCore::is_obj(vp) && PyCore::read_number(vp, fv, -1.0e7, 1.0e7);
            if (is_num) wline("    %-28s num=%-14.5g val=%llx raw=%llx",
                              nm, fv, (unsigned long long)vp, (unsigned long long)raw18);
            else        wline("    %-28s num=%-14s val=%llx raw=%llx",
                              nm, "-", (unsigned long long)vp, (unsigned long long)raw18);
        }
        wline("");
    }
    wline("找坐标：上面那些值在几百~几千量级、且三个实例各不相同的，就是世界坐标。");
    snprintf((char*)g_status, 128, "item_probe 完成");
}

// ---- Dump 21i: 高频救人状态记录 -------------------------------------------
// 用法: fn=rescue_log  name=start | stop | status
//   挂在绘制线程上（60fps = 16ms 分辨率），把求生 state_machine._state 的
//   **每一次变化**带毫秒时间戳写进 /data/local/tmp/rescue_log.txt。
//   用途：量出「完整救人时长」和「松手时刻」的精确值 ——
//        出刀时机 = 完整时长 - 前摇 - 余量，必须精确到几十毫秒。
static void dump_rescue_log(const char *op, uint64_t arg, int64_t arg2)
{
    (void)arg; (void)arg2;
    reset_buf();
    if (op && !strcmp(op, "start"))      RescueLog::Start();
    else if (op && !strcmp(op, "stop"))  RescueLog::Stop();
    char b[200];
    wline("%s", RescueLog::Status(b, sizeof(b)));
    wline("");
    wline("用法: name=start 开始记录 / name=stop 停止 / 不带 name 看状态");
    wline("日志: /data/local/tmp/rescue_log.txt  格式: 毫秒 uid 状态栈");
    snprintf((char*)g_status, 128, "rescue_log 完成");
}
// ---- Dump 21g: 绘制逐门统计 ------------------------------------------------
// 用法: fn=draw_stats
//   绘制循环里每道 continue 都是一道门。只看画面分不清是哪道门吃的人，
//   这里把每一门的淘汰数打出来，哪门数字大就是它在吃人。
//   注：读的是上一帧发布的快照（在 draw_Gui 里每帧开头发布），差一帧不影响判断。
static void dump_draw_stats()
{
    reset_buf();
    const DrawStats &d = g_ds_pub;
    wline("===== 绘制逐门统计（上一帧）=====");
    wline("  实体总数              %d", d.total);
    wline("  --------------------------------");
    wline("  跳过 buzz/nvyao 类名  %d", d.skip_cls);
    wline("  跳过自身              %d", d.is_self);
    wline("  跳过 goldrush 队友    %d", d.goldrush);
    wline("  >> 判为局外(lobby)    %d", d.lobby);
    wline("  >> 簿子没有且非本体   %d", d.cc_skip);
    wline("  簿子没有但是本体      %d  (照画，无身份标注)", d.cc_nobook);
    wline("  >> 不是本体(body)     %d", d.body_skip);
    wline("  本体集合 命中/落空    %d / %d%s", d.body_hit, d.body_miss,
          (d.body_hit == 0 && d.body_miss >= 3) ? "   (集合与当前场景无交集)" : "");
    wline("  假面替身 认领/让位    %d / %d", d.masq, d.masq_dup);
    wline("  坐标认领 座位数       %d%s", d.pos_claim,
          d.pos_claim > 0 ? "   <<< 假面/换模型时按位置把人捡回来了" : "");
    wline("  认领诊断 无指针/零值/可用  %d / %d / %d", d.pc_noobj, d.pc_zero, d.pc_have);
    wline("  靠旧锚点认出 位置改用unit   %d%s", d.pos_live,
          d.pos_live > 0 ? "   <<< 假面中：这些框的位置来自活坐标" : "");
    wline("  坐标读失败(沿用旧位置)    %d", d.pos_fail);
    wline("  锚点缺失(>=3 判定假面)   %d", d.anchor_missing);
    wline("  位置改写 被挡/按号跳自己 %d / %d", d.pos_reject, d.skip_self_cc);
    wline("  重复框 让位(body_old)     %d", d.dup_old);
    wline("  坐标来自跟踪(当帧读失败)  %d", d.pos_track);
    wline("  按座位补框 / 无坐标      %d / %d", d.seatbox, d.seatbox_nopos);

    // ★ 性能剖析：PySelf 线程累计耗时/次数（读两次相减 = 这段时间它占了多少）
    //   ⚠ 注意：这里用的是局部 static，所以**同一行里的"增量"是相对上一次打印**。
    {
        static uint64_t p_ns = 0, p_n = 0;
        const uint64_t c_ns = PySelf::g_t_pyself_ns.load(std::memory_order_relaxed);
        const uint64_t c_n  = PySelf::g_n_pyself.load(std::memory_order_relaxed);
        wline("  [剖析] PySelf线程 累计 %.2f 秒 / %llu 次  (本次增量 %.1f 毫秒 / %llu 次)",
              c_ns / 1e9, (unsigned long long)c_n, (c_ns - p_ns) / 1e6,
              (unsigned long long)(c_n - p_n));
        p_ns = c_ns; p_n = c_n;
    }
    wline("  假面配对 精确/跟踪/跳过  %d / %d / %d%s", d.pair_exact, d.pair_loose, d.pair_skip,
          (d.pair_exact + d.pair_loose) > 0 ? "   <<< 位置按跟踪坐标配对(不是按对象地址)" : "");
    wline("  跳过 坐标xy=0         %d", d.xy0);
    wline("  跳过 地下 z<=-300     %d", d.dz);
    wline("  自身是监管->跳监管    %d", d.camp_self);
    wline("  超过 300 米           %d", d.dist);
    wline("  --------------------------------");
    wline("  实际绘制              %d", d.drawn);
    //   ★ 2026-10-13 新增：天赋绘制逐门（用户反馈"框上天赋不见了"）✓
    wline("  ---- 天赋绘制逐门 ----");
    wline("  进入天赋分支          %d", d.tn_enter);
    wline("  判为镜像->整块跳过    %d", d.tn_mirror);
    wline("  lookup 失败(无数据)   %d", d.tn_lookup);
    wline("  三段全空(没东西画)    %d", d.tn_empty);
    wline("  >> 天赋实际画出       %d", d.tn_drawn);
    wline("");
    wline("  状态: in_lobby=%d  book_ready=%d  body_ready=%d",
          (int)PySelf::in_lobby(),
          (int)PySelf::copycat_book_ready(), (int)PySelf::body_set_ready());
    wline("");
    if (d.lobby > 0 && d.drawn <= 1)
        wline("  !! 判成局外 -> 阵营 1/2 整类被跳。这就是 全部人不画 的原因。");
    else if (d.cc_nobook > 2 && d.drawn > 0)
        wline("  身份簿有 %d 个对不上 -> 已退回本体集合判断，照画(只是没身份标注)。",
              d.cc_nobook);
    else if (d.body_skip > 2)
        wline("  !! 本体集合吃掉 %d 个 -> body_set 不全（开会阶段常见）。", d.body_skip);
    else
        wline("  没有异常的门。");
    snprintf((char*)g_status, 128, "draw_stats 完成");
}

// ---- Dump 21f: 模仿者身份簿 ------------------------------------------------
// 用法: fn=copycat_book
//
// 排查「模仿者局里人突然不画了」的关键：
//   绘制侧是靠身份簿过滤的（不在簿子里就跳过），所以簿子里缺谁、谁的
//   body 地址过期了，直接决定了画面上少谁。
static void dump_copycat_book()
{
    reset_buf();
    PySelf::CopycatBook b = PySelf::copycat_book();

    wline("===== 模仿者身份簿 =====");
    wline("  copycat_book_ready = %s   (false 时绘制侧退回按本体集合过滤)",
          PySelf::copycat_book_ready() ? "true" : "false");
    wline("  self_seat          = %d", b.self_seat);
    wline("");
    wline("  号码  known  dead   identity  身份名          body 地址          灵魂体地址        不可见轮数");
    int known = 0, withBody = 0, dead = 0, withSoul = 0;
    for (int i = 0; i < 16; i++) {
        const auto &x = b.seat[i];
        if (x.known) known++;
        if (x.known && x.body) withBody++;
        if (x.dead) dead++;
        if (x.known && x.soul) withSoul++;
        const char *nm = x.identity ? PySelf::copycat_identity_name(x.identity) : nullptr;
        wline("  %2d    %-5s  %-5s  %-9d %-14s 0x%llX  0x%llX  %d",
              i + 1, x.known ? "是" : "否", x.dead ? "是" : "否",
              x.identity, nm ? nm : "-",
              (unsigned long long)x.body, (unsigned long long)x.soul, x.invis);
    }
    wline("");
    wline("  不可见轮数 = 本体模型 +0x70 连续读到 0 的轮数(**只是诊断**，不参与死亡判定：");
    wline("  活人被镜头剔掉时它一样会涨。死亡只认 [1089] 灵魂名单 + 锁存)。");
    wline("");
    wline("  合计: 已知 %d 格   有 body %d 格   已死 %d 格   有灵魂体 %d 格", known, withBody, dead, withSoul);
    wline("");
    wline("  判定：");
    if (!PySelf::copycat_book_ready())
        wline("    ⚠ 簿子为空 -> 绘制侧走本体集合过滤。开会阶段本体集合读不全时，人会不画。");
    else if (withBody < known)
        wline("    ⚠ 有 %d 格知道身份但没读到 body -> 那些人在**永远**不会被画出来。", known - withBody);
    else if (dead > withSoul)
        wline("    ⚠ 有 %d 个死者没记到灵魂体地址 -> 他们只会被藏起来，不会标成「N号 身份（已死）」。",
              dead - withSoul);
    else
        wline("    簿子看起来正常。");
    snprintf((char*)g_status, 128, "copycat_book 完成");
}

// ---- Dump 21g: 模仿者单位实况（[1088] / [1089] 逐条）------------------------
// 用法: fn=copycat_units
//
// 为什么单独做这一个：排查「死掉了还显示活着」「死者顶着角色名」时，唯一有用的
// 事实是 **引擎自己怎么表示死亡** —— [1088] 那一条和它在 [1089] 里的对应项各自
// 有哪些字段、值是多少。type_units 会把整个 __dict__(几百个键)打出来，一份就超
// 512KB 缓冲(而且正是它把进程撑崩过一次)，根本不适合反复取。
// 这里只按名字取固定的几个键，输出二十来行，随时可以连着采好几次做差分。
static void dump_copycat_units()
{
    reset_buf();
    const char *why = nullptr;
    uint64_t md = PyCore::game_kernel_dict(why);
    if (!md) { wline("game_kernel 不可用: %s", why ? why : "?"); return; }

    // unit_mgr.units_by_type（同 dump_all_units 的走法，不依赖 PySelf 的内部缓存）
    PyCore::Dict dk;
    if (!PyCore::read_dict(md, dk)) { wline("game_kernel dict 读取失败"); return; }
    uint64_t um = 0;
    for (int64_t i = 0; i < dk.n_entries; i++) {
        char kn[64];
        if (PyCore::read_str(getPtr64(PyCore::key_slot(dk, i)), kn, sizeof(kn)) && strcmp(kn, "unit_mgr") == 0) {
            um = getPtr64(PyCore::value_slot(dk, i));
            break;
        }
    }
    if (!um) { wline("找不到 unit_mgr"); return; }
    uint64_t ud = PyCore::get_inst_dict(um);
    PyCore::Dict udk;
    if (!ud || !PyCore::read_dict(ud, udk)) { wline("unit_mgr.__dict__ 读取失败"); return; }
    uint64_t ubt = 0;
    for (int64_t i = 0; i < udk.n_entries; i++) {
        char kn[64];
        if (PyCore::read_str(getPtr64(PyCore::key_slot(udk, i)), kn, sizeof(kn)) && strcmp(kn, "units_by_type") == 0) {
            ubt = getPtr64(PyCore::value_slot(udk, i));
            break;
        }
    }
    if (!ubt) { wline("找不到 units_by_type"); return; }

    wline("===== 模仿者单位实况 =====");
    wline("  book_ready=%d  copycat_active=%d  book_has_self=%d",
          PySelf::copycat_book_ready() ? 1 : 0, PySelf::copycat_active() ? 1 : 0,
          PySelf::copycat_book().self_seat);
    wline("");

    // 逐条打印一个 list 里的单位。want_soul=1 时额外打 unique_id / model(灵魂体)
    struct Row { int64_t idx; uint64_t unit, scene; int64_t uid, ident, seat; int isdead; char uq[32]; };
    static Row rows[64];

    const int64_t types[2] = { 1088, 1089 };
    const char *tnames[2] = { "[1088] CopycatPlayerUnit(活人+尸体)",
                              "[1089] GhostCopycatPlayerUnit(灵魂体)" };
    for (int t = 0; t < 2; t++) {
        uint64_t lst = PyCore::get_type_list(ubt, (int)types[t]);
        int64_t n = 0; uint64_t items = 0;
        if (!PyCore::read_list(lst, n, items)) {
            wline("--- %s  list 读取失败 ---", tnames[t]);
            wline("");
            continue;
        }
        wline("--- %s  数量=%lld ---", tnames[t], (long long)n);

        int cnt = 0;
        for (int64_t j = 0; j < n && cnt < 64; j++) {
            uint64_t unit = getPtr64(items + j * 8);
            if (!PyCore::is_obj(unit)) continue;
            uint64_t d = PyCore::get_inst_dict(unit);
            PyCore::Dict lik;
            if (!d || !PyCore::read_dict(d, lik)) continue;

            static int64_t i_uid = -1, i_ident = -1, i_seat = -1, i_uq = -1, i_model = -1;
            static int64_t i_isdead = -1, i_isdead2 = -1;
            static int64_t i_cu = -1, i_cud = -1;      // unit.combat_unit -> ._is_dead
            Row &r = rows[cnt];
            memset(&r, 0, sizeof(r));
            r.idx = j;
            r.unit = unit;
            r.uq[0] = 0;

            i_uid = PyCore::resolve_index(d, lik, i_uid, "uid");
            if (i_uid >= 0) PyCore::read_int(getPtr64(PyCore::value_slot(lik, i_uid)), r.uid);
            i_ident = PyCore::resolve_index(d, lik, i_ident, "identity_id");
            if (i_ident >= 0) PyCore::read_int(getPtr64(PyCore::value_slot(lik, i_ident)), r.ident);
            i_seat = PyCore::resolve_index(d, lik, i_seat, "copycat_index");
            if (i_seat >= 0) PyCore::read_int(getPtr64(PyCore::value_slot(lik, i_seat)), r.seat);
            i_uq = PyCore::resolve_index(d, lik, i_uq, "unique_id");
            if (i_uq >= 0) PyCore::read_str(getPtr64(PyCore::value_slot(lik, i_uq)), r.uq, sizeof(r.uq));
            i_model = PyCore::resolve_index(d, lik, i_model, "model");
            if (i_model >= 0) r.scene = PyCore::get_scene_obj(getPtr64(PyCore::value_slot(lik, i_model)));

            // _is_dead：三态打印，方便判断"到底有没有这个属性"：
            //   -  属性不存在
            //   0  存在且为 False
            //   1  存在且为 True  <- 这才是"他死了"的权威证据
            //   2  存在但既不是 True 也不是 False(值被换成了别的东西)
            //
            // ★ 注意读的是**哪个对象**：恒为 False 的是 700 键的玩家单位自己的 _is_dead；
            //   真正表示死活的在它挂着的 62 键 ECS 实体 unit.combat_unit 上。
            //   所以这里先走 unit -> combat_unit，再读 combat_unit 的 _is_dead。
            {
                uint64_t cu = 0;
                i_cu = PyCore::resolve_index(d, lik, i_cu, "combat_unit");
                if (i_cu >= 0) cu = getPtr64(PyCore::value_slot(lik, i_cu));
                uint64_t cud = cu ? PyCore::get_inst_dict(cu) : 0;
                PyCore::Dict cuk;
                bool got = false;
                if (cud && PyCore::read_dict(cud, cuk)) {
                    i_cud = PyCore::resolve_index(cud, cuk, i_cud, "_is_dead");
                    if (i_cud >= 0) {
                        uint64_t v = getPtr64(PyCore::value_slot(cuk, i_cud));
                        r.isdead = (v == PyRoot::g_true) ? 1 : ((v == PyRoot::g_false) ? 0 : 2);
                        got = true;
                    }
                }
                if (!got) {
                    // 退回玩家单位自己的，聊胜于无(实测恒 False)
                    i_isdead = PyCore::resolve_index(d, lik, i_isdead, "_is_dead");
                    if (i_isdead >= 0) {
                        uint64_t v = getPtr64(PyCore::value_slot(lik, i_isdead));
                        r.isdead = (v == PyRoot::g_true) ? 1 : ((v == PyRoot::g_false) ? 0 : 2);
                    }
                }
            }
            cnt++;
        }

        wline("  idx  unit 地址     号  身份  身份名        unique_id       model(scene)       v70        v1a0   _is_dead  书籍  [认领坐标]");
        const PySelf::CopycatBook bb = PySelf::copycat_book();   // 只取一次，逐条比对用
        for (int k = 0; k < cnt; k++) {
            const Row &r = rows[k];
            uint32_t v70 = 0; float v1a0 = 0.f;
            if (r.scene) { vm_readv(r.scene + 0x70, &v70, 4); vm_readv(r.scene + 0x1a0, &v1a0, 4); }
            // 这一条在身份簿里是什么状态(绘制侧就是按这个判的)
            const char *bk = "-";
            {
                int s = (int)r.seat + 1;
                if (r.seat >= 0 && s >= 1 && s <= 16) {
                    const auto &x = bb.seat[s - 1];
                    if (!x.known)              bk = "空";
                    else if (x.dead)           bk = (x.soul == r.scene && r.scene) ? "死者(灵魂体已对上)" : "死者";
                    else if (x.body == r.scene) bk = "活人(本体已对上)";
                    else                       bk = "活人(body不符)";
                }
            }
            const char *nm = r.ident ? PySelf::copycat_identity_name((int)r.ident) : nullptr;
            const char *dflag = (r.isdead == 1) ? "1(死)" : (r.isdead == 0 ? "0" : (r.isdead == 2 ? "?" : "-"));
            // ★ 坐标认领用的那个坐标。假面时绘制侧就靠它把被换掉模型的人认回来 ——
            //   这里打出来才能立刻看出"坐标到底有没有读到"，不用再靠 0/非 0 猜。
            char posb[48];
            {
                float p[3];
                if (PySelf::copycat_seat_pos((int)r.seat + 1, p))
                    snprintf(posb, sizeof(posb), "(%.0f,%.0f,%.0f)", p[0], p[1], p[2]);
                else
                    snprintf(posb, sizeof(posb), "无坐标");
            }
            wline("  %3lld  0x%-8llX %2lld  %-4lld  %-11s  %-13s  0x%-14llX  0x%08X %7.1f  %-8s  %-10s %s",
                  (long long)r.idx, (unsigned long long)r.unit,
                  (long long)(r.seat + 1), (long long)r.ident, nm ? nm : "-",
                  r.uq[0] ? r.uq : "-",
                  (unsigned long long)r.scene, v70, v1a0, dflag, bk, posb);
        }
        wline("");
    }

    wline("  说明：");
    wline("    · 死亡判据 = [1088] 的 unique_id 出现在 [1089] 灵魂名单里(死亡那一瞬间才有，");
    wline("      引擎随后会把整张 1089 删掉)，或者该号码在身份簿里已经锁存为死者。");
    wline("    · 身份簿会落盘到 " " /data/local/tmp/dwrg_ccbook.bin" " —— overlay 重启不再丢死者。");
    wline("    · v70/v1a0 是渲染可见性，**远处活人也是 0**，不能单独拿来判死亡。");
    snprintf((char*)g_status, 128, "copycat_units 完成");
}

// ---- Dump 21h: 模仿者每个人的技能冷却 --------------------------------------
// 用法: fn=copycat_skills
//
// 目的：用户要"把每个人的技能 CD 画到他框右边"。冷却本身不用重新逆向 ——
// PyGenius::collect_all_skill_cd 已经把 unit.skill_mgr.skill_dict 那条链走通了
// (监管侧在用)。缺的只有一样东西：**模仿者这些身份的技能 id 分别是什么**，
// 现有代码里只有监管辅助特质的表(701/711/741…)，没有模仿者的。
// 所以这个 dump 只做一件事：把每个座位的 skill_dict 原样打出来，顺便把 unit 自己
// 名字里带 "cd" 的字段也列一遍(刀人冷却可能就挂在 unit 上而不是 skill_dict 里)。
static void dump_copycat_skills()
{
    reset_buf();
    const char *why = nullptr;
    uint64_t md = PyCore::game_kernel_dict(why);
    if (!md) { wline("game_kernel 不可用: %s", why ? why : "?"); return; }

    PyCore::Dict dk;
    if (!PyCore::read_dict(md, dk)) { wline("game_kernel dict 读取失败"); return; }
    uint64_t um = 0;
    for (int64_t i = 0; i < dk.n_entries; i++) {
        char kn[64];
        if (PyCore::read_str(getPtr64(PyCore::key_slot(dk, i)), kn, sizeof(kn)) && strcmp(kn, "unit_mgr") == 0) {
            um = getPtr64(PyCore::value_slot(dk, i));
            break;
        }
    }
    if (!um) { wline("找不到 unit_mgr"); return; }
    uint64_t ud = PyCore::get_inst_dict(um);
    PyCore::Dict udk;
    if (!ud || !PyCore::read_dict(ud, udk)) { wline("unit_mgr.__dict__ 读取失败"); return; }
    uint64_t ubt = 0;
    for (int64_t i = 0; i < udk.n_entries; i++) {
        char kn[64];
        if (PyCore::read_str(getPtr64(PyCore::key_slot(udk, i)), kn, sizeof(kn)) && strcmp(kn, "units_by_type") == 0) {
            ubt = getPtr64(PyCore::value_slot(udk, i));
            break;
        }
    }
    if (!ubt) { wline("找不到 units_by_type"); return; }

    int64_t n = 0; uint64_t items = 0;
    if (!PyCore::read_list(PyCore::get_type_list(ubt, 1088), n, items)) {
        wline("[1088] CopycatPlayerUnit 列表读取失败 —— 现在不在模仿者局里？");
        return;
    }
    wline("===== 模仿者技能冷却取样 ([1088] 共 %lld 人) =====", (long long)n);
    wline("");

    static int64_t h_sm = -1, h_sd = -1;
    int shown = 0;
    for (int64_t i = 0; i < n && shown < 12; i++) {
        uint64_t unit = getPtr64(items + i * 8);
        if (!PyCore::is_obj(unit)) continue;
        uint64_t d = PyCore::get_inst_dict(unit);
        PyCore::Dict lik;
        if (!d || !PyCore::read_dict(d, lik)) continue;

        static int64_t i_seat = -1, i_ident = -1;
        int64_t seat = -1, ident = 0;
        i_seat = PyCore::resolve_index(d, lik, i_seat, "copycat_index");
        if (i_seat >= 0) PyCore::read_int(getPtr64(PyCore::value_slot(lik, i_seat)), seat);
        i_ident = PyCore::resolve_index(d, lik, i_ident, "identity_id");
        if (i_ident >= 0) PyCore::read_int(getPtr64(PyCore::value_slot(lik, i_ident)), ident);

        const char *nm = ident ? PySelf::copycat_identity_name((int)ident) : nullptr;
        wline("--- %lld号  身份 %lld %s   unit 0x%llX ---",
              (long long)(seat + 1), (long long)ident, nm ? nm : "?",
              (unsigned long long)unit);

        // ① skill_mgr.skill_dict 里的每个技能
        uint64_t sm = PyGenius::get_unit_field(unit, "skill_mgr", h_sm);
        uint64_t sd = sm ? PyGenius::get_unit_field(sm, "skill_dict", h_sd) : 0;
        PyCore::Dict sdk;
        if (sd && PyCore::read_dict(sd, sdk)) {
            wline("    skill_dict 共 %lld 条：", (long long)sdk.n_entries);
            for (int64_t k = 0; k < sdk.n_entries; k++) {
                int64_t sid = 0;
                if (!PyCore::read_int(getPtr64(PyCore::key_slot(sdk, k)), sid)) continue;
                float remain = 0.f, total = 0.f; int cmax = 0, ccur = 0;
                bool ok = PyGenius::read_skill_cd(getPtr64(PyCore::value_slot(sdk, k)),
                                                  remain, total, cmax, ccur);
                wline("      id=%-8lld cd_delta=%-8s cd_time=%-8s power=%d/%d",
                      (long long)sid,
                      ok ? "读到了" : "读不到", "-", cmax, ccur);
                if (ok) {
                    // 覆盖掉上面那行里占位的 "-"：直接补一行可读数值
                    wline("          -> 剩余 %.2f 秒 / 总 %.2f 秒%s",
                          remain, total, remain <= 0.05f ? "  (就绪)" : "");
                }
            }
        } else {
            wline("    skill_dict 读不到(没有 skill_mgr，或这一轮内存读失败)");
        }

        // ② unit 自己名字里带 "cd" 的字段 —— 刀人冷却很可能挂在这里而不是 skill_dict
        wline("    unit 上名字带 cd 的字段：");
        int hits = 0;
        for (int64_t k = 0; k < lik.n_entries && hits < 12; k++) {
            char kn[64];
            if (!PyCore::read_str(getPtr64(PyCore::key_slot(lik, k)), kn, sizeof(kn))) continue;
            char low[64];
            int j = 0;
            for (; kn[j] && j < 63; j++) low[j] = (char)tolower((unsigned char)kn[j]);
            low[j] = 0;
            if (!strstr(low, "cd")) continue;
            w("      %-28s = ", kn);
            dump_value(getPtr64(PyCore::value_slot(lik, k)), 0);
            w("\n");
            hits++;
        }
        if (!hits) wline("      (没有)");
        wline("");
        shown++;
    }
    //   ★ 2026-10-13：发布闸门抓到这句明文里含 "draw_Gui"（内部文件名）✗
    //     去掉文件名，并走 GUARD_STR —— **零明文锚点**是发布要求 ✓
    wline(GUARD_STR("  用法：拿这份对照游戏里每个身份的技能，把 id -> 名字填进技能名表。"));
    snprintf((char*)g_status, 128, "copycat_skills 完成");
}


// ---- Dump 22: 在所有已加载模块里按名字找 ---------------
// find_path 是从 game_kernel 出发的 DFS，够不到所有模块；
// 但 sys.modules 我们是能完整读的。所以想找一个类（MiniMapBigVer 之类）
// 直接遍历所有模块的 __dict__ 最靠谱。
static void dump_find_in_modules(const char *want)
{
    reset_buf();
    if (!want || !*want) { wline("需要名字（用 name= 传）"); return; }

    bool substr = (want[0] == '*');
    char needle[128] = {0};
    if (substr) {
        snprintf(needle, sizeof(needle), "%s", want + 1);
        size_t n = strlen(needle);
        if (n && needle[n - 1] == '*') needle[n - 1] = 0;
    } else {
        snprintf(needle, sizeof(needle), "%s", want);
    }
    for (size_t i = 0; needle[i]; i++)
        if (needle[i] >= 'A' && needle[i] <= 'Z') needle[i] = (char)(needle[i] - 'A' + 'a');

    uint64_t sm_slot = PyRoot::g_modules_slot;
    uint64_t sm = sm_slot ? getPtr64(sm_slot) : 0;
    if (!is_obj(sm)) { wline("sys.modules 取不到"); return; }
    PyCore::Dict mk;
    if (!PyCore::read_dict(sm, mk)) { wline("sys.modules 不是 dict"); return; }

    wline("===== 在 %lld 个模块里找 %s\"%s\" =====",
          (long long)mk.n_entries, substr ? "(子串) " : "", want);
    wline("");

    int hits = 0, scanned = 0;
    for (int64_t i = 0; i < mk.n_entries && hits < 80; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(mk, i));
        char modname[160] = {0};
        if (!PyCore::read_str(kp, modname, sizeof(modname))) continue;
        uint64_t mod = getPtr64(PyCore::value_slot(mk, i)) & 0x00FFFFFFFFFFFFFFULL;
        uint64_t d = attr_dict_of(mod);
        if (!is_obj(d)) continue;

        PyCore::Dict dk;
        if (!PyCore::read_dict(d, dk)) continue;
        scanned++;

        for (int64_t e = 0; e < dk.n_entries; e++) {
            uint64_t ekp = getPtr64(PyCore::key_slot(dk, e));
            char kn[128] = {0};
            if (!PyCore::read_str(ekp, kn, sizeof(kn))) continue;

            bool hit;
            if (substr) {
                char low[128] = {0};
                snprintf(low, sizeof(low), "%s", kn);
                for (size_t q = 0; low[q]; q++)
                    if (low[q] >= 'A' && low[q] <= 'Z') low[q] = (char)(low[q] - 'A' + 'a');
                hit = (strstr(low, needle) != nullptr);
            } else {
                hit = (strcmp(kn, needle) == 0);
            }
            if (!hit) continue;

            uint64_t vp = getPtr64(PyCore::value_slot(dk, e)) & 0x00FFFFFFFFFFFFFFULL;
            char vd[200];
            describe_obj(vp, vd, sizeof(vd));
            wline("★ %s.%s", modname, kn);
            wline("     = 0x%llX  %s", (unsigned long long)vp, vd);
            if (++hits >= 80) break;
        }
    }
    wline("");
    wline("扫了 %d 个模块（sys.modules 共 %lld），命中 %d 处",
          scanned, (long long)mk.n_entries, hits);
    if (!hits) wline("(没有 —— 说明这个类/名字没有被任何已加载模块导出)");
    snprintf((char*)g_status, 128, "find_in_modules 完成");
}

// ---- 把任意值格式化成**一行**文本（容器递归展开）-------------------------
// 为什么需要：dump_expand 递归时是「另起一个块」输出，
// 嵌套元组会与外层元素交错，人看还行，**没法解析**
// （room_data = [(room_id,(x,y,z),rot), ...] 就是这种）。
// 这里改成把嵌套容器压成一行： (10001, (16, 1, 8), 0)
static void fmt_value(uint64_t vp, char *out, size_t cap, int depth)
{
    if (cap < 8) return;
    out[0] = 0;
    if (!is_obj(vp)) { snprintf(out, cap, "?"); return; }

    char tn[64] = {0};
    type_name_of(vp, tn, sizeof(tn));

    if (strcmp(tn, "bool") == 0) { snprintf(out, cap, "%s", vp == 0x71D62A9420ULL ? "True" : "False"); return; }
    int64_t iv = 0;
    if (strcmp(tn, "int") == 0 && PyCore::read_int(vp, iv)) { snprintf(out, cap, "%lld", (long long)iv); return; }
    if (strcmp(tn, "float") == 0) {
        double d = 0;
        if (vm_readv(vp + 16, &d, 8)) { snprintf(out, cap, "%g", d); return; }
    }
    char s[160] = {0};
    if (strcmp(tn, "str") == 0) {
        if (!PyCore::read_str(vp, s, sizeof(s))) PyCore::read_str_any(vp, s, sizeof(s));
        snprintf(out, cap, "\"%s\"", s);
        return;
    }

    const bool is_tup  = (strcmp(tn, "tuple") == 0);
    const bool is_list = (strcmp(tn, "list") == 0);
    if (is_tup || is_list) {
        if (depth <= 0) { snprintf(out, cap, "(...)"); return; }
        int64_t n = 0;
        uint64_t items = 0;
        if (is_tup) {
            if (!vm_readv(vp + 16, &n, 8)) { snprintf(out, cap, "(?)"); return; }
            items = vp + 24;
        } else if (!PyCore::read_list(vp, n, items)) { snprintf(out, cap, "[?]"); return; }
        if (n < 0 || n > 512) { snprintf(out, cap, "(bad n=%lld)", (long long)n); return; }

        size_t o = 0;
        out[o++] = is_tup ? '(' : '[';
        out[o] = 0;
        for (int64_t i = 0; i < n && o + 4 < cap; i++) {
            uint64_t ep = getPtr64(items + (uint64_t)i * 8) & 0x00FFFFFFFFFFFFFFULL;
            char one[220] = {0};
            fmt_value(ep, one, sizeof(one), depth - 1);
            int w = snprintf(out + o, cap - o, "%s%s", i ? ", " : "", one);
            if (w < 0) break;
            o += (size_t)w;
        }
        if (o + 2 < cap) { out[o++] = is_tup ? ')' : ']'; out[o] = 0; }
        return;
    }

    if (strstr(tn, "taggeddict") != nullptr) {
        int64_t cnt = 0; vm_readv(vp + 24, &cnt, 8);
        snprintf(out, cap, "{taggeddict %lld 对}", (long long)cnt);
        return;
    }
    if (strcmp(tn, "dict") == 0) {
        PyCore::Dict d;
        if (PyCore::read_dict(vp, d)) { snprintf(out, cap, "{dict %lld 条}", (long long)d.n_entries); return; }
    }
    // 现在
// 改成
snprintf(out, cap, "<%s @ 0x%llx>", tn, (unsigned long long)vp);
}

// ---- Dump 23: 万能展开 --------------------------------------------------
// 按地址把任意对象摊开：
//   tuple/list -> 逐元素（int 顺便把数值打出来）
//   dict       -> 走 dump_dict_raw
//   普通对象   -> 属性字典；再不行就打内存
// 需要它是因为 pydata.goldrush_minimap_configs.data 的键是 tuple、值是 taggeddict，
// 现有的 dict_raw / memdump 都看不进去。
static void dump_expand(uint64_t addr, int64_t depth)
{
    // ⚠ 必须用 maybe_reset 而不是 reset_buf：
    //   值是容器时本函数会递归，reset_buf 会把外层已经写好的
    //   「头部 + 键」全部冲掉 —— 表现就是输出直接从**值的**头部开始，
    //   看起来像"传进去的地址被改了"。这个坑排查了很久。
    maybe_reset();
    if (!is_obj(addr)) { wline("0x%llX 不是合法对象", (unsigned long long)addr); return; }
    if (depth <= 0 || depth > 4) depth = 3;

    char tn[64] = {0};
    type_name_of(addr, tn, sizeof(tn));
    wline("===== 展开 0x%llX  类型=\"%s\" =====", (unsigned long long)addr, tn);
    wline("");

    // taggeddict：{数组指针 @+16, 对数 @+24}，数组是 [键, 值, 键, 值, ...] 扁平表
    // 实测 pydata.goldrush_minimap_configs.data 的 92 个值全是它。
    // 它不是 dict 子类（read_dict 读不了），只能按这个布局自己解。
    //
    // ⚠ 数组里的指针**低位带 tag**：实测形如 0x73365ba8e6 / 0x73366094e1 / 0x73168796b1，
    //   直接按这个地址读会得到"错位几字节"的垃圾头（之前一直以为是数组步长不对）。
    //   所以这里做一次探测：直接读不出合法对象头就把低 3 位抹掉重试。
    if (strstr(tn, "taggeddict") != nullptr) {
        auto fix_ptr = [](uint64_t p) -> uint64_t {
            char t[64] = {0};
            if (is_obj(p) && type_name_of(p, t, sizeof(t)) && t[0]) return p;
            uint64_t q = p & ~(uint64_t)0x7;
            char t2[64] = {0};
            if (is_obj(q) && type_name_of(q, t2, sizeof(t2)) && t2[0]) return q;
            return p;
        };

        uint64_t arr = getPtr64(addr + 16) & 0x00FFFFFFFFFFFFFFULL;
        int64_t  cnt = 0;
        vm_readv(addr + 24, &cnt, 8);
        wline("taggeddict: %lld 对，键值数组 @0x%llX", (long long)cnt,
              (unsigned long long)arr);
        wline("");
        if (!is_obj(arr) || cnt < 0 || cnt > 512) { wline("数组指针/数量异常"); return; }
        for (int64_t i = 0; i < cnt; i++) {
            uint64_t kp = fix_ptr(getPtr64(arr + (uint64_t)i * 16)     & 0x00FFFFFFFFFFFFFFULL);
            uint64_t vp = fix_ptr(getPtr64(arr + (uint64_t)i * 16 + 8) & 0x00FFFFFFFFFFFFFFULL);
            if (!kp && !vp) continue;                 // 空槽
            char kd[200], vd[220];
            describe_obj(kp, kd, sizeof(kd));
            describe_obj(vp, vd, sizeof(vd));
            wline("  %-34s = %s", kd, vd);
            // 值本身是容器就再展开一层
            if (depth > 1 && is_obj(vp)) {
                char vtn[64] = {0};
                type_name_of(vp, vtn, sizeof(vtn));
                if (strcmp(vtn, "tuple") == 0 || strcmp(vtn, "list") == 0 ||
                    strcmp(vtn, "dict") == 0 || strstr(vtn, "taggeddict")) {
                    g_append_mode = true;
                    dump_expand(vp, depth - 1);
                    g_append_mode = false;
                }
            }
        }
        return;
    }

    // tuple / list
    if (strcmp(tn, "tuple") == 0 || strcmp(tn, "list") == 0) {
        int64_t n = 0;
        uint64_t items = 0;
        // ⚠ tuple 的元素**内联**在对象里（+24 起），而 list 在 +24 放的是
        //   指向元素数组的指针 —— 两者不能用同一个读法。
        //   之前用 read_list 读 tuple 会把 ob_size(2) 当成地址，元素全读错。
        if (strcmp(tn, "tuple") == 0) {
            if (!vm_readv(addr + 16, &n, 8)) { wline("读不出 tuple 长度"); return; }
            items = addr + 24;
        } else {
            if (!PyCore::read_list(addr, n, items)) { wline("读不出 list 元素"); return; }
        }
        if (n < 0 || n > 4096) { wline("元素个数异常: %lld", (long long)n); return; }
        wline("共 %lld 个元素：", (long long)n);
        for (int64_t i = 0; i < n && i < 512; i++) {
            uint64_t ep = getPtr64(items + (uint64_t)i * 8) & 0x00FFFFFFFFFFFFFFULL;
            // 容器直接**一行压平**输出，便于解析（见 fmt_value 的说明）
            char flat[600] = {0};
            fmt_value(ep, flat, sizeof(flat), 3);
            wline("  [%3lld] %s", (long long)i, flat);
            if (depth <= 1) continue;
        }
        return;
    }

    // dict
    if (strcmp(tn, "dict") == 0) {
        g_append_mode = true;
        dump_dict_raw(addr);
        g_append_mode = false;
        return;
    }

    // 普通对象：先属性字典，再不行打内存
    uint64_t d = attr_dict_of(addr);
    if (is_obj(d)) {
        wline("改用它自己的属性字典 @ 0x%llX：", (unsigned long long)d);
        wline("");
        g_append_mode = true;
        dump_dict_raw(d);
        g_append_mode = false;
        return;
    }
    wline("没有属性字典，打原始内存：");
    g_append_mode = true;
    dump_memdump(addr, 256);
    g_append_mode = false;
}

// ---- Dump 24: 按类型找实例 ----------------------------------------------
// 思路：对象的 +8 是它的类型指针。想找某个类的所有实例，就在堆里搜
// 「等于该类型地址的 8 字节值」—— 命中的位置就是实例对象头 +8。
//
// 用途：find_mod 能拿到 ui.UIBattleGoldRushPart...MiniMap 这个**类**，
// 但拿不到**实例**（实例在 UI 树里，find_path 的 DFS 够不着）。
// 有了 find_inst 就能直接把实例揪出来，读它的 voxel_imgs（游戏自己的小地图摆放）。
//
// 用法: fn=find_inst  arg=类型地址  name="起始:长度"（十六进制，不带 0x）
static void dump_find_inst(uint64_t type_addr, const char *range)
{
    reset_buf();
    if (!is_obj(type_addr)) { wline("arg 需要类型对象的地址"); return; }
    if (!range || !*range) { wline("name 需要 \"起始:长度\"（十六进制）"); return; }

    uint64_t start = 0, len = 0;
    const char *colon = strchr(range, ':');
    if (!colon) { wline("name 格式应为 起始:长度"); return; }
    start = strtoull(range, nullptr, 16);
    len   = strtoull(colon + 1, nullptr, 16);
    if (!start || !len) { wline("起始/长度解析失败"); return; }
    const uint64_t kMaxScan = 1024ULL * 1024 * 1024;
    if (len > kMaxScan) len = kMaxScan;

    char tn[96] = {0};
    type_name_of(type_addr, tn, sizeof(tn));
    wline("===== 找 \"%s\" (type @0x%llX) 的实例 =====", tn,
          (unsigned long long)type_addr);
    wline("扫描 0x%llX .. 0x%llX  (%llu MB)", (unsigned long long)start,
          (unsigned long long)(start + len), (unsigned long long)(len / 1048576));
    wline("");

    // Android 的 TBI：指针高字节带 tag，两种形态都认
    const uint64_t want[2] = {
        type_addr & 0x00FFFFFFFFFFFFFFULL,
        (type_addr & 0x00FFFFFFFFFFFFFFULL) | 0xB400000000000000ULL
    };

    const size_t kChunk = 64 * 1024;
    static uint8_t buf[64 * 1024];
    int hits = 0;
    uint64_t off = 0;
    while (off < len && hits < 32) {
        size_t want_n = (size_t)((len - off) < kChunk ? (len - off) : kChunk);
        if (!vm_readv(start + off, buf, want_n)) { off += kChunk; continue; }
        for (size_t i = 0; i + 8 <= want_n; i += 8) {
            uint64_t v = 0;
            memcpy(&v, buf + i, 8);
            if (v != want[0] && v != want[1]) continue;
            uint64_t obj = start + off + i - 8;      // +8 是类型指针，对象头在 -8
            wline("★ 实例 @0x%llX", (unsigned long long)obj);
            if (++hits >= 32) break;
        }
        off += want_n;
    }
    wline("");
    wline("命中 %d 个实例", hits);
    if (!hits) wline("(说明这个类当前没被实例化 —— 比如那个 UI 还没创建)");
    snprintf((char*)g_status, 128, "find_inst 完成");
}

// ---- 保存当前 dump 结果到文件 ----
// 写 g_buf 全部内容到 path。成功/失败都写进 g_status，面板直接显示。
// fallback 是备路：主路径打不开(SELinux/权限)就改用 fallback，状态里会写明实际落到哪。
// 返回 true=完整写入。
static bool save_buf(const char *path, const char *fallback = nullptr)
{
    if (!path || !path[0])          { snprintf((char*)g_status, 128, "保存失败: 路径为空"); return false; }
    if (g_buf_len <= 0)             { snprintf((char*)g_status, 128, "保存失败: 当前没有 dump 内容"); return false; }

    FILE *f = fopen(path, "w");
    const char *used = path;
    if (!f && fallback && fallback[0]) { f = fopen(fallback, "w"); if (f) used = fallback; }
    if (!f) {
        snprintf((char*)g_status, 128, "保存失败: 打不开 %s%s%s (权限/SELinux?)", path,
                 (fallback && fallback[0]) ? " / " : "", fallback ? fallback : "");
        printf(GUARD_STR("[PyDump] fopen 失败: %s%s%s\n"), path,
               (fallback && fallback[0]) ? " / " : "", fallback ? fallback : "");
        fflush(stdout);
        return false;
    }
    // ★ 第二道防线(见 w()/vw() 的注释)：g_buf_len 现在恒 <= g_buf_cap-1，
    //   这里再显式夹一次 —— 就算将来有人又改坏了写入端的账目，
    //   fwrite 也绝不会越过 g_buf 的末尾被 FORTIFY 打死。
    size_t want = (size_t)g_buf_len;
    if (want > (size_t)g_buf_cap) want = (size_t)g_buf_cap;
    size_t n = fwrite(g_buf, 1, want, f);
    fclose(f);
    if (n != want) {
        snprintf((char*)g_status, 128, "保存不完整: %zu/%zu 字节 -> %s", n, want, used);
        return false;
    }
    if (want != (size_t)g_buf_len) {
        // 缓冲写满过 -> 文件是被截断的。必须说出来，否则又是一份"看着正常"的半个 dump。
        snprintf((char*)g_status, 128, "已保存但**被截断** %zu/%d 字节 -> %s (单次 dump 超过 %d 字节上限)",
                 want, g_buf_len, used, g_buf_cap);
        printf(GUARD_STR("[PyDump] 已保存(截断) %zu/%d 字节 -> %s\n"), want, g_buf_len, used);
        fflush(stdout);
        return false;
    }
    if (g_buf_len >= g_buf_cap - 1) {
        // 恰好写满：内容可能正好完整，也可能差最后一点，如实标注
        snprintf((char*)g_status, 128, "已保存 %d 字节 -> %s (**缓冲已满，可能被截断**)", g_buf_len, used);
        printf(GUARD_STR("[PyDump] 已保存 %d 字节(缓冲已满) -> %s\n"), g_buf_len, used);
        fflush(stdout);
        return true;
    }
    printf(GUARD_STR("[PyDump] 已保存 %d 字节 -> %s\n"), g_buf_len, used);
    fflush(stdout);
    return true;
}

// ---- 供 ImGui 调用的接口 ----
static const char *buf()          { return g_buf; }
static int  buf_len()             { return g_buf_len; }
static void set_filter(const char *f) {
    if (!f) f = "";
    snprintf(g_filter, sizeof(g_filter), "%s", f);
}
static void reset_status(const char *msg) {
    if (msg) snprintf(g_status, sizeof(g_status), "%s", msg);
}
static const char *filter()       { return g_filter; }
static const char *status()       { return g_status; }

} // namespace PyDump
