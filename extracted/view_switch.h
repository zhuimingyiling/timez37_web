#pragma once
// ============================================================================
// view_switch.h —— 「改视角」功能（2026-10-13）
//
// 用户需求（原话）：
//   「每局扫描并获取人物坐标地址，将识别到的角色名称显示在改视角窗口中。
//     点击对应角色名称，即可将视角切换到该角色。结合求生者或监管者的技能、
//     辅助特质，以及地图中可交互的中立建筑，可以实现快速位移或瞬间传送效果。」
//
// ── 本文件负责「扫描 + 列表」这一半（纯读取，零风险）────────────────────
//   已验证可靠的读取链路（见 SESSION_HANDOFF）：
//     · 单位列表      PyAttack::get_all_units_of_type(type, ...)   type 2=求生 1=监管
//     · 角色名        unit.__dict__["player_name"]  (PyCore::read_str)
//     · 实时坐标      unit.model -> +0x20 = scene -> +0x28 = coor(带 tag，要掩码)
//                     coor + 0xa0 / +0xa4 / +0xa8 = x / y / z
//                     ⚠ 不要用 __dict__["position"] —— 那是**陈旧副本**，不更新 ✗
//     · 坐标指针掩码  值形如 0xB4000073DBE44950，真地址 = 低 48 位 ✓
//
// ── 「切换到该角色」这一半还没接（原因见下）──────────────────────────
//   FollowMode 里有官方机制：lock_target_unit / lock_pos / lock_pos_vec /
//   lock_unit / lock_turn_speed —— 看着就是「把视角锁到某单位/某点」✓
//   但**改 Python 对象属性需要往它的 __dict__ 值槽里写指针**，
//   而本工程的 dict 布局与标准 CPython 不同（实测 keys 数组偏移对不上）✗
//   在没精确证明值槽地址之前**不写内存** ✗
//   ⚠ 教训：曾经靠猜偏移写 memwrite，把用户的对局写崩了 ☠
//
//   所以本文件的 ApplyView() **只记录、不注入** ✓
//   等值槽地址被证明、或找到游戏自己的 setter 接口，再接上 ✓
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>

#include "PyCore.h"
#include "PyAttack.h"
#include "PySelf.h"      // ★ 模仿者身份簿（序号 + 身份）
#include "Name.h"        // ★ getplayer() / getboss() —— 模型路径 -> 角色名

