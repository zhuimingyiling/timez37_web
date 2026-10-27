#ifndef IDV_PY_GENIUS_H
#define IDV_PY_GENIUS_H
#include "guard.h"   // GUARD_APP_LOG（日志路径不留明文）

// ============================================================================
// 天赋与辅助特质 —— 走游戏内嵌 CPython 的对象图读取
// ============================================================================
//
// 链路跟 PyProgress.h 完全一样(sys.modules -> game_kernel -> unit_mgr ->
// units_by_type)，只是取的是 1(ButcherUnit)、2(CivilianUnit)、236(梦之信徒，只读冷却) 三个键，
// 读的属性不同。CPython 3.11 的结构布局、防竞态读法、按名字查找+序号缓存
// 这些共通的东西在 PyProgress.h 文件头有完整说明，这里不重复。
//
// 属性：
//   genius_id_lv_lst   list[[天赋id, 等级], ...]
//   support_skill_id   list[id]，监管的辅助特质；求生者也有这个字段
//   model              -> +0x20 = 场景对象指针，用来跟绘制循环里的 obj 配对
//   position           -> +0x10/+0x14/+0x18 = X/高度/Y，model 为空时的兜底
//
// ！！两个必须记住的坑 ！！
//
// 1) **活体属性名和录像里的不一样**：录像快照里是 `genius_id_lvs`，
//    Python 层是 `genius_id_lv_lst`。照录像的名字去查会一个也找不到。
//
// 2) **监管和求生者共用同一套天赋 id 号段，同一个 id 在两边是不同天赋。**
//    尤其 id 26 在求生者侧是绝处逢生，在监管侧是另一个(还没查清的)天赋 ——
//    所以"有绝处逢生就标绿"这条只能对 阵营==2 生效，绝不能按 id 直接判。
//
// 3) **genius_id_lv_lst 经常读不出内容**(PC 侧脚本里表现成空列表或读不到)，
//    而同一时刻隔壁的 support_skill_id 每次都好。**原因还没定论。**
//    最合理的假设是冷热页差异：support_skill_id 是界面一直在用的热数据、结构也浅；
//    genius_id_lv_lst 是十几个嵌套 list，要碰的页多得多，而且开局读一次之后
//    再没人碰，是冷页 —— 内存压力下优先被换进 zram。
//
//    ⚠ 注意这里的读法：本文件的 read_genius() 自己读 ob_size，能分清"真的空"和"读不到"。
//    PC 侧那些脚本用的 pyw.list_items() 把这两种情况都返回 []，**不能拿它判断列表是不是空的**。
//
//    **做法：按 uid 增量累积 + 二次确认**(见 read_genius())。天赋一局之内不会变，所以每轮能读到几条
//    就合并几条进记忆，哪怕每一轮都只读到一部分，记忆也会逐渐补全：
//      - **一轮就读全**(n 条全部合法且 id 不重复)直接判完整 —— 天赋页是冷页，内存压力下
//        被压进 zram 后驱动读不回来，必须趁热一次拿下(2026-09-22 VmSwap 1.1G 时天赋全不显示)
//      - 没读全时，单条 [id, 等级] 要在**两个不同轮次读到相同值**才确认，挡住偶发垃圾(进记忆就是一整局)
//      - 列表长度 ob_size 本身读得稳，记为 n；已确认条数 == n 才算"完整"
//      - 只有"完整"时才能断言"没带某个天赋"；不完整时只能说"已确认带了哪些"
//    旧做法是"单次读到 >=2 个大天赋(求生者还要有 26)才锁存"，把"读全了没有"和"一般人怎么带"
//    混在一起判，导致少带大天赋/没带绝处逢生的人永远锁不上，而且"没带"和"读失败"分不开。
//
//    记忆的清理时机：**unit_mgr 地址变了才清**。uid 每局都从 1000001 重新开始，
//    不清的话下一局会套用上一局同号玩家的天赋，那是实打实的错误信息。
//    而 unit_mgr 实例每局重新分配(见 python-layer.md 的生命周期)，
//    地址一变就说明换局了，这个信号比"连续失败 N 轮"准得多 ——
//    后者在对局中途链路短暂中断(内存压力把 CPython 堆压进 zram)时会误清。
// ============================================================================

#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <thread>
#include <ctime>
#include <atomic>
#include <unistd.h>
#include "PyRoot.h"
#include "PyCore.h"

namespace PyGenius {

// sys.modules 与 dict/int/float/True/False 由 PyRoot.h 运行时找出，这里不再写死模块偏移

static const int MAX_UNITS = 24;   // 1 监管 + 4 求生者 + 梦之信徒(实测一局 5 个) + 余量
static const int BUTCHER_UNIT_TYPE  = 1;
static const int CIVILIAN_UNIT_TYPE = 2;
// 梦之女巫的信徒(YidhraPuppetUnit)。2026-09-22 实测(求生者视角读对面女巫)：每个信徒有**自己的**
// skill_mgr / skill_dict / 711 闪现技能对象，_cd_delta 各自独立递减；support_skill_id 与本体相同([7])。
// 所以按特质找主技能那套逻辑原样适用。信徒不读天赋/绝处逢生，只要冷却
static const int YIDHRA_PUPPET_UNIT_TYPE = 236;

// 绝处逢生的**被动技能 id**(不是天赋 id)。天赋 id 是 26，落地成被动是 102。
// unit.ability_used 是 dict{被动id: bool}，True = 本局已消耗。
// 2026-09-22 实测：非本机的 CivilianUnit 上读到 {102: True}，
// 说明服务器**会把别人的消耗状态下发给本机**，不是只有自己的能看。
// 这个属性只存在于求生者类上，监管(MyButcherUnit)身上根本没有。
static const int PASSIVE_DESPERATE = 102;

// 飞轮效应(求生者天赋 8，被动 207)的**技能 id**。被动配表
// passive_skill_data[(207,1)]['active_skills'] = (11111,)，冷却挂在 skill_dict[11111] 上：
// SkillFlywheelSprint，cd_time 135、game_start_cd 50，_cd_delta 每秒 -1(2026-09-22 注入实测)。
// ⚠ 配表里那个 skill_cd = 10.0 不是飞轮冷却。
// ⚠ 只在本机玩家身上验过；非本机求生者的 skill_dict 里有没有它还没验(那局没人带)。
static const int SKILL_FLYWHEEL = 11111;

// 天赋 id -> 位号。只关心四个分支终端 + 26(求生者的绝处逢生)，其余忽略。
// 两个阵营共用这张 id->位 的映射，名字在格式化时才按阵营区分。
enum {
    BIT_ID_8  = 1 << 0,   // id 8 : 求生=飞轮效应  监管=紧闭空间
    BIT_ID_16 = 1 << 1,  // id 16: 求生=回光返照  监管=底牌
    BIT_ID_24 = 1 << 2,  // id 24: 求生=化险为夷  监管=挽留
    BIT_ID_26 = 1 << 3,  // id 26: 求生=绝处逢生  监管=未知
    BIT_ID_32 = 1 << 4,  // id 32: 求生=膝跳反射  监管=张狂
};

struct Info {
    int      camp;       // 1=监管 2=求生 236=梦之信徒(只有特质冷却，天赋状态恒为未知)
    uint32_t genius_bits;     // 只含**已确认**的条目
    int      support_trait;   // 1..8，0=没读到
    int      genius_state;   // GENIUS_UNKNOWN / GENIUS_PARTIAL / GENIUS_COMPLETE，见 read_genius()
    uint8_t  genius_levels[41]; // 已确认的 {天赋id: 等级}，0 = 未确认或没带。下标 1~40
    bool     desperate_used;   // ability_used[102] == True，本局已经触发过绝处逢生
    // 监管辅助特质的冷却(求生者侧全为 0)
    bool     has_cd;     // false = 没读到技能对象
    float    cd_remain;   // 秒，0 = 就绪。**读到那一刻的值**，显示时用 cd_remain_now() 往下推
    int64_t  cd_read_ms;   // now_ms() 时钟，配合 冷却剩余 做推算
    float    cd_rate;   // skill_mgr.cd_rate，实测恒 1；推算时乘上它
    float    cd_total;   // cd_time，画进度环用
    int      charge_max;   // power_num，非充能型为 0
    int      charge_cur;   // _cur_power_num
    // 求生者飞轮效应的冷却(skill_dict[SKILL_FLYWHEEL])，监管侧全为 0
    bool     has_flywheel;     // skill_dict 里有飞轮技能且读到了 _cd_delta
    float    flywheel_remain;   // 秒，0 = 就绪。读到那一刻的值
    int64_t  flywheel_read_ms;   // now_ms() 时钟
    float    flywheel_rate;   // skill_mgr.cd_rate
};

struct Entry {
    uint64_t scene_obj;

    // 对应的 CPython Unit 对象地址
    // 供自动技能读取 skill_mgr / skill_dict
    uint64_t unit_obj;

    float    x, y;

    Info     info;
};

struct OwnSkillCD {
    bool has = false;

    int64_t skill_id = 0;

    float remain = 0.f;
    float total = 0.f;

    int charge_max = 0;
    int charge_cur = 0;

