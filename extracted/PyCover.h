#pragma once
// ============================================================================
// PyCover.h —— 掩体 / 遮挡判断（"我和他之间有没有东西挡着，打不到"）
// ============================================================================
// 用途
//   · 自动技能 / 自动飞轮：隔墙时不要浪费技能（门控里当一道否决条件）
//   · 自动瞄准 / 出刀判断：知道这一刀砍不到就别砍
//
// 数据来源（**不是**猜的，是游戏自己的碰撞表）
//   `g_world_mgr.cur_world.collision_mgr.collision_dict`
//     = 本局真实存在的碰撞单元表，实测一局（h55_demo）176 条，按类型统计：
//         SoftWallUnit   64   软墙（地图隔断）
//         PanelUnit      32   墙板
//         HookUnit       30   狂欢之椅
//         CupboardUnit   17   柜子
//         WoodUnit       10   可砸的板子
//         BoxUnit         8   箱子
//         GeneratorUnit   7   密码机
//         DoorUnit        6   门
//         MyCivilianUnit  1 / ButcherUnit 1   ← 是玩家自己/对手，不算障碍
//   这些都是 physx 侧的碰撞单元，位置从单位的 `position`（math3d.vector）读，
//   朝向从 `angle` 读。实测 math3d.vector 的三个 float 在 +0x10 / +0x14 / +0x18，
//   与世界坐标同单位（除以 dist_scale=11.886 才是米）。
//
// ⚠ 一个必须写下来的限制
//   本工程**只读内存，不执行游戏代码**，所以没法调用 physx 的 raycast。
//   这里是"用碰撞单元表 + 线段求交"复刻 raycast 的结论，精度取决于下面
//   kKinds[] 里每种单元的尺寸（单位：米）。尺寸是按模型实测标定的近似值，
//   不是从内存读的精确 OBB —— 要更宽/更窄，调 g_scale 或那张表即可。
//   判据对"近战范围（<= 8 米）有没有东西横在中间"足够可靠，
//   但不等于服务器那套胶囊体横扫的逐三角面结果。
//
// 线程
//   自己一个线程，2 秒一轮；**只有换图/换局才重新读整张表**（整表约 176 条，
//   一次性读 20ms 量级，不能每帧做）。
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>
#include <atomic>
#include <unistd.h>

#include "kerneldriver-paradise.hpp"
#include "PyRoot.h"
#include "PyCore.h"

