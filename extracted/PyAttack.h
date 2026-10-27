#pragma once
// ============================================================================
// PyAttack.h —— 监管当前状态识别（固定读监管，不随视角变化）
// ============================================================================
// 数据源：units_by_type[1] 里第一个 unit 的 state_machine._state[0]
//   监管 unit_type = 1，梦之信徒 = 236，求生 = 2 —— 所以 type=1 就是监管。
//   无论你玩求生还是监管，读的都是"监管者"的状态。
//
// 用户实测规律：
//   1  = 站立
//   2  = 移动
//   26/27 = 落地刀（攻击）
//   45 = 移动出刀（攻击）
//   46 = 静止出刀（攻击）
//
// 配色：出刀（含移动出刀/静止出刀/落地刀）红字，其它黑字。
// ============================================================================

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <atomic>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <string>
#include <mutex>
#include <cctype>
#include <cmath>
#include <sched.h>
#include <pthread.h>
#include "PySelf.h"
#include "PyCore.h"
#include "PyRoot.h"
#include "AutoPallet.h"   // 复用板子列表和状态判据
#include <ctime>
namespace PyAttack {

// ---- 对外发布 ----
static std::atomic<bool> g_ready{false};
static std::atomic<bool> g_attacking{false};
static std::atomic<bool> g_raw_attacking{false};   // 门控前的原始攻击状态
static std::atomic<uint64_t> g_last_attack_ms{0};   // 上次出刀上升沿的毫秒时刻
static std::atomic<int>  g_cur_state{-1};
static std::atomic<int>  g_idle_state{-1};
static std::atomic<float>   g_max_distance{50.f};   // 触发距离(米)，50=不限制
static std::atomic<uint64_t> g_hunter_scene{0};
static std::atomic<bool>  g_require_facing{false};
//   2026-10-26：**门控失败原因**（fn=dbg_atk 里看）
//   0=通过 1=距离 2=朝向 3=隔板 4=位置读不到 5=hs 为空
static std::atomic<int> g_gate_why{0};
static std::atomic<int> g_cb_hits{0};
static std::atomic<int> g_id_hits{0};   // 技能id判据命中次数
static std::atomic<int> g_edge_hits{0}; // 状态上升沿判据命中次数   // 回调真正被调用的次数（2026-10-26 诊断）
static std::atomic<int> g_gate_dist_cm{0};   // 最近一次算出的距离(x100, 米)
static std::atomic<int> g_gate_facing_cm{0}; // 最近一次的朝向点积(x100)       // 是否要求监管朝向自己
static std::atomic<float> g_facing_cos_threshold{0.3f};  // cos 阈值，0.3 ≈ 72°，越小越宽松
static std::atomic<bool> g_skip_if_board_between{false};  // 隔着板子不放技能
// 出刀上升沿回调：PyAttack 检测到 false→true 的那一瞬间，直接调这个函数
// 由外部（draw_Gui.cpp）注册，PyAttack 线程里同步执行
// ---- 门控预读（用户可选）----
// 目的：把距离/朝向/板子判定从"出刀瞬间"提前到周期性刷新，消除缺页异常带来的卡顿。
// 代价：门控值最多滞后 ~10ms，极端情况下可能误判（监管 10ms 内急转）。
static std::atomic<bool> g_use_skill_id{true};   // 用技能ID变化做识别（默认开）
static std::atomic<bool> g_attack_is_charge{false};   // 本次出刀是否蓄力刀
static std::atomic<int>  g_last_cast_skill_id{0};
static std::atomic<bool> g_skillmgr_ready{false};
static uint64_t g_cached_skill_mgr = 0;
static int64_t  g_i_skill_mgr = -1;
static int64_t  g_i_last_cast = -1;

typedef void (*AttackCallback)();
static std::atomic<AttackCallback> g_attack_cb{nullptr};

static std::mutex g_name_mtx;
static char       g_name_buf[64] = "未知";

// ---- 内部缓存 ----
static uint64_t g_cached_unit = 0;
static std::atomic<uint64_t> g_atk_unit{0};   // ★ 正在出刀的那个 unit（可能是信徒）
static uint64_t g_cached_sm   = 0;   // 监管 unit 的 state_machine 地址
static std::unordered_map<int, std::string> g_id2name;

// ---- 攻击状态 ID（用户实测）----
//   2026-10-24：**14711 / 14721 是技能，不是出刀**。
//   这两个 id 的 %10 都是 1，会命中下面"技能 id 末位 1/5 = 出刀"的判据 → 误触发。
static inline bool is_non_attack_skill(int id) {
    return id == 14711 || id == 14721;
}

static inline bool is_attack_id(int id) {
    return id == 26 || id == 27 || id == 45 || id == 46;
}

// ★★ 2026-10-28：A、B 双判据的时间窗（毫秒）
//   原逻辑是"if/else 短路"：A 命中就跳过 B。
//   新逻辑是"A、B 都要在窗口内命中"。
//   为什么不要求**同一帧**：A（技能 id 变化）和 B（_state 上升沿）都是瞬时事件，
//   实测两次轮询（5ms 间隔）之间几乎不可能同时落到同一帧 → 严格同帧 = 永不触发。
//   200ms 覆盖真实的游戏事件时序（技能 id 变化先到，_state 上升沿晚 5~50ms）。
static const int64_t kDualWindowMs = 200;

// PyAttack 内部用的毫秒时间戳(CLOCK_MONOTONIC)
static inline uint64_t pyattack_now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// ---- 读一个 PyLong 整数值 ----
static bool read_pylong(uint64_t obj, int &out)
{
    if (!PyCore::is_obj(obj)) return false;
    int64_t  sz = 0;
    uint32_t dg = 0;
    if (!vm_readv(obj + 0x10, &sz, 8)) return false;
    if (!vm_readv(obj + 0x18, &dg, 4)) return false;
    int64_t v = (sz == 0) ? 0 : (int64_t)dg;
    if (sz < 0) v = -v;
    out = (int)v;
    return true;
}

// ---- 读 list 的最后一个 int（状态机把最新状态 append 到末尾）----
static bool read_list_last_int(uint64_t list_obj, int &out)
{
    if (!PyCore::is_obj(list_obj)) return false;
    int64_t  n     = 0;
    uint64_t items = 0;
    if (!PyCore::read_list(list_obj, n, items)) return false;
    if (n <= 0) return false;
    uint64_t e_last = getPtr64(items + (n - 1) * 8);
    return read_pylong(e_last, out);
}

// ---- 读 list 里是否含攻击 ID（任意位置）----
static bool read_list_has_attack(uint64_t list_obj)
{
    if (!PyCore::is_obj(list_obj)) return false;
    int64_t  n     = 0;
    uint64_t items = 0;
    if (!PyCore::read_list(list_obj, n, items)) return false;
    if (n <= 0 || n > 16) return false;          // 正常 1~2 个元素，超过就是坏数据
    for (int64_t i = 0; i < n; i++) {
        int v = 0;
        if (read_pylong(getPtr64(items + i * 8), v) && is_attack_id(v))
            return true;
    }
    return false;
}

// ---- 从任意对象的 __dict__ 里取字段指针（无 hint，全表扫）----
static uint64_t get_obj_field(uint64_t obj, const char *field)
{
    if (!PyCore::is_obj(obj)) return 0;
    uint64_t d = PyCore::get_inst_dict(obj);
    if (!d) return 0;
    PyCore::Dict dk;
    if (!PyCore::read_dict(d, dk)) return 0;
    for (int64_t i = 0; i < dk.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(dk, i));
        char kname[64] = {0};
        if (!PyCore::read_str(kp, kname, sizeof(kname))) continue;
        if (strcmp(kname, field) == 0)
            return getPtr64(PyCore::value_slot(dk, i));
    }
    return 0;
}
// 带 hint 的 __dict__ 字段读取。hint 命中时只花 2 次驱动读（键指针校验 + 值指针），
// 未命中时退化成一次全扫。见 PyCore::resolve_index 的说明。
static uint64_t get_obj_field_hint(uint64_t obj, const char *field, int64_t &hint)
{
    if (!PyCore::is_obj(obj)) return 0;
    uint64_t d = PyCore::get_inst_dict(obj);
    if (!d) return 0;
    PyCore::Dict dk;
    if (!PyCore::read_dict(d, dk)) return 0;
    int64_t idx = PyCore::resolve_index(d, dk, hint, field);
    if (idx < 0) return 0;
    hint = idx;
    return getPtr64(PyCore::value_slot(dk, idx));
}

