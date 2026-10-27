#ifndef IDV_PY_KNIGHT_H
#define IDV_PY_KNIGHT_H
#include "guard.h"   // GUARD_APP_LOG（日志路径不留明文）

// ============================================================================
// 骑士（dxzh）头盔预测 —— 读取器（2026-10-21 实现）
// ============================================================================
//
// ── 先说清"能读什么 / 读不到什么"（每一步都有实测支撑，见 skills/knight-prediction/SKILL.md）
//
//   ✔ 这个单位是不是骑士          unit.knight_item_data != None
//   ✔ 头盔猜了几次                World.knight_predict_use_counter（每用一次 +1）
//                                 计数读不到时退化为"观测到的 use_id[0] 变了几次"（带 ~ 标记）
//   ✔ 头盔冷却 / 是否正在读条      skill_dict[14101]._cd_delta、cast_args.will_cast
//   ✔ 他这次锁了几个目标           cast_args.predict_arg.arg.target_uids 的长度
//   ✘ **猜的到底是什么**（出刀 / 技能 / 什么都不做）—— **不在客户端**
//        证据 1：技能对象 222 个字段做「出刀」vs「不做」全量对比，除 use_id 计数器外全同
//        证据 2：10 秒窗口扫过 2000MB，predict_arg / use_id / target_uids 零命中
//        证据 3：协议是 protobuf（数字 tag 编码，字段名根本不进内存）—— 解释了证据 2 必零命中
//      所以本模块**只发布次数**，不给"他猜了什么"的假答案。给假答案比不给更糟。
//
// ── 读数链路（MCP 实测逐层验证过）
//
//   sys.modules -> game_kernel(模块) -> md_dict
//     ├ "unit_mgr" -> 实例 - 0x18 -> "units_by_type" -> [2] 求生者列表
//     │    └ CivilianUnit 实例 - 0x18 -> dict
//     │         ├ "knight_item_data"  -> dict{ id:141, model:..., socket:"toukui" }
//     │         ├ "model"  -> +0x20 = 场景对象（跟绘制循环里的 obj 配对）
//     │         ├ "position" -> +0x10/+0x14/+0x18 = X/高度/Y
//     │         └ "skill_mgr" -> 实例 - 0x18 -> "skill_dict" -> [14101]
//     │              └ SkillCivilianProphetPredict 实例 - 0x18 -> dict
//     │                   ├ "cast_args" -> dict{ item_uid, predict_arg, will_cast }
//     │                   │     └ "predict_arg" -> dict{ t, arg }
//     │                   │           └ "arg" -> dict{ target_uids, remove_buff, use_id }
//     │                   │                 └ "use_id" -> list [use_seq, sub]
//     │                   └ "_cd_delta" / "cd_time"  -> 头盔冷却
//     └ "g_world_mgr" -> 实例 - 0x18 -> "cur_world" -> 实例 - 0x18
//          └ "knight_predict_use_counter" -> int，每用一次头盔 +1
//
// ── 两个必须守住的坑（本项目通用，抄自 SKILL.md §4）
//
//   1) **判 None 不能用 is_obj**：Py_None 地址 0x7969901950 也落在 is_obj 的区间里，
//      于是"字段有值"的判定全部失真。这里改用类型名判：ob_type 的 tp_name == "NoneType"。
//   2) **读字典对象要直接把地址传给 read_dict**：`getPtr64(x - 0x18)` 是"普通对象 -> 实例字典"
//      的取法，对已经是字典的值用它，会读到**另一个字典**（表现为"按名字找不到 14101"）。
//      PyDump::dump_dict_at 就是这么做的，本文件与它同一套做法。
//
// ── 线程/发布模型跟 PyProgress / PyGenius 一致
//   200ms 一轮（用户只关心"猜了几次"，不需要更高频率）；双缓冲发布，
//   g_active 用 std::atomic 而不是 volatile（理由见 PyProgress.h 同名变量的注释）。
// ============================================================================

#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <thread>
#include <atomic>
#include <ctime>
#include <unistd.h>
#include "PyRoot.h"
#include "PyCore.h"

// 日志开关（Android.mk 用 -DENABLE_LOG=0/1 传进来；兜底默认关）
#ifndef ENABLE_LOG
#define ENABLE_LOG 0
#endif

namespace PyKnight {

static const int MAX_KNIGHTS          = 4;      // 一局最多 4 个求生者，骑士最多 1 个，留余量
//   ⚠ 约瑟夫相中世界 / 红夫人镜像里的**复制体也带 knight_item_data**，
//     所以 count() 可能是 2（真人 + 镜像）。这是对的：镜像框上确实该显示同样的次数。
//     注意次数本身来自 World 计数器（全局），不会因为多一个条目而多算一次。
static const int CIVILIAN_UNIT_TYPE   = 2;
static const int ITEM_HELMET_ID       = 141;    // knight_item_data.id
static const int SKILL_PREDICT        = 14101;  // SkillCivilianProphetPredict（第 1 次预测）
//   2026-10-21 实测：一个头盔两次预测 = **两个技能对象**
//     14101 带 item_uid=5（第 1 次）
//     14102 也带 item_uid=5（第 2 次）
//   14104 / 14105 是后置（cast_args 5 个键、没有 item_uid），不参与选择
static const int SKILL_PREDICT_2      = 14102;
static const int GUESS_MAX            = 2;      // 头盔一次能猜 2 次，满 2 次就该结算了

// 猜测次数的来源。**来源必须能区分**：世界计数器是实测值，use_id 变化次数是估算值，
// 两者显示上要不一样，否则用户会把估算当实测（"读不到"和"没有"必须是两种返回值）
enum GuessSrc {
    SRC_NONE  = 0,   // 没读到，次数未知
    SRC_WORLD = 1,   // World.knight_predict_use_counter（实测）
    SRC_USEID = 2,   // 观测到的 use_id[0] 变化次数（估算，显示带 ~）
};

struct KnightState {
    bool     valid = false;

    uint64_t unit_obj  = 0;     // CivilianUnit 实例地址
    uint64_t scene_obj = 0;     // model+0x20，跟绘制循环的 data[i].obj 配对
    float    x = 0.f, y = 0.f;  // position（兜底配对用）

    // ---- 头盔物品（判"是不是骑士"的权威字段） ----
    bool     item_read = false; // knight_item_data 能读成 dict
    int      item_id   = 0;     // 期望 141

    // ---- 猜测进度 ----
    int      guesses     = 0;   // 已猜次数（显示用，已 clamp 到 0..GUESS_MAX）
    int      guesses_raw = 0;   // 原始读数（可能是 >2 的怪值，诊断用）
    int      guess_src   = SRC_NONE;
    int64_t  world_counter = -1;   // -1 = 没读到
    int64_t  cast_seq = 0;         // cast_args...use_id[0]，每用一次 +1
    int64_t  cast_seq1 = 0;        // use_id[1]（实测恒为 1，留作对照）
    bool     casting = false;      // cast_args.will_cast == True：正在读条/将要施放
    bool     casting_read = false; // will_cast 本次读到了没有（区分 false 与"读不到"）
    int      target_cnt = 0;       // predict_arg.arg.target_uids 的长度
    int64_t  last_cast_ms = 0;     // 本进程观测到"又猜了一次"的时刻（mono ms）
    bool     cast_fresh = false;   // last_cast_ms 在 3 秒内 —— 给 UI 闪一下

    // ---- 头盔冷却（skill_dict[14101]） ----
    bool     has_cd   = false;     // false = 没读到技能对象，不是"就绪"
    float    cd_remain = 0.f;
    float    cd_total  = 0.f;
    int64_t  cd_read_ms = 0;