namespace PyCover {

// ---------------- 配置（由 draw_Gui 的配置表读写）----------------
inline std::atomic<bool>  g_enabled{true};     // 隔墙不打（总开关）
inline std::atomic<bool>  g_raycast{true};     // ★ 优先用 physx raycast（装着 hook 时才生效）
inline std::atomic<bool>  g_debug{false};      // 在 ESP 上标出命中的那个碰撞体
inline std::atomic<bool>  g_draw{false};       // ★ UI 绘制页的「PhysX」开关：3D 画出所有碰撞体积
inline std::atomic<float> g_scale{1.0f};       // 尺寸缩放（1.0 = 标定值）
inline std::atomic<int>   g_floor_dy_m{6};     // 高度差超过这么多米 = 不同楼层，也算打不到

// ---- 真实墙体三角面（physx 网格）的 3D 显示 ----
inline std::atomic<bool>  g_draw_mesh{false};        // 开关：把 physx 三角面画出来
inline std::atomic<bool>  g_draw_mesh_fill{true};    // 是否填充（关掉只看线框）
inline std::atomic<float> g_draw_mesh_range{25.f};   // 只画玩家周围这么多米
inline std::atomic<int>   g_draw_mesh_n{0};          // 本帧画了多少个（调试显示）

// ★ 真·raycast 的挂载点（PyCall 装好 hook 后把它挂上来；没装就是 nullptr）。
//   签名： (ax,ay,az, bx,by,bz, out_blocked) -> 是否拿到了有效答案
//   用函数指针而不是直接 include PyCall，是为了避开 PyCall -> PyCover 的循环依赖。
typedef bool (*LosFn)(float, float, float, float, float, float, bool *);
inline std::atomic<LosFn> g_los_fn{nullptr};
inline std::atomic<int>   g_ray_hit{0};        // raycast 说"被挡"的次数
inline std::atomic<int>   g_ray_ok{0};         // raycast 给出有效答案的次数

// 世界单位 -> 米（与 draw_Gui 的 dist_scale 同一个常数）
static const float kUnitsPerMeter = 11.886f;

// ---------------- 碰撞体 ----------------
enum Kind : uint8_t {
    K_NONE = 0, K_SOFTWALL, K_PANEL, K_DOOR, K_CUPBOARD,
    K_BOX, K_GEN, K_WOOD, K_HOOK, K_OTHER,
};

// 每种单元的尺寸（米）。hl = 沿朝向垂直方向(长轴)的半长，ht = 法向半厚，h = 高度
//   round=1 的类型**忽略朝向、按圆盘判**（半径 = hl）：宁可多挡，不能漏挡。
//   ★ 2026-10-23 实测标定（fn=cover 导出的 141 个碰撞体做最近邻统计）：
//       SoftWallUnit  相邻两段间距中位 **1.58 米**（最大 1.65）→ 它是**一小段墙**、成链拼接
//       PanelUnit     相邻间距中位 10.9 米 → 独立隔板
//       GeneratorUnit 7 台、间距 49 米 → 单台约 2.4x1.4 米
//       HookUnit      成对出现在同一坐标（两条 cid 同位置）
//   ★★ 2026-10-23 第二轮（用户报"有时候隔着墙还是会触发"，三个漏判原因）★★
//     ① 软墙段之间有 ~0.05~0.1 米的缝，线段擦着缝过去就漏了
//        -> 软墙改成 **圆盘(round=1) 半径 0.95 米**（> 半间距 0.83），链上完全连续
//     ② t 只取中段 6%~94%，猎人**贴着墙**时 t<0.06 被跳过 —— 而这正是最常见的情形
//        -> 改成**绝对距离**：只放过线段两端 0.22 米以内
//     ③ 表里没列的类型被整类跳过（换图就可能是 WallUnit/TrapUnit…）
//        -> 未知类型不再丢，按**通用盒子**（0.7x0.7x2.0 米）参与判定
struct KindDef { const char *cls; float hl, ht, h; uint8_t kind; uint8_t round; };
static const KindDef kKinds[] = {
    { "SoftWallUnit",  0.95f, 0.95f, 3.00f, K_SOFTWALL, 1 },   // 软墙（小段，成链；圆盘判）
    { "PanelUnit",     1.20f, 0.18f, 2.60f, K_PANEL,    0 },   // 隔板
    { "DoorUnit",      1.00f, 0.20f, 2.40f, K_DOOR,     0 },   // 门
    { "CupboardUnit",  0.60f, 0.50f, 2.00f, K_CUPBOARD, 0 },   // 柜子
    { "BoxUnit",       0.50f, 0.50f, 1.20f, K_BOX,      0 },   // 箱子
    { "GeneratorUnit", 1.20f, 0.70f, 1.40f, K_GEN,      0 },   // 电机
    { "WoodUnit",      1.40f, 0.25f, 1.20f, K_WOOD,     0 },   // 板子（position 恒 0，实际用不上）
    { "HookUnit",      0.50f, 0.50f, 1.80f, K_HOOK,     1 },   // 椅子
};
static const int kKindCount = (int)(sizeof(kKinds) / sizeof(kKinds[0]));

// 表里没有的类型：**不再跳过**，按通用盒子参与判定（漏挡比多挡危险得多）
static const KindDef kUnknownKind = { "?", 0.70f, 0.70f, 2.00f, K_OTHER, 0 };

static const char* kind_name(int k)
{
    static const char *N[] = { "?", "软墙", "墙板", "门", "柜子", "箱子", "电机", "板子", "椅子", "其它" };
    return (k >= 0 && k <= K_OTHER) ? N[k] : "?";
}

struct Col {
    float x, z;      // 世界 X / 水平 Z（原单位）
    float y;         // 落地高度（原单位）
    float top;       // 顶面高度（原单位）＝ y + 高度
    float hl, ht;    // 半长 / 半厚（原单位，已乘过 g_scale）
    float rot;       // 朝向（弧度）
    int   cid;       // collision_dict 的键
    uint8_t kind;
    uint8_t round;   // 1 = 忽略朝向按圆盘判（半径 hl）
};
static const int MAX_COLS = 384;
static Col  g_buf[2][MAX_COLS];
static std::atomic<int> g_active{0};      // 双缓冲：读线程永远读到完整一份
static std::atomic<int> g_n[2] = { 0, 0 };

static char      g_status[96] = "未启动";
static std::atomic<int>  g_rebuilds{0};
static std::atomic<int>  g_last_scene_id{-1};
static std::atomic<uint64_t> g_last_dict{0};

// ---------------- CPython 小工具 ----------------
static inline bool is_obj(uint64_t p) { return PyCore::is_obj(p); }

// 类名（tp_name，+0x18 是 char*）；测出来的名字都是 ASCII
static bool class_of(uint64_t inst, char *out, size_t cap)
{
    out[0] = 0;
    if (!is_obj(inst)) return false;
    uint64_t cls = getPtr64(inst + 8);
    if (!is_obj(cls)) return false;
    uint64_t np = getPtr64(cls + 0x18);
    if (!is_obj(np)) return false;
    if (!vm_readv(np, out, cap - 1)) return false;
    out[cap - 1] = 0;
    return out[0] != 0;
}

static bool read_pylong(uint64_t o, int &v)
{
    if (!is_obj(o)) return false;
    int64_t sz = 0; uint32_t dg = 0;
    if (!vm_readv(o + 0x10, &sz, 8)) return false;
    if (!vm_readv(o + 0x18, &dg, 4)) return false;
    int64_t x = (sz == 0) ? 0 : (int64_t)dg;
    if (sz < 0) x = -x;
    v = (int)x;
    return true;
}

// 从 __dict__ 里按名字取值的指针（带 hint：命中就 2 次读）
static uint64_t field_ptr(uint64_t obj, const char *name, int64_t &hint)
{
    if (!is_obj(obj)) return 0;
    uint64_t d = PyCore::get_inst_dict(obj);
    if (!d) return 0;
    PyCore::Dict dk;
    if (!PyCore::read_dict(d, dk)) return 0;
    int64_t i = PyCore::resolve_index(d, dk, hint, name);
    if (i < 0) return 0;
    hint = i;
    return getPtr64(PyCore::value_slot(dk, i));
}

// ---------------- 建表 ----------------
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// 读 collision_dict，把里面的碰撞单元抄成一张本地表（只在换图/换局时做）
static bool rebuild(uintptr_t libbase, const char *&why)
{
    why = "";
    if (!libbase) { why = "no libbase"; return false; }
    if (!PyRoot::ensure()) { why = PyRoot::status_text(); return false; }

    const char *w = "";
    uint64_t md = PyCore::game_kernel_dict(w);
    if (!md) { why = w; return false; }

    static int64_t h_gw = -1, h_cw = -1, h_cm = -1, h_cd = -1;
    int64_t i_gw = PyCore::find_key(md, "g_world_mgr", h_gw);
    if (i_gw < 0) { why = "没有 g_world_mgr"; return false; }
    h_gw = i_gw;
    uint64_t gw = PyCore::get_attr(md, i_gw);
    if (!is_obj(gw)) { why = "g_world_mgr 无效"; return false; }

    uint64_t cw = field_ptr(gw, "cur_world", h_cw);
    if (!is_obj(cw)) { why = "cur_world 无效(不在对局?)"; return false; }

    // scene_id：换图判据（同一张图重开也换 id 以外的状态，所以还看 dict 指针）
    int sid = -1;
    { int64_t h = -1; uint64_t v = field_ptr(cw, "scene_id", h); read_pylong(v, sid); }
    g_last_scene_id.store(sid);

    uint64_t cm = field_ptr(cw, "collision_mgr", h_cm);
    if (!is_obj(cm)) { why = "collision_mgr 无效"; return false; }
    uint64_t cd = field_ptr(cm, "collision_dict", h_cd);
    if (!is_obj(cd)) { why = "collision_dict 无效"; return false; }
    g_last_dict.store(cd);

    PyCore::Dict dk;
    if (!PyCore::read_dict(cd, dk) || dk.n_entries <= 0) { why = "collision_dict 读不出来"; return false; }

    const float sc = clampf(g_scale.load(), 0.2f, 4.0f);
    const int wr = 1 - g_active.load();
    int n = 0, skipped = 0, unknown = 0;

    // 每个类的字段序号缓存（position / direction），一次建表只扫一遍名字
    struct ClsHint { uint64_t cls; int64_t ip, id; };
    static ClsHint ch[16]; static int ch_n = 0;
    if (g_rebuilds.load() == 0) ch_n = 0;      // 首次清一遍（类指针换局会变）

    for (int64_t i = 0; i < dk.n_entries && n < MAX_COLS; i++) {
        uint64_t ko = getPtr64(PyCore::key_slot(dk, i));
        uint64_t uo = getPtr64(PyCore::value_slot(dk, i));
        if (!is_obj(uo)) continue;

        char cls[48];
        if (!class_of(uo, cls, sizeof(cls))) continue;

        // 玩家单位本身不算障碍（collision_dict 里确实有自己/对手各一条）
        if (strstr(cls, "CivilianUnit") || strstr(cls, "ButcherUnit") || strstr(cls, "PlayerUnit")) {
            skipped++;
            continue;
        }

        // 类型 -> 尺寸；**未知类型按通用盒子参与判定**，不再整类丢掉
        const KindDef *kd = &kUnknownKind;
        for (int k = 0; k < kKindCount; k++)
            if (strcmp(cls, kKinds[k].cls) == 0) { kd = &kKinds[k]; break; }
        if (kd == &kUnknownKind) unknown++;

        int cid = 0; read_pylong(ko, cid);

        // 该类的 position / direction 序号（带缓存；resolve_index 每次按名字核对，缓存错了会重扫）
        uint64_t ctp = getPtr64(uo + 8);
        ClsHint *h = nullptr;
        for (int k = 0; k < ch_n; k++) if (ch[k].cls == ctp) { h = &ch[k]; break; }
        if (!h && ch_n < 16) { h = &ch[ch_n++]; h->cls = ctp; h->ip = h->id = -1; }
        static int64_t ip_fb = -1, id_fb = -1;
        int64_t &ip = h ? h->ip : ip_fb;
        int64_t &id = h ? h->id : id_fb;

        uint64_t pv = field_ptr(uo, "position", ip);
        if (!is_obj(pv)) continue;
        float px[3] = { 0, 0, 0 };
        if (!vm_readv(pv + 0x10, px, 12)) continue;
        if (fabsf(px[0]) > 100000.f || fabsf(px[2]) > 100000.f) continue;
        // 实测 WoodUnit（可砸的板子）的 position 恒为 (0,0,0) —— 它的位置挂在 model 上。
        // 这种就跳过：板子另有 AutoPallet 那条路在管，不影响"隔墙不打"。
        if (fabsf(px[0]) < 0.01f && fabsf(px[2]) < 0.01f) { skipped++; continue; }

        // 朝向：angle 实测恒 0（不是朝向），改用 direction（单位朝前向量，实测面板类=(0,0,1)）。
        //   墙/板/门的"面"朝 direction，所以**长度方向是它的垂线** -> yaw + 90°。
        float dir[3] = { 0.f, 0.f, 1.f };
        {
            uint64_t dv = field_ptr(uo, "direction", id);
            if (!is_obj(dv) || !vm_readv(dv + 0x10, dir, 12) ||
                (dir[0] * dir[0] + dir[2] * dir[2]) < 0.04f) {
                dir[0] = 0.f; dir[2] = 1.f;
            }
        }
        float yaw = atan2f(dir[0], dir[2]) + 1.57079633f;   // 长轴 = 朝向的垂线

        Col &c = g_buf[wr][n++];
        c.x  = px[0];
        c.y  = px[1];
        c.z  = px[2];
        c.top = px[1] + kd->h * kUnitsPerMeter;
        c.hl = kd->hl * kUnitsPerMeter * sc;
        c.ht = kd->ht * kUnitsPerMeter * sc;
        c.rot = yaw;
        c.cid = cid;
        c.kind = kd->kind;
        c.round = kd->round;
    }

    g_n[wr] = n;
    g_active.store(wr);
    g_rebuilds.fetch_add(1);

    // ★★ 软墙链条合并（"补墙"）★★
    //   实测软墙是 1.6 米一小段、成链拼接（最近邻中位 1.58 米）。单段按圆盘判时，
    //   相邻两段之间会留出极窄的缝，而且视觉上是一串小方块、看不出是"一面墙"。
    //   这里把**同一链条上相邻的两段**之间补一条细长墙段（中心取中点、朝向取两点连线），
    //   判定和渲染就都变成连续的整面墙 —— 贴着墙站着也不会从缝里漏过去。
    if (g_n[wr].load() > 1) {
        int base_n = g_n[wr].load();
        int added = 0;
        const float kLinkMax = 2.4f * kUnitsPerMeter;      // 只在 2.4 米内配对（间距实测 1.65）
        for (int i = 0; i < base_n && g_n[wr].load() < MAX_COLS - 4; i++) {
            const Col &a = g_buf[wr][i];
            if (a.kind != K_SOFTWALL) continue;
            for (int j = i + 1; j < base_n && g_n[wr].load() < MAX_COLS - 4; j++) {
                const Col &b2 = g_buf[wr][j];
                if (b2.kind != K_SOFTWALL) continue;
                const float dx = b2.x - a.x, dz = b2.z - a.z;
                const float d2 = dx * dx + dz * dz;
                if (d2 > kLinkMax * kLinkMax) continue;
                Col &c = g_buf[wr][g_n[wr].load()];
                c.x = (a.x + b2.x) * 0.5f;
                c.z = (a.z + b2.z) * 0.5f;
                c.y = (a.y < b2.y ? a.y : b2.y);
                c.top = (a.top > b2.top ? a.top : b2.top);
                c.hl = sqrtf(d2) * 0.5f + 0.25f * kUnitsPerMeter;   // 两端各多盖 0.25 米
                c.ht = 0.22f * kUnitsPerMeter * sc;
                c.rot = atan2f(dx, dz);                              // 长轴 = 两点连线
                c.cid = -1;                                          // -1 = 补出来的墙段
                c.kind = K_SOFTWALL;
                c.round = 0;
                g_n[wr].store(g_n[wr].load() + 1);
                added++;
            }
        }
        if (added) printf("[掩体] 软墙链条补墙 %d 段（原 %d）\n", added, base_n);
    }

    snprintf(g_status, sizeof(g_status), "正常 %d 个(跳过%d 未知%d) scene=%d",
             g_n[wr].load(), skipped, unknown, sid);
    return g_n[wr].load() > 0;
}

// ---------------- 线段 x 有向矩形 / 圆盘 ----------------
// 返回 true = 相交，out_t 给出交点在 A->B 上的参数(0..1)
//   extra_ht：额外半厚容差（墙本身有厚度，判定时放宽一点，少漏判）
static inline bool seg_hit_obb(float ax, float az, float bx, float bz, const Col &c,
                               float extra_ht, float &out_t)
{
    const float hl = c.hl, ht = c.ht + extra_ht;
    float ca = cosf(c.rot), sa = sinf(c.rot);
    // 把两个点转到盒子的局部坐标（绕 Y 顺时针转 -rot）
    float p0x = (ax - c.x), p0z = (az - c.z);
    float p1x = (bx - c.x), p1z = (bz - c.z);
    float l0x =  p0x * ca + p0z * sa;
    float l0z = -p0x * sa + p0z * ca;
    float l1x =  p1x * ca + p1z * sa;
    float l1z = -p1x * sa + p1z * ca;

    const float dx = l1x - l0x, dz = l1z - l0z;
    float tmin = 0.f, tmax = 1.f;

    if (fabsf(dx) < 1e-6f) {
        if (l0x < -hl || l0x > hl) return false;
    } else {
        float t1 = (-hl - l0x) / dx, t2 = (hl - l0x) / dx;
        if (t1 > t2) { float s = t1; t1 = t2; t2 = s; }
        if (t1 > tmin) tmin = t1;
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return false;
    }
    if (fabsf(dz) < 1e-6f) {
        if (l0z < -ht || l0z > ht) return false;
    } else {
        float t1 = (-ht - l0z) / dz, t2 = (ht - l0z) / dz;
        if (t1 > t2) { float s = t1; t1 = t2; t2 = s; }
        if (t1 > tmin) tmin = t1;
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return false;
    }
    out_t = tmin;
    return true;
}

// 线段 x 圆盘（软墙/椅子这种"朝向不可信"的碰撞体按这个判）
static inline bool seg_hit_disc(float ax, float az, float bx, float bz,
                               float cx, float cz, float r, float &out_t)
{
    const float dx = bx - ax, dz = bz - az;
    const float fx = ax - cx, fz = az - cz;
    const float a = dx * dx + dz * dz;
    if (a < 1e-9f) return false;
    const float bq = 2.f * (fx * dx + fz * dz);
    const float cq = fx * fx + fz * fz - r * r;
    float disc = bq * bq - 4.f * a * cq;
    if (disc < 0.f) return false;
    disc = sqrtf(disc);
    float t1 = (-bq - disc) / (2.f * a);
    if (t1 < 0.f) t1 = (-bq + disc) / (2.f * a);   // 起点已在圆内 -> 取出射点
    if (t1 < 0.f || t1 > 1.f) return false;
    out_t = t1;
    return true;
}

// ============================================================================
// ★★★ physx 场景定位（纯读，不改游戏任何内存）★★★
// ============================================================================
// 2026-10-23 实测链路（在设备上用 RTTI 一路验证出来的）：
//
//   g_world_mgr.cur_world.collision_mgr._scene_col     ← Python 对象，类型名 scene_col
//     · PyObject 头 16 字节（refcnt/type），**C++ 成员从 +0x10 开始**
//     · scene_col + 0x10  -> impl 结构（首字段是指回 Python 对象的自指针）
//   impl：
//     +0x18  neox::dynamics::ClothManager
//     +0x68  physx::NpScene          ★ 这就是 PxScene，真正的物理世界
//     +0x78  neox::dynamics::VehicleMgr
//     +0x88  neox::dynamics::RayResult
//     +0x90  neox::dynamics::RayMultiResult
//     +0x98  neox::dynamics::SweepResult
//     +0xA0  neox::dynamics::SweepIntersectResult
//     +0xA8  neox::dynamics::OverlapResult
//
// 怎么认出来的：C++ 对象首 qword 是 vtable，vtable[-1] 是 &type_info，
// type_info+8 是 mangled 类名 —— 直接读出 N5physx7NpSceneE 这种名字，不用猜。
// 静态墙体的三角面就在这个 NpScene 里（来自 scene_battle/<map>/nav/<map>.scn_col），
// Python 层完全没有几何，所以只能走 physx。
// ============================================================================
struct PhysxInfo {
    uint64_t scene_col = 0;      // Python 对象
    uint64_t impl      = 0;      // C++ 实现结构
    uint64_t px_scene  = 0;      // physx::NpScene*
    uint64_t vtable    = 0;
    int      vt_slots  = 0;      // vtable 里连续的代码槽数量
    char     rt_name[80] = "";   // vtable[-1] 解出来的类名（应含 NpScene）
    char     why[96] = "";       // 失败原因
};
static PhysxInfo g_physx;

// 读 C 字符串（mangled 类名/方法名都是 ASCII）
static void read_cstr(uint64_t addr, char *out, size_t cap)
{
    out[0] = 0;
    if (!is_obj(addr) || cap < 2) return;
    if (!vm_readv(addr, out, cap - 1)) { out[0] = 0; return; }
    out[cap - 1] = 0;
    for (size_t i = 0; i < cap; ++i) if (!out[i]) return;
}

// Itanium ABI：vtable[-1] = &type_info，type_info+8 = const char* __name
static bool rtti_name_of(uint64_t vtable, char *out, size_t cap)
{
    out[0] = 0;
    if (!is_obj(vtable)) return false;
    uint64_t ti = 0;
    if (!vm_readv(vtable - 8, &ti, 8)) return false;
    ti &= 0x00FFFFFFFFFFFFFFULL;
    if (!is_obj(ti)) return false;
    uint64_t nm = 0;
    if (!vm_readv(ti + 8, &nm, 8)) return false;
    nm &= 0x00FFFFFFFFFFFFFFULL;
    if (!is_obj(nm)) return false;
    read_cstr(nm, out, cap);
    return out[0] != 0;
}

// 定位 physx 场景（幂等；每轮建表时顺手刷新一次）
static bool physx_probe(PhysxInfo &out)
{
    out = PhysxInfo{};
    if (!PyRoot::ensure()) { snprintf(out.why, sizeof(out.why), "%s", PyRoot::status_text()); return false; }
    const char *w = "";
    uint64_t md = PyCore::game_kernel_dict(w);
    if (!md) { snprintf(out.why, sizeof(out.why), "%s", w); return false; }

    static int64_t h_gw = -1, h_cw = -1, h_cm = -1, h_sc = -1;
    int64_t i_gw = PyCore::find_key(md, "g_world_mgr", h_gw);
    if (i_gw < 0) { snprintf(out.why, sizeof(out.why), "no g_world_mgr"); return false; }
    h_gw = i_gw;
    uint64_t gw = PyCore::get_attr(md, i_gw);
    uint64_t cw = field_ptr(gw, "cur_world", h_cw);
    if (!is_obj(cw)) { snprintf(out.why, sizeof(out.why), "no cur_world"); return false; }
    uint64_t cm = field_ptr(cw, "collision_mgr", h_cm);
    if (!is_obj(cm)) { snprintf(out.why, sizeof(out.why), "no collision_mgr"); return false; }
    uint64_t sc = field_ptr(cm, "_scene_col", h_sc);
    if (!is_obj(sc)) { snprintf(out.why, sizeof(out.why), "no _scene_col"); return false; }
    out.scene_col = sc;

    // PyObject 头 16 字节之后才是 C++ 成员
    uint64_t impl = 0;
    if (!vm_readv(sc + 0x10, &impl, 8)) { snprintf(out.why, sizeof(out.why), "impl 读不到"); return false; }
    impl &= 0x00FFFFFFFFFFFFFFULL;
    if (!is_obj(impl)) { snprintf(out.why, sizeof(out.why), "impl 无效"); return false; }
    out.impl = impl;

    uint64_t px = 0;
    if (!vm_readv(impl + 0x68, &px, 8)) { snprintf(out.why, sizeof(out.why), "impl+0x68 读不到"); return false; }
    px &= 0x00FFFFFFFFFFFFFFULL;
    if (!is_obj(px)) { snprintf(out.why, sizeof(out.why), "NpScene 指针无效"); return false; }
    out.px_scene = px;

    uint64_t vt = 0;
    if (!vm_readv(px, &vt, 8)) { snprintf(out.why, sizeof(out.why), "vtable 读不到"); return false; }
    vt &= 0x00FFFFFFFFFFFFFFULL;
    out.vtable = vt;
    rtti_name_of(vt, out.rt_name, sizeof(out.rt_name));

    // vtable 槽数：连续指向"代码段"(本进程里模块基址附近)的指针个数。
    // 只做参考计数，不靠它判类型 —— 判类型靠上面的 RTTI 名字。
    int slots = 0;
    uint64_t fn = 0;
    for (int i = 0; i < 200; ++i) {
        if (!vm_readv(vt + (uint64_t)i * 8, &fn, 8)) break;
        fn &= 0x00FFFFFFFFFFFFFFULL;
        if (!is_obj(fn)) break;
        slots++;
    }
    out.vt_slots = slots;
    snprintf(g_status, sizeof(g_status), "physx ok NpScene=0x%llX", (unsigned long long)px);
    return true;
}

// ---------------- 对外：遮挡判断 ----------------
struct Hit {
    int   cid;
    uint8_t kind;
    float t;        // 交点在 A->B 上的位置(0..1)
    float x, z;     // 碰撞体中心
    bool  floor;    // true = 不是被墙挡，是"不同楼层"（高度差太大）
};
static Hit g_last_hit;
static std::atomic<int> g_query_n{0};
static std::atomic<int> g_block_n{0};

// 主接口：从 A(ax,ay,az) 到 B(bx,by,bz) 是否被挡。
//   坐标系与 draw_Gui 一致：ay/by 是**高度**，az/bz 是水平 Z；单位 = 原单位(裸坐标)。
static bool blocked(float ax, float ay, float az,
                   float bx, float by, float bz,
                   Hit *out)
{
    g_query_n.fetch_add(1);
    auto finish = [&](bool r, Hit *h) { if (r) g_block_n.fetch_add(1); if (h) g_last_hit = *h; return r; };

    Hit dummy{};
    Hit &h = out ? *out : dummy;
    h = Hit{};

    const float ddx = bx - ax, ddz = bz - az;
    const float d2 = ddx * ddx + ddz * ddz;
    if (d2 < 1.0f) return finish(false, out);          // 贴脸

    // ---- ① 首选：真 physx raycast（PyCall 装了 hook 才有这个函数指针）----
    //   它问的是"physx 世界（含静态墙体）这条线有没有撞到东西"，比下面的碰撞体近似权威。
    //   拿不到答案（没装/超时/距离太短）就落到 ② 的几何判据。
    if (g_raycast.load() && g_los_fn.load() != nullptr) {
        bool rb = false;
        if (g_los_fn.load()(ax, ay, az, bx, by, bz, &rb)) {
            g_ray_ok.fetch_add(1);
            h.kind = K_OTHER;
            h.cid = -2;                                 // -2 = 结论来自 raycast
            if (rb) { g_ray_hit.fetch_add(1); return finish(true, out); }
            return finish(false, out);
        }
    }

    // ---- ② 兜底：碰撞体表 + 线段求交 ----
    // 不同楼层：近战打不到（楼梯处 6 米内不算）
    const float dy = fabsf(by - ay) / kUnitsPerMeter;
    if (dy > (float)g_floor_dy_m.load()) {
        h.floor = true; h.t = 0.f;
        return finish(true, out);
    }

    const int b = g_active.load();
    const int n = g_n[b].load();
    if (n <= 0) return finish(false, out);

    // ★ 端点余量改成**绝对距离**（原来按 6%/94% 取比例）：
    //   猎人贴着墙站时 t 会 < 0.06，被当成"我自己贴着墙"直接跳过 —— 这正是
    //   用户报的"有时候隔着墙还是会触发"的主因之一。
    //   现在只放过线段两端 0.22 米以内（真正贴脸的那一段），中间一律判。
    const float seglen = sqrtf(d2);
    float t_lo = (0.22f * kUnitsPerMeter) / seglen;
    if (t_lo > 0.45f) t_lo = 0.45f;            // 极短距离：只看正中一半也不至于全漏
    const float t_hi = 1.f - t_lo;

    const float ylo = (ay < by ? ay : by), yhi = (ay > by ? ay : by);
    // 高度不重叠的容差：1.2 米（矮箱子不会挡二楼那条线，但墙一定盖住脚底到胸口）
    const float yslack = 1.2f * kUnitsPerMeter;
    // 墙本身有厚度：判定时把半厚放宽 0.12 米，少漏判
    const float extra_ht = 0.12f * kUnitsPerMeter;

    bool any = false;
    float best_t = 2.f;
    for (int i = 0; i < n; i++) {
        const Col &c = g_buf[b][i];
        if (c.top + yslack < ylo) continue;      // 障碍整个在这一层之下
        if (c.y - yslack > yhi)   continue;      // 障碍整个在这一层之上
        float t = 0.f;
        const bool hit = c.round
            ? seg_hit_disc(ax, az, bx, bz, c.x, c.z, c.hl, t)
            : seg_hit_obb(ax, az, bx, bz, c, extra_ht, t);
        if (!hit) continue;
        if (t < t_lo || t > t_hi) continue;
        if (t >= best_t) continue;
        best_t = t;
        h.cid = c.cid; h.kind = c.kind; h.t = t; h.x = c.x; h.z = c.z; h.floor = false;
        any = true;
    }
    return finish(any, out);
}

// 便捷：两个**场景对象**之间（自身 / 监管）
static bool blocked_scenes(uint64_t sa, uint64_t sb, Hit *out)
{
    auto pos = [](uint64_t scene, float &x, float &y, float &z) -> bool {
        if (!is_obj(scene)) return false;
        uint64_t co = getPtr64(scene + 0x28);
        if (!is_obj(co)) return false;
        float raw[3];
        if (!vm_readv(co + 0xa0, raw, 12)) return false;
        x = raw[0]; y = raw[1]; z = raw[2];
        return true;
    };
    float ax, ay, az, bx, by, bz;
    if (!pos(sa, ax, ay, az) || !pos(sb, bx, by, bz)) return false;
    return blocked(ax, ay, az, bx, by, bz, out);
}

// ---------------- 线程 ----------------
static uintptr_t g_libbase = 0;

static void thread_main()
{
    DisguiseThreadName(13);            // 与其它模块区分开（伪装成内核线程名）
    int tick = 0;
    while (true) {
        // 换图/换局才整表重建；平时每 5 秒核对一次 dict 指针
        bool need = (g_rebuilds.load() == 0) || (++tick >= 25);
        if (need) {
            tick = 0;
            // 先花 2 次读确认 dict 指针没变（换局/换图会新建）
            uint64_t old_dict = g_last_dict.load();
            const char *why = "";
            int before = g_rebuilds.load();
            rebuild(g_libbase, why);
            // 顺手把 physx 场景重新定位一次（热更后地址会变）
            physx_probe(g_physx);
            if (g_rebuilds.load() == before) {
                // 没建成：把原因写出去（每轮都写，方便 dwrg_log 里看）
                snprintf(g_status, sizeof(g_status), "未就绪: %s", why && *why ? why : "?");
            } else if (old_dict != 0 && old_dict != g_last_dict.load()) {
                printf("[掩体] 换局/换图 -> 重建 %d 个碰撞体\n", g_n[g_active.load()].load());
                fflush(stdout);
            }
        }
        usleep(200 * 1000);
    }
}

static void start(uintptr_t libbase)
{
    if (g_libbase != 0) return;
    g_libbase = libbase;
    snprintf(g_status, sizeof(g_status), "启动中");
    std::thread(thread_main).detach();
    printf("[掩体] 已启动 libbase=0x%lx\n", (unsigned long)libbase);
    fflush(stdout);
}

// ---------------- 给 UI / dump 用 ----------------
static inline bool  enabled()      { return g_enabled.load(); }
static inline int   count()        { return g_n[g_active.load()].load(); }
static inline const char* status_text() { return g_status; }
static inline int   query_count()  { return g_query_n.load(); }
static inline int   block_count()  { return g_block_n.load(); }
static inline Hit   last_hit()     { return g_last_hit; }
static inline int   rebuild_count(){ return g_rebuilds.load(); }
static inline const Col* cols(int &n) { int b = g_active.load(); n = g_n[b].load(); return g_buf[b]; }

} // namespace PyCover
