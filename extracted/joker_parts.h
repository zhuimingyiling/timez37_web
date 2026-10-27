#pragma once
// ============================================================================
// joker_parts.h —— 小丑的地面零件（推进器 / 风翼 / 钻头）绘制
//
// ── 定位过程（2026-10-11，全部实测，不是猜的）──────────────────────────────
//   这三样东西**不在 read_thread 遍历的 curArray 里** —— 用户站在零件旁边实测
//   data[] 只有 camp 1/2/3，四个记录器（near_class / lost_entry / lost_scene /
//   lost_prop）也全都抓不到它。所以"直接读内存"这条路根本走不通。
//
//   改用 CPython 对象图（密码机当初也是这么解决的）：
//     1. fn=unit_types  全键扫描 -> 21 个键
//     2. 用户捡起 2 个零件 -> 键 209 的数量 31 -> 29，**正好少 2 个**
//     3. fn=item_probe 209 -> 类型名 "SawComponentUnit"（小丑锯子的组件）
//     4. 字段表里找到坐标字段 _pos_vec2（math3d.vector2）
//
// ── 数据来源 ──────────────────────────────────────────────────────────────
//   game_kernel -> unit_mgr -> units_by_type[209] -> 每个实例的 managed dict
//     · _pos_vec2    : math3d.vector2，+0x10 = x，+0x14 = y（水平世界坐标）
//     · _creature_id : 子类型（推进器/风翼/钻头各一个 id）
//
// ── 绘制要求（用户明确说明）────────────────────────────────────────────────
//   **只画名字和距离，不画方框** —— 零件满地都是，画框会糊成一片。
// ============================================================================

#include "PyCore.h"