    int64_t read_ms = 0;
};

// 双缓冲发布：写线程填非活跃缓冲，填完再翻转，绘制线程永远读到完整的一份
static Entry g_buf[2][MAX_UNITS];
static volatile int g_count[2] = {0, 0};
// 花名册(见 read_roster())跟着同一次翻转发布。局外没有单位，g_count 会是 0，但花名册仍有内容
static const int MAX_ROSTER = 8;
struct RosterEntry {
    char     uid[24];      // 真人的账号 id 字符串；人机为空串
    int64_t  eid;          // 人机的 eid(int)；真人是 ObjectId，读不成 int 时为 -1
    int      role_type;    // 1=监管 2=求生
    int      pid;          // 角色 id
    uint32_t bits;         // 终端天赋位(监管恒为 0，游戏不公开监管天赋)
    int      seat;         // 准备界面座位号 1..4(UIRoomWait.uid2pos)，0 = 不知道。只有准备阶段求生者条目填
};
static RosterEntry g_roster_buf[2][MAX_ROSTER];
static volatile int g_roster_cnt[2] = {0, 0};
static RosterEntry g_civ_buf[2][MAX_ROSTER];
static volatile int g_civ_cnt[2] = {0, 0};
static std::atomic<int> g_active{0};      // 见 PyProgress.h 同名变量的注释(volatile 不保证发布顺序)
// 本机在准备房间里是监管(UIRoomWait.role == 1)。每轮重算，没有面板/读不到一律 false。
// 准备阶段的求生者天赋只在这个为真时显示(用户要求：自己是求生者时不需要)
static std::atomic<bool> g_prep_as_hunter{false};
//   ★ 2026-10-21：准备界面读到的 role 原值（-1 = 还没读到），用于诊断"我到底被判成什么"
static std::atomic<int>  g_prep_role{-1};
//   ★ 2026-10-21：上次确认真在准备界面的时刻（毫秒），用于给"面板重建"留宽限
static int64_t g_prep_last_ms = 0;
static inline int64_t prep_now_ms()
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// 天赋锁存：genius_id_lv_lst 会间歇性读空(见文件头第 3 条)，按 uid 记住最后一次非空的结果
enum { GENIUS_UNKNOWN = 0, GENIUS_PARTIAL = 1, GENIUS_COMPLETE = 2 };

// 每个玩家的天赋记忆(按 uid)，增量累积，见 read_genius()
struct GeniusMemory {
    int64_t uid;
    int     n;              // genius_id_lv_lst 的长度(ob_size)，0 = 还没读到过
    int     confirmed;
    uint8_t levels[41];       // 已确认的等级，0 = 未确认
    uint8_t candidates[41];       // 读到过一次、等待第二次确认的等级
};
static GeniusMemory g_mem[MAX_UNITS];
static int      g_mem_count = 0;
static uint64_t g_last_unit_mgr = 0;    // 地址一变就说明换局了，此时才清记忆

static uintptr_t g_libbase = 0;
static uint64_t  g_module_dict = 0;
static int64_t   g_i_unit_mgr = -1, g_i_ubt = -1;


// ★ 2026-10-22：units_by_type 的查找单独存一份 hint。
//   之前三处都传 -1，等于**每次强制全扫**（上千条指针比较）。
static int64_t g_hint_ubt = -1;
static int64_t   g_i_genius = -1, g_i_support = -1, g_i_model = -1, g_i_pos = -1, g_i_uid = -1;
static int64_t   g_i_unique = -1;   // unit.unique_id，用来和花名册配对
static int64_t   g_i_ability = -1;   // ability_used，只有求生者类上有
// 监管技能冷却链：unit.skill_mgr -> skill_dict{id: Skill} / leader_skills(set)
static int64_t   g_i_skillmgr = -1, g_i_skilldict = -1, g_i_cdrate = -1;
// Skill 对象上的属性(223 条)，各自一套缓存
static int64_t   g_i_cd = -1, g_i_cdtime = -1, g_i_power = -1, g_i_curpower = -1;

static void clear_mem(GeniusMemory &m, int64_t uid)
{
    memset(&m, 0, sizeof(m));
    m.uid = uid;
}

// 取某个 uid 的记忆，没有就新建；表满了返回 nullptr
static GeniusMemory *get_mem(int64_t uid)
{
    for (int i = 0; i < g_mem_count; i++)
        if (g_mem[i].uid == uid) return &g_mem[i];
    if (g_mem_count >= MAX_UNITS) return nullptr;
    clear_mem(g_mem[g_mem_count], uid);
    return &g_mem[g_mem_count++];
}
static volatile bool g_available = false;
static volatile int  g_fail_streak = 0;
static char g_status[96] = "未启动";

// ---- 公共 CPython 工具：实现在 PyCore.h，三个模块共用一份(见那里的文件头) ----
using PyCore::is_obj; using PyCore::str_equals; using PyCore::Dict; using PyCore::read_dict;
using PyCore::key_slot; using PyCore::value_slot; using PyCore::find_key; using PyCore::get_attr;
using PyCore::resolve_index; using PyCore::get_type_list; using PyCore::read_int; using PyCore::read_list;
using PyCore::get_inst_dict; using PyCore::read_str;

// 冷却是 0~600 秒级(闪现 csv_cd_time 就 150)，**不能套进度那条 0~100 的闸**
static inline bool read_float(uint64_t slot, float &out, float lo, float hi) { return PyCore::read_number(slot, out, lo, hi); }

static bool resolve_module()
{
    const char *why = "";
    g_module_dict = PyCore::game_kernel_dict(why);
    if (!g_module_dict) { snprintf(g_status, sizeof(g_status), "%s", why); return false; }
    return true;
}

// 读线程和绘制线程共用的单调时钟，冷却推算用
static inline int64_t now_ms()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// 天赋 id 的合法等级。来自配表(genius-table.md 的 lv 列)，求生者和监管这组 id 完全相同：
// 层 2.2 和 3.1/3.2/3.3 的节点可加 1~3 级，其余(分支根、2.1/2.3、终端、33~40)只有 1 级。
// 用来挡垃圾：读到的 [id, 等级] 不在配表允许范围里就不认。
static bool level_valid(int64_t id, int64_t lv)
{
    if (id < 1 || id > 40) return false;
    if (lv == 1) return true;
    if (lv < 1 || lv > 3) return false;
    if (id > 32) return false;                        // 33~40 只有 1 级
    int r = (int)(id % 8);
    return r == 3 || r == 5 || r == 6 || r == 7;      // 3/5/6/7、11/13/14/15、… 可加到 3 级
}

// genius_id_lv_lst 是 list[list[id, lv]]。每轮读一次，**增量合并**进这个玩家的记忆：
//   - 某一轮一次读全(合法不重复条数 == n)就直接判完整，见函数里的说明。
//   - 没读全时：每条 [id, 等级] 第一次读到进"候选"，之后某一轮读到相同值才"确认"(挡偶发垃圾)。
//     候选和再读到的值不一致，就用新值重新当候选，不确认。
//   - 列表长度 n 记在记忆里；已确认条数 == n 就是完整。
//   - n 变了(同一局同一个人不该变)说明记忆不可信，整份重来。
//   - 读到空列表(n==0)不动记忆：天赋一局不会变，空只可能是还没同步或已被清理。
// 返回 false = 这一轮连列表头都没读到；记忆不受影响。
static bool read_genius(uint64_t slot, GeniusMemory &mem)
{
    uint64_t lp = getPtr64(slot);
    int64_t n = 0; uint64_t items = 0;
    if (!read_list(lp, n, items)) return false;
    if (getPtr64(slot) != lp) return false;             // 解引用期间被重新赋值了
    if (n == 0) return true;
    if (n > 40) return false;                         // 天赋一共就 40 个，再多一定是垃圾

    if (mem.n != 0 && mem.n != (int)n) clear_mem(mem, mem.uid);
    mem.n = (int)n;

    // 先把这一轮读到的合法条目收齐，再决定怎么合并
    uint8_t this_round[41] = {0};
    int     this_round_n = 0;
    for (int64_t i = 0; i < n; i++) {
        uint64_t pair = getPtr64(items + i * 8);
        int64_t m = 0; uint64_t pit = 0;
        if (!read_list(pair, m, pit) || m != 2) continue;  // 单个条目读不到就跳过，下一轮再补
        int64_t id = 0, lv = 0;
        if (!read_int(getPtr64(pit), id)) continue;
        if (!read_int(getPtr64(pit + 8), lv)) continue;
        if (!level_valid(id, lv)) continue;
        if (this_round[id]) continue;                         // 同一轮里重复出现，不算两次
        this_round[id] = (uint8_t)lv;
        this_round_n++;
    }

    // 一轮就读全(合法且不重复的条数 == n)：直接判完整，不等第二轮。
    // 天赋页是冷页，开局读一次后游戏不再碰，内存压力下会被压进 zram 且再也换不回来；
    // 页还热的时候一次拿下，比等两轮确认可靠得多。垃圾值要同时满足
    // "n 条全部是合法 [id, 等级]、id 互不重复"几乎不可能。
    // 跟已确认的记忆冲突，说明有一方是垃圾：整份重来，这一轮的值只当候选。
    if (this_round_n == (int)n) {
        bool conflict = false;
        for (int id = 1; id <= 40; id++)
            if (mem.levels[id] != 0 && mem.levels[id] != this_round[id]) { conflict = true; break; }
        if (conflict) {
            clear_mem(mem, mem.uid);
            mem.n = (int)n;
            memcpy(mem.candidates, this_round, sizeof(mem.candidates));
        } else {
            memcpy(mem.levels, this_round, sizeof(mem.levels));
            mem.confirmed = (int)n;
        }
        return true;
    }

    // 没读全：逐条增量合并，两个不同轮次读到相同值才确认
    for (int id = 1; id <= 40; id++) {
        int lv = this_round[id];
        if (lv == 0) continue;
        if (mem.levels[id] != 0) continue;                 // 已确认，天赋一局不变
        if (mem.candidates[id] == (uint8_t)lv) {              // 两个不同轮次读到相同值 -> 确认
            mem.levels[id] = (uint8_t)lv;
            mem.confirmed++;
        } else {
            mem.candidates[id] = (uint8_t)lv;
        }
    }
    // 确认的比列表还多，说明有垃圾混进来了，整份重来
    if (mem.confirmed > mem.n) clear_mem(mem, mem.uid);
    return true;
}

// 记忆 -> 显示用的位掩码 / 状态
static uint32_t mem_bits(const GeniusMemory &mem)
{
    uint32_t bits = 0;
    if (mem.levels[8])  bits |= BIT_ID_8;
    if (mem.levels[16]) bits |= BIT_ID_16;
    if (mem.levels[24]) bits |= BIT_ID_24;
    if (mem.levels[26]) bits |= BIT_ID_26;
    if (mem.levels[32]) bits |= BIT_ID_32;
    return bits;
}

static int mem_state(const GeniusMemory &mem)
{
    if (mem.confirmed == 0) return GENIUS_UNKNOWN;
    return (mem.n > 0 && mem.confirmed == mem.n) ? GENIUS_COMPLETE : GENIUS_PARTIAL;
}

// ability_used = dict{被动技能id: bool}。找键 102(绝处逢生)，值为 True 就是本局已消耗。
// 键不在表里 = 还没用过(实测没用过的人这个 dict 是空的 {})，所以"找不到"要返回 false 而不是失败。
static bool read_desperate_used(uint64_t slot)
{
    uint64_t dp = getPtr64(slot);
    if (!is_obj(dp) || getPtr64(dp + 8) != PyRoot::g_dict_type) return false;
    Dict k;
    if (!read_dict(dp, k)) return false;
    for (int64_t i = 0; i < k.n_entries; i++) {
        int64_t id = 0;
        if (!read_int(getPtr64(key_slot(k, i)), id) || id != PASSIVE_DESPERATE) continue;
        uint64_t vp = getPtr64(value_slot(k, i));
        if (vp == PyRoot::g_true) return true;
        if (vp == PyRoot::g_false) return false;
        // 单例比不上就按 PyLong 布局退化解，热更挪了单例地址也还能用
        uint8_t b[32];
        if (!vm_readv(vp, b, 32)) return false;
        int64_t sz = 0; memcpy(&sz, b + 16, 8);
        uint32_t dg = 0; memcpy(&dg, b + 24, 4);
        return sz != 0 && dg != 0;
    }
    return false;
}

// 监管当前辅助特质的剩余冷却。链路见 idv-replay-genius 第四节：
//   unit.skill_mgr -> skill_dict{技能id: Skill}
//   Skill._cd_delta  = 剩余秒数，**不在冷却中时恒为 0**(所以 0 就是"就绪")
//   充能型(窥视者)：power_num=最大层数 _cur_power_num=当前层数 _cd_delta=距下一层回满
//
// ！！不能用 leader_skills + is_leader_skill 筛！！(旧实现就是这么写的，结果永远"就绪")
// leader_skills 里混着角色自身技能(红夫人 2601 普攻 lead=True、_cd_delta 恒 0)，
// is_leader_skill=True 的也不止一个(窥视者 25732/25733 都是，但只有 25732 在走)。
//
// 特质 -> 技能 id 来自游戏配表 GK.DM.support_skill_data[特质][handheld_id]['skill_id']
// (2026-09-22 注入 dump，原始数据存档在 逆向存档\2026.0917_b6f53566\captures\support_skill_data.txt)。
// 同一特质按监管角色(handheld_id)有不同的技能 id，而且**没有统一规律**：
//   传送 多数 x741/x743，但也有 12644、13718、3844……；移形每个角色都不同(8802、8811、8883……)
// 每组的**第一个 id 是主技能**，已实测：窥视者 732(733 的 _cd_delta 恒 30)、闪现 711、
// 传送 741(与 743 共享冷却)。这里把每个特质下所有角色的主技能 id 收成一个集合，
// 在 skill_dict 里找落在集合里的那个 —— 一个监管身上只挂自己那一版，不会撞。
// support_skill_id 是实时值(底牌换完立刻变)，旧特质那些冻结的技能对象不在当前集合里，自然不会被选中。
// ⚠️ 热更上新监管时这张表会缺新角色的 id：表现是该角色只显示特质名、不显示冷却，不会给错值。
//    重新 dump：注入 probe2，命令通道里读 GK.DM.support_skill_data。
static const int32_t MAIN_SKILL_LISTEN[]   = {701};
static const int32_t MAIN_SKILL_ABNORMAL[]   = {751};
static const int32_t MAIN_SKILL_EXCITEMENT[]   = {722};
static const int32_t MAIN_SKILL_PATROLLER[] = {761, 763, 765, 3850, 9761, 10761, 11761, 12650, 13450, 13724, 13761, 14050,
                                      14559, 14738, 15159, 15359, 15569, 17761, 21761, 22761, 23761, 24761, 25761,
                                      26761, 27761, 28761, 29761, 30761, 31761, 32761, 34761, 35761, 125750, 128750,
                                      136850, 143750};
static const int32_t MAIN_SKILL_TELEPORT[]   = {741, 3741, 3844, 8741, 9741, 10741, 12644, 13444, 13718, 13741, 14044, 14553,
                                      14732, 15153, 15353, 15563, 17741, 21741, 22741, 23741, 24741, 25741, 26741,
                                      27741, 28741, 29741, 30741, 31741, 32741, 34741, 35741, 125744, 128744, 136844,
                                      143744};
static const int32_t MAIN_SKILL_PEEPER[] = {732, 735, 738, 3841, 9732, 10732, 11732, 12641, 13441, 13715, 13732, 14041,
                                      14550, 14729, 15150, 15350, 15560, 17732, 21732, 22732, 23732, 24732, 25732,
                                      26732, 27732, 28732, 29732, 30732, 31732, 32732, 34732, 35732, 125741, 128741,
                                      136841, 143741};
static const int32_t MAIN_SKILL_BLINK[]   = {711};
static const int32_t MAIN_SKILL_SHIFT[]   = {8802, 8805, 8808, 8811, 8814, 8817, 8820, 8823, 8826, 8829, 8832, 8835, 8838,
                                      8841, 8844, 8847, 8850, 8853, 8856, 8859, 8862, 8865, 8868, 8871, 8874, 8877,
                                      8880, 8883, 8886, 8890, 8893, 8896, 14562, 14742, 15162, 15362, 15572, 143753};

#define PICK_TABLE(a) do { p = a; n = (int)(sizeof(a) / sizeof(a[0])); } while (0)
static bool is_trait_main_skill(int trait, int64_t sid)
{
    const int32_t *p = nullptr; int n = 0;
    switch (trait) {
        case 1: PICK_TABLE(MAIN_SKILL_LISTEN);   break;
        case 2: PICK_TABLE(MAIN_SKILL_ABNORMAL);   break;
        case 3: PICK_TABLE(MAIN_SKILL_EXCITEMENT);   break;
        case 4: PICK_TABLE(MAIN_SKILL_PATROLLER); break;
        case 5: PICK_TABLE(MAIN_SKILL_TELEPORT);   break;
        case 6: PICK_TABLE(MAIN_SKILL_PEEPER); break;
        case 7: PICK_TABLE(MAIN_SKILL_BLINK);   break;
        case 8: PICK_TABLE(MAIN_SKILL_SHIFT);   break;
        default: return false;
    }
    for (int i = 0; i < n; i++) if (p[i] == sid) return true;
    return false;
}

// unit.skill_mgr -> skill_dict 的条目表。监管和求生者共用。
// 速率 = skill_mgr.cd_rate：实测恒为 int 1，但名字说明它能变(加速冷却类效果)，读不到就按 1
static bool get_skill_dict(uint64_t d, const Dict &dk, Dict &sdk, float &rate)
{
    rate = 1.f;
    g_i_skillmgr = resolve_index(d, dk, g_i_skillmgr, "skill_mgr");
    if (g_i_skillmgr < 0) return false;
    uint64_t sm = getPtr64(value_slot(dk, g_i_skillmgr));
    if (!is_obj(sm)) return false;
    uint64_t smd = getPtr64(sm - 0x18);
    Dict smk;
    if (!read_dict(smd, smk)) return false;

    g_i_skilldict = resolve_index(smd, smk, g_i_skilldict, "skill_dict");
    if (g_i_skilldict < 0) return false;
    g_i_cdrate = resolve_index(smd, smk, g_i_cdrate, "cd_rate");
    if (g_i_cdrate >= 0 && !read_float(value_slot(smk, g_i_cdrate), rate, 0.01f, 10.f)) rate = 1.f;
    uint64_t sd = getPtr64(value_slot(smk, g_i_skilldict));
    return is_obj(sd) && getPtr64(sd + 8) == PyRoot::g_dict_type && read_dict(sd, sdk);
}

// 读一个 Skill 对象的冷却。_cd_delta 读不到返回 false(不显示冷却，不猜)，其余几项读不到就保持原值
static bool read_skill_cd(uint64_t sk, float &remain, float &total, int &charge_max, int &charge_cur)
{
    if (!is_obj(sk)) return false;
    uint64_t skd = getPtr64(sk - 0x18);
    Dict skk;
    if (!read_dict(skd, skk)) return false;

    g_i_cd       = resolve_index(skd, skk, g_i_cd,       "_cd_delta");
    g_i_cdtime   = resolve_index(skd, skk, g_i_cdtime,   "cd_time");
    g_i_power    = resolve_index(skd, skk, g_i_power,    "power_num");
    g_i_curpower = resolve_index(skd, skk, g_i_curpower, "_cur_power_num");
    if (g_i_cd < 0) return false;
    if (!read_float(value_slot(skk, g_i_cd), remain, -0.5f, 600.0f)) return false;

    if (g_i_cdtime >= 0) read_float(value_slot(skk, g_i_cdtime), total, 0.f, 600.f);
    int64_t v = 0;
    if (g_i_power    >= 0 && read_int(getPtr64(value_slot(skk, g_i_power)),    v) && v > 0 && v < 16) charge_max = (int)v;
    if (g_i_curpower >= 0 && read_int(getPtr64(value_slot(skk, g_i_curpower)), v) && v >= 0 && v < 16) charge_cur = (int)v;
    return true;
}

static void collect_hunter_skill(uint64_t d, const Dict &dk, int trait, Info &res)
{
    if (trait < 1 || trait > 8) return;
    Dict sdk; float rate;
    if (!get_skill_dict(d, dk, sdk, rate)) return;

    for (int64_t i = 0; i < sdk.n_entries; i++) {
        int64_t sid = 0;
        if (!read_int(getPtr64(key_slot(sdk, i)), sid)) continue;
        if (!is_trait_main_skill(trait, sid)) continue;          // 先按 id 筛，只解那一个技能对象
        float cd = 0.f;
        if (!read_skill_cd(getPtr64(value_slot(sdk, i)), cd, res.cd_total, res.charge_max, res.charge_cur)) return;
        res.cd_remain = cd;
        res.cd_read_ms = now_ms();
        res.cd_rate = rate;
        res.has_cd   = true;
        return;                                        // 一个监管只挂一个主技能
    }
}

// 求生者的飞轮效应冷却。skill_dict 里没有 11111 = 没带飞轮(或这一轮没读到)，什么都不填
static void collect_flywheel(uint64_t d, const Dict &dk, Info &res)
{
    Dict sdk; float rate;
    if (!get_skill_dict(d, dk, sdk, rate)) return;

    for (int64_t i = 0; i < sdk.n_entries; i++) {
        int64_t sid = 0;
        if (!read_int(getPtr64(key_slot(sdk, i)), sid) || sid != SKILL_FLYWHEEL) continue;
        float cd = 0.f, total = 0.f; int full = 0, cur = 0;
        if (!read_skill_cd(getPtr64(value_slot(sdk, i)), cd, total, full, cur)) return;
        res.flywheel_remain = cd;
        res.flywheel_read_ms = now_ms();
        res.flywheel_rate = rate;
        res.has_flywheel   = true;
        return;
    }
}

// ============================================================================
// 读 unit 的所有技能 CD（监管主动技能 + 求生道具/技能通用）
// ============================================================================
// 数据源跟辅助特质那条链完全同源：
//   unit.skill_mgr -> skill_dict{技能id: Skill}
//   Skill._cd_delta  剩余秒(0=就绪)  cd_time 总秒  power_num/_cur_power_num 充能层数
// 实测(2026-09-27 dump)：
//   MyButcherUnit.skill_mgr.skill_dict  22 个 key（含主动+辅助特质+被动）
//   MyCivilianUnit.skill_mgr.skill_dict  6 个 key（含飞轮/治疗/弓箭手 14404 等）
// 这里不分类、全返回，调用方按 skill_id 自己过滤。

struct SkillCDEntry {
    int64_t skill_id;
    float   remain;       // _cd_delta
    float   total;        // cd_time
    int     charge_max;   // power_num（非充能型=0）
    int     charge_cur;   // _cur_power_num
    int64_t read_ms;      // 读到的时刻，绘制时用它推算
};
static const int MAX_SKILLS_CD = 32;

// 从一个 managed object 的 __dict__ 里取字段（带 hint，省一半驱动读）
static uint64_t get_unit_field(uint64_t obj, const char *name, int64_t &hint)
{
    if (!is_obj(obj)) return 0;
    uint64_t d = get_inst_dict(obj);
    if (!d) return 0;
    Dict dk;
    if (!read_dict(d, dk)) return 0;
    int64_t idx = resolve_index(d, dk, hint, name);
    if (idx < 0) return 0;
    hint = idx;
    return getPtr64(value_slot(dk, idx));
}

// 遍历 unit.skill_mgr.skill_dict，把每个技能的 CD 读出来。
// 返回条数；0 = 读失败/没技能。out 数组的 cap 上限 MAX_SKILLS_CD。
static int collect_all_skill_cd(uint64_t unit_obj, SkillCDEntry *out, int cap)
{
    if (!is_obj(unit_obj) || !out || cap <= 0) return 0;
    static int64_t h_sm = -1, h_sd = -1;   // 只读线程碰，不用锁

    uint64_t sm = get_unit_field(unit_obj, "skill_mgr", h_sm);
    if (!is_obj(sm)) return 0;
    uint64_t sd = get_unit_field(sm, "skill_dict", h_sd);
    if (!is_obj(sd)) return 0;

    Dict sdk;
    if (!read_dict(sd, sdk)) return 0;

    int n = 0;
    for (int64_t i = 0; i < sdk.n_entries && n < cap; i++) {
        int64_t sid = 0;
        if (!read_int(getPtr64(key_slot(sdk, i)), sid)) continue;
        uint64_t sk = getPtr64(value_slot(sdk, i));
        if (!is_obj(sk)) continue;

        float remain = 0.f, total = 0.f;
        int cmax = 0, ccur = 0;
        if (!read_skill_cd(sk, remain, total, cmax, ccur)) continue;

        out[n].skill_id   = sid;
        out[n].remain     = remain;
        out[n].total      = total;
        out[n].charge_max = cmax;
        out[n].charge_cur = ccur;
        out[n].read_ms    = now_ms();
        n++;
    }
    return n;
}

// ============================================================================
// 根据 scene_obj 找当前单位的原始 Unit 对象
// ============================================================================

static uint64_t find_unit_by_scene(uint64_t scene_obj)
{
    if (!scene_obj) return 0;

    int b = g_active;
    int n = g_count[b];

    for (int i = 0; i < n; i++) {
        if (g_buf[b][i].scene_obj == scene_obj) {
            // 这里需要 Entry 里保存 unit_obj
            return g_buf[b][i].unit_obj;
        }
    }

    return 0;
}

// 2026-10-27：给共享地图（朝向读取）用 ——
//   `data[i].obj` 是场景对象，要反查对应的 Python unit 才能读 __dict__ 属性。
inline uint64_t unit_of_scene(uint64_t scene_obj) { return find_unit_by_scene(scene_obj); }


// 放在 collect_all_skill_cd 后面
static uint64_t debug_get_unit_field(uint64_t obj, const char *name)
{
    int64_t h = -1;
    return get_unit_field(obj, name, h);
}

// 按技能 id 找 CD 条目，没找到返回 nullptr
static const SkillCDEntry *find_skill_cd(const SkillCDEntry *list, int n, int64_t sid)
{
    for (int i = 0; i < n; i++) if (list[i].skill_id == sid) return &list[i];
    return nullptr;
}

// ============================================================================
// 当前操控单位的普通技能 CD
// ============================================================================
//
// scene_obj -> units_by_type 中对应 CivilianUnit
//
// exclude_skill_id:
//   11111 = 飞轮，自动技能读取时排除
//
// 返回：
//   found = 找到了一个非飞轮技能
//   ready = _cd_delta <= 0.05
//
// 注意：这里只负责读取，不负责决定是否使用。
// ============================================================================



// 从读到的值按经过时间往下推（跟辅助特质那套一致）
static float skill_cd_remain_now(const SkillCDEntry &e, float rate = 1.f)
{
    if (e.remain <= 0.05f) return 0.f;
    float r = e.remain - (float)(now_ms() - e.read_ms) / 1000.f * rate;
    return r > 0.f ? r : 0.f;
}

static bool get_civilian_last_cast_skill(
    uint64_t unit_obj,
    int64_t &skill_id,
    bool &is_none)
{
    skill_id = 0;
    is_none = false;

    if (!is_obj(unit_obj))
        return false;

    static int64_t h_skill_mgr = -1;
    static int64_t h_last_cast = -1;

    uint64_t skill_mgr =
        get_unit_field(
            unit_obj,
            "skill_mgr",
            h_skill_mgr
        );

    if (!is_obj(skill_mgr))
        return false;

    /*
     * SkillManagerCivilian.last_cast_skill_id
     */
    uint64_t value =
        get_unit_field(
            skill_mgr,
            "last_cast_skill_id",
            h_last_cast
        );

    if (!value) {
        /*
         * Python None / 空值
         */
        is_none = true;
        return true;
    }

    int64_t sid = 0;

    if (read_int(value, sid)) {
        skill_id = sid;
        return true;
    }

    /*
     * 能拿到字段，但不是整数，
     * 这里按照 None 处理。
     */
    is_none = true;
    return true;
}

static int64_t get_civilian_active_skill_id(
    uint64_t unit_obj)
{
    if (!is_obj(unit_obj))
        return 0;

    SkillCDEntry list[MAX_SKILLS_CD];

    int count =
        collect_all_skill_cd(
            unit_obj,
            list,
            MAX_SKILLS_CD
        );

    /*
     * 调香师
     */
    for (int i = 0; i < count; i++) {
        if (list[i].skill_id == 3017)
            return 3017;
    }

    /*
     * 教授
     */
    for (int i = 0; i < count; i++) {
        if (list[i].skill_id == 12201)
            return 12201;
    }

    return 0;
}

static bool get_own_skill_cd(
    uint64_t scene_obj,
    OwnSkillCD &out)
{
    out = OwnSkillCD{};

    if (!scene_obj)
        return false;

    int b =
        g_active.load(
            std::memory_order_acquire
        );

    int n = g_count[b];

    for (int i = 0; i < n; i++) {

        if (g_buf[b][i].scene_obj != scene_obj)
            continue;

        uint64_t unit_obj =
            g_buf[b][i].unit_obj;

        if (!is_obj(unit_obj))
            return false;

        /*
         * 找当前角色真正的主动技能。
         *
         * 目前已经确认：
         * 调香师 = 3017
         * 教授   = 12201
         */
        int64_t active_skill =
            get_civilian_active_skill_id(
                unit_obj
            );

        if (active_skill == 0) {
            printf(
                "[技能] 未识别主动技能\n"
            );
            return false;
        }

        /*
         * 读取 SkillManagerCivilian
         * 的 last_cast_skill_id
         */
        int64_t last_skill = 0;
        bool is_none = false;

        bool ok =
            get_civilian_last_cast_skill(
                unit_obj,
                last_skill,
                is_none
            );

        if (!ok)
            return false;

        out.has = true;
        out.skill_id = active_skill;
        out.total = 0.f;
        out.charge_max = 0;
        out.charge_cur = 0;
        out.read_ms = now_ms();

        /*
         * last_cast_skill_id == 当前技能
         *
         * = 技能正在 CD
         *
         * None
         *
         * = 技能已经好了
         */
        if (!is_none &&
            last_skill == active_skill) {

            out.remain = 1.f;
        }
        else {
            out.remain = 0.f;
        }

        printf(
            "[技能判断] skill=%lld "
            "last_cast=%lld "
            "none=%d "
            "ready=%d\n",
            (long long)active_skill,
            (long long)last_skill,
            is_none ? 1 : 0,
            out.remain <= 0.05f ? 1 : 0
        );

        return true;
    }

    return false;
}

// ============================================================================
// 花名册 GK.g_avatar.player_data_list —— 大天赋的**权威来源**(2026-09-23 注入实测)
// ============================================================================
// 每条是一个 dict：{role_type(1监管/2求生), eid, pid, final_genius[终端天赋], support_skill_id,
//                   player_name, bot, cloth_id, is_ready, uid(真人才有)}
// 为什么改走这里：genius_id_lv_lst 是十几个嵌套 list 的冷页，内存压力下整片读不出(待办 #7)，
// 为它写了一整套"增量累积+二次确认"；而这份是账号级对象上的一层 list+dict，结构浅得多。
// 局内局外都在（准备阶段 UI 上那份 UIRoomWait.eid_2_player_info 进局就销毁，这份不会）。
//
// ⚠ 三件必须记住的事：
//   1) **final_genius 只有四个终端天赋(8/16/24/32)，不含 26(绝处逢生)** —— 26 仍然只能从
//      genius_id_lv_lst 读，所以那套记忆逻辑不能全删，只是现在只服务于这一个 bit。
//   2) **监管那条的 final_genius 恒为 None**(实测真人局)：天赋只对同阵营公开。
//      但 support_skill_id **是公开的**，求生者也能看到监管带什么特质。
//   3) 配对分两种：人机 unit.unique_id 是 int，对 eid；真人是账号字符串，对 uid。
static RosterEntry g_roster[MAX_ROSTER];   // 读线程的工作副本，填完拷进 g_roster_buf 发布
static RosterEntry g_civ[MAX_ROSTER];      // 准备阶段(我是监管)的求生者天赋，同样双缓冲发布
static int g_civ_n = 0;
// ★ 2026-10-22：最后一次**成功填到天赋数据**的时刻。
//   用它代替"每轮开头无条件 g_civ_n = 0"——后者会把准备阶段的数据
//   在面板键偶发读不到的那几帧清空，屏幕上就是一闪一闪。
static int64_t g_civ_last_ok_ms = 0;

// ★ 最后一次**填充成功**的副本：面板键抖动时 civ_size/civ_at 用它兜底。
//   独立于双缓存 g_civ_buf/g_active —— 那套的切换时序不可控，改它会导致"完全不显示"（实测）。
static RosterEntry g_civ_keep[MAX_ROSTER];
static int         g_civ_keep_n  = 0;
static int64_t     g_civ_keep_ms = 0;
static int g_roster_n = 0;
static int64_t g_i_avatar = -1, g_i_pdl = -1;
static int64_t g_i_role = -1, g_i_eid = -1, g_i_fg = -1, g_i_ruid = -1, g_i_rpid = -1;

static uint32_t bits_of_terminal(int64_t id)
{
    switch (id) {
        case 8:  return BIT_ID_8;
        case 16: return BIT_ID_16;
        case 24: return BIT_ID_24;
        case 26: return BIT_ID_26;
        case 32: return BIT_ID_32;
        default: return 0;
    }
}


static void read_roster()
{
    g_roster_n = 0;
    g_i_avatar = find_key(g_module_dict, "g_avatar", g_i_avatar);
    if (g_i_avatar < 0) return;
    uint64_t av = get_attr(g_module_dict, g_i_avatar);
    uint64_t ad = get_inst_dict(av);
    if (!ad) return;
    Dict adk;
    if (!read_dict(ad, adk)) return;
    g_i_pdl = resolve_index(ad, adk, g_i_pdl, "player_data_list");
    if (g_i_pdl < 0) return;

    int64_t n = 0; uint64_t items = 0;
    if (!read_list(getPtr64(value_slot(adk, g_i_pdl)), n, items) || n == 0) return;

    for (int64_t i = 0; i < n && g_roster_n < MAX_ROSTER; i++) {
        uint64_t e = getPtr64(items + i * 8);        // 每条是普通 dict，不是实例
        Dict ek;
        if (!read_dict(e, ek)) continue;
        RosterEntry r;
        memset(&r, 0, sizeof(r));
        r.eid = -1;

        g_i_role = resolve_index(e, ek, g_i_role, "role_type");
        int64_t v = 0;
        if (g_i_role < 0 || !read_int(getPtr64(value_slot(ek, g_i_role)), v)) continue;
        r.role_type = (int)v;

        g_i_rpid = resolve_index(e, ek, g_i_rpid, "pid");
        if (g_i_rpid >= 0 && read_int(getPtr64(value_slot(ek, g_i_rpid)), v)) r.pid = (int)v;

        // eid: 人机是 int，真人是 ObjectId(读不成 int，留 -1)
        g_i_eid = resolve_index(e, ek, g_i_eid, "eid");
        if (g_i_eid >= 0 && read_int(getPtr64(value_slot(ek, g_i_eid)), v)) r.eid = v;

        // uid: 真人才有，是账号 id 字符串(纯数字，紧凑 ASCII)
        g_i_ruid = resolve_index(e, ek, g_i_ruid, "uid");
        if (g_i_ruid >= 0) read_str(getPtr64(value_slot(ek, g_i_ruid)), r.uid, sizeof(r.uid));

        // final_genius: 终端天赋列表(监管恒为 None)
        g_i_fg = resolve_index(e, ek, g_i_fg, "final_genius");
        if (g_i_fg >= 0) {
            int64_t m = 0; uint64_t fit = 0;
            if (read_list(getPtr64(value_slot(ek, g_i_fg)), m, fit)) {
                for (int64_t j = 0; j < m && j < 8; j++) {
                    int64_t id = 0;
                    if (read_int(getPtr64(fit + j * 8), id)) r.bits |= bits_of_terminal(id);
                }
            }
        }

        g_roster[g_roster_n++] = r;
    }
}

// 准备阶段(本机是监管时)的求生者天赋：**GK.g_avatar.final_genius_dict** = {str(eid): [终端天赋...]}
//   2026-09-24 监管视角 60 秒差分(对面真人求生者 60 秒内换了 16 次天赋)：只有 final_genius_dict 每次都跟着变；
//   UIRoomWait.eid_2_player_info 和 player_data_list 从头到尾停在进房时的值(只有 status 在变) —— 都是快照。
//   c_skill_data 在我是监管时是空列表(它只装本阵营)。第一、二版分别用了 c_skill_data / eid_2_player_info，都不对。
// 键是字符串：人机 = str(int eid)(如 "1000")；真人 = ObjectId 的 24 位小写 hex。
//   ObjectId 布局(2026-09-24 实测)：basicsize 24、只有一个槽 __id → 对象 +0x10 是 bytes 对象，
//   bytes +0x10 = 长度(12)、+0x20 起 12 字节原始内容，转 hex 与 str(oid) 一致。
// 座位号：UIRoomWait.uid2pos = {eid 对象: 座位 1..4}，只含求生者(本机监管在 b_uid2pos)。按座位逐个拼键去查。
// 本机阵营：UIRoomWait.role，1 = 我是监管、2 = 我是求生者(两个阵营各实测过)。只有 role == 1 才收集。
//   UIRoomWait 进局就销毁；final_genius_dict 在大厅里是上一局的数据，所以必须以面板存在为前提。
// ⚠ 不能用 find_key 按这些动态字符串查：它的驻留缓存会**保存 name 指针**(只能传字符串常量)。
static int64_t g_i_uimgr = -1, g_i_uidict = -1, g_i_prole = -1, g_i_uid2pos = -1, g_i_fgd = -1;
static bool g_room_wait = false;   // 本轮找到了 UIRoomWait 面板(只给读线程自己用，见 try_refresh)

// eid 对象 -> final_genius_dict 的键：int 转十进制；ObjectId 取 12 字节转 24 位小写 hex(布局见上)
static bool eid_key_string(uint64_t eid_obj, char *out, size_t cap)
{
    int64_t iv = 0;
    if (read_int(eid_obj, iv)) { snprintf(out, cap, "%lld", (long long)iv); return true; }
    if (!is_obj(eid_obj) || cap < 25) return false;
    uint64_t b = getPtr64(eid_obj + 0x10);
    if (!is_obj(b)) return false;
    int64_t n = 0;
    uint8_t raw[12];
    if (!vm_readv(b + 0x10, &n, 8) || n != 12 || !vm_readv(b + 0x20, raw, 12)) return false;
    static const char hx[] = "0123456789abcdef";
    for (int i = 0; i < 12; i++) { out[i * 2] = hx[raw[i] >> 4]; out[i * 2 + 1] = hx[raw[i] & 15]; }
    out[24] = 0;
    return true;
}

// ---- 座位缓存：每个房间只建一次 "座位 -> final_genius_dict 的键"，之后每轮只读 final_genius_dict ----
// 2026-09-24 实测(VmSwap 1.1G > VmRSS 675M)：uid2pos 里的 eid 对象(ObjectId / int)是进房时建的冷页，
// 几分钟后就被换进 zram，驱动读不到 -> 每轮现拼键就全部失败 -> "求生者天赋0条"。
// 而 final_genius_dict 每次有人换天赋都会被游戏改写，一直是热的。所以：
//   - 键字符串一个房间内不变，建好就缓存(面板对象换了 = 换房间，清空)
//   - 还缓存 final_genius_dict 里匹配到的键对象指针，下一轮先比指针，连键字符串都不用读
//   - 某轮读不到就沿用上一次读到的天赋，不清空(读到了就覆盖，包括读到空列表 —— 对面换天赋途中会短暂是 [])
struct SeatCache { int seat; char key[32]; uint64_t key_obj; uint32_t bits; };
static SeatCache g_seat[4];
static int g_seat_n = 0;
static uint64_t g_seat_panel = 0;      // 建缓存时的 UIRoomWait 实例
static uint64_t g_seat_src = 0;        // 建缓存时的 uid2pos dict；全部座位都建成功才记下，否则下一轮继续补

static SeatCache *seat_slot(int seat)
{
    for (int i = 0; i < g_seat_n; i++) if (g_seat[i].seat == seat) return &g_seat[i];
    if (g_seat_n >= 4) return nullptr;
    SeatCache &c = g_seat[g_seat_n++];
    memset(&c, 0, sizeof(c));
    c.seat = seat;
    return &c;
}

// ★ 诊断日志（统一出口 /data/adb/.d/o.log，跟随 ENABLE_LOG）
#ifndef ENABLE_LOG
#define ENABLE_LOG 0
#endif
static void pg_log(const char *fmt, ...)
{
#if ENABLE_LOG
    va_list ap; va_start(ap, fmt);
    FILE *f = fopen(GUARD_APP_LOG(), "a");
    if (f) { fputs("[pg] ", f); vfprintf(f, fmt, ap); fputc(10, f); fclose(f); }
    va_end(ap);
#else
    (void)fmt;
#endif
}

//   节流 2 秒打一次"卡在哪一步"
#define PGWHY(reason) do { \
    static int64_t _next = 0; \
    const int64_t _n = prep_now_ms(); \
    if (_n >= _next) { _next = _n + 2000; \
        pg_log("卡住: %s (hunter=%d role=%d)", reason, g_prep_as_hunter ? 1 : 0, g_prep_role.load()); } \
} while (0)