// ---- 拿 units_by_type[type] 列表的第一个 unit ----
static uint64_t get_first_unit_of_type(int type)
{
    const char *why = nullptr;
    uint64_t md = PyCore::game_kernel_dict(why);
    if (!md) return 0;

    PyCore::Dict dk;
    if (!PyCore::read_dict(md, dk)) return 0;

    uint64_t gk_obj = 0;
    for (int64_t i = 0; i < dk.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(dk, i));
        char kname[64] = {0};
        if (PyCore::read_str(kp, kname, sizeof(kname)) && strcmp(kname, "unit_mgr") == 0) {
            gk_obj = getPtr64(PyCore::value_slot(dk, i));
            break;
        }
    }
    if (!gk_obj) return 0;

    uint64_t umg_dict = PyCore::get_inst_dict(gk_obj);
    if (!umg_dict) return 0;
    PyCore::Dict umd;
    if (!PyCore::read_dict(umg_dict, umd)) return 0;

    uint64_t ubt = 0;
    for (int64_t i = 0; i < umd.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(umd, i));
        char kname[64] = {0};
        if (PyCore::read_str(kp, kname, sizeof(kname)) && strcmp(kname, "units_by_type") == 0) {
            ubt = getPtr64(PyCore::value_slot(umd, i));
            break;
        }
    }
    if (!ubt) return 0;

    uint64_t lst = PyCore::get_type_list(ubt, type);
    if (!lst) return 0;
    int64_t  n     = 0;
    uint64_t items = 0;
    if (!PyCore::read_list(lst, n, items) || n <= 0) return 0;
    return getPtr64(items);
}

// ============================================================
// ★ 梦之女巫支持：拿全部 type=1 的 unit
// ============================================================
// ★★ 诊断（2026-10-14）：本函数原来有 6 个"静默失败"分支，任何一环断掉都只是
//   return 0，和"这个 type 确实没有列表"完全分不开（SKILL.md 4.6：读不到 ≠ 没有）✗
//   限频打印，一眼看出断在哪一环。slot_n==0 时本函数是每帧调的，必须限频 ✗
static void ubt_fail_diag(int type, const char *step)
{
    static int s_n = 0;
    ++s_n;
    if (s_n > 6 && (s_n % 200) != 0) return;
    printf("[状态] units_by_type[%d] 读取中断 @%s (第 %d 次)\n", type, step, s_n);
    fflush(stdout);
}

// ★★ 2026-10-14 返回值语义修正（原来"读失败"和"确实没有"都返回 0）★★
//   起因：用户报"在模仿者局里被识别成正常对局"，现场日志里
//     `[自动识别] 本局 -> 模仿者局 / 正常对局` 反复交替 —— 识别在抖 ✗
//   根因就是这里分不开两种情况：
//     开会阶段 Python 堆页被换出、内存压力下页换出、跨对局瞬间 ——
//     链路读失败时本函数同样返回 0，调用方只能当成"本局没有 1088" ✗
//   而识别一旦翻成 false，draw_Gui 就会 set_copycat_book(false)，
//   book_merge 头一句把整本身份簿 memset 掉 ——
//   表现就是"设置页显示模仿者局，但一个人都不标身份" ✗
//
//   现在：
//     > 0 = 读到 n 个单位
//     = 0 = **链路是通的，但这个 type 确实没有列表**（真的没有）✓
//     < 0 = **链路读失败**，调用方必须保持上一次判定，不能当成"没有" ✗
//   `get_type_list` 返回 0 无法自证是哪种，所以补一次"units_by_type 字典本身
//   能不能读出来"来区分：字典读得出来 -> 链路通 -> 确实是没这个键 ✓
static int get_all_units_of_type(int type, uint64_t *out, int cap)
{
    const char *why = nullptr;
    uint64_t md = PyCore::game_kernel_dict(why);
    if (!md) { ubt_fail_diag(type, "game_kernel_dict"); return -1; }

    PyCore::Dict dk;
    if (!PyCore::read_dict(md, dk)) { ubt_fail_diag(type, "read_dict(game_kernel)"); return -1; }

    uint64_t gk_obj = 0;
    for (int64_t i = 0; i < dk.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(dk, i));
        char kname[64] = {0};
        if (PyCore::read_str(kp, kname, sizeof(kname)) && strcmp(kname, "unit_mgr") == 0) {
            gk_obj = getPtr64(PyCore::value_slot(dk, i));
            break;
        }
    }
    if (!gk_obj) { ubt_fail_diag(type, "找不到 unit_mgr 键"); return -1; }

    uint64_t umg_dict = PyCore::get_inst_dict(gk_obj);
    if (!umg_dict) { ubt_fail_diag(type, "get_inst_dict(unit_mgr)"); return -1; }
    PyCore::Dict umd;
    if (!PyCore::read_dict(umg_dict, umd)) { ubt_fail_diag(type, "read_dict(unit_mgr)"); return -1; }

    uint64_t ubt = 0;
    for (int64_t i = 0; i < umd.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(umd, i));
        char kname[64] = {0};
        if (PyCore::read_str(kp, kname, sizeof(kname)) && strcmp(kname, "units_by_type") == 0) {
            ubt = getPtr64(PyCore::value_slot(umd, i));
            break;
        }
    }
    if (!ubt) { ubt_fail_diag(type, "找不到 units_by_type 键"); return -1; }

    uint64_t lst = PyCore::get_type_list(ubt, type);
    if (!lst) {
        // 区分"确实没有这个键"还是"读失败"：units_by_type 字典本身读得出来，链路就是通的
        PyCore::Dict ubt_dk;
        if (PyCore::read_dict(ubt, ubt_dk) && ubt_dk.n_entries > 0) return 0;   // 确实没有 ✓
        ubt_fail_diag(type, "get_type_list=0 且 units_by_type 也读不出");
        return -1;                                                            // 读失败 ✗
    }
    int64_t  n     = 0;
    uint64_t items = 0;
    if (!PyCore::read_list(lst, n, items)) { ubt_fail_diag(type, "read_list 失败"); return -1; }
    if (n <= 0) return 0;                                                     // 空列表 = 确实没有 ✓
    if (n > cap) n = cap;
    for (int64_t i = 0; i < n; i++)
        out[i] = getPtr64(items + i * 8);
    return (int)n;
}