namespace JokerParts {

// ---- 参数（绘制页「小丑零件」折叠块里调）--------------------------------
inline bool  g_show        = false;      // 总开关
inline float g_range_m     = 120.0f;     // 绘制距离限制：50 ~ 200 米
inline bool  g_show_name   = true;       // 画名字
inline bool  g_show_dist   = true;       // 画距离
inline float g_font_size   = 22.0f;      // ★ 名字/距离的字号（可调，8~48）

// 三种道具的颜色 —— 因为零件**只画文字不画方框**，所以这个颜色就是
// "名字和距离"那几个字的颜色。按 _creature_id 区分，用户可调。
inline ImU32 g_col[3] = {
    IM_COL32(255, 210,  60, 255),   // 槽0 = 17 = 风翼
    IM_COL32(120, 220, 255, 255),   // 槽1 = 18 = 钻头
    IM_COL32(255, 140, 200, 255),   // 槽2 = 19 = 推进器
};
inline bool  g_enable[3] = { true, true, true };

// 已知的 _creature_id。实测只见到 17，另外两个等运行时收集后补进来。
// 匹配不上时归到 0 号槽，颜色是一致的，只是名字会显示成 "零件#<id>"。
//   ⚠ 17/18/19 是 **2026-10-11 用 fn=joker_parts 实测**出来的三个 id
//     （29 个零件里正好出现这三种，和"三种道具"完全吻合）。
//     但**哪个 id 对应哪个名字**还需要用户确认 —— 现在按 17/18/19 顺序填，
//     颜色一定是区分开的；如果名字对错了，用户看一眼告诉我，改这一行即可。
inline int   g_id[3] = { 17, 18, 19 };
// ★ 名字顺序（2026-10-11 用户实测校正）：
//   我原来按 17/18/19 = 推进器/风翼/钻头 猜的，**猜错了**。
//   用户对照画面对出来的真实对应是：
//     17 -> 风翼      18 -> 钻头      19 -> 推进器
//   所以 g_id 保持 {17,18,19} 不变，只把 label 顺序跟着改过来。
inline const char *g_label[3] = { "风翼", "钻头", "推进器" };

// ---- 读到的条目 ----------------------------------------------------------
struct Item { float x, y; int cid; };
inline Item  g_items[64];
inline int   g_n      = 0;
inline int   g_cid_seen[8]  = {0};       // 见过的 _creature_id（供 dump 用）
inline int   g_cid_n  = 0;
inline int64_t g_last_ok_ms = 0;

inline int64_t now_ms()
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// ---- 读一次（挂在绘制线程上，每帧或每几帧调一次）--------------------------
//   开销很小：只是几次指针解引用，没有遍历对象图。
inline void refresh()
{
    g_n = 0;
    const char *why = "";
    uint64_t gmd = PyCore::game_kernel_dict(why);
    if (!gmd) return;

    const int64_t i_um = PyCore::find_key(gmd, "unit_mgr", -1);
    const uint64_t um = (i_um >= 0) ? PyCore::get_attr(gmd, i_um) : 0;
    if (!PyCore::is_obj(um)) return;

    const uint64_t ud = getPtr64(um - 0x18);
    const int64_t i_ubt = PyCore::find_key(ud, "units_by_type", -1);
    const uint64_t ubt = (i_ubt >= 0) ? PyCore::get_attr(ud, i_ubt) : 0;
    if (!PyCore::is_obj(ubt)) return;

    const uint64_t lst = PyCore::get_type_list(ubt, 209);
    if (!PyCore::is_obj(lst)) return;

    int64_t n = 0;
    vm_readv(lst + 0x10, &n, 8);
    const uint64_t items = getPtr64(lst + 0x18);
    if (n <= 0 || !PyCore::is_obj(items)) return;
    if (n > 64) n = 64;

    // 字段下标缓存（第一次查出来后就不用再查字典了）
    static int64_t s_i_pos = -1, s_i_cid = -1;
    static uint64_t s_hint_dict = 0;

    for (int64_t i = 0; i < n; i++) {
        const uint64_t inst = getPtr64(items + (uint64_t)i * 8);
        if (!PyCore::is_obj(inst)) continue;
        const uint64_t d = getPtr64(inst - 0x18);
        PyCore::Dict dk;
        if (!PyCore::read_dict(d, dk)) continue;

        // 对象换了就重置下标缓存（字典布局跟着类型走，同类型不用重查）
        if (d != s_hint_dict) { s_hint_dict = d; s_i_pos = -1; s_i_cid = -1; }

        s_i_pos = PyCore::resolve_index(d, dk, s_i_pos, "_pos_vec2");
        s_i_cid = PyCore::resolve_index(d, dk, s_i_cid, "_creature_id");
        if (s_i_pos < 0) continue;

        // _pos_vec2 是 math3d.vector2：对象 +0x10 = x，+0x14 = y
        const uint64_t vp = getPtr64(PyCore::value_slot(dk, s_i_pos));
        if (!PyCore::is_obj(vp)) continue;
        float xy[2] = {0, 0};
        if (!vm_readv(vp + 0x10, xy, 8)) continue;
        if (!(xy[0] == xy[0]) || !(xy[1] == xy[1])) continue;      // NaN
        if (xy[0] == 0.0f && xy[1] == 0.0f) continue;              // 没读到

        // _creature_id 的读法要小心：它是**小整数**(Python int)，
        // 而坐标字段是 float 对象 —— 两者的对象表示不一样。
        // 实测 `read_number` 对小整数读不出来（29 个零件全是 cid=-1），
        // 所以这里多试几条路，任一条成功就行。
        int cid = -1;
        if (s_i_cid >= 0) {
            const uint64_t vraw = getPtr64(PyCore::value_slot(dk, s_i_cid));

            // 路子 1：当数字读（对 float 对象有效）
            float cv = 0.0f;
            if (PyCore::is_obj(vraw) && PyCore::read_number(vraw, cv, -1.0e6, 1.0e6))
                cid = (int)cv;

            // 路子 2：CPython 的 PyLong 布局 —— ob_digit 在 +0x18（64 位对齐），
            //         小整数就是这一个 digit。路子 1 失败时走这条。
            if (cid < 0 && PyCore::is_obj(vraw)) {
                int64_t iv = 0;
                if (vm_readv(vraw + 0x18, &iv, 8) && iv > -100000 && iv < 100000)
                    cid = (int)iv;
            }
        }

        g_items[g_n].x = xy[0];
        g_items[g_n].y = xy[1];
        g_items[g_n].cid = cid;
        g_n++;

        // 收集见过的 cid（dump 用，最多 8 个）
        if (cid >= 0) {
            bool seen = false;
            for (int k = 0; k < g_cid_n; k++) if (g_cid_seen[k] == cid) { seen = true; break; }
            if (!seen && g_cid_n < 8) g_cid_seen[g_cid_n++] = cid;
        }
    }
    g_last_ok_ms = now_ms();
}

// cid -> 0/1/2 槽位；认不出就用 0 号槽（颜色仍然统一）
inline int slot_of(int cid)
{
    for (int k = 0; k < 3; k++) if (g_id[k] >= 0 && g_id[k] == cid) return k;
    return -1;
}

inline const char *name_of(int cid)
{
    const int s = slot_of(cid);
    if (s >= 0) return g_label[s];
    return "零件";
}

// ---- 绘制：**只画名字和距离，不画方框**（用户明确要求）--------------------
//   投影公式照抄 Draw_Main 里画大门那一段（实测可用）：
//     cam = m[3]*X + m[7]*Z + m[11]*Y + m[15]        相机空间深度
//     sx  = px + (m[0]*X + m[4]*Z + m[8]*Y + m[12]) / cam * px
//     sy  = py - (m[1]*X + m[5]*Z + m[9]*Y + m[13]) / cam * py
//   注意坐标约定是 (X, 高度Z, 水平Y) —— 零件的 _pos_vec2 给的是 (x, y)，
//   所以 X = item.x，Y = item.y，高度 Z 用自身高度代替（零件贴地）。
inline void draw(ImDrawList *dl, const float *matrix, float px, float py,
                 float self_x, float self_z, float self_y, float dist_scale)
{
    if (!g_show || g_n == 0) return;

    for (int i = 0; i < g_n; i++) {
        const float X = g_items[i].x;
        const float Y = g_items[i].y;
        const float Z = self_z;                 // 零件贴地，高度取自身高度即可

        // 距离（米）：水平距离就够，零件不需要算高度差
        const float dx = X - self_x, dy = Y - self_y;
        const float dist_m = sqrtf(dx * dx + dy * dy) / (dist_scale > 0.0f ? dist_scale : 1.0f);
        if (dist_m > g_range_m) continue;

        const float cam = matrix[3]*X + matrix[7]*Z + matrix[11]*Y + matrix[15];
        if (cam <= 0.01f) continue;             // 在相机背后
        const float sx = px + (matrix[0]*X + matrix[4]*Z + matrix[8]*Y + matrix[12]) / cam * px;
        const float sy = py - (matrix[1]*X + matrix[5]*Z + matrix[9]*Y + matrix[13]) / cam * py;
        if (sx < 0 || sy < 0 || sx > px * 2 || sy > py * 2) continue;   // 屏幕外

        const int slot = slot_of(g_items[i].cid);
        const ImU32 col = g_col[(slot >= 0) ? slot : 0];
        if (slot >= 0 && !g_enable[slot]) continue;      // 这一类被关掉了

        char buf[64];
        if (g_show_name && g_show_dist)
            snprintf(buf, sizeof(buf), "%s %.0fm", name_of(g_items[i].cid), dist_m);
        else if (g_show_name)
            snprintf(buf, sizeof(buf), "%s", name_of(g_items[i].cid));
        else if (g_show_dist)
            snprintf(buf, sizeof(buf), "%.0fm", dist_m);
        else
            continue;

        // 字号可调（用户要求）：用 AddText(font, size, ...) 这个重载，
        //   不用默认字号 —— 否则调不动。文字居中在投影点上，
        //   **不画方框**（零件满地都是，画框会糊成一片）。
        ImFont *fnt = ImGui::GetFont();
        const float fs = (g_font_size < 8.0f) ? 8.0f : (g_font_size > 48.0f ? 48.0f : g_font_size);
        const ImVec2 ts = fnt->CalcTextSizeA(fs, FLT_MAX, 0.0f, buf);
        dl->AddText(fnt, fs, ImVec2(sx - ts.x * 0.5f, sy - ts.y * 0.5f), col, buf);
    }
}

} // namespace JokerParts
