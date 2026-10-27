#ifndef IDV_PY_SELF_H
#define IDV_PY_SELF_H

// ============================================================================
// 自身锚点 + 废弃模型黑名单 —— 走游戏内嵌 CPython 的对象图读取
// ============================================================================
//
// 解决两件一直靠启发式凑的事：
//
// 一、"当前操控的是谁"
//   旧做法是 draw_Gui.cpp 里那套 `相机深度(10~40) + zy + 阵营是1或2`。
//   它的问题：任何一个求生者走进 10~40 这条深度带就会被误判成自身，而 +0xaa(zy)
//   按实测是通用状态位、**不区分是否自身**。自身锚错 -> Z 锚错 -> 全场距离全错，
//   而且那个对象会被 continue 掉不再绘制。
//
//   ★ 引擎自己持有答案：GK.g_cam_ctrl.unit 就是"当前视角/操控的单位"。
//     2026-09-22 受控实验，两角色两阵营各切换一次共 4 次读数，4/4 全部指对：
//       求生者机械师  本体 ⇄ 机械玩偶(MyCivilianPuppetUnit, 在 units_by_type[2])
//       监管梦之女巫  本体 ⇄ 梦之信徒(MyYidhraPuppetUnit,   在 units_by_type[236])
//
//   ！！`GK.g_unit` 不能用 ！！它是"我的主角色"，切到从属时纹丝不动(4/4 从不跟随)。
//   拿它做自身高亮，机械师操控玩偶 / 女巫操控信徒时会高亮错人。这是个陷阱，
//   曾经的记录"g_unit = 本机玩家"在能切换操控对象的角色上就是错的。
//   同理 `g_cam_ctrl.free_mode_unit` 恒为主角色，也不是我们要的。
//
//   另外这两个字段**不要碰**：
//     is_operating     机械师挂在本体上，语义跟名字相反("本体正在遥控玩偶中")，
//                      而且每个角色名字都不一样(女巫叫 yidhra_operating)，没通用性
//     bj_operating     所有单位(含 AI)恒为 True，完全没用
//
// 二、"废弃模型"
//   形态切换类角色(红蝶等)换形态**不是换操控对象**(cam.unit 不变)，是同一个 unit
//   换模型。旧形态的场景对象留在数组里、仍然被绘制循环捕获，这就是"废弃模型"。
//
//   ★ 判据：unit.another_model + 0x20 就是废弃形态的场景对象指针。
//     实测红蝶一局 model+0x20 = 0x7ACBDFFF80(在画的那个)、
//     another_model+0x20 = 0x7ACDDD0600，后者与独立抓到的废弃对象地址逐位相同。
//   这替掉了 ShouldSkipEntity 里那份红蝶/木偶师类名黑名单：精确、跟视角无关、不用维护名单。
//
//   ⚠️ 不要拿 `+0x73`/`+0x70`(= NeoX 模型对象的 visible) 当过滤器。
//   它是渲染剔除的结果：废弃模型恒不可见，但**远处的真实对象同样不可见**
//   (实测手持飞刀这种真道具 +0x70 也是 0)。用它过滤会重演"求生者只看得到身边的密码机"。
//
//   ⚠️ 只覆盖 another_model 这一类。实测另外还有 balloon_model(女巫气球)、
//   _fragrance_image(木偶师忘忧之香残影) 等同样指向非活跃模型的属性，它们目前靠
//   ShouldSkipEntity 的鬼魂判定挡住。要做全的话，应当取 unit.model 的 ob_type
//   拿到 world.model 类型指针，再扫整个实例 dict 把该类型的值全部拉黑(只留 model 那份)，
//   并把扫出来的序号按对局缓存住，否则每轮扫上千个条目开销太大。
//
// 链路、CPython 3.11 布局、三条铁律跟 PyProgress.h 完全同源，那边文件头有详细说明。
// 这里按同样风格自带一份 dict 工具，免得去改那两个已经验证过的文件。
// ============================================================================

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <malloc.h>   // mallopt(M_PURGE)
#include <thread>
#include <atomic>
#include <unistd.h>
#include "PyRoot.h"
#include "PyCore.h"