    // ---- knight_item_data 里的数值字段（纯观测，给"下次能读到什么"留证据） ----
    int      extra_n = 0;
    char     extra_name[6][24];
    double   extra_val[6];
};

// 双缓冲发布：写线程填非活跃缓冲，填完翻转，绘制线程永远看到完整的一份
static KnightState g_buf[2][MAX_KNIGHTS];
static volatile int g_cnt[2] = {0, 0};
static std::atomic<int> g_active{0};

// 本局的全局状态（跟骑士条目同一次翻转发布）
static int64_t g_world_counter[2] = {-1, -1};   // World.knight_predict_use_counter
static int     g_scan_ok[2] = {0, 0};           // 1 = 这一轮确实枚举到了求生者列表

// 每轮的读取结果（写线程自己的工作副本）
static KnightState g_work[MAX_KNIGHTS];
static int g_work_n = 0;
static int g_work_scan_ok = 0;
static int64_t g_work_world = -1;

static uintptr_t g_libbase = 0;
static uint64_t  g_module_dict = 0;
static volatile bool g_available = false;
static volatile int  g_fail_streak = 0;
static char g_status[96] = "未启动";

// ---- 序号缓存（只读线程碰，不用锁；热更后 resolve_index 会自动重找） ----
static int64_t g_i_unit_mgr = -1, g_i_ubt = -1;
static int64_t g_i_kitem = -1, g_i_model = -1, g_i_pos = -1;
static int64_t g_i_item_id = -1;
static int64_t g_i_skillmgr = -1, g_i_skilldict = -1;
static int64_t g_i_castargs = -1, g_i_predict = -1, g_i_t_arg = -1;
static int64_t g_i_use_id = -1, g_i_willcast = -1, g_i_tgt = -1;
static int64_t g_i_cd = -1, g_i_cdtime = -1;
static int64_t g_i_wm = -1, g_i_cw = -1, g_i_wcount = -1, g_i_mcount = -1;

// ---- 观察历史：cast_args 每次使用都会被覆盖，只保留最后一次，所以要自己记 ----
// 换局判据跟 PyGenius 一致：unit_mgr 地址变了 = 换局（uid 每局重新开始）
struct Hist {
    uint64_t unit_obj = 0;
    int64_t  seen[6] = {0, 0, 0, 0, 0, 0};   // 观测到的不同 use_id[0]
    int      seen_n  = 0;
    int64_t  last_seq = 0;
    int64_t  last_cast_ms = 0;               // 上一次"序号变了"的时刻（mono ms）
};
static Hist g_hist[MAX_KNIGHTS];
static int  g_hist_n = 0;
static uint64_t g_last_unit_mgr = 0;

// ---- 公共 CPython 工具：实现在 PyCore.h（见那里的文件头） ----
using PyCore::is_obj; using PyCore::Dict; using PyCore::read_dict;
using PyCore::key_slot; using PyCore::value_slot; using PyCore::find_key; using PyCore::get_attr;
using PyCore::resolve_index; using PyCore::get_type_list; using PyCore::read_int;
using PyCore::read_list; using PyCore::read_bool; using PyCore::read_str;
using PyCore::get_inst_dict;

static inline int64_t now_ms()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// 判 None：**不能**用 is_obj（Py_None 也在区间内，见文件头坑 1）。
// 用类型名判 —— NoneType 只可能是 None，且不依赖任何写死地址。
static bool is_none_obj(uint64_t vp)
{
    if (!is_obj(vp)) return true;                 // 空指针按"没有"处理
    uint64_t tp = getPtr64(vp + 8);
    return PyRoot::type_name_is(tp, "NoneType");
}

// managed 对象的实例字典。get_inst_dict 已经校验过 ob_type == dict
static inline bool inst_dict_of(uint64_t obj, Dict &dk)
{
    uint64_t d = get_inst_dict(obj);
    return d != 0 && read_dict(d, dk);
}

// dict(int -> value) 按 int 键取。skill_dict / units_by_type 都是这种表
static uint64_t dict_get_int_key(uint64_t d, int64_t want)
{
    Dict k;
    if (!read_dict(d, k)) return 0;
    for (int64_t i = 0; i < k.n_entries; i++) {
        int64_t key = 0;
        if (!read_int(getPtr64(key_slot(k, i)), key)) continue;
        if (key == want) return getPtr64(value_slot(k, i));
    }
    return 0;
}

// 冷却是 0~600 秒级（跟 PyGenius 同一道闸），**不能套进度那条 0~100 的**
static inline bool read_seconds(uint64_t slot, float &out)
{
    return PyCore::read_number(slot, out, -1.0, 900.0);
}

// 从一个**值对象指针**（不是槽位）读数字：int / float 两种都认。
// ⚠ 别拿 PyCore::read_number 干这个 —— 它内部还要再 getPtr64(slot) 一次，
//   传值指针进去等于多解一层，读到的永远是垃圾（dump_value 里也是这么写的）
static bool read_value_num(uint64_t vp, double &out)
{
    if (!is_obj(vp)) return false;
    uint8_t b[32];
    if (!vm_readv(vp, b, 32)) return false;
    int64_t rc = 0; memcpy(&rc, b, 8);
    if (rc <= 0) return false;
    uint64_t tp = 0; memcpy(&tp, b + 8, 8); tp &= 0x00FFFFFFFFFFFFFFULL;
    if (tp == PyRoot::g_float_type) {
        double v = 0; memcpy(&v, b + 16, 8);
        if (!(v > -1e9 && v < 1e9)) return false;
        out = v;
        return true;
    }
    if (tp == PyRoot::g_int_type) {
        int64_t sz = 0; memcpy(&sz, b + 16, 8);
        uint32_t dg = 0; memcpy(&dg, b + 24, 4);
        out = (sz == 0) ? 0.0 : (double)dg;
        if (sz < 0) out = -out;
        return true;
    }
    return false;
}

// ----------------------------------------------------------------------------
// 观察历史（按 unit_obj 归位）
// ----------------------------------------------------------------------------
static Hist *hist_of(uint64_t unit_obj)
{
    for (int i = 0; i < g_hist_n; i++)
        if (g_hist[i].unit_obj == unit_obj) return &g_hist[i];
    if (g_hist_n >= MAX_KNIGHTS) return nullptr;
    g_hist[g_hist_n] = Hist();          // 不用 memset：Hist 有默认成员初值，赋一个空对象更干净
    g_hist[g_hist_n].unit_obj = unit_obj;
    return &g_hist[g_hist_n++];
}

static void hist_note(Hist &h, int64_t seq, KnightState &ks)
{
    if (seq <= 0) return;
    bool dup = false;
    for (int i = 0; i < h.seen_n; i++)
        if (h.seen[i] == seq) { dup = true; break; }
    if (!dup && h.seen_n < (int)(sizeof(h.seen) / sizeof(h.seen[0])))
        h.seen[h.seen_n++] = seq;
    // "又猜了一次"：序号变了就记时刻（UI 用来闪一下）
    if (h.last_seq != 0 && seq != h.last_seq) h.last_cast_ms = now_ms();
    h.last_seq = seq;

    ks.last_cast_ms = h.last_cast_ms;
    ks.cast_fresh   = (h.last_cast_ms != 0) && (now_ms() - h.last_cast_ms < 3000);
}

// ----------------------------------------------------------------------------
// 头盔物品：knight_item_data
// ----------------------------------------------------------------------------
// 只收数值型字段，最多 6 条。目的是给"以后想找别的线索"留一手证据：
// 万一某个版本这里真出现了 use_count 之类的东西，面板上能直接看到，不用再上 MCP 翻。
static void read_item_extras(uint64_t item_obj, KnightState &ks)
{
    Dict ik;
    if (!read_dict(item_obj, ik)) return;
    int64_t limit = ik.n_entries < 24 ? ik.n_entries : 24;   // 防 taggeddict 太大，每轮别读太多
    for (int64_t i = 0; i < limit && ks.extra_n < 6; i++) {
        char name[24] = {0};
        uint64_t kp = getPtr64(key_slot(ik, i));
        if (!read_str(kp, name, sizeof(name)) || !name[0]) continue;
        uint64_t vp = getPtr64(value_slot(ik, i));
        double v = 0;
        if (!read_value_num(vp, v)) continue;
        snprintf(ks.extra_name[ks.extra_n], sizeof(ks.extra_name[0]), "%s", name);
        ks.extra_val[ks.extra_n] = v;
        ks.extra_n++;
    }
}

// ============================================================================
// 「战术预见」猜了什么 —— 预测窗口内的高频采样（20ms）
// ============================================================================
//
// 为什么不能按名字直接读一个字段：
//   这个选择**没有常驻字段**（222 字段全量对比 / 2000MB 扫描零命中 / protobuf 无字段名），
//   它在客户端只可能以两种形态存在：
//     ① 玩家点选那一下的**瞬态状态**（UI/选择项/入参），几百毫秒后就被覆盖；
//     ② 出站 protobuf 消息里的一个数字 —— 那是方法 B（要钩 send 或扫消息实例），本工程没走。
//
// 所以这里做的是 ①：**不猜字段名，先找出所有"像预测"的字段，再逐帧比对谁在变。**
//   ① `collect_watch_fields()`：骑士一出现，就把三个字典里名字命中关键词的数值字段扫出来
//      （skill 实例 / knight_item_data / unit 本体），缓存 dict 地址 + 序号
//   ② `content_probe_thread()`：20ms 轮一轮，只看这些候选；值一变就记时间线并落盘
//   ③ 面板上把"最近变过"的那个候选当成**编码**显示出来
//   ④ 编码 → 中文 的映射由用户点一下标注（三个按钮），之后只存 codes，不存字段名
//
// ⚠ 这不是"编数据"：显示的永远是**实测读到的值**。没读到就显示"未捕获"，不显示"出刀"。
// ============================================================================

static const int MAX_WATCH = 12;

// 一个候选字段（dict 地址 + 序号 + 名字 + 上次的值）
struct WatchField {
    uint64_t dict_addr = 0;
    int64_t  hint      = -1;
    char     name[40]  = {0};
    bool     is_float  = false;
    bool     has      = false;
    int64_t  last_i    = 0;
    double   last_f    = 0.0;
    int      changes   = 0;
    int      distinct  = 0;      // 见过几种不同取值（封顶 4）
    int64_t  v[4]      = {0, 0, 0, 0};
    int64_t  change_ms = 0;
};
static WatchField g_wf[MAX_WATCH];
static int        g_wf_n = 0;

// 发布给绘制侧的一行（跟 g_active 同一次翻转）
struct WatchRow {
    char    name[40];
    int64_t v;
    int     distinct;
    int     changes;
    bool    fresh;      // 3 秒内变过
};
static WatchRow g_wrow[2][MAX_WATCH];
static int      g_wrow_n[2] = {0, 0};
static std::atomic<int> g_pp_active{0};   // 预测状态/候选行的发布下标（原子，理由同 g_active）
// 显示窗口续期请求：加载/过场/窗口重建期间 HUD 根本没画，10 秒白走。
//   绘制侧发现"有渲染空档"就写这里，采样线程下一次轮询把 show_ms 挪到此刻。
static std::atomic<int64_t> g_ttl_extend{0};

// 预测窗口状态（同一份发布）
struct PredictPub {
    bool    have      = false;    // 找到骑士的预测技能对象了没有
    bool    casting   = false;    // will_cast
    int64_t t         = 0;        // predict_arg.t（实测恒 512，留着对照）
    int64_t use_id    = 0;
    int     casts     = 0;        // 本局观测到"序号变了"的次数
    bool    code_ok   = false;    // 有可用的编码值
    char    code_name[40] = {0};
    int64_t code_val  = 0;
    int     code_distinct = 0;

    // 每次预测记一条：t（predict_arg.t，实测 512/768 都出现过）+ 当时/之后的技能 id
    // 这是"选项"最可能的两个载体，标注一次就能定死
    int     hist_n = 0;
    int64_t hist_t[8]     = {0, 0, 0, 0, 0, 0, 0, 0};
    int64_t hist_sk[8]    = {0, 0, 0, 0, 0, 0, 0, 0};

    // 最近几次"选择值"的序列（**每变一次记一条**，不是每次使用记一条）
    //   一个头盔能猜两次：如果两次都写进 predict_arg.t，这里就会有两条
    int     pick_n = 0;
    int64_t pick_v[3]  = {0, 0, 0};
    int64_t pick_ms[3] = {0, 0, 0};

    // 当前"一次头盔窗口"内的选择缓冲：一个头盔能猜两次，两次都在这里
    //   ⚠ 不能只记"提交那一刻"的 t：窗口内先选的那次会被后一次盖掉（就是"第二次不显示"）
    int     win_n = 0;
    int64_t win_v[4] = {0, 0, 0, 0};

    // 最近两次**提交**（一个头盔两次预测就是这两条）
    bool    pair_ok = false;
    int64_t pair_gap_ms = 0;

    // 显示有效期：每次出现新预测就把时间戳刷新，UI 只在这之后 10 秒内显示
    int64_t show_ms = 0;