// ============================================================
// ★ 读 Python 对象的类名（拷自 AutoPallet::class_name_of）
// ============================================================
static std::string py_class_name(uint64_t obj)
{
    if (!PyCore::is_obj(obj)) return std::string();
    uint64_t head = getPtr64(obj + 0xf8);
    if (head < 0x5000000000ULL || head >= 0x8000000000ULL) return std::string();
    uint64_t name_obj = getPtr64(getPtr64(getPtr64(getPtr64(head) + 0x8) + 0x20) + 0x20);
    int len = getDword(name_obj + 0x10);
    if (len <= 0 || len >= 256) return std::string();
    std::string s(len, '\0');
    if (!vm_readv(getPtr64(name_obj + 0x8), &s[0], len)) return std::string();
    return s;
}

// ============================================================
// ★ 判断：类名是否梦之女巫本体
//
//   yidhra        → 本体（不出刀、不会打人）
//   yidhra_xintu  → 信徒（会攻击、能打人）
// ============================================================
static bool is_yidhra_host(const std::string &cn)
{
    if (cn.empty()) return false;
    std::string s = cn;
    for (auto &c : s) c = (char)::tolower((unsigned char)c);
    return s.find("yidhra") != std::string::npos
        && s.find("xintu") == std::string::npos;
}

// ============================================================
// ★ 从 Python unit 拿对应的 scene object（PyAttack 版）
// ============================================================
static uint64_t py_get_unit_scene(uint64_t u)
{
    if (!PyCore::is_obj(u)) return 0;
    uint64_t d = PyCore::get_inst_dict(u);
    if (!d) return 0;
    PyCore::Dict dk;
    if (!PyCore::read_dict(d, dk)) return 0;
    for (int64_t i = 0; i < dk.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(dk, i));
        char kname[64] = {0};
        if (!PyCore::read_str(kp, kname, sizeof(kname))) continue;
        if (strcmp(kname, "model") == 0) {
            uint64_t vp = getPtr64(PyCore::value_slot(dk, i));
            if (!PyCore::is_obj(vp)) return 0;
            return PyCore::get_scene_obj(vp);
        }
    }
    return 0;
}

// ---- 拿监管 unit ----
static inline uint64_t get_hunter_unit()
{
    return get_first_unit_of_type(1);   // type 1 = 监管
}

// 读监管的场景对象朝向向量（水平面）
// 尝试顺序：
//   1. 场景对象 coor + 0x90 / +0x98（跟红夫人镜子同一位置，最可能对）
//   2. unit 的 direction 属性（math3d.vector）
// 返回 false = 两种都读不到（此时不做朝向过滤，放宽）
static bool get_hunter_forward(uint64_t hunter_unit, uint64_t hunter_scene,
                                float &fx, float &fy)
{
    fx = fy = 0;

    // 方法 1：从场景对象旋转矩阵读
    if (PyCore::is_obj(hunter_scene)) {
        uint64_t coor = getPtr64(hunter_scene + 0x28);
        if (PyCore::is_obj(coor)) {
            float x1 = 0, y1 = 0;
            if (vm_readv(coor + 0x90, &x1, 4) && vm_readv(coor + 0x98, &y1, 4)) {
                if (fabsf(x1) + fabsf(y1) > 0.01f) { fx = x1; fy = y1; return true; }
            }
        }
    }

    // 方法 2：从 unit 的 direction 属性读
    uint64_t dir_obj = get_obj_field(hunter_unit, "direction");
    if (PyCore::is_obj(dir_obj)) {
        float x2 = 0, y2 = 0, z2 = 0;
        if (vm_readv(dir_obj + 0x10, &x2, 4) &&
            vm_readv(dir_obj + 0x14, &y2, 4) &&
            vm_readv(dir_obj + 0x18, &z2, 4)) {
            // 有的布局是 (x, y, z)，有的可能是 (x, z, y)，只要有一对大于 0 就行
            if (fabsf(x2) + fabsf(y2) > 0.01f) { fx = x2; fy = y2; return true; }
            if (fabsf(x2) + fabsf(z2) > 0.01f) { fx = x2; fy = z2; return true; }
        }
    }
    return false;
}

// 监管是否朝向自身。读不到方向时返回 true（= 不过滤，宁可多触发也不漏）
static bool is_hunter_facing_self(uint64_t hunter_unit, uint64_t hunter_scene,
                                   float hx, float hy, float sx, float sy)
{
    float fx = 0, fy = 0;
    if (!get_hunter_forward(hunter_unit, hunter_scene, fx, fy)) return true;

    float len = sqrtf(fx*fx + fy*fy);
    if (len < 0.01f) return true;

    // 从监管指向自身的向量（水平面）
    float tx = sx - hx, ty = sy - hy;
    float tlen = sqrtf(tx*tx + ty*ty);
    if (tlen < 0.01f) return true;   // 贴脸了，任何朝向都算朝向

    float dot = (fx / len) * (tx / tlen) + (fy / len) * (ty / tlen);
    return dot >= g_facing_cos_threshold.load();
}