namespace ViewSwitch {

// ---- 参数 ---------------------------------------------------------------
//   ★ 默认打开：扫描是**纯读取**，没有副作用，开着才能立刻看到列表 ✓
//     （用户要的是"每局扫描并显示角色名"，默认开更符合预期）
inline bool g_on       = true;
// （2026-10-13 用户要求：删掉「按距离排序」开关 ✓
//   模仿者局按号码排；普通局也按距离排（固定行为，不再给开关）✓）
inline int  g_filter   = 0;        // 0=全部 1=求生 2=监管

// ---- 目标 ---------------------------------------------------------------
struct Target {
    uint64_t unit    = 0;   // 单位对象
    uint64_t coor    = 0;   // 场景坐标块（已掩码）
    uint64_t scene   = 0;
    unsigned uid     = 0;
    int      camp    = 0;   // 1=监管 2=求生
    float    x = 0, y = 0, z = 0;
    float    dist_m  = -1;  // 到我的距离（米）
    int      seat    = 0;   // 模仿者局的号码（1~12）；0 = 不是模仿者局/读不到
    bool     dead    = false;
    char     name[48] = {0};
    bool     moved   = false;   // 与上次扫描相比有没有动
};

inline const int MAXT = 32;
inline Target g_t[MAXT];
inline int    g_n = 0;

// ---- 自身坐标（由绘制循环喂进来 —— 那边才有可信的自己人坐标）----------
inline float g_self_x = 0, g_self_y = 0, g_self_z = 0;
inline bool  g_have_self = false;

// ---- 选中 / 状态 --------------------------------------------------------
inline int    g_sel      = -1;
inline unsigned g_sel_uid = 0;      // 认 uid 不认下标（排序会变）✓
inline int    g_scan_n   = 0;
inline int    g_apply_n  = 0;
inline char   g_status[192] = "待机";
inline uint64_t g_last_scan_ms = 0;
// 模仿者局但身份簿是空的 -> 提示用户去开「模仿者模式」开关（见 read_one 的说明）
inline bool   g_need_copycat_mode = false;

// 上次坐标，用来判断"动没动"
inline float g_px[MAXT] = {0}, g_py[MAXT] = {0}, g_pz[MAXT] = {0};
inline unsigned g_puid[MAXT] = {0};

inline float meters(float ax, float ay, float az, float bx, float by, float bz)
{
    const float dx = ax - bx, dy = ay - by, dz = az - bz;
    return sqrtf(dx * dx + dy * dy + dz * dz) / 11.886f;   // 与 ESP 同一换算 ✓
}

// coor 指针带高位 tag（0xB40000xxxxxxxxxx）-> 取低 48 位才是真地址 ✓
inline uint64_t untag(uint64_t v)
{
    return (v >> 48) == 0xB400 ? (v & 0x0000FFFFFFFFFFFFULL) : v;
}

// ---- 读一个单位（全部用已验证的读取方式）-------------------------------
inline bool read_one(uint64_t unit, int camp, Target &t)
{
    memset(&t, 0, sizeof(t));
    t.unit = unit;
    t.camp = camp;

    const uint64_t d = PyCore::get_inst_dict(unit);
    if (!d) return false;
    PyCore::Dict dk;
    if (!PyCore::read_dict(d, dk)) return false;

    // uid
    {
        const int64_t i = PyCore::resolve_index(d, dk, -1, "uid");
        if (i >= 0) {
            int64_t uv = 0;
            if (PyCore::read_int(getPtr64(PyCore::value_slot(dk, i)), uv)) t.uid = (unsigned)uv;
        }
    }
    if (!t.uid) return false;

    // ★★ 角色名（2026-10-13）★★
    //   用户要求：「显示角色名字就行，例如 空军，模仿者则显示序号和身份」
    //
    //   信息来源（都不是我猜的，都是工程里现成的）：
    //     · 普通局：单位属性 `model_path`（如 "chr/player/dm65_survivor_w/..."）
    //                -> Name.h 的 getplayer() / getboss() 查表得到 "[空军]"
    //     · 模仿者局：PySelf 的身份簿 -> seat（序号）+ identity（身份）
    //                -> 组合成 "3号 神偷"
    //
    //   ⚠ 不要用 `_player_name`：那是**账号昵称**，实测这局它的数据指针=0（空的）✗
    // ★ 实时坐标：必须走 model->scene->coor 链，不能用 __dict__["position"] ✗
    {
        const int64_t i = PyCore::resolve_index(d, dk, -1, "model");
        if (i < 0) return false;
        const uint64_t model = getPtr64(PyCore::value_slot(dk, i));
        if (!PyCore::is_obj(model)) return false;
        const uint64_t scene = untag(getPtr64(model + 0x20));
        if (!PyCore::is_obj(scene)) return false;
        const uint64_t coor = untag(getPtr64(scene + 0x28));
        if (!PyCore::is_obj(coor)) return false;
        float p[3] = {0, 0, 0};
        if (!vm_readv(coor + 0xa0, p, sizeof(p))) return false;
        t.scene = scene;
        t.coor  = coor;
        t.x = p[0]; t.y = p[1]; t.z = p[2];
        if (t.x == 0.0f && t.z == 0.0f) return false;   // 空槽
        if (t.y < -300.0f) return false;                // 地下的不要
    }

    {
        char model[192] = {0};
        const int64_t im = PyCore::resolve_index(d, dk, -1, "model_path");
        if (im >= 0) {
            const uint64_t mp = getPtr64(PyCore::value_slot(dk, im));
            if (PyCore::is_obj(mp)) PyCore::read_str_any(mp, model, sizeof(model));
        }

        // 模仿者局：序号 + 身份
        //   ⚠ 用**局部** buffer，不要用 g_apply（那是别处的共用缓冲）✗
        const bool is_copycat_unit = (camp == PySelf::COPYCAT_PLAYER_TYPE ||
                                      camp == PySelf::COPYCAT_GHOST_TYPE);
        bool named = false;
        if (is_copycat_unit || PySelf::copycat_active()) {
            int ident = 0, cseat = 0;
            // ★★ 必须传**场景对象** t.scene，不能传单位对象 unit ✗✗
            //   理由（PySelf.h）：
            //     `CopycatEntry::body = get_scene_obj(unit.model)` —— 身份簿里
            //     存的是**场景对象**，而 copycat_identity() 拿它做指针相等比较 ✓
            //   我一开始传的是单位对象（get_all_units_of_type 返回的那个），
            //   两者永远不相等 -> 身份永远读不出来 -> 名字只能是 "?" ✗
            //   （第一次修的时候就是这么错的，用户实测"一个目标都没有/全是?"）
            //
            //   ⚠ 也**不能**改用坐标认领（copycat_seat_near）：
            //     开会阶段 12 个人挤在一张桌子旁（实测坐标全在 770~830 之间），
            //     按距离认领会认错人 ✗ 只有对象指针才靠得住 ✓
            if (PySelf::copycat_identity(t.scene, ident, cseat)) {
                const char *nm = PySelf::copycat_identity_name(ident);
                t.seat = cseat;                 // ★ 记下号码，列表要按它排 ✓
                if (cseat > 0)
                    snprintf(t.name, sizeof(t.name), "%d号 %s", cseat,
                             nm ? nm : "?");
                else
                    snprintf(t.name, sizeof(t.name), "%s", nm ? nm : "?");
                named = true;
            }
        }
        // 普通局：模型路径查角色名
        //   模仿者局里模型路径长得像求生者，**别用它**（会显示成角色名而不是身份）✗
        if (!named && !is_copycat_unit && model[0]) {
            const char *disp = (camp == 1) ? getboss(model) : getplayer(model);
            if (disp && disp[0]) snprintf(t.name, sizeof(t.name), "%s", disp);
        }
        // 兜底：座位号（用 unique_id）
        if (!t.name[0]) {
            const int64_t i2 = PyCore::resolve_index(d, dk, -1, "unique_id");
            if (i2 >= 0) {
                int64_t seat = 0;
                if (PyCore::read_int(getPtr64(PyCore::value_slot(dk, i2)), seat) && seat > 0)
                    snprintf(t.name, sizeof(t.name), "%lld号", (long long)(seat % 10000));
            }
        }
        // 模仿者局但身份簿是空的 -> 明确提示要开「模仿者模式」✓
        //   原因：身份簿只在**绘制侧那个开关**打开时才记录（PySelf::book_merge）：
        //     Draw_Main() 里 `PySelf::set_copycat_book(copycat_mode);`
        //   copycat_mode 关着 -> book_merge 直接 return -> 簿子永远空
        //   -> 身份读不出来，只能显示 "?" ✗
        //   ⚠ 我不去覆盖那个开关（那是绘制侧的控制），只**把原因告诉用户** ✓
        if (is_copycat_unit && !named) {
            // ★ 区分两种情况（之前混了，已死的 4 人被误报成"需开模仿者模式"）✗
            //   ① 簿子里**有**这个 body -> 这人只是**死了/离场**
            //      （copycat_identity 会**主动跳过死人**，所以查不到 ≠ 没数据）
            //      ⚠ 号码和身份一律从**簿子**取 —— 查死人那条路不会填 cseat，
            //        所以不能依赖上面的 cseat，否则死者 seat=0、排序会被甩到最后 ✗
            //   ② 簿子里**一个都没有** -> 才是真没记录 -> 提示开开关 ✓
            int seat_no = 0, idn = 0;
            const PySelf::CopycatBook bk = PySelf::copycat_book();
            bool any_known = false;
            for (int k = 0; k < PySelf::MAX_COPYCAT; k++) {
                if (!bk.seat[k].known) continue;
                any_known = true;
                if (bk.seat[k].body == t.scene) {
                    seat_no = k + 1;
                    idn = bk.seat[k].identity;
                    t.dead = bk.seat[k].dead;
                    break;
                }
            }
            if (any_known && seat_no > 0) {
                const char *nm = PySelf::copycat_identity_name(idn);
                t.seat = seat_no;                       // ★ 死者也有号码 -> 排序正常 ✓
                if (nm) snprintf(t.name, sizeof(t.name), "%d号 %s%s",
                                 seat_no, nm, t.dead ? " 已死" : "");
                else    snprintf(t.name, sizeof(t.name), "%d号%s",
                                 seat_no, t.dead ? " 已死" : "");
            } else if (any_known) {
                snprintf(t.name, sizeof(t.name), "%s", "已死/离场");
            } else {
                snprintf(t.name, sizeof(t.name), "%s", "记录中…");
                g_need_copycat_mode = true;
            }
        }
        if (!t.name[0]) snprintf(t.name, sizeof(t.name), "?");
    }

    t.dist_m = g_have_self ? meters(t.x, t.y, t.z, g_self_x, g_self_y, g_self_z) : -1.0f;
    return true;
}

// ---- 扫描（挂绘制线程，外部按节流调用）--------------------------------
//   ★★ 2026-10-13 修：模仿者局的玩家**不在 type 1/2 里** ✗✗
//     实测（PySelf.h 也写了）：模仿者局的玩家是
//       units_by_type[1088] 的 CopycatPlayerUnit（12 人）✓
//     死亡后原单位留在 1088，另生成 [1089] GhostCopycatPlayerUnit ✓
//     所以只扫 1/2 会在模仿者局里**一个目标都扫不到** ✗（用户实测反馈）
//     → 四类全扫：2=求生 1=监管 1088=模仿者本体 1089=模仿者灵魂 ✓
//
//   ★★ 2026-10-13 再修：**自动识别模仿者局**（用户要求去掉那个开关）★★
//     判据：`units_by_type[1088]` 里**有人** = 模仿者局 ✓
//     这个判据是可靠的：普通局根本不会出现 1088 这个类型 ✓
//     识别到之后自动打开身份簿（见 draw_Gui 的 Draw_Main），
//     用户就不用再去「绘制」页手动开「模仿者模式」了 ✓
inline bool  g_is_copycat = false;    // 本局是不是模仿者局（自动识别）
// ★★ 2026-10-14：识别的"未命中"计数（去抖用，见 scan() 末尾）★★
//   必须是 inline 全局而不是 scan() 里的 static —— view_switch.h 被多个 TU 包含，
//   函数内 static 会变成每个 TU 一份，几处各自计数就失去意义了 ✗
inline int   g_copycat_miss = 0;
inline int   g_skip_reason[8] = {0};  // 诊断：各类跳过计数
inline bool  g_verbose_skip = false;

// ============================================================================
// ★★ 2026-10-14：改视角所需的全局状态 + 跨对局检测 ★★
//   ⚠ 这一整段**必须放在 scan() 之前** ✗✗
//     两个原因（都是编译期 ODR 硬要求）：
//       ① scan() 里要调 check_new_match()，函数必须先声明/定义
//       ② check_new_match() 里要清 g_cam_dict_addr / g_cam_why，
//          而这两个变量原来的定义位置比它还靠后 ✗
//
//   原来那个 bug（第一局能用、第二局失效）：
//     g_cam_slot / g_my_unit 是第一局解析出来的地址，
//     换局后 game_kernel 重建 -> 值槽地址全变，
//     而 resolve_cam_slot() 命中永久缓存直接 return 旧地址 ✗
//     -> 写内存落到无效页 -> "没效果"，扫描却照常（另一条读取链）
//
//   判据：game_kernel_dict 的地址在换局时必然改变 ✓
// ============================================================================

// ---- 视角注入状态（原来在"视角注入"段落里的那 5 个）----
inline uint64_t g_cam_slot   = 0;    // g_cam_ctrl.unit 的值槽地址
inline uint64_t g_my_unit    = 0;    // 我自己的单位对象（要还原成它）
inline uint64_t g_lock_unit  = 0;    // 当前锁定的目标（0 = 没锁）
inline int      g_lock_uid   = 0;
inline int      g_inject_n   = 0;    // 注入次数（诊断）

// ---- 诊断信息（原来在"解析 g_cam_ctrl"段落里的那 2 个）----
inline const char *g_cam_why    = "";  // 解析失败时的原因（诊断用）
inline uint64_t g_cam_dict_addr = 0;   // 诊断：g_cam_ctrl 的 dict 地址

// ---- 跨对局指纹 ----
inline uint64_t g_kernel_fp = 0;       // game_kernel 地址指纹（换局必变）✓

// 返回值：true = 本轮刚检测到新对局（已清空所有跨局会失效的缓存）
inline bool check_new_match()
{
    const char *why = nullptr;
    const uint64_t md = PyCore::game_kernel_dict(why);
    if (!md) return false;                 // 读失败 -> 什么都不做（别误清）✗
    if (g_kernel_fp == 0) { g_kernel_fp = md; return false; }  // 首次
    if (md == g_kernel_fp) return false;   // 同一局

    // ★ 新对局：清空所有"跨局会失效"的缓存
    g_kernel_fp     = md;
    g_cam_slot      = 0;      // 值槽地址（罪魁祸首）
    g_cam_dict_addr = 0;
    g_my_unit       = 0;      // 我自己的单位对象
    g_lock_unit     = 0;      // 锁定目标
    g_lock_uid      = 0;
    g_inject_n      = 0;
    g_cam_why       = "";
    return true;
}

inline void scan()
{
    g_scan_n++;
    check_new_match();          // ★ 跨对局检测：换局后自动清缓存 ✓
    g_need_copycat_mode = false;
    int n = 0;
    // ★ 三态（配合 get_all_units_of_type 的新返回值语义）：
    //     0 = 未知（链路读失败，本轮不作判断）
    //     1 = 1088 里有人        -> 模仿者局
    //     2 = 链路通、确实没有    -> 正常对局
    //   ⚠ 关键区别：**读失败不能当成"没有"**，否则识别会抖，
    //     而识别一抖就会 set_copycat_book(false) -> 整本身份簿被清空 ✗
    int copycat_state = 0;
    for (int k = 0; k < 8; k++) g_skip_reason[k] = 0;
    const int types[4] = { 2, 1,
                           PySelf::COPYCAT_PLAYER_TYPE,    // 1088
                           PySelf::COPYCAT_GHOST_TYPE };   // 1089
    for (int ti = 0; ti < 4 && n < MAXT; ti++) {
        if (types[ti] <= 0) continue;
        uint64_t us[64] = {};
        const int un = PyAttack::get_all_units_of_type(types[ti], us, 64);
        if (types[ti] == PySelf::COPYCAT_PLAYER_TYPE) {
            if (un > 0)       copycat_state = 1;   // 有人 -> 模仿者局 ✓
            else if (un == 0) copycat_state = 2;   // 链路通且确实没有 -> 正常对局 ✓
            // un < 0：链路读失败 -> 保持 0，本轮不动判定 ✗
        }
        for (int i = 0; i < un && n < MAXT; i++) {
            if (!PyCore::is_obj(us[i])) { g_skip_reason[0]++; continue; }
            Target t;
            if (!read_one(us[i], types[ti], t)) { g_skip_reason[1]++; continue; }
            if (g_filter == 1 && t.camp != 2) { g_skip_reason[2]++; continue; }
            if (g_filter == 2 && t.camp != 1) { g_skip_reason[3]++; continue; }
            g_t[n++] = t;
        }
    }
    g_n = n;

    // ★★ 2026-10-14 修复："识别抖动"会把整本身份簿清空 ★★
    //
    //   原实现是 `g_is_copycat = copycat_seen;` —— **无条件覆盖** ✗
    //   而 copycat_seen 只在 `get_all_units_of_type(1088) > 0` 时为真，
    //   可那个函数在**读失败**时同样返回 0（开会阶段 Python 页被换出、
    //   内存压力下页换出、跨对局的瞬间…），和"本局确实没有 1088"
    //   **完全分不开** —— 正是 SKILL.md 4.6 说的"读不到 ≠ 没有" ✗
    //
    //   后果链（用户报的"设置页显示是模仿者局，但一个人都不标身份"）：
    //     某一轮读失败 -> g_is_copycat=false -> draw_Gui 每帧
    //     set_copycat_book(false) -> PySelf 的 g_book_enabled=false ->
    //     book_merge 头一句就把 g_book_work 整个 memset 掉（簿子清空）✗
    //     下一轮又读到 1088 -> 识别回到 true，但簿子已经空了 ✗
    //   （现场日志：`[自动识别] 本局 -> 模仿者局 / 正常对局` 反复交替）
    //
    //   修法：只有**连续 kCopycatMissRounds 轮**都没看到 1088 才翻成 false；
    //     期间保持上一次判定 —— 单轮读失败不再影响识别 ✓
    //   代价：真的从模仿者局回到正常对局时，最多晚 3 轮（400ms/轮 = 1.2 秒）
    //     才纠正。这个延迟无害（那一刻本来也没身份可画），换来的是身份簿不再被误清 ✓
    static const int kCopycatMissRounds = 3;
    if (copycat_state == 1) {              // 1088 里读到人 -> 模仿者局
        g_copycat_miss = 0;
        g_is_copycat = true;
    } else if (copycat_state == 2) {       // 链路通、确实没有 -> 连续 N 轮才翻 false
        if (g_copycat_miss < kCopycatMissRounds) {
            if (++g_copycat_miss >= kCopycatMissRounds) g_is_copycat = false;
        }
    }
    // copycat_state == 0（链路读失败）：**什么都不做**，保持上一次判定 ✗
    //   这是最关键的一条：开会阶段 / 内存压力下 1088 会整段读不出来，
    //   那时候绝对不能判成"正常对局" —— 否则身份簿会被清空 ✗

    // 标记"动没动"（对比上次同一 uid 的坐标）
    for (int i = 0; i < g_n; i++) {
        g_t[i].moved = false;
        for (int k = 0; k < MAXT; k++) {
            if (g_puid[k] == g_t[i].uid) {
                const float dx = g_t[i].x - g_px[k], dy = g_t[i].y - g_py[k],
                            dz = g_t[i].z - g_pz[k];
                g_t[i].moved = (dx * dx + dy * dy + dz * dz) > 4.0f;   // >2 单位算动了
                break;
            }
        }
    }
    for (int i = 0; i < g_n && i < MAXT; i++) {
        g_puid[i] = g_t[i].uid;
        g_px[i] = g_t[i].x; g_py[i] = g_t[i].y; g_pz[i] = g_t[i].z;
    }

    // 排序
    //   ★ 模仿者局**按号码排**（用户要求"模仿者排列按序号来排列"）✓
    //     普通局还是按距离排 ✓
    if (g_is_copycat) {
        // 插入排序：号码小的在前；没号码的（死者/未知）排到最后
        for (int i = 1; i < g_n; i++) {
            Target key = g_t[i];
            int j = i - 1;
            while (j >= 0) {
                const int a = (g_t[j].seat > 0) ? g_t[j].seat : 9999;
                const int b = (key.seat    > 0) ? key.seat    : 9999;
                if (a <= b) break;
                g_t[j + 1] = g_t[j];
                j--;
            }
            g_t[j + 1] = key;
        }
    } else {
        // 普通局：按距离由近到远（固定行为，没有开关）✓
        for (int i = 1; i < g_n; i++) {
            Target key = g_t[i];
            int j = i - 1;
            while (j >= 0 && g_t[j].dist_m >= 0.0f && key.dist_m >= 0.0f &&
                   key.dist_m < g_t[j].dist_m) {
                g_t[j + 1] = g_t[j];
                j--;
            }
            g_t[j + 1] = key;
        }
    }

    // 选中项按 uid 重新定位
    g_sel = -1;
    for (int i = 0; i < g_n; i++)
        if (g_t[i].uid == g_sel_uid) { g_sel = i; break; }

    snprintf(g_status, sizeof(g_status), "扫到 %d 个角色%s", g_n,
             g_sel >= 0 ? "，已选中" : "");
}

// ---- ★★ 视角注入（2026-10-13 打通）★★ --------------------------------
//
// 机制：**`g_cam_ctrl.unit` 就是"相机跟随谁"** ✓
//   把它换成别的角色对象，相机立刻切到那个角色的视角 ✓
//
// 为什么这个可行（和失败的那些的区别）：
//   · 游戏**自己**在切操控对象（切机械玩偶/信徒）时就会改这个字段
//     → 它在**活跃代码路径**上 ✓
//   · 实测：写进去后 12 秒一直保持，游戏不会覆盖它 ✓
//   · 实测：相机持续跟随目标移动（cam 值一路在变）✓
//   · 实测：**全程不崩**，引用计数 457/281 不变 ✓
//   · 原值可保存、可一键还原 ✓
//
// 反面教材（都试过、都失败）：
//   ✗ 写单位坐标快照      -> 引擎每帧重算（断点实测 162 次/帧）
//   ✗ FollowMode.lock_*   -> 断点实测游戏**从不写**这些字段 = 休眠功能
//   ✗ 手改 refcount       -> 破坏 CPython 不变量 -> 崩游戏 ☠
//
// ⚠ 安全要点：
//   · 只改 `unit` 这一个字段，**绝不碰 refcount** ✓
//   · 切换前保存原值，退出/关闭时还原 ✓
//   · 目标要一直存活：游戏本来就持有所有单位对象，所以不用我们操心 ✓

// 找 g_cam_ctrl 这个 dict 对象（它是 dict，不是实例对象）
inline uint64_t gcam_dict()
{
    const char *why = nullptr;
    const uint64_t md = PyCore::game_kernel_dict(why);
    if (!md) return 0;
    PyCore::Dict dk;
    if (!PyCore::read_dict(md, dk)) return 0;
    const int64_t i = PyCore::resolve_index(md, dk, -1, "g_cam_ctrl");
    if (i < 0) return 0;
    return getPtr64(PyCore::value_slot(dk, i));
}

// 解析 g_cam_ctrl.unit 的【值槽地址】—— 用工程自己的布局代码，不猜偏移 ✓
inline uint64_t resolve_cam_slot()
{
    // ★ 跨对局检测：换了局就先把旧槽地址清掉，重新解析 ✓
    check_new_match();

    // ★★ 2026-10-14：去掉永久缓存 ★★
    //   ...
    g_cam_why = "";

    const char *why = nullptr;
    const uint64_t md = PyCore::game_kernel_dict(why);
    if (!md) { g_cam_why = why ? why : "game_kernel_dict 失败"; g_cam_slot = 0; return 0; }

    PyCore::Dict mk;
    if (!PyCore::read_dict(md, mk)) { g_cam_why = "read_dict(game_kernel) 失败"; g_cam_slot = 0; return 0; }
    const int64_t ig = PyCore::find_key(md, "g_cam_ctrl", -1);
    if (ig < 0) { g_cam_why = "game_kernel 里没有 g_cam_ctrl"; g_cam_slot = 0; return 0; }
    const uint64_t d = PyCore::get_attr(md, ig);
    g_cam_dict_addr = d;
    if (!PyCore::is_obj(d)) { g_cam_why = "g_cam_ctrl 不是对象"; g_cam_slot = 0; return 0; }

    uint64_t target = d;
    PyCore::Dict cd;
    if (!PyCore::read_dict(target, cd)) {
        target = PyCore::get_inst_dict(d);
        if (!target || !PyCore::read_dict(target, cd)) {
            g_cam_why = "g_cam_ctrl 既不是 dict 也不是实例字典";
            g_cam_slot = 0;
            return 0;
        }
    }
    const int64_t iu = PyCore::find_key(target, "unit", -1);
    if (iu < 0) { g_cam_why = "g_cam_ctrl 里没有 unit"; g_cam_slot = 0; return 0; }
    g_cam_slot = PyCore::value_slot(cd, iu);
    return g_cam_slot;
}

// 读当前 unit（相机正跟着谁）
inline uint64_t cam_unit()
{
    const uint64_t s = resolve_cam_slot();
    if (!s) return 0;
    return getPtr64(s);
}

// 把 unit 写成指定对象。⚠ 先取原值存进 g_my_unit ✓
inline bool set_cam_unit(uint64_t u)
{
    const uint64_t s = resolve_cam_slot();
    if (!s || !u) return false;
    if (!g_my_unit) g_my_unit = getPtr64(s);      // 第一次调用时记住"我"
    vm_writev(s, &u, 8);
    const uint64_t back = getPtr64(s);
    g_inject_n++;
    return back == u;
}

// 切换到某个角色（view = 那个角色的单位对象）
inline bool LockTo(uint64_t unit_obj, int uid)
{
    if (!unit_obj) { snprintf(g_status, sizeof(g_status), "目标无效"); return false; }
    if (!set_cam_unit(unit_obj)) {
        snprintf(g_status, sizeof(g_status), "写入失败（拿不到 g_cam_ctrl.unit 槽）");
        return false;
    }
    g_lock_unit = unit_obj;
    g_lock_uid  = uid;
    snprintf(g_status, sizeof(g_status), "已切换到 uid=%d 的视角", uid);
    return true;
}

// 还原成我自己的视角
inline bool UnlockView()
{
    if (!g_my_unit) {
        snprintf(g_status, sizeof(g_status), "还没记录过自己的单位");
        return false;
    }
    if (!set_cam_unit(g_my_unit)) {
        snprintf(g_status, sizeof(g_status), "还原失败");
        return false;
    }
    g_lock_unit = 0;
    g_lock_uid  = 0;
    snprintf(g_status, sizeof(g_status), "已还原成自己的视角");
    return true;
}

// 目标飞了/死了（对象还在但不在列表里）时自动还原 —— 免得相机卡在别人身上
inline void Tick()
{
    if (!g_lock_unit) return;
    bool alive = false;
    for (int i = 0; i < g_n; i++) if (g_t[i].unit == g_lock_unit) { alive = true; break; }
    if (!alive) UnlockView();
}

// ---- 应用（把视角切到选中角色）----------------------------------------
inline bool ApplyView()
{
    if (g_sel < 0 || g_sel >= g_n) {
        snprintf(g_status, sizeof(g_status), "没选中角色");
        return false;
    }
    const Target &t = g_t[g_sel];
    g_apply_n++;
    return LockTo(t.unit, (int)t.uid);
}

inline void Reset()
{
    g_n = 0; g_sel = -1; g_sel_uid = 0;
    snprintf(g_status, sizeof(g_status), "已清空");
}

} // namespace ViewSwitch