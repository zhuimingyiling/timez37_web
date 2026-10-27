#include "draw.h"
// ★★ 2026-10-13 合并：原来这里有两套并行的安全头文件 ★★
//   `obf.h`       —— 字符串加密(OBF 宏) + 反调试(TracerPid/进程表)
//   `secconfig.h` —— 配置加密 + 代码段自校验 + frida 检测
//   加上 `guard.h` 一共**三套**，功能大面积重叠 ✗
//
//   现在：
//     · `guard.h/cpp` 是**唯一权威**（反调试 7 项 = 三套的并集）✓
//     · `secconfig.h` 变成薄转发（保留 SecCfg/SecMem/Sec 三个命名空间，
//        所以下面那些调用点一个字都不用改）✓
//     · `obf.h` **已删除** —— 它的 OBF 宏已无使用点（全改 GUARD_STR），
//        DebuggerAttached/SuspiciousProcess 等**没有任何调用者** ✗
//        那是"看起来在防护"的死代码，留着只会让人误以为有保护 ✓
//        备份在 _backup/removed-obf-20261013/ ✓
//   ⚠ 保留这个 include 会编译失败（文件已不存在）—— 所以必须删掉 ✓
#include "secconfig.h"    // 薄转发 -> guard.h ✓
#include <thread>
#include <cstdint>
#include <stdio.h>
#include "My_font/wrg_font.h"
#include "kerneldriver-paradise.hpp"
#include "share_assets.h"   // 2026-10-26：内置地图/头像
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <unistd.h>
#include "DrawTool.h"
#include "Name.h"
#include "PyProgress.h"
#include "joker_parts.h"   // ★ 小丑地面零件（推进器/风翼/钻头）绘制
#include "joseph_camera.h"
// （2026-10-13：touch_proxy.h 已随「自动震慑」一起删除 —— 本来就废弃了 ✓）
#include "mirror_learn.h"   // ★ 镜像实体跳变学习器（先只记录，不改变绘制）
// ★★ 2026-10-13 新增：「自动校准」的数据侦测（先只观测，不点击）★★
//   用户需求：修机时随机弹出校准条，要在**黄条区域**自动点校准键 ✓
//   ⚠ **误点会让密码机暂停** ✗ 所以先只观测，把黄区/游标的数据找出来
//     再谈点击 —— 没有数据就点击 = 纯随机误点 ✗
#include "CalibProbe.h"
#include "view_switch.h"    // ★ 「功能」页：改视角（扫描角色坐标 + 列表选人）
#include "heartbeat.h"
#include "Security.h"    // ★ 授权核心：数据源闸门 / 数值降级
#include "guard.h"
#include "SecMath.h"      // ★ 2026-10-24 敏感数学（矩阵投影/瞄准/触摸时序）→ security 满档 TU
#include "SecOffsets.h"   // ★ 2026-10-24 偏移表运行时解密 enc[i]=off[i]^key(seed,token)

// 前向声明：定义在 2300 行附近（那里有完整注释），但启动扫描循环在它之前就要用。
//   上下界现在由 SecOff 提供（加密表 + 会话密钥），未授权时恒 false。
static inline bool is_user_ptr(uint64_t p);
#include "NativeBones.h"   // 原生骨骼（驱动读，零注入；抄若辰 3A6074/39F0CC 偏移链）
#include "PyScene.h"       // 阶段判定（大厅/准备/局内），单一真相源
#include "decoy.h"   // 备选读取路径（早期实现，保留用于回退）          // ★ GUARD_STR 字符串加密（诊断文案也要加密）       // ★ 授权心跳诊断（fn=hb）    // ★ 「功能」页：改视角（扫描角色坐标 + 列表选人）

// ★ 密码机进度悬浮窗（2026-10-12）—— 逻辑照「身份悬浮窗」，
//   但排序是**进度最多的在最上方**（PyProgress::get_list 已按进度降序排好 ✓）
static bool  g_gen_list_show = false;
static float g_gen_list_x    = 60.0f;
static float g_gen_list_y    = 240.0f;
static float g_gen_size      = 22.0f;
static ImU32 g_gen_bg        = IM_COL32(20, 20, 24, 170);
static ImU32 g_gen_txt       = IM_COL32(120, 220, 255, 255);

// ★ 约瑟夫相机绘制（2026-10-12 事故后重新接回）
//   相机类名形如 `scene\model\...\h55_joseph_camera.gim` —— **里面带 "scene"**，
//   所以分 camp 的分支必须排在 scene 分支之前，否则会被 getscene() 丢弃（返回 NULL -> continue）。
static bool  show_draw_joseph_cam = true;
static ImU32 g_joseph_cam_col     = IM_COL32(120, 200, 255, 255);
// ★ 镜像固定色（学习器学到镜像地址后用它上色）
static ImU32 g_mirror_col         = IM_COL32(255, 255, 255, 255);

#include "PyGenius.h"
#include "PySelf.h"
#include "PyMiniMap.h"      // ← 新增
#include "PyDump.h"
#include "SurvSkillDump.h"
#include "ItemIcon.h"

#ifndef ENABLE_FREE
#define ENABLE_FREE 0
#endif
// ★ 骑士（dxzh）头盔预测读取器（2026-10-21）
//   放在 PyDump.h 之后：它只依赖 PyRoot/PyCore，但绘制侧的注释里同时要提到 dump_dict_at
//   那套"字典地址直接交给 read_dict"的做法，两个头文件放在一起看最省事
#include "PyKnight.h"
#include "JokerSpeed.h"
#include "AutoCrash.h"       // ★ 自动坠机
#include "AntiCowboy.h"      // ★ 防牛仔
#include "AutoSkill.h"
#include "TouchKeyboard.h"
#include "soft_keyboard.h"   // 内置屏幕键盘（系统输入法在本架构下不可达）
#include "PyAttack.h"
#include "PyCover.h"   // 2026-10-26: scene_id (cur_world.scene_id)     // ← 新增
#include "McpBridge/mcp_bridge.h"   // ImGui-MCP：沙盒窗口绘制
#include "McpBridge/mcp_data.h"     // ImGui-MCP：把配置/实体暴露给 AI


#include "AutoPallet.h"
#include "TouchHelperA.h"
#include "theme.h"
#include "fang_ui.h"
#include "my_ui.h"
#include "effects.h"
#include <linux/input.h>
#include <sstream>
#include <iomanip>
#include <ctime>          // clock_gettime
#include <cstdarg>
#include <algorithm>
#include <atomic>
#include <unordered_set>
#include <sched.h>
#include <sys/resource.h>
#include <pthread.h>
#include <mutex>
#include <algorithm>   // std::swap

// ==================== 日志（统一出口：/data/adb/.d/o.log） ====================
//   ★ 2026-10-21 用户要求：**所有输出日志只写 /data/adb/.d/o.log，不写别处**，
//     并且跟随 Android.mk 的 ENABLE_LOG 开关（=0 时全部静默，连文件都不创建）。
#ifndef ENABLE_LOG
#define ENABLE_LOG 0
#endif

// 全工程唯一日志文件（与 logdump.h 的 o.log 是同一个文件，追加写，不互相覆盖）
// static const char* kAppLogPath = GUARD_APP_LOG();  ← 静态指针会长期持有明文
// 改用 GUARD_APP_LOG()：在调用点即时解密，产物里不留明文（2026-10-22）

// 给别的模块（PyKnight / QteAuto）也用的统一写入口
//   注意：这是**不受** ENABLE_LOG 影响的底层函数，调用方自己判断开关
static void AppLogRaw(const char* fmt, ...) {
#if !ENABLE_LOG
    /*   2026-10-24：发布版**一个字都不写**（原来这里会创建 o.log）。 */
    (void)fmt;
    return;
#else
    va_list ap;
    va_start(ap, fmt);
    FILE* f = fopen(GUARD_APP_LOG(), "a");
    if (f) { vfprintf(f, fmt, ap); fputc(10, f); fclose(f); }
    va_end(ap);
#endif
}

static FILE* g_log_fp = nullptr;
static bool  g_log_inited = false;

static void LOG(const char* fmt, ...) {

#if ENABLE_LOG

    if (!g_log_inited) {
        g_log_fp = fopen(GUARD_APP_LOG(), "a");
        g_log_inited = true;
        if (g_log_fp) {
            fprintf(g_log_fp, "=== dwrg touch log start ===\n");
            fflush(g_log_fp);
        }
    }

    va_list ap1, ap2;

    va_start(ap1, fmt);
    vprintf(fmt, ap1);
    va_end(ap1);

    fflush(stdout);

    if (g_log_fp) {
        va_start(ap2, fmt);
        vfprintf(g_log_fp, fmt, ap2);
        va_end(ap2);

        fflush(g_log_fp);
    }

#else

    (void)fmt;

#endif
}

std::string filter_class_name,class_name;
// 类名缓存：对象地址 -> {第一跳指针(校验用), 类名}。只有读线程碰它，不用加锁。见读取循环里的说明
static std::unordered_map<uintptr_t, std::pair<uint64_t, std::string>> g_name_cache;
float dist_scale=11.886;
float redqueen_x, redqueen_y, redqueen_z;
float redqueen_mirror_x, redqueen_mirror_y, redqueen_mirror_z;
typedef struct {
    uintptr_t obj;
    uintptr_t objcoor;
    int camp;
    char str[256];//翻译名
    char class_name[256];//类名
    //   ★ 2026-10-21 台球手变球：obj+0x28 那条坐标链会失效（实测 0 / -65536），
    //     这里存一份"Python 层 unit.position"的兜底坐标，绘制侧优先用它。
    float fb_x, fb_y, fb_z;
    bool  fb_ok;
}DataStruct;
DataStruct data[3000];
// `data[]` is the published snapshot. read_thread builds the next frame in
// g_data_build and publishes it under g_data_mutex, so render/API readers never
// observe a partially filled entity row or array.
static std::recursive_mutex g_data_mutex;
static DataStruct g_data_build[3000];

//   ★ 2026-10-21：统一的世界坐标读取入口。
//     正常情况下走 objcoor+0xa0；读不到/读到垃圾值时，用读取阶段准备好的
//     Python unit.position 兜底（台球手变球时 obj+0x28 那条链就是坏的）。
extern int data_count;   // 真定义在下面 397 行附近
static inline void unit_xyz(int i, float out[3])
{
    std::lock_guard<std::recursive_mutex> data_lock(g_data_mutex);
    out[0] = out[1] = out[2] = 0.0f;
    if (i < 0 || i >= data_count) return;          // data_count 在上面，已声明
    if (data[i].fb_ok) { out[0] = data[i].fb_x; out[1] = data[i].fb_y; out[2] = data[i].fb_z; return; }
    if (data[i].objcoor) vm_readv(data[i].objcoor + (uint64_t)SecOff::u64(SecOff::DG_01), out, 12);
}

bool permeate_record = false;
bool permeate_record_ini = false;
struct Last_ImRect LastCoordinate = {0, 0, 0, 0};
static uint32_t orientation = -1;
ANativeWindow *window; 
// 屏幕信息
android::ANativeWindowCreator::DisplayInfo displayInfo;
// 窗口信息
ImGuiWindow *g_window;
// 绝对屏幕X _ Y
int abs_ScreenX, abs_ScreenY;
int native_window_screen_x, native_window_screen_y;
std::unique_ptr<AndroidImgui>  graphics;
ImFont* zh_font = NULL;
ImFont* zh_display = NULL;      // 46px 显示字号（精选字形）
// 2026-10-27: rebuild 后需要下一帧居中主窗口
static bool g_center_main_window_next_frame = false;
bool niexi;
float cam_dist;
float niexi_dist,niexi_hold_dist;
/*定义*/
bool DrawIo[50];
float niexi_touch_x,niexi_touch_y;
// ---------------------------------------------------------------------------
// 显示字号需要的字符集
// ---------------------------------------------------------------------------
// 不能直接上 GetGlyphRangesChineseFull()（两万多字形）：25px 那档已经占了
// 十几 MB 图集，再来一档 46px 会翻到 40MB+，手机覆盖层扛不住。
// 大字只用于标题、关键数值、页名，字符集**有限且已知**，精确列举即可。
static const char* kDisplayGlyphs =
    "绘制触摸设置加页小地图概览连接当前角色监管状态宝箱怪物队员地址信息"
    "技能帧率内存进程换出显存缓冲数据已获取到未正在就绪展开调试位置"
    "模块数量显示队友号码调整大小缩放距离颜色天赋鬼魂道具夫人模式预知"
    "密码机大门宿伞模仿者射线名字方框粗细样式背景纯色贴图加载中错误"
    "无敌透视自动攻击拾取治疗闪现传送速度高度宽度半径角度层级透明度"
    "游戏目标剩余时间倒计分秒局内对战场次胜率历史记录保存重置确认取消"
    "开启关闭启用禁用选择全部无自定义默认高级基础新增删除修改完成失败"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
    " .,:%/+-()[]#@!?_'\"=<>*&$~|\\^;{}`";

static void BuildDisplayFont() {
    ImGuiIO& io = ImGui::GetIO();
    static ImVector<ImWchar> ranges;      // 必须活到图集构建之后
    ranges.clear();
    ImFontGlyphRangesBuilder builder;
    builder.AddText(kDisplayGlyphs);
    builder.BuildRanges(&ranges);

    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    cfg.OversampleH = 2;                  // 大字要采样，不然边缘发毛
    cfg.OversampleV = 2;
    cfg.SizePixels  = 46.0f;
    ::zh_display = io.Fonts->AddFontFromMemoryTTF(
        (void*)WRG_Font, WRG_Font_size, 46.0f, &cfg, ranges.Data);
}

bool M_Android_LoadFont(float SizePixels) {
    ImGuiIO &io = ImGui::GetIO();
    
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    config.OversampleH = 1;
    config.SizePixels = SizePixels;
    ::zh_font = io.Fonts->AddFontFromMemoryTTF((void *)WRG_Font, WRG_Font_size, SizePixels, &config, io.Fonts->GetGlyphRangesChineseFull());
    BuildDisplayFont();

    return zh_font != nullptr;
}
void init_My_drawdata() {
    //   2026-10-27：**幂等化**
    //   旋转屏幕会走 drawBegin() 的 rebuild 分支，那里原来又调了一次这个函数；
    //   而它每次都会往 ImGui 字体图集里 AddFontFromMemoryTTF 两份中文字体
    //   （25px + 46px，全 Unicode 两万多字形），第二次就撑爆图集 →
    //   ImGui 1.92 的动态纹理创建失败 → EndFrame() 渲染文字时访问无效
    //   VkDescriptorSet → SIGSEGV。
    //   字体图集是整个 ImGui 上下文共用的，Shutdown/Init 只重建 Vulkan 后端，
    //   不会销毁字体，所以 rebuild 时**完全不需要**重新加载。
    static bool s_done = false;
    if (s_done) return;
    s_done = true;
    M_Android_LoadFont(25.0f);
}


void screen_config() {
    ::displayInfo = android::ANativeWindowCreator::GetDisplayInfo();
}

void ResetMiniMapTexture();   // 前向声明，供 drawBegin() 使用
static bool show_draw_AutoQte = false;   // 自动校准（QTE）   // 治疗进度（画在天赋行上方，数据来自 PyGenius::heal_of，纯驱动读）
namespace QteAuto { void EnsureStarted(); void SetOn(bool on); }

// ============================================================================
// 骑士头盔预测（读取器在 PyKnight.h，这里只放状态和绘制）
//
//   读数链路：skill_mgr -> skill_dict -> [14101] -> cast_args -> predict_arg
//             -> arg -> use_id = [use_seq, sub]
//             World.knight_predict_use_counter（更权威，优先用它）
//   文本中转：PyDump::dump_dict_at 是 MCP 用的同一套实现（实测能读对，
//             而手写字段解析读这个对象会拿到错的字典）。
//
//   ⚠ **"猜了什么"读不到**：客户端根本不收这个字段（222 字段全量对比 / 2000MB 扫描零命中 /
//     协议是 protobuf 三条独立证据）。所以本功能只给"头盔猜了几次"，
//     满 2 次 = 马上结算 —— 那才是真正能用来做判断的信息。
// ============================================================================

bool drawBegin() {
    if (!graphics) return false;
    #if !ENABLE_FREE
QteAuto::EnsureStarted();   // 自动校准线程：只启动一次
#endif
    // 每帧同步开关。原来只在"自动校准"那一页渲染时才 store，
    // 切到别的页之后线程读到的还是旧值，表现为"面板显示已开但不生效"。
#if !ENABLE_FREE
    QteAuto::SetOn(show_draw_AutoQte);
#endif

    screen_config();

// ★★★ 2026-10-27 修复：**只用 displayInfo，删掉 ANativeWindow 判据** ★★★
//
//   根因（本次 o.log 铁证）：
//     ANativeWindow_getWidth/Height 返回的是**surface 的物理尺寸**，
//     它**不会随屏幕旋转更新**（除非我们主动重建 surface）。
//     上一版把它当"权威判据"，于是：
//       disp=2400x1080（displayInfo 报横屏）✓
//       ANativeWindow=1080x2400（旧 surface 尺寸）✓
//       -> 强行用 ANativeWindow 的值覆盖 cur_w/cur_h
//       -> need_recreate=0
//       -> surface 永远是 1080x2400
//       -> my_ui 按 1080x2400 居中，屏幕上自然不在正中 ✗
//
//   displayInfo 由 screen_config() 每帧刷新，是**真正实时**的：
//     旋转前: ori=0 disp=1080x2400
//     旋转后: ori=3 disp=2400x1080 ✓ 它变对了！
//
//   所以判据只用 displayInfo：
//     ① window 没创建
//     ② displayInfo 的宽高和当前 native_window_screen_x/y 不一致
int cur_w = displayInfo.width;
int cur_h = displayInfo.height;

// 极端兜底：displayInfo 完全读不到时，用上次的
if (cur_w <= 0 || cur_h <= 0) {
    cur_w = native_window_screen_x;
    cur_h = native_window_screen_y;
}

bool need_recreate = (::window == nullptr) ||
                     (native_window_screen_x != cur_w) ||
                     (native_window_screen_y != cur_h);

if (need_recreate) {
    usleep(500 * 1000);

    // ① 关闭旧 backend（清 ImGui_ImplVulkan_Data）
    if (::window != nullptr) {
        graphics->Shutdown();

        ResetMiniMapTexture();

        android::ANativeWindowCreator::Destroy(::window);
        ::window = nullptr;
    }

    // Shutdown() 已经负责销毁旧 Context；此处不能再次 DestroyContext。
    // 如果上次创建 window 失败，Context 和字体会保留，重试时不要重复添加字体。
    bool created_context = false;
    if (ImGui::GetCurrentContext() == nullptr) {
        ImGui::CreateContext();
        created_context = true;
    }

    g_window = nullptr;

    // 新 Context 必须在 Init_Render 之前加载字体；同一 Context 内只加载一次。
    if (created_context) {
        if (!M_Android_LoadFont(25.0f)) {
            ImGui::DestroyContext();
            return false;
        }
    }

    // ④ 创建新 window
    native_window_screen_x = cur_w;
    native_window_screen_y = cur_h;

::window = android::ANativeWindowCreator::Create(
        "37", cur_w, cur_h, permeate_record);

if (::window == nullptr) {
    // 让主循环跳过本帧；下一帧 drawBegin() 会继续尝试重建。
    return false;
}

int real_w = ANativeWindow_getWidth(::window);
int real_h = ANativeWindow_getHeight(::window);
    if (real_w <= 0 || real_h <= 0) { real_w = cur_w; real_h = cur_h; }
    if (real_w == cur_h && real_h == cur_w && cur_w != cur_h) {
        const int t = real_w; real_w = real_h; real_h = t;
    }

    // ⑤ 初始化新 backend（此时 context 干净，backend 干净）
    if (!graphics->Init_Render(::window, real_w, real_h)) {
        return false;
    }

    native_window_screen_x = real_w;
    native_window_screen_y = real_h;

    g_center_main_window_next_frame = true;

}
    

    if (::orientation != displayInfo.orientation) {
    ::orientation = displayInfo.orientation;
    Touch::setOrientation(displayInfo.orientation);
    
    // ★ 新增：让 KMA 按新方向重新初始化触摸
    extern bool g_kma_touch_inited;
    g_kma_touch_inited = false;
}

    // ⚠ 不要在这里动 io.DisplaySize —— 它在 graphics->NewFrame() 里会被
    //   ImGui_ImplVulkan_NewFrame() 从 wd->Width/Height 重新赋值，
    //   我们在这里改会被覆盖。让 wd 的尺寸对，io.DisplaySize 自然就对。
    return true;
}

struct Vector3A
{
	float X;
	float Y;
	float Z;

	  Vector3A()
	{
		this->X = 0;
		this->Y = 0;
		this->Z = 0;
	}

	Vector3A(float x, float y, float z)
	{
		this->X = x;
		this->Y = y;
		this->Z = z;
	}

};

float xs_final, ys_final;
void calculate_line_reflection(float x1, float y1, float x2, float y2, float xs, float ys, float *xs_prime, float *ys_prime) {
    float A = y2 - y1;
    float B = x1 - x2;
    float C = x2 * y1 - x1 * y2;

    float D = A * xs + B * ys + C;
    float denom = A * A + B * B;

    *xs_prime = xs - 2.0 * A * D / denom;
    *ys_prime = ys - 2.0 * B * D / denom;
}


uintptr_t libbase;
/*   2026-10-26：重载后的绘制冷却 —— 重连后前几帧缓存还是旧局的，
 *   会画出"自己身上的框""停在原地的台球形态"，所以冷却期内直接不画。 */
int g_reload_cooldown = 0;
static std::atomic<bool> g_share_on{false};   // 2026-10-26: share map switch
static void (*g_share_frame_hook)() = nullptr;   // 主线程每帧构建 /state

/*   2026-10-26：**借用外挂已经算好的天赋/CD**
 *   ESP 循环里本来就把每个角色的天赋(seg.pre/flywheel/post)和技能CD(trait_line)拼出来了，
 *   这里存一份给共享地图用，避免网页那边再读一遍内存。
 *
 *   2026-10-27：`dx/dz` 两个字段已删 ——
 *     upsert 里从来没有赋过真实值（原先是 `se.dx = 0; se.dz = 0;`），
 *     网页那边改为**每次都自己读 direction**（`PyGenius::unit_of_scene + UnitFieldPtr`）。
 *     留着它只会让读代码的人以为"网页的朝向是缓存来的"，误导。
 */
struct ShareEnt {
    uint64_t obj;        // 场景对象地址（data[i].obj）
    char     tal[96];    // 天赋三段（pre + flywheel + post）
    char     cd[64];     // 辅助特质 CD 文本
    float    fw;         // 飞轮剩余秒
    float    skcd;       // 技能剩余秒
    int      has_fw;     // 是否带飞轮天赋（0/1）
};
static ShareEnt g_share_ent[48];
static int      g_share_ent_n = 0;
uintptr_t Arrayaddr, Count, Matrix;
uintptr_t cur_obj,self_obj,self_camp,namezfcz,namezfc;
uintptr_t redqueen_obj,redqueen_mirror_obj,mirror_obj,mirror_preview_obj;
float mirror_line_x1, mirror_line_y1, mirror_line_x2, mirror_line_y2;   // 本帧镜面在水平面上的直线(两点)，mirror==true 时有效
int data_count,zfcz,zfc;
static inline int GetPublishedDataCount()
{
    std::lock_guard<std::recursive_mutex> data_lock(g_data_mutex);
    return data_count;
}
// 2026-10-26：监管是否处于「台球形态」（AutoPallet 屏蔽用，每帧由 PyGenius 更新）
namespace AutoPallet { bool g_hunter_ball = false; }
// 2026-10-26：导轨里"点了当前页"的信号（定义在 my_ui.cpp）
namespace MyUI { extern int g_same_page_click; }
float matrix[16];
float angle;

static bool show_draw_Rect = true;//方框
static bool show_draw_Bones = false;//骨骼（原生，驱动读；默认关，开了才读）
static bool show_draw_Line = true;//射线
static bool show_draw_Camera = false;//相机
static bool show_draw_Door = true;//门(开门进度，走 Python 层单独绘制，不依赖 getscene)
static bool show_draw_Box = false;//盒子
static bool show_draw_Name = true;//名字
static bool show_draw_Distance = true;//距离
static bool show_draw_Mirror   = true;//红夫人镜像（玩求生时看对面）
static bool show_draw_Cellar = true;//地窖
static bool show_draw_Chair = false;//椅子
static bool show_draw_Prop = false;
static float g_icon_px = 28.0f;   // 2026-10-25：技能道具图标大小（绘制页滑条控制）//道具
//   2026-10-26：图标 / 数量文字各一对 XY 偏移（用户要求能自己微调位置）
static float g_icon_dx = 0.0f, g_icon_dy = 0.0f;
static float g_txt_dx  = 0.0f, g_txt_dy  = 0.0f;
static bool show_draw_Genius = true;//天赋/辅助特质(走 CPython 链路，见 PyGenius.h)
static bool show_draw_prophet = true;//预知监管者
//   ★ 2026-10-21 用户要求：**「预知天赋」独立开关** —— 准备阶段那行求生者天赋
//     不再跟随「预知监管」，两者互不影响。
static bool show_draw_prep_genius = true;//预知天赋（准备阶段求生者天赋）
static bool redqueenmod = false;//红夫人模式
// ★ 2026-10-13 新增：「隐藏红夫人自身镜像」
//   打开后 camp==8（镜像本体）**整个不画**，只保留镜中的求生者 ✓
//   用户场景：镜子放下时镜像本体和真身会重叠，看着乱 ✗
//   ⚠ 默认 false —— 保持原有行为，不给老用户带来意外变化 ✓
static bool hide_rq_self_mirror = false;
static bool show_draw_umbrella = false;   // 宿伞模式：绘制传伞光标实时落点
static bool copycat_mode = false;//【已自动】本局是不是模仿者局（角色名换成"N号 身份"，按阵营着色）
// ★★ 2026-10-13 用户要求：**删掉「模仿者模式」开关，改成自动识别** ★★
//   原话：「能否取消"模仿者模式"开关，自动识别模仿者对局还是正常对局，
//           因为模仿者模式需要绘制身份，而正常对局正常绘制就行」
//   判据：`ViewSwitch::g_is_copycat`（扫描时发现 units_by_type[1088] 有人）✓
//     普通局根本不会出现 1088 这个类型，所以这个判据可靠 ✓
//   `copycat_mode` 保留成变量（下面几处绘制逻辑都在用它），
//   但**不再由用户控制** —— 每帧由 Draw_Main 按识别结果赋值 ✓
static bool auto_copycat = false;   // 自动识别结果（供界面显示/配置忽略）
static bool show_draw_trait = true;   // 辅助特质 CD（从 show_draw_Genius 拆出来，独立开关）
static bool show_draw_Heal  = true;
// ★★ 2026-10-21 新增：骑士头盔预测（数据来自 PyKnight.h，纯驱动读，200ms 一轮）★★
//   只发布"这个单位是不是骑士"+"头盔猜了几次"，以及头盔 CD / 是否在读条。
//   **"猜的到底是什么"客户端没有** —— 面板上会明写，免得用户以为功能没做完。
static bool  show_draw_Knight    = true;
//   ★ 2026-10-21 用户要求：设置里加「测试」开关
//     开 -> 无条件显示（自身不是监管、甚至自己就是骑士，都显示）
//     关 -> 只有自身是监管时才显示（默认；看对面的骑士才有意义）
static bool  g_knight_test_mode  = false;

// ---------------------------------------------------------------------------
// 治疗进度的显示平滑
//
//   为什么要平滑：驱动侧采样有间隔（快速线程 50ms，主循环 500ms 重新锚定），
//   游戏自己的数值也可能是阶梯式下发的。直接画 raw 会看到"停一下 → 跳 3% → 停一下"。
//
//   做法：每个单位记一个 shown，每帧向 raw 做指数逼近（时间常数 90ms）：
//     · 视觉上连续爬升，不再整段停顿
//     · 最终一定收敛到 raw —— 显示的是真实进度，不是编出来的数
//   条目按 scene_obj 匹配，单位离场后会被新单位复用，不会串号。
// ---------------------------------------------------------------------------
// 治疗进度：速率积分（门控已由 PyGenius 用 state==14 精确判定）
//
//   现在"是否正在治疗"是**权威信号**：有人 state_machine._state[0]==14 且
//   距离 <=2m 时，PyGenius 才会把被治疗者放进表里。表里有 = 正在治疗，
//   表里没有 = 没在治疗 —— 不再需要靠"多久没刷新"去猜，松手立刻生效。
//
//   积分只负责把两次实测锚点之间补平滑：
//     · cured_process_client 一变 -> 重新锚定（误差归零）
//     · 两帧之间 -> 锚点 + cured_speed_client(实测 %/秒) × 已过时间，封顶 100
// ---------------------------------------------------------------------------
struct HealInteg { uint64_t scene; float raw; float shown; double t_anchor; bool prev_active; };
static HealInteg g_heal_int[64];

static bool heal_integrate(uint64_t scene, float raw, float rate, bool active, float &out)
{
    if (scene == 0) return false;
    const double now = ImGui::GetTime();

    HealInteg *slot = nullptr, *reuse = &g_heal_int[0];
    for (int i = 0; i < 64; i++) {
        if (g_heal_int[i].scene == scene) { slot = &g_heal_int[i]; break; }
        if (g_heal_int[i].t_anchor < reuse->t_anchor) reuse = &g_heal_int[i];
    }
    if (!slot) {                              // 刚开始治疗：直接用实测值起步
        slot = reuse;
        slot->scene = scene; slot->raw = raw; slot->shown = raw;
        slot->t_anchor = now; slot->prev_active = active;
        out = raw;
        return true;
    }
    if (fabsf(raw - slot->raw) > 0.05f) {     // 实测锚点更新 -> 误差归零
        slot->raw      = raw;
        slot->shown    = raw;
        slot->t_anchor = now;
    } else if (active && rate > 0.01f) {
        // 正在被治疗（curer_uids 非空）就一直按实测速率积分，**不设上限**。
        // 实测校正：cured_process_client 在连续治疗中几乎不刷新，加任何"最多外推N秒"
        // 的钳位都会让数字卡死（曾出现"涨一会停住、最后停在 85"）。
        // 松手检测交给 curer_uids（active），它一假立刻停止积分。
        float v = slot->raw + rate * (float)(now - slot->t_anchor);
        if (v > 100.f) v = 100.f;
        slot->shown = v;
    }

    // 刚刚松手：如果已经接近满，按"治疗完成"处理（置 100 -> 由显隐规则隐藏）
    if (slot->prev_active && !active && slot->shown >= 95.f) slot->shown = 100.f;
    slot->prev_active = active;

    // 实测锚点自己到 100 也算完成
    if (raw >= 99.5f) slot->shown = 100.f;

    if (slot->shown < 0.f) slot->shown = 0.f;
    out = slot->shown;
    return true;
}
//   ★ 2026-10-13：只给「文字行间距」那个 Fold 用的展开状态 ✓
//     它不是一个功能开关（没有"开/关"语义），只是让那一行能展开/收起 ✓
//     ⚠ 不要拿别的功能开关来当 Fold 的绑定变量 ✗
//       第一版我绑了 `show_draw_Name` -> 界面上出现两个"名字"开关 ✗
static bool show_draw_line_gap = false;

// ============================================================================
// ★★☠☠ 2026-10-13 空档的**真正根因**：我一直把"行盒高"当成"占位高" ✗✗
//
//   ── 实测（fn=label_pos 的"字体度量"那几行）──────────────────────
//     ImFontBaked(字号 26.66): Size=27.0  Ascent=21.0  Descent=-7.0
//     字形 'A':  Y0=5.0  Y1=22.0     <- **字形在行盒里从 5.0 才开始** ✗
//     字形 'g':  Y0=9.0  Y1=26.0
//
//   ── 问题 ────────────────────────────────────────────────────
//     `CalcTextSizeA(size,…)` 返回的是**行盒高度**（≈ size = 26.7）✗
//     而字母**真实墨迹高只有 17**（Y0=5 -> Y1=22）✗
//     => 行盒里**上方空 5px、下方空 4.7px** ✗
//     我拿"行盒高"当占位高 -> 每层凭空多 ~10px，
//       加上行盒本身比墨迹高的部分 -> **视觉空档 ≈ 26.7px** ✗
//     **这就是我推 4 次都说"只有 1px"、而用户看到"空很大"的原因**：
//        我量的是**行盒**，用户看的是**墨迹** ✗✗
//
//   ⚠ 我第一版用 Ascent/Descent 算，算出"墨迹高 40.5"（字号才 26.7）✗✗
//     原因：**我连这两个值的符号都没确认**就套公式 ✗
//   ★ 改用**字形自己的 Y0/Y1** —— 那是 ImGui 真正用来画的量 ✓
//     `RenderText()` 里：`y1 = y + glyph->Y0 * scale`，`y2 = y + glyph->Y1 * scale` ✓
//     所以：字形顶 = 文字y + Y0*scale ；字形底 = 文字y + Y1*scale ✓ **不用猜 ✓**
//   ⚠ 用 'A'(无降部) 的 Y0 当"墨迹顶"、'g'(带降部) 的 Y1 当"墨迹底" ✓
//     这两个字形合起来正好覆盖"顶到降部底"的真实占位 ✓
// ============================================================================
struct InkMetrics { float off; float h; bool ok; };
static InkMetrics ink_metrics(ImFont* f, float size) {
    InkMetrics m{0.0f, size, false};
    if (!f) return m;
    ImFontBaked* bk = f->GetFontBaked(size);
    if (!bk || bk->Size <= 0.0f) return m;
    const float scale = size / bk->Size;          // 和 RenderText 里同一个式子 ✓
    const ImFontGlyph* gA = bk->FindGlyph((ImWchar)'A');   // 顶（无降部）
    const ImFontGlyph* gg = bk->FindGlyph((ImWchar)'g');   // 底（带降部）
    if (!gA || !gg) return m;
    const float top = gA->Y0 * scale;             // 相对文字 y（正=更低）
    const float bot = gg->Y1 * scale;
    m.off = top;                                  // 字形顶 = y + off
    m.h   = bot - top;                            // 真实墨迹高
    m.ok  = (m.h > 1.0f && m.h < size * 2.0f);    // 合理性闸门：不可能超过两倍字号
    return m;
}

// ★★☠☠ 2026-10-13：**准备阶段**的判据（用户现场问题"预知监管下方不显示求生天赋"）
//
//   ── 为什么必须新增这一个（实测数据，不是推理）──────────────────
//   现场抓到的准备阶段状态：
//     scene_path = "scene/scene_battle/h55_wait/h55_wait.scn"
//     in_lobby()        = 0   ✗ 区分不了
//     in_battle_scene() = 1   ✗ 也区分不了
//   **已有的两个"局内/局外"判据，在准备阶段都给不出正确答案** ✗
//     · in_lobby() 的语义是"没有玩家单位" —— 而准备阶段**是有玩家的** ✗
//       （`check_lobby()` 把 HALL_COMPONENT_TYPE(69) 也算玩家，
//         而 69 的注释就是"准备阶段站大厅的玩家" ✗ 自相矛盾）
//     · in_battle_scene() 只看路径含不含 scene_battle —— 准备阶段也在它下面 ✗
//
//   ── 正确的判据：场景名里的 `wait` ──────────────────────────────
//     准备阶段实测: `h55_wait` / `h55_wait_copycat2`  <- **都带 wait** ✓
//     对局中不带 wait ✓
//   ★ 两处独立印证：PySelf.h 的注释也记过
//     "实测准备界面(h55_wait_copycat2)的 units_by_type 只有 69/1007/100" ✓
//   ⚠ 用 **contains** 而不是等号 ✗ 不同模式后缀不同（有 _copycat2）✗
//
//   ⚠ 放这里而不是 PyGenius.h：那里没有 PyMiniMap ✗（第一版就编译失败）
static inline bool in_prep_scene()
{
    // ★★ 2026-10-22 性能：**100ms 缓存** ★★
    //   这个函数在**渲染循环每帧**被调用（预知监管那行），
    //   而 PyMiniMap::scene_path() 是**驱动读**（ioctl），每帧读一次就是 60 次/秒。
    //   场景路径不可能 100ms 内变，所以缓存 100ms。
    //   用原子变量：read_thread 和渲染线程都会调这里。
    static std::atomic<int64_t> s_ms{0};
    static std::atomic<int>     s_val{0};

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const int64_t now = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    if (now - s_ms.load(std::memory_order_relaxed) < 100)
        return s_val.load(std::memory_order_relaxed) != 0;

    bool raw = false;
    const char* p = PyMiniMap::scene_path();
    if (p[0] != 0) {
        raw = (strstr(p, "h55_wait") != nullptr);      // 准备房间
    } else if (PyScene::phase() != PyCore::PHASE_UNKNOWN) {
        raw = PyScene::in_prepare();
    }

    // 迟滞：真评为真立即生效；评为假要连续 800ms 才算真离开
    static std::atomic<int64_t> s_last_true{0};
    bool result;
    if (raw) { s_last_true.store(now, std::memory_order_relaxed); result = true; }
    else     { result = (now - s_last_true.load(std::memory_order_relaxed)) < 800; }

    s_ms.store(now, std::memory_order_relaxed);
    s_val.store(result ? 1 : 0, std::memory_order_relaxed);
    return result;
}
//   ★ 2026-10-13 诊断：准备阶段"天赋列表"那行的前置条件 + 是否真画了 ✓
//     用户反馈"准备阶段的天赋显示不见了"，但代码看着是对的 ✗
//     所以把每一步的条件都记下来，用 fn=draw_stats 看，而不是猜 ✗
//   ⚠ **不能加 static** ✗
//     `static` 是内部链接，`PyDump.h` 里的 `dump_draw_stats()` 是**另一个翻译单元**
//     取不到 -> 链接失败 ✗（我第一版加了 static，正是这个原因）
int g_dbg_prep_show = 0, g_dbg_prep_lobby = 0, g_dbg_prep_hunter = 0;
int g_dbg_prep_civ = 0, g_dbg_prep_drawn = 0;
//   ★ 新的准备阶段判据（!in_battle_scene()）✓
//     旧的 `in_lobby()` 在准备阶段**永远为假**，是这行不显示的真因 ✗
int g_dbg_prep_notbattle = 0;
//   ★ 准备阶段天赋行的逐环节诊断（用户现场反馈"设置页有4条、下面不显示"）✓
int  g_dbg_prep_used = 0;    // bits!=0 的候选条数
int  g_dbg_prep_len  = 0;    // 拼接出的串长度（>0 才会画）
char g_dbg_prep_str[320] = {0};   // 拼接出来的实际内容

// ==================== ESP 样式（用户可调） ====================
struct EspStyle {
    // 方框
    int   rect_style     = 0;                            // 0=全框 1=四角括号
    float rect_thickness = 1.8f;
    ImU32 rect_hunter    = IM_COL32(255,   0,   0, 255);
    ImU32 rect_survivor  = IM_COL32(  0, 255,   0, 255);
    ImU32 rect_ghost     = IM_COL32(255, 255, 255, 255);
    ImU32 rect_cc_good   = IM_COL32( 60, 150, 255, 255);
    ImU32 rect_cc_bad    = IM_COL32(255,  60,  60, 255);
    ImU32 rect_cc_neutral= IM_COL32(255, 220,   0, 255);
    // 射线
    float line_thickness = 2.0f;
    ImU32 line_color     = IM_COL32(255, 255, 255, 255);
    // 骨骼（2026-10-22 新增）
    ImU32 bones_hunter   = IM_COL32(255,  60,  60, 230);   // 监管：红
    ImU32 bones_survivor = IM_COL32( 40, 255,  90, 230);   // 求生：绿
    float bones_thickness= 2.2f;
    float bones_head_size = 9.0f;     // 2026-10-26：头部圆半径
    bool  bones_head     = true;                           // 画头（圆）
    // 名字
    float name_size      = 25.0f;
    ImU32 name_color     = IM_COL32(255, 255,   255, 255);
    // 距离
    float dist_size      = 25.0f;
    ImU32 dist_color     = IM_COL32(255, 255, 255, 255);   // 默认白色
    // 天赋
    float genius_size    = 25.0f;
    ImU32 genius_color   = IM_COL32(255, 255, 255, 255);
    //   ★★ 2026-10-13 用户要求：天赋颜色**分开调监管和求生** ★★
    //     原话：「现在天赋颜色调整只对监管的生效，你补个求生的，
    //            把原先的标注成监管」
    //   为什么原来"只对监管生效"：`genius_color` 是**兜底色**，
    //     只有 `desperate_state()==UNKNOWN` 时才用它 ✗
    //     而求生者的天赋状态大多是 COMPLETE -> 走 `color_green`
    //     （大心脏可用=绿 / 已用=灰）-> **根本走不到 genius_color** ✗
    //     监管那边状态常是 UNKNOWN -> 才用得上 ✓ 所以看起来"只对监管生效"✓
    //   ⚠ 但**不能把求生也改成用这个兜底色** ✗
    //     `color_green / 灰` 是**语义色**（可不可用 / 用没用），信息量比
    //     用户自选色更大 —— 覆盖掉等于把状态信息弄丢 ✗
    //   所以做法是：**求生也加一个自选色**，作为**兜底**（和监管同地位）✓
    //     哪些情况会用到它，见下面绘制处的注释 ✓
    ImU32 genius_color_surv = IM_COL32(255, 255, 255, 255);
    // 技能CD
    float trait_size     = 25.0f;
    ImU32 trait_color    = IM_COL32(255, 255, 255, 255);   // **CD 中**的颜色

    // 治疗进度那行：字号 + 颜色（用户可调）
    float heal_size      = 25.0f;
    ImU32 heal_color     = IM_COL32(210, 200, 60, 255);
    //   ★★ 2026-10-13 用户要求：「技能cd就绪」颜色也要能调 ★★
    //     原话：「写个'技能cd就绪'颜色调整，之前那个只能调整在cd中的颜色」
    //   原来就绪时是**写死的 color_green**：
    //     `PyGenius::cd_ready(gi) ? color_green : ImColor(g_esp.trait_color)` ✗
    //     -> 用户能调的只有"CD 中"那一半，"就绪"永远绿 ✗
    //   默认值就取原来的 color_green，这样不改配置的人看到的效果**完全不变** ✓
    //     （加可调项不应该顺手改默认观感 ✗）
    ImU32 trait_ready_color = IM_COL32( 80, 220,  80, 255);   // 就绪（原写死绿）

    //   ★ 2026-10-21 骑士头盔预测那行（画在框下方，跟治疗进度同一层）
    //     两种颜色分开：**没满**和**已经猜满**是两种完全不同的战术态势
    //     （满 2 次 = 马上结算，这时候出刀最容易被预判）✓
    float knight_size    = 25.0f;
    ImU32 knight_color   = IM_COL32(255, 170,  90, 255);   // 还能猜 / 次数未知
    ImU32 knight_warn    = IM_COL32(255,  70,  70, 255);   // 已经猜满 2 次

    // ================================================================
    // ★ 2026-10-13 新增：把"可以调但没得调"的都补上
    //
    //   动因（用户的真实抱怨）：名字和距离等绘制**字号/颜色调不了** ✗
    //   排查发现两类问题：
    //     ① `dist_size` **声明了、也存进配置了，但绘制时根本没用** ✗
    //        —— 距离文字用的是默认字号
    //     ② 大量绘制点**字号和颜色完全写死** ✗
    //        （道具 / 密码机 / 宝箱怪物 / 预知 / 最后一台）
    //
    //   ☠ 还发现一个真 bug：那些地方写的是
    //        ImGui::CalcTextSize(text, 0, 25)
    //      而 CalcTextSize 的第 3 个参数是 **bool hide_text_after_double_hash**，
    //      不是字号 ✗ `25` 被隐式转成 true，字号参数缺失 -> 用默认值 ✗
    //      正确写法是 font->CalcTextSizeA(size, FLT_MAX, 0.0f, text) ✓
    //      （名字那段本来就是对写法，可作参照）
    // ================================================================

    // 密码机（文字 + 进度条）
    float gen_size       = 25.0f;
    ImU32 gen_color      = IM_COL32(255, 255, 255, 255);   // 距离那一段的颜色
    float gen_bar_w      = 120.0f;                          // 进度条宽
    float gen_bar_h      = 10.0f;                           // 进度条高
    ImU32 gen_bar_bg     = IM_COL32(  0,   0,   0,  90);   // 进度条底槽

    // 道具
    float prop_size      = 25.0f;
    ImU32 prop_color     = IM_COL32(255, 200,   0, 255);

    // 宝箱 / 怪物
    //   ⚠ 这里只加**字号**，不加颜色 ✗
    //     颜色本来就有专门的 g_mark_col_chest / g_mark_col_monster（宝箱怪物分开调），
    //     再加一个"统一覆盖色"只会让人困惑"到底哪个生效" ✗
    //     **能调的就都要真生效** —— 不留假配置 ✓
    float mark_size      = 25.0f;

    // 预知监管 / 准备阶段天赋列表
    float prophet_size   = 25.0f;
    ImU32 prophet_color  = IM_COL32(255,   0,   0, 255);   // 预知那行（原写死红）
    ImU32 prep_color     = IM_COL32(255, 200,   0, 255);   // 准备阶段天赋那行（原写死黄）

    // "最后一台"进度
    float last_gen_size  = 25.0f;
    float last_gen_w     = 160.0f;                          // 进度条宽
    float last_gen_h     = 14.0f;                           // 进度条高

    //   ★★ 2026-10-13 用户反馈两次"名字和天赋中间还是空了很大距离" ★★
    //     我推了两轮公式都说"应该只有 1px"，但用户看到的就是空的 ✗
    //     => **说明我量的那个量不是用户看到的那个量** ✗
    //     我不再猜，直接把"层间距"**做成可调**，默认给 **0**（真正贴死）✓
    //     这样即使我的行高计算和用户实际字体有出入，用户自己也能调到位 ✓
    //     范围给到负数（-20）：允许**重叠**，万一字体度量偏大也能压下去 ✓
    float label_line_gap = 0.0f;   // 名字/天赋/CD 三层之间的间隙（可负=重叠）
    //   ★ 2026-10-13 用户要求：名字**往上移一点**（原来墨迹底边贴死框顶，太近）✓
    //     原话：「现在名字和框离得太近了，一点点就行」
    //   ⚠ 独立成一个配置，**不要**并进 label_line_gap ✗
    //     那个是"层与层之间"的间隙；这个是"名字与框之间"的距离 —— 两回事 ✓
    //     而且只影响名字，不影响天赋/CD 的相对堆叠 ✓
    //   默认 6px：按墨迹算（现在摆法是墨迹对齐）6px 是"一点点"的观感 ✓
    float name_lift = 6.0f;        // 名字的墨迹底边离框顶的距离（px）

    // ★ 2026-10-13 用户要求：地窖的名字/距离**字号与颜色**可调 ✓
    //   ⚠ 开关用**已有的** `show_draw_Cellar` —— 不另造一个 ✗
    //     （我第一版加了 `show_draw_hatch`，发现重复后删掉了：
    //       同一个功能两个开关是最糟的配置 —— 两个都开才显示，
    //       用户会以为"开关坏了" ✗）
    float hatch_size     = 25.0f;
    ImU32 hatch_color    = IM_COL32(255, 215,   0, 255);   // 默认金黄（地窖常用色）

    // ========================================================================
    // ★ 2026-10-13 抗锯齿两项（用户问"抗锯齿怎么调整"）✓
    //   现状：本项目**一个抗锯齿设置都没覆盖**，全是 ImGui 默认值 ✓（查过）
    //   能调的只有三处，这两处是"运行时 + 几乎免费"的 ✓
    //   第三处（字体过采样 OversampleH/V）**不能在运行时改** ✗
    //     它是建图集时的参数，改了必须重建图集 + 重传纹理 ✓
    //     而图集已 16MB（25px 中文 + 两万多字形）-> 加采样内存吃不消 ✗
    // ========================================================================
    //   ① 文字**亚像素定位** —— 针对"移动时名字晃" ✓
    //      ImGui 默认把文字位置截断到整像素（imgui_draw.cpp 里的 IM_TRUNC）：
    //        `if (!(Flags & TextNoPixelSnap)) { x = IM_TRUNC(x); y = IM_TRUNC(y); }`
    //      本意是防抖，但**框位置在半像素边界来回摆时，截断点会在两个整数
    //      之间跳 -> 就是 1px 的抖** ✗
    //      打开 -> 文字按亚像素定位，移动时平滑挪动而不是跳 ✓
    //      代价：静止时字边缘略糊一点点（手机上基本看不出）✓
    //      => 默认 **true**：移动平滑比"静止锐 1px"重要 ✓
    bool  text_subpixel = true;
    //   ② 曲线细分容差 —— 只影响圆/曲线（圆角进度条、胶囊底槽、圆点）✓
    //      **完全不吃图集内存** ✓ 数值越小越圆滑，代价只是顶点数略增 ✓
    //      默认 0.75（ImGui 默认 1.25，收紧一点观感更好）✓
    float curve_tol     = 0.75f;
};
static EspStyle g_esp;
static bool show_draw_secret_mechine = true;//密码机(含破译进度/进度条/最后一台)
// ★ 2026-10-13 用户要求："最后一台"从「加页」页搬进绘制页的「显示密码机」里 ✓
//   所以它现在是一个**独立的 Fold 开关**（和 名字/距离/天赋 同一套交互）✓
//   ⚠ 默认 true —— 保持搬之前的行为，不给老用户带来意外变化 ✗
//     （搬界面不该顺手改默认值：那是两件事，混在一起出问题就查不清 ✗）
static bool show_draw_last_gen = true;
static bool show_draw_Role = false;//角色
static bool show_draw_touch = false;//孽蜥
static bool Debugging = false;//调试
static bool mirror = false;//镜子状态
static bool show_demo_window = false;
static bool show_another_window = false;
static bool show_window = true;  // 音量键控制：音量下=隐藏，音量上=显示
static bool voice = true;
static bool inform_ghost = true; // 显示鬼魂
static bool show_sohook = false;  // 骨骼与进度覆盖层

// ============================================================================
// ★ 2026-10-13：把 game_kernel 的字典交给 CalibProbe ✓
//   为什么用"外部提供"而不是让 CalibProbe 直接 include PySelf.h：
//     PySelf.h 很大，而且里面有 `g_i_ubt` 这类**同名 static** ✗
//     两个头同时进一个翻译单元容易撞名/各自维护一份缓存 ✗
//   这里只读 PySelf 已经解析好的那一份，**不重复解析** ✓
// ============================================================================
namespace CalibProbe {
bool HostModuleDict(uint64_t* out) {
    //   ☠ 第一版只读 `PySelf::g_module_dict` -> 实测**拿到的是 0**（"units_by_type 无效"）✗
    //     原因：那个缓存的解析时机是 PySelf 自己的刷新节奏，
    //     用户按下"开始记录"的那一刻它可能还没解析 / 已被清空（切场景会清零）✗
    //   修法：**没有就现场解析一次** ✓（PySelf::resolve_module 就是干这个的）
    //     解析成功会写进 g_module_dict，同时也帮了 PySelf，没有副作用 ✓
    if (!PySelf::g_module_dict) {
        if (!PySelf::resolve_module()) return false;
    }
    if (!PySelf::g_module_dict) return false;
    *out = PySelf::g_module_dict;
    return true;
}
} // namespace CalibProbe

// ============================================================================
// 自动校准（QTE）—— 数据侦测结果 + 实现
//
// ── 判据怎么来的（全部来自 MCP/探针实测，不是猜的）───────────────────────
//   对象：game_kernel.input，其实是 UIBattle（1430 项字典）
//   字段：
//     qte_start_time   float  本次校准的起始时刻 —— **一变就是新一次校准**
//     qte_alert        bool   指针进入黄区时置 1，然后**锁住**直到本次结束
//     _in_qte          bool   校准流程激活（会连续为 1 很久，不能当单次判据）
//     exce_pos         tuple  (lo, hi) 黄区角度区间，宽度恒为 8°（= 2×exce_half_width）
//     qte_speed        int    120°/秒 -> 8° 的窗口只有 66ms
//
//   实测时序（20ms 采样，每次校准都一样）：
//     a0h0 ×33       校准条还没走到黄区
//     a1h0 ×4~6      alert 刚变 1（你还没按）  ← **就是这一刻**
//     a1h1 ×49       按下去了
//     a0h0 ×59       结束
//   以及"没按"的那次：a1h0 持续 80 个采样(1.6s) 也不回 0 -> alert 是**锁存**的。
//   => 判据不能写 "alert==1 就点"（会连点），必须用 **alert 的上升沿**，
//      并按 qte_start_time 区分是不是新的一次校准。
//
// ── 点击 ────────────────────────────────────────────────────────────────
//   照抄 AutoPallet::do_tap_raw 的三级兜底（虚拟手指 -> KMA 基坐标 -> uinput），
//   那条路在平板上已验证可用。坐标是**屏幕坐标**。
// ============================================================================
namespace QteAuto {

static std::atomic<bool> g_on{false};
static float g_x = 2200.0f;      // 校准按钮的屏幕 X（用户可调）
static float g_y =  900.0f;      // 校准按钮的屏幕 Y
static int   g_hold_ms = 40;     // 按住时长
// ★ 黄区里"点哪一下"：alert 是**指针刚进入黄区**那一刻置位的，
//   所以立刻点会落在黄区最左沿。黄区宽 8°、速度 120°/s -> 窗口 66ms，
//   加一点延迟就能落在黄区中间，判定更稳。
static int   g_delay_ms = 0;     // 额外微调偏移（毫秒）；真正的延迟按黄区宽度/速度自动算
static bool  g_show_point = false; // 叠加层显示触摸点（2026-10-26：默认关，启动时不再有个点停在屏幕上）
//   ★ 验证用：跳过虚拟手指，强制走内核 touch（排查"点了但游戏没吃"）
static std::atomic<int> g_fires{0};
static char  g_status[96] = "未启用";

// ★ 诊断：把自动校准每一步写进 /data/local/tmp/qte_auto.log
//   用户反馈"开关点了没反应"，先用日志看清卡在哪一层：线程 / 模块 / 字段 / 触摸
static int64_t qte_now_ms()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void qte_log(const char *fmt, ...)
{
#if ENABLE_LOG
    //   ★ 统一出口：只写 /data/adb/.d/o.log，跟随 ENABLE_LOG 开关
    va_list ap;
    va_start(ap, fmt);
    FILE *f = fopen(GUARD_APP_LOG(), "a");
    if (f) {
        fputs("[qte] ", f);
        vfprintf(f, fmt, ap);
        fputc(10, f);
        fclose(f);
    }
    va_end(ap);
#else
    (void)fmt;
#endif
}

static void do_tap()
{
    const int slot = 8;   // 与 AutoPallet 的 TOUCH_SLOT_AUTO 同值（自动功能专用 slot）
    const float cx = g_x, cy = g_y;

    //   ★ 2026-10-21 用户要求：触摸顺序改成和「自动坠机」一致，但**paradise 内核优先**。
    //     ① paradise 内核触摸（touch_down/touch_up，必须先转基坐标）
    //     ② 虚拟手指（TouchHelperA uinput）
    //     ③ SimulateTapScreen 最后兜底
    //   ⚠ 原实现是"虚拟手指优先" —— 实测日志里虚拟手指返回 OK，游戏却没吃，
    //     所以内核优先更靠谱。

    // ---- ① paradise 内核触摸 ----
    {
        ::Vector2 bp = Touch::ScreenToBase(cx, cy);
        bool down_ok = touch_down(slot, (int)bp.x, (int)bp.y);
        if (down_ok) {
            usleep(g_hold_ms * 1000);
            touch_up(slot);
            qte_log("[tap] ① 内核触摸 OK slot=%d 屏幕(%.0f,%.0f) 基(%.0f,%.0f) hold=%dms",
                    slot, cx, cy, bp.x, bp.y, g_hold_ms);
            return;
        }
        qte_log("[tap] ① 内核触摸失败 (%.0f,%.0f) 基(%.0f,%.0f)", cx, cy, bp.x, bp.y);
    }

    // ---- ② 虚拟手指 ----
    if (Touch::IsVirtualActive(slot)) Touch::VirtualUp(slot);
    if (Touch::VirtualDown(slot, cx, cy)) {
        usleep(g_hold_ms * 1000);
        Touch::VirtualUp(slot);
        qte_log("[tap] ② 虚拟手指 OK slot=%d (%.0f,%.0f)", slot, cx, cy);
        return;
    }

    // ---- ③ uinput 兜底 ----
    if (Touch::SimulateTapScreen((int)cx, (int)cy, g_hold_ms)) {
        qte_log("[tap] ③ SimulateTapScreen OK (%.0f,%.0f)", cx, cy);
    } else {
        qte_log("[tap] 三条路全失败 (%.0f,%.0f)", cx, cy);
    }
}

static void thread_main()
{
    int64_t i_input = -1, i_in = -1, i_alert = -1, i_st = -1;
    float last_st = -1e9f;
    bool  fired   = false;
    // ★ 诊断：2 秒一条状态，直接回答"为什么开关没效果"
    int64_t dbg_next = 0;
    int     dbg_stage = -1;      // 0=开关关着 1=module拿不到 2=input拿不到 3=字段拿不到 4=正常监听
    qte_log("[init] 自动校准线程已启动（git-free 诊断版）");

    while (true) {
        usleep(20 * 1000);                       // 20ms 一轮：黄区窗口 66ms，够用

        const int64_t nowq = qte_now_ms();
        if (!g_on.load()) {
            fired = false; last_st = -1e9f;
            snprintf(g_status, sizeof(g_status), "开关关着");
            if (nowq >= dbg_next) { dbg_next = nowq + 2000; qte_log("[st] on=0（开关关着，不会做任何事）"); }
            continue;
        }
        if (!PySelf::g_module_dict && !PySelf::resolve_module()) {
            if (nowq >= dbg_next) { dbg_next = nowq + 2000;
                qte_log("[st] on=1 但 game_kernel 字典拿不到: %s", PySelf::status_text()); }
            continue;
        }
        const uint64_t md = PySelf::g_module_dict;
        if (!md) continue;

        i_input = PyCore::find_key(md, "input", i_input);
        if (i_input < 0) {
            if (nowq >= dbg_next) { dbg_next = nowq + 2000;
                qte_log("[st] on=1 但 game_kernel 里没有 input 模块"); }
            continue;
        }
        if (i_input < 0) continue;
        const uint64_t ui = PyCore::get_attr(md, i_input);
        if (!PyCore::is_obj(ui)) continue;
        const uint64_t d = getPtr64(ui - 0x18);
        PyCore::Dict dk;
        if (!PyCore::read_dict(d, dk)) continue;

        i_in    = PyCore::resolve_index(d, dk, i_in,    "_in_qte");
        i_alert = PyCore::resolve_index(d, dk, i_alert, "qte_alert");
        i_st    = PyCore::resolve_index(d, dk, i_st,    "qte_start_time");
        if (i_alert < 0 || i_st < 0) {
            //   ★★ 2026-10-21 根因：PyCore::find_key 有**否定缓存**（"这个名字在这个字典里查不到"
            //     也记下来，省得每帧重扫）。而 qte_start_time 这类属性是**游戏运行中才赋值**的：
            //     开局还没弹过校准条时它确实不存在 -> 被缓存成"永远没有" -> **整局都读不到** ✗
            //     实测日志：`key[1469] qte_start_time` 明明在字典里，代码却一直拿 -1。
            //     修法：一旦没找到就把本线程的否定缓存清掉，下一轮重新按名字扫。
            PyCore::neg_reset();
            // ★ 诊断：input 在，但字段对不上。**把 input 里所有带 qte 的键名打出来**，
            //   这样下一次不用猜字段名（游戏热更/不同模式会换字段）。
            if (nowq >= dbg_next) {
                dbg_next = nowq + 10000;
                qte_log("[st] 字段缺失 alert=%lld start=%lld；input 里带 qte 的键:",
                        (long long)i_alert, (long long)i_st);
                int shown = 0;
                for (int64_t k = 0; k < dk.n_entries && shown < 40; k++) {
                    char nm[64] = {0};
                    if (!PyCore::read_str(getPtr64(PyCore::key_slot(dk, k)), nm, sizeof(nm))) continue;
                    if (strstr(nm, "qte") || strstr(nm, "calib") || strstr(nm, "alert")) {
                        qte_log("    key[%lld] %s", (long long)k, nm);
                        shown++;
                    }
                }
                if (shown == 0) qte_log("    （一个都没有：这一局可能不是修机校准的场景）");
            }
            continue;
        }
        // ★ 诊断：走到这里说明链路通了，只报"有没有在监听 / 触发过几次"
        if (nowq >= dbg_next) {
            dbg_next = nowq + 2000;
            qte_log("[st] 正常监听 on=1 fires=%d status=%s", (int)g_fires.load(), g_status);
        }

        bool alert = false;
        PyCore::read_bool(PyCore::value_slot(dk, i_alert), alert);
        float st = 0.f;
        PyCore::read_number(PyCore::value_slot(dk, i_st), st, -1e9f, 1e9f);

        // 黄区区间 + 指针角速度：用来算"从黄区左沿走到中心要多久"
        float zone_lo = 0.f, zone_hi = 0.f;
        int   zone_speed = 0;
        static int64_t i_exce = -1, i_spd = -1;
        i_exce = PyCore::resolve_index(d, dk, i_exce, "exce_pos");
        if (i_exce >= 0) {
            const uint64_t tp = getPtr64(PyCore::value_slot(dk, i_exce));
            if (PyCore::is_obj(tp)) {
                int64_t sz = 0;
                if (vm_readv(tp + (uint64_t)SecOff::u64(SecOff::DG_02), &sz, 8) && sz >= 2) {
                    PyCore::read_number(tp + (uint64_t)SecOff::u64(SecOff::DG_03), zone_lo, -1e9f, 1e9f);
                    PyCore::read_number(tp + (uint64_t)SecOff::u64(SecOff::DG_04), zone_hi, -1e9f, 1e9f);
                }
            }
        }
        i_spd = PyCore::resolve_index(d, dk, i_spd, "qte_speed");
        if (i_spd >= 0) {
            int64_t v = 0;
            if (PyCore::read_int(getPtr64(PyCore::value_slot(dk, i_spd)), v)) zone_speed = (int)v;
        }

        // 新的一次校准（qte_start_time 变化）-> 重新武装
        if (st != last_st) {
            last_st = st;
            fired   = false;
        }
        // 上升沿：alert 置位 == 指针刚进入黄区（最左沿）
        if (alert && !fired) {
            fired = true;
            // ★ 延迟按"黄区宽度 / 指针速度"自动算，不是写死常数：
            //     宽度 = hi - lo（实测恒 8°），速度 = qte_speed（实测 120°/s）
            //     -> 半区时长 ≈ 33ms，即"黄区中心"
            //   alert 的上升沿实测落在左沿（拟合斜率 ≈ 1/速度），
            //   所以这个延迟与黄区在条上的位置无关，位置随机也能自动对准中心。
            int delay_ms = 30;
            const float width = zone_hi - zone_lo;
            if (zone_speed > 0 && width > 0.1f) {
                delay_ms = (int)((width * 0.5f) / (float)zone_speed * 1000.0f + 0.5f);
                if (delay_ms < 0)   delay_ms = 0;
                if (delay_ms > 150) delay_ms = 150;
            }
            delay_ms += g_delay_ms;              // 用户微调（可为负）
            qte_log("[fire] 黄区 %.2f..%.2f 速度=%d 延迟=%dms 点位=(%.0f,%.0f)",
                    zone_lo, zone_hi, zone_speed, delay_ms, g_x, g_y);
            if (delay_ms > 0) usleep(delay_ms * 1000);
            do_tap();
            const int n = ++g_fires;
            snprintf(g_status, sizeof(g_status), "已点击 %d 次 (%dms)", n, delay_ms);
        }
    }
}

// 供 drawBegin 每帧同步开关用（g_on 是内部 static，外部拿不到）
void SetOn(bool on) { g_on.store(on); }

inline void EnsureStarted()
{
    static std::once_flag once;
    std::call_once(once, []() { std::thread(thread_main).detach(); });
}

} // namespace QteAuto

static std::atomic<bool> g_umbrella_dump_request{false};
static bool g_umbrella_dump_active = false;
static double g_umbrella_dump_start = 0.0;
static int g_umbrella_dump_frame = 0;

// ← 新增
static uint64_t g_umbrella_charge_start = 0;
static float    g_umbrella_charge_ratio = 0.0f;
static bool     g_umbrella_charging     = false;
static float    g_umbrella_start_x      = 0.0f;   // 蓄力起点世界坐标(x)
static float    g_umbrella_start_z      = 0.0f;   // 蓄力起点世界坐标(水平 z)
static bool     g_umbrella_start_valid  = false;
static const float kUmbrellaMaxMeters   = 76.0f;  // 满蓄力从起点算的传伞距离(米)
static const float kUmbrellaStartOffset = 6.0f;   // 起点在自身身前多少米
static const int   kUmbrellaFullMs      = 4400;   // 满蓄力时长(ms)

static MyUI::AppState g_ui_state;

// ==================== 小地图状态 ====================
static bool  g_minimap_enabled   = true;
static bool  g_minimap_edit      = false;      // 调整模式（可拖动）
static bool  g_minimap_show_name = true;
static float g_minimap_x         = 20.0f;
static float g_minimap_y         = 300.0f;
static float g_minimap_w         = 260.0f;
static float g_minimap_h         = 260.0f;
static float g_minimap_zoom      = 0.5f;       // 像素/世界单位
static bool  g_minimap_auto_self = true;   // 自动判定自己
static int   g_minimap_self_num  = 2;      // 手动指定自己的编号 1~4
static bool  g_minimap_dragging  = false;
static float g_minimap_drag_dx   = 0.f;
static float g_minimap_drag_dy   = 0.f;
// ==================== 模仿者身份悬浮窗 ====================
// 需求(2026-10-10)：把 1~12 号的号+身份做成一个**透明背景**的悬浮窗贴在屏幕上，
// 一排一个或多个、整体向下排；活着用他身份自己的阵营色，死了转灰并标「已死」。
// 位置/大小都用「绘制」页的滑块调，所以这里只保存数值。
//   x/y 是**屏幕绝对像素**，滑块范围 ±3000 —— 刻意给到屏幕外，
//   这样用户可以把它推到任何角落(甚至暂时推出画面)而不必改成相对坐标。
static bool  g_ccwin_show = false;
static float g_ccwin_x    = 40.0f;
static float g_ccwin_y    = 320.0f;
static float g_ccwin_size = 26.0f;   // 字号，等同于"整体大小"
static float g_ccwin_cols = 1.0f;    // 一排几个(1~3)，取整用

// ==================== 宝箱/怪物标记 ====================
static bool  g_mark_enabled        = true;
static char  g_mark_kw_chest[64]   = "box";       // 宝箱关键词，用户自己调
static char  g_mark_kw_monster[64] = "npc";       // 怪物关键词
static ImU32 g_mark_col_chest      = IM_COL32(255, 215,   0, 255);  // 金色
static ImU32 g_mark_col_monster    = IM_COL32(255,  60,  60, 255);  // 红色
static float g_mark_max_dist       = 150.f;       // 超过这个距离不画
// ==================== 场景 Dump ====================
static std::atomic<bool> g_scene_dump_mode{false};   // 打开后收进 data[] 的对象不再过滤
static std::atomic<bool> g_scene_dump_save{false};   // 点一下保存当前 data[] 到文件

static bool g_dark_theme = false;
// 系统高斯模糊强度（1~500）。由 SurfaceFlinger 的 SetBackgroundBlurRadius 使用，
// 数值越大越糊。0 = 关闭模糊层。见 ui_kit / Blurred.cpp。
int g_blur_strength = 90;
// 动画速度倍率（百分比）。100 = 正常。调低可以放慢所有过场，
// 用来观察动效细节（调试用），也可以给喜欢慢节奏的人用。
int g_anim_scale = 100;

// 主题强调色（0 = 用主题自带的琥珀色）。
// 在设置页用取色块改，会同时改掉 accent / accentHi / accentSoft 和上面的文字色。
int g_theme_accent = 0;

// ============================================================================
// 诊断埋点：最近一次"自动技能 / 自动盖板"实际发出的点击
//   AI 可以通过 MCP 读这几个键，确认点击落在哪、驱动有没有接受。
//   tap_x/y = -1 表示一次都没发出去过。
// ============================================================================
int g_diag_tap_x     = -1;
int g_diag_tap_y     = -1;
int g_diag_tap_ok    = -1;   // KMA touch_down 的返回值
int g_diag_tap_kind  = 0;    // 0=无 1=技能 2=飞轮
int g_diag_tap_count = 0;    // 累计发送次数

// ============================================================================
// ★★ 2026-10-13 新增：**框上文字的实际屏幕 Y 坐标**（诊断用，只读）★★
//   用户反馈"名字和天赋中间空档还是很大"，我推了两轮公式都说只有 ~1px ✗
//   推不出来就别推了 —— 直接把**每帧实际用的坐标**记下来，
//   再用 MCP 读，和屏幕上的像素对照，就能知道到底是谁离得远 ✗
//
//   记录的是**最后一个被画的实体**（通常就是玩家自己/最近的目标）✓
//   只读，不参与任何绘制逻辑 -> 加它不会改变画面 ✓
// ============================================================================
float g_diag_lbl_name_y  = -1.f;   // 名字的 Y（文字顶部）
float g_diag_lbl_gen_y   = -1.f;   // 天赋的 Y
float g_diag_lbl_cd_y    = -1.f;   // 技能CD 的 Y
float g_diag_lbl_y1      = -1.f;   // 那个实体的框顶 Y1（用来算离框多远）
float g_diag_lbl_nlh     = -1.f;   // 量出来的名字行高
float g_diag_lbl_glh     = -1.f;   // 量出来的天赋行高
float g_diag_lbl_gap     = -1.f;   // 当前层间距配置值
//   ★ 墨迹度量（修空档的关键数据）✓
float g_diag_lbl_ink_off = 0.f;    // 字形顶相对行盒顶的偏移（负）
float g_diag_lbl_ink_h   = 0.f;    // 字形真实墨迹高
//   ⚠ 三层**字号不同 -> 墨迹偏移也不同** ✗
//     我第一版诊断对天赋/CD 也用了"名字的"偏移 -> 算出的空隙是假的（5.7px）✗
//     实测：名字 26.66 / 天赋 25 / CD 25，三者的 off 和 h 都不一样 ✓
//     所以**每层各记一份** ✓
float g_diag_lbl_g_off   = 0.f;    // 天赋层的墨迹偏移
float g_diag_lbl_g_h     = 0.f;    // 天赋层的墨迹高
float g_diag_lbl_c_off   = 0.f;    // CD 层的墨迹偏移
float g_diag_lbl_c_h     = 0.f;    // CD 层的墨迹高

// ---------------------------------------------------------------------------
// 测试探针：走**和自动技能完全相同**的 KMA 路径点一下屏幕。
//   存在的意义：当"驱动接受了但游戏没反应"时，用它手动发一次，
//   就能区分【坐标不对】和【注入没送达】—— 不用再猜。
//   MCP:  {"cmd":"dwrg_tap","probe":"flywheel"}  或  {"cmd":"dwrg_tap","x":1584,"y":972}
// ---------------------------------------------------------------------------
bool TapScreen(int x, int y, int hold_ms) {
    if (hold_ms < 10) hold_ms = 10;
    const int SLOT = 9;                       // 和自动技能(6/7)、盖板(8) 都分开
    // 入参是**屏幕坐标**，这里换成基坐标再下发（和自动技能同一条路）
    ::Vector2 bp = Touch::ScreenToBase((float)x, (float)y);
    bool ok1 = touch_down(SLOT, (int)bp.x, (int)bp.y);
    usleep((useconds_t)hold_ms * 1000);
    bool ok2 = touch_up(SLOT);
    LOG("[探针] 屏幕(%d,%d) -> 基坐标(%d,%d) 按住%ddms down=%d up=%d\n",
        x, y, (int)bp.x, (int)bp.y, hold_ms, ok1 ? 1 : 0, ok2 ? 1 : 0);
    if (ok1 && ok2) return true;
    bool ok3 = Touch::SimulateTapScreen(x, y, hold_ms);
    LOG("[探针] KMA 失败，uinput=%d\n", ok3 ? 1 : 0);
    return ok3;
}

float z_x, z_y, z_z, d_x, d_y, d_z, camera, r_x, r_y, r_w;
float X1,Y1,X2,Y2,W,H,MIDDLE,TOP,BOTTOM;
float dist;   // 2026-09-23: int -> float。以前的截断迫使两处亚米级判断另外重算一遍
char objtext[256];
//char content[1024];
char Team[1024];
char Name[1024];
char prophet_text[1024];

float px,py;
Vector3A D,Z;


ImColor color_red = ImColor(255,0,0,255);
ImColor color_green = ImColor(0,255,0,255);
ImColor color_blue = ImColor(0,0,255,255);
ImColor color_yellow = ImColor(255,255,0,255);
ImColor color_purple = ImColor(255,0,255,255);
ImColor color_black = ImColor(0,0,0,255);
ImColor BoneColor = ImColor(255,0,0,255);
ImColor BotBoneColor = ImColor(255,255,255,255);
int read_state = 0;
// read_state、data_count 和正在展示的实体快照统一使用 g_data_mutex 发布/读取。
static inline void SetReadState(int value)
{
    std::lock_guard<std::recursive_mutex> data_lock(g_data_mutex);
    read_state = value;
}
static inline void ClearPublishedDataAndSetReadState(int value)
{
    std::lock_guard<std::recursive_mutex> data_lock(g_data_mutex);
    data_count = 0;
    read_state = value;
}
static inline void PublishScanMeta(uintptr_t array_addr, uintptr_t count, int state)
{
    std::lock_guard<std::recursive_mutex> data_lock(g_data_mutex);
    Arrayaddr = array_addr;
    Count = count;
    read_state = state;
}
bool first_frame_logged = false;
bool first_matrix_logged = false;
char extractedString[64];
long int MatrixOffset = 0,ArrayaddrOffset = 0;
typedef struct {
    unsigned long addr;
    unsigned long taddr;
} ModuleBssInfo;


ModuleBssInfo get_module_bss(int pid, const char *module_name) {
    // This helper also inspects the published entity table. It can run after
    // other reader workers have started, so use the same mutex as publishers.
    std::lock_guard<std::recursive_mutex> data_lock(g_data_mutex);
    FILE *fp;
    ModuleBssInfo info = {0, 0};
    char filename[64];
    char line[1024];

    // 生成文件名
    snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);

    // 打开文件
    fp = fopen(filename, "r");

    bool found_module = false;

    if (fp!= NULL) {
        while (fgets(line, sizeof(line), fp)) {
            // 先判断是否包含模块名
            if (strstr(line, module_name)!= NULL) {
                found_module = true;
            }

            if (found_module) {
                // 检查是否满足rw权限且行长度符合要求
                long addr,taddr;
                sscanf(line, "%lx-%lx", &addr, &taddr);
                if (strstr(line, "rw")!= NULL && strlen(line) < 86 &&(taddr-addr)/4096>=2800) {
                //printf("%d", (taddr-addr)/4096);
                
                    // 将行按空格分割成字符串数组（这里简单示意，实际可能需要更完善的分割函数）
                    char *words[10];
                    int numWords = 0;
                    char *token = strtok(line, " ");
                    while (token!= NULL && numWords < 10) {
                        words[numWords++] = token;
                        token = strtok(NULL, " ");
                    }

                    // 遍历分割后的字符串数组，查找地址范围并转换
                    for (int i = 0; i < numWords; i++) {
                        if (sscanf(words[i], "%lx-%lx", &info.addr, &info.taddr) == 2) {
                            fclose(fp);
                            return info;
                        }
                    }

                    // 如果未找到正确格式的地址范围，设置为0并返回
                    info.addr = 0;
                    info.taddr = 0;
                    fclose(fp);
                    return info;
                }
            }
        }

        fclose(fp);
    }

    return info;
}

ModuleBssInfo get_module_bssgjf(int pid, const char *module_name) {
    FILE *fp;
    ModuleBssInfo info = {0, 0};
    long addr,taddr;
    char *pch;
    char filename[64];
    char line[1024];
    snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);
    fp = fopen(filename, "r");
    bool is = false;
    if (fp!= NULL) {
        while (fgets(line, sizeof(line), fp)) {
        sscanf(line, "%lx-%lx", &addr, &taddr);
            if (strstr(line, module_name) &&strstr(line, "r-xp")!= NULL &&(taddr-addr)== 114982912) {
                is = true;
            }
            if (is) {
                if (strstr(line, "rw")!= NULL &&!feof(fp) && (strlen(line) < 86)) {
                long addr,taddr;
                sscanf(line, "%lx-%lx", &addr, &taddr);
                if ((taddr-addr)/4096<=3000)
                continue;
                    if (sscanf(line, "%lx-%lx", &info.addr, &info.taddr)!= 2) {
                        // 处理转换失败的情况
                        info.addr = 0;
                        info.taddr = 0;
                        break;
                    }
                    break;
                }
            }
        }
        fclose(fp);
    }
    return info;
}
int get_name_pid1(const char *packageName) {
    int id = -1;
    DIR *dir;
    FILE *fp;
    char filename[64];
    char cmdline[64] = {};
    struct dirent *entry;
    dir = opendir("/proc");
    if (dir == NULL) {
        return -1;
    }
    while ((entry = readdir(dir))!= NULL) {
        id = atoi(entry->d_name);
        if (id!= 0) {
            //   ★ 2026-10-13：走 GUARD_STR（发布要求零明文锚点）✓
            //     ⚠ 这里必须用 "%s" 把格式串传进去，不能直接拼字面量 ✗
            //       GUARD_STR 返回运行期解密的串，编译器无法校验格式符 ——
            //       直接当格式串用会触发 -Wformat-security（已全局关掉），
            //       但更稳的写法是显式 "%s" ✓
            sprintf(filename, GUARD_STR("/proc/%d/cmdline"), id);
            fp = fopen(filename, "r");
            if (fp) {
                char *readResult = fgets(cmdline, sizeof(cmdline), fp);
                fclose(fp);
                if (readResult != NULL &&
                    (strstr(cmdline, packageName) != NULL || strstr(cmdline, "com.netease.idv") != NULL) &&
                    strstr(cmdline, "com") != NULL && strstr(cmdline, "PushService") == NULL &&
                    strstr(cmdline, "gcsdk") == NULL) {
                    sprintf(extractedString, "%s", cmdline);
                    closedir(dir);
                    return id;
                }
            }
        }
    }
    closedir(dir);
    return -1;
}
long getModuleBasegjf(int pid, const char *module_name) {
    FILE *fp;
    long addr,taddr;
    char *pch;
    char filename[64];
    char line[1024];
    snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);
    fp = fopen(filename, "r");
    bool is = false;
    if (fp!= NULL) {
        while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "r-xp")!= NULL &&!feof(fp) && strstr(line, module_name)) {
                sscanf(line, "%lx-%lx", &addr, &taddr);
                    if ((taddr-addr)== 114982912) {
                        // 处理转换失败的情况
                        fclose(fp);
                        return addr;
                        break;
                    }
                    //break;
                }
            
        }
        fclose(fp);
    }
    return 0;
}

int c;
// char libso[256] = {"libclient.so"};  ← 数组拷贝初始化，-sobf 不覆盖，明文残留
// 改为运行时拼接：静态扫描看不到 "libclient.so" 这个整体（2026-10-22）
static const char* libso_name() {
    static thread_local char buf[24];
    snprintf(buf, sizeof(buf), "%s%s", GUARD_STR("libclient"), GUARD_STR(".so"));
    return buf;
}

// ==================== 过滤表 ====================
static const char* g_filter_keywords[] = {
    "creature",
    "dm65_survivor_girl_page",
    "skill_hudie",
    "h55_joseph_camera",
    "burke_console",
    "redqueen_e_heijin_yizi",
    "qiutu_box",
    "weapon",
    "nvyao"
};
static constexpr int g_filter_count = sizeof(g_filter_keywords) / sizeof(g_filter_keywords[0]);
inline bool should_filter(const std::string& name) {
    for (int i = 0; i < g_filter_count; ++i) {
        if (name.find(g_filter_keywords[i]) != std::string::npos)
            return true;
    }
    return false;
}

// ================== 幽灵/隐身状态判定(合并原有特殊场景排除) ==================
// +0x70 == 0x1000000 且 +0x1a0 == 450.0 才是"正常在场"的真实角色/道具;
// 其余取值(包括65150鬼魂视角等)统一视为幽灵态, 由 inform_ghost 决定是否仍然显示。
// 方框配色也用这一个判据 —— 热更后鬼魂的 +0x70 不再是 65150, 只认 65150 会让鬼魂框不变白。
bool IsGhostEntity(const DataStruct& obj) {
    int checkVal = getDword(obj.obj + (uint64_t)SecOff::u64(SecOff::DG_05));
    float checkFloat = getFloat(obj.obj + (uint64_t)SecOff::u64(SecOff::DG_06));
    return (checkVal != 0x1000000 || checkFloat != 450.0f);
}

// 空槽位过滤的安全网计数（draw_Gui 每帧调用 ShouldSkipEntityResetFrame 置 -1 = 未统计）
static int  s_nz_surv = -1;
static void ShouldSkipEntityResetFrame() { s_nz_surv = -1; }
// ★ 诊断计数（2026-10-12）：确认空槽过滤到底有没有生效
static int  g_dbg_camp2_seen = 0;    // ShouldSkipEntity 一共见过多少 camp2
static int  g_dbg_camp2_skip = 0;    // 其中被当作空槽跳过的
static int  g_dbg_data_camp2 = 0;    // data[] 里 camp2 的条数（每帧统计）

bool ShouldSkipEntity(const DataStruct& obj) {
    // ★ 未激活的备用槽位不画（2026-10-12 用户反馈：开局时"没有实体的框"被画成绿色）
    //   实测依据：这类实体坐标恒为 (0,0)（mirror_watch 100 帧全程不动），
    //   是引擎预分配、还没被派去当镜像的空槽；真人坐标永远是几百量级，
    //   不可能正好是 (0.0, 0.0) -> 这个判据不会误伤真人。
    //   安全网（上次"一个人都不画"的教训）：只有本帧确实存在至少一个
    //   "坐标非 (0,0) 的 camp2 实体"时才过滤，否则一律不过滤。
    //   ⚠ 修正（2026-10-12）：空槽有两种形态 ——
    //     ① objcoor 指针本身是 0（mirror_watch 里读出来是 (0,0) 的真身）
    //     ② objcoor 有效但坐标恰好是 (0,0)
    //   上一版只判了 ②，所以 ① 那批照样被画成绿框 ✗（用户反馈没修好）。
    // ★★ 镜像槽不画（2026-10-12 用户描述 + 数据双重确认）
    //
    //   用户描述：「每个求生有两个框；刚开局那两个框在**求生刷新出来的地方**，
    //             拍照瞬间则移到求生当时的位置」
    //   实测数据：camp2 实体成对出现，**`+0x1a0` 一个是 450.0、一个是 0.0**：
    //       2  1  ( 355.10, 240.69)  450.0   h55_survivor_m_qiutu.gim   <- 真人
    //       2  1  (   0.00,   0.00)    0.0   h55_survivor_m_qiutu.gim   <- 镜像槽
    //
    //   ⚠ 之前两版都失败的根因：我拿 `objcoor + 0xa0` 当坐标判空 ✗，
    //     而**绘制用的是另一个来源（D.X/D.Y）** ✗ —— 镜像槽的 D 是"出生点"不是 (0,0)，
    //     所以坐标判据永远命中不了。这次改用数据里直接确认过的 `+0x1a0 == 0` ✓
    //
    //   安全网：只有本帧确实存在至少一个 `+0x1a0 != 0` 的 camp2 实体时才过滤 ✓
    //   ★ 最终判据（2026-10-12，用户需求没开镜像时不画镜像框）：
    //     未激活的镜像槽 = 该 camp2 实体**静止 >= 5 秒**，且**存在同角色且在动的伙伴**。
    //     第二条排除了真人蹲点（蹲点的人没有第二条自己在跑）✓
    //     拍照瞬间镜像被瞬移 -> 静止计时清零 -> 立刻显示 ✓
    //     镜像结束后它又静止 -> 5 秒后自动消失 ✓
    if (obj.camp == 2 && obj.obj) {
        g_dbg_camp2_seen++;
        // ★★ 镜像槽不画（2026-10-12 最终方案，判据实测确认）
        //   用户从界面上看出来的：镜像的天赋显示带 "?"（= GENIUS_PARTIAL）
        //   实测（fn=unit_genius）：4 个真人 COMPLETE、4 个镜像 PARTIAL，完美二分
        //   本质：镜像是个半成品拷贝，游戏没给它天赋数据 ✓
        // ★★ 镜像不画（2026-10-12 最终方案 = 用户从截图上看出来的"天赋带问号"）
        //
        //   截图证据：同一角色两个框 ——
        //     [囚徒] 16m  双弹大心脏？  <- ★ 带问号 = 镜像
        //     [囚徒]  3m  双弹大心脏    <- 无问号 = 真人
        //   代码依据：镜像是个半成品拷贝，游戏没给它天赋数据 ->
        //             读不到逐单位天赋 -> GENIUS_PARTIAL -> 显示层补 "?"
        //   实测（fn=unit_genius）：4 真人 COMPLETE / 4 镜像 PARTIAL，完美二分 ✓
        //   ⚠ 查找必须用 scene_obj（实测 data[i].obj == PyGenius.scene_obj ✓）
        //   ★ 最终行为（2026-10-12 用户选定方案②）：
        //     **镜像永远画，用镜像色** ✓ —— 不再做"开镜像时才显示"的过滤 ✗
        //     原因：17 次尝试证明"镜像世界是否开启"这个状态在单位数据里读不到 ✗
        //     （+0x70 / +0x1c0 / 坐标 / +0x100 / +0x108 / +0x1c8 全试过，都不随开关变）
        //     代价：没开镜像时也会多一个镜像框 —— 但它颜色不同，不会和真人混淆 ✓
        //   （原来这里的"隐藏"分支已删除）
        //
        //   对照 fn=unit_genius（PyGenius 单位表）与 fn=mirror_watch（data[]）：
        //     PyGenius.scene_obj      data[].obj        f1a0      结论
        //     0x73bc0e6270  state=2   0x73bc0e6270   450.0     <- 完全一致 = 真人 ✓
        //     0x73bc0cdef0  state=2   0x73bc0cdef0   450.0     <- 真人 ✓
        //     0x73bc0e8930  state=2   0x73bc0e8930   450.0     <- 真人 ✓
        //     （无）                  0x73bc0ce8a0   0.0       <- ★ 空槽，不在 PyGenius 里
        //     （无）                  0x73bc0ccb90   0.0       <- ★ 空槽
        //   结论：**data[i].obj 就是 PyGenius 的 scene_obj** ✓
        //         真人 f1a0=450.0 且在 PyGenius 表里；空槽 f1a0=0.0 且不在表里 ✓
        //   所以：camp2 且 f1a0==0 -> 空槽 -> 不画 ✓
        //   安全网：本帧确实存在 f1a0!=0 的 camp2 时才过滤 ✓
        // （旧的 f1a0==0 空槽判据已由上方的 PyGenius 镜像判据取代）
    }

    // 这几类名字命中即直接跳过, 与幽灵判定无关(原本散落在渲染循环里, 现在收拢到一处)
    // ★★ 约瑟夫相机（2026-10-12 找到真正原因）：
    //   这条是**无条件跳过** ✗ —— 所以相机**永远**画不出来，开关根本管不到。
    //   （之前我在下面 is_ghost_obj 那里做的豁免是无效的，因为根本走不到那儿。）
    //   现在改成受开关控制：开关关掉才跳过。
    if (strstr(obj.class_name, "h55_joseph_camera") != NULL) return !show_draw_joseph_cam;
    // 红夫人"准备放镜"的预览模型：始终跳过。
    //   fx\model\redqueen_mirror_model_obj_001.gim，常驻复用同一个对象，只在准备阶段可见。
    //   ⚠ 注意别把 redqueen_mirror.gim 也跳了 —— **那个才是镜像本身**，要画。
    if (strstr(obj.class_name, "redqueen_mirror_model") != NULL) return true;
    if (strstr(obj.class_name, "burke_console") != NULL) return true;        // 疯眼场景
    if (strstr(obj.class_name, "chr\\guajian") != NULL) return true;
    if (strstr(obj.class_name, "girl_e_sj_zuoyi") != NULL) return true;
    if (strstr(obj.class_name, "h55_survivor_w_shangren_tiaoban") != NULL) return true; // 商人跳板
    if (strstr(obj.class_name, "trap.gim") != NULL) return true;   // 鹿头夹子

    // 废弃模型(形态切换后留在数组里的旧形态)。这条取代了原来那份
    // "红蝶/无常/歌剧/破轮/木偶/冒险家" 的类名黑名单 —— 那份是按角色名猜的,
    // 漏一个角色就漏一个, 而且只在 inform_ghost 打开时才生效。
    // 现在直接问引擎: unit.another_model + 0x20 就是废弃形态的场景对象指针,
    // 精确、跟视角无关、不用维护名单。详见 PySelf.h 文件头。
    //   ★★ 2026-10-21 台球手变球：**正在渲染的玩家单位不许被"残留"判据剔除** ★★
    //     实测：变球后
    //       旧人形对象  is_stale_model=1、has_scene=0
    //       球的模型    is_stale_model=0、has_scene=0
    //     两条"残留/废弃"判据各命中一次 -> 整只不画（用户报的"变球就不绘制"）。
    //     真正的残留（约瑟夫相中世界那批）**是不渲染的**（IsGhostEntity=true），
    //     所以把这两条判据都加上"且当前没在渲染"这个前提。
    if (PySelf::is_stale_model(obj.obj) && IsGhostEntity(obj)) return true;

    bool is_ghost_obj = IsGhostEntity(obj);

    // 注意 +0x70 就是 NeoX 模型对象的 visible, 是**渲染剔除的结果**:
    // 废弃模型恒不可见, 但远处的真实对象同样不可见。所以它只能当"要不要按幽灵显示"的开关,
    // 绝不能当存在性/真假判据 —— 那会重演"求生者只看得到身边的密码机"。
    // ★ 约瑟夫相机豁免幽灵过滤：相机是相中世界的物体，本身满足 IsGhostEntity，
    //   一旦「显示灵魂」关着就被无条件跳过 —— 那样相机开关根本管不到。
    //   相机有自己的开关，不该受灵魂开关影响。
    // ★★ 2026-10-20：**玩家单位（求生/监管）同样豁免** ★★
    //   根因（实测数据）：约瑟夫的相中世界不是第二个 world，而是把两套单位放进
    //   同一个 units_by_type（一局 8 个求生者 = 现实 4 + 相中 4，坐标几乎重合）。
    //   ESP 其实把 9 个单位全都枚举进来了（debug: other_n=9 / data_count=54 /
    //   全部 in_data=1），但另一套在当前世界**不渲染** -> IsGhostEntity = true
    //   -> 「显示灵魂」关着时被这条规则整批跳过 -> 表现为"看不到另一个世界的人"。
    //   玩家单位就是玩家，不该由灵魂开关决定画不画。
    // ★★ 2026-10-20：相中世界的**残留实体**要清掉 ★★
    //   上面豁免了玩家单位的灵魂过滤（否则另一世界的人画不出来），
    //   但代价是：相中世界结束后，那批镜像实体仍会被场景扫描读到（data[] 里还在），
    //   于是框会永久留在场上。
    //   权威判据：units_by_type —— 世界结束后游戏会把它们从单位表里删掉。
    //   has_scene() 在表为空时返回 true（不过滤），所以开局不会把所有人吞掉。
    if ((obj.camp == 2 || obj.camp == 1) && !PyGenius::has_scene(obj.obj)
        && IsGhostEntity(obj))                   // 正在渲染的玩家（含台球形态）不算残留
        return true;

    // ★★ 镜像副本只在"相中世界生效"时显示 ★★
    //   相中世界结束后那批单位（uid 1000236~ 那一段）**仍留在 units_by_type 里**，
    //   所以上面那条 has_scene 判据对它们恒为 true，框会一直残留。
    //   实测（MCP dict_at）：镜像副本 out_photo_uid != None；
    //   相中世界生效时 in_photo_world 是 world 对象，关闭后是 None。
    //   ⚠ 判 None 不能用 is_obj（Py_None 的地址正落在 is_obj 区间内），
    //     PyGenius 里改用"能否 read_int / read_dict 解析成功"。
    //   ★ 最终判据（不依赖"相中世界是否开启"—— 那个状态在单位数据里读不到）：
    //     活着的单位每帧都被 tick（frame_id 递增）；相中世界结束后镜像单位不再被 tick，
    //     frame_id 停住 -> 超过 2 秒判为残留，不画。
    //   镜像副本（out_photo_uid != None）只在相中世界生效时显示。
    //   "生效"判据 = UIBattle.in_photo（MCP 实测会 True/False 翻转；
    //   而 in_photo_world 即使开启也是 None，不可用）。
    if ((obj.camp == 2 || obj.camp == 1)
        && PyGenius::is_photo_copy(obj.obj)
        && !PyGenius::g_photo_active)
        return true;
    if (is_ghost_obj && !inform_ghost
      && obj.camp != 2 && obj.camp != 1
      && !strstr(obj.class_name, "h55_joseph_camera"))   // 数组恒非空，无需判
      return true;
    return false;
}

// ============================================================================
// 2026-10-28：**body 集合命中率保护**
// ----------------------------------------------------------------------------
//   背景（实测铁证，来自 share.log 和 o.log）：
//     换局后 units_by_type 读不出（`get_type_list=0 且 units_by_type 也读不出`），
//     但 PySelf::body_set_ready() 仍返回 true（它看的是"上次成功过"），
//     于是 is_body() 对**整局实体**全部返回 false。
//     后果：Draw_Main 主循环和 ShareMapBuildJson 都把所有人过滤掉：
//       [绘制] 每帧: ... | 遍历=20 画出=0        ← ESP 一个人都不画
//       BuildJson ok total=969 svs_n=5 hunter=1   ← 网页只能看到监管
//
//   本函数每 200ms 统计一次命中率（< 30% 视为 body 集合不可靠）：
//     正常局：camp1/2 的 player/boss 对象里至少 80% 命中 is_body
//              （模型 + 备用形态两条 record，至少一条必命中）
//     换局瞬间：0%
//   30% 是留了两倍余量的阈值。
//
//   ★ 两个调用点（Draw_Main / ShareMapBuildJson）共用这一份判断，
//     保证"本地画多少 = 网页显示多少"，不会再出现两边不一致。
// ============================================================================
static bool ShareMapBodyFilterShouldSkip()
{
    std::lock_guard<std::recursive_mutex> data_lock(g_data_mutex);
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const int64_t now_ms = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    static int64_t s_last_ms = 0;
    static bool    s_skip    = false;
    if (now_ms - s_last_ms < 200) return s_skip;   // 200ms 内复用
    s_last_ms = now_ms;

    if (!PySelf::body_set_ready()) { s_skip = false; return false; }

    int total = 0, hit = 0;
    const int lim = (data_count > 0 && data_count < 3000) ? data_count : 0;
    for (int i = 0; i < lim; ++i) {
        if (data[i].camp != 1 && data[i].camp != 2) continue;
        if (!strstr(data[i].class_name, "player") &&
            !strstr(data[i].class_name, "boss")) continue;
        total++;
        if (PySelf::is_body(data[i].obj)) hit++;
    }
    // 没有可判对象时不动（交给别的判据）
    // 命中率 < 30% -> body 集合不可靠 -> 本帧跳过 body 过滤
    s_skip = (total > 0 && hit * 100 < total * 30);
    return s_skip;
}

void read_thread(long int PD1,long int PD2,long int PD3)
{
    // ★ 2026-10-21 用户要求"框要跟得上"：扫描线程给最高优先级，
    //   宁可抢渲染的 CPU，也不要让实体位置比画面晚一帧。
    // 备选读取路径自查：主路径连续读到残缺数据时，切到 decoy.h 那套更直白的
    // 实现反而能出数（个别机型）。这里每 30 轮看一次开关，未启用时立即返回。
    {
        static int sAltTick = 0;
        if (++sAltTick >= 30) {
            sAltTick = 0;
            AltRead::RunOnce((uint64_t)PD1, (uint64_t)PD2);
        }
    }

    setpriority(PRIO_PROCESS, 0, -20);
    {
        struct sched_param sp;
        memset(&sp, 0, sizeof(sp));
        sp.sched_priority = 80;
        sched_setscheduler(0, SCHED_FIFO, &sp);   // 失败无妨（非 root/无权限会被拒）
    }
    bool waitingLogged = false;
    while (pid <= 0) {
        pid = get_name_pid1("dwrg");
        if (pid > 0) break;
        if (!waitingLogged) {
            printf("[进程] 等待游戏进程启动\n");
            waitingLogged = true;
        }
        sleep(1);
    }
    driver->initialize(pid);
    printf("[进程] 已获取游戏进程 PID=%d 包名=%s\n", pid, extractedString);

    ModuleBssInfo result;
    // libbase 统一从maps取, 不用内核ioctl
    while (libbase == 0) {
        char mappath[64];
        snprintf(mappath, sizeof(mappath), "/proc/%d/maps", pid);
        FILE *fp = fopen(mappath, "r");
        if (fp == NULL) {
            printf("[进程] 无法打开 %s，重新等待游戏进程\n", mappath);
            pid = -1;
            while (pid <= 0) {
                sleep(1);
                pid = get_name_pid1("dwrg");
            }
            driver->initialize(pid);
            continue;
        }
        char line[1024];
        bool is_official = strstr(extractedString, "com.netease.idv") != NULL;
        while (fgets(line, sizeof(line), fp)){
            long a, t;
            if (sscanf(line, "%lx-%lx", &a, &t) != 2) continue;
            if (libbase == 0 && strstr(line, "r-xp")){
                if (is_official && strstr(line, "."))    libbase = a;
                if (!is_official && strstr(line, libso_name())) libbase = a;
            }
        }
        fclose(fp);
        if (libbase == 0) {
            printf("[基址] 暂未找到游戏模块，1秒后重试\n");
            sleep(1);
        }
    }
    if (strstr(extractedString, "com.netease.idv") != NULL)
        result = get_module_bssgjf(pid, ".");
    else
        result = get_module_bss(pid, libso_name());
    printf("[基址] libbase=0x%llX BSS=0x%llX-0x%llX\n",
           (unsigned long long)libbase, (unsigned long long)result.addr, (unsigned long long)result.taddr);

    // 阶段判定线程（大厅/准备/局内）：判据是 g_ui_mgr.ui_dict 挂的 UI 对象，
    // 比"场景路径含 h55_wait"那种字符串匹配可靠 —— 后者换个模式就永久 false。
    PyScene::start(libbase);

    if (libbase < 0x5000000000){
        printf("[错误] libbase无效, 无法继续\n");
        sleep(9999);
    }
    c = (result.taddr-result.addr)/4096;
    long buff[512];
    /*   2026-10-24：**这个循环以前会把进程钉死**
     *   原来授权检查在内层 for 里，每 4KB 页都 `sleep(3); continue;`：
     *     BSS ≈14MB → c ≈ 3500 页 → 一轮就是 3500×3s ≈ **3 小时**，
     *     而且 MatrixOffset 永远为 0 → while 永不退出 → 后面的 PyRoot/绘制线程
     *     全被挡住（用户看到的就是"界面在、但一帧都不动"）。
     *   现在：
     *     ① 授权检查提到**外层**，一次无效就整体跳出（不再空转）；
     *     ② 加总超时（60 秒）兜底，超时就放弃扫描继续启动其它模块。
     */
    const time_t scan_t0 = time(nullptr);
    while (MatrixOffset==0||ArrayaddrOffset==0)
    {
    	const uint64_t _lic_sig = Lic::blob_u64(1);
    	const uint64_t _lic_mag = Lic::blob_u64(2);
    	const uint64_t _lic_aux = Lic::blob_u64(3);
    	if (_lic_sig == 0 || _lic_mag == 0 || _lic_aux == 0) {
    	    printf("[基址] 授权无效，跳过矩阵扫描（直接继续，不空转）\n");
    	    fflush(stdout);
    	    break;                        // ← 关键：不再死等
    	}
    	if (time(nullptr) - scan_t0 > 60) {
    	    printf("[基址] 矩阵扫描超时(60s)，放弃扫描继续启动\n");
    	    fflush(stdout);
    	    break;
    	}
    	for (int i = 0; i < c; i++){
        	vm_readv(result.addr+(i*4096), &buff, 0x1000);

        	for (int ii=0;ii<512;ii+=1){

        	    if (MatrixOffset == 0 && *(long long*)(&buff[ii]) == (long long)_lic_sig){
        	        uint64_t sig_addr = result.addr + i*4096 + ii*8;
        	        // 2026-09-17 热更后 +0x430 恒为0。该处是一组步长0x88的指针，三个元素都能走到
        	        // 同一个矩阵对象，这次更新只是让它整体位移了0x10。写死任何一个下次还会废，
        	        // 改成在窗口内找"整条链都走得通"的槽位(p1有效 且 p1+0xa58 也有效)，自愈。
        	        // 注意上界：只判 >0x5000000000 会让浮点垃圾(如0x400674954293DF)也蒙混过关，
        	        // 本机用户态指针都在 0x77xx/0x79xx 段，统一卡 <0x8000000000。
        	        //   窗口起点也走加密表：未授权时 _win==0 → 这一轮不找
        	        const uint64_t _win = SecOff::u64(SecOff::SCAN_WIN);
        	        if (_win == 0) continue;
        	        for (int off = (int)_win; off <= (int)_win + (uint64_t)SecOff::u64(SecOff::DG_07); off += 8){
        	            uint64_t cand_slot = sig_addr + off;
        	            uint64_t p1 = getPtr64(cand_slot);            // getPtr64 已做 0xB4 掩码
        	            if (!is_user_ptr(p1)) continue;
        	            uint64_t p2 = getPtr64(p1 + SecOff::u64(SecOff::MAT_P2));
        	            if (!is_user_ptr(p2)) continue;
        	            MatrixOffset = cand_slot - libbase;
        	            printf("[矩阵命中] 签名=0x%llX 槽位=+0x%X 根=0x%llX MatrixOffset=0x%lx\n",
        	                   (unsigned long long)sig_addr, off, (unsigned long long)cand_slot, MatrixOffset);
        	            break;
        	        }
        	    }

        	    if (ArrayaddrOffset == 0 && (uint64_t)buff[ii] == (_lic_mag ^ (_lic_aux & 0ULL))){
                    if (getDword(result.addr + 4096*i + 8*ii - 0x8) == 257 &&
                        getFloat(result.addr + 4096*i + 8*ii - 16) == 1.0f){
                        ArrayaddrOffset = result.addr - libbase + i*4096 + ii*8 + (long)SecOff::u64(SecOff::ARR_TAIL);
                        printf("[数组命中] 偏移=0x%llX\n", (unsigned long long)ArrayaddrOffset);
                    }
                }
    	    }
        }
        if (MatrixOffset!=0 && ArrayaddrOffset!=0){
            uint64_t tmpArr = getPtr64(libbase + ArrayaddrOffset);
            uint64_t tmpEnd = getPtr64(libbase + ArrayaddrOffset + 8);
            if (tmpArr > 0x5000000000 && tmpEnd > tmpArr) break;
            printf("[数组无效] 重新扫描 Array=0x%llX End=0x%llX\n", (unsigned long long)tmpArr, (unsigned long long)tmpEnd);
            ArrayaddrOffset = 0;
        }
        sleep(5);
    }
    SetReadState(2);

    // CPython 根(sys.modules + 类型对象)：读 so 的 ELF 段表定位 .PyRuntime，三个 Py 模块共用
    PyRoot::start(pid, libbase);
    // 密码机破译进度：走 CPython 对象图，自带后台线程(400ms 一轮)，跟这里的 3 秒大循环解耦
    PyProgress::start(libbase);
    // 天赋与辅助特质：同一条 CPython 链路，独立线程 500ms 一轮(一局内基本不变，不用刷那么勤)
    PyGenius::start(libbase);
    // 自身锚点(g_cam_ctrl.unit)与废弃模型黑名单：100ms 一轮，切换操控对象时要立刻跟上
PySelf::start(libbase);
#if !ENABLE_FREE
//   收费版才启动这些功能线程；免费版整块不跑（不是"关了开关"，是根本不创建）
PyAttack::start();
AutoPallet::start();
AutoSkill::Start();
#endif

// ★ 2026-10-23：正常退出（SIGTERM/SIGINT）也要抬手
//   信号处理器只置标志（async-signal-safe），真正的 TapUp 在 worker 循环里做。
//   不装这个的话：MT 管理器"停止"/kill 正好落在"手指按下、还没抬起"的窗口，
//   内核输入状态里会留一根按下的手指 → 用户体感"整台手机触摸都坏了"。
signal(SIGTERM, +[](int) { AutoSkill::g_term_request = 1; });
signal(SIGINT,  +[](int) { AutoSkill::g_term_request = 1; });
PyMiniMap::start(libbase);   // ← 新增
// 骑士（dxzh）头盔预测：同一条 CPython 链路，200ms 一轮（只要次数，不用刷那么勤）
// ⚠ 它只发布"是不是骑士 + 猜了几次 + 头盔CD/读条"，**猜的内容客户端没有**
PyKnight::start(libbase);
    PyCover::start(libbase);   // 2026-10-26：共享地图要用它读 scene_id

    uint64_t ArrayEnd = getPtr64(libbase + ArrayaddrOffset + 8);
    uintptr_t initial_array_addr = getPtr64(libbase + ArrayaddrOffset);
    uintptr_t initial_count = (ArrayEnd > initial_array_addr) ? (ArrayEnd - initial_array_addr) / 8 : 0;
    if (initial_count <= 0 || initial_count > 10000) initial_count = 1000;   // 兜底值：实测对局中 Count 只有 274~307
    PublishScanMeta(initial_array_addr, initial_count, 2);
    printf("[数组] Arrayaddr=0x%llX End=0x%llX Count=%d\n", (unsigned long long)initial_array_addr, (unsigned long long)ArrayEnd, (int)initial_count);

    while (true)
    {        
        // ★★ 2026-10-23 数据源闸门（授权 → 数据）★★
        //   这是整套防护里**唯一让"把 Authorized 改成 return true"失去意义**的位置：
        //   没有有效租约 / 没有服务端凭证时，扫描循环直接空转，data[] 永不更新
        //   → ESP、自瞄、小地图、自动类全部"没有数据"，但进程活着、菜单还在。
        //
        //   ⚠ 表现刻意用 read_state=1（"正在获取游戏数据"）：
        //     不弹窗、不报错、不退出 → 逆向者第一反应是"游戏更新了/版本坏了"，
        //     而不是"我踩到校验了"。
        //   ⚠ 两个不同 salt 的 tag 一起判：只 patch 一处仍然拦得住
        if (Lic::tag(0x51) | Lic::tag(0x52)) {
            ClearPublishedDataAndSetReadState(1);
            usleep(8000);
            continue;
        }
        // ★ 2026-10-21 每轮耗时统计（用户反馈框跟不上，先量化再优化）
        struct timespec t_round0;
        clock_gettime(CLOCK_MONOTONIC, &t_round0);
    	uint64_t curArray = getPtr64(libbase + ArrayaddrOffset);
        uint64_t curArrayEnd = getPtr64(libbase + ArrayaddrOffset + 8);
        uint64_t curCount = curArrayEnd > curArray ? (curArrayEnd - curArray) / 8 : 0;
	    // ★ 数组太小也视为无效：
// 进对局/进大厅的过渡期，游戏会动态 append 场景对象，
// 数组长度会从 0 慢慢涨到 200+。中间会短暂经过 30/60/100 这种半成品状态，
// 若不挡掉，就会用残缺的 data[] 画一整轮，表现为"只看到两三个人、没有密码机"。
// 实测正常对局 Count 都是 200~1000，准备阶段是 0。
// 100 这个阈值：既挡住半成品，又不会误伤任何一个真正的小对局。
// ★ 加载中判定(2026-10-10 改)：
//   原来是"Count 连续 2 轮**完全不变**才发布"。这个判据进对局时确实管用
//   (30→60→100→207 一路在涨，永远凑不满 2 轮相同)，但它在**准备界面/大厅**里是错的：
//   那里的 Count 是在一个小值附近**上下抖动**(实测 39±几个，家具/挂件随视角进出数组)，
//   每轮都在变 -> s_stable_rounds 永远归零 -> data_count 一直贴着 0，
//   界面就一直在「正在获取游戏数据 / 实体 0」和偶尔凑巧两轮相同闪出来的「实体 39」之间跳。
//
//   改成只看"是不是在**变大**"—— 进对局的加载特征是**持续增长**，抖动不是：
//     - 准备界面 39→41→38→40：单轮涨幅都 <= kGrowTolerance -> 不算加载 -> 正常发布
//     - 进对局   30→60→100 ：单轮涨 30/40 > kGrowTolerance  -> 认为还在加载 -> 继续等
//   容差取 8：实测准备界面的抖动幅度只有几个对象，而加载时单轮至少涨二三十个。
//   kSmallArrayMax 取 150："半成品"只可能出现在小数组上(正常对局实测 200~1000)，
//   数组一旦 >= 150 就直接发布，不必再等 —— 否则大数组边缘的小抖动会把它无谓地按住。
static uint64_t s_prev_count = 0;
static bool     s_have_prev  = false;   // 第一轮没有"上一轮"，不能拿 0 当基准判"变大"
static const int kGrowTolerance = 8;
static const int kSmallArrayMax = 150;

const bool s_growing = s_have_prev && (curCount > s_prev_count + (uint64_t)kGrowTolerance);
s_prev_count = curCount;
s_have_prev  = true;

// 基础合法性
if (curArray < 0x5000000000 || curCount == 0 || curCount > 10000) {
    ClearPublishedDataAndSetReadState(1);
    usleep(5000);   // ★ 2026-10-21：原来是 sleep(1) —— 每轮停 1 秒，框必然追不上
    continue;
}

// 小数组且**还在长大** → 认为在加载，本轮不发布
if (curCount < (uint64_t)kSmallArrayMax && s_growing) {
    ClearPublishedDataAndSetReadState(1);
    usleep(5000);   // ★ 2026-10-21：同上，1 秒的等待在准备阶段会让整个画面卡死
    continue;
}
        PublishScanMeta((uintptr_t)curArray, (uintptr_t)curCount, 2);
    	int entity_count=0;
        // 镜面这几个指针和预知文本先收在局部变量里，扫完一次性发布(见本轮末尾)。
        // 原来是"每轮先把全局清零、扫描中途再赋值"，绘制线程撞进这段(约1ms)就会读到 0，
        // 表现是红夫人模式下镜线约每分钟消失一帧。收在局部里，全局永远要么是上一轮的值、要么是新值。
        // 清零的本意(防跨局残留)仍然保留：局部变量每轮从 0 开始，发布时整体覆盖。
        uintptr_t rq_local = 0, rq_mirror_local = 0, mirror_local = 0, mirror_preview_local = 0;
        char prophet_local[sizeof(prophet_text)];
        prophet_local[0] = 0;
        // 自动盖板用的板子(类名含 woodplane)
uint64_t boards_local[AutoPallet::MAX_BOARDS];
int boards_n = 0;

// ★ 2026-10-21：查重从"每对象线性扫一遍 data[]"改成哈希表。
        //   原来 300 个实体一轮要比较 9 万次（纯 CPU，读内存都还没算），
        //   这是每轮耗时里最不值的一段。改成 unordered_set 后是 O(1)。
        std::unordered_set<uint64_t> seen_set;
        seen_set.reserve((size_t)(Count > 4096 ? 4096 : Count) * 2);
        // ★ 2026-10-21 提速关键：把整个指针数组**一次性**读进来。
        //   原来是每个槽位 getPtr64() 一次（一次驱动 ioctl），1000 个对象
        //   就是 1000 次 ioctl —— 这一条就占了每轮耗时的大头，也是"框追不上"的根因。
        static uint64_t s_obj_buf[10000];
        uint64_t s_obj_n = 0;
        {
            uint64_t want = Count > 10000 ? 10000 : Count;
            if (vm_readv(curArray, s_obj_buf, (size_t)(want * 8))) s_obj_n = want;
        }

        // ★★ 2026-10-24 「拒绝缓存」—— 试过，**没生效，已撤**（留档别重复走）★★
        //   思路：判过"无关"的对象 K 轮内不再读 0xf8，省掉上千次/轮的 ioctl。
        //   实测（真机、游戏在跑、75s 进稳态）：TTL=0/16/60 三档 CPU 与 ioctl 没有区别，
        //   而且它引入"地址被复用 → 新实体最多 K 轮看不见"的功能风险。
        //   ⇒ 结论：大厅/对局里真正的大头不是"装饰物的 0xf8"，别在这条路上再花时间。
        for (int ii = 0; ii < Count && entity_count < 3000; ii++){
            cur_obj = (ii < (int)s_obj_n) ? (s_obj_buf[ii] & 0x00FFFFFFFFFFFFFFULL) : 0;	// 遍历数量次数            
                
    		if (cur_obj == 0)   			
        		continue;    			    			

    	    // 类名要走五跳指针再加长度和内容，共 7 次驱动读；一轮 300 个对象就是 2000 多次，
    	    // 实测占了整轮 12ms 里的一半。同一个对象活着的时候类名不会变，所以按地址缓存。
    	    // ⚠ 地址会被复用(对象释放后新对象可能落在同一地址)，所以缓存**必须带校验**：
    	    // 每轮仍读一次第一跳(obj+0xf8)，值对得上才用缓存。7 次读降到 1 次，又不会张冠李戴。
    	    // 校验不是绝对的(新对象的第一跳恰好相同就会漏网)，但那种情况下最多错一轮的标签，
    	    // 下一轮对象就稳定了；换来的是整轮耗时减半。
    	    //
    	    // ★ 读失败时沿用缓存(2026-09-23)：内存压力下类名那条链要碰约 6 个堆页，
    	    //   任何一个被换进 zram 就整条断掉、对象被丢弃 —— 这正是"实体数量整体塌陷"的成因。
    	    //   而校验只碰对象自己那一页。所以这里必须分清"读失败"和"值就是 0"：
    	    //   getPtr64 两种情况都返回 0(内部 T result{} 值初始化)，因此改用 vm_readv 拿返回值。
    	    //   读失败 -> 沿用缓存里的类名，让对象继续画出来(坐标仍是实时读的，位置不会错)；
    	    //   代价是万一该地址已换成别的对象，标签会是旧的 —— 比整个消失可接受。
    	    uint64_t chain_head = 0;
    	    bool head_ok = vm_readv(cur_obj + (uint64_t)SecOff::u64(SecOff::DG_08), &chain_head, 8);
    	    chain_head &= 0x00FFFFFFFFFFFFFFULL;
    	    auto nc = g_name_cache.find(cur_obj);
    	    if (nc != g_name_cache.end() && (!head_ok || nc->second.first == chain_head)) {
    	        filter_class_name = nc->second.second;
    	    } else {
    	        uint64_t class_name_obj = getPtr64(getPtr64(getPtr64(getPtr64(chain_head)+ (uint64_t)SecOff::u64(SecOff::DG_09))+ (uint64_t)SecOff::u64(SecOff::DG_10))+ (uint64_t)SecOff::u64(SecOff::DG_11))+ (uint64_t)SecOff::u64(SecOff::DG_12);
    	        int len = getDword(class_name_obj + (uint64_t)SecOff::u64(SecOff::DG_13));
    	        if (len >= 256 || len == 0 || len < 0)
    	            continue;
    	        filter_class_name.resize(len);
    	        vm_readv(getPtr64(class_name_obj + (uint64_t)SecOff::u64(SecOff::DG_14)), &filter_class_name[0], len);
    	        // 跨局会不断有新对象，攒太多就整体清一次，下一轮重建(只是多花一轮的读取)
    	        // 每条约 180 字节(map 节点 + 类名字符串)：对局中约 300 条≈50KB，1500 条≈250KB
    	        if (g_name_cache.size() > 1500) g_name_cache.clear();
    	        g_name_cache[cur_obj] = std::make_pair(chain_head, filter_class_name);
    	    }

        		
			if (boards_n < AutoPallet::MAX_BOARDS && filter_class_name.find("woodplane") != std::string::npos) {
    bool seen = false;
    for (int k = 0; k < boards_n; k++) if (boards_local[k] == cur_obj) { seen = true; break; }
    if (!seen) boards_local[boards_n++] = cur_obj;
}


	int is_dup = 0;
	float pd1 = getFloat(cur_obj + (uint64_t)SecOff::u64(SecOff::DG_15));   // 只有实体才需要这个字段
	if (!seen_set.insert(cur_obj).second) is_dup = 1;   // 哈希判重 O(1)
        	if (is_dup == 1){
    		    continue;
    		}
        	if (should_filter(filter_class_name)) {
        		continue;//过滤随从等无关对象
        	}
        	std::string s;
        	//预知监管者
            // 局内：CPython 侧有监管单位，直接取它 unit.model 对应的场景对象的类名。
            //   这样另一形态(无常黑白)、约瑟夫相机、大厅残留都不会顶掉真监管。
            // 准备阶段/大厅：units_by_type 里还没有监管单位(实测)，退回下面按类名匹配的老逻辑。
            //   ⚠ 准备阶段不能用 +0x73(visible) 过滤：打求生者时真监管的模型在准备界面是不可见的(实测)
            uint64_t real_hunter = 0;
            const bool has_real_hunter = PySelf::hunter_body(real_hunter);
            if (show_draw_prophet && has_real_hunter){
                if (cur_obj == real_hunter) snprintf(prophet_local, sizeof(prophet_local), "%s", getboss(filter_class_name.c_str()));
            }
            else if (show_draw_prophet){//预知开始
                if (strstr(filter_class_name.c_str(), "burke_console") == NULL&&strstr(filter_class_name.c_str(), "h55_joseph_camera") == NULL&&strstr(filter_class_name.c_str(), "redqueen_e_heijin_yizi") == NULL&&strstr(filter_class_name.c_str(), "_lod") == NULL){
                    if (strstr(filter_class_name.c_str(), "boss") != NULL){
                        s += getboss(filter_class_name.c_str());
                        snprintf(prophet_local, sizeof(prophet_local), "%s", s.c_str());
                    }       
                }
            }//预知结束
    		
			// ★ 宝箱/怪物用固定表匹配（Name.h 的 chest_table / monster_table）
int mark_type = 0;   // 0=不匹配 6=宝箱 7=怪物
if (g_mark_enabled) {
    if (getchest(filter_class_name.c_str()) != NULL) {
        mark_type = 6;
    }
    else if (getmonster(filter_class_name.c_str()) != NULL) {
        mark_type = 7;
    }
    // 关键词命中后再判本局是否真的存在：v70 != 0 才算
    // （宝箱/怪物的 ex240 恒为 0，跟大门一样失效，只能用 v70 区分真假）
    if (mark_type != 0 && getDword(cur_obj + (uint64_t)SecOff::u64(SecOff::DG_16)) == 0) {
        mark_type = 0;
    }
}

// ★ 显式纳入约瑟夫相机（2026-10-12 实测：相机**从没进过 data[]** ✗）。
//   它的模型路径 `scene\model\...\h55_joseph_camera.gim` 虽然含 "scene"，
//   但实测整个实体列表里**既没有 camp9 也没有 h55_joseph_camera** ✗ ——
//   说明 filter_class_name 拿到的字符串和模型路径不是一回事。
//   这里直接按 "joseph" 收进来（相机 h55_joseph_camera / 相片 h55_joseph_photo），
//   后面的 camp 分支再按 "h55_joseph_camera" 精确判成 camp9 ✓
if (mark_type != 0 || strstr(filter_class_name.c_str(), "player") != NULL||strstr(filter_class_name.c_str(), "boss") != NULL || pd1 == 450 || strstr(filter_class_name.c_str(), "scene") != NULL || strstr(filter_class_name.c_str(), "prop") != NULL || strstr(filter_class_name.c_str(), "mirror") != NULL || strstr(filter_class_name.c_str(), "joseph") != NULL || Debugging || g_scene_dump_mode.load() )
{
			
    			memset(&g_data_build[entity_count], 0, sizeof(g_data_build[entity_count]));
                g_data_build[entity_count].obj = cur_obj;
    			// 阵营/str 必须先清零：下面那串 if-else 只有五个分支，而入口条件里的
    			// `pd1 == 450` 和 `Debugging` 能让对象进来却一个分支都不命中。
    			// data[] 是全局数组、原地复用，不清就会**沿用上一帧同下标那个对象的
    			// 阵营和名字** —— 表现是装饰物被当成角色画出来、还顶着别人的名字，
    			// 并且 内核人物数量 虚高。正常游玩时类名基本都能命中 player/boss，
    			// 所以这个洞主要在开「绘制调试」时发作。
    			g_data_build[entity_count].camp = 0;
    			g_data_build[entity_count].str[0] = '\0';
    			// ------------------------------------------------------------------
    			// 红夫人镜像：**必须在下面那串 boss 分支之前判**。
    			//   镜像类名 chr\boss\redqueen\redqueen_mirror.gim 里也带 "boss"，
    			//   先到先得会被当成普通监管 —— 画成红色、名字也叫"红夫人"，和本体分不清。
    			//   两种形态都认：
    			//     (a) 镜子本身 / 预览镜   类名带 redqueen_mirror
    			//     (b) 镜中的红夫人       类名也是 redqueen.gim，
    			//                            只能靠 +0x6D 实体种类位的 0x80 区分
    			// ------------------------------------------------------------------
    			// ------------------------------------------------------------------
    			// 红夫人镜像。
    			//
    			// 实测（镜子已放出时，直接读内存得到的 +0x6D 种类位）：
    			//   redqueen.gim         (280.11, 0.16, 357.67)  +0x6D = 0x50  <- 本体
    			//   redqueen_mirror.gim  (366.42, 0.16, 317.58)  +0x6D = 0x00  <- 镜像
    			//
    			// 所以判据是「**没有 0x40 位** = 不是本体 = 镜像」，
    			// 而不是之前那套 kind & 0x80 —— 实测根本没有任何实体带 0x80 位，
    			// 那个条件永远不成立，结果就是"镜像怎么都不画"。
    			//
    			// 坐标用的是 +0xa0(x) / +0xa4(y)：
    			//   镜子没放出来时镜像停在 y≈-1000，用 y >= -300 把它滤掉。
    			//   （注意是 +0xa4 不是 +0xa8 —— 日志里那个 -999.8 是第二位。）
    			// ------------------------------------------------------------------
    			bool is_rq_mirror = false;
    			{
    			    const char* cn_ = filter_class_name.c_str();
    			    if (strstr(cn_, "redqueen") != NULL && strstr(cn_, "mirror_model") == NULL) {
    			        uintptr_t cp_ = getPtr64(cur_obj + (uint64_t)SecOff::u64(SecOff::DG_17));
    			        float mx_ = getFloat(cp_ + (uint64_t)SecOff::u64(SecOff::DG_18));
    			        float my_ = getFloat(cp_ + (uint64_t)SecOff::u64(SecOff::DG_19));
    			        if (mx_ != 0 && my_ >= -300.0f) {
    			            uint8_t kind_ = 0;
    			            vm_readv(cur_obj + (uint64_t)SecOff::u64(SecOff::DG_20), &kind_, 1);
    			            if (!(kind_ & 0x40)) is_rq_mirror = true;
    			        }
    			    }
    			}
    			if (is_rq_mirror) {
    			    snprintf(g_data_build[entity_count].str, sizeof(g_data_build[entity_count].str),
    			             "%s", "红夫人镜像");
    			    g_data_build[entity_count].camp = 8;      // 8 = 红夫人镜像（白色）
    			}
    			else    			// ★ 宝箱/怪物优先判定（覆盖原来的分类）
if (mark_type == 6) {
    const char* nm = getchest(filter_class_name.c_str());
    strcpy(g_data_build[entity_count].str, nm ? nm : "宝箱");
    g_data_build[entity_count].camp = 6;
}
else if (mark_type == 7) {
    const char* nm = getmonster(filter_class_name.c_str());
    strcpy(g_data_build[entity_count].str, nm ? nm : "怪物");
    g_data_build[entity_count].camp = 7;
}
else
    			if (strstr(filter_class_name.c_str(), "boss") != NULL){
    			//data[指针数量].str=getboss(过滤类名.c_str());
    			strcpy(g_data_build[entity_count].str, getboss(filter_class_name.c_str()));
    			g_data_build[entity_count].camp=1;
    			}
    			else if (strstr(filter_class_name.c_str(), "player") != NULL||strstr(class_name.c_str(), "npc_deluosi_dress_ghost") != NULL||strstr(class_name.c_str(), "h55_pendant_huojian") != NULL){
    			//data[指针数量].str=getplayer(过滤类名.c_str());
    			strcpy(g_data_build[entity_count].str, getplayer(filter_class_name.c_str()));
    			g_data_build[entity_count].camp=2;
    			}
    			else if (strstr(filter_class_name.c_str(), "h55_joseph_camera") != NULL){
			strcpy(g_data_build[entity_count].str, "约瑟夫相机");
			g_data_build[entity_count].camp = 9;
		}
		else if (strstr(filter_class_name.c_str(), "scene") != NULL){
    			const char* scene_result = getscene(filter_class_name.c_str());
    			if (scene_result == NULL) continue;
    			strcpy(g_data_build[entity_count].str, scene_result);
    			g_data_build[entity_count].camp=3;
    			}
    			else if (strstr(filter_class_name.c_str(), "prop") != NULL){
    			const char* prop_result = getprop(filter_class_name.c_str());
    			if (prop_result == NULL) continue;
    			strcpy(g_data_build[entity_count].str, prop_result);
    			g_data_build[entity_count].camp=4;
    			}

    			else if (strstr(filter_class_name.c_str(), "redqueen") != NULL&&strstr(filter_class_name.c_str(), "mirror") != NULL&&strstr(filter_class_name.c_str(), "model") != NULL){
    			// fx/model/redqueen_mirror_model_obj_001.gim：准备放镜时的预览镜子(PlaceIndicator.rtc_model)，
    			// 常驻复用同一个对象，只在准备阶段可见。用法见 Draw_Main 里的镜线计算
    			g_data_build[entity_count].camp=5;
    			mirror_preview_local = cur_obj;
    			}
    			//sprintf(data[指针数量].类名, "%s", 过滤类名.c_str());
    			strcpy(g_data_build[entity_count].class_name, filter_class_name.c_str());
    			g_data_build[entity_count].objcoor=getPtr64(cur_obj+ (uint64_t)SecOff::u64(SecOff::DG_21));
    			//   ★ 台球手变球时坐标链会失效：先读一次，无效就用 Python 层 unit.position 兜底
    			g_data_build[entity_count].fb_ok = false;
    			{
    			    float _c[3] = {0, 0, 0};
    			    if (g_data_build[entity_count].objcoor)
    			        vm_readv(g_data_build[entity_count].objcoor + (uint64_t)SecOff::u64(SecOff::DG_22), _c, sizeof(_c));
    			    const bool bad = (fabsf(_c[0]) > 100000.0f || fabsf(_c[2]) > 100000.0f ||
    			                      (fabsf(_c[0]) < 1.0f && fabsf(_c[2]) < 1.0f));
    			    if (bad) {
    			        float fx = 0, fy = 0, fz = 0;
    			        const uint64_t sc = getPtr64(cur_obj + (uint64_t)SecOff::u64(SecOff::DG_23));
    			        bool got = PyGenius::other_pos_of_scene(sc, fx, fy, fz);
    			        if (!got) got = PyGenius::other_sole_pos(g_data_build[entity_count].camp, fx, fy, fz);
    			        if (got) {
    			            g_data_build[entity_count].fb_x = fx;
    			            g_data_build[entity_count].fb_y = fy;
    			            g_data_build[entity_count].fb_z = fz;
    			            g_data_build[entity_count].fb_ok = true;
    			        }
    			    }
    			}
    			if (!first_frame_logged){
        			printf("[实体] %s 地址=0x%llX 坐标地址=0x%llX 坐标=(%.1f,%.1f,%.1f) 类名=%s\n",
        			       g_data_build[entity_count].str, (unsigned long long)cur_obj,
        			       (unsigned long long)g_data_build[entity_count].objcoor,
        			       getFloat(g_data_build[entity_count].objcoor + (uint64_t)SecOff::u64(SecOff::DG_24)),
        			       getFloat(g_data_build[entity_count].objcoor + (uint64_t)SecOff::u64(SecOff::DG_25)),
        			       getFloat(g_data_build[entity_count].objcoor + (uint64_t)SecOff::u64(SecOff::DG_26)),
        			       g_data_build[entity_count].class_name);
        		}
    			entity_count++;
			}
    			
			//红夫人模式：本体和镜中红夫人的类名都是 redqueen.gim，只能靠对象字段区分。
			// 2026-09-22 热更后旧判据失效：镜中红夫人的 +0x70 不再是 65150(鬼魂)，也在地面上，
			// 于是被当成本体，镜面算错，求生者镜像跟着红夫人跑。
			// 现在用 +0x6D 实体种类位(实测，镜子放出状态)：
			//   本体       0x50 = 0x40(角色实体) | 0x10
			//   镜中红夫人 0x90 = 0x80(不占玩家槽位的角色) | 0x10
			// 整字节会跳变(见过 0x50->0xD0)，但 0x40 位稳定，所以只看位不看整字节。
			// 镜面 = 本体与镜中红夫人连线的垂直平分线，已用 Python 侧 MaryMirrorUnit.position/direction 验证。
			// 镜子没放出时镜中红夫人停在 y≈-1000，下面坐标读取处的 Z>=-300 会让 mirror=false。
			if (pd1==450){
			    uintptr_t coorPtr = getPtr64(cur_obj + (uint64_t)SecOff::u64(SecOff::DG_27));
			    if (strstr(filter_class_name.c_str(), "boss") != NULL && strstr(filter_class_name.c_str(), "redqueen") != NULL && strstr(filter_class_name.c_str(), "mirror") == NULL
			        && getFloat(coorPtr + (uint64_t)SecOff::u64(SecOff::DG_28)) != 0 && getFloat(coorPtr + (uint64_t)SecOff::u64(SecOff::DG_29)) != 0) {
			        uint8_t kind = 0;
			        vm_readv(cur_obj + (uint64_t)SecOff::u64(SecOff::DG_30), &kind, 1);
			        if (kind & 0x40)      rq_local = cur_obj;       // 本体
			        else if (kind & 0x80) rq_mirror_local = cur_obj;   // 镜中红夫人
			    }
    			if (strstr(filter_class_name.c_str(), "boss") != NULL && strstr(filter_class_name.c_str(), "mirror") != NULL
    			    && getFloat(coorPtr + (uint64_t)SecOff::u64(SecOff::DG_31)) != 0 && getFloat(coorPtr + (uint64_t)SecOff::u64(SecOff::DG_32)) != 0)
    			{
    		    	mirror_local=cur_obj;
    			}    			
			}    						
        }
        if (!first_frame_logged){
            first_frame_logged = true;
            printf("[首帧调试] 矩阵16值已打印 实体列表已打印\n");
        }
        // 本轮结果一次性发布。prophet_local 为空(这轮没扫到监管)时保留上一轮的文本，
        // 跟原来"prophet_text 从不清零"的行为一致
        AutoPallet::publish_boards(boards_local, boards_n);
        //   ★ 独立闸门（不经过 mask()）：每 32 个实体自检一次，
        //   只改 mask() 的人在这里会被挡住。
        if ((entity_count & 0x1F) == 0 && !Lic::QuickGate()) break;

        {
            std::lock_guard<std::recursive_mutex> data_lock(g_data_mutex);
            if (entity_count > 0) {
                memcpy(data, g_data_build, sizeof(DataStruct) * (size_t)entity_count);
            }
            // 与实体表同一把锁发布；Draw_Main 不会读到跨轮的镜像指针/预知文本。
            redqueen_obj        = rq_local;
            redqueen_mirror_obj = rq_mirror_local;
            mirror_obj          = mirror_local;
            mirror_preview_obj  = mirror_preview_local;
            if (prophet_local[0]) memcpy(prophet_text, prophet_local, sizeof(prophet_text));
            data_count = entity_count;
            read_state = 2;
        }
        {
            static int64_t s_sum_us = 0; static int s_rounds = 0;
            struct timespec t_round1; clock_gettime(CLOCK_MONOTONIC, &t_round1);
            s_sum_us += (t_round1.tv_sec - t_round0.tv_sec) * 1000000LL + (t_round1.tv_nsec - t_round0.tv_nsec) / 1000;
            s_rounds++;
            if (s_rounds >= 60) {
                printf("[扫描] 平均每轮 %.2f ms (实体=%d, %d 轮)\\n",
                       (double)s_sum_us / s_rounds / 1000.0, entity_count, s_rounds);
                fflush(stdout);
                s_sum_us = 0; s_rounds = 0;
            }
        }

        // ★★ 2026-10-24 定速（性能，用户反馈 CPU 太高）★★
        //   实测（simpleperf + strace，游戏在跑、200+ 实体）：
        //     · 这个循环**原来没有任何 sleep**，靠驱动读自身的耗时"自然限速"；
        //     · 每轮 ioctl 约 250µs（每次都要 find_vpid/get_task_mm + copy_to_user），
        //       读线程单核 61%，进程合计 169%；
        //     · 另一半时间在抢 g_kma_mtx（futex 16k 次/4s）—— 多线程被串行化。
        //   定速到 kScanPeriodUs：**只补差额**，某轮本来就超时就不补（避免雪崩）。
        //   125Hz 是 60fps 的两倍多，框不会滞后；要更快/更省改这一个数字即可。
        {
            //   kScanPeriodUs 可用环境变量 IDV_SCAN_US 覆盖（0 = 不定速，老行为），
            //   方便在真机上直接扫参数，不用每次改代码重编。
            static const int64_t kScanPeriodUs = [] {
                const char* s = getenv("IDV_SCAN_US");
                long v = s ? atol(s) : 8000;
                if (v < 0) v = 0;
                if (v > 200000) v = 200000;
                return (int64_t)v;
            }();
            if (kScanPeriodUs > 0) {
                struct timespec t_round2; clock_gettime(CLOCK_MONOTONIC, &t_round2);
                const int64_t spent_us = (t_round2.tv_sec - t_round0.tv_sec) * 1000000LL
                                       + (t_round2.tv_nsec - t_round0.tv_nsec) / 1000;
                if (spent_us < kScanPeriodUs) usleep((useconds_t)(kScanPeriodUs - spent_us));
            }
        }
        
        // ==================== 场景 dump 保存 ====================
if (g_scene_dump_save.exchange(false)) {
    std::lock_guard<std::recursive_mutex> data_lock(g_data_mutex);
    const char *paths[] = {
        "/sdcard/scene_dump.txt",
        "/data/local/tmp/scene_dump.txt"
    };
    FILE* f = nullptr;
    for (auto p : paths) { f = fopen(p, "w"); if (f) break; }
    if (f) {
        fprintf(f, "=== scene dump  count=%d ===\n", entity_count);
        fprintf(f, "%-16s %4s %-22s %5s %10s %8s  %s\n",
                "addr", "camp", "pos(x,y,z)", "ex240", "v70", "v1a0", "class_name");
        for (int i = 0; i < entity_count; i++) {
            float xyz[3] = {0,0,0};
            unit_xyz(i, xyz);
            fprintf(f, "0x%014llX %4d (%8.1f,%7.1f,%8.1f) %5d 0x%08X %8.1f  %s\n",
                (unsigned long long)data[i].obj,
                data[i].camp,
                xyz[0], xyz[1], xyz[2],
                getDword(data[i].obj + (uint64_t)SecOff::u64(SecOff::DG_33)),
                getDword(data[i].obj + (uint64_t)SecOff::u64(SecOff::DG_34)),
                getFloat(data[i].obj + (uint64_t)SecOff::u64(SecOff::DG_35)),
                data[i].class_name);
        }
        fclose(f);
        printf("[dump] saved %d objects\n", entity_count);
    } else {
        printf("[dump] fopen failed\n");
    }
}
        // 2026-09-23：3 秒 -> 2 秒。一轮实测约 9ms，读线程 CPU 0.3% -> 0.45%，
        // data[] 的撕裂窗口同比例从 0.3% 到 0.45%，都可忽略；换来新对象和预知监管更快出现
        sleep(2);
    }
}




// ---- 场景对象"是否本局真实存在"判定 ----
// 游戏把所有候选刷新点、以及角色的每种形态都实例化成对象放进数组，本局/当前只激活其中一部分。
// 未激活的那些类名、坐标全都正常，光看类名/坐标区分不出来。
//
// **判据是 +0x70（代码里原本叫 jxpd/checkVal）**，它是三态的：
//     0          = 本局根本不存在 / 当前不是活跃形态
//     0x1000000  = 正常存在
//     其它非0    = 存在，但处于鬼魂/特殊状态（旧记录里的 65150 属于这一档）
//
// 怎么确认的：红蝶本体与 opposite 形态做变身前后差分，两个对象在 0x300 字节里
// **各自只有 +0x70 这一列发生变化，且方向相反**（本体 0x1000000->0，opposite 0->0x1000000），
// 同时对照组(9个约瑟夫相机 + 15个密码机)零变化。语义非常干净。
// 横向验证：约瑟夫相机(每局默认加载但不存在) 4/4 全为 0；求生者 8 个对象里 +0x70!=0 的正好 4 个(实际人数)。
//
// 注意 +0x1a0 是"类型"字段(450=角色 500=可交互物)，**不是存在性**：
// 曾经误用它单独做判据，结果一个不存在的密码机 +0x1a0 恰好就是 500，照样被画出来。
//
// 曾经用过 +0x30(实例化节点指针)，**已废弃**：它不稳定，同样是约瑟夫相机，
// 一局里测到 13个只有1个非空，另一局 9个全部非空，不能用。
// +0x6D 与 +0x73 都是单字节，分别藏在 +0x6C / +0x70 这两个 dword 里。
//   +0x73 = 1  -> 当前活跃/在场（常见的 0x1000000 就是这个字节的 dword 形式）
//   +0x6D      -> 实体种类位域: 0x40=角色/生物  0x10=玩家阵营相关  0x00=纯场景装饰
static inline unsigned entity_active_byte(uintptr_t obj) { return (getDword(obj + (uint64_t)SecOff::u64(SecOff::DG_36)) >> 24) & 0xFF; }
static inline unsigned entity_kind_byte(uintptr_t obj) { return (getDword(obj + (uint64_t)SecOff::u64(SecOff::DG_37)) >> 8)  & 0xFF; }

// **千万不要拿活跃位当"本局是否存在"用**：它是"当前对本机客户端可见"的意思，跟视角走。
// 实测同一张图，监管视角下 12 台密码机活跃位全是 1，换成求生者视角只剩 1 台是 1。
// 早先用它做密码机判据，导致求生者只能看到身边那一台，绕了一大圈才发现。
//
// 真正视角无关的存在性标记是 +0x240：
//   实测 约瑟夫相机 7/7 = 0，破轮台 4/4 = 0（这两类都是每局默认加载、本局并不存在的装饰）
//        密码机 真的 7 个 = 2 / 假的 5 个 = 0，箱子、求生者、监管 全部 = 2
static inline bool entity_exists(uintptr_t obj)
{
    return getDword(obj + (uint64_t)SecOff::u64(SecOff::DG_38)) == 2;
}
// 破译进度配色：<20 绿、20~60 黄、>60 红
static inline ImColor progress_color(float v)
{
    //   2026-10-26：用户要求改成三段：0-40 绿 / 40-70 黄 / 70-100 红
    if (v < 40.0f)  return color_green;
    if (v < 70.0f)  return color_yellow;
    return color_red;
}

// 按 g_esp.rect_style 画方框（全框 / 四角括号）
// 本帧已画框数 —— 由 DrawEspRect 累加，[绘制] 日志里用来区分"实体多"还是"画得多"。
// 必须声明在 DrawEspRect 之前（它是文件里较早定义的函数）。
static int g_drw_drawn_n = 0;

/*   2026-10-26：**骨骼设置单独存一份**
 *   实测主配置里存了也不生效（重进回到默认），这里用独立文件兜住：
 *   /data/adb/1/bones.cfg  6 个值：颜色x2 线粗x10 画头 头大小x10 同步框
 */
// 骨骼头顶（给"同步绘制框"用）
static uint64_t g_bone_top_obj = 0;
static float    g_bone_top_y   = 0.0f;
static float    g_bone_shift   = 0.0f;   // 本角色上一帧"框顶要上移多少"
static uint64_t g_bone_shift_obj = 0;    // 上面那个位移属于哪个实体

static void DrawEspRect(ImDrawList* dl, float x1, float y1, float x2, float y2,
                        float W, float H, ImU32 col) {
    g_drw_drawn_n++;   // 统计本帧画了几个框（见 [绘制] 那行日志）
    /*   2026-10-26：**同步绘制框**
     *   高个子角色（约瑟夫/红夫人那类）骨骼的头会跑到框外面。
     *   打开这个开关就按框高的比例往上加一截，把骨骼整个包住；关着就是原来的样子。
     */
    /*   2026-10-26：加高改由调用方按骨骼头计算（这里不再按百分比猜） */
    float th = g_esp.rect_thickness;
    if (g_esp.rect_style == 0) {
        dl->AddRect({x1, y1}, {x2, y2}, col, 3, 0, th);
    } else {
        float cw = W * 0.28f;
        float maxcw = H * 0.22f;
        if (cw > maxcw) cw = maxcw;
        if (cw < 4.0f)  cw = 4.0f;
        dl->AddLine({x1, y1}, {x1 + cw, y1}, col, th);
        dl->AddLine({x1, y1}, {x1, y1 + cw}, col, th);
        dl->AddLine({x2, y1}, {x2 - cw, y1}, col, th);
        dl->AddLine({x2, y1}, {x2, y1 + cw}, col, th);
        dl->AddLine({x1, y2}, {x1 + cw, y2}, col, th);
        dl->AddLine({x1, y2}, {x1, y2 - cw}, col, th);
        dl->AddLine({x2, y2}, {x2 - cw, y2}, col, th);
        dl->AddLine({x2, y2}, {x2, y2 - cw}, col, th);
    }
}

// ============================================================================
// 2026-10-24：梦之女巫「本体画、信徒不画」修复（见 docs/梦之女巫信徒不绘制-修复记录.md）
//
//   信徒是 units_by_type[236] 的独立单位，PyGenius::hunter_scene() 只是
//   units_by_type[1] 第一条 —— 拿它当"是不是监管"的白名单，会把**所有从属单位**
//   （女巫信徒、巡视者、机械玩偶…）一起枪毙。
//   所以这里改成**按类名**认族，不再依赖 Python 单位表。
// ============================================================================
//   本体 + 信徒（含 sickle 等子模型）都算 yidhra 族
static inline bool is_yidhra_class(const char* n)
{
    return n && strstr(n, "yidhra") != nullptr;
}

//   只要**主模型**：实测一局 camp1 里有 3 个 yidhra_xintu_sickle.gim，
//   它们 body=0，必须继续挡掉，否则一个信徒会多出 1~2 个框。
static inline bool is_yidhra_main_model(const char* n)
{
    if (!n || !strstr(n, "yidhra")) return false;
    static const char* kSub[] = { "sickle", "weapon", "separate_dir", "_lod", "guajian" };
    for (const char* k : kSub) if (strstr(n, k)) return false;
    return true;
}

static inline bool is_real_generator(uintptr_t obj)
{
    // 活跃位 + 一个密码机专用的辅助值。单用任何一个都不够：
    //   只判 +0x1a0 -> 未激活的候选机器该值恰好也可能是 500
    //   只判活跃位  -> 会混进一个位于原点(0,0,0)的占位对象
    return getFloat(obj + (uint64_t)SecOff::u64(SecOff::DG_39)) == 500.0f && getDword(obj + (uint64_t)SecOff::u64(SecOff::DG_40)) == 2;
}

// ---- VP 矩阵校验(指针范围 + 16 个值全部有限) ----
// 本机用户态指针都在 0x77xx/0x79xx 段，与启动扫描用的范围一致
//   ★ 2026-10-24：上下界改由 SecOff 提供（加密表 + 会话密钥解密）。
//     未授权时 u64() 返回 0 → is_user_ptr 恒 false → 所有指针校验全部失败，
//     这与"没有卡密就没有数据"的失效语义一致，而且比明文常量难抄。
static inline bool is_user_ptr(uint64_t p) {
    const uint64_t lo = SecOff::u64(SecOff::PTR_LO);
    const uint64_t hi = SecOff::u64(SecOff::PTR_HI);
    if (lo == 0 || hi == 0) return false;
    return p > lo && p < hi;
}
// 16 个 float 全部 finite 才算有效矩阵（实现在 SecMath，吃满档混淆）
static inline bool matrix_finite(const float *m) { return SecMath::ValidMatrix(m); }
// 校验不过时沿用上一帧的矩阵，连续超过这么多帧才停止投影(约 80ms，肉眼看不出偏移)。
// 初值大于上限：启动后还没拿到过一次有效矩阵时，matrix[] 全是 0，不能拿来投影。
static constexpr int MATRIX_STALE_MAX = 5;
static int g_matrix_stale = MATRIX_STALE_MAX + 1;

// 模仿者身份的配色：侦探团蓝 / 模仿者红 / 中立黄 / 还没读到白。绘制和界面的身份簿列表共用
static ImColor copycat_color(int identity)
{
    switch (PySelf::copycat_camp(identity)) {
        case 1:  return ImColor(60,150,255,255);
        case 2:  return ImColor(255,60,60,255);
        case 3:  return ImColor(255,220,0,255);
        default: return ImColor(255,255,255,255);
    }
}

// ★ 死者(灵魂体 / 尸体)的固定配色：灰白。
//   刻意**不跟阵营色走** —— 死人被染成侦探团蓝/模仿者红时，画面上跟活人长得一样，
//   用户直接反馈成"有的人死掉了还是显示活着"。灰白只表达"这人已经出局"。
static const ImColor kCopycatDeadColor = ImColor(200, 200, 205, 255);
// 尸体专用：比"已死"再暗一点，和灵魂体区分开(用户要求尸体用灰色字体)
static const ImColor kCorpseColor     = ImColor(150, 150, 155, 255);

// 前向声明（定义在 Draw_Main 后面）
// 前向声明（定义在 Draw_Main 后面）
static void TouchRect_Draw(ImDrawList* Draw);


// 摸金模式：自己是谁？
static int64_t GoldrushSelfUid()
{
    if (g_minimap_auto_self) {
        // 自动：先看 PySelf 能不能给出准确的 uid
        int64_t u = PySelf::self_uid();
        if (u >= 1000001 && u <= 1000100) return u;
        return 0;   // 0 = 让调用方退回"uid 最小"兜底
    }
    if (g_minimap_self_num >= 1 && g_minimap_self_num <= 99)
        return (int64_t)(1000000 + g_minimap_self_num);
    return 0;
}

// ==================== 小地图 ====================
// 只用**纯色背景**。
//
// 早先试过三套「读游戏随机布局 -> 拼成完整小地图」的方案，都没能稳定对上
// 游戏的真实布局，相关代码已全部移除：
//   1) export_configs[map_id].room_data 的格子 ×64  —— 那是生成挂载格，会重叠
//   2) voxel_imgs 节点位置                        —— 是迷雾格子，不是房间
//   3) _area_cfgs 区域图 + aabb                   —— 模型对，但缺 2 张区域贴图
// 结论与证据见 docs/小地图布局-完整算法.md

// 图形系统重建时调用：现在没有贴图缓存了，留空实现即可
void ResetMiniMapTexture() {
}

static void DrawMiniMap(ImDrawList* dl);   // ← 新增

// 从 unit.__dict__ 里取某字段的值对象指针
static uint64_t UnitFieldPtr(uint64_t unit, const char* name) {
    uint64_t d = PyCore::get_inst_dict(unit);
    PyCore::Dict dk;
    if (!d || !PyCore::read_dict(d, dk)) return 0;
    for (int64_t i = 0; i < dk.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(dk, i));
        char kname[64] = {0};
        if (!PyCore::read_str(kp, kname, sizeof(kname))) continue;
        if (strcmp(kname, name) == 0)
            return getPtr64(PyCore::value_slot(dk, i));
    }
    return 0;
}

// 从场景对象读世界坐标（scene_obj + 0x28 是坐标指针）
static bool SceneWorldPos(uint64_t scene_obj, float& x, float& y, float& z) {
    if (!PyCore::is_obj(scene_obj)) return false;
    uint64_t coor = getPtr64(scene_obj + (uint64_t)SecOff::u64(SecOff::DG_41));
    if (!coor) return false;
    float raw[3];
    if (!vm_readv(coor + (uint64_t)SecOff::u64(SecOff::DG_42), raw, 12)) return false;
    x = raw[0]; z = raw[1]; y = raw[2];
    return true;
}

// 临时诊断：把监管 unit.__dict__ 里所有可能跟"位置/光标/技能点"沾边的字段列出来
static void DumpUmbrellaCandidates() {
    uint64_t hu = PyAttack::hunter_unit_raw();
    if (!hu) { printf("[伞dump] unit 未就绪\n"); return; }
    uint64_t d = PyCore::get_inst_dict(hu);
    if (!d) { printf("[伞dump] dict 未就绪\n"); return; }
    PyCore::Dict dk;
    if (!PyCore::read_dict(d, dk)) { printf("[伞dump] dict 读失败\n"); return; }

    printf("\n[伞dump] === hunter_unit.__dict__ (%lld 项) ===\n", (long long)dk.n_entries);

    static const char* kws[] = {
        "pos", "point", "cursor", "aim", "indic", "umbrella", "imper",
        "target", "skill", "cast", "loc", "world", "vector", "offset",
        "preview", "predict", "hover", "select", "choose"
    };

    for (int64_t i = 0; i < dk.n_entries; i++) {
        uint64_t kp = getPtr64(PyCore::key_slot(dk, i));
        char kname[64] = {0};
        if (!PyCore::read_str(kp, kname, sizeof(kname))) continue;

        bool interesting = false;
        for (auto kw : kws) if (strstr(kname, kw)) { interesting = true; break; }
        if (!interesting) continue;

        uint64_t vp = getPtr64(PyCore::value_slot(dk, i));

        // 试着当 float3 读
        float f3[3] = {0};
        bool f3ok = vm_readv(vp + (uint64_t)SecOff::u64(SecOff::DG_43), f3, 12);
        // 试着当 PyLong 读
        int64_t asint = 0;
        uint32_t digit = 0;
        bool int_ok = vm_readv(vp + (uint64_t)SecOff::u64(SecOff::DG_44), &asint, 8) && vm_readv(vp + (uint64_t)SecOff::u64(SecOff::DG_45), &digit, 4);

        // PyFloat 的特征：+0x08 是 &PyFloat_Type，值在 +0x10
        // math3d.vector 的特征：+0x10 起三个 float

        printf("[伞dump] %-28s @0x%llX  f3=(%.2f, %.2f, %.2f)",
               kname, (unsigned long long)vp,
               f3ok ? f3[0] : 0.f, f3ok ? f3[1] : 0.f, f3ok ? f3[2] : 0.f);
        if (int_ok)
            printf("  int=%lld", (long long)(asint >= 0 ? digit : -digit));
        printf("\n");
    }
        printf("[伞dump] === 结束 ===\n");
    fflush(stdout);
}

// ==================== 模仿者身份悬浮窗绘制 ====================
// 直接画在 foreground draw list 上，**不是** ImGui 窗口：
//   - 不占窗口栈，不会被别的窗口盖住，也不参与布局；
//   - 最关键的是它**不接收任何输入** —— 用 ImGui::Begin 做的话那块区域会把触摸吃掉，
//     而这个 overlay 是贴在游戏上面的，吃触摸等于玩不了。
// 透明背景所以不画底色；但为了在亮场景里也看得清，文字下面垫一层深色描边。
static void DrawCopycatWindow(ImDrawList* dl)
{
    if (!g_ccwin_show) return;
    if (!copycat_mode) return;

    PySelf::CopycatBook book = PySelf::copycat_book();

    struct Ent { char text[64]; ImU32 col; };
    Ent ent[PySelf::MAX_COPYCAT];
    int n = 0, known = 0;
    // ★ 只画 1~12 号。身份簿的格子是 16 格(MAX_COPYCAT)，但模仿者狂欢就 12 个人，
    //   早先照着 MAX_COPYCAT 循环，界面上会多出「13号 ? 14号 ? 15号 ? 16号 ?」四行噪声。
    const int seats = (PySelf::MAX_COPYCAT < 12) ? PySelf::MAX_COPYCAT : 12;
    for (int k = 0; k < seats; ++k) {
        const PySelf::CopycatSeat &st = book.seat[k];
        if (st.known) known++;
        Ent &e = ent[n];
        const char *nm = PySelf::copycat_identity_name(st.identity);
        int len = snprintf(e.text, sizeof(e.text), "%d号 ", k + 1);
        if (nm)                 snprintf(e.text + len, sizeof(e.text) - len, "%s", nm);
        else if (st.identity)   snprintf(e.text + len, sizeof(e.text) - len, "身份%d", st.identity);
        else                    snprintf(e.text + len, sizeof(e.text) - len, "?");
        if (st.dead) {
            int l2 = (int)strlen(e.text);
            snprintf(e.text + l2, sizeof(e.text) - l2, " 已死");
            e.col = kCopycatDeadColor;                       // 死了 -> 灰，并标「已死」
        } else if (st.known && st.identity) {
            e.col = (ImU32)copycat_color(st.identity);        // 活着 -> 他自己身份的阵营色
        } else {
            e.col = IM_COL32(185, 185, 190, 225);            // 还没读到 -> 中性灰
        }
        n++;
    }

    ImFont *font = ImGui::GetFont();
    const float fs = (g_ccwin_size < 8.0f) ? 8.0f : (g_ccwin_size > 90.0f ? 90.0f : g_ccwin_size);
    const int   cols = (g_ccwin_cols < 1.0f) ? 1 : (g_ccwin_cols > 3.0f ? 3 : (int)(g_ccwin_cols + 0.5f));

    if (known == 0) {
        const char *msg = "模仿者身份：还没读到身份簿";
        dl->AddText(font, fs, ImVec2(g_ccwin_x + 1.5f, g_ccwin_y + 1.5f), IM_COL32(0, 0, 0, 190), msg);
        dl->AddText(font, fs, ImVec2(g_ccwin_x, g_ccwin_y), IM_COL32(200, 200, 205, 235), msg);
        return;
    }

    // 每列宽度取该列里最宽的一条，列与列之间留 0.9 个字号
    float colw[3] = { 0.f, 0.f, 0.f };
    for (int i = 0; i < n; i++) {
        const int c = i % cols;
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, ent[i].text);
        if (ts.x > colw[c]) colw[c] = ts.x;
    }
    float colx[3] = { 0.f, 0.f, 0.f };
    for (int c = 1; c < cols; c++) colx[c] = colx[c - 1] + colw[c - 1] + fs * 0.9f;

    const float lh = fs * 1.28f;
    for (int i = 0; i < n; i++) {
        const int r = i / cols, c = i % cols;
        const ImVec2 p(g_ccwin_x + colx[c], g_ccwin_y + r * lh);
        dl->AddText(font, fs, ImVec2(p.x + 1.5f, p.y + 1.5f), IM_COL32(0, 0, 0, 190), ent[i].text);
        dl->AddText(font, fs, p, ent[i].col, ent[i].text);
    }
}

// ==================== 小地图绘制 ====================
static void DrawMiniMap(ImDrawList* dl)
{
    std::lock_guard<std::recursive_mutex> data_lock(g_data_mutex);
    if (!g_minimap_enabled) return;
    if (!PyMiniMap::in_goldrush()) return;

    // 收集队员
    PyMiniMap::TeamMember team[PyMiniMap::MAX_TEAM];
    int n = 0;
    int raw_n = PyMiniMap::team_count();
    for (int i = 0; i < raw_n && n < PyMiniMap::MAX_TEAM; i++) {
        PyMiniMap::TeamMember tm;
        if (!PyMiniMap::get_member(i, tm) || !tm.valid) continue;
        team[n++] = tm;
    }
    if (n == 0) return;

    // ★ 找自己：用 uid（每局唯一）
int self_idx = -1;
int64_t self_uid = 0;
if (g_minimap_auto_self) {
    self_uid = PySelf::self_uid();
    if (self_uid < 1000001 || self_uid > 1000100) self_uid = 0;
} else if (g_minimap_self_num >= 1 && g_minimap_self_num <= 99) {
    self_uid = 1000000 + g_minimap_self_num;
}
if (self_uid > 0) {
    for (int i = 0; i < n; i++)
        if (team[i].uid == self_uid) { self_idx = i; break; }
}
if (self_idx < 0) return;

    // 以自己为中心
    float cx = team[self_idx].x;
    float cz = team[self_idx].z;

    ImVec2 p1(g_minimap_x, g_minimap_y);
    ImVec2 p2(g_minimap_x + g_minimap_w, g_minimap_y + g_minimap_h);
    ImVec2 ctr((p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f);
    const float scale = g_minimap_zoom;
    const float margin = 10.0f;

    // ★ 底图：纯色
    dl->AddRectFilled(p1, p2, IM_COL32(16, 20, 28, 215), 10.0f);
    dl->AddRect(p1, p2, IM_COL32(100, 140, 200, 220), 10.0f, 0, 2.0f);

    const ImVec2 d1 = p1, d2 = p2;

    dl->PushClipRect(d1, d2, true);
    // ---- 绘制宝箱/怪物 ----
    if (g_mark_enabled) {
        for (int di = 0; di < data_count; di++) {
            if (data[di].camp != 6 && data[di].camp != 7) continue;

            float xyz[3] = {0, 0, 0};
            unit_xyz(di, xyz);
            float mx = xyz[0], mz = xyz[1], my = xyz[2];
            if (mx == 0.f && my == 0.f) continue;

            // 以自己为中心 + zoom
            float wx = ctr.x + (mx - cx) * scale;
            float wy = ctr.y - (my - cz) * scale;

            bool is_chest = (data[di].camp == 6);
            ImU32 mc = is_chest ? g_mark_col_chest : g_mark_col_monster;
            float r  = is_chest ? 4.0f : 5.0f;

            // ★ 超界 → 画到边缘，用三角指示方向
            bool clamped = false;
            float ex = wx, ey = wy;
            if (ex < p1.x + margin) { ex = p1.x + margin; clamped = true; }
            if (ex > p2.x - margin) { ex = p2.x - margin; clamped = true; }
            if (ey < p1.y + margin) { ey = p1.y + margin; clamped = true; }
            if (ey > p2.y - margin) { ey = p2.y - margin; clamped = true; }

            if (clamped) {
                float dx = wx - ctr.x;
                float dy = wy - ctr.y;
                float len = sqrtf(dx*dx + dy*dy);
                if (len > 0.001f) {
                    dx /= len; dy /= len;
                    ImVec2 tip(ex + dx * 3.0f, ey + dy * 3.0f);
                    ImVec2 a(ex - dy * 5.0f - dx * 3.0f, ey + dx * 5.0f - dy * 3.0f);
                    ImVec2 b(ex + dy * 5.0f - dx * 3.0f, ey - dx * 5.0f - dy * 3.0f);
                    dl->AddTriangleFilled(tip, a, b, mc);
                }
            } else if (is_chest) {
                dl->AddRectFilled(ImVec2(wx - r, wy - r), ImVec2(wx + r, wy + r), mc);
                dl->AddRect(ImVec2(wx - r, wy - r), ImVec2(wx + r, wy + r),
                            IM_COL32(255,255,255,180), 0, 0, 1.0f);
            } else {
                dl->AddTriangleFilled(
                    ImVec2(wx, wy - r), ImVec2(wx - r, wy + r), ImVec2(wx + r, wy + r), mc);
                dl->AddTriangle(
                    ImVec2(wx, wy - r), ImVec2(wx - r, wy + r), ImVec2(wx + r, wy + r),
                    IM_COL32(255,255,255,180), 1.0f);
            }
        }
    }

    // ---- 绘制队友（超界也画到边缘）----
    for (int i = 0; i < n; i++) {
        if (i == self_idx) continue;

        // 以自己为中心 + zoom
        float wx = ctr.x + (team[i].x - cx) * scale;
        float wy = ctr.y - (team[i].z - cz) * scale;

        int64_t num = team[i].uid - 1000000;
        ImU32 col;
        switch (num) {
            case 1: col = IM_COL32(255, 220,   0, 255); break;
            case 2: col = IM_COL32( 60, 150, 255, 255); break;
            case 3: col = IM_COL32( 60, 220,  60, 255); break;
            case 4: col = IM_COL32(255,  60,  60, 255); break;
            default: col = IM_COL32(200, 200, 200, 255); break;
        }

        bool clamped = false;
        float ex = wx, ey = wy;
        if (ex < p1.x + margin) { ex = p1.x + margin; clamped = true; }
        if (ex > p2.x - margin) { ex = p2.x - margin; clamped = true; }
        if (ey < p1.y + margin) { ey = p1.y + margin; clamped = true; }
        if (ey > p2.y - margin) { ey = p2.y - margin; clamped = true; }

        if (clamped) {
            float dx = wx - ctr.x, dy = wy - ctr.y;
            float len = sqrtf(dx*dx + dy*dy);
            if (len > 0.001f) {
                dx /= len; dy /= len;
                ImVec2 tip(ex + dx * 4.0f, ey + dy * 4.0f);
                ImVec2 a(ex - dy * 6.0f - dx * 4.0f, ey + dx * 6.0f - dy * 4.0f);
                ImVec2 b(ex + dy * 6.0f - dx * 4.0f, ey - dx * 6.0f - dy * 4.0f);
                dl->AddTriangleFilled(tip, a, b, col);
            }
        } else {
            // 朝向箭头
            if (team[i].dirx != 0.f || team[i].dirz != 0.f) {
                float ax = wx + team[i].dirx * 12.0f;
                float ay = wy - team[i].dirz * 12.0f;
                dl->AddLine(ImVec2(wx, wy), ImVec2(ax, ay), col, 2.5f);
            }
            dl->AddCircleFilled(ImVec2(wx, wy), 5.0f, col);
            dl->AddCircle(ImVec2(wx, wy), 7.0f, IM_COL32(255,255,255,220), 16, 1.5f);

            if (g_minimap_show_name && num >= 1 && num <= 99) {
                char lbl[8];
                snprintf(lbl, sizeof(lbl), "%lld", (long long)num);
                dl->AddText(ImVec2(wx + 8.0f, wy - 8.0f), col, lbl);
            }
        }
    }

    // ---- 自己 ----
    {
        ImU32 col = IM_COL32(255, 255, 255, 255);
        const auto &me = team[self_idx];
        ImVec2 sp = ctr;          // 自己在中心
        if (me.dirx != 0.f || me.dirz != 0.f) {
            float ax = sp.x + me.dirx * 14.0f;
            float ay = sp.y - me.dirz * 14.0f;
            dl->AddLine(sp, ImVec2(ax, ay), col, 3.0f);
        }
        dl->AddCircleFilled(sp, 7.0f, col);
        dl->AddCircle(sp, 9.0f, IM_COL32(255,255,255,220), 20, 1.5f);
        dl->AddText(ImVec2(sp.x + 10.0f, sp.y - 8.0f), col, "我");
    }

    dl->PopClipRect();
    dl->AddText(ImVec2(p1.x + 8, p1.y + 6),
                IM_COL32(180, 220, 255, 230), "小地图");
                {
    char dbg[128];
    snprintf(dbg, sizeof(dbg), "X=%.0f  Z=%.0f",
             team[self_idx].x, team[self_idx].z);
    dl->AddText(ImVec2(p1.x + 8, p1.y + 30),
                IM_COL32(255, 255, 0, 255), dbg);
}

    // 调整模式：橙色边框 + 拖动
    if (g_minimap_edit) {
        dl->AddRect(p1, p2, IM_COL32(255, 160, 40, 255), 10.0f, 0, 3.0f);

        ImGuiIO &io = ImGui::GetIO();
        ImVec2 m = io.MousePos;
        bool inside = m.x >= p1.x && m.x <= p2.x && m.y >= p1.y && m.y <= p2.y;

        if (io.MouseDown[0]) {
            if (!g_minimap_dragging && inside) {
                g_minimap_dragging = true;
                g_minimap_drag_dx = m.x - p1.x;
                g_minimap_drag_dy = m.y - p1.y;
            }
            if (g_minimap_dragging) {
                g_minimap_x = m.x - g_minimap_drag_dx;
                g_minimap_y = m.y - g_minimap_drag_dy;
                int sw = displayInfo.width  > 0 ? displayInfo.width  : native_window_screen_x;
                int sh = displayInfo.height > 0 ? displayInfo.height : native_window_screen_y;
                if (g_minimap_x < 0) g_minimap_x = 0;
                if (g_minimap_y < 0) g_minimap_y = 0;
                if (g_minimap_x + g_minimap_w > sw) g_minimap_x = sw - g_minimap_w;
                if (g_minimap_y + g_minimap_h > sh) g_minimap_y = sh - g_minimap_h;
            }
        } else {
            g_minimap_dragging = false;
        }
    }
}

static inline long long drw_cpu_ns() {
    struct timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

// 2026-10-26：求生世界坐标的**跨帧缓存** —— 只服务一个判据：
//   「这个 camp==4 的道具是不是被人拿着」。主循环里道具可能排在宿主**前面**，
//   用当帧坐标会漏判，所以用**上一帧**的坐标。
//   （框右边那排道具名已按用户要求删掉，这里只留坐标记录）
static float s_svx_cur[16]  = {0}, s_svy_cur[16]  = {0};
static float s_svx_prev[16] = {0}, s_svy_prev[16] = {0};
static int   s_svn_cur = 0, s_svn_prev = 0;

// 绘制钩子计时的累加器（文件级：入口块和"循环起点"两处都要用）
static long long g_drw_d0 = 0, g_drw_d1 = 0, g_drw_d2 = 0;
static long long g_drw_acc_total = 0, g_drw_acc_pre = 0, g_drw_acc_loop = 0;
static long long g_drw_acc_minimap = 0, g_drw_acc_ccwin = 0;   // 小地图 / 身份窗各自的耗时
static int       g_drw_n = 0;
static int       g_drw_last_data_count = 0;   // 上一帧遍历了多少实体
static int       g_drw_last_drawn = 0;        // 上一帧画了几个框

void Draw_Main(ImDrawList *Draw){
    std::lock_guard<std::recursive_mutex> data_lock(g_data_mutex);
    // ========================================================================
    // ★★ 2026-10-13 新增：抗锯齿相关的两个可调项 ★★
    //
    //   用户问"抗锯齿怎么调整"，起因是"移动时名字看着一直在晃"✓
    //   查下来本项目**一个抗锯齿设置都没覆盖**（全是 ImGui 默认值），
    //   而能调的只有三处，其中这两处是"运行时 + 几乎免费"的 ✓
    //
    //   ── ① 文字亚像素定位（TextNoPixelSnap）────────────────────────
    //     ImGui 默认会把文字位置**截断到整像素**：
    //       `imgui_draw.cpp`: if (!(Flags & TextNoPixelSnap)) { x=IM_TRUNC(x); y=IM_TRUNC(y); }
    //     这本意是防抖，但**当框位置在半像素边界来回摆时，截断点会在
    //     两个整数之间跳 -> 表现为 1px 的抖动** ✗
    //     打开这个标志 -> 文字按**亚像素**定位，移动时是平滑挪动而不是跳 ✓
    //     代价：静止时字边缘略糊一点点（手机上看不出来）✓
    //     => 这正是"移动时名字晃"最可能的解法，而且一行、可回退 ✓
    //
    //   ── ② 曲线细分容差（CurveTessellationTol）──────────────────────
    //     只影响**圆/曲线**的细分密度（圆角进度条、胶囊底槽、圆点）✓
    //     **不影响字体图集内存**（那个是 16MB 的大头，动不了）✓
    //     默认 1.25；调小 -> 更圆滑，顶点略增 ✓
    //
    //   ⚠ 另一项"字体过采样 OversampleH/V"**不能在运行时改** ✗
    //     它是构建字体图集时的参数，改了必须重建图集 + 重传纹理 ✓
    //     而你的图集已经 16MB（25px 中文 + 两万多字形）-> 加采样内存吃不消 ✗
    //     所以那一项保持现状，界面里给个读数说明它是什么、为什么不能动 ✓
    // ========================================================================
    if (g_esp.text_subpixel) Draw->Flags |= ImDrawListFlags_TextNoPixelSnap;

    // ---- 骨骼坐标轴校准：用自身锚点做一次（g_swap_yz: -1 未定 / 0 不换 / 1 换）----
    //   骨骼原始世界坐标的 y/z 是否要交换取决于引擎侧约定，用自身 objcoor 比一次最省事。
    //   校准失败不影响绘制（非自身骨骼按"不换"处理）。
    if (show_draw_Bones && NativeBones::g_swap_yz.load() < 0) {
        uint64_t self_m = 0;
        if (PySelf::anchor(self_m) && self_m) {
            NativeBones::Skeleton tmp;
            NativeBones::read_unit(self_m, true, tmp);
        }
    }
    else                    Draw->Flags &= ~ImDrawListFlags_TextNoPixelSnap;
    {
        ImGuiStyle& st = ImGui::GetStyle();
        if (st.CurveTessellationTol != g_esp.curve_tol && g_esp.curve_tol > 0.05f)
            st.CurveTessellationTol = g_esp.curve_tol;
    }

    // ★★ 2026-10-13 用户要求：**自动识别模仿者局**，删掉手动开关 ★★
    //   原话：「能否取消"模仿者模式"开关，自动识别模仿者对局还是正常对局，
    //           因为模仿者模式需要绘制身份，而正常对局正常绘制就行」
    //   判据（可靠）：本局 units_by_type[1088] 里**有人** = 模仿者局 ✓
    //     普通局根本不会出现 1088 这个类型 ✓
    //   识别到 -> copycat_mode=1 -> 绘制侧换成「N号 身份」+ 阵营色 ✓
    //   没识别到 -> copycat_mode=0 -> 正常绘制（角色名 / ESP）✓
    //
    //   ⚠ ViewSwitch::g_is_copycat 由扫描更新（400ms 一次），这里每帧跑 ——
    //     用上一次的结果就够了 ✓
    //   ⚠ 关键：**身份簿必须跟着一起开**，否则身份永远读不出来 ✗
    //     （原来靠用户那个开关，现在靠自动识别）
    {
        const bool want = ViewSwitch::g_is_copycat;
        if (want != copycat_mode) {
            copycat_mode = want;
            printf("[自动识别] 本局 -> %s\n", want ? "模仿者局" : "正常对局");
            fflush(stdout);
        }
        auto_copycat = want;
        PySelf::set_copycat_book(want);
    }
    // ★★ 绘制钩子分段计时（2026-10-10）
    //   钩子整体实测 6.3~7.7ms/帧 = 单核 38~46%（CPU 最大项，也是"滑视角框跟不上"的根源），
    //   但这段钱花在哪此前没量过。这里量两段：
    //     pre  = 实体循环之前的准备（自身锚点/座位追踪/假面预扫等）
    //     rest = 实体循环 + 绘制 + 收尾
    //   ⚠ 钩子有**提前 return** 的路径（数据未就绪时），那条路径不会打 g_drw_d1 这个点，
    //     所以累加时必须先判 g_drw_d1 >= g_drw_d0，否则会累进一个负值
    //     （第一版就是这样，打印出来是 -20000ms）。
    {
        long long _now = drw_cpu_ns();
        if (g_drw_d0 && g_drw_d1 >= g_drw_d0 && g_drw_d2 >= g_drw_d1) {
            g_drw_acc_total += _now - g_drw_d0;
            g_drw_acc_pre   += g_drw_d1 - g_drw_d0;
            g_drw_n++;
        }
        Lic::SetDrawPhase(0);   // 帧开始
        Lic::SetDrawTid();       // 登记 TID（冻结快照按 TID 看内核状态）
        if (show_draw_Prop) SurvSkill::Tick();     // 2026-10-26：道具采样（200ms 限流，关了零开销）
        AutoPallet::g_hunter_ball = PyGenius::hunter_ball();   // 2026-10-26：台球形态（自动盖板要屏蔽它）
        /*   2026-10-26：**自身身份**（自动技能/飞轮的额外识别 + 记者按住时长用）
         *   自身 scene 用 PySelf::anchor 拿，名字在 data[] 里同对象那条上，
         *   道具用 SurvSkill 的缓存（model 里有 xiangshuiping 就是忘忧之香）。
         */
        {
            uint64_t _sc = 0;
            PySelf::anchor(_sc);
            const char *_sn = "";
            int _cnt = 0;
            bool _xiang = false;
            /*   2026-10-26：**崩溃修复**
             *   实测 SIGSEGV 就出在这里 —— data_count 是采样线程在改的，
             *   绘制线程读到它时数组可能还没填满 / str 是野指针，
             *   strcmp/strstr 一碰就段错误（之前"台球形态误判 + 自己白框"也是读到错位数据的表现）。
             *   现在：硬边界 3000 + obj/str 双非空。
             */
            if (_sc) {
                const int _lim = (data_count > 0 && data_count < 3000) ? data_count : 0;
                for (int _i = 0; _i < _lim; ++_i) {
                    if (!data[_i].obj) continue;
                    if ((uint64_t)data[_i].obj == _sc) { _sn = data[_i].str; break; }
                }
                const SurvSkill::ItemInfo *_ii = SurvSkill::ItemOfScene(_sc);
                if (_ii) {
                    _cnt = _ii->n;
                    for (int _k = 0; _k < _ii->n; ++_k)
                        if (strstr(_ii->it[_k].model, "xiangshuiping")) _xiang = true;
                }
            }
            AutoSkill::g_self_is_report = (strcmp(_sn, "记者") == 0);
            AutoSkill::g_self_is_prof   = (strcmp(_sn, "教授") == 0);
            AutoSkill::g_self_has_xiang = _xiang;
            AutoSkill::g_self_item_cnt  = _cnt;
            /*   2026-10-26：**身份读不到就别拦**
             *   名字为空说明 data[] 里还没这条（换局中/自检没跑），
             *   这时如果按"不满足条件"处理，就会把自动技能整个关死。
             */
            AutoSkill::g_self_unknown  = (_sn[0] == 0);
            AutoSkill::g_self_has_flywheel = false;    // 下面天赋循环里再置真
            AutoSkill::g_self_fly_probed   = false;
        }
        /*   2026-10-25：**冻结探针**（用户反馈"玩着玩着卡住"）
         *   只在帧间隔 > 500ms 时往 /data/adb/.d/freeze.log 追加一行，
         *   平时只有一次减法，零开销。记录：
         *     间隔、上一帧停在哪个阶段、实体数、授权掩码
         *   阶段含义：0=帧开始 1=读实体前 2=实体循环结束 3=呈现前 4=呈现后
         */
        {
            static long long s_fz_last = 0;
            long long fz_now = drw_cpu_ns();
            if (s_fz_last && fz_now - s_fz_last > 500LL * 1000 * 1000) {
                FILE* fzf = fopen("/data/adb/.d/freeze.log", "a");
                if (fzf) {
                    fprintf(fzf, "[冻结] 间隔=%.2f秒 阶段=%d 实体=%d 掩码=0x%llx\n",
                            (double)(fz_now - s_fz_last) / 1e9,
                            Lic::GetDrawPhase(), data_count,
                            (unsigned long long)Lic::mask());
                    fclose(fzf);
                }
            }
            s_fz_last = fz_now;
        }

        g_drw_d0 = _now;
        g_drw_d1 = 0; g_drw_d2 = 0;         // 清掉，下次提前 return 时不会用到陈旧值
        if (g_drw_n >= 60) {
            /*   2026-10-24 诊断：每 5 秒一行"绘制还活着"，卡死时能看出停在哪个阶段 */
            {
                static time_t wd_last = 0;
                static unsigned long wd_frames = 0;
                ++wd_frames;
                time_t wd_now = time(nullptr);
                if (wd_now - wd_last >= 5) {
                    wd_last = wd_now;
                    printf("[看门狗] 绘制 帧=%lu\n", wd_frames);
                    fflush(stdout);
                }
            }
            printf("[绘制] 每帧: 准备=%.2f 实体循环=%.2f 循环后=%.2f 合计=%.2f ms | 遍历=%d 画出=%d\n",
                   g_drw_acc_pre  / (double)g_drw_n / 1e6,
                   g_drw_acc_loop / (double)g_drw_n / 1e6,
                   (g_drw_acc_total - g_drw_acc_loop - g_drw_acc_pre) / (double)g_drw_n / 1e6,
                   g_drw_acc_total / (double)g_drw_n / 1e6,
                   g_drw_last_data_count, g_drw_last_drawn);
            printf("[绘制]   其中 小地图=%.2f  身份窗=%.2f ms\n",
                   g_drw_acc_minimap / (double)g_drw_n / 1e6,
                   g_drw_acc_ccwin   / (double)g_drw_n / 1e6);
            fflush(stdout);
            fflush(stdout);
            g_drw_acc_total = g_drw_acc_pre = g_drw_acc_loop = 0;
            g_drw_acc_minimap = g_drw_acc_ccwin = 0; g_drw_n = 0;
        }
    }
    // ★ 求生坐标缓存滚动（上一帧 -> prev，本帧重新收集）
    for (int _k = 0; _k < s_svn_cur && _k < 16; _k++) {
        s_svx_prev[_k] = s_svx_cur[_k];
        s_svy_prev[_k] = s_svy_cur[_k];
    }
    s_svn_prev = (s_svn_cur < 16) ? s_svn_cur : 16;
    s_svn_cur  = 0;
    ShouldSkipEntityResetFrame();      // 每帧重置"空槽位过滤"的安全网计数
    g_dbg_data_camp2 = 0;              // 诊断：统计 data[] 里 camp2 的条数
    for (int i = 0; i < data_count; i++)
        if (data[i].camp == 2) g_dbg_data_camp2++;
    // ★★ 2026-10-13 用户要求：**删除整个「自动震慑」功能** ★★
    //   原话：「算了，删除这个功能吧，删除自动震慑页的全部包括自动震慑页」
    //   已删除的东西：
    //     · TouchProxy 触摸透明代理（本来就废弃了，EBUSY 那条死路）
    //     · JokerHold 小丑代按 / 自动松手
    //     · AutoZhenShe 200Hz 判定线程
    //     · AutoShock 自动出刀震慑
    //     · 「触摸 -> 自动震慑」子页 + 所有相关配置项与诊断 dump
    // ★★ 改视角：扫描角色列表（2026-10-13）★★
    //   每 400ms 扫一次就够（角色不会瞬移），别每帧读对象图 ✓
    //   自身坐标由下面的实体循环写入 ViewSwitch::g_self_* ✓
    if (ViewSwitch::g_on) {
        static int64_t s_vs_last = 0;
        const int64_t vs_now = (int64_t)(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch()).count());
        if (vs_now - s_vs_last > 400) {
            s_vs_last = vs_now;
            ViewSwitch::scan();
            ViewSwitch::Tick();     // ★ 目标不在了就自动还原我的视角，免得卡在别人身上
        }
    }
    RescueLog::Tick();
JokerSpeed::Tick();
AutoCrash::Tick();          // ★ 自动坠机
AntiCowboy::Tick();         // ★ 防牛仔

{
        static uint64_t s_ml_obj[64];
        static float    s_ml_x[64], s_ml_y[64];
        static int      s_ml_camp[64];
        static const char *s_ml_cls[64];
        int ml_n = 0;
        for (int i = 0; i < data_count && ml_n < 64; i++) {
            if (data[i].camp != 2 || !data[i].obj || !data[i].objcoor) continue;
            s_ml_obj[ml_n]  = data[i].obj;
            s_ml_x[ml_n]    = getFloat(data[i].objcoor + (uint64_t)SecOff::u64(SecOff::DG_46));
            s_ml_y[ml_n]    = getFloat(data[i].objcoor + (uint64_t)SecOff::u64(SecOff::DG_47));
            s_ml_camp[ml_n] = data[i].camp;
            s_ml_cls[ml_n]  = data[i].class_name;
            ml_n++;
        }
        if (ml_n > 0) MirrorLearn::Feed(ml_n, s_ml_obj, s_ml_x, s_ml_y, s_ml_camp, s_ml_cls);
    }
    // ★ 小丑零件：每 250ms 刷一次就够（零件不会瞬移），别每帧读对象图
    if (JokerParts::g_show) {
        static int64_t s_jp_last = 0;
        const int64_t jp_now = JokerParts::now_ms();
        if (jp_now - s_jp_last > 250) { s_jp_last = jp_now; JokerParts::refresh(); }
    }
    // ★ 约瑟夫相机：同样 250ms 刷一次
    if (JosephCamera::g_show) {
        static int64_t s_jc_last = 0;
        const int64_t jc_now = JosephCamera::now_ms();
        if (jc_now - s_jc_last > 250) { s_jc_last = jc_now; JosephCamera::refresh(); }
    }   // 高频救人状态记录（未开启时立即返回，零开销）
    if (g_share_frame_hook) g_share_frame_hook();   // 2026-10-26：共享地图数据
    if (libbase == 0 || read_state == 0) return;  // 数据未就绪，跳过本帧绘制
    if (g_reload_cooldown > 0) { g_reload_cooldown--; return; }   // 2026-10-26：重载冷却



    int char_count = 0;
    
    

    // 触摸红框（如果开启了"调整触摸区域"）
    AutoSkill::TouchRect_Draw(Draw);
AntiCowboy::TouchRect_Draw(Draw);
AutoCrash::TouchRect_Draw(Draw);     // ★ 自动坠机的触摸区显示

    // ---- 自身锚点：引擎自己持有的答案。相机深度启发式兜底已停用，这是唯一来源 ----
    // g_cam_ctrl.unit 就是"当前视角/操控的单位"，切到机械玩偶/梦之信徒时会跟着换，
    // 所以这里每帧重取：一旦换了操控对象，自身锚点立刻跟上。
    // (不能用 g_unit —— 那是"我的主角色"，切从属时纹丝不动，会高亮错人。见 PySelf.h)
    uint64_t auth_self = 0;
    const bool has_auth_self = PySelf::anchor(auth_self);
    if (has_auth_self) {
        self_obj = (uintptr_t)auth_self;
        int c = PySelf::camp();
        if (c == 1 || c == 2) self_camp = (uintptr_t)c;
    }

    // ---- VP 矩阵：每一跳都校验，读出来的 16 个数也校验 ----
    // 原来是 getPtr64(getPtr64(..)+0xa58)+0x2c0 一步到位：任一跳读成 0(内存压力下驱动静默返回 0，
    // 或跨对局时 +0xa58 真的为空)就得到 0x2c0 这种地址，后面 vm_readv 失败也没人看返回值。
    // 现在校验不过就不更新 Matrix/matrix[]，沿用上一帧，见 MATRIX_STALE_MAX。
    bool matrix_fresh = false;
    {
        //   两跳偏移都改成运行时解密（未授权 → 0 → 整条链断掉，读不到矩阵）
        const uint64_t off_p2  = SecOff::u64(SecOff::MAT_P2);
        const uint64_t off_mat = SecOff::u64(SecOff::MAT_KEY);
        if (off_p2 && off_mat) {
            uint64_t root = getPtr64(libbase + MatrixOffset);
            uint64_t mobj = is_user_ptr(root) ? getPtr64(root + off_p2) : 0;
            if (is_user_ptr(mobj)) {
                float tmp[16];
                if (vm_readv(mobj + off_mat, tmp, sizeof(tmp)) && matrix_finite(tmp)) {
                    Matrix = mobj + off_mat;
                    memcpy(matrix, tmp, sizeof(matrix));
                    matrix_fresh = true;
                }
            }
        }
    }
    if (matrix_fresh) g_matrix_stale = 0;
    else if (g_matrix_stale <= MATRIX_STALE_MAX) g_matrix_stale++;
    const bool matrix_usable = g_matrix_stale <= MATRIX_STALE_MAX;
    
    // ---- 红夫人镜面：两个来源，先放下的镜子，其次准备阶段的预览镜子 ----
    // (1) 镜子已放下：本体与镜中红夫人连线的垂直平分线(已用 Python 侧 MaryMirrorUnit 验证)。
    //     镜子没放时镜中红夫人停在 y≈-1000/-2000，Z>=-300 挡掉。
    // (2) 准备放镜(SkillMaryPlaceMirrorPrepare)：镜中红夫人还在地下，但预览镜子已经可见。
    //     预览镜子的 objcoor 是 3x3 旋转(+0x78 起、每行 12 字节) + 位置(+0xa0)：
    //       +0x90/+0x98 = 局部 Z 轴 = 投掷方向 = 镜面法向(与 Python rtc_model.world_transformation 第 3 行一致)
    //     可见位 +0x73 只在准备阶段为 1。
    mirror = false;
    bool body_valid = false, mirror_valid = false;
    if (redqueen_obj != 0) {
        uintptr_t redqueen_coor = getPtr64(redqueen_obj + (uint64_t)SecOff::u64(SecOff::DG_48));
        if (redqueen_coor != 0) {
            redqueen_x = getFloat(redqueen_coor + (uint64_t)SecOff::u64(SecOff::DG_49));
            redqueen_z = getFloat(redqueen_coor + (uint64_t)SecOff::u64(SecOff::DG_50));
            redqueen_y = getFloat(redqueen_coor + (uint64_t)SecOff::u64(SecOff::DG_51));
            body_valid = redqueen_z >= -300 && redqueen_x != 0 && redqueen_y != 0;
        }
    }
    if (redqueen_mirror_obj != 0) {
        uintptr_t mirror_coor = getPtr64(redqueen_mirror_obj + (uint64_t)SecOff::u64(SecOff::DG_52));
        if (mirror_coor != 0) {
            redqueen_mirror_x = getFloat(mirror_coor + (uint64_t)SecOff::u64(SecOff::DG_53));
            redqueen_mirror_z = getFloat(mirror_coor + (uint64_t)SecOff::u64(SecOff::DG_54));
            redqueen_mirror_y = getFloat(mirror_coor + (uint64_t)SecOff::u64(SecOff::DG_55));
            mirror_valid = redqueen_mirror_z >= -300 && redqueen_mirror_x != 0 && redqueen_mirror_y != 0;
        }
    }
    if (body_valid && mirror_valid) {
        float mx = (redqueen_x + redqueen_mirror_x) / 2.0f, my = (redqueen_y + redqueen_mirror_y) / 2.0f;
        float dx = redqueen_mirror_x - redqueen_x,      dy = redqueen_mirror_y - redqueen_y;   // 法向
        if (dx * dx + dy * dy > 1e-4f) {
            mirror_line_x1 = mx;      mirror_line_y1 = my;
            mirror_line_x2 = mx - dy; mirror_line_y2 = my + dx;                              // 沿镜面方向
            mirror = true;
        }
    }
    if (!mirror && mirror_preview_obj != 0) {
        uint8_t visible = 0;
        vm_readv(mirror_preview_obj + (uint64_t)SecOff::u64(SecOff::DG_56), &visible, 1);
        uintptr_t cp = getPtr64(mirror_preview_obj + (uint64_t)SecOff::u64(SecOff::DG_57));
        if (visible == 1 && cp != 0) {
            float px0 = getFloat(cp + (uint64_t)SecOff::u64(SecOff::DG_58)), pz0 = getFloat(cp + (uint64_t)SecOff::u64(SecOff::DG_59)), py0 = getFloat(cp + (uint64_t)SecOff::u64(SecOff::DG_60));
            float nx = getFloat(cp + (uint64_t)SecOff::u64(SecOff::DG_61)), ny = getFloat(cp + (uint64_t)SecOff::u64(SecOff::DG_62));
            float norm_sq = nx * nx + ny * ny;
            if (px0 != 0 && py0 != 0 && pz0 >= -300 && norm_sq > 0.9f && norm_sq < 1.1f) {
                mirror_line_x1 = px0;      mirror_line_y1 = py0;
                mirror_line_x2 = px0 - ny; mirror_line_y2 = py0 + nx;
                mirror = true;
            }
        }
    }
    if (matrix_fresh && !first_matrix_logged){
        first_matrix_logged = true;
        printf("[矩阵]");
        for (int i = 0; i < 16; i++) printf(" %.4f", matrix[i]);
        printf("\n");
    }  // 直接从Matrix读16个float
    // 模仿者局没有监管，类名兜底会把场景里的 chr\boss\spkantan(磁铁)当成监管报出来，不显示
    // ★ 2026-10-22（用户反馈："预知监管在大厅界面也在显示"）：加阶段限制★
    //   原来只有 `show_draw_prophet && !copycat_active()`，**没有任何阶段判断**，
    //   而 prophet_text 是上一次读到就一直保留的（清零机制只在新一轮扫描时），
    //   所以退回大厅后那行字还挂在屏幕上。
    //   现在：**只在准备房间或局内**才画；大厅（非 wait、非 scene_battle）不画。
    //   用的都是已有判据：in_prep_scene()（h55_wait*）+ in_battle_scene()（scene_battle）。
    // ★★ 2026-10-22 （第二次修）：不能用 in_battle_scene() ★★
    //   它的语义只是"路径含 scene_battle"，实测会客室也命中：
    //     scene/scene_battle/h55_guestroom_sm/h55_guestroom_sm.scn
    //   于是在大厅/会客室也判成"局内" -> 预知监管照样显示 ✗
    //   改用**玩家单位**判局内：!PySelf::in_lobby()
    //     准备房间 -> 靠 in_prep_scene()（h55_wait*）
    //     局内    -> 有玩家单位 -> in_lobby()=0 -> !in_lobby()=1
    //     大厅/会客室 -> 无玩家单位（实测只有道具）-> in_lobby()=1 -> 不显示 ✓
    const bool prophet_phase_ok = in_prep_scene() || !PySelf::in_lobby();
    if (show_draw_prophet && !PySelf::copycat_active() && prophet_phase_ok){
        //   ★ 字号/颜色改为可调（原来是写死的 25 + color_red）✓
        //   ⚠ 用 CalcTextSizeA（带字号）而不是 CalcTextSize(…,0,25) ✗
        ImFont* pf = ImGui::GetFont();
        float ps = g_esp.prophet_size;
        ImVec2 textSize = pf->CalcTextSizeA(ps, FLT_MAX, 0.0f, prophet_text);
        Draw->AddText(pf, ps, {px-(textSize.x/2), 130}, g_esp.prophet_color, prophet_text);
    }

    // 准备阶段：预知那行下面按座位号列出四个求生者的大天赋，**只在我是监管时显示**(用户要求)
    //   数据来自 g_avatar.final_genius_dict(实时；60 秒差分实测只有它跟着对面换天赋变)，座位号来自 uid2pos，见 PyGenius.h
    //   不再显示监管辅助特质(用户 2026-09-24 决定不要)；也不再看花名册 —— 大厅里那是上一局的旧数据
    //   ★ 诊断：逐条记录准备阶段天赋那行的前置条件（用户反馈"不见了"）✓
    //     ⚠ 用**独立的诊断变量**，别去动已有的 g_ds 计数 ✗
    g_dbg_prep_show   = show_draw_prophet ? 1 : 0;
    g_dbg_prep_lobby  = PySelf::in_lobby() ? 1 : 0;
    g_dbg_prep_hunter = PyGenius::prep_as_hunter() ? 1 : 0;
    g_dbg_prep_civ    = PyGenius::civ_size();
    g_dbg_prep_drawn  = 0;
    //   ★★ 2026-10-13 诊断：准备阶段"天赋列表"那行的逐环节记录 ★★
    //     用户现场反馈：**设置页显示识别到了 4 条天赋，但预知监管下方不显示** ✗
    //     => 数据有、条件却没过，或者拼接出来是空串 ✗
    //     所以把**每个环节**都记下来（进入/各条件/候选条数/拼出的串长度）
    //     然后用 MCP 实时读，而不是靠看代码猜 ✗
    {
        char tmp[320]; int tlen = 0;
        PyGenius::Info ti; memset(&ti, 0, sizeof(ti));
        ti.camp = 2; ti.genius_state = PyGenius::GENIUS_COMPLETE;
        PyGenius::GeniusLine tg;
        PyGenius::genius_segments(ti, tg);
        int n2 = PyGenius::civ_size();
        int used = 0;
        for (int i = 0; i < n2 && tlen < (int)sizeof(tmp) - 48; i++) {
            int seat = 0, pid = 0; uint32_t bits = 0;
            if (!PyGenius::civ_at(i, seat, pid, bits) || bits == 0) continue;
            used++;
            PyGenius::Info t2; memset(&t2, 0, sizeof(t2));
            t2.camp = 2; t2.genius_bits = bits; t2.genius_state = PyGenius::GENIUS_COMPLETE;
            PyGenius::GeniusLine sg;
            PyGenius::genius_segments(t2, sg);
            int w = seat ? snprintf(tmp + tlen, sizeof(tmp) - tlen, "%s%d号[%s%s%s]", tlen ? "  " : "", seat, sg.pre, sg.flywheel, sg.post)
                         : snprintf(tmp + tlen, sizeof(tmp) - tlen, "%s?号[%s%s%s]", tlen ? "  " : "", sg.pre, sg.flywheel, sg.post);
            if (w < 0) break;
            tlen += w;
        }
        g_dbg_prep_show   = show_draw_prophet ? 1 : 0;
        g_dbg_prep_lobby  = PySelf::in_lobby() ? 1 : 0;
        //   ★ 用**新的**判据（原来是 in_lobby()/in_battle_scene()，两个都区分不了 ✗）
        g_dbg_prep_notbattle = in_prep_scene() ? 1 : 0;
        g_dbg_prep_hunter = PyGenius::prep_as_hunter() ? 1 : 0;
        g_dbg_prep_civ    = n2;          // civ_size()
        g_dbg_prep_used   = used;        // bits != 0 的条数（真正的候选）
        g_dbg_prep_len    = tlen;        // 拼出来的串长度（>0 才会画）
        snprintf(g_dbg_prep_str, sizeof(g_dbg_prep_str), "%s", tmp);
        g_dbg_prep_drawn  = 0;           // 下面真画了会置 1
        //   ⚠ 这是**诊断复刻**，不代表绘制真的跑了 —— 但能区分
        //     "数据/条件不对" 和 "条件都对、就是没画" ✓
    }
    //   ★★☠☠ 2026-10-13 真因：这里原来用的是 `PySelf::in_lobby()` —— **条件永远不成立** ✗✗
    //
    //   用户现场证据：「我在准备阶段，预知监管下方不显示求生天赋，
    //                   但**设置页里显示识别出来了四条天赋**」
    //
    //   ── 为什么数据有、行却不画 ──────────────────────────────────
    //   `in_lobby()` 的语义是"**没有玩家单位 = 局外**"：
    //     PySelf.h 的 check_lobby()：
    //       if (sz==1 && (dg==1||dg==2||dg==HALL_COMPONENT_TYPE||dg==COPYCAT_PLAYER_TYPE))
    //           has_player = true;
    //       in_lobby = !has_player;
    //     而 `HALL_COMPONENT_TYPE(69)` 的注释写得明明白白：
    //       "**准备阶段站大厅的玩家**" ✗
    //   => 准备阶段**有玩家** -> has_player=true -> **in_lobby()=false** ✗
    //   => 这一行要求 in_lobby() 为真 -> **在准备阶段永远为假** -> 整行不画 ✗
    //
    //   **它和自己的用途自相矛盾**：
    //     这行的用途是"准备阶段显示求生天赋"，
    //     却用了一个"准备阶段必定为假"的条件 ✗
    //
    //   ── 为什么设置页那 4 条能看到 ────────────────────────────────
    //     设置页直接读 `g_civ_buf`（来自 final_genius_dict），**不看 in_lobby()** ✓
    //     而这一行被 in_lobby() 挡住了 ✗
    //     两者**同一个数据源** -> 数据有、行不画 = 纯粹的条件错误 ✓
    //     （我一开始还怀疑"两条数据源不同"，是错的 —— 查了才发现同源 ✗）
    //
    //   ── 修法 ───────────────────────────────────────────────────
    //     换成**已有的、语义正确**的判据：`!PyMiniMap::in_battle_scene()`
    //       = "还挂在 scene_battle 下 = 对局已开始" -> 取反 = **还没开打** ✓
    //     这正是"准备阶段"的定义，而且不受 units_by_type 键号变动影响 ✓
    //   ★★ 2026-10-21 修「刚画出来又消失」★★
    //     原来门槛里带 `civ_size() > 0`，而 PyGenius 每次重读 final_genius_dict 时
    //     会先把 g_civ_n 清 0、读不全就留 0 —— 于是那一行会**一闪一闪**。
    //     现在：门槛只保留"准备阶段 + 我是监管 + 开关开"，绘制时再用**最近 5 秒的缓存**兜底，
    //     读不到的那几帧继续显示上一次的内容（不会把"读不到"当成"没有天赋"）。
    //   ★ 进局后清一次"我是监管"标记：PyGenius 那边不再因为面板消失而清，
    //   ★ 不在绘制侧清"我是监管"了：in_prep_scene() 依赖 PyMiniMap，进局时会在
    //     false/true 之间翻，一翻就把标记清掉（屏幕上一会儿有一会儿没）。
    //     真正"换了一局"的信号交给 PyGenius："准备房间的面板对象换了"——
    //     那才是新的一轮，且不会因为面板被销毁而误判。
    //   ★ 这里用**独立的「预知天赋」开关**，不再跟随「预知监管」（用户要求）
    if (show_draw_prep_genius && in_prep_scene() && PyGenius::prep_as_hunter()) {
        static char   s_last_line[320] = {0};
        static double s_last_time = -1e9;
        const double prep_now = ImGui::GetTime();
        char line[320];
        int len = 0;
        int n = PyGenius::civ_size();
        for (int i = 0; i < n && len < (int)sizeof(line) - 48; i++) {
            int seat = 0, pid = 0; uint32_t bits = 0;
            if (!PyGenius::civ_at(i, seat, pid, bits) || bits == 0) continue;
            PyGenius::Info tmp;
            memset(&tmp, 0, sizeof(tmp));
            tmp.camp = 2; tmp.genius_bits = bits; tmp.genius_state = PyGenius::GENIUS_COMPLETE;
            PyGenius::GeniusLine seg;
            PyGenius::genius_segments(tmp, seg);
            // GeniusLine 是 前/飞轮/后 三段，三段都要拼，漏了中间那段飞轮就没了
            int w = seat ? snprintf(line + len, sizeof(line) - len, "%s%d号[%s%s%s]",
                                    len ? "  " : "", seat, seg.pre, seg.flywheel, seg.post)
                         : snprintf(line + len, sizeof(line) - len, "%s?号[%s%s%s]",
                                    len ? "  " : "", seg.pre, seg.flywheel, seg.post);
            if (w < 0) break;
            len += w;
            if (len >= (int)sizeof(line)) { len = (int)sizeof(line) - 1; break; }
        }

        //   ★ 读不到的那几帧不当作"没有天赋"：用最近 5 秒的内容兜底，避免一行字一闪一闪
        if (len > 0) {
            snprintf(s_last_line, sizeof(s_last_line), "%s", line);
            s_last_time = prep_now;
        } else if (s_last_line[0] && (prep_now - s_last_time) < 5.0) {
            snprintf(line, sizeof(line), "%s", s_last_line);
            len = (int)strlen(line);
        }

        if (len > 0) {
            //   ★ 字号/颜色可调（原来写死 25 + 黄色）✓
            //   y 固定 165：用户明确说过"要改的不是准备阶段这一行"
            ImFont* pf2 = ImGui::GetFont();
            float ps2 = g_esp.prophet_size;
            ImVec2 ts2 = pf2->CalcTextSizeA(ps2, FLT_MAX, 0.0f, line);
            Draw->AddText(pf2, ps2, {px - (ts2.x / 2), 165}, g_esp.prep_color, line);
            g_dbg_prep_drawn = 1;    // ★ 诊断：真的画了 ✓
        }
    }

    // 已破译 4 台后，剩下那台在修的就是最后一台 —— 单独用进度条标出来
    {
        float last_progress = 0.f;
        //   ★ 2026-10-13：加了独立的 `show_draw_last_gen` 开关 ✓
        //     原来它挂在 `show_draw_secret_mechine` 下面 —— 关掉密码机就一起没了 ✗
        //     搬进「显示密码机」之后它是个独立 Fold，所以判据也要跟着独立 ✓
        if (show_draw_secret_mechine && show_draw_last_gen &&
            PyProgress::last_generator(last_progress)){
            char label[64];
            snprintf(label, sizeof(label), "最后一台 %.1f%%", last_progress);
            //   ★ 字号/进度条尺寸改为可调（原来写死 25 / 160×14）✓
            ImFont* lg = ImGui::GetFont();
            float lgs = g_esp.last_gen_size;
            ImVec2 ts = lg->CalcTextSizeA(lgs, FLT_MAX, 0.0f, label);
            const float bar_w = g_esp.last_gen_w, bar_h = g_esp.last_gen_h, gap = 8.0f;
            float x0 = px - (bar_w + gap + ts.x) / 2.0f;
            float y0 = 158.0f;                                  // 预知监管那行(y=130)的下一行
            ImColor c = progress_color(last_progress);

            //   ★★ 2026-10-13 用户要求：**改成和其他密码机一样的圆润边角** ★★
            //     原来这里只有"一个直角填充 + 一个直角描边"，观感和
            //     普通密码机（胶囊形：底槽 + 填充 + 描边）**明显不一致** ✗
            //     用户原话：「把'最后一台'的绘制进度条改成和其他密码机一样的圆润边角」
            //   做法直接照搬密码机那段（保持两处一致，别再各写一套 ✗）：
            //     底槽 + 填充 + 描边 都做成胶囊 ；填充很短时圆角不超过自身宽度一半，
            //     否则会"扁成一条线"（这条经验来自密码机那段）✓
            const float rad = bar_h * 0.5f;
            // 底槽
            Draw->AddRectFilled({x0, y0}, {x0 + bar_w, y0 + bar_h},
                                (ImU32)g_esp.gen_bar_bg, rad);
            // 填充
            const float fill_w = bar_w * (last_progress / 100.0f);
            if (fill_w > 0.0f) {
                const float fr = (fill_w < bar_h) ? fill_w * 0.5f : rad;
                Draw->AddRectFilled({x0, y0}, {x0 + fill_w, y0 + bar_h}, c, fr);
            }
            // 描边
            Draw->AddRect({x0, y0}, {x0 + bar_w, y0 + bar_h},
                          ImColor(255,255,255,180), rad, 0, 1.2f);
            Draw->AddText(lg, lgs, {x0 + bar_w + gap, y0 + bar_h/2.0f - ts.y/2.0f}, c, label);
        }
    }


    // ===================== 骑士「战术预见」固定显示 =====================
    //   ★ 2026-10-21 用户要求：删掉悬浮窗，固定画在**预知监管下方**这块 HUD 区域；
    //     字号/颜色走设置页「骑士头盔预测」，位置不给调（固定居中）。
    //   ★ 显示时长：**只在"这一顶头盔的第一次预测"出现时开始计时 10 秒**，
    //     第二次预测不再续期（用户明确要求），所以第二次可能赶在尾巴上一起显示。
    //   ★ 渲染空档续期：加载/过场/窗口重建期间 HUD 根本没画，10 秒白走 ——
    //     一旦发现"上一帧距今 > 500ms"，就把显示窗口挪到现在，保证玩家真的看得到。
    {
        static int64_t s_last_hud_ms = 0;
        static int64_t s_next_log = 0;
        const int64_t hud_now = PyKnight::now_ms();
        // 第一次画 HUD（比如刚过完加载）也要续期 —— 否则开局那顶头盔的 10 秒早就走完了
        if (s_last_hud_ms == 0 || hud_now - s_last_hud_ms > 500)
            PyKnight::extend_display(hud_now);
        s_last_hud_ms = hud_now;
        // 诊断（2 秒一条）：确认 HUD 到底有没有在画、当时是不是"新鲜"
#if ENABLE_LOG
        if (hud_now >= s_next_log) {
            s_next_log = hud_now + 2000;
            AppLogRaw("[hud] 骑士HUD绘制 fresh=%d pick_n=%d show_draw=%d",
                      PyKnight::predict_fresh() ? 1 : 0,
                      PyKnight::predict_pub().pick_n, show_draw_Knight ? 1 : 0);
        }
#endif
    }
    // ===================== 骑士「战术预见」的显示条件 =====================
    //   ★ 2026-10-21 用户要求（"测试"开关）：
    //     开关 **开** -> 无条件显示（自身不是监管、甚至自己就是骑士，都显示）
    //     开关 **关** -> **只有自身是监管**时才显示（看对面的骑士才有意义）
    //   ⚠ 自身阵营每帧从 PySelf::camp() 取（1=监管 2=求生），切从属也跟着变
    const bool self_is_hunter = has_auth_self && self_camp == 1;
    const bool knight_hud_allow = g_knight_test_mode ? true : self_is_hunter;
    {
        static int s_last_allow = -1;
        const int a = knight_hud_allow ? 1 : 0;
        if (a != s_last_allow) {
            s_last_allow = a;
            AppLogRaw("[hud] 预见显示条件: 测试=%d 自身监管=%d -> %s",
                      g_knight_test_mode ? 1 : 0, self_is_hunter ? 1 : 0,
                      a ? "显示" : "不显示");
        }
    }
    if (show_draw_Knight && knight_hud_allow && PyKnight::predict_fresh()) {
        char pv[48];
        PyKnight::predict_short(pv, sizeof(pv));
        char line[64];
        snprintf(line, sizeof(line), "预见:%s", pv);
        ImFont* kfp = ImGui::GetFont();
        const float kszp = g_esp.knight_size;
        const ImVec2 kts = kfp->CalcTextSizeA(kszp, FLT_MAX, 0.0f, line);
        //   y = 200：预知监管那行在 130、准备阶段天赋 165、"最后一台" 158，200 正好在其下方且不打架
        Draw->AddText(kfp, kszp, {px - kts.x / 2.0f, 200.0f}, g_esp.knight_color, line);
    }
    // 以上是屏幕坐标的 HUD(预知/花名册/最后一台)，不依赖矩阵；以下全部要投影
    if (!matrix_usable) return;
    // ★ 相机俯仰角：世界深度方向在相机空间竖直分量的反正切
//   matrix[9]：深度 D.Y 对屏幕 y 的系数
//   matrix[11]：深度 D.Y 对相机深度（w）的系数


    // ---- 大门开门进度 ----
    // 大门**不走下面那个实体循环**：getscene() 只认 prop_76/sender，大门那条分支收不到东西
    // (原版认 6 个类名，Name.h 重构时缩成 2 个，门/箱/椅三条分支就成了孤儿)。
    // 所以这里直接拿 Python 侧的 model+0x20 场景对象自己投影。
    // 也不能靠场景侧判据补救：实测两扇门的类名都不一样(prop_30 / wooddoor01a)，
    // 而且 +0x240 两扇都是 0 —— "最可靠的存在性判据"在大门上失效。
    if (show_draw_Door){
            // ★ 小丑地面零件（推进器 / 风翼 / 钻头）—— **只画名字和距离，不画方框**
    //   数据不来自 data[]（零件根本不在 read_thread 的 curArray 里），
    //   而是走 CPython 对象图的 units_by_type[209]。详见 joker_parts.h 文件头。
    if (JokerParts::g_show) {
        JokerParts::draw(Draw, matrix, px, py, Z.X, Z.Z, Z.Y, dist_scale);
    }
    // ★ 约瑟夫相机（只画名字和距离，不画框）
    if (JosephCamera::g_show) {
        JosephCamera::draw(Draw, matrix, px, py, Z.X, Z.Z, Z.Y, dist_scale);
    }

for (int i = 0; i < PyProgress::door_count(); i++){
            PyProgress::DoorEntry door;
            if (!PyProgress::get_door(i, door)) continue;
            if (PyProgress::door_opened(door)) continue;          // 已经开了的不用再画
            if (!door.can_open) continue;                         // 还没通电(不可开)的不画

            uintptr_t coor_ptr = getPtr64((uintptr_t)door.scene_obj + (uint64_t)SecOff::u64(SecOff::DG_63));
            if (coor_ptr == 0) continue;
            float mx = getFloat(coor_ptr + (uint64_t)SecOff::u64(SecOff::DG_64));
            float mz = getFloat(coor_ptr + (uint64_t)SecOff::u64(SecOff::DG_65));          // +0xa4 是高度
            float my = getFloat(coor_ptr + (uint64_t)SecOff::u64(SecOff::DG_66));
            if (mx == 0 && my == 0) continue;

            float cam = matrix[3]*mx + matrix[7]*mz + matrix[11]*my + matrix[15];
            if (cam <= 0.01f) continue;                    // 在相机背后
            float sx = px + (matrix[0]*mx + matrix[4]*mz + matrix[8]*my + matrix[12]) / cam * px;
            float sy = py - (matrix[1]*mx + matrix[5]*(mz+8.5f) + matrix[9]*my + matrix[13]) / cam * py;

            int meters = (int)(sqrt(pow(mx - Z.X, 2) + pow(my - Z.Y, 2) + pow(mz - Z.Z, 2)) / dist_scale);
            char label[64];
            if (door.is_opening)      snprintf(label, sizeof(label), "[%.1f%%]", door.progress);
            else                snprintf(label, sizeof(label), "[%.1f%%]",  door.progress);   // 不可开时按灰色画，见下

            // 有进度就在文字上方画一条，跟密码机那套一致
            if (door.progress > 0.05f){
                const float bar_w = 120.0f, bar_h = 10.0f;
                float bx = sx - bar_w / 2.0f, by = sy - bar_h - 3.0f;
                Draw->AddRectFilled({bx, by}, {bx + bar_w * (door.progress / 100.0f), by + bar_h}, progress_color(door.progress));
                Draw->AddRect({bx, by}, {bx + bar_w, by + bar_h}, ImColor(255,255,255,255));
            }
            auto ts = ImGui::CalcTextSize(label, 0, 25);
            Draw->AddText({sx - ts.x/2.0f, sy}, door.can_open ? progress_color(door.progress) : ImColor(180,180,180,255), label);
        }
    }
    
     // ==================== 宿伞传伞：计算蓄力进度 + 缓存起点 ====================
if (show_draw_umbrella && !PySelf::copycat_active() && PyAttack::cur_state() == 47) {
    uint64_t now = AutoSkill::TouchNowMs();
    if (g_umbrella_charge_start == 0) g_umbrella_charge_start = now;
    uint64_t held = now - g_umbrella_charge_start;
    float ratio = (float)held / (float)kUmbrellaFullMs;
    if (ratio > 1.0f) ratio = 1.0f;
    g_umbrella_charge_ratio = ratio;
    g_umbrella_charging     = true;

    // 缓存起点：自身位置 + 朝向 × 6 米
    uint64_t hu = PyAttack::hunter_unit_raw();
    if (hu) {
        float pos[3] = {0, 0, 0}, dir[3] = {0, 0, 0};
        uint64_t po   = UnitFieldPtr(hu, "position");
        uint64_t dobj = UnitFieldPtr(hu, "direction");
        if (PyCore::is_obj(po))   vm_readv(po   + (uint64_t)SecOff::u64(SecOff::DG_67), pos, 12);
        if (PyCore::is_obj(dobj)) vm_readv(dobj + (uint64_t)SecOff::u64(SecOff::DG_68), dir, 12);

        // 朝向水平归一化
        float dl = sqrtf(dir[0]*dir[0] + dir[2]*dir[2]);
        if (dl > 0.01f) { dir[0] /= dl; dir[2] /= dl; }

        if (fabsf(pos[0]) < 10000.0f && fabsf(pos[2]) < 10000.0f
            && !(pos[0] == 0.0f && pos[2] == 0.0f)) {
            g_umbrella_start_x = pos[0] + dir[0] * kUmbrellaStartOffset * dist_scale;
            g_umbrella_start_z = pos[2] + dir[2] * kUmbrellaStartOffset * dist_scale;
            g_umbrella_start_valid = true;
        } else {
            g_umbrella_start_valid = false;
        }
    } else {
        g_umbrella_start_valid = false;
    }
} else {
    g_umbrella_charge_start = 0;
    g_umbrella_charge_ratio = 0.0f;
    g_umbrella_charging     = false;
    g_umbrella_start_valid  = false;
}

// ★ GoldRush 模式：用 PySelf::self_uid() 拿自己的 uid，再去 PyMiniMap 里找。
//   uid 每局唯一且不变，绝不会错；PySelf 读取成功率高（连续 8 次失败才放弃），
//   比直读 g_cam_ctrl.unit 的对象指针稳。
float g_self_x = 0, g_self_y = 0, g_self_z = 0;
bool  g_has_self_pos = false;
uint64_t g_self_scene = 0;   // 自己的场景对象指针（主循环跳过时用）
if (PyMiniMap::in_goldrush()) {
    int64_t self_uid = 0;
    if (g_minimap_auto_self) {
        self_uid = PySelf::self_uid();
        if (self_uid < 1000001 || self_uid > 1000100) self_uid = 0;
    } else if (g_minimap_self_num >= 1 && g_minimap_self_num <= 99) {
        self_uid = 1000000 + g_minimap_self_num;
    }

    if (self_uid > 0) {
        int rn = PyMiniMap::team_count();
        for (int k = 0; k < rn; k++) {
            PyMiniMap::TeamMember tm;
            if (!PyMiniMap::get_member(k, tm)) continue;
            if (tm.uid == self_uid) {
                g_self_scene = tm.scene_obj;
                if (tm.valid) {
                    g_self_x = tm.x;   // 世界 X
                    g_self_y = tm.y;   // 高度
                    g_self_z = tm.z;   // 水平 Z
                    g_has_self_pos = true;
                }
                break;
            }
        }
    }
}

// ★ 每帧重新取一次"自己"的两种来源（都是场景对象指针），
// 用它们双重判定，保证不管玩监管还是求生都不会画自己。
//   anchor: 当前操控单位(g_cam_ctrl.unit)对应的场景对象
//   hunter_body: 当前监管单位的 unit.model 对应场景对象
// 两者返回的都是 scene_obj（可直接跟 data[i].obj 比较）
uint64_t self_anchor  = 0;
PySelf::anchor(self_anchor);

// 发布上一帧的统计，再清零开算这一帧。
//   放在循环开头而不是结尾，是为了不用去精确定位那个巨大的 for 的右花括号
//   （里面还有一堆 continue/嵌套），差一帧对排查没有任何影响。
PyDump::g_ds_pub = PyDump::g_ds;
memset(&PyDump::g_ds, 0, sizeof(PyDump::g_ds));

// ★ 本体集合是否跟当前场景脱节 —— **只用于面板显示，不再参与过滤**。
//   上一版曾用它停用过滤来救"假面不画人"，结果画重(12 人 23 框)，已回退。
static bool s_body_stale = false;
s_body_stale = (PyDump::g_ds_pub.body_hit == 0 && PyDump::g_ds_pub.body_miss >= 3);

// 把 PySelf 线程统计的"unit.position 读失败次数"搬进面板统计，并清零。
// 读失败时框会沿用旧位置(不跳)，但必须看得见 —— 否则"框为什么不动"又只能靠猜。
PyDump::g_ds.pos_fail = PySelf::g_pos_fail_tick;
PySelf::g_pos_fail_tick = 0;

// ★ 本帧"哪些号已经画过了"的位掩码 —— 保证一人一个框。
//   两个来源都会占位：① 身份簿直接认出来的  ② 按世界坐标认领回来的(假面)。
//   有了它，坐标认领天然被压在上限 12 个框，不会像上一版放开过滤那样变成满屏框。
//
//   ★★ 必须是 static 且**先存上一帧、再清零**：
//   补框通道(见下面"按座位画框")靠"上一帧谁被实体认领过"来决定给谁补框。
//   2026-10-10 事故：原来是 `unsigned int s_seat_claimed = 0;`(每帧清零的局部量)，
//   而快照语句写在清零**之后**，于是快照永远是 0 -> 补框通道给**每个人**都补一个
//   -> 用户看到"除了12号，其他人身上都有两个框"(12号只被补了一次，所以只有一个)。
//   正确顺序：进来先把上一帧的值留一份，再清零。
static unsigned int s_seat_claimed = 0;
static unsigned int s_claimed_last = 0;
s_claimed_last = s_seat_claimed;   // ← 必须在清零之前
s_seat_claimed = 0;

// ★★ 每个号"连续多少帧没被任何实体认领" —— 用于按座位保底补框。
//   起因：用户报"6号的绘制会时不时瞬间消失闪烁一下又变回来…还是会经常闪烁消失几帧"。
//   先试过给去重加迟滞(无效) -> 说明不是去重抖动，而是**实体列表本身逐帧漏掉了这一号**
//   (实体每帧重采，某些帧里这个对象不在列表里，绘制就没了)。
//   修法：连续 2 帧没被认领，就用这个号自己的活坐标补一个框 —— 不再依赖实体列表。
//   只漏 1 帧不补(避免和实体通道撞车出现两个框)。
static int s_seat_miss[16] = {0};
// ★ 保底通道的迟滞：一旦为某个号启用保底，就保持 30 帧(~0.5 秒)不退出。
//   2026-10-10 用户报"只有一个框在闪、框瞬移、两个位置的框重叠" —— 根因就是
//   保底开关在"实体漏 2 帧 / 实体回来"之间**两帧一翻转**：实体框(位置A)与
//   保底框(位置B，取自座位活坐标)差一点点，切换时看着就是瞬移闪烁。
//   加保持期后，切换频率从"每 2 帧"降到"每 30 帧"，肉眼就看不出来了。
static int s_fb_hold[16] = {0};
for (int s = 0; s < 16; s++) {
    if (s_claimed_last & (1u << s)) s_seat_miss[s] = 0;
    else if (s_seat_miss[s] < 1000) s_seat_miss[s]++;
    if (s_fb_hold[s] > 0) s_fb_hold[s]--;
}

// ★ 坐标认领 —— **默认关闭**，这是刻意的。
//
//   用户的要求是"百分百无误判"。而现在已经有确定性链接了：
//   `getPtr64(getPtr64(实体+0x28)+0x58)` == 该座位 unit.model（见 PySelf::copycat_owner_model），
//   它是引擎自己的归属关系、离散的，两个号贴着跑也永远不会互相认错。
//
//   相比之下，按坐标认领本质是"用连续量猜离散身份"：两人贴身时原理上就不可判定。
//   留着一个"猜"的退路，就等于允许它在边缘情况**标错的号** —— 那比不标更糟，
//   因为绘制侧会拿错误的身份去做判断(颜色、已死)。
//   所以这条**只保留代码与计数，恒为关**，需要时把下面的 false 改成条件即可。
const bool pos_claim = false;

// (s_claimed_last 的快照已经在帧首、清零**之前**做过了 —— 见 s_seat_claimed 那里的注释。
//  这里曾经又写了一遍 `s_claimed_last = s_seat_claimed;`，那时读到的是刚清零的 0，
//  于是补框通道以为"谁都没被认领"、给每个人补了一个框 —— 那正是"人人两个框"的根因，已删。)

// ★★★ 假面配对预扫：给每个座位挑定**一个**实体对象，主循环只认这个。
//
//   为什么要预扫+全局配对：单帧"各自找最近"会被两类噪声毁掉(实测数据见 PySelf 里
//   g_seat_track 的注释)：position 有时读到 0、有时飘 5~7 米。各自找最近时，飘了的那
//   一号要么匹配失败(退回按地址匹配 → 画到别的楼层)、要么抢走别人的实体。
//
//   两级配对，先严后宽：
//     A 级(≤0.5 米)：位置与某个实体**精确重合** —— 实测 3/1/8/5 号就是这样，
//                     直接用，最可信。
//     B 级(≤8 米)：用**持久跟踪坐标**(读不到时保留上一次可信值)给剩下的号配对，
//                   距离从小往大贪心，一个实体只能被一个号占。容差放宽是因为跟踪坐标
//                   可能滞后几米，但**绝不会是 0**，所以不会配到"未初始化"的对象上。
//   两级都带**迟滞**：上一帧配上的那个对象如果这一帧仍在容差内，就继续用它 ——
//   避免两人靠近时来回跳号。
//
//   配对结果 s_seat_obj[] 在主循环里被查表使用；不在表里的实体照旧走原来的判据。
static uint64_t s_seat_obj[16] = {0};
static uint64_t s_seat_obj_prev[16] = {0};
// ★★ 整套假面专用逻辑的总开关：**只有判定"现在确实是假面"时才启用**。
//
//   2026-10-10 事故：这些逻辑一开始没有开关，结果**正常对局也生效** ——
//   正常对局里 unit.position 本来就等于实体坐标，位置精确匹配当然会命中，于是
//   "只信配对表、跳过按地址匹配"被误触发，正常框被成片跳掉，表现为**频繁闪烁**；
//   而配对选中的对象可能是尸体/灵魂体，又出现"死人被当活人画在天上/地下"。
//   所以加这道总开关：**正常对局一个字节都不变**，新逻辑只在假面期间跑。
//
//   判据：数一数有多少个座位的**当前锚点不在本帧实体列表里**。
//     正常对局 = 0（锚点本身就是渲染实体）
//     假面     = 10 个以上（unit.model 被整批换成没被渲染的新对象，实测 12/12 全缺）
//   取 >= 3 作为阈值，留足余量。
static bool s_masq_now = false;
static int  s_masq_hold = 0;      // 假面迟滞计数，见 s_masq_now 的赋值处
// 本帧"位置配对是否在工作"(假面期间才可能为真)。为真时只认配对表，见主循环的 pair_skip。
static bool s_pair_active = false;
// 每个号"当前锚点(body)命中"的那个实体(0=没命中)。用来给 body_old 让位，防重复框。
static uint64_t s_body_obj[16] = {0};
const bool cc_pair_ready = copycat_mode && PySelf::copycat_book_ready();
if (cc_pair_ready) {
    for (int s = 0; s < 16; s++) s_seat_obj[s] = 0;
    struct Cand { uint64_t obj; float x, y, z; };
    static Cand cand[PySelf::MAX_BODIES];
    int nc = 0;
    // ★★ 性能：这一段原来**无条件**对每个实体读一次 objcoor+0xa0，而绘制循环稍后
    //   还会再读同一处 -> 位置读取直接翻倍。实测 overlay 的 stime(内核态) 是
    //   utime 的 3.2 倍(65483 vs 20365)，耗时几乎全在系统调用上，每次 vm_readv
    //   都是一次驱动 ioctl。所以现在分两步：
    //     ① 只用**对象指针比较**判定"是不是假面"(零读取)
    //     ② 确认是假面了，才去读位置建候选表
    //   正常对局因此**一次多余的读取都不做**。
    // ---- ① 纯指针判定：本局锚点命中 + 锁定 body 命中的实体 ----
    for (int s = 0; s < 16; s++) s_body_obj[s] = 0;
    int anchor_missing = 0;
    for (int s = 1; s <= 12; s++) {
        const uint64_t b = PySelf::copycat_seat_body(s);
        if (!b) continue;
        for (int i = 0; i < data_count; i++) {
            if ((uint64_t)data[i].obj != b) continue;
            s_body_obj[s - 1] = b;      // 这一号的 body 本帧确实在场景里
            break;
        }
        if (!s_body_obj[s - 1]) anchor_missing++;
    }
    // ★★ 迟滞（2026-10-10 修闪烁）：锚缺在阈值附近抖动时，s_masq_now 会**逐帧翻转**，
    //   而它控制着"框的位置用实体坐标还是用座位活坐标" —— 两个来源差一点点，
    //   于是用户看到"只有一个框，但它在两个重叠位置之间瞬移、重复闪烁"
    //   (原话："框瞬移，两次位置的框是重叠的")。
    //   进入假面后保持 40 帧(~0.7 秒)不退出，抖动就被吞掉了；
    //   退出条件也放宽到锚缺<3，不会卡在假面态出不来。
    if (anchor_missing >= 3) s_masq_hold = 40;
    else if (s_masq_hold > 0) s_masq_hold--;
    s_masq_now = (anchor_missing >= 3) || (s_masq_hold > 0);
    PyDump::g_ds.anchor_missing = anchor_missing;
    if (s_masq_now) {
        for (int i = 0; i < data_count && nc < PySelf::MAX_BODIES; i++) {
            if (data[i].camp != 1 && data[i].camp != 2) continue;
            if (!strstr(data[i].class_name, "player") && !strstr(data[i].class_name, "boss")) continue;
            if (data[i].objcoor == 0 && !data[i].fb_ok) continue;
            float xyz[3] = {0, 0, 0};
            unit_xyz(i, xyz);
            if (xyz[0] == 0.f && xyz[1] == 0.f && xyz[2] == 0.f) continue;   // 未初始化的对象不参与
            cand[nc].obj = (uint64_t)data[i].obj;
            cand[nc].x = xyz[0]; cand[nc].y = xyz[1]; cand[nc].z = xyz[2];
            nc++;
        }
    }
    // ---- ② 每个号"当前锚点命中"的实体已经在上面按指针定好了(s_body_obj) ----
    //
    //   2026-10-10 事故：加了 body_old 之后，同一号会有**两条**匹配路径 ——
    //   ① 当前锚点 body 命中一个实体，② 旧锚点 body_old 命中另一个实体。
    //   两条都画出来就是"同一个人两个框"。用户实测："7号身边有个12号的绘制框
    //   (贴得比较近 1m 左右，但 12 并不在这里)，3 号 12 号都画出了两个框"。
    //   所以这里先把"body 命中的实体"钉下来；主循环里凡是靠 body_old/父子链认出来的，
    //   只要这一号的 body 已经命中过别的实体，就让位不画。
    //   (s_body_obj 与 anchor_missing 已经在上面第 ① 步用**纯指针比较**定好了，
    //    不再重复扫一遍 —— 那段原来也是每次遍历 cand[]，纯属重复劳动。)
    if (!s_masq_now) {
        // 正常对局：清空配对表，后面所有假面逻辑都不参与，行为与改动前完全一致
        for (int s = 0; s < 16; s++) s_seat_obj_prev[s] = 0;
        s_pair_active = false;
    } else {
    // 允许被占用的实体（两级共用一张占用表）
    bool used[PySelf::MAX_BODIES] = {false};
    // ---- 迟滞 + 宽松级：**已停用** ------------------------------------------
    //   2026-10-10 实测教训：加上它们之后用户反馈"一个正确的都没看到"，
    //   而只保留下面 A 级(精确匹配)时至少 1 号是对的。原因：
    //     · 迟滞**会把错误的配对锁死** —— 某号一旦被宽松级配错，下一帧它仍在 8 米内，
    //       于是继续沿用，永远错下去；
    //     · 宽松级(8 米)几乎给每个号都强行配上一个，哪怕是错的，把原本靠精确匹配
    //       正确的那些也搅乱了。
    //   结论：位置这个判据**只信精确重合**，不外推、不迟滞、不放宽。
    //   配不上的号宁可退回按对象地址匹配，也不用一个"大概在那儿"的位置去画 ——
    //   画错楼层比画不出来更糟(用户看到"二楼的人框在脚下"就是这么来的)。
    const bool cc_pair_loose_enable = false;
    if (cc_pair_loose_enable)
    for (int s = 0; s < 16; s++) {
        if (!s_seat_obj_prev[s]) continue;
        float tp[3];
        if (!PySelf::copycat_seat_track(s + 1, tp)) continue;
        for (int c = 0; c < nc; c++) {
            if (cand[c].obj != s_seat_obj_prev[s] || used[c]) continue;
            const float dx = tp[0] - cand[c].x, dy = tp[1] - cand[c].y, dz = tp[2] - cand[c].z;
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 <= 8.0f * 8.0f) { s_seat_obj[s] = cand[c].obj; used[c] = true; }
            break;
        }
    }
    // ---- A 级：位置精确重合(≤0.5 米) —— 唯一启用的判据 ----
    for (int s = 0; s < 16; s++) {
        if (s_seat_obj[s]) continue;
        float tp[3];
        if (!PySelf::copycat_seat_pos(s + 1, tp)) continue;     // 只信"当帧可信"的值
        int best = -1; float bd = 0.5f * 0.5f;
        for (int c = 0; c < nc; c++) {
            if (used[c]) continue;
            const float dx = tp[0] - cand[c].x, dy = tp[1] - cand[c].y, dz = tp[2] - cand[c].z;
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 <= bd) { bd = d2; best = c; }
        }
        if (best >= 0) { s_seat_obj[s] = cand[best].obj; used[best] = true; PyDump::g_ds.pair_exact++; }
    }
    if (cc_pair_loose_enable)
    for (;;) {
        int bs = -1, bc = -1; float bd = 8.0f * 8.0f;
        for (int s = 0; s < 16; s++) {
            if (s_seat_obj[s]) continue;
            float tp[3];
            if (!PySelf::copycat_seat_track(s + 1, tp)) continue;
            for (int c = 0; c < nc; c++) {
                if (used[c]) continue;
                const float dx = tp[0] - cand[c].x, dy = tp[1] - cand[c].y, dz = tp[2] - cand[c].z;
                const float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 < bd) { bd = d2; bs = s; bc = c; }
            }
        }
        if (bs < 0) break;
        s_seat_obj[bs] = cand[bc].obj; used[bc] = true;
        PyDump::g_ds.pair_loose++;
    }
    for (int s = 0; s < 16; s++) s_seat_obj_prev[s] = s_seat_obj[s];    // 本帧"位置配对是否在工作"：只要配上了至少一个，就说明这一帧实体列表里确实存在
    // 能与座位坐标对上的对象。此时**只信配对表** —— 见主循环里的 pair_skip。
    s_pair_active = (PyDump::g_ds.pair_exact > 0);
    }   // ← 对应上面 if (!s_masq_now) { ... } else { 的 else
}

// ★★★ 按座位画框 —— 假面期间**不依赖实体**的补框通道。
//
//   2026-10-10 用户实测："这次其他人都是正常的，但是12号不绘制"。
//   日志对上了：假面窗口里 `改位置=12`(12 个座位坐标都拿到了)、`挡位置=1`，
//   但 `绘制=6` —— 说明有 6 个号**根本没有实体可挂**：现有绘制是"逐实体"的，
//   认不到实体的号就永远没有框，哪怕它的坐标完全正确。
//   所以补这条通道：凡是**上一帧没有任何实体认领**的号(s_claimed_last 里没占位)，
//   直接按它的活坐标投影画一个框。已被实体认领的号不动 —— 它们由实体通道画，
//   两边不会重复出框。
//   投影公式与实体通道完全一致(分量映射同样是 sp[0]=世界X, sp[1]=高度, sp[2]=水平Z)，
//   头/脚偏移照抄 8.5 / 28.5，所以框的高度与实体通道画出来的一致。
const bool cc_seatbox_on = s_masq_now || PySelf::copycat_book_ready();
if (cc_seatbox_on) {
    const int self_seat = PySelf::copycat_self_seat();
    ImDrawList *sdl = ImGui::GetForegroundDrawList();
    for (int s = 1; s <= 16; s++) {
        if (s == self_seat) continue;                                 // 自己永远不画
        // 正常对局：只有**连续 2 帧**没被认领才补(漏 1 帧不补，避免和实体通道撞车出两个框)
        // 假面期间：口径不变，仍用"上一帧有没有被认领"
        // ★★ 判据简化成一条：**上一帧这一号没被任何实体认领 -> 就补框**。
        //   2026-10-10 两次踩坑都在这里：
        //     · 原来只在假面期间补 -> 正常对局漏帧没盖住
        //     · 后来改成"连续漏 2 帧才补 + 保持 30 帧" -> ① 漏 1 帧的情况盖不住，还是闪；
        //       ② 保持期内实体回来了，两条通道同时画 -> 又出现重叠框
        //   现在直接看占位表：实体在 -> 上一帧被认领过 -> 不补(不会重复)；
        //   实体缺席(哪怕只缺 1 帧) -> 没被认领 -> 补上(不会闪没)。
        //   判据与"上一帧画没画"一一对应，所以既不会漏也不会重。
        const bool need = (s_claimed_last & (1u << (s - 1))) == 0;
        if (!need) continue;
        // ★ 死者不归这条通道管：死者由"尸体 / 灵魂体"那条路径负责，
        //   而这里画出来的是 rect_ghost(白) —— 2026-10-10 用户报"还把死人的白框绘制出来了"，
        //   就是这条路把死者的座位也补了框。活着的人才补。
        {
            int idn = 0; bool dd = false;
            if (PySelf::copycat_seat_info(s, idn, dd) && dd) continue;
        }
        int ident = 0; bool dead = false;
        if (!PySelf::copycat_seat_info(s, ident, dead)) continue;
        // ★ 身份必须**已知**才画 —— 否则会出现"没有名字的白框"。
        //   2026-10-10 用户报"楼上有一个没有名字的白框"：补框通道只画框不画名字，
        //   而身份取不到时 copycat_camp() 落到 default 分支 = 鬼魂色(白)，
        //   于是看起来就是一个来路不明的白框。没有身份就不画。
        if (!PySelf::copycat_camp_known(ident)) { PyDump::g_ds.seatbox_nopos++; continue; }
        float sp[3];
        if (!PySelf::copycat_seat_pos(s, sp) && !PySelf::copycat_seat_track(s, sp)) {
            PyDump::g_ds.seatbox_nopos++;
            continue;
        }
        if ((sp[0] == 0.f && sp[1] == 0.f && sp[2] == 0.f)
            || fabsf(sp[0]) >= 60000.f || fabsf(sp[1]) >= 60000.f || fabsf(sp[2]) >= 60000.f) {
            PyDump::g_ds.seatbox_nopos++;
            continue;
        }
        const float camera = matrix[3] * sp[0] + matrix[7] * sp[1] + matrix[11] * sp[2] + matrix[15];
        if (camera <= 0.001f) { PyDump::g_ds.seatbox_nopos++; continue; }
        const float rx = px + (matrix[0] * sp[0] + matrix[4] * sp[1] + matrix[8] * sp[2] + matrix[12]) / camera * px;
        const float ry = py - (matrix[1] * sp[0] + matrix[5] * (sp[1] + 8.5f)  + matrix[9] * sp[2] + matrix[13]) / camera * py;
        const float rw = py - (matrix[1] * sp[0] + matrix[5] * (sp[1] + 28.5f) + matrix[9] * sp[2] + matrix[13]) / camera * py;
        const float H = ry - rw, W = H * 0.6f;
        if (W <= 0.f) { PyDump::g_ds.seatbox_nopos++; continue; }
        const float x1 = rx - (ry - rw) / 4.f, y1 = ry - H / 2.f;
        // ★ 配色必须与实体通道**完全一致**，否则同一个人的框看起来像两种类别。
        //   2026-10-10 事故：这里原来写死白色(死者灰)，于是补出来的那个框是纯白，
        //   而实体通道画的是阵营色(好/坏/中立) —— 用户看到 12 号是白框，以为是
        //   "被当成死人灵魂体画了"。其实身份没问题，只是颜色没跟上。
        //   活人 -> copycat_camp(身份) 取阵营色；死者 -> 鬼魂色(与实体通道同一项)。
        ImU32 col;
        if (dead) {
            col = g_esp.rect_ghost;
        } else {
            switch (PySelf::copycat_camp(ident)) {
                case 1:  col = g_esp.rect_cc_good;    break;
                case 2:  col = g_esp.rect_cc_bad;     break;
                case 3:  col = g_esp.rect_cc_neutral; break;
                default: col = g_esp.rect_ghost;      break;
            }
        }
        DrawEspRect(sdl, x1, y1, x1 + W, y1 + H, W, H, col);
        PyDump::g_ds.seatbox++;
    }
}

// ★ 性能剖析：量"绘制一帧真正吃多少 CPU"（CLOCK_THREAD_CPUTIME_ID，见 PySelf.h 的注释）
//   做法：帧首取 CPU 时间，与**上一帧帧首**的差值 = 这一帧(含收尾)消耗的 CPU。
//   不用去找绘制函数的收尾位置，改动只在帧首，风险最低。
{
    static uint64_t s_prev_cpu = 0;
    const uint64_t c = PySelf::now_cpu_ns();
    if (s_prev_cpu) PySelf::g_t_frame_ns.fetch_add(c - s_prev_cpu, std::memory_order_relaxed);
    s_prev_cpu = c;
}
PySelf::g_n_frame.fetch_add(1, std::memory_order_relaxed);
    Lic::SetDrawPhase(1);       // 实体循环开始
g_drw_d1 = drw_cpu_ns();   // 实体循环开始
    g_drw_last_data_count = data_count;
for (int i = 0; i < data_count; i++){
    PyDump::g_ds.total++;
    
        if (strstr(data[i].class_name, "buzz") != NULL) { PyDump::g_ds.skip_cls++; continue; }
        if (strstr(data[i].class_name, "nvyao.gim") != NULL) { PyDump::g_ds.skip_cls++; continue; }
        // 三个坐标是连续的 12 字节，一次读完。原来是三次 getFloat = 三次 ioctl，
        // 而每次驱动读约 3µs，30 个实体 60fps 下这一项就占渲染线程约 1% CPU。
        // 读失败时保持 0，和 getFloat 失败返回 0 的行为一致。
float xyz[3] = {0, 0, 0};
unit_xyz(i, xyz);
D.X = xyz[0];
D.Z = xyz[1];
D.Y = xyz[2];

// ★ GoldRush 模式：优先用 PyMiniMap 拿到的自身坐标更新 Z
if (g_has_self_pos) {
    Z.X = g_self_x;    // 世界 X
    Z.Z = g_self_y;    // 高度 Y
    Z.Y = g_self_z;    // 水平 Z
}

// ★ 自身跳过：缓存 self_obj + 每帧取到的 anchor
//   只用 anchor(PySelf::anchor)，它表示"当前操控单位"，玩什么角色都跟着走
//   绝对不要用 hunter_body —— 它表示"本局的监管"，玩求生时那是敌人
const bool is_self =
        (self_obj != 0      && data[i].obj == self_obj) ||
        (self_anchor != 0   && data[i].obj == self_anchor) ||
        (g_self_scene != 0  && data[i].obj == g_self_scene);   // ★ GoldRush 里用这个
if (is_self) {
    if (!ShouldSkipEntity(data[i]) && !(D.X==0 && D.Y==0) && D.Z>-300) {
        Z.X = D.X; Z.Z = D.Z; Z.Y = D.Y;
    }
    // （2026-10-13：原来这里把自身坐标喂给 AutoZhenShe 的 200Hz 线程，
    //   自动震慑功能已整体删除 -> 这几行一并去掉 ✓）
    // ★★ 改视角要用自身坐标算距离 —— 这里正是每帧算好它的地方 ✓
    //   用绘制侧的 D.X/D.Y/D.Z（和 ESP 标签同源，数字必然一致）✓
    ViewSwitch::g_self_x = D.X;
    ViewSwitch::g_self_y = D.Y;
    ViewSwitch::g_self_z = D.Z;
    ViewSwitch::g_have_self = true;
    // ★ 玩监管时，所有监管阵营的对象都不画
//   (camp==1 包含本体 + LOD + 备用形态 + 分身，都是自己)
//   玩求生时不动，因为这时 camp==1 是"对面监管"，要正常显示。
if (has_auth_self && self_camp == 1 && data[i].camp == 1) {
    continue;
}
    // ★【必补】自身无条件跳过后续所有实体绘制（原来这行在 if 外面被挪丢了）
    // ★ 2026-10-28：自身被 continue 掉，走不到下面的天赋/CD upsert，
//   导致网页 svs 里自己那张卡片 tal/cd 永远是空。这里补一次。
{
    PyGenius::Info gi_self;
    if (PyGenius::lookup((uintptr_t)data[i].obj, D.X, D.Y, gi_self)) {
        PyGenius::GeniusLine seg_self;
        PyGenius::genius_segments(gi_self, seg_self);
        char tl_self[48] = {0};
        if (gi_self.support_trait != 0 &&
            (gi_self.camp == 1 || gi_self.camp == PyGenius::YIDHRA_PUPPET_UNIT_TYPE)) {
            PyGenius::trait_line_text(gi_self, PyGenius::trait_name(gi_self.support_trait),
                                      tl_self, sizeof(tl_self));
        }
        const uint64_t _o2 = (uint64_t)data[i].obj;
        int idx2 = -1;
        for (int q2 = 0; q2 < g_share_ent_n; ++q2)
            if (g_share_ent[q2].obj == _o2) { idx2 = q2; break; }
        if (idx2 < 0) {
            if (g_share_ent_n < 48) idx2 = g_share_ent_n++;
            else {
                for (int q = 0; q < 48; ++q)
                    if (g_share_ent[q].obj == 0) { idx2 = q; break; }
                if (idx2 < 0) idx2 = 0;
            }
            g_share_ent[idx2].obj = _o2;
        }
        ShareEnt &se = g_share_ent[idx2];
        snprintf(se.tal, sizeof(se.tal), "%s%s%s", seg_self.pre, seg_self.flywheel, seg_self.post);
        snprintf(se.cd,  sizeof(se.cd),  "%s", tl_self);
        se.fw   = (gi_self.flywheel_remain > 0.0f) ? gi_self.flywheel_remain : 0.0f;
        se.skcd = (gi_self.cd_remain       > 0.0f) ? gi_self.cd_remain       : 0.0f;
        se.has_fw = (seg_self.flywheel[0] != 0) ? 1 : 0;
    }
}
PyDump::g_ds.is_self++;
    continue;
}

// ★【新增】非自身的 goldrush 队友（camp==2）由 PyMiniMap 画，主循环跳过
if (PyMiniMap::in_goldrush() && data[i].camp == 2) { PyDump::g_ds.goldrush++; continue; }

        // 只画角色本体：CPython 侧 units_by_type[1]/[2]/[236]/[1065] 的 unit.model 对应的场景对象(见 PySelf.h 本体集合)。
        // 模仿者局是 [1088] 里的活人(死者和 [1089] 鬼魂不收，所以不画)。
        // 挡掉同名分身副本(_fragrance_image)、另一形态、挂件、时装、魔术师/幻灯师分身；机械玩偶保留。
        // 只管按 player/boss 类名归进 1/2 的对象 —— 类名分类那里另有两个特例(deluosi 鬼魂、火箭挂件)
        // 被故意归成求生者，它们不是任何单位的 model，不能被这层挡掉。
        // 本体集合不可用(准备阶段/大厅/读取失败)时不过滤，照旧按类名画。
        // 大厅/准备阶段(units_by_type 读全了且没有键 1/2)：没有任何玩家单位，场景里的人物对象全是
        // 无宿主的时装挂件(头饰/袖子)，阵营 1/2 整类不画。预知监管在读取循环里算，不受影响
        // ★ 局外判据加场景兜底。
        //   PySelf::check_lobby 靠 units_by_type 的键号(1/2/1088)判局内，但模仿者局
        //   准备阶段的键是 69/1007/100，一个都对不上 -> 误判成局外 -> 阵营 1/2 整类
        //   被跳 -> **所有人都不画**。场景路径还挂在 scene_battle 下就说明在对局中，
        //   这个判据不受键号变动影响。
        const bool in_lobby_now = PySelf::in_lobby() && !PyMiniMap::in_battle_scene();
        if ((data[i].camp == 1 || data[i].camp == 2) && in_lobby_now) { PyDump::g_ds.lobby++; continue; }
        // 模仿者模式开着且身份簿有内容：按簿子过滤(只画簿子里的活人)。开会阶段 Python 页被换出，
        // 本体集合读不全甚至整个不可用，那时退回按类名画会把尸体、鬼魂、石像全画出来
        // ★ 灵魂体不受身份簿过滤。
        //   模仿者局里死者的灵魂是 [1089] 那类：既不在身份簿里(copycat_identity 对
        //   死者一律返回 false)，也不在本体集合里(collect_copycat 只收活人的 body)。
        //   于是被下面这层身份簿过滤直接吃掉 —— 表现就是"显示灵魂"开关按了没反应。
        //
        //   ⚠ 判据必须用 PySelf::is_soul(从引擎 [1089] 列表里精确取的对象)，
        //     **不能**用 IsGhostEntity。后者看的是 +0x70/+0x1a0，也就是**渲染可见性**：
        //     分身的子模型、LOD、separate_dir 部件、远处被剔除的真实对象全都满足，
        //     于是它们也一起绕过过滤 -> 一个活人身上会多出好几个框。
        // ---- 模仿者：先把"这个对象是谁"认出来（活人 / 已出局的玩家 / 不认识）----
        //   三个来源都是**精确到对象**的判据，不靠可见性猜：
        //     ① copycat_identity        活人本体(身份簿，按 body 地址)
        //     ② copycat_ghost_identity  死者的灵魂体(身份簿，按 soul 地址) —— 局内画的就是它
        //     ③ copycat_dead_body       死者当前挂着的那个对象(身份簿，按 body 地址)
        //   ③ 是给**会议桌**用的：开会时引擎把每个人换成桌边的座位模型，
        //   ① 查不到(死者不在活人表里)、② 的 soul 地址也过期了，
        //   只有 ③ 认得出 —— "到会议桌上就不绘制死人"就是缺了这一条。
        int cc_ident = 0, cc_seat = 0;
        bool cc_live = false, cc_soul = false, cc_dead_body_hit = false;
        bool cc_dead_unrendered = false;   // 死者身上那个"引擎没渲染"的对象 = 局内那具尸体
        bool cc_masq_dead = false;         // 假面替身对应的那一号在簿子里是死者
        bool cc_via_old = false;           // 这个框是靠 body_old(旧锚点)认出来的 -> 位置要改用 unit.position
        bool cc_by_pos = false;            // 这个实体是靠"位置精确匹配"认下来的(位置就是对的，不需要改写)
        if (copycat_mode) {
            // ★★★ 位置精确匹配 —— **放在最前面**，这是假面真正画在正确位置上的关键。
            //
            //   2026-10-10 逐分量实测(假面进行中，每个座位打完整三量)：
            //     03:53:53  3N pos(826,0,717) ent(826,0,717) d=0.0
            //               8Y pos(785,0,676) ent(785,0,676) d=0.0
            //              11Y pos(774,0,717) ent(774,0,717) d=0.0
            //   **假面期间 unit.position 与某个渲染实体的位置精确相等(含高度分量)** ——
            //   位置本身没问题。真正的问题是"画在了错的那个实体上"：
            //   匹配走的是父子链那条，它认出来的对象位置在别处，于是框被画在
            //   天上/地上(用户原话"二楼的人框在他脚下")；而位置与座位完全重合的
            //   那个实体才是真正渲染出来的角色。
            //   所以改成：先用位置把实体认到座位上(容差 0.4 米，实测是 0.0 的精确相等)，
            //   认到了就直接用它画 —— 后面所有按对象地址的判据都让位。
            //   每个座位一帧只认领一次(s_seat_claimed)，上限压在 12 个框。
            // (原来这里有一条"单帧 0.4 米位置认领"，已被上面的**全局配对**取代：
            //  单帧各自找最近会被"读到 0"和"飘几米"两种噪声毁掉，配对版用持久跟踪坐标 +
            //  迟滞把这两种噪声都压掉了。留着它会和配对表抢座位，所以删掉。)

            // ★ 预扫配对表优先：这个实体是不是某个座位选定的那个对象。
            //   配对是"按位置"做的(两级 + 迟滞，见上面预扫那段)，所以它挑中的实体
            //   位置天然是对的 —— 认下来后不要再改写位置(cc_by_pos = true)。
            if (!cc_live && cc_pair_ready && s_masq_now) {
                for (int s = 0; s < 16; s++) {
                    if (s_seat_obj[s] != (uint64_t)data[i].obj) continue;
                    int pid3 = 0; bool pdead3 = false;
                    if (PySelf::copycat_seat_info(s + 1, pid3, pdead3)) {
                        cc_seat = s + 1;
                        cc_ident = pid3;
                        cc_live = !pdead3;
                        cc_masq_dead = pdead3;
                        cc_by_pos = true;
                    }
                    break;
                }
            }

            if (!cc_live)
                cc_live = PySelf::copycat_identity(data[i].obj, cc_ident, cc_seat, &cc_via_old);
            // ★★ 把自己从绘制里排除 —— 按身份簿的号，确定性判据。
            //
            //   原来靠 self_obj / self_anchor / g_self_scene 三条跳过自己，但那三条在模仿者
            //   对局里经常一条都不命中(实测:跳过自身=1 但自己的框仍然画了出来)，于是屏幕上
            //   出现一个"画在自己身上"的框 —— 而画在自己身上的框**永远在屏幕中心**，
            //   走动时不动、只有转视角才上下动，看起来就像跟踪坏了。
            //   所以补一条按号的：cc_seat == 簿子里的自己号 -> 不画。
            if (cc_live && cc_seat >= 1) {
                const int self_seat = PySelf::copycat_self_seat();
                if (self_seat >= 1 && cc_seat == self_seat) {
                    PyDump::g_ds.skip_self_cc++;
                    continue;
                }
            }
            // ★★ 假面期间"宁可不画，不可画错"。
            //
            //   2026-10-10 实测(假面进行中，每座位打最近三个实体+距离)：
            //     10Y pos(-107,0,431) [佣兵](-107,0,431) d=0.0    ← 能精确对上
            //      7Y pos(106,0,-251) [先知](106,0,-251) d=0.0    ← 能
            //     11N pos(-41,61,103) 最近 15.4 米                ← 附近**根本没有实体**
            //      5Y pos(34,61,150)  最近 21.3 米                ← 没有
            //      1N pos(-42,61,178) 最近 16.5 米                ← 没有
            //      6N pos(-8,1,-351)  最近 19.4 米                ← 没有
            //   也就是说假面期间多数座位的渲染对象**不在 unit.position 附近**(差 10~21 米)，
            //   只有约 7 个能精确对上。对不上的那些如果退回按对象地址匹配，框就会画到
            //   别的楼层/别的地方 —— 用户看到的就是"没一个准"。
            //   所以：本帧只要配对在工作(s_pair_active)，就**只画配对表选中的实体**，
            //   靠地址匹配出来的那些一律不画。假面时表现为"部分人没有框"，
            //   而不是"框全在错地方" —— 与"宁可不标，不可标错"同一条原则。
            // ★★ 假面期间：**位置一律按座位的活坐标来**，不挂在实体上。
            //
            //   2026-10-10 结论(用户提问"假面期间到底有没有实际在走的模型数据")：
            //   **有，就是 unit.position 本身** ——
            //     · 它跟着人动(实测假面中 (-73,224) → (-65,230))
            //     · 它与渲染实体同一坐标系(正常状态逐分量误差 0.00)
            //     · 假面中有约 7 个座位与某个实体**精确相等**(d=0.0)
            //   也就是说框根本不必挂在实体上：**按座位坐标直接投影**即可。
            //   之前那条"位置改写"方向是对的，死在两个具体错误上：
            //     ① 分量映射写反(D.Y/D.Z 对调) —— 已修
            //     ② 坏坐标没挡(-94 这种能过"非零"检查) —— 已加闸门
            //   但它**只对 body_old 命中的才改写**，而假面期间多数匹配走父子链 ->
            //   没被改写 -> 位置仍是过期对象的 -> 用户看到"绘制的地方根本没有人"。
            //   现在改成：假面期间所有框都按座位活坐标定位；**没有可信坐标的号不画**
            //   (宁可不画，不可画错)。
            if (s_masq_now && cc_live && cc_seat >= 1 && cc_seat <= 16) {
                float sp[3];
                // ★ 当帧可信值优先；读不到就退到**持久跟踪**的上一次可信值。
                //
                //   2026-10-10 用户实测："绘制出来的框确实是准的，不过就绘制几个人而已，
                //   其他活人根本不绘制，而且绘制出来的那几个人隔一会就直接不绘制了"。
                //   两个现象同一个根因：假面期间 unit.position 是**间歇性**可用的 ——
                //   当帧读到 0/坏值的那一号就没框(其他活人不绘制)，已经画出来的号
                //   下一次读失败就消失(隔一会不绘制)。
                //   而"持久跟踪坐标"(g_seat_track)本来就是为这件事准备的：只在读到可信值
                //   时前进，读不到时保留上一次的值。之前却没用它 —— 这里补上。
                //   效果：读失败的那一帧，框停在最后一次可信的位置，而不是消失。
                bool okpos = PySelf::copycat_seat_pos(cc_seat, sp);
                if (okpos) PyDump::g_ds.pos_live++;
                else if (PySelf::copycat_seat_track(cc_seat, sp)) PyDump::g_ds.pos_track++;
                else {
                    PyDump::g_ds.pos_reject++;
                    continue;      // 连历史值都没有 -> 这一号确实没法定位，不画
                }
                // ⚠ 这里**不能**拿"实体位置"当参照做合理性判断 —— 假面期间那个实体本身
                //   就是过期副本，拿它当基准会把**正确的**座位坐标误判成"太离谱"而拒绝，
                //   那正是"其他活人根本不绘制"的一部分原因。只做绝对合理性检查。
                if (fabsf(sp[0]) >= 60000.f || fabsf(sp[1]) >= 60000.f || fabsf(sp[2]) >= 60000.f
                    || (sp[0] == 0.f && sp[1] == 0.f && sp[2] == 0.f)) {
                    PyDump::g_ds.pos_reject++;
                    continue;
                }
                D.X = sp[0];   // 世界 X
                D.Z = sp[1];   // 高度
                D.Y = sp[2];   // 水平 Z
            }

            // (原来这里有一条"假面期间只画位置配对选中的实体"的硬跳过。已**撤掉** ——
            //  配对在假面期间只配得上 0~1 个，那条等于整个假面不画人；而现在改成了
            //  "按座位活坐标定位"，配对不再是必要条件，框照样能画在正确位置。
            //  唯一还会被跳过的，是**没有可信坐标**的那些号 —— 那是"宁可不画"，不是不认人。)
            // ★★ 让位给"当前锚点命中"的那个实体 —— 防同一个人两个框。
            //
            //   2026-10-10 用户实测："7号身边有个12号的绘制框(贴得比较近 1m 左右，但 12
            //   并不在这里)，3 号 12 号都画出了两个框"。原因：加了 body_old 之后同一号有
            //   两条匹配路径 —— ① 当前锚点 body 命中一个实体，② 旧锚点 body_old 命中另一个
            //   实体，两条都画 = 同一个人两个框。
            //   规则：靠 body_old/父子链 认出来的(cc_via_old)，如果这一号的 body 已经在
            //   别处命中过实体，就让位不画 —— body 命中的那个才是本人。
            //   **不受 s_masq_now 限制**：重复框在正常对局里同样会发生。
            if (cc_live && cc_via_old && cc_seat >= 1 && cc_seat <= 16) {
                const uint64_t bo = s_body_obj[cc_seat - 1];
                // ⚠ 必须再加一道 is_body() —— 判据要问"那个 body 现在**真的在渲染**吗"。
                //   2026-10-10 事故：只看 bo != 0 就够的话，在一局里身份簿锁存的 body
                //   **全部过期**的情况下(实测"本体集合 命中/落空 0/32")，那些过期地址
                //   恰好与 2 个实体地址相同(内存复用)，于是去重误判、把这 2 个号
                //   (含用户报的"1号还活着但不绘制")的框挡掉了。
                //   is_body() 查的是**本帧真正的本体集合**，过期地址不在里面 -> 不会误拦。
                // ★★ 再加"上一帧这一号到底画没画" —— 治**闪烁**。
                //   用户报"6号的绘制会时不时瞬间消失闪烁一下又变回来"。
                //   原因：本判据依赖"该号 body 本帧在不在场景"，而实体列表每帧重采，
                //   这个状态会逐帧抖动 -> body_old 那个框一会儿被让位、一会儿被放行，
                //   表现就是**闪一下又回来**。
                //   加上 s_claimed_last(上一帧真正占过位的号)：上一帧这一号根本没画出来，
                //   这一帧就不让位 —— 交替被打断，框就稳定了。
                const bool drew_last = (s_claimed_last & (1u << (cc_seat - 1))) != 0;
                if (drew_last && bo != 0 && bo != (uint64_t)data[i].obj && PySelf::is_body(bo)) {
                    PyDump::g_ds.dup_old++;
                    continue;
                }
            }
            // 这个对象不是"位置精确匹配"选中的那一个，但它认出来的号码已经被那个实体
            // 占走了 —— 说明它是同一个人的**另一个副本**(位置在别处)，画它就会出现
            // "框在天上/地上"。让位给它。
            // ⚠ 整段都必须在 s_masq_now 里面：正常对局里 s_seat_claimed 也会被占位，
            //   如果让位逻辑在正常对局也生效，同一个人的合法实体就会被误跳过(实测导致
            //   正常对局框频繁闪烁)。
            if (s_masq_now) {
                if (cc_live && !cc_by_pos && cc_seat >= 1 && cc_seat <= 16
                    && (s_seat_claimed & (1u << (cc_seat - 1)))) {
                    PyDump::g_ds.masq_dup++;
                    continue;
                }
            }
            // ★★ 占位表必须在**正常对局也填** —— 2026-10-10 事故：
            //   原来这行被关在上面的 s_masq_now 里(当初为了避免正常对局闪烁)，
            //   结果正常对局 s_claimed_last 永远是 0 -> 保底通道以为"每个号都连续漏帧"
            //   -> 给每个人都补了一个框 -> 和实体框重叠、重复闪烁
            //   (用户原话"直接瞬移出现在偏右一点，两次框是重叠的，重复闪烁")。
            //   占位表有两个用途，要分开对待：
            //     · 让位去重(dup_old) —— 只在假面期间做(留在上面那道门里)
            //     · 供保底通道判断"这一号本帧有没有被认领" —— **任何时候都要填**
            if (cc_live && !cc_by_pos && cc_seat >= 1 && cc_seat <= 16)
                s_seat_claimed |= (1u << (cc_seat - 1));
            if (!cc_live) {
                cc_soul = PySelf::copycat_ghost_identity(data[i].obj, cc_ident, cc_seat);
                if (!cc_soul)
                    cc_dead_body_hit = PySelf::copycat_dead_body(data[i].obj, cc_ident, cc_seat);
                // 局内那具**没被渲染**的尸体会同时命中 ② 和 ③：灵魂体已经占了一个框，
                // 再画尸体就是同一个死人两个框。判据问的是"引擎这帧画它没有"，
                // 不是"他死没死" —— 会议桌上的座位模型会被渲染，所以不会被这条挡掉。
                if (cc_dead_body_hit && !PySelf::scene_is_rendered(data[i].obj)) {
                    cc_dead_body_hit  = false;
                    cc_dead_unrendered = true;
                }
            }

            // ★★ 按世界坐标认领座位 —— "假面下不画人"的正解。
            //   2026-10-10 假面现场实测：假面把每个玩家的**渲染模型换成别的角色**
            //   (实体列表里出现 [入殓师]/[作曲家]/[园丁]/[小女孩]/[先知]/[幸运儿]/
            //    [斗牛士]/[画家] 八个不同角色名)，于是身份簿锁存的 body 地址(unit.model)
            //   和本体集合(也拿 unit.model 建)**同时失效**，44 个实体全被
            //   "簿子没有且非本体"丢掉，实际绘制掉到 1。
            //   但同一时刻逐座位比对发现 unit.position 与某个渲染实体的世界坐标
            //   **精确重合**(5号 (265.6,0.7,-210.0)↔[园丁] d=0.00，
            //    4号 (240.8,0.7,-282.0)↔[先知] d=0.00)：
            //   **对象地址会变、模型会换，位置不会骗人。**
            //   每个座位一帧只认领一次(s_seat_claimed)，上限天然压在 12 个框，
            //   所以不会像上一版那样放开过滤就变成满屏框。
            if (!cc_live && pos_claim) {
                // 诊断：把"这次认领为什么没成"的原因分开计数，别再靠猜。
                //   objcoor=0 -> 位置指针本身是空的(getPtr64(obj+0x28) == 0)
                //   D=0       -> 指针在，但 +0xa0 读出来是全 0
                if (data[i].objcoor == 0) PyDump::g_ds.pc_noobj++;
                else if (D.X == 0.f && D.Y == 0.f && D.Z == 0.f) PyDump::g_ds.pc_zero++;
                else PyDump::g_ds.pc_have++;
                const int ps = PySelf::copycat_seat_near(D.X, D.Y, D.Z, 1.2f);
                if (ps > 0 && !(s_seat_claimed & (1u << (ps - 1)))) {
                    int pid2 = 0; bool pdead = false;
                    if (PySelf::copycat_seat_info(ps, pid2, pdead)) {
                        s_seat_claimed |= (1u << (ps - 1));
                        cc_seat = ps;
                        cc_ident = pid2;
                        cc_live = !pdead;          // 活人：照常画，用簿子里的阵营色和身份名
                        cc_masq_dead = pdead;      // 死者：走死者配色 + 「已死」
                        PyDump::g_ds.pos_claim++;
                    }
                }
            }
            if (cc_live && cc_seat >= 1 && cc_seat <= 16)
                s_seat_claimed |= (1u << (cc_seat - 1));   // 簿子认出来的也占位，避免同一号画两次

            // (旧的「扮演者N」名字匹配保留：实测它对不上，但无害，留着以防别的版本用它)
            const int ms = PySelf::copycat_masq_seat(data[i].str);
            if (ms > 0 && !cc_live) {
                int mid = 0; bool mdead = false;
                if (PySelf::copycat_seat_info(ms, mid, mdead)) {
                    cc_live = !mdead;
                    cc_masq_dead = mdead;
                    cc_ident = mid;
                    cc_seat  = ms;
                    PyDump::g_ds.masq++;
                }
            }
        }
        const bool cc_dead  = cc_soul || cc_dead_body_hit || cc_masq_dead;
        const bool cc_label = cc_live || cc_dead;

        // ★★ 假面：靠 body_old 认出来的框，**位置必须改用 unit.position**。
        //
        //   2026-10-10 实测：假面把 unit.model 换到新对象上，而旧对象(也就是 body_old
        //   指向的那个)虽然还留在场景里、还能被认出来，但**引擎已经不再驱动它的位置**了。
        //   所以画出来的框会冻在假面触发那一刻 —— 用户原话"绘制框不会跟着人动，
        //   一直停留在拉假面那一瞬间的位置"。
        //   同一批采样里 unit.position 却在跟着人变：
        //     03:20:35  12:N(-77,224) 1:N(-220,3)  10:N(284,-219) 8:N(-385,1)
        //     03:20:40  5:N(275,-210) 6:N(266,-196) 10:N(196,-178) 8:N(-152,-116)
        //   所以位置改从 unit.position 取。**必须在下面 2240 的 xy0 检查和 2253 的
        //   投影之前改写 D**，否则改的是没人用的值。
        //   读不到就保持原样(宁可画在旧位置，也不要跳到 (0,0))。
        // ★★ 只在**假面期间**才改写位置 —— 2026-10-10 修"6号框会突然往右偏又拉回"。
        //   原来这条没有假面开关：正常对局里同一个号只要匹配路径在
        //     ① 当前锚点 body   -> 用实体自身位置(正确)
        //     ② 旧锚点 body_old -> 位置被改写成"座位活坐标"(差一点点)
        //   之间逐帧交替，框就会在两个位置之间来回跳 —— 用户看到的就是"突然往右偏、
        //   瞬间又拉回去"。
        //   正常对局的实体位置本来就是对的，**根本不需要改写**；只在假面(box 挂在过期
        //   对象上、位置冻住)时才有必要换成座位活坐标。
        if (s_masq_now && cc_via_old && cc_seat >= 1 && cc_seat <= 16) {
            float sp[3];
            if (PySelf::copycat_seat_pos(cc_seat, sp)) {
                // ⚠ 改写前必须做**合理性**校验，否则会把框送到地底下。
                //   2026-10-10 实测：unit.position 里存在 (-54,-94,483) 这类"看着像坐标"
                //   的坏值 —— y=-94 能通过"非零 / 不超 60000"的检查，于是框被投到地下，
                //   用户看到的就是"别的绘制框在地底下"。面板上对应的现象是
                //   "本体集合 命中/落空 0/79" 同时 "靠旧锚点认出 位置改用unit = 4"。
                //   闸门：水平距离不超过 40 米、高度差不超过一层(60)。假面期间实测
                //   位置与实体的差距是 10~21 米、高度一致，正常通过；跨图垃圾值被挡掉。
                const float dxh = sp[0] - D.X, dzh = sp[2] - D.Y;   // D.X=世界X, D.Y=水平Z
                const float dh  = fabsf(sp[1] - D.Z);               // D.Z=高度
                if (dxh * dxh + dzh * dzh <= 40.f * 40.f && dh <= 60.f) {
                    // ★ 分量映射必须与实体一致：实体的读法是
                    //     D.X = xyz[0](世界X)   D.Z = xyz[1](高度)   D.Y = xyz[2](水平Z)
                    //   而 unit.position 用的是同一套 xyz 布局(实测逐分量 d=0.0)，
                    //   所以这里也必须 D.Z = sp[1]、D.Y = sp[2]。
                    //   ⚠ 原来写的是 D.Y = sp[1]; D.Z = sp[2]; —— **把高度和水平分量对调了**，
                    //     框因此被投到天上/地下(用户原话"应该是高度反了")。这个映射错误
                    //     和上面那道合理性闸门是两回事，两个都得对。
                    D.X = sp[0];   // 世界 X
                    D.Z = sp[1];   // 高度
                    D.Y = sp[2];   // 水平 Z
                    PyDump::g_ds.pos_live++;
                } else {
                    PyDump::g_ds.pos_reject++;   // 位置太离谱，保持原样(不跳)
                }
            }
        }

        const bool is_soul_ent = cc_soul || PySelf::is_soul(data[i].obj);

        // 已经确认是某位出局玩家身上**且引擎没在渲染**的对象：无条件丢掉。
        // 不能只靠下面"不是本体就跳"那一层 —— 本体集合读空的时候(body_set_ready()==false)
        // 那层会整个失效，这具不可见的尸体就会以"活人"的样子被画出来，
        // 也就是用户报的"有的人死掉了还显示活着"。
        if (cc_dead_unrendered) { PyDump::g_ds.cc_skip++; continue; }

        const bool cc_filter = copycat_mode && PySelf::copycat_book_ready();
        if (!cc_dead && !is_soul_ent && cc_filter && (data[i].camp == 1 || data[i].camp == 2)
            && (strstr(data[i].class_name, "player") != NULL || strstr(data[i].class_name, "boss") != NULL)) {
            // ★ 身份簿里没有**不能直接跳过**。
            //   身份簿是锁存的(book_merge 只增不减)，锁的是场景对象的地址；
            //   换场景/换局/重开模型时地址全变，锁存值就过期了。
            //   原来这里直接 continue，结果过期时**所有人都不画**。
            //   正确做法：簿子是"优先判据"，认不出来就退回本体集合判断
            //   (body_set),不在本体集合里才跳。
            if (!cc_live) {
                // ⚠ 这里**不要**为了"假面不画人"而放开本体集合过滤 —— 实测过，会画重。
                //   2026-10-10 假面现场：假面给每个玩家额外生成一个「扮演者N」替身对象，
                //   本人和替身**同时存在**。当时命中 0/落空 30 就把过滤停用了，结果
                //   12 个人画出 23 个框(截图为证)：白框"扮演者N" + 身份簿认出的本人各一个。
                //   正确的做法是**认出「扮演者N」= N 号本人**(号码就在名字里)，一人只画一个，
                //   见下面 cc_masq_seat 那段。
                if (!ShareMapBodyFilterShouldSkip()
    && PySelf::body_set_ready() && !PySelf::is_body(data[i].obj)) {
                    PyDump::g_ds.body_miss++;
                    PyDump::g_ds.cc_skip++;
                    continue;
                }
                PyDump::g_ds.body_hit++;
                // 是本体但簿子里没有 -> 画出来，只是没有身份标注
                PyDump::g_ds.cc_nobook++;
            }
        }
        else if (!cc_dead && !is_soul_ent && (data[i].camp == 1 || data[i].camp == 2)
    && !ShareMapBodyFilterShouldSkip()
    && PySelf::body_set_ready()
    && (strstr(data[i].class_name, "player") != NULL || strstr(data[i].class_name, "boss") != NULL)
    && !PySelf::is_body(data[i].obj))
    { PyDump::g_ds.body_skip++; continue; }

        if (D.X==0 || D.Y==0){
		    PyDump::g_ds.xy0++; continue;//跳过xy0
		}
		if (D.Z<=-300){
		    PyDump::g_ds.dz++; continue;//跳过地下
		}
		if (data[i].camp == 1 || data[i].camp == 2) { char_count++; PyDump::g_ds.drawn = char_count; }

		
		// 本体是监管时，游戏自己就会显示监管者个体，这里不再重复画任何监管者。
		// 只认 CPython 锚点给的阵营：兜底的相机深度启发式可能把求生者误判成监管，
		// 那样会让求生者视角下的监管整个消失，代价远大于多画一个。
		if (has_auth_self && self_camp == 1 && data[i].camp == 1) { PyDump::g_ds.camp_self++; continue; }
		// 这里原本还读一次 +0x70(jxpd) 并算 cam_dist / niexi_dist，三者都没有任何地方使用，已删。
		// jxpd 那次是每实体每帧一次驱动读，删掉直接省 CPU；另两个只是浮点运算。
		/* ★ 2026-10-24: 投影改由 SecMath（security 满档 TU） */
		SecMath::BoxToScreen(matrix, px, py, D.X, D.Y, D.Z, r_x, r_y, r_w, camera);
        dist = sqrt(pow(D.X - Z.X, 2) + pow(D.Y - Z.Y, 2) + pow(D.Z - Z.Z, 2)) / dist_scale;
												
		W = (r_y - r_w) / 2;	// 宽度
		H = r_y - r_w;		// 高度
		X1 = r_x - (r_y - r_w) / 4;	// X1
		Y1 = r_y - H / 2;	// Y1
		X2 = X1 + W;		// X2
		Y2 = Y1 + H;		// Y2
		if (dist>=300){
            PyDump::g_ds.dist++; continue;
        }
        if (W>0){
            if (Debugging){
                // 调试绘制是独立分支，不走上面任何类别过滤，所以会把每局默认加载的装饰对象
                // 一起画出来(表现为"同一个位置两份不同地址"、约瑟夫相机等)。这里统一挡掉。
                // 判据用 +0x240 而不是活跃位：活跃位是"当前对本机可见"，跟视角走，
                // 求生者视角下大量真实对象的活跃位也是 0，用它会把真东西一起挡掉。
                // 想看全部对象(比如排查新偏移时)，把下面这行注释掉即可。
                //if (!entity_exists(data[i].obj)) continue;
                std::string test;
                sprintf(objtext, "%lx", data[i].obj);
                test += " [";
                test += std::to_string((int) dist);    
                test += " 米]  0x";
                test += objtext;    
                test += " [类名] ";
                test += data[i].class_name;
                auto textSize = ImGui::CalcTextSize(test.c_str(), 0, 25);
                Draw->AddText({r_x-(textSize.x/2),r_y}, ImColor(255,200,0,255), test.c_str());
            }
        
            if (strstr(data[i].class_name, "camera") != NULL && dist < 38){
                // 约瑟夫的相机每局都会默认加载十几个，跟这局有没有约瑟夫无关。
                // 实测那些默认加载的 +0x240 全是 0（7/7），本局真实存在的物件是 2。
                if (!entity_exists(data[i].obj)) continue;
                if (getDword(data[i].obj + (uint64_t)SecOff::u64(SecOff::DG_69))==256){
		            continue;//跳过使用过的椅子
		        }
                std::string s;
			    if (show_draw_Camera){                          
                    s += "[摄影机]";
                }
                auto textSize = ImGui::CalcTextSize(s.c_str(), 0, 25);
                Draw->AddText({r_x-(textSize.x/2),r_y}, ImColor(255,200,0,255), s.c_str());
            }
            

		
    		if (data[i].camp==3)
    		{
    		    if (strstr(data[i].class_name, "dm65_scene_prop_30") != NULL){
    			    std::string s;
    		        if (show_draw_Door){
                        s += "[大门]";
                    }
                    auto textSize = ImGui::CalcTextSize(s.c_str(), 0, 25);
                    Draw->AddText({r_x-(textSize.x/2),r_y}, ImColor(255,200,0,255), s.c_str());
    		    }
    		
    		    else if (strstr(data[i].class_name, "dm65_scene_prop_01") != NULL&&dist<38){
    			    std::string s;
    		        if (show_draw_Box){                          
    		            if (getDword(data[i].obj + (uint64_t)SecOff::u64(SecOff::DG_70))==0){
    		                continue;//跳过使用过的箱子
    		            }
                        s += "[道具箱]";
                    }    
                    auto textSize = ImGui::CalcTextSize(s.c_str(), 0, 25);
                    Draw->AddText({r_x-(textSize.x/2),r_y}, color_red, s.c_str());
    		    }

    		    else if (strstr(data[i].class_name, "dm65_scene_gallow") != NULL&&strstr(data[i].class_name, "bashou") == NULL&&dist<38){
    			    std::string s;
    		        if (show_draw_Chair){                          
    		            if (getDword(data[i].obj + (uint64_t)SecOff::u64(SecOff::DG_71))==256){
    		                continue;//跳过使用过的椅子
    		            }
                        s += "[狂欢之椅]";
                    }
                    auto textSize = ImGui::CalcTextSize(s.c_str(), 0, 25);
                    Draw->AddText({r_x-(textSize.x/2),r_y}, color_red, s.c_str());
    		    }
    		
    		    else if (strstr(data[i].class_name, "dm65_scene_prop_76") != NULL){
    		        //   ★★ 2026-10-13 用户要求：地窖改成**名字在上、距离在下**两排 ★★
    		        //     原话：「地窖绘制也改成1地窖名字在上距离在下」
    		        //   原来是一行横排 "[地窖] 77 m " ✗
    		        //   ⚠ 距离**不显示小数**（和密码机一致）✓
    		        if (show_draw_Cellar){
    		            //   ★ 字号/颜色可调 ✓
    		            //     ⚠ 原来 `CalcTextSize(s.c_str(), 0, 25)` 是**误用**：
    		            //       CalcTextSize 第 3 个参数是 bool，`25` 被转成 true，
    		            //       字号参数缺失 -> 一直用默认字号 ✗
    		            ImFont* hf = ImGui::GetFont();
    		            float hsz = g_esp.hatch_size;
    		            const ImU32 hcol = (ImU32)g_esp.hatch_color;
    		            const float hlh = hsz + 2.0f;          // 行高跟字号走 ✓

    		            // ① 名字（上）
    		            const char* hname = "[地窖]";
    		            auto hts = hf->CalcTextSizeA(hsz, FLT_MAX, 0.0f, hname);
    		            Draw->AddText(hf, hsz, {r_x - hts.x / 2.0f, r_y}, hcol, hname);

    		            // ② 距离（下）
    		            char hdist[16];
    		            snprintf(hdist, sizeof(hdist), "%dm", (int)dist);
    		            auto hds = hf->CalcTextSizeA(hsz, FLT_MAX, 0.0f, hdist);
    		            Draw->AddText(hf, hsz, {r_x - hds.x / 2.0f, r_y + hlh}, hcol, hdist);
    		        }
    		    }

    	    else if (strstr(data[i].class_name, "sender") != NULL){
    	        // 判据见上面 is_real_generator() 的注释。若发现密码机被破译完成后从叠加层消失，改那里。
    	        if (show_draw_secret_mechine && is_real_generator(data[i].obj)){
    	            // 进度来自 Python 侧的 GeneratorUnit，靠 model 指针身份配对(见 PyProgress.h)。
    	            // 配不上时(model 为空/链路失效)退化成原来的 "[密码机] X.X 米"。
    	            float progress = 0.f;
    	            bool has_progress = PyProgress::lookup(data[i].obj, D.X, D.Y, progress);
    	            if (!(has_progress && PyProgress::is_decoded(progress))){      // 破译完的机器本身和进度都不画
    	                //   ★★ 2026-10-13 用户要求：改成**三排**布局 ★★
    	                //     原话：「密码机绘制改成距离和进度还有进度条不同排，
    	                //            距离在上，进度在中间，进度条在下方」
    	                //   ★ 同时：**距离不显示小数** ✓
    	                //     「比如 77.4m 显示 77m 就行」-> `(int)dist` ✓
    	                //     原来用 `ostringstream << fixed << setprecision(1)`
    	                //     是**一位小数** ✗ 现在直接整数 ✓
    	                char dist_text[16];
    	                snprintf(dist_text, sizeof(dist_text), "%dm", (int)dist);
    	                char head[24];
    	                if (has_progress) snprintf(head, sizeof(head), "%.1f%%", progress);
    	                else              snprintf(head, sizeof(head), "密码机");

    	                ImFont* gf = ImGui::GetFont();
    	                float gsz = g_esp.gen_size;
    	                const float lh = gsz + 2.0f;          // 行高跟着字号走 ✓

    	                //   ---- 布局：以 r_x 水平居中，r_y 是"距离"那一排的基线 ----
    	                //     距离在上 -> 进度在中间 -> 进度条在下方 ✓
    	                auto ds = gf->CalcTextSizeA(gsz, FLT_MAX, 0.0f, dist_text);
    	                auto hs = gf->CalcTextSizeA(gsz, FLT_MAX, 0.0f, head);
    	                const float y_dist  = r_y;
    	                const float y_prog  = r_y + lh;
    	                const float y_bar   = r_y + lh * 2.0f;   // 进度条顶边

    	                //   颜色规则**沿用原来的**（没有要求改色）✓
    	                //     距离：61~63 米变绿（原来是"[密码机] + 距离"一起上色，
    	                //       现在距离单独一排，规则照旧适用）
    	                const ImU32 dist_col =
    	                    ((int)dist >= 61 && (int)dist <= 63) ? (ImU32)color_green
    	                                                         : (ImU32)g_esp.gen_color;
    	                //     进度文字/条：有进度用进度色，无进度用配置色
    	                //   ⚠ 三元里两个分支类型必须一致 ✗
    	                //     progress_color() 返回 ImColor，g_esp.gen_color 是 ImU32，
    	                //     两者可互相隐式转换 -> 编译器报"歧义" ✗ 都显式转 ImU32 ✓
    	                const ImU32 prog_col = has_progress ? (ImU32)progress_color(progress)
    	                                                    : (ImU32)g_esp.gen_color;

    	                // ① 距离（上）
    	                Draw->AddText(gf, gsz, {r_x - ds.x / 2.0f, y_dist}, dist_col, dist_text);
    	                // ② 进度（中）
    	                Draw->AddText(gf, gsz, {r_x - hs.x / 2.0f, y_prog}, prog_col, head);

    	                // ③ 进度条（下）；进度为 0 时没有意义，不画
    	                if (has_progress && progress > 0.05f){
    	                    //   ★ 尺寸 / 底槽色可调 ✓
    	                    const float bar_w = g_esp.gen_bar_w, bar_h = g_esp.gen_bar_h;
    	                    float bx = r_x - bar_w / 2.0f, by = y_bar;
    	                    // ★ 圆润边角（用户要求）：底槽 + 填充段都做成胶囊形，
    	                    //   填充很短时用一个不超过自身宽度一半的圆角，避免"扁成一条线"
    	                    const float rad = bar_h * 0.5f;
    	                    Draw->AddRectFilled({bx, by}, {bx + bar_w, by + bar_h},
    	                                        (ImU32)g_esp.gen_bar_bg, rad);
    	                    const float fw = bar_w * (progress / 100.0f);
    	                    const float fr = (fw < bar_h) ? fw * 0.5f : rad;
    	                    Draw->AddRectFilled({bx, by}, {bx + fw, by + bar_h},
    	                                        progress_color(progress), fr);
    	                    Draw->AddRect({bx, by}, {bx + bar_w, by + bar_h},
    	                                  ImColor(255,255,255,180), rad, 0, 1.2f);
    	                }
    	            }
    	        }
    	    }
    		}
	
            /*   2026-10-26：**地面道具**（用户要求恢复）
             *   只画"没被人拿着"的道具：
             *     ① 水平距离 >= 1.5 米（挂件贴在骨骼上，三维距离恒有 0.7 米底噪）
             *     ② 上一帧没有求生站在它 2 米内
             *   画法：道具名 + 距离（"橄榄球 4m"），位置按道具本体的投影点。
             */
            if (show_draw_Prop && data[i].camp == 4) {
                /*   2026-10-26：**只画"躺在地上没人拿"的道具**
                 *   三道门（实测踩过的坑都在这）：
                 *     ① 坐标全 0 = 这个道具没真正摆到场上（厂长傀儡就是 (0,0,0)），
                 *        以前会画在屏幕中间一个随机位置 —— 直接跳过。
                 *     ② 上一帧任何求生站在它 2 米内 = 被人拿着 -> 不画。
                 *        这条**不再依赖距离判断**：挂点比脚底高 0.7 米，三维距离恒有底噪。
                 *     ③ 自己身上的再算一次水平距离（<1.5 米也当拿着）。
                 *   标签位置用**道具自己的投影**，不用 r_x/r_y（那两个是上一个实体的，
                 *   会串位置）。
                 */
                if (fabsf(D.X) < 1.0f && fabsf(D.Y) < 1.0f) continue;

                bool on_body = false;
                /*   2026-10-26：**这里原来少乘 dist_scale**
                 *   `<= 2.0f * 2.0f` 里的 2.0 是**世界单位**（≈0.17 米），
                 *   而挂在人身上的道具离人 0.2~0.5 米 = 2.4~6 世界单位 ->
                 *   判据永远不成立，手上拿着的道具照样被当成"地上的"画出来。
                 */
                const float ob_r = 2.0f * dist_scale;   // 2 米 -> 世界单位
                for (int k = 0; k < s_svn_prev; k++) {
                    const float ddx = D.X - s_svx_prev[k];
                    const float ddy = D.Y - s_svy_prev[k];
                    if (ddx * ddx + ddy * ddy <= ob_r * ob_r) { on_body = true; break; }
                }
                if (on_body) continue;

                float prop_hdist = sqrt(pow(D.X - Z.X, 2) + pow(D.Y - Z.Y, 2)) / dist_scale;
                if (prop_hdist < 1.5f) continue;

                const char* propName = getprop(data[i].class_name);
                if (propName) {
                    float pcam = matrix[3]*D.X + matrix[7]*D.Z + matrix[11]*D.Y + matrix[15];
                    if (pcam > 0.01f) {
                        char pb[64];
                        snprintf(pb, sizeof(pb), "%s %dm", propName, (int)dist);
                        float pbx = px + (matrix[0]*D.X + matrix[4]*D.Z + matrix[8]*D.Y + matrix[12]) / pcam * px;
                        float pby = py - (matrix[1]*D.X + matrix[5]*D.Z + matrix[9]*D.Y + matrix[13]) / pcam * py;
                        ImFont* ppf = ImGui::GetFont();
                        float pps = g_esp.prop_size;
                        ImVec2 psz = ppf->CalcTextSizeA(pps, FLT_MAX, 0.0f, pb);
                        Draw->AddText(ppf, pps, {pbx - (psz.x / 2), pby}, g_esp.prop_color, pb);
                    }
                }
            }

            // 这里原本读一次 +0xaa(zy)，唯一用到它的是下面那段已停用的相机深度启发式，已删。
            // ★ 宝箱/怪物 ESP —— 独立于 ShouldSkipEntity，先处理
if (data[i].camp == 6 || data[i].camp == 7) {
    // 距离：优先用 PyMiniMap 拿到的"自己"坐标，不依赖 Z（Z 在 goldrush 里可能没更新）
//
// ★ 坐标系约定（务必对齐，否则距离会算错）：
//   主循环里 D 的语义：  D.X = 世界X    D.Z = 高度      D.Y = 水平Z
//   PyMiniMap 里 tm 的： tm.x = 世界X   tm.y = 高度     tm.z = 水平Z
//   所以 X 用 D.X，高度用 D.Z，水平Z 用 D.Y —— 三个方向要一一对应。
float self_wx = g_has_self_pos ? g_self_x : Z.X;   // 世界 X
float self_wy = g_has_self_pos ? g_self_y : Z.Z;   // 高度
float self_wz = g_has_self_pos ? g_self_z : Z.Y;   // 水平 Z

float mdx = D.X - self_wx;   // X 方向
float mdy = D.Z - self_wy;   // 高度方向
float mdz = D.Y - self_wz;   // 水平 Z 方向
float mdist = sqrtf(mdx*mdx + mdy*mdy + mdz*mdz) / dist_scale;

    // 用宝箱自己的中心点做投影，不用 W/H（W/H 是给角色算的，宝箱可能为 0）
    float cam = matrix[3]*D.X + matrix[7]*D.Z + matrix[11]*D.Y + matrix[15];
    if (cam > 0.01f && mdist <= g_mark_max_dist) {
        float bx = px + (matrix[0]*D.X + matrix[4]*D.Z + matrix[8]*D.Y + matrix[12]) / cam * px;
        float by = py - (matrix[1]*D.X + matrix[5]*D.Z + matrix[9]*D.Y + matrix[13]) / cam * py;

        bool is_chest = (data[i].camp == 6);
ImU32 mc = is_chest ? g_mark_col_chest : g_mark_col_monster;

// 宝箱和怪物都不画框，只显示名字+距离

        char mbuf[64];
        snprintf(mbuf, sizeof(mbuf), "%s %.0fm",
            is_chest ? "宝箱" : "怪物", mdist);
        //   ★ 字号/颜色可调（原来字号写死 25，颜色用 g_mark_col_*）✓
        //     `mc` 仍按宝箱/怪物区分；g_esp.mark_color 是**可选的统一覆盖** ——
        //     默认透明，这样不去动原有的宝箱/怪物配色 ✓
        ImFont* font = ImGui::GetFont();
        float ns2 = g_esp.mark_size;
        ImVec2 ts2 = font->CalcTextSizeA(ns2, FLT_MAX, 0.0f, mbuf);
        ImGui::GetForegroundDrawList()->AddText(font, ns2,
            ImVec2(bx - ts2.x*0.5f, by - 50.0f), mc, mbuf);
    }
    continue;   // 不进入角色 ESP 分支
}

if (!ShouldSkipEntity(data[i])){
    if (show_draw_Role&&strstr(data[i].class_name, "chr") != NULL){
    std::string test;
    test += " [";
    test += std::to_string((int) dist);    
    test += " m]  0x";
    test += objtext;    
    test += " [类名] ";
    test += data[i].class_name;
    auto textSize = ImGui::CalcTextSize(test.c_str(), 0, 25);
    Draw->AddText({r_x-(textSize.x/2),r_y}, ImColor(255,200,0,255), test.c_str());
}
            			            
// 旧的自身判定：相机深度落在 10~40 + zy + 阵营是1或2。**已停用**(见下)。
// 原本只在 CPython 链路拿不到锚点时兜底，它的问题见 PySelf.h 文件头：
//   - 任何一个求生者走进 10~40 这条深度带都会被误判成自身
//   - zy 是 +0xaa，实测是通用状态位，**不区分是否自身**，挡不住
//   - 自身锚错 -> Z 锚错 -> 全场距离全错，而且那个人会被 continue 掉不再绘制
// 2026-09-22 停用：暂时只走 CPython 锚点(PySelf.h)，锚点拿不到时宁可没有自身也不猜。
// if (!有权威自身 && camera < 40 && camera > 10 && zy&&(data[i].阵营==1||data[i].阵营==2)){
//     自身 = data[i].obj;
//     Z.X = D.X;
//     Z.Z = D.Z;
//     Z.Y = D.Y;
//     自身阵营=对象阵营;
//        continue;
// }

std::string s;

    // （原来的宝箱/怪物 ESP 段删掉）

                // 离自己 1 米以内的角色不画方框和附带数据(名字/距离/天赋/特质/射线)：
                // 贴身的基本是自己身上的其它模型(备用形态、残留模型)，画出来只会挡视线。
                // 角色原点都在脚底，三维距离即可(道具那边是挂点偏高，才只能算水平距离)。
                // 没有自身锚点时 Z 不可信，不做这层过滤。
                //bool too_close = (self_obj != 0) && dist < 1.0f;

                //if (!too_close && (data[i].camp==1||data[i].camp==2)){
                // [已删除] 原来 1 米内不绘制的过滤，用户要求删除
// ⚠ camp 8 = 红夫人镜像，也必须放进来。
//   之前只放 1/2，所以镜像即使被正确分类、颜色也写好了，
//   整段方框+名字绘制根本不会执行 —— 表现就是"分类对了但什么都不画"。
//
//   ★★ 2026-10-13 新增：「隐藏红夫人自身镜像」开关 ✓
//     打开后 **camp 8 整个不画**（框、名字、距离都不画）✗
//   为什么放在这个总入口判：这是 camp8 绘制的**唯一入口** ✓
//     （下面 3440/3458 只是给它选颜色，不放行就都到不了）
//   为什么要这个开关（用户的实际需要）：
//     红夫人放镜子时，镜像的**本体**也会被识别成一个实体，
//     画出来会和真身重叠/混淆 ✗ 玩家只想看**镜中的求生者**
//     （那是上面 `mirror && redqueenmod` 那段算出来的）✗
if (data[i].camp==1||data[i].camp==2||(data[i].camp==8 && !hide_rq_self_mirror)){
                    //   ★★ 形态判据用**数据来源**，不用坐标（用户要求）★★
                    //     人形态：ButcherUnit.model 的场景 == data[] 这条的对象（同一个）-> 正常画
                    //     球形态：它指向另一个场景（球的模型）-> 这条是人形旧对象，跳过，交给单位表那条
                    if (data[i].camp == 1) {
                        //   ★★ 2026-10-24：这条是给**台球手变球**打的补丁
                        //     （变球后 ButcherUnit.model 指向球，data[] 里还留着人形旧对象）。
                        //     但它把"监管的从属单位"也一起丢掉了 —— 梦之女巫的信徒
                        //     永远 != hunter_scene()，于是整批 continue，屏幕上只剩本体。
                        //   两道限定：
                        //     ① 女巫族主模型（本体/信徒）永不放这条枪下
                        //     ② 只有**真·球形态**才启用这条（人形态根本不需要）
                        const uint64_t hs = PyGenius::hunter_scene();
                        if (hs != 0 && (uint64_t)data[i].obj != hs
                            && !is_yidhra_main_model(data[i].class_name)
                            && PyGenius::hunter_ball())
                            continue;
                    }
                    // 模仿者模式：名字换成"N号 身份"，文字和方框按阵营着色(侦探团蓝/模仿者红/中立黄)。
                    // 身份进局约 2 秒后才赋值，之前只有号码、画白色；不是模仿者玩家本体的对象照旧画角色名
                    // ★ cc_live / cc_dead / cc_ident / cc_seat 在过滤链开头就算好了(那里必须知道
                    //   这个对象是谁才能决定放不放行)，这里只负责排版，不再重复查一遍簿子。
                    ImColor name_color = ImColor(255,200,0,255);
                    if (cc_label){
                        char cc_text[64];
                        int len = 0;
                        // ★ 尸体：用户要的是一眼认出"这是具尸体"，所以打头就是「尸体」两个字，
                        //   后面跟上号码和身份(免得还要去身份悬浮窗对号)。距离由下面
                        //   show_draw_Distance 那套自动画在框下方，不用在这里重复。
                        if (cc_dead_body_hit) {
                            len = snprintf(cc_text, sizeof(cc_text), "尸体");
                            if (cc_seat > 0)
                                len += snprintf(cc_text + len, sizeof(cc_text) - len, " %d号", cc_seat);
                            const char *nm2 = PySelf::copycat_identity_name(cc_ident);
                            if (nm2)               snprintf(cc_text + len, sizeof(cc_text) - len, " %s", nm2);
                            else if (cc_ident)     snprintf(cc_text + len, sizeof(cc_text) - len, " 身份%d", cc_ident);
                            name_color = kCorpseColor;
                        } else {
                            const char *nm = PySelf::copycat_identity_name(cc_ident);
                            len = cc_seat > 0 ? snprintf(cc_text, sizeof(cc_text), "%d号 ", cc_seat) : 0;
                            if (len < 0) len = 0;
                            if (nm) snprintf(cc_text + len, sizeof(cc_text) - len, "%s", nm);
                            else if (cc_ident) snprintf(cc_text + len, sizeof(cc_text) - len, "身份%d", cc_ident);
                            else snprintf(cc_text + len, sizeof(cc_text) - len, "?");
                            if (cc_dead) {
                                const int l2 = (int)strlen(cc_text);
                                snprintf(cc_text + l2, sizeof(cc_text) - l2, "（已死）");
                                name_color = kCopycatDeadColor;
                            } else {
                                name_color = copycat_color(cc_ident);
                            }
                        }
                        s += cc_text;
                    } else {
                        s+=data[i].str;
                    }
                    
                    ImFont* font = ImGui::GetFont();
float ns = g_esp.name_size;
ImVec2 ts = font->CalcTextSizeA(ns, FLT_MAX, 0.0f, s.c_str());
ImU32 nc = cc_label ? (ImU32)name_color : g_esp.name_color;
//   ★★ 2026-10-13 用户要求：**天赋显示到角色名字上方** ★★
//     原话：「天赋显示到绘制的角色名字上方」
//   原来布局：
//     框上方 Y1-45  -> 名字
//     框下方 Y2+10  -> 距离（往下一排排堆）-> 天赋 -> 技能CD   ✗
//   现在布局（全部堆在**框上方**，从下往上）：
//     Y1-45                    -> 名字          （位置不变 ✓）
//     名字上方                 -> 特长/飞轮/大心脏（天赋）    <- 新位置
//     天赋上方                 -> 技能CD（辅助特质）          <- 跟着上移，保持相对顺序
//     框下方 Y2+10 往下的       -> 距离（**只有它留在下面**）✓
//   ⚠ 这一改必须把"往下的游标"和"往上的游标"分开 ✗
//     原来共用一个 `next_y = Y2 + 10`，天赋就是靠它被推到下方的 ✗
//     现在：`next_y` 只给距离用；天赋/CD 用新的 `above_y`（从名字往上）✓
//   为什么名字位置不动：用户只说"天赋到名字上方"，
//     没说移动名字 —— 移动它会连带改变其他勾选项的观感 ✗
//     不动 = 改动面最小 = 最不容易出意外 ✓
//   ★★ 2026-10-13 用户反馈（第二次）："名字和天赋中间的空档太大，
//      改成名字上面紧靠着天赋名字，天赋名字上面紧靠着cd" ★★
//
//   ── 为什么第一版还是显得空 ────────────────────────────────────
//     第一版：`name_lift = size*0.30 + 8`，`gap = 8`
//     问题在于**我没有考虑字体自身的行高** ✗
//       ImGui 的字体有 ascent/descent，字号 25 的字体行高约 29~30px，
//       但我按 `size`(=25) 留的间隙 -> 实际视觉间隙 ≈ gap + (行高-字号) ✗
//     而且 `+8` 是我拍的，没有测量依据 ✗
//
//   ── 现在：**紧贴**（间隙只用"字形的实际墨迹高度"算，不留拍脑袋的余量）──
//     用字体自己的 `CalcTextSizeA` 结果（含 ascent/descent）作为行高 ✓
//     层与层之间只留 1px（几乎接触，视觉上就是"紧靠"）✓
//
//   ── 堆叠顺序（从下往上）────────────────────────────────────
//       技能CD   <- 紧靠天赋上面
//       天赋     <- 紧靠名字上面
//       名字     <- 紧靠框顶
//       [ 框 ]
ImFont* lf = ImGui::GetFont();
//   ★ 层间隙改为**配置项**（默认 0 = 贴死）✓
//     原来是写死的 kLineTouch=1.0 —— 用户反馈还是空 ✗
//     与其继续猜，不如让用户自己能调到位 ✓
const float kLineTouch = g_esp.label_line_gap;
// ============================================================================
// ★★☠☠ 2026-10-13 空档的**真正根因**：我一直把"行盒高"当成"占位高" ✗✗
//
//   ── 实测（fn=label_pos 的"字体度量"那几行）──────────────────────
//     ImFontBaked(字号 26.66): Size=27.0  Ascent=21.0  Descent=-7.0
//     字形 'A':  Y0=5.0  Y1=22.0     <- **字形在行盒里从 5.0 才开始** ✗
//     字形 'g':  Y0=9.0  Y1=26.0
//
//   ── 问题 ────────────────────────────────────────────────────
//     `CalcTextSizeA(size,…)` 返回的是**行盒高度**（≈ size = 26.7）✗
//     而字母**真实墨迹高只有 17**（Y0=5 -> Y1=22）✗
//     => 行盒里**上方空 5px、下方空 4.7px** ✗
//     我拿"行盒高"当占位高 -> 每层凭空多 ~10px，
//       加上行盒本身比墨迹高的部分 -> **视觉空档 ≈ 26.7px** ✗
//     **这就是我推 4 次都说"只有 1px"、而用户看到"空很大"的原因**：
//        我量的是**行盒**，用户看的是**墨迹** ✗✗
//
//   ── 修法：按**墨迹**对齐 ✗ 不按行盒 ✗
//     文字 y 是**行盒顶**；字形顶 = y + off（off = -Ascent，负值）✓
//     字形底 = y + size + Descent（Descent 在本版是负值）✓
//     => 墨迹高 h = (size + Descent) - (-Ascent) ✓
//        顶部偏移 off = -Ascent ✓
//     摆放：文字 y = 目标墨迹顶 - off ；游标上移量 = **h + 层间距** ✓
//   ⚠ Ascent/Descent 是**未缩放**值，用前乘 scale = size / baked->Size ✗
//   ⚠ 这个函数必须放在**文件作用域** ✗
//     我第一版写在了 Draw() 内部 -> 编译报 "function definition is not allowed here" ✗
//     C++ 不允许函数内定义函数 ✓
//   ★ 定义已移到文件上方（`ink_metrics`），这里只是使用 ✓
// ============================================================================
//   名字行高仍记给诊断（对比用），但**摆放改用墨迹** ✓
const float name_line_h = lf->CalcTextSizeA(g_esp.name_size, FLT_MAX, 0.0f, "Ag").y;
const InkMetrics nm_ink = ink_metrics(lf, g_esp.name_size);
//   ★★ `above_y` 的语义统一为「**下一层的墨迹顶部**」✗✗
//     我前几版把它当"行盒顶"，于是每层都要在"行盒"和"墨迹"之间换算，
//     换错一次就多出十几 px 的空隙 ✗（实测两层墨迹空隙 19px，配置要 0 ✗）
//   => 统一语义后，摆放公式对**每一层都一样**，不可能再算错 ✓：
//        文字 y = above_y - 该层墨迹偏移(off)
//        画完后  above_y -= (该层墨迹高 + 层间距)
//   名字的墨迹**底边**摆在框顶**上方 name_lift px** 处：
//     墨迹顶 = Y1 - name_lift - h  =>  above_y = Y1 - g_esp.name_lift - h ✓
//   ★ 2026-10-13 用户要求：原来贴死框顶(0)太近 -> 默认 6px ✓
//     天赋/CD 会**跟着一起上移**（它们是相对名字堆叠的）✓ 这是对的：
//       用户只说"名字离框太近"，上移名字后天赋跟着走，三层的相对关系不变 ✓
/*   2026-10-26：**标签跟随框顶（先斩后奏版）**
 *   骨骼头是后面才算出来的，而名字这行在它之前就画了。
 *   所以这里先用**上一帧**算出的位移把 Y1 挪上去 —— 一帧的延迟肉眼看不出来，
 *   但名字/天赋/技能整块都会跟框一起上移。
 */

float above_y = nm_ink.ok ? (Y1 - g_esp.name_lift - nm_ink.h)
                          : (Y1 - g_esp.name_lift - name_line_h);
//   ★ 诊断：记下当前实体的框顶、名字 Y、以及量出来的行高 ✓（只读）
g_diag_lbl_y1  = Y1;
g_diag_lbl_nlh = name_line_h;
g_diag_lbl_gap = kLineTouch;
g_diag_lbl_ink_off = nm_ink.off;
g_diag_lbl_ink_h   = nm_ink.h;
if (show_draw_Name) {
        ImFont* font = ImGui::GetFont();
        float ns = g_esp.name_size;
        ImVec2 ts = font->CalcTextSizeA(ns, FLT_MAX, 0.0f, s.c_str());
        ImU32 nc = cc_label ? (ImU32)name_color : g_esp.name_color;
        if (!cc_label && data[i].camp == 8) nc = IM_COL32(255, 255, 255, 255);   // 镜像名字也白
        //   ★ above_y 是"墨迹顶"语义 -> 文字 y = above_y - off ✓
        const float name_draw_y = nm_ink.ok ? (above_y - nm_ink.off) : above_y;
        Draw->AddText(font, ns, {X1 + W/2 - ts.x/2, name_draw_y}, nc, s.c_str());
        g_diag_lbl_name_y = name_draw_y;      // ★ 诊断：名字实际画在哪 ✓
        //   ★ 游标上移**墨迹高 + 层间距**（不是行盒高）✗ 这是修好空档的关键 ✓
        above_y -= ((nm_ink.ok ? nm_ink.h : name_line_h) + kLineTouch);
    }

if (show_draw_Bones) {
    // ---- 原生骨骼：M = data[i].obj（就是 model+0x20 那个场景对象）----
    //   读不到就跳过（read_unit 内部逐阶段写 g_log，面板上能看到断在哪）
    NativeBones::Skeleton sk;
    if (NativeBones::read_unit((uint64_t)data[i].obj, false, sk) && sk.valid) {
        ImVec2 bp[NativeBones::NUM_TARGET];
        bool   bok[NativeBones::NUM_TARGET];
        for (int t = 0; t < NativeBones::NUM_TARGET; t++) {
            bok[t] = false;
            if (!sk.ok[t]) continue;
            float ox = sk.x[t], oy = sk.y[t], oz = sk.z[t];
            float cam = matrix[3]*ox + matrix[7]*oy + matrix[11]*oz + matrix[15];
            if (cam <= 0.01f) continue;                 // 背后剔除
            float sx = px + (matrix[0]*ox + matrix[4]*oy + matrix[8]*oz + matrix[12]) / cam * px;
            float sy = py - (matrix[1]*ox + matrix[5]*oy + matrix[9]*oz + matrix[13]) / cam * py;
            bp[t] = ImVec2(sx, sy); bok[t] = true;
        }
        const ImU32 bcol = (data[i].camp == 1) ? g_esp.bones_hunter : g_esp.bones_survivor;
        const float bth  = g_esp.bones_thickness;
        // 16 条边；slot0(头) 不参与连线，用圆表示
        for (int e = 0; e < NativeBones::NUM_EDGE; e++) {
            int a = NativeBones::EDGES[e][0], b = NativeBones::EDGES[e][1];
            if (a == 0 || b == 0) continue;
            if (bok[a] && bok[b]) Draw->AddLine(bp[a], bp[b], bcol, bth);
        }
        // 头：颈→头方向外推一个圆；半径按颈到脊柱距离算（跟着镜头远近缩放）
        if (g_esp.bones_head && bok[0]) {
            ImVec2 up(0.f, -1.f);
            float r = 6.0f;
            if (bok[1] && bok[2]) {
                float dx = bp[1].x - bp[2].x, dy = bp[1].y - bp[2].y;
                float L = sqrtf(dx*dx + dy*dy);
                if (L > 0.5f) { up.x = dx / L; up.y = dy / L; r = L * 0.28f; }
            }
            //   2026-10-26：头部粗细（滑条以 9px 为基准做缩放）
            {
                const float k = g_esp.bones_head_size / 9.0f;
                if (k > 0.2f) r *= k;
            }
            if (r < 2.0f) r = 2.0f;
            if (r > 40.0f) r = 40.0f;
            ImVec2 base = bok[1] ? bp[1] : bp[0];
            ImVec2 hc(base.x + up.x * (r + 3.0f), base.y + up.y * (r + 3.0f));
            /*   2026-10-26：记下这个角色骨骼头的最上方（同步绘制框按它算） */
            g_bone_top_obj = (uint64_t)data[i].obj;
            g_bone_top_y   = hc.y - r - 4.0f;   // 仅诊断用，不再改 Y1
            if (bok[1]) Draw->AddLine(bp[1], ImVec2(hc.x - up.x * r, hc.y - up.y * r), bcol, bth);
            Draw->AddCircleFilled(hc, r, bcol, 16);
        }
    }
}
if (show_draw_Rect){
    ImU32 rc;
    if (cc_live) {
        switch (PySelf::copycat_camp(cc_ident)) {
            case 1:  rc = g_esp.rect_cc_good;    break;
            case 2:  rc = g_esp.rect_cc_bad;     break;
            case 3:  rc = g_esp.rect_cc_neutral; break;
            default: rc = g_esp.rect_ghost;      break;
        }
    }
    // ★ 死者(灵魂体 / 会议桌上的座位模型)：固定用"鬼魂"色(配置里 esp.rect_ghost，默认白)。
    //   必须排在那两条 camp==1/2 之前 —— 死者身上那个对象的 camp 也是 1/2，
    //   否则死人又被涂成求生者白/监管红，看着跟活人一样。
    else if (cc_dead)                 rc = g_esp.rect_ghost;
    else if (data[i].camp == 8)       rc = IM_COL32(255, 255, 255, 255);  // 红夫人镜像：白
    // ⚠ 只对**非求生**用幽灵色（2026-10-12 用户反馈后修）
    //   原来这条不限阵营：玩家一进镜像世界，**真人自己就变成异世界** ✓
    //   -> 被涂成白色 ✗（用户实测：进入镜像后现实变白色了）。
    //   求生（camp2）的颜色现在完全由「学习器学到的镜像表」决定：
    //     在表里 -> 镜像色；不在表里 -> 正常求生色 ✓ 与玩家在哪个世界无关 ✓
    //   死者另有 cc_dead 分支处理，不受影响 ✓
    else if (data[i].camp != 2 && IsGhostEntity(data[i]))  rc = g_esp.rect_ghost;
    else if (data[i].camp == 1)       rc = g_esp.rect_hunter;
    // ★★ 镜像（学习器学到的地址）—— 恒用镜像色，**与玩家在哪个世界无关** ✓
    //   判据来源：mirror_learn.h 的跳变学习器（实测抓到 169~756 米/17ms 的瞬移）
// ★ 回滚到用户已验证正常的版本（2026-10-12）：只用**学到的地址表**上色。
    //   加时间窗/静止判据那两版都失败了（把真人也染白 ✗ / 过滤不生效 ✗）。
    else if (data[i].camp == 2 && PyGenius::is_mirror_unit(data[i].obj))
                                      rc = g_mirror_col;
    else if (data[i].camp == 9)       rc = g_joseph_cam_col;   // ★ 约瑟夫相机（排在幽灵之前）
    else                              rc = g_esp.rect_survivor;
    //   ★ 诊断对照：camp1 的框同时用**两个列表**各画一次
    //     Draw       = 名字/距离用的那个列表（截图里能看到）
    //     foreground = 原来框用的列表（截图里看不到）
    //   ★ 台球形态：主循环这条框就是球本身（球形态下 raw 坐标是坏的，fb_ok=1），
    //     标签直接画在这个框上，别再让"单位表补画"那套去猜位置。
    //   ★ 球形态（camp1 且 raw 坐标坏 -> fb_ok）**交给"单位表补画"那条画**：
    //     球的场景对象不在 data[] 里，主循环这条对球不生效（实测截图无框）。
    //   ★ 走到这里说明不是球形态（球形态在上面的 4440 行已经 continue 了），
    //     所以监管在这里正常画框 —— 之前写成 `camp != 1` 把人形态也挡掉了。
    DrawEspRect(Draw, X1, Y1, X2, Y2, W, H, rc);
    }   // if (show_draw_Rect)
//    道具固定在人物绘制框右边，而不是跟着道具本体跑）。
//    做法：扫描 data[] 里的道具(camp==4)，读它的世界坐标，
//    和**当前这个求生**（本轮的 D.X/D.Y 就是它）水平距离 2 米内就认作"挂在他身上"。
//    代价：每个求生每帧多读几次道具坐标（道具通常 1~5 个，每次 ioctl ~3µs），
//    60fps 下约 0.1% CPU，可以接受。
//    挂在哪儿：框的右边缘 (X1+W) 往外 8px，从上往下依次排，避免多个道具重叠。
/*   2026-10-26：**框右边的道具名已删**（用户要求）
 *   这里原来把"挂在求生身上"的道具名画在框右边缘（如 "橄榄球"）。
 *   现在只**记坐标**，给下面 camp==4 那段判断"这个道具是不是被人拿着"用。
 */
if (data[i].camp == 2) {
    if (s_svn_cur < 16) {
        s_svx_cur[s_svn_cur] = D.X;
        s_svy_cur[s_svn_cur] = D.Y;
        s_svn_cur++;
    }
}

                    
                    // 宿伞模式：显示"还差多少米"刚好能传到该求生者
if (g_umbrella_charging && g_umbrella_start_valid && data[i].camp == 2) {
    // 起点到求生者的水平距离(米)
    float dx = D.X - g_umbrella_start_x;
    float dz = D.Y - g_umbrella_start_z;
    float dist_from_start = sqrtf(dx*dx + dz*dz) / dist_scale;

    // 当前蓄力已经能传的距离
    float already = g_umbrella_charge_ratio * kUmbrellaMaxMeters;

    // 还差多少米
    float gap = dist_from_start - already;
    char ubuf[32];
    if (gap <= 0.5f) snprintf(ubuf, sizeof(ubuf), "OK");
    else             snprintf(ubuf, sizeof(ubuf), "%.0fm", gap);
    Draw->AddText({X2 + 8, Y1}, ImColor(255, 255, 255, 255), ubuf);
}

float next_y = Y2 + 10;
const float dist_line_h = ImGui::GetFontSize() + 2.0f;
if (show_draw_Distance){
    std::string dist_str = std::to_string((int) dist) + "m";
    //   ★★ 这里原来有个真 bug：`dist_size` 声明了、也存进配置了，
    //      但**绘制时根本没用** —— 文字一直用默认字号 ✗
    //      而且 CalcTextSize(…, 0, 25) 的第 3 个参数是 bool 不是字号 ✗
    //      现在统一走 CalcTextSizeA + AddText(font,size,…) ✓
    ImFont* df = ImGui::GetFont();
    float dsz = g_esp.dist_size;
    auto textSize = df->CalcTextSizeA(dsz, FLT_MAX, 0.0f, dist_str.c_str());
    Draw->AddText(df, dsz, {X1 + W/2-(textSize.x/2), next_y}, g_esp.dist_color, dist_str.c_str());
    next_y += dsz + 2.0f;      // 行高跟着字号走（原来用 GetFontSize，和实际字号不一致 ✗）
}

// ===================== 治疗进度（天赋上方）=====================
//   数据：PyGenius::heal_of(scene_obj) <- unit.cured_process_client，纯驱动读
//   只画求生者(camp==2)，且 heal>0.5 才画 —— 没被治疗时该字段恒 0，
//   不过滤的话每个框都会多挂一行 "治疗 0%"。
if (show_draw_Heal && data[i].camp == 2) {
    float heal = 0.f, hrate = 0.f, shown = 0.f;
    // 显隐判据用**实际画出来的值**(shown)，不是实测锚点(raw)：
    //   · 治疗刚开始 raw 可能还是 0（游戏没同步），若拿 raw 判就永远看不到 0~30 段
    //   · 快结束时会积分到 100 而 raw 还停在 98，若拿 raw 判就会一直显示 100%
    // 规则：0 < shown < 100 就一直显示
    if (PyGenius::heal_of(data[i].obj, heal) &&
        PyGenius::heal_rate_of(data[i].obj, hrate) &&
        heal_integrate(data[i].obj, heal, hrate,
                       PyGenius::heal_active_of(data[i].obj), shown) &&
        shown > 0.5f && shown < 99.5f) {
        char heal_buf[32];
        snprintf(heal_buf, sizeof(heal_buf), "治疗 %.0f%%", shown);
        ImFont* hf = ImGui::GetFont();
        const float hsz = g_esp.heal_size;
        const ImVec2 hs = hf->CalcTextSizeA(hsz, FLT_MAX, 0.0f, heal_buf);
        Draw->AddText(hf, hsz, {X1 + W/2 - hs.x / 2, next_y}, g_esp.heal_color, heal_buf);
        next_y += hsz + 2.0f;
    }
}
// ============================================================
// ★★ 2026-10-27：天赋/CD 的**数据获取**与**屏幕绘制**拆开 ★★
//
//   起因：`g_share_ent[]`（共享地图网页的数据源）的 upsert 原本
//   整块写在 `if (show_draw_Genius)` 内部 —— 关掉"天赋"显示开关后，
//   网页上的天赋/技能CD 也一起空掉 ✗。
//
//   现在拆成三段：
//     ① 取数据 + 写 g_share_ent  —— 不受任何显示开关控制
//     ② 绘制天赋 / CD            —— 受 show_draw_Genius / show_draw_trait 控制
//     ③ 绘制道具图标             —— 受 show_draw_Prop 独立控制
//       （原来它被嵌在 show_draw_Genius 里，和图标开关没有耦合关系）
//
//   ⚠ 保留原有全部注释 —— 里面记着踩过的坑，别删。
// ============================================================
const bool is_mirror = (data[i].camp == 2) &&
                        PyGenius::is_mirror_unit(data[i].obj);
PyGenius::Info gi;
//   ★ 诊断：逐门计数（用户反馈"局内框上天赋不见了"）✓
//     ⚠ 只在**每道门**各加一个计数，**不动原来的嵌套结构** ✗
//       我第一版插 else 分支时把括号配错了，trait 块整个跑到条件外面 ✗
//       已重写：结构保持原样，计数插在门内第一行 ✓
//   ★ 2026-10-27：这三个计数器现在写在开关外面 —— 它们统计的是"数据获取"，
//     和显示开关无关。看 fn=draw_stats 时，关掉天赋显示也能看到数据有没有读到。
PyDump::g_ds.tn_enter++;
if (is_mirror) PyDump::g_ds.tn_mirror++;
const bool got = (!is_mirror) &&
                 PyGenius::lookup(data[i].obj, D.X, D.Y, gi);
if (!is_mirror && !got) PyDump::g_ds.tn_lookup++;   // 拿不到天赋数据 ✓

// 天赋三段：先算好（绘制和共享地图共用，避免算两遍）
PyGenius::GeniusLine seg;
if (got) PyGenius::genius_segments(gi, seg);

// CD 文本：原来只在 `show_draw_trait` 打开时算 —— 但共享地图要，
//   所以去掉显示开关判断，只保留数据侧条件（监管 + 有特质）。
//   ⚠ 显示开关在下面的绘制块里再判一次，不影响屏上行为。
char trait_line[48] = {0};
if (got && gi.support_trait != 0 &&
    (gi.camp == 1 || gi.camp == PyGenius::YIDHRA_PUPPET_UNIT_TYPE)) {
    PyGenius::trait_line_text(gi, PyGenius::trait_name(gi.support_trait),
                              trait_line, sizeof(trait_line));
}

// ---- ① 共享地图 upsert（不看显示开关）----
if (got) {
    const uint64_t _o2 = (uint64_t)data[i].obj;
    int idx2 = -1;
    for (int q2 = 0; q2 < g_share_ent_n; ++q2)
        if (g_share_ent[q2].obj == _o2) { idx2 = q2; break; }

    // 没找到：先看有没有空槽；满了就找 obj==0 的空槽替换
    if (idx2 < 0) {
        if (g_share_ent_n < 48) {
            idx2 = g_share_ent_n++;
        } else {
            // 表满：优先复用 obj==0 的空槽（换局已 memset，通常全空）
            int oldest = 0;
            for (int q = 1; q < 48; ++q)
                if (g_share_ent[q].obj == 0) { oldest = q; break; }
            idx2 = oldest;
        }
        g_share_ent[idx2].obj = _o2;
    }

    ShareEnt &se = g_share_ent[idx2];
    snprintf(se.tal, sizeof(se.tal), "%s%s%s", seg.pre, seg.flywheel, seg.post);
    snprintf(se.cd,  sizeof(se.cd),  "%s", trait_line);
    se.fw   = (gi.flywheel_remain > 0.0f) ? gi.flywheel_remain : 0.0f;
    se.skcd = (gi.cd_remain > 0.0f) ? gi.cd_remain : 0.0f;
    se.has_fw = (seg.flywheel[0] != 0) ? 1 : 0;
}

// ---- ② 屏幕绘制天赋 / 技能CD（受 show_draw_Genius 控制）----
if (show_draw_Genius){
    if (got){
        if (seg.pre[0] || seg.flywheel[0] || seg.post[0]){
            PyDump::g_ds.tn_drawn++;
            //   ★★ 2026-10-13 第二次修：用户反馈"调成什么颜色都还是绿的" ★★
            //
            //   ── 实测数据（fn=genius_style）──────────────────────────────
            //     camp state bits  desp  segments      最终色
            //     2    2     0x1A  1     双弹||大心脏   #00FF00 语义绿(可用)  ✗
            //     2    2     0x1A  1     双弹||大心脏   #00FF00 语义绿(可用)  ✗
            //     2    2     0xE   1     ||搏命大心脏   #00FF00 语义绿(可用)  ✗
            //     2    2     0xB   1     |飞轮|大心脏   #00FF00 语义绿(可用)  ✗
            //   **每一个求生者都带大心脏** -> 全部走 DESPERATE_AVAILABLE
            //   -> 全部是语义绿 -> 用户配的 `genius_color_surv` 一次都没用上 ✗
            //
            //   ── 我第一版的判断错了 ────────────────────────────────────
            //     我以为"保留语义色 = 保留状态信息，更好" ✗
            //     但：**"带了大心脏且没用"本来就是默认状态** ——
            //     它几乎不提供区分度（人人都绿 = 等于没有信息）✗
            //     而用户明确要的是"**我调的颜色要生效**" ✓
            //     => 用户色优先，语义色退居次要 ✓
            //
            //   ── 现在的规则 ─────────────────────────────────────────
            //     用户配的颜色 = 天赋文字的**基色**（一定能看到，不再被覆盖）✓
            //     仅保留**飞轮就绪=红**这个真正有时效意义的语义色 ✓
            //       （飞轮"就绪"是个瞬时状态，红/不红一眼可辨，值得保留）
            const bool is_surv = (gi.camp == 2);
            const ImU32 base = is_surv ? (ImU32)g_esp.genius_color_surv
                                       : (ImU32)g_esp.genius_color;
            ImColor c = ImColor(base);
            ImColor flywheel_color = seg.flywheel_ready ? color_red : c;
            ImFont* font = ImGui::GetFont();
            float gs = g_esp.genius_size;
            float w_pre = font->CalcTextSizeA(gs, FLT_MAX, 0.0f, seg.pre).x;
            float w_fly = font->CalcTextSizeA(gs, FLT_MAX, 0.0f, seg.flywheel).x;
            float w_pos = font->CalcTextSizeA(gs, FLT_MAX, 0.0f, seg.post).x;
            float tx = X1 + W/2 - (w_pre + w_fly + w_pos)/2;
            //   ★ 从名字上方往上画，**紧贴**（见上面 kLineTouch 的说明）✓
            //     用字体真实行高，不留拍脑袋的余量 ✗
            //   ★ 从名字上方往上画，**按墨迹对齐**（见上面 ink_metrics 的说明）✓
            //     ⚠ 这里**不要动 above_y 的语义** ✗
            //       `above_y` 全程表示"下一层的**行盒顶**" ✓
            //       墨迹对齐只体现在"上移量用墨迹高"上，不需要再加 off ✗
            //       （我第一版在这里加了又减 off，等于没加，还多出两个 bug ✗）
            const InkMetrics g_ink = ink_metrics(font, gs);
            const float gen_line_h = font->CalcTextSizeA(gs, FLT_MAX, 0.0f, "Ag").y;
            //   ★ 统一"墨迹顶"语义：文字 y = above_y - off ；游标 -= (墨迹高 + 间距) ✓
            const float gen_draw_y = g_ink.ok ? (above_y - g_ink.off) : above_y;
            g_diag_lbl_gen_y = gen_draw_y;         // ★ 诊断：天赋画在哪 ✓
            g_diag_lbl_glh   = gen_line_h;
            g_diag_lbl_g_off = g_ink.off;          // ★ 天赋层自己的墨迹偏移 ✓
            g_diag_lbl_g_h   = g_ink.h;
            if (seg.pre[0])      Draw->AddText(font, gs, {tx, gen_draw_y}, c, seg.pre);
            if (seg.flywheel[0]) Draw->AddText(font, gs, {tx + w_pre, gen_draw_y}, flywheel_color, seg.flywheel);
            if (seg.post[0])     Draw->AddText(font, gs, {tx + w_pre + w_fly, gen_draw_y}, c, seg.post);
            above_y -= ((g_ink.ok ? g_ink.h : gen_line_h) + kLineTouch);
        } else {
            PyDump::g_ds.tn_empty++;      // 三段全空，没东西可画 ✓
        }
        if (show_draw_trait && trait_line[0]){
            //   ★ 就绪/冷却**两色都可调** ✓（原来就绪是写死的 color_green ✗）
            ImColor cc2 = PyGenius::cd_ready(gi) ? ImColor(g_esp.trait_ready_color)
                                                 : ImColor(g_esp.trait_color);
            ImFont* font = ImGui::GetFont();
            float cs = g_esp.trait_size;
            float ts2 = font->CalcTextSizeA(cs, FLT_MAX, 0.0f, trait_line).x;
            //   技能CD 画在天赋**紧上方**（同样用真实行高 + 1px）✓
            //   技能CD 画在天赋**紧上方**（同样用墨迹高 + 层间距）✓
            const InkMetrics c_ink = ink_metrics(font, cs);
            const float cd_line_h2 = font->CalcTextSizeA(cs, FLT_MAX, 0.0f, "Ag").y;
            //   2026-10-27：原来这里有一段 `g_share_ent[q].cd` 更新，
            //     现在已合并进上面 ① 的 upsert（`snprintf(se.cd, ...)`），不再重复。
            const float cd_draw_y = c_ink.ok ? (above_y - c_ink.off) : above_y;
            g_diag_lbl_cd_y = cd_draw_y;           // ★ 诊断：CD 画在哪 ✓
            g_diag_lbl_c_off = c_ink.off;          // ★ CD 层自己的墨迹偏移 ✓
            g_diag_lbl_c_h   = c_ink.h;
            Draw->AddText(font, cs, {X1 + W/2 - ts2/2, cd_draw_y}, cc2, trait_line);
            above_y -= ((c_ink.ok ? c_ink.h : cd_line_h2) + kLineTouch);
        }
    }
}

// ---- ③ 道具图标绘制（独立开关，不再嵌在 show_draw_Genius 里）----
//   2026-10-27：原来这段整个包在 `if (show_draw_Genius)` 内部 ——
//   关掉"天赋"显示，图标也一起没了，但它们是**独立开关**，没有耦合的道理。
if (show_draw_Prop) {
    /*   2026-10-26：逐门计数（fn=dbg_icon 读）+ 三种显示模式 + **最多两个道具**
     *   一个求生身上可能同时挂着两件（幸运儿实测 信号枪 1/1 + 橄榄球 耐久 2.99/6），
     *   所以这里按槽循环，一件一行往上叠。
     */
    SurvSkill::IconDbg &_dbg = SurvSkill::g_icon_dbg;
    ++_dbg.draw_frames;
    if (data[i].camp == 2) ++_dbg.draw_c2;
    const JueseIcon *_fic = ItemIcon::Find(data[i].str);
    if (_fic) ++_dbg.draw_find;
    const SurvSkill::ItemInfo *_it = SurvSkill::ItemOfScene((uint64_t)data[i].obj);
    if (_it) ++_dbg.draw_hit;
    //   ★ 最后一帧样本：只记 camp 1/2，dump 里看这一行就知道卡在哪
    if (data[i].camp == 1 || data[i].camp == 2) {
        const SurvSkill::ItemSlot *s0 = (_it && _it->n) ? &_it->it[0] : nullptr;
        _dbg.last_obj  = (uint64_t)data[i].obj;
        _dbg.last_camp = data[i].camp;
        _dbg.last_rem  = s0 ? s0->rem : -2;
        _dbg.last_ini  = s0 ? s0->ini : -2;
        _dbg.last_mode = s0 ? s0->mode : -2;
        _dbg.last_find = _fic ? 1 : 0;
        _dbg.last_hit  = _it ? 1 : 0;
        _dbg.last_px   = g_icon_px;
        _dbg.last_above_y = above_y;
        _dbg.last_x1 = X1; _dbg.last_y1 = Y1; _dbg.last_w = W; _dbg.last_h = H;
        snprintf(_dbg.last_str, sizeof(_dbg.last_str), "%s", data[i].str);
    }
    if (_it && _it->n > 0) {
        const float _px = g_icon_px;
        for (int _k = 0; _k < _it->n; ++_k) {
            const SurvSkill::ItemSlot &_sl = _it->it[_k];
            //   次数型用光就不画（用户要求）
            if (_sl.mode == SurvSkill::ITEM_COUNT && _sl.rem <= 0) continue;
            /*   **手上拿什么就画什么**：先用 item.model_name 找对应图标
             *   （h55_pendant_flaregun → 信号枪），找不到才退回角色自带那张。
             */
            const JueseIcon *_pc = ItemIcon::ByModel(_sl.model);
            if (!_pc) _pc = _fic;
            ImTextureID _ic = ItemIcon::TextureOf(_pc);
            if (!_ic) continue;
            ++_dbg.draw_tex;
            const float _ix = X1 + W/2 - _px/2 + g_icon_dx;
            const float _iy = above_y - _px + g_icon_dy;
            Draw->AddImage(_ic, ImVec2(_ix, _iy), ImVec2(_ix + _px, _iy + _px));
            /*   2026-10-26：**文字规则**（用户要求）
             *     次数型 -> 只写"现在几个"（上限 / N 那个尾巴删了：
             *               勘探员陨石磁铁 _max_use_times=3 而 _init_use_times=1，
             *               以前会显示成刺眼的 "3/1"）
             *     耐久型 -> 不写数字，改成图标外圈的进度环
             *     充能型 -> 同上，环 = _cur_power_num / power_num
             *     CD 型  -> 剩余秒
             *     无消耗 -> 什么都不写
             */
            char _b[32]; _b[0] = 0;
            if (_sl.mode == SurvSkill::ITEM_COUNT) {
                snprintf(_b, sizeof(_b), "%d", _sl.rem);
            } else if (_sl.mode == SurvSkill::ITEM_DURA) {
                //   拿不到上限时环画不出来，这时才退回原始值
                if (!_sl.ring) snprintf(_b, sizeof(_b), "%.0f", (double)_sl.life);
            } else if (_sl.mode == SurvSkill::ITEM_CD) {
                if (_sl.cd > 0.05f) snprintf(_b, sizeof(_b), "%.1f", (double)_sl.cd);
            }

            /*   耐久 / 充能：**图标外面套一圈进度环**
             *   底环压暗色打底，进度从 12 点方向顺时针走。
             */
            if (_sl.ring) {
                const ImVec2 _cc(_ix + _px * 0.5f, _iy + _px * 0.5f);
                const float  _rr = _px * 0.60f;
                float _rate = _sl.ring_rate;
                if (_rate < 0.0f) _rate = 0.0f;
                if (_rate > 1.0f) _rate = 1.0f;
                Draw->PathArcTo(_cc, _rr, -IM_PI * 0.5f, IM_PI * 1.5f, 40);
                Draw->PathStroke(IM_COL32(0, 0, 0, 150), 0, 2.0f);
                if (_rate > 0.002f) {
                    Draw->PathArcTo(_cc, _rr, -IM_PI * 0.5f,
                                    -IM_PI * 0.5f + 2.0f * IM_PI * _rate, 40);
                    Draw->PathStroke(g_esp.prop_color, 0, 3.0f);
                }
            }
            if (_b[0])
                Draw->AddText(nullptr, g_esp.prop_size,
                              ImVec2(_ix + _px + 4.0f + g_txt_dx, _iy + _px * 0.2f + g_txt_dy),
                              g_esp.prop_color, _b);
            above_y -= (_px + 4.0f);
            ++_dbg.draw_ok;
        }
    }
}

if (show_draw_Line && !copycat_mode){
    ImGui::GetForegroundDrawList()->AddLine({px, 160},{X1 + W/2, Y1},
        g_esp.line_color, g_esp.line_thickness);
}
	               } 
	   // ==================== 宿伞快照 dump ====================
// 触发后：接下来 5 秒内，把 cur==47 期间每一帧的场景对象全 dump 出来，
// 让你看到传伞时新增了哪些对象。
if (g_umbrella_dump_request.exchange(false)) {
    g_umbrella_dump_active = true;
    g_umbrella_dump_start = ImGui::GetTime();
    g_umbrella_dump_frame = 0;
    FILE* f = fopen(GUARD_APP_LOG(), "w");
    if (f) { fprintf(f, "=== 宿伞快照 dump 开始 ===\n"); fclose(f); }
}
if (g_umbrella_dump_active) {
    double now = ImGui::GetTime();
    if (now - g_umbrella_dump_start > 5.0) {
        g_umbrella_dump_active = false;
        FILE* f = fopen(GUARD_APP_LOG(), "a");
        if (f) { fprintf(f, "=== dump 结束 ===\n"); fclose(f); }
    } else {
        int cur = PyAttack::cur_state();
        if (cur == 47) {
            // 只在 cur==47 期间 dump，每帧一次
            FILE* f = fopen(GUARD_APP_LOG(), "a");
            if (f) {
                fprintf(f, "\n--- frame %d t=%.2f ---\n",
                        g_umbrella_dump_frame++, now - g_umbrella_dump_start);
                for (int i = 0; i < data_count; i++) {
                    float xyz[3] = {0, 0, 0};
                    unit_xyz(i, xyz);
                    fprintf(f, "0x%llX [%d] (%.1f,%.1f,%.1f) %s\n",
                            (unsigned long long)data[i].obj,
                            data[i].camp,
                            xyz[0], xyz[1], xyz[2],
                            data[i].class_name);
                }
                fclose(f);
            }
        } else if (cur == 45 || cur == 46 || cur == 1) {
            // 也 dump 一下前后状态作为对照
            FILE* f = fopen(GUARD_APP_LOG(), "a");
            if (f) {
                fprintf(f, "\n--- frame %d cur=%d (对照) ---\n",
                        g_umbrella_dump_frame++, cur);
                fclose(f);
            }
        }
    }
}

// 临时诊断：把监管 unit.__dict__ 里所有可能跟"位置/光标/技能点"沾边的字段列出来


//红夫人镜像                                   
	    if (mirror&&redqueenmod){
            if (getFloat(data[i].obj+ (uint64_t)SecOff::u64(SecOff::DG_72))==450&&data[i].camp==2){
                std::string ss;
                // 镜线在 Draw_Main 开头算好(放下的镜子 / 准备阶段预览镜子二选一)，这里只做一次关于直线的对称
                float orig_x = D.X, orig_y = D.Y;
                calculate_line_reflection(mirror_line_x1, mirror_line_y1, mirror_line_x2, mirror_line_y2, orig_x, orig_y, &D.X, &D.Y);
                /* ★ 2026-10-24: 投影改由 SecMath（security 满档 TU） */
		SecMath::BoxToScreen(matrix, px, py, D.X, D.Y, D.Z, r_x, r_y, r_w, camera);
                dist = sqrt(pow(D.X - Z.X, 2) + pow(D.Y - Z.Y, 2) + pow(D.Z - Z.Z, 2)) / dist_scale;
												
        		W = (r_y - r_w) / 2;	// 宽度
        		H = r_y - r_w;		// 高度
        		X1 = r_x - (r_y - r_w) / 4;	// X1
        		Y1 = r_y - H / 2;	// Y1
        		X2 = X1 + W;		// X2
        		Y2 = Y1 + H;		// Y2
        
                if (W>0){

                    ss += data[i].str;
                    auto textSize = ImGui::CalcTextSize(ss.c_str(), 0, 25);
                    Draw->AddText({X1 + W/2-(textSize.x/2),Y1-45}, BotBoneColor, ss.c_str());
                        
                    if (show_draw_Rect){
    DrawEspRect(ImGui::GetForegroundDrawList(), X1, Y1, X2, Y2, W, H, g_esp.rect_ghost);
}

if (show_draw_Line){
    ImGui::GetForegroundDrawList()->AddLine({px, 160},{X1 + W/2, Y1},
        g_esp.line_color, g_esp.line_thickness);
}
    			        
                    if (show_draw_Distance){
                        std::string mirror_dist_str;
                        mirror_dist_str += std::to_string((int) dist);
                        mirror_dist_str += " m";
                        auto textSize = ImGui::CalcTextSize(mirror_dist_str.c_str(), 0, 25);
                        Draw->AddText({X1 + W/2-(textSize.x/2),Y2+10}, BotBoneColor, mirror_dist_str.c_str());
                    }
                        
                    
                            }//判断W
        }                
    }
}
}   // ← 新增：关闭 if(W>0) 外层
}   // ← 新增：关闭 for (int i = 0; ...)
// 发布版本: 注入功能已停用
    // if (show_sohook)
    //     SoHook::DrawOverlay(Draw, matrix, px, py, 内核人物数量,
    //                         Z.X, Z.Z, Z.Y, 距离比例);

// （2026-10-13：原来这里是「自动震慑可视」——画出刀点/视角点。
//   自动震慑功能已整体删除，这一段一并去掉 ✓）

// ==================== 自动盖板可视 ====================
{
    ImDrawList* gdl = ImGui::GetForegroundDrawList();

    // 触摸点（红圆 + 十字 + 标签）
    if (AutoPallet::show_touch_point) {
        const float tx = AutoPallet::touch_x;
        const float ty = AutoPallet::touch_y;
        const ImU32 c = IM_COL32(255, 60, 60, 230);
        gdl->AddCircle(ImVec2(tx, ty), 22.0f, c, 32, 3.0f);
        gdl->AddLine(ImVec2(tx - 30, ty), ImVec2(tx + 30, ty), c, 3.0f);
        gdl->AddLine(ImVec2(tx, ty - 30), ImVec2(tx, ty + 30), c, 3.0f);
        char buf[64];
        snprintf(buf, sizeof(buf), "盖板触摸 (%.0f,%.0f)", tx, ty);
        gdl->AddText(ImVec2(tx + 28, ty - 8), c, buf);
    }

    // 自动校准的触摸点（青绿圆 + 十字 + 标签）
    if (QteAuto::g_show_point) {
        const float tx = QteAuto::g_x;
        const float ty = QteAuto::g_y;
        const ImU32 c = IM_COL32(60, 230, 180, 230);
        gdl->AddCircle(ImVec2(tx, ty), 22.0f, c, 32, 3.0f);
        gdl->AddLine(ImVec2(tx - 30, ty), ImVec2(tx + 30, ty), c, 3.0f);
        gdl->AddLine(ImVec2(tx, ty - 30), ImVec2(tx, ty + 30), c, 3.0f);
        char buf[64];
        snprintf(buf, sizeof(buf), "校准触摸 (%.0f,%.0f)", tx, ty);
        gdl->AddText(ImVec2(tx + 28, ty - 8), c, buf);
    }

//   2026-10-27：**g_other 与 data[] 必须来自同一局**
//
//   症状（用户现场）：旋转/换局/从后台切回时，**所有人**被画成紫色"平行 Xm"，
//   同时还画出假的 [台球形态]。
//   根因：
//     · g_other 由 other_world_thread 每 500ms 独立刷新（新局）
//     · data[] 由 read_thread 每 2 秒大循环 + 每帧补刷（换局瞬间可能还是旧局/空）
//     · 绘制侧的 `already` 检查依赖 `data[di].obj == g_other[k].scene`，
//       两边不同源时全部 miss → 每个条目都被当成"另一世界的单位"画紫色
//
//   判据（宁可不画，不画错）：
//     ① data_count 必须 > 0
//     ② g_other 必须 1 秒内刷新过（防 B1 的粘性）
//     ③ g_other 里 camp==2 的条目至少要有一个能在 data[] 里找到
//        （求生者两边都有，是"同源"最可靠的证据；监管按设计不去重，不能用来判）
    bool other_world_ok = false;
if (PyGenius::g_other_n > 0 && matrix_usable) {
    // ⚠ prep_now_ms() 是 PyGenius 命名空间的 static inline，必须带前缀
    const int64_t other_age = (PyGenius::other_ms() != 0)
                            ? (PyGenius::prep_now_ms() - PyGenius::other_ms())
                            : 999999;
    int other_sv_n = 0, other_sv_hit = 0;
    for (int k = 0; k < PyGenius::g_other_n; k++) {
        if (PyGenius::g_other[k].camp != 2) continue;
        other_sv_n++;
        const uint64_t sc = PyGenius::g_other[k].scene;
        for (int di = 0; di < data_count; di++)
            if ((uint64_t)data[di].obj == sc) { other_sv_hit++; break; }
    }
    other_world_ok =
        (data_count > 0)
        && (other_age < 1000)
        && (other_sv_n == 0 || other_sv_hit > 0);
}




    //   ⚠ 这里原来挂着一句残留注释「在骑士的框右侧画他头盔里猜的两次…猜的类型 id 映射」，
    //     那个方案**已经不做了**：猜的类型（出刀/技能/什么都不做）**客户端根本收不到**
    //     （三重证据见 skills/knight-prediction/SKILL.md §2），照 id 映射去画等于编数据 ✗
    //     现在的实现：读取器在 PyKnight.h（14101/14102 各带一次选择），显示固定在预知监管下方的 HUD
// ==================== 约瑟夫：另一个世界的单位 ====================
    //   MCP 实测：相中世界不是第二个 world，而是把两套单位放进**同一个**
    //   units_by_type[2]（一局里 8 个求生者 = 现实 4 + 相中 4，坐标几乎重合）。
    //   ESP 的方框来自"当前世界场景图扫描"，所以另一套扫不到。
    //   这里把单位表里的全部求生者过一遍：**凡是不在 data[] 里的，就补画一份**。
    if (other_world_ok) {
        const ImU32 oc = IM_COL32(190, 120, 255, 235);
        ImFont* of = ImGui::GetFont();
        const float osz = 20.0f;
        for (int k = 0; k < PyGenius::g_other_n; k++) {
        //   ★ 同一只监管在 g_other 里会有**两条**（model 和 another_model 各一条），
        //     会画出两个框（实测）。这里按位置去重：3 米内只画第一条。
        static bool  s_c1_drawn = false;
        static float s_c1_x = 0, s_c1_z = 0;
        if (k == 0) s_c1_drawn = false;
            const uint64_t sc = PyGenius::g_other[k].scene;

            //   ★ 监管（camp1）**不做任何去重**：
            //     · 人形态时它的 scene 也在 data[] 里（会被 already 干掉 → 人形态没框，用户实测）
            //     · 球形态时坐标对的只有单位表这条
            //     所以 camp1 一律在这里画；camp2 才做 scene/近邻去重。
            bool already = false;                 // ESP 已经画过这个实体？
            for (int di = 0; di < data_count; di++) {
                if ((uint64_t)data[di].obj == sc) { already = true; break; }
            }
            if (already && PyGenius::g_other[k].camp != 1) continue;
            //   ★★ 2026-10-21：这条路升级成"真框"★★
            //     台球手变球后，球的场景对象（unit.model+0x20）**根本不在 data[] 里**
            //     （实测：单位表 scene=0x79BDC8B700，data[] 里台球手是 0x79BDC96560），
            //     所以主循环永远画不到球。而这里读的是**单位表的 Python position**，
            //     位置是准的（跟球的"平行 Xm"就是证据）。
            //     所以：玩家单位（camp 1/2）在这里按阵营色画一个完整框，只跳过"canvas 里已有同位置框"的情况。
            {
                const float gx = PyGenius::g_other[k].x;
                const float gz = PyGenius::g_other[k].z;
                const int   gc = PyGenius::g_other[k].camp;
                bool near_dup = false;
                for (int di = 0; di < data_count; di++) {
                    if (data[di].camp != gc) continue;
                    if (data[di].fb_ok) continue;   // ← 坐标是兜底的 = 那条本身就没画出来，不算重复
                    float p[3];
                    unit_xyz(di, p);
                    const float ddx = p[0] - gx, ddz = p[2] - gz;
                    if (ddx * ddx + ddz * ddz < 1.0f) { near_dup = true; break; }
                }
                if (PyGenius::g_other[k].camp != 1 && near_dup) continue;   // 监管不去重
            }
            float BX = PyGenius::g_other[k].x;
            float BY = PyGenius::g_other[k].y;          // 人形态 = 脚底(0.4)，球形态 = 球心(10.1)
            float BZ = PyGenius::g_other[k].z;
            const int   gcamp = PyGenius::g_other[k].camp;
            if (fabsf(BX) < 1.0f && fabsf(BZ) < 1.0f) continue;   // 坐标无效（实测有个 (0,0) 的残留条目 -> 画出 0m 空框）
            //   ★ 这条只负责**球形态**：人形态交给主循环（那边名字/距离/天赋都是现成的）
            //   ★ 监管球形态：位置改用 refresh_hunter_pos()（16ms/60Hz），
            //     不再依赖 g_other（那是 100ms 的全量枚举，看着卡）。
            if (gcamp == 1) {
                //   ★ 球形态才画：监管当前 model 的场景**不在** data[] 里
                const uint64_t hs = PyGenius::hunter_scene();
                if (hs == 0) continue;
                bool in_data = false;
                for (int di = 0; di < data_count; di++)
                    if ((uint64_t)data[di].obj == hs) { in_data = true; break; }
                if (in_data) continue;                 // 人形态 -> 主循环负责
                float hx = 0, hy = 0, hz = 0;
                if (!PyGenius::hunter_pos(hx, hy, hz)) continue;
                BX = hx; BY = hy; BZ = hz;
            }
            if (gcamp == 1) {
                if (s_c1_drawn) {
                    const float ddx2 = BX - s_c1_x, ddz2 = BZ - s_c1_z;
                    if (ddx2 * ddx2 + ddz2 * ddz2 < 9.0f) continue;   // 同一个人的另一条模型
                }
                s_c1_drawn = true; s_c1_x = BX; s_c1_z = BZ;
            }
            {
                // 框：脚底 BY -> 头顶 BY+20（和队友框同一套换算）
                //   ★ 高度模型（实测）：人形态 position.y=0.4 是**脚底**；
                //     球形态 position.y=10.1 是**球心**。所以：
                //       y < 3  -> 脚底，框 = [y, y+20]
                //       y >= 3 -> 球心，框 = [y-10, y+10]
                const bool ball_form = (gcamp == 1) && (BY >= 3.0f);
                const float y_lo = ball_form ? (BY - 10.0f) : BY;
                const float y_hi = ball_form ? (BY + 10.0f) : (BY + 20.0f);
                float cam_t = matrix[3]*BX + matrix[7]*y_hi + matrix[11]*BZ + matrix[15];
                if (cam_t <= 0.01f) continue;
                const float sx_t = px + (matrix[0]*BX + matrix[4]*y_hi + matrix[8]*BZ + matrix[12]) / cam_t * px;
                const float sy_t = py - (matrix[1]*BX + matrix[5]*y_hi + matrix[9]*BZ + matrix[13]) / cam_t * py;
                const float sx_b = px + (matrix[0]*BX + matrix[4]*y_lo + matrix[8]*BZ + matrix[12]) / cam_t * px;
                const float sy_b = py - (matrix[1]*BX + matrix[5]*y_lo + matrix[9]*BZ + matrix[13]) / cam_t * py;
                const float H2 = sy_b - sy_t;
                if ((gcamp == 1 || gcamp == 2) && H2 > 4.0f && H2 < 4000.0f) {
                    const float W2 = H2 * 0.5f;
                    const ImU32 bc = (gcamp == 1) ? g_esp.rect_hunter : g_esp.rect_survivor;
                    //   ★ 开关和样式全部跟随正常绘制：框 / 名字 / 距离 各自独立
                        DrawEspRect(gdl, sx_b - W2 * 0.5f, sy_t, sx_b + W2 * 0.5f, sy_b, W2, H2, bc);
                    if (gcamp == 1) {
                        //   ★ 全局 Z 是**换过轴**的：Z.X=世界X、Z.Z=高度、Z.Y=水平Z
                        //     （BY 是高度、BZ 是水平 Z，必须分别对 Z.Z / Z.Y，写错就是 3m 变 42m）
                        const float dm = sqrtf((BX - Z.X)*(BX - Z.X) +
                                               (BY - Z.Z)*(BY - Z.Z) +
                                               (BZ - Z.Y)*(BZ - Z.Y)) / dist_scale;
                        ImFont* lf = ImGui::GetFont();
                        float lab_y = sy_t - g_esp.name_lift;
                        if (show_draw_Distance) {
                            char db[32];
                            snprintf(db, sizeof(db), "%.0fm", dm);
                            const ImVec2 dt = lf->CalcTextSizeA(g_esp.dist_size, FLT_MAX, 0.0f, db);
                            gdl->AddText(lf, g_esp.dist_size,
                                         ImVec2(sx_b - dt.x * 0.5f, lab_y - dt.y),
                                         g_esp.dist_color, db);
                            lab_y -= (dt.y + 2.0f);
                        }
                        if (show_draw_Name) {
                            const char* nm2 = "[台球形态]";
                            const ImVec2 nt = lf->CalcTextSizeA(g_esp.name_size, FLT_MAX, 0.0f, nm2);
                            gdl->AddText(lf, g_esp.name_size,
                                         ImVec2(sx_b - nt.x * 0.5f, lab_y - nt.y),
                                         g_esp.name_color, nm2);
                        }
                    }
                }
            }
            const float cam = matrix[3]*BX + matrix[7]*BY + matrix[11]*BZ + matrix[15];
            if (cam <= 0.001f) continue;
            const float sx = px + (matrix[0]*BX + matrix[4]*BZ + matrix[8]*BY + matrix[12]) / cam * px;
            const float sy = py - (matrix[1]*BX + matrix[5]*BZ + matrix[9]*BY + matrix[13]) / cam * py;
            if (sx < 0 || sy < 0 || sx > displayInfo.width || sy > displayInfo.height) continue;

            const float dx = BX - g_self_x, dz = BZ - g_self_z;
            const float dist = sqrtf(dx*dx + dz*dz) / dist_scale;
            char ob[64];
            snprintf(ob, sizeof(ob), "平行 %.0fm", dist);
            const ImVec2 ts = of->CalcTextSizeA(osz, FLT_MAX, 0.0f, ob);
            //   ★ 用**框的几何**定位（框底中点 + 框高），不要再独立投影一次 ——
            //     之前用 sy（单位原点）定位，和框用的 cam_t 不是同一个相机量，标签会跑到框外。
            float cam_l = matrix[3]*BX + matrix[7]*BY + matrix[11]*BZ + matrix[15];
            float lx = sx, ly = sy;
            {
                const float head_off = 20.0f;
                float ct = matrix[3]*BX + matrix[7]*(BY + head_off) + matrix[11]*BZ + matrix[15];
                if (ct > 0.01f) {
                    const float bxp = px + (matrix[0]*BX + matrix[4]*BY + matrix[8]*BZ + matrix[12]) / ct * px;
                    const float byp = py - (matrix[1]*BX + matrix[5]*(BY + head_off) + matrix[9]*BZ + matrix[13]) / ct * py;
                    lx = bxp;
                    ly = byp - ts.y - g_esp.name_lift;    // 贴着框顶上方
                }
            }
            (void)cam_l;
                    gdl->AddText(of, osz, ImVec2(lx - ts.x * 0.5f, ly), oc, ob);
    }
}
// 判定范围（矩形四角投影）
if (AutoPallet::show_range && matrix_usable) {
        AutoPallet::RangeRect rects[16];
        const int rn = AutoPallet::ranges(rects, 16);
        for (int ri = 0; ri < rn; ri++) {
            const AutoPallet::RangeRect &r = rects[ri];

            // r.cx = X, r.cy = 水平 Y, r.cz = 高度 Z
            auto proj = [&](float wx, float wy, float wz,
                            float &ox, float &oy) -> bool {
                float cam2 = matrix[3]*wx + matrix[7]*wz + matrix[11]*wy + matrix[15];
                if (cam2 <= 0.01f) return false;
                ox = px + (matrix[0]*wx + matrix[4]*wz + matrix[8]*wy + matrix[12]) / cam2 * px;
                oy = py - (matrix[1]*wx + matrix[5]*(wz+8.5f) + matrix[9]*wy + matrix[13]) / cam2 * py;
                return true;
            };

            const float dx = r.ux, dy = r.uy;
            const float pxd = -dy, pyd = dx;

            float c1x, c1y, c2x, c2y, c3x, c3y, c4x, c4y;
            bool ok = true;
            ok &= proj(r.cx + dx*r.a + pxd*r.b, r.cy + dy*r.a + pyd*r.b, r.cz, c1x, c1y);
            ok &= proj(r.cx + dx*r.a - pxd*r.b, r.cy + dy*r.a - pyd*r.b, r.cz, c2x, c2y);
            ok &= proj(r.cx - dx*r.a - pxd*r.b, r.cy - dy*r.a - pyd*r.b, r.cz, c3x, c3y);
            ok &= proj(r.cx - dx*r.a + pxd*r.b, r.cy - dy*r.a + pyd*r.b, r.cz, c4x, c4y);
            if (!ok) continue;

            const ImU32 col = r.candidate
                ? IM_COL32(255, 0, 0, 220)
                : IM_COL32(255, 200, 0, 180);
            const ImU32 fill = r.candidate
                ? IM_COL32(255, 0, 0, 50)
                : IM_COL32(255, 200, 0, 30);
            gdl->AddQuadFilled(ImVec2(c1x,c1y), ImVec2(c2x,c2y),
                               ImVec2(c3x,c3y), ImVec2(c4x,c4y), fill);
            gdl->AddQuad(ImVec2(c1x,c1y), ImVec2(c2x,c2y),
                         ImVec2(c3x,c3y), ImVec2(c4x,c4y), col, 2.0f);
        }
    }
}
// ==================== 摸金队友 ESP ====================
if (PyMiniMap::in_goldrush() && matrix_usable) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    int raw_n = PyMiniMap::team_count();

    // ========== 1. 找自身 uid ==========
// ★ 用 PySelf::self_uid() —— PySelf 状态栏显示"正常 从属uid=1000004"时就能直接拿到。
//   手动模式下手选编号优先。
int64_t self_uid = 0;
if (g_minimap_auto_self) {
    self_uid = PySelf::self_uid();
    if (self_uid < 1000001 || self_uid > 1000100) self_uid = 0;
} else if (g_minimap_self_num >= 1 && g_minimap_self_num <= 99) {
    self_uid = 1000000 + g_minimap_self_num;
}

    // ========== 2. 找自身位置（用于队友距离）==========
    float self_x = 0, self_y = 0, self_z = 0;
    bool has_self_pos = false;
    if (self_uid > 0) {
        for (int i = 0; i < raw_n; i++) {
            PyMiniMap::TeamMember tm;
            if (!PyMiniMap::get_member(i, tm) || !tm.valid) continue;
            if (tm.uid == self_uid) {
                self_x = tm.x; self_y = tm.y; self_z = tm.z;
                has_self_pos = true;
                break;
            }
        }
    }

    // ========== 3. 逐队友画框 ==========
    for (int ti = 0; ti < raw_n; ti++) {
        PyMiniMap::TeamMember tm;
        if (!PyMiniMap::get_member(ti, tm) || !tm.valid) continue;
        if (self_uid > 0 && tm.uid == self_uid) continue;   // 跳过自己

        // 世界坐标（Python position 顺序：X, Y=高度/脚底, Z=水平）
        float X   = tm.x;
        float Yh  = tm.y;   // 脚底
        float Zh  = tm.z;

        // 投影（跟主循环 matrix 布局一致）
        float cam = matrix[3]*X + matrix[7]*Yh + matrix[11]*Zh + matrix[15];
        if (cam <= 0.01f) continue;

        float sx = px + (matrix[0]*X + matrix[4]*Yh + matrix[8]*Zh + matrix[12]) / cam * px;

        // ★ 框高：position.Y 是脚底，用 0~20（≈1.7m 身高）
        const float foot_off =  0.0f;
        const float head_off = 20.0f;
        float sy_top    = py - (matrix[1]*X + matrix[5]*(Yh + head_off) + matrix[9]*Zh + matrix[13]) / cam * py;
        float sy_bottom = py - (matrix[1]*X + matrix[5]*(Yh + foot_off) + matrix[9]*Zh + matrix[13]) / cam * py;
        float H = sy_bottom - sy_top;
        if (H < 4.0f || H > 4000.0f) continue;

        float W  = H * 0.5f;
        float X1 = sx - W * 0.5f;
        float Y1 = sy_top;
        float X2 = X1 + W;
        float Y2 = Y1 + H;

        // ★ 距离 = 队友到"自己"（同一坐标系，永远准确）
        float dm = 0.f;
        if (has_self_pos) {
            float ddx = tm.x - self_x;
            float ddy = tm.y - self_y;
            float ddz = tm.z - self_z;
            dm = sqrtf(ddx*ddx + ddy*ddy + ddz*ddz) / dist_scale;
        } else {
            // 没找到自身位置：用主循环 Z 兜底
            float ddx = tm.x - Z.X;
            float ddy = tm.y - Z.Z;
            float ddz = tm.z - Z.Y;
            dm = sqrtf(ddx*ddx + ddy*ddy + ddz*ddz) / dist_scale;
        }
        // ★ 不再按距离过滤（准备阶段距离可能异常，也要画）

        // 编号配色
        int64_t num = tm.uid - 1000000;
        ImU32 col;
        switch (num) {
            case 1: col = IM_COL32(255, 220,   0, 255); break;
            case 2: col = IM_COL32( 60, 150, 255, 255); break;
            case 3: col = IM_COL32( 60, 220,  60, 255); break;
            case 4: col = IM_COL32(255,  60,  60, 255); break;
            default: col = IM_COL32(200, 200, 200, 255); break;
        }

        // 方框
        dl->AddRect(ImVec2(X1, Y1), ImVec2(X2, Y2), col, 3.0f, 0, 2.0f);

        // 编号
        char lbl[64];
        if (num >= 1 && num <= 99) snprintf(lbl, sizeof(lbl), "%lld 号", (long long)num);
        else                        snprintf(lbl, sizeof(lbl), "队友");

        ImFont* font = ImGui::GetFont();
        float ns = g_esp.name_size;
        ImVec2 ts = font->CalcTextSizeA(ns, FLT_MAX, 0.0f, lbl);
        dl->AddText(font, ns, ImVec2(X1 + W*0.5f - ts.x*0.5f, Y1 - 45.0f), col, lbl);

        // 距离
        char dbuf[32];
        snprintf(dbuf, sizeof(dbuf), "%.0f m", dm);
        ImVec2 ds = ImGui::CalcTextSize(dbuf, 0, 25);
        dl->AddText(ImVec2(X1 + W*0.5f - ds.x*0.5f, Y2 + 10.0f),
                    IM_COL32(255, 200, 0, 255), dbuf);
    }
}

// ==================== 小地图 ====================
    Lic::SetDrawPhase(2);       // 实体循环结束
g_drw_d2 = drw_cpu_ns();   // 实体循环结束（小地图之前）
    {
        // ★★ 直接量这两个调用本身的 CPU（2026-10-10）
        //   前面用"括号配对/锚点"定位段落失败了好几轮，这里最直接：
        //   在调用前后各取一次线程 CPU 时间戳，差值就是这一次调用的真实开销。
        //   目的：确认 6.5ms 的"循环后"到底是不是小地图/身份窗吃掉的。
        long long _tm = drw_cpu_ns();
        DrawMiniMap(ImGui::GetForegroundDrawList());
        g_drw_acc_minimap += drw_cpu_ns() - _tm;
    }
    {
        long long _tc = drw_cpu_ns();
        DrawCopycatWindow(ImGui::GetForegroundDrawList());
        g_drw_acc_ccwin += drw_cpu_ns() - _tc;
    }


}
        


// ==================== 每个监管的出刀延迟 ====================
// 目的：不同监管出刀速度不一样，需要在"检测到出刀"和"点技能"之间插入可调延迟。
// 存储：按 Name.h 里 boss_table 的中文名（去方括号）建索引，每个一个 atomic<int>，
//       UI 线程写、PyAttack 回调线程读，无锁。
// 配置文件：/sdcard/dwrgcfg.txt，每行 "名字 毫秒"，0 或不写 = 不延迟。
static const int MAX_BOSSES = 64;
static std::atomic<int> g_boss_windup_normal[MAX_BOSSES];    // 普通刀前摇(ms)
static std::atomic<int> g_boss_windup_charge[MAX_BOSSES];    // 蓄力刀前摇(ms)
static int              g_skill_lead_time_ms = 200;           // 提前量(ms)：前摇结束前多久按
// 兜底前摇(ms)。当监管不在 /sdcard/dwrgcfg.txt 里时用它代替 0。
//
// ⚠ 为什么要这个兜底：原来查不到前摇时 windup 保持 0，
//   于是 delta = 0 - 提前量 -> 被夹成 0 -> elapsed>=0 恒成立 ->
//   **出刀瞬间立刻按，提前量设多少都没用**。
//   表现就是"不管怎么调都按得太早"。给个合理默认值，至少时序是可控的。
static int              g_skill_windup_fallback_ms = 400;
static std::string      g_skill_windup_warned_for;   // 同一位监管只提醒一次
static char             g_boss_names[MAX_BOSSES][24];
static int              g_boss_n = 0;
static std::unordered_map<std::string, int> g_boss_name_idx;
static std::mutex g_hunter_name_mtx;
static std::string g_cached_hunter_name;

// ==================== 监管出刀前摇（内置，单位：毫秒）====================
// 格式：{ 显示名, 普通刀前摇ms, 蓄力刀前摇ms }
// 名字跟 Name.h 里 getboss() 返回的一致（去掉方括号）
struct BossWindupEntry {
    const char* name;
    int normal_ms;
    int charge_ms;
};

static const BossWindupEntry g_boss_windup_table[] = {
    // 监管者
    { "约瑟夫",     420,  980 },
    { "红蝶",       430,  820 },
    { "小丑",       250,  990 },
    { "红夫人",     590, 1420 },
    { "鹿头",       300, 1700 },
    { "杰克",       470, 1270 },
    { "蜘蛛",       370,  780 },
    { "孽蜥",       450, 1620 },
    { "黄衣之主",   480, 1090 },
    { "疯眼",       470, 1110 },
    { "信徒",       400,  730 },
    { "梦之女巫",   400,  730 },
    { "爱哭鬼",     370, 1080 },
    { "26号守卫",   380, 1640 },
    { "噩梦",       360, 1480 },
    { "使徒",       570, 1410 },
    { "小提琴家",   410, 1210 },
    { "雕刻家",     550, 1620 },
    { "破轮",       430, 1540 },
    { "博士",       760, 1940 },
    { "渔女",       320, 1800 },
    { "蜡像师",     500, 1200 },
    { "记录员",     410, 1100 },
    { "守夜人",     370,  860 },
    { "隐士",       400, 1150 },
    { "歌剧演员",   310,  850 },
    { "愚人金",     390,  960 },
    { "跛脚羊",     380, 1010 },
    { "喧嚣",       430,  970 },
    { "杂货商",     350, 1000 },
    { "台球手",     350, 1100 },
    { "女王蜂",     380, 1190 },
    { "心兽",       650, 1600 },
    { "牙医",       370, 1100 },
    { "白无常",     700, 1290 },
    { "黑无常",     320,  880 },
    { "厂长",       350, 2060 },
    { "时空之影",   350, 1160 },
};


// 读场景对象的类名（跟读取循环里那 5 跳指针链一模一样）
static std::string read_scene_class_name(uint64_t obj) {
    if (!obj) return "";
    uint64_t chain_head = getPtr64(obj + (uint64_t)SecOff::u64(SecOff::DG_73));
    if (!chain_head) return "";
    uint64_t class_name_obj = getPtr64(getPtr64(getPtr64(getPtr64(chain_head) + (uint64_t)SecOff::u64(SecOff::DG_74)) + (uint64_t)SecOff::u64(SecOff::DG_75)) + (uint64_t)SecOff::u64(SecOff::DG_76));
    if (!class_name_obj) return "";
    int len = getDword(class_name_obj + (uint64_t)SecOff::u64(SecOff::DG_77));
    if (len <= 0 || len >= 256) return "";
    uint64_t str_ptr = getPtr64(class_name_obj + (uint64_t)SecOff::u64(SecOff::DG_78));
    if (!str_ptr) return "";
    std::string s;
    s.resize(len);
    if (!vm_readv(str_ptr, &s[0], len)) return "";
    return s;
}

// 当前局内监管的中文名（去方括号），"" = 未识别/不在局内
static std::string current_hunter_name() {
    uint64_t hs = 0;
    if (!PySelf::hunter_body(hs)) return "";
    std::string cls = read_scene_class_name(hs);
    if (cls.empty()) return "";
    const char* zh = getboss(cls.c_str());
    if (!zh || !zh[0]) return "";
    if (strcmp(zh, cls.c_str()) == 0) return "";   // getboss 未知时返回原类名
    std::string s = zh;
    if (s.size() >= 2 && s.front() == '[' && s.back() == ']')
        s = s.substr(1, s.size() - 2);
    return s;
}

// 当前操控单位的中文名（求/监均可，""=读不到）
static std::string current_self_name() {
    uint64_t s = 0;
    if (!PySelf::anchor(s)) return "";
    std::string cls = AutoSkill::read_scene_class_name(s);
    if (cls.empty()) return "";
    int camp = PySelf::camp();
    const char* zh = nullptr;
    if (camp == 1)      zh = getboss(cls.c_str());
    else if (camp == 2) zh = getplayer(cls.c_str());
    if (!zh || !zh[0]) return "";
    if (strcmp(zh, cls.c_str()) == 0) return "";   // 未命中返回原类名
    std::string r = zh;
    if (r.size() >= 2 && r.front() == '[' && r.back() == ']')
        r = r.substr(1, r.size() - 2);
    return r;
}


// 内存占用（KB）。原来只读 statm 的 RSS，漏了两大块：换进 zram 的页、图形缓冲。
// 2026-09-24 实测 RSS 22MB，而实际约 216MB：交换链 4 张 3200x3200 占 157MB（dmabuf）、
// GPU 私有显存 20MB（字体纹理 16MB）、换出 33MB，RSS 里 18MB 还是共享库。
struct MemUsage {
    size_t pss_kb = 0;      // 进程自身常驻（共享页按进程数分摊）
    size_t swap_kb = 0;     // 已换进 zram
    size_t gpu_kb = 0;      // GPU 私有显存（仅 kgsl 能读到）
    size_t buffer_kb = 0;   // 画面缓冲（dmabuf，交换链图像）
    bool gpu_ok = false;
    size_t total_kb() const { return pss_kb + swap_kb + gpu_kb + buffer_kb; }
};

static size_t read_size_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    unsigned long long v = 0;
    if (fscanf(f, "%llu", &v) != 1) v = 0;
    fclose(f);
    return (size_t)v;
}

static MemUsage read_memory_usage() {
    MemUsage m;
    if (FILE *f = fopen("/proc/self/smaps_rollup", "r")) {
        char line[128];
        size_t v;
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "Pss: %zu kB", &v) == 1) m.pss_kb = v;
            else if (sscanf(line, "SwapPss: %zu kB", &v) == 1) m.swap_kb = v;
        }
        fclose(f);
    }

    // 高通：kgsl 按进程统计，kernel = GPU 私有、imported_mem = 导入的 dmabuf，单位字节
    char path[96];
    snprintf(path, sizeof(path), "/sys/class/kgsl/kgsl/proc/%d/kernel", getpid());
    if (access(path, R_OK) == 0) {
        m.gpu_ok = true;
        m.gpu_kb = read_size_file(path) / 1024;
        snprintf(path, sizeof(path), "/sys/class/kgsl/kgsl/proc/%d/imported_mem", getpid());
        m.buffer_kb = read_size_file(path) / 1024;
        return m;
    }

    // 非高通：只能从 fdinfo 统计持有的 dmabuf，GPU 私有显存读不到
    if (DIR *dir = opendir("/proc/self/fd")) {
        while (dirent *e = readdir(dir)) {
            if (e->d_name[0] == '.') continue;
            char link[128], target[128];
            snprintf(link, sizeof(link), "/proc/self/fd/%s", e->d_name);
            ssize_t n = readlink(link, target, sizeof(target) - 1);
            if (n <= 0) continue;
            target[n] = 0;
            if (!strstr(target, "dmabuf")) continue;
            snprintf(link, sizeof(link), "/proc/self/fdinfo/%s", e->d_name);
            if (FILE *f = fopen(link, "r")) {
                char line[128];
                size_t v;
                while (fgets(line, sizeof(line), f))
                    if (sscanf(line, "size: %zu", &v) == 1) { m.buffer_kb += v / 1024; break; }
                fclose(f);
            }
        }
        closedir(dir);
    }
    return m;
}

int GetInputDeviceCount() {
    DIR *dir = opendir("/dev/input/");
    if (!dir) return -1;
    dirent *ptr = NULL;
    int count = 0;
    while ((ptr = readdir(dir)) != NULL) {
        if (strstr(ptr->d_name, "event"))
            count++;
    }
    closedir(dir);
    return count ? count : -1;
}


// 读触摸屏设备：遍历 /dev/input/event*，找 EV_ABS + ABS_MT_POSITION_X 的设备
static int find_touch_device() {
    for (int i = 0; i < 32; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        // 读 capabilities 判断是不是触摸屏
        unsigned long absbit[ABS_CNT / (sizeof(unsigned long)*8) + 1] = {0};
        if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbit)), absbit) >= 0) {
            int has_x = (absbit[ABS_MT_POSITION_X / (sizeof(unsigned long)*8)] 
                         >> (ABS_MT_POSITION_X % (sizeof(unsigned long)*8))) & 1;
            int has_y = (absbit[ABS_MT_POSITION_Y / (sizeof(unsigned long)*8)] 
                         >> (ABS_MT_POSITION_Y % (sizeof(unsigned long)*8))) & 1;
            if (has_x && has_y) {
                printf("[触发] 触摸屏: %s\n", path);
                return fd;
            }
        }
        close(fd);
    }
    return -1;
}

void VolumeKeyHide() {
    int EventCount = GetInputDeviceCount();
    //   ★ 2026-10-24 诊断：音量键读不到时，先看这里
    printf("[音量] /dev/input 设备数=%d\n", EventCount); fflush(stdout);
    if (EventCount <= 0) { printf("[音量] 打不开 /dev/input 目录 -> 音量键失效\n"); fflush(stdout); return; }

    int *fdArray = (int *)malloc(EventCount * sizeof(int));
    if (!fdArray) return;

    int opened = 0;
    for (int i = 0; i < EventCount; i++) {
        char temp[128];
        sprintf(temp, "/dev/input/event%d", i);
        fdArray[i] = open(temp, O_RDONLY | O_NONBLOCK);   // 只读按键，不需要写权限
        if (fdArray[i] >= 0) ++opened;
        else printf("[音量] 打开 %s 失败 errno=%d(%s)\n", temp, errno, strerror(errno));
    }
    printf("[音量] 成功打开 %d/%d 个输入设备，开始监听音量键\n", opened, EventCount); fflush(stdout);

    input_event ev;
    while (1) {
        for (int i = 0; i < EventCount; i++) {
            if (fdArray[i] < 0) continue;
            memset(&ev, 0, sizeof(ev));
            while (read(fdArray[i], &ev, sizeof(ev)) == sizeof(ev)) {
                if (ev.type == EV_KEY && ev.code == KEY_VOLUMEDOWN && ev.value == 1)
                    voice = false;
                if (ev.type == EV_KEY && ev.code == KEY_VOLUMEUP && ev.value == 1)
                    voice = true;
            }
        }
        show_window = voice;
        usleep(10000);
    }
    free(fdArray);
}

// ==================== 配置保存/加载 ====================
// ⚠ 路径用 OBF 加密存 —— 明文的话一句 strings 就能捞出来。
//   OBF 是运行期解密，所以这里不能再是 const char*，改成每次解到缓冲。
//
// ☠☠ 2026-10-13 改：原来存 /data/local/tmp/cg.config ✗
//   /data/local/tmp 是 **771（人人可进）**，文件是 644 ——
//   任何拿到 shell 的用户（uid 2000，**不需要 root**）都能读 ✗
//   配置里有**全部功能开关和参数**（jp.* / jcam.* / esp.* / aim.* …），
//   等于把你的功能清单免费交出去 ✗ 而且 644 之外还可能被**改写** ✗
//   改到 /data/adb/.d/（700 root）+ 写后 chmod 600 ✓
//   ⚠ OBF 加密仍然保留：它防的是"二进制里的明文路径"，
//     而权限防的是"文件系统上的可读性" —— **两者防的不是一回事**，
//     都要有 ✓（别以为加密了就不用管权限 ✗）
//   ☠☠ 2026-10-13 发布闸门抓到：`OBF("/data/adb/.d/c.bin")` 的**明文仍在二进制里** ✗
//     根因：`OBF` 是**运行期**解密，但它的**常量本身是明文** ——
//     和 `GUARD_STR` 第一版踩的是同一个坑 ✗（编译器把路径原样放进 .rodata）
//     => 所以这里不能再靠 OBF，改成 GUARD_STR ✓
//   ★ 只加密文件名、不加密目录名等于没加密 ✗
//     所以 `mkdir` 的目录名也必须走 GUARD_STR ✓
static std::string ConfigPath() {
    return std::string(GUARD_STR("/data/adb/.d/c.bin"));
}
static void GetConfigPath(char* out, size_t n) {
    const std::string p = ConfigPath();
    snprintf(out, n, "%s", p.c_str());
    const std::string d = GUARD_STR("/data/adb/.d");
    mkdir(d.c_str(), 0700);        // 确保私有目录存在 ✓
}

struct CfgKV {
    char key[64];
    char val[128];
    bool used;
};
// ⚠ 表容量。原来是 128，被加满之后 CfgPut 会**静默丢弃**后面的键 ——
//   表现是「某个配置项明明写了 CfgPut，读回来却不存在」，
//   而且丢的是排在 SaveConfig 后半段的那些（ui.show 就是这么没的）。
//   加容量 + 满了要吵，别再让它悄悄吃掉配置。
static constexpr int kCfgMax = 384;
static CfgKV g_cfg[kCfgMax];
static int   g_cfg_n = 0;
static bool  g_cfg_full_warned = false;

static void CfgPut(const char* k, int v) {
    for (int i = 0; i < g_cfg_n; i++) if (strcmp(g_cfg[i].key, k) == 0) {
        snprintf(g_cfg[i].val, sizeof(g_cfg[i].val), "%d", v); return;
    }
    if (g_cfg_n >= kCfgMax) {
        if (!g_cfg_full_warned) {
            g_cfg_full_warned = true;
            printf("[配置] ⚠ 表已满(%d)，键被丢弃 —— 请调大 kCfgMax\n", kCfgMax);
            fflush(stdout);
        }
        return;
    }
    snprintf(g_cfg[g_cfg_n].key, sizeof(g_cfg[g_cfg_n].key), "%s", k);
    snprintf(g_cfg[g_cfg_n].val, sizeof(g_cfg[g_cfg_n].val), "%d", v);
    g_cfg_n++;
}
static void CfgPut(const char* k, float v) {
    for (int i = 0; i < g_cfg_n; i++) if (strcmp(g_cfg[i].key, k) == 0) {
        snprintf(g_cfg[i].val, sizeof(g_cfg[i].val), "%.4f", v); return;
    }
    if (g_cfg_n >= kCfgMax) {
        if (!g_cfg_full_warned) {
            g_cfg_full_warned = true;
            printf("[配置] ⚠ 表已满(%d)，键被丢弃 —— 请调大 kCfgMax\n", kCfgMax);
            fflush(stdout);
        }
        return;
    }
    snprintf(g_cfg[g_cfg_n].key, sizeof(g_cfg[g_cfg_n].key), "%s", k);
    snprintf(g_cfg[g_cfg_n].val, sizeof(g_cfg[g_cfg_n].val), "%.4f", v);
    g_cfg_n++;
}
static bool CfgGet(int idx, const char* k, int& out) {
    if (strcmp(g_cfg[idx].key, k) != 0) return false;
    /*   2026-10-26：**颜色是 %08X 存的，不能用 atoi 读**
     *   实测 esp.bones_hunter 存成 "FFFFFFFF"，atoi 解析成 0 —— 于是重进就变黑/变默认。
     *   这里先按十进制试，读不完再按十六进制读。
     */
    const char* v = g_cfg[idx].val;
    char* end = nullptr;
    long d = strtol(v, &end, 10);
    if (end && *end == 0) { out = (int)d; return true; }
    out = (int)strtoul(v, nullptr, 16);
    return true;
}
static bool CfgGet(int idx, const char* k, float& out) {
    if (strcmp(g_cfg[idx].key, k) != 0) return false;
    out = (float)atof(g_cfg[idx].val); return true;
}

// 字符串版（原来没有，只能存数字/颜色）
static void CfgPut(const char* k, const char* v) {
    if (!v) v = "";
    for (int i = 0; i < g_cfg_n; i++) if (strcmp(g_cfg[i].key, k) == 0) {
        snprintf(g_cfg[i].val, sizeof(g_cfg[i].val), "%s", v); return;
    }
    if (g_cfg_n >= kCfgMax) {
        if (!g_cfg_full_warned) {
            g_cfg_full_warned = true;
            printf("[配置] ⚠ 表已满(%d)，键被丢弃 —— 请调大 kCfgMax\n", kCfgMax);
            fflush(stdout);
        }
        return;
    }
    snprintf(g_cfg[g_cfg_n].key, sizeof(g_cfg[g_cfg_n].key), "%s", k);
    snprintf(g_cfg[g_cfg_n].val, sizeof(g_cfg[g_cfg_n].val), "%s", v);
    g_cfg_n++;
}

static void CfgPut(const char* k, ImU32 v) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%08X", (unsigned)v);
    for (int i = 0; i < g_cfg_n; i++) if (strcmp(g_cfg[i].key, k) == 0) {
        snprintf(g_cfg[i].val, sizeof(g_cfg[i].val), "%s", buf); return;
    }
    if (g_cfg_n >= kCfgMax) {
        if (!g_cfg_full_warned) {
            g_cfg_full_warned = true;
            printf("[配置] ⚠ 表已满(%d)，键被丢弃 —— 请调大 kCfgMax\n", kCfgMax);
            fflush(stdout);
        }
        return;
    }
    snprintf(g_cfg[g_cfg_n].key, sizeof(g_cfg[g_cfg_n].key), "%s", k);
    snprintf(g_cfg[g_cfg_n].val, sizeof(g_cfg[g_cfg_n].val), "%s", buf);
    g_cfg_n++;
}
static bool CfgGet(int idx, const char* k, ImU32& out) {
    if (strcmp(g_cfg[idx].key, k) != 0) return false;
    unsigned int v = 0;
    if (sscanf(g_cfg[idx].val, "%x", &v) != 1) return false;
    out = (ImU32)v;
    return true;
}

static void SaveConfig() {
    g_cfg_n = 0;
        // ★ 小丑零件（11 项）
    CfgPut("jp.show",      JokerParts::g_show ? 1 : 0);
    CfgPut("jp.range_m",   (int)JokerParts::g_range_m);
    CfgPut("jp.font_size", (int)JokerParts::g_font_size);
    CfgPut("jp.name",      JokerParts::g_show_name ? 1 : 0);
    CfgPut("jp.dist",      JokerParts::g_show_dist ? 1 : 0);
    CfgPut("jp.e0",        JokerParts::g_enable[0] ? 1 : 0);
    CfgPut("jp.e1",        JokerParts::g_enable[1] ? 1 : 0);
    CfgPut("jp.e2",        JokerParts::g_enable[2] ? 1 : 0);
    CfgPut("jp.c0",        (int)JokerParts::g_col[0]);
    CfgPut("jp.c1",        (int)JokerParts::g_col[1]);
    CfgPut("jp.c2",        (int)JokerParts::g_col[2]);
    // ★ 约瑟夫相机（2 项）
    CfgPut("joseph_cam.show", show_draw_joseph_cam ? 1 : 0);
    CfgPut("joseph_cam.col",  (int)g_joseph_cam_col);
    CfgPut("mirror.col",      (int)g_mirror_col);
    // ★ 约瑟夫相机
    CfgPut("jcam.show",      JosephCamera::g_show ? 1 : 0);
    CfgPut("jcam.range_m",   (int)JosephCamera::g_range_m);
    CfgPut("jcam.font_size", (int)JosephCamera::g_font_size);
    CfgPut("jcam.col",       (int)JosephCamera::g_col);
    // ★ 密码机进度悬浮窗
    CfgPut("genlist.show",  g_gen_list_show ? 1 : 0);
    CfgPut("genlist.x",     (int)g_gen_list_x);
    CfgPut("genlist.y",     (int)g_gen_list_y);
    CfgPut("genlist.size",  (int)g_gen_size);
    // ★ 小丑蓄力自动松手 —— 2026-10-13 用户要求整体删除
    //   （jh.* 与 as.* 全部配置项随之去掉；旧的配置文件里残留的键会被忽略 ✓）

    // ---- AutoShock (auto terror-shock) ---- 已删除 ✓

    // ---- misc draw toggles that were never saved ----
    CfgPut("role.show",       show_draw_Role ? 1 : 0);
    CfgPut("touch.show",      show_draw_touch ? 1 : 0);
    CfgPut("minimap.h",       (int)g_minimap_h);
    CfgPut("mark.enabled",    g_mark_enabled ? 1 : 0);
    CfgPut("mark.col_chest",  (int)g_mark_col_chest);
    CfgPut("mark.col_monster",(int)g_mark_col_monster);
    CfgPut("mark.max_dist",   (int)g_mark_max_dist);
CfgPut("show_draw_Rect",         show_draw_Rect ? 1 : 0);
    CfgPut("show_draw_Line",         show_draw_Line ? 1 : 0);
    CfgPut("show_draw_Camera",       show_draw_Camera ? 1 : 0);
    CfgPut("show_draw_Door",         show_draw_Door ? 1 : 0);
    CfgPut("show_draw_umbrella",     show_draw_umbrella ? 1 : 0);
    CfgPut("show_draw_Box",          show_draw_Box ? 1 : 0);
    CfgPut("show_draw_Name",         show_draw_Name ? 1 : 0);
    CfgPut("show_draw_Distance",     show_draw_Distance ? 1 : 0);
CfgPut("show_draw_Mirror",       show_draw_Mirror ? 1 : 0);
    CfgPut("show_draw_Cellar",       show_draw_Cellar ? 1 : 0);
    CfgPut("show_draw_Chair",        show_draw_Chair ? 1 : 0);
    CfgPut("show_draw_Prop",         show_draw_Prop ? 1 : 0);
    //   2026-10-26：图标大小 + 图标/文字 XY 偏移（调完必须记住，重启别回默认）
    CfgPut("esp.icon_px",      g_icon_px);
    CfgPut("esp.icon_dx",      g_icon_dx);
    CfgPut("esp.icon_dy",      g_icon_dy);
    CfgPut("esp.icon_tx_dx",   g_txt_dx);
    CfgPut("esp.icon_tx_dy",   g_txt_dy);
    CfgPut("show_draw_Genius",       show_draw_Genius ? 1 : 0);
    CfgPut("show_draw_prophet",      show_draw_prophet ? 1 : 0);
    CfgPut("redqueenmod",            redqueenmod ? 1 : 0);
    // ★ 2026-10-13 新增：隐藏红夫人自身镜像 ✓
    CfgPut("hide_rq_self_mirror",    hide_rq_self_mirror ? 1 : 0);
    // （2026-10-13：copycat_mode 不再保存 —— 它是**自动识别**出来的，不是用户设置 ✓
    //   存了反而会在下次启动时被旧值覆盖，把自动识别弄坏 ✗）
    CfgPut("show_draw_secret_mechine", show_draw_secret_mechine ? 1 : 0);
    // ★ 2026-10-13："最后一台"独立开关（搬进「显示密码机」后新增）✓
    CfgPut("show_draw_last_gen",     show_draw_last_gen ? 1 : 0);
    CfgPut("show_draw_Heal",         show_draw_Heal ? 1 : 0);
    CfgPut("inform_ghost",           inform_ghost ? 1 : 0);
    CfgPut("Debugging",              Debugging ? 1 : 0);
    CfgPut("show_draw_trait",        show_draw_trait ? 1 : 0);
CfgPut("esp.rect_style",         g_esp.rect_style);
CfgPut("esp.rect_thickness",     g_esp.rect_thickness);
CfgPut("esp.rect_hunter",        g_esp.rect_hunter);
CfgPut("esp.rect_survivor",      g_esp.rect_survivor);
CfgPut("esp.rect_ghost",         g_esp.rect_ghost);
CfgPut("esp.rect_cc_good",       g_esp.rect_cc_good);
CfgPut("esp.rect_cc_bad",        g_esp.rect_cc_bad);
CfgPut("esp.rect_cc_neutral",    g_esp.rect_cc_neutral);
CfgPut("esp.line_thickness",     g_esp.line_thickness);
CfgPut("esp.line_color",         g_esp.line_color);
CfgPut("esp.name_size",          g_esp.name_size);
CfgPut("esp.name_color",         g_esp.name_color);
    CfgPut("show_draw_Bones",        show_draw_Bones ? 1 : 0);
    CfgPut("esp.bones_hunter",       g_esp.bones_hunter);
    CfgPut("esp.bones_survivor",     g_esp.bones_survivor);
    CfgPut("esp.bones_thickness",    g_esp.bones_thickness);
    CfgPut("esp.bones_head",         g_esp.bones_head ? 1 : 0);
    CfgPut("esp.bones_head_size",    (int)(g_esp.bones_head_size * 10.0f));   // ×10 保精度
    CfgPut("auto_skill.report_lead",  AutoSkill::g_report_lead_ms);   // 2026-10-26：记者松手快慢
    CfgPut("esp.bones_thickness_x10",(int)(g_esp.bones_thickness * 10.0f));
CfgPut("esp.genius_size",        g_esp.genius_size);
CfgPut("esp.genius_color",       g_esp.genius_color);
//   ★ 2026-10-13：天赋的**求生**颜色（用户要求分开调）✓
//     `esp.genius_color` 键名保持不动 = 老用户已调的值不丢 ✓
CfgPut("esp.genius_color_surv",  g_esp.genius_color_surv);
//   ★ 2026-10-13：地窖的字号/颜色 ✓
CfgPut("esp.hatch_size",         g_esp.hatch_size);
CfgPut("esp.hatch_color",        g_esp.hatch_color);
CfgPut("esp.trait_size",         g_esp.trait_size);
CfgPut("esp.trait_color",        g_esp.trait_color);
CfgPut("esp.heal_size",          g_esp.heal_size);
    CfgPut("esp.heal_color",         g_esp.heal_color);
    // ★ 2026-10-21 骑士头盔预测（界面 -> 存盘 -> 绘制 链路必须完整，不留假配置）✓
    CfgPut("show_draw_prep_genius",   show_draw_prep_genius ? 1 : 0);
    CfgPut("show_draw_Knight",       show_draw_Knight ? 1 : 0);
    CfgPut("knight.test",            g_knight_test_mode ? 1 : 0);
    CfgPut("esp.knight_size",        g_esp.knight_size);
    CfgPut("esp.knight_color",       g_esp.knight_color);
    CfgPut("esp.knight_warn",        g_esp.knight_warn);
    //   ★ 战术预见：编码 -> 中文 的标注（-1 = 还没标注）。点一次按钮就存这里 ✓
    CfgPut("knight.code_attack",     (int)PyKnight::g_bind_code[0]);
    CfgPut("knight.code_skill",      (int)PyKnight::g_bind_code[1]);
    CfgPut("knight.code_idle",       (int)PyKnight::g_bind_code[2]);
    CfgPut("auto_qte_on",            show_draw_AutoQte ? 1 : 0);
    CfgPut("auto_qte_x",             (int)QteAuto::g_x);
    CfgPut("auto_qte_y",             (int)QteAuto::g_y);
        CfgPut("auto_qte_hold",          QteAuto::g_hold_ms);
    CfgPut("auto_qte_delay",         QteAuto::g_delay_ms);
    CfgPut("auto_qte_point",         QteAuto::g_show_point ? 1 : 0);

//   ★ 2026-10-13：CD **就绪** 的颜色（原来写死绿，用户要求可调）✓
CfgPut("esp.trait_ready_color",  g_esp.trait_ready_color);
//   ★ 2026-10-13：名字/天赋/CD 三层的层间距（默认 0 = 贴死，可负=重叠）✓
CfgPut("esp.label_line_gap",     g_esp.label_line_gap);
//   ★ 2026-10-13：名字墨迹底边离框顶的距离（用户要求"往上移一点"）✓
CfgPut("esp.name_lift",          g_esp.name_lift);
//   ★ 2026-10-13 抗锯齿两项 ✓
CfgPut("esp.text_subpixel",      g_esp.text_subpixel ? 1 : 0);
CfgPut("esp.curve_tol",          g_esp.curve_tol);
CfgPut("esp.dist_color",        g_esp.dist_color);
//   ★★ 2026-10-13：把"能调但没存"的全补上 ✗
//     `esp.dist_size` 原来**连存盘都没有** —— 界面上没滑条、存盘也没写，
//     等于这个字段从头到尾是个死变量 ✗（声明了、有默认值，但没人用）
//     **规矩：界面上给用户调的东西，必须存盘**，否则重启就白调 ✗
CfgPut("esp.dist_size",          g_esp.dist_size);
CfgPut("esp.gen_size",           g_esp.gen_size);
CfgPut("esp.gen_color",          g_esp.gen_color);
CfgPut("esp.gen_bar_w",          g_esp.gen_bar_w);
CfgPut("esp.gen_bar_h",          g_esp.gen_bar_h);
CfgPut("esp.gen_bar_bg",         g_esp.gen_bar_bg);
CfgPut("esp.prop_size",          g_esp.prop_size);
CfgPut("esp.prop_color",         g_esp.prop_color);
CfgPut("esp.mark_size",          g_esp.mark_size);
CfgPut("esp.prophet_size",       g_esp.prophet_size);
CfgPut("esp.prophet_color",      g_esp.prophet_color);
CfgPut("esp.prep_color",         g_esp.prep_color);
CfgPut("esp.last_gen_size",      g_esp.last_gen_size);
CfgPut("esp.last_gen_w",         g_esp.last_gen_w);
CfgPut("esp.last_gen_h",         g_esp.last_gen_h);

    CfgPut("g_touch_adjust_enabled",    AutoSkill::g_touch_adjust_enabled ? 1 : 0);
CfgPut("g_auto_skill_enabled",      AutoSkill::g_auto_skill_enabled ? 1 : 0);
CfgPut("g_touch_rect_x",            AutoSkill::g_touch_rect_x);
CfgPut("g_touch_rect_y",            AutoSkill::g_touch_rect_y);
CfgPut("g_touch_rect_w",            AutoSkill::g_touch_rect_w);
CfgPut("g_touch_rect_h",            AutoSkill::g_touch_rect_h);
CfgPut("g_auto_flywheel_enabled",   AutoSkill::g_auto_flywheel_enabled ? 1 : 0);
CfgPut("g_priority_flywheel",       AutoSkill::g_priority_flywheel ? 1 : 0);
CfgPut("g_flywheel_rect_x",         AutoSkill::g_flywheel_rect_x);
CfgPut("g_flywheel_rect_y",         AutoSkill::g_flywheel_rect_y);
CfgPut("g_flywheel_rect_w",         AutoSkill::g_flywheel_rect_w);
CfgPut("g_flywheel_rect_h",         AutoSkill::g_flywheel_rect_h);
CfgPut("g_auto_skill_tap_count",    AutoSkill::g_auto_skill_tap_count);
CfgPut("g_auto_skill_tap_delay_ms", AutoSkill::g_auto_skill_tap_delay_ms);
CfgPut("g_skill_lead_time_ms",      AutoSkill::g_skill_lead_time_ms);
CfgPut("g_skill_windup_fallback_ms",AutoSkill::g_skill_windup_fallback_ms);

    CfgPut("AutoPallet.enabled",     AutoPallet::enabled ? 1 : 0);
    CfgPut("AutoPallet.mode",        AutoPallet::mode);
    CfgPut("AutoPallet.touch_x",     AutoPallet::touch_x);
    CfgPut("AutoPallet.touch_y",     AutoPallet::touch_y);
    CfgPut("AutoPallet.hold_ms",     AutoPallet::hold_ms);
    CfgPut("AutoPallet.show_range",  AutoPallet::show_range ? 1 : 0);
    CfgPut("AutoPallet.show_touch_point", AutoPallet::show_touch_point ? 1 : 0);
    
    // ★★★ 2026-10-17 补：这 4 项在 AutoSkill 页的 UI 里能调，
//   但原 SaveConfig 只 Load 不 Save —— 每次重启都回默认 ✗
//   （外迁 AutoSkill 时把这几行弄丢了）
CfgPut("PyAttack.max_distance",         PyAttack::max_distance());
CfgPut("PyAttack.require_facing",       PyAttack::require_facing() ? 1 : 0);
CfgPut("PyAttack.facing_cos_threshold", PyAttack::facing_cos_threshold());
CfgPut("PyAttack.skip_if_board_between",PyAttack::skip_if_board_between() ? 1 : 0);

    // ★ 防牛仔
CfgPut("acw.on",        AntiCowboy::g_on ? 1 : 0);
CfgPut("acw.dist",      (int)AntiCowboy::g_dist_m);
CfgPut("acw.fov",       (int)AntiCowboy::g_fov_deg);
CfgPut("acw.tap_x",     (int)AntiCowboy::g_tap_x);
CfgPut("acw.tap_y",     (int)AntiCowboy::g_tap_y);
CfgPut("acw.hold_ms",   AntiCowboy::g_hold_ms);
CfgPut("acw.show_rect", AntiCowboy::g_show_rect ? 1 : 0);
    

CfgPut("minimap.enabled",   g_minimap_enabled ? 1 : 0);
CfgPut("minimap.edit",      g_minimap_edit ? 1 : 0);
CfgPut("minimap.show_name", g_minimap_show_name ? 1 : 0);
CfgPut("minimap.x",         g_minimap_x);
CfgPut("minimap.y",         g_minimap_y);
CfgPut("minimap.w",         g_minimap_w);
CfgPut("minimap.zoom",      g_minimap_zoom);
CfgPut("minimap.auto_self", g_minimap_auto_self ? 1 : 0);
CfgPut("minimap.self_num",  g_minimap_self_num);

CfgPut("ac.on",    AutoCrash::g_on ? 1 : 0);
CfgPut("ac.dist",  (int)(AutoCrash::g_dist_m * 10));
CfgPut("ac.tap_x", (int)AutoCrash::g_tap_x);
CfgPut("ac.tap_y", (int)AutoCrash::g_tap_y);
CfgPut("ac.show_rect", AutoCrash::g_show_rect ? 1 : 0);
CfgPut("ac.rect_w",    (int)AutoCrash::g_rect_w);
CfgPut("ac.rect_h",    (int)AutoCrash::g_rect_h);

    // 模仿者身份悬浮窗(见 DrawCopycatWindow)
    CfgPut("ccwin.show",        g_ccwin_show ? 1 : 0);
    CfgPut("ccwin.x",           g_ccwin_x);
    CfgPut("ccwin.y",           g_ccwin_y);
    CfgPut("ccwin.size",        g_ccwin_size);
    CfgPut("ccwin.cols",        g_ccwin_cols);
CfgPut("ui.dark",           g_dark_theme ? 1 : 0);
CfgPut("ui.blur",           g_blur_strength);
CfgPut("ui.anim",           g_anim_scale);
CfgPut("ui.accent",         (int)g_theme_accent);
// 诊断：当前监管 + 它的前摇，用来确认内置表有没有生效

CfgPut("diag.tap_x",        g_diag_tap_x);
//   ★ 框上文字的实际 Y 坐标（诊断用，只读）✓
CfgPut("diag.lbl_y1",       g_diag_lbl_y1);
CfgPut("diag.lbl_name_y",   g_diag_lbl_name_y);
CfgPut("diag.lbl_gen_y",    g_diag_lbl_gen_y);
CfgPut("diag.lbl_cd_y",     g_diag_lbl_cd_y);
CfgPut("diag.lbl_nlh",      g_diag_lbl_nlh);
CfgPut("diag.lbl_glh",      g_diag_lbl_glh);
CfgPut("diag.lbl_gap",      g_diag_lbl_gap);
CfgPut("diag.tap_y",        g_diag_tap_y);
CfgPut("diag.tap_ok",       g_diag_tap_ok);
CfgPut("diag.tap_kind",     g_diag_tap_kind);
CfgPut("diag.tap_count",    g_diag_tap_count);
// 诊断：世界状态的真值（只写不读，用来确认主题标志有没有被运行时改掉）
CfgPut("ui.world",          g_ui_state.world);
CfgPut("ui.worldt",         (int)(g_ui_state.worldT * 100.0f));
CfgPut("ui.island",         (int)(g_ui_state.islandP * 100.0f));
CfgPut("ui.islandt",        (int)(g_ui_state.islandT * 100.0f));
CfgPut("ui.fps",            (int)(ImGui::GetIO().Framerate + 0.5f));
CfgPut("ui.dt_us",          (int)(ImGui::GetIO().DeltaTime * 1000000.0f));
// ⚠ 存/取必须操作同一个变量（voice）。
//   存 show_window、取 voice 的话，dwrg_set 的 "改表->存盘->重载" 序列里
//   存盘那步会把刚设的值覆盖掉，配置永远改不动。
CfgPut("ui.show",           voice ? 1 : 0);

    char cfgp[64]; GetConfigPath(cfgp, sizeof(cfgp));
    // 先拼成完整文本，再整体加密写出。
    //   原来是直接 fprintf 明文 —— cat 一下就知道开了哪些功能、坐标在哪。
    {
        std::string plain;
        plain.reserve(g_cfg_n * 32);
        char line[96];
        for (int i = 0; i < g_cfg_n; i++) {
            snprintf(line, sizeof(line), "%s=%s\n", g_cfg[i].key, g_cfg[i].val);
            plain += line;
        }
        std::string enc = SecCfg::Encrypt(plain);
        SecCfg::WriteAll(cfgp, enc);
    }
    printf("[配置] 已保存到 %s (%d 项, 已加密)\n", cfgp, g_cfg_n);
    fflush(stdout);
}

// 直接 _exit() 绕过 atexit 和所有 detached 线程清理，只留 50ms 让 UI 刷一帧
static void ForceExit() {
    //   ★ 2026-10-21 用户要求：退出**不存盘**（只有设置页「保存配置」按钮才写）
    std::thread([](){
        usleep(50 * 1000);
        _exit(0);   // 系统调用直接退，不清理（内存问题已定位，还原）
    }).detach();
}

static void LoadConfig() {
    char cfgp[64]; GetConfigPath(cfgp, sizeof(cfgp));
    // 整体读进来，带魔数就解密，不带就是旧版明文（向后兼容，下次保存自动转密文）
    std::string raw, text;
    if (!SecCfg::ReadAll(cfgp, raw)) {
        printf("[配置] %s 不存在，使用默认值\n", cfgp);
        return;
    }
    bool encrypted = SecCfg::Decrypt(raw, text);
    if (!encrypted) text = raw;
    printf("[配置] %s  %s，%zu 字节\n", cfgp,
           encrypted ? "已加密" : "明文(旧格式,下次保存转加密)", text.size());

    // 逐行解析（用内存里的文本，不再读文件）
    g_cfg_n = 0;
    {
        size_t pos = 0;
        char line[256];
        while (pos < text.size() && g_cfg_n < kCfgMax) {
            size_t nl = text.find('\n', pos);
            size_t len = (nl == std::string::npos ? text.size() : nl) - pos;
            if (len >= sizeof(line)) len = sizeof(line) - 1;
            memcpy(line, text.data() + pos, len);
            line[len] = 0;
            pos = (nl == std::string::npos) ? text.size() : nl + 1;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char* k = line;
        char* v = eq + 1;
        while (*v == ' ') v++;
        size_t vl = strlen(v);
        while (vl > 0 && (v[vl-1] == '\n' || v[vl-1] == '\r' || v[vl-1] == ' ')) v[--vl] = 0;
        snprintf(g_cfg[g_cfg_n].key, sizeof(g_cfg[g_cfg_n].key), "%s", k);
        snprintf(g_cfg[g_cfg_n].val, sizeof(g_cfg[g_cfg_n].val), "%s", v);
        g_cfg_n++;
        }
    }

    int iv; float fv; ImU32 cv;
    for (int i = 0; i < g_cfg_n; i++) {
        // ★ 链首必须从 if 开始（我的恢复脚本把 else if 插到了链首，编译报 expected expression）
        if      (CfgGet(i, "jp.show",      iv)) JokerParts::g_show      = iv != 0;
        else if (CfgGet(i, "jp.range_m",   iv)) JokerParts::g_range_m   = (float)iv;
        else if (CfgGet(i, "jp.font_size", iv)) JokerParts::g_font_size = (float)iv;
        else if (CfgGet(i, "jp.name",      iv)) JokerParts::g_show_name = iv != 0;
        else if (CfgGet(i, "jp.dist",      iv)) JokerParts::g_show_dist = iv != 0;
        else if (CfgGet(i, "jp.e0",        iv)) JokerParts::g_enable[0] = iv != 0;
        else if (CfgGet(i, "jp.e1",        iv)) JokerParts::g_enable[1] = iv != 0;
        else if (CfgGet(i, "jp.e2",        iv)) JokerParts::g_enable[2] = iv != 0;
        else if (CfgGet(i, "jp.c0",        iv)) JokerParts::g_col[0]    = (ImU32)iv;
        else if (CfgGet(i, "jp.c1",        iv)) JokerParts::g_col[1]    = (ImU32)iv;
        else if (CfgGet(i, "jp.c2",        iv)) JokerParts::g_col[2]    = (ImU32)iv;
        else if (CfgGet(i, "joseph_cam.show", iv)) show_draw_joseph_cam = iv != 0;
        else if (CfgGet(i, "joseph_cam.col",  iv)) g_joseph_cam_col     = (ImU32)iv;
        else if (CfgGet(i, "mirror.col",      iv)) g_mirror_col         = (ImU32)iv;
        else if (CfgGet(i, "jcam.show",      iv)) JosephCamera::g_show      = iv != 0;
        else if (CfgGet(i, "jcam.range_m",   iv)) JosephCamera::g_range_m   = (float)iv;
        else if (CfgGet(i, "jcam.font_size", iv)) JosephCamera::g_font_size = (float)iv;
        else if (CfgGet(i, "jcam.col",       iv)) JosephCamera::g_col       = (ImU32)iv;
        else if (CfgGet(i, "genlist.show",  iv)) g_gen_list_show = iv != 0;
        else if (CfgGet(i, "genlist.x",     iv)) g_gen_list_x    = (float)iv;
        else if (CfgGet(i, "genlist.y",     iv)) g_gen_list_y    = (float)iv;
        else if (CfgGet(i, "genlist.size",  iv)) g_gen_size      = (float)iv;
        // （2026-10-13：jh.* 与 as.* 的读取全部删除 —— 自动震慑功能整体移除 ✓）
        //   旧的配置文件里如果还有这些键，会被下面的链自然跳过，不会报错 ✓
        // （as.* 的读取也一并删除，同上 ✓）
        else if (CfgGet(i, "role.show",       iv)) show_draw_Role            = iv != 0;
        else if (CfgGet(i, "touch.show",      iv)) show_draw_touch           = iv != 0;
        else if (CfgGet(i, "minimap.h",       iv)) g_minimap_h                 = (float)iv;
        else if (CfgGet(i, "mark.enabled",    iv)) g_mark_enabled              = iv != 0;
        else if (CfgGet(i, "mark.col_chest",  iv)) g_mark_col_chest            = (ImU32)iv;
        else if (CfgGet(i, "mark.col_monster",iv)) g_mark_col_monster          = (ImU32)iv;
        else if (CfgGet(i, "mark.max_dist",   iv)) g_mark_max_dist             = (float)iv;
if      (CfgGet(i, "show_draw_Rect", iv))           show_draw_Rect = iv != 0;
        else if (CfgGet(i, "show_draw_Bones", iv))          show_draw_Bones = iv != 0;
        else if (CfgGet(i, "esp.bones_hunter", iv))         g_esp.bones_hunter   = (ImU32)iv;
        else if (CfgGet(i, "esp.bones_survivor", iv))       g_esp.bones_survivor = (ImU32)iv;
        else if (CfgGet(i, "esp.bones_thickness", iv))      g_esp.bones_thickness = (float)iv;
        else if (CfgGet(i, "esp.bones_head", iv))           g_esp.bones_head     = iv != 0;
        else if (CfgGet(i, "esp.bones_head_size", iv))      g_esp.bones_head_size = (float)iv / 10.0f;
        else if (CfgGet(i, "auto_skill.report_lead", iv))     AutoSkill::g_report_lead_ms = iv;
        else if (CfgGet(i, "esp.bones_thickness_x10", iv))  g_esp.bones_thickness = (float)iv / 10.0f;
        else if (CfgGet(i, "show_draw_Line", iv))           show_draw_Line = iv != 0;
        else if (CfgGet(i, "show_draw_Camera", iv))         show_draw_Camera = iv != 0;
        else if (CfgGet(i, "show_draw_Door", iv))           show_draw_Door = iv != 0;
        else if (CfgGet(i, "show_draw_umbrella", iv))       show_draw_umbrella = iv != 0;
        else if (CfgGet(i, "show_draw_Box", iv))            show_draw_Box = iv != 0;
        else if (CfgGet(i, "show_draw_Name", iv))           show_draw_Name = iv != 0;
        else if (CfgGet(i, "show_draw_Distance", iv))       show_draw_Distance = iv != 0;
else if (CfgGet(i, "show_draw_Mirror",   iv))       show_draw_Mirror   = iv != 0;
        else if (CfgGet(i, "show_draw_Cellar", iv))         show_draw_Cellar = iv != 0;
        else if (CfgGet(i, "show_draw_Chair", iv))          show_draw_Chair = iv != 0;
        else if (CfgGet(i, "show_draw_Prop", iv))           show_draw_Prop = iv != 0;
        //   ★ 2026-10-26：与上面的 CfgPut 一一对应
        else if (CfgGet(i, "esp.icon_px", fv))              g_icon_px = fv;
        else if (CfgGet(i, "esp.icon_dx", fv))              g_icon_dx = fv;
        else if (CfgGet(i, "esp.icon_dy", fv))              g_icon_dy = fv;
        else if (CfgGet(i, "esp.icon_tx_dx", fv))           g_txt_dx = fv;
        else if (CfgGet(i, "esp.icon_tx_dy", fv))           g_txt_dy = fv;
        else if (CfgGet(i, "show_draw_Genius", iv))         show_draw_Genius = iv != 0;
        else if (CfgGet(i, "show_draw_prophet", iv))        show_draw_prophet = iv != 0;
        else if (CfgGet(i, "redqueenmod", iv))              redqueenmod = iv != 0;
        // （2026-10-13：copycat_mode 不再从配置读回 —— 它是**自动识别**的 ✓
        //   读回来会把它钉死成上次的值，把自动识别弄坏 ✗
        //   旧配置文件里残留的 copycat_mode 键会被这条链自然跳过，不报错 ✓）
        else if (CfgGet(i, "show_draw_secret_mechine", iv)) show_draw_secret_mechine = iv != 0;
        else if (CfgGet(i, "show_draw_last_gen", iv))       show_draw_last_gen = iv != 0;
        else if (CfgGet(i, "show_draw_Heal", iv))           show_draw_Heal = iv != 0;
        else if (CfgGet(i, "inform_ghost", iv))             inform_ghost = iv != 0;
        else if (CfgGet(i, "Debugging", iv))                Debugging = iv != 0;
        else if (CfgGet(i, "show_draw_trait", iv))        show_draw_trait = iv != 0;
else if (CfgGet(i, "esp.rect_style", iv))         g_esp.rect_style = iv;
else if (CfgGet(i, "esp.rect_thickness", fv))     g_esp.rect_thickness = fv;
else if (CfgGet(i, "esp.rect_hunter", cv))        g_esp.rect_hunter = cv;
else if (CfgGet(i, "esp.rect_survivor", cv))      g_esp.rect_survivor = cv;
else if (CfgGet(i, "esp.rect_ghost", cv))         g_esp.rect_ghost = cv;
else if (CfgGet(i, "esp.rect_cc_good", cv))       g_esp.rect_cc_good = cv;
else if (CfgGet(i, "esp.rect_cc_bad", cv))        g_esp.rect_cc_bad = cv;
else if (CfgGet(i, "esp.rect_cc_neutral", cv))    g_esp.rect_cc_neutral = cv;
else if (CfgGet(i, "esp.line_thickness", fv))     g_esp.line_thickness = fv;
else if (CfgGet(i, "esp.line_color", cv))         g_esp.line_color = cv;
else if (CfgGet(i, "esp.name_size", fv))          g_esp.name_size = fv;
else if (CfgGet(i, "esp.name_color", cv))         g_esp.name_color = cv;
else if (CfgGet(i, "esp.genius_size", fv))        g_esp.genius_size = fv;
else if (CfgGet(i, "esp.genius_color", cv))       g_esp.genius_color = cv;
        //   ★ 2026-10-13 新增字段的读取（与上面的 CfgPut 一一对应）✓
        else if (CfgGet(i, "esp.genius_color_surv", cv))   g_esp.genius_color_surv = cv;
        else if (CfgGet(i, "esp.hatch_size", fv))          g_esp.hatch_size = fv;
        else if (CfgGet(i, "esp.hatch_color", cv))         g_esp.hatch_color = cv;
else if (CfgGet(i, "esp.trait_size", fv))         g_esp.trait_size = fv;
else if (CfgGet(i, "esp.trait_color", cv))        g_esp.trait_color = cv;
else if (CfgGet(i, "esp.heal_size", fv))          g_esp.heal_size = fv;
        else if (CfgGet(i, "esp.heal_color", cv))         g_esp.heal_color = cv;
        //   ★ 2026-10-21 骑士头盔预测（与上面的 CfgPut 一一对应）✓
        else if (CfgGet(i, "show_draw_prep_genius", iv))  show_draw_prep_genius = iv != 0;
        else if (CfgGet(i, "show_draw_Knight", iv))        show_draw_Knight = iv != 0;
        else if (CfgGet(i, "knight.test", iv))             g_knight_test_mode = iv != 0;
        else if (CfgGet(i, "esp.knight_size", fv))         g_esp.knight_size = fv;
        else if (CfgGet(i, "esp.knight_color", cv))        g_esp.knight_color = cv;
        else if (CfgGet(i, "esp.knight_warn", cv))         g_esp.knight_warn = cv;
        //   ★ 战术预见标注（-1 = 未标注）✓
        else if (CfgGet(i, "knight.code_attack", iv))      PyKnight::g_bind_code[0] = iv;
        else if (CfgGet(i, "knight.code_skill", iv))       PyKnight::g_bind_code[1] = iv;
        else if (CfgGet(i, "knight.code_idle", iv))        PyKnight::g_bind_code[2] = iv;
        else if (CfgGet(i, "auto_qte_on", iv))              show_draw_AutoQte = iv != 0;
        else if (CfgGet(i, "auto_qte_x", iv))               QteAuto::g_x = (float)iv;
        else if (CfgGet(i, "auto_qte_y", iv))               QteAuto::g_y = (float)iv;
                else if (CfgGet(i, "auto_qte_hold", iv))            QteAuto::g_hold_ms = iv;
        else if (CfgGet(i, "auto_qte_delay", iv))           QteAuto::g_delay_ms = iv;
        else if (CfgGet(i, "auto_qte_point", iv))           QteAuto::g_show_point = iv != 0;

        //   ★ 2026-10-13 新增字段的读取（与上面的 CfgPut 一一对应）✓
        else if (CfgGet(i, "esp.trait_ready_color", cv))   g_esp.trait_ready_color = cv;
        else if (CfgGet(i, "esp.label_line_gap", fv))      g_esp.label_line_gap = fv;
        //   ★ 2026-10-13：名字离框顶的距离 ✓
        else if (CfgGet(i, "esp.name_lift", fv))           g_esp.name_lift = fv;
        //   ★ 2026-10-13 抗锯齿两项 ✓
        else if (CfgGet(i, "esp.text_subpixel", iv))       g_esp.text_subpixel = iv != 0;
        else if (CfgGet(i, "esp.curve_tol", fv))           g_esp.curve_tol = fv;
else if (CfgGet(i, "esp.dist_color", cv))       g_esp.dist_color = cv;
        //   ★ 2026-10-13 新增字段的读取（与上面的 CfgPut 一一对应）✓
        else if (CfgGet(i, "hide_rq_self_mirror", iv))      hide_rq_self_mirror = iv != 0;
        else if (CfgGet(i, "esp.dist_size", fv))            g_esp.dist_size = fv;
        else if (CfgGet(i, "esp.gen_size", fv))             g_esp.gen_size = fv;
        else if (CfgGet(i, "esp.gen_color", cv))            g_esp.gen_color = cv;
        else if (CfgGet(i, "esp.gen_bar_w", fv))            g_esp.gen_bar_w = fv;
        else if (CfgGet(i, "esp.gen_bar_h", fv))            g_esp.gen_bar_h = fv;
        else if (CfgGet(i, "esp.gen_bar_bg", cv))           g_esp.gen_bar_bg = cv;
        else if (CfgGet(i, "esp.prop_size", fv))            g_esp.prop_size = fv;
        else if (CfgGet(i, "esp.prop_color", cv))           g_esp.prop_color = cv;
        else if (CfgGet(i, "esp.mark_size", fv))            g_esp.mark_size = fv;
        else if (CfgGet(i, "esp.prophet_size", fv))         g_esp.prophet_size = fv;
        else if (CfgGet(i, "esp.prophet_color", cv))        g_esp.prophet_color = cv;
        else if (CfgGet(i, "esp.prep_color", cv))           g_esp.prep_color = cv;
        else if (CfgGet(i, "esp.last_gen_size", fv))        g_esp.last_gen_size = fv;
        else if (CfgGet(i, "esp.last_gen_w", fv))           g_esp.last_gen_w = fv;
        else if (CfgGet(i, "esp.last_gen_h", fv))           g_esp.last_gen_h = fv;
       
       else if (CfgGet(i, "g_touch_adjust_enabled", iv))   AutoSkill::g_touch_adjust_enabled = iv != 0;
else if (CfgGet(i, "g_auto_skill_enabled", iv))     AutoSkill::g_auto_skill_enabled = iv != 0;
else if (CfgGet(i, "g_touch_rect_x", fv))           AutoSkill::g_touch_rect_x = fv;
else if (CfgGet(i, "g_touch_rect_y", fv))           AutoSkill::g_touch_rect_y = fv;
else if (CfgGet(i, "g_touch_rect_w", fv))           AutoSkill::g_touch_rect_w = fv;
else if (CfgGet(i, "g_touch_rect_h", fv))           AutoSkill::g_touch_rect_h = fv;
else if (CfgGet(i, "g_auto_flywheel_enabled", iv))  AutoSkill::g_auto_flywheel_enabled = iv != 0;
else if (CfgGet(i, "g_priority_flywheel", iv))      AutoSkill::g_priority_flywheel = iv != 0;
else if (CfgGet(i, "g_flywheel_rect_x", fv))        AutoSkill::g_flywheel_rect_x = fv;
else if (CfgGet(i, "g_flywheel_rect_y", fv))        AutoSkill::g_flywheel_rect_y = fv;
else if (CfgGet(i, "g_flywheel_rect_w", fv))        AutoSkill::g_flywheel_rect_w = fv;
else if (CfgGet(i, "g_flywheel_rect_h", fv))        AutoSkill::g_flywheel_rect_h = fv;
else if (CfgGet(i, "g_auto_skill_tap_count", iv))   AutoSkill::g_auto_skill_tap_count = (iv<1?1:(iv>8?8:iv));
else if (CfgGet(i, "g_auto_skill_tap_delay_ms", iv))AutoSkill::g_auto_skill_tap_delay_ms = (iv<0?0:(iv>1000?1000:iv));
else if (CfgGet(i, "g_skill_lead_time_ms", iv))     AutoSkill::g_skill_lead_time_ms = iv;
else if (CfgGet(i, "g_skill_windup_fallback_ms", iv)) AutoSkill::g_skill_windup_fallback_ms = (iv<50?50:(iv>2000?2000:iv));

        else if (CfgGet(i, "AutoPallet.enabled", iv))       AutoPallet::enabled = iv != 0;
        else if (CfgGet(i, "AutoPallet.mode", iv))          AutoPallet::mode = iv;
        else if (CfgGet(i, "AutoPallet.touch_x", fv))       AutoPallet::touch_x = fv;
        else if (CfgGet(i, "AutoPallet.touch_y", fv))       AutoPallet::touch_y = fv;
        else if (CfgGet(i, "AutoPallet.hold_ms", iv))       AutoPallet::hold_ms = iv;
        else if (CfgGet(i, "AutoPallet.show_range", iv))    AutoPallet::show_range = iv != 0;
        else if (CfgGet(i, "AutoPallet.show_touch_point", iv)) AutoPallet::show_touch_point = iv != 0;
        else if (CfgGet(i, "PyAttack.require_facing", iv))       PyAttack::set_require_facing(iv != 0);
        else if (CfgGet(i, "PyAttack.facing_cos_threshold", fv)) PyAttack::set_facing_cos_threshold(fv);
        else if (CfgGet(i, "PyAttack.max_distance", fv))         PyAttack::set_max_distance(fv);
        else if (CfgGet(i, "PyAttack.skip_if_board_between", iv)) PyAttack::set_skip_if_board_between(iv != 0);
        
        
        else if (CfgGet(i, "acw.on",    iv)) AntiCowboy::g_on     = iv != 0;
else if (CfgGet(i, "acw.dist",  iv)) AntiCowboy::g_dist_m = (float)iv;
else if (CfgGet(i, "acw.fov",   iv)) AntiCowboy::g_fov_deg = (float)iv;
else if (CfgGet(i, "acw.tap_x", iv)) AntiCowboy::g_tap_x  = (float)iv;
else if (CfgGet(i, "acw.tap_y", iv)) AntiCowboy::g_tap_y  = (float)iv;
else if (CfgGet(i, "acw.hold_ms", iv))   AntiCowboy::g_hold_ms   = iv;
else if (CfgGet(i, "acw.show_rect", iv)) AntiCowboy::g_show_rect = iv != 0;
        
else if (CfgGet(i, "minimap.enabled",   iv)) g_minimap_enabled   = iv != 0;
else if (CfgGet(i, "minimap.edit",      iv)) g_minimap_edit      = iv != 0;
else if (CfgGet(i, "minimap.show_name", iv)) g_minimap_show_name = iv != 0;
else if (CfgGet(i, "minimap.x",         fv)) g_minimap_x         = fv;
else if (CfgGet(i, "minimap.y",         fv)) g_minimap_y         = fv;
else if (CfgGet(i, "minimap.w",         fv)) { g_minimap_w = fv; g_minimap_h = fv; }
else if (CfgGet(i, "minimap.zoom",      fv)) g_minimap_zoom      = fv;
else if (CfgGet(i, "minimap.auto_self", iv)) g_minimap_auto_self = iv != 0;
else if (CfgGet(i, "minimap.self_num",  iv)) g_minimap_self_num  = iv;

else if (CfgGet(i, "ac.on",    iv)) AutoCrash::g_on     = iv != 0;
else if (CfgGet(i, "ac.dist",  iv)) AutoCrash::g_dist_m = (float)iv / 10.0f;
else if (CfgGet(i, "ac.tap_x", iv)) AutoCrash::g_tap_x  = (float)iv;
else if (CfgGet(i, "ac.tap_y", iv)) AutoCrash::g_tap_y  = (float)iv;
else if (CfgGet(i, "ac.show_rect", iv)) AutoCrash::g_show_rect = iv != 0;
else if (CfgGet(i, "ac.rect_w",    iv)) AutoCrash::g_rect_w    = (float)iv;
else if (CfgGet(i, "ac.rect_h",    iv)) AutoCrash::g_rect_h    = (float)iv;

// 模仿者身份悬浮窗
else if (CfgGet(i, "ccwin.show",        iv)) g_ccwin_show  = iv != 0;
else if (CfgGet(i, "ccwin.x",           fv)) g_ccwin_x     = fv;
else if (CfgGet(i, "ccwin.y",           fv)) g_ccwin_y     = fv;
else if (CfgGet(i, "ccwin.size",        fv)) g_ccwin_size  = fv;
else if (CfgGet(i, "ccwin.cols",        fv)) g_ccwin_cols  = fv;
else if (CfgGet(i, "ui.dark",           iv)) g_dark_theme        = iv != 0;
else if (CfgGet(i, "ui.blur",           iv)) g_blur_strength     = iv;
else if (CfgGet(i, "ui.anim",           iv)) g_anim_scale        = (iv < 5 ? 5 : (iv > 300 ? 300 : iv));
else if (CfgGet(i, "ui.accent",         iv)) g_theme_accent      = iv;
// ⚠ 必须写 voice 而不是 show_window —— 有个后台线程每 10ms 执行
//   `show_window = voice;`，直接写 show_window 会被立刻覆盖。
else if (CfgGet(i, "ui.show",           iv)) voice               = iv != 0;
    
    }
    // 如果配置里读到了有效的触摸区域坐标，标记为已初始化，
// 否则 TouchRect_Draw / AutoSkill_Worker 会把它重置回屏幕默认位置
if (AutoSkill::g_touch_rect_x > 0.f || AutoSkill::g_touch_rect_y > 0.f)         AutoSkill::g_touch_rect_inited = true;
if (AutoSkill::g_flywheel_rect_x > 0.f || AutoSkill::g_flywheel_rect_y > 0.f)   AutoSkill::g_flywheel_rect_inited = true;


printf("[配置] 已从 %s 加载 %d 项\n", cfgp, g_cfg_n);
    fflush(stdout);
}

// ==================== 新 UI 的四个页面（供 my_ui.cpp 调用） ====================
namespace UI_Pages {

// ---------- 页 0：绘制 ----------
void Draw(Theme::Palette& pal) {
    using namespace FangUI;

    // ===================== 方框 =====================
    if (Fold("方框", &show_draw_Rect, pal)) {
        ImGui::Indent(20.0f);
        {
            static const char* st[] = { "全框", "四角" };
            int ns = Segmented("rect_style", st, 2, g_esp.rect_style, pal);
            if (ns != g_esp.rect_style) g_esp.rect_style = ns;
        }
        {
            float v = g_esp.rect_thickness;
            if (Slider("rect_th", &v, 0.5f, 6.0f, "%.1f px", pal))
                g_esp.rect_thickness = v;
        }
        Rule(pal, Space::M);
        ColorRow("监管者",  &g_esp.rect_hunter,     pal);
        ColorRow("求生者",  &g_esp.rect_survivor,   pal);
        ColorRow("鬼魂",    &g_esp.rect_ghost,      pal);
        ColorRow("侦探团",  &g_esp.rect_cc_good,    pal);
        ColorRow("模仿者",  &g_esp.rect_cc_bad,     pal);
        ColorRow("中立",    &g_esp.rect_cc_neutral, pal);
        ImGui::Unindent(20.0f);
        ImGui::Dummy(ImVec2(0, Space::S));
    }

    // ===================== 骨骼 =====================
    if (Fold("骨骼", &show_draw_Bones, pal)) {
        ImGui::Indent(20.0f);
        ColorRow("监管者", &g_esp.bones_hunter,   pal);
        ColorRow("求生者", &g_esp.bones_survivor, pal);
        {
            float v = g_esp.bones_thickness;
            if (Slider("bones_th", &v, 0.5f, 6.0f, "%.1f px", pal))
                g_esp.bones_thickness = v;
            //   2026-10-26：头部粗细（圆半径）—— 只影响头那个圆
            {
                float hv = g_esp.bones_head_size;
                if (Slider("bones_hs", &hv, 3.0f, 26.0f, "%.1f px", pal))
                    g_esp.bones_head_size = hv;
            }
            //   2026-10-26：同步绘制框 —— 高个子角色的骨骼超出方框时打开

        }
        ImGui::Checkbox("画头部", &g_esp.bones_head);
        ImGui::Unindent(20.0f);
        ImGui::Dummy(ImVec2(0, Space::S));
    }

    // ===================== 射线 =====================
    if (Fold("射线", &show_draw_Line, pal)) {
        ImGui::Indent(20.0f);
        ColorRow("颜色", &g_esp.line_color, pal);
        {
            float v = g_esp.line_thickness;
            if (Slider("ln_th", &v, 0.5f, 6.0f, "%.1f px", pal))
                g_esp.line_thickness = v;
        }
        ImGui::Unindent(20.0f);
        ImGui::Dummy(ImVec2(0, Space::S));
    }

    // ===================== 名字 =====================
    if (Fold("名字", &show_draw_Name, pal)) {
        ImGui::Indent(20.0f);
        ColorRow("颜色", &g_esp.name_color, pal);
        {
            float v = g_esp.name_size;
            if (Slider("nm_sz", &v, 12.0f, 48.0f, "%.0f", pal))
                g_esp.name_size = v;
        }
        ImGui::Unindent(20.0f);
        ImGui::Dummy(ImVec2(0, Space::S));
    }

    // ===================== 距离 =====================
if (Fold("距离", &show_draw_Distance, pal)) {
    ImGui::Indent(20.0f);
    ColorRow("颜色", &g_esp.dist_color, pal);
    //   ★ 2026-10-13：补上尺寸滑条 ✗
    //     原来**只有颜色没有尺寸** —— 而 dist_size 这个字段早就存在，
    //     只是既没界面也没存盘、绘制时也没用（三个地方全断）✗
    //     现在补齐：界面 -> 存盘 -> 绘制，链路完整 ✓
    {
        float v = g_esp.dist_size;
        if (Slider("ds_sz", &v, 12.0f, 48.0f, "%.0f", pal))
            g_esp.dist_size = v;
    }
    ImGui::Unindent(20.0f);
    ImGui::Dummy(ImVec2(0, Space::S));
}
RowSwitch("红夫人镜像", &show_draw_Mirror, pal);
//   ★ 2026-10-13 新增：隐藏镜像**本体**（只留镜中的求生者）✓
{
    ImGui::Indent(20.0f);
    RowSwitch("隐藏红夫人自身镜像", &hide_rq_self_mirror, pal);
    ImGui::Unindent(20.0f);
}

    // ===================== 天赋 =====================
    if (Fold("天赋", &show_draw_Genius, pal)) {
        ImGui::Indent(20.0f);
        //   ★★ 2026-10-13 用户要求：天赋颜色**分开调监管和求生** ★★
        //     原话：「现在天赋颜色调整只对监管的生效，你补个求生的，
        //            把原先的标注成监管」
        //   所以原来的"颜色"那行**标注成「监管颜色」**（键名不变 ✓），
        //   并**新增「求生颜色」** ✓
        //   ⚠ 键名 `esp.genius_color` 保持不变 ✗
        //     它是老用户已经调过的值 —— 改名等于把他们的设置清空 ✗
        ColorRow("监管颜色", &g_esp.genius_color, pal);
        ColorRow("求生颜色", &g_esp.genius_color_surv, pal);
        //   ★ 2026-10-21 用户要求：把「预知天赋」的设置从"预知监管"搬到「天赋」里，
        //     并给它**独立开关**（原来跟 show_draw_prophet 绑在一起，关掉预知监管就一起没了）
        ImGui::Dummy(ImVec2(0, Space::S));
        RowSwitch("预知天赋", &show_draw_prep_genius, pal);
        ColorRow("预知天赋颜色", &g_esp.prep_color, pal);
        {
            float pv = g_esp.prophet_size;
            if (Slider("pg_sz", &pv, 12.0f, 48.0f, "字号 %.0f", pal))
                g_esp.prophet_size = pv;
        }
        {
            float v = g_esp.genius_size;
            if (Slider("gn_sz", &v, 12.0f, 48.0f, "%.0f", pal))
                g_esp.genius_size = v;
        }
        ImGui::Unindent(20.0f);
        ImGui::Dummy(ImVec2(0, Space::S));
    }

    // ===================== 技能 CD =====================
    if (Fold("技能CD", &show_draw_trait, pal)) {
        ImGui::Indent(20.0f);
        //   ★★ 2026-10-13 用户要求：**就绪 / 冷却中 两个颜色都要能调** ★★
        //     原话：「写个'技能cd就绪'颜色调整，之前那个只能调整在cd中的颜色」
        //     原来就绪时是写死的 color_green -> 用户只能调"CD 中"那一半 ✗
        ColorRow("冷却中颜色", &g_esp.trait_color, pal);
        ColorRow("就绪颜色",   &g_esp.trait_ready_color, pal);
        {
            float v = g_esp.trait_size;
            if (Slider("tr_sz", &v, 12.0f, 48.0f, "%.0f", pal))
                g_esp.trait_size = v;
        }
        ImGui::Unindent(20.0f);
        ImGui::Dummy(ImVec2(0, Space::S));
    }

    // ===================== 治疗进度 =====================
    if (Fold("治疗进度", &show_draw_Heal, pal)) {
        ImGui::Indent(20.0f);
        ColorRow("颜色", &g_esp.heal_color, pal);
        {
            float v = g_esp.heal_size;
            if (Slider("heal_sz", &v, 10.0f, 60.0f, "%.0f", pal))
                g_esp.heal_size = v;
        }
        ImGui::Unindent(20.0f);
        ImGui::Dummy(ImVec2(0, Space::S));
    }

    // ===================== 骑士头盔预测（战术预见）=====================
    //   ★ 2026-10-21 用户要求：显示固定到"预知监管下方"那块 HUD 区，只留字号/颜色可调
    //     所以这里**只有**：开关（Fold 标题）+ 颜色 + 字号，其它选项全部删掉
    if (Fold("骑士头盔预测", &show_draw_Knight, pal)) {
        ImGui::Indent(20.0f);
        RowSwitch("测试", &g_knight_test_mode, pal);
        ColorRow("文字颜色", &g_esp.knight_color, pal);
        {
            float v = g_esp.knight_size;
            if (Slider("kn_sz", &v, 10.0f, 60.0f, "%.0f", pal))
                g_esp.knight_size = v;
        }
        ImGui::TextDisabled("显示位置：预知监管下方（居中）；10 秒内无新预测自动消失");
        ImGui::Unindent(20.0f);
        ImGui::Dummy(ImVec2(0, Space::S));
    }

    // ===================== 名字/天赋/CD 的层间距 =====================
    //   ★ 2026-10-13 新增：用户两次反馈"名字和天赋中间空太大" ✓
    //     我推了两轮公式都说只有 1px，说明**我量的量不是用户看到的量** ✗
    //     与其继续猜，不如做成可调（默认 0 = 贴死，可拖到负数=重叠）✓
    //   ⚠ 这个 Fold 绑的是**它自己的 UI 变量**，不是某个功能开关 ✗
    //     第一版我图省事绑了 `show_draw_Name` -> 界面上出现**两个"名字"开关** ✗
    //     同一个功能两个开关是最糟的（用户会以为开关坏了）✗
    if (Fold("文字行间距", &show_draw_line_gap, pal)) {
        ImGui::Indent(20.0f);
        {
            float v = g_esp.label_line_gap;
            if (Slider("llg", &v, -20.0f, 30.0f, "层间距 %.0f", pal))
                g_esp.label_line_gap = v;
        }
        ImGui::TextDisabled("名字/天赋/CD 三层之间；0=贴死，负数=重叠");
        //   ★ 2026-10-13 用户要求：名字往上移一点（离框太近）✓
        {
            float v = g_esp.name_lift;
            if (Slider("nlift", &v, 0.0f, 40.0f, "名字离框 %.0f px", pal))
                g_esp.name_lift = v;
        }
        ImGui::TextDisabled("只动名字与框的距离；天赋/CD 跟着一起上移");

        // ===================== 抗锯齿 =====================
        //   ★ 2026-10-13 用户问"抗锯齿怎么调整" ✓
        Rule(pal, Space::XS);
        Head("抗锯齿", pal, "AA");
        //   ① 文字亚像素定位 —— 针对"移动时名字晃" ✓
        RowSwitch("文字亚像素定位", &g_esp.text_subpixel, pal);
        ImGui::TextDisabled("移动时文字更平滑；关掉则对齐整像素、静止更锐");
        //   ② 曲线细分容差 ✓
        {
            float v = g_esp.curve_tol;
            if (Slider("ctol", &v, 0.3f, 2.0f, "曲线细分 %.2f", pal))
                g_esp.curve_tol = v;
        }
        ImGui::TextDisabled("越小圆角越顺滑（只影响圆/曲线，不吃字体图集）");
        //   ③ 字体过采样：**只能改代码 + 重启**，所以这里只给读数，不给控件 ✗
        //      （给控件会让人以为拖一下就能生效 ✗ 那是假的）
        ImGui::TextDisabled("字体过采样 H1/V1：需改代码重编，占用图集 16MB");
        ImGui::Unindent(20.0f);
        ImGui::Dummy(ImVec2(0, Space::S));
    }

    Rule(pal, Space::L);
    RowSwitch("显示灵魂",   &inform_ghost, pal);   // 死者的灵魂体(鬼魂态)；在模仿者局里也管用
        // ★ 小丑零件（推进器 / 风翼 / 钻头）—— 可展开；只画名字和距离，不画方框
    RowSwitch("小丑零件", &JokerParts::g_show, pal);
    if (JokerParts::g_show) {
        static bool s_jp_open = false;
        if (Fold("小丑零件设置", &s_jp_open, pal)) {
            { float v = JokerParts::g_range_m;
              if (Slider("jp_range", &v, 50.0f, 200.0f, "绘制距离 %.0f m", pal)) JokerParts::g_range_m = v; }
            { float v = JokerParts::g_font_size;
              if (Slider("jp_font", &v, 8.0f, 48.0f, "名字/距离字号 %.0f", pal)) JokerParts::g_font_size = v; }
            RowSwitch("绘制名字", &JokerParts::g_show_name, pal);
            RowSwitch("绘制距离", &JokerParts::g_show_dist, pal);
            ImGui::Dummy(ImVec2(0, Theme::Sp::S));
            for (int k = 0; k < 3; k++) {
                ImGui::AlignTextToFramePadding();
                char cid[32]; snprintf(cid, sizeof(cid), "##jp_c%d", k);
                float col[4] = {
                    ((JokerParts::g_col[k] >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
                    ((JokerParts::g_col[k] >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
                    ((JokerParts::g_col[k] >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
                    ((JokerParts::g_col[k] >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f,
                };
                if (ImGui::ColorEdit4(cid, col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaPreview))
                    JokerParts::g_col[k] = IM_COL32((int)(col[0]*255), (int)(col[1]*255), (int)(col[2]*255), (int)(col[3]*255));
                ImGui::SameLine();
                ImGui::TextUnformatted(JokerParts::g_label[k]);
                ImGui::SameLine();
                { const float av = ImGui::GetContentRegionAvail().x;
                  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + av - 46.0f); }
                char sid[32]; snprintf(sid, sizeof(sid), "##jp_s%d", k);
                Switch(sid, &JokerParts::g_enable[k], pal);
            }
            UI::FoldEnd(pal);
        }
    }
    // ★ 约瑟夫相机
    // ★ 约瑟夫相机（走对象图，只画名字和距离）
    // ★ 约瑟夫相机 —— Fold 自带那个开关就是功能开关，展开就是设置
    //   （不再另建"相机设置"折叠块：一个控件同时管开关和收展 ✓）
    if (Fold("绘制约瑟夫相机", &JosephCamera::g_show, pal)) {
        static bool s_jc_open_unused = false; (void)s_jc_open_unused;
        { float v = JosephCamera::g_range_m;
          if (Slider("jc_range", &v, 20.0f, 300.0f, "显示距离 %.0f m", pal)) JosephCamera::g_range_m = v; }
        { float v = JosephCamera::g_font_size;
          if (Slider("jc_font", &v, 8.0f, 48.0f, "名字/距离字号 %.0f", pal)) JosephCamera::g_font_size = v; }
        {
            ImGui::AlignTextToFramePadding();
            float c4[4] = {
                ((JosephCamera::g_col >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
                ((JosephCamera::g_col >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
                ((JosephCamera::g_col >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
                ((JosephCamera::g_col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f,
            };
            if (ImGui::ColorEdit4("##jc_col", c4, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaPreview))
                JosephCamera::g_col = IM_COL32((int)(c4[0]*255), (int)(c4[1]*255), (int)(c4[2]*255), (int)(c4[3]*255));
            ImGui::SameLine();
            ImGui::TextDisabled("名字/距离颜色");
        }
        // ★ 镜像框颜色（用户要求挪进这个折叠块）
        {
            ImGui::AlignTextToFramePadding();
            float mcol[4] = {
                ((g_mirror_col >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
                ((g_mirror_col >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
                ((g_mirror_col >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
                ((g_mirror_col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f,
            };
            if (ImGui::ColorEdit4("##mir_col", mcol, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaPreview))
                g_mirror_col = IM_COL32((int)(mcol[0]*255), (int)(mcol[1]*255), (int)(mcol[2]*255), (int)(mcol[3]*255));
            ImGui::SameLine();
            ImGui::TextDisabled("镜像框颜色");
        }
        UI::FoldEnd(pal);
    }
    // ★ 密码机进度悬浮窗（进度最多的在最上）
    RowSwitch("密码机悬浮窗", &g_gen_list_show, pal);
    if (g_gen_list_show) {
        { float v = g_gen_list_x;
          if (Slider("gl_x", &v, -3000.0f, 3000.0f, "X %.0f px", pal)) g_gen_list_x = v; }
        { float v = g_gen_list_y;
          if (Slider("gl_y", &v, -3000.0f, 3000.0f, "Y %.0f px", pal)) g_gen_list_y = v; }
        { float v = g_gen_size;
          if (Slider("gl_size", &v, 8.0f, 90.0f, "整体大小 %.0f", pal)) g_gen_size = v; }
        ImGui::TextDisabled("按进度降序；可直接拖动窗口");
    }
    // （2026-10-13：小丑自动松手已随「自动震慑」整体删除 ✓）
    //   ★ 2026-10-13：道具 / 密码机 从"只有开关"改成"开关 + 可展开样式" ✓
    //     用户要求：这两个的文字大小和颜色也要能调 ✗
    //     用 Fold 保持一致（和上面的 名字/距离/天赋/技能CD 同一套交互）✓
    /*   2026-10-25：**"绘制道具" 换成 "技能道具图标"**
     *   用户要求：删掉原来的「绘制道具」，改成带图标的可展开开关，
     *   并且能调 图片大小 / 文字大小 / 文字颜色。
     *   为了不动存盘结构，直接**复用** show_draw_Prop / prop_color / prop_size
     *   （它们本来就有 CfgPut/CfgGet，重启后能记住）。
     *   图片大小先用局部 static，确认效果后再加进存盘。
     */
    if (Fold("技能道具图标", &show_draw_Prop, pal)) {
        ImGui::Indent(20.0f);
        ColorRow("文字颜色", &g_esp.prop_color, pal);
        {
            float v = g_esp.prop_size;
            if (Slider("ic_tx", &v, 10.0f, 48.0f, "文字 %.0f", pal))
                g_esp.prop_size = v;
        }
        {
            float v = g_icon_px;
            if (Slider("ic_img", &v, 12.0f, 64.0f, "图片 %.0f", pal))
                g_icon_px = v;
            if (g_icon_px < 12.0f) g_icon_px = 12.0f;
        }
        //   ★ 2026-10-26：图标 / 文字各一对 XY 偏移（用户要求能自己挪到想要的位置）
        { float v = g_icon_dx;
          if (Slider("ic_dx",  &v, -200.0f, 200.0f, "图标 X %.0f", pal)) g_icon_dx = v; }
        { float v = g_icon_dy;
          if (Slider("ic_dy",  &v, -200.0f, 200.0f, "图标 Y %.0f", pal)) g_icon_dy = v; }
        { float v = g_txt_dx;
          if (Slider("ic_tdx", &v, -200.0f, 200.0f, "文字 X %.0f", pal)) g_txt_dx = v; }
        { float v = g_txt_dy;
          if (Slider("ic_tdy", &v, -200.0f, 200.0f, "文字 Y %.0f", pal)) g_txt_dy = v; }
        ImGui::Unindent(20.0f);
        ImGui::Dummy(ImVec2(0, Space::S));
    }
    RowSwitch("夫人模式",   &redqueenmod, pal);
    RowSwitch("绘制调试",   &Debugging, pal);

    if (Fold("显示密码机", &show_draw_secret_mechine, pal)) {
        ImGui::Indent(20.0f);
        ColorRow("文字颜色", &g_esp.gen_color, pal);
        {
            float v = g_esp.gen_size;
            if (Slider("gn_sz2", &v, 12.0f, 48.0f, "%.0f", pal))
                g_esp.gen_size = v;
        }
        //   ★ 进度条尺寸也做成可调（原来写死 120×10）✓
        {
            float v = g_esp.gen_bar_w;
            if (Slider("gn_bw", &v, 40.0f, 400.0f, "条宽 %.0f", pal))
                g_esp.gen_bar_w = v;
        }
        {
            float v = g_esp.gen_bar_h;
            if (Slider("gn_bh", &v, 4.0f, 40.0f, "条高 %.0f", pal))
                g_esp.gen_bar_h = v;
        }
        ColorRow("条底色", &g_esp.gen_bar_bg, pal);

        //   ★★ 2026-10-13 用户要求：把「加页」页的"最后一台"设置**搬进来** ★★
        //     原话：「把'加页'页里的'最后一台'设置写到绘制页的'显示密码机'**里面**」
        //     ⚠ 关键词是"里面" —— 所以它要嵌在「显示密码机」的展开内容里，
        //       而不是在它下面另起一个同级块 ✗（第一版就放错成同级了）
        //   为什么这样更合理：它本来就是**密码机破译进度**的一部分
        //     （已破译 4 台后，剩下那台单独用进度条标出来），
        //     放在「加页」页是**分类放错了** ✗
        //   ⚠ 配置键仍叫 `esp.last_gen_*`，**没改名** ✗
        //     改名会让老用户已调好的值全部丢失（CfgGet 匹配不上就回落默认）✗
        //     搬界面不动键名 —— 这是配置向前兼容的基本要求 ✓
        //
        //   ⚠ 嵌套 Fold 的行为说明：展开「显示密码机」后才会看到「最后一台」✗
        //     这是**符合用户要求**的（他说了"里面"）✓
        //     代价：想看"最后一台"的设置必须先展开父项 —— 可接受 ✓
        Rule(pal, Space::XS);
        if (Fold("最后一台", &show_draw_last_gen, pal)) {
            ImGui::Indent(20.0f);
            {
                float v = g_esp.last_gen_size;
                if (Slider("lg_sz", &v, 12.0f, 48.0f, "字号 %.0f", pal))
                    g_esp.last_gen_size = v;
            }
            {
                float v = g_esp.last_gen_w;
                if (Slider("lg_bw", &v, 40.0f, 400.0f, "条宽 %.0f", pal))
                    g_esp.last_gen_w = v;
            }
            {
                float v = g_esp.last_gen_h;
                if (Slider("lg_bh", &v, 4.0f, 40.0f, "条高 %.0f", pal))
                    g_esp.last_gen_h = v;
            }
            ImGui::Unindent(20.0f);
            ImGui::Dummy(ImVec2(0, Space::S));
        }

        ImGui::Unindent(20.0f);
        ImGui::Dummy(ImVec2(0, Space::S));
    }
    RowSwitch("显示大门",   &show_draw_Door, pal);
    //   ★★ 2026-10-13 用户要求：地窖加开关 + 可展开调名字/距离的字号与颜色 ★★
    //     原话：「地窖绘制在绘制页添加开关'显示地窖'，用户可以点击开关来控制
    //            显示隐藏，也可以展开'显示地窖'调整地窖名字和距离的大小与颜色」
    //   ⚠ 复用**已有**的 `show_draw_Cellar` ✗ 不另造变量
    //     它原来有存盘/读盘，但**界面上没有开关** -> 用户根本改不了 ✗
    //     补上这个 Fold 就等于把那条断掉的链路接通了 ✓
    if (Fold("显示地窖", &show_draw_Cellar, pal)) {
        ImGui::Indent(20.0f);
        ColorRow("名字/距离颜色", &g_esp.hatch_color, pal);
        {
            float v = g_esp.hatch_size;
            if (Slider("ht_sz", &v, 12.0f, 48.0f, "字号 %.0f", pal))
                g_esp.hatch_size = v;
        }
        ImGui::Unindent(20.0f);
        ImGui::Dummy(ImVec2(0, Space::S));
    }
    RowSwitch("宿伞模式",   &show_draw_umbrella, pal);

    // ===================== 预知 / 模仿者（互斥）=====================
    Head("身份推断", pal, "Identity");
    {
        //   ★★ 2026-10-13 用户要求：把「加页」页的"预知样式"**做成预知监管的收展** ★★
        //     原话：「把加页页的预知样式调整到绘制页的预知监管，做成预知监管的收展」
        //   所以原来那行 "预知监管 + 右侧 Switch" 换成一个 `Fold`：
        //     左边文字 = 预知监管，右边开关 = 同一个 Switch，
        //     点行本身 = 展开/收起 → 展开里就是样式 ✓
        //   （这正是 Fold 的设计用途，和 名字/距离/天赋/显示密码机 同一套交互 ✓）
        if (Fold("预知监管", &show_draw_prophet, pal)) {
            ImGui::Indent(20.0f);
            {
                float v = g_esp.prophet_size;
                if (Slider("ph_sz", &v, 12.0f, 48.0f, "字号 %.0f", pal))
                    g_esp.prophet_size = v;
            }
            ColorRow("预知监管颜色", &g_esp.prophet_color, pal);
            ImGui::Unindent(20.0f);
            ImGui::Dummy(ImVec2(0, Space::S));
        }

        // ★★ 2026-10-13 用户要求：**「模仿者模式」开关已删除，改成自动识别** ★★
        //   原话：「能否取消"模仿者模式"开关，自动识别模仿者对局还是正常对局」
        //   → 不再是开关，改成一行状态显示 ✓
        //   识别到模仿者局自动换「N号 身份」绘制，正常局照常绘制 ✓
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("对局类型");
        ImGui::SameLine();
        ImGui::TextColored(Theme::U32ToVec4(copycat_mode ? pal.accent : pal.textDim),
                           "%s", copycat_mode ? "模仿者局（自动识别）" : "正常对局");
    }

    if (copycat_mode) {
        PySelf::CopycatBook book = PySelf::copycat_book();
        int known = 0, with_id = 0;
        for (int k = 0; k < PySelf::MAX_COPYCAT; ++k)
            if (book.seat[k].known) { known++; if (book.seat[k].identity) with_id++; }
        ImGui::Dummy(ImVec2(0, Space::S));
        char hb[64];
        snprintf(hb, sizeof(hb), "%d / %d", with_id, known);
        Field("身份已知 / 已记录", hb, pal, pal.textDim);
        for (int k = 0; k < PySelf::MAX_COPYCAT; ++k) {
            const PySelf::CopycatSeat &st = book.seat[k];
            if (!st.known) continue;
            const char *nm = PySelf::copycat_identity_name(st.identity);
            char row[64];
            int len = snprintf(row, sizeof(row), "%2d 号", k + 1);
            if (nm) len += snprintf(row + len, sizeof(row) - len, "  %s", nm);
            else if (st.identity) len += snprintf(row + len, sizeof(row) - len, "  身份%d", st.identity);
            else len += snprintf(row + len, sizeof(row) - len, "  ?");
            if (book.self_seat == k + 1)
                len += snprintf(row + len, sizeof(row) - len, "  (我)");
            if (st.dead) snprintf(row + len, sizeof(row) - len, "  已死");
            // 颜色只在"有语义"时用：死亡=弱化，其余走正文色
            ImGui::PushStyleColor(ImGuiCol_Text,
                Theme::U32ToVec4(st.dead ? pal.textFaint : pal.text));
            ImGui::TextUnformatted(row);
            ImGui::PopStyleColor();
        }

        // ===================== 身份悬浮窗 =====================
        // 需求：把 1~12 号的号+身份贴在屏幕上(透明背景)，位置/大小/列数都在这调。
        // 用 Fold 做可展开分组，跟本页其它小节一致；实际绘制在 DrawCopycatWindow()。
        static bool ccwin_fold = false;
        // ★ 两个开关合成一个（2026-10-10 用户报"身份悬浮窗有两个开关，保留一个就行"）。
        //   原因：Fold 自带一个 Switch(绑 v)，而展开区里又放了一个 RowSwitch —— 两个开关
        //   控制的是同一件事。现在让 Fold 的开关直接绑真正的显示标志 g_ccwin_show，
        //   下面那个 RowSwitch 删除。ccwin_fold 只是 Fold 自己的展开态，不再需要外部变量。
        static bool ccwin_open_unused = false;
        if (Fold("身份悬浮窗", &g_ccwin_show, pal)) { (void)ccwin_open_unused;
            ImGui::Indent(20.0f);
            // (原"显示身份悬浮窗"开关已删除 —— 与 Fold 自带的那个重复)
            {
                float v = g_ccwin_x;
                if (Slider("ccwin_x", &v, -3000.0f, 3000.0f, "X %.0f px", pal))
                    g_ccwin_x = v;
            }
            {
                float v = g_ccwin_y;
                if (Slider("ccwin_y", &v, -3000.0f, 3000.0f, "Y %.0f px", pal))
                    g_ccwin_y = v;
            }
            {
                float v = g_ccwin_size;
                if (Slider("ccwin_size", &v, 8.0f, 90.0f, "整体大小 %.0f", pal))
                    g_ccwin_size = v;
            }
            {
                float v = g_ccwin_cols;
                if (Slider("ccwin_cols", &v, 1.0f, 3.0f, "一排 %.0f 个", pal))
                    g_ccwin_cols = v;
            }
            ImGui::TextDisabled("点住滑块左右拖；X/Y 给到 ±3000，推到画面外就看不见了");
            ImGui::Unindent(20.0f);
        }
    }

    // ===================== 底部动作 =====================
    //   （2026-10-10 用户要求：这两个按钮**移到「设置」页最上方** ——
    //     保存配置在左、退出程序在右，比例改成 高2:宽3，退出程序用纯色背景不描边。
    //     代码搬到了 Settings() 的开头。这里只留注释说明去向，
    //     免得以后有人在两处各找到一份、改了一处以为改好了。）
}

// ---------- 页 1：触摸 ----------
void QteAuto_DrawUI(Theme::Palette& pal) {
    using namespace FangUI;

    Head("自动校准", pal, "QTE");
    RowSwitch("启用自动校准", &show_draw_AutoQte, pal);
    ::QteAuto::g_on.store(show_draw_AutoQte);
    RowSwitch("显示触摸点", &::QteAuto::g_show_point, pal);


    {
        float v = ::QteAuto::g_x;
        if (Slider("aq_x", &v, 0.0f, (float)displayInfo.width, "X %.0f", pal)) ::QteAuto::g_x = v;
    }
    {
        float v = ::QteAuto::g_y;
        if (Slider("aq_y", &v, 0.0f, (float)displayInfo.height, "Y %.0f", pal)) ::QteAuto::g_y = v;
    }
    {
        float v = (float)::QteAuto::g_hold_ms;
        if (Slider("aq_hp", &v, 10.0f, 200.0f, "按住 %.0fms", pal)) ::QteAuto::g_hold_ms = (int)v;
    }
    {
        float v = (float)::QteAuto::g_delay_ms;
        if (Slider("aq_dl", &v, 0.0f, 120.0f, "点击延迟 %.0fms", pal)) ::QteAuto::g_delay_ms = (int)v;
    }
    if (ImGui::Button("测试点击")) {
        ::QteAuto::do_tap();
        snprintf(::QteAuto::g_status, sizeof(::QteAuto::g_status), "手动测试点击");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("[%s] %s", ::QteAuto::g_on.load() ? "开关已开" : "开关关着", ::QteAuto::g_status);
}

// 2026-10-26：给 AutoPallet 用的「台球形态」标志（见 AutoPallet.h 顶部说明）
void Touch(Theme::Palette& pal) {
    using namespace FangUI;

    /*   2026-10-26：**双样式**
     *   正常从别的页进来 -> 原来的"自动技能/自动盖板/..."子页
     *   **在这一页再点一次左侧「触摸」** -> 切成"调整触摸区域"
     *   （点同页的信号由 my_ui 的导轨按钮给出，见 g_same_page_click）
     */
    static bool s_adjust = false;
    if (MyUI::g_same_page_click == 1) { s_adjust = !s_adjust; MyUI::g_same_page_click = -1; }
    AutoSkill::g_touch_adjust_enabled = s_adjust;   // 只有调整模式才画框

    if (s_adjust) {
        Head("调整触摸区域", pal, "Touch Zones");
        ImGui::TextDisabled("再点一次左侧「触摸」回到功能页");
        ImGui::Dummy(ImVec2(0, 10));

        AutoSkill::DrawAdjustUI(pal);        // 技能（红）/ 飞轮（绿）

        /*   2026-10-26：其余模块的"触摸区显示 / 位置"也搬到这里。
         *   它们比 AutoSkill.h 后 include，所以只能写在 draw_Gui 这边。
         */
        if (Fold("自动盖板·判定范围", &AutoPallet::show_range, pal)) {
            ImGui::Indent(20.0f);
            ImGui::TextDisabled("地上的判定框（斜矩形），只在盖板范围里画");
            ImGui::Unindent(20.0f);
            ImGui::Dummy(ImVec2(0, Space::S));
        }
        if (Fold("自动盖板·触摸点", &AutoPallet::show_touch_point, pal)) {
            ImGui::Indent(20.0f);
            { float v = AutoPallet::touch_x;
              if (Slider("za_ap_x", &v, 0.0f, 3000.0f, "X %.0f", pal)) AutoPallet::touch_x = v; }
            { float v = AutoPallet::touch_y;
              if (Slider("za_ap_y", &v, 0.0f, 1600.0f, "Y %.0f", pal)) AutoPallet::touch_y = v; }
            ImGui::Unindent(20.0f);
            ImGui::Dummy(ImVec2(0, Space::S));
        }
        if (Fold("防牛仔·触摸区", &AntiCowboy::g_show_rect, pal)) {
            ImGui::Indent(20.0f);
            ImGui::TextDisabled("放人按键的触摸范围（开「防牛仔」才画）");
            ImGui::Unindent(20.0f);
            ImGui::Dummy(ImVec2(0, Space::S));
        }
        if (Fold("自动校准·触摸点", &QteAuto::g_show_point, pal)) {
            ImGui::Indent(20.0f);
            { float v = QteAuto::g_x;
              if (Slider("za_qe_x", &v, 0.0f, 3000.0f, "X %.0f", pal)) QteAuto::g_x = v; }
            { float v = QteAuto::g_y;
              if (Slider("za_qe_y", &v, 0.0f, 1600.0f, "Y %.0f", pal)) QteAuto::g_y = v; }
            ImGui::Unindent(20.0f);
            ImGui::Dummy(ImVec2(0, Space::S));
        }
        return;
    }


    // ---- 顶部切换：一整块底在三个选项之间滑动 ----
    //   之前是三个各自亮灭的按钮，切换是硬切、还各占一个圆角，
    //   看着像三个不相干的东西。现在是一块会滑的指示器，
    //   文字按"离指示器多远"交叉淡入 —— 和左侧导轨用的是同一套手感。
    static int g_touch_sub = 0;
    static const char* sub_items[] = { "自动技能", "自动盖板", "自动坠机", "防牛仔", "自动校准" };
{
    int ns = Segmented("touch_sub", sub_items, 5, g_touch_sub, pal, 48.0f);
    if (ns != g_touch_sub) g_touch_sub = ns;
}
    ImGui::Dummy(ImVec2(0, 14));


    if (g_touch_sub == 0) {
    AutoSkill::DrawUI(pal);
}

    else if (g_touch_sub == 1) {
    AutoPallet::DrawUI(pal);
}
    
    else if (g_touch_sub == 2) {
    AutoCrash::DrawUI(pal);
}

    else if (g_touch_sub == 3) {
    AntiCowboy::DrawUI(pal);
}

    else if (g_touch_sub == 4) {
    QteAuto_DrawUI(pal);
}

}

// ---------- 页 4：加页（摸金模式） ----------
void Add(Theme::Palette& pal) {
    using namespace FangUI;

    Head("小地图", pal, "Minimap");
    RowSwitch("显示小地图",   &g_minimap_enabled,   pal);
    RowSwitch("调整位置/大小", &g_minimap_edit,      pal);
    RowSwitch("显示队友号码", &g_minimap_show_name, pal);
    {
        float v = g_minimap_w;
        if (Slider("mm_size", &v, 100.0f, 800.0f, "%.0f px", pal)) {
            g_minimap_w = v;
            g_minimap_h = v;
        }
    }
    {
        float v = g_minimap_zoom;
        if (Slider("mm_zoom", &v, 0.05f, 3.0f, "%.2f", pal))
            g_minimap_zoom = v;
    }
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.0f, %.0f", g_minimap_x, g_minimap_y);
        Field("位置", buf, pal, pal.textDim);
        Field("模块状态", PyMiniMap::status_text(), pal, pal.textDim);
    }

    Head("宝箱 / 怪物", pal, "Marks");
    RowSwitch("显示宝箱/怪物", &g_mark_enabled, pal);
    {
        float v = g_mark_max_dist;
        if (Slider("mark_dist", &v, 20.0f, 300.0f, "%.0f m", pal))
            g_mark_max_dist = v;
    }
    ColorRow("宝箱颜色", &g_mark_col_chest, pal);
    ColorRow("怪物颜色", &g_mark_col_monster, pal);
    //   ★ 2026-10-13：补字号（原来写死 25）✓
    //     颜色本来就有上面两个（宝箱/怪物分开），所以这里只加尺寸 ——
    //     再加一个"统一颜色"只会让人困惑哪个生效 ✗
    {
        float v = g_esp.mark_size;
        if (Slider("mark_sz", &v, 12.0f, 48.0f, "%.0f", pal))
            g_esp.mark_size = v;
    }

    //   ★★ 2026-10-13 用户要求：把这一整段"预知样式"**搬去绘制页的「预知监管」** ★★
    //     原话：「把加页页的预知样式调整到绘制页的预知监管，做成预知监管的收展」
    //     -> 已做成 `Fold("预知监管", ...)`（见绘制页的"身份推断"段）✓
    //   ⚠ 这里**整段必须删掉** ✗
    //     留着会有两处同名控件（`ph_sz` + 两个 ColorRow）：
    //     ImGui 的控件 ID 会冲突 -> 调一个另一个跟着动，或者根本调不动 ✗
    //   ⚠ 配置键**没改名**（仍是 esp.prophet_size / prophet_color / prep_color）✓
    //     所以用户已调好的值照常生效 ✓

    //   ★ 2026-10-13 用户要求：**"最后一台"的设置搬去绘制页的「显示密码机」里** ✓
    //     原话：「把'加页'页里的'最后一台'设置写到绘制页的'显示密码机'里面」
    //     理由也很自然：它本来就是密码机的一部分（破译进度提示），
    //     放在「加页」页属于**分类放错了** ✗
    //   ⚠ 这里只删界面，`g_esp.last_gen_*` 字段和 CfgPut/CfgGet 都**不动** ✗
    //     配置键保持原名 -> 老配置照常能读，不会丢用户已调的值 ✓

    Head("队员状态", pal, "Team");
    if (PyMiniMap::in_goldrush()) {
        int rn = PyMiniMap::team_count();
        char hb[16]; snprintf(hb, sizeof(hb), "%d", rn);
        Field("人数", hb, pal, pal.textDim);
        for (int i = 0; i < rn; i++) {
            PyMiniMap::TeamMember tm;
            if (!PyMiniMap::get_member(i, tm)) continue;
            char lbl[32], val[48];
            snprintf(lbl, sizeof(lbl), "#%lld", (long long)(tm.uid - 1000000));
            snprintf(val, sizeof(val), "v%d  sc%d", tm.valid ? 1 : 0, tm.has_scene ? 1 : 0);
            Field(lbl, val, pal, tm.valid ? pal.text : pal.textFaint);
        }
    } else {
        ImGui::TextDisabled("(不在摸金局)");
        ImGui::Dummy(ImVec2(0, Space::S));
    }
}

// ============================================================================
// ★★ 2026-10-16：读一个单位"当前正在使用的技能" ★★
//   给「设置 → 技能CD」区顶部显示用。
//
//   数据来源（都用工程里验证过的读取方式，没猜偏移）：
//     · unit.skill_mgr.last_cast_skill_id  —— 游戏在**施放技能那一刻**写它 ✓
//     · unit.state_machine._state 列表末尾  —— 当前状态机状态 id（辅助信息）✓
//
//   ⚠ last_cast_skill_id 是**残留值**：技能用完后游戏**不会**把它清 0 ✓
//     所以我们需要"上一次 id 变化到现在多久"来判断是不是"正在使用" ✗
//     判据：3 秒内 id 变过 = 正在使用；超过 3 秒 = 已结束 ✓
//
//   ⚠ 缓存用 (unit, id) 做 key：
//     · 不同角色用同一个技能 id 时**不能串**（比如两个监管都按 3015）✗
//     · 单条缓存即可 —— 用户一次只看一个角色 ✓
// ============================================================================
struct SkcdUsingSkill {
    int64_t  id      = 0;    // 0 = 读不到
    int64_t  state   = -1;   // -1 = 读不到
    uint64_t seen_ms = 0;    // 上一次观察到 id 变化的时刻
};

inline uint64_t skcd_now_ms() {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

inline bool skcd_read_using(uint64_t unit, SkcdUsingSkill &out) {
    static uint64_t s_unit = 0;
    static int64_t  s_id   = 0;
    static uint64_t s_ms   = 0;

    out = SkcdUsingSkill{};
    if (!PyCore::is_obj(unit)) return false;
    const uint64_t d = PyCore::get_inst_dict(unit);
    if (!d) return false;
    PyCore::Dict dk;
    if (!PyCore::read_dict(d, dk)) return false;

    // ---- skill_mgr.last_cast_skill_id ----
    const int64_t ism = PyCore::resolve_index(d, dk, -1, "skill_mgr");
    if (ism >= 0) {
        const uint64_t sm = getPtr64(PyCore::value_slot(dk, ism));
        if (PyCore::is_obj(sm)) {
            const uint64_t smd = PyCore::get_inst_dict(sm);
            if (smd) {
                PyCore::Dict smk;
                if (PyCore::read_dict(smd, smk)) {
                    const int64_t ilc = PyCore::resolve_index(smd, smk, -1, "last_cast_skill_id");
                    if (ilc >= 0) {
                        int64_t v = 0;
                        if (PyCore::read_int(getPtr64(PyCore::value_slot(smk, ilc)), v))
                            out.id = v;
                    }
                }
            }
        }
    }

    // ---- state_machine._state 列表末尾（辅助信息）----
    const int64_t ism2 = PyCore::resolve_index(d, dk, -1, "state_machine");
    if (ism2 >= 0) {
        const uint64_t sm2 = getPtr64(PyCore::value_slot(dk, ism2));
        if (PyCore::is_obj(sm2)) {
            const uint64_t sm2d = PyCore::get_inst_dict(sm2);
            if (sm2d) {
                PyCore::Dict sm2k;
                if (PyCore::read_dict(sm2d, sm2k)) {
                    const int64_t ist = PyCore::resolve_index(sm2d, sm2k, -1, "_state");
                    if (ist >= 0) {
                        const uint64_t lst = getPtr64(PyCore::value_slot(sm2k, ist));
                        int64_t n = 0; uint64_t items = 0;
                        if (PyCore::read_list(lst, n, items) && n > 0) {
                            int64_t v = 0;
                            if (PyCore::read_int(getPtr64(items + (n - 1) * 8), v))
                                out.state = v;
                        }
                    }
                }
            }
        }
    }

    // ---- 时间戳：unit 换了 或 id 变了 -> 记当前时刻 ✓ ----
    if (unit != s_unit || out.id != s_id) {
        s_unit = unit;
        s_id   = out.id;
        s_ms   = skcd_now_ms();
    }
    out.seen_ms = s_ms;
    return true;
}

// ---------- 页 2：设置 ----------
// ============================================================================
//   2026-10-26：**截图 / 录屏**（设置页按钮）
// ----------------------------------------------------------------------------
//   为什么自己做：本层在 Android 12+ 建 surface 时设了 setTrustedOverlay(true)，
//   **虚拟显示（系统录屏 / MediaProjection）拍不到它** —— 录出来只有游戏画面。
//   root 的 `screencap` 走的是 captureDisplay，能拍到整屏（游戏 + 本层）。
//   本进程本来就是 root，所以直接 fork/exec 系统二进制，比自己写编码器省事得多。
//
//   trusted overlay 现在默认已经关掉（见 ANativeWindowCreator.h），
//   所以「系统自带录屏」也能拍到本层了；这两个按钮是更省事的一条路。
// ============================================================================
#include <sys/wait.h>
#include <signal.h>
#include <sys/stat.h>

static bool  g_rec_on   = false;            // 录屏开关（UI 用）
static pid_t g_rec_pid  = -1;               // screenrecord 的 pid
static char  g_cap_msg[192] = "";           // 状态行
static char g_rec_last[192] = "";     // 最后一段录像路径（停止后扫进相册）
/*   2026-10-26：**直接进相册**
 *   放 DCIM 下（相册的"图片/视频"就是扫 DCIM + Movies + Pictures），
 *   拍完再发一次 MEDIA_SCANNER_SCAN_FILE 让相册立刻能看到。
 */
static const char *kShotDir = "/sdcard/DCIM/Screenshots";
static const char *kRecDir  = "/sdcard/DCIM/ScreenRecorder";

// 把刚写的文件塞进媒体库（相册马上可见）。异步，不阻塞绘制线程。
static void cap_close_fds();     // 2026-10-26：前置声明（定义在后面，子进程 exec 前关 fd）

static void cap_mediascan(const char *path)
{
    char uri[256];
    if (!path || !*path) return;
    snprintf(uri, sizeof(uri), "file://%s", path);
    pid_t p = fork();
    if (p == 0) {
        int fd = open("/dev/null", O_WRONLY);
        if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); }
        cap_close_fds();
        execl("/system/bin/am", "am", "broadcast",
              "-a", "android.intent.action.MEDIA_SCANNER_SCAN_FILE",
              "-d", uri, (char *)nullptr);
        _exit(127);
    }
}

/*   2026-10-26：**系统录屏入口**
 *   我们自己的按钮走 screenrecord —— 它天生没有音轨（AOSP v1.4 无音频选项）。
 *   要内录（游戏声音）只能用 MIUI 的录屏（MediaProjection + AudioPlaybackCapture）。
 *   它没有可用的启动广播，隐藏的 RecorderService 也点不起来，所以这里退一步：
 *   直接把快捷开关面板拉下来，用户点一下【屏幕录制】图块即可。
 *   好处：不旋转屏幕、不打断游戏。
 */
static void cap_open_qs()
{
    pid_t p = fork();
    if (p == 0) {
        int fd = open("/dev/null", O_WRONLY);
        if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); }
        execl("/system/bin/cmd", "cmd", "statusbar", "expand-settings", (char *)nullptr);
        _exit(127);
    }
    snprintf(g_cap_msg, sizeof(g_cap_msg),
             "已拉下快捷开关：点【屏幕录制】图块（可开内录，能拍到本层）");
}

/*   2026-10-26：**重新加载悬浮层**
 *   把当前进程干掉，再由一个脱离会话的 sh 重新拉起 you.dump.sh 的明文流程：
 *   重新找 com.netease.dwrg 的 pid、重新开驱动、重新扫 game_kernel……
 *   等价于手动"杀掉再启动"，但不用拔线。
 */
/*   2026-10-26：**fork 之后必须把多余 fd 关掉**
 *   事故现场：点「开始录屏」后 screenrecord 继承了我们监听 19790 的 socket，
 *   `netstat` 里 19790 的 LISTEN 变成了 screenrecord 的 pid ——
 *   叠加层重启后绑不上端口，MCP 全哑、触摸处理也一起卡死（连游戏都点不动）。
 *   所以每个 execl 之前都先 close 3..255。
 */
static void cap_close_fds()
{
    for (int fd = 3; fd < 256; ++fd) close(fd);
}

// 统一的"重启自己"实现（按钮和看门狗共用）
static void restart_overlay_self()
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "sleep 0.3; kill -TERM %d 2>/dev/null; sleep 1.2; "
             "printf '1\n' | /data/adb/1/you.dump.sh > /data/local/tmp/x.out 2>&1",
             (int)getpid());
    pid_t p = fork();
    if (p == 0) {
        setsid();
        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) { dup2(fd, 0); dup2(fd, 1); dup2(fd, 2); }
        cap_close_fds();
        execl("/system/bin/sh", "sh", "-c", cmd, (char *)nullptr);
        _exit(127);
    }
}

static void reload_overlay()
{
    /*   2026-10-26：重新加载 = 重启本进程（原地重连驱动做不到，已确认过三次） */
    restart_overlay_self();
    snprintf(g_cap_msg, sizeof(g_cap_msg), "正在重新加载（1~2 秒后恢复）");
}

/*   2026-10-26：**游戏换进程就自动重启辅助**
 *   每 3 秒看一次 com.netease.dwrg 的 pid（或 dwrg），
 *   一旦和启动时绑定的不同（= 你退了游戏又重进），自动重启本进程，
 *   不用手动点按钮。
 */
static void start_pid_watchdog()
{
    static bool s_started = false;
    if (s_started) return;
    s_started = true;
    const int bound = (int)pid;
    std::thread([bound]() {
        while (true) {
            sleep(3);
            int cur = get_name_pid1("dwrg");
            if (cur > 0 && cur != bound) {
                restart_overlay_self();
                for (;;) sleep(60);     // 等被 kill 掉
            }
        }
    }).detach();
}
static void reload_overlay_old()
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "sleep 0.3; kill -TERM %d 2>/dev/null; sleep 1.2; "
             "printf '1\\n' | /data/adb/1/you.dump.sh > /data/local/tmp/x.out 2>&1",
             (int)getpid());
    pid_t p = fork();
    if (p == 0) {
        setsid();                                   // 脱离本会话，别跟着一起死
        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) { dup2(fd, 0); dup2(fd, 1); dup2(fd, 2); }
        cap_close_fds();                            // ★ 重启用的 sh 也不能继承监听 socket
        execl("/system/bin/sh", "sh", "-c", cmd, (char *)nullptr);
        _exit(127);
    }
    if (p > 0) snprintf(g_cap_msg, sizeof(g_cap_msg), "正在重新加载（1~2 秒后恢复）");
}

static void take_shot()
{
    mkdir(kShotDir, 0775);
    char path[192];
    time_t tt = time(nullptr);
    struct tm tv; localtime_r(&tt, &tv);
    snprintf(path, sizeof(path), "%s/shot_%04d%02d%02d_%02d%02d%02d.png",
             kShotDir, tv.tm_year + 1900, tv.tm_mon + 1, tv.tm_mday,
             tv.tm_hour, tv.tm_min, tv.tm_sec);
    pid_t p = fork();
    if (p == 0) {
        cap_close_fds();
        execl("/system/bin/screencap", "screencap", "-p", path, (char *)nullptr);
        _exit(127);
    }
    if (p > 0) {
        snprintf(g_cap_msg, sizeof(g_cap_msg), "截图已存: %s", path);
        cap_mediascan(path);
    } else {
        snprintf(g_cap_msg, sizeof(g_cap_msg), "截图失败: fork 出错");
    }
}

static void start_rec()
{
    if (g_rec_pid > 0) return;
    /*   2026-10-26：**去掉这里的 pkill**
     *   它是一个并发的子进程，和我们刚起的 screenrecord 抢时序 ——
     *   有时会把新起的那个一起杀掉，表现就是"按钮闪一下、没有文件"。
     *   残留的旧进程由 cap_tick 的 waitpid 和重启流程处理。
     */
    mkdir(kRecDir, 0775);
    char path[192];
    time_t tt = time(nullptr);
    struct tm tv; localtime_r(&tt, &tv);
    snprintf(path, sizeof(path), "%s/rec_%04d%02d%02d_%02d%02d%02d.mp4",
             kRecDir, tv.tm_year + 1900, tv.tm_mon + 1, tv.tm_mday,
             tv.tm_hour, tv.tm_min, tv.tm_sec);
    pid_t p = fork();
    if (p == 0) {
        cap_close_fds();          // ★ 别把 MCP 的监听 socket 传给 screenrecord
        execl("/system/bin/screenrecord", "screenrecord",
              "--bit-rate", "8000000", path, (char *)nullptr);
        _exit(127);
    }
    if (p > 0) {
        g_rec_pid = p;
        g_rec_on  = true;
        snprintf(g_rec_last, sizeof(g_rec_last), "%s", path);
        snprintf(g_cap_msg, sizeof(g_cap_msg), "录屏中: %s", path);
    } else {
        snprintf(g_cap_msg, sizeof(g_cap_msg), "录屏失败: fork 出错");
    }
}

static void stop_rec()
{
    if (g_rec_pid > 0) {
        kill(g_rec_pid, SIGINT);   // screenrecord 收到 SIGINT 会把 moov 写完再退
        snprintf(g_cap_msg, sizeof(g_cap_msg), "录屏已停止（写盘 1~2 秒）");
    }
    g_rec_pid = -1;
    g_rec_on  = false;
}

// 每帧一次：收掉结束的 screenrecord，不留僵尸；顺手把录像塞进媒体库
static void cap_tick()
{
    if (g_rec_pid > 0) {
        int st = 0;
        if (waitpid(g_rec_pid, &st, WNOHANG) == g_rec_pid) {
            g_rec_pid = -1;
            g_rec_on  = false;
            if (g_rec_last[0]) cap_mediascan(g_rec_last);
        }
    }
}

void Settings(Theme::Palette& pal) {
    std::lock_guard<std::recursive_mutex> data_lock(g_data_mutex);
    using namespace FangUI;

    // ================= 顶部动作（2026-10-10 从「绘制」页底部搬来）=================
    //   用户要求：放在设置页**最上方**、两个并排、保存配置在左、退出程序在右，
    //   比例 **高2 : 宽3**，退出程序**纯色背景、不描边**。
    {
        const float avail2 = ImGui::GetContentRegionAvail().x;
        // ★ 尺寸与排布（2026-10-10 第二次修）
        //   第一版按"各自半宽 + 3:2"算 -> 每个 420px 宽、280px 高，**巨大且被裁掉**。
        //   错在我把比例套到了"半宽"上。用户原意是**两个按钮分列左右两端**：
        //     保存配置在最左、退出程序在最右，中间留空。
        //   所以改成固定尺寸的按钮卡：宽3 高2（w=132 -> h=88），
        //   左按钮贴左边缘，右按钮贴右边缘 —— 与"最左 / 最右"的要求一致。
        const float bw = 132.0f;
        const float bh = bw * (2.0f / 3.0f);          // 宽3 : 高2
        // ⚠ 用**屏幕坐标直接摆**，不要用 SameLine + SetCursorPosX 累加：
        //   SameLine 之后的 GetCursorPosX() 已经是第一个按钮的右侧，再加一行宽就越界，
        //   按钮会被推到面板外看不见（第一版就是这样，"退出程序"直接消失了）。
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(p0);
        if (Button("保存配置", ImVec2(bw, bh), pal, true)) {
            SaveConfig();
            PyDump::reset_status("配置已保存");
        }
        /*   2026-10-26：**重新加载** —— 紧贴在「保存配置」右边
         *   直接把本进程重启：重新找游戏进程、重新开驱动、重新扫 Python 对象。
         *   起一个脱离会话的 sh，等我们退出后 1 秒再拉起来。
         */
        ImGui::SameLine(0.0f, 8.0f);
        if (Button("重新加载", ImVec2(bw, bh), pal, false))   // 2026-10-26：和「保存配置」一样大
            reload_overlay();
        // 右按钮贴右边缘
        ImGui::SetCursorScreenPos(ImVec2(p0.x + avail2 - bw, p0.y));
        // "退出程序"是不可逆操作 —— danger 实色、无描边，一眼和普通按钮区分开
        if (Button("退出程序", ImVec2(bw, bh), pal, false, true)) {
            ForceExit();
        }
        ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + bh));
        ImGui::Dummy(ImVec2(0, Space::L));
    }

    // ---- 2026-10-26：截图 / 录屏 ----
    Head("截图 / 录屏", pal, "Capture");
    {
        cap_tick();
        const float bw2 = 132.0f;
        const float bh2 = bw2 * (2.0f / 3.0f);
        if (Button("截图", ImVec2(bw2, bh2), pal, true)) take_shot();
        ImGui::SameLine();
        if (g_rec_on) {
            if (Button("停止录屏", ImVec2(bw2, bh2), pal, false, true)) stop_rec();
        } else {
            if (Button("开始录屏", ImVec2(bw2, bh2), pal, false)) start_rec();
        }

        if (g_cap_msg[0]) ImGui::TextDisabled("%s", g_cap_msg);
        ImGui::Dummy(ImVec2(0, Space::S));
    }

    // ---- 外观：主题色 / 模糊强度（都是实时生效）----
    Head("外观", pal, "Appearance");

    // 主题强调色。0 表示用主题自带色，所以这里用一个独立变量中转 ——
    // 取色块不能直接绑 g_theme_accent，否则用户一拖就把"默认"弄丢了。
    {
        static ImU32 s_accent = 0;
        static bool  s_init   = false;
        // 没设过自定义色时，色块显示**当前主题实际在用的强调色** ——
        // 直接绑 g_theme_accent(0) 的话色块是个黑框，看不出当前是什么颜色。
        if (!s_init) { s_init = true; s_accent = g_theme_accent ? (ImU32)g_theme_accent : pal.accent; }

        if (ColorRow("主题颜色", &s_accent, pal))
            g_theme_accent = (int)s_accent;

        // (2026-10-10 用户要求删除"恢复默认强调色"按钮：
        //  主题色已经能直接在色块上改，再放一个"恢复默认"是多余的一步。)
        if (!g_theme_accent)
            ImGui::TextDisabled("当前用主题自带色（深色=琥珀，浅色=深琥珀）");
    }
    {
        float b = (float)g_blur_strength;
        if (Slider("blur_str", &b, 0.0f, 300.0f, "%.0f", pal))
            g_blur_strength = (int)(b + 0.5f);
    }
    // (2026-10-10 用户要求删除：深色/浅色切换由**左侧导轨上的那个开关**负责，
    //  设置页里不必再放一个 —— 两处控制同一个状态，容易让人以为改的是两个东西。)
    // RowSwitch("深色主题", &g_dark_theme, pal);

    static float g_target_fps_f = 60.0f;
    static MemUsage mem;
    static double   mem_time = -10.0;
    if (ImGui::GetTime() - mem_time >= 1.0) { mem = read_memory_usage(); mem_time = ImGui::GetTime(); }

    // 帧率时序（**真实数据**，不是拿 sparkline 凑版面）
    static float fps_hist[64] = {0};
    static int   fps_head = 0;
    {
        static double fps_last = 0.0;
        double now = ImGui::GetTime();
        if (now - fps_last >= 0.1) {
            fps_last = now;
            fps_hist[fps_head] = ImGui::GetIO().Framerate;
            fps_head = (fps_head + 1) % 64;
        }
    }
    float fps_seq[64];
    for (int i = 0; i < 64; i++) fps_seq[i] = fps_hist[(fps_head + i) % 64];
    float fps_max = 1.0f;
    for (int i = 0; i < 64; i++) if (fps_seq[i] > fps_max) fps_max = fps_seq[i];

    ImGui::Dummy(ImVec2(0, Space::S));

    // ================= 运行 =================
    GroupBegin("g_run");
    {
        char buf[48];
        // 大号数值：不套卡片、不配小标签模板，就是一行读数
        snprintf(buf, sizeof(buf), "%.0f", ImGui::GetIO().Framerate);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImFont* df = DisplayFont();
        float big = zh_display ? 46.0f : ImGui::GetFontSize() * 1.7f;
        dl->AddText(df, big, pos, pal.textHi, buf);
        ImVec2 vs = df->CalcTextSizeA(big, FLT_MAX, 0.0f, buf);
        ImFont* f = ImGui::GetFont();
        float sf = ImGui::GetFontSize() * 0.86f;
        dl->AddText(f, sf, ImVec2(pos.x + vs.x + Space::S, pos.y + big - sf - 6.0f),
                    pal.textFaint, "帧 / 秒");
        float avail = ImGui::GetContentRegionAvail().x;
        snprintf(buf, sizeof(buf), "%.1f MB", mem.total_kb() / 1024.0f);
        ImVec2 ms = f->CalcTextSizeA(sf, FLT_MAX, 0.0f, buf);
        dl->AddText(f, sf, ImVec2(pos.x + avail - ms.x, pos.y + big - sf - 6.0f),
                    pal.textDim, buf);
        ImGui::Dummy(ImVec2(avail, big + Space::S));
    }
    Sparkline("fps_spark", fps_seq, 64, 0.0f, fps_max * 1.15f, pal, 46.0f, pal.accent);
    ImGui::Dummy(ImVec2(0, Space::S));
    if (Fader("fps", &g_target_fps_f, 60.0f, 144.0f, "上限 %.0f", pal))
        ::fps = g_target_fps_f;
    {
        char buf[128];
        if (mem.gpu_ok)
            snprintf(buf, sizeof(buf), "%.1f / %.1f / %.1f / %.1f",
                     mem.pss_kb/1024.0f, mem.swap_kb/1024.0f,
                     mem.gpu_kb/1024.0f, mem.buffer_kb/1024.0f);
        else
            snprintf(buf, sizeof(buf), "%.1f / %.1f / %.1f",
                     mem.pss_kb/1024.0f, mem.swap_kb/1024.0f, mem.buffer_kb/1024.0f);
        Field("进程 / 换出 / 显存 / 缓冲", buf, pal, pal.textDim);
    }
    GroupEnd(pal);

    // ================= 连接 =================
    GroupBegin("g_link");
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 lp = ImGui::GetCursorScreenPos();
        ImU32 sc; const char* stxt;
        if (read_state == 2)      { sc = pal.ok;        stxt = "已获取到游戏数据"; }
        else if (read_state == 1) { sc = pal.warn;      stxt = "正在获取游戏数据"; }
        else                      { sc = pal.textFaint; stxt = "未连接"; }
        float fs = ImGui::GetFontSize();
        StatusDot(ImVec2(lp.x + 5.0f, lp.y + fs * 0.5f), 5.0f, sc, read_state == 1);
        dl->AddText(ImVec2(lp.x + 18.0f, lp.y), sc, stxt);
        ImGui::Dummy(ImVec2(0, fs + Space::S));
        char buf[64];
        snprintf(buf, sizeof(buf), "%d", pid);
        Field("游戏进程", buf, pal);
        snprintf(buf, sizeof(buf), "0x%lx", (unsigned long)libbase);
        Field("模块入口", buf, pal, pal.textDim);
        snprintf(buf, sizeof(buf), "%d", GetPublishedDataCount());
        Field("实体数量", buf, pal, pal.textDim);
    }
    GroupEnd(pal);

    // ================= 角色 =================
    GroupBegin("g_role");
    {
        std::string self = current_self_name();
        int camp = PySelf::camp();
        if (self.empty()) {
            ImGui::TextDisabled("(未识别)");
            ImGui::Dummy(ImVec2(0, Space::S));
        } else {
            TextDisplay(self.c_str(), pal.textHi, 0.86f);
            ImGui::Dummy(ImVec2(0, Space::XS));
            ImGui::PushStyleColor(ImGuiCol_Text,
                Theme::U32ToVec4(camp == 1 ? pal.danger : pal.ok));
            ImGui::TextUnformatted(camp == 1 ? "监管者" : "求生者");
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, Space::S));
        }
        if (camp == 1) {
            std::string h = AutoSkill::current_hunter_name();
            if (!h.empty()) Field("当前监管", h.c_str(), pal, pal.textDim);
        }
        ImGui::TextWrapped("密码机 %s（已破译 %d）", PyProgress::status_text(), PyProgress::decoded_count());
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::U32ToVec4(pal.textDim));
        ImGui::TextWrapped("天赋 %s", PyGenius::status_text());
        ImGui::TextWrapped("自身 %s", PySelf::status_text());
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, Space::S));
    }
    GroupEnd(pal);

    #if SHOW_DUMP_PAGE
    GroupBegin("g_skcd");
    {
        // ============================================================================
        // ★★ 2026-10-16 新增：**角色选择器** —— 让技能CD区能看任意角色的技能 ★★
        //   用户需求：在最上方显示一个角色选择，选中后技能ID显示该角色的，
        //             而不是固定显示成监管的
        //
        //   数据源：直接复用**「改视角」页那份列表** ViewSwitch::g_t ✓
        //     ⚠ 不要自己再扫一遍 ✗（两份数据源迟早不一致；
        //       且「改视角」那份已经做过 uid 兜底、排序）
        //
        //   选中项记 **uid**，不记下标 ——
        //     列表排序会变（模仿者局按号排 / 正常局按距离排），
        //     记下标会在排序切换时漂移 ✗
        //
        //   默认「当前监管」= 保持原行为 ✓
        // ============================================================================
        static int      s_skcd_is_hunter = 1;   // 1=监管 0=具体角色
        static unsigned s_skcd_uid       = 0;   // 非监管时使用：目标 uid

        // 找到当前选中项对应的名字和 unit 对象
        const char* sel_name = "当前监管";
        uint64_t    sel_unit = 0;
        if (s_skcd_is_hunter) {
            sel_unit = PyAttack::hunter_unit_raw();
        } else {
            sel_name = "(已失效)";
            for (int i = 0; i < ViewSwitch::g_n; i++) {
                if (ViewSwitch::g_t[i].uid == s_skcd_uid) {
                    sel_name = ViewSwitch::g_t[i].name;
                    sel_unit = ViewSwitch::g_t[i].unit;
                    break;
                }
            }
        }

        // ---- 下拉 ----
        //   ⚠ 用 ImGui::BeginCombo 而不是 FangUI::Segmented ——
        //     候选数量不定（1~十几个），Segmented 铺不开 ✗
        //   ⚠ BeginCombo 的第一个参数是 **id 标签**，预览文本是第二个 ✓
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("显示角色");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##skcd_sel", sel_name)) {
            if (ImGui::Selectable("当前监管", s_skcd_is_hunter != 0)) {
                s_skcd_is_hunter = 1;
                s_skcd_uid = 0;
            }
            for (int i = 0; i < ViewSwitch::g_n; i++) {
                const ViewSwitch::Target &t = ViewSwitch::g_t[i];
                char buf[96];
                snprintf(buf, sizeof(buf), "%s  %.1fm%s",
                         t.name,
                         t.dist_m < 0.f ? 0.f : t.dist_m,
                         t.dead ? "  已死" : "");
                const bool sel = (!s_skcd_is_hunter && s_skcd_uid == t.uid);
                //   ⚠ PushID 必须包住 —— 模仿者局里会有多个 name 是 "?" 的项，
                //     不带 ID 时 ImGuiID 会撞车，点第一个后面的全跟着高亮 ✗
                ImGui::PushID(i);
                if (ImGui::Selectable(buf, sel)) {
                    s_skcd_is_hunter = 0;
                    s_skcd_uid = t.uid;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (!s_skcd_is_hunter && sel_unit == 0) {
            ImGui::TextDisabled("（该角色已离场，暂时回退到监管）");
        }
        ImGui::Dummy(ImVec2(0, Space::S));
        
        if (!s_skcd_is_hunter && sel_unit == 0) {
    ImGui::TextDisabled("（该角色已离场，暂时回退到监管）");
}
ImGui::Dummy(ImVec2(0, Space::S));

// ====================================================================
// ★★ 正在使用的技能 ★★
//   用途：观察选中角色**此刻**在放什么技能（比下面的 CD 列表更实时）
//   判据：last_cast_skill_id 在 3 秒内变过 = 正在使用
//         （last_cast_skill_id 游戏不清 0，只靠"新鲜度"判断）
// ====================================================================
{
    uint64_t hu_probe = sel_unit ? sel_unit : PyAttack::hunter_unit_raw();
    SkcdUsingSkill us;
    if (hu_probe && skcd_read_using(hu_probe, us)) {
        const uint64_t now_ms = skcd_now_ms();
        const uint64_t age_ms = (now_ms >= us.seen_ms) ? (now_ms - us.seen_ms) : 0;
        const bool  is_recent = (us.id != 0 && age_ms < 3000);

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("正在使用");
        ImGui::SameLine();

        if (us.id == 0) {
            ImGui::TextDisabled("(无)");
        } else if (is_recent) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%lld   %.1fs 前", (long long)us.id, age_ms / 1000.0);
            ImGui::TextColored(Theme::U32ToVec4(pal.accent), "%s", buf);
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "%lld   %.0fs 前（已结束）",
                     (long long)us.id, age_ms / 1000.0);
            ImGui::TextDisabled("%s", buf);
        }
        if (us.state >= 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("  state=%lld", (long long)us.state);
        }
    } else {
        ImGui::TextDisabled("正在使用: (读不到)");
    }
    ImGui::Dummy(ImVec2(0, Space::S));
}

uint64_t hu_probe = sel_unit ? sel_unit : PyAttack::hunter_unit_raw();
SkcdUsingSkill us;

        // ---- 技能 CD（逻辑不变，只是 hu 换成"选中角色"）----
        uint64_t hu = sel_unit ? sel_unit : PyAttack::hunter_unit_raw();
        if (!hu) {
            ImGui::TextDisabled(s_skcd_is_hunter ? "监管 unit 未就绪" : "角色 unit 未就绪");
            ImGui::Dummy(ImVec2(0, Space::S));
        } else {
            static PyGenius::SkillCDEntry sk_list[PyGenius::MAX_SKILLS_CD];
            static int sk_n = 0;
            static double sk_last = -10.0;
            if (ImGui::GetTime() - sk_last >= 0.2) {
                sk_n = PyGenius::collect_all_skill_cd(hu, sk_list, PyGenius::MAX_SKILLS_CD);
                sk_last = ImGui::GetTime();
            }
            if (sk_n == 0) {
                ImGui::TextDisabled("(无技能)");
                ImGui::Dummy(ImVec2(0, Space::S));
            } else {
                for (int i = 0; i < sk_n; i++) {
                    const auto& e = sk_list[i];
                    float r = PyGenius::skill_cd_remain_now(e);
                    float prog = e.total > 0.01f ? (1.0f - r / e.total) : 1.0f;
                    if (prog < 0) prog = 0;
                    if (prog > 1) prog = 1;
                    char lbl[64], val[48];
                    if (e.charge_max > 0)
                        snprintf(lbl, sizeof(lbl), "技能 %lld · %d/%d",
                                 (long long)e.skill_id, e.charge_cur, e.charge_max);
                    else
                        snprintf(lbl, sizeof(lbl), "技能 %lld", (long long)e.skill_id);
                    snprintf(val, sizeof(val), r > 0.05f ? "%.1f 秒" : "就绪", r);
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImVec2 bp = ImGui::GetCursorScreenPos();
                    float bw = ImGui::GetContentRegionAvail().x;
                    float fs = ImGui::GetFontSize();
                    ImFont* f = ImGui::GetFont();
                    dl->AddText(f, fs, ImVec2(bp.x, bp.y), pal.textDim, lbl);
                    ImVec2 vs = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, val);
                    ImU32 vc = r > 0.05f ? pal.textHi : pal.ok;
                    dl->AddText(f, fs, ImVec2(bp.x + bw - vs.x, bp.y), vc, val);
                    ImGui::Dummy(ImVec2(bw, fs + Space::XS));
                    ImVec2 tp = ImGui::GetCursorScreenPos();
                    dl->AddRectFilled(tp, ImVec2(tp.x + bw, tp.y + 2.0f), pal.track, 1.0f);
                    if (prog > 0.01f)
                        dl->AddRectFilled(tp, ImVec2(tp.x + bw * prog, tp.y + 2.0f),
                                          r > 0.05f ? pal.accent : pal.ok, 1.0f);
                    ImGui::Dummy(ImVec2(bw, Space::M));
                }
            }
        }
    }
    GroupEnd(pal);
#endif

    // ================= 地址 =================
    GroupBegin("g_addr");
    {
        static bool show_addr = false;
        if (Disclosure("调试地址", &show_addr, pal)) {
            char buf[64];
            snprintf(buf, sizeof(buf), "0x%lx", (unsigned long)Matrix);
            Field("矩阵", buf, pal, pal.textDim);
            snprintf(buf, sizeof(buf), "0x%lx", (unsigned long)Arrayaddr);
            Field("数组", buf, pal, pal.textDim);
            snprintf(buf, sizeof(buf), "0x%lx", (unsigned long)MatrixOffset);
            Field("矩阵偏移", buf, pal, pal.textDim);
            snprintf(buf, sizeof(buf), "0x%lx", (unsigned long)ArrayaddrOffset);
            Field("数组偏移", buf, pal, pal.textDim);
            snprintf(buf, sizeof(buf), "%d", c);
            Field("模块页数", buf, pal, pal.textDim);
            Field("包名", extractedString, pal, pal.textDim);
            Field("监管者", prophet_text, pal, pal.textDim);
        }
    }
    GroupEnd(pal, false);
}

// ---------- 页 3：Dump ----------
void Dump(Theme::Palette& pal) {
    std::lock_guard<std::recursive_mutex> data_lock(g_data_mutex);
    using namespace FangUI;

    static char filter_buf[64] = "";
    Head("查询", pal);
    // ★ 系统软键盘在本架构下不可达，改用内置键盘（见 soft_keyboard.h 顶部说明）：
    //   点输入框 -> 键盘展开；「收起」折叠。
    static SoftKeyboard::State kb_filter;
    SoftKeyboard::DrawFor("dump_filter", "过滤词", filter_buf, sizeof(filter_buf),
                          ImVec2(ImGui::GetContentRegionAvail().x * 0.6f, 0),
                          SoftKeyboard::MODE_TEXT, kb_filter);
    PyDump::set_filter(filter_buf);

    ImGui::TextDisabled("状态: %s",   PyDump::status());
    ImGui::TextDisabled("解释器: %s", PyRoot::status_text());

    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::Spacing();
    if (CenterButton("Dump game_kernel", ImVec2(avail * 0.48f, 38), pal, true))
        PyDump::dump_game_kernel();
    ImGui::SameLine(0, avail * 0.04f);
    if (CenterButton("Dump units_by_type", ImVec2(avail * 0.48f, 38), pal, true))
        PyDump::dump_units_by_type();
    ImGui::Spacing();
    if (CenterButton("Dump 1999(加页手记)", ImVec2(avail * 0.48f, 38), pal, true))
    PyDump::dump_type_units(1999);
ImGui::SameLine(0, avail * 0.04f);
if (CenterButton("Dump 1(监管)", ImVec2(avail * 0.48f, 38), pal, true))
    PyDump::dump_type_units(1);
    if (CenterButton("Dump 监管[0]", ImVec2(avail * 0.48f, 38), pal, true))
        PyDump::dump_unit_by_type(1, 0);
    ImGui::SameLine(0, avail * 0.04f);
    if (CenterButton("保存 dump", ImVec2(avail * 0.48f, 38), pal, true))
        PyDump::save_buf("/sdcard/dwdump.txt", "/data/local/tmp/dwdump.txt");
    
    if (CenterButton("Dump 当前地图元数据", ImVec2(ImGui::GetContentRegionAvail().x, 38), pal, true))
    PyDump::dump_current_scene();
    
        if (CenterButton("Dump 地图候选字段", ImVec2(ImGui::GetContentRegionAvail().x, 38), pal, true))
    PyDump::dump_map_candidates();
        
    if (CenterButton("Dump 所有单位详情", ImVec2(avail * 0.48f, 38), pal, true))
    PyDump::dump_all_units();
    
    if (CenterButton("Dump scene_childs（找房间）", ImVec2(ImGui::GetContentRegionAvail().x, 38), pal, true))
    PyDump::dump_scene_childs();
    
    if (CenterButton("Dump 找房间数据2", ImVec2(ImGui::GetContentRegionAvail().x, 38), pal, true))
    PyDump::dump_room_hunt();

    ImGui::Spacing();
    if (CenterButton("Dump 技能字段", ImVec2(-1, 38), pal, true)) {
        uint64_t hu = PyAttack::hunter_unit_raw();
        if (hu) PyDump::dump_skill_fields(hu);
        else    PyDump::reset_status("监管 unit 未就绪");
    }

    uint64_t self_scene = 0;
    if (PySelf::anchor(self_scene)) {
        if (CenterButton("Dump 当前操控单位", ImVec2(ImGui::GetContentRegionAvail().x, 38), pal, true))
            PyDump::dump_unit_by_scene(self_scene);
    } else {
        ImGui::TextDisabled("当前操控单位锚点不可用");
    }

    static char addr_buf[128] = "";
static SoftKeyboard::State kb_addr;
SoftKeyboard::DrawFor("dump_addr", "对象地址 / 路径 (0x...)", addr_buf, sizeof(addr_buf),
                      ImVec2(ImGui::GetContentRegionAvail().x * 0.6f, 0),
                      SoftKeyboard::MODE_TEXT, kb_addr);
if (CenterButton("Dump 指定地址", ImVec2(ImGui::GetContentRegionAvail().x, 38), pal, true)) {
        uint64_t a = strtoull(addr_buf, nullptr, 16);
        if (a) PyDump::dump_dict_at(a);
        else   PyDump::reset_status("地址无效");
    }


    ImGui::Dummy(ImVec2(0, 10));
    Head("输出", pal);
    const char *p = PyDump::buf();
    int shown = 0;
    while (*p && shown < 400) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len > 0) {
            char tmp[512];
            if (len > sizeof(tmp) - 1) len = sizeof(tmp) - 1;
            memcpy(tmp, p, len); tmp[len] = 0;
            ImGui::TextUnformatted(tmp);
        }
        if (!nl) break;
        p = nl + 1; shown++;
    }
    if (shown >= 400) ImGui::TextDisabled("... (截断, 共 %d 字符)", PyDump::buf_len());
    ImGui::Dummy(ImVec2(0, 12));
Head("场景 Dump（找宝箱/怪物类名）", pal);
{
    bool dm = g_scene_dump_mode.load();
    if (RowSwitch("收全部对象（扫描变慢）", &dm, pal))
        g_scene_dump_mode.store(dm);
    ImGui::TextDisabled("开启后 read_thread 不再按类名过滤");
    ImGui::TextDisabled("走到宝箱/怪物旁边再点保存");
}
if (CenterButton("保存场景 Dump 到文件",
                 ImVec2(ImGui::GetContentRegionAvail().x, 38), pal, true)) {
    PyDump::reset_status("保存中...");

    const char *paths[] = { "/sdcard/scene_dump.txt",
                            "/data/local/tmp/scene_dump.txt" };
    FILE *f = nullptr;
    const char *used = nullptr;
    for (auto p : paths) { f = fopen(p, "w"); if (f) { used = p; break; } }

    if (f) {
        int cnt = data_count;
        fprintf(f, "=== scene dump  count=%d ===\n", cnt);
        fprintf(f, "%-16s %4s %-24s %5s %10s %8s  %s\n",
                "addr", "camp", "pos(x,y,z)", "ex240", "v70", "v1a0", "class_name");
        for (int i = 0; i < cnt; i++) {
            float xyz[3] = {0,0,0};
            unit_xyz(i, xyz);
            fprintf(f,
                "0x%014llX %4d (%8.1f,%7.1f,%8.1f) %5d 0x%08X %8.1f  %s\n",
                (unsigned long long)data[i].obj,
                data[i].camp,
                xyz[0], xyz[1], xyz[2],
                getDword(data[i].obj + (uint64_t)SecOff::u64(SecOff::DG_79)),
                getDword(data[i].obj + (uint64_t)SecOff::u64(SecOff::DG_80)),
                getFloat(data[i].obj + (uint64_t)SecOff::u64(SecOff::DG_81)),
                data[i].class_name);
        }
        fclose(f);
        char msg[128];
        snprintf(msg, sizeof(msg), "已保存 %d 个对象 -> %s", cnt, used);
        PyDump::reset_status(msg);
    } else {
        PyDump::reset_status("保存失败: /sdcard 和 /data/local/tmp 都打不开");
    }
}
}

#if SHOW_MCP_PAGE
// ============================================================================
// MCP 页 —— 实时显示 ImGui-MCP 桥的启动 / 连接状态与最近任务
// ============================================================================
//  数据全部来自 McpBridge 的状态快照（渲染线程本地读，无阻塞）。
//  编译期用 -DSHOW_MCP_PAGE=0 可以把这一页连同 MCP 启动一起关掉。
// ============================================================================
namespace {

void McpFmtUptime(uint64_t ms, char* out, size_t n) {
    uint64_t s = ms / 1000;
    if (s < 60)         snprintf(out, n, "%llus", (unsigned long long)s);
    else if (s < 3600)  snprintf(out, n, "%llum%02llus", (unsigned long long)(s / 60), (unsigned long long)(s % 60));
    else                snprintf(out, n, "%lluh%02llum", (unsigned long long)(s / 3600), (unsigned long long)((s % 3600) / 60));
}

void McpFmtAge(uint64_t ms, char* out, size_t n) {
    if (ms == UINT64_MAX) { snprintf(out, n, "从未"); return; }
    if (ms < 1000)        { snprintf(out, n, "刚刚"); return; }
    uint64_t s = ms / 1000;
    if (s < 60)           snprintf(out, n, "%llu秒前", (unsigned long long)s);
    else if (s < 3600)    snprintf(out, n, "%llu分前", (unsigned long long)(s / 60));
    else                  snprintf(out, n, "%llu时前", (unsigned long long)(s / 3600));
}

void McpFmtBytes(uint64_t b, char* out, size_t n) {
    if (b < 1024)             snprintf(out, n, "%llu B", (unsigned long long)b);
    else if (b < 1024 * 1024) snprintf(out, n, "%.1f KB", (double)b / 1024.0);
    else                      snprintf(out, n, "%.2f MB", (double)b / (1024.0 * 1024.0));
}

} // namespace

void Mcp(Theme::Palette& pal) {
    using namespace FangUI;

    McpBridge::Status st;
    McpBridge::GetStatus(&st);

    // ---------------- 运行状态 ----------------
    Head("运行状态", pal);
    if (!st.running) {
        ImGui::TextColored(ImVec4(1, 0.23f, 0.19f, 1), "● 未启动");
        ImGui::TextDisabled("（编译时 SHOW_MCP_PAGE=0 会完全关闭 MCP）");
        return;
    }
    if (st.client_connected)
        ImGui::TextColored(ImVec4(0, 0.78f, 0.35f, 1), "● 运行中 | 已连接");
    else
        ImGui::TextColored(ImVec4(1, 0.65f, 0.1f, 1), "● 运行中 | 等待连接");

    ImGui::TextDisabled("监听: %s:%d%s",
                        st.lan ? "0.0.0.0" : "127.0.0.1",
                        st.port,
                        st.lan ? "  (LAN)" : "  (adb forward)");
    if (st.client_connected)
        ImGui::Text("客户端: %s", st.client_peer);
    else
        ImGui::TextDisabled("客户端: 无");

    char up[32];  McpFmtUptime(st.process_uptime_ms, up, sizeof(up));
    char last[32]; McpFmtAge(st.last_command_age_ms, last, sizeof(last));
    ImGui::TextDisabled("进程运行: %s", up);
    ImGui::TextDisabled("上条命令: %s", last);
    ImGui::TextDisabled("版本: bridge %s / ImGui %s", st.bridge_version, st.imgui_version);

    // ---------------- 统计 ----------------
    ImGui::Dummy(ImVec2(0, 10));
    Head("统计", pal);
    {
        char by[32]; McpFmtBytes(st.bytes_sent, by, sizeof(by));
        ImGui::Text("命令 %llu 条   失败 %llu 条",
                    (unsigned long long)st.commands_total,
                    (unsigned long long)st.commands_failed);
        ImGui::TextDisabled("截图 %llu 张   待执行 %d 条   已回 %s",
                            (unsigned long long)st.screenshots_total,
                            st.queue_depth, by);
    }

    // ---------------- 开关 ----------------
    ImGui::Dummy(ImVec2(0, 10));
    Head("开关", pal);
    {
        bool aw = st.allow_write;
        if (RowSwitch("允许 AI 修改设备数据", &aw, pal))
            McpBridge::SetAllowWrite(aw);
        ImGui::TextDisabled(aw ? "  AI 可读写配置（dwrg_set / dwrg_save）"
                               : "  只读模式：AI 只能读，不能改");
    }
    {
        static float maxw = 0.0f;
        if (maxw == 0.0f) maxw = (float)McpBridge::GetDefaultMaxWidth();
        if (Slider("mcp_maxw", &maxw, 0.0f, 2400.0f, "%.0f px", pal))
            McpBridge::SetDefaultMaxWidth((int)maxw);
        ImGui::TextDisabled("截图长边上限（0 = 不缩放）");
    }

    // ---------------- 实时任务 ----------------
    ImGui::Dummy(ImVec2(0, 10));
    Head("实时任务", pal);

    // 固定 32 个缓冲位；GetRecentTasks() 会按实际条数截断
    static McpBridge::Task tasks[32];
    const int n = McpBridge::GetRecentTasks(tasks, 32);
    ImGui::TextDisabled("最近 %d 条（共缓存 %u）", n, McpBridge::TaskCapacity());

    if (n == 0) {
        ImGui::TextDisabled("  还没有收到任何命令");
    } else {
        for (int i = 0; i < n; i++) {
            const McpBridge::Task& t = tasks[i];
            char age[32]; McpFmtAge(t.age_ms, age, sizeof(age));
            ImVec4 col = t.ok ? ImVec4(0.75f, 0.75f, 0.75f, 1.0f)
                              : ImVec4(1.0f, 0.35f, 0.3f, 1.0f);
            ImGui::TextColored(col, "#%-4llu %-18s %3ums %-8s %s",
                               (unsigned long long)(t.seq % 10000),
                               t.cmd, t.dur_ms, age,
                               t.detail[0] ? t.detail : "");
        }
    }

    ImGui::Dummy(ImVec2(0, 8));
    float avail = ImGui::GetContentRegionAvail().x;
    if (CenterButton("清空任务列表", ImVec2(avail, 34), pal)) McpBridge::ClearRecentTasks();

    ImGui::Dummy(ImVec2(0, 6));
    ImGui::TextDisabled("PC 端用法见 docs/ImGui-1.92升级与ImGui-MCP接入说明.md");
}
#endif // SHOW_MCP_PAGE

// ============================================================================
// ★★ 「功能」页 —— 改视角（2026-10-13 新增）★★
//
// 用户需求（原话）：
//   「每局扫描并获取人物坐标地址，将识别到的角色名称显示在改视角窗口中。
//     点击对应角色名称，即可将视角切换到该角色。结合求生者或监管者的技能、
//     辅助特质，以及地图中可交互的中立建筑，可以实现快速位移或瞬间传送效果。」
//
// 本页做「扫描 + 列表 + 选人」这一半（纯读取）✓
// 「切视角」那一半**还没接通**，原因写在 view_switch.h 文件头：
//   改 Python 属性要往 __dict__ 值槽写指针，而本工程 dict 布局与标准
//   CPython 不同（实测 keys 数组偏移对不上）✗
//   曾经靠猜偏移写内存把用户的对局写崩过 ☠ 所以**没证明之前不写** ✓
// ============================================================================
// 诊断：把功能页实际拿到的面板高度暴露出来（排查"列表被裁"用）
//   ⚠ 这些值只能在**渲染时**（Feature() 内）填 —— dump 回调里不能碰 ImGui ☠
namespace FloatProbe {
    inline float panel = 0.0f;
    inline float y0    = 0.0f;
    inline float winH  = 0.0f;
    inline int   rows  = 0;
}


static void Feature_ViewSwitch(Theme::Palette& pal) {
    using namespace FangUI;

    // ★★★ 2026-10-13 布局定稿 ★★★
    //   实测：本页 `GetContentRegionAvail().y` 只有约 **390~400px**（面板高度
    //   由 my_ui.cpp 的 baseH 决定 = 视口的 63%，见那里），而 12 行需要
    //   312px（26px/行）。**父窗口会裁掉子窗口** —— 这就是"列表被框住、
    //   只显示 9 行"的真因 ✗
    //   所以：把列表上方的东西压到**一行**，把省下的高度全给列表 ✓
    {
        // 左：启用扫描开关；右：过滤（用一个窄的 Segmented）
        RowSwitch("\u542f\u7528", &ViewSwitch::g_on, pal);
        {
            static const char* fl[] = { "\u5168\u90e8", "\u6c42\u751f", "\u76d1\u7ba1" };
            int nf = Segmented("vs_filter", fl, 3, ViewSwitch::g_filter, pal, 32.0f, true);
            if (nf != ViewSwitch::g_filter) ViewSwitch::g_filter = nf;
        }
    }

    ImGui::Dummy(ImVec2(0, Theme::Sp::XS));

    // ---- 角色列表 ----
    //   ★★★ 2026-10-13 用户要求（三条一起）★★★
    //     ① 去掉按距离排序开关
    //     ② 模仿者局只显示「N号 身份」；正常局只显示角色名 —— **都不显示阵营** ✓
    //     ③ 两个按钮移到底部；列表**不滚动、完整展开 1~12 号** ✓
    //
    //   布局：先把底部那条按钮带预留出来，剩下的高度全给列表 ✓
    //   这样按钮永远贴面板底部，跟行数无关 ✓
    //
    //   ⚠ 关于高度：面板本身是 "视口的 94%，上限 980px"（my_ui.cpp baseH），
    //     比列表需要的高度大得多，所以列表按**内容**给高度就不会被裁 ✓
    //     （之前写死 340px，12 行要 372px，就被裁掉 2 行 ✗）
    //   ★★★ 2026-10-13 关键实测（"列表被框住"的真因）★★★
    //     `GetContentRegionAvail().y` 实测只有 **168px**，而 12 行需要 372px ✗
    //     **父窗口会裁掉子窗口** —— 所以 10~12 号被切掉，而且列表外面那个
    //     圆角框（BeginChild 的边框）是固定高度，看起来就"框住了" ✗
    //     （之前探针读到 660px 是假象：那次列表把面板撑大了才量到）
    //   对策：把每行压到 26px、按钮压到 38px，让 12 行**真能装进可用高度** ✓
    //     12*26+10 = 322px，加上方控件约 120px + 按钮 38px = 480px，
    //     比当前可用高度更紧凑；装不下的极端情况仍可靠拖动滚动 ✓
    const float rowH   = 26.0f;
    const float btnH   = 38.0f;
    const float gap    = Theme::Sp::XS;
    const float panelH = ImGui::GetContentRegionAvail().y;
    const float panelY0 = ImGui::GetCursorPosY();   // 本页正文起始 Y（定位用）
    const float yBody0  = panelY0;
    FloatProbe::panel = panelH;
    FloatProbe::y0    = panelY0;
    FloatProbe::winH  = ImGui::GetWindowHeight();   // ★ 只能在渲染时取 ✓
    FloatProbe::rows  = ViewSwitch::g_n;

    if (ViewSwitch::g_on && ViewSwitch::g_n > 0) {
        const float avail = ImGui::GetContentRegionAvail().x;
        //   ★★ 关键（实测踩过）：这里的 `panelH` 只有 **168px** ✗
        //     而 12 行需要 372px —— 空间**根本不够**，不是布局算错 ✗
        //     （之前探针读到 660px 是假象：那次列表把面板撑大了才量到）
        //   所以列表按"**剩余空间**"给高度：总高 - 上面已用 - 底部按钮带 ✓
        //   显示不下的部分靠拖动滚动看（拖动已实现）✓
        //   ★★ 关键（实测踩了两次）：列表行的实际高度不是 rowH，而是
        //     `rowH + ImGuiStyleVar_ItemSpacing.y`（默认约 8px）✗
        //     12 行 * (26+8) = 408px，按 324px 给高度就会把 10~12 号裁掉 ✗
        //     所以这里把行间距算进去，并且把列表里的 ItemSpacing 压到 2px ✓
        const float isp = 2.0f;                       // 列表内行间距
        float listH = (rowH + isp) * (float)ViewSwitch::g_n + 8.0f;
        const float yNow = ImGui::GetCursorPosY();          // 上面已用到哪
        const float room = panelY0 + panelH - btnH - gap - yNow;
        if (room > 40.0f && listH > room) listH = room;
        if (listH < 60.0f) listH = 60.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, isp));
        //   ★ 去掉 BeginChild 的**边框**（第 3 个参数 false）——
        //     用户反馈"身份列表被框住的"。那个圆角框高度固定，看起来就像
        //     列表被一个盒子夹住 ✗ 去掉边框后视觉上就是普通列表 ✓
        ImGui::BeginChild("vs_list", ImVec2(avail, listH), false,
                          ImGuiWindowFlags_NoScrollbar);

        //   Drag-to-scroll kept as a safety net for the clipped case only;
        //   with the layout above it normally has nothing to scroll.
        static bool  s_dragging = false;
        static float s_drag_y0  = 0.0f;
        static float s_scroll0  = 0.0f;
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
            ImGui::IsMouseClicked(0)) {
            s_dragging = false;
            s_drag_y0  = ImGui::GetIO().MousePos.y;
            s_scroll0  = ImGui::GetScrollY();
        }
        if (ImGui::IsMouseDown(0)) {
            const float dy = ImGui::GetIO().MousePos.y - s_drag_y0;
            if (!s_dragging && (dy > 8.0f || dy < -8.0f)) s_dragging = true;
            if (s_dragging) ImGui::SetScrollY(s_scroll0 - dy);
        }
        if (ImGui::IsMouseReleased(0)) s_dragging = false;

        for (int i = 0; i < ViewSwitch::g_n; i++) {
            const ViewSwitch::Target& t = ViewSwitch::g_t[i];
            char label[128];
            const char* who = t.name[0] ? t.name : "?";
            //   ★ 2026-10-13 user: "copycat match -> only N hao + identity;
            //     normal match -> only the character name" -- so NO camp word.
            //   Distance is still useful for aiming a teleport, keep it.
            snprintf(label, sizeof(label), "%s%s  %.1fm",
                     (i == ViewSwitch::g_sel) ? "\u25cf " : "   ",
                     who,
                     t.dist_m);
            ImGui::PushID(i);
            const bool sel = (i == ViewSwitch::g_sel);
            const bool hit = Button(label, ImVec2(avail - 24.0f, rowH), pal, sel);
            if (hit && !s_dragging && !ImGui::IsMouseDragging(0, 8.0f)) {
                ViewSwitch::g_sel     = i;
                ViewSwitch::g_sel_uid = t.uid;
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    // ---- bottom buttons ----
    //   2026-10-13 user: "move the two buttons to the very bottom of the UI".
    //   Pin them to the panel bottom instead of letting them follow the list,
    //   so their position no longer depends on how many rows there are.
    {
        const float avail = ImGui::GetContentRegionAvail().x;
        const float bw = (avail - 12.0f) * 0.5f;
        //   ⚠ 不要用 GetWindowHeight() —— 实测它在 EndChild() 之后返回的值
        //     偏小，按钮会停在中间而不是底部 ✗（踩过）
        //     用进来时量到的 panelH（= 本页可用高度，探针实测 660px）✓
        const float yWant = yBody0 + panelH - btnH - gap;
        if (yWant > ImGui::GetCursorPosY()) ImGui::SetCursorPosY(yWant);
        if (Button("\u5207\u6362\u5230\u8be5\u89d2\u8272", ImVec2(bw, btnH), pal, true)) {
            ViewSwitch::ApplyView();
        }
        ImGui::SameLine(0.0f, 12.0f);
        if (Button("\u8fd8\u539f\u6211\u7684\u89c6\u89d2", ImVec2(bw, btnH), pal)) {
            ViewSwitch::UnlockView();
        }
    }
}

// ============================================================================
// 「小丑加速」子页 —— 2026-10-14 新建，暂空
//   用户要求：先只建页，内容以后再说
// ============================================================================
static void Feature_JokerSpeed(Theme::Palette& pal) {
    JokerSpeed::DrawUI(pal);
}

// ============================================================================
// 「功能」页 —— 顶层容器，只负责两个子页的切换
//
//   2026-10-14 用户要求：把原来的功能收进「改视角」子页，
//   再加一个空的「小丑加速」子页。
//
//   做法：原 Feature() 整体改名成 Feature_ViewSwitch()，
//   这里只做子页切换壳 —— 原有的布局/高度计算一行没动 ✓
//
//   ⚠ 子页切换的写法与「触摸」页完全一致（Segmented + 索引）✗
//     别用 ImGui::TabBar/Button：本项目的 Segmented 已经处理了
//     主题色、滑块指示器、文字交叉淡入 —— 用别的控件视觉不统一 ✗
// ============================================================================

// ============================================================================
//   2026-10-26：**共享地图**（HTTP 服务，端口 12750）
//   数据来源：data[]（求生/监管坐标）+ PyProgress::get_list（密码机进度/坐标）
//             + PyMiniMap::g_scene_id（地图自动切换）+ 地窖（类名 prop_76）
//   关闭开关或退出程序都会关掉端口。
// ============================================================================
static int   g_share_fd  = -1;
static std::atomic<bool> g_share_stop{false};
static std::thread g_share_thr;
static char  g_share_state[8192] = "{}";   // ← 初始值改成 "{}"，启动瞬间也是有效 JSON
static std::mutex g_share_mtx;             // ★ 新增：主线程写 / 服务线程读的唯一同步点

static char  g_share_mapname[64] = "";

static const char *kMapDir   = "/data/adb/1/share_map/maps";
static const char *kPortrDir = "/data/adb/1/share_map/portraits";

// 2026-10-28：加 worldW/worldH —— 地图的世界坐标范围（世界单位，1 单位 ≈ 0.084 米）
//   这是"自动对齐"的唯一依赖：知道范围，前端用密码机坐标自动算中心，
//   图片和点就能天然对齐，用户不用调。
//   估得不准也没关系：前端还会用密码机坐标自动微调中心，这里只决定"点缩放比例"。
struct MapIdName { int id; const char *file; const char *zh; float worldW, worldH; };
static const MapIdName kMaps[] = {
    {  6, "军工厂.png",     "军工厂",     1400.0f, 1400.0f },
    { 11, "红教堂.png",     "红教堂",     1300.0f, 1300.0f },
    { 18, "圣心医院.png",   "圣心医院",   1400.0f, 1400.0f },
    { 19, "湖景村.png",     "湖景村",     1600.0f, 1600.0f },
    { 30, "月亮河公园.png", "月亮河公园", 1900.0f, 1900.0f },
    { 33, "里奥的回忆.png", "里奥的回忆", 1500.0f, 1500.0f },
    { 39, "永眠镇.png",     "永眠镇",     1700.0f, 1700.0f },
    { 40, "唐人街.png",     "唐人街",     1400.0f, 1400.0f },
    { 75, "不归林.png",     "不归林",     1800.0f, 1800.0f },
};

static const char *ShareMapFile(int64_t sid, const char **zh, float *wW, float *wH)
{
    for (auto &m : kMaps) if ((int64_t)m.id == sid) {
        if (zh) *zh = m.zh;
        if (wW) *wW = m.worldW;
        if (wH) *wH = m.worldH;
        return m.file;
    }
    if (zh) *zh = "未知地图";
    if (wW) *wW = 1400.0f;
    if (wH) *wH = 1400.0f;
    return "军工厂.png";
}

/*   2026-10-27：**共享地图专用日志**
 *   为什么不复用 AppLogRaw / LOG：
 *     · 那两路写的是 o.log，会和触摸/天赋/战斗日志混在一起，
 *       排查"网页一直连接中"时 grep 起来很吵；
 *     · LOG 受 ENABLE_LOG 控制，发布版=0 就完全静默 ——
 *       但"网页连不上"恰恰是**发布版**才会出现的问题，
 *       必须在发布版也能看到日志。
 *   ⇒ 单独一个文件 /data/adb/.d/share.log，**不受 ENABLE_LOG 影响**，
 *      每次 ShareMapStart 时 truncate（新会话 = 新日志）。
 *
 *   加锁原因：服务线程 + 主线程都会调用，
 *     fopen("a") + 多次 fprintf 之间如果不加锁，
 *     时间戳和正文会交错，日志就废了。
 */
#include <mutex>
static std::mutex g_share_log_mtx;

static void share_log(const char *fmt, ...)
{
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tmv; localtime_r(&ts.tv_sec, &tmv);
    char tb[32];
    snprintf(tb, sizeof(tb), "%02d:%02d:%02d.%03d",
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
             (int)(ts.tv_nsec / 1000000));

    std::lock_guard<std::mutex> lk(g_share_log_mtx);
    FILE *f = fopen("/data/adb/.d/share.log", "a");
    if (!f) return;
    fprintf(f, "[%s] ", tb);
    va_list ap; va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

// 生成一帧 JSON（在主线程里调，直接读 data[] / PyProgress）
static void ShareMapTick()
{
    static int64_t s_last_scene = -999;
    int64_t sid = PyCover::g_last_scene_id.load();
    if (sid <= 0) sid = PyMiniMap::g_scene_id;
    if (sid > 0 && sid != s_last_scene) {
        if (s_last_scene != -999) {
            share_log("换局: scene %lld -> %lld，清空缓存",
                      (long long)s_last_scene, (long long)sid);
            ClearPublishedDataAndSetReadState(1);
            g_reload_cooldown = 60;
            PyGenius::g_other_n = 0;
            PyAttack::ResetBindings();
            g_share_ent_n = 0;
            memset(g_share_ent, 0, sizeof(g_share_ent));
        }
        s_last_scene = sid;
    }
}

// 2026-10-26：坐标/进度里可能是 nan/inf —— 直接打进 JSON 会让浏览器解析失败（页面显示"断开"）
static inline float share_num(float v)
{
    if (!(v == v)) return 0.0f;                  // NaN
    if (v > 1.0e9f || v < -1.0e9f) return 0.0f;  // inf / 异常
    return v;
}

//   2026-10-26：**JSON 字符串必须转义**
//   实测抓到一个名字是 `chr\player\dm65_survivor`（原始类名），
//   里面的反斜杠让浏览器报 "Bad escaped character in JSON"，整页就废了。
static void share_str(char *out, int cap, const char *in)
{
    int o = 0;
    if (cap <= 1) { if (cap == 1) out[0] = 0; return; }
    for (int i = 0; in && in[i] && o < cap - 3; ++i) {
        unsigned char c = (unsigned char)in[i];
        if (c == '\\' || c == '"') { out[o++] = '\\'; out[o++] = (char)c; }
        else if (c < 0x20)         { out[o++] = ' '; }
        else                       { out[o++] = (char)c; }
    }
    out[o] = 0;
}

// 名字是否是"原始类名"（chr\player\xxx / model 路径），这种直接跳过
static inline bool share_bad_name(const char *n)
{
    if (!n || !*n) return true;
    if (strchr(n, '\\') || strchr(n, '/')) return true;
    if (!strncmp(n, "chr", 3)) return true;
    return false;
}

// 2026-10-28：body 集合命中率保护
//   换局后 units_by_type 读不到，PySelf::body_set_ready() 仍返回 true（旧缓存），
//   is_body() 对整局实体全部返回 false -> ESP/网页全空。
//   本函数每 200ms 统计一次命中率；< 30% 时返回 true（= 本帧应该**跳过 body 过滤**）。
//   阈值 30%：正常局命中 >= 80%（模型+备用形态两条 record 至少一条命中），
//              换局瞬间 0%，60% 留两倍余量。
static bool ShareMapBodyFilterShouldSkip()
{
    std::lock_guard<std::recursive_mutex> data_lock(g_data_mutex);
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const int64_t now_ms = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    static int64_t s_last_ms = 0;
    static bool    s_skip    = false;
    if (now_ms - s_last_ms < 200) return s_skip;
    s_last_ms = now_ms;

    if (!PySelf::body_set_ready()) { s_skip = false; return false; }

    int total = 0, hit = 0;
    const int lim = (data_count > 0 && data_count < 3000) ? data_count : 0;
    for (int i = 0; i < lim; ++i) {
        if (data[i].camp != 1 && data[i].camp != 2) continue;
        if (!strstr(data[i].class_name, "player") && !strstr(data[i].class_name, "boss")) continue;
        total++;
        if (PySelf::is_body(data[i].obj)) hit++;
    }
    s_skip = (total > 0 && hit * 100 < total * 30);
    return s_skip;
}

static void ShareMapBuildJson()
{
    std::lock_guard<std::recursive_mutex> data_lock(g_data_mutex);
    /*   2026-10-26：scene_id 优先取 PyCover 的（直接读 cur_world.scene_id），
     *   小地图那份只在开了小地图相关功能时才刷新，实测局内一直是 0。 */
    int64_t sid = PyCover::g_last_scene_id.load();
    if (sid <= 0) sid = PyMiniMap::g_scene_id;
    // ★★ 2026-10-28：sid 迟滞 ★★
    //   PyCover 与 PyMiniMap 两路刷新时机不同、偶尔读到 0，
    //   直接下发会让 map 在「军工厂 ↔ 未知地图」之间抖，
    //   前端 imgName 跟着变 → new Image → 那一帧 img.complete=false。
    //   记住最近 3 秒内最后一次有效值，读失败/读空时沿用它。
    {
        static int64_t s_last_good_sid = 0;
        static int64_t s_last_good_ms  = 0;
        struct timespec ts2; clock_gettime(CLOCK_MONOTONIC, &ts2);
        const int64_t nw = (int64_t)ts2.tv_sec * 1000 + ts2.tv_nsec / 1000000;
        if (sid > 0) {
            s_last_good_sid = sid;
            s_last_good_ms  = nw;
        } else if (s_last_good_sid > 0 && nw - s_last_good_ms < 3000) {
            sid = s_last_good_sid;
        }
    }
    const char *zh = "";
float wW = 1400.0f, wH = 1400.0f;
ShareMapFile(sid, &zh, &wW, &wH);
snprintf(g_share_mapname, sizeof(g_share_mapname), "%s", zh);

    /* ★★★ 2026-10-28：主线程先写自己的缓冲，最后一把 memcpy ★★★
     *   原代码直接写全局 g_share_state —— 服务线程虽然拿了 g_share_mtx，
     *   但主线程从来没拿过锁，等于锁形同虚设。
     *   服务线程 strlen() 那一刻主线程正在 snprintf，读到的是
     *   "上轮残留 + 本轮写一半" 的混合内容 —— 长度和内容全是垃圾。
     */
    static char s_local[8192];                                // 主线程独占（只在这里跑）
    char *p = s_local;
    const char *end = s_local + sizeof(s_local) - 64;
// ★ 2026-10-28：in_prep 改用 PyScene::in_prepare()（读 g_ui_mgr.ui_dict 里的 UIRoomWait）
//   原来的 in_prep_scene() 依赖 scene_path 含 h55_wait，实测不总准（有时是 h55_wait_copycat2，
//   或者准备阶段只是 ui_dict 里有 UIRoomWait 但 scene_path 已经是别的）。
//   PyScene 走的是"哪个 UI 对象存在"，跟辅助 UI 里"预知天赋"的判据一致（实测准）。
const bool in_prep = PyScene::in_prepare();
int n = snprintf(p, (size_t)(end - p),
                 "{\"scene\":%lld,\"map\":\"%s\",\"worldW\":%.0f,\"worldH\":%.0f,\"in_prep\":%d,\"ciphers\":[",
                 (long long)sid, zh, (double)wW, (double)wH, in_prep ? 1 : 0);
p += n;

if (show_draw_secret_mechine) {
    PyProgress::GenListEntry gens[16];
    int gn = PyProgress::get_list(gens, 16);
    // ★ 2026-10-28：100% 的密码机网页上不画（用户要求）。
    //   用 first_cipher 控制逗号：过滤后 i 已经不是"是不是第一条"的正确标志。
    bool first_cipher = true;
    for (int i = 0; i < gn && p < end; ++i) {
        if (gens[i].progress >= 99.95f) continue;
        n = snprintf(p, (size_t)(end - p), "%s[%.2f,%.2f,%.1f]",
                     first_cipher ? "" : ",", share_num(gens[i].x),
                     share_num(gens[i].y), share_num(gens[i].progress));
        p += n;
        first_cipher = false;
    }
}
    // 天赋（和外挂准备阶段那一行同源：PyGenius::civ_at + genius_segments），500ms 更新一次
    {
        static char s_tal[512] = "";
        static uint64_t s_tal_ms = 0;
        const uint64_t nowt = (uint64_t)PyAttack::pyattack_now_ms();
        if (nowt - s_tal_ms > 500) {
            s_tal_ms = nowt;
            int len2 = 0; s_tal[0] = 0;
            const int cn = PyGenius::civ_size();
            for (int ci2 = 0; ci2 < cn && len2 < (int)sizeof(s_tal) - 40; ++ci2) {
                int seat = 0, pid2 = 0; uint32_t bits = 0;
                if (!PyGenius::civ_at(ci2, seat, pid2, bits) || bits == 0) continue;
                PyGenius::Info ti2; memset(&ti2, 0, sizeof(ti2));
                ti2.camp = 2; ti2.genius_bits = bits; ti2.genius_state = PyGenius::GENIUS_COMPLETE;
                PyGenius::GeniusLine sg2; PyGenius::genius_segments(ti2, sg2);
                len2 += snprintf(s_tal + len2, sizeof(s_tal) - len2, "%s%d号[%s%s%s]",
                                 len2 ? "  " : "", seat, sg2.pre, sg2.flywheel, sg2.post);
            }
        }
        char tes[640]; share_str(tes, sizeof(tes), s_tal);
        n = snprintf(p, (size_t)(end - p), "],\"talents\":\"%s\",\"svs\":[", tes); p += n;
    }
    
    // ★ 2026-10-28：网页上不显示"ESP 原本就不绘制"的实体（除了自身）。
//   判据和 ESP 主循环保持一致，避免网页画出一堆游戏里没框的东西。
//   ESP 主循环会因这些原因跳过：
//     ① ShouldSkipEntity 返回 true（空槽/预览镜/burke/guajian/trap/stale/镜像副本/鬼魂）
//     ② 局外（in_lobby 且不在战斗场景）
//     ③ 台球形态下的人形旧对象
//     ④ 本体集合未通过（body_set_ready 为真时只画 is_body，剔除分身/挂件/LOD/时装）
//   除了自身——自身在 ESP 里被 is_self 无条件 continue，但网页上要显示。
uint64_t self_anchor_now = 0;
PySelf::anchor(self_anchor_now);

    bool first = true;
for (int i = 0; i < data_count && p < end; ++i) {
    // ★ 2026-10-28：把 camp 1/2 都收进来——监管者也要 upsert 到 g_share_ent，
    //   否则 hunter 段读 g_share_ent 永远是空（网页上监管的天赋/CD 会是 "—"）。
    if (data[i].camp != 1 && data[i].camp != 2) continue;
    if (share_bad_name(data[i].str)) continue;      // 原始类名不发给网页
    float wp[3] = {0, 0, 0};
    unit_xyz(i, wp);

    // 先判断是不是自己：自身在 ESP 里被 is_self 无条件跳过，但网页上要显示
    // 自身判据：self_obj（全局缓存）+ anchor（每帧现取）已经覆盖绝大多数情况。
// GoldRush 下主循环还多一条 g_self_scene 判据，但那是 Draw_Main 的局部变量，
// 这里拿不到；而且 GoldRush 下 anchor 一般也能命中，就算偶尔拿不到，
// 自身的 wp[0]/wp[2] 也读不到，本来也画不出来，影响可忽略。
const bool is_self_here =
    (self_obj != 0 && data[i].obj == self_obj) ||
    (self_anchor_now != 0 && data[i].obj == self_anchor_now);

    if (!is_self_here) {
        // ① ESP 的实体级过滤（空槽 / 预览镜 / burke / guajian / zuoyi / trap /
        //    stale 废弃模型 / 镜像副本 / 鬼魂；相机是 camp 9 上面已经过滤掉）
        if (ShouldSkipEntity(data[i])) continue;

        // ② 局外（大厅/准备阶段、且不在战斗场景）——ESP 里 camp 1/2 整类不画
        const bool in_lobby_now = PySelf::in_lobby() && !PyMiniMap::in_battle_scene();
        if (in_lobby_now) continue;

        // ③ 台球形态下的人形旧对象（ESP 里靠 hunter_ball 判据跳过）
        if (data[i].camp == 1) {
            const uint64_t hs = PyGenius::hunter_scene();
            if (hs != 0 && (uint64_t)data[i].obj != hs
                && !is_yidhra_main_model(data[i].class_name)
                && PyGenius::hunter_ball())
                continue;
        }

        // ④ 本体集合过滤 + 命中率保护
if (!ShareMapBodyFilterShouldSkip()
    && PySelf::body_set_ready() && !PySelf::is_body(data[i].obj)
    && (strstr(data[i].class_name, "player") != NULL
        || strstr(data[i].class_name, "boss") != NULL))
    continue;
    }

    // ⑤ 坐标有效性：ESP 用 "D.X==0 || D.Y==0" 跳过（世界 X 或水平 Z 为 0 = 未初始化）
    if (wp[0] == 0.0f || wp[2] == 0.0f) continue;
    // ⑥ 地下：ESP 用 "D.Z <= -300"（wp[1] 是高度）
    if (wp[1] <= -300.0f) continue;
    
        /* 2026-10-28：**加命中率保护** —— 换局后 body 集合不可靠时会全踢 */
if (!ShareMapBodyFilterShouldSkip()
    && PySelf::body_set_ready() && !PySelf::is_body(data[i].obj))
    continue;
    
        char sn[80];
        share_str(sn, sizeof(sn), data[i].str);
        /*   2026-10-26：道具信息按外挂端那套给网页：
         *   mode: 0=无 1=数量 2=耐久 3=充能/CD；rem=数量；rate=耐久或充能进度 0~1
         */
        char item[40] = "", icon_key[48] = "";
        int  imode = 0, irem = 0;
        float irate = 0.0f;
        {
            const SurvSkill::ItemInfo *ii = SurvSkill::ItemOfScene((uint64_t)data[i].obj);
            if (ii && ii->n > 0) {
                const SurvSkill::ItemSlot *sl0 = &ii->it[0];
                if (sl0->model[0]) {
                    /*   2026-10-26：**关键词反查中文名**
                     *   ByModel 有时会回落到角色图标（名字变成"调香师"），
                     *   这里按外挂选图标用的同一批关键词自己查一遍，更准。
                     */
                    static const char *kIKw[][2] = {
                        {"flaregun",     "信号枪"}, {"football",   "橄榄球"},
                        {"glim",         "手电筒"}, {"huzhou",     "护肘"},
                        {"jiutong",      "酒桶"},   {"xiangshuiping","忘忧之香"},
                        {"huaibiao",     "怀表"},   {"gjx",        "工具箱"},
                        {"inject",       "镇静剂"}, {"moshubang",  "魔术棒"},
                        {"huojian",      "火箭筒"}, {"citie",      "磁铁"},
                        {"kantan",       "磁铁"},
                    };
                    for (auto &kw : kIKw) {
                        if (strstr(sl0->model, kw[0])) { snprintf(item, sizeof(item), "%s", kw[1]); break; }
                    }
                    /*   2026-10-26：图标键 —— 直接查 ItemIcon 的关键词表 kMap[]，
                     *   拿到和外挂画图标同一个 item 名（如 "60px-钢铁护肘"）。
                     */
                    /*   2026-10-26：图标键 —— 用中文道具名去内置图标表里反查
                     *   （kJueseIcons[i].item 形如 "60px-钢铁护肘"，含关键词即命中）
                     */
                    if (item[0]) {
                        for (int ii3 = 0; ii3 < kJueseIconCount; ++ii3) {
                            const char *it2 = kJueseIcons[ii3].item;
                            if (it2 && strstr(it2, item)) {
                                snprintf(icon_key, sizeof(icon_key), "%s", it2);
                                break;
                            }
                        }
                    }
                    if (!icon_key[0]) {
                        const JueseIcon *ic = ItemIcon::ByModel(sl0->model);
                        if (ic && ic->item) snprintf(icon_key, sizeof(icon_key), "%s", ic->item);
                    }
                    /*   2026-10-26：**没有手持物的角色（医生/囚徒那类）**
                     *   外挂画的是"角色技能图标"，这里按角色名去图标表里取同一张。
                     */
                    if (!icon_key[0]) {
                        const JueseIcon *ci = ItemIcon::Find(data[i].str);
                        if (ci && ci->item) snprintf(icon_key, sizeof(icon_key), "%s", ci->item);
                    }
                }
                imode = sl0->mode;
                irem  = sl0->rem;
                if (sl0->mode == SurvSkill::ITEM_DURA && sl0->life_mx > 0.0f)
                    irate = sl0->life / sl0->life_mx;
                else if (sl0->ring && sl0->ring_rate > 0.0f)
                    irate = sl0->ring_rate;
            }
        }
        // 2026-10-26：朝向优先用外挂已算好的（ESP 表），没有再自己读
        // float fdx = 0.0f, fdz = 0.0f;
// {
//     const uint64_t _o = (uint64_t)data[i].obj;
//     for (int q = 0; q < g_share_ent_n; ++q) {
//         if (g_share_ent[q].obj == _o && (g_share_ent[q].dx || g_share_ent[q].dz)) { ... }
//     }
// }
float fdx = 0.0f, fdz = 0.0f;   // 直接从这里开始读 direction
        {
    // `data[i].obj` 是场景对象 —— 先用它反查 unit_obj，再读 direction
    uint64_t unit_obj = PyGenius::unit_of_scene((uint64_t)data[i].obj);
    if (unit_obj) {
        uint64_t dobj = UnitFieldPtr(unit_obj, "direction");
        if (PyCore::is_obj(dobj)) {
            float dv[3] = {0, 0, 0};
            vm_readv(dobj + (uint64_t)SecOff::u64(SecOff::DG_68), dv, 12);
            const float dl = sqrtf(dv[0]*dv[0] + dv[2]*dv[2]);
            if (dl > 0.01f) { fdx = dv[0] / dl; fdz = dv[2] / dl; }
        }
    }
}
        char tal[96] = "", cds[64] = "";
        float sfw = 0.0f, ssk = 0.0f; int shfw = 0;
        for (int q = 0; q < g_share_ent_n; ++q) {
            if (g_share_ent[q].obj == (uint64_t)data[i].obj) {
                snprintf(tal, sizeof(tal), "%s", g_share_ent[q].tal);
                snprintf(cds, sizeof(cds), "%s", g_share_ent[q].cd);
                sfw = g_share_ent[q].fw; ssk = g_share_ent[q].skcd;
                shfw = g_share_ent[q].has_fw;
                break;
            }
        }
        char tal_e[192], cd_e[128], item_e[80], icon_e[80];
        share_str(tal_e, sizeof(tal_e), tal);
        share_str(cd_e, sizeof(cd_e), cds);
        share_str(item_e, sizeof(item_e), item);
        share_str(icon_e, sizeof(icon_e), icon_key);
        n = snprintf(p, (size_t)(end - p),
                     "%s[\"%s\",%.2f,%.2f,\"%s\",%d,%d,%.2f,%.2f,%.2f,\"%s\",\"%s\",\"%s\",%.2f,%.2f,%d]",
                     first ? "" : ",", sn, share_num(wp[0]), share_num(wp[2]),
                     item_e, imode, irem, share_num(irate), fdx, fdz, tal_e, cd_e, icon_e,
                     sfw, ssk, shfw);
        p += n; first = false;
    }
    n = snprintf(p, (size_t)(end - p), "],\"hunter\":"); p += n;

    // 2026-10-27：优先找"真正的监管本体"（PyGenius::hunter_scene / PySelf::hunter_body 命中）
//   而不是 data[] 里第一个 camp1 —— 那个可能是分身/备用形态/过期对象。
uint64_t hs = PyGenius::hunter_scene();
uint64_t hb = 0;
PySelf::hunter_body(hb);
int best_i = -1, fallback_i = -1;
for (int i = 0; i < data_count; ++i) {
    if (data[i].camp != 1) continue;
    float wp2[3] = {0, 0, 0};
    unit_xyz(i, wp2);
    if (wp2[0] == 0 && wp2[2] == 0) continue;   // 坐标无效的跳过
    if (fallback_i < 0) fallback_i = i;
    if (hs && (uint64_t)data[i].obj == hs) { best_i = i; break; }   // 引擎给的答案，最可信
    if (hb && (uint64_t)data[i].obj == hb) { best_i = i; break; }   // 次之
}
if (best_i < 0) best_i = fallback_i;             // 都匹配不上，退回第一个坐标有效的

bool hd = false;
if (best_i >= 0 && p < end) {
    float wp2[3] = {0, 0, 0};
    unit_xyz(best_i, wp2);
    char hn[80];
    share_str(hn, sizeof(hn), data[best_i].str);
    char htal[96] = "", hcd[64] = "";
    for (int q = 0; q < g_share_ent_n; ++q) {
        if (g_share_ent[q].obj == (uint64_t)data[best_i].obj) {
            snprintf(htal, sizeof(htal), "%s", g_share_ent[q].tal);
            snprintf(hcd,  sizeof(hcd),  "%s", g_share_ent[q].cd);
            break;
        }
    }
    /* 2026-10-27：**hdx/hdz 不再从 g_share_ent 读** —— 那两字段是历史遗留死字段，
     *   upsert 里 `se.dx = 0.0f; se.dz = 0.0f;` 写死，从没赋过真实值。
     *   原来 `hdx = g_share_ent[q].dx;` 拿到的永远是 0。
     *
     *   现在直说：hunter[5]/[6] 恒 0，保留这两个字段只为网页结构兼容
     *   （网页用 `(+d.hunter[5]||0)` 兜底，改成 0 也自动走位移方向猜角度）。
     *
     *   想让监管也画准确朝向：把 #if 0 改成 #if 1 —— 和求生段同一套 direction 读法。
     */
    float hdx = 0.0f, hdz = 0.0f;
#if 1
    {
        uint64_t _u = PyGenius::unit_of_scene((uint64_t)data[best_i].obj);
        if (_u) {
            uint64_t _d = UnitFieldPtr(_u, "direction");
            if (PyCore::is_obj(_d)) {
                float _v[3] = {0, 0, 0};
                vm_readv(_d + (uint64_t)SecOff::u64(SecOff::DG_68), _v, 12);
                const float _l = sqrtf(_v[0]*_v[0] + _v[2]*_v[2]);
                if (_l > 0.01f) { hdx = _v[0] / _l; hdz = _v[2] / _l; }
            }
        }
    }
#endif
    char htal_e[192], hcd_e[128];
    share_str(htal_e, sizeof(htal_e), htal);
    share_str(hcd_e,  sizeof(hcd_e),  hcd);
    n = snprintf(p, (size_t)(end - p), "[\"%s\",%.2f,%.2f,\"%s\",\"%s\",%.2f,%.2f]",
                 hn, share_num(wp2[0]), share_num(wp2[2]), htal_e, hcd_e, hdx, hdz);
    p += n; hd = true;
}
if (!hd) { n = snprintf(p, (size_t)(end - p), "null"); p += n; }

n = snprintf(p, (size_t)(end - p), ",\"others\":["); p += n;
{
    bool first_other = true;
    for (int i = 0; i < data_count && p < end; ++i) {
        if (data[i].camp != 1) continue;
        if (i == best_i) continue;                    // 主监管已在 hunter 里
        if (share_bad_name(data[i].str)) continue;    // ★ 新增：原始类名（chr\boss\...）不发给网页
        float wp4[3] = {0, 0, 0};
        unit_xyz(i, wp4);
        if (wp4[0] == 0.0f && wp4[2] == 0.0f) continue;   // 空槽
        char on[80];
        share_str(on, sizeof(on), data[i].str);
        n = snprintf(p, (size_t)(end - p), "%s[\"%s\",%.2f,%.2f]",
                     first_other ? "" : ",", on, share_num(wp4[0]), share_num(wp4[2]));
        p += n; first_other = false;
    }
}
n = snprintf(p, (size_t)(end - p), "]"); p += n;

n = snprintf(p, (size_t)(end - p), ",\"hatch\":"); p += n;
{
    bool ht = false;
    if (show_draw_Cellar) {
        for (int i = 0; i < data_count && p < end; ++i) {
            /* 2026-10-27：改用和本地绘制一致的判据（class_name 含 dm65_scene_prop_76）。
             *   原来按 data[i].str（getscene 翻译）匹配"地窖" —— Name.h 改一次翻译，
             *   网页就静默丢，本地不受影响。
             */
            if (!data[i].class_name[0] || !strstr(data[i].class_name, "dm65_scene_prop_76"))
                continue;
            float wp3[3] = {0, 0, 0};
            unit_xyz(i, wp3);
            if (wp3[0] == 0.0f && wp3[2] == 0.0f) continue;   // ★ 新增：空槽 / 未激活的候选
            n = snprintf(p, (size_t)(end - p), "[%.2f,%.2f]",
                         share_num(wp3[0]), share_num(wp3[2]));
            p += n; ht = true; break;
        }
    }
    if (!ht) { n = snprintf(p, (size_t)(end - p), "null"); p += n; }
}
    /* ★ 2026-10-28：赛前预知监管
 * 准备阶段 data[] 里没有 camp==1（求生者准备阶段 units 只有道具/HallComponentUnit，
 * 实测 user role=2），所以 hunter 段永远是 null。辅助 UI 的"预知监管"那行有内容，
 * 是因为它用的是 prophet_text（read_thread 里通过类名匹配 "boss" 得到的，
 * 或者上局留下的）。这里把 prophet_text 单独发出去，让网页在准备阶段显示"预知：XXX"。
 * 只有名字，没有坐标/天赋/CD。
 */
{
    char pn[64] = "";
    const char* pt = prophet_text;
    int pl = (int)strlen(pt);
    // prophet_text 格式是 "[噩梦]"（getboss 返回带方括号），去掉方括号
    if (pl >= 2 && pt[0] == '[' && pt[pl - 1] == ']') {
        snprintf(pn, sizeof(pn), "%.*s", pl - 2, pt + 1);
    } else if (pl > 0) {
        snprintf(pn, sizeof(pn), "%s", pt);
    }
    char pn_e[96];
    share_str(pn_e, sizeof(pn_e), pn);
    n = snprintf(p, (size_t)(end - p), ",\"prophet\":\"%s\"", pn_e);
    p += n;
}

/*   2026-10-26：自身技能CD + 道具数（网页右侧列表用） */
    n = snprintf(p, (size_t)(end - p),
         ",\"self\":[%.2f,%.2f],\"own\":{\"cd\":%.2f,\"has\":%d,\"items\":%d,\"report\":%d}}",
         share_num(ViewSwitch::g_self_x), share_num(ViewSwitch::g_self_y),
         (double)AutoSkill::g_own_remain.load() / 100.0,
         AutoSkill::g_own_has.load(),
         AutoSkill::g_self_item_cnt,
         AutoSkill::g_self_is_report ? 1 : 0);
/*   2026-10-27：**必须更新 p** ——
 *   原来这里漏了 `p += n`，`p` 还停在 self/own 段的**开头**，
 *   后面降频心跳的 `total` / `closed` 全部算错位置：
 *     · `closed` 永远 false（p[-1] 是 `]` 不是 `}`）
 *     · 兜底闭合会把 `,'` 改写成 `}`，直接破坏 JSON
 */
p += n;

    


    /*   2026-10-27：**兜底闭合** —— 如果缓冲区在写入 `self/own` 时被截断，
     *   末尾的 `}}` 就写不进去，产物是半截 JSON。
     *   ★ 2026-10-28：现在闭合对象是 **s_local**（主线程独占的缓冲），
     *     不再直接动 g_share_state。
     */
    if (p >= s_local + sizeof(s_local) - 2) {
        char *q = p - 1;
        while (q > s_local && *q != '}') --q;
        p = (q > s_local) ? (q + 1) : s_local;
    }
    if (p > s_local && p[-1] != '}') {
        *p++ = '}';
    }
    *p = 0;

    /* ★★★ 2026-10-28 核心修复：一把提交 ★★★
     *   到这里 s_local 里已经是完整、有效的 JSON。
     *   锁内只做 memcpy（8192 字节约 2~3µs），然后立刻放锁。
     *   服务线程读 g_share_state 时永远看到"上一帧完整结果"或"这一帧完整结果"，
     *   不会看到中间态 —— 这就是"网页一直闪"的根因。
     */
    {
        std::lock_guard<std::mutex> lk(g_share_mtx);
        size_t local_len = (size_t)(p - s_local);
        if (local_len >= sizeof(g_share_state)) local_len = sizeof(g_share_state) - 1;
        memcpy(g_share_state, s_local, local_len);
        g_share_state[local_len] = 0;
    }

    /* 2026-10-27：**降频心跳**
     *   每 5 秒一条就够了，正常时用来确认"主线程在构建"；
     *   出错时（截断 / 未闭合）**立刻**写，不等 5 秒。
     *   ★ 2026-10-28：全部改成基于 s_local 计算 —— 判"未闭合/接近满"要判的是
     *     主线程实际构建出来的东西，而不是 g_share_state（那是"已提交"的成品，
     *     提交之前一定闭合过，用它判永远 false，日志就失去意义）。
     */
    static int64_t s_last_hb_ms = 0;
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    const int64_t now_ms = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

    const size_t total = (size_t)(p - s_local);
    const bool closed = (total >= 2 && s_local[total - 1] == '}');
    const bool near_full = (total >= sizeof(s_local) - 128);

    if (!closed) {
        share_log("BuildJson 未闭合! total=%zu 最后 8 字节: %.8s",
                  total, total >= 8 ? s_local + total - 8 : "(空)");
    } else if (near_full) {
        share_log("BuildJson 接近满! total=%zu / %zu",
                  total, sizeof(s_local));
    } else if (now_ms - s_last_hb_ms >= 5000) {
        s_last_hb_ms = now_ms;
        share_log("BuildJson ok total=%zu svs_n=%d hunter=%d",
                  total, (int)g_share_ent_n,
                  (strstr(s_local, "\"hunter\":null") ? 0 : 1));
    }
}   // ← ShareMapBuildJson 在这里闭合（放回函数内）

// 2026-10-26：**必须写满** —— 之前只 write 一次，大响应会被截断，
//   浏览器看到 Content-Length 和实际字节数不一致就整个请求失败（页面显示"断开"）。
static bool ShareMapWriteAll(int fd, const char *buf, int len)
{
    int off = 0;
    int guard = 0;
    while (off < len) {
        /*   2026-10-26：**用 send + MSG_NOSIGNAL**
         *   之前用 write()：对端浏览器提前关连接时，write 可能一直阻塞，
         *   整个服务线程就卡死在那一条连接上 —— 后面所有请求全变成超时
         *   （netstat 里一堆 CLOSE_WAIT，接收队列还有数据没人读，就是这个症状）。
         *   现在：不触发 SIGPIPE，发送超时 3 秒（accept 后设置），超时就放弃这条连接。
         */
        ssize_t w = send(fd, buf + off, (size_t)(len - off), MSG_NOSIGNAL);
        if (w <= 0) return false;
        off += (int)w;
        if (++guard > 10000) return false;
    }
    return true;
}

static void ShareMapHttpReply(int fd, const char *ctype, const char *body, int len)
{
    char hdr[256];
    int hn = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n", ctype, len);
    if (!ShareMapWriteAll(fd, hdr, hn)) return;
    if (body && len > 0) ShareMapWriteAll(fd, body, len);
}

// 百分号解码（浏览器会把自己的中文名转成 %E5%86%9B…）
static void ShareMapUrlDecode(const char *in, char *out, int cap)
{
    int o = 0;
    for (int i = 0; in[i] && o < cap - 1; ++i) {
        if (in[i] == '%' && isxdigit((unsigned char)in[i+1]) && isxdigit((unsigned char)in[i+2])) {
            char h[3] = { in[i+1], in[i+2], 0 };
            out[o++] = (char)strtol(h, nullptr, 16);
            i += 2;
        } else {
            out[o++] = in[i];
        }
    }
    out[o] = 0;
}

static void ShareMapServeFile(int fd, const char *dir, const char *name)
{
    char dec[192];
    ShareMapUrlDecode(name, dec, sizeof(dec));
    // 2026-10-26：网页传的是地图/角色**名字**（没有扩展名），这里补成 .png
    if (!strchr(dec, '.')) {
        size_t dl = strlen(dec);
        if (dl + 5 < sizeof(dec)) { dec[dl] = '.'; dec[dl+1]='p'; dec[dl+2]='n'; dec[dl+3]='g'; dec[dl+4]=0; }
    }
    /*   2026-10-26：**内置资源优先** —— 图片已经编进程序，不再依赖 /data/adb 下的文件 */
    for (int i = 0; i < kShareAssetCount; ++i) {
        if (strcmp(kShareAssets[i].name, dec) == 0) {
    // ★ 2026-10-28：给浏览器真缓存 1 小时。
    //   原来走 ShareMapHttpReply，它一律带 Cache-Control: no-store，
    //   而前端每 100ms 重建一次 innerHTML，头像会被反复重新请求 → 左面板一直闪。
    char hdr[256];
    int hn = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
        "Cache-Control: public, max-age=3600\r\nConnection: close\r\n\r\n",
        kShareAssets[i].ctype, (int)kShareAssets[i].len);
    if (!ShareMapWriteAll(fd, hdr, hn)) return;
    ShareMapWriteAll(fd, (const char *)kShareAssets[i].data, (int)kShareAssets[i].len);
    return;
}
    }
    /*   2026-10-26：**只走内置资源**（不再读磁盘、不在外部留文件） */
    (void)dir; (void)name;
    ShareMapHttpReply(fd, "text/plain", "not found", 9);
}

// 内置网页（轮询 /state，300ms 一次）
static const char *kShareHtml = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<title>共享地图</title><style>
*{box-sizing:border-box}
html,body{margin:0;height:100%;background:#0d1117;color:#e6edf3;font:13px system-ui;overflow:hidden}
#bar{padding:8px 12px;background:#161b22;display:flex;gap:14px;align-items:center}
#left{position:absolute;left:0;top:44px;bottom:0;width:190px;padding:8px;overflow:auto;background:#0d1117ee;border-right:1px solid #30363d}
#wrap{position:absolute;left:190px;right:0;top:44px;bottom:0;overflow:hidden}
canvas{display:block;width:100%;height:100%}
#card{margin-bottom:8px;padding:6px;border:1px solid #30363d;border-radius:6px}
#card img{width:34px;height:34px;float:left;margin-right:6px}
#set{position:absolute;right:8px;top:52px;background:#161b22f2;border:1px solid #30363d;border-radius:8px;font-size:12px;width:220px}
#set h4{margin:0;padding:6px 10px;cursor:pointer;background:#21262d;border-radius:8px 8px 0 0}
#setbd{padding:8px 10px;display:none;line-height:1.9}
#set.open #setbd{display:block}
.row{display:flex;justify-content:space-between;align-items:center;gap:6px}
input[type=range]{width:200px}
.num-in{width:64px;background:#0d1117;color:#e6edf3;border:1px solid #30363d;border-radius:4px;padding:2px 4px;font-size:12px;text-align:right;font-family:inherit}
.num-in:focus{outline:none;border-color:#58a6ff}
.b{width:100%;margin-top:6px;padding:4px;background:#21262d;border:1px solid #30363d;color:#e6edf3;border-radius:6px;cursor:pointer}
#pre{position:absolute;inset:0;display:none;align-items:center;justify-content:center;flex-direction:column;gap:10px}
#pre img{width:120px;height:120px}
</style></head><body>
<div id="bar"><b id="map">--</b><span id="st">连接中…</span></div>
<div id="left"></div>
<div id="wrap"><canvas id="cv"></canvas>
  <div id="pre"><img id="prei" src=""><div id="pren">--</div><div style="opacity:.6">赛前预知</div></div>
</div>
<div id="set">
 <h4 onclick="this.parentNode.classList.toggle('open')">地图设置 <span style="float:right">▾</span></h4>
 <div id="setbd">
  <div class="row"><span>偏移 X</span><input type="number" class="num-in" id="v_ox" step="1"></div>
<input id="ox2" type="range" min="-800" max="800" step="1">
<div class="row"><span>偏移 Y</span><input type="number" class="num-in" id="v_oy" step="1"></div>
<input id="oy2" type="range" min="-800" max="800" step="1">
<div class="row"><span>缩放 X</span><input type="number" class="num-in" id="v_kx" step="0.01"></div>
<input id="kx" type="range" min="0.2" max="3" step="0.01">
<div class="row"><span>缩放 Y</span><input type="number" class="num-in" id="v_ky" step="0.01"></div>
<input id="ky" type="range" min="0.2" max="3" step="0.01">
<div class="row"><span>地图大小</span><input type="number" class="num-in" id="v_ms" step="0.01"></div>
<input id="ms" type="range" min="0.3" max="4" step="0.01">
  <div class="row"><label><input id="flipy" type="checkbox"> 上下翻转</label>
                  <label><input id="flipx" type="checkbox"> 左右翻转</label></div>
  <div class="row"><label><input id="show_items" type="checkbox"> 地图显示道具</label>
                <label><input id="show_talents" type="checkbox"> 地图显示天赋/CD</label></div>
  <button class="b" id="rst">重置设置</button>
 </div>
</div>
<script>
const cv=document.getElementById('cv'),ctx=cv.getContext('2d');
let img=null,imgName='',lastGoodImg=null;
const pcache={},lastPos={},angleCache={};
function portImg(n){if(!pcache[n]){const i=new Image();i.src='/portrait/'+encodeURIComponent(n);pcache[n]=i;}return pcache[n];}
/* 2026-10-27：三参数职责拆分，语义变了，旧校准作废 */
const CAL_VER = 6;  // 3：worldW/worldH 对齐算法变了，旧校准作废
if ((parseInt(localStorage.getItem('gxdt_ver')||'0',10)) < CAL_VER) {
  localStorage.removeItem('gxdt');
  localStorage.setItem('gxdt_ver', String(CAL_VER));
}
const DEF={kx:1,ky:1,ox:0,oy:0,ms:1,flipx:false,flipy:false,show_items:false,show_talents:false};
let K=Object.assign({},DEF),curScene=-1;

// 2026-10-28：**换局时确定一次世界中心，之后固定**
//   为什么不能每帧算：用 min/max(可见点) 时，你自己走到边缘，range 变大，所有点重新缩放 → 队友"整体偏移"
//   这里用**密码机的平均位置**作为地图中心（密码机分布对称，平均位置接近真实地图中心）
//   锁定后不再重算 —— 换局时 scene 变化才会重新算
const mapSceneCenter = {};
// 滑块 id -> K 里的字段名（ox2/oy2 因为 HTML 里已经有 id="ox2"/"oy2"，避免和变量撞名）
const ID2KEY={kx:'kx',ky:'ky',ox2:'ox',oy2:'oy',ms:'ms'};

// ★ 2026-10-28：道具图标缓存（和头像一样只建一次 Image 对象）
const icache={};
function iconImg(k){if(!icache[k]){const i=new Image();i.src='/icon/'+encodeURIComponent(k);icache[k]=i;}return icache[k];}

function allCal(){
  try{return JSON.parse(localStorage.getItem('gxdt')||'{}');}catch(_){return {};}
}
function saveCal(){
  try{const a=allCal();a[curScene]=K;localStorage.setItem('gxdt',JSON.stringify(a));}catch(_){}
}

// 2026-10-27：**去掉 `if(sc===curScene)return` 的副作用** ——
//   原来 curScene 在**加载前**赋值，中途如果抛异常，curScene 已变，
//   下次同场景调用直接早退 -> 滑块留在错误位置。
//   现在 curScene 在**加载完成后**才更新。
function loadCal(sc){
  /* ★ 2026-10-28：无效 scene（-1/undefined/null）直接 return
   *   后端发 `{"scene":-1}` 占位帧时，不能拿它去查校准表 ——
   *   查不到就 K=DEF，用户设过的缩放/偏移会被重置。
   *   下一帧正常数据又加载回来 → 地图在 DEF 和用户配置之间来回跳。
   */
  if (sc === undefined || sc === null || sc < 0) return;
  if(sc===curScene)return;
  const a=allCal(); K=Object.assign({},DEF,a[sc]||{});
  for (const s of SLIDERS) {
  const se = document.getElementById(s.slider);
  if (se) se.value = String(Math.max(s.min, Math.min(s.max, +K[s.key])));
}
  document.getElementById('flipx').checked=!!K.flipx;
document.getElementById('flipy').checked=!!K.flipy;
document.getElementById('show_items').checked=!!K.show_items;
document.getElementById('show_talents').checked=!!K.show_talents;
  syncLabels();
  curScene=sc;   // ★ 加载成功后才记，防止中途异常留下"半加载"状态
}

// 2026-10-27：**修复 DOM id**
//   HTML 里的标签 id 是 v_kx / v_ky / v_ox / v_oy / v_ms，
//   原代码请求的 kx_v / ky_v / ... 一个都不存在 -> 每次调都抛 TypeError ->
//   滑块回调里的 saveCal() 根本走不到 -> 刷新后设置全丢。
// 滑块配置：id 对应关系 + 上下限（滑块范围；数字输入框不受此限制）
const SLIDERS = [
  { slider:'ox2', num:'v_ox', key:'ox', isInt:true,  min:-800, max:800 },
  { slider:'oy2', num:'v_oy', key:'oy', isInt:true,  min:-800, max:800 },
  { slider:'kx',  num:'v_kx', key:'kx', isInt:false, min:0.2,  max:3   },
  { slider:'ky',  num:'v_ky', key:'ky', isInt:false, min:0.2,  max:3   },
  { slider:'ms',  num:'v_ms', key:'ms', isInt:false, min:0.3,  max:4   },
];

// K -> 数字输入框（滑块位置由调用方决定是否同步）
function syncLabels(){
  for (const s of SLIDERS) {
    const ni = document.getElementById(s.num);
    if (!ni) continue;
    const v = +K[s.key];
    ni.value = s.isInt ? String(Math.round(v)) : v.toFixed(2);
  }
}

// 数字输入框值写回 K，同时同步滑块位置（越界夹紧）
function setFromNumber(s, raw){
  const t = String(raw).trim();
  if (t === '' || t === '-' || t === '.' || t === '-.') return;
  const v = parseFloat(t);
  if (!isFinite(v)) return;
  K[s.key] = s.isInt ? Math.round(v) : v;
  const se = document.getElementById(s.slider);
  if (se) se.value = String(Math.max(s.min, Math.min(s.max, K[s.key])));
  saveCal();
}

// 滑块事件：拖动 -> 写 K + 更新数字框
for (const s of SLIDERS) {
  const se = document.getElementById(s.slider);
  if (!se) continue;
  se.oninput = () => {
    const v = parseFloat(se.value);
    if (!isFinite(v)) return;
    K[s.key] = v;
    syncLabels();
    saveCal();
  };
}

// 数字框事件：手动输入 -> 写 K + 同步滑块（实时生效）
for (const s of SLIDERS) {
  const ni = document.getElementById(s.num);
  if (!ni) continue;
  ni.addEventListener('input', () => setFromNumber(s, ni.value));
  ni.addEventListener('change', () => setFromNumber(s, ni.value));
}

document.getElementById('flipx').onchange=e=>{K.flipx=e.target.checked;saveCal();};
document.getElementById('flipy').onchange=e=>{K.flipy=e.target.checked;saveCal();};
document.getElementById('show_items').onchange=e=>{K.show_items=e.target.checked;saveCal();};
document.getElementById('show_talents').onchange=e=>{K.show_talents=e.target.checked;saveCal();};
// 2026-10-27：**重置按钮修法**
//   原来：`loadCal(curScene=9999);loadCal(curScene)`
//   第一次 loadCal(9999) 里 curScene 被设成 9999，然后 `if(sc===curScene)` 早退
//   第二次 loadCal(9999) 同样早退 -> 滑块/标签都没刷，用户看到"点了没反应"
//   现在：直接手动把 K 写进滑块和标签，不绕 loadCal。
document.getElementById('rst').onclick=()=>{
  K=Object.assign({},DEF);
  saveCal();
  for (const s of SLIDERS) {
    const se = document.getElementById(s.slider);
    if (se) se.value = String(Math.max(s.min, Math.min(s.max, +K[s.key])));
  }
  document.getElementById('flipx').checked=false;
  document.getElementById('flipy').checked=false;
  document.getElementById('show_items').checked=false;
  document.getElementById('show_talents').checked=false;
  syncLabels();
};
function fit(){
  const dpr=window.devicePixelRatio||1;
  const w=Math.max(1,cv.clientWidth),h=Math.max(1,cv.clientHeight);
  const W=Math.round(w*dpr),H=Math.round(h*dpr);
  if(cv.width!==W||cv.height!==H){cv.width=W;cv.height=H;}
  ctx.setTransform(dpr,0,0,dpr,0,0);
  return [w,h];
}
async function tick(){
  try{
    /* 2026-10-27：**先写 st，再 fetch** ——
     *   原来 st 只在 fetch 成功/失败之后更新，fetch 一旦挂起
     *   用户就永远看到初始的「连接中…」，分不清是"脚本没跑"还是"请求挂住"。
     *   现在每次 tick 立刻打时间戳，挂住时用户会看到
     *   「12:34:56 请求中」一直不动 —— 那就确诊是 fetch pending。
     */
    document.getElementById('st').textContent = new Date().toLocaleTimeString() + ' 请求中';

    // 3 秒超时：pending 会被 abort -> 走 catch -> 显示错误原因
    const ctrl = new AbortController();
    const to = setTimeout(()=>ctrl.abort('timeout'), 3000);
    const r = await fetch('/state', {cache:'no-store', signal: ctrl.signal});
    clearTimeout(to);

    const text = await r.text();
    let d;
    try { d = JSON.parse(text); }
    catch(je){
      document.getElementById('st').textContent =
        'JSON无效(' + text.length + 'B): ' + text.slice(0, 80);
      return;
    }
    document.getElementById('map').textContent = d.map + ' (' + d.scene + ')';
    document.getElementById('st').textContent = new Date().toLocaleTimeString();
    loadCal(d.scene);
    if(d.map && d.map!==imgName){
  imgName = d.map;
  const newImg = new Image();
  // ★ 加载完成才替换 lastGoodImg —— 加载期间继续显示上一张，避免"新图没到、旧图已丢"的空白窗口
  newImg.onload  = () => { lastGoodImg = newImg; };
  newImg.onerror = () => { /* 404 时宁可继续显示上一张，也不清空 */ };
  newImg.src = '/map/' + encodeURIComponent(imgName);
  img = newImg;
}
    render(d);
  }catch(e){
    document.getElementById('st').textContent =
      'ERR ' + (e && e.name ? e.name + ':' : '') + (e && e.message ? e.message : String(e));
  }
}
function render(d){
 const [W,H]=fit();

 if (!d || typeof d !== 'object' || d.scene === undefined || d.scene < 0) {
   return;
 }

 /* ★★★ 2026-10-28 数据完整性闸门 ★★★
  *   后端主线程改成"一把 memcpy"之后，理论上不会再发半截 JSON，
  *   但这里再加一道防御：
  *   如果一帧里"任何东西都没有"（ciphers/svs/hunter/hatch 全空），
  *   说明这一帧的数据不全 —— 直接返回，**不清屏**，
  *   保持上一帧画面。
  *
  *   为什么必须放在 clearRect 之前：
  *     原代码 `ctx.clearRect(0,0,W,H);` 然后 `if(!xs.length){leftPanel;return;}`
  *     → 数据不全的那一帧先清屏再 return → 屏幕上全空
  *     → 下一帧 100ms 后有数据 → 重新画 → 视觉上就是"一直闪"
  */
 const _hasCiphers = Array.isArray(d.ciphers) && d.ciphers.length > 0;
 const _hasSvs     = Array.isArray(d.svs)     && d.svs.length     > 0;
 const _hasHunter  = d.hunter && Array.isArray(d.hunter) && d.hunter.length >= 3;
 const _hasHatch   = Array.isArray(d.hatch)   && d.hatch.length   >= 2 && (d.hatch[0] || d.hatch[1]);
 if (!_hasCiphers && !_hasSvs && !_hasHunter && !_hasHatch) {
   return;   // 完全空帧：不清屏，保持上一帧
 }

 const noCipher=!d.ciphers||d.ciphers.length===0;
/* ★ 2026-10-28：赛前预知改成优先用后端 d.prophet
 *   准备阶段 data[] 里没有 camp==1（求生者准备阶段 units 只有道具/HallComponentUnit），
 *   所以 d.hunter 是 null；辅助 UI 的"预知监管"那行是 prophet_text，一直有内容。
 *   后端新增了 d.prophet 字段（= prophet_text 去掉方括号），这里用它兜底。
 *   顺序：d.hunter 优先（有坐标/天赋/CD），没有就用 d.prophet（只有名字）。
 */
const preName = d.hunter ? d.hunter[0] : (d.prophet || '');
const pre = (d.in_prep === 1) && preName && preName.length > 0;
const preEl=document.getElementById('pre');
if(pre){                       // 赛前预知：不画地图，只显示监管
  preEl.style.display='flex';
  const preiEl = document.getElementById('prei');
  const wantSrc = '/portrait/' + encodeURIComponent(preName);
  // ★ 只在 src 真的变了时才赋值，避免每 100ms 重新请求头像
  if (preiEl.getAttribute('data-src') !== wantSrc) {
    preiEl.setAttribute('data-src', wantSrc);
    preiEl.onerror = () => { preiEl.style.visibility = 'hidden'; };
    preiEl.onload  = () => { preiEl.style.visibility = 'visible';  };
    preiEl.src = wantSrc;
  }
  document.getElementById('pren').textContent = preName;
  ctx.clearRect(0,0,W,H);
  leftPanel(d,null); return;
}
preEl.style.display='none';
ctx.clearRect(0,0,W,H);

 /* 2026-10-27：三参数职责拆分
      偏移 X/Y  -> 地图 + 所有点的整体平移
      缩放 X/Y  -> 点的 X/Y 分布（不影响地图大小）
      地图大小  -> 背景图片大小（不影响点）                        */
/* 2026-10-28 第三版：**固定世界范围** + **朝向不跟随翻转**
 *
 *   上一版（动态范围）的问题（用户实测反馈）：
 *     · 自己在边缘走 → 世界范围变大 → 队友"整体偏移"
 *     · 位置有偏差（中心估计不准）
 *   修正：
 *     · 范围固定为 worldW/worldH（每张地图一个固定值）
 *     · 中心在**换局时**用密码机平均位置确定一次，之后固定
 *     · 你走到边缘 → 只有你自己的点移动，地图和队友都不动
 */
 /* 2026-10-28 第四版：
 *   1. **图片按原图比例显示**（不拉伸）
 *   2. **偏移X/Y、缩放X/Y 只对"点"起作用**（图片完全不受它们影响）
 *   3. **只有地图大小(K.ms) 影响图片**
 *   4. 朝向**不跟随翻转**（flip 后位置翻，箭头方向不变）
 *
 *   对齐约定：
 *     · 图片居中（mapX/mapY），大小 = 图片原始宽高比 × REF × K.ms
 *     · 点：世界范围 [cx0±wW/2, cz0±wH/2] 归一化到 [0,1]，映射到图片区域
 *     · 用户通过 K.kx/ky 缩放点的分布，K.ox/oy 平移点的位置
 *     · 若 wW/wH 和图片宽高比不一致，点会轻微变形 → 用户用 K.kx/ky 修正
 */
 const REF  = Math.min(W, H);
 const wW   = d.worldW || 1400;
 const wH   = d.worldH || 1400;

// 收集坐标（只用于判断"没数据就不画"）
 const xs=[],ys=[];
 const push=a=>{
   if(!a)return;
   const x=+a[0],y=+a[1];
   if(isFinite(x)&&isFinite(y)&&(x||y)){xs.push(x);ys.push(y);}
 };
 (d.ciphers||[]).forEach(c=>push(c));
 push(d.hatch); push(d.self);
 (d.svs||[]).forEach(v=>push([v[1],v[2]]));
 if(d.hunter)push([d.hunter[1],d.hunter[2]]);
 if(!xs.length){leftPanel(d,null);return;}

// ---- 世界中心：换局时用**密码机平均位置**锁定一次 ----
 let cx0 = 0, cz0 = 0;
 {
   const sid = d.scene;
   if (sid !== undefined && sid >= 0) {
     const slot = mapSceneCenter[sid] || (mapSceneCenter[sid] = { cx: 0, cz: 0, locked: false });
     if (!slot.locked) {
       const cs = d.ciphers || [];
       if (cs.length >= 2) {
         let sx = 0, sz = 0;
         for (let i = 0; i < cs.length; i++) { sx += +cs[i][0]; sz += +cs[i][1]; }
         slot.cx = sx / cs.length;
         slot.cz = sz / cs.length;
         slot.locked = true;
       }
     }
     cx0 = slot.cx;
     cz0 = slot.cz;
   }
 }

// ---- 图片尺寸：**按原图宽高比**，长边 = REF * K.ms ----
 const drawSrc = (img && img.complete && img.naturalWidth) ? img : lastGoodImg;
 let mapW, mapH;
 if (drawSrc && drawSrc.naturalWidth) {
   const iw = drawSrc.naturalWidth, ih = drawSrc.naturalHeight;
   const imax = Math.max(iw, ih) || 1;
   mapW = (iw / imax) * REF * K.ms;
   mapH = (ih / imax) * REF * K.ms;
 } else {
   // 图片没加载：用 wW/wH 比例兜底（不影响最终结果，加载完会重算）
   const imax = Math.max(wW, wH) || 1;
   mapW = (wW / imax) * REF * K.ms;
   mapH = (wH / imax) * REF * K.ms;
 }
 const mapX = W/2 - mapW/2;   // 图片永远居中，K.ox/K.oy 不影响
 const mapY = H/2 - mapH/2;

// ---- 画图片（不翻转）----
 if (drawSrc && drawSrc.naturalWidth) {
   ctx.drawImage(drawSrc, mapX, mapY, mapW, mapH);
 }

// ---- 点映射：世界范围 → 归一化 → 缩放(K.kx/ky) → 翻转(flip) → 图片像素 → 偏移(K.ox/oy) ----
 const X = v => {
   let t = (v - cx0) / wW + 0.5;         // -0.5..+0.5 -> 0..1
   t = (t - 0.5) * K.kx + 0.5;            // 缩放（相对中心）
   if (K.flipx) t = 1 - t;                 // 翻转（在图片内镜像）
   return mapX + t * mapW + K.ox;         // 图片像素 + 用户偏移（屏幕像素）
 };
 const Y = v => {
   let t = (v - cz0) / wH + 0.5;
   t = (t - 0.5) * K.ky + 0.5;
   if (K.flipy) t = 1 - t;
   return mapY + t * mapH + K.oy;
 };

 // 密码机
 (d.ciphers||[]).forEach(c=>{
   const cxp=X(c[0]),cyp=Y(c[1]),r=7,prog=Math.max(0,Math.min(100,c[2]));
   const col=prog<40?'#3fb950':(prog<70?'#d29922':'#f85149');
   if(prog<=0.01){ctx.fillStyle='#2ea043';ctx.beginPath();ctx.arc(cxp,cyp,4,0,7);ctx.fill();return;}
   ctx.beginPath();ctx.arc(cxp,cyp,r,0,Math.PI*2);ctx.strokeStyle='#30363d';ctx.lineWidth=2;ctx.stroke();
   ctx.beginPath();ctx.arc(cxp,cyp,r,-Math.PI/2,-Math.PI/2+Math.PI*2*prog/100);ctx.strokeStyle=col;ctx.lineWidth=2;ctx.stroke();
   ctx.fillStyle=col;ctx.font='9px system-ui';ctx.textAlign='center';ctx.fillText(Math.round(prog),cxp,cyp+3);
 });

 // 人物（含朝向扇形）
 const people=[];
 (d.svs||[]).forEach(v=>people.push({n:v[0],x:X(v[1]),y:Y(v[2]),h:0,
     item:v[3]||'',mode:(v[4]|0),rem:(v[5]|0),rate:(+v[6]||0),dx:(+v[7]||0),dz:(+v[8]||0),
     tal:v[9]||'',cd:v[10]||'',icon:v[11]||'',fw:(+v[12]||0),skcd:(+v[13]||0),hasfw:(v[14]|0)}));
 if(d.hunter)people.push({n:d.hunter[0],x:X(d.hunter[1]),y:Y(d.hunter[2]),h:1,
     item:'',mode:0,rem:0,rate:0,dx:(+d.hunter[5]||0),dz:(+d.hunter[6]||0),
     tal:d.hunter[3]||'',cd:d.hunter[4]||'',icon:'',fw:0,skcd:0,hasfw:0});
     /* 2026-10-27：从属单位（女巫信徒 / 巡视者 / 机械玩偶…）也画 —— h=2 用橙色 */
(d.others||[]).forEach(v=>people.push({n:v[0],x:X(v[1]),y:Y(v[2]),h:2,
    item:'',mode:0,rem:0,rate:0,dx:0,dz:0,
    tal:'',cd:'',icon:'',fw:0,skcd:0,hasfw:0}));
    

 people.forEach(p=>{
 const key=p.n, lp=lastPos[key];
 let a=(angleCache[key]!==undefined)?angleCache[key]:null;
 // 2026-10-28 修：原先用 sy/sx（旧投影量），新投影段已删除 → ReferenceError。
 // 朝向只需**方向**，不需要绝对缩放，所以用轴向缩放比例和翻转方向即可。
if(p.dx||p.dz){
  /* 2026-10-28：朝向**跟随** K.flipx/K.flipy
   *   · flipx 关、flipy 关：世界 +X → 箭头朝右；世界 +Z → 箭头朝下
   *   · flipx 开：世界 +X → 箭头朝左（位置也翻到左边，视觉一致）
   *   · flipy 开：世界 +Z → 箭头朝上（位置也翻到上方，视觉一致）
   *   (mapH/wH)、(mapW/wW) 是正比例因子，保留是为了箭头方向匹配屏幕实际位移方向
   */
  a = Math.atan2(p.dz * (K.flipy ? -1 : 1) * (mapH / wH),
                 p.dx * (K.flipx ? -1 : 1) * (mapW / wW));
} else if(lp){
  const dx=p.x-lp.x,dy=p.y-lp.y;
  if(dx*dx+dy*dy>2) a=Math.atan2(dy,dx);
}
  if(a!==null) angleCache[key]=a;
  lastPos[key]={x:p.x,y:p.y};

  // ① 先画头像（半透明背景圆，18px 半径——刚好包住 22x22 头像 + 少量边距）
  const R = 13;                     // 覆盖头像的半径（头像 22x22 -> 半 11，留 2px 边距）
  ctx.beginPath();
  ctx.arc(p.x, p.y - 15, R, 0, Math.PI * 2);   // 圆心上移 15px（头像中心在 y-15，因为头像从 y-26 画到 y-4）
  ctx.fillStyle = 'rgba(0,0,0,0.35)';
  ctx.fill();

  // ② 画头像图片（22x22，圆心位置不变）
  const im=portImg(p.n);
  if(im&&im.complete&&im.naturalWidth)ctx.drawImage(im,p.x-11,p.y-26,22,22);
  else{ctx.fillStyle='#3fb950';ctx.beginPath();ctx.arc(p.x,p.y-15,6,0,7);ctx.fill();}

  // ③ 头像**外面**画箭头（底边贴圆边、尖端朝 a 方向、随朝向沿圆边转动）
//   底边两点在半径 R 处（正好是圆边），所以是贴合的；外伸 L 是原箭头尺寸的 1/3。
if(a!==null){
  const R  = 13;                // 圆半径（与上面黑底圆一致）
  const L  = R / 3;             // 外伸长度 ≈4.33，用户要求的 1/3
  const halfW = 0.34;           // 底边半角（约 19.5°），底宽 ≈8.6px
  const cx = p.x, cy = p.y - 15;
  const tipR  = R + L;
  const tipX  = cx + Math.cos(a) * tipR;
  const tipY  = cy + Math.sin(a) * tipR;
  const b1X   = cx + Math.cos(a + halfW) * R;
  const b1Y   = cy + Math.sin(a + halfW) * R;
  const b2X   = cx + Math.cos(a - halfW) * R;
  const b2Y   = cy + Math.sin(a - halfW) * R;
  ctx.beginPath();
  ctx.moveTo(tipX, tipY);
  ctx.lineTo(b1X,  b1Y);
  ctx.lineTo(b2X,  b2Y);
  ctx.closePath();
  // 稍微不透明一点，因为面积小了
  ctx.fillStyle = p.h===1 ? 'rgba(248,81,73,.95)'
           : p.h===2 ? 'rgba(210,153,34,.95)'
                     : 'rgba(255,255,255,.95)';
  ctx.fill();
}
  
  // ★ 2026-10-28：头像上方显示**图片**道具（和左面板同一套图标）+
//   天赋/CD 文本（地图开关打开时才画）。
//
//   布局（从下往上）：
//     头像圆    p.y - 26 .. p.y - 4
//     箭头      最多到 p.y - 32.33（朝上）
//     道具图标  24x24，底边 p.y - 36，顶边 p.y - 60
//     天赋文本  p.y - 66
//     CD 文本   p.y - 78
//   朝上的箭头到 p.y - 32.33，离道具底边 3.67px，不冲突。
if (K.show_items && p.icon) {
  const iSz = 24;
  const ix = p.x - iSz / 2;
  const iy = p.y - 26 - iSz - 10;        // 头像顶上方 10px
  const ic = iconImg(p.icon);
  if (ic && ic.complete && ic.naturalWidth) {
    ctx.drawImage(ic, ix, iy, iSz, iSz);
  } else {
    ctx.fillStyle = 'rgba(60,60,70,0.8)';
    ctx.fillRect(ix, iy, iSz, iSz);
  }
  // 右下角 badge：数量 或 百分比
  let badge = '';
  if (p.mode === 1 && p.rem >= 0) badge = String(p.rem);
  else if ((p.mode === 2 || p.mode === 3) && p.rate > 0) badge = Math.round(p.rate * 100) + '%';
  if (badge) {
    ctx.font = 'bold 10px system-ui';
    ctx.textAlign = 'center';
    ctx.lineWidth = 3;
    ctx.strokeStyle = '#0d1117';
    ctx.strokeText(badge, ix + iSz - 2, iy + iSz - 2);
    ctx.fillStyle = '#ffd700';
    ctx.fillText(badge, ix + iSz - 2, iy + iSz - 2);
  }
}
// 天赋文本（地图开关打开时才显示）
if (K.show_talents && p.tal) {
  ctx.font = '10px system-ui';
  ctx.textAlign = 'center';
  // 有图标时天赋在图标上方；没图标时贴头像顶
  const talY = (K.show_items && p.icon) ? (p.y - 26 - 24 - 10 - 6) : (p.y - 26 - 8);
  ctx.fillStyle = '#e6edf3';
  ctx.fillText(p.tal, p.x, talY);
  // CD 一行：特质文本（监管用）+ 飞轮/技能剩余
  let cdLine = p.cd || '';
  if (p.hasfw && p.fw > 0) cdLine += (cdLine ? ' ' : '') + '飞轮' + Math.round(p.fw) + 's';
  if (p.skcd > 0)         cdLine += (cdLine ? ' ' : '') + '技能' + Math.round(p.skcd) + 's';
  if (cdLine) {
    ctx.font = '9px system-ui';
    ctx.fillStyle = '#a0a0b0';
    ctx.fillText(cdLine, p.x, talY - 12);
  }
}

  // ④ 最后画名字（在头像下方）
  ctx.fillStyle=p.h?'#f85149':'#e6edf3';
  ctx.font='12px system-ui';ctx.textAlign='center';ctx.fillText(p.n,p.x,p.y+18);
});

 // 地窖
 if(d.hatch&&(d.hatch[0]||d.hatch[1])){
   const hx=X(d.hatch[0]),hy=Y(d.hatch[1]);
   ctx.strokeStyle='#d29922';ctx.lineWidth=2;ctx.beginPath();ctx.arc(hx,hy,7,0,7);ctx.stroke();
   ctx.fillStyle='#d29922';ctx.textAlign='center';ctx.fillText('地窖',hx,hy-12);
 }

 leftPanel(d,{X,Y});
}
let lastLeftKey = '';
function leftPanel(d,tr){
 // ★ 2026-10-28：数据没变就不重建 DOM —— 配合上面的图片缓存，左面板不再闪
 let key = (d.talents || '') + '|';
 if (d.hunter) key += d.hunter.join(',') + '|';
 (d.svs || []).forEach(v => { key += v.join(',') + '|'; });
 if (key === lastLeftKey) return;
 lastLeftKey = key;

 const box=document.getElementById('left');
 let h='';
 if(d.talents) h+=`<div style="padding:4px 2px;border-bottom:1px solid #30363d;margin-bottom:6px">天赋：${d.talents}</div>`;
 // 2026-10-26：监管者置顶
 if(d.hunter){
   const htal=d.hunter[3]||'—', hcd=d.hunter[4]||'—';
   h+=`<div id="card" style="border-color:#f85149"><img src="/portrait/${encodeURIComponent(d.hunter[0])}">
       <div><b style="color:#f85149">${d.hunter[0]}</b>（监管）
       <div>天赋：${htal}</div><div style="opacity:.7">技能：${hcd||'—'}</div></div>
       <div style="clear:both"></div></div>`;
 }
 // 4 个求生依次在下，新出现的（机器人/额外角色）自然排在后面
 (d.svs||[]).forEach(v=>{
   const nm=v[3]||'', md=(v[4]|0), rem=(v[5]|0), rate=Math.max(0,Math.min(1,+v[6]||0));
   const tal=v[9]||'—', cd=v[10]||'—';
   let item='—', bar='';
   if(nm){
     if(md===1) item=nm+' ×'+rem;
     else if(md===2) item=nm+' 耐久 '+Math.round(rate*100)+'%';
     else if(md===3) item=nm+' 充能 '+Math.round(rate*100)+'%';
     else item=nm;
     if(md===2||md===3){
       const c=rate<0.4?'#f85149':(rate<0.7?'#d29922':'#3fb950');
       bar=`<div style="height:4px;background:#30363d;margin:2px 0"><div style="height:4px;width:${Math.round(rate*100)}%;background:${c}"></div></div>`;
     }
   }
   const ic=(v[11]||'');
   let badge='';
   if(ic){
     if(md===1&&rem>=0) badge=`<b style="position:absolute;right:-3px;bottom:-3px;background:#0d1117;border:1px solid #30363d;border-radius:8px;font-size:10px;padding:0 3px">${rem}</b>`;
     else if(md===2||md===3) badge=`<b style="position:absolute;right:-3px;bottom:-3px;background:#0d1117;border:1px solid #30363d;border-radius:8px;font-size:10px;padding:0 3px">${Math.round(rate*100)}%</b>`;
   }
   // ★ 2026-10-28：改用 background-image 代替 <img>。
//   key 比较虽然让 DOM 不再频繁重建，但每次重建时 <img> 元素重新挂载，
//   浏览器会走一遍"元素创建→src 解析→渲染"流程，视觉上闪一下。
//   背景图不会：CSS 引擎处理背景图时不会重新解析 URL，直接复用缓存。
const icimg = ic
  ? `<span style="position:relative;display:inline-block;width:30px;height:30px;margin:2px 0;background-image:url('/icon/${encodeURIComponent(ic)}');background-size:30px 30px;background-repeat:no-repeat;background-position:center">${badge}</span>`
  : `<span>${item}</span>`;
   h+=`<div id="card"><img src="/portrait/${encodeURIComponent(v[0])}">
       <div><b>${v[0]}</b>
       <div>${icimg}</div>${bar}
       <div>天赋：${tal}</div>
       ${(+v[14])?`<div>飞轮：${(+v[12]>0)?(+v[12]).toFixed(1)+'s':'就绪'}</div>`:''}
       <div>技能：${(+v[13]>0)?(+v[13]).toFixed(1)+'s':'就绪'}</div>
       <div style="opacity:.7">CD：${cd||'—'}</div></div>
       <div style="clear:both"></div></div>`;
 });
 if(!h)h='<div style="opacity:.6">等待数据…</div>';
 box.innerHTML=h;
}
setInterval(tick,100);tick();
addEventListener('load',()=>{fit();tick();});
</script></body></html>)HTML";

static void ShareMapHandleConn(int cfd)
{
    struct timeval stv{2, 0};
    setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, &stv, sizeof(stv));
    setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &stv, sizeof(stv));

    char req[1024] = {0};
    ssize_t rn = read(cfd, req, sizeof(req) - 1);

    if (rn > 0) {
        char path[256] = {0};
        sscanf(req, "%*s %255s", path);
        share_log("请求 %s", path);

        if (!strncmp(path, "/icon/", 6)) {
            char nm[128];
            ShareMapUrlDecode(path + 6, nm, sizeof(nm));
            bool sent = false;
            for (int ii2 = 0; ii2 < kJueseIconCount; ++ii2) {
                if ((kJueseIcons[ii2].item && !strcmp(kJueseIcons[ii2].item, nm)) ||
                    (kJueseIcons[ii2].juese && !strcmp(kJueseIcons[ii2].juese, nm))) {
                    ShareMapHttpReply(cfd, "image/png",
                                      (const char *)kJueseIcons[ii2].png,
                                      (int)kJueseIcons[ii2].len);
                    sent = true; break;
                }
            }
            if (!sent) ShareMapHttpReply(cfd, "text/plain", "not found", 9);
        }
        else if (!strncmp(path, "/state", 6)) {
            /* ★★★ 关键：s_copy 必须在**栈上** ★★★
             *   原代码是 `static char s_copy[8192]`。
             *   单线程时无害；一旦改成每连接一个线程，两个线程会同时
             *   往同一个 static 里 memcpy —— 一个线程发出去的是另一个
             *   线程刚写进去的内容，而且并发 strlen 会直接数据竞争。
             *   8192 在栈上没问题（Android 主线程栈 8MB，每个 worker 也是 8MB）。
             */
            char s_copy[8192];
            int len = 0;
            {
                std::lock_guard<std::mutex> lk(g_share_mtx);
                const size_t raw = strlen(g_share_state);
                if (raw < 2 || raw >= sizeof(s_copy) ||
                    g_share_state[0] != '{' || g_share_state[raw - 1] != '}') {
                    static int bad_n = 0;
                    if (++bad_n % 50 == 1)
                        share_log("/state 无效(len=%zu) 已发占位 (累计坏 %d 次)", raw, bad_n);
                    static const char kFiller[] = "{\"scene\":-1}";
                    len = (int)sizeof(kFiller) - 1;
                    memcpy(s_copy, kFiller, len);
                } else {
                    len = (int)raw;
                    memcpy(s_copy, g_share_state, len);
                }
            }
            ShareMapHttpReply(cfd, "application/json; charset=utf-8", s_copy, len);
        }
        else if (!strncmp(path, "/map/", 5)) {
            ShareMapServeFile(cfd, kMapDir, path + 5);
        }
        else if (!strncmp(path, "/portrait/", 10)) {
            ShareMapServeFile(cfd, kPortrDir, path + 10);
        }
        else {
            ShareMapHttpReply(cfd, "text/html; charset=utf-8",
                              kShareHtml, (int)strlen(kShareHtml));
        }
        share_log("已回 %s", path);
    } else if (rn == 0) {
        share_log("read 返回 0（对端关闭）");
    } else {
        share_log("read 失败 errno=%d (%s)", errno, strerror(errno));
    }

    shutdown(cfd, SHUT_WR);
    close(cfd);
}

static void ShareMapServerLoop()
{
    signal(SIGPIPE, SIG_IGN);

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) { g_share_on = false; share_log("socket() 失败 errno=%d", errno); return; }

    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(12750);

    if (bind(lfd, (sockaddr *)&addr, sizeof(addr)) < 0) {
        share_log("bind(:12750) 失败 errno=%d —— 端口被占？", errno);
        close(lfd); g_share_on = false; return;
    }
    /* ★ listen backlog 从 16 提到 32：多设备并发请求会更密集，
     *   原来的 16 在一台设备加载页面（/ + favicon + 若干 icon）时
     *   就会把另一台设备的 /state 挤到队列尾。 */
    if (listen(lfd, 32) < 0) {
        share_log("listen() 失败 errno=%d", errno);
        close(lfd); g_share_on = false; return;
    }
    g_share_fd = lfd;
    share_log("监听 0.0.0.0:12750 成功");

    /* ★ 每个连接一个线程：
     *   · 上限 16，超过就关掉（防止异常情况下把内存耗尽）
     *   · 线程 detach，自己不持有；连接处理完就退
     *   · accept 本身仍由主循环串行做，非常快（几百 ns），不构成瓶颈
     */
    static std::atomic<int> s_active_conn{0};
    const int kMaxConn = 16;
    int served = 0;

    while (!g_share_stop.load()) {
        fd_set rf; FD_ZERO(&rf); FD_SET(lfd, &rf);
        timeval tv{0, 200000};
        if (select(lfd + 1, &rf, nullptr, nullptr, &tv) <= 0) continue;

        int cfd = accept(lfd, nullptr, nullptr);
        if (cfd < 0) continue;

        if (s_active_conn.load() >= kMaxConn) {
            close(cfd);
            continue;
        }
        s_active_conn.fetch_add(1);

        std::thread([cfd]() {
            ShareMapHandleConn(cfd);
            s_active_conn.fetch_sub(1);
        }).detach();

        if ((++served % 50) == 0) {
            share_log("已服务 %d 个请求", served);
        }
    }
    share_log("服务线程退出");
    close(lfd);
    g_share_fd = -1;
}

static void ShareMapStart()
{
    if (g_share_on.load()) return;

    /* 2026-10-27：**新会话 = 新日志**
     *   每次用户点"开启共享"（或程序启动）就 truncate 一次，
     *   否则跑一天 share.log 会涨到几 MB。
     *   文件不存在时 fopen("w") 会自动创建，不需要先 mkdir，
     *   因为 /data/adb/.d/ 已经在 GetConfigPath() 里 mkdir 过了。
     */
    { FILE *f = fopen("/data/adb/.d/share.log", "w"); if (f) fclose(f); }
    share_log("=== ShareMapStart ===");

    g_share_stop = false;
    g_share_on = true;
    ShareMapTick();
    ShareMapBuildJson();
    g_share_thr = std::thread(ShareMapServerLoop);
    g_share_thr.detach();
}

static void ShareMapStop()
{
    if (!g_share_on.load() && g_share_fd < 0) return;
    g_share_stop = true;
    g_share_on = false;
    // 让 select 立刻返回：连一下自己
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) {
        sockaddr_in a{}; a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons(12750);
        connect(fd, (sockaddr *)&a, sizeof(a));
        close(fd);
    }
}

// 2026-10-26: 程序启动就开端口（开关关掉仍会停）
// 主线程每帧调用（构建 /state）
static void ShareMapFrameTick()
{
    static bool s_started = false;
    if (!s_started) { s_started = true; ShareMapStart(); }
    if (!g_share_on.load()) return;
    ShareMapTick();
    ShareMapBuildJson();
}

static const int s_share_autostart = [](){ g_share_frame_hook = ShareMapFrameTick; return 0; }();

static void Feature_ShareMap(Theme::Palette& pal)
{
    using namespace FangUI;
    Head("共享地图", pal, "ShareMap");
    bool on = g_share_on.load();
    if (RowSwitch("开启共享", &on, pal)) {
        if (on) ShareMapStart();
        else    ShareMapStop();
    }
    ImGui::TextDisabled("端口 12750（HTTP）");
    if (g_share_on.load()) {
        ImGui::TextColored(ImVec4(0.2f, 0.85f, 0.4f, 1), "已开启：");
        ImGui::TextDisabled("  本机：http://127.0.0.1:12750/");
        ImGui::TextDisabled("  局域网：http://<本机IP>:12750/");
        ImGui::TextDisabled("当前地图：%s", g_share_mapname[0] ? g_share_mapname : "(未进入对局)");
    } else {
        ImGui::TextDisabled("未开启");
    }
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::TextDisabled("网页显示：地图/密码机进度/求生/监管/地窖/自身位置");
    ImGui::TextDisabled("关闭本开关或退出程序会自动关闭端口");
}

void Feature(Theme::Palette& pal) {
    using namespace FangUI;

    static int g_feature_sub = 0;
    static const char* kFeatureSubs[] = { "共享地图", "改视角", "小丑加速" };

    {
        int ns = Segmented("feature_sub", kFeatureSubs, 3, g_feature_sub, pal, 48.0f);
        if (ns != g_feature_sub) g_feature_sub = ns;
    }
    ImGui::Dummy(ImVec2(0, 14));

    if (g_feature_sub == 0)      Feature_ShareMap(pal);
    else if (g_feature_sub == 1) Feature_ViewSwitch(pal);
    else if (g_feature_sub == 2) Feature_JokerSpeed(pal);
}

// ---------- 主窗口 End() 之后调用的额外窗口（弹窗类） ----------
void ExtraWindows(Theme::Palette& pal) {
    (void)pal;
    // 监管前摇已内置，不再需要"选择监管"弹窗
}
} // namespace UI_Pages

void Layout_tick_UI(bool *main_thread_flag) {
    // One frame may read data[] in several UI pages/diagnostics outside Draw_Main
    // (which has its own lock). Holding the recursive mutex for the complete UI
    // tick keeps those less-obvious readers from racing read_thread's publish.
    // The mutex is recursive because Draw_Main, Settings, unit_xyz, and API-style
    // helpers can acquire the same mutex from this call path.
    std::lock_guard<std::recursive_mutex> data_frame_lock(g_data_mutex);
    static bool data_sync_v2_logged = false;
    if (!data_sync_v2_logged) {
        data_sync_v2_logged = true;
        printf("[DATA-SYNC] v2 frame-lock active; Vulkan code unchanged\n");
        fflush(stdout);
    }

    static bool volume_thread_started = false;
    if (!volume_thread_started) {
        std::thread(VolumeKeyHide).detach();
        volume_thread_started = true;
    }

    // ---- 反调试：启动时查一次，命中就不进入主逻辑 ----
    //   不做"闪退"这种显眼动作 —— 那等于告诉逆向者"这里就是检测点"。
    //   这里只是把功能静默关掉，让他调出一个能跑但什么都不做的壳。
    // ---- 安全：基线 + 周期检测 ----
    //   ① 代码段 CRC 基线：必须在**所有初始化完成之后**取，
    //      早了会把还没重定位完的内容当基线。
    //   ② 反调试改为**连续命中才判定**，且不用 ptrace ——
    //      旧版查 PTRACE_TRACEME 在 Magisk 上 100% 误报。
    static int  sec_stage = 0;
    static double sec_next = 0.0;
    if (sec_stage == 0) {
        sec_stage = 1;
        SecMem::TakeBaseline();
        // 方案 A：改格式符（最小改动）
printf("[SEC] 代码段基线已记录：%d 个可执行段\n", SecMem::Ranges_count());

// 方案 B：显式转换（如果以后 Ranges_count 改成 size_t 就不必再动）
//printf("[SEC] 代码段基线已记录：%zu 个可执行段\n",
//       (size_t)SecMem::Ranges_count());
        const char* why = Sec::ProbeOnce();
        printf("[SEC] 首次环境检查：%s\n", why ? why : "干净");
        fflush(stdout);
    } else {
        double now = ImGui::GetTime();
        if (now >= sec_next) {
            sec_next = now + 2.0;          // 每 2 秒一次，别太频繁
            if (SecMem::Verify()) {
                static bool once = false;
                if (!once) { once = true; printf("[SEC] ⚠ 代码段被修改（内存补丁）\n"); fflush(stdout); }
            }
            if (Sec::Tick()) {
                static bool once2 = false;
                if (!once2) { once2 = true; printf("[SEC] ⚠ 连续检出调试环境，相关功能已停用\n"); fflush(stdout); }
            }
        }
    }

    static bool config_loaded = false;
    if (!config_loaded) {
        config_loaded = true;
        // ★ 内置前摇表初始化。这一步以前漏了 —— InitBossList 只被两个死函数调用过，
        //   导致内置的 38 条前摇数据从来没进过 g_boss_name_idx，
        //   查表全部失败，所有监管都退到兜底值。
        
        LoadConfig();
        //   ★★ 2026-10-21 用户要求：**去掉一切自动存盘** ★★
        //     不注册 atexit、不做周期保存 —— 只有设置页「保存配置」按钮才写盘。
        //     （退出程序 / 被 kill / 掉电 都不会覆盖配置文件）
    }

    px = static_cast<float>(native_window_screen_x) / 2;
py = static_cast<float>(native_window_screen_y) / 2;
AutoPallet::set_screen_size(native_window_screen_x, native_window_screen_y);


    

    Draw_Main(ImGui::GetForegroundDrawList());

    // ★ 密码机进度悬浮窗（2026-10-12 重写）
    //   要求：透明背景 / 圆角平滑 / 每行「序号 + 进度条 + 百分比」
    //         进度条颜色跟随进度 / 进度最高的在最上方
    {
        if (g_gen_list_show) {
            PyProgress::GenListEntry gens[16];
            const int gn = PyProgress::get_list(gens, 16);
            ImDrawList *fdl = ImGui::GetForegroundDrawList();
            ImFont *fnt = ImGui::GetFont();
            const float fs = (g_gen_size < 8.0f) ? 8.0f : (g_gen_size > 90.0f ? 90.0f : g_gen_size);
            const float lh   = fs + 8.0f;          // 行高
            const float barw = fs * 4.5f;          // 进度条宽度
            const float barh = fs * 0.62f;         // 进度条高度
            const float pad  = 12.0f;
            const float nw   = fs * 1.25f;         // 序号列宽（压紧，让序号和进度条贴在一起）
            const float pw   = fs * 3.2f;          // 百分比列宽

            const float bx = (g_gen_list_x < 0) ? 0 : g_gen_list_x;
            const float by = (g_gen_list_y < 0) ? 0 : g_gen_list_y;
            const float bw = pad * 2 + nw + barw + pw;
            const float bh = ((gn > 0 ? gn : 1) * lh) + pad * 2;

            // 用户要求：**不要边框**，完全透明背景 ✓

            if (gn == 0) {
                fdl->AddText(fnt, fs, ImVec2(bx + pad, by + pad), g_gen_txt, "无密码机数据");
            } else {
                int shown = 0;
                for (int i = 0; i < gn; i++) {
                    // 进度 100% 的已经开好，直接过测 ✓
                    if (gens[i].progress >= 99.5f) continue;
                    const float ry = by + pad + shown * lh;
                    shown++;
                    float p = gens[i].progress;
                    if (p < 0) p = 0; if (p > 100) p = 100;

                    // 序号（白色 ✓）
                    char nb[16]; snprintf(nb, sizeof(nb), "%d", shown);
                    fdl->AddText(fnt, fs, ImVec2(bx, ry), IM_COL32(255,255,255,255), nb);

                    // 进度条：底槽 + 已填充段（都圆角，看起来平滑）
                    const float sx0 = bx + pad + nw;
                    const float sy0 = ry + (fs - barh) * 0.5f;
                    const float sx1 = sx0 + barw;
                    const float sy1 = sy0 + barh;
                    fdl->AddRectFilled(ImVec2(sx0, sy0), ImVec2(sx1, sy1),
                                       IM_COL32(255, 255, 255, 26), barh * 0.5f);
                    // 颜色**直接用世界里那个进度条的同一个函数** progress_color()，
                    // 这样两边永远同步 ✓（用户要求：直接去读密码机进度绘制就行）
                    //   <20% 绿、<=60% 黄、>60% 红
                    const ImColor pc = progress_color(p);
                    const int rr = (int)(pc.Value.x * 255.0f);
                    const int gg = (int)(pc.Value.y * 255.0f);
                    const int bb = (int)(pc.Value.z * 255.0f);

                    // ★ 已填充段（上次的补丁把这段误删了，导致进度条看着是空的 ✗）
                    //   圆角同样按胶囊处理；很短时用自身宽度的一半，避免扁成一条线
                    if (p > 0.4f) {
                        const float fx = sx0 + (barw * p / 100.0f);
                        const float fw = fx - sx0;
                        const float frad = (fw < barh) ? fw * 0.5f : barh * 0.5f;
                        fdl->AddRectFilled(ImVec2(sx0, sy0), ImVec2(fx, sy1),
                                           IM_COL32(rr, gg, bb, 235), frad);
                    }

                    // 百分比
                    char pb[24]; snprintf(pb, sizeof(pb), "%.0f%%", p);
                    fdl->AddText(fnt, fs, ImVec2(sx1 + pad * 0.6f, ry),
                                 IM_COL32(rr, gg, bb, 255), pb);
                }
            }

            const ImVec2 mp = ImGui::GetIO().MousePos;
            const bool in = (mp.x >= bx && mp.x <= bx + bw && mp.y >= by && mp.y <= by + bh);
            static bool s_drag = false; static float s_dx = 0, s_dy = 0;
            if (in && ImGui::IsMouseClicked(0)) { s_drag = true; s_dx = mp.x - bx; s_dy = mp.y - by; }
            if (!ImGui::IsMouseDown(0)) s_drag = false;
            if (s_drag) { g_gen_list_x = mp.x - s_dx; g_gen_list_y = mp.y - s_dy; }
        }
    }

    // ★ ImGui-MCP 沙盒：AI 通过 MCP 现场搭的原型 UI 画在这里。
    //   没有沙盒窗口时这个调用什么都不做。
    McpBridge::RenderSandbox();

    // ★ 开合动画：**不能**在这里 return。
    //   之前是 `if (!show_window) return;` —— 松开关闭键那一帧 UI 就没了，
    //   没有任何过渡。现在把 show_window 当"目标状态"交给 UI，
    //   由它自己把 visT 从 1 走到 0（0.22s 收合 + 玻璃化开 + 内容淡出），
    //   走完返回 false 才真正停渲染。
g_ui_state.world = g_dark_theme ? Theme::WORLD_DARK : Theme::WORLD_LIGHT;  // 从持久化状态同步

// ★★ 2026-10-27 修复：**DisplaySize 变化时**主窗口重新居中（每次都会触发）
//
//   上一版用 `g_center_main_window_next_frame` 只在 rebuild 分支设置，
//   但用户实测"只按启动时的屏幕算" —— 说明：
//     ① rebuild 触发后，my_ui 主窗口可能不是第一个 Begin，SetNextWindowPos 被别的窗口吃了
//     ② 或者 my_ui 里用了 SetWindowPos 覆盖了
//   现在改成：**只要 DisplaySize 变了就居中**，不管 rebuild 有没有触发、触发几次。
//   这样：
//     · 启动时 DisplaySize 从 0x0 变到实际 → 居中 ✓
//     · 旋转后 DisplaySize 变到新值 → **再居中一次** ✓（这是之前缺的）
//   `ImGuiCond_Always` 保证覆盖任何 FirstUseEver 的旧位置。
{
    static ImVec2 s_last_ds(-1.0f, -1.0f);
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if (ds.x > 0.5f && ds.y > 0.5f &&
        (ds.x != s_last_ds.x || ds.y != s_last_ds.y)) {
        s_last_ds = ds;
        ImGui::SetNextWindowPos(
            ImVec2(ds.x * 0.5f, ds.y * 0.5f),
            ImGuiCond_Always,
            ImVec2(0.5f, 0.5f));
        printf("[居中] DisplaySize=(%.0f,%.0f) -> 主窗口居中\n", ds.x, ds.y);
        fflush(stdout);
    }
}

printf("[Frame] MyUI::Draw 开始\n"); fflush(stdout);
MyUI::Draw(g_ui_state, ImGui::GetIO().DeltaTime, show_window);
printf("[Frame] MyUI::Draw 完成\n"); fflush(stdout);
g_dark_theme = (g_ui_state.world == Theme::WORLD_DARK);
}   // ← 闭合 Layout_tick_UI
// ============================================================================
// ImGui-MCP 数据面实现
// ============================================================================
//  只有这个翻译单元能看到 g_cfg / SaveConfig / LoadConfig / data[] 这些
//  static 全局量，所以访问器必须写在这里。
//
//  读：先 SaveConfig() 把「实时状态」刷进内存表并落盘，再抄出来。
//      —— 这样 AI 拿到的一定是当前真实值，而不是上次存盘时的旧值。
//  写：改表 -> 落盘 -> LoadConfig() 重新应用。
//      —— 和用户在界面上改完再重启一次效果完全一致，
//         不需要为 100 多个配置项逐个写 setter。
// ============================================================================
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace McpData {

int ListKV(KV* out, int max) {
    // 刷新：把当前实时状态写进 g_cfg（并落盘，保证文件与内存一致）
    SaveConfig();
    int n = g_cfg_n;
    if (n < 0) n = 0;
    if (!out) return n;
    if (n > max) n = max;
    for (int i = 0; i < n; i++) {
        snprintf(out[i].key, sizeof(out[i].key), "%s", g_cfg[i].key);
        snprintf(out[i].val, sizeof(out[i].val), "%s", g_cfg[i].val);
    }
    return n;
}

bool GetKV(const char* key, char* out, size_t outsz) {
    if (!key || !out || outsz == 0) return false;
    out[0] = '\0';
    for (int i = 0; i < g_cfg_n; i++) {
        if (strcmp(g_cfg[i].key, key) == 0) {
            snprintf(out, outsz, "%s", g_cfg[i].val);
            return true;
        }
    }
    // 表可能是空的/过期的，刷一次再找
    SaveConfig();
    for (int i = 0; i < g_cfg_n; i++) {
        if (strcmp(g_cfg[i].key, key) == 0) {
            snprintf(out, outsz, "%s", g_cfg[i].val);
            return true;
        }
    }
    return false;
}

// 这个 key 是不是工程真正会应用的配置项？
// 判据：SaveConfig() 枚举出来的键集合 == LoadConfig() 能套用的键集合（已核对：各 111 项）。
// 所以「能在刷新后的 g_cfg 里找到」就等于「写了会生效」。
bool HasKV(const char* key) {
    if (!key) return false;
    for (int i = 0; i < g_cfg_n; i++)
        if (strcmp(g_cfg[i].key, key) == 0) return true;
    SaveConfig();       // 表可能是空的/过期的，刷一次再找
    for (int i = 0; i < g_cfg_n; i++)
        if (strcmp(g_cfg[i].key, key) == 0) return true;
    return false;
}

// 把 g_cfg 整表写到配置文件
static bool WriteConfigFile() {
    char cfgp2[64]; GetConfigPath(cfgp2, sizeof(cfgp2));
    //   ★ 先以 0600 创建再写：不能先创建后 chmod
    //     （中间那一瞬间别人能读到，窗口很短但是真实存在 ✗）
    int fd = open(cfgp2, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    FILE* f = fdopen(fd, "w");
    if (!f) { close(fd); return false; }
    for (int i = 0; i < g_cfg_n; i++) fprintf(f, "%s=%s\n", g_cfg[i].key, g_cfg[i].val);
    fclose(f);
    chmod(cfgp2, 0600);      // 已存在的老文件可能权限松，强制收紧 ✓
    return true;
}

bool SetKV(const char* key, const char* val) {
    if (!key || !val) return false;
    if (strlen(key) >= sizeof(g_cfg[0].key)) return false;

    // 1) 先把实时状态刷成基线（同时确保 g_cfg 里是当前值和当前键集合）
    SaveConfig();

    // 2) 只允许改工程真正会应用的 key。
    //    ★ 关键：不要「找不到就追加」—— 追加一个 LoadConfig 不认识
    //      的 key 会静默无效（读得到但不起作用），那比直接失败更坑。
    bool found = false;
    for (int i = 0; i < g_cfg_n; i++) {
        if (strcmp(g_cfg[i].key, key) == 0) {
            snprintf(g_cfg[i].val, sizeof(g_cfg[i].val), "%s", val);
            found = true;
            break;
        }
    }
    if (!found) {
        printf("[MCP] dwrg_set 拒绝：未知配置项 %s（本工程共 %d 项，可用 dwrg_list 查）\n",
               key, g_cfg_n);
        fflush(stdout);
        return false;
    }

    // 3) 落盘 + 4) 重新应用（LoadConfig 会逐项套用到实时全局量）
    if (!WriteConfigFile()) return false;
    LoadConfig();

    printf("[MCP] dwrg_set %s=%s\n", key, val);
    fflush(stdout);
    return true;
}

bool SaveToDisk() {
    SaveConfig();       // 内部自带落盘
    return true;
}

bool ReloadFromDisk() {
    LoadConfig();
    return true;
}

const char* ConfigPath() {
    static char b[64];
    GetConfigPath(b, sizeof(b));
    return b;
}

bool GetRuntime(Runtime* out) {
    std::lock_guard<std::recursive_mutex> data_lock(g_data_mutex);
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    out->entity_count = data_count;
    out->fps          = fps;
    out->screen_w     = native_window_screen_x;
    out->screen_h     = native_window_screen_y;
    out->orientation  = displayInfo.orientation;
    out->pid          = (int)getpid();
    // 真正的进程运行时长（读 /proc/self/stat + /proc/uptime）
    out->uptime_ms    = McpBridge::ProcessUptimeMs();
    snprintf(out->render_name, sizeof(out->render_name), "%s",
             graphics ? graphics->RenderName : "unknown");
    return true;
}

int ListEntities(Entity* out, int max) {
    std::lock_guard<std::recursive_mutex> data_lock(g_data_mutex);
    int n = data_count;
    if (n < 0) n = 0;
    if (n > 3000) n = 3000;
    if (!out) return n;
    if (n > max) n = max;

    for (int i = 0; i < n; i++) {
        memset(&out[i], 0, sizeof(out[i]));
        out[i].obj  = (uint64_t)data[i].obj;
        out[i].camp = data[i].camp;
        float xyz[3] = {0.0f, 0.0f, 0.0f};
    unit_xyz(i, xyz);
        out[i].x = xyz[0];
        out[i].y = xyz[1];
        out[i].z = xyz[2];
        snprintf(out[i].name, sizeof(out[i].name), "%s", data[i].str);
        snprintf(out[i].cls,  sizeof(out[i].cls),  "%s", data[i].class_name);
    }
    return n;
}

// ---------------------------------------------------------------------------
// 主菜单页面控制（让 AI 能自己切页去看）
// ---------------------------------------------------------------------------
int UiPageCount() { return MyUI::PageCount(); }

const char* UiPageName(int index) { return MyUI::PageName(index); }

int GetUiPage() {
    const int n = MyUI::PageCount();
    return (g_ui_state.page >= 0 && g_ui_state.page < n) ? g_ui_state.page : -1;
}

bool SetUiPage(int page) {
    if (page < 0 || page >= MyUI::PageCount()) return false;
    g_ui_state.page = page;
    printf("[MCP] 切到页 %d (%s)\n", page, MyUI::PageName(page));
    fflush(stdout);
    return true;
}

bool SetUiPageByName(const char* name) {
    int i = MyUI::FindPageByName(name);
    if (i < 0) return false;
    return SetUiPage(i);
}

// ---------------------------------------------------------------------------
// Dump 触发（把 Dump 页那些按钮变回 MCP 命令）
// ---------------------------------------------------------------------------
namespace {
struct DumpFn { const char* name; int args; const char* desc; };
const DumpFn kDumpFns[] = {
    { "cfg_bones",      0, "2026-10-26: bones config diagnostic" },
    { "room_hunt",      0, "在 game_kernel 里按 room/region/corridor/tile/grid/layout 等关键词找候选键（找房间布局先用这个）" },
    { "layout_blocks",  0, "★ 展开 cur_world.world_visible_block_list 等「区块集合」，并列全部看起来像区块/房间/走廊的键（找随机布局用这个）" },
    { "scene_childs",   0, "dump g_world_mgr.cur_world.scene_childs 的每个子场景对象" },
    { "current_scene",  0, "dump 当前地图的元数据" },
    { "map_candidates", 0, "dump 地图候选字段" },
    { "units_by_type",  0, "按 type 统计 units_by_type" },
    { "all_units",      0, "dump 所有单位的详情" },
    { "game_kernel",    0, "dump game_kernel 模块字典的所有键名" },
    { "type_units",     1, "dump 指定 type 下的所有单位（arg=type，例如 1999 / 1）" },
    { "unit_by_type",   2, "dump 指定 type 下第 idx 个单位的完整 __dict__（arg=type, arg2=idx）" },
    { "unit_by_scene",  1, "dump 指定场景对象上的单位（arg=scene 对象地址）" },
    { "skill_fields",   1, "dump 指定单位的技能字段（arg=unit 对象地址）" },
    { "dict_at",        1, "dump 任意 dict 对象的所有键（arg=地址）" },
    { "dict_raw",       1, "★ 同上但**带类型名**和原始字节（arg=地址）—— 用来搞清读不出名字的键到底是什么类型" },
    { "modules",        0, "★ 枚举 sys.modules 全部模块名 + 地址（找地图/小地图相关模块）" },
    { "named_raw",      0, "取 game_kernel 里某个名字（用 Dump 页过滤词框填名字）打印类型再探它" },
    { "module_raw",     0, "★ 按模块名探 sys.modules 里的模块（name=common_cs.RoomSurfaceMgr 之类）" },
    { "path_raw",       0, "★ 点号路径探（name=g_world_mgr.cur_world.surface_mgr 或 mod:模块名.属性）" },
    { "memdump",        2, "★ 原始内存 dump（arg=地址, arg2=长度）—— 定位 __slots__/C 扩展对象的字段" },
    { "typeinfo",       1, "★★ 类型信息（arg=type 对象地址）：tp_basicsize 决定实例能放几个字段，并 dump 它的 __slots__/方法名" },
    { "find_path",      0, "★★ 按名字在对象图里全局搜键并打印路径（name=maze_mgr）—— 不知道路径时的主力工具" },
    { "memscan",        2, "★★ 在内存里按字节搜字符串（name=要搜的, arg=起始, arg2=长度）—— 判断某个类名/字段名是否已加载" },
    { "draw_stats",     0, "绘制逐门统计 —— 每道过滤各淘汰多少实体。排查模仿者局里人不画时看哪门数字大" },
    { "copycat_book",   0, "★★ 模仿者身份簿实时内容 —— 排查模仿者局里人突然不画：簿子里缺谁、谁的 body 过期，画面上就少谁" },
    { "copycat_units",  0, "★★ 模仿者 [1088]/[1089] 逐条实况(uid/号/身份/unique_id/model/v70) —— 排查「死了还显示活着」和死亡判据" },
    { "copycat_skills", 0, "★★ 模仿者每个人的 skill_dict 原样列出(id/剩余/总时长/充能) + unit 上带 cd 的字段 —— 做技能CD绘制前先跑这个" },
    { "rescue_state",   0, "★★ 救援/动作状态取样 —— 列出附近求生/监管的动作 state id 与状态名。找「救人」状态用：救人前/中/后各跑一次对比" },
    { "cam_dump",       0, "★★ 相机/VP 矩阵 —— 打印 libbase->+MatrixOffset->+ (uint64_t)SecOff::u64(SecOff::DG_82)->+ (uint64_t)SecOff::u64(SecOff::DG_83) 的 16 个 float。第 4 列就是相机世界位置，「改视野」是否生效看这个" },
    { "slot_addr",      0, "★★ 解析 Python 属性的值槽地址（name=\"0x<对象>,<属性名>\"）—— 只读；改属性就是往这个槽写指针，带键名校验" },
    { "vs_lock",        0, "★★ 改视角：把 g_cam_ctrl.unit 换成目标角色 -> 相机就切过去（name=\"lock <uid>\" / \"unlock\" / \"status\"）" },
    { "vs_names",       0, "★ 改视角列表诊断：打印每个角色读到的名字 + model_path 原始字节（排查乱码）" },
    { "hb",             0, "★ 授权心跳诊断：状态/成功失败次数/凭证/掩码 —— 排查心跳是否生效" },
    { "vs_layout",      0, "★ 改视角布局诊断：功能页实际拿到的面板高度 / 行数（排查列表被裁）" },
    { "rescue_log",     0, "★★ 高频记录救人状态（name=start/stop）—— 16ms 分辨率写 /data/adb/.d/o.log，用来量完整救人时长" },
    { "memhwbp",        2, "★★ 硬件断点（name=exec|read|write|rw|clear|info, arg=地址, arg2=长度）—— 命中时抓完整寄存器+PC+LR，用来从数据/API 倒推回代码" },
    { "memwrite",       2, "⚠ 写内存（name=f|i|d 类型, arg=地址, arg2=值）—— 会先读旧值并写后校验；把旧值再写一次即可还原" },
    { "memfind",        2, "★★ 按**数值**搜内存（name=f:60,30@512, arg=起始, arg2=长度）—— f=float, i=int32, d=double；逗号分隔多个值表示「按顺序出现在 @ 窗口内」，用来从一堆孤立数值里收敛出真正的配置" },
    { "find_mod",       0, "★★ 在所有已加载模块的 __dict__ 里找名字（name=MiniMapBigVer 或 *MiniMap*）" },
    { "expand",         2, "★★ 按地址万能展开（arg=地址, arg2=深度）：tuple/list 逐元素、dict 走 dict_raw、对象走属性字典/内存" },
    { "find_inst",      2, "★★ 按类型找实例（arg=类型地址, name=\"起始:长度\"十六进制）—— 找 UI 节点这类不在对象图里的实例" },
    { "entity_camps",   0, "★ 按阵营统计当前跟踪到的实体（camp 3 = 场景/墙体/门，含世界坐标），看地图几何能不能从实体拿" },
    { "knight",         0, "★★ 骑士头盔预测实测值：是不是骑士 / 头盔猜了几次 / 次数来源(计数·估算) / 头盔CD / 是否读条 —— 验证 PyKnight 读数链路" },
    { "dbg_icon",       0, "★★ 技能道具图标诊断：开关值 / 采样器是否在跑 / 缓存条目 / 每道门的命中计数 + 最后一帧样本 + data[] 实时对照。图不显示先跑这个" },
};
const int kDumpFnCount = (int)(sizeof(kDumpFns) / sizeof(kDumpFns[0]));
} // namespace

int DumpFunctionCount() { return kDumpFnCount; }
const char* DumpFunctionName(int i) {
    return (i >= 0 && i < kDumpFnCount) ? kDumpFns[i].name : "";
}
int DumpFunctionArgs(int i) {
    return (i >= 0 && i < kDumpFnCount) ? kDumpFns[i].args : -1;
}

void SetDumpFilter(const char* f) { PyDump::set_filter(f ? f : ""); }

// ============================================================================
//   2026-10-26：fn=dbg_icon —— 技能道具图标为什么画不出来
// ----------------------------------------------------------------------------
//   只读：不碰 ImGui、不碰 graphics（纹理只能在绘制线程里上传）。
//   四段输出，按"卡在哪一环"递进：
//     ① 开关 / 采样器 / 缓存      -> 数据源到底有没有在跑
//     ② 绘制块逐门计数            -> 哪一道门把图挡掉了
//     ③ data[] 实时对照           -> data[i].str / obj 能不能对上图标表和缓存
//     ④ unit.model -> scene 对照  -> 缓存 key 与 data[i].obj 是不是同一个对象
// ============================================================================
static void dbg_icon_dump()
{
    std::lock_guard<std::recursive_mutex> data_lock(g_data_mutex);
    SurvSkill::IconDbg &d = SurvSkill::g_icon_dbg;
    PyDump::reset_buf();
    PyDump::wline("===== 技能道具图标诊断 =====");
    PyDump::wline("开关 show_draw_Prop = %d  %s", show_draw_Prop ? 1 : 0,
                  show_draw_Prop ? "" : "<-- 0 = 绘制页「技能道具图标」没打开，后面都不用看");
    PyDump::wline("图片大小 g_icon_px   = %.1f", (double)g_icon_px);
    PyDump::wline("图标表条目           = %d 张 (juese_png.h)", kJueseIconCount);
    PyDump::wline("");

    PyDump::wline("---- ① 采样器 SurvSkill::Tick() ----");
    PyDump::wline("Tick 调用次数        = %d  %s", d.tick_calls,
                  d.tick_calls ? "" : "<-- 0 = 采样器没接上！缓存永远是空的");
    PyDump::wline("最近一次 type=2 单位数 = %d", d.tick_units);
    PyDump::wline("CachePut 写入次数    = %d", d.cache_puts);
    PyDump::wline("缓存条目 g_icount    = %d / 16", SurvSkill::g_icount);
    for (int k = 0; k < SurvSkill::g_icount; ++k) {
        const SurvSkill::ItemInfo &ci = SurvSkill::g_icache[k];
        PyDump::wline("   [%d] scene=0x%012llx 道具 %d 个", k,
                      (unsigned long long)ci.scene, ci.n);
        for (int m = 0; m < ci.n && m < 2; ++m) {
            const SurvSkill::ItemSlot &cs = ci.it[m];
            PyDump::wline("       [%d] mode=%d slot=%d 次数=%d/%d 耐久=%g/%g CD=%g/%g"
                          " ring=%d %.2f model=%s",
                          m, cs.mode, cs.slot, cs.rem, cs.ini,
                          (double)cs.life, (double)cs.life_mx,
                          (double)cs.cd, (double)cs.cd_mx,
                          cs.ring, (double)cs.ring_rate, cs.model);
        }
    }
    PyDump::wline("");

    PyDump::wline("---- ② 绘制块逐门计数（累计，实体×帧）----");
    PyDump::wline("进入绘制块           = %d  (camp==2: %d)  %s", d.draw_frames, d.draw_c2,
                  d.draw_frames ? "" : "<-- 0 = 这一整块没执行（开关 / if(got) 天赋门 / camp 门）");
    PyDump::wline("  Find 命中          = %d  %s", d.draw_find,
                  (d.draw_frames && d.draw_find == 0) ? "<-- data[i].str 在图标表里查不到" : "");
    PyDump::wline("  Texture 非 0       = %d  %s", d.draw_tex,
                  (d.draw_find && d.draw_tex == 0) ? "<-- PNG 解码/纹理上传失败" : "");
    PyDump::wline("  ItemOfScene 命中   = %d  %s", d.draw_hit,
                  (d.draw_frames && d.draw_hit == 0) ? "<-- 缓存/映射对不上（看 ①③④）" : "");
    PyDump::wline("  实际 AddImage      = %d  %s", d.draw_ok,
                  (d.draw_hit && d.draw_ok == 0) ? "<-- 全被'用完隐藏'挡掉（看图里 slot/rem）或纹理 0" : "");
    PyDump::wline("最后一帧样本         : camp=%d obj=0x%012llx name=[%s]",
                  d.last_camp, (unsigned long long)d.last_obj, d.last_str);
    PyDump::wline("  find=%d tex=%d hit=%d mode=%d rem=%d init=%d",
                  d.last_find, d.last_tex, d.last_hit, d.last_mode, d.last_rem, d.last_ini);
    PyDump::wline("  X1=%.1f Y1=%.1f W=%.1f H=%.1f above_y=%.1f px=%.1f",
                  (double)d.last_x1, (double)d.last_y1, (double)d.last_w, (double)d.last_h,
                  (double)d.last_above_y, (double)d.last_px);
    PyDump::wline("");

    PyDump::wline("---- ③ data[] 实时对照（camp 1/2）----");
    const int n = (data_count > 0 && data_count < 3000) ? data_count : 0;
    PyDump::wline("data_count = %d", data_count);
    int matched = 0;
    for (int i = 0; i < n; ++i) {
        if (data[i].camp != 1 && data[i].camp != 2) continue;
        const JueseIcon *ic = ItemIcon::Find(data[i].str);
        const SurvSkill::ItemInfo *ii = SurvSkill::ItemOfScene((uint64_t)data[i].obj);
        const bool hit = ii != nullptr;
        if (hit) ++matched;
        PyDump::wline("  i=%d camp=%d obj=0x%012llx name=[%s] find=%s 默认图=%s hit=%d 道具=%d",
                      i, data[i].camp, (unsigned long long)data[i].obj, data[i].str,
                      ic ? "OK" : "NULL", ic ? ic->item : "-", hit ? 1 : 0,
                      ii ? ii->n : -1);
        for (int m = 0; ii && m < ii->n && m < 2; ++m) {
            const SurvSkill::ItemSlot &cs = ii->it[m];
            const JueseIcon *pic = ItemIcon::ByModel(cs.model);
            PyDump::wline("      [%d] mode=%d slot=%d 次数=%d/%d 耐久=%g/%g CD=%g/%g"
                          " ring=%d %.2f 图=%s model=[%s]",
                          m, cs.mode, cs.slot, cs.rem, cs.ini,
                          (double)cs.life, (double)cs.life_mx,
                          (double)cs.cd, (double)cs.cd_mx,
                          cs.ring, (double)cs.ring_rate,
                          pic ? pic->item : "(角色默认)", cs.model);
        }
    }
    PyDump::wline("  命中 %d 条（0 = 图上一条都画不出来）", matched);
    PyDump::wline("");

    PyDump::wline("---- ④ unit.model -> get_scene_obj() 对照（最多 8 个）----");
    {
        const char *why = nullptr;
        uint64_t md = PyCore::game_kernel_dict(why);
        PyCore::Dict dk, umd;
        uint64_t um = 0, ubt = 0, lst = 0, items = 0;
        int64_t un = 0;
        bool ok = false;
        if (!md)                                        PyDump::wline("  game_kernel 不可用: %s", why ? why : "?");
        else if (!PyCore::read_dict(md, dk))            PyDump::wline("  game_kernel dict 读取失败");
        else if (!(um = SurvSkill::field_of(dk, "unit_mgr")))
                                                        PyDump::wline("  unit_mgr 找不到");
        else if (!PyCore::read_dict(PyCore::get_inst_dict(um), umd))
                                                        PyDump::wline("  unit_mgr.__dict__ 读取失败");
        else if (!(ubt = SurvSkill::field_of(umd, "units_by_type")))
                                                        PyDump::wline("  units_by_type 找不到");
        else if (!(lst = PyCore::get_type_list(ubt, 2)) || !PyCore::read_list(lst, un, items))
                                                        PyDump::wline("  type=2 列表读取失败");
        else ok = true;
        if (ok) {
            PyDump::wline("  type=2 单位 %lld 个", (long long)un);
            for (int64_t k = 0; k < un && k < 8; ++k) {
                uint64_t u = getPtr64(items + k * 8);
                if (!PyCore::is_obj(u)) continue;
                PyCore::Dict ud;
                if (!PyCore::read_dict(PyCore::get_inst_dict(u), ud)) continue;
                char pname[48] = {0};
                uint64_t vp = SurvSkill::field_of(ud, "_player_name");
                if (vp) PyCore::read_str(vp, pname, sizeof(pname));
                int64_t uid = 0;
                PyCore::read_int(SurvSkill::field_of(ud, "uid"), uid);
                uint64_t mv = SurvSkill::field_of(ud, "model");
                uint64_t sc = mv ? PyCore::get_scene_obj(mv) : 0;
                int in_data = 0, in_cache = 0;
                for (int i = 0; i < n; ++i)
                    if (data[i].camp == 2 && (uint64_t)data[i].obj == sc) { in_data = 1; break; }
                for (int q = 0; q < SurvSkill::g_icount; ++q)
                    if (SurvSkill::g_icache[q].scene == sc) { in_cache = 1; break; }
                PyDump::wline("  unit=0x%012llx uid=%lld name=[%s] model=0x%012llx scene=0x%012llx inData=%d inCache=%d",
                              (unsigned long long)u, (long long)uid, pname,
                              (unsigned long long)mv, (unsigned long long)sc, in_data, in_cache);
                /*   2026-10-26：**item 明细（挑槽 + 三模式判定字段）**
                 *   不再把 209 个键全打出来（太吵），只打判定要用的 8 个字段 +
                 *   keys 数，一眼看出这个道具为什么被判成 次数 / 耐久 / CD / 无消耗。
                 */
                {
                    uint64_t il = SurvSkill::field_of(ud, "item_lst");
                    int64_t iln = 0; uint64_t ili = 0;
                    if (il && PyCore::read_list(il, iln, ili)) {
                        PyDump::wline("      item_lst %lld 个", (long long)iln);
                        for (int64_t a = 0; a < iln && a < 6; ++a) {
                            uint64_t it = getPtr64(ili + a * 8);
                            if (!PyCore::is_obj(it)) continue;
                            PyCore::Dict itd;
                            const uint64_t itdi = PyCore::get_inst_dict(it);
                            if (!PyCore::read_dict(itdi, itd)) continue;
                            const float v_rem  = SurvSkill::field_num(itdi, itd, "_remain_use_times");
                            const float v_ini  = SurvSkill::field_num(itdi, itd, "_init_use_times");
                            const float v_life = SurvSkill::field_num(itdi, itd, "_remain_lifespan");
                            const float v_lmx  = SurvSkill::field_num(itdi, itd, "_init_lifespan");
                            const float v_ctc  = SurvSkill::field_num(itdi, itd, "current_total_cd");
                            const float v_cdt  = SurvSkill::field_num(itdi, itd, "item_cd_time");
                            const float v_use  = SurvSkill::field_num(itdi, itd, "total_use_times");
                            const float v_cons = SurvSkill::field_num(itdi, itd, "consume_cnt");
                            const char *md = (v_ini > 0.0f) ? "次数"
                                           : (v_life > 0.5f ? "耐久"
                                           : ((v_ctc > 0.0f || v_cdt > 0.0f) ? "CD" : "无消耗"));
                            char s_mod[64] = {0}, s_mesh[80] = {0}, s_icu[64] = {0}, s_ico[64] = {0};
                            SurvSkill::field_str(itdi, itd, "model_name",      s_mod,  sizeof(s_mod));
                            SurvSkill::field_str(itdi, itd, "item_mesh_path",  s_mesh, sizeof(s_mesh));
                            SurvSkill::field_str(itdi, itd, "icon_using",      s_icu,  sizeof(s_icu));
                            SurvSkill::field_str(itdi, itd, "icon_origin",     s_ico,  sizeof(s_ico));
                            const float v_type = SurvSkill::field_num(itdi, itd, "type");
                            const float v_iid  = SurvSkill::field_num(itdi, itd, "item_id");
                            PyDump::wline("        item[%lld] 0x%012llx keys=%lld -> %s"
                                          "  remain=%g init=%g life=%g life_mx=%g ctc=%g cdt=%g use=%g cons=%g"
                                          " type=%g item_id=%g",
                                          (long long)a, (unsigned long long)it,
                                          (long long)itd.n_entries, md,
                                          (double)v_rem, (double)v_ini, (double)v_life, (double)v_lmx,
                                          (double)v_ctc, (double)v_cdt, (double)v_use, (double)v_cons,
                                          (double)v_type, (double)v_iid);
                            PyDump::wline("           model_name=[%s] mesh=[%s] icon_using=[%s] icon_origin=[%s]",
                                          s_mod, s_mesh, s_icu, s_ico);
                            {
                                /*   充能链路逐步自检：道具上的 charge_skill_id -> 技能对象
                                 *   -> _cur_power_num / power_num。哪一步断了看这一行。
                                 */
                                const float d_chid = SurvSkill::field_num(itdi, itd, "charge_skill_id");
                                uint64_t d_so = (d_chid > 0.0f)
                                              ? SurvSkill::charge_skill_of(it, (int64_t)d_chid, ud) : 0;
                                float d_cur = -1.0f, d_mx = -1.0f;
                                if (d_so) {
                                    uint64_t dsdi = PyCore::get_inst_dict(d_so);
                                    PyCore::Dict sdd;
                                    if (PyCore::read_dict(dsdi, sdd)) {
                                        d_cur = SurvSkill::field_num(dsdi, sdd, "_cur_power_num");
                                        d_mx  = SurvSkill::field_num(dsdi, sdd, "power_num");
                                    }
                                }
                                PyDump::wline("           charge: id=%g skill=0x%012llx cur=%g max=%g",
                                              (double)d_chid, (unsigned long long)d_so,
                                              (double)d_cur, (double)d_mx);
                            }
                            /*   2026-10-26：**关键词键扫描**（找"充能/耐久"到底存在哪个字段）
                             *   只打名字里带这些词的数值键，一条 ~30 行，够看清了。
                             */
                            for (int64_t q = 0; q < itd.n_entries; ++q) {
                                char kn[64] = {0};
                                if (!PyCore::read_str(getPtr64(PyCore::key_slot(itd, q)), kn, sizeof(kn))) continue;
                                bool want = false;
                                for (const char *kw : {"power", "energy", "charge", "dura", "life", "use", "num", "count", "cd"})
                                    if (strstr(kn, kw)) { want = true; break; }
                                if (!want) continue;
                                float kv = 0.0f;
                                if (!PyCore::read_number(PyCore::value_slot(itd, q), kv, -1.0e6, 1.0e6)) continue;
                                PyDump::wline("           .%-30s = %g", kn, (double)kv);
                            }
                        }
                    }
                }
            }
        }
    }
    snprintf((char *)PyDump::g_status, 128, "dbg_icon 完成");
}

// ============================================================================
//   2026-10-26：fn=dbg_atk —— **出刀判据诊断**（约瑟夫"只有第一刀触发"查因）
//   两条判据都要看：
//     · skill_changed：cur_skill != prev_skill（同一把刀 id 不变就永远不成立）
//     · _state 上升沿：state 列表里有没有 26/27/45/46，且要从"非出刀"变成"出刀"
// ============================================================================
static void dbg_atk_dump()
{
    PyDump::reset_buf();
    PyDump::wline("===== 出刀判据诊断 =====");
    PyDump::wline("hunter slots = %d", PyAttack::g_slot_n);
    for (int i = 0; i < PyAttack::g_slot_n; ++i) {
        const PyAttack::HunterSlot &s = PyAttack::g_slots[i];
        PyDump::wline("");
        PyDump::wline("---- slot %d ----", i);
        PyDump::wline("  unit=0x%llx scene=0x%llx sm=0x%llx",
                      (unsigned long long)s.unit, (unsigned long long)s.scene,
                      (unsigned long long)s.sm);
        PyDump::wline("  cur_skill=%d prev_skill=%d changed=%d non_atk=%d last_digit=%d",
                      s.cur_skill, s.prev_skill,
                      (s.cur_skill != s.prev_skill) ? 1 : 0,
                      PyAttack::is_non_attack_skill(s.cur_skill) ? 1 : 0,
                      s.dbg_last_digit);
        PyDump::wline("  _state 有出刀=%d prev_st_atk=%d %s",
                      s.dbg_now_atk, s.prev_st_atk ? 1 : 0,
                      (s.dbg_now_atk && !s.prev_st_atk) ? "<- 本帧上升沿" : "");
        PyDump::wline("  cur_state=%d  hunter_ball=%d", s.cur_state, AutoPallet::g_hunter_ball ? 1 : 0);
    }
    PyDump::wline("");
    {
        const int why = PyAttack::g_gate_why.load();
        const char *wt = (why == 0) ? "通过"
                       : (why == 1) ? "距离超了"
                       : (why == 2) ? "朝向不符"
                       : (why == 3) ? "隔着板子"
                       : (why == 4) ? "位置读不到(放行)"
                       : (why == 5) ? "scene为空(放行)" : "?";
        PyDump::wline("门控: %d=%s  距离=%.2fm  回调已注册=%d",
                      why, wt, (double)PyAttack::g_gate_dist_cm.load() / 100.0,
                      PyAttack::attack_cb_registered() ? 1 : 0);
        PyDump::wline("回调触发次数=%d  当前判定攻击中=%d  出刀态(原始)=%d",
                      PyAttack::g_cb_hits.load(),
                      PyAttack::g_attacking.load() ? 1 : 0,
                      PyAttack::g_raw_attacking.load() ? 1 : 0);
        PyDump::wline("判据命中: id路=%d  状态上升沿=%d",
                      PyAttack::g_id_hits.load(), PyAttack::g_edge_hits.load());
    }
    PyDump::wline("");
    PyDump::wline("回调链路: 进入=%d 开关关=%d 触摸未init=%d busy=%d 未清=%d camp1=%d 显示未就绪=%d",
                  AutoSkill::g_as_enter.load(), AutoSkill::g_as_disabled.load(),
                  AutoSkill::g_as_noinit.load(), AutoSkill::g_as_busy.load(),
                  AutoSkill::g_as_stale.load(), AutoSkill::g_as_camp.load(),
                  AutoSkill::g_as_disp.load());
    PyDump::wline("决策: 要按=%d 未就绪=%d QuickGate关=%d",
                  AutoSkill::g_as_action.load(), AutoSkill::g_as_noready.load(),
                  AutoSkill::g_as_gate.load());
    PyDump::wline("自身技能CD: 读到=%d has=%d id=%lld remain=%.2f total=%.2f 充能=%d/%d",
                  AutoSkill::g_own_ok.load(), AutoSkill::g_own_has.load(),
                  (long long)AutoSkill::g_own_skillid.load(),
                  (double)AutoSkill::g_own_remain.load() / 100.0,
                  (double)AutoSkill::g_own_total.load() / 100.0,
                  AutoSkill::g_own_ccur.load(), AutoSkill::g_own_cmax.load());
    PyDump::wline("松手开关: 开关=%d Apply=%d",
                  AutoSkill::g_report_proxy ? 1 : 0,
                  AutoSkill::g_proxy_applies.load());
    PyDump::wline("松手: 标记=%d 见到手指=%d 不在区=%d 区域=(%d,%d,%d,%d) 最后手指=(%d,%d) 条件=%d",
                  Touch::ReleaseDebug(0), Touch::ReleaseDebug(1), Touch::ReleaseDebug(2),
                  Touch::ReleaseDebug(3), Touch::ReleaseDebug(4),
                  Touch::ReleaseDebug(5), Touch::ReleaseDebug(6),
                  Touch::ReleaseDebug(7), Touch::ReleaseDebug(8), Touch::ReleaseDebug(9));
    PyDump::wline("只松手: 开关=%d 排UP=%d 发UP=%d touch_up成功=%d",
                  AutoSkill::g_report_up_only ? 1 : 0,
                  AutoSkill::g_up_only_sched.load(), AutoSkill::g_up_only_sent.load(),
                  AutoSkill::g_up_only_ok.load());
    PyDump::wline("飞轮: probed=%d has=%d",
                  AutoSkill::g_self_fly_probed ? 1 : 0,
                  AutoSkill::g_self_has_flywheel ? 1 : 0);
    PyDump::wline("触摸: inited=%d  自身camp=%d",
                  AutoSkill::g_touch_inited ? 1 : 0, PySelf::camp());
    PyDump::wline("");
    PyDump::wline("自动技能=%d  自动飞轮=%d",
                  AutoSkill::g_auto_skill_enabled ? 1 : 0,
                  AutoSkill::g_auto_flywheel_enabled ? 1 : 0);
    snprintf((char *)PyDump::g_status, 128, "dbg_atk 完成");
}

//   2026-10-26：fn=atk_trace —— 出刀采样的 dump（数据和采样逻辑在 PyAttack.h）
static void atk_trace_dump()
{
    PyDump::reset_buf();
    PyDump::wline("===== 出刀采样（%d 行，每 100ms 只在变化时记）=====", PyAttack::g_atk_trace_n);
    for (int i = 0; i < PyAttack::g_atk_trace_n; ++i) {
        const int idx = (PyAttack::g_atk_trace_head + i) % PyAttack::kAtkTraceMax;
        PyDump::wline("  %s", PyAttack::g_atk_trace[idx]);
    }
    PyDump::wline("");
    PyDump::wline("提示：让监管砍几刀再看；有变化才会有行");
    snprintf((char *)PyDump::g_status, 128, "atk_trace %d 行", PyAttack::g_atk_trace_n);
}

//   2026-10-26：fn=cfg_bones —— 骨骼配置诊断（保存端有没有写进去 / 载入值对不对）
static void cfg_bones_dump()
{
    PyDump::reset_buf();
    PyDump::wline("===== 骨骼配置诊断 =====");
    PyDump::wline("配置项总数 g_cfg_n = %d", g_cfg_n);
    const char *keys[6] = { "esp.bones_hunter", "esp.bones_survivor", "esp.bones_thickness",
                            "esp.bones_head", "esp.bones_head_size", "esp.bones_thickness_x10" };
    for (int k = 0; k < 6; ++k) {
        const char *val = nullptr;
        for (int i = 0; i < g_cfg_n; ++i) {
            if (strcmp(g_cfg[i].key, keys[k]) == 0) { val = g_cfg[i].val; break; }
        }
        PyDump::wline("  %-26s -> %s", keys[k], val ? val : "(不存在)");
    }
    PyDump::wline("");
    PyDump::wline("内存里的值: 监管色=0x%08X 求生色=0x%08X 线粗=%.2f 画头=%d 头大小=%.2f",
                  (unsigned)g_esp.bones_hunter, (unsigned)g_esp.bones_survivor,
                  g_esp.bones_thickness, g_esp.bones_head ? 1 : 0, g_esp.bones_head_size);
    PyDump::wline("记者松手快慢 = %d ms", AutoSkill::g_report_lead_ms);
    snprintf((char *)PyDump::g_status, 128, "cfg_bones 完成");
}

bool RunDump(const char* fn, int64_t arg, int64_t arg2) {
    std::lock_guard<std::recursive_mutex> data_lock(g_data_mutex);
    if (!fn || !*fn) return false;
    // ---- 场景实体按阵营统计 ----
    //   data[] 是 read_thread 填的实体表；camp 3 是场景物件（墙/门/房间道具）。
    //   随机地图的几何如果能在客户端侧拿到，最可能就是这批东西的世界坐标。
    if (!strcmp(fn, "entity_camps")) {
        PyDump::reset_buf();
        int hist[16] = {0};
        const int n = (data_count > 0 && data_count < 3000) ? data_count : 0;
        for (int i = 0; i < n; ++i) {
            int c = data[i].camp;
            if (c >= 0 && c < 16) hist[c]++;
        }
        PyDump::wline("===== data[] 阵营分布（data_count=%d）=====", data_count);
        for (int c = 0; c < 16; ++c)
            if (hist[c]) PyDump::wline("  camp %-2d : %d", c, hist[c]);
        PyDump::wline("");
        // 全部列出（含坐标），按阵营排序便于阅读
        for (int c = 1; c <= 7; ++c) {
            int shown = 0;
            for (int i = 0; i < n; ++i) {
                if (data[i].camp != c) continue;
                float xyz[3] = {0, 0, 0};
    unit_xyz(i, xyz);
                if (shown == 0) PyDump::wline("----- camp %d -----", c);
                PyDump::wline("  0x%012llX (%9.2f,%9.2f,%9.2f)  %s | %s",
                              (unsigned long long)data[i].obj,
                              xyz[0], xyz[1], xyz[2],
                              data[i].str, data[i].class_name);
                shown++;
                if (shown >= 200) { PyDump::wline("  ...(本阵营截断于 200 条)"); break; }
            }
        }
        snprintf((char*)PyDump::g_status, 128, "entity camps dump 完成");
        return true;
    }
    // ★★ 全求生状态快照（2026-10-12 新增，给"找倒地 id"用）★★
    //   纯粹是**观测**工具：把每个求生**此刻**的 state 栈原样打出来，
    //   不做任何分类判断 —— 因为"倒地是哪个 id"还没定论，
    //   先看清原始数据，再决定判据 ✓
    //   用法：有人倒地时调一次 fn=snap，记下那个人的末位是多少
    // 2026-10-13: the snap / jh_stat / az_stat dumps were removed together with
    //   the auto-terror-shock feature they were written to diagnose.
    // ★ 相机读取诊断
    if (!strcmp(fn, "jcam_dump")) {
        JosephCamera::refresh();
        PyDump::reset_buf();
        PyDump::wline("===== jcam_dump =====");
        PyDump::wline("读到 %d 个 CameraUnit，其中 placed=%d", JosephCamera::g_n, JosephCamera::g_placed);
        PyDump::wline("");
        PyDump::wline("%-4s %-10s %-10s %-10s %s", "#", "x", "z", "y", "placed");
        for (int i = 0; i < JosephCamera::g_n; i++) {
            PyDump::wline("%-4d %-10.2f %-10.2f %-10.2f %d", i,
                JosephCamera::g_items[i].x, JosephCamera::g_items[i].z,
                JosephCamera::g_items[i].y, JosephCamera::g_items[i].placed ? 1 : 0);
        }
        if (JosephCamera::g_n == 0)
            PyDump::wline("(一个都没读到 -> units_by_type[217] 取不到，或 position 字段不是这个名字)");
        return true;
    }
    // ★★ 骑士头盔预测（2026-10-21 新增）
    //   纯读已发布的状态，不再额外扫内存 —— 在渲染线程里跑也是零风险。
    //   用法：局内有骑士时调一次 → count 应该是 1、guess 应该是 N/2；
    //        没有骑士时 count=0 且 scan_ok=1（这两个数字合起来才能说"确实没有骑士"）
    if (!strcmp(fn, "knight")) {
        PyDump::reset_buf();
        PyDump::wline("===== 骑士头盔预测（PyKnight）=====");
        PyDump::wline("available=%d scan_ok=%d count=%d world_counter=%lld",
                      PyKnight::available() ? 1 : 0,
                      PyKnight::scan_ok()      ? 1 : 0,
                      PyKnight::count(),
                      (long long)PyKnight::world_counter());
        PyDump::wline("status: %s", PyKnight::status_text());
        PyDump::wline("");
        PyDump::wline("%-2s %-14s %-14s %-8s %-5s %-5s %-10s %-4s %-8s %s",
                      "#", "unit_obj", "scene_obj", "guess", "src", "item",
                      "use_id", "tgt", "cd", "casting");
        for (int ki = 0; ki < PyKnight::count(); ki++) {
            PyKnight::KnightState ks;
            if (!PyKnight::get(ki, ks)) continue;
            char gt[24];
            PyKnight::guess_text(ks, gt, sizeof(gt));
            const char *gsrc = (ks.guess_src == PyKnight::SRC_WORLD) ? "计数"
                             : (ks.guess_src == PyKnight::SRC_USEID) ? "估算" : "未知";
            char cds[16];
            if (!ks.has_cd) snprintf(cds, sizeof(cds), "-");
            else            snprintf(cds, sizeof(cds), "%.1fs", PyKnight::cd_remain_now(ks));
            PyDump::wline("%-2d 0x%-12llx 0x%-12llx %-8s %-5s %-5d %lld,%lld %-4d %-8s %d",
                          ki,
                          (unsigned long long)ks.unit_obj,
                          (unsigned long long)ks.scene_obj,
                          gt, gsrc, ks.item_id,
                          (long long)ks.cast_seq, (long long)ks.cast_seq1,
                          ks.target_cnt, cds,
                          (ks.casting_read && ks.casting) ? 1 : 0);
            for (int e = 0; e < ks.extra_n; e++)
                PyDump::wline("     extra %s = %g", ks.extra_name[e], ks.extra_val[e]);
        }
        PyDump::wline("");
        // ---- 战术预见：编码 + 全部候选（20ms 采样线的实测值）----
        const PyKnight::PredictPub &pp = PyKnight::predict_pub();
        PyDump::wline("===== 战术预见（20ms 采样）=====");
        PyDump::wline("have=%d casting=%d t=%lld use_id=%lld casts=%d",
                      pp.have ? 1 : 0, pp.casting ? 1 : 0,
                      (long long)pp.t, (long long)pp.use_id, pp.casts);
        if (pp.code_ok) {
            int lab = PyKnight::label_of_code(pp.code_val);
            PyDump::wline("code: %s = %lld  (%d 种取值)  -> %s",
                          pp.code_name, (long long)pp.code_val, pp.code_distinct,
                          lab >= 0 ? PyKnight::label_name(lab) : "未标注(点面板按钮)");
        } else {
            PyDump::wline("code: 还没出现「两种取值」的候选 —— 多打一次头盔再看");
        }
        PyDump::wline("标注: 出刀=%lld 技能=%lld 不做=%lld",
                      (long long)PyKnight::g_bind_code[0],
                      (long long)PyKnight::g_bind_code[1],
                      (long long)PyKnight::g_bind_code[2]);
        PyDump::wline("");
        PyDump::wline("%-32s %-10s %-8s %s", "candidate", "value", "distinct", "changes");
        for (int i = 0; i < PyKnight::watch_size(); i++) {
            PyKnight::WatchRow wr;
            if (!PyKnight::watch_row(i, wr)) continue;
            PyDump::wline("%-32s %-10lld %-8d %d%s",
                          wr.name, (long long)wr.v, wr.distinct, wr.changes,
                          wr.fresh ? "  *刚变" : "");
        }
        if (PyKnight::watch_size() == 0)
            PyDump::wline("(候选取不到 —— 技能对象还没读到，或 cast_args 是 None)");
        PyDump::wline("");
        PyDump::wline("时间线文件: %s", PyKnight::kPredictLogPath());
        PyDump::wline("提示：编码 -> 中文 由面板上三个按钮标注；标注前只显示原始值，不猜");
        snprintf((char*)PyDump::g_status, 128, "knight dump 完成");
        return true;
    }
    // ★ 天赋状态 dump（镜像判据核对用）
        //   实现放在这里而不是 PyGenius.h —— 那边没有 PyDump 的声明；
        //   而 PyGenius 的内部变量是 static，同一编译单元（本文件）可见 ✓
        if (!strcmp(fn, "unit_genius")) {
            PyDump::reset_buf();
            PyDump::wline("===== unit genius state =====");
            const int b = PyGenius::active_buf_index();
            const int n = PyGenius::unit_count(b);
            PyDump::wline("units = %d   (state: 0=UNKNOWN 1=PARTIAL带? 2=COMPLETE)", n);
            PyDump::wline("");
            PyDump::wline("%-14s %-14s %-5s %-8s %s", "unit_obj", "scene_obj", "camp", "state", "tag");
            for (int i = 0; i < n; i++) {
                PyGenius::UnitBrief u;
                if (!PyGenius::unit_brief(b, i, u)) continue;
                const char *tag = (u.state == 2) ? "正常"
                                : (u.state == 1) ? "★带问号" : "★无数据";
                PyDump::wline("0x%-12llx 0x%-12llx %-5d %-8d %s",
                    (unsigned long long)u.unit_obj,
                    (unsigned long long)u.scene_obj, u.camp, u.state, tag);
            }
            PyDump::wline("");
            PyDump::wline("判读: 真人应为 COMPLETE; 镜像应为 UNKNOWN/PARTIAL");
            return true;
        }

    // ★★ 2026-10-13：**框上文字的实际屏幕 Y 坐标**（实时诊断）★★
    //   用户反复反馈"名字和天赋之间的空档太大"，我推公式 4 次都算出 ~0px ✗
    //   => 说明**我量的量不是用户看的量** ✗
    //   所以直接把**每帧实际用的坐标**读出来，用像素对照，不再推 ✗
    //   ⚠ 必须走 MCP 实时查询，**不能走 CfgPut** ✗
    //     配置只在存盘时写，读到的是旧值（我上一版就栽在这）✗
    if (!strcmp(fn, "label_pos")) {
        PyDump::reset_buf();
        PyDump::wline("===== 框上文字的实际屏幕 Y 坐标 =====");
        PyDump::wline("  (记的是**最后一个被画的实体**，只读，不参与绘制)");
        PyDump::wline("");
        PyDump::wline("  框顶 Y1            = %.1f", g_diag_lbl_y1);
        PyDump::wline("  名字 y (文字顶部)  = %.1f", g_diag_lbl_name_y);
        PyDump::wline("  天赋 y (文字顶部)  = %.1f", g_diag_lbl_gen_y);
        PyDump::wline("  CD   y (文字顶部)  = %.1f", g_diag_lbl_cd_y);
        PyDump::wline("");
        PyDump::wline("  量出来的行高: 名字=%.1f  天赋=%.1f", g_diag_lbl_nlh, g_diag_lbl_glh);
        PyDump::wline("  层间距配置 label_line_gap = %.1f", g_diag_lbl_gap);
        PyDump::wline("  ★ 名字墨迹: 顶部偏移=%.1f 墨迹高=%.1f  (摆放按墨迹, 不按行盒)",
                      g_diag_lbl_ink_off, g_diag_lbl_ink_h);
        //   ★★ 字体真实垂直度量 —— 这是定方案的关键数据 ★★
        //     怀疑点：`CalcTextSizeA` 返回的是**行高(box)**，不是**字面墨迹高度** ✗
        //     字号 25 的字体，行高可能远大于 25（含 ascent+descent）✗
        //     拿行高当"占位高度"就会在两层之间凭空多出 (行高 - 字号) 的空隙 ✗
        //   所以把 baked 的 Size/Ascent/Descent 和真实字形边框都打出来 ✓
        {
            ImFont* mf = ImGui::GetFont();
            ImFontBaked* bk = mf->GetFontBaked(g_esp.name_size);
            if (bk) {
                PyDump::wline("");
                PyDump::wline("  字体度量(名字字号=%.1f): Size=%.1f Ascent=%.1f Descent=%.1f",
                              g_esp.name_size, bk->Size, bk->Ascent, bk->Descent);
                PyDump::wline("    -> 行高(Ascent+Descent) = %.1f", bk->Ascent + bk->Descent);
                PyDump::wline("    -> 与字号之差 = %.1f  (这个差就是**凭空多出的空隙**)",
                              (bk->Ascent + bk->Descent) - bk->Size);
                //   真实字形边框（'A' 是大写字母，最能代表"看起来占多高"）✓
                const ImFontGlyph* gA = bk->FindGlyph((ImWchar)'A');
                const ImFontGlyph* gg = bk->FindGlyph((ImWchar)'g');
                if (gA) PyDump::wline("    字形 'A': Y0=%.1f Y1=%.1f (墨迹高 %.1f)",
                                      gA->Y0, gA->Y1, gA->Y1 - gA->Y0);
                if (gg) PyDump::wline("    字形 'g': Y0=%.1f Y1=%.1f (带降部)",
                                      gg->Y0, gg->Y1);
                if (gA && gg)
                    PyDump::wline("    -> **实际墨迹总高('A'顶到'g'底) = %.1f**",
                                  gg->Y1 - gA->Y0);
                //   ★ 直接按"字形相对文字 y 的绝对位置"算墨迹 —— 不绕 Ascent/Descent ✓
                //     上面那两个值我连**符号**都没确认（算出 40.5 的墨迹高，明显错 ✗）
                //     而字形 Y0/Y1 就是"相对文字 y"的像素坐标（RenderText 里 y+Y0*scale）✓
                //     所以直接用它们最可靠：字形顶 = y + Y0*scale，字形底 = y + Y1*scale ✓
                if (gA && gg) {
                    const float scl = g_esp.name_size / bk->Size;
                    PyDump::wline("    ★ 按字形算: scale=%.3f  'A'顶=%.1f  'g'底=%.1f -> 墨迹高=%.1f",
                                  scl, gA->Y0 * scl, gg->Y1 * scl, (gg->Y1 - gA->Y0) * scl);
                    PyDump::wline("      (这才是**真实的、写在 RenderText 里的**那个偏移)");
                }
            } else {
                PyDump::wline("  (拿不到 ImFontBaked)");
            }
        }
        PyDump::wline("");
        //   ★★ 关键读数：**按墨迹算**，不按行盒 ✗✗
        //     我前几版这个读数用的是 `y + 行高` 当"文字底边" ✗
        //     —— **那正是我犯了一路的错**（把行盒当墨迹）✗
        //     所以诊断自己也在骗我：它报"空档 41px"，但那 41 里有一大半是行盒留白 ✗
        //     现在直接用字形算：墨迹底 = y + off + h ✓
        if (g_diag_lbl_name_y > -1.f && g_diag_lbl_gen_y > -1.f) {
            const float nm_top = g_diag_lbl_name_y + g_diag_lbl_ink_off;
            const float nm_bot = nm_top + g_diag_lbl_ink_h;
            //   ⚠ 天赋层用**它自己的**墨迹偏移 ✗（字号和名字不同）
            const float g_top  = g_diag_lbl_gen_y + g_diag_lbl_g_off;
            const float g_bot  = g_top + g_diag_lbl_g_h;
            PyDump::wline("  >>> 【按墨迹算，这才是屏幕上看到的】");
            PyDump::wline("  >>> 名字墨迹: %.1f .. %.1f   (off=%.1f h=%.1f)",
                          nm_top, nm_bot, g_diag_lbl_ink_off, g_diag_lbl_ink_h);
            PyDump::wline("  >>> 天赋墨迹: %.1f .. %.1f   (off=%.1f h=%.1f)",
                          g_top,  g_bot,  g_diag_lbl_g_off, g_diag_lbl_g_h);
            PyDump::wline("  >>> **两层墨迹之间的空隙 = %.1f px**", nm_top - g_bot);
            PyDump::wline("  >>> 名字墨迹底边离框顶 = %.1f px  (配置 name_lift=%.1f)",
                          g_diag_lbl_y1 - nm_bot, g_esp.name_lift);
            PyDump::wline("      (层空隙应 ≈ label_line_gap = %.1f)", g_diag_lbl_gap);
            PyDump::wline("");
            PyDump::wline("  (对照) 旧算法按**行盒**算会得到 %.1f px —— 那个数是假的 ✗",
                          (g_diag_lbl_name_y + g_diag_lbl_nlh) - g_diag_lbl_gen_y);
        } else {
            PyDump::wline("  (还没画过任何实体 -> 数值是 -1)");
        }
        return true;
    }

    // ★★ 2026-10-13：采样（自动校准侦测）★★
    //   放在这里（每帧一次）—— 这是**主循环**，和 PySelf/Draw 同一节奏 ✓
    //   ⚠ Sample() 内部只在 active 时干活，关着的时候零开销 ✓
    //   ★ 自动重试发现：用户按"开始记录"时可能还没解析出 game_kernel
    //     （实测就是这样："units_by_type 无效"）✗
    //     所以**每 30 帧重试一次**，解析出来就自动接上 ✓
    //     用户不用再按一次，也不会打断他修机 ✓
    //   ⚠ 一旦发现成功（n>0）就不再重试 ✗ 遍历整张 dict 每帧做太贵 ✓
    {
        CalibProbe::Recorder& cr = CalibProbe::R();
        if (cr.active && cr.n == 0 && (++cr.retry_tick % 30) == 0) {
            char e[160] = {0};
            if (CalibProbe::Discover(e, sizeof(e)))
                snprintf(cr.status, sizeof(cr.status), "自动接上: %s", e);
            else
                snprintf(cr.status, sizeof(cr.status), "重试中: %s", e);
        }
    }
    CalibProbe::Sample();

    // ★★ 2026-10-13：自动校准 —— 数据侦测（先只观测，不点击）★★
    //   用户需求（原话）：
    //     「玩求生修密码机时密码机会时不时弹出校准条，必须在**进度条黄色时**
    //       点下校准键…让辅助每次都在自身修机时出现校准条时，
    //       自动在黄条区域时点下校准键」
    //   已确认的关键约束：
    //     · 校准条**随机出现**，摸一下不一定出，也可能直接出 ✓
    //     · 校准键是**屏幕上的固定按钮** ✓
    //     · **误点有代价 —— 会让密码机暂停** ✗✗
    //   => 这一步**只做侦测、不点击** ✗
    //      没有数据就点击 = 纯随机误点 = 必定拖慢修机 ✗
    //      所以先把"黄区位置 / 游标位置"这两个量找出来 ✓
    if (!strcmp(fn, "calib_keys")) {
        PyDump::reset_buf();
        PyDump::wline("===== 自动校准 · 一次性全量诊断 =====");
        PyDump::wline("  （目标是**一次问完所有问题**，不要一问一编 —— 用户明确要求过）");
        //   --- 1) game_kernel ---
        uint64_t ud = 0;
        if (!CalibProbe::HostModuleDict(&ud)) {
            PyDump::wline("  [1] game_kernel: ✗ 拿不到");
            const char* why = "";
            PyCore::game_kernel_dict(why);
            PyDump::wline("      原因: %s", why);
            return true;
        }
        PyDump::wline("  [1] game_kernel = 0x%llx ✓", (unsigned long long)ud);

        //   --- 2) unit_mgr -> units_by_type ---
        const int64_t i_um = PyCore::find_key(ud, "unit_mgr", -1);
        const uint64_t um  = (i_um >= 0) ? PyCore::get_attr(ud, i_um) : 0;
        PyDump::wline("  [2] unit_mgr: idx=%lld val=0x%llx is_obj=%d",
                      (long long)i_um, (unsigned long long)um, PyCore::is_obj(um) ? 1 : 0);
        uint64_t ubt = 0; const char* ubt_how = "(都没拿到)";
        if (PyCore::is_obj(um)) {
            const int64_t a = PyCore::find_key(um, "units_by_type", -1);
            if (a >= 0) { ubt = PyCore::get_attr(um, a); ubt_how = "um 自身当 dict"; }
            if (!PyCore::is_obj(ubt)) {
                const uint64_t umd = getPtr64(um - 0x18) & 0x00FFFFFFFFFFFFFFULL;
                PyDump::wline("      um-0x18 = 0x%llx is_obj=%d", (unsigned long long)umd,
                              PyCore::is_obj(umd) ? 1 : 0);
                if (PyCore::is_obj(umd)) {
                    const int64_t b = PyCore::find_key(umd, "units_by_type", -1);
                    if (b >= 0) { ubt = PyCore::get_attr(umd, b); ubt_how = "um 的实例字典"; }
                }
            }
        }
        PyDump::wline("  [3] units_by_type = 0x%llx is_obj=%d  来源=%s",
                      (unsigned long long)ubt, PyCore::is_obj(ubt) ? 1 : 0, ubt_how);
        if (!PyCore::is_obj(ubt)) return true;

        //   --- 4) 密码机列表 ---
        PyCore::Dict k;
        if (!PyCore::read_dict(ubt, k)) { PyDump::wline("  [4] read_dict(units_by_type) 失败"); return true; }
        uint64_t lst = 0;
        for (int64_t i = 0; i < k.n_entries; i++) {
            uint64_t kp = getPtr64(PyCore::key_slot(k, i)) & 0x00FFFFFFFFFFFFFFULL;
            if (!PyCore::is_obj(kp)) continue;
            if (getPtr64(kp + 8) != PyRoot::g_int_type) continue;
            int64_t sz = 0; uint32_t dg = 0;
            vm_readv(kp + 16, &sz, 8); vm_readv(kp + 24, &dg, 4);
            if (sz == 1 && (int)dg == 3) {
                lst = getPtr64(PyCore::value_slot(k, i)) & 0x00FFFFFFFFFFFFFFULL;
                break;
            }
        }
        PyDump::wline("  [4] 密码机列表 = 0x%llx is_obj=%d", (unsigned long long)lst,
                      PyCore::is_obj(lst) ? 1 : 0);
        if (!PyCore::is_obj(lst)) return true;
        int64_t cnt = 0; vm_readv(lst + (uint64_t)SecOff::u64(SecOff::DG_84), &cnt, 8);
        uint64_t items = getPtr64(lst + (uint64_t)SecOff::u64(SecOff::DG_85));
        PyDump::wline("      ob_size=%lld ob_item=0x%llx", (long long)cnt,
                      (unsigned long long)items);

        //   --- 5) ★ 逐个试"实例字典"的取法，一次把结论拿到 ★ ---
        //     ⚠ 我之前只试了 `inst - 0x18`（PyProgress 用的），结果字段登记 0 个 ✗
        //       所以这里**把几种可能都试一遍**，看哪种能读出字段名 ✓
        PyDump::wline("");
        PyDump::wline("  [5] 密码机实例字典：逐个试偏移");
        const int64_t kTry[] = { -0x18, -0x10, -0x8, 0x0 };
        for (int64_t ii = 0; ii < cnt && ii < 1; ii++) {      // 只看第一台，够定结论 ✓
            uint64_t inst = getPtr64(items + ii * 8) & 0x00FFFFFFFFFFFFFFULL;
            PyDump::wline("      实例[%lld] = 0x%llx is_obj=%d", (long long)ii,
                          (unsigned long long)inst, PyCore::is_obj(inst) ? 1 : 0);
            if (!PyCore::is_obj(inst)) continue;
            for (size_t t = 0; t < sizeof(kTry) / sizeof(kTry[0]); t++) {
                const int64_t off = kTry[t];
                const uint64_t d = getPtr64(inst + off) & 0x00FFFFFFFFFFFFFFULL;
                PyDump::wline("        inst%+#llx -> 0x%llx is_obj=%d", (long long)off,
                              (unsigned long long)d, PyCore::is_obj(d) ? 1 : 0);
                PyCore::Dict dk;
                if (!PyCore::is_obj(d) || !PyCore::read_dict(d, dk)) {
                    PyDump::wline("           (read_dict 失败)");
                    continue;
                }
                PyDump::wline("           read_dict OK: 条目=%lld stride=%d  -> 前 12 个字段名:",
                              (long long)dk.n_entries, dk.stride);
                int shown = 0;
                for (int64_t j = 0; j < dk.n_entries && shown < 12; j++) {
                    uint64_t kp = getPtr64(PyCore::key_slot(dk, j)) & 0x00FFFFFFFFFFFFFFULL;
                    if (!PyCore::is_obj(kp)) continue;
                    char nm[40] = {0};
                    if (!CalibProbe::ReadName(getPtr64(kp + 16), nm, sizeof(nm))) continue;
                    PyDump::wline("             %s", nm);
                    shown++;
                }
                if (!shown) PyDump::wline("             （一个字段名都读不出）");
            }
        }
        return true;
    }
    if (!strcmp(fn, "calib")) {
        PyDump::reset_buf();
        CalibProbe::Recorder& cr = CalibProbe::R();
        PyDump::wline("===== 自动校准 · 数据侦测 =====");
        PyDump::wline("  状态: %s", cr.active ? "**记录中**" : "已停止");
        PyDump::wline("  采样次数=%d  登记字段=%d  密码机=%d  发现次数=%d",
                      cr.ticks, cr.n, cr.gen_n, cr.disc_cnt);
        PyDump::wline("  说明: %s", cr.status);
        PyDump::wline("");
        PyDump::wline("  ⚠ 本功能**只观测、不点击** ✗");
        PyDump::wline("     因为误点会让密码机暂停 -> 必须先拿到真实数据再谈点击 ✓");
        PyDump::wline("");
        PyDump::wline("  命令:");
        PyDump::wline("    fn=calib_start  开始记录（会先发现密码机字段）");
        PyDump::wline("    fn=calib_stop   停止记录");
        PyDump::wline("    fn=calib_dump   导出各字段的历史（找游标/黄区用）");
        PyDump::wline("    fn=calib_find   自动挑出\"像游标\"的字段");
        PyDump::wline("    fn=calib_reset  清空重来");
        return true;
    }
    if (!strcmp(fn, "calib_start")) {
        PyDump::reset_buf();
        CalibProbe::Recorder& cr = CalibProbe::R();
        char e[160] = {0};
        const bool ok = CalibProbe::Discover(e, sizeof(e));
        snprintf(cr.status, sizeof(cr.status), "%s", e);
        cr.active = true;
        PyDump::wline("===== 自动校准 · 开始记录 =====");
        PyDump::wline("  发现: %s", e);
        PyDump::wline("  active = 1（从现在起每帧采样）");
        PyDump::wline("");
        PyDump::wline("  接下来:");
        PyDump::wline("    1) 回游戏，去修密码机（最好是**自己**修，因为要自身修机才弹校准）");
        PyDump::wline("    2) 修到**弹出校准条**，正常点一次完成校准");
        PyDump::wline("    3) 回来执行 fn=calib_find 让我分析");
        PyDump::wline("  ⚠ 中途**不要**再调 calib_start（会清掉已记的历史）✗");
        if (!ok) PyDump::wline("  ⚠ 发现失败：%s（可能不在局内 / 还没解析出 game_kernel）", e);
        return true;
    }
    if (!strcmp(fn, "calib_stop")) {
        PyDump::reset_buf();
        CalibProbe::R().active = false;
        PyDump::wline("已停止记录（历史保留，可以用 calib_dump 导出）");
        return true;
    }
    if (!strcmp(fn, "calib_reset")) {
        PyDump::reset_buf();
        CalibProbe::R().Reset();
        PyDump::wline("已清空");
        return true;
    }
    if (!strcmp(fn, "calib_find")) {
        PyDump::reset_buf();
        CalibProbe::Recorder& cr = CalibProbe::R();
        PyDump::wline("===== 自动校准 · 找游标 =====");
        PyDump::wline("  采了 %d 次，登记 %d 个字段", cr.ticks, cr.n);
        PyDump::wline("");
        float lo = 0, hi = 0; int len = 0;
        const int hit = CalibProbe::FindCursor(&lo, &hi, &len);
        if (hit < 0) {
            PyDump::wline("  ✗ 没找到\"像游标\"的字段");
            PyDump::wline("    可能原因: 记录期间**没弹出校准条** / 数据不在 GeneratorUnit 上");
            PyDump::wline("    下一步: 用 fn=calib_dump 看哪个字段在变（人工比对）");
        } else {
            CalibProbe::FieldTrack& t = cr.f[hit];
            PyDump::wline("  ★ 命中: [%s].%s", t.owner, t.name);
            PyDump::wline("    取值范围 %.4f ~ %.4f（幅度 %.4f）", lo, hi, hi - lo);
            PyDump::wline("    变化次数 %d", len);
            PyDump::wline("");
            PyDump::wline("    判读: 若这个的范围和\"校准条黄区\"能对上，它就是游标 ✓");
            PyDump::wline("          再用 fn=calib_dump 看它旁边哪个字段在**校准开始那一刻跳变**");
            PyDump::wline("          -> 那个跳变的就是**黄区位置** ✓");
        }
        return true;
    }
    if (!strcmp(fn, "calib_dump")) {
        PyDump::reset_buf();
        CalibProbe::Recorder& cr = CalibProbe::R();
        PyDump::wline("===== 自动校准 · 字段历史 =====");
        PyDump::wline("  采了 %d 次（每帧 1 次）", cr.ticks);
        //   只列**变化过**的字段，否则 96 个字段刷屏 ✗
        PyDump::wline("");
        PyDump::wline("%-12s %-22s %8s %8s %8s %6s", "owner", "name", "min", "max", "幅度", "变化");
        int shown = 0;
        for (int i = 0; i < cr.n && shown < 40; i++) {
            CalibProbe::FieldTrack& t = cr.f[i];
            const int ns = t.n_samp;
            if (ns < 8) continue;
            const int start = (ns < CalibProbe::HIST) ? 0 : t.head;
            float mn = 1e30f, mx = -1e30f; int moves = 0; float last = 0;
            for (int j = 0; j < ns; j++) {
                const float v = t.hist[(start + j) % CalibProbe::HIST];
                if (v < mn) mn = v;
                if (v > mx) mx = v;
                if (j && (v - last > 1e-6f || v - last < -1e-6f)) moves++;
                last = v;
            }
            if (mx - mn < 1e-6f && moves == 0) continue;    // 没变过的不列 ✓
            PyDump::wline("%-12s %-22s %8.3f %8.3f %8.3f %6d",
                          t.owner, t.name, mn, mx, mx - mn, moves);
            shown++;
        }
        if (!shown) PyDump::wline("  （没有任何字段变化过 —— 记录期间可能不在修机）");
        PyDump::wline("");
        PyDump::wline("  判读: 找**幅度大且变化次数多**的 -> 候选游标 ✓");
        PyDump::wline("        找**只变 1~2 次、幅度中等**的 -> 候选黄区位置 ✓");
        return true;
    }

    // ★★ 2026-10-13：准备阶段"天赋列表"逐环节实时诊断 ★★
    //   用户现场：**设置页显示识别到 4 条天赋，但预知监管下方不显示** ✗
    //   ⚠ 必须做成 MCP 实时查询，**不能走 CfgPut** ✗
    //     配置只在存盘时写，读到的是旧值（我上一版就栽在这）✗
    if (!strcmp(fn, "prep_diag")) {
        PyDump::reset_buf();
        PyDump::wline("===== 准备阶段天赋列表 诊断 =====");
        PyDump::wline("  四个前置条件（都要成立才画）:");
        PyDump::wline("    show_draw_prophet  = %d   (预知监管开着?)",    g_dbg_prep_show);
        PyDump::wline("    in_prep_scene()    = %d   (★ 现在的判据: 场景名含 h55_wait)",
                      g_dbg_prep_notbattle);
        PyDump::wline("    in_lobby()         = %d   (⚠ 旧判据之一，区分不了 -> 已弃用)",
                      g_dbg_prep_lobby);
        PyDump::wline("    prep_as_hunter()   = %d   (我是监管?)",          g_dbg_prep_hunter);
        PyDump::wline("    civ_size()         = %d   (>0 才有求生者)",      g_dbg_prep_civ);
        PyDump::wline("  合成: %s", (g_dbg_prep_show && g_dbg_prep_notbattle &&
                                     g_dbg_prep_hunter && g_dbg_prep_civ > 0)
                                     ? "★ 条件都成立 -> 应该画" : "✗ 有条件不成立 -> 不画");
        //   ★ 场景标识 —— 用来找出"准备阶段 vs 局内"真正能区分的判据 ✓
        //     （实测：准备阶段 in_lobby=0 且 in_battle_scene=1，
        //       说明**两个已有判据都区分不了这两种状态** ✗
        //       所以必须看具体的场景名，而不是再猜一个布尔条件 ✗）
        PyDump::wline("");
        PyDump::wline("  场景标识（用来定正确的判据）:");
        PyDump::wline("    scene_path = [%s]", PyMiniMap::scene_path());
        PyDump::wline("    scene_pic  = [%s]", PyMiniMap::scene_pic());
        PyDump::wline("    minimap available/team = %d / %d",
                      PyMiniMap::available() ? 1 : 0, PyMiniMap::team_count());
        PyDump::wline("");
        PyDump::wline("  数据侧:");
        PyDump::wline("    bits!=0 的候选条数 = %d", g_dbg_prep_used);
        PyDump::wline("    拼接出的串长度     = %d", g_dbg_prep_len);
        PyDump::wline("    拼接内容: [%s]", g_dbg_prep_str);
        PyDump::wline("");
        PyDump::wline("  绘制侧:");
        PyDump::wline("    g_dbg_prep_drawn   = %d   (1=真的调了 AddText)", g_dbg_prep_drawn);
        PyDump::wline("");
        PyDump::wline("  判读:");
        PyDump::wline("    候选=%d 但串长=0  -> 数据到了但拼不出内容（看 civ_at 的 seat/bits）", g_dbg_prep_used);
        PyDump::wline("    条件全 1 且 drawn=1 -> 画了，但用户看不到（位置/被挡/颜色透明）");
        PyDump::wline("    条件有 0           -> 是哪一条不成立，针对性修");
        PyDump::wline("");
        PyDump::wline("  ★ 另外看: 设置页那 4 条走的是**另一条数据源**");
        PyDump::wline("     (g_avatar.final_genius_dict)，本行走 civ_at()/g_civ_buf  ✗");
        PyDump::wline("     两者不同源 -> 一个有一个没有是**完全可能**的 ✓");
        return true;
    }

    // ★★ 2026-10-13 新增诊断：天赋**显示链路**逐项核对 ★★
    //   起因（用户反馈）：
    //     · "现在不显示监管的天赋了"
    //     · "调整了求生的天赋颜色也没效果，不管调成什么颜色他都还是绿的"
    //     · "天赋显示改成紧贴着名字上方，现在这个间隔太大了"
    //   这三条都跟**决策链**有关，所以这里把每一步都打出来，
    //   而不是靠猜哪一个条件没满足 ✗
    //   看什么：camp / state / bits / 三段文字 / desperate_state / **最终颜色** / Y
    if (!strcmp(fn, "genius_style")) {
        PyDump::reset_buf();
        PyDump::wline("===== 天赋显示链路 =====");
        //   先看配置值 —— 排除"配置没生效"这一类原因 ✓
        PyDump::wline("配置: show_draw_Genius=%d show_draw_trait=%d",
                      show_draw_Genius ? 1 : 0, show_draw_trait ? 1 : 0);
        PyDump::wline("配置: genius_size=%.0f genius_color=#%08X genius_color_surv=#%08X",
                      g_esp.genius_size,
                      (unsigned)g_esp.genius_color, (unsigned)g_esp.genius_color_surv);
        PyDump::wline("配置: trait_size=%.0f trait_color=#%08X  name_size=%.0f",
                      g_esp.trait_size, (unsigned)g_esp.trait_color, g_esp.name_size);
        PyDump::wline("配置: prophet_size=%.0f hatch_size=%.0f hatch_color=#%08X",
                      g_esp.prophet_size, g_esp.hatch_size, (unsigned)g_esp.hatch_color);
        //   ★ 行高实测：证明"紧贴"是真的贴着，而不是我口头说紧了 ✓
        //     （之前两版都是"拍脑袋留余量"，所以一直显得空 ✗）
        {
            ImFont* mf = ImGui::GetFont();
            const float nlh = mf->CalcTextSizeA(g_esp.name_size,   FLT_MAX, 0.0f, "Ag").y;
            const float glh = mf->CalcTextSizeA(g_esp.genius_size, FLT_MAX, 0.0f, "Ag").y;
            const float clh = mf->CalcTextSizeA(g_esp.trait_size,  FLT_MAX, 0.0f, "Ag").y;
            const float T = 1.0f;      // kLineTouch
            PyDump::wline("行高实测: 名字=%.1f 天赋=%.1f CD=%.1f  (层间隙=%.1f)",
                          nlh, glh, clh, T);
            //   ★ 复刻绘制处的真实游标（第一版这里算术写错了 ✗ 已按公式重算）
            //     起点 above_y = Y1 - T - 名字行高
            //     名字画在起点 -> 天赋前再 -= (名字行高 + T) -> CD 前再 -= (天赋行高 + T)
            const float y_name = -(T + nlh);
            const float y_gen  = y_name - (nlh + T);
            const float y_cd   = y_gen  - (glh + T);
            PyDump::wline("堆叠(自框顶 Y1 往上): 名字顶=Y1%.1f  天赋顶=Y1%.1f  CD顶=Y1%.1f",
                          y_name, y_gen, y_cd);
            PyDump::wline("  -> 名字底边离框顶 %.1fpx ; 天赋底边离名字顶 %.1fpx ; CD底边离天赋顶 %.1fpx",
                          T, T, T);
        }
        PyDump::wline("");
        const int b = PyGenius::active_buf_index();
        const int n = PyGenius::unit_count(b);
        PyDump::wline("%-14s %-4s %-6s %-9s %-4s %-22s %s",
                      "unit_obj", "camp", "state", "bits", "desp", "segments", "最终色");
        for (int i = 0; i < n; i++) {
            PyGenius::UnitBrief u;
            if (!PyGenius::unit_brief(b, i, u)) continue;
            //   ☠ 第一版这里传的是 `u.unit_obj` 且坐标给 (0,0) -> **全部 lookup 失败** ✗
            //     两个原因：
            //       ① `lookup` 第一优先比的是 **`scene_obj`**，不是 unit_obj ✗
            //          （绘制处传的 `data[i].obj` 就是 scene_obj ✓）
            //       ② 距离兜底也要真实坐标；(0,0) 离任何单位都远 ✗
            //     教训：**诊断代码本身也会错**，看到"全都失败"要先怀疑诊断 ✗
            PyGenius::Info gi;
            if (!PyGenius::lookup((uintptr_t)u.scene_obj, 0.0f, 0.0f, gi)) {
                PyDump::wline("0x%-12llx scene=0x%-12llx camp=%d  ★lookup 失败(scene_obj 为空?)",
                              (unsigned long long)u.unit_obj,
                              (unsigned long long)u.scene_obj, u.camp);
                continue;
            }
            PyGenius::GeniusLine seg;
            PyGenius::genius_segments(gi, seg);
            //   2026-10-26：记下"自己带飞轮"（自动飞轮的触发门）
            {
                uint64_t _selfsc = 0;
                PySelf::anchor(_selfsc);
                if (_selfsc && data[i].obj && (uint64_t)data[i].obj == _selfsc) {
                    AutoSkill::g_self_fly_probed = true;
                    if (seg.flywheel[0]) AutoSkill::g_self_has_flywheel = true;
                }
            }
            const int ds = PyGenius::desperate_state(gi);
            //   ★ **完全复刻绘制处的颜色决策**，这样看到的色就是屏幕上的色 ✓
            //   ⚠ 这里必须和绘制处**改成一样** ✗
            //     第一版忘了同步 -> 诊断报"语义绿"而屏幕其实是用户色 ->
            //     差点让我误判"修复没生效" ✗
            //     **教训：诊断代码和绘制代码是两份逻辑，改一处必须改另一处** ✗
            const bool is_surv = (gi.camp == 2);
            const ImU32 base = is_surv ? (ImU32)g_esp.genius_color_surv
                                       : (ImU32)g_esp.genius_color;
            ImColor c = ImColor(base);
            const char* which = is_surv ? "配置(求生色)" : "配置(监管色)";
            char segbuf[64];
            snprintf(segbuf, sizeof(segbuf), "%s|%s|%s", seg.pre, seg.flywheel, seg.post);
            //   ★ 把**实际要用的 Y 坐标**也打出来，方便和截图对照 ✓
            //     用户反馈"名字和天赋中间还是空很大距离" -> 我不再靠推，
            //     直接打出坐标，再用截图量 ✗（推了两轮都没解决）
            PyDump::wline("0x%-12llx %-4d %-6d 0x%-7X %-4d %-22s #%02X%02X%02X %s%s",
                          (unsigned long long)u.unit_obj, gi.camp, gi.genius_state,
                          (unsigned)gi.genius_bits, ds, segbuf,
                          (unsigned)(c.Value.x * 255), (unsigned)(c.Value.y * 255),
                          (unsigned)(c.Value.z * 255), which,
                          seg.flywheel_ready ? "  [飞轮段=红]" : "");        }
        PyDump::wline("");
        PyDump::wline("判读要点:");
        PyDump::wline("  camp 1=监管 2=求生 236=梦之信徒");
        PyDump::wline("  '最终色' 就是**屏幕上的颜色**（配置色，不再被语义绿覆盖）");
        PyDump::wline("  唯一保留的语义色: 飞轮就绪 -> 红（那一小段）");
        PyDump::wline("  监管 state=1(PARTIAL) 会显示 '?' —— 那是数据本身不全,不是没画");
        return true;
    }
    // ★ 空槽过滤诊断
    if (!strcmp(fn, "skip_stat")) {
        PyDump::reset_buf();
        PyDump::wline("===== 空槽过滤诊断 =====");
        PyDump::wline("ShouldSkipEntity 见过 camp2 : %d", g_dbg_camp2_seen);
        PyDump::wline("其中判为空槽并跳过     : %d", g_dbg_camp2_skip);
        PyDump::wline("data[] 里 camp2 条数(本帧): %d", g_dbg_data_camp2);
        PyDump::wline("安全网计数 s_nz_surv      : %d  (<=0 表示过滤被禁用)", s_nz_surv);
        PyDump::wline("");
        PyDump::wline("判读:");
        PyDump::wline("  见过=0          -> ShouldSkipEntity 没被调用");
        PyDump::wline("  见过>0 跳过=0   -> 空槽条件没命中");
        PyDump::wline("  跳过>0 但框还在 -> 有第二条绘制路径");
        return true;
    }
    // ★ 镜像学习器日志（只读，不改变任何绘制行为）
    if (!strcmp(fn, "mirror_learn")) {
        PyDump::reset_buf();
        PyDump::wline("===== mirror_learn 日志 =====");
        PyDump::wline("跟踪实体 %d 个；已记住镜像地址 %d 个",
                      MirrorLearn::g_trk_n, MirrorLearn::g_mirror_n);
        PyDump::wline("");
        PyDump::wline("--- 记住的镜像地址 ---");
        for (int i = 0; i < MirrorLearn::g_mirror_n; i++)
            PyDump::wline("  0x%llx", (unsigned long long)MirrorLearn::g_mirror_addr[i]);
        PyDump::wline("");
        PyDump::wline("--- 跳变日志（最多 32 条）---");
        const int ln = MirrorLearn::g_log_n;
        for (int i = (ln > 32 ? ln - 32 : 0); i < ln; i++)
            PyDump::wline("  %s", MirrorLearn::g_log[i % 32]);
        PyDump::wline("");
        PyDump::wline("--- 当前跟踪表（前 16 个）---");
        for (int i = 0; i < MirrorLearn::g_trk_n && i < 16; i++)
            PyDump::wline("  0x%llx  (%.0f,%.0f)  跳变 %d 次%s",
                (unsigned long long)MirrorLearn::g_trk[i].obj,
                MirrorLearn::g_trk[i].x, MirrorLearn::g_trk[i].y,
                MirrorLearn::g_trk[i].jumps,
                MirrorLearn::is_mirror(MirrorLearn::g_trk[i].obj) ? "  ★镜像" : "");
        return true;
    }
    if (!strcmp(fn, "mirror_learn_reset")) {
        MirrorLearn::Reset();
        PyDump::reset_buf();
        PyDump::wline("mirror_learn 已重置");
        return true;
    }
if (!strcmp(fn, "mirror_watch")) {
        PyDump::reset_buf();
        PyDump::wline("===== mirror_watch =====");
            for (int i = 0; i < data_count; i++) {
        if (!data[i].obj) continue;
        const bool hasc = (data[i].objcoor != 0);
        float px_ = 0, py_ = 0;
        if (hasc) {
            px_ = getFloat(data[i].objcoor + (uint64_t)SecOff::u64(SecOff::DG_86));
            py_ = getFloat(data[i].objcoor + (uint64_t)SecOff::u64(SecOff::DG_87));
        }
        // 现在**不过滤阵营** —— 相机是 camp9，必须能看到
        // ★ 追加候选判别位（2026-10-12）：
        //   +0x70  : 实测在"同角色两条实体"之间是 1 / 0（二值 ✓）
        //            ⚠ 注意：IsGhostEntity 里写的是 != 0x1000000，
        //            但实际 dword 是 1 或 0 —— 那个比较恒成立，是个真 bug。
        //   +0x100 / +0x108 : 疑似模型/渲染对象指针（用户提议用"有没有实际模型"判断 ✓）
        //   先只记录，不改任何判定 ✓
        // ★★ 短名：先取最后一个反斜杠，再决定用"斜杠后那段"还是"整串"
        //    ⚠ 必须放在 wline(...) **之前** ✗
        //      它是**变量定义**，不能塞进函数实参列表里 ✗（编译报 expected expression）
        const char* sl = strrchr(data[i].class_name, 0x5C);
        PyDump::wline("M %llx %d %.2f %.2f %.1f 0x%X %d %llx %llx %llx %s",
            (unsigned long long)data[i].obj, data[i].camp, px_, py_,
            getFloat(data[i].obj + (uint64_t)SecOff::u64(SecOff::DG_88)), getDword(data[i].obj + (uint64_t)SecOff::u64(SecOff::DG_89)),
            hasc ? 1 : 0,
            (unsigned long long)getPtr64(data[i].obj + (uint64_t)SecOff::u64(SecOff::DG_90)),
            (unsigned long long)getPtr64(data[i].obj + (uint64_t)SecOff::u64(SecOff::DG_91)),
            (unsigned long long)getPtr64(data[i].obj + (uint64_t)SecOff::u64(SecOff::DG_92)),
            (sl && sl[1]) ? (sl + 1)
                          : (data[i].class_name[0] ? data[i].class_name : "?"));
    }
    return true;
}
if (!strcmp(fn, "room_hunt"))      { PyDump::dump_room_hunt();                 return true; }
    if (!strcmp(fn, "layout_blocks"))  { PyDump::dump_layout_blocks();             return true; }
    if (!strcmp(fn, "scene_childs"))   { PyDump::dump_scene_childs();              return true; }
    if (!strcmp(fn, "current_scene"))  { PyDump::dump_current_scene();             return true; }
    if (!strcmp(fn, "map_candidates")) { PyDump::dump_map_candidates();            return true; }
    if (!strcmp(fn, "units_by_type"))  { PyDump::dump_units_by_type();             return true; }
    if (!strcmp(fn, "all_units"))      { PyDump::dump_all_units();                 return true; }
    if (!strcmp(fn, "game_kernel"))    { PyDump::dump_game_kernel();               return true; }
    if (!strcmp(fn, "type_units"))     { PyDump::dump_type_units(arg);             return true; }
    if (!strcmp(fn, "unit_by_type"))   { PyDump::dump_unit_by_type((int)arg, (int)arg2); return true; }
    if (!strcmp(fn, "unit_by_scene"))  { PyDump::dump_unit_by_scene((uint64_t)arg); return true; }
    if (!strcmp(fn, "skill_fields"))   { PyDump::dump_skill_fields((uint64_t)arg); return true; }
    if (!strcmp(fn, "dict_at"))        { PyDump::dump_dict_at((uint64_t)arg);      return true; }
    if (!strcmp(fn, "dict_raw"))       { PyDump::dump_dict_raw((uint64_t)arg);      return true; }
    if (!strcmp(fn, "modules"))        { PyDump::dump_modules();                   return true; }
    if (!strcmp(fn, "named_raw"))      { PyDump::dump_named_raw(PyDump::filter());  return true; }
    if (!strcmp(fn, "module_raw"))     { PyDump::dump_module_raw(PyDump::filter()); return true; }
    if (!strcmp(fn, "path_raw"))       { PyDump::dump_path_raw(PyDump::filter());   return true; }
    if (!strcmp(fn, "memdump"))        { PyDump::dump_memdump((uint64_t)arg, arg2); return true; }
    if (!strcmp(fn, "typeinfo"))       { PyDump::dump_typeinfo((uint64_t)arg);      return true; }
    if (!strcmp(fn, "find_path"))      { PyDump::dump_find_path(PyDump::filter());  return true; }
    if (!strcmp(fn, "memfind"))        { PyDump::dump_memfind(PyDump::filter(), (uint64_t)arg, arg2); return true; }
    if (!strcmp(fn, "memwrite"))       { PyDump::dump_memwrite(PyDump::filter(), (uint64_t)arg, arg2); return true; }
    if (!strcmp(fn, "draw_stats"))     { PyDump::dump_draw_stats(); return true; }
    if (!strcmp(fn, "dbg_icon"))       { dbg_icon_dump(); return true; }
    //   2026-10-26：**出刀判据诊断**（约瑟夫"只有第一刀触发"查因）
    if (!strcmp(fn, "cfg_bones"))      { cfg_bones_dump(); return true; }
    if (!strcmp(fn, "dbg_atk"))        { dbg_atk_dump(); return true; }
    if (!strcmp(fn, "cfg_bones"))      { cfg_bones_dump(); return true; }
    if (!strcmp(fn, "atk_trace"))      { atk_trace_dump(); return true; }
    if (!strcmp(fn, "copycat_book"))   { PyDump::dump_copycat_book(); return true; }
    if (!strcmp(fn, "copycat_units"))  { PyDump::dump_copycat_units(); return true; }
    if (!strcmp(fn, "copycat_skills")) { PyDump::dump_copycat_skills(); return true; }
    if (!strcmp(fn, "rescue_state"))   { PyDump::dump_rescue_state(); return true; }
    // ★ 心跳诊断（2026-10-13）—— 只读缓存值，**绝不碰 ImGui** ✗（见 vs_layout 的教训）
    if (!strcmp(fn, "hb")) {
        PyDump::reset_buf();
        PyDump::wline("===== 授权心跳 =====");
        PyDump::wline("状态        : %s", Wenxin::StatusText());
        PyDump::wline("分类        : %s", Wenxin::LastClassText());
        PyDump::wline("              死=降级 / 警告=放行（一卡多用必须放行）");
        PyDump::wline("授权有效    : %d", Wenxin::Authorized() ? 1 : 0);
        PyDump::wline("运行中      : %d", Wenxin::g_hb_cfg.enabled ? 1 : 0);
        PyDump::wline("总请求/成功 : %d / %d",
                      Wenxin::g_hb.total_n.load(), Wenxin::g_hb.ok_n.load());
        PyDump::wline("连续失败    : %d (上限 %d)",
                      Wenxin::g_hb.fail_n.load(), Wenxin::g_hb_cfg.fail_limit);
        PyDump::wline("当前凭证    : 0x%08X", Wenxin::g_hb.token.load());
        PyDump::wline("功能掩码    : 0x%08X  (非 0 = 已降级)",
                      Wenxin::CoreMask());
        PyDump::wline("");
        PyDump::wline("--- type=31 用户扣时间（服务端强制计时）---");
        PyDump::wline("扣时间开关  : %d", Wenxin::g_hb_cfg.deduct_time);
        PyDump::wline("成功/失败   : %d / %d",
                      Wenxin::g_hb.deduct_ok_n.load(),
                      Wenxin::g_hb.deduct_fail_n.load());
        PyDump::wline("最后返回    : \"%s\"", Wenxin::g_hb.deduct_last);
        if (Wenxin::g_hb.deduct_ok_n.load() == 0 &&
            Wenxin::g_hb.deduct_fail_n.load() > 0) {
            PyDump::wline("--- 官方错误码对照 ---");
            PyDump::wline("-81067 扣除的时间数量必须大于0");
            PyDump::wline("-81068 当前扣费模式为点数计费，无法使用扣除时间功能");
            PyDump::wline("-81025 软件扣费模式和卡类不符");
            PyDump::wline("-81026 当前扣费模式为时间计费，无法使用扣点功能");
            PyDump::wline("=> 这些是「扣费模式不匹配」，**不是账号失效** ✗");
            PyDump::wline("   所以默认 deduct_required=0（只看 type=8）✓");
            PyDump::wline("   要用严格模式先把 software 扣费模式改成时间计费");
        }
        PyDump::wline("");
        PyDump::wline("周期        : %d + [0,%d) 秒   宽限 %d 秒",
                      Wenxin::g_hb_cfg.base_sec, Wenxin::g_hb_cfg.jitter_sec,
                      Wenxin::g_hb_cfg.grace_sec);
        PyDump::wline("");
        PyDump::wline("--- type=8 会命中的关键错误码（官方对照表）---");
        PyDump::wline("-81023 请先登陆后再调用 或 账号已被顶下线 ★心跳主要抓这个");
        PyDump::wline("-81031 用户已在别的地方登陆 ★");
        PyDump::wline("-81028 用户在别的机器登陆，请等10分钟 ★");
        PyDump::wline("-81029 用户在别的IP登陆，请等10分钟");
        PyDump::wline("-81030 已超过最大登陆数量（多开超限）★");
        PyDump::wline("-81040 检测周期必须 >= 3 分钟 ← 代码已写死 180 秒下限 ✓");
        PyDump::wline("-81015 频繁调用，请等10分钟");
        PyDump::wline("-81016 单IP频繁访问限制");
        PyDump::wline("-81039 用户电脑特征已被列入黑名单");
        PyDump::wline("-81062 当前软件位已到期 / -83006 单码已到期");
        PyDump::wline("");
        PyDump::wline(GUARD_STR("=> 心跳一次覆盖：卡密失效 / 被顶下线 / 超多开 / 黑名单 ✓"));
        return true;
    }
    if (!strcmp(fn, "vs_layout")) {
        PyDump::reset_buf();
        //   ☠ 千万不要在这里调 ImGui 函数 ✗✗
        //     dump 处理器**不在 ImGui 帧内**（它是 MCP 回调里跑的），
        //     调 ImGui::GetMainViewport()/GetWindowHeight() 会**直接把覆盖层搞崩** ☠
        //     实测：日志停在 `ExecuteOne: -> dwrg_dump` 之后，进程就没了
        //   只能用渲染时缓存下来的值（FloatProbe 在 Feature() 里填）✓
        PyDump::wline("功能页可用高度 panelH = %.1f px", UI_Pages::FloatProbe::panel);
        PyDump::wline("本页正文起始 Y  panelY0 = %.1f px", UI_Pages::FloatProbe::y0);
        PyDump::wline("窗口高度       winH    = %.1f px", UI_Pages::FloatProbe::winH);
        PyDump::wline("列表行数       rows    = %d  (需 %d px)",
                      UI_Pages::FloatProbe::rows, UI_Pages::FloatProbe::rows * 26 + 12);
        PyDump::wline("装得下: %s",
                      (UI_Pages::FloatProbe::winH >= UI_Pages::FloatProbe::rows * 26 + 12 + 46)
                          ? "是" : "否 -> 行 10~12 会被父窗口裁掉");
        return true;
    }
    // 诊断：看改视角列表里每个角色的名字到底读成了什么
    if (!strcmp(fn, "vs_names")) {
        PyDump::reset_buf();
        // ★ 先列出这局 units_by_type 里都有哪些 type、各多少个 ✓
        //   模仿者局的玩家在 1088（CopycatPlayerUnit），**不在 1/2** ✗
        {
            const char *why = nullptr;
            const uint64_t md = PyCore::game_kernel_dict(why);
            uint64_t gk = 0;
            if (md) {
                PyCore::Dict mk;
                if (PyCore::read_dict(md, mk)) {
                    const int64_t i = PyCore::find_key(md, "unit_mgr", -1);
                    if (i >= 0) gk = PyCore::get_attr(md, i);
                }
            }
            PyDump::wline("unit_mgr = 0x%llX", (unsigned long long)gk);
            if (gk) {
                const uint64_t umd = PyCore::get_inst_dict(gk);
                PyCore::Dict ud;
                if (umd && PyCore::read_dict(umd, ud)) {
                    const int64_t iu = PyCore::find_key(umd, "units_by_type", -1);
                    const uint64_t ubt = (iu >= 0) ? PyCore::get_attr(umd, iu) : 0;
                    PyDump::wline("units_by_type = 0x%llX", (unsigned long long)ubt);
                    PyCore::Dict uk;
                    if (ubt && PyCore::read_dict(ubt, uk)) {
                        PyDump::wline("--- 本局各 type 数量 ---");
                        for (int64_t k = 0; k < uk.n_entries; k++) {
                            const uint64_t ko = getPtr64(PyCore::key_slot(uk, k));
                            int64_t tv = 0;
                            if (!PyCore::read_int(ko, tv)) continue;
                            const uint64_t lst = getPtr64(PyCore::value_slot(uk, k));
                            uint64_t n = 0;
                            int64_t ln = 0;
                            if (lst && vm_readv(lst + (uint64_t)SecOff::u64(SecOff::DG_93), &ln, 8) && ln > 0) n = (uint64_t)ln;
                            if (n > 0)
                                PyDump::wline("  type %-6lld 数量 %llu", (long long)tv,
                                              (unsigned long long)n);
                        }
                    }
                }
            }
        }
        ViewSwitch::scan();
        PyDump::wline("--- 扫描结果 ---");
        ViewSwitch::scan();
        PyDump::wline("is_copycat=%d  filter=%d  MAXT=%d",
                      ViewSwitch::g_is_copycat ? 1 : 0, ViewSwitch::g_filter,
                      ViewSwitch::MAXT);
        PyDump::wline("跳过: 非对象=%d read_one失败=%d 过滤求生=%d 过滤监管=%d",
                      ViewSwitch::g_skip_reason[0], ViewSwitch::g_skip_reason[1],
                      ViewSwitch::g_skip_reason[2], ViewSwitch::g_skip_reason[3]);
        PyDump::wline("扫到 %d 个角色", ViewSwitch::g_n);
        for (int i = 0; i < ViewSwitch::g_n; i++) {
            const ViewSwitch::Target &t = ViewSwitch::g_t[i];
            PyDump::wline("[%d] uid=%u camp=%d name=\"%s\"  (%.0f,%.0f) %.1fm  unit=0x%llX scene=0x%llX coor=0x%llX",
                          i, t.uid, t.camp, t.name, t.x, t.z, t.dist_m,
                          (unsigned long long)t.unit,
                          (unsigned long long)t.scene,
                          (unsigned long long)t.coor);
        }
        // 对比身份簿里的 body 地址，看差在哪
        {
            PySelf::CopycatBook bk = PySelf::copycat_book();
            PyDump::wline("--- 身份簿 body 地址 ---");
            for (int i = 0; i < 12; i++) {
                if (!bk.seat[i].known) continue;
                PyDump::wline("  %2d号  identity=%d  body=0x%llX  dead=%d",
                              i + 1, bk.seat[i].identity,
                              (unsigned long long)bk.seat[i].body,
                              bk.seat[i].dead ? 1 : 0);
            }
            PyDump::wline("--- 直接对每个 scene 调 copycat_identity ---");
            for (int i = 0; i < ViewSwitch::g_n; i++) {
                int idn = -1, st = -1;
                const bool ok = PySelf::copycat_identity(ViewSwitch::g_t[i].scene, idn, st);
                PyDump::wline("  scene=0x%llX -> ok=%d ident=%d seat=%d   active=%d",
                              (unsigned long long)ViewSwitch::g_t[i].scene,
                              ok ? 1 : 0, idn, st, PySelf::copycat_active() ? 1 : 0);
            }
        }
        return true;
    }
    // ★★ 改视角：锁定 / 还原（2026-10-13）★★
    //   用法: fn=vs_lock  name="lock <uid>" | "unlock" | "status"
    //   机制：把 g_cam_ctrl.unit 换成目标角色对象 -> 相机就跟过去了 ✓
    //   ⚠ 只改这一个字段，**绝不碰 refcount** ✓（改 refcount 会崩游戏 ☠）
    if (!strcmp(fn, "vs_lock")) {
        PyDump::reset_buf();
        const char *arg = PyDump::filter();
        char op[16] = {0};
        unsigned uid = 0;
        if (arg) sscanf(arg, "%15s %u", op, &uid);

        const uint64_t slot = ViewSwitch::resolve_cam_slot();
        PyDump::wline("g_cam_ctrl dict = 0x%llX", (unsigned long long)ViewSwitch::g_cam_dict_addr);
        PyDump::wline("g_cam_ctrl.unit 值槽 = 0x%llX", (unsigned long long)slot);
        if (!slot) PyDump::wline("解析失败原因: %s", ViewSwitch::g_cam_why);
        PyDump::wline("当前 unit = 0x%llX", (unsigned long long)ViewSwitch::cam_unit());
        PyDump::wline("我的单位  = 0x%llX", (unsigned long long)ViewSwitch::g_my_unit);
        PyDump::wline("当前锁定  = %d (uid=%d)", ViewSwitch::g_lock_unit ? 1 : 0,
                      ViewSwitch::g_lock_uid);

        if (!strcmp(op, "unlock")) {
            PyDump::wline("还原 -> %s", ViewSwitch::UnlockView() ? "成功" : "失败");
            PyDump::wline("现在 unit = 0x%llX", (unsigned long long)ViewSwitch::cam_unit());
            return true;
        }
        if (!strcmp(op, "lock") && uid) {
            // 在已扫描到的目标里找
            ViewSwitch::scan();
            uint64_t obj = 0;
            for (int i = 0; i < ViewSwitch::g_n; i++)
                if (ViewSwitch::g_t[i].uid == uid) { obj = ViewSwitch::g_t[i].unit; break; }
            PyDump::wline("扫到 %d 个目标，找 uid=%u -> 0x%llX",
                          ViewSwitch::g_n, uid, (unsigned long long)obj);
            if (!obj) { PyDump::wline("没找到这个 uid（不在对局里？）"); return true; }
            const bool ok = ViewSwitch::LockTo(obj, (int)uid);
            PyDump::wline("锁定 -> %s", ok ? "成功" : "失败");
            PyDump::wline("现在 unit = 0x%llX", (unsigned long long)ViewSwitch::cam_unit());
            PyDump::wline("状态: %s", ViewSwitch::g_status);
            return true;
        }
        PyDump::wline("用法: name=\"lock <uid>\" / \"unlock\" / \"status\"");
        return true;
    }
    // ★★ 解析 Python 属性的【值槽地址】（改视角用）—— 只读，不写内存 ✓
    //   用法: fn=slot_addr  name="<对象地址十六进制>,<属性名>"
    //   例:   fn=slot_addr  name="0x76042DB8B0,lock_target_unit"
    //
    //   为什么需要它：改一个 Python 对象的属性 = 把新指针写进它 __dict__ 的
    //   对应「值槽」。而槽地址必须按本工程的 dict 布局算 ——
    //   我自己在 PC 侧解析键对象时对不上（字符串布局比预期复杂）✗
    //   所以直接调工程自己的 resolve_index + value_slot，那是**到处在用、
    //   已经验证过**的代码 ✓ 这样就不存在"猜偏移"的问题了 ✓
    if (!strcmp(fn, "slot_addr")) {
        PyDump::reset_buf();
        const char *arg = PyDump::filter();     // name= 的内容
        unsigned long long obj = 0;
        char attr[96] = {0};
        if (sscanf(arg, "%llx,%95s", &obj, attr) != 2 &&
            sscanf(arg, "0x%llx,%95s", &obj, attr) != 2) {
            PyDump::wline("用法: name=\"0x<对象地址>,<属性名>\"");
            return true;
        }
        PyDump::wline("对象   = 0x%llX", obj);
        PyDump::wline("属性名 = %s", attr);
        // ★ 两种来源：
        //   ① obj 是**实例** -> 用 get_inst_dict 拿 __dict__（g_cam_ctrl.cam_handler 这种）
        //   ② obj 本身就是 **dict** -> 直接用（g_cam_ctrl 自己就是 dict ✗ 用①会失败）
        //   判据：先试①，失败就把 obj 当 dict ✓
        uint64_t d = PyCore::get_inst_dict(obj);
        const char *src = "实例的 __dict__";
        if (!d) { d = obj; src = "参数本身就是 dict"; }
        if (!d) { PyDump::wline("既不是实例也不是 dict"); return true; }
        PyDump::wline("字典来源 = %s", src);
        PyDump::wline("实例字典 = 0x%llX", (unsigned long long)d);
        PyCore::Dict dk;
        if (!PyCore::read_dict(d, dk)) { PyDump::wline("read_dict 失败"); return true; }
        PyDump::wline("条目数=%lld stride=%d entries=0x%llX",
                      (long long)dk.n_entries, dk.stride,
                      (unsigned long long)dk.entries);
        const int64_t idx = PyCore::resolve_index(d, dk, -1, attr);
        if (idx < 0) { PyDump::wline("找不到属性 %s", attr); return true; }
        const uint64_t kslot = PyCore::key_slot(dk, idx);
        const uint64_t vslot = PyCore::value_slot(dk, idx);
        const uint64_t kobj  = getPtr64(kslot);
        const uint64_t vobj  = getPtr64(vslot);
        char kname[96] = {0};
        if (kobj) PyCore::read_str_any(kobj, kname, sizeof(kname));
        PyDump::wline("下标     = %lld", (long long)idx);
        PyDump::wline("键槽     = 0x%llX  键对象=0x%llX  键名=\"%s\"",
                      (unsigned long long)kslot, (unsigned long long)kobj, kname);
        PyDump::wline("★★ 值槽 = 0x%llX  当前值对象=0x%llX",
                      (unsigned long long)vslot, (unsigned long long)vobj);
        // 校验：键名必须和我们找的一致，否则说明下标错了 -> 绝不能拿去写 ✓
        PyDump::wline("校验: 键名与请求%s", strcmp(kname, attr) == 0 ? "一致 ✔" : "不一致 ✘ 不要用这个槽!");
        return true;
    }
    // ★ 相机 / VP 矩阵诊断（改视野功能用）——
    //   实现在本文件里，因为要用 libbase / MatrixOffset / Matrix / is_user_ptr
    //   这几个**本文件定义的全局**（放 PyDump.h 里编译不过 ✗）
    if (!strcmp(fn, "cam_dump")) {
        PyDump::reset_buf();
        PyDump::wline("===== 相机 / VP 矩阵 =====");
        PyDump::wline("libbase         = 0x%llX", (unsigned long long)libbase);
        PyDump::wline("MatrixOffset    = 0x%lX", MatrixOffset);
        PyDump::wline("Matrix (地址)   = 0x%llX", (unsigned long long)Matrix);
        const uint64_t root = libbase ? getPtr64(libbase + MatrixOffset) : 0;
        PyDump::wline("root            = 0x%llX", (unsigned long long)root);
        const uint64_t mobj = is_user_ptr(root) ? getPtr64(root + (uint64_t)SecOff::u64(SecOff::DG_94)) : 0;
        PyDump::wline("mobj(+ (uint64_t)SecOff::u64(SecOff::DG_95))    = 0x%llX", (unsigned long long)mobj);
        if (is_user_ptr(mobj)) {
            float m[16] = {0};
            if (vm_readv(mobj + (uint64_t)SecOff::u64(SecOff::DG_96), m, sizeof(m))) {
                PyDump::wline("");
                PyDump::wline("--- VP 矩阵（4x4）---");
                for (int r = 0; r < 4; r++)
                    PyDump::wline("  [%d] %12.4f %12.4f %12.4f %12.4f", r,
                                  m[r*4+0], m[r*4+1], m[r*4+2], m[r*4+3]);
                PyDump::wline("");
                PyDump::wline("★ 相机世界位置 = (%.3f, %.3f, %.3f)", m[12], m[13], m[14]);
                PyDump::wline("   写入目标坐标后这三个数若跟着变 = 改视野生效 ✓");
            } else {
                PyDump::wline("读矩阵失败");
            }
        } else {
            PyDump::wline("链路断了 —— 不在对局中？");
        }
        return true;
    }
    if (!strcmp(fn, "rescue_log"))     { PyDump::dump_rescue_log(PyDump::filter(), arg, arg2); return true; }
    if (!strcmp(fn, "memhwbp"))        { PyDump::dump_memhwbp(PyDump::filter(), (uint64_t)arg, arg2); return true; }
    if (!strcmp(fn, "memscan"))        { PyDump::dump_memscan(PyDump::filter(), (uint64_t)arg, arg2); return true; }
    if (!strcmp(fn, "find_mod"))       { PyDump::dump_find_in_modules(PyDump::filter()); return true; }
    if (!strcmp(fn, "expand"))         { PyDump::dump_expand((uint64_t)arg, arg2);   return true; }
    if (!strcmp(fn, "find_inst"))      { PyDump::dump_find_inst((uint64_t)arg, PyDump::filter()); return true; }
    return false;
}

const char* DumpOutput()   { return PyDump::buf(); }
int  DumpOutputLen()       { return PyDump::buf_len(); }
const char* DumpStatus()   { return PyDump::status(); }
bool SaveDumpTo(const char* path) {
    if (!path || !*path) return false;
    return PyDump::save_buf(path, "/data/local/tmp/dwdump.txt");
}

} // namespace McpData
