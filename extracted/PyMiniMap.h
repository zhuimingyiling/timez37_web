#ifndef IDV_PY_MINIMAP_H
#define IDV_PY_MINIMAP_H

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <thread>
#include <atomic>
#include <unistd.h>
#include "PyRoot.h"
#include "PyCore.h"

namespace PyMiniMap {

static const int MAX_TEAM = 8;
static const int GOLDRUSH_UNIT_TYPE = 1999;

// ---- 当前地图 ID / scene_path（供小地图选底图用） ----
static int64_t g_scene_id = 0;
static char    g_scene_path[160] = "";
static char    g_scene_pic[160] = "";
static int64_t i_scene_pic = -1;

struct TeamMember {
    uint64_t unit;        // Python GoldRushPlayerUnit
    uint64_t scene_obj;   // unit.model + 0x20（场景对象）
    int64_t  uid;
    int64_t  pid;
    float    x, y, z;     // Python position（(X, Y=脚踝, Z)）
    float    sx, sy, sz;  // 场景坐标（来自 scene_obj + 0x28 + 0xa0）
    bool     has_scene;   // 场景坐标是否读到
    float    dirx, dirz;
    int64_t  gender;
    int64_t  identity;
    char     name[32];
    bool     valid;
};

struct Snapshot {
    TeamMember team[MAX_TEAM];
    int        count;
    bool       active;
    uint64_t   self_unit_obj;   // ★ g_cam_ctrl.unit 的 Python 对象指针（GoldRush 里就是自己）
};

static Snapshot         g_buf[2];
static std::atomic<int> g_active{0};
static std::atomic<bool> g_available{false};
static uintptr_t        g_libbase = 0;
static char             g_status[96] = "未启动";

static int64_t i_um = -1, i_ubt = -1;
static int64_t i_pos = -1, i_dir = -1, i_uid = -1, i_pid = -1,
               i_ident = -1, i_gender = -1, i_name = -1, i_model = -1;
// 地图相关序号缓存
static int64_t i_world_mgr = -1, i_cur_world = -1, i_scene_id = -1, i_scene_path = -1;

static bool try_refresh()
{
    if (!PyRoot::ensure()) return false;

    const char *why = "";
    uint64_t md = PyCore::game_kernel_dict(why);
    if (!md) return false;

    // ---- 读当前地图 ID / scene_path ----
    i_world_mgr = PyCore::find_key(md, "g_world_mgr", i_world_mgr);
    if (i_world_mgr >= 0) {
        uint64_t wm = PyCore::get_attr(md, i_world_mgr);
        uint64_t wmd = PyCore::get_inst_dict(wm);
        PyCore::Dict wmk;
        if (wmd && PyCore::read_dict(wmd, wmk)) {
            i_cur_world = PyCore::resolve_index(wmd, wmk, i_cur_world, "cur_world");
            if (i_cur_world >= 0) {
                uint64_t cw = getPtr64(PyCore::value_slot(wmk, i_cur_world));
                uint64_t cwd = PyCore::get_inst_dict(cw);
                PyCore::Dict cwk;
                if (cwd && PyCore::read_dict(cwd, cwk)) {
                    int64_t v = 0;
                    i_scene_id = PyCore::resolve_index(cwd, cwk, i_scene_id, "scene_id");
                    if (i_scene_id >= 0 &&
                        PyCore::read_int(getPtr64(PyCore::value_slot(cwk, i_scene_id)), v))
                        g_scene_id = v;
                    i_scene_path = PyCore::resolve_index(cwd, cwk, i_scene_path, "scene_path");
                    if (i_scene_path >= 0) {
                        uint64_t sp = getPtr64(PyCore::value_slot(cwk, i_scene_path));
                        PyCore::read_str(sp, g_scene_path, sizeof(g_scene_path));
                    }
                    // scene_pic 不在 cur_world 里，在 cur_world.scene_data 里
static int64_t i_scene_data = -1;
i_scene_data = PyCore::resolve_index(cwd, cwk, i_scene_data, "scene_data");
if (i_scene_data >= 0) {
    uint64_t sd = getPtr64(PyCore::value_slot(cwk, i_scene_data));
    PyCore::Dict sdk;
    if (PyCore::read_dict(sd, sdk)) {
        i_scene_pic = PyCore::resolve_index(sd, sdk, i_scene_pic, "scene_pic");
        if (i_scene_pic >= 0) {
            uint64_t sp2 = getPtr64(PyCore::value_slot(sdk, i_scene_pic));
            PyCore::read_str(sp2, g_scene_pic, sizeof(g_scene_pic));
        }
    }
}
                }
            }
        }
    }

    // ★ 顺便读 g_cam_ctrl.unit 的 Python 对象指针 —— GoldRush 里它就是"本地玩家自己"。
    //   用对象地址比较比 uid 更可靠：uid 会因为 cam.unit 滞后一秒而读到上一次的值，
    //   而对象指针就是"我现在操控的那个 unit"，不会有歧义。
    static int64_t i_cam = -1, i_cam_unit = -1;
    uint64_t self_unit_new = 0;
    i_cam = PyCore::find_key(md, "g_cam_ctrl", i_cam);
    if (i_cam >= 0) {
        uint64_t cam = PyCore::get_attr(md, i_cam);
        if (PyCore::is_obj(cam)) {
            uint64_t camd = PyCore::get_inst_dict(cam);
            if (camd) {
                PyCore::Dict camk;
                if (PyCore::read_dict(camd, camk)) {
                    i_cam_unit = PyCore::resolve_index(camd, camk, i_cam_unit, "unit");
                    if (i_cam_unit >= 0) {
                        self_unit_new = getPtr64(PyCore::value_slot(camk, i_cam_unit));
                    }
                }
            }
        }
    }

    i_um = PyCore::find_key(md, "unit_mgr", i_um);
    if (i_um < 0) return false;
    uint64_t um = PyCore::get_attr(md, i_um);
    uint64_t ud = PyCore::get_inst_dict(um);
    if (!ud) return false;
    i_ubt = PyCore::find_key(ud, "units_by_type", i_ubt);
    if (i_ubt < 0) return false;
    uint64_t ubt = PyCore::get_attr(ud, i_ubt);
    if (!PyCore::is_obj(ubt)) return false;

    uint64_t lst = PyCore::get_type_list(ubt, GOLDRUSH_UNIT_TYPE);
    int64_t n = 0; uint64_t items = 0;

    if (!PyCore::read_list(lst, n, items)) return false;
    if (n == 0) return g_available;

    Snapshot res{};
    res.active = true;
    res.self_unit_obj = self_unit_new;   // ★ 保存自己
    if (n > MAX_TEAM) n = MAX_TEAM;

    for (int64_t i = 0; i < n; i++) {
        uint64_t unit = getPtr64(items + i * 8);
        if (!PyCore::is_obj(unit)) continue;
        uint64_t d = PyCore::get_inst_dict(unit);
        if (!d) continue;
        PyCore::Dict dk;
        if (!PyCore::read_dict(d, dk)) continue;

        i_pos    = PyCore::resolve_index(d, dk, i_pos,    "position");
        i_dir    = PyCore::resolve_index(d, dk, i_dir,    "direction");
        i_uid    = PyCore::resolve_index(d, dk, i_uid,    "uid");
        i_pid    = PyCore::resolve_index(d, dk, i_pid,    "pid");
        i_ident  = PyCore::resolve_index(d, dk, i_ident,  "goldrush_identity");
        i_gender = PyCore::resolve_index(d, dk, i_gender, "gender");
        i_name   = PyCore::resolve_index(d, dk, i_name,   "player_name");
        i_model  = PyCore::resolve_index(d, dk, i_model,  "model");

        TeamMember &m = res.team[res.count];
        memset(&m, 0, sizeof(m));
        m.unit = unit;

        if (i_pos >= 0) {
            uint64_t po = getPtr64(PyCore::value_slot(dk, i_pos));
            if (PyCore::is_obj(po) && vm_readv(po + 0x10, &m.x, 12)) {
                m.valid = (m.x != 0.f || m.z != 0.f) && m.y > -300.f;
            }
        }
        if (!m.valid) continue;

        // ★ 尝试读 unit.model → scene_obj → 场景坐标
        if (i_model >= 0) {
            uint64_t model = getPtr64(PyCore::value_slot(dk, i_model));
            if (PyCore::is_obj(model)) {
                uint64_t so = getPtr64(model + 0x20);
                if (PyCore::is_obj(so)) {
                    m.scene_obj = so;
                    uint64_t coor = getPtr64(so + 0x28);
                    if (PyCore::is_obj(coor)) {
                        float sc[3] = {0, 0, 0};
                        if (vm_readv(coor + 0xa0, sc, 12)) {
                            m.sx = sc[0]; m.sy = sc[1]; m.sz = sc[2];
                            m.has_scene = (m.sx != 0.f || m.sz != 0.f);
                        }
                    }
                }
            }
        }

        if (i_dir >= 0) {
            uint64_t dv = getPtr64(PyCore::value_slot(dk, i_dir));
            float f3[3] = {0, 0, 0};
            if (PyCore::is_obj(dv) && vm_readv(dv + 0x10, f3, 12)) {
                float len = sqrtf(f3[0]*f3[0] + f3[2]*f3[2]);
                if (len > 0.01f) { m.dirx = f3[0]/len; m.dirz = f3[2]/len; }
            }
        }
        if (i_uid >= 0)    PyCore::read_int(getPtr64(PyCore::value_slot(dk, i_uid)),    m.uid);
        if (i_pid >= 0)    PyCore::read_int(getPtr64(PyCore::value_slot(dk, i_pid)),    m.pid);
        if (i_ident >= 0)  PyCore::read_int(getPtr64(PyCore::value_slot(dk, i_ident)),  m.identity);
        if (i_gender >= 0) PyCore::read_int(getPtr64(PyCore::value_slot(dk, i_gender)), m.gender);
        if (i_name >= 0) {
            uint64_t np = getPtr64(PyCore::value_slot(dk, i_name));
            if (PyCore::is_obj(np)) PyCore::read_str(np, m.name, sizeof(m.name));
        }

        res.count++;
    }

    int w = 1 - g_active;
    g_buf[w] = res;
    g_active = w;
    g_available = true;
    snprintf(g_status, sizeof(g_status), "正常 %d 人 scene=%lld",
             res.count, (long long)g_scene_id);
             
    if (g_scene_pic[0]) {
    static char s_last[160] = "";
    if (strcmp(s_last, g_scene_pic) != 0) {
        snprintf(s_last, sizeof(s_last), "%s", g_scene_pic);
        printf("[小地图] scene_pic='%s'\n", g_scene_pic);
        fflush(stdout);
    }
}         
    return true;
}

static void thread_main()
{
    DisguiseThreadName(15);   // PyMiniMap
    while (true) {
        try_refresh();
        usleep(150 * 1000);
    }
}

static void start(uintptr_t libbase)
{
    if (g_libbase != 0) return;
    g_libbase = libbase;
    snprintf(g_status, sizeof(g_status), "启动中");
    std::thread(thread_main).detach();
    printf("[小地图] 已启动 libbase=0x%lx\n", (unsigned long)libbase);
    fflush(stdout);
}

static inline bool available()   { return g_available; }
static inline const char* status_text() { return g_status; }
static inline int  team_count()  { return g_available ? g_buf[g_active].count : 0; }
static inline bool in_goldrush() { return g_available && g_buf[g_active].count > 0; }

// ★ 当前地图 ID / scene_path
static inline int64_t scene_id() { return g_scene_id; }
static inline const char* scene_path() { return g_scene_path; }

// ★ 当前场景是不是"对局中"。
//   为什么需要它：PySelf::check_lobby 是靠 units_by_type 里有没有键 1/2/1088
//   来判断"局内/局外"的。但模仿者局的**准备阶段**（实测场景
//   scene/scene_battle/h55_wait_copycat2）里三个键一个都没有，只有 69/1007/100，
//   于是被判成"没有玩家 = 局外"，绘制侧把阵营 1/2 整类跳过 ——
//   表现就是**所有人突然都不画了**。
//
//   比键号可靠得多的判据：场景路径。只要还挂在 scene_battle 下就是在对局中，
//   键号怎么变都不影响。大厅场景不含 scene_battle。
static inline bool in_battle_scene() {
    return g_scene_path[0] != 0 && strstr(g_scene_path, "scene_battle") != nullptr;
}
static inline const char* scene_pic() { return g_scene_pic; }

static bool get_member(int i, TeamMember &out)
{
    if (!g_available) return false;
    const Snapshot &s = g_buf[g_active];
    if (i < 0 || i >= s.count) return false;
    out = s.team[i];
    return true;
}

// ★ g_cam_ctrl.unit 的 Python 对象指针（GoldRush 里就是自己）
static inline uint64_t self_unit_obj()
{
    if (!g_available) return 0;
    uint64_t u = g_buf[g_active].self_unit_obj;
    if (u != 0) return u;

    int64_t uid = PySelf::self_uid();
    if (uid >= 1000001 && uid <= 1000100) {
        const Snapshot &s = g_buf[g_active];
        for (int i = 0; i < s.count; i++)
            if (s.team[i].uid == uid) return s.team[i].unit;
    }
    return 0;
}

} // namespace PyMiniMap

#endif