// 判断"自身(S)和监管(H)之间是否有板子挡路"
// 返回 true = 有板子挡路 → 应该跳过技能
static bool has_board_between(float hx, float hy, float sx, float sy)
{
    int ba = AutoPallet::g_board_active.load();
    int nb = AutoPallet::g_board_n[ba];
    if (nb <= 0) return false;

    float ABx = sx - hx, ABy = sy - hy;
    float ab2 = ABx*ABx + ABy*ABy;
    if (ab2 < 0.01f) return false;   // 贴脸不算"隔着"

    for (int i = 0; i < nb; i++) {
        uint64_t o = AutoPallet::g_boards[ba][i];

        // 板子位置
        float B[3];
        if (!AutoPallet::read_pos(o, B)) continue;

        // 板子状态：state 有效就当作障碍（立着=16，砸下值待实测）
        int state = -1;
        if (!AutoPallet::read_state(o, state)) continue;
        if (state <= 0) continue;

        // 板子必须在 监管→自身 连线中段，不能贴自己或贴监管
        float APx = B[0] - hx, APy = B[1] - hy;
        float t = (APx*ABx + APy*ABy) / ab2;
        if (t < 0.15f || t > 0.85f) continue;

        // 板子中心到连线的垂距，小于板子半长就算挡路
        float projx = hx + ABx * t, projy = hy + ABy * t;
        float dx = B[0] - projx, dy = B[1] - projy;
        float d = sqrtf(dx*dx + dy*dy);
        if (d <= AutoPallet::HALF_LEN + AutoPallet::MARGIN)
            return true;
    }
    return false;
}

// ---- 加载 id -> 类名 表 ----
static void load_id2name(uint64_t sm_obj)
{
    g_id2name.clear();
    uint64_t map_obj = get_obj_field(sm_obj, "state_id_2_class_name");
    if (!PyCore::is_obj(map_obj)) {
        printf("[状态] 未找到 state_id_2_class_name\n");
        return;
    }
    PyCore::Dict mdk;
    if (!PyCore::read_dict(map_obj, mdk)) return;
    for (int64_t i = 0; i < mdk.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(mdk, i));
        uint64_t vp = getPtr64(PyCore::value_slot(mdk, i));
        int id = 0;
        if (!read_pylong(kp, id)) continue;
        char vn[128] = {0};
        if (!PyCore::read_str(vp, vn, sizeof(vn))) continue;
        g_id2name[id] = vn;
    }
    printf("[状态] id2name 表加载 %zu 条\n", g_id2name.size());
}

// ---- 状态 ID -> 中文名 ----
static const char* state_zh(int id, const std::string& cls)
{
    switch (id) {
        case 1:  return "站立";
        case 2:  return "移动";
        case 26: return "落地刀";
        case 27: return "落地刀";
        case 45: return "移动出刀";
        case 46: return "静止出刀";
    }
    std::string s = cls;
    for (auto& c : s) c = (char)::tolower((unsigned char)c);
    if (s.empty()) return nullptr;
    if (s.find("attack") != std::string::npos) {
        if (s.find("jump")   != std::string::npos) return "落地刀";
        if (s.find("charge") != std::string::npos) return "蓄力刀";
        return "出刀";
    }
    if (s.find("idle")     != std::string::npos) return "站立";
    if (s.find("move")     != std::string::npos) return "移动";
    if (s.find("walk")     != std::string::npos) return "行走";
    if (s.find("run")      != std::string::npos) return "奔跑";
    if (s.find("jump_end") != std::string::npos) return "落地";
    if (s.find("jump")     != std::string::npos) return "跳跃";
    if (s.find("hurt")     != std::string::npos ||
        s.find("hit")      != std::string::npos) return "受击";
    if (s.find("stun")     != std::string::npos) return "眩晕";
    if (s.find("die")      != std::string::npos ||
        s.find("dead")     != std::string::npos) return "死亡";
    if (s.find("cast")     != std::string::npos ||
        s.find("skill")    != std::string::npos) return "施法";
    return nullptr;
}

// ---- 门控判定（抽出来给实时/预读两条路径共用）----
// 返回 true = 通过所有门控（或不满足拦截条件）
//   committed = 这一帧监管**已经在出刀状态**里（_state 里有 26/27/45/46）
//   2026-10-26：约瑟夫那类"背身起刀、最后一下才转向"的打法，如果照旧要求
//   "面朝自己"，出刀瞬间永远判不成立 —— 已经挥刀了就不再看向哪边了，
//   反正这一刀该落谁身上是定好的。所以 committed 时跳过朝向门控。
// ============================================================================
//   2026-10-26：**出刀采样器**（fn=atk_trace）
//   为什么要有：现在这批监管（约瑟夫 1862 / 现在这个 2602）的
//   `last_cast_skill_id` 是**恒定**的，`_state` 里也没有 26/27/45/46，
//   两条判据都拿不到信号 -> 回调永不触发 -> 自动技能/飞轮全哑。
//   这里把 100ms 一次的快照（cur_skill / cur_state / _state 前几个 id）
//   只在**变化时**记一行，事后 dump 出来就能看到"他出刀那一刻到底变了什么"。
// ============================================================================
static const int kAtkTraceMax = 64;
static char  g_atk_trace[kAtkTraceMax][112];
static int   g_atk_trace_n = 0, g_atk_trace_head = 0;
static int64_t g_atk_trace_ms = 0;
static int   g_atk_trace_last_skill = -999999;
static int   g_atk_trace_last_state = -999999;
static int   g_atk_trace_last_natk  = -1;

static void atk_trace_push(const char *fmt, ...)
{
    char line[112];
    va_list ap; va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    const int idx = (g_atk_trace_head + g_atk_trace_n) % kAtkTraceMax;
    if (g_atk_trace_n < kAtkTraceMax) ++g_atk_trace_n;
    else g_atk_trace_head = (g_atk_trace_head + 1) % kAtkTraceMax;
    snprintf(g_atk_trace[idx], sizeof(g_atk_trace[idx]), "%s", line);
}

// 把 _state 列表的前几个 id 拼成字符串（看得出"出刀态"到底长什么样）
static void atk_state_ids(uint64_t list_obj, char *out, int cap)
{
    out[0] = 0;
    if (!PyCore::is_obj(list_obj)) return;
    int64_t n = 0; uint64_t items = 0;
    if (!PyCore::read_list(list_obj, n, items)) return;
    int used = 0;
    for (int64_t i = 0; i < n && i < 6; ++i) {
        int64_t v = 0;
        PyCore::read_int(getPtr64(items + i * 8), v);
        used += snprintf(out + used, (size_t)(cap - used), "%s%d", i ? "," : "", (int)v);
        if (used >= cap - 8) break;
    }
}