static void read_civilian_skill_rows()
{
    // ★★ 2026-10-22 修「天赋行一闪一闪」★★
    //   原来这里无条件 `g_civ_n = 0`，理由是"进局后面板会销毁，
    //   下面多个提前 return 会把旧数据留下"。但它只对了一半：
    //   **面板键偶发读不到**（内存压力/页换出，连续几帧）也会走到
    //   `if (i_panel < 0) return;`，于是数据被清空 -> civ_size()=0
    //   -> 天赋行 len=0 不画。实测就是"能显示但不稳定"。
    //   现在：**超过 3 秒没成功读到才清**。
    //     准备阶段抖动几帧 -> 不清，数据保留 ✓
    //     真进局 / 真离开 -> 3 秒后自然清掉 ✓
    // ★★ 2026-10-22（修正）：这里**必须**每轮清零 ★★
    //   g_civ_n 是**本轮填充计数**（下面 `g_civ_n++`），不是对外数量。
    //   对外的是双缓冲 g_civ_cnt[g_active]（civ_size/civ_at 读的就是它）。
    //   上一版改成"超过 3 秒才清"，导致跨轮累加：
    //     4 个求生者 × 2 轮 = 8 条，每人显示两次（用户实测）；
    //     而且 g_civ_n++ 无上限 -> 再跑几轮就**越界写 g_civ[]**。
    //   "Panel 读不到时不要丢数据"改在 civ_size()/civ_at() 里做：
    //   当前 buffer 空时回退到**另一个 buffer**（上一轮成功发布的那份，没被覆盖）。
    g_civ_n = 0;
                      //    放到循环前面会让局内残留上一次准备阶段的值
    g_room_wait = false;
    //   ★★ 2026-10-21 ★★ 这里原来每轮无条件 `g_prep_as_hunter = false;` ——
    //     只要某一轮读不到 role（换房间/面板重建/驱动偶发返回 0），那一瞬就被判成"我不是监管"，
    //     屏幕上那行天赋就闪断。现在改成**只在确知不是监管/换房间/离开准备界面时才清**，
    //     读不到就保持上一次的结论（"读不到"≠"没有"）。
    g_i_uimgr = find_key(g_module_dict, "g_ui_mgr", g_i_uimgr);
    if (g_i_uimgr < 0) { PGWHY("no_g_ui_mgr"); g_prep_as_hunter = false; return; }
    uint64_t um = get_attr(g_module_dict, g_i_uimgr);
    uint64_t ud = get_inst_dict(um);
    if (!ud) { PGWHY("no_ui_mgr_dict"); g_prep_as_hunter = false; return; }
    Dict udk;
    if (!read_dict(ud, udk)) { PGWHY("ui_mgr_dict_unreadable"); g_prep_as_hunter = false; return; }
    g_i_uidict = resolve_index(ud, udk, g_i_uidict, "ui_dict");
    if (g_i_uidict < 0) { PGWHY("no_ui_dict_key"); g_prep_as_hunter = false; return; }
    uint64_t uid_dict = getPtr64(value_slot(udk, g_i_uidict));
    // ★★ 2026-10-22 修「不在准备阶段启动就识别不到天赋」的真根因 ★★
    //   不能用 find_key：它有**否定缓存**（PyCore.h:353-354, 378）：
    //     if (neg_hit(d, name)) return -1;      // 上次查不到 -> 直接返回，连扫都不扫
    //     ... neg_add(d, name);                  // 扫完没有 -> 记下来
    //   实测（用户发现）：
    //     在大厅启动 -> ui_dict 里当时**确实没有** UIRoomWait
    //       -> neg_add 记下"这个 dict 没有这个键"
    //     进准备界面 -> ui_dict **往里面加了键**，但 dict 对象地址没变
    //       -> neg_hit 命中 -> 永远返回 -1 -> i_panel<0 -> no_panel_key
    //   而 PyScene 判得出"准备"是因为它**自己遍历 + str_equals**，不走缓存。
    //   所以这里也改成自己遍历（键数就 1~2 个，开销可忽略）。
    int64_t i_panel = -1;
    {
        Dict udk2;
        if (read_dict(uid_dict, udk2)) {
            for (int64_t pi = 0; pi < udk2.n_entries; pi++) {
                if (str_equals(getPtr64(key_slot(udk2, pi)), "UIRoomWait")) { i_panel = pi; break; }
            }
        }
    }
    if (i_panel < 0) {                                        // 不在准备界面
        //   ★★ 2026-10-21 最终结论：**只要没看到别的界面，就保持"我是监管"** ★★
        //     实测（日志 `卡住: no_panel_key>30s`）：倒计时一结束游戏就把 UIRoomWait 整个销毁，
        //     但画面还停在准备阶段，in_prep_scene() 仍为真 —— 宽限 30 秒一到标记还是被清掉，
        //     屏幕上的天赋于是又消失（这就是"正常一会儿又没了"）。
        //     正确判据不是"面板还在不在"，而是"**是不是进局了**"：进局的判定交给
        //     绘制侧的 in_prep_scene()（!PyMiniMap::in_battle_scene()）。这里不再清。
        PGWHY("no_panel_key");   // ★ 加了：能看到它是否常走这条路（2s 一条）
        (void)0;   // 保留原结构：面板键暂时没有 -> 保持上一次判定
        return;
    }
    uint64_t panel = get_attr(uid_dict, i_panel);
    //   ★★ 2026-10-21：**换局**的唯一可靠信号 = 准备房间的面板对象换了地址。
    //     （面板被销毁时键直接消失，那只是"倒计时结束/切加载"，不是换局，不能清标记）
    if (g_seat_panel != 0 && panel != 0 && panel != g_seat_panel) {
        g_prep_as_hunter = false;
        g_prep_role = -1;
        g_civ_n = 0;
        g_seat_n = 0;
        g_seat_src = 0;
        pg_log("新的一局（准备面板 0x%llx -> 0x%llx），标记已重置",
                  (unsigned long long)g_seat_panel, (unsigned long long)panel);
    }
    if (panel != 0) g_seat_panel = panel;
    uint64_t pd = get_inst_dict(panel);
    if (!pd) { PGWHY("panel_dict_null"); return; }               // 面板还在、读不到字典 -> 保持原判
    Dict pdk;
    if (!read_dict(pd, pdk)) { PGWHY("panel_dict_unreadable"); return; }
    g_room_wait = true;
    g_prep_last_ms = prep_now_ms();        // 记一次"准备界面确实在"
    int64_t v = 0;
    g_i_prole = resolve_index(pd, pdk, g_i_prole, "role");
    {   // 诊断：role 到底读没读到（节流 2 秒）
        static int64_t _next = 0; const int64_t _n = prep_now_ms();
        if (_n >= _next) { _next = _n + 2000;
            int64_t _vp = -999;
            const bool _ok = (g_i_prole >= 0) && read_int(getPtr64(value_slot(pdk, g_i_prole)), _vp);
            pg_log("role: idx=%lld ok=%d v=%lld hunter=%d panel=0x%llx",
                   (long long)g_i_prole, _ok ? 1 : 0, (long long)_vp,
                   g_prep_as_hunter ? 1 : 0, (unsigned long long)panel);
        }
    }
    if (g_i_prole >= 0 && read_int(getPtr64(value_slot(pdk, g_i_prole)), v)) {
        g_prep_role = (int)v;
        g_prep_as_hunter = (v == BUTCHER_UNIT_TYPE);
    }
    if (!g_prep_as_hunter) { PGWHY("role_not_hunter"); return; }  // role 读不到时用上次的结论

    // 一、座位缓存：uid2pos 变了(有人进出)或上次没建全，就(再)建一次。读不到的座位留到下一轮补
    g_i_uid2pos = resolve_index(pd, pdk, g_i_uid2pos, "uid2pos");
    uint64_t src = (g_i_uid2pos >= 0) ? getPtr64(value_slot(pdk, g_i_uid2pos)) : 0;
    Dict sk;
    if (src && src != g_seat_src && read_dict(src, sk)) {
        int want = 0, ok = 0;
        for (int64_t i = 0; i < sk.n_entries; i++) {
            uint64_t kp = getPtr64(key_slot(sk, i));
            if (kp == 0) continue;                                   // 已删除的条目
            want++;
            if (!read_int(getPtr64(value_slot(sk, i)), v) || v < 1 || v > 4) continue;
            char key[32];
            if (!eid_key_string(kp, key, sizeof(key))) continue;
            SeatCache *c = seat_slot((int)v);
            if (!c) continue;
            if (strcmp(c->key, key) != 0) {                          // 这个座位换人了
                snprintf(c->key, sizeof(c->key), "%s", key);
                c->key_obj = 0; c->bits = 0;
            }
            ok++;
        }
        if (want > 0 && ok == want) g_seat_src = src;
    }
    if (g_seat_n == 0) return;

    // 二、每轮只读 final_genius_dict(read_roster() 已在本轮先解析过 g_i_avatar)。读不到就沿用缓存
    Dict fk;
    bool fk_ok = false;
    if (g_i_avatar >= 0) {
        uint64_t ad = get_inst_dict(get_attr(g_module_dict, g_i_avatar));
        Dict adk;
        if (ad && read_dict(ad, adk)) {
            g_i_fgd = resolve_index(ad, adk, g_i_fgd, "final_genius_dict");
            fk_ok = g_i_fgd >= 0 && read_dict(getPtr64(value_slot(adk, g_i_fgd)), fk);
        }
    }
    for (int s = 0; fk_ok && s < g_seat_n; s++) {
        SeatCache &c = g_seat[s];
        int64_t hit = -1;
        for (int64_t i = 0; i < fk.n_entries && hit < 0; i++) {      // 先比缓存的键对象指针：不用读字符串
            uint64_t kp = getPtr64(key_slot(fk, i));
            if (kp && kp == c.key_obj) hit = i;
        }
        for (int64_t i = 0; i < fk.n_entries && hit < 0; i++) {
            uint64_t kp = getPtr64(key_slot(fk, i));
            if (str_equals(kp, c.key)) { hit = i; c.key_obj = kp; }
        }
        if (hit < 0) continue;                                       // 没找到(也可能是键字符串页读不到)：沿用
        int64_t m = 0; uint64_t git = 0;
        if (!read_list(getPtr64(value_slot(fk, hit)), m, git)) continue;   // 值读不到：沿用
        uint32_t bits = 0;
        bool all = true;
        for (int64_t j = 0; j < m && j < 8; j++) {
            if (read_int(getPtr64(git + j * 8), v)) bits |= bits_of_terminal(v);
            else all = false;
        }
        if (all) c.bits = bits;                                      // 读全了才覆盖(含空列表)
    }

    // 三、按座位号发布
    for (int s = 0; s < g_seat_n && g_civ_n < MAX_ROSTER; s++) {
        if (g_seat[s].bits == 0) continue;
        RosterEntry e;
        memset(&e, 0, sizeof(e));
        e.eid = -1;
        e.role_type = CIVILIAN_UNIT_TYPE;
        e.seat = g_seat[s].seat;
        e.bits = g_seat[s].bits;
        int k = g_civ_n++;
        while (k > 0 && g_civ[k - 1].seat > e.seat) { g_civ[k] = g_civ[k - 1]; k--; }
        g_civ[k] = e;
    }
    // ★ 填到数据就记下时间戳（上面的 3 秒兜底靠它）
    if (g_civ_n > 0) {
        g_civ_last_ok_ms = prep_now_ms();
        // ★ 存一份副本：下轮面板键抖动时拿它兑底
        for (int i = 0; i < g_civ_n && i < MAX_ROSTER; i++) g_civ_keep[i] = g_civ[i];
        g_civ_keep_n  = g_civ_n;
        g_civ_keep_ms = g_civ_last_ok_ms;
    }
}