    // 当前这一顶头盔的版本戳（= arg.use_id[0]）；变了就是新头盔
    int64_t cur_use = -1;
};
static PredictPub g_pp[2];
static PredictPub g_pp_work;

// 编码 → 中文 的映射（由用户点选标注，持久化在配置里；-1 = 还没标注）
//   0 = 出刀  1 = 技能  2 = 什么都不做
//   2026-10-21 实测：512 = 出刀、768 = 技能（用户确认）；
//   1024 = 什么都不做（从 sk14102 的 predict_arg.t 实测到第三种取值，
//   512/768/1024 = 0x200/0x300/0x400 正好三档，与"三个选项"一一对应）
static int64_t g_bind_code[3] = {512, 768, 1024};

// 探测线程的工作变量
static uint64_t g_probe_skill_obj = 0;    // 骑士的 SkillCivilianProphetPredict 实例
static int64_t  g_probe_last_use  = 0;
static bool     g_probe_last_will = false;
static bool     g_probe_on        = false;
static bool     g_first_logged    = false;   // 本局是否已经记过"首次捕获"
// 本局开始时刻（match reset 时更新）。开局那段时间画面还在加载/过场，
//   Draw_Main 会 `if (read_state == 0) return;` 直接不画 —— 10 秒窗口白白流走，
//   所以**开局 30 秒内用 30 秒窗口**，之后回到用户要求的 10 秒。
static int64_t  g_match_start_ms  = 0;
static bool     g_probe_need_dump = false;   // 事件点要打整链快照（只在一帧里为真）
static uint64_t g_probe_skill_dict = 0;      // 技能表（skill_dict）地址，collect 时缓存
static uint64_t g_probe_item_obj   = 0;      // knight_item_data（小字典，全部数值都挂上）
static uint64_t g_probe_skill_mgr  = 0;      // SkillManagerCivilian（last_cast_skill_id 在这里）
static int64_t  g_probe_last_skill = 0;      // 最近一次读到的 last_cast_skill_id
static int64_t  g_i_stage = -1;              // skill.current_cast_stage 的序号缓存
static int64_t  g_pending_sid   = 0;      // 施放事件待读：要读哪个技能的 t
static int64_t  g_pending_until = 0;      // 读到这个时刻为止（毫秒）
static int64_t  g_last_stage = -1;           // 上一次读到的 stage（找"第几次猜测"）

// 「战术预见」的选项极可能就是**另外几个技能 id**：实测 skill_dict 里有
//   14101(起手) 14102 14103 14104(后置) 14105
// 所以除了看 cast_args，还要盯两处：
//   ① skill_mgr.last_cast_skill_id —— 选中的那个技能 id（最可能是答案）
//   ② 五个技能各自的 _cd_delta —— 被选中的那个通常会有变化
static const int MAX_SK = 8;
struct SkillWatch {
    int64_t  sid = 0;
    uint64_t obj = 0;
    int64_t  hint_cd = -1;
    // 每个技能自己的 cast_args -> predict_arg -> t 的序号缓存
    //   ★ 实测：一个头盔两次预测是**两个不同的技能对象**（14101 第一次、14102 第二次），
    //     只盯 14101 的话第二次永远读不到（这就是"第二次不显示"的真正原因）
    int64_t  hint_ca = -1, hint_pa = -1, hint_t = -1;
    int64_t  last_t = 0;
    int64_t  last_use = -1;      // 这个技能链上 arg.use_id[0] 的版本戳
    bool     has_t = false;
};
static SkillWatch g_sk[MAX_SK];
static int        g_sk_n = 0;

static void sk_reset()
{
    g_sk_n = 0;
    for (int i = 0; i < MAX_SK; i++) g_sk[i] = SkillWatch();
}
// static char g_probe_log_path[64] = GUARD_APP_LOG();  ← 数组初始化，明文残留
// 改为调用点宏（2026-10-22）

static const char* kPredictLogPath() { return GUARD_APP_LOG(); }

static void probe_log(const char *fmt, ...)
{
#if ENABLE_LOG
    //   ★ 统一出口：只写 /data/adb/.d/o.log（用户要求：所有日志只落这一个文件）
    //     ENABLE_LOG=0 时整个函数被编成空壳，连文件都不开
    va_list ap;
    va_start(ap, fmt);
    FILE *f = fopen(GUARD_APP_LOG(), "a");
    if (f) {
        fputs("[knight] ", f);
        vfprintf(f, fmt, ap);
        fputc(10, f);
        fclose(f);
    }
    va_end(ap);
#else
    (void)fmt;
#endif
}

// 名字是"像预测"的那种吗（大小写不敏感的子串）
static bool name_is_predictish(const char *n)
{
    if (!n || !n[0]) return false;
    static const char *kw[] = {
        "predict", "prophet", "toukui", "guess", "choice", "choose",
        "select", "option", "result", "success", "foresee",
    };
    char low[40];
    int i = 0;
    for (; n[i] && i < (int)sizeof(low) - 1; i++) {
        char c = n[i];
        low[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    low[i] = 0;
    for (unsigned k = 0; k < sizeof(kw) / sizeof(kw[0]); k++)
        if (strstr(low, kw[k])) return true;
    return false;
}

// 扫一个字典里名字命中关键词的数值字段，追加进候选表
static void scan_watch_dict(uint64_t dict_addr, int cap_total)
{
    if (!is_obj(dict_addr) || g_wf_n >= cap_total) return;
    Dict dk;
    if (!read_dict(dict_addr, dk)) return;

    int64_t limit = dk.n_entries < 400 ? dk.n_entries : 400;   // unit 字典上千条，封顶防卡
    for (int64_t i = 0; i < limit && g_wf_n < cap_total; i++) {
        char nm[40] = {0};
        if (!read_str(getPtr64(key_slot(dk, i)), nm, sizeof(nm))) continue;
        if (!name_is_predictish(nm)) continue;
        // 已经收过同名同字典的跳过
        bool dup = false;
        for (int k = 0; k < g_wf_n; k++)
            if (g_wf[k].dict_addr == dict_addr && strcmp(g_wf[k].name, nm) == 0) { dup = true; break; }
        if (dup) continue;

        // 只收数值型：int 或 float
        uint64_t vp = getPtr64(value_slot(dk, i));
        int64_t iv = 0;
        double  fv = 0.0;
        bool is_i = read_int(vp, iv);
        bool is_f = false;
        if (!is_i) {
            if (!read_value_num(vp, fv)) continue;
            // 只认 float 类型（read_value_num 也认 int，这里区分一下）
            uint64_t tp = getPtr64(vp + 8);
            is_f = (tp == PyRoot::g_float_type);
            if (!is_f) { iv = (int64_t)fv; is_i = true; }
        }
        WatchField &w = g_wf[g_wf_n];
        w = WatchField();
        w.dict_addr = dict_addr;
        w.hint      = i;
        w.is_float  = is_f;
        w.has       = true;
        w.last_i    = is_i ? iv : (int64_t)fv;
        w.last_f    = is_f ? fv : (double)iv;
        snprintf(w.name, sizeof(w.name), "%s", nm);
        g_wf_n++;
    }
}

// 骑士一出现就收一次候选（三个字典：技能实例 / 头盔物品 / 单位本体）
// 前置声明：候选表在 collect 时被重置，而 cv_reset 的实现放在下面（链上数值那一节）
static void cv_reset();

// ----------------------------------------------------------------------------
// 属性审计：把"可能有第二次预测"的容器全打出来（带类型）
// ----------------------------------------------------------------------------
// 之前的关键词扫描只收 **int** 字段，list/dict 型的（比如 predict_list / remove_buff）
// 直接跳过了 —— 而"一个头盔两次预测"最可能就存在一个长度 2 的容器里。
// 这个函数只跑一次（collect 时），把命中关键词的字段连**类型**一起打进日志：
//   int / float 直接给值；list 给长度 + 前 3 个元素；dict 给键数；其它给类型名。
static void type_name_of_obj(uint64_t vp, char *out, size_t cap)
{
    if (!out || cap == 0) return;
    out[0] = 0;
    if (!is_obj(vp)) return;
    uint64_t tp = getPtr64(vp + 8);
    if (!is_obj(tp)) return;
    uint64_t np = getPtr64(tp + 0x18);        // PyTypeObject.tp_name (C 字符串)
    if (!is_obj(np)) return;
    vm_readv(np, out, cap - 1);
    out[cap - 1] = 0;
}

static bool name_is_interesting(const char *n)
{
    if (!n || !n[0]) return false;
    if (name_is_predictish(n)) return true;
    static const char *kw[] = {"use", "buff", "target", "arg", "item", "t_", "stage", "times", "count", "list"};
    char low[48];
    int i = 0;
    for (; n[i] && i < (int)sizeof(low) - 1; i++) {
        char c = n[i];
        low[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    low[i] = 0;
    for (unsigned k = 0; k < sizeof(kw) / sizeof(kw[0]); k++)
        if (strstr(low, kw[k])) return true;
    return false;
}

static void audit_dict_log(const char *tag, uint64_t dict_addr, int cap = 400)
{
    Dict dk;
    if (!read_dict(dict_addr, dk)) { probe_log("[audit] %s: 字典读不到", tag); return; }
    probe_log("[audit] %s: %lld keys", tag, (long long)dk.n_entries);
    int64_t limit = dk.n_entries < cap ? dk.n_entries : cap;
    for (int64_t i = 0; i < limit; i++) {
        char nm[48] = {0};
        if (!read_str(getPtr64(key_slot(dk, i)), nm, sizeof(nm)) || !nm[0]) continue;
        if (!name_is_interesting(nm)) continue;
        uint64_t vp = getPtr64(value_slot(dk, i));
        int64_t iv = 0;
        if (read_int(vp, iv)) { probe_log("   %-28s int   = %lld", nm, (long long)iv); continue; }
        int64_t n = 0; uint64_t items = 0;
        if (read_list(vp, n, items)) {
            char seg[96] = {0};
            for (int64_t k = 0; k < n && k < 3; k++) {
                int64_t ev = 0;
                char one[24];
                if (read_int(getPtr64(items + k * 8), ev)) snprintf(one, sizeof(one), "%s%lld", k ? "," : "", (long long)ev);
                else                                      snprintf(one, sizeof(one), "%s?", k ? "," : "");
                strncat(seg, one, sizeof(seg) - strlen(seg) - 1);
            }
            probe_log("   %-28s list  = [%lld] %s", nm, (long long)n, seg);
            continue;
        }
        Dict sub;
        if (read_dict(vp, sub)) { probe_log("   %-28s dict  = %lld keys", nm, (long long)sub.n_entries); continue; }
        char tn[32] = {0};
        type_name_of_obj(vp, tn, sizeof(tn));
        probe_log("   %-28s %-5s = <%s>", nm, tn[0] ? tn : "obj", tn[0] ? tn : "?");
    }
}

static void collect_watch_fields(uint64_t skill_obj, uint64_t skill_dict,
                                 uint64_t item_obj, uint64_t unit_obj,
                                 uint64_t skill_mgr)
{
    // 收表期间先停掉 20ms 那条线：它是另一个线程，正写到一半时它会读到半截表
    g_probe_on = false;
    g_wf_n = 0;
    g_probe_skill_dict = skill_dict;     // 事件快照要用它打技能表
    g_probe_item_obj   = item_obj;       // 头盔物品：字段少，全量挂上
    g_probe_skill_mgr  = skill_mgr;
    cv_reset();
    sk_reset();

    // 把 skill_dict 里所有 1410x 技能挂上看门狗（每个只看 _cd_delta）
    Dict sdk;
    if (read_dict(skill_dict, sdk)) {
        for (int64_t i = 0; i < sdk.n_entries && g_sk_n < MAX_SK; i++) {
            int64_t sid = 0;
            if (!read_int(getPtr64(key_slot(sdk, i)), sid)) continue;
            uint64_t sk = getPtr64(value_slot(sdk, i));
            if (!is_obj(sk)) continue;
            g_sk[g_sk_n].sid  = sid;
            g_sk[g_sk_n].obj  = sk;
            g_sk_n++;
        }
    }
    uint64_t ds = get_inst_dict(skill_obj);
    if (ds) scan_watch_dict(ds, MAX_WATCH);
    if (is_obj(item_obj)) scan_watch_dict(item_obj, MAX_WATCH);
    //   ⚠ 单位字典（1300+ 键）**不扫**：开局扫一遍要上千次驱动读，会把 collect 拖慢，
    //     而它只贡献"面板上显示原始值"，对识别预测没有作用（识别只看 skill_dict 那 5 个技能）
#if PY_KNIGHT_AUDIT
    // ★ 一次性审计（默认关）：把可能装"第二次预测"的容器连类型打出来
    //   ⚠ 它一次会打 400 行、每行 fopen/fclose，跑的时候 20ms 采样是关着的 ——
    //     开局第一顶头盔会正好撞进来被漏掉（真机上就是这么漏的）
    probe_log("[audit] ==== 属性审计开始 ====");
    if (is_obj(unit_obj))     audit_dict_log("unit",      get_inst_dict(unit_obj));
    if (is_obj(skill_obj))    audit_dict_log("skill",     get_inst_dict(skill_obj));
    if (is_obj(skill_mgr))    audit_dict_log("skill_mgr", get_inst_dict(skill_mgr));
    if (is_obj(item_obj))     audit_dict_log("item",      item_obj);
    if (is_obj(skill_dict)) {
        Dict sdk;
        if (read_dict(skill_dict, sdk)) {
            for (int64_t i2 = 0; i2 < sdk.n_entries && i2 < 8; i2++) {
                int64_t sid = 0;
                if (!read_int(getPtr64(key_slot(sdk, i2)), sid)) continue;
                uint64_t sk2 = getPtr64(value_slot(sdk, i2));
                if (!is_obj(sk2)) continue;
                char tg[32];
                snprintf(tg, sizeof(tg), "sk[%lld]", (long long)sid);
                audit_dict_log(tg, get_inst_dict(sk2), 120);
            }
        }
    }
    probe_log("[audit] ==== 属性审计结束 ====");
#endif // PY_KNIGHT_AUDIT


    probe_log("[collect] 候选 %d 个:", g_wf_n);
    for (int i = 0; i < g_wf_n; i++)
        probe_log("   %s%s", g_wf[i].name, g_wf[i].is_float ? "(f)" : "(i)");
    // collect 结束，立刻恢复采样（不让采样线因为 collect 停顿）
    g_probe_on = true;
}

// 读候选表的当前值；变了就更新统计（**这是整个探测的核心**）
static void poll_watch_fields(int64_t now)
{
    for (int i = 0; i < g_wf_n; i++) {
        WatchField &w = g_wf[i];
        Dict dk;
        if (!read_dict(w.dict_addr, dk)) continue;
        w.hint = resolve_index(w.dict_addr, dk, w.hint, w.name);
        if (w.hint < 0) continue;
        uint64_t vp = getPtr64(value_slot(dk, w.hint));

        int64_t iv = w.last_i;
        double  fv = w.last_f;
        bool got = false;
        if (w.is_float) {
            double d = 0.0;
            if (read_value_num(vp, d)) { fv = d; iv = (int64_t)d; got = true; }
        } else {
            int64_t t = 0;
            if (read_int(vp, t)) { iv = t; got = true; }
        }
        if (!got) continue;

        bool changed = w.has && (w.is_float ? (fv != w.last_f) : (iv != w.last_i));
        if (changed) {
            w.changes++;
            w.change_ms = now;
            bool seen = false;
            for (int k = 0; k < w.distinct && k < 4; k++) if (w.v[k] == iv) { seen = true; break; }
            if (!seen && w.distinct < 4) w.v[w.distinct++] = iv;
            probe_log("  [%lld] %s: %lld -> %lld   (distinct=%d)",
                      (long long)now, w.name, (long long)w.last_i, (long long)iv, w.distinct);
        }
        w.has    = true;
        w.last_i = iv;
        w.last_f = fv;
    }
}

// ---- 链上所有数值字段（**不看名字**）----
// 为什么要有这一层：实测名字里带 predict 的字段只有一个 can_predict，
// 而"他选了哪个"这种数据完全可以叫 `k` / `v` / `id`。
// 所以把 cast_args / predict_arg / arg 三个字典里**所有**数值键都挂上来逐帧比对。
static const int MAX_CV = 16;
struct ChainVal {
    char    name[48];
    bool    has;
    int64_t last;
    int     changes;
    int     distinct;
    int64_t v[4];
    int64_t change_ms;
};
static ChainVal g_cv[MAX_CV];
static int      g_cv_n = 0;

static void cv_reset()
{
    g_cv_n = 0;
    for (int i = 0; i < MAX_CV; i++) g_cv[i] = ChainVal();
}

static void cv_note(const char *name, int64_t val, int64_t now)
{
    for (int i = 0; i < g_cv_n; i++) {
        ChainVal &c = g_cv[i];
        if (strcmp(c.name, name) != 0) continue;
        bool changed = c.has && c.last != val;
        if (changed) {
            c.changes++;
            c.change_ms = now;
            bool seen = false;
            for (int k = 0; k < c.distinct && k < 4; k++) if (c.v[k] == val) { seen = true; break; }
            if (!seen && c.distinct < 4) c.v[c.distinct++] = val;
            probe_log("  [%lld] %s: %lld -> %lld   (distinct=%d)",
                      (long long)now, c.name, (long long)c.last, (long long)val, c.distinct);
        }
        c.has  = true;
        c.last = val;
        return;
    }
    if (g_cv_n >= MAX_CV) return;
    ChainVal &c = g_cv[g_cv_n++];
    c = ChainVal();
    snprintf(c.name, sizeof(c.name), "%s", name);
    c.has  = true;
    c.last = val;
}

// 整链原样落盘（只在 will_cast 上升沿 / use_id 变化时调一次，不是每帧）
static void dump_dict_log(const char *tag, uint64_t dict_addr, int64_t cap = 40)
{
    Dict dk;
    if (!read_dict(dict_addr, dk)) { probe_log("    %s: (读不到)", tag); return; }
    probe_log("    %s: %lld keys", tag, (long long)dk.n_entries);
    int64_t limit = dk.n_entries < cap ? dk.n_entries : cap;
    for (int64_t i = 0; i < limit; i++) {
        char nm[48] = {0};
        uint64_t kp = getPtr64(key_slot(dk, i));
        uint64_t vp = getPtr64(value_slot(dk, i));
        if (!read_str(kp, nm, sizeof(nm))) {
            int64_t ki = 0;
            if (read_int(kp, ki)) snprintf(nm, sizeof(nm), "#%lld", (long long)ki);
            else                  snprintf(nm, sizeof(nm), "?");
        }
        int64_t iv = 0; double fv = 0.0; char sv[48] = {0};
        if (read_int(vp, iv))                   probe_log("      %s = %lld", nm, (long long)iv);
        else if (read_value_num(vp, fv))        probe_log("      %s = %g", nm, fv);
        else if (read_str(vp, sv, sizeof(sv)))  probe_log("      %s = '%s'", nm, sv);
        else                                    probe_log("      %s = <obj>", nm);
    }
}

// 技能表里都有谁（有些服会把三个选项做成三个技能 id）
static void dump_skill_dict_log(uint64_t sd)
{
    Dict dk;
    if (!read_dict(sd, dk)) { probe_log("  skill_dict: (读不到)"); return; }
    probe_log("  skill_dict: %lld 项", (long long)dk.n_entries);
    for (int64_t i = 0; i < dk.n_entries && i < 64; i++) {
        int64_t sid = 0;
        if (!read_int(getPtr64(key_slot(dk, i)), sid)) continue;
        uint64_t sk = getPtr64(value_slot(dk, i));
        char tn[48] = {0};
        if (is_obj(sk)) {
            uint64_t tp = getPtr64(sk + 8);
            if (is_obj(tp)) read_str(getPtr64(tp + 0x18), tn, sizeof(tn));
        }
        probe_log("    [%lld] %s", (long long)sid, tn[0] ? tn : "(?)");
    }
}

// ---- 技能字典全量快照 / 差分 ----
// 为什么需要：实测「一个头盔猜两次」时，第二次**没有**改 `predict_arg.t`
// （日志：use_id 42→43 但 t 一直是 512），所以第二次一定写在别处。
// 这里在窗口打开时把 222 个 int 字段全拍一张，窗口关/下次使用时再拍一张做差，
// **不看名字**，谁变了就把它打出来 —— 这是找"第二次"最直接的办法。
static const int SNAP_MAX = 260;
struct SnapItem { char name[40]; int64_t v; };
static SnapItem g_snap[SNAP_MAX];
static int       g_snap_n = 0;

static void snap_skill_dict(uint64_t skill_obj)
{
    g_snap_n = 0;
    Dict dk;
    if (!inst_dict_of(skill_obj, dk)) return;
    for (int64_t i = 0; i < dk.n_entries && g_snap_n < SNAP_MAX; i++) {
        char nm[40] = {0};
        if (!read_str(getPtr64(key_slot(dk, i)), nm, sizeof(nm)) || !nm[0]) continue;
        int64_t iv = 0;
        if (!read_int(getPtr64(value_slot(dk, i)), iv)) continue;   // 只盯 int 字段
        snprintf(g_snap[g_snap_n].name, sizeof(g_snap[0].name), "%s", nm);
        g_snap[g_snap_n].v = iv;
        g_snap_n++;
    }
    probe_log("[snap] 技能字典 %d 个 int 字段", g_snap_n);
}

static void diff_skill_dict(uint64_t skill_obj, const char *tag)
{
    Dict dk;
    if (!inst_dict_of(skill_obj, dk)) return;
    int changes = 0;
    for (int64_t i = 0; i < dk.n_entries; i++) {
        char nm[40] = {0};
        if (!read_str(getPtr64(key_slot(dk, i)), nm, sizeof(nm)) || !nm[0]) continue;
        int64_t iv = 0;
        if (!read_int(getPtr64(value_slot(dk, i)), iv)) continue;
        for (int k = 0; k < g_snap_n; k++) {
            if (strcmp(g_snap[k].name, nm) != 0) continue;
            if (g_snap[k].v != iv)
                probe_log("[diff:%s] %s: %lld -> %lld", tag, nm,
                          (long long)g_snap[k].v, (long long)iv);
            changes++;
            break;
        }
    }
    if (changes == 0) probe_log("[diff:%s] 技能字典无变化", tag);
}

// 把"当前这顶头盔已经采到的选择"立刻发布出去
//   ★ 关键：**不要等到 use_id 提交时才发布**。实测第二次预测的数据比第一次晚约 1.8 秒才落到
//     14102 上，等到提交再发就永远是上一次头盔的内容（用户报的"慢一轮"就是这个）。
//     这里每采到一次就刷新，UI 上先是"技能"，1~2 秒后变成"技能->出刀"。
static void publish_picks(int64_t now)
{
    int n = g_pp_work.win_n;
    if (n > 2) n = 2;
    if (n < 0) n = 0;
    for (int i = 0; i < n; i++) {
        g_pp_work.pick_v[i]  = g_pp_work.win_v[i];
        g_pp_work.pick_ms[i] = now;
    }
    g_pp_work.pick_n  = n;
    //   ★ 用户要求：**第二次预测不续期**。10 秒只在"这一顶头盔的第一次预测"出现时开始计，
    //     所以只有 n==1 才刷新 show_ms；n==2 时保持第一次的时刻不动。
    if (n == 1) g_pp_work.show_ms = now;
    g_pp_work.pick_n  = n;
    g_pp_work.pair_ok = (n >= 2);
}

// 从**任意**技能对象里取 predict_arg.t（五个 1410x 都可能带着自己的选择）
// 走链：skill.__dict__ -> cast_args -> predict_arg -> t，全程用序号缓存
static bool skill_predict_t(SkillWatch &w, int64_t &t, int64_t &use_seq)
{
    t = 0; use_seq = 0;
    Dict skd;
    if (!inst_dict_of(w.obj, skd)) return false;
    w.hint_ca = resolve_index(get_inst_dict(w.obj), skd, w.hint_ca, "cast_args");
    if (w.hint_ca < 0) return false;
    uint64_t ca = getPtr64(value_slot(skd, w.hint_ca));
    if (is_none_obj(ca)) return false;
    Dict cad;
    if (!read_dict(ca, cad)) return false;

    w.hint_pa = resolve_index(ca, cad, w.hint_pa, "predict_arg");
    if (w.hint_pa < 0) return false;
    uint64_t pa = getPtr64(value_slot(cad, w.hint_pa));
    if (is_none_obj(pa)) return false;
    Dict pad;
    if (!read_dict(pa, pad)) return false;

    w.hint_t = resolve_index(pa, pad, w.hint_t, "t");
    if (w.hint_t < 0) return false;
    int64_t tv = 0;
    if (!read_int(getPtr64(value_slot(pad, w.hint_t)), tv)) return false;
    t = tv;

    int64_t ia = resolve_index(pa, pad, -1, "arg");
    if (ia >= 0) {
        uint64_t ar = getPtr64(value_slot(pad, ia));
        Dict ard;
        if (read_dict(ar, ard)) {
            int64_t iu = resolve_index(ar, ard, -1, "use_id");
            if (iu >= 0) {
                int64_t n = 0; uint64_t items = 0;
                if (read_list(getPtr64(value_slot(ard, iu)), n, items) && n >= 1)
                    read_int(getPtr64(items), use_seq);
            }
        }
    }
    return true;
}

// 轻量读一次预测状态，并把**整条链上的所有数值字段**都喂给 cv_note 逐帧比对
// 每秒按技能表补挂缺失的技能（开局 14101/14102 是第一次用头盔时才创建的）
// 每轮都把技能表和观察表对齐（去掉 1 秒节流：只有 5 个键、十几次驱动读，20ms 一轮完全够用）
//   ★ 为什么不再节流：换局后观察表被清空、而对象地址可能被复用，节流会让"重新对齐"
//     最多晚 1 秒 —— 正好错过开局第一顶头盔（真机上就是这么漏的）。
static void ensure_skills(int64_t now)
{
    (void)now;
    Dict sdk;
    if (!read_dict(g_probe_skill_dict, sdk)) return;
    for (int64_t i = 0; i < sdk.n_entries && g_sk_n < MAX_SK; i++) {
        int64_t sid = 0;
        if (!read_int(getPtr64(key_slot(sdk, i)), sid)) continue;
        uint64_t sk = getPtr64(value_slot(sdk, i));
        if (!is_obj(sk)) continue;
        int found = -1;
        for (int k = 0; k < g_sk_n; k++) if (g_sk[k].sid == sid) { found = k; break; }
        if (found >= 0) {
            g_sk[found].obj = sk;          // 对象可能被重建，地址跟着更新
            continue;
        }
        g_sk[g_sk_n].sid = sid;
        g_sk[g_sk_n].obj = sk;
        g_sk_n++;
        probe_log("[skills] 补挂 sk%lld（技能表里新出现）", (long long)sid);
    }
}

static void scan_skill_ticks(int64_t now)
{
    // ---- ★ 五个 1410x 技能各扫一遍 predict_arg.t ----
    //   一个头盔两次预测 = 14101 一次 + 14102 一次（实测这两个都带 item_uid=5），
    //   所以"第二次"必须从 14102 身上读，盯 14101 是读不到的。
    //   ★ 判据：**等这个技能链上的 use_id 版本戳变化**再采信它的 t。
    //     之前是"技能施放事件一到就读"，那时 cast_args 还没写新值 —— 读到的**永远是上一次头盔的**
    //     （用户报的"一直识别上次的头盔"就是这个）。use_id 变了才说明这次的数据落地了。
    for (int k = 0; k < g_sk_n; k++) {
        int64_t tv = 0, use_seq = 0;
        if (!skill_predict_t(g_sk[k], tv, use_seq)) continue;
        if (tv <= 0) continue;

        char key[48];
        snprintf(key, sizeof(key), "sk%lld.t", (long long)g_sk[k].sid);
        cv_note(key, tv, now);

        //   ★★ 2026-10-21 最终修法：**不要依赖 use_id** ★★
        //     实测心跳：14101/14102 的 t 读得到（512/768），但 arg.use_id 一直是 **0**
        //     （这一版游戏里那条链没写 use_id）。老判据要求 `use_seq>0` 才算"第一次"，
        //     于是第一顶头盔永远不记录、后面 t 不变也触发不了 —— "第一次不显示"的真因。
        //   现在的判据只看"这个技能有没有新数据"：
        //     · 首次读到 t            -> 算新数据
        //     · t 变了                -> 算新数据
        //     · use 变了（能用时）     -> 算新数据
        bool fresh_data = false;
        if (!g_sk[k].has_t) {
            fresh_data = true;                       // 首次读到这个技能就有数据
        } else {
            if (g_sk[k].last_t != tv) fresh_data = true;
            if (use_seq > 0 && g_sk[k].last_use > 0 && use_seq != g_sk[k].last_use) fresh_data = true;
        }
        if (fresh_data) {
            // ★ 本局第一顶头盔被捕获时记一条：用来判断"前几次漏掉"到底漏了几顶
            {
                static bool s_first_logged = false;
                if (!s_first_logged) {
                    s_first_logged = true;
                    probe_log("[first] 本局首次捕获预测: use=%lld t=%lld（若 use>1 说明前面漏了 use-1 顶）",
                              (long long)use_seq, (long long)tv);
                }
            }
            // 14101 出现新数据 = 换了一顶头盔：清空这一顶的缓冲（不看 use_id，实测它可能是 0）
            if (g_sk[k].sid == SKILL_PREDICT) {
                g_pp_work.win_n   = 0;
                g_pp_work.cur_use = use_seq;
                probe_log("[helmet] 新头盔（14101 新数据，use=%lld）", (long long)use_seq);
            }
            if (g_pp_work.win_n < 4) {
                g_pp_work.win_v[g_pp_work.win_n++] = tv;
                probe_log("[win] t=%lld sk%lld 第 %d 次 = %lld (use=%lld)",
                          (long long)now, (long long)g_sk[k].sid,
                          g_pp_work.win_n, (long long)tv, (long long)use_seq);
            }
            // ★ 采到就立刻发布（不要等提交，否则永远慢一个头盔）
            publish_picks(now);
        }
        g_sk[k].has_t    = true;
        g_sk[k].last_t   = tv;
        g_sk[k].last_use = use_seq;
    }
}

static void poll_cast_state(uint64_t skill_obj)
{
    if (!is_obj(skill_obj)) return;
    Dict skd;
    if (!inst_dict_of(skill_obj, skd)) return;
    int64_t now = now_ms();

    // current_cast_stage：窗口内**唯一**会变的技能字段（实测 2 -> 1）
    // 它极可能就是"第几次猜测"——一个头盔两次预测时，stage 会走两次
    g_i_stage = resolve_index(get_inst_dict(skill_obj), skd, g_i_stage, "current_cast_stage");
    if (g_i_stage >= 0) {
        int64_t sv = 0;
        if (read_int(getPtr64(value_slot(skd, g_i_stage)), sv)) {
            cv_note("stage", sv, now);
            if (g_last_stage != sv) {
                probe_log("[stage] current_cast_stage = %lld   (t=%lld)",
                          (long long)sv, (long long)g_pp_work.t);
                g_last_stage = sv;
            }
        }
    }

    g_i_castargs = resolve_index(get_inst_dict(skill_obj), skd, g_i_castargs, "cast_args");
    if (g_i_castargs < 0) return;
    uint64_t ca = getPtr64(value_slot(skd, g_i_castargs));
    if (is_none_obj(ca)) return;
    Dict cad;
    if (!read_dict(ca, cad)) return;

    bool wc = false;
    g_i_willcast = resolve_index(ca, cad, g_i_willcast, "will_cast");
    if (g_i_willcast >= 0 && read_bool(value_slot(cad, g_i_willcast), wc)) {
        if (wc && !g_probe_last_will) {
            //   ★★ 2026-10-21 修「同一顶头盔只显示第一次」★★
            //     这里原来有 `g_pp_work.win_n = 0;` —— will_cast 上升沿（第二次预测开始时）
            //     会把**刚采到的第 1 次**抹掉，于是第二次被当成第 1 次，屏幕上只剩一条。
            //     会话现在只由"14101 出现新数据"定义，这里不再清零。
            // 窗口打开 = 最可能带选项/状态的时刻：整链落盘 + 技能字典拍快照
            probe_log("[cast] will_cast 1（预测窗口打开）→ 整链快照:");
            dump_dict_log("cast_args", ca);
            g_probe_need_dump = true;
            snap_skill_dict(skill_obj);
        } else if (!wc && g_probe_last_will && g_snap_n > 0) {
            // 窗口关闭：跟刚才那张快照做差，谁变了就是"这次选择"写进去的东西
            probe_log("[cast] will_cast 0（窗口关闭）→ 技能字典差分:");
            diff_skill_dict(skill_obj, "close");
            g_snap_n = 0;
        }
        g_pp_work.casting = wc;
        g_probe_last_will = wc;
    }

    // cast_args 里除 will_cast 之外的数值字段也挂上（item_uid 之类）
    for (int64_t i = 0; i < cad.n_entries && i < 24; i++) {
        char nm[32] = {0};
        if (!read_str(getPtr64(key_slot(cad, i)), nm, sizeof(nm)) || !nm[0]) continue;
        if (!strcmp(nm, "will_cast")) continue;
        int64_t iv = 0;
        if (read_int(getPtr64(value_slot(cad, i)), iv)) {
            char key[48];
            snprintf(key, sizeof(key), "cast_args.%s", nm);
            cv_note(key, iv, now);
        }
    }

    g_i_predict = resolve_index(ca, cad, g_i_predict, "predict_arg");
    if (g_i_predict < 0) return;
    uint64_t pa = getPtr64(value_slot(cad, g_i_predict));
    if (is_none_obj(pa)) return;
    Dict pad;
    if (!read_dict(pa, pad)) return;

    g_i_t_arg = resolve_index(pa, pad, g_i_t_arg, "t");
    if (g_i_t_arg >= 0) {
        int64_t t = 0;
        if (read_int(getPtr64(value_slot(pad, g_i_t_arg)), t)) {
            // 窗口内的序列改由下面"五个技能各扫一遍"统一记（避免 14101 被记两次）
            g_pp_work.t = t;
            cv_note("predict_arg.t", t, now);
        }
    }
    // predict_arg 里其它数值字段
    for (int64_t i = 0; i < pad.n_entries && i < 16; i++) {
        char nm[32] = {0};
        if (!read_str(getPtr64(key_slot(pad, i)), nm, sizeof(nm)) || !nm[0]) continue;
        if (!strcmp(nm, "t") || !strcmp(nm, "arg")) continue;
        int64_t iv = 0;
        if (read_int(getPtr64(value_slot(pad, i)), iv)) {
            char key[48];
            snprintf(key, sizeof(key), "predict_arg.%s", nm);
            cv_note(key, iv, now);
        }
    }

    int64_t aidx = resolve_index(pa, pad, -1, "arg");
    if (aidx < 0) return;
    uint64_t ar = getPtr64(value_slot(pad, aidx));
    Dict ard;
    if (!read_dict(ar, ard)) return;

    // arg 里**每一个**数值字段（不看名字）：三个选项最可能就藏在这里
    for (int64_t i = 0; i < ard.n_entries && i < 24; i++) {
        char nm[32] = {0};
        if (!read_str(getPtr64(key_slot(ard, i)), nm, sizeof(nm)) || !nm[0]) continue;
        uint64_t vp = getPtr64(value_slot(ard, i));
        int64_t iv = 0;
        if (read_int(vp, iv)) {
            char key[48];
            snprintf(key, sizeof(key), "arg.%s", nm);
            cv_note(key, iv, now);
        }
    }

    // target_uids 也挂上：三个选项有可能伪装成"目标 uid"（长度 + 前三个值）
    {
        int64_t it = resolve_index(ar, ard, -1, "target_uids");
        if (it >= 0) {
            int64_t n = 0; uint64_t items = 0;
            if (read_list(getPtr64(value_slot(ard, it)), n, items)) {
                cv_note("arg.target_uids.len", n, now);
                for (int64_t k = 0; k < n && k < 3; k++) {
                    int64_t uv = 0;
                    if (read_int(getPtr64(items + k * 8), uv)) {
                        char key[48];
                        snprintf(key, sizeof(key), "arg.target_uids[%lld]", (long long)k);
                        cv_note(key, uv, now);
                    }
                }
            }
        }
    }

    // use_id[1]：文档说恒为 1，但"第几次预测"如果存在，最可能就藏在这个第二元素里
    {
        int64_t ir = resolve_index(ar, ard, -1, "remove_buff");
        if (ir >= 0) {
            int64_t n = 0; uint64_t items = 0;
            if (read_list(getPtr64(value_slot(ard, ir)), n, items)) {
                cv_note("arg.remove_buff.len", n, now);
                for (int64_t k = 0; k < n && k < 2; k++) {
                    int64_t bv = 0;
                    if (read_int(getPtr64(items + k * 8), bv)) {
                        char key[48];
                        snprintf(key, sizeof(key), "arg.remove_buff[%lld]", (long long)k);
                        cv_note(key, bv, now);
                    }
                }
            }
        }
    }

    int64_t iu = resolve_index(ar, ard, -1, "use_id");
    if (iu >= 0) {
        int64_t n = 0; uint64_t items = 0;
        if (read_list(getPtr64(value_slot(ard, iu)), n, items) && n >= 1) {
            int64_t v0 = 0;
            if (read_int(getPtr64(items), v0)) {
                g_pp_work.use_id = v0;
                cv_note("arg.use_id[0]", v0, now);
                if (n >= 2) {
                    int64_t v1 = 0;
                    if (read_int(getPtr64(items + 8), v1)) cv_note("arg.use_id[1]", v1, now);
                }
                if (g_probe_last_use != 0 && v0 != g_probe_last_use) {
                    g_pp_work.casts++;
                    //   ★ 发布已经在"采到一次选择"那一刻做了（见 publish_picks）。
                    //     这里**绝不能**再发布一次：提交时第二次预测往往还没落地，
                    //     那样会把刚显示的新内容覆盖成上一次头盔的 —— "慢一个头盔"就是这么来的。
                    //     下面只算个时间差给诊断用。
                    g_pp_work.pair_gap_ms = (g_pp_work.pick_n >= 2)
                                          ? (g_pp_work.pick_ms[1] - g_pp_work.pick_ms[0]) : 0;
                    {
                        char wb[64] = {0};
                        for (int pk = 0; pk < g_pp_work.pick_n; pk++) {
                            char seg[16];
                            snprintf(seg, sizeof(seg), "%s%lld", pk ? "," : "",
                                     (long long)g_pp_work.pick_v[pk]);
                            strncat(wb, seg, sizeof(wb) - strlen(wb) - 1);
                        }
                        probe_log("[pick] 本头盔 %d 次预测: %s  (stage=%lld)",
                                  g_pp_work.pick_n, wb, (long long)g_last_stage);
                    }
                    // 收一条历史：这次的 t + 此刻的技能 id
                    if (g_pp_work.hist_n < 8) {
                        int h = g_pp_work.hist_n++;
                        g_pp_work.hist_t[h]  = g_pp_work.t;
                        g_pp_work.hist_sk[h] = g_probe_last_skill;
                    }
                    probe_log("=== [第 %d 次] use_id %lld -> %lld, t=%lld, skill=%lld, will_cast=%d ===",
                              g_pp_work.casts, (long long)g_probe_last_use, (long long)v0,
                              (long long)g_pp_work.t, (long long)g_probe_last_skill,
                              g_pp_work.casting ? 1 : 0);
                    g_probe_need_dump = true;
                    // 上一次窗口还开着快照 -> 先做差（第二次选择可能就写在窗口里）
                    if (g_snap_n > 0) { diff_skill_dict(g_probe_skill_obj, "next_use"); g_snap_n = 0; }
                }
                g_probe_last_use = v0;
            }
        }
    }

    // 头盔物品字典：字段很少（id/model/socket…），全量挂上，避免漏掉"选项"这种没名字线索的字段
    if (is_obj(g_probe_item_obj)) {
        Dict idk;
        if (read_dict(g_probe_item_obj, idk)) {
            for (int64_t i = 0; i < idk.n_entries && i < 24; i++) {
                char nm[32] = {0};
                if (!read_str(getPtr64(key_slot(idk, i)), nm, sizeof(nm)) || !nm[0]) continue;
                int64_t iv = 0;
                if (read_int(getPtr64(value_slot(idk, i)), iv)) {
                    char key[48];
                    snprintf(key, sizeof(key), "item.%s", nm);
                    cv_note(key, iv, now);
                }
            }
        }
    }

    // ---- skill_mgr.last_cast_skill_id：**选项最可能就是"哪个技能被施放"** ----
    if (is_obj(g_probe_skill_mgr)) {
        Dict smd;
        if (inst_dict_of(g_probe_skill_mgr, smd)) {
            int64_t il = resolve_index(get_inst_dict(g_probe_skill_mgr), smd, -1,
                                       "last_cast_skill_id");
            if (il >= 0) {
                int64_t v = 0;
                if (read_int(getPtr64(value_slot(smd, il)), v)) {
                    // 只记录，不再在这里"读 t"：施放事件那一下 cast_args 还是旧值，
                    // 采信它就等于报上一次头盔（见上面五技能扫描里的说明）
                    g_probe_last_skill = v;
                    cv_note("mgr.last_cast_skill_id", v, now);
                }
            }
            // 五个 1410x 技能的冷却：被选中的那个会进入自己的冷却/状态
            for (int k = 0; k < g_sk_n; k++) {
                Dict d2;
                if (!inst_dict_of(g_sk[k].obj, d2)) continue;
                int64_t ic = resolve_index(get_inst_dict(g_sk[k].obj), d2,
                                           g_sk[k].hint_cd, "_cd_delta");
                g_sk[k].hint_cd = ic;
                if (ic < 0) continue;
                float f = 0.f;
                if (read_seconds(value_slot(d2, ic), f)) {
                    char key[48];
                    snprintf(key, sizeof(key), "sk%lld.cd", (long long)g_sk[k].sid);
                    cv_note(key, (int64_t)(f * 100.0f), now);   // 放大 100 保两位小数
                }
            }
        }
    }

    if (g_probe_need_dump) {
        g_probe_need_dump = false;
        dump_dict_log("cast_args", ca);
        dump_dict_log("predict_arg", pa);
        dump_dict_log("arg", ar);
        // 三个选项也有可能被做成不同技能 id -> 技能表也打一份（地址在 collect 时缓存）
        if (is_obj(g_probe_skill_dict)) dump_skill_dict_log(g_probe_skill_dict);
    }
}
// 挑出"编码"：变化最多的那个候选（同为 0 变化时取名字带 predict 的）
static void pick_code()
{
    int64_t now = now_ms();
    int best_score = -1;
    snprintf(g_pp_work.code_name, sizeof(g_pp_work.code_name), "%s", "");
    g_pp_work.code_ok = false;

    // ① 名字像预测的静态候选
    for (int i = 0; i < g_wf_n; i++) {
        if (g_wf[i].distinct < 2) continue;                 // 至少出现过两种值才算编码
        int score = g_wf[i].distinct * 10 + g_wf[i].changes;
        if (strstr(g_wf[i].name, "predict") || strstr(g_wf[i].name, "type")) score += 5;
        if (now - g_wf[i].change_ms < 5000) score += 3;      // 最近还在变 -> 更可信
        if (score > best_score) {
            best_score = score;
            snprintf(g_pp_work.code_name, sizeof(g_pp_work.code_name), "%s", g_wf[i].name);
            g_pp_work.code_val      = g_wf[i].last_i;
            g_pp_work.code_distinct = g_wf[i].distinct;
            g_pp_work.code_ok = true;
        }
    }
    // ② 链上数值字段（不看名字）
    for (int i = 0; i < g_cv_n; i++) {
        if (g_cv[i].distinct < 2) continue;
        if (strstr(g_cv[i].name, "use_id")) continue;             // 计数器，不是选项
        if (strstr(g_cv[i].name, "last_cast_skill_id")) continue; // 连发序列，不是单选值
        int score = g_cv[i].distinct * 10 + g_cv[i].changes + 2;
        if (now - g_cv[i].change_ms < 5000) score += 4;
        if (score > best_score) {
            best_score = score;
            snprintf(g_pp_work.code_name, sizeof(g_pp_work.code_name), "%s", g_cv[i].name);
            g_pp_work.code_val      = g_cv[i].last;
            g_pp_work.code_distinct = g_cv[i].distinct;
            g_pp_work.code_ok = true;
        }
    }
    // ③ predict_arg.t 是**每个预测单独一个值**（实测 512 / 768），优先用它当"这次选了什么"
    //    它可能不是最终答案（三选项只看到两种取值），但它是目前唯一"每次预测都在变"的量
    for (int i = 0; i < g_cv_n; i++) {
        if (strcmp(g_cv[i].name, "predict_arg.t") != 0) continue;
        if (!g_cv[i].has) break;
        snprintf(g_pp_work.code_name, sizeof(g_pp_work.code_name), "%s", g_cv[i].name);
        g_pp_work.code_val      = g_cv[i].last;
        g_pp_work.code_distinct = g_cv[i].distinct;
        g_pp_work.code_ok = (g_cv[i].distinct >= 2 || g_pp_work.hist_n > 0);
        break;
    }
}

// 20ms 一轮：一次 cast 只活几百毫秒，200ms 那一档根本抓不住
static const int PROBE_INTERVAL_MS = 20;

static void content_probe_thread()
{
    DisguiseThreadName(20);   // 20 = PyKnight 预测采样（19 是 200ms 那一档）
    probe_log("===== 战术预见采样开始 =====");
    while (true) {
        if (g_probe_on) {
            int64_t now = now_ms();
            //   ★ 先补挂技能表里新出现的技能，再扫每个技能自己的 predict_arg.t。
            //     这一步**不依赖 14101 是否存在** —— 开局那几次头盔正是"技能还没创建"的时候。
            ensure_skills(now);
            scan_skill_ticks(now);
            if (is_obj(g_probe_skill_obj)) poll_cast_state(g_probe_skill_obj);
            poll_watch_fields(now);
            pick_code();

            // 绘制侧请求续期（刚经历过渲染空档）-> 把 10 秒窗口挪到现在
            int64_t ext = g_ttl_extend.exchange(0);
            if (ext != 0 && g_pp_work.pick_n > 0) {
                g_pp_work.show_ms = ext;
                probe_log("[ttl] 渲染空档后重新计时（pick_n=%d）", g_pp_work.pick_n);
            }

            // 发布：写非活跃缓冲再翻转
            int w = 1 - (int)g_pp_active;
            g_pp_work.have = true;
            g_pp[w] = g_pp_work;
            int n = 0;
            for (int i = 0; i < g_wf_n && n < MAX_WATCH; i++) {
                snprintf(g_wrow[w][n].name, sizeof(g_wrow[w][n].name), "%s", g_wf[i].name);
                g_wrow[w][n].v        = g_wf[i].last_i;
                g_wrow[w][n].distinct = g_wf[i].distinct;
                g_wrow[w][n].changes  = g_wf[i].changes;
                g_wrow[w][n].fresh    = (now - g_wf[i].change_ms < 3000);
                n++;
            }
            // 链上数值字段（cast_args.* / predict_arg.* / arg.*）排在后面
            for (int i = 0; i < g_cv_n && n < MAX_WATCH; i++) {
                snprintf(g_wrow[w][n].name, sizeof(g_wrow[w][n].name), "%s", g_cv[i].name);
                g_wrow[w][n].v        = g_cv[i].last;
                g_wrow[w][n].distinct = g_cv[i].distinct;
                g_wrow[w][n].changes  = g_cv[i].changes;
                g_wrow[w][n].fresh    = (now - g_cv[i].change_ms < 3000);
                n++;
            }
            g_wrow_n[w] = n;
            g_pp_active = w;
        }
        usleep(PROBE_INTERVAL_MS * 1000);
    }
}

// ----------------------------------------------------------------------------
// 技能链：skill_mgr -> skill_dict[14101] -> cast_args / predict_arg / arg / use_id
// ----------------------------------------------------------------------------
static void read_skill_chain(uint64_t inst, const Dict &dk, KnightState &ks)
{
    int64_t i_sm = resolve_index(get_inst_dict(inst), dk, g_i_skillmgr, "skill_mgr");
    if (i_sm < 0) return;
    g_i_skillmgr = i_sm;
    uint64_t sm = getPtr64(value_slot(dk, i_sm));
    Dict skm;
    if (!inst_dict_of(sm, skm)) return;

    g_i_skilldict = resolve_index(get_inst_dict(sm), skm, g_i_skilldict, "skill_dict");
    if (g_i_skilldict < 0) return;
    uint64_t sd = getPtr64(value_slot(skm, g_i_skilldict));
    if (!is_obj(sd)) return;

    uint64_t sk = dict_get_int_key(sd, SKILL_PREDICT);
    if (!is_obj(sk)) return;                    // 技能对象不在 = 还没拿到头盔（不是"猜了 0 次"）

    Dict skd;
    if (!inst_dict_of(sk, skd)) return;

    // ---- 冷却 ----
    g_i_cd = resolve_index(get_inst_dict(sk), skd, g_i_cd, "_cd_delta");
    if (g_i_cd >= 0) {
        float remain = 0.f;
        if (read_seconds(value_slot(skd, g_i_cd), remain)) {
            ks.has_cd     = true;
            ks.cd_remain  = remain;
            ks.cd_read_ms = now_ms();
            g_i_cdtime = resolve_index(get_inst_dict(sk), skd, g_i_cdtime, "cd_time");
            if (g_i_cdtime >= 0) {
                float total = 0.f;
                if (read_seconds(value_slot(skd, g_i_cdtime), total)) ks.cd_total = total;
            }
        }
    }

    // ---- cast_args（每次使用被覆盖，只留最后一次） ----
    // 这个对象**必须**把字典地址直接交给 read_dict，不能用 getPtr64(x-0x18)（见文件头坑 2）
    g_i_castargs = resolve_index(get_inst_dict(sk), skd, g_i_castargs, "cast_args");
    if (g_i_castargs < 0) return;
    uint64_t ca = getPtr64(value_slot(skd, g_i_castargs));
    if (is_none_obj(ca)) return;          // 还没用过头盔：cast_args 是 None
    Dict cad;
    if (!read_dict(ca, cad)) return;

    g_i_willcast = resolve_index(ca, cad, g_i_willcast, "will_cast");
    if (g_i_willcast >= 0) {
        bool wc = false;
        if (read_bool(value_slot(cad, g_i_willcast), wc)) {
            ks.casting      = wc;
            ks.casting_read = true;
        }
    }

    g_i_predict = resolve_index(ca, cad, g_i_predict, "predict_arg");
    if (g_i_predict < 0) return;
    uint64_t pa = getPtr64(value_slot(cad, g_i_predict));
    if (is_none_obj(pa)) return;
    Dict pad;
    if (!read_dict(pa, pad)) return;

    g_i_t_arg = resolve_index(pa, pad, g_i_t_arg, "arg");
    if (g_i_t_arg < 0) return;
    uint64_t ar = getPtr64(value_slot(pad, g_i_t_arg));
    if (is_none_obj(ar)) return;
    Dict ard;
    if (!read_dict(ar, ard)) return;

    // target_uids：他锁了几个目标（不是"他要对他做什么"，那部分没有）
    g_i_tgt = resolve_index(ar, ard, g_i_tgt, "target_uids");
    if (g_i_tgt >= 0) {
        int64_t n = 0; uint64_t items = 0;
        if (read_list(getPtr64(value_slot(ard, g_i_tgt)), n, items) && n >= 0 && n < 32)
            ks.target_cnt = (int)n;
    }

    // use_id = [use_seq, sub]。use_seq 每次使用 +1，**与选了什么无关**（实测 61/62/63 只是连续递增）
    g_i_use_id = resolve_index(ar, ard, g_i_use_id, "use_id");
    if (g_i_use_id >= 0) {
        int64_t n = 0; uint64_t items = 0;
        if (read_list(getPtr64(value_slot(ard, g_i_use_id)), n, items) && n >= 1) {
            int64_t v0 = 0, v1 = 0;
            if (read_int(getPtr64(items), v0)) ks.cast_seq = v0;
            if (n >= 2 && read_int(getPtr64(items + 8), v1)) ks.cast_seq1 = v1;
        }
    }
}

// ----------------------------------------------------------------------------
// 世界级计数器：g_world_mgr -> cur_world -> knight_predict_use_counter
// ----------------------------------------------------------------------------
// 这是**实测来源**，优先于 use_id 估算。读不到就返回 -1（调用方退回估算），
// 绝不用 0 冒充 —— "读不到"和"猜了 0 次"必须是两种返回值。
static int64_t read_world_counter()
{
    if (!g_module_dict) return -1;
    g_i_wm = find_key(g_module_dict, "g_world_mgr", g_i_wm);
    uint64_t wm = (g_i_wm >= 0) ? get_attr(g_module_dict, g_i_wm) : 0;
    if (!is_obj(wm)) return -1;

    Dict wmd;
    if (!inst_dict_of(wm, wmd)) return -1;

    // ① 正常路径：cur_world 的实例字典
    g_i_cw = resolve_index(get_inst_dict(wm), wmd, g_i_cw, "cur_world");
    if (g_i_cw >= 0) {
        uint64_t cw = getPtr64(value_slot(wmd, g_i_cw));
        Dict cwd;
        if (inst_dict_of(cw, cwd)) {
            g_i_wcount = resolve_index(get_inst_dict(cw), cwd, g_i_wcount,
                                       "knight_predict_use_counter");
            if (g_i_wcount >= 0) {
                int64_t v = 0;
                if (read_int(getPtr64(value_slot(cwd, g_i_wcount)), v) && v >= 0 && v <= 99)
                    return v;
            }
        }
    }
    // ② 兜底：有的版本 World 不是 cur_world 那个实例，直接在 g_world_mgr 的字典上
    g_i_mcount = resolve_index(get_inst_dict(wm), wmd, g_i_mcount, "knight_predict_use_counter");
    if (g_i_mcount >= 0) {
        int64_t v = 0;
        if (read_int(getPtr64(value_slot(wmd, g_i_mcount)), v) && v >= 0 && v <= 99)
            return v;
    }
    return -1;
}

// ----------------------------------------------------------------------------
// 单个求生者单位 -> 骑士条目
// ----------------------------------------------------------------------------
// 返回 true 表示"这个单位是骑士"（不管其余字段读到多少）。
// 判据只用 knight_item_data != None：类名/技能 id 都不可靠（实测别的求生者技能表里也有 14xxx）
static bool read_unit(uint64_t inst, KnightState &ks)
{
    Dict dk;
    if (!inst_dict_of(inst, dk)) return false;

    g_i_kitem = resolve_index(get_inst_dict(inst), dk, g_i_kitem, "knight_item_data");
    if (g_i_kitem < 0) return false;
    uint64_t item = getPtr64(value_slot(dk, g_i_kitem));
    if (is_none_obj(item)) return false;          // ★ 不是骑士（正常求生者全是 None）

    ks.valid    = true;
    ks.unit_obj = inst;

    // ---- 头盔物品 ----
    Dict ik;
    if (read_dict(item, ik)) {
        ks.item_read = true;
        g_i_item_id = resolve_index(item, ik, g_i_item_id, "id");
        if (g_i_item_id >= 0) {
            int64_t id = 0;
            if (read_int(getPtr64(value_slot(ik, g_i_item_id)), id)) ks.item_id = (int)id;
        }
        read_item_extras(item, ks);
    }

    // ---- 场景对象 + 坐标 ----
    g_i_model = resolve_index(get_inst_dict(inst), dk, g_i_model, "model");
    if (g_i_model >= 0) {
        uint64_t mo = getPtr64(value_slot(dk, g_i_model));
        ks.scene_obj = PyCore::get_scene_obj(mo);
    }
    g_i_pos = resolve_index(get_inst_dict(inst), dk, g_i_pos, "position");
    if (g_i_pos >= 0) {
        uint64_t pos = getPtr64(value_slot(dk, g_i_pos));
        float xyz[3] = {0, 0, 0};
        if (is_obj(pos) && vm_readv(pos + 0x10, xyz, 12)) { ks.x = xyz[0]; ks.y = xyz[2]; }
    }

    // ---- 技能链（次数 / 读条 / 冷却） ----
    read_skill_chain(inst, dk, ks);

    // ---- 预测内容探测：把 20ms 那条线挂到这个骑士的技能对象上 ----
    //   ⚠ 只在**第一次**发现骑士时收候选（扫字典很贵），之后靠 20ms 线程比对
    //   ⚠ 候选表按 match 生命周期：换局时随 g_hist 一起清（见 try_refresh）
    {
        uint64_t sk_obj = 0, sd = 0, sm_keep = 0;
        Dict skm;
        if (g_i_skillmgr >= 0) {
            uint64_t sm = getPtr64(value_slot(dk, g_i_skillmgr));
            sm_keep = sm;
            if (inst_dict_of(sm, skm) && g_i_skilldict >= 0) {
                sd = getPtr64(value_slot(skm, g_i_skilldict));
                if (is_obj(sd)) sk_obj = dict_get_int_key(sd, SKILL_PREDICT);
            }
        }
        //   ★★ 2026-10-21 修「新进对局前几次头盔不显示」★★
        //     以前只有 `14101 技能对象存在` 才把采样打开（g_probe_on = true）；
        //     可是开局那几次头盔时 14101/14102 这两个技能对象**还没被创建**
        //     （游戏是第一次用头盔时才 import 出这两个技能），于是整条采样线一直关着，
        //     等对象出现时前几次已经错过了。
        //     现在：只要**技能表(skill_dict)在**就打开采样，缺的技能由 ensure_skills()
        //     每秒补挂一次 —— 技能一出现就能立刻读到它的 cast_args。
        if (is_obj(sk_obj)) {
            //   ★★ 2026-10-21 修「换局后一顶都抓不到」★★
            //     换局时观察表被清空（g_sk_n=0），但新局的技能对象**地址可能被回收复用到同一个值**，
            //     于是 `g_probe_skill_obj != sk_obj` 为假 -> collect 不跑 -> g_sk 永远是空的。
            //     所以判据补两条：观察表空了、或者技能表地址变了，都必须重新收。
            if (g_probe_skill_obj != sk_obj || g_sk_n == 0 || g_probe_skill_dict != sd) {
                g_probe_skill_obj  = sk_obj;
                g_probe_last_use   = 0;
                g_probe_last_will  = false;
                g_pp_work = PredictPub();
                collect_watch_fields(sk_obj, sd, item, inst, sm_keep);
                probe_log("[collect] 重新挂上技能观察表（sk_n=%d）", g_sk_n);
            }
            g_probe_on = true;
        } else if (is_obj(sd)) {
            // 技能对象还没创建，但技能表在 -> 也开着采样，缺的技能由 ensure_skills() 每秒补挂
            if (g_probe_skill_dict != sd) {
                g_probe_skill_dict = sd;
                g_probe_skill_mgr  = sm_keep;
                sk_reset();
                probe_log("[skills] 技能表在但 14101 还没创建，先挂空表等它出现");
            }
            g_probe_on = true;
        } else {
            g_probe_on = false;                     // 技能表都没有（还没拿到头盔）
        }
    }

    // ---- 历史 + 次数判定 ----
    Hist *h = hist_of(inst);
    if (h) hist_note(*h, ks.cast_seq, ks);

    ks.world_counter = g_work_world;
    if (g_work_world >= 0) {
        ks.guesses_raw = (int)g_work_world;
        ks.guess_src   = SRC_WORLD;
    } else if (h && h->seen_n > 0) {
        ks.guesses_raw = h->seen_n - 1;   // 第一次读到的那个序号是"用之前的基线"，不算一次猜测
        if (ks.guesses_raw < 0) ks.guesses_raw = 0;
        ks.guess_src = SRC_USEID;
    } else {
        ks.guesses_raw = 0;
        ks.guess_src   = SRC_NONE;
    }
    ks.guesses = ks.guesses_raw;
    if (ks.guesses > GUESS_MAX) ks.guesses = GUESS_MAX;
    return true;
}

// ----------------------------------------------------------------------------
// 一轮刷新
// ----------------------------------------------------------------------------
static bool try_refresh()
{
    const char *why = "";
    if (!g_module_dict) {
        g_module_dict = PyCore::game_kernel_dict(why);
        if (!g_module_dict) { snprintf(g_status, sizeof(g_status), "%s", why); return false; }
    }

    g_i_unit_mgr = find_key(g_module_dict, "unit_mgr", g_i_unit_mgr);
    uint64_t um = (g_i_unit_mgr >= 0) ? get_attr(g_module_dict, g_i_unit_mgr) : 0;
    if (!is_obj(um)) {
        //   ★ 开局/换局瞬间 game_kernel 字典可能是旧的：**立刻作废**，下一轮（100ms 后）重新找。
        //     原来要等连续失败 15 轮（200ms × 15 = 3 秒）才重解析 —— 那 3 秒正是漏掉前几顶头盔的时间。
        g_module_dict = 0;
        snprintf(g_status, sizeof(g_status), "unit_mgr 无效(未在对局中?)");
        return false;
    }

    // 换局：use_id 序号和"猜了几次"都是本局的东西，上一局的必须作废
    //   ★★ 2026-10-21 修「HUD 永远 pick_n=0」★★
    //     实测日志里 `[scan] 骑士 0 个 → 1 个 → 0 个` 在抖：读取偶尔读不到玩家列表，
    //     unit_mgr 地址也跟着在两个值之间跳。原来一看到地址变就整块重置，
    //     **刚采到的预测立刻被 g_pp_work = PredictPub() 抹掉**，所以面板永远没数据。
    //     现在：地址变化要**稳定持续 1 秒**才认定换局；而且重置**不清已发布的显示**
    //     （g_pp 保留，屏幕上那句话不会因为一次抖动就消失）。
    {
        static uint64_t   cand_um = 0;
        static int64_t    cand_since = 0;
        if (g_last_unit_mgr != 0 && um != g_last_unit_mgr) {
            const int64_t tnow = now_ms();
            if (cand_um != um) { cand_um = um; cand_since = tnow; }
            else if (tnow - cand_since >= 1000) {
                g_hist_n = 0;
                for (int i = 0; i < MAX_KNIGHTS; i++) g_hist[i] = Hist();
                g_i_wcount = g_i_mcount = g_i_cw = g_i_wm = -1;
                g_i_kitem = g_i_castargs = g_i_predict = g_i_t_arg = g_i_use_id = -1;
                g_wf_n = 0;
                cv_reset();
                g_probe_need_dump = false;
                g_probe_skill_dict = 0;
                g_probe_item_obj   = 0;
                g_probe_skill_mgr  = 0;
                sk_reset();
                g_wrow_n[0] = g_wrow_n[1] = 0;
                g_pp_work = PredictPub();          // 只清工作副本，不清已发布的那份
                g_probe_skill_obj = 0;
                g_probe_last_use  = 0;
                g_probe_last_will = false;
                g_probe_on        = false;
                g_first_logged = false;
                g_match_start_ms = now_ms();
                probe_log("[match] 换局（unit_mgr 稳定变化 1s）→ 状态已重置");
                cand_um = 0;
                g_last_unit_mgr = um;
            }
        } else {
            cand_um = 0;
            //   ★★ 2026-10-21 补：**程序刚启动后的第一局**根本不会走上面的换局分支
            //     （g_last_unit_mgr 还是 0），于是 g_match_start_ms 一直是 0 ->
            //     "开局 30 秒"的宽限从不生效 -> 第一顶头盔还是被加载画面吃掉。
            //     这里在第一次看到对局时把开局时刻补上。
            if (g_last_unit_mgr == 0 || g_match_start_ms == 0) {
                g_match_start_ms = now_ms();
                probe_log("[match] 本局开始计时（首次见到 unit_mgr）");
            }
            g_last_unit_mgr = um;
        }
    }

    Dict umd;
    if (!inst_dict_of(um, umd)) { snprintf(g_status, sizeof(g_status), "unit_mgr 字典异常"); return false; }
    g_i_ubt = resolve_index(get_inst_dict(um), umd, g_i_ubt, "units_by_type");
    if (g_i_ubt < 0) { snprintf(g_status, sizeof(g_status), "找不到 units_by_type"); return false; }
    uint64_t ubt = getPtr64(value_slot(umd, g_i_ubt));
    if (!is_obj(ubt)) { snprintf(g_status, sizeof(g_status), "units_by_type 无效"); return false; }

    g_work_n = 0;
    g_work_world = read_world_counter();

    // 求生者列表。**扫描成功与"没有骑士"必须分开**：列表读不到时不能说"本局没有骑士"
    uint64_t lst = get_type_list(ubt, CIVILIAN_UNIT_TYPE);
    int64_t n = 0; uint64_t items = 0;
    g_work_scan_ok = (is_obj(lst) && read_list(lst, n, items)) ? 1 : 0;

    if (g_work_scan_ok) {
        for (int64_t i = 0; i < n && g_work_n < MAX_KNIGHTS; i++) {
            uint64_t inst = getPtr64(items + i * 8);
            if (!is_obj(inst)) continue;
            KnightState ks;
            if (!read_unit(inst, ks)) continue;
            g_work[g_work_n++] = ks;
        }
    }

    // 发布（双缓冲）
    int w = 1 - g_active;
    for (int i = 0; i < g_work_n; i++) g_buf[w][i] = g_work[i];
    g_cnt[w] = g_work_n;
    g_world_counter[w] = g_work_world;
    g_scan_ok[w] = g_work_scan_ok;

    // ★ 骑士数量变化时记一条：排查"开局抓不到"时，先确认这一局到底有没有扫到骑士
    {
        static int last_knight_n = -1;
        static int last_scan_ok  = -1;
        if (g_work_n != last_knight_n || g_work_scan_ok != last_scan_ok) {
            last_knight_n = g_work_n;
            last_scan_ok  = g_work_scan_ok;
            probe_log("[scan] 骑士 %d 个 scan_ok=%d（技能表=%s 14101=%s）",
                      g_work_n, g_work_scan_ok,
                      is_obj(g_probe_skill_dict) ? "有" : "无",
                      is_obj(g_probe_skill_obj) ? "有" : "无");
        }
    }
    g_active = w;

    if (!g_work_scan_ok)      snprintf(g_status, sizeof(g_status), "求生者列表读不到");
    else if (g_work_n == 0)   snprintf(g_status, sizeof(g_status), "本局没有骑士");
    else                      snprintf(g_status, sizeof(g_status), "骑士 1 人 计数=%lld",
                                       (long long)g_work_world);
    //   ★ 这里**故意返回 true**（哪怕列表没读到）：
    //     unit_mgr / units_by_type 都已经拿到了 = 确实在对局里。
    //     若返回 false，绘制侧只能显示"读取未就绪"，就分不出
    //     "本局没有骑士"和"列表读不到"这两种完全不同的情况 ✗
    //     （项目规矩：读不到和没有必须是两种返回值）
    return true;
}

// 200ms 一轮：一轮约几十次驱动读（微秒级），开销可以忽略
static const int REFRESH_INTERVAL_MS = 100;

static void refresh_once()
{
    if (g_libbase == 0) return;
    if (!PyRoot::ensure()) {
        g_available = false;
        snprintf(g_status, sizeof(g_status), "%s", PyRoot::status_text());
        return;
    }
    if (try_refresh()) { g_available = true; g_fail_streak = 0; return; }
    g_available = false;
    if (++g_fail_streak >= 15) {          // 连续失败：序号缓存全部作废，下一轮按名字重找
        g_module_dict = 0;
        g_i_unit_mgr = g_i_ubt = -1;
        g_i_kitem = g_i_model = g_i_pos = g_i_item_id = -1;
        g_i_skillmgr = g_i_skilldict = -1;
        g_i_castargs = g_i_predict = g_i_t_arg = g_i_use_id = g_i_willcast = g_i_tgt = -1;
        g_i_cd = g_i_cdtime = -1;
        g_i_wm = g_i_cw = g_i_wcount = g_i_mcount = -1;
        g_fail_streak = 0;
        // 注意：这里**不清观察历史**，只在换局时清（对局中途链路短暂中断很常见）
    }
}

static void thread_main()
{
    DisguiseThreadName(19);   // 19 = PyKnight（11 PySelf / 12 PyAttack / 13 AutoAim / 15 PyMiniMap
                              //      16 PyGenius / 17 AutoPallet / 18 另一个世界）
    while (true) {
        refresh_once();
        usleep(REFRESH_INTERVAL_MS * 1000);
    }
}

static void start(uintptr_t libbase)
{
    if (g_libbase != 0) return;
    g_libbase = libbase;
    snprintf(g_status, sizeof(g_status), "启动中");
    std::thread(thread_main).detach();
    std::thread(content_probe_thread).detach();   // 战术预见：20ms 采样
    printf("[骑士] 读取器已启动 libbase=0x%lx\n", (unsigned long)libbase);
    fflush(stdout);
}

// ----------------------------------------------------------------------------
// 给绘制侧用的只读接口
// ----------------------------------------------------------------------------
static inline bool available()        { return g_available; }
static inline const char *status_text() { return g_status; }
static inline int  count()            { return g_cnt[g_active]; }
static inline bool scan_ok()          { return g_scan_ok[g_active] != 0; }
static inline int64_t world_counter() { return g_world_counter[g_active]; }
static inline bool has_knight()       { return g_available && g_cnt[g_active] > 0; }

static bool get(int i, KnightState &out)
{
    int b = g_active;
    if (i < 0 || i >= g_cnt[b]) return false;
    out = g_buf[b][i];
    return true;
}

// 把场景对象配到骑士。首选 model+0x20 的指针身份（跟 PyProgress/PyGenius 同一套判据）；
// model 暂时为空时才退回坐标近邻，阈值 1.0m —— 人是在动的，宁可配不上也不要配错。
static bool lookup(uintptr_t scene_obj, KnightState &out)
{
    if (!g_available) return false;
    int b = g_active, n = g_cnt[b];
    if (!scene_obj) return false;
    for (int i = 0; i < n; i++) {
        if (g_buf[b][i].scene_obj == (uint64_t)scene_obj) { out = g_buf[b][i]; return true; }
    }
    return false;
}

static bool lookup_near(float x, float y, KnightState &out)
{
    if (!g_available) return false;
    int b = g_active, n = g_cnt[b];
    int best = -1; float best_d = 1.0f;
    for (int i = 0; i < n; i++) {
        if (g_buf[b][i].scene_obj != 0) continue;                 // 有身份的上面已经比过了
        if (g_buf[b][i].x == 0.f && g_buf[b][i].y == 0.f) continue;
        float dd = fabsf(g_buf[b][i].x - x) + fabsf(g_buf[b][i].y - y);
        if (dd < best_d) { best_d = dd; best = i; }
    }
    if (best < 0) return false;
    out = g_buf[b][best];
    return true;
}

// 冷却推算：读到的值按经过时间往下走（跟 PyGenius 的 cd_remain_now 同一套）
static inline float cd_remain_now(const KnightState &k)
{
    if (!k.has_cd || k.cd_remain <= 0.05f) return 0.f;
    float r = k.cd_remain - (float)(now_ms() - k.cd_read_ms) / 1000.f;
    return r > 0.f ? r : 0.f;
}

// ---------------- 战术预见：内容侧只读接口 ----------------

static inline const PredictPub &predict_pub() { return g_pp[g_pp_active]; }
// 诊断用：当前发布下标（确认采样线程到底有没有在翻转缓冲）
static inline int active_index() { return (int)g_pp_active; }

// 显示有效期：出现一次新预测就开始算 10 秒，期间框上/面板上才显示"预见:xxx"
static const int64_t PREDICT_SHOW_MS = 10000;

// 请求把显示窗口续到 now（绘制侧在"渲染空档"之后调用）
static inline void extend_display(int64_t now) { g_ttl_extend.store(now); }
static inline bool predict_fresh()
{
    const PredictPub &p = predict_pub();
    if (p.pick_n <= 0 || p.show_ms == 0) return false;
    const int64_t n = now_ms();
    //   ★ 开局 30 秒内放大到 30 秒：那段时间画面常被加载/过场挡住（Draw_Main 的 read_state 闸），
    //     10 秒窗口会在"玩家还没看到任何东西"的时候就走完。过了开局再回到 10 秒。
    const bool early = (g_match_start_ms != 0) && (n - g_match_start_ms < 30000);
    const int64_t ttl = early ? 30000 : PREDICT_SHOW_MS;
    return (n - p.show_ms) < ttl;
}
static inline int watch_size() { return g_wrow_n[g_pp_active]; }

static inline bool watch_row(int i, WatchRow &out)
{
    int b = g_pp_active;
    if (i < 0 || i >= g_wrow_n[b]) return false;
    out = g_wrow[b][i];
    return true;
}

// 编码值 -> 中文标签。-1 = 没标注过
//   0 出刀 / 1 技能 / 2 什么都不做
static inline int label_of_code(int64_t code)
{
    if (code < 0) return -1;
    for (int i = 0; i < 3; i++)
        if (g_bind_code[i] == code) return i;
    return -1;
}

// 面板上"标注"按钮调它：把当前编码值绑定到某个标签，并落盘（存盘由 draw_Gui 的 SaveConfig 做）
static inline void bind_code(int label, int64_t code)
{
    if (label < 0 || label > 2) return;
    g_bind_code[label] = code;
    probe_log("[bind] %s = %lld", label == 0 ? "出刀" : (label == 1 ? "技能" : "不做"),
              (long long)code);
}

static const char *label_name(int label)
{
    if (label == 0) return "出刀";
    if (label == 1) return "技能";
    if (label == 2) return "不做";
    return "?";
}

// 框上/面板上那句"战术预见：xxx"
//   "出刀"       —— 编码已标注（用户点过一次）
//   "值2?"       —— 编码读到了但还没标注（**不猜**，把原始值给出来）
//   "未捕获"     —— 没读到（也可能他还没用头盔）
static void predict_short(char *buf, size_t cap)
{
    if (!buf || cap == 0) return;
    const PredictPub &p = predict_pub();
    // 有"选择序列"就按序列显示：一个头盔猜两次就显示 出刀→技能
    //   ⚠ 第二次如果没写进 predict_arg.t，这里只能显示一条 —— 那种情况要靠日志里的
    //     [diff:close] 找出第二次落在哪个字段（见 diff_skill_dict）
    if (p.pick_n > 0) {
        // ⚠ 两个缓冲都要先填满：上一版 pick_n==1 时 b 是未初始化的栈内存，
        //   面板上就打出 "Ptr2" 这种乱码（真机上踩过一次）
        char a[24] = {0}, b[24] = {0};
        int lab0 = label_of_code(p.pick_v[0]);
        if (lab0 >= 0) snprintf(a, sizeof(a), "%s", label_name(lab0));
        else           snprintf(a, sizeof(a), "值%lld", (long long)p.pick_v[0]);

        if (p.pick_n >= 2) {
            int lab1 = label_of_code(p.pick_v[1]);
            if (lab1 >= 0) snprintf(b, sizeof(b), "%s", label_name(lab1));
            else           snprintf(b, sizeof(b), "值%lld", (long long)p.pick_v[1]);
        }
        // 只有**同一个窗口内采到两次**才连起来；否则显示这一次
        if (p.pair_ok && p.pick_n >= 2) snprintf(buf, cap, "%s->%s", a, b);   // 箭头用 ASCII：面板字体没有 →
        else                            snprintf(buf, cap, "%s", a);
        return;
    }
    if (!p.have || !p.code_ok) { snprintf(buf, cap, "未捕获"); return; }
    int lab = label_of_code(p.code_val);
    if (lab >= 0) snprintf(buf, cap, "%s", label_name(lab));
    else          snprintf(buf, cap, "值%lld?", (long long)p.code_val);
}

// 框上那行的文本。**估算值带 ~**，未知带 ?，绝不给"看着像实测"的假数
//   "头盔 1/2"       实测
//   "头盔 ~1/2"      估算（世界计数器读不到，按 use_id 变化推的）
//   "头盔 ?/2"       次数读不到
//   "头盔 2/2 满了"  已经猜满 2 次 —— 马上要结算，这时候出刀最危险
//   ⚠ 2026-10-21 实测修正：`use_id` / `World.knight_predict_use_counter` 都是**全局使用计数**
//     （实测一局里同一骑士已经用到 36），**不是"只能用 2 次"** —— 旧文档那条结论作废。
//     所以这里只报"已用 N 次"，不再出现"满了/马上结算"这种会误导人的字眼。
static void guess_text(const KnightState &k, char *buf, size_t cap)
{
    if (!buf || cap == 0) return;
    if (k.guess_src == SRC_NONE) { snprintf(buf, cap, "已用?次"); return; }
    if (k.guess_src == SRC_USEID) { snprintf(buf, cap, "已用~%d次", k.guesses_raw); return; }
    snprintf(buf, cap, "已用%d次", k.guesses_raw);
}

// 悬浮窗用的一行摘要（不含换行，调用方自己排版）
static void detail_line(const KnightState &k, char *buf, size_t cap)
{
    if (!buf || cap == 0) return;
    char guess[32];
    guess_text(k, guess, sizeof(guess));
    char src[16];
    if (k.guess_src == SRC_WORLD)      snprintf(src, sizeof(src), "计数");
    else if (k.guess_src == SRC_USEID) snprintf(src, sizeof(src), "估算");
    else                               snprintf(src, sizeof(src), "未知");

    char cds[24];
    if (!k.has_cd)                 snprintf(cds, sizeof(cds), "CD -");
    else if (cd_remain_now(k) <= 0.05f) snprintf(cds, sizeof(cds), "CD 就绪");
    else                           snprintf(cds, sizeof(cds), "CD %.1fs", cd_remain_now(k));

    //   以"预见"打头，但只在 10 秒有效期内显示（过期就只留次数/CD，用户要求）
    if (predict_fresh()) {
        char pv[32];
        predict_short(pv, sizeof(pv));
        snprintf(buf, cap, "预见:%s · 已用%d次 · %s%s",
                 pv,
                 k.guesses_raw > 0 ? k.guesses_raw : 0,
                 cds,
                 k.casting_read ? (k.casting ? " 读条中" : "") : "");
    } else {
        snprintf(buf, cap, "已用%d次 · %s%s",
                 k.guesses_raw > 0 ? k.guesses_raw : 0,
                 cds,
                 k.casting_read ? (k.casting ? " 读条中" : "") : "");
    }
}

} // namespace PyKnight

#endif // IDV_PY_KNIGHT_H