static bool compute_gate_pass(uint64_t hunter_unit, uint64_t hs, bool committed = false)
{
    float mx = g_max_distance.load();
    bool need_dist   = (mx < 49.9f);
    bool need_facing = g_require_facing.load();
    bool need_board  = g_skip_if_board_between.load();
    if (!need_dist && !need_facing && !need_board) { g_gate_why = 0; return true; }
    if (!hs) { g_gate_why = 5; return true; }   // 监管场景对象读不到，不拦截

    uint64_t self_scene = 0;
    float hx = 0, hy = 0, sx = 0, sy = 0;
    auto read_xy = [](uint64_t obj, float &x, float &y) -> bool {
        if (!PyCore::is_obj(obj)) return false;
        uint64_t c = getPtr64(obj + 0x28);
        if (!PyCore::is_obj(c)) return false;
        float raw[3];
        if (!vm_readv(c + 0xa0, raw, 12)) return false;
        x = raw[0]; y = raw[2];
        return true;
    };
    bool have_pos = PySelf::anchor(self_scene)
                 && read_xy(hs, hx, hy) && read_xy(self_scene, sx, sy);
    if (!have_pos) { g_gate_why = 4; return true; }   // 位置读不到，不拦截

    if (need_dist) {
        float dx = hx - sx, dy = hy - sy;
        float d = sqrtf(dx*dx + dy*dy) / 11.886f;
        g_gate_dist_cm = (int)(d * 100.0f);
        if (d > mx) { g_gate_why = 1; return false; }
    }
    if (need_facing && !committed) {
        if (!is_hunter_facing_self(hunter_unit, hs, hx, hy, sx, sy)) {
            g_gate_why = 2;
            return false;
        }
    }
    if (need_board) {
        if (has_board_between(hx, hy, sx, sy)) { g_gate_why = 3; return false; }
    }
    g_gate_why = 0;
    return true;
}

// ============================================================
// ★ 梦之女巫：所有"有效监管"（排除本体）的槽位
// ============================================================
#define MAX_HUNTER_SLOTS 8

struct HunterSlot {
    uint64_t unit = 0;
    uint64_t scene = 0;   // ★ 新增：unit.model 对应的 scene object
    uint64_t sm   = 0;
    uint64_t skill_mgr = 0;

    int64_t  state_hint     = -1;
    int64_t  skill_mgr_hint = -1;
    int64_t  last_cast_hint = -1;

    int      prev_skill = -1;
    int      cur_state  = -1;
    int      cur_skill  = 0;
    bool     follower   = false;   // ★ 梦之女巫信徒（yidhra_xintu）
    bool     prev_st_atk = false;  // ★ 上一次状态判据是否为"出刀"（用来取上升沿）
    //   2026-10-26：诊断用（fn=dbg_atk）—— 约瑟夫"只有第一刀触发"就看这几项
    int      dbg_now_atk = 0;
    int      dbg_last_digit = -1;
    int64_t  dbg_trig_ms = 0;
    uint64_t st_atk_ms  = 0;       // ★ 上次用状态判据触发的时间（冷却用）

    // ★★ 2026-10-28：A、B 双判据（都要命中才生效）
    uint64_t a_hit_ms     = 0;     // A 判据（技能id变化+末位1/5）最近命中时刻
    bool     a_hit_charge = false; // A 命中时是否蓄力（末位5）
    uint64_t b_edge_ms    = 0;     // B 判据（_state上升沿）最近命中时刻
};

static HunterSlot g_slots[MAX_HUNTER_SLOTS];

static int        g_slot_n = 0;

static void rescan_hunter_slots()
{
    // ★ 同时遍历 type=1 和 type=236
    uint64_t units[24] = {};
    int un = 0;
    // ★ get_all_units_of_type 现在会把"链路读失败"返回 -1（见它的注释），
    //   这里必须只累加真正的正数 —— 否则 un 会被 -1 减掉，监管槽位凭空少一个 ✗
    const int u1 = get_all_units_of_type(1, units + un, 24 - un);
    if (u1 > 0) un += u1;
    if (un < 24) {
        const int u2 = get_all_units_of_type(236, units + un, 24 - un);
        if (u2 > 0) un += u2;
    }

    // 保留旧 hint
    int64_t  old_state_hint[MAX_HUNTER_SLOTS];
    int64_t  old_smgr_hint[MAX_HUNTER_SLOTS];
    int64_t  old_lcast_hint[MAX_HUNTER_SLOTS];
    int      old_prev_skill[MAX_HUNTER_SLOTS];
    for (int i = 0; i < g_slot_n && i < MAX_HUNTER_SLOTS; i++) {
        old_state_hint[i] = g_slots[i].state_hint;
        old_smgr_hint[i]  = g_slots[i].skill_mgr_hint;
        old_lcast_hint[i] = g_slots[i].last_cast_hint;
        old_prev_skill[i] = g_slots[i].prev_skill;
    }

    int new_n = 0;
    for (int i = 0; i < un; i++) {
        uint64_t u = units[i];
        if (u < 0x5000000000ULL || u >= 0x8000000000ULL) continue;

        // ★ 拿 unit.model → scene
        uint64_t scene = py_get_unit_scene(u);
        if (scene < 0x5000000000ULL || scene >= 0x8000000000ULL) {
            continue;
        }

        // ★ 对 scene 拿类名
        std::string cn = py_class_name(scene);
        // ⚠ 这行原来**每次 rescan 都打**，而 rescan 在 slot_n==0 时是每帧调的。
        //   每次 printf+fflush 都是系统调用，热路径上纯属白烧。
        //   现在只在「unit/类名」真的变了的时候才打。
        {
            static uint64_t l_u = 0;
            static std::string l_cn;
            if (l_u != u || l_cn != cn) {
                l_u = u; l_cn = cn;
                printf("[状态] unit=0x%llx scene=0x%llx cls=%s\n",
                       (unsigned long long)u,
                       (unsigned long long)scene,
                       cn.c_str());
                fflush(stdout);
            }
        }

        //   2026-10-24：标记信徒 —— 它们的 skill_id 判据不一定成立，
        //   所以后面会给它们补一条"状态列表"判据（见攻击循环）
        bool is_follower = (cn.find("xintu") != std::string::npos);

        if (is_yidhra_host(cn)) {
            static bool warned = false;
            if (!warned) { warned = true; printf("[状态]   → 女巫本体，跳过\n"); fflush(stdout); }
            continue;
        }

        if (new_n >= MAX_HUNTER_SLOTS) break;

        HunterSlot &s = g_slots[new_n];

    if (new_n < g_slot_n && g_slots[new_n].unit == u) {
    s.prev_st_atk    = g_slots[new_n].prev_st_atk;   // ★ 重扫别把边沿/冷却弄丢
    s.st_atk_ms      = g_slots[new_n].st_atk_ms;
    // ★ 新增：A/B 时间戳跨 rescan 保留
    s.a_hit_ms       = g_slots[new_n].a_hit_ms;
    s.a_hit_charge   = g_slots[new_n].a_hit_charge;
    s.b_edge_ms      = g_slots[new_n].b_edge_ms;
    s.state_hint     = old_state_hint[new_n];
            s.skill_mgr_hint = old_smgr_hint[new_n];
            s.last_cast_hint = old_lcast_hint[new_n];
            s.prev_skill     = old_prev_skill[new_n];
        } else {
            s.state_hint     = -1;
            s.skill_mgr_hint = -1;
            s.last_cast_hint = -1;
            s.prev_skill     = -1;
        }

        s.unit = u;
        s.scene = scene;
        s.follower = is_follower;
        s.sm   = get_obj_field(u, "state_machine");
        s.skill_mgr = get_obj_field(u, "skill_mgr");

        new_n++;
    }

    for (int i = new_n; i < g_slot_n && i < MAX_HUNTER_SLOTS; i++)
        g_slots[i] = HunterSlot{};
    g_slot_n = new_n;

    {
        static int l_n = -1;
        if (l_n != g_slot_n) {          // 同上：只在结果变了才打
            l_n = g_slot_n;
            printf("[状态] rescan 完成 slot_n=%d\n", g_slot_n);
            fflush(stdout);
        }
    }

    if (g_slot_n > 0) {
        g_cached_unit = g_slots[0].unit;
        g_cached_sm   = g_slots[0].sm;
    } else {
        g_cached_unit = 0;
        g_cached_sm   = 0;
    }
}