// 按单位的 unique_id 找花名册条目：人机是 int(对 eid)，真人是账号字符串(对 uid)
static const RosterEntry *roster_find(uint64_t unique_slot)
{
    if (g_roster_n == 0) return nullptr;
    uint64_t vp = getPtr64(unique_slot);
    int64_t iv = 0;
    if (read_int(vp, iv)) {
        for (int i = 0; i < g_roster_n; i++)
            if (g_roster[i].eid == iv) return &g_roster[i];
        return nullptr;
    }
    char s[24];
    if (!read_str(vp, s, sizeof(s))) return nullptr;
    for (int i = 0; i < g_roster_n; i++)
        if (g_roster[i].uid[0] && strcmp(g_roster[i].uid, s) == 0) return &g_roster[i];
    return nullptr;
}

static int collect_type(uint64_t ubt, int type, int write_idx, int count)
{
    uint64_t lst = get_type_list(ubt, type);
    int64_t n = 0; uint64_t items = 0;
    if (!read_list(lst, n, items) || n == 0) return count;

    for (int64_t i = 0; i < n && count < MAX_UNITS; i++) {
        uint64_t inst = getPtr64(items + i * 8);
        if (!is_obj(inst)) continue;
        uint64_t d = getPtr64(inst - 0x18);           // managed-dict 在对象前 0x18
        Dict dk;
        if (!read_dict(d, dk)) continue;

        const bool is_follower = (type == YIDHRA_PUPPET_UNIT_TYPE);
        g_i_support = resolve_index(d, dk, g_i_support, "support_skill_id");
        g_i_model   = resolve_index(d, dk, g_i_model,   "model");
        g_i_pos     = resolve_index(d, dk, g_i_pos,     "position");

        // 大天赋优先走花名册(见 read_roster())：结构浅、不受冷页换出影响
        const RosterEntry *rt_entry = nullptr;
        if (!is_follower) {
            g_i_unique = resolve_index(d, dk, g_i_unique, "unique_id");
            if (g_i_unique >= 0) rt_entry = roster_find(value_slot(dk, g_i_unique));
        }

        GeniusMemory *mem = nullptr;
        // 逐单位的 genius_id_lv_lst 是天赋的权威来源(实时、且含 26 绝处逢生 —— 26 不在 final_genius 里)
        if (!is_follower) {
            g_i_genius = resolve_index(d, dk, g_i_genius, "genius_id_lv_lst");
            g_i_uid    = resolve_index(d, dk, g_i_uid,    "uid");
            // 只在求生者类上找 ability_used：监管类上没这个属性，每个单位白扫一遍上千条不值
            if (type == CIVILIAN_UNIT_TYPE)
                g_i_ability = resolve_index(d, dk, g_i_ability, "ability_used");
            if (g_i_genius < 0) continue;

            int64_t uid = 0;
            bool has_uid = (g_i_uid >= 0) && read_int(getPtr64(value_slot(dk, g_i_uid)), uid);

            // 天赋按 uid 增量累积(见 read_genius())。这一轮 uid 读不到时天赋状态记为未知(只影响这一轮的显示)，
            // 记忆本身不动，下一轮读到 uid 会接着用
            mem = has_uid ? get_mem(uid) : nullptr;
            if (mem) read_genius(value_slot(dk, g_i_genius), *mem);
        }

        int trait = 0;
        if (g_i_support >= 0) {
            uint64_t sp = getPtr64(value_slot(dk, g_i_support));
            int64_t m = 0; uint64_t sit = 0;
            if (read_list(sp, m, sit) && m >= 1) {
                int64_t v = 0;
                if (read_int(getPtr64(sit), v) && v >= 1 && v <= 8) trait = (int)v;
            }
        }

        uint64_t scene = 0;
        if (g_i_model >= 0) {            uint64_t mo = getPtr64(value_slot(dk, g_i_model));
            if (is_obj(mo)) {
                uint64_t sp2 = getPtr64(mo + 0x20);
                if (is_obj(sp2)) scene = sp2;
            }
        }
        float xyz[3] = {0, 0, 0};
        if (g_i_pos >= 0) {
            uint64_t pos = getPtr64(value_slot(dk, g_i_pos));
            if (is_obj(pos)) vm_readv(pos + 0x10, xyz, 12);
        }

        g_buf[write_idx][count].scene_obj = scene;

// 保存原始 CivilianUnit / ButcherUnit / PuppetUnit 对象
// 后面自动技能可以直接通过这个地址读取 skill_mgr。
g_buf[write_idx][count].unit_obj = inst;

g_buf[write_idx][count].x = xyz[0];
g_buf[write_idx][count].y = xyz[2];

        bool used = false;
        if (type == CIVILIAN_UNIT_TYPE && g_i_ability >= 0)
            used = read_desperate_used(value_slot(dk, g_i_ability));

        Info inf;
        memset(&inf, 0, sizeof(inf));              // 冷却那几项求生者侧不填，必须先清零
        inf.camp     = type;
        inf.support_trait = trait;
        inf.desperate_used = used;
        if (mem) {
            inf.genius_bits   = mem_bits(*mem);
            inf.genius_state = mem_state(*mem);
            memcpy(inf.genius_levels, mem->levels, sizeof(inf.genius_levels));
        }
        // 花名册只作**兜底**：它的 final_genius 是进房时的快照，玩家进局前改了天赋它不会更新
        // (2026-09-23 实测：准备界面把天赋从 [16,32] 改成 [8,24]，player_data_list 纹丝不动，
        //  只有 g_avatar.final_genius_dict 和 UIRoomWait.c_skill_data 跟着变)。
        // 所以逐单位的 genius_id_lv_lst 仍然是权威；只有它一条都没读出来时，才用快照顶一下，
        // 免得内存压力下天赋行整个空掉。**特质绝不用快照**(同样会滞后)，只认 unit.support_skill_id。
        if (rt_entry && inf.genius_state == GENIUS_UNKNOWN && rt_entry->bits) {
            inf.genius_bits |= rt_entry->bits;
            inf.genius_state = GENIUS_PARTIAL;      // 标成"不完整"，显示时带 ?，提醒这是兜底值
        }
        if (type == BUTCHER_UNIT_TYPE || is_follower) collect_hunter_skill(d, dk, trait, inf);
        else                                  collect_flywheel(d, dk, inf);

        g_buf[write_idx][count].info = inf;
        count++;
    }
    return count;
}

