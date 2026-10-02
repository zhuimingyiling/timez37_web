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
static std::atomic<bool>  g_require_facing{false};       // 是否要求监管朝向自己
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
static uint64_t g_cached_unit = 0;   // 监管 unit 的地址
static uint64_t g_cached_sm   = 0;   // 监管 unit 的 state_machine 地址
static std::unordered_map<int, std::string> g_id2name;

// ---- 攻击状态 ID（用户实测）----
static inline bool is_attack_id(int id) {
    return id == 26 || id == 27 || id == 45 || id == 46;
}

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
static bool compute_gate_pass(uint64_t hunter_unit, uint64_t hs)
{
    float mx = g_max_distance.load();
    bool need_dist   = (mx < 49.9f);
    bool need_facing = g_require_facing.load();
    bool need_board  = g_skip_if_board_between.load();
    if (!need_dist && !need_facing && !need_board) return true;
    if (!hs) return true;                   // 监管场景对象读不到，不拦截

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
    if (!have_pos) return true;              // 位置读不到，不拦截

    if (need_dist) {
        float dx = hx - sx, dy = hy - sy;
        float d = sqrtf(dx*dx + dy*dy) / 11.886f;
        if (d > mx) return false;
    }
    if (need_facing) {
        if (!is_hunter_facing_self(hunter_unit, hs, hx, hy, sx, sy))
            return false;
    }
    if (need_board) {
        if (has_board_between(hx, hy, sx, sy))
            return false;
    }
    return true;
}

// ---- 后台线程 ----
static void run_thread()
{
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
        // ---- hunter_unit 缓存 ----
        uint64_t hunter_unit = g_cached_unit;
        if (!hunter_unit || recheck_counter >= 500) {
            recheck_counter = 0;
            uint64_t fresh = get_hunter_unit();
            if (fresh != hunter_unit) {
                hunter_unit = fresh;
                g_cached_unit = fresh;
                g_cached_sm   = fresh ? get_obj_field(fresh, "state_machine") : 0;
                g_cached_skill_mgr = 0;
                g_i_skill_mgr = -1;
                g_i_last_cast = -1;
                g_skillmgr_ready = false;
                state_hint = -1;
                idle_hint  = -1;
                model_hint = -1;
                last_logged = -99999;
                prev_skill = -1;
                prev_atk = false;
                if (g_cached_sm) load_id2name(g_cached_sm);
            }
        }
        // ---- SkillManager.last_cast_skill_id：用来区分普通刀/蓄力刀 ----
if (!g_cached_skill_mgr) {
    g_cached_skill_mgr = get_obj_field_hint(hunter_unit, "skill_mgr", g_i_skill_mgr);
}
if (PyCore::is_obj(g_cached_skill_mgr)) {
    uint64_t d = PyCore::get_inst_dict(g_cached_skill_mgr);
    if (d) {
        PyCore::Dict dk;
        if (PyCore::read_dict(d, dk)) {
            g_i_last_cast = PyCore::resolve_index(d, dk, g_i_last_cast, "last_cast_skill_id");
            if (g_i_last_cast >= 0) {
                int v = 0;
                if (read_pylong(getPtr64(PyCore::value_slot(dk, g_i_last_cast)), v)) {
                    g_last_cast_skill_id = v;
                    g_skillmgr_ready = true;
                }
            }
        }
    }
}
        recheck_counter++;

        if (!hunter_unit) { g_ready = false; usleep(200 * 1000); continue; }
        uint64_t sm = g_cached_sm;
        if (!sm) { g_ready = false; usleep(200 * 1000); continue; }

        static int gate_tick = 0;
        // ---- 读 state ----
int cur = -1;
uint64_t state_list = get_obj_field_hint(sm, "_state", state_hint);
if (!read_list_last_int(state_list, cur) || cur < 0 || cur > 10000) {
    g_ready = false;
    usleep(1000);
    continue;
}
g_cur_state = cur;
g_ready = true;

// ---- 读 last_cast_skill_id（每轮都读，捕捉变化） ----
int cur_skill = 0;
if (!g_cached_skill_mgr) {
    g_cached_skill_mgr = get_obj_field_hint(hunter_unit, "skill_mgr", g_i_skill_mgr);
}
if (PyCore::is_obj(g_cached_skill_mgr)) {
    uint64_t d = PyCore::get_inst_dict(g_cached_skill_mgr);
    if (d) {
        PyCore::Dict dk;
        if (PyCore::read_dict(d, dk)) {
            g_i_last_cast = PyCore::resolve_index(d, dk, g_i_last_cast, "last_cast_skill_id");
            if (g_i_last_cast >= 0) {
                read_pylong(getPtr64(PyCore::value_slot(dk, g_i_last_cast)), cur_skill);
                g_last_cast_skill_id = cur_skill;
                g_skillmgr_ready = true;
            }
        }
    }
}

// ---- idle 降频读 ----
if (++idle_tick >= 500) {
    idle_tick = 0;
    uint64_t idle_obj = get_obj_field_hint(sm, "idle_state", idle_hint);
    int idle = -1;
    read_pylong(idle_obj, idle);
    g_idle_state = idle;
}

// ---- 状态机是否显示攻击（持续性，用于到点复查）----
bool state_has_attack = read_list_has_attack(state_list);

// ---- 判定施法：只在状态机没显示攻击时才查 id2name ----
// 状态机说攻击就一定不是施法，绕开 id2name 误判
bool cur_is_casting = false;
if (!state_has_attack) {
    auto it = g_id2name.find(cur);
    if (it != g_id2name.end()) {
        std::string low = it->second;
        for (auto& ch : low) ch = (char)::tolower((unsigned char)ch);
        bool has_attack = low.find("attack") != std::string::npos;
        bool has_cast   = low.find("cast")   != std::string::npos;
        bool has_skill  = low.find("skill")  != std::string::npos;
        if (!has_attack && (has_cast || has_skill))
            cur_is_casting = true;
    }
}

// ---- 出刀上升沿：skill_id 变化 + 末尾 1/5 ----
bool atk = false;
if (g_use_skill_id.load() && !cur_is_casting
    && cur_skill > 0 && cur_skill != prev_skill) {
    int last_digit = cur_skill % 10;
    if (last_digit == 1 || last_digit == 5) {
        atk = true;
        g_attack_is_charge.store(last_digit == 5);
    }
}
prev_skill = cur_skill;

// ---- 持续状态：到点复查用状态机结果，而不是瞬时上升沿 ----
g_raw_attacking = state_has_attack;

// ---- 门控 ----
if (atk) {
    uint64_t hs = 0;
    {
        uint64_t md = get_obj_field_hint(hunter_unit, "model", model_hint);
        if (PyCore::is_obj(md)) hs = PyCore::get_scene_obj(md);
    }
    g_hunter_scene = hs;
    if (!compute_gate_pass(hunter_unit, hs)) atk = false;
}

g_attacking = atk;

// ---- 上升沿回调 ----
if (atk && !prev_atk) {
    g_last_attack_ms.store(pyattack_now_ms());   // 记录时刻
    auto cb = g_attack_cb.load();
    if (cb) cb();
}
prev_atk = atk;

       

        // ---- 名字只在变化时更新 ----
        if (cur != last_logged) {
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

        usleep(200);
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
    return compute_gate_pass(g_cached_unit, g_hunter_scene.load());
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

} // namespace PyAttack