// ---- 后台线程 ----
static void run_thread()
{
    DisguiseThreadName(12);   // PyAttack（usleep(200)=5000Hz）
    // 直接内联一份，避免 PyAttack.h 依赖 draw_Gui.cpp
{
    struct sched_param sp; sp.sched_priority = 1;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
}
while (!PyRoot::ensure()) usleep(500 * 1000);
    printf("[状态] 监管状态识别线程已启动\n");

    int last_logged = -99999;
    int recheck_counter = 0;
    int64_t state_hint = -1;
    int64_t idle_hint  = -1;
    int64_t model_hint = -1;
    int idle_tick = 0;
    int prev_skill = -1;   // 上一次的 last_cast_skill_id
    bool prev_atk = false;          // ★ 上升沿检测

    while (true) {
                // ========================================================
        // ★ 定期重扫所有监管（含梦之女巫信徒），过滤掉女巫本体
        // ========================================================
        static int rescan_tick = 0;
        if (g_slot_n == 0 || ++rescan_tick >= 500) {
            rescan_tick = 0;
            rescan_hunter_slots();
        }

        if (g_slot_n == 0) {
            g_ready = false;
            g_attacking = false;
            /*   2026-10-24 诊断：每 5 秒报一次"状态线程还活着"，
         *   卡死时这份日志会显示它停在哪一句之前。 */
        {
            static time_t wd_last = 0;
            static unsigned long wd_n = 0;
            ++wd_n;
            time_t now = time(nullptr);
            if (now - wd_last >= 5) {
                wd_last = now;
                printf("[看门狗] 状态线程 轮次=%lu\n", wd_n);
                fflush(stdout);
            }
        }
        usleep(20 * 1000);
            continue;
        }

        // ========================================================
        // ★ 遍历所有候选（每个信徒一个槽），找出"正在出刀"的那个
        // ========================================================
        int any_atk_slot = -1;

        for (int ci = 0; ci < g_slot_n; ci++) {
            HunterSlot &s = g_slots[ci];
            if (!s.sm) continue;

            // ---- 读 state 列表的最后一个 int ----
            uint64_t state_list = get_obj_field_hint(s.sm, "_state", s.state_hint);
            int cur = -1;
            if (read_list_last_int(state_list, cur) && cur >= 0 && cur <= 10000) {
                s.cur_state = cur;
            } else {
                s.cur_state = -1;
            }

            // ---- 读 skill_mgr.last_cast_skill_id ----
            int cur_skill = 0;
            if (!s.skill_mgr) s.skill_mgr = get_obj_field(s.unit, "skill_mgr");
            if (PyCore::is_obj(s.skill_mgr)) {
                uint64_t d = PyCore::get_inst_dict(s.skill_mgr);
                if (d) {
                    PyCore::Dict dk;
                    if (PyCore::read_dict(d, dk)) {
                        s.last_cast_hint = PyCore::resolve_index(
                            d, dk, s.last_cast_hint, "last_cast_skill_id");
                        if (s.last_cast_hint >= 0) {
                            read_pylong(getPtr64(PyCore::value_slot(dk, s.last_cast_hint)),
                                        cur_skill);
                        }
                    }
                }
            }
            s.cur_skill = cur_skill;

            // ============================================================
// ★★ 2026-10-28：A、B 双判据（都必须命中才触发）
//
//   A：技能 id 变化 + 末位 1/5   （瞬时事件）
//   B：_state 出现 26/27/45/46 的上升沿  （瞬时事件）
//
//   两个瞬时事件几乎不会落在同一次 usleep(5000) 轮询里，
//   所以用 200ms 时间窗：A、B 都在这个窗口内命中才算"同一刀"。
//   窗口外的旧命中不会和本次凑成一对。
//
//   ★ prev_st_atk 必须**每帧更新**（不能像老代码那样只在 A 未命中时更新），
//     否则 A 命中的那一帧 B 判据的上升沿就丢了。
// ============================================================
const uint64_t t_now = pyattack_now_ms();

// ---- A 判据（独立计算，不早退）----
//   ⚠ 这里**不设 `a_hit` 局部变量** —— 组合判定用的是 s.a_hit_ms（时间戳），
//     局部 bool 从没被读过，留着只会触发 unused-variable 警告 ✗
if (g_use_skill_id.load()
    && cur_skill > 0
    && cur_skill != s.prev_skill
    && !is_non_attack_skill(cur_skill)) {
    int last_digit = cur_skill % 10;
    s.dbg_last_digit = last_digit;
    if (last_digit == 1 || last_digit == 5) {
        s.a_hit_ms     = t_now;
        s.a_hit_charge = (last_digit == 5);
    }
}

// ---- B 判据（独立计算，每帧更新 prev_st_atk）----
uint64_t sfl = get_obj_field_hint(s.sm, "_state", s.state_hint);
const bool now_atk = read_list_has_attack(sfl);
s.dbg_now_atk = now_atk ? 1 : 0;
const bool b_edge = now_atk && !s.prev_st_atk
                 && (t_now - s.st_atk_ms) >= 700;
if (b_edge) s.b_edge_ms = t_now;
s.prev_st_atk = now_atk;   // ★ 必须每帧更新

// ---- 组合判定：A、B 都在窗口内命中 ----
if (s.a_hit_ms != 0 && s.b_edge_ms != 0) {
    int64_t diff = (int64_t)s.a_hit_ms - (int64_t)s.b_edge_ms;
    if (diff < 0) diff = -diff;
    if (diff <= kDualWindowMs && (t_now - s.st_atk_ms) >= 700) {
        any_atk_slot = ci;
        g_id_hits.fetch_add(1);
        g_edge_hits.fetch_add(1);
        s.st_atk_ms   = t_now;
        s.dbg_trig_ms = t_now;
        g_attack_is_charge.store(s.a_hit_charge);
        // 清掉时间戳，避免同一刀重复触发
        s.a_hit_ms  = 0;
        s.b_edge_ms = 0;
    }
}

            /*   2026-10-26：出刀采样（100ms 一次，只在变化时记一行） */
            {
                const int64_t nowt = pyattack_now_ms();
                if (nowt - g_atk_trace_ms >= 100) {
                    g_atk_trace_ms = nowt;
                    char ids[48] = {0};
                    uint64_t sl2 = get_obj_field_hint(s.sm, "_state", s.state_hint);
                    atk_state_ids(sl2, ids, sizeof(ids));
                    const int natk = read_list_has_attack(sl2) ? 1 : 0;
                    int sig = 0;
                    for (const char *q = ids; *q; ++q) sig = sig * 31 + *q;
                    if (cur_skill != g_atk_trace_last_skill || sig != g_atk_trace_last_state
                        || natk != g_atk_trace_last_natk) {
                        g_atk_trace_last_skill = cur_skill;
                        g_atk_trace_last_state = sig;
                        g_atk_trace_last_natk  = natk;
                        atk_trace_push("t=%lld sk=%d prev=%d nAtk=%d cur_state=%d state=[%s]",
                                       (long long)nowt, cur_skill, s.prev_skill, natk,
                                       s.cur_state, ids);
                    }
                }
            }
            s.prev_skill = cur_skill;
        }

        // ---- 主槽位的 state 用来更新 UI / ready 状态 ----
        g_cur_state = g_slots[0].cur_state;
        g_ready = (g_slots[0].cur_state >= 0);
        g_skillmgr_ready = (g_slots[0].skill_mgr != 0);
        g_last_cast_skill_id = g_slots[0].cur_skill;

        // ---- 用主槽位读 idle（用来触发降频读）----
        {
            static int idle_tick = 0;
            static int64_t idle_hint = -1;
            if (++idle_tick >= 500) {
                idle_tick = 0;
                uint64_t idle_obj = get_obj_field_hint(g_slots[0].sm, "idle_state", idle_hint);
                int idle = -1;
                read_pylong(idle_obj, idle);
                g_idle_state = idle;
            }
        }

        // ========================================================
        // ★ 攻击触发 + 门控
        //   门控用"正在出刀的那个 unit"，而不是全局锁定主监管
        // ========================================================
        //   ★ 2026-10-26：先把"有没有人处于出刀状态"算出来，门控要用
        bool raw_atk_now = false;
        for (int ci = 0; ci < g_slot_n; ci++) {
            uint64_t sl = get_obj_field_hint(g_slots[ci].sm, "_state", g_slots[ci].state_hint);
            if (read_list_has_attack(sl)) { raw_atk_now = true; break; }
        }

        bool atk = (any_atk_slot >= 0);

        if (atk) {
    uint64_t attacker_unit = g_slots[any_atk_slot].unit;
    uint64_t hs = g_slots[any_atk_slot].scene;   // ★ 用 slot 缓存的 scene
    g_atk_unit.store(attacker_unit);              // ★ 门控要用**攻击者**，不能固定用主监管
    g_hunter_scene = hs;

    if (!compute_gate_pass(attacker_unit, hs, raw_atk_now)) {
        atk = false;
    }
}

        g_raw_attacking = false;
        for (int ci = 0; ci < g_slot_n; ci++) {
            uint64_t sl = get_obj_field_hint(g_slots[ci].sm, "_state", g_slots[ci].state_hint);
            if (read_list_has_attack(sl)) {
                g_raw_attacking = true;
                break;
            }
        }

        g_attacking = atk;

        // ---- 上升沿回调 ----
        if (atk && !prev_atk) {
            g_last_attack_ms.store(pyattack_now_ms());
            auto cb = g_attack_cb.load();
            if (cb) { g_cb_hits.fetch_add(1); cb(); }
        }
        prev_atk = atk;

                // ---- 状态名（用主槽位）----
        {
            int cur = g_slots[0].cur_state;
            if (cur != last_logged && cur >= 0) {
                std::string cls;
                auto it = g_id2name.find(cur);
                if (it != g_id2name.end()) cls = it->second;
                const char* zh = state_zh(cur, cls);
                std::lock_guard<std::mutex> lk(g_name_mtx);
                if (zh)                snprintf(g_name_buf, sizeof(g_name_buf), "%s", zh);
                else if (!cls.empty()) snprintf(g_name_buf, sizeof(g_name_buf), "%s", cls.c_str());
                else                   snprintf(g_name_buf, sizeof(g_name_buf), "状态%d", cur);
                last_logged = cur;
            }
        }

// ★ 轮询间隔：200us(5000Hz) -> 2000us(500Hz)（2026-10-10）-> 10000us(100Hz)（2026-10-14）
//   实测这个线程占**单核 39%**，而且是 SCHED_FIFO **实时优先级** ——
//   5000Hz 的实时线程会抢占其他所有线程，代价远超过它需要的信息新鲜度。
//   监管技能状态的变化尺度是**几十毫秒**(g_skill_lead_time_ms 默认 203)，
//   500Hz 的响应是 2ms，仍然远快于需求。
//   ★ 2026-10-14 再降到 100Hz：adb `top -H` 实测本线程 11~14.8% 单核
//     （伪装名 kworker/u8:12），累计 utime=932 / stime=8385 —— **90% 在内核态**，
//     全是驱动 ioctl；100Hz = 10ms 响应，仍远快于"几十毫秒"的状态尺度 ✓
//     它同时还是实时线程，降频也顺带减少了对游戏线程的抢占。
//   要更跟手就把这个数改小（5000=200Hz），但 200us 是明显过量的。
//   ★ 2026-10-14 再降一档：100Hz -> 50Hz
//     现场实测（adb `top -H`，非混淆版）：本线程占 **26.9% 单核**，
//     而且它是 SCHED_FIFO **实时**线程 —— 会抢占游戏与渲染线程 ✗
//     按 0.27ms/次反推，每次循环约 90 次驱动 ioctl ✗
//     而监管技能状态的变化尺度是 g_skill_lead_time_ms 默认 **203ms**，
//     20ms 的粒度仍然远超需求 ✓
//     要更跟手改 10000(100Hz)；**不要**回到 2000(500Hz) ——
//     那是作者实测"占单核 39%"的档位 ✗
usleep(5000);
    }
}
       

        