static bool try_refresh()
{
    // ★★ 2026-10-22（回退）：恢复"只在为 0 时解析" ★★
    //   曾经改成"每轮重解析"和"每轮轻量校验"，两个都不对：
    //     · 每轮重解析 -> game_kernel_dict() 内部 find_key 全扫（千条）
    //     · 每轮轻量校验 -> read_dict 要读 136 条的 entries 块（~3KB ioctl）
    //   实测两者都把 PySelf 刷新从 0.29ms/次 拉到 1.2~1.6ms/次，框跟不上人。
    //   而"启动时机"那个 bug 的**真正修复是下面绕开否定缓存**，与这里无关。
    if (g_module_dict == 0 && !resolve_module()) return false;

    // 花名册先读：局内用来给单位配大天赋，局外(准备阶段)是唯一的数据源。
    // 它和 units 无关，所以即使下面 unit_mgr 无效(局外)也要先读、先发布
    read_roster();                 // player_data_list：身份 + 监管特质(进房快照)
    read_civilian_skill_rows();    // g_avatar.final_genius_dict：求生者天赋(我是监管、有 UIRoomWait 时)
    {
        int w = 1 - g_active;
        for (int i = 0; i < g_roster_n; i++) g_roster_buf[w][i] = g_roster[i];
        g_roster_cnt[w] = g_roster_n;
        for (int i = 0; i < g_civ_n; i++) g_civ_buf[w][i] = g_civ[i];
        g_civ_cnt[w] = g_civ_n;
    }

    // ★★ 2026-10-22 性能：**分阶段跳过不必要的读取** ★★
    //   实测：准备阶段 units_by_type 里**只有道具**
    //   （Halloween2022/Bottle/Bookshelf/Chair/MyCity），没有 type=1/2 玩家。
    //   但每轮仍会：查 unit_mgr -> 查 units_by_type -> 3 次 collect_type
    //   每次 collect_type 都要 find_key（全扫）+ 遍历 list -> 纯白烧。
    //   日志里 `units_by_type[N] 读取中断` 反复刷屏就是它。
    //   现在：确定在准备界面（g_room_wait）且已读到求生者天赋 -> 直接跳过后面整段。
    //   保底：g_civ_n==0（天赋还没读到）不跳，不影响发现阶段变化。
    if (g_room_wait && g_civ_n > 0) {
        snprintf(g_status, sizeof(g_status), "准备阶段 求生者天赋%d条（跳过units）", g_civ_n);
        return true;
    }

    g_i_unit_mgr = find_key(g_module_dict, "unit_mgr", g_i_unit_mgr);
    uint64_t um = (g_i_unit_mgr >= 0) ? get_attr(g_module_dict, g_i_unit_mgr) : 0;
    if (!is_obj(um)) {
        // 局外：没有单位，但花名册有内容就发布出去(准备阶段显示监管特质/求生者天赋靠它)
        if (g_roster_n > 0 || g_civ_n > 0) {
            int w = 1 - g_active;
            g_count[w] = 0;
            g_active = w;
            snprintf(g_status, sizeof(g_status), "局外 花名册%d人 实时天赋%d条", g_roster_n, g_civ_n);
            return true;
        }
        snprintf(g_status, sizeof(g_status), "unit_mgr 无效(未在对局中?)");
        return false;
    }

    // 换局了：uid 会从 1000001 重新开始，上一局的天赋记忆必须作废，否则会张冠李戴。
    // 注意只在**拿到有效新地址**时比较；um 无效时不动记忆，免得对局中途的
    // 短暂读失败把记忆误清。
    if (g_last_unit_mgr != 0 && um != g_last_unit_mgr) g_mem_count = 0;
    g_last_unit_mgr = um;

    uint64_t ud = getPtr64(um - 0x18);
    g_i_ubt = find_key(ud, "units_by_type", g_i_ubt);
    uint64_t ubt = (g_i_ubt >= 0) ? get_attr(ud, g_i_ubt) : 0;
    if (!is_obj(ubt)) { snprintf(g_status, sizeof(g_status), "units_by_type 无效"); return false; }

    int write_idx = 1 - g_active, count = 0;
    // 监管和求生者的属性序号缓存是共用的；两个类的插入顺序不同，
    // 所以每个单位都会走一次"按名字校验，不对就重扫"，多花的时间可以忽略
    count = collect_type(ubt, BUTCHER_UNIT_TYPE,  write_idx, count);
    count = collect_type(ubt, CIVILIAN_UNIT_TYPE, write_idx, count);
    count = collect_type(ubt, YIDHRA_PUPPET_UNIT_TYPE, write_idx, count);   // 没有女巫时这个键不存在，直接返回

    if (count == 0) {
        // 准备阶段：unit_mgr 是存在的(大厅/准备阶段 units_by_type 有 69/100/1007 等键，2026-09-24 实测)，
        // 所以会走到这里而不是上面的 !is_obj(um) 分支。以前在这里直接 return false、不翻转缓冲，
        // 结果准备阶段的天赋读到了却从没发布，绘制侧一直读上一局局内那份 —— 准备阶段不显示求生者天赋的根因。
        // 有 UIRoomWait 面板就确定是准备阶段，照样发布；没有面板(局内中途读失败)仍保留旧缓冲。
        if (g_room_wait) {
            g_count[write_idx] = 0;
            g_active = write_idx;
            snprintf(g_status, sizeof(g_status), "准备阶段 %s 求生者天赋%d条",
                     g_prep_as_hunter ? "我是监管" : "我是求生者(不显示)", g_civ_n);
            return true;
        }
        snprintf(g_status, sizeof(g_status), "一个单位也没读出");
        return false;
    }
    g_count[write_idx] = count;
    g_active = write_idx;
    snprintf(g_status, sizeof(g_status), "正常 %d 人", count);
    return true;
}

// ============================================================================
// 治疗进度诊断：全属性差分快照（临时工具）
//
//   ── 已有的实测结论（三次迭代）──────────────────────────────
//     ① cured_process_client 不是实时值：50ms 采样 392 次纹丝不动，
//        只在"开始治疗"的瞬间跳一次（34.61 / 60.02 / 78.33 / 86.00）。
//     ② 它旁边只有 cured_speed_client(=6.67，治疗速率)，其余候选字段都不存在。
//     ③ 所以真正的"摸的过程中涨的进度"不在这两个字段上。
//
//   ── 这次做什么：不求猜对，直接做差分 ──────────────────────
//     每 500ms 把每个求生者单位的**全部浮点属性**快照下来写文件。
//     治疗过程中抓 40 张（20 秒），然后对比前后两张：
//       哪个 name 的数值一路在涨，哪个就是实时进度。
//     如果一张快照里"没有一个数值在动"，说明进度不在 unit 的字典上，
//     那就去看它挂的组件/技能对象（本工具也会把 skill_dict 的 key 打出来）。

// ============================================================================
// 治疗进度：50ms 独立读取（生产版）
//
//   ── 侦察结论（六轮实测，别再重复踩）──────────────────────────
//     ① cured_process_client 不是实时值：200ms 采样 392 次不动，
//        只在"开始摸"的瞬间跳一次；不同次治疗分别是 12.17 / 34.61 / 86.00 ...
//     ② 单位字典 1305 个属性里没有任何 cure* 进度字段（只有 cured_process_client
//        / cured_speed_client / low_cure_speed_mode / micro_quick_fix_extra）。
//     ③ skill_mgr / skill_dict[id] / current_skill / cur_action / action_list
//        全量变化检测（进程内逐帧比对，按对象地址做键）：
//        整局里 progress 类字段 0 次变化，只有 last_cast_skill_id 等事件字段在动。
//     => 治疗进度不是内存里的持久字段，是游戏 UI 层按帧算出来的。
//        所以这里只读两个**真实**数据：进度锚点 + 速率，
//        中间的连续变化交给绘制侧用速率积分补出来（和游戏自己的做法一致）。
// ============================================================================
static const int HEAL_INTERVAL_MS = 50;
#define HEAL_PROBE 0

// ============================================================================
// 治疗进度：按"state==14 = 正在摸人"判定（用户实测规律）
//
//   ── 判据（用户提供，比任何启发式都可靠）────────────────────
//     · 求生者 state_machine._state[0] == 14  -> 该单位正在摸人（治疗）
//     · 治疗目标 = 离它最近的另一个求生者，且水平距离 <= 2m
//     · state != 14 -> 没在摸人
//
//   ── 之前为什么一直失败（记录一下，别再走弯路）──────────────
//     state 存在 state_machine._state 这个**列表**里，属性名以 `_` 开头。
//     我前面所有 dump 都有 `if (nm[0]=='_') continue;` —— 正好把它过滤掉了，
//     所以永远看不到状态字段，只能靠"刷新间隔"猜治疗是否结束，才会出现
//     "松手了数字还在涨"。
//
//   ── 输出 ───────────────────────────────────────────────────
//     表里只有**正在被治疗的那个人**（target），active=1：
//       prog = 目标的 cured_process_client（实测锚点）
//       rate = 目标的 cured_speed_client（实测速率，%/秒）
//     绘制侧只在 active 时显示，active 一消失（state 变回非14）立刻隐藏。
// ============================================================================
// ============================================================================
// 治疗进度：发布所有求生者的 cured_process_client（含"是否正在被治疗"标记）
//
//   ── 字段（MCP dict_at 实测）────────────────────────────────
//     cured_process_client  0~100  治疗进度；没人摸时是上次残留值，开始摸时归 0
//     cured_speed_client    6.667  治疗速率 %/秒
//     curer_uids            list   谁正在治疗我；非空 = 正在被治疗（松手立刻清空）
//
//   ── 显示规则（用户要求）────────────────────────────────────
//     不再由"是否正在被治疗"决定显隐，而是由**数值本身**决定：
//       0 < prog < 100  -> 一直显示
//       prog == 0 或 >=100 -> 不显示
//     curer_uids 只用来判断"现在要不要按速率继续积分"，避免松手后数字继续涨。
// ============================================================================
struct HealRec { uint64_t target; float prog; float rate; int active; };
static HealRec  g_heal_tab[2][MAX_UNITS];
static volatile int g_heal_cnt[2] = {0, 0};
static volatile int g_heal_pub = 0;
static volatile bool g_heal_available = false;
static volatile int g_heal_hit = 0, g_heal_skip = 0;
static int64_t g_i_prog = -1, g_i_rate = -1, g_i_curer = -1;