// 命名空间跟文件同名(PySelf)；draw_Gui.cpp 里另有全局变量 `uintptr_t self_obj`，别起成 self_obj 之类重名
namespace PySelf {

// sys.modules 与 dict/int/True 由 PyRoot.h 运行时找出，这里不再写死模块偏移

static const int MAX_STALE = 32;
static const int MAX_BODIES = 32;

// 要扫的单位类型：1=监管(巡视者也在这里) 2=求生者(机械玩偶、幻灯师分身也在这里) 236=梦之信徒
// 1065=伊斯人幻影(yith_ghost03，2026-09-22 实测；漏了它时幻影被本体集合挡掉)。
// 局中召出来的从属单位常有独立的键，某个召唤物被误挡时先用 _scripts\cmd_phase.py 查它在哪个键下
// ★ 69 = HallComponentUnit，**准备/等待阶段站在大厅里的玩家模型**。
//   实测准备界面(h55_wait_copycat2)的 units_by_type 只有 69/1007/100 三种，
//   玩家全在 69 里。不收它 -> 本体集合为空 -> 绘制侧没有过滤 -> 16 个玩家的
//   全部子模型(separate_dir/LOD)一起画出来 -> **准备界面满屏框**。
static const int UNIT_TYPES[] = {1, 2, 69, 236, 1065};
static const int UNIT_TYPE_COUNT = sizeof(UNIT_TYPES) / sizeof(UNIT_TYPES[0]);

// ---- 本体集合：哪些场景对象是"角色本体"(2026-09-22 实测) ----
// 场景数组里跟角色同名、或者挂在角色身上的对象很多：_fragrance_image(每个求生者一份、同名、恒不可见)、
// another_model(另一形态)、balloon_model/umbrella_model(挂件)、item_lst[i].model(手持道具)、
// 时装挂件、约瑟夫相机……类名和 +0x240/+0x6D 都分不开(另一形态、气球跟本体一样是 2 / 0x50)。
// 唯一干净的定义：单位类型表里各键下每个单位的 unit.model + 0x20。
//   - 魔术师分身是 CloneUnit，在独立的键 [17] 里，不在这张表里，自然排除
//   - 幻灯师分身 SlideManCloneUnit 却在 [2] 里，靠 is_civilian_puppet == True 排除
//     (它 is_clone=False、is_puppet=False，这两个名字都骗人，别用)
//   - 机械玩偶 MyCivilianPuppetUnit 在 [2] 里、is_civilian_puppet=False，**保留**(用户要画它)
//   - owner_uid 别用：机械玩偶实测为 None，可能只在被操控时才有值
// ---- 模仿者模式(身份局，2026-09-24 两局实测，见 skill ruochen-teardown.md ⑦) ----
// 玩家是 units_by_type[1088] 的 CopycatPlayerUnit(12 人)，**不在键 1/2**。
//   identity_id    隐藏身份，进局约 2 秒后赋值、整局不变；全场真身份都下发给客户端
//   copycat_index  0~11，游戏里的号码 = 它 + 1
// 死亡后原单位留在 1088(模型不可见)，另生成 [1089] GhostCopycatPlayerUnit(同 unique_id，identity=None)。
// 所以"谁死了" = 1088 里 unique_id 出现在 1089 里的那些；死者不收进本体集合，绘制侧自然不画。
static const int COPYCAT_PLAYER_TYPE = 1088;
static const int HALL_COMPONENT_TYPE  = 69;    // 准备/等待阶段站大厅的玩家模型
static const int COPYCAT_GHOST_TYPE = 1089;
static const int MAX_COPYCAT = 16;
// ★ 这一局**真实存在**的最大座位号(实测模仿者一局 12 个座位)。
//
//   2026-10-10 用户报"正常对局 12 号死后被绘制成 13 号"：MAX_COPYCAT 是 16，
//   而身份簿里可能残留 13~16 号的过期条目(上一局/上一次运行的 known 位没清干净)，
//   于是拿尸体去查身份簿时命中了那个过期座位，**框上就写成了 13 号** —— 一个这局
//   根本不存在的座位号。
//   所以座位号一律先过这一道：超过本局实际座位数的，直接被当成无效。
static int g_cc_seat_max = 0;
static inline bool cc_seat_valid(int seat)
{
    // ★ 上界直接钉死 12 —— 模仿者**一局就是 12 个座位**，13~16 号只可能来自
    //   身份簿里的残留条目，永远不该被画出来。
    //   2026-10-10 教训：原来写成 `g_cc_seat_max == 0 || seat <= g_cc_seat_max`，
    //   而 g_cc_seat_max 是靠读 copycat_index 学出来的 —— **读失败时它一直是 0**，
    //   而 0 被我当成"未知、放行"，于是用户又看到"在我身上绘制出了13号的框"。
    //   别拿"学出来的上界"当唯一防线，硬上限才可靠。
    (void)g_cc_seat_max;
    return seat >= 1 && seat <= 12;
}
// identity 0 = 还没赋值/读不到；seat 从 1 开始，0 = 读不到；body 0 = 读不到
// ★ soul = 这个人死后那一具**灵魂体的场景对象**([1089] 里对应项的 model)。
//   为什么要记它：绘制侧要能把"灵魂体"这个名字换成"N号 身份（已死）"。
//   灵魂体和本体是**两个不同的场景对象**，只靠 body 认不出来；而 [1089] 那一项
//   带的是同一个 unique_id，所以这里用 unique_id 把两者接起来(见 collect_copycat)。
// ★ invis = 本体模型**连续多少轮**读到 +0x70==0(引擎没渲染它)。见 collect_copycat 里
//   死亡判据第三条：攒够 kInvisDeadRounds 轮才认定出局，单次读数不做数。
struct CopycatEntry { uint64_t body; uint64_t soul; int identity; int seat; int invis; bool dead; };

struct Snapshot {
    uint64_t anchor;                  // 当前操控单位的场景对象，0 = 没读到
    int      camp;                  // 1=监管 2=求生者 其它=从属单位的 unit_type
    int64_t  uid;
    uint64_t stale[MAX_STALE];
    int      stale_count;
    uint64_t body[MAX_BODIES];      // 本体集合，见上
    int      body_count;
    uint64_t hunter_body;              // [1] 里第一个类名不含 Puppet 的单位(跳过巡视者)，0 = 没有
    CopycatEntry copycat[MAX_COPYCAT]; // 模仿者模式的活人，见上
    int      copycat_count;
    // 模仿者模式死者灵魂的场景对象(= [1089] 每一项的 model)。
    //   为什么要单独收：绘制侧原来用 IsGhostEntity(+0x70/+0x1a0) 判"是不是灵魂"，
    //   但那两个字段其实是**渲染可见性** —— 分身的子模型、LOD、separate_dir 部件、
    //   远处被剔除的真实对象全都满足，于是一个活人身上会多画好几个框。
    //   这里直接从引擎的 [1089] 列表拿，精确到对象，不再靠可见性猜。
    uint64_t soul[MAX_BODIES];
    int      soul_count;
    int self_skill_state;   // 自身当前技能状态 id，0=不在技能
};

// 双缓冲发布：写线程填非活跃缓冲，填完再翻转，绘制线程永远读到完整的一份
static Snapshot g_buf[2];
static std::atomic<int> g_active{0};      // 见 PyProgress.h 同名变量的注释(volatile 不保证发布顺序)

static uintptr_t g_libbase = 0;
static uint64_t  g_module_dict = 0;
static int64_t   g_i_cam = -1, g_i_cam_unit = -1;
static int64_t   g_i_unit_mgr = -1, g_i_ubt = -1;
static int64_t   g_i_model = -1, g_i_another = -1, g_i_utype = -1, g_i_uid = -1;
// another_model 的序号缓存**按类分开存**(键 = 实例的 ob_type)。
// 各玩家类(ButcherUnit / CivilianUnit / MyCivilianUnit / 各种 Puppet ...)实例字典大小不同，
// another_model 的序号也不同；共用一个缓存时，遍历每换一个类就失效一次、从头按名字扫到它
// (字典上千条，每条 2 次读取)，一轮要扫 2~4 次，占了这个线程九成的读取量。
// 分开存之后每个类只在第一次出现时扫一次。缓存从不被直接信任：resolve_index() 每次都按名字核对，
// 核对不上就重扫，所以最坏情况就是退回共用缓存那种多扫几遍，不会读错属性。
// 表满了(类比这个多)就退回共用的 g_i_*。model / is_civilian_puppet 同理，一起按类存。
struct ClassIndexCache { uint64_t type; int64_t i_another; int64_t i_model; int64_t i_cpuppet; bool is_puppet; };
static const int MAX_CLASSES = 8;
static ClassIndexCache g_class_cache[MAX_CLASSES];
static int     g_class_cache_count = 0;
static int64_t g_i_unit_model = -1, g_i_cpuppet = -1;   // 表满时的共用缓存
// 模仿者模式只有两个类(CopycatPlayerUnit / GhostCopycatPlayerUnit)，各用一组序号缓存就够
static int64_t g_cc_i_model = -1, g_cc_i_another = -1, g_cc_i_ident = -1, g_cc_i_index = -1, g_cc_i_uq = -1;
static int64_t g_cc_i_isdead = -1, g_cc_i_isdead2 = -1;   // unit._is_dead / unit.is_dead
static int64_t g_cc_i_cu = -1, g_cc_i_dbu = -1, g_cc_i_cud = -1;   // unit.combat_unit / .dead_body_uid / ._is_dead
static int64_t g_cc_i_pos = -1;   // unit.position —— 唯一坐标源(不要加 last_server_pos 后备，见下面那段注释)
// 本帧有多少个座位的 unit.position 读失败。只用于面板显示：读失败时那个框会沿用旧位置
// (不跳)，但面板上必须看得见，否则"框为什么不动"又只能靠猜。
static int g_pos_fail_tick = 0;
static int64_t g_ccg_i_uq = -1;
static int64_t g_ccg_i_model = -1;   // [1089] 灵魂项的 model 序号缓存（按类分开存）

static volatile bool g_available = false;
// 局外(大厅/准备阶段)：units_by_type 完整读出来了、而且没有键 1/2。
// 准备阶段 cam.unit 是 MyCityUnit，它的 model+0x20 读不出场景指针(2026-09-22 实测)，锚点那步会失败，
// 所以这个标志不挂在 快照/g_available 上，在锚点之前单独算、单独发布。读不全一律算"不是局外"
static volatile bool g_in_lobby = false;
static volatile int  g_fail_streak = 0;
static char g_status[96] = "未启动";

// ---- 公共 CPython 工具：实现在 PyCore.h，三个模块共用一份(见那里的文件头) ----
using PyCore::is_obj; using PyCore::str_equals; using PyCore::Dict; using PyCore::read_dict;
using PyCore::key_slot; using PyCore::value_slot; using PyCore::find_key; using PyCore::get_attr;
using PyCore::resolve_index; using PyCore::get_type_list; using PyCore::get_inst_dict;
using PyCore::read_int; using PyCore::read_list; using PyCore::get_scene_obj;

static bool resolve_module()
{
    const char *why = "";
    g_module_dict = PyCore::game_kernel_dict(why);
    if (!g_module_dict) { snprintf(g_status, sizeof(g_status), "%s", why); return false; }
    return true;
}

// 判断是不是局外。units_by_type 的每个键都要读成功才下结论 —— get_type_list() 返回 0 分不清
// "没有这个键"和"键对象所在页读不到"，拿它判会在对局中途的读失败里误判成局外、把人全藏掉。
// 局外 = 没有键 1(监管) 也没有键 2(求生者)。2026-09-22 准备阶段实测键只有 53/59/74/100/1009。
// 模仿者模式的玩家在键 1088，不算的话这个模式会被判成局外、人物整类不画
static bool check_lobby(uint64_t ubt, bool &in_lobby)
{
    Dict k;
    if (!read_dict(ubt, k) || k.n_entries <= 0) return false;
    bool has_player = false;
    for (int64_t i = 0; i < k.n_entries; i++) {
        uint64_t kp = getPtr64(key_slot(k, i));
        if (kp == 0) continue;                         // 已删除的条目
        if (!is_obj(kp) || getPtr64(kp + 8) != PyRoot::g_int_type) return false;
        int64_t sz = 0; uint32_t dg = 0;
        if (!vm_readv(kp + 16, &sz, 8) || !vm_readv(kp + 24, &dg, 4)) return false;
        // 69 = HallComponentUnit(准备阶段站大厅的玩家)，见 UNIT_TYPES 的注释。
        // 不认它 -> 准备界面被判成"没有玩家=局外" -> 阵营 1/2 整类不画。
        if (sz == 1 && (dg == 1 || dg == 2 || dg == HALL_COMPONENT_TYPE || dg == COPYCAT_PLAYER_TYPE))
            has_player = true;
    }
    in_lobby = !has_player;
    return true;
}

// 类名(PyTypeObject.tp_name，+0x18 是 char*)里含不含 Puppet。
// 实测 [1] 里的巡视者是 MyButcherPatrolPuppetUnit，挑"监管本体"时要跳过它
static bool class_name_has_puppet(uint64_t type)
{
    uint64_t np = getPtr64(type + 0x18);
    if (!is_obj(np)) return false;
    char buf[48] = {0};                                // 最长的 MyButcherPatrolPuppetUnit 也才 25 字节
    if (!vm_readv(np, buf, sizeof(buf) - 1)) return false;
    return strstr(buf, "Puppet") != nullptr;
}

static inline void add_unique(uint64_t *tbl, int &cnt, int cap, uint64_t sp)
{
    for (int j = 0; j < cnt; j++) if (tbl[j] == sp) return;
    if (cnt < cap) tbl[cnt++] = sp;
}

// 扫一类单位：
//   another_model -> 废弃模型黑名单
//   model         -> 本体集合(幻灯师分身 is_civilian_puppet==True 不收)
//   [1] 里第一个类名不含 Puppet 的 -> 监管本体(预知监管用)
static void collect_units(uint64_t ubt, int type, Snapshot &res)
{
    uint64_t lst = get_type_list(ubt, type);
    int64_t n = 0; uint64_t items = 0;
    if (!read_list(lst, n, items) || n == 0) return;

    for (int64_t i = 0; i < n; i++) {
        uint64_t inst = getPtr64(items + i * 8);
        uint64_t d = get_inst_dict(inst);
        if (!d) continue;
        Dict dk;
        if (!read_dict(d, dk)) continue;

        // 按实例的类取序号缓存(见 g_class_cache)。类型指针读不到、或表满了就用共用缓存
        // 别叫 type：会遮住参数 type(单位类型键)，导致下面 type == 1 永远不成立、hunter_body 永远拿不到
        uint64_t cls = getPtr64(inst + 8);
        ClassIndexCache *slot = nullptr;
        if (is_obj(cls)) {
            for (int j = 0; j < g_class_cache_count; j++)
                if (g_class_cache[j].type == cls) { slot = &g_class_cache[j]; break; }
            if (!slot && g_class_cache_count < MAX_CLASSES) {
                slot = &g_class_cache[g_class_cache_count++];
                slot->type = cls;
                slot->i_another = slot->i_model = slot->i_cpuppet = -1;
                slot->is_puppet = class_name_has_puppet(cls);    // 类名一个类只读一次
            }
        }
        int64_t &i_another = slot ? slot->i_another : g_i_another;
        int64_t &i_model = slot ? slot->i_model : g_i_unit_model;
        int64_t &i_cpuppet = slot ? slot->i_cpuppet : g_i_cpuppet;

        // 实测所有玩家类都有 another_model(没有第二形态时值是 None)，查不到就跳过(不是错误)
        i_another = resolve_index(d, dk, i_another, "another_model");
        if (i_another >= 0) {
            uint64_t sp = get_scene_obj(getPtr64(value_slot(dk, i_another)));
            if (sp) add_unique(res.stale, res.stale_count, MAX_STALE, sp);
        }

        i_model = resolve_index(d, dk, i_model, "model");
        if (i_model < 0) continue;
        uint64_t body = get_scene_obj(getPtr64(value_slot(dk, i_model)));
        if (!body) continue;

        // 幻灯师分身：is_civilian_puppet 是 True 单例。属性不存在(监管类)或读不到都按"不是分身"
        i_cpuppet = resolve_index(d, dk, i_cpuppet, "is_civilian_puppet");
        if (i_cpuppet >= 0 && getPtr64(value_slot(dk, i_cpuppet)) == PyRoot::g_true) continue;

        add_unique(res.body, res.body_count, MAX_BODIES, body);
        if (type == 1 && res.hunter_body == 0 && !(slot ? slot->is_puppet : class_name_has_puppet(cls)))
            res.hunter_body = body;
    }
}

// 判据本身带一点"渲染剔除"的味道，所以**只用于诊断输出**，不参与死亡判定
// (2026-10-10 用连续采样否掉了它，理由见 collect_copycat 里那段注释)。
static const int kInvisDeadRounds = 50;

// ---- 模仿者身份簿(锁存) ----
// 思路同 PyGenius.h 的天赋锁存，但简单得多：身份整局不变，按号码(copycat_index)记，
//   - 读到就写进簿子(新读到的值永远覆盖旧值 —— 所以万一没清干净，新一局第一次读到就自动纠正)
//   - 读不到就沿用簿子
//   - 死亡只会置上、不会撤销
//   - unit_mgr 地址变了(换局/回大厅)就整本清空
// 只在界面上「模仿者模式」打开时记(g_book_enabled)，关掉就清空。
// 发布方式跟 Snapshot 一样双缓冲，但**不依赖锚点成功** —— 锚点那步失败时簿子照样更新和发布。
//
// ★ 类型和全局必须定义在 collect_copycat **之前**：collect_copycat 要用簿子里已经锁存的
//   dead 标志(按号码)当死亡判据的第二条腿 —— 见那里的注释。
// ★ body_old = 这个座位**上一个**锚点对象。假面的正解靠它。
//
//   2026-10-10 抓到假面现场(逐帧，锚点地址 + 是否在渲染实体列表里)：
//     03:00:58  绘制=1   9:N/34154B0 2:N/341D2A0 … 5:N/3423380    ← 12 个全 N
//     03:01:03  绘制=1   7:Y/AAB480                                ← 7 号先恢复
//     03:01:11  绘制=3   1:Y/AB9D00
//   假面把 unit.model 整批换成新对象(0x034 段)，而那批**旧对象其实还在渲染**
//   (假面结束后地址原样变回来，就是铁证)。
//
//   我先试过"只在新锚点确实在渲染(+0x70!=0)时才更新"—— **无效**：假面那批新对象的
//   +0x70 也是非 0(引擎认为它在渲染，却不在绘制侧遍历的实体列表里)。所以放弃"判断该不该
//   更新"，改成**新旧都留着**：绘制时任一匹配上都算认出这个座位。
//   假面期间新锚点谁也匹配不上，旧锚点仍匹配着那批在渲染的对象 —— 照样画得出来。
struct CopycatSeat { bool known; uint64_t body; uint64_t body_old; uint64_t soul; int identity; int invis; bool dead; };
struct CopycatBook { CopycatSeat seat[MAX_COPYCAT]; int self_seat; };   // seat[号码-1]；self_seat 从 1 开始，0 = 不知道
static CopycatBook g_book[2];
// 每个座位的世界坐标(每帧从 unit.position 刷新)。**必须声明在 collect_copycat 之前** ——
// 那里要写它；而按坐标认领座位的辅助函数在文件后面读它。它一直在动，所以不落盘。
static float g_seat_pos[16][3];
static bool  g_seat_pos_ok[16];

// ★ 持久跟踪坐标：只在**读到有效值时**更新，读不到(0/垃圾)时保留上一次的值。
//   2026-10-10 实测(假面进行中，逐座位逐分量)：
//      3N pos(-72,1,-218) ent(-72,1,-218) d=0.0      ← 精确
//      5Y pos(-13,61,-86) ent(-13,61,-86) d=0.0      ← 精确(二楼 y=61)
//      7N pos(0,0,0)      ent(0,0,0)      d=0.0      ← 假匹配：两边都是 0
//      4Y pos(-122,1,-24) ent(-127,1,-19) d=6.6      ← 飘了 6.6 米
//     10Y pos(-132,1,-14) ent(-127,1,-19) d=7.4      ← 飘了 7.4 米
//   也就是说 unit.position **是活的、但不总是可用**。单帧快照会把"这一帧恰好读到 0
//   或恰好飘了"当成事实，于是那一号的框就退回按地址匹配、画到别的楼层去。
//   持久跟踪把这类瞬时噪声滤掉：坐标只在可信时前进，不可信时原地不动。
static float g_seat_track[16][3];
static bool  g_seat_track_ok[16];

// 刚拿到的这一帧原始坐标是不是可信的(非零、有限、量级正常)
static bool seat_pos_trustworthy(const float *xyz)
{
    if (!(xyz[0] == xyz[0]) || !(xyz[1] == xyz[1]) || !(xyz[2] == xyz[2])) return false;  // NaN
    if (xyz[0] == 0.f && xyz[1] == 0.f && xyz[2] == 0.f) return false;                    // 未初始化
    return fabsf(xyz[0]) < 60000.f && fabsf(xyz[1]) < 60000.f && fabsf(xyz[2]) < 60000.f;
}

static std::atomic<int> g_book_active{0};
static CopycatBook g_book_work;                 // 只有 PySelf 线程碰
static uint64_t g_book_um = 0;
static std::atomic<bool> g_book_enabled{false};

// ★ 身份簿的落盘(2026-10-10 加)。为什么必须存：
//   死亡只由 [1089] 那个 GhostCopycatPlayerUnit 在**死亡那一瞬间**短暂出现来发现，
//   之后引擎就把整张表删掉了(type=1089 会整个消失)。也就是说"谁死了"这件事只有一次
//   采集机会，全靠簿子锁存下来。而簿子原来只在内存里 —— **overlay 一重启(崩溃/换版本)
//   就全丢**，那一局剩下的时间里所有死者都会被当成活人画出来(用户报的正是这个)。
//   存盘之后重启不再丢；换局时靠 um(unit_mgr 地址)判定，不是同一局就整本作废。
//   ⚠ um 必须在**第一次 refresh 之后**才能比对，所以加载时先挂在 g_book_pending_um 上，
//     等 book_merge 拿到当前 um 再决定"认领"还是"作废"。
static uint64_t g_book_pending_um = 0;
static int      g_book_save_tick = 0;           // 身份更新的落盘节流(见 book_merge)
static const char *kBookPath = "/data/local/tmp/dwrg_ccbook.bin";

// 下面两个在文件更靠后的位置定义(book_publish/book_empty)，这里先声明：
// 落盘/加载要用到它们，而它们必须排在 collect_copycat 之后。
static void book_publish();
static bool book_empty(const CopycatBook &b);

struct BookFile {
    char     magic[8];        // "DWCCBK2\0" —— ★ 加了 body_old 字段，布局变了，必须换 magic，
    uint64_t um;
    int      self_seat;
    CopycatSeat seat[MAX_COPYCAT];
};

static void book_save()
{
    if (!g_book_enabled) return;
    BookFile f;
    memset(&f, 0, sizeof(f));
    memcpy(f.magic, "DWCCBK2", 7);
    f.um        = g_book_um;
    f.self_seat = g_book_work.self_seat;
    memcpy(f.seat, g_book_work.seat, sizeof(f.seat));
    FILE *fp = fopen(kBookPath, "wb");
    if (!fp) return;                       // 存不下就算了，不能影响主流程
    fwrite(&f, 1, sizeof(f), fp);
    fclose(fp);
}

static void book_load()
{
    FILE *fp = fopen(kBookPath, "rb");
    if (!fp) return;
    BookFile f;
    size_t n = fread(&f, 1, sizeof(f), fp);
    fclose(fp);
    if (n != sizeof(f) || memcmp(f.magic, "DWCCBK2", 7) != 0) return;
    memcpy(g_book_work.seat, f.seat, sizeof(g_book_work.seat));
    g_book_work.self_seat = f.self_seat;
    g_book_pending_um = f.um;
    bool any = false;
    for (int i = 0; i < MAX_COPYCAT; i++) if (g_book_work.seat[i].known) any = true;
    if (!any) { g_book_pending_um = 0; return; }
    // 先乐观打开"记簿"开关：绘制线程每帧都会用 set_copycat_book(copycat_mode) 覆盖它，
    // 但 PySelf 线程可能先跑一轮。若这里不开，第一轮 book_merge 会看到 g_book_enabled=false
    // 而把刚捡回来的簿子当场清掉。真关掉了模仿者模式的话，下一帧就会被纠正回来。
    g_book_enabled = true;
    book_publish();
    printf("[身份簿] 已从 %s 恢复(换局判定待第一次 refresh)\n", kBookPath);
    fflush(stdout);
}

// 模仿者模式：1088 里每个活人的 model+0x20 进本体集合，同时记下身份和号码；1089(鬼魂)只用来认死者
static void collect_copycat(uint64_t ubt, Snapshot &res)
{
    int64_t n = 0; uint64_t items = 0;
    if (!read_list(get_type_list(ubt, COPYCAT_PLAYER_TYPE), n, items) || n == 0) return;

    // 死者的 unique_id(字符串，账号 uid 这类纯数字)。读不到就当没人死，最坏是多画一具尸体
    char dead[MAX_COPYCAT][24];
    // ★ 与 dead[] 下标一一对应：那一项([1089] 的 GhostCopycatPlayerUnit)的 model 场景对象。
    //   回到 [1088] 里认出"这个人死了"之后，顺手把他的灵魂体地址也一起记下来，
    //   绘制侧才能把那个对象标成"N号 身份（已死）"，而不是退回显示角色名。
    uint64_t dead_soul[MAX_COPYCAT];
    int dead_n = 0;
    int64_t gn = 0; uint64_t gitems = 0;
    if (read_list(get_type_list(ubt, COPYCAT_GHOST_TYPE), gn, gitems)) {
        for (int64_t i = 0; i < gn && dead_n < MAX_COPYCAT; i++) {
            uint64_t d = get_inst_dict(getPtr64(gitems + i * 8));
            Dict dk;
            if (!d || !read_dict(d, dk)) continue;

            // 先收灵魂体本身。**这一步不能因为 unique_id 读不到就跳过** ——
            // 那样这个对象连"是灵魂"都不算了，会被后面当成分身部件/LOD 画出一堆框。
            uint64_t so = 0;
            g_ccg_i_model = resolve_index(d, dk, g_ccg_i_model, "model");
            if (g_ccg_i_model >= 0) {
                so = get_scene_obj(getPtr64(value_slot(dk, g_ccg_i_model)));
                if (so) add_unique(res.soul, res.soul_count, MAX_BODIES, so);   // "灵魂体"的精确名单
            }

            // 再取 unique_id 记进死者名单。读不到就只跳过"记名字"这一步，
            // 不能让 dead[] 和 dead_soul[] 的下标错位。
            g_ccg_i_uq = resolve_index(d, dk, g_ccg_i_uq, "unique_id");
            if (g_ccg_i_uq < 0 || !PyCore::read_str(getPtr64(value_slot(dk, g_ccg_i_uq)), dead[dead_n], sizeof(dead[0])))
                continue;
            dead_soul[dead_n] = so;      // 这个死者对应的灵魂体地址(可能为 0：model 没读到)
            dead_n++;
        }
    }

    for (int64_t i = 0; i < n; i++) {
        uint64_t d = get_inst_dict(getPtr64(items + i * 8));
        Dict dk;
        if (!d || !read_dict(d, dk)) continue;

        g_cc_i_another = resolve_index(d, dk, g_cc_i_another, "another_model");
        if (g_cc_i_another >= 0) {
            uint64_t sp = get_scene_obj(getPtr64(value_slot(dk, g_cc_i_another)));
            if (sp) add_unique(res.stale, res.stale_count, MAX_STALE, sp);
        }

        // 每一项都可能单独读失败(内存压力下页被换出)，读不到的留 0，交给身份簿沿用旧值
        uint64_t body = 0;
        g_cc_i_model = resolve_index(d, dk, g_cc_i_model, "model");
        if (g_cc_i_model >= 0) body = get_scene_obj(getPtr64(value_slot(dk, g_cc_i_model)));

        // 号码(1 起)先读出来：下面判死活要用它去查身份簿
        int seat_no = 0;
        {
            int64_t v = 0;
            g_cc_i_index = resolve_index(d, dk, g_cc_i_index, "copycat_index");
            if (g_cc_i_index >= 0 && read_int(getPtr64(value_slot(dk, g_cc_i_index)), v) && v >= 0 && v < MAX_COPYCAT)
                seat_no = (int)v + 1;
            // ★ 本局真实的最大座位号：每轮取所有单位里最大的 copycat_index。
            //   用它把"身份簿里残留的 13~16 号过期条目"挡在外面（见 g_cc_seat_max 的注释）。
            if (seat_no > g_cc_seat_max) g_cc_seat_max = seat_no;
        }

        // ★ 世界坐标(假面按坐标认领座位用的就是它)：unit.position 是一个
        //   math3d.vector，payload 在对象 +0x10 处的 3 个 float —— 与实体侧
        //   (objcoor+0xa0) 同一坐标系，实测能精确重合(d=0.00)。
        //   读不到就把 ok 置 false，让那一号退出坐标认领，而不是拿旧坐标硬套。
        if (seat_no >= 1 && seat_no <= MAX_COPYCAT) {
            // ⚠ 只用 unit.position，**不要**退到 last_server_pos。
            //
            //   2026-10-10 实测(12 个座位逐个比对，与渲染实体的距离)：
            //     11号 position=(774,2,717) 最近实体 0.0   last_server_pos=(-185,1,-1) 最近 185.5
            //     12号 position=(785,0,728) 最近实体 0.0   last_server_pos=(-107,0,431) 最近 444.5
            //     …12/12 全是 position 距离 0.0、last_server_pos 距离 53~451
            //   两者**根本不在同一个坐标系**。曾经把 last_server_pos 当后备，结果 position
            //   一读失败就会拿它去投影 —— 框会跑到错误的地方(实测假面期间出现过
            //   (-73,224) 这类值，就是这个后备漏出来的)。
            //   宁可"这一帧没有坐标"(位置沿用旧值、框不跟着跳)，也不用错空间的值。
            bool ok = false;
            g_cc_i_pos = resolve_index(d, dk, g_cc_i_pos, "position");
            if (g_cc_i_pos >= 0) {
                uint64_t vp = getPtr64(value_slot(dk, g_cc_i_pos));
                float xyz[3] = { 0.f, 0.f, 0.f };
                // 不加 is_obj 守卫：它实测会误判(害我丢过 read_bool 那条判据)，
                // 加了就等于坐标一个都填不上。直接读，靠数值校验兜底。
                if (vm_readv(vp + 0x10, xyz, sizeof(xyz))) {
                    if (seat_pos_trustworthy(xyz)) {
                        memcpy(g_seat_pos[seat_no - 1], xyz, sizeof(xyz));
                        // ★ 可信值才推进"持久跟踪坐标"；不可信时它原地不动(见上面的注释)
                        memcpy(g_seat_track[seat_no - 1], xyz, sizeof(xyz));
                        g_seat_track_ok[seat_no - 1] = true;
                        ok = true;
                    }
                }
            }
            g_seat_pos_ok[seat_no - 1] = ok;
            if (!ok) g_pos_fail_tick++;      // 读失败计数(面板显示，见 g_pos_fail_tick 的注释)
        }

        // ★ 死亡判据现在是三条腿，缺一不可：
        //   ① 引擎的 [1089] 名单(unique_id 对得上) —— 死亡**发生的那一刻**只有它会变，
        //      是发现新死亡的唯一来源。
        //   ② 身份簿里已经锁存的死者(按号码) —— [1089] 整表读不到的时候(会议阶段、
        //      内存压力下 Python 堆页被换进 zram)唯一的来源。
        //   ③ combat_unit.dead_body_uid 有值 —— 见下面那段，**最可靠**的一条。
        //   只留 ① 的表现就是"有的人死掉了还是显示活着"：他死的那一刻恰好没读到 [1089]，
        //   之后 [1089] 一直是空的，这个死亡就**永远不会**被发现。
        bool is_dead = false;
        uint64_t soul_obj = 0;               // 这个人的灵魂体场景对象(只有在他是死者时才非 0)
        if (dead_n > 0) {
            g_cc_i_uq = resolve_index(d, dk, g_cc_i_uq, "unique_id");
            char uq[24];
            if (g_cc_i_uq >= 0 && PyCore::read_str(getPtr64(value_slot(dk, g_cc_i_uq)), uq, sizeof(uq)))
                for (int j = 0; j < dead_n; j++)
                    if (strcmp(uq, dead[j]) == 0) { is_dead = true; soul_obj = dead_soul[j]; break; }
        }
        if (!is_dead && seat_no >= 1 && seat_no <= MAX_COPYCAT) {
            const CopycatSeat &bs = g_book_work.seat[seat_no - 1];
            if (bs.known && bs.dead) is_dead = true;
        }

        // ---- ★ 第三条腿：combat_unit.dead_body_uid 有值 => 出局 ----------------------
        // 2026-10-10 实测(录像抓到 seat 10 出局那一刻)：
        //     dead_body_uid : None -> 1000029   ← 只有这一名下变，其余 11 人不动
        // 它是**死者自己的**尸体单位 uid，活人恒为 None。语义干净：
        //   "这个玩家身上挂着一具尸体" == 他出局了。
        //
        // 为什么这条最要紧：① 依赖的 [1089] 只在死亡那一瞬间存在，引擎随后把整张表
        // 删掉(dump 里 type=1089 会整个消失)，错过就永远认不出来 —— 那正是
        // "死人还显示活着"。dead_body_uid 是**持续存在**的，任何时候读都在。
        // 三种情况严格区分(读失败 != None != 有值)：
        //   读不到 combat_unit / 该键 / 该值 -> 什么都不改，交给另外两条腿
        //   NoneType                        -> 活着，明确不置死
        //   整数                            -> 死了
        if (!is_dead) {
            g_cc_i_cu = resolve_index(d, dk, g_cc_i_cu, "combat_unit");
            uint64_t cu = (g_cc_i_cu >= 0) ? getPtr64(value_slot(dk, g_cc_i_cu)) : 0;
            if (is_obj(cu)) {
                uint64_t cud = get_inst_dict(cu);
                Dict cuk;
                if (cud && read_dict(cud, cuk)) {
                    g_cc_i_dbu = resolve_index(cud, cuk, g_cc_i_dbu, "dead_body_uid");
                    if (g_cc_i_dbu >= 0) {
                        int64_t dbu = 0;
                        if (read_int(getPtr64(value_slot(cuk, g_cc_i_dbu)), dbu))
                            is_dead = true;          // 有值 = 有尸体 = 出局
                    }
                }
            }
        }

        // ---- ★★ 第三条腿：combat_unit._is_dead == True => 出局 ----------------------
        // 2026-10-10 实测(用户口述 9/11 已死但显示活人，2/10 判对了，全部 12 人逐个核对):
        //
        //     号  身份名     combat_unit._is_dead   dead_body_uid   实际
        //      2  哨兵       True                   1000027         死 ✓
        //     10  烟火师     True                   1000029         死 ✓
        //      9  地下医生   True                   None            死 ✓(原来漏判)
        //     11  修士       True                   None            死 ✓(原来漏判)
        //   其余 8 人         False                  None            活 ✓
        //
        //   **4/4 死者为 True，8/8 活人为 False** —— 这是目前唯一一个既持续存在、
        //   又 12/12 全对的判据。dead_body_uid 只对**部分**死亡落值(2、10 有、9、11 无)，
        //   所以它降级为兜底。
        //
        // 这里纠正一个害我绕了好几轮的误判：以前记的是"unit._is_dead 对已确认的死者
        // 也读出 False，所以没用"。错在**读错了对象** —— 恒为 False 的是 700 键的
        // 玩家单位 CopycatPlayerUnit._is_dead；真正有值的是它挂着的 **62 键 ECS 实体**
        // (unit.combat_unit) 上的 _is_dead。叠加上"dump 把 bool 一律打成 raw[..]、
        // 探针又把 bool 一律打成字符串 bool"，真假值被藏了两层。
        //
        // 三种情况照旧严格区分：读不到 combat_unit/该键 -> 交给别的判据；
        // False -> 明确活着，不置死；True -> 死。
        if (!is_dead) {
            g_cc_i_cu = resolve_index(d, dk, g_cc_i_cu, "combat_unit");
            uint64_t cu = (g_cc_i_cu >= 0) ? getPtr64(value_slot(dk, g_cc_i_cu)) : 0;
            if (is_obj(cu)) {
                uint64_t cud = get_inst_dict(cu);
                Dict cuk;
                if (cud && read_dict(cud, cuk)) {
                    // ⚠ 这里**不要**用 PyCore::read_bool：它在第一行
                    //   `if (!is_obj(vp)) return false;` 就静默退出了，而同样的值
                    //   在 dump 里用 `v == PyRoot::g_true` 直接比是读得到的(实测打印出
                    //   1(死))。一个返回 false 表示"读不到"，调用方再 `&& b` 一叠，
                    //   整条判据就永远不生效、还不报错 —— 这一条正是这么被坑掉的。
                    //   所以照 dump 里那套已验证可用的写法：直接比两个单例，三态。
                    g_cc_i_cud = resolve_index(cud, cuk, g_cc_i_cud, "_is_dead");
                    if (g_cc_i_cud >= 0) {
                        uint64_t v = getPtr64(value_slot(cuk, g_cc_i_cud));
                        if (v == PyRoot::g_true)       is_dead = true;   // 明确死了
                        else if (v != PyRoot::g_false) { /* 不是 bool，当读不到 */ }
                    }

                    // 兜底：dead_body_uid 有值(整数)同样表示这个人身上挂了一具尸体。
                    // 只对部分死亡生效，但读到就一定算数。
                    if (!is_dead) {
                        g_cc_i_dbu = resolve_index(cud, cuk, g_cc_i_dbu, "dead_body_uid");
                        if (g_cc_i_dbu >= 0) {
                            int64_t dbu = 0;
                            if (read_int(getPtr64(value_slot(cuk, g_cc_i_dbu)), dbu))
                                is_dead = true;
                        }
                    }
                }
            }
        }

        // ★ 第四条(保留但**实测无效**，只是不给判定添乱)：玩家单位自己的 _is_dead / is_dead。
        //   2026-10-10 实测：这两个键在 [1088] 的 CopycatPlayerUnit 上**都存在**，但
        //   对一具**已确认出局**的玩家(8号，v70=0、会议桌上游戏自己都不画他)读出来仍然是 False。
        //   (真正的死活在它挂的 combat_unit 上，见上一条。)留着只是因为成本近乎为零、
        //   且万一以后版本把它改成全局语义就能白捡一条判据 —— 它**只可能**把判定往"死"推，
        //   不会把已经认出来的死者变回活人。
        if (!is_dead) {
            g_cc_i_isdead = resolve_index(d, dk, g_cc_i_isdead, "_is_dead");
            if (g_cc_i_isdead >= 0 && getPtr64(value_slot(dk, g_cc_i_isdead)) == PyRoot::g_true) {
                is_dead = true;
            } else {
                g_cc_i_isdead2 = resolve_index(d, dk, g_cc_i_isdead2, "is_dead");
                if (g_cc_i_isdead2 >= 0 && getPtr64(value_slot(dk, g_cc_i_isdead2)) == PyRoot::g_true)
                    is_dead = true;
            }
        }

        // ---- +0x70 不可见轮数：**只做诊断，不参与死亡判定** --------------------------
        // 2026-10-10 一度想拿它当死亡判据(两局 12/12 都对上)，随后用连续 90 秒采样
        // **自己把它否掉了**：同一局里，用户确认已出局的 7 号 +0x70 在 71 秒后从 0 翻成
        // 0x1000000(尸体被重新渲染出来了)，而活着的 12 号同时从 0x1000000 翻成 0
        // (人被镜头剔除)。也就是说它确实是"引擎这一帧画没画它"，出了视野的活人一样是 0。
        // 拿它判死活，会把"背后站着的活人"标成尸体，而且是永久锁存 —— 代价远大于晚一拍。
        // 现在它只回答一件事："这个已知对象此刻引擎有没有在渲染"，供绘制侧决定
        // 要不要给一具尸体画框（见 draw_Gui 里 cc_dead 那段）。
        int invis = 0;
        if (seat_no >= 1 && seat_no <= MAX_COPYCAT) {
            const CopycatSeat &ps = g_book_work.seat[seat_no - 1];
            if (ps.body == body) invis = ps.invis;      // 地址不同 = 换了对象，从 0 重新数
        }
        if (body) {
            uint32_t v70 = 0;
            if (vm_readv(body + 0x70, &v70, 4)) {
                if (v70 == 0) invis++;
                else          invis = 0;
            }
            // 读失败：什么都不做。读不到和"确实是 0"必须分开(SKILL.md 4.6)
        }

        if (body && !is_dead) add_unique(res.body, res.body_count, MAX_BODIES, body);
        if (res.copycat_count >= MAX_COPYCAT) continue;

        CopycatEntry &e = res.copycat[res.copycat_count++];
        e.body = body;
        e.soul = soul_obj;
        e.identity = 0;
        e.seat = seat_no;
        e.invis = invis;
        e.dead = is_dead;
        int64_t v = 0;
        g_cc_i_ident = resolve_index(d, dk, g_cc_i_ident, "identity_id");
        const bool ident_read_ok =
            (g_cc_i_ident >= 0 && read_int(getPtr64(value_slot(dk, g_cc_i_ident)), v));
        if (ident_read_ok && v > 100 && v < 400)
            e.identity = (int)v;

        // ★★ 诊断（2026-10-14）：用户报"局内重开时识别得出模仿者局，但就是不画身份" ★★
        //   识别与绘制用的是**两套判据**，这正是问题能"只坏一半"的原因：
        //     · 识别(g_is_copycat)：看 units_by_type[1088] 里有没有人 -> 与身份值无关 ✓
        //     · 绘制(copycat_identity)：要求 identity != 0，即上面那个 (100,400) 闸门 ✓
        //   所以只要 identity_id 的真实取值跑到区间外（游戏更新改了编码/加偏移/换类型），
        //   就会**静默丢弃** -> 簿子里 identity 恒为 0 -> 识别正常但一个人都不标身份 ✗
        //   这里把原始读数打出来，一眼区分三种情况：
        //     读到=-1          -> 属性链没读到（页被换出/偏移变了）
        //     读到了但超范围    -> 编码变了，需要按实测值改上面的区间
        //     判定=有效        -> 闸门没问题，得往绘制侧查
        //   ⚠ 限频 60 条：本函数每次 refresh 都会跑
        {
            static int s_n = 0;
            if (s_n < 60 && e.seat >= 1) {
                ++s_n;
                printf("[身份诊断] seat=%d body=0x%llx idx=%lld 原始值=%lld -> %s\n",
                       e.seat, (unsigned long long)e.body, (long long)g_cc_i_ident,
                       (long long)(ident_read_ok ? v : -1),
                       e.identity ? "有效"
                                  : (ident_read_ok ? "读到了但超出(100,400)被丢弃" : "读取失败"));
                fflush(stdout);
            }
        }
    }
}

// ---- 身份簿(锁存)：模仿者开会阶段游戏负载低，Python 堆页会被换进 zram，身份/模型全读不到 ----
// ---- 身份簿的读写(类型与全局定义见文件上方，collect_copycat 之前) ----

static void book_publish()
{
    int w = 1 - g_book_active;
    g_book[w] = g_book_work;
    g_book_active = w;
}

static bool book_empty(const CopycatBook &b)
{
    for (int i = 0; i < MAX_COPYCAT; i++) if (b.seat[i].known) return false;
    return true;
}

// 前向声明：book_merge 要用它当"新锚点是否真在渲染"的闸门，而它定义在文件后面
static bool scene_is_rendered(uint64_t scene);

static void book_merge(const Snapshot &res, uint64_t um)
{
    if (!g_book_enabled) {
        if (!book_empty(g_book_work)) { memset(&g_book_work, 0, sizeof(g_book_work)); book_publish(); }
        g_book_um = 0;
        g_cc_seat_max = 0;      // 簿子清空 -> 座位范围也跟着重新学(见 g_cc_seat_max 的注释)
        return;
    }
    bool changed = false;
    bool death_latched = false;
    if (is_obj(um) && um != g_book_um) {         // um 读不到(0)时什么都不动
        // ★ 第一次拿到 um：如果盘上那本簿子记的就是它，说明还是同一局(overlay 重启过)，
        //   直接认领 —— 这才是"重启不丢死者"的关键一步。不是同一局就按老规矩清空。
        if (g_book_pending_um != 0 && um == g_book_pending_um) {
            g_book_um = um;
            g_book_pending_um = 0;
            printf("[身份簿] 与磁盘记录同局(um=0x%llX)，沿用锁存的身份/死者\n",
                   (unsigned long long)um);
            fflush(stdout);
        } else {
            if (g_book_pending_um != 0) {
                printf("[身份簿] 盘上那本属于别的局(0x%llX != 0x%llX)，作废\n",
                       (unsigned long long)g_book_pending_um, (unsigned long long)um);
                fflush(stdout);
            }
            g_book_pending_um = 0;
            if (!book_empty(g_book_work)) changed = true;
            memset(&g_book_work, 0, sizeof(g_book_work));
            g_book_um = um;
        }
    }
    for (int i = 0; i < res.copycat_count; i++) {
        const CopycatEntry &e = res.copycat[i];
        if (e.seat < 1 || e.seat > MAX_COPYCAT) continue;       // 号码读不到就不知道记到哪一格
        CopycatSeat &b = g_book_work.seat[e.seat - 1];
        if (!b.known) { b.known = true; changed = true; }
        // ★ 锚点更新：**把旧值存进 body_old**，而不是丢弃它。
        //
        //   2026-10-10 逐帧实测(假面前后)：
        //     02:04:57  绘制=10  锚点 A58550 A598B0 A5AC10 …   全 Y   ← 假面前
        //     02:04:58  绘制=1   锚点 D8EBA80 D8DAB40 A8ED30 … 全 N   ← 假面开始
        //     02:05:00  绘制=9   锚点 A58550 A598B0 A5AC10 …   全 Y   ← 假面结束，地址变回来
        //   假面把 unit.model 换成一批**没被渲染**的新对象，而那批**旧对象仍然在渲染**。
        //   原来这里无条件覆盖 body，旧地址当场丢失 —— 12 个座位全被过滤，绘制掉到 1
        //   (当时那个 1 就是唯一没被换锚点的 8 号)。
        //   现在旧值留在 body_old 里，识别时两个都比 —— 假面期间旧锚点照样命中。
        //   (先试过用 scene_is_rendered 当闸门拦更新，**无效**：假面那批新对象的 +0x70
        //    也是非 0，闸门放行了。所以不再判断该不该更新，改成两个都留。)
        if (e.body && e.body != b.body) {
            if (b.body) b.body_old = b.body;
            b.body = e.body;
            changed = true;
        }
        // 灵魂体地址：新读到的覆盖旧的(重生/换局后 [1089] 会换对象)，读不到(0)时保留旧值
        if (e.soul && e.soul != b.soul) { b.soul = e.soul; changed = true; }
        if (e.identity && e.identity != b.identity) { b.identity = e.identity; changed = true; }
        // 不可见轮数：每轮都在动，**不**用它触发发布(否则 60fps 下白抄一整个簿子)
        if (e.invis != b.invis) b.invis = e.invis;
        if (e.dead && !b.dead) { b.dead = true; changed = true; death_latched = true; }
    }
    if (changed) book_publish();

    // 落盘。死亡一发生就**立刻**存 —— [1089] 只在死亡那一瞬间存在，这是唯一一次采集机会，
    // 而 overlay 一重启内存里的簿子就没了(用户报的"死人画成活人"就是这么来的)。
    //
    // ⚠ 这里曾经写成"changed 时才 ++tick"，结果**一次都不会存**：身份在第一轮就全读到了，
    //   之后 changed 恒为 false，tick 永远停在 1。所以 tick 必须每轮都涨，
    //   并且再补一条"每 10 秒无条件存一次"的兜底 —— 保证这一局读到过的东西最终一定在盘上。
    ++g_book_save_tick;
    if (death_latched || g_book_save_tick >= 600 ||
        (changed && g_book_save_tick >= 120)) {
        g_book_save_tick = 0;
        book_save();
    }
}

static void book_mark_self(uint64_t anchor)
{
    if (!g_book_enabled || anchor == 0) return;
    for (int i = 0; i < MAX_COPYCAT; i++) {
        if (!g_book_work.seat[i].known) continue;
        // 当前锚点或上一个锚点都对得上就算自己 —— 假面换锚点时"自己"也要认得出
        if (g_book_work.seat[i].body != anchor && g_book_work.seat[i].body_old != anchor) continue;
        if (g_book_work.self_seat != i + 1) { g_book_work.self_seat = i + 1; book_publish(); }
        return;
    }
}

static bool try_refresh()
{
    if (g_module_dict == 0 && !resolve_module()) { g_in_lobby = false; return false; }

    Snapshot res;
    memset(&res, 0, sizeof(res));

    // ---- 零、units_by_type + 局外判定(必须在锚点之前，见 g_in_lobby) ----
    g_i_unit_mgr = find_key(g_module_dict, "unit_mgr", g_i_unit_mgr);
    uint64_t um = (g_i_unit_mgr >= 0) ? get_attr(g_module_dict, g_i_unit_mgr) : 0;
    uint64_t ud = get_inst_dict(um);
    uint64_t ubt = 0;
    if (ud) {
        g_i_ubt = find_key(ud, "units_by_type", g_i_ubt);
        ubt = (g_i_ubt >= 0) ? get_attr(ud, g_i_ubt) : 0;
    }
    bool in_lobby = false;
    g_in_lobby = is_obj(ubt) && check_lobby(ubt, in_lobby) && in_lobby;

    // 模仿者模式放在锚点之前：锚点失败时身份簿照样要更新(开会阶段锚点常读不到)
    if (is_obj(ubt)) collect_copycat(ubt, res);
    book_merge(res, um);

    // ---- 一、当前操控单位：g_cam_ctrl.unit ----
    g_i_cam = find_key(g_module_dict, "g_cam_ctrl", g_i_cam);
    uint64_t cam = (g_i_cam >= 0) ? get_attr(g_module_dict, g_i_cam) : 0;
    if (!is_obj(cam)) { snprintf(g_status, sizeof(g_status), "g_cam_ctrl 无效(未在对局中?)"); return false; }

    uint64_t camd = get_inst_dict(cam);
    if (!camd) { snprintf(g_status, sizeof(g_status), "g_cam_ctrl 不是 managed-dict 布局"); return false; }
    Dict camk;
    if (!read_dict(camd, camk)) { snprintf(g_status, sizeof(g_status), "g_cam_ctrl 字典异常"); return false; }

    g_i_cam_unit = resolve_index(camd, camk, g_i_cam_unit, "unit");
    if (g_i_cam_unit < 0) { snprintf(g_status, sizeof(g_status), "g_cam_ctrl 里没有 unit"); return false; }
    uint64_t me = getPtr64(value_slot(camk, g_i_cam_unit));
    if (!is_obj(me)) { snprintf(g_status, sizeof(g_status), "cam.unit 为空"); return false; }

    uint64_t me_d = get_inst_dict(me);
    if (!me_d) { snprintf(g_status, sizeof(g_status), "cam.unit 没有实例字典"); return false; }
    Dict me_k;
    if (!read_dict(me_d, me_k)) { snprintf(g_status, sizeof(g_status), "cam.unit 字典异常"); return false; }

    g_i_model = resolve_index(me_d, me_k, g_i_model, "model");
    g_i_utype = resolve_index(me_d, me_k, g_i_utype, "unit_type");
    g_i_uid   = resolve_index(me_d, me_k, g_i_uid,   "uid");

    if (g_i_model >= 0) res.anchor = get_scene_obj(getPtr64(value_slot(me_k, g_i_model)));
    if (g_i_utype >= 0) { int64_t v = 0; if (read_int(getPtr64(value_slot(me_k, g_i_utype)), v)) res.camp = (int)v; }
    if (g_i_uid   >= 0) { int64_t v = 0; if (read_int(getPtr64(value_slot(me_k, g_i_uid)),   v)) res.uid  = v; }

    // ★ GoldRush 模式：cam.unit 没有 .model 属性（GoldRushPlayerUnit 结构不同），
//   但 .uid 能读到（1000001~1000004）。只要 uid 有值就认为成功。
if (res.anchor == 0 && res.uid == 0) {
    snprintf(g_status, sizeof(g_status), "cam.unit.model/uid 都读不到");
    return false;
}

    // ---- 二、废弃模型黑名单 + 本体集合 + 监管本体 ----
    if (is_obj(ubt))
        for (int i = 0; i < UNIT_TYPE_COUNT; i++) collect_units(ubt, UNIT_TYPES[i], res);
    book_mark_self(res.anchor);
    // 这一段读不到不算失败：锚点已经拿到了。本体数为 0 时绘制侧会退回按类名画
    
    // 顺便读自身 state_machine 的 _state 列表
int64_t i_sm = resolve_index(me_d, me_k, -1, "state_machine");
if (i_sm >= 0) {
    uint64_t sm_obj = getPtr64(value_slot(me_k, i_sm));
    if (is_obj(sm_obj)) {
        uint64_t sm_d = get_inst_dict(sm_obj);
        if (sm_d) {
            Dict sm_k;
            if (read_dict(sm_d, sm_k)) {
                int64_t i_state = resolve_index(sm_d, sm_k, -1, "_state");
                if (i_state >= 0) {
                    uint64_t lst = getPtr64(value_slot(sm_k, i_state));
                    int64_t n = 0; uint64_t items = 0;
                    if (read_list(lst, n, items)) {
                        int best = 0;
                        for (int64_t k = 0; k < n && k < 8; k++) {
                            int64_t v = 0;
                            if (read_int(getPtr64(items + k*8), v)) {
                                // 1=站立 8=战斗姿态 52/53=跳/落 都不算技能
                                if (v > 10 && v != 52 && v != 53) best = (int)v;
                            }
                        }
                        res.self_skill_state = best;
                    }
                }
            }
        }
    }
}

    int write_idx = 1 - g_active;
    g_buf[write_idx] = res;
    g_active = write_idx;                                   // 填完再翻转
    snprintf(g_status, sizeof(g_status), "正常 %s uid=%lld 本体%d 废弃%d 身份%d",
             res.camp == 1 ? "监管" : (res.camp == 2 ? "求生" : (res.camp == COPYCAT_PLAYER_TYPE ? "模仿者局" : "从属")),
             (long long)res.uid, res.body_count, res.stale_count, res.copycat_count);
    return true;
}

static void refresh_once()
{
    if (g_libbase == 0) return;
    // ★★ 2026-10-14：否定缓存的清空策略 ★★
    //   否定缓存记的是"上次扫完确实没有这个键"，本来设计成**跨轮复用**
    //   （作者原话：同一个不存在的名字**每帧**都要把 700 条重新比一遍，永远重复 ✗）。
    //   唯一的风险是"字典内容被游戏改写、新增了键"——那时旧记忆会漏掉新键 ✗
    //   所以这里取折中：**既不清得太勤，也不永不清理** ——
    //     每 30 轮（@33ms 约 1 秒）整体清一次，
    //     跨轮复用的收益基本全拿到，内容变化最多只影响 1 秒 ✓
    //   （详见 PyCore.h 的 neg_reset / neg_hit 说明）
    {
        static int s_neg_tick = 0;
        if (++s_neg_tick >= 30) { s_neg_tick = 0; PyCore::neg_reset(); }
    }
    if (!PyRoot::ensure()) {
        g_available = false;
        snprintf(g_status, sizeof(g_status), "%s", PyRoot::status_text());
        return;
    }
    if (try_refresh()) {
        g_available = true;
        g_fail_streak = 0;
        return;
    }
    // ★ 关键改动：单次失败不清 g_available，沿用上一次的成功快照
    // 连续 8 次(约 128ms)失败才认为真的失效
    if (++g_fail_streak >= 8) {
        g_available = false;
        // 清缓存（原代码）
        g_module_dict = 0;
        g_i_cam = g_i_cam_unit = g_i_unit_mgr = g_i_ubt = -1;
        g_i_model = g_i_another = g_i_utype = g_i_uid = -1;
        g_i_unit_model = g_i_cpuppet = -1;
        g_cc_i_model = g_cc_i_another = g_cc_i_ident = g_cc_i_index = g_cc_i_uq = g_ccg_i_uq = -1;
        g_cc_i_isdead = g_cc_i_isdead2 = -1;
        g_cc_i_cu = g_cc_i_dbu = g_cc_i_cud = -1;
        g_cc_i_pos = -1;
        g_class_cache_count = 0;
        g_fail_streak = 0;
    }
    // 否则：保持 g_available = true，让调用方继续读上一帧的快照
}

// 自身锚点每帧都要用，而且切换操控对象时要立刻跟上，所以比天赋刷得勤(约一帧 60fps)
//
// ★★ 2026-10-14 CPU 优化：16ms(62.5Hz) -> 33ms(30Hz) ★★
//   adb `top -H` 实测（设备 8 核，本线程伪装名 kworker/u8:11）：
//     本线程**单个就占 29.0~29.6% 单核**，是整进程(约 70%)里最大的一项 ✗
//     累计 utime=3862 / stime=20232 —— **84% 的时间在内核态**，
//     也就是每次刷新的开销几乎全花在驱动 ioctl 读内存上（每次读约 3µs）。
//   16ms 时每次刷新约 4.7ms CPU（29.3% ÷ 62.5Hz ≈ 4.7ms），33ms 直接砍掉一半 ✓
//   代价：切换操控对象时锚点最多晚 33ms 跟上 ——
//     相对于游戏自身几十毫秒的动画/状态尺度可以忽略。
//   要更跟手可以改 24；要更省可以改 50（本值是收益/响应性的折中）。
static const int REFRESH_INTERVAL_MS = 33;

// ★★ 性能：每 N 轮才做一次真正的 refresh —— **实测结论：这条路走不通，已回退为 1**。
//
//   2026-10-10 想省掉最贵的内核态读取(见下)，把整轮刷新降到 1/3。实测**反而更糟**：
//     REFRESH_SKIP=3 -> top 98%(稳定后)，utime=1935 stime=5704 —— 几乎跑满一个核
//     REFRESH_SKIP=1 -> top 62%，utime/stime 明显更低
//   原因：refresh_once() 不只读数据，它还负责维持 g_available / g_buf 这些**状态**。
//   少跑几轮会让状态变陈旧，绘制侧的兜底路径(相机深度启发式、逐实体补读)被反复触发，
//   读得比省下来的还多 —— 典型的"优化错了地方"。
//   所以保持 1。要再动性能，得先量清楚**每一处读取各占多少**，而不是拍脑袋降频。
static const int REFRESH_SKIP = 1;      // 1 = 每轮都刷（与改动前一致）

// ★★ 性能剖析：**按 CPU 时间**统计（2026-10-10 修正）
//
//   上一版用墙钟(CLOCK_MONOTONIC)包住 refresh_once()，算出"占单核 110%"这种不可能
//   的数 —— 线程会被抢占，墙钟把等待时间也算进去了。这个错数让我误判 PySelf 是整机
//   CPU 的大头；实际把它优化到耗时减半(墙钟 17.6ms -> 8.5ms)后，**整机 CPU 纹丝不动**
//   (局内 69% -> 55~70%)。
//   所以改用 CLOCK_THREAD_CPUTIME_ID —— 只算真正在 CPU 上跑的时间，才可跨组件比较。
static std::atomic<uint64_t> g_t_pyself_ns{0};
static std::atomic<uint64_t> g_n_pyself{0};
static std::atomic<uint64_t> g_t_frame_ns{0};      // 绘制一帧真正吃的 CPU
static std::atomic<uint64_t> g_n_frame{0};
static std::atomic<uint64_t> g_t_pair_ns{0};       // 假面预扫那一小段

static inline uint64_t now_ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// 本线程真正消耗的 CPU 时间（不含被抢占/等待）—— 跨组件比较必须用它
static inline uint64_t now_cpu_ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void thread_main()
{
    DisguiseThreadName(11);   // PySelf
    int tick = 0;
    while (true) {
        // 见 REFRESH_SKIP 的注释：整轮刷新降到 1/N，省掉的是最贵的内核态读取。
        if (++tick >= REFRESH_SKIP) {
            tick = 0;
            const uint64_t t0 = now_cpu_ns();
            refresh_once();
            g_t_pyself_ns.fetch_add(now_cpu_ns() - t0, std::memory_order_relaxed);
            g_n_pyself.fetch_add(1, std::memory_order_relaxed);
            // ★ 性能剖析写日志 —— 为什么不走 draw_stats：
            //   实测 draw_stats 在很多状态下返回空(不在局内、或绘制循环没跑)，
            //   取不到数就没法判断该优化哪里。而 stdout 一定会落到
            //   /data/local/tmp/hack_stdout.log，任何时候都能读。
            //   每 300 轮(~5 秒)打一行：PySelf 线程平均每次刷新的耗时 + 字典扫描次数。
            static uint64_t last_report = 0;
            const uint64_t n = g_n_pyself.load(std::memory_order_relaxed);
            if (n - last_report >= 300) {
                const uint64_t ns = g_t_pyself_ns.load(std::memory_order_relaxed);
                const uint64_t fc = PyCore::g_dbg_find_calls.load(std::memory_order_relaxed);
                const uint64_t fh = PyCore::g_dbg_find_hint.load(std::memory_order_relaxed);
                const uint64_t ff = PyCore::g_dbg_find_fullscan.load(std::memory_order_relaxed);
                const uint64_t fs = PyCore::g_dbg_find_slow.load(std::memory_order_relaxed);
                const uint64_t fn = PyCore::g_dbg_find_neg.load(std::memory_order_relaxed);
                const uint64_t fn2 = g_t_frame_ns.load(std::memory_order_relaxed);
                const uint64_t nn2 = g_n_frame.load(std::memory_order_relaxed);
                // 每秒的 CPU 时间 = 单次 CPU 时间 × 每秒次数；两者相加就是整机单核占用
                // ★ 定期把空闲堆归还系统（2026-10-10 内存优化）
                //   实测 RSS 82MB 里 **54MB 是 [anon:scudo:primary]** —— Android 的
                //   Scudo 分配器堆。Scudo 的 primary heap 释放后**不会主动还给系统**，
                //   所以峰值多少就常驻多少。这个程序在轮询循环里会反复分配临时对象
                //   (std::string / unordered_map 节点 / vector 增长)，
                //   峰值上去之后就一直占着。
                //   malloc_trim(0) 让分配器把空闲页真正归还 —— 代价是它会遍历一次堆，
                //   所以只在**每 30 秒**做一次（按 16ms 刷新算 ≈ 1875 次），
                //   摊到每帧上完全看不见。
                {
                    static int s_trim_tick = 0;
                    //   ⚠ 用的是 mallopt(M_PURGE, 0) 而不是 glibc 的 malloc_trim ——
                    //     **bionic(Android) 没有 malloc_trim**（那是 glibc 的），
                    //     Android 上让分配器归还内存的是 mallopt：M_PURGE 立即清空缓存。
                    if (++s_trim_tick >= 1875) {
                        s_trim_tick = 0;
#ifdef M_PURGE
                        mallopt(M_PURGE, 0);
#endif
                    }
                }
                const double py_per_s = ns / 1e9 / (n ? n : 1) * (1000.0 / REFRESH_INTERVAL_MS) * REFRESH_SKIP;
                const double fr_per_s = nn2 ? (fn2 / 1e9 / nn2 * 60.0) : 0.0;
                printf("[剖析] PySelf: %llu 次, CPU %.2f ms/次 -> 占单核 %.1f%%\n",
                       (unsigned long long)n, ns / 1e6 / (n ? n : 1), py_per_s * 100.0);
                printf("[剖析] 绘制:   %llu 帧, CPU %.2f ms/帧 -> 占单核 %.1f%%   (两者之和≈整机)\n",
                       (unsigned long long)nn2, nn2 ? fn2 / 1e6 / nn2 : 0.0, fr_per_s * 100.0);
                printf("[剖析] 字典: 调用 %llu (hint命中 %llu / 全扫 %llu / 逐条 %llu / 否定缓存 %llu)  平均每次刷新 调用 %.1f 全扫 %.1f\n",
                       (unsigned long long)fc, (unsigned long long)fh,
                       (unsigned long long)ff, (unsigned long long)fs, (unsigned long long)fn,
                       (double)fc / (n ? n : 1), (double)ff / (n ? n : 1));
                fflush(stdout);
                last_report = n;
            }
        }
        usleep(REFRESH_INTERVAL_MS * 1000);
    }
}

static void start(uintptr_t libbase)
{
    if (g_libbase != 0) return;
    g_libbase   = libbase;
    snprintf(g_status, sizeof(g_status), "启动中");
    book_load();                 // ★ 先把上一进程锁存的身份/死者捡回来(见 kBookPath 的注释)
    std::thread(thread_main).detach();
    printf("[自身] 已启动 libbase=0x%lx\n", (unsigned long)libbase);
    fflush(stdout);
}

// ---------------- 给绘制侧用的只读接口 ----------------

static inline bool available() { return g_available; }
static inline const char *status_text() { return g_status; }


// 当前操控单位的场景对象。返回 false 时绘制侧要退回相机深度启发式
static bool anchor(uint64_t &obj)
{
    if (!g_available) return false;
    uint64_t v = g_buf[g_active].anchor;
    if (v == 0) return false;
    obj = v;
    return true;
}

// 当前视角的阵营：1=监管 2=求生者。0=没读到。
// 注意操控从属单位时这里是从属的 unit_type(机械玩偶仍是 2、梦之信徒是 236)
static inline int camp() { return g_available ? g_buf[g_active].camp : 0; }
static inline int64_t self_uid() { return g_available ? g_buf[g_active].uid : 0; }

// 形态切换留下的废弃模型 —— 替掉红蝶/木偶师类名黑名单
static bool is_stale_model(uintptr_t obj)
{
    if (!g_available) return false;
    const Snapshot &s = g_buf[g_active];
    for (int i = 0; i < s.stale_count; i++) if (s.stale[i] == (uint64_t)obj) return true;
    return false;
}

// 大厅/准备阶段：Python 侧还没有任何玩家单位，场景里的 chr/player、chr/boss 对象(时装挂件、头饰、袖子)
// 全都没有宿主，绘制侧据此整类不画。读不全时是 false，照旧按类名画
static inline bool in_lobby() { return g_in_lobby; }

// 本体集合能不能用。准备阶段/大厅里 units_by_type 没有 [1]/[2](实测两次)，这里就是 false，
// 绘制侧据此退回按类名画(读取失败时同样是 false，所以"是否局外"要看 in_lobby()，别用它)
static inline bool body_set_ready() { return g_available && g_buf[g_active].body_count > 0; }

static bool is_body(uintptr_t obj)
{
    if (!g_available) return false;
    const Snapshot &s = g_buf[g_active];
    for (int i = 0; i < s.body_count; i++) if (s.body[i] == (uint64_t)obj) return true;
    return false;
}

// 局内监管本体的场景对象(跳过巡视者)。准备阶段没有 [1] 单位，返回 false
static bool hunter_body(uint64_t &obj)
{
    if (!g_available) return false;
    uint64_t v = g_buf[g_active].hunter_body;
    if (v == 0) return false;
    obj = v;
    return true;
}

// ---- 模仿者模式 ----
// 当前是不是模仿者对局(1088 里读到了活人)
static inline bool copycat_active() { return g_available && g_buf[g_active].copycat_count > 0; }

// 界面开关「模仿者模式」每帧同步过来：开着才记身份簿，关掉就清空
static inline void set_copycat_book(bool on) { g_book_enabled = on; }

// 身份簿当前内容(拷贝一份给界面用，16 格很小)
static inline CopycatBook copycat_book() { return g_book[g_book_active]; }

// ---- 假面(紧急任务)：「扮演者N」替身 ----------------------------------------
// 2026-10-10 实测：假面一开，引擎给**每个玩家额外生成一个**名字叫「扮演者N」的对象
// (type 1213 CopycatSceneUnit，一人一个)，本人和替身同时存在。于是绘制侧两难：
//   · 不过滤 -> 一个人画两个框(12 人画出 23 个框，用户看到的就是这个)
//   · 过滤掉替身 -> 替身和本人的模型都不在本体集合里，变成"一个人都不画"
// 关键突破口：**号码就写在替身的名字里**(扮演者1..扮演者12)，所以替身可以直接
// 对回座位，再拿身份簿里的身份 —— 这样替身就能"顶替"本人正常绘制，一人一个框。
//
// 返回 1..MAX_COPYCAT；名字里没有「扮演者」或号码读不出时返回 0。
static int copycat_masq_seat(const char *name)
{
    if (!name || !*name) return 0;
    const char *p = strstr(name, "扮演者");
    if (!p) return 0;
    p += 6;                                   // "扮演者" 是 3 个汉字 = 6 字节(UTF-8)
    while (*p && (*p < '0' || *p > '9')) p++; // 名字里可能有空格/其它修饰
    if (!*p) return 0;
    int v = 0, n = 0;
    while (*p >= '0' && *p <= '9' && n < 3) { v = v * 10 + (*p - '0'); p++; n++; }
    return (v >= 1 && v <= MAX_COPYCAT) ? v : 0;
}

// 查身份簿里某一号的身份与死活。返回 false = 簿子里这一号还没有记录
// (那时调用方应当放弃"顶替"，退回原来的判断，不要凭空造一个身份)。
static bool copycat_seat_info(int seat, int &ident, bool &dead)
{
    if (!cc_seat_valid(seat)) return false;
    const CopycatSeat &s = g_book[g_book_active].seat[seat - 1];
    if (!s.known) return false;
    ident = s.identity;
    dead  = s.dead;
    return true;
}

// ---- 按世界坐标把渲染实体认回座位 -------------------------------------------
// 2026-10-10 假面现场实测(这是"假面下不画人"的正解)：
//   假面把每个玩家的**渲染模型换成了别的角色**(实体列表里出现 [入殓师]/[作曲家]/
//   [园丁]/[小女孩]/[先知]/[幸运儿]/[斗牛士]/[画家] 八个不同角色名)，于是
//     · 身份簿锁存的 body 地址(unit.model) —— 对不上
//     · 本体集合(body_set，也是拿 unit.model 建的) —— 对不上
//   44 个实体全被"簿子没有且非本体"丢掉，实际绘制掉到 1。
//
//   但同一时刻逐座位比对发现：**unit.position 和某个渲染实体的世界坐标完全重合**
//     (5号 演说家 position=(265.6,0.7,-210.0) 与 [园丁] d=0.00
//      4号 千面人 position=(240.8,0.7,-282.0) 与 [先知] d=0.00)
//   也就是说：**对象地址会变、模型会换，但位置不会骗人**。所以改用坐标认领座位 ——
//   这条判据与模型无关，能同时治好假面以及以后任何"换模型"的场景。
//
// 每帧从 unit.position 刷新；**不落盘**(它一直在动，没有锁存价值)。
// (数组本身声明在文件上方 collect_copycat 之前，这里只是补充说明。)

// 给 dump 用：取某一号当前的认领坐标。返回 false = 这一号现在没有可用坐标(会被坐标认领跳过)。
// (放在这里是因为它要读 g_seat_pos —— 那组数组声明在文件上方、collect_copycat 之前，
//  但早期那批 g_cc_i_* 静态量在更前面，在那里引用会报未声明。)
static bool copycat_seat_pos(int seat, float out[3])
{
    if (!cc_seat_valid(seat) || !g_seat_pos_ok[seat - 1]) return false;
    memcpy(out, g_seat_pos[seat - 1], sizeof(float) * 3);
    return true;
}

// 身份簿里"哪一号是我"(从 1 开始，0 = 还不知道)。用来把**自己**从绘制里排除。
// 原来靠 self_obj / self_anchor / g_self_scene 三条判据跳过自己，但那三条在模仿者
// 对局里经常一条都不命中，于是自己的框被画了出来 —— 而画在自己身上的框永远在屏幕
// 中心、走动时"不动、只上下动"，用户会以为跟踪坏了。按号跳过是确定性的。
static int copycat_self_seat()
{
    return g_book[g_book_active].self_seat;
}

// 取某一号的**当前锚点**(unit.model 解析出的场景对象)。给绘制侧判断"这一号的锚点还在不在
// 本帧实体列表里"用 —— 正常对局它就在(所以一个都不缺)，假面把它整批换掉(缺 10 个以上)。
// 绘制侧**只在这个数够大时才启用假面专用逻辑**，正常对局完全不受影响。
static uint64_t copycat_seat_body(int seat)
{
    if (!cc_seat_valid(seat)) return 0;
    const CopycatSeat &s = g_book[g_book_active].seat[seat - 1];
    return s.known ? s.body : 0;
}

// 取某一号的**持久跟踪**坐标 —— 只在读到可信值时前进，读不到时保留上一次的值。
// 返回 false = 这一号从来没读到过可信坐标。假面配对用这个，而不是当帧原始值。
static bool copycat_seat_track(int seat, float out[3])
{
    if (!cc_seat_valid(seat) || !g_seat_track_ok[seat - 1]) return false;
    memcpy(out, g_seat_track[seat - 1], sizeof(float) * 3);
    return true;
}

// 找一个离 (x,y,z) 最近的、容差 tol 米以内的座位。返回 1..MAX_COPYCAT，没有返回 0。
static int copycat_seat_near(float x, float y, float z, float tol)
{
    // 实体坐标没读到的(0,0,0)不参与 —— 否则会和同样没初始化的座位凑出 d=0 的假命中
    if (x == 0.f && y == 0.f && z == 0.f) return 0;
    int best = 0;
    float bd = tol * tol;
    for (int i = 0; i < MAX_COPYCAT; i++) {
        if (!cc_seat_valid(i + 1) || !g_seat_pos_ok[i]) continue;
        const float dx = x - g_seat_pos[i][0];
        const float dy = y - g_seat_pos[i][1];
        const float dz = z - g_seat_pos[i][2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 <= bd) { bd = d2; best = i + 1; }
    }
    return best;
}

// 簿子里有没有东西 —— 有就说明是模仿者局，绘制侧改用簿子过滤(开会时本体集合读不全)
static bool copycat_book_ready()
{
    const CopycatBook &b = g_book[g_book_active];
    for (int i = 0; i < MAX_COPYCAT; i++) if (b.seat[i].known && b.seat[i].body) return true;
    return false;
}

// 这个场景对象是不是死者灵魂体(= [1089] 里某一项的 model)。
//   绘制侧用它决定"要不要放行身份簿过滤"，比 IsGhostEntity 精确得多 ——
//   后者看的是渲染可见性，会把分身部件/LOD/远处剔除对象一起算进来。
static bool is_soul(uintptr_t obj)
{
    if (!g_available || obj == 0) return false;
    const Snapshot &r = g_buf[g_active];
    for (int i = 0; i < r.soul_count; i++) if (r.soul[i] == (uint64_t)obj) return true;
    return false;
}

// ★★ 渲染实体 -> 座位 model 的**确定性**链接 —— 假面问题的正解，也是唯一能保证
//    "不误判"的判据(坐标做不到，原理上就做不到)。
//
//   2026-10-10 深度 2 走链实测(在假面期间也是一样成立)：
//        getPtr64(getPtr64(entity + 0x28) + 0x58) == 该座位 unit.model 的地址
//      例：[小女孩]->3号  [先知]->5号  [默剧艺人]->12号  [骑士]->2号
//          [气象学家]->8号  [杂技演员]->6号  [机械师]->9号/1号
//
//   为什么这条是"确定性"而不是"猜"：它是引擎自己的归属关系（这个渲染对象属于哪个 unit），
//   是**离散**的 —— 不像坐标那样两个号贴近时就不可分辨。所以哪怕 11 号和 12 号
//   贴着一起跑，各自的渲染对象仍然分别指着各自的 model，永远不会互相认错。
//
//   注意 +0x28 就是绘制侧已经在用的 objcoor(data[i].objcoor = getPtr64(obj+0x28))，
//   所以这里不需要额外读一次。
#define COPYCAT_OBJCOOR_OFF  0x28
#define COPYCAT_OWNERMODEL_OFF 0x58

// 取一个渲染实体所属座位的 model 地址。0 = 读不到(调用方必须当作"不知道"，不能猜)。
static uint64_t copycat_owner_model(uintptr_t obj)
{
    if (obj == 0) return 0;
    const uint64_t coor = getPtr64(obj + COPYCAT_OBJCOOR_OFF) & 0x00FFFFFFFFFFFFULL;
    if (!coor) return 0;
    return getPtr64(coor + COPYCAT_OWNERMODEL_OFF) & 0x00FFFFFFFFFFFFULL;
}

// 场景对象 -> 身份号(identity_id) 和 游戏里的号码。查身份簿，所以读不到的轮次沿用上次的值。
// 不是簿子里的活人(包括死者、假面舞会的石像等不认识的模型)返回 false
//
// 判据按"多可靠"排序，命中即返回：
//   ① obj 等于某一号的**当前** model —— 正常情况。
//   ② obj 等于某一号的 **body_old**(上一个 model) —— **假面的正解**。
//      假面把 unit.model 换成一批没被渲染的新对象，而那批**旧对象仍在渲染**，
//      所以旧地址照样认得出本人(逐帧实测见 CopycatSeat 里 body_old 的注释)。
//   ③ getPtr64(getPtr64(obj+0x28)+0x58) 等于某一号的 model —— 在有些窗口能命中，
//      但不是普遍成立(实测有的窗口 10/10、有的只有 2/45)，所以只当补充，不指望它。
//   ④ (已废弃)曾用 obj+0x510 冒充"回指"：实测那是个**自指针**，命中的对象本来就
//      是簿子里的本体，属于同义反复，已删掉。坐标认领也不可靠：假面期间
//      unit.position 滞后，只有 1/12 能落在 1.2 米内，其余差 2~133 米。
// via_old：命中 ②(上一个锚点 body_old)时置 true。调用方据此知道"这个框的锚点对象
//   已经不被引擎驱动了"，于是把**位置**改从 unit.position 取(那个是活的)。
//   2026-10-10 实测：假面期间 12 个锚点全 N(旧对象冻在触发那一刻)，而玩家走动时
//   unit.position 持续变化 —— 滞后的从来是锚点对象，不是 unit.position。
static bool copycat_identity(uintptr_t obj, int &identity, int &seat, bool *via_old = nullptr)
{
    if (via_old) *via_old = false;
    if (obj == 0) return false;
    const CopycatBook &bk = g_book[g_book_active];
    const uint64_t o = (uint64_t)obj;
    // ① 当前锚点 —— 正常对局永远走这条
    for (int i = 0; i < MAX_COPYCAT; i++) {
        if (!cc_seat_valid(i + 1)) continue;              // 本局没有这个号(残留的 13~16)
        const CopycatSeat &s = bk.seat[i];
        if (!s.known || s.dead) continue;
        if (s.body != o) continue;
        identity = s.identity;
        seat = i + 1;
        return true;
    }
    // ② 上一个锚点 —— 假面换锚点时走这条，位置要改用 unit.position
    for (int i = 0; i < MAX_COPYCAT; i++) {
        if (!cc_seat_valid(i + 1)) continue;              // 本局没有这个号(残留的 13~16)
        const CopycatSeat &s = bk.seat[i];
        if (!s.known || s.dead) continue;
        if (s.body_old != o) continue;
        identity = s.identity;
        seat = i + 1;
        if (via_old) *via_old = true;
        return true;
    }
    // ③ 父子链(有就白捡，没有不影响)
    const uint64_t owner = copycat_owner_model(obj);
    if (!owner) return false;
    for (int i = 0; i < MAX_COPYCAT; i++) {
        if (!cc_seat_valid(i + 1)) continue;              // 本局没有这个号(残留的 13~16)
        const CopycatSeat &s = bk.seat[i];
        if (!s.known || s.dead) continue;
        if (s.body != owner && s.body_old != owner) continue;
        identity = s.identity;
        seat = i + 1;
        if (via_old && s.body_old == owner) *via_old = true;
        return true;
    }
    return false;
}

// ★ 这个场景对象是不是某个**已知死者**的灵魂体。是的话给出他的身份和号码。
//   跟 copycat_identity 的分工：那个只认活人本体(str 画身份+阵营色)，
//   这个认灵魂体(str 画"N号 身份（已死）"+灰白)。
//   走身份簿而不是当轮快照里的 soul[]：簿子是**锁存**的，开会阶段 Python 堆页被换出、
//   [1089] 整张表读不到的时候，这里照样认得出死者 —— 那正是用户看到"死了还显示活着"的时段。
static bool copycat_ghost_identity(uintptr_t obj, int &identity, int &seat)
{
    if (obj == 0) return false;
    const CopycatBook &bk = g_book[g_book_active];
    for (int i = 0; i < MAX_COPYCAT; i++) {
        if (!cc_seat_valid(i + 1)) continue;              // 本局没有这个号(残留的 13~16)
        const CopycatSeat &s = bk.seat[i];
        if (!s.known || !s.dead || s.soul == 0 || s.soul != (uint64_t)obj) continue;
        identity = s.identity;
        seat = i + 1;
        return true;
    }
    return false;
}

// 只要"是/不是"，给过滤链用(不想为了判一下去开两个局部变量)
static bool copycat_is_ghost(uintptr_t obj)
{
    int ident = 0, seat = 0;
    return copycat_ghost_identity(obj, ident, seat);
}

// ★ 死者的**当前**场景对象 -> 身份号。
//   局内：这一项指向那具模型不可见的尸体；会议桌上：引擎把每个人换成座位模型，
//   这一项就指向他在桌边的那个模型。
//   绘制侧靠它把死人**画出来**并标成「N号 身份（已死）」。
//
//   为什么必须走身份簿而不是当轮快照：开会阶段本体集合/锚点经常读不全，快照可能整轮
//   都不发布；而 book_merge 在 try_refresh 里排在锚点**之前**，只要 collect_copycat
//   这一轮读到东西，簿子就会更新 —— 这是唯一在会议桌上还稳的来源。
//   （原来这个函数只用来"跳过尸体"，于是会议桌上把死人的座位模型一起跳掉了 ——
//     用户看到的就是"到会议桌上就不绘制死人了"。）
static bool copycat_dead_body(uintptr_t obj, int &identity, int &seat)
{
    if (obj == 0) return false;
    const CopycatBook &bk = g_book[g_book_active];
    for (int i = 0; i < MAX_COPYCAT; i++) {
        if (!cc_seat_valid(i + 1)) continue;              // 本局没有这个号(残留的 13~16)
        const CopycatSeat &s = bk.seat[i];
        if (!s.known || !s.dead) continue;
        // 死者也同样要比 body_old：假面换锚点时，尸体的锚点一样会被换掉
        if (s.body != (uint64_t)obj && s.body_old != (uint64_t)obj) continue;
        identity = s.identity;
        seat = i + 1;
        return true;
    }
    return false;
}

// 引擎这一帧到底有没有把这个场景对象渲染出来(NeoX 模型 +0x70)。
//
// ⚠ 用途被严格限定成一句话：**判断"这个已知对象要不要画"**。
//   绝对不要拿它反推"这个人死没死" —— 远处活人、被视角剔除的活人同样是 0，
//   用它判死活会在"背后站着个活人"时把人藏掉(见 SKILL.md 3.4)。
//   这里问的只是"引擎自己画没画它"：一具不可见的尸体不必再画一遍，
//   而会议桌上的座位模型是会被渲染的，所以照样放行。
static bool scene_is_rendered(uint64_t scene)
{
    if (!scene) return false;
    uint32_t v = 0;
    if (!vm_readv(scene + 0x70, &v, 4)) return false;
    return v != 0;
}

// ---- 自身当前是否在施放技能 ----
// 判据：读 cam.unit.state_machine._state 列表，只要列表里出现"非基础状态"的 id 就算施放中。
// 基础状态实测：1=站立、8=战斗姿态(拿枪/拿武器)、52/53=跳跃/落地
// 剩下的 id (>=10) 基本都是技能/蓄力/瞄准态
static bool self_is_using_skill()
{
    if (!g_available) return false;
    // 复用已有字段缓存：cam.unit 在 g_buf[active] 里没保存，需要新加一个字段
    // 简化做法：从 anchor 的场景对象反查 unit —— 引擎侧没提供这条路
    // 所以这里改为：在 try_refresh 里顺便把 state_machine._state 读出来发布
    return g_buf[g_active].self_skill_state > 0;
}

// ★ 自身当前动作状态 id 的**原始值**（2026-10-12 新增）
//   为什么需要它：`self_is_using_skill()` 是"任何 >10 且非 52/53 都算施放技能"，
//   粒度太粗 ✗ —— 75/124/36/12 这些普通动作也会返回 true ✗
//   小丑蓄力其实只对应 **47**（StateButcherSkillChannel）✓
//   所以调用方要拿原始 id 自己判断，不能只用那个布尔值 ✓
static int self_skill_state_id()
{
    if (!g_available) return 0;
    return g_buf[g_active].self_skill_state;
}

// 阵营按号段：1xx 侦探团 / 2xx 模仿者 / 3xx 中立(没有独立的阵营字段，若辰那些 camp* 属性在这个模式下全不存在)
static inline int copycat_camp(int identity) { return identity / 100; }

// 身份号是不是"已知的模仿者身份"(101~118 / 201~209 / 301~308 三个阵营)。
// 补框通道用它当闸门：身份未知就不画 —— 否则会出现"没有名字的白框"。
static inline bool copycat_camp_known(int identity)
{
    const int c = identity / 100;
    return c >= 1 && c <= 3 && identity > 100;
}

// 身份名：若辰 sub_394ED4 的跳转表(本人两局 114/301 已实测对上；101 它是"取对象自带名"，这里没有那条路)
static const char *copycat_identity_name(int identity)
{
    static const char *const good[] = { "侦探", "哨兵", "治安官", "猎人", "香料师", "锁匠", "银行家", "修士", "演说家",
                                        "拳击手", "灵媒", "学徒", "密探", "掮客", "药剂师", "巡林员", "执灯人", "评论家" };
    static const char *const bad[] = { "神偷", "千面人", "阴谋家", "烟火师", "怪盗", "催眠师", "地下医生", "处刑人", "指挥家" };
    static const char *const neutral[] = { "流浪汉", "送货员", "愚人", "棋手", "清洁工", "顾问", "降灵师", "导演" };
    int k = identity % 100 - 1;
    switch (identity / 100) {
        case 1: if (k >= 0 && k < (int)(sizeof(good) / sizeof(good[0]))) return good[k]; break;
        case 2: if (k >= 0 && k < (int)(sizeof(bad) / sizeof(bad[0]))) return bad[k]; break;
        case 3: if (k >= 0 && k < (int)(sizeof(neutral) / sizeof(neutral[0]))) return neutral[k]; break;
    }
    return nullptr;
}

} // namespace PySelf

#endif // IDV_PY_SELF_H