static void start()
{
    static bool s = false;
    if (s) return;
    s = true;
    std::thread(run_thread).detach();
}


static bool ready()     { return g_ready.load(); }
static bool attacking() { return g_attacking.load(); }
static int  cur_state() { return g_cur_state.load(); }
static int  idle_state(){ return g_idle_state.load(); }
static int  last_cast_skill_id() { return g_last_cast_skill_id.load(); }
static bool skillmgr_ready()     { return g_skillmgr_ready.load(); }
static float max_distance() { return g_max_distance.load(); }
static void  set_max_distance(float m) { g_max_distance.store(m); }
static bool attack_cb_registered() { return g_attack_cb.load() != nullptr; }

static bool  require_facing()             { return g_require_facing.load(); }
static void  set_require_facing(bool v)   { g_require_facing.store(v); }
static float facing_cos_threshold()       { return g_facing_cos_threshold.load(); }
static void  set_facing_cos_threshold(float v) { g_facing_cos_threshold.store(v); }
static bool skip_if_board_between()             { return g_skip_if_board_between.load(); }
static void set_skip_if_board_between(bool v)   { g_skip_if_board_between.store(v); }
static void set_attack_callback(AttackCallback cb) { g_attack_cb.store(cb); }
static uint64_t hunter_unit_raw() { return g_cached_unit; }