static void refresh_heal_quick()
{
    if (!g_heal_available || g_module_dict == 0) return;

    const int64_t i_um = find_key(g_module_dict, "unit_mgr", -1);
    if (i_um < 0) return;
    uint64_t um = get_attr(g_module_dict, i_um);
    if (!is_obj(um)) return;
    uint64_t ud = getPtr64(um - 0x18);
    const int64_t i_ubt = find_key(ud, "units_by_type", g_hint_ubt);
    if (i_ubt >= 0) g_hint_ubt = i_ubt;
    if (i_ubt < 0) return;
    uint64_t ubt = get_attr(ud, i_ubt);
    if (!is_obj(ubt)) return;

    const int w = 1 - g_heal_pub;
    int cnt = 0, hit = 0, skip = 0;

    const int types[2] = { CIVILIAN_UNIT_TYPE, BUTCHER_UNIT_TYPE };
    for (int t = 0; t < 2; t++) {
        uint64_t lst = get_type_list(ubt, types[t]);
        int64_t n = 0; uint64_t items = 0;
        if (!read_list(lst, n, items) || n == 0) continue;
        for (int64_t i = 0; i < n && cnt < MAX_UNITS; i++) {
            uint64_t inst = getPtr64(items + i * 8);
            if (!is_obj(inst)) { skip++; continue; }
            uint64_t d = getPtr64(inst - 0x18);
            Dict dk;
            if (!read_dict(d, dk)) { skip++; continue; }

            g_i_prog = resolve_index(d, dk, g_i_prog, "cured_process_client");
            if (g_i_prog < 0) { skip++; continue; }
            float prog = 0.f, rate = 0.f;
            if (!read_float(value_slot(dk, g_i_prog), prog, -1.0f, 100.5f)) { skip++; continue; }
            g_i_rate = resolve_index(d, dk, g_i_rate, "cured_speed_client");
            if (g_i_rate >= 0) read_float(value_slot(dk, g_i_rate), rate, -1.0f, 100.5f);

            // 是否正在被治疗：curer_uids 非空（只影响"要不要继续积分"）
            int active = 0;
            g_i_curer = resolve_index(d, dk, g_i_curer, "curer_uids");
            if (g_i_curer >= 0) {
                uint64_t cl = getPtr64(value_slot(dk, g_i_curer));
                int64_t cn = 0; uint64_t citems = 0;
                if (is_obj(cl) && read_list(cl, cn, citems) && cn > 0) active = 1;
            }

            g_heal_tab[w][cnt].target = inst;
            g_heal_tab[w][cnt].prog   = prog;
            g_heal_tab[w][cnt].rate   = rate;
            g_heal_tab[w][cnt].active = active;
            cnt++; hit++;
        }
    }
    g_heal_cnt[w] = cnt;
    g_heal_pub    = w;
    g_heal_hit    = hit;
    g_heal_skip   = skip;
}
// ============================================================================
// QTE（自动校准）探针
//   对象：game_kernel.input -> 实际是 UIBattle（MCP 实测）
//   字段（MCP dict_at 实测，1430 项字典里的第 365~1429 项）：
//     _in_qte              bool   QTE 是否激活
//     qte_bar_flag         bool   校准条是否显示
//     qte_start_time       float  本次 QTE 起始时刻（游戏时钟）
//     qte_speed            int    指针角速度
//     qte_half_good_range  int    好区半宽
//     exce_pos             tuple  (lo, hi) 好区角度区间
//     qte_success_flag / qte_perfect_flag  bool
//   探针只在这些值**发生变化**时写一行，文件很小。
// ============================================================================
static void qte_probe_thread()
{
    DisguiseThreadName(17);
    int64_t i_input = -1, i_in_qte = -1, i_bar = -1, i_st = -1, i_spd = -1,
            i_half = -1, i_exce = -1, i_ok = -1, i_pf = -1, i_zh = -1;
    float last_st = -1.f;
    int   last_in = -1, last_bar = -1, last_ok = -1, last_pf = -1;
    float last_lo = -999.f, last_hi = -999.f;
    int   last_spd = -1, last_half = -1;

    while (true) {
        usleep(20 * 1000);
        if (g_module_dict == 0) continue;

        i_input = find_key(g_module_dict, "input", i_input);
        if (i_input < 0) continue;
        uint64_t ui = get_attr(g_module_dict, i_input);
        if (!is_obj(ui)) continue;
        uint64_t d = getPtr64(ui - 0x18);
        Dict dk;
        if (!read_dict(d, dk)) continue;

        i_in_qte = resolve_index(d, dk, i_in_qte, "_in_qte");
        i_bar    = resolve_index(d, dk, i_bar,    "qte_bar_flag");
        i_st     = resolve_index(d, dk, i_st,     "qte_start_time");
        i_spd    = resolve_index(d, dk, i_spd,    "qte_speed");
        i_half   = resolve_index(d, dk, i_half,   "qte_half_good_range");
        i_exce   = resolve_index(d, dk, i_exce,   "exce_pos");
        i_ok     = resolve_index(d, dk, i_ok,     "qte_success_flag");
        i_pf     = resolve_index(d, dk, i_pf,     "qte_perfect_flag");
        i_zh     = resolve_index(d, dk, i_zh,     "qte_zhizhen");
        uint64_t zh_ptr = (i_zh >= 0) ? getPtr64(value_slot(dk, i_zh)) : 0;

        int  in_qte = 0, bar = 0, ok = 0, pf = 0, spd = 0, half = 0;
        float st = 0.f, lo = 0.f, hi = 0.f;
        int exce_n = 0, has_lo = 0, has_hi = 0;

        if (i_in_qte >= 0) { bool b=false; if (PyCore::read_bool(value_slot(dk, i_in_qte), b)) in_qte = b; }
        if (i_bar >= 0)    { bool b=false; if (PyCore::read_bool(value_slot(dk, i_bar), b)) bar = b; }
        if (i_ok >= 0)     { bool b=false; if (PyCore::read_bool(value_slot(dk, i_ok), b)) ok = b; }
        if (i_pf >= 0)     { bool b=false; if (PyCore::read_bool(value_slot(dk, i_pf), b)) pf = b; }
        if (i_st >= 0)     read_float(value_slot(dk, i_st), st, -1e9f, 1e9f);
        if (i_spd >= 0)    { int64_t v=0; if (read_int(getPtr64(value_slot(dk, i_spd)), v)) spd=(int)v; }
        if (i_half >= 0)   { int64_t v=0; if (read_int(getPtr64(value_slot(dk, i_half)), v)) half=(int)v; }
        if (i_exce >= 0) {
            // tuple 布局（memdump 实测）：+0x10 = ob_size，+0x18 = item[0] 的**槽位**，
            // +0x20 = item[1] 的槽位。槽位里放的是 PyObject*，read_float 要的正是槽位地址。
            //   ⚠ 早先写成 "it = *(tp+0x18); read_float(it+8)" 是错的 ——
            //     那是 item[0] 对象内部 +8（类型指针），不是 item[1]。
            uint64_t tp = getPtr64(value_slot(dk, i_exce));
            if (is_obj(tp)) {
                int64_t sz = 0;
                if (vm_readv(tp + 0x10, &sz, 8) && sz >= 2 && sz <= 4) {
                    exce_n = (int)sz;
                    if (read_float(tp + 0x18, lo, -1e9f, 1e9f)) has_lo = 1;
                    if (read_float(tp + 0x20, hi, -1e9f, 1e9f)) has_hi = 1;
                }
            }
        }

        bool changed = (in_qte != last_in) || (bar != last_bar) ||
                       (fabsf(st - last_st) > 0.0001f) ||
                       (fabsf(lo - last_lo) > 0.001f) || (fabsf(hi - last_hi) > 0.001f) ||
                       (ok != last_ok) || (pf != last_pf) ||
                       (spd != last_spd) || (half != last_half);

        // 每次 qte_start_time 变化 -> 记下我们自己的起点，用来算"已过时间"
        static double t_start_ms = 0.0;
        static float  st_seen    = -1.f;
        struct timespec ts0; clock_gettime(CLOCK_MONOTONIC, &ts0);
        double now_ms = (double)ts0.tv_sec * 1000.0 + (double)ts0.tv_nsec / 1e6;
        if (fabsf(st - st_seen) > 0.0001f) { st_seen = st; t_start_ms = now_ms; }

        // QTE 期间逐帧记录（要看清"经过多久 -> 是否命中"），非 QTE 只在变化时记
        if (!changed && in_qte == 0) { last_in=in_qte; last_bar=bar; last_st=st;
            last_lo=lo; last_hi=hi; last_ok=ok; last_pf=pf; last_spd=spd; last_half=half; continue; }

        last_in = in_qte; last_bar = bar; last_st = st;
        last_lo = lo; last_hi = hi; last_ok = ok; last_pf = pf;
        last_spd = spd; last_half = half;

        // ★ 找"是否在黄区"的信号：把 QTE 相关的布尔量/计数在 QTE 期间 20ms 一轮全打出来，
        //   哪个周期性翻转、且周期接近 66ms（8° ÷ 120°/s），哪个就是命中判据。
        //   节点内存那条路已排除：qte_zhizhen 的 0x180 字节 116 次采样无一列变化，
        //   说明指针转动是动画/时间线驱动，不写回该节点。
        {
            static int64_t i_alert = -1, i_hit = -1, i_fail = -1, i_times = -1,
                           i_ct = -1, i_sf = -1, i_cm = -1;
            i_alert = resolve_index(d, dk, i_alert, "qte_alert");
            i_hit   = resolve_index(d, dk, i_hit,   "qte_hit_flag");
            i_fail  = resolve_index(d, dk, i_fail,  "qte_fail_flag");
            i_times = resolve_index(d, dk, i_times, "qte_times");
            i_sf    = resolve_index(d, dk, i_sf,    "qte_show_frame_id");
            i_cm    = resolve_index(d, dk, i_cm,    "qte_coming_time");

            int alert = 0, hitf = 0, failf = 0, sup2 = 0;
            int64_t times = -1, sfid = -1;
            float cmt = 0.f;

            if (i_alert >= 0) { bool b=false; if (PyCore::read_bool(value_slot(dk, i_alert), b)) alert = b; }
            if (i_hit   >= 0) { bool b=false; if (PyCore::read_bool(value_slot(dk, i_hit),   b)) hitf  = b; }
            if (i_fail  >= 0) { bool b=false; if (PyCore::read_bool(value_slot(dk, i_fail),  b)) failf = b; }
            if (i_sf    >= 0) { int64_t v=0; if (read_int(getPtr64(value_slot(dk, i_sf)), v)) sfid = v; }
            if (i_times >= 0) { int64_t v=0; if (read_int(getPtr64(value_slot(dk, i_times)), v)) times = v; }
            if (i_cm    >= 0) read_float(value_slot(dk, i_cm), cmt, -1e9f, 1e9f);

            if (in_qte == 1) {
                static int bl = 0;
                if (bl < 4000) {
                    FILE *bf = fopen("/data/local/tmp/qte_bools.txt", "a");
                    if (bf) {
                        struct timespec tb; clock_gettime(CLOCK_MONOTONIC, &tb);
                        double bms = (double)tb.tv_sec * 1000.0 + (double)tb.tv_nsec / 1e6;
                        fprintf(bf, "t=%.0f el=%.0f st=%.4f lo=%.1f hi=%.1f alert=%d hit=%d fail=%d sup=%d pf=%d times=%lld sfid=%lld cm=%.4f\n",
                                bms, (double)(bms - t_start_ms), st, lo, hi,
                                alert, hitf, failf, ok, pf, (long long)times, (long long)sfid, cmt);
                        fclose(bf);
                        bl++;
                    }
                }
            }
        }
        //   ★ 2026-10-21 修：这里原来有一段 #if !ENABLE_LOG 的石盘探针，
        //     把 while 的收尾括号写在了条件编译**里面** —— ENABLE_LOG=1 时不成立，
        //     于是 `}` 消失、后面所有函数被当成"嵌套在函数里"而编译失败。
        //     探针早已废弃（日志统一走 /data/adb/.d/o.log），整段删掉。
    }   // while
}
// ============================================================================
// 双世界并发枚举（约瑟夫相中世界）
//
//   根因（MCP 实测）：`unit_mgr` 是**每个世界一份**的 —— world.unit_mgr。
//   game_kernel.unit_mgr 绑定的是 cur_world，所以进相中世界后现实世界的单位
//   根本不在 units_by_type 里，ESP 自然只显示当前那一个世界。
//
//   做法：缓存见过的世界对象（进/出相机时 cur_world 会换人），
//   对"非当前世界"各读一次它的 unit_mgr.units_by_type，发布成另一张表，
//   绘制侧用约瑟夫相机那套投影公式单独画（不同颜色）。
// ============================================================================
struct OtherUnit { uint64_t scene; float x, y, z; int camp; int is_mirror; };
static OtherUnit g_other[32];
static volatile int g_other_n = 0;
static volatile int g_photo_active = 0;   // 相中世界是否生效（in_photo_world != None）
//   ★ 2026-10-21：监管是否处于"台球形态"。判据 = 单位表里 camp1 的 position.y >= 3
//     （实测：人形态 y≈0.1~0.4 = 脚底；球形态 y=10.1 = 球心）
static std::atomic<int> g_hunter_ball{0};
//   ★ 监管当前位置（refresh_hunter_pos 每 16ms 更新；球形态的框直接用这个）
static volatile float g_hp_x = 0, g_hp_y = 0, g_hp_z = 0;
//   ★★ 2026-10-21 用户要求：**不要用坐标判断形态，用水据来源判断** ★★
//     人形态：ButcherUnit.model 的场景对象 == ESP data[] 里那条（同一个对象）
//     球形态：ButcherUnit.model 指向**另一个**场景对象（球的模型），data[] 里那条是人形的旧对象
static volatile uint64_t g_hp_scene = 0;   // 监管当前 model 的场景指针
static std::atomic<int> g_hp_ok{0};
//   2026-10-27：**监管位置的"新鲜度"时间戳**
//   原来 g_hp_* 是"粘性"的：refresh_hunter_pos 读失败时直接 return，
//   上一局的值（尤其台球形态的 y=10.1）就一直留着。换局后新一局没有
//   台球手，绘制侧仍然看到 y>=3 → 画出假的 [台球形态]。
static std::atomic<int64_t> g_hp_ms{0};
// 在 g_hp_ms 声明附近（文件顶部）加：
static std::atomic<int64_t> g_other_ms{0};

// ============================================================================
// 单位"是否还活着"判据（用于清掉约瑟夫相中世界的残留实体）
//
//   ── 为什么需要它 ───────────────────────────────────────────
//   相中世界结束后，那批镜像单位**仍留在 units_by_type 里**（实测 uid 1000236~），
//   而"相中世界是否生效"这个状态在单位数据里读不到：
//     · in_photo_world  —— 开启时也是 None（实测，不是猜的）
//     · PhotoUnit 的字典 —— 全是静态布尔，没有计时
//   所以不能靠"世界状态"决定画不画。
//
//   ── 这个判据 ───────────────────────────────────────────────
//   所有单位都有逐帧递增的字段（`frame_id` / `now`）。活着的单位每帧都在变，
//   被废弃的镜像单位不再被 tick -> 该字段停止变化。
//   于是：某个玩家单位的 frame_id 超过 2 秒没变 -> 判定为残留 -> 不画。
//   相中世界**开启时**镜像是活的（会 tick）-> 照常显示 ✓
// ============================================================================
struct TickRec { uint64_t scene; int64_t frame; int64_t ms; };
static TickRec g_tick[32];
static int g_tick_n = 0;

static int64_t mono_ms()
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// 返回 true = 这个 scene 已经"死"了（超过 2 秒没有 tick）
inline bool is_stale_scene(uint64_t scene_obj)
{
    if (scene_obj == 0) return false;
    const int64_t now = mono_ms();
    for (int i = 0; i < g_tick_n; i++)
        if (g_tick[i].scene == scene_obj)
            return (now - g_tick[i].ms) > 2000;
    return false;          // 没见过就不判死（安全网）
}

static void note_unit_tick(uint64_t scene, int64_t frame)
{
    const int64_t now = mono_ms();
    for (int i = 0; i < g_tick_n; i++) {
        if (g_tick[i].scene == scene) {
            if (g_tick[i].frame != frame) { g_tick[i].frame = frame; g_tick[i].ms = now; }
            return;
        }
    }
    if (g_tick_n < 32) {
        g_tick[g_tick_n].scene = scene;
        g_tick[g_tick_n].frame = frame;
        g_tick[g_tick_n].ms    = now;
        g_tick_n++;
    }
}
//   ★ 2026-10-21：**只读监管**的轻量刷新（每 16ms 一次，为球形态的框提供 60Hz 的位置）。
//     原来球形态的框走 refresh_other_world()，那一趟要枚举全部求生+监管、几十次字典读，
//     实测约 30~50ms 才跑完 —— 30Hz 的循环实际只有 15~20Hz，画面看着就是"卡一下才更新"。
//     这里只碰 unit_mgr.units_by_type[1] 里的那一个 ButcherUnit：约 8 次读，微秒级。
static void refresh_hunter_pos()
{
    if (g_module_dict == 0) { g_hp_ok = 0; return; }   // 明确清空
    const int64_t i_um = find_key(g_module_dict, "unit_mgr", -1);
    if (i_um < 0) { g_hp_ok = 0; return; }
    uint64_t um = get_attr(g_module_dict, i_um);
    if (!is_obj(um)) { g_hp_ok = 0; return; }
    uint64_t ud = getPtr64(um - 0x18);
    const int64_t i_ubt = find_key(ud, "units_by_type", g_hint_ubt);
    if (i_ubt >= 0) g_hint_ubt = i_ubt;
    if (i_ubt < 0) { g_hp_ok = 0; return; }
    uint64_t ubt = get_attr(ud, i_ubt);
    if (!is_obj(ubt)) { g_hp_ok = 0; return; }

    uint64_t lst = get_type_list(ubt, BUTCHER_UNIT_TYPE);
    int64_t cnt = 0; uint64_t items = 0;
    if (!read_list(lst, cnt, items) || cnt <= 0) {
        //   2026-10-27：**读不到监管 = 这一局没监管或单位表还没就绪**
        //   不再保留旧值 —— 上一局的台球形态会在这里被清掉
        g_hp_ok = 0;
        g_hp_scene = 0;
        g_hunter_ball = 0;
        return;
    }
    uint64_t inst = getPtr64(items);
    if (!is_obj(inst)) { g_hp_ok = 0; return; }
    uint64_t d = getPtr64(inst - 0x18);
    Dict dk;
    if (!read_dict(d, dk)) { g_hp_ok = 0; return; }

    static int64_t i_model = -1, i_pos = -1;
    i_model = resolve_index(d, dk, i_model, "model");
    if (i_model < 0) { g_hp_ok = 0; return; }
    uint64_t mo = getPtr64(value_slot(dk, i_model));
    if (!is_obj(mo)) { g_hp_ok = 0; return; }
    i_pos = resolve_index(d, dk, i_pos, "position");
    if (i_pos < 0) { g_hp_ok = 0; return; }
    uint64_t pos = getPtr64(value_slot(dk, i_pos));
    if (!is_obj(pos)) { g_hp_ok = 0; return; }
    float xyz[3] = {0, 0, 0};
    if (!vm_readv(pos + 0x10, xyz, 12)) { g_hp_ok = 0; return; }
    if (fabsf(xyz[0]) > 100000.0f || fabsf(xyz[2]) > 100000.0f) { g_hp_ok = 0; return; }
    if (fabsf(xyz[0]) < 1.0f && fabsf(xyz[2]) < 1.0f) { g_hp_ok = 0; return; }

    g_hp_scene = getPtr64(mo + 0x20);
    g_hp_x = xyz[0]; g_hp_y = xyz[1]; g_hp_z = xyz[2];
    g_hp_ok = 1;
    g_hp_ms.store(prep_now_ms());
    // 2026-10-27：球形态判断（原来就有，别删）
    //   人形态 position.y ≈ 0.1~0.4（脚底），球形态 position.y = 10.1（球心）。
    //   加 300ms 迟滞：y 在 3 附近抖动时不会瞬间翻回人形态。
    {
        static int64_t s_ball_ms = 0;
        g_hunter_ball = 0;
    }
}

// refresh_other_world() 内：
static void refresh_other_world()
{
    if (g_module_dict == 0) { g_other_n = 0; g_photo_active = 0; return; }
    const int64_t i_um = find_key(g_module_dict, "unit_mgr", -1);
    if (i_um < 0) { g_other_n = 0; g_photo_active = 0; return; }
    uint64_t um = get_attr(g_module_dict, i_um);
    if (!is_obj(um)) { g_other_n = 0; g_photo_active = 0; return; }
    uint64_t ud = getPtr64(um - 0x18);
    const int64_t i_ubt = find_key(ud, "units_by_type", g_hint_ubt);
    if (i_ubt >= 0) g_hint_ubt = i_ubt;
    if (i_ubt < 0) { g_other_n = 0; g_photo_active = 0; return; }
    uint64_t ubt = get_attr(ud, i_ubt);
    if (!is_obj(ubt)) { g_other_n = 0; g_photo_active = 0; return; }

    int n = 0, photo_active_any = 0;
    const int types[2] = { CIVILIAN_UNIT_TYPE, BUTCHER_UNIT_TYPE };
    for (int ti = 0; ti < 2 && n < 32; ti++) {
        uint64_t lst = get_type_list(ubt, types[ti]);
        int64_t cnt = 0; uint64_t items = 0;
        if (!read_list(lst, cnt, items)) continue;
        for (int64_t k = 0; k < cnt && n < 32; k++) {
            uint64_t inst = getPtr64(items + k * 8);
            if (!is_obj(inst)) continue;
            uint64_t d = getPtr64(inst - 0x18);
            Dict dk;
            if (!read_dict(d, dk)) continue;

            static int64_t i_model = -1, i_pos = -1;
            i_model = resolve_index(d, dk, i_model, "model");
            if (i_model < 0) continue;
            uint64_t mo = getPtr64(value_slot(dk, i_model));
            if (!is_obj(mo)) continue;
            uint64_t scene = getPtr64(mo + 0x20);
            if (!is_obj(scene)) continue;

            i_pos = resolve_index(d, dk, i_pos, "position");
            if (i_pos < 0) continue;
            uint64_t pos = getPtr64(value_slot(dk, i_pos));
            if (!is_obj(pos)) continue;
            float xyz[3] = {0, 0, 0};
            if (!vm_readv(pos + 0x10, xyz, 12)) continue;
            // 位置无效的（0,0,0 或异常大）跳过
            if (fabsf(xyz[0]) < 1.0f && fabsf(xyz[2]) < 1.0f) continue;
            if (fabsf(xyz[0]) > 100000.0f || fabsf(xyz[2]) > 100000.0f) continue;

            // ---- 相中世界判据（MCP 实测字段）----
            //   out_photo_uid != None  -> 这个单位是**镜像副本**
            //   in_photo_world != None -> 相中世界**正在生效**
            // ⚠ 关键：**不能用 is_obj 判 None**。is_obj 是地址区间判断
            //   (0x5000000000..0x8000000000)，而 Py_None 是个合法对象、地址正好
            //   落在这个区间里 —— 于是 "in_photo_world = None" 被误判成"有值"，
            //   photo_active 恒为 1，镜像永远不隐藏（上一版残影的根因）。
            //   改用"真正能读出来"来判：
            //     out_photo_uid 是 int    -> read_int 成功
            //     in_photo_world 是 world -> 有实例字典，read_dict 成功
            //   None 两者都读不出来。
            static int64_t i_out = -1, i_ipw = -1;
            i_out = resolve_index(d, dk, i_out, "out_photo_uid");
            i_ipw = resolve_index(d, dk, i_ipw, "in_photo_world");
            int is_mirror = 0;
            if (i_out >= 0) {
                int64_t tmp = 0;
                if (PyCore::read_int(getPtr64(value_slot(dk, i_out)), tmp)) is_mirror = 1;
            }
            // 相中世界是否生效：读 UIBattle.in_photo（MCP 实测会 True/False 翻转）
            //   ⚠ 不能用 in_photo_world —— 实测开启时也是 None。
            {
                static int64_t i_ui = -1, i_ip = -1;
                i_ui = find_key(g_module_dict, "input", i_ui);
                if (i_ui >= 0) {
                    uint64_t ui = get_attr(g_module_dict, i_ui);
                    if (is_obj(ui)) {
                        uint64_t uid = getPtr64(ui - 0x18);
                        Dict uik;
                        if (read_dict(uid, uik)) {
                            i_ip = resolve_index(uid, uik, i_ip, "in_photo");
                            if (i_ip >= 0) {
                                bool b = false;
                                if (PyCore::read_bool(value_slot(uik, i_ip), b) && b)
                                    photo_active_any = 1;
                            }
                        }
                    }
                }
            }
            // 记录"这个单位还在不在被 tick"：frame_id 每帧递增，废弃单位会停住
            static int64_t i_fid = -1;
            i_fid = resolve_index(d, dk, i_fid, "frame_id");
            int64_t fid = 0;
            if (i_fid >= 0) PyCore::read_int(getPtr64(value_slot(dk, i_fid)), fid);
            note_unit_tick(scene, fid);

            g_other[n].scene     = scene;
            g_other[n].x         = xyz[0];
            g_other[n].y         = xyz[1];   // ★ 2026-10-21 补：y 一直没赋值 -> 球形态判断(Y>=3)永远失败
            g_other[n].z         = xyz[2];
            g_other[n].camp      = types[ti];
            g_other[n].is_mirror = is_mirror;
            n++;
        }
    }

    g_other_n = n;
    g_photo_active = photo_active_any;
    g_other_ms.store(prep_now_ms());
}
static void other_world_thread()
{
    DisguiseThreadName(18);
    int tick = 0;
    while (true) {
        usleep(16 * 1000);            // ★ 16ms：监管位置（球形态的框）≈60Hz
        refresh_hunter_pos();
        if ((++tick % 30) == 0) refresh_other_world();  // 全量枚举（相中世界那套）降到 ≈500ms 一次，减少卡顿
    }
}
static void heal_thread_main()
{
    while (true) {
        refresh_heal_quick();
        usleep(HEAL_INTERVAL_MS * 1000);
    }
}
static const int REFRESH_INTERVAL_MS = 500;

static void refresh_once()
{
    if (g_libbase == 0) return;
    if (!PyRoot::ensure()) {
        g_available = false;
        snprintf(g_status, sizeof(g_status), "%s", PyRoot::status_text());
        return;
    }
    if (try_refresh()) {
        g_available = true;
        g_heal_available = true;      // 单位表已建立，放行治疗进度的高频线程
        g_fail_streak = 0;
        return;
    }
    g_available = false;
    if (++g_fail_streak >= 8) {       // 连续失败就把序号缓存全部作废，下一轮重新按名字找
        g_module_dict = 0;
        g_i_unit_mgr = g_i_ubt = -1;
        g_i_genius = g_i_support = g_i_model = g_i_pos = g_i_uid = g_i_ability = -1;
        g_i_skillmgr = g_i_skilldict = g_i_cdrate = -1;
        g_i_unique = g_i_avatar = g_i_pdl = -1;
        g_i_role = g_i_eid = g_i_fg = g_i_ruid = g_i_rpid = -1;
        g_i_uimgr = g_i_uidict = g_i_prole = g_i_uid2pos = g_i_fgd = -1;
        g_i_cd = g_i_cdtime = g_i_power = g_i_curpower = -1;
        g_fail_streak = 0;
        // 这里**不清天赋记忆**：对局中途链路短暂中断也会走到这里，清了会让天赋行白白消失。
        // 记忆只在 unit_mgr 地址变化(换局)时清，见 try_refresh()
    }
}
static void thread_main()
{
    DisguiseThreadName(16);   // PyGenius
    while (true) {
        refresh_once();
        usleep(REFRESH_INTERVAL_MS * 1000);
    }
}

static void start(uintptr_t libbase)
{
    if (g_libbase != 0) return;
    g_libbase   = libbase;
    snprintf(g_status, sizeof(g_status), "启动中");
    std::thread(thread_main).detach();
    std::thread(heal_thread_main).detach();
    std::thread(other_world_thread).detach();
    // 约瑟夫：另一个世界的单位枚举   // 治疗进度：50ms 独立刷新
    std::thread(qte_probe_thread).detach();   // QTE 探针：50ms 采样，变化才写
    printf("[天赋] 已启动 libbase=0x%lx\n", (unsigned long)libbase);
    fflush(stdout);
}

// ---------------- 给绘制侧用的只读接口 ----------------

static inline bool available() { return g_available; }
static inline const char *status_text() { return g_status; }

// ---- 花名册：局内给单位配大天赋的兜底(见 collect_type)，局外不再直接拿来显示 ----
// (大厅里它仍是上一局的数据，2026-09-24 实测；准备阶段显示改走下面的 civ_*)
static inline int roster_size() { return g_roster_cnt[g_active]; }

// 准备阶段的求生者天赋(来自 final_genius_dict，实时)，已按座位号排好序。
// 只有本机是监管(prep_as_hunter())时才有内容；局内这里是 0 条，天赋走 lookup()
// ★★ 2026-10-22（第三版，最终）：**不动现有双缓冲，另开一份「最后成功」副本** ★★
//   为什么不能改 g_civ_n 的清零：它是本轮填充计数（g_civ_n++），
//   不清就跨轮累加（实测 4人×2轮=8 条、每人两次），而且会越界写 g_civ[]。
//   为什么不能改 civ_size 去读另一个 buffer：那个 buffer 的 g_active 切换时序不可控，
//   实测直接导致「完全不显示」。
//   现在：保持 g_civ_n 每轮清零、保持双缓冲原样，
//   另存一份 g_civ_keep[]（填充成功时拷贝），
//   civ_size/civ_at 在当前 buffer 为空时回退到它（3 秒内）。
static inline int civ_size()
{
    if (g_civ_cnt[g_active] > 0) return g_civ_cnt[g_active];
    if (prep_now_ms() - g_civ_keep_ms <= 3000) return g_civ_keep_n;
    return 0;
}
static inline bool prep_as_hunter() { return g_prep_as_hunter; }
static inline int  prep_role()      { return g_prep_role; }
//   ★ 监管是否球形态（绘制侧用：球形态时主循环别画，交给单位表那条）
static inline bool hunter_ball() { return g_hunter_ball.load() != 0; }
static inline uint64_t hunter_scene() { return g_hp_scene; }
static inline bool hunter_pos(float &x, float &y, float &z)
{
    if (g_hp_ok.load() == 0) return false;
    //   2026-10-27：**超过 500ms 没刷新就作废**
    //   other_world_thread 每 16ms 调一次 refresh_hunter_pos，正常情况下
    //   g_hp_ms 一直在更新。超过 500ms 说明 refresh_hunter_pos 已经
    //   连续失败（换局/单位表未就绪），旧值不能再拿来画 ——
    //   否则会把上一局的台球形态位置画到新局。
    const int64_t t = g_hp_ms.load();
    if (t != 0 && prep_now_ms() - t > 500) return false;
    x = g_hp_x; y = g_hp_y; z = g_hp_z;
    return true;
}
// 进局/离开准备阶段时由绘制侧调用：把"我是监管"与天赋缓存清掉，下一局重新判定
static inline void prep_clear()
{
    g_prep_as_hunter = false;
    g_prep_role = -1;
    g_civ_n = 0;
    g_prep_last_ms = 0;
}

//   ★ 2026-10-13：准备阶段的判据 `in_prep_scene()` **放在 draw_Gui.cpp** ✓
//     因为它要用 PyMiniMap::scene_path()，而本头文件**没有** PyMiniMap ✗
//     （我第一版写在这里，编译报 use of undeclared identifier 'PyMiniMap' ✗）
//     放 draw_Gui.cpp 最合适：那里同时能看到 PyMiniMap 和 PyGenius ✓

// seat：座位号 1..4，0 = 没配上
static bool civ_at(int i, int &seat, int &pid, uint32_t &bits)
{
    if (g_civ_cnt[g_active] > 0) {
        if (i < 0 || i >= g_civ_cnt[g_active]) return false;
        seat = g_civ_buf[g_active][i].seat; pid = g_civ_buf[g_active][i].pid; bits = g_civ_buf[g_active][i].bits;
        return true;
    }
    // ★ 当前 buffer 空 -> 3 秒内成功过就用副本（防面板键抖动导致天赋行闪）
    if (prep_now_ms() - g_civ_keep_ms <= 3000 && i >= 0 && i < g_civ_keep_n) {
        seat = g_civ_keep[i].seat; pid = g_civ_keep[i].pid; bits = g_civ_keep[i].bits;
        return true;
    }
    return false;
    return true;
}

// 配对规则跟 PyProgress.h 一致：先用 model+0x20 的指针身份，读不到再退回坐标近邻。
// 人物是移动的，坐标兜底给的阈值比密码机那边紧，宁可配不上也不要配错。
static bool lookup(uintptr_t obj, float x, float y, Info &out)
{
    if (!g_available) return false;
    int b = g_active, n = g_count[b];
    for (int i = 0; i < n; i++) {
        if (g_buf[b][i].scene_obj == (uint64_t)obj) { out = g_buf[b][i].info; return true; }
    }
    int nearest = -1; float min_dist = 1.0f;
    for (int i = 0; i < n; i++) {
        if (g_buf[b][i].scene_obj != 0) continue;
        if (g_buf[b][i].x == 0.f && g_buf[b][i].y == 0.f) continue;
        float d = fabsf(g_buf[b][i].x - x) + fabsf(g_buf[b][i].y - y);
        if (d < min_dist) { min_dist = d; nearest = i; }
    }
    if (nearest < 0) return false;
    out = g_buf[b][nearest].info;
    return true;
}

// 绝处逢生只在求生者侧成立：id 26 在监管侧是另一个天赋，按 id 直接判会误标
static inline bool has_desperate(const Info &g)
{
    return g.camp == CIVILIAN_UNIT_TYPE && (g.genius_bits & BIT_ID_26);
}

// 绝处逢生四态：没带 / 带了还没用 / 带了已经用掉 / 还不知道
// **"没带"只有在天赋表完整时才能断言**；不完整又还没确认到 26，就是"未知"，不能当成没带
enum { DESPERATE_NONE = 0, DESPERATE_AVAILABLE = 1, DESPERATE_USED = 2, DESPERATE_UNKNOWN = 3 };
static inline int desperate_state(const Info &g)
{
    if (has_desperate(g)) return g.desperate_used ? DESPERATE_USED : DESPERATE_AVAILABLE;
    return g.genius_state == GENIUS_COMPLETE ? DESPERATE_NONE : DESPERATE_UNKNOWN;
}

// 监管辅助特质那一行的完整文本：特质名 + 冷却/充能。
//   普通技能   "闪现 12.7s" / "闪现 就绪"
//   充能型技能 "窥视者 2/3 20.9s"(还在攒下一层) / "窥视者 3/3"(满层)
// _cd_delta 不在冷却中时恒为 0，所以 0 直接当"就绪"用，不需要额外的标志位。
static void trait_line_text(const Info &g, const char *name, char *buf, size_t cap);

// 辅助特质名。1~8 连号，全表已确认
static const char *trait_name(int id)
{
    switch (id) {
        case 1: return "聆听";
        case 2: return "失常";
        case 3: return "兴奋";
        case 4: return "巡视者";
        case 5: return "传送";
        case 6: return "窥视者";
        case 7: return "闪现";
        case 8: return "移形";
        default: return "";
    }
}