static bool use_skill_id() { return g_use_skill_id.load(); }
static void set_use_skill_id(bool v) { g_use_skill_id.store(v); }
static bool raw_attacking() {
    // 出刀上升沿后 1500ms 内都算"在出刀"，覆盖前摇 + 出刀动作
    uint64_t last = g_last_attack_ms.load();
    if (last == 0) return g_raw_attacking.load();
    uint64_t now = pyattack_now_ms();
    return (now - last) < 1500;
}
// 实时门控判定：给外部（延迟复查）用
static bool gate_pass_now() {
    //   2026-10-24：信徒出刀时 g_cached_unit 是**本体**，拿它算面朝会错。
    //   优先用"正在出刀的 unit"。
    uint64_t u = g_atk_unit.load();
    if (!u) u = g_cached_unit;
    return compute_gate_pass(u, g_hunter_scene.load());
}

static bool attack_is_charge() { return g_attack_is_charge.load(); }

// 宿伞传伞光标位置：读 hunter_unit.imper_umbrella_position (math3d.vector)
// 返回 true 时 (x,y,z) 是光标世界坐标
static bool get_umbrella_position(float &x, float &y, float &z) {
    uint64_t hu = g_cached_unit;
    if (!hu) return false;
    uint64_t vec = get_obj_field(hu, "imper_umbrella_position");
    if (!PyCore::is_obj(vec)) return false;
    float vx = 0, vy = 0, vz = 0;
    if (!vm_readv(vec + 0x10, &vx, 4)) return false;
    if (!vm_readv(vec + 0x14, &vy, 4)) return false;
    if (!vm_readv(vec + 0x18, &vz, 4)) return false;
    if (vx == 0 && vy == 0 && vz == 0) return false;
    x = vx; y = vy; z = vz;
    return true;
}

// 读 imper_umbrella_dis（float 字段，PyFloat 对象值在 +0x10）
static float get_umbrella_dis() {
    uint64_t hu = g_cached_unit;
    if (!hu) return 0.f;
    uint64_t p = get_obj_field(hu, "imper_umbrella_dis");
    if (!PyCore::is_obj(p)) return 0.f;
    float fv = 0.f;
    if (!vm_readv(p + 0x14, &fv, 4)) return 0.f;   // ← y 分量，这才是距离
    return fv;
}

// 读宿伞传伞光标：技能管理器里的 move_touch_info
// 返回 true 时 tx,ty = 手指当前屏幕坐标，state = 0/1
static bool get_umbrella_cursor(float &tx, float &ty, int &state) {
    uint64_t hu = g_cached_unit;
    if (!hu) return false;
    uint64_t sm = get_obj_field(hu, "skill_mgr");
    if (!PyCore::is_obj(sm)) return false;
    uint64_t sd = PyCore::get_inst_dict(sm);
    if (!sd) return false;
    PyCore::Dict sdk;
    if (!PyCore::read_dict(sd, sdk)) return false;
    for (int64_t i = 0; i < sdk.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(sdk, i));
        char kname[64] = {0};
        if (!PyCore::read_str(kp, kname, sizeof(kname))) continue;
        if (strcmp(kname, "move_touch_info") != 0) continue;
        uint64_t vp = getPtr64(PyCore::value_slot(sdk, i));
        if (!PyCore::is_obj(vp)) return false;
        float f3[3] = {0};
        if (!vm_readv(vp + 0x10, f3, 12)) return false;
        tx = f3[0]; ty = f3[1]; state = (int)f3[2];
        return true;
    }
    return false;
}

static std::string state_name()
{
    std::lock_guard<std::mutex> lk(g_name_mtx);
    return std::string(g_name_buf);
}

/*   2026-10-26：进程内重新加载 —— 只清绑定缓存，不重启进程、不动驱动 */
inline void ResetBindings()
{
    for (int i = 0; i < MAX_HUNTER_SLOTS; ++i) g_slots[i] = HunterSlot{};
    g_slot_n = 0;
    g_cached_unit = 0;
    g_cached_sm   = 0;
    g_ready = false;
    g_skillmgr_ready = false;
    g_attacking.store(false);
    g_raw_attacking.store(false);
}

} // namespace PyAttack