// 读线程 500ms 才读一次 _cd_delta，直接显示会一卡一卡地跳 0.5。
// _cd_delta 实测严格按每秒 -1(乘 cd_rate)递减，所以绘制时从"读到的值"按经过的时间往下推。
// 推到 0 以下只停在 0.0，**不自己判就绪** —— 就绪以真正读到的 0 为准(见 cd_ready())，
// 否则底牌改写冷却、或者推算略快时会误报。下一轮读取会覆盖推算值，误差不超过一个读取周期。
static float extrapolate(float read_val, int64_t at_ms, float rate)
{
    if (read_val <= 0.05f) return 0.f;
    if (rate <= 0.f) rate = 1.f;
    float r = read_val - (float)(now_ms() - at_ms) / 1000.f * rate;
    return r > 0.f ? r : 0.f;
}
static float cd_remain_now(const Info &g)
{
    return g.has_cd ? extrapolate(g.cd_remain, g.cd_read_ms, g.cd_rate) : 0.f;
}

// 飞轮剩余冷却（秒）。返回 999 = 没带飞轮或读不到，返回 0 = 就绪
static inline float flywheel_remain_now(const Info &g)
{
    if (!g.has_flywheel) return 999.f;
    return extrapolate(g.flywheel_remain, g.flywheel_read_ms, g.flywheel_rate);
}

// 就绪 = 真正读到 0(不是推算到 0)
static inline bool cd_ready(const Info &g) { return g.has_cd && g.cd_remain <= 0.05f; }
static inline bool flywheel_ready(const Info &g) { return g.has_flywheel && g.flywheel_remain <= 0.05f; }

// 天赋简称行，拆成 前 / 飞轮 / 后 三段，飞轮单独一段好让绘制侧单独上色。
// 求生者：双弹 [飞轮] 搏命 大心脏(固定最后)；监管全部在"前"段：封窗 底牌 张狂 挽留(固定最后)。
//   未知：三段全空(不画)
//   部分：已确认的大天赋 + "?"，表示可能还有没读到的
//   完整：全部大天赋；一个大天赋都没带就写"无"
// 飞轮段：就绪 "飞轮"(飞轮就绪=true，画红) / 冷却中 "飞轮13s" / 没读到冷却 "飞轮"。
// skill_dict 里有飞轮技能本身就证明带了飞轮，所以天赋表读不全(未知/部分)时也照样显示它。
struct GeniusLine { char pre[48]; char flywheel[24]; char post[48]; bool flywheel_ready; };
static void genius_segments(const Info &g, GeniusLine &o)
{
    memset(&o, 0, sizeof(o));
    bool is_survivor = (g.camp == CIVILIAN_UNIT_TYPE);
    bool carries_flywheel = is_survivor && ((g.genius_bits & BIT_ID_8) || g.has_flywheel);
    if (g.genius_state == GENIUS_UNKNOWN && !carries_flywheel) return;
    if (is_survivor) {
        if (g.genius_bits & BIT_ID_32) strncat(o.pre, "双弹", sizeof(o.pre) - strlen(o.pre) - 1);
        if (carries_flywheel) {
            if (flywheel_ready(g)) { snprintf(o.flywheel, sizeof(o.flywheel), "飞轮"); o.flywheel_ready = true; }
            else if (g.has_flywheel) snprintf(o.flywheel, sizeof(o.flywheel), "飞轮%.0fs", extrapolate(g.flywheel_remain, g.flywheel_read_ms, g.flywheel_rate));
            else               snprintf(o.flywheel, sizeof(o.flywheel), "飞轮");
        }
        if (g.genius_bits & BIT_ID_24) strncat(o.post, "搏命", sizeof(o.post) - strlen(o.post) - 1);
        if (g.genius_bits & BIT_ID_16) strncat(o.post, "大心脏", sizeof(o.post) - strlen(o.post) - 1);
    } else {
        if (g.genius_bits & BIT_ID_8)   strncat(o.pre, "封窗", sizeof(o.pre) - strlen(o.pre) - 1);
        if (g.genius_bits & BIT_ID_16) strncat(o.pre, "底牌", sizeof(o.pre) - strlen(o.pre) - 1);
        if (g.genius_bits & BIT_ID_32) strncat(o.pre, "张狂", sizeof(o.pre) - strlen(o.pre) - 1);
        if (g.genius_bits & BIT_ID_24) strncat(o.pre, "挽留", sizeof(o.pre) - strlen(o.pre) - 1);
    }
    if (g.genius_state != GENIUS_COMPLETE) strncat(o.post, "?", sizeof(o.post) - strlen(o.post) - 1);
    else if (!o.pre[0] && !o.flywheel[0] && !o.post[0]) snprintf(o.pre, sizeof(o.pre), "无");
}

static void trait_line_text(const Info &g, const char *name, char *buf, size_t cap)
{
    if (!name || !name[0]) { buf[0] = '\0'; return; }
    if (!g.has_cd) { snprintf(buf, cap, "%s", name); return; }   // 读不到冷却就只写名字
    if (g.charge_max > 0) {                                      // 充能型
        // 满层时 _cd_delta 的表现没验过，按读到的层数判满层；层数本身不推算，等下一轮读取
        if (g.charge_cur >= g.charge_max) snprintf(buf, cap, "%s %d/%d", name, g.charge_cur, g.charge_max);
        else snprintf(buf, cap, "%s %d/%d %.1fs", name, g.charge_cur, g.charge_max, cd_remain_now(g));
        return;
    }
    if (cd_ready(g)) snprintf(buf, cap, "%s 就绪", name);
    else           snprintf(buf, cap, "%s %.1fs", name, cd_remain_now(g));
}

// ★★ 镜像判据（2026-10-12 用户发现 + 实测确认）
//
//   用户从界面上看出来的：现实本体天赋 "双弹大心脏"，镜像 "双弹大心脏？"（多一个问号）
//   代码依据：逐单位天赋来自 unit 的 genius_id_lv_lst；读不到就退回花名册快照
//             并标 genius_state = GENIUS_PARTIAL，显示层见 PARTIAL 补 "?"
//   实测确认（fn=unit_genius，一局 8 个单位）：
//       0x7604174760 camp2 state=2 COMPLETE   <- 真人
//       0x76042b4580 camp2 state=2 COMPLETE   <- 真人
//       0x760438e640 camp2 state=2 COMPLETE   <- 真人
//       0x76043a4e90 camp2 state=1 PARTIAL    <- ★ 镜像
//       0x76043661e0 camp2 state=1 PARTIAL    <- ★ 镜像
//       0x760431a9c0 camp2 state=1 PARTIAL    <- ★ 镜像
//       0x76043d4910 camp2 state=1 PARTIAL    <- ★ 镜像
//   **4 真人 COMPLETE / 4 镜像 PARTIAL，完美二分** ✓
//
//   这个判据的本质：**镜像是个半成品拷贝，游戏没给它天赋数据** ✓
//   与"玩家在哪个世界"完全无关 ✓ 真人在内存压力下偶尔也可能读失败，但那是暂态 ✓
//   ★★ 重要（2026-10-12 实测）：传入的必须是 **scene_obj**，不是 unit_obj！
//       data[i].obj（覆盖层从 curArray 拿到的对象）与 PyGenius 的 scene_obj 完全相同，
//       而 unit_obj 是另一个地址段（Python 侧对象），永远匹配不上 —— 这正是
//       前几版判据"跳过计数为 0"的原因 ✗
inline int genius_state_of(uint64_t scene_obj)
{
    const int b = g_active;
    const int n = g_count[b];
    for (int i = 0; i < n; i++)
        if (g_buf[b][i].scene_obj == scene_obj)
            return g_buf[b][i].info.genius_state;
    return -1;      // 不在表里（不是求生/监管单位，或数据还没读到）
}

// 这个 scene_obj 是不是"镜像副本"（out_photo_uid != None）？
//   相中世界结束后这些单位仍留在 units_by_type 里（uid 1000236~ 那一批），
//   所以不能靠"在不在表里"判断死活，要看这个标记 + 相中世界是否生效。
//   ★ 2026-10-21：按场景对象查"Python 层的世界坐标"(unit.position)。
//     用途：台球手变球时 `obj+0x28` 那条坐标链会失效（实测读出来是 0 / -65536），
//     但 Python 里 unit.position 仍然是好的 —— 绘制侧拿它兜底，球形态就不会消失。
// 诊断：g_other 表的规模与内容
static inline int  other_count() { return g_other_n; }
static inline uint64_t other_scene_at(int i) { return (i >= 0 && i < 32) ? g_other[i].scene : 0; }
//   ★ 2026-10-27：g_other 的"刷新时间戳"。
//   绘制侧用它判断"这张表是不是当前局的" —— 换局瞬间 g_other 已经是新局、
//   而 data[] 可能还是旧局/空的，两边不同源就会把所有人画成紫色。
static inline int64_t other_ms() { return g_other_ms.load(); }

//   ★ 2026-10-21：按阵营取"唯一候选项"的 Python 坐标。
//     C++ 侧 data[].obj 与 Python 侧 unit.model 不是同一个对象（实测 scene 对不上），
//     但**同一阵营只有一个有效候选项**时（监管局里 camp=1 就一个人）可以安全兜底。
//     返回 false 的条件：该阵营 0 个或 ≥2 个有效坐标（宁可不画，也不画错人）。
inline bool other_sole_pos(int camp, float &x, float &y, float &z)
{
    int n = 0;
    float rx = 0, ry = 0, rz = 0;
    const int total = g_other_n;
    for (int i = 0; i < total && i < 32; i++) {
        if (g_other[i].camp != camp) continue;
        if (fabsf(g_other[i].x) > 100000.0f || fabsf(g_other[i].z) > 100000.0f) continue;
        if (fabsf(g_other[i].x) < 1.0f && fabsf(g_other[i].z) < 1.0f) continue;
        rx = g_other[i].x; ry = g_other[i].y; rz = g_other[i].z;
        n++;
        if (n >= 2) return false;
    }
    if (n != 1) return false;
    x = rx; y = ry; z = rz;
    return true;
}

inline bool other_pos_of_scene(uint64_t scene_obj, float &x, float &y, float &z)
{
    if (scene_obj == 0) return false;
    const int n = g_other_n;
    for (int i = 0; i < n && i < 32; i++) {
        if (g_other[i].scene != scene_obj) continue;
        if (fabsf(g_other[i].x) > 100000.0f || fabsf(g_other[i].z) > 100000.0f) return false;
        if (fabsf(g_other[i].x) < 1.0f && fabsf(g_other[i].z) < 1.0f) return false;
        x = g_other[i].x; y = g_other[i].y; z = g_other[i].z;
        return true;
    }
    return false;
}

inline bool is_photo_copy(uint64_t scene_obj)
{
    const int n = g_other_n;
    if (scene_obj == 0) return false;
    for (int i = 0; i < n; i++)
        if (g_other[i].scene == scene_obj) return g_other[i].is_mirror != 0;
    return false;
}
// 这个 scene_obj 是否还在单位表里？
//   用途：约瑟夫相中世界结束后，那批镜像实体还会被场景扫描（data[]）读到，
//   但游戏已经把它们从 units_by_type 里删掉了 —— 用这张表当"实体是否还活着"的权威。
//   ⚠ 表为空时（还没读到数据）返回 true（不过滤），否则开局会把所有框都吞掉。
inline bool has_scene(uint64_t scene_obj)
{
    const int b = g_active;
    const int n = g_count[b];
    if (n <= 0) return true;                 // 安全网：没数据就不拦
    if (scene_obj == 0) return false;
    for (int i = 0; i < n; i++)
        if (g_buf[b][i].scene_obj == scene_obj) return true;
    return false;
}

// 取某个单位的治疗进度（0..100），纯驱动读。
//   返回 false = 不在表里（不是玩家单位，或这一轮还没读到）
//   返回 true 但 out<=0.5 = 当前没人治疗（游戏里 cured_process_client 恒 0）
//   ⚠ 参数和 genius_state_of 一样必须是 **scene_obj**，不是 unit_obj。
inline bool heal_of(uint64_t scene_obj, float &out)
{
    out = 0.f;
    if (scene_obj == 0) return false;
    const uint64_t unit = find_unit_by_scene(scene_obj);
    if (unit == 0) return false;
    const int b = g_heal_pub, n = g_heal_cnt[b];
    for (int i = 0; i < n; i++) {
        if (g_heal_tab[b][i].target == unit) {
            out = g_heal_tab[b][i].prog;
            return true;
        }
    }
    return false;
}

// 是否正在被治疗（curer_uids 非空）。只用于决定"要不要继续按速率积分"。
inline bool heal_active_of(uint64_t scene_obj)
{
    if (scene_obj == 0) return false;
    const uint64_t unit = find_unit_by_scene(scene_obj);
    if (unit == 0) return false;
    const int b = g_heal_pub, n = g_heal_cnt[b];
    for (int i = 0; i < n; i++)
        if (g_heal_tab[b][i].target == unit) return g_heal_tab[b][i].active != 0;
    return false;
}
// 治疗速率（%/秒），供绘制侧做速率积分
inline bool heal_rate_of(uint64_t scene_obj, float &out)
{
    out = 0.f;
    if (scene_obj == 0) return false;
    const uint64_t unit = find_unit_by_scene(scene_obj);
    if (unit == 0) return false;
    const int b = g_heal_pub, n = g_heal_cnt[b];
    for (int i = 0; i < n; i++) {
        if (g_heal_tab[b][i].target == unit) {
            out = g_heal_tab[b][i].rate;
            return true;
        }
    }
    return false;
}

// 是不是镜中产物：在表里、且天赋不是 COMPLETE
//   实测依据（截图 + fn=unit_genius）：镜像的天赋显示带 "?"，状态为 GENIUS_PARTIAL；
//   真人是 COMPLETE。4 真人 / 4 镜像，完美二分 ✓
//
//   ★★☠☠ 2026-10-13 重要警告：**这条判据只对求生者(camp 2)成立** ✗✗
//     监管者的天赋状态**天然就是 PARTIAL**（不是被镜像了）：
//       实测 fn=genius_style:
//         camp state bits   segments
//         1    1     0x6    底牌挽留||?     <- 监管，PARTIAL 是**正常的**
//         2    2     0x1A   双弹||大心脏    <- 求生，COMPLETE
//     原因：监管有 7 个天赋位，而游戏页面只让看 4 个 ->
//           **永远读不全** -> 永远是 PARTIAL ✗
//     => 拿这条判据去判监管，**必然把监管误判成镜像** ✗
//
//   ⚠ 所以调用方**必须自己带阵营限定**：
//       `(camp == 2) && is_mirror_unit(obj)`     ✓ 正确
//       `!is_mirror_unit(obj)`                   ✗ 会误杀监管
//     这个坑已经真实发生过一次（监管天赋整块不显示）✗
//     我**没有**把这个限定写进函数体，因为：
//       函数只拿得到 scene_obj，拿不到阵营 ✗
//       而"镜像"这个概念本身就只属于求生侧（红夫人/约瑟夫的镜像都是求生者的像）✓
//     所以限定放在调用方，但必须在**每个**调用点都写 ✗
inline bool is_mirror_unit(uint64_t scene_obj)
{
    const int st = genius_state_of(scene_obj);
    return (st >= 0 && st != GENIUS_COMPLETE);
}

// ★ 只读数据接口（2026-10-12）：给 draw_Gui 的 unit_genius dump 用。
//   放这里而不是让 dump 直接读 g_buf —— 那些是 static，跨头文件访问不方便；
//   而且这样也避开了 PyDump 在本文件未声明的问题 ✓
struct UnitBrief { uint64_t unit_obj; uint64_t scene_obj; int camp; int64_t uid; int state; unsigned bits; };
inline int  active_buf_index() { return g_active; }
inline int  unit_count(int b)   { return (b == 0 || b == 1) ? g_count[b] : 0; }
inline bool unit_brief(int b, int i, UnitBrief &out)
{
    if (b != 0 && b != 1) return false;
    if (i < 0 || i >= g_count[b]) return false;
    const Entry &e = g_buf[b][i];
    out.unit_obj = e.unit_obj;
    out.scene_obj = e.scene_obj;
    out.camp     = e.info.camp;
    out.uid      = 0;                // Entry/Info 里都没有 uid，用 unit_obj 认身份即可
    out.state    = e.info.genius_state;
    out.bits     = (unsigned)e.info.genius_bits;
    return true;
}

} // namespace 天赋

#endif // IDV_PY_GENIUS_H
