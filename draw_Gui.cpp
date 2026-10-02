#include "draw.h"
#include <thread>
#include <cstdint>
#include <stdio.h>
#include "My_font/wrg_font.h"
#include "kerneldriver-kma.hpp"
#include "DrawTool.h"
#include "Name.h"
#include "SoHookIntegration.h"
#include "PyProgress.h"
#include "PyGenius.h"
#include "PySelf.h"
#include "PyDump.h"
#include "TouchKeyboard.h"
#include "PyAttack.h"     // ← 新增
#include "AutoAim.h"
#include "AutoPallet.h"
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
#include <sched.h>
#include <pthread.h>
#include <mutex>
#include <algorithm>   // std::swap



// ==================== 日志（串口 + /sdcard/dwrg.txt） ====================
static FILE* g_log_fp = nullptr;
static bool  g_log_inited = false;

static void LOG(const char* fmt, ...) {
    if (!g_log_inited) {
        g_log_fp = fopen("/sdcard/dwrg.txt", "w");
        if (!g_log_fp) g_log_fp = fopen("/data/local/tmp/dwrg.txt", "w");
        g_log_inited = true;
        if (g_log_fp) {
            fprintf(g_log_fp, "=== dwrg touch log start ===\n");
            fflush(g_log_fp);
        }
    }
    va_list ap1, ap2;
    va_start(ap1, fmt); vprintf(fmt, ap1);    va_end(ap1);
    fflush(stdout);
    if (g_log_fp) {
        va_start(ap2, fmt); vfprintf(g_log_fp, fmt, ap2); va_end(ap2);
        fflush(g_log_fp);
    }
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
}DataStruct;
DataStruct data[3000];

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
bool niexi;
float cam_dist;
float niexi_dist,niexi_hold_dist;
/*定义*/
bool DrawIo[50];
float niexi_touch_x,niexi_touch_y;
bool M_Android_LoadFont(float SizePixels) {
    ImGuiIO &io = ImGui::GetIO();
    
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    config.OversampleH = 1;
    config.SizePixels = SizePixels;
    ::zh_font = io.Fonts->AddFontFromMemoryTTF((void *)WRG_Font, WRG_Font_size, SizePixels, &config, io.Fonts->GetGlyphRangesChineseFull());

    return zh_font != nullptr;
}
void init_My_drawdata() {
    M_Android_LoadFont(25.0f); //加载内存字体(含中文TTF+图标)
}


void screen_config() {
    ::displayInfo = android::ANativeWindowCreator::GetDisplayInfo();
}

void drawBegin() {
    if (!graphics) return;

    screen_config();

    // ---- 按当前方向算 overlay 应有的尺寸 ----
    int a = displayInfo.width, b = displayInfo.height;
    int short_side = a < b ? a : b;
    int long_side  = a < b ? b : a;
    bool is_portrait = ((displayInfo.orientation % 2) == 0);   // 0/2 竖屏
    int cur_w = is_portrait ? short_side : long_side;
    int cur_h = is_portrait ? long_side : short_side;

    // ---- 用我们自己记录的目标尺寸判断，不用 ANativeWindow 的实际尺寸 ----
    // ANativeWindow_getWidth 返回的是系统裁剪后的值，可能跟请求值不同，
    // 那样就检测不到"请求尺寸跟 wd 尺寸不一致"这个问题
    bool need_recreate = (::window == nullptr) ||
                         (native_window_screen_x != cur_w) ||
                         (native_window_screen_y != cur_h);

    if (need_recreate) {
        if (::window != nullptr) {
            graphics->Shutdown();
            android::ANativeWindowCreator::Destroy(::window);
            ::window = nullptr;
        }

        native_window_screen_x = cur_w;
        native_window_screen_y = cur_h;

        ::window = android::ANativeWindowCreator::Create(
                "37", cur_w, cur_h, permeate_record);

        // 用 window 实际 buffer 尺寸做 swapchain，避免系统裁剪导致 Init_Render 尺寸错
        int real_w = ANativeWindow_getWidth(::window);
        int real_h = ANativeWindow_getHeight(::window);
        if (real_w <= 0 || real_h <= 0) { real_w = cur_w; real_h = cur_h; }

        graphics->Init_Render(::window, real_w, real_h);
        ::init_My_drawdata();

        // 目标尺寸记录成实际尺寸，下帧才不会又重建
        native_window_screen_x = real_w;
        native_window_screen_y = real_h;

        if (g_window != nullptr) {
            float w = 1000.0f, h = 760.0f;
            if (w > real_w * 0.95f) w = real_w * 0.95f;
            if (h > real_h * 0.95f) h = real_h * 0.95f;
            g_window->Pos.x  = (real_w - w) * 0.5f;
            g_window->Pos.y  = (real_h - h) * 0.5f;
            g_window->Size.x = w;
            g_window->Size.y = h;
        }

        printf("[方向] 重建 overlay 请求%dx%d 实际%dx%d orient=%d disp=%dx%d\n",
               cur_w, cur_h, real_w, real_h,
               displayInfo.orientation, displayInfo.width, displayInfo.height);
        fflush(stdout);
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
uintptr_t Arrayaddr, Count, Matrix;
uintptr_t cur_obj,self_obj,self_camp,namezfcz,namezfc;
uintptr_t redqueen_obj,redqueen_mirror_obj,mirror_obj,mirror_preview_obj;
float mirror_line_x1, mirror_line_y1, mirror_line_x2, mirror_line_y2;   // 本帧镜面在水平面上的直线(两点)，mirror==true 时有效
int data_count,zfcz,zfc;
float matrix[16];
float angle;

static bool show_draw_Rect = true;//方框
static bool show_draw_Line = true;//射线
static bool show_draw_Camera = false;//相机
static bool show_draw_Door = true;//门(开门进度，走 Python 层单独绘制，不依赖 getscene)
static bool show_draw_Box = false;//盒子
static bool show_draw_Name = true;//名字
static bool show_draw_Distance = true;//距离
static bool show_draw_Cellar = true;//地窖
static bool show_draw_Chair = false;//椅子
static bool show_draw_Prop = false;//道具
static bool show_draw_Genius = true;//天赋/辅助特质(走 CPython 链路，见 PyGenius.h)
static bool show_draw_prophet = true;//预知监管者
static bool redqueenmod = false;//红夫人模式
static bool show_draw_umbrella = false;   // 宿伞模式：绘制传伞光标实时落点
static bool copycat_mode = false;//模仿者模式：角色名换成"N号 身份"，按阵营着色(见 PySelf.h 模仿者模式那节)
static bool show_draw_trait = true;   // 辅助特质 CD（从 show_draw_Genius 拆出来，独立开关）

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
    // 名字
    float name_size      = 25.0f;
    ImU32 name_color     = IM_COL32(255, 200,   0, 255);
    // 天赋
    float genius_size    = 25.0f;
    ImU32 genius_color   = IM_COL32(255, 255, 255, 255);
    // 技能CD
    float trait_size     = 25.0f;
    ImU32 trait_color    = IM_COL32(255, 255, 255, 255);
};
static EspStyle g_esp;
static bool show_draw_secret_mechine = true;//密码机(含破译进度/进度条/最后一台)
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
// ==================== 触摸功能状态 ====================
static bool  g_touch_adjust_enabled = false;   // "调整触摸区域"开关
static float g_touch_rect_x = 0.f;
static float g_touch_rect_y = 0.f;
static float g_touch_rect_w = 200.f;
static float g_touch_rect_h = 200.f;
static bool  g_touch_rect_inited   = false;    // 位置/大小首次初始化
static bool  g_touch_rect_dragging = false;
static float g_touch_drag_dx = 0.f;
static float g_touch_drag_dy = 0.f;

static constexpr int TOUCH_SLOT    = 6;
static constexpr int FLYWHEEL_SLOT = 7;

static bool  g_auto_skill_enabled  = false;    // "自动技能"开关
static int   g_auto_skill_tap_count     = 1;    // 点击次数 1~8
static int   g_auto_skill_tap_delay_ms  = 100;  // 每次点击之间的延迟 0~1000ms
static int   g_auto_skill_remaining_taps = 0;   // 剩余点击次数（内部状态）
static uint64_t g_auto_skill_next_tap_ms = 0;   // 下次点击时刻（内部状态）
static bool  g_auto_skill_pending_up = false;  // 已 touch_down，等待 touch_up
static uint64_t g_auto_skill_down_ms = 0;
static int   g_auto_skill_last_slot = TOUCH_SLOT;  // 本次 down 用的 slot
static bool  g_last_attacking      = false;    // 上升沿检测
static bool  g_touch_inited        = false;    // touch_init 是否已调用

// ==================== 自动飞轮状态 ====================
static bool  g_auto_flywheel_enabled  = false;
static bool  g_priority_flywheel      = false;  // 出刀时优先飞轮

static float g_flywheel_rect_x = 0.f;
static float g_flywheel_rect_y = 0.f;
static float g_flywheel_rect_w = 200.f;
static float g_flywheel_rect_h = 200.f;
static bool  g_flywheel_rect_inited   = false;
static bool  g_flywheel_rect_dragging = false;
static float g_flywheel_drag_dx = 0.f;
static float g_flywheel_drag_dy = 0.f;

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
static bool g_dark_theme = false;

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
bool first_frame_logged = false;
bool first_matrix_logged = false;
char extractedString[64];
long int MatrixOffset = 0,ArrayaddrOffset = 0;
typedef struct {
    unsigned long addr;
    unsigned long taddr;
} ModuleBssInfo;


ModuleBssInfo get_module_bss(int pid, const char *module_name) {
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
            sprintf(filename, "/proc/%d/cmdline", id);
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
char libso[256] = {"libclient.so"};

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
    int checkVal = getDword(obj.obj + 0x70);
    float checkFloat = getFloat(obj.obj + 0x1a0);
    return (checkVal != 0x1000000 || checkFloat != 450.0f);
}

bool ShouldSkipEntity(const DataStruct& obj) {
    // 这几类名字命中即直接跳过, 与幽灵判定无关(原本散落在渲染循环里, 现在收拢到一处)
    if (strstr(obj.class_name, "h55_joseph_camera") != NULL) return true;   // 约瑟夫相机
    if (strstr(obj.class_name, "redqueen_mirror") != NULL) return true;      // 红夫人镜子
    if (strstr(obj.class_name, "burke_console") != NULL) return true;        // 疯眼场景
    if (strstr(obj.class_name, "chr\\guajian") != NULL) return true;
    if (strstr(obj.class_name, "girl_e_sj_zuoyi") != NULL) return true;
    if (strstr(obj.class_name, "h55_survivor_w_shangren_tiaoban") != NULL) return true; // 商人跳板

    // 废弃模型(形态切换后留在数组里的旧形态)。这条取代了原来那份
    // "红蝶/无常/歌剧/破轮/木偶/冒险家" 的类名黑名单 —— 那份是按角色名猜的,
    // 漏一个角色就漏一个, 而且只在 inform_ghost 打开时才生效。
    // 现在直接问引擎: unit.another_model + 0x20 就是废弃形态的场景对象指针,
    // 精确、跟视角无关、不用维护名单。详见 PySelf.h 文件头。
    if (PySelf::is_stale_model(obj.obj)) return true;

    bool is_ghost_obj = IsGhostEntity(obj);

    // 注意 +0x70 就是 NeoX 模型对象的 visible, 是**渲染剔除的结果**:
    // 废弃模型恒不可见, 但远处的真实对象同样不可见。所以它只能当"要不要按幽灵显示"的开关,
    // 绝不能当存在性/真假判据 —— 那会重演"求生者只看得到身边的密码机"。
    if (is_ghost_obj && !inform_ghost) return true;
    return false;
}

void read_thread(long int PD1,long int PD2,long int PD3)
{
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
                if (!is_official && strstr(line, libso)) libbase = a;
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
        result = get_module_bss(pid, libso);
    printf("[基址] libbase=0x%llX BSS=0x%llX-0x%llX\n",
           (unsigned long long)libbase, (unsigned long long)result.addr, (unsigned long long)result.taddr);
    if (libbase < 0x5000000000){
        printf("[错误] libbase无效, 无法继续\n");
        sleep(9999);
    }
    c = (result.taddr-result.addr)/4096;
    long buff[512];
    while (MatrixOffset==0||ArrayaddrOffset==0)
    {
    	for (int i = 0; i < c; i++){
        	vm_readv(result.addr+(i*4096), &buff, 0x1000);
        	for (int ii=0;ii<512;ii+=1){

        	    if (MatrixOffset == 0 && *(long long*)(&buff[ii]) == 0x656A624F72655028LL){
        	        uint64_t sig_addr = result.addr + i*4096 + ii*8;
        	        // 2026-09-17 热更后 +0x430 恒为0。该处是一组步长0x88的指针，三个元素都能走到
        	        // 同一个矩阵对象，这次更新只是让它整体位移了0x10。写死任何一个下次还会废，
        	        // 改成在窗口内找"整条链都走得通"的槽位(p1有效 且 p1+0xa58 也有效)，自愈。
        	        // 注意上界：只判 >0x5000000000 会让浮点垃圾(如0x400674954293DF)也蒙混过关，
        	        // 本机用户态指针都在 0x77xx/0x79xx 段，统一卡 <0x8000000000。
        	        for (int off = 0x300; off <= 0x500; off += 8){
        	            uint64_t cand_slot = sig_addr + off;
        	            uint64_t p1 = getPtr64(cand_slot);            // getPtr64 已做 0xB4 掩码
        	            if (p1 <= 0x5000000000 || p1 >= 0x8000000000) continue;
        	            uint64_t p2 = getPtr64(p1 + 0xa58);
        	            if (p2 <= 0x5000000000 || p2 >= 0x8000000000) continue;
        	            MatrixOffset = cand_slot - libbase;
        	            printf("[矩阵命中] 签名=0x%llX 槽位=+0x%X 根=0x%llX MatrixOffset=0x%lx\n",
        	                   (unsigned long long)sig_addr, off, (unsigned long long)cand_slot, MatrixOffset);
        	            break;
        	        }
        	    }

        	    if (ArrayaddrOffset == 0 && buff[ii] == 16384){
                    if (getDword(result.addr + 4096*i + 8*ii - 0x8) == 257 &&
                        getFloat(result.addr + 4096*i + 8*ii - 16) == 1.0f){
                        ArrayaddrOffset = result.addr - libbase + i*4096 + ii*8 + 56;
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
    read_state = 2;

    // CPython 根(sys.modules + 类型对象)：读 so 的 ELF 段表定位 .PyRuntime，三个 Py 模块共用
    PyRoot::start(pid, libbase);
    // 密码机破译进度：走 CPython 对象图，自带后台线程(400ms 一轮)，跟这里的 3 秒大循环解耦
    PyProgress::start(libbase);
    // 天赋与辅助特质：同一条 CPython 链路，独立线程 500ms 一轮(一局内基本不变，不用刷那么勤)
    PyGenius::start(libbase);
    // 自身锚点(g_cam_ctrl.unit)与废弃模型黑名单：100ms 一轮，切换操控对象时要立刻跟上
PySelf::start(libbase);
// 监管出刀识别：200ms 一轮，读 state_machine._state[0] 与 idle_state 对比
PyAttack::start();
// 自动盖板：独立线程 10ms 一轮，板子列表由下面的读取循环每轮发布过去
AutoPallet::start();
AutoAim::start();

    Arrayaddr = getPtr64(libbase + ArrayaddrOffset);
    uint64_t ArrayEnd = getPtr64(libbase + ArrayaddrOffset + 8);
    Count = (ArrayEnd - Arrayaddr) / 8;
    if (Count <= 0 || Count > 10000) Count = 1000;   // 兜底值：实测对局中 Count 只有 274~307
    printf("[数组] Arrayaddr=0x%llX End=0x%llX Count=%d\n", (unsigned long long)Arrayaddr, (unsigned long long)ArrayEnd, (int)Count);

    while (true)
    {        
    	uint64_t curArray = getPtr64(libbase + ArrayaddrOffset);
        uint64_t curArrayEnd = getPtr64(libbase + ArrayaddrOffset + 8);
        uint64_t curCount = curArrayEnd > curArray ? (curArrayEnd - curArray) / 8 : 0;
	    // ★ 数组太小也视为无效：
// 进对局/进大厅的过渡期，游戏会动态 append 场景对象，
// 数组长度会从 0 慢慢涨到 200+。中间会短暂经过 30/60/100 这种半成品状态，
// 若不挡掉，就会用残缺的 data[] 画一整轮，表现为"只看到两三个人、没有密码机"。
// 实测正常对局 Count 都是 200~1000，准备阶段是 0。
// 100 这个阈值：既挡住半成品，又不会误伤任何一个真正的小对局。
// ★ 稳定性判定：
//   原来用固定阈值 100 挡"进对局瞬间的半成品数组"，但准备界面本来
//   就 Count 几十，会被误挡。改成看"是否稳定"：
//     - 准备界面：Count 长期不变(30、61 都是稳定值) → 允许发布
//     - 进对局过程：Count 每轮都在涨(30→60→100→207) → 一直"不稳定" → 等
//   阈值 < 100 只是配合稳定性判据，防止极端情况(比如 Count=0 的空数组)。
static uint64_t s_prev_count = 0;
static int      s_stable_rounds = 0;
static const int kStableRounds = 2;   // 连续 2 轮 Count 不变 = 稳定

if (curCount == s_prev_count) {
    s_stable_rounds++;
} else {
    s_stable_rounds = 0;
    s_prev_count = curCount;
}

// 基础合法性
if (curArray < 0x5000000000 || curCount == 0 || curCount > 10000) {
    data_count = 0;
    read_state = 1;
    sleep(1);
    continue;
}

// 小 Count 且还在变化 → 认为在加载，跳过本轮
// (每轮 sleep 2 秒，kStableRounds=2 → 至少稳定 4 秒才发布)
if (curCount < 100 && s_stable_rounds < kStableRounds) {
    data_count = 0;
    read_state = 1;
    sleep(1);
    continue;
}
        Arrayaddr = curArray;
        Count = curCount;
        read_state = 2;
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

for (int ii = 0; ii < Count && entity_count < 3000; ii++){
            cur_obj = getPtr64(curArray+0x8 * ii);	// 遍历数量次数            
                
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
    	    bool head_ok = vm_readv(cur_obj + 0xf8, &chain_head, 8);
    	    chain_head &= 0x00FFFFFFFFFFFFFFULL;
    	    auto nc = g_name_cache.find(cur_obj);
    	    if (nc != g_name_cache.end() && (!head_ok || nc->second.first == chain_head)) {
    	        filter_class_name = nc->second.second;
    	    } else {
    	        uint64_t class_name_obj = getPtr64(getPtr64(getPtr64(getPtr64(chain_head)+0x8)+0x20)+0x20)+0x0;
    	        int len = getDword(class_name_obj + 0x10);
    	        if (len >= 256 || len == 0 || len < 0)
    	            continue;
    	        filter_class_name.resize(len);
    	        vm_readv(getPtr64(class_name_obj + 0x8), &filter_class_name[0], len);
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

int is_dup=0;
			float pd1 = getFloat(cur_obj + 0x1a0);   // 原来还读一次 +0x298(pd2)，没人用，已删
			for (int i = 0; i < entity_count; i++){
        		if(cur_obj == data[i].obj){
        		    is_dup=1;
        		}        		    
        	}
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
    		
			if (strstr(filter_class_name.c_str(), "player") != NULL||strstr(filter_class_name.c_str(), "boss") != NULL || pd1 == 450 || strstr(filter_class_name.c_str(), "scene") != NULL || strstr(filter_class_name.c_str(), "prop") != NULL || strstr(filter_class_name.c_str(), "mirror") != NULL || Debugging )
			{
    			data[entity_count].obj = cur_obj;
    			// 阵营/str 必须先清零：下面那串 if-else 只有五个分支，而入口条件里的
    			// `pd1 == 450` 和 `Debugging` 能让对象进来却一个分支都不命中。
    			// data[] 是全局数组、原地复用，不清就会**沿用上一帧同下标那个对象的
    			// 阵营和名字** —— 表现是装饰物被当成角色画出来、还顶着别人的名字，
    			// 并且 内核人物数量 虚高。正常游玩时类名基本都能命中 player/boss，
    			// 所以这个洞主要在开「绘制调试」时发作。
    			data[entity_count].camp = 0;
    			data[entity_count].str[0] = '\0';
    			if (strstr(filter_class_name.c_str(), "boss") != NULL){
    			//data[指针数量].str=getboss(过滤类名.c_str());
    			strcpy(data[entity_count].str, getboss(filter_class_name.c_str()));
    			data[entity_count].camp=1;
    			}
    			else if (strstr(filter_class_name.c_str(), "player") != NULL||strstr(class_name.c_str(), "npc_deluosi_dress_ghost") != NULL||strstr(class_name.c_str(), "h55_pendant_huojian") != NULL){
    			//data[指针数量].str=getplayer(过滤类名.c_str());
    			strcpy(data[entity_count].str, getplayer(filter_class_name.c_str()));
    			data[entity_count].camp=2;
    			}
    			else if (strstr(filter_class_name.c_str(), "scene") != NULL){
    			const char* scene_result = getscene(filter_class_name.c_str());
    			if (scene_result == NULL) continue;
    			strcpy(data[entity_count].str, scene_result);
    			data[entity_count].camp=3;
    			}
    			else if (strstr(filter_class_name.c_str(), "prop") != NULL){
    			const char* prop_result = getprop(filter_class_name.c_str());
    			if (prop_result == NULL) continue;
    			strcpy(data[entity_count].str, prop_result);
    			data[entity_count].camp=4;
    			}

    			else if (strstr(filter_class_name.c_str(), "redqueen") != NULL&&strstr(filter_class_name.c_str(), "mirror") != NULL&&strstr(filter_class_name.c_str(), "model") != NULL){
    			// fx/model/redqueen_mirror_model_obj_001.gim：准备放镜时的预览镜子(PlaceIndicator.rtc_model)，
    			// 常驻复用同一个对象，只在准备阶段可见。用法见 Draw_Main 里的镜线计算
    			data[entity_count].camp=5;
    			mirror_preview_local = cur_obj;
    			}
    			//sprintf(data[指针数量].类名, "%s", 过滤类名.c_str());
    			strcpy(data[entity_count].class_name, filter_class_name.c_str());
    			data[entity_count].objcoor=getPtr64(cur_obj+0x28);
    			if (!first_frame_logged){
        			printf("[实体] %s 地址=0x%llX 坐标地址=0x%llX 坐标=(%.1f,%.1f,%.1f) 类名=%s\n",
        			       data[entity_count].str, (unsigned long long)cur_obj,
        			       (unsigned long long)data[entity_count].objcoor,
        			       getFloat(data[entity_count].objcoor + 0xa0),
        			       getFloat(data[entity_count].objcoor + 0xa4),
        			       getFloat(data[entity_count].objcoor + 0xa8),
        			       data[entity_count].class_name);
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
			    uintptr_t coorPtr = getPtr64(cur_obj + 0x28);
			    if (strstr(filter_class_name.c_str(), "boss") != NULL && strstr(filter_class_name.c_str(), "redqueen") != NULL && strstr(filter_class_name.c_str(), "mirror") == NULL
			        && getFloat(coorPtr + 0xa0) != 0 && getFloat(coorPtr + 0xa8) != 0) {
			        uint8_t kind = 0;
			        vm_readv(cur_obj + 0x6D, &kind, 1);
			        if (kind & 0x40)      rq_local = cur_obj;       // 本体
			        else if (kind & 0x80) rq_mirror_local = cur_obj;   // 镜中红夫人
			    }
    			if (strstr(filter_class_name.c_str(), "boss") != NULL && strstr(filter_class_name.c_str(), "mirror") != NULL
    			    && getFloat(coorPtr + 0xa0) != 0 && getFloat(coorPtr + 0xa8) != 0)
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
        redqueen_obj        = rq_local;
        redqueen_mirror_obj = rq_mirror_local;
        mirror_obj          = mirror_local;
        mirror_preview_obj  = mirror_preview_local;
        if (prophet_local[0]) memcpy(prophet_text, prophet_local, sizeof(prophet_text));
        AutoPallet::publish_boards(boards_local, boards_n);
        data_count = entity_count;
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
static inline unsigned entity_active_byte(uintptr_t obj) { return (getDword(obj + 0x70) >> 24) & 0xFF; }
static inline unsigned entity_kind_byte(uintptr_t obj) { return (getDword(obj + 0x6C) >> 8)  & 0xFF; }

// **千万不要拿活跃位当"本局是否存在"用**：它是"当前对本机客户端可见"的意思，跟视角走。
// 实测同一张图，监管视角下 12 台密码机活跃位全是 1，换成求生者视角只剩 1 台是 1。
// 早先用它做密码机判据，导致求生者只能看到身边那一台，绕了一大圈才发现。
//
// 真正视角无关的存在性标记是 +0x240：
//   实测 约瑟夫相机 7/7 = 0，破轮台 4/4 = 0（这两类都是每局默认加载、本局并不存在的装饰）
//        密码机 真的 7 个 = 2 / 假的 5 个 = 0，箱子、求生者、监管 全部 = 2
static inline bool entity_exists(uintptr_t obj)
{
    return getDword(obj + 0x240) == 2;
}
// 破译进度配色：<20 绿、20~60 黄、>60 红
static inline ImColor progress_color(float v)
{
    if (v < 20.0f)  return color_green;
    if (v <= 60.0f) return color_yellow;
    return color_red;
}

// 按 g_esp.rect_style 画方框（全框 / 四角括号）
static void DrawEspRect(ImDrawList* dl, float x1, float y1, float x2, float y2,
                        float W, float H, ImU32 col) {
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

static inline bool is_real_generator(uintptr_t obj)
{
    // 活跃位 + 一个密码机专用的辅助值。单用任何一个都不够：
    //   只判 +0x1a0 -> 未激活的候选机器该值恰好也可能是 500
    //   只判活跃位  -> 会混进一个位于原点(0,0,0)的占位对象
    return getFloat(obj + 0x1a0) == 500.0f && getDword(obj + 0x240) == 2;
}

// ---- VP 矩阵校验(指针范围 + 16 个值全部有限) ----
// 本机用户态指针都在 0x77xx/0x79xx 段，与启动扫描用的范围一致
static inline bool is_user_ptr(uint64_t p) { return p > 0x5000000000 && p < 0x8000000000; }
static inline bool matrix_finite(const float *m)
{
    for (int i = 0; i < 16; i++) if (!std::isfinite(m[i])) return false;
    return true;
}
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

// 前向声明（定义在 Draw_Main 后面）
// 前向声明（定义在 Draw_Main 后面）
static void TouchRect_Draw(ImDrawList* Draw);
static std::string read_scene_class_name(uint64_t obj);
static uint64_t TouchNowMs();   // ← 新增;

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
    uint64_t coor = getPtr64(scene_obj + 0x28);
    if (!coor) return false;
    float raw[3];
    if (!vm_readv(coor + 0xa0, raw, 12)) return false;
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
        bool f3ok = vm_readv(vp + 0x10, f3, 12);
        // 试着当 PyLong 读
        int64_t asint = 0;
        uint32_t digit = 0;
        bool int_ok = vm_readv(vp + 0x10, &asint, 8) && vm_readv(vp + 0x18, &digit, 4);

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

void Draw_Main(ImDrawList *Draw){
    PySelf::set_copycat_book(copycat_mode);        // 身份簿只在开关打开时记，见 PySelf.h
    if (libbase == 0 || read_state == 0) return;  // 数据未就绪，跳过本帧绘制
    int char_count = 0;
    
    

    // 触摸红框（如果开启了"调整触摸区域"）
    TouchRect_Draw(Draw);

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
        uint64_t root = getPtr64(libbase + MatrixOffset);
        uint64_t mobj = is_user_ptr(root) ? getPtr64(root + 0xa58) : 0;
        if (is_user_ptr(mobj)) {
            float tmp[16];
            if (vm_readv(mobj + 0x2c0, tmp, sizeof(tmp)) && matrix_finite(tmp)) {
                Matrix = mobj + 0x2c0;
                memcpy(matrix, tmp, sizeof(matrix));
                matrix_fresh = true;
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
        uintptr_t redqueen_coor = getPtr64(redqueen_obj + 0x28);
        if (redqueen_coor != 0) {
            redqueen_x = getFloat(redqueen_coor + 0xa0);
            redqueen_z = getFloat(redqueen_coor + 0xa4);
            redqueen_y = getFloat(redqueen_coor + 0xa8);
            body_valid = redqueen_z >= -300 && redqueen_x != 0 && redqueen_y != 0;
        }
    }
    if (redqueen_mirror_obj != 0) {
        uintptr_t mirror_coor = getPtr64(redqueen_mirror_obj + 0x28);
        if (mirror_coor != 0) {
            redqueen_mirror_x = getFloat(mirror_coor + 0xa0);
            redqueen_mirror_z = getFloat(mirror_coor + 0xa4);
            redqueen_mirror_y = getFloat(mirror_coor + 0xa8);
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
        vm_readv(mirror_preview_obj + 0x73, &visible, 1);
        uintptr_t cp = getPtr64(mirror_preview_obj + 0x28);
        if (visible == 1 && cp != 0) {
            float px0 = getFloat(cp + 0xa0), pz0 = getFloat(cp + 0xa4), py0 = getFloat(cp + 0xa8);
            float nx = getFloat(cp + 0x90), ny = getFloat(cp + 0x98);
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
    if (show_draw_prophet && !PySelf::copycat_active()){
        auto textSize = ImGui::CalcTextSize(prophet_text, 0, 25);
        Draw->AddText({px-(textSize.x/2),130}, color_red, prophet_text);
    }

    // 准备阶段：预知那行下面按座位号列出四个求生者的大天赋，**只在我是监管时显示**(用户要求)
    //   数据来自 g_avatar.final_genius_dict(实时；60 秒差分实测只有它跟着对面换天赋变)，座位号来自 uid2pos，见 PyGenius.h
    //   不再显示监管辅助特质(用户 2026-09-24 决定不要)；也不再看花名册 —— 大厅里那是上一局的旧数据
    if (show_draw_prophet && PySelf::in_lobby() && PyGenius::prep_as_hunter() && PyGenius::civ_size() > 0) {
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
            // GeniusLine 是 前/飞轮/后 三段(飞轮单独一段，局内要单独上色)，三段都要拼，漏了中间那段飞轮就没了
            int w = seat ? snprintf(line + len, sizeof(line) - len, "%s%d号[%s%s%s]", len ? "  " : "", seat, seg.pre, seg.flywheel, seg.post)
                         : snprintf(line + len, sizeof(line) - len, "%s?号[%s%s%s]", len ? "  " : "", seg.pre, seg.flywheel, seg.post);
            if (w < 0) break;
            len += w;
            if (len >= (int)sizeof(line)) { len = (int)sizeof(line) - 1; break; }   // 截断了，别让下一轮越界
        }
        if (len > 0) {
            auto ts2 = ImGui::CalcTextSize(line, 0, 25);
            Draw->AddText({px - (ts2.x / 2), 165}, ImColor(255, 200, 0, 255), line);
        }
    }

    // 已破译 4 台后，剩下那台在修的就是最后一台 —— 单独用进度条标出来
    {
        float last_progress = 0.f;
        if (show_draw_secret_mechine && PyProgress::last_generator(last_progress)){
            char label[64];
            snprintf(label, sizeof(label), "最后一台 %.1f%%", last_progress);
            auto ts = ImGui::CalcTextSize(label, 0, 25);
            const float bar_w = 160.0f, bar_h = 14.0f, gap = 8.0f;
            float x0 = px - (bar_w + gap + ts.x) / 2.0f;
            float y0 = 158.0f;                                  // 预知监管那行(y=130)的下一行
            ImColor c = progress_color(last_progress);
            float fill_w = bar_w * (last_progress / 100.0f);
            if (fill_w > 0.0f)
                Draw->AddRectFilled({x0, y0}, {x0 + fill_w, y0 + bar_h}, c);
            Draw->AddRect({x0, y0}, {x0 + bar_w, y0 + bar_h}, ImColor(255,255,255,255));
            Draw->AddText({x0 + bar_w + gap, y0 + bar_h/2.0f - ts.y/2.0f}, c, label);
        }
    }

    // 以上是屏幕坐标的 HUD(预知/花名册/最后一台)，不依赖矩阵；以下全部要投影
    if (!matrix_usable) return;

    // ---- 大门开门进度 ----
    // 大门**不走下面那个实体循环**：getscene() 只认 prop_76/sender，大门那条分支收不到东西
    // (原版认 6 个类名，Name.h 重构时缩成 2 个，门/箱/椅三条分支就成了孤儿)。
    // 所以这里直接拿 Python 侧的 model+0x20 场景对象自己投影。
    // 也不能靠场景侧判据补救：实测两扇门的类名都不一样(prop_30 / wooddoor01a)，
    // 而且 +0x240 两扇都是 0 —— "最可靠的存在性判据"在大门上失效。
    if (show_draw_Door){
        for (int i = 0; i < PyProgress::door_count(); i++){
            PyProgress::DoorEntry door;
            if (!PyProgress::get_door(i, door)) continue;
            if (PyProgress::door_opened(door)) continue;          // 已经开了的不用再画
            if (!door.can_open) continue;                         // 还没通电(不可开)的不画

            uintptr_t coor_ptr = getPtr64((uintptr_t)door.scene_obj + 0x28);
            if (coor_ptr == 0) continue;
            float mx = getFloat(coor_ptr + 0xa0);
            float mz = getFloat(coor_ptr + 0xa4);          // +0xa4 是高度
            float my = getFloat(coor_ptr + 0xa8);
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
    uint64_t now = TouchNowMs();
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
        if (PyCore::is_obj(po))   vm_readv(po   + 0x10, pos, 12);
        if (PyCore::is_obj(dobj)) vm_readv(dobj + 0x10, dir, 12);

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

// ★ 每帧重新取一次"自己"的两种来源（都是场景对象指针），
// 用它们双重判定，保证不管玩监管还是求生都不会画自己。
//   anchor: 当前操控单位(g_cam_ctrl.unit)对应的场景对象
//   hunter_body: 当前监管单位的 unit.model 对应场景对象
// 两者返回的都是 scene_obj（可直接跟 data[i].obj 比较）
uint64_t self_anchor  = 0;
PySelf::anchor(self_anchor);

for (int i = 0; i < data_count; i++){
    
        if (strstr(data[i].class_name, "buzz") != NULL)
            continue;//跳过不知所谓的东西
        if (strstr(data[i].class_name, "nvyao.gim") != NULL)
            continue;//跳过女妖蜡烛
        // 三个坐标是连续的 12 字节，一次读完。原来是三次 getFloat = 三次 ioctl，
        // 而每次驱动读约 3µs，30 个实体 60fps 下这一项就占渲染线程约 1% CPU。
        // 读失败时保持 0，和 getFloat 失败返回 0 的行为一致。
        float xyz[3] = {0, 0, 0};
        vm_readv(data[i].objcoor + 0xa0, xyz, 12);
        D.X = xyz[0];
        D.Z = xyz[1];
        D.Y = xyz[2];
        
        

        // ★ 自身跳过：缓存 self_obj + 每帧取到的 anchor
//   只用 anchor(PySelf::anchor)，它表示"当前操控单位"，玩什么角色都跟着走
//   绝对不要用 hunter_body —— 它表示"本局的监管"，玩求生时那是敌人
const bool is_self =
        (self_obj != 0      && data[i].obj == self_obj) ||
        (self_anchor != 0   && data[i].obj == self_anchor);
if (is_self) {
    if (!ShouldSkipEntity(data[i]) && !(D.X==0 && D.Y==0) && D.Z>-300) {
        Z.X = D.X; Z.Z = D.Z; Z.Y = D.Y;
    }
    // ★ 玩监管时，所有监管阵营的对象都不画
//   (camp==1 包含本体 + LOD + 备用形态 + 分身，都是自己)
//   玩求生时不动，因为这时 camp==1 是"对面监管"，要正常显示。
if (has_auth_self && self_camp == 1 && data[i].camp == 1) {
    continue;
}
continue; // 不管有效无效, 自身都不需要再走下面的常规实体流程
}


        // 只画角色本体：CPython 侧 units_by_type[1]/[2]/[236]/[1065] 的 unit.model 对应的场景对象(见 PySelf.h 本体集合)。
        // 模仿者局是 [1088] 里的活人(死者和 [1089] 鬼魂不收，所以不画)。
        // 挡掉同名分身副本(_fragrance_image)、另一形态、挂件、时装、魔术师/幻灯师分身；机械玩偶保留。
        // 只管按 player/boss 类名归进 1/2 的对象 —— 类名分类那里另有两个特例(deluosi 鬼魂、火箭挂件)
        // 被故意归成求生者，它们不是任何单位的 model，不能被这层挡掉。
        // 本体集合不可用(准备阶段/大厅/读取失败)时不过滤，照旧按类名画。
        // 大厅/准备阶段(units_by_type 读全了且没有键 1/2)：没有任何玩家单位，场景里的人物对象全是
        // 无宿主的时装挂件(头饰/袖子)，阵营 1/2 整类不画。预知监管在读取循环里算，不受影响
        if ((data[i].camp == 1 || data[i].camp == 2) && PySelf::in_lobby())
            continue;
        // 模仿者模式开着且身份簿有内容：按簿子过滤(只画簿子里的活人)。开会阶段 Python 页被换出，
        // 本体集合读不全甚至整个不可用，那时退回按类名画会把尸体、鬼魂、石像全画出来
        const bool cc_filter = copycat_mode && PySelf::copycat_book_ready();
        if (cc_filter && (data[i].camp == 1 || data[i].camp == 2)
            && (strstr(data[i].class_name, "player") != NULL || strstr(data[i].class_name, "boss") != NULL)) {
            int ident_unused = 0, seat_unused = 0;
            if (!PySelf::copycat_identity(data[i].obj, ident_unused, seat_unused)) continue;
        }
        else if ((data[i].camp == 1 || data[i].camp == 2) && PySelf::body_set_ready()
            && (strstr(data[i].class_name, "player") != NULL || strstr(data[i].class_name, "boss") != NULL)
            && !PySelf::is_body(data[i].obj))
            continue;

        if (D.X==0 || D.Y==0){
		    continue;//跳过xy0
		}
		if (D.Z<=-300){
		    continue;//跳过地下
		}
		if (data[i].camp == 1 || data[i].camp == 2) char_count++;
		// 本体是监管时，游戏自己就会显示监管者个体，这里不再重复画任何监管者。
		// 只认 CPython 锚点给的阵营：兜底的相机深度启发式可能把求生者误判成监管，
		// 那样会让求生者视角下的监管整个消失，代价远大于多画一个。
		if (has_auth_self && self_camp == 1 && data[i].camp == 1) continue;
		// 这里原本还读一次 +0x70(jxpd) 并算 cam_dist / niexi_dist，三者都没有任何地方使用，已删。
		// jxpd 那次是每实体每帧一次驱动读，删掉直接省 CPU；另两个只是浮点运算。
		camera = matrix[3] * D.X + matrix[7] * D.Z + matrix[11] * D.Y + matrix[15];
        dist = sqrt(pow(D.X - Z.X, 2) + pow(D.Y - Z.Y, 2) + pow(D.Z - Z.Z, 2)) / dist_scale;
		r_x = px + (matrix[0] * D.X + matrix[4] * D.Z + matrix[8] * D.Y + matrix[12]) / camera * px;
        r_y = py - (matrix[1] * D.X + matrix[5] * (D.Z+ 8.5) + matrix[9] * (D.Y) + matrix[13]) / camera * py;
        r_w = py - (matrix[1] * D.X + matrix[5] * (D.Z+ 28.5) + matrix[9] * (D.Y) + matrix[13]) / camera * py;
												
		W = (r_y - r_w) / 2;	// 宽度
		H = r_y - r_w;		// 高度
		X1 = r_x - (r_y - r_w) / 4;	// X1
		Y1 = r_y - H / 2;	// Y1
		X2 = X1 + W;		// X2
		Y2 = Y1 + H;		// Y2
		if (dist>=300){
            continue;
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
                if (getDword(data[i].obj + 0xa8)==256){
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
    		            if (getDword(data[i].obj + 0x148)==0){
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
    		            if (getDword(data[i].obj + 0xa8)==256){
    		                continue;//跳过使用过的椅子
    		            }
                        s += "[狂欢之椅]";
                    }
                    auto textSize = ImGui::CalcTextSize(s.c_str(), 0, 25);
                    Draw->AddText({r_x-(textSize.x/2),r_y}, color_red, s.c_str());
    		    }
    		
    		    else if (strstr(data[i].class_name, "dm65_scene_prop_76") != NULL){
    			    std::string s;
    		        if (show_draw_Cellar){                                                  
                            s += "[地窖] ";
                            s += std::to_string((int) dist);    
                            s += " 米 ";
                    }
                    auto textSize = ImGui::CalcTextSize(s.c_str(), 0, 25);
                    Draw->AddText({r_x-(textSize.x/2),r_y}, color_purple, s.c_str());
    		    }

    	    else if (strstr(data[i].class_name, "sender") != NULL){
    	        // 判据见上面 is_real_generator() 的注释。若发现密码机被破译完成后从叠加层消失，改那里。
    	        if (show_draw_secret_mechine && is_real_generator(data[i].obj)){
    	            // 进度来自 Python 侧的 GeneratorUnit，靠 model 指针身份配对(见 PyProgress.h)。
    	            // 配不上时(model 为空/链路失效)退化成原来的 "[密码机] X.X 米"。
    	            float progress = 0.f;
    	            bool has_progress = PyProgress::lookup(data[i].obj, D.X, D.Y, progress);
    	            if (!(has_progress && PyProgress::is_decoded(progress))){      // 破译完的机器本身和进度都不画
    	                std::ostringstream oss;
    	                oss << std::fixed << std::setprecision(1) << dist;
    	                std::string dist_text = " " + oss.str() + " 米";
    	                char head[24];
    	                if (has_progress) snprintf(head, sizeof(head), "[%.1f%%]", progress);
    	                else        snprintf(head, sizeof(head), "[密码机]");

    	                // 两段分开上色：头用进度色，距离沿用原来 61~63 米变绿的规则
    	                auto hs = ImGui::CalcTextSize(head, 0, 25);
    	                auto ds = ImGui::CalcTextSize(dist_text.c_str(), 0, 25);
    	                float x0 = r_x - (hs.x + ds.x) / 2.0f;
    	                Draw->AddText({x0, r_y}, has_progress ? progress_color(progress) : ImColor(255,255,255,255), head);
    	                Draw->AddText({x0 + hs.x, r_y},
    	                    ((int)dist >= 61 && (int)dist <= 63) ? color_green : ImColor(255, 255, 255, 255),
    	                    dist_text.c_str());

    	                // 进度条画在文字上方；进度为 0 时没有意义，不画
    	                if (has_progress && progress > 0.05f){
    	                    const float bar_w = 120.0f, bar_h = 10.0f;
    	                    float bx = r_x - bar_w / 2.0f, by = r_y - bar_h - 3.0f;
    	                    Draw->AddRectFilled({bx, by}, {bx + bar_w * (progress / 100.0f), by + bar_h}, progress_color(progress));
    	                    Draw->AddRect({bx, by}, {bx + bar_w, by + bar_h}, ImColor(255,255,255,255));
    	                }
    	            }
    	        }
    	    }
    		}
	
		    if (show_draw_Prop&&data[i].camp==4){
                // 自己身上的道具按距离剔除。必须只算**水平**距离:
                // 道具是挂在角色骨骼上的(手/胸口)，挂点比角色原点(脚底)高一截，
                // 实测 h55_pendant_glim(手电筒) 高度差固定 +0.73 米、水平只差 0.36 米。
                // 三维距离因此恒有 0.7+ 米的底噪，亚米阈值永远不成立 —— 挂件类道具靠
                // 三维距离**不可能**滤掉，跟站位无关，是结构性偏移。
                // 所以这里必须单独算水平距离，不能拿全局 dist(三维)比。
                float prop_hdist = sqrt(pow(D.X - Z.X, 2) + pow(D.Y - Z.Y, 2)) / dist_scale;
                // 阈值 1.5 米：手电筒实测水平只差 0.36 米，但 1.0 米实战仍有漏网，
                // 说明别的挂件(火箭/橄榄球等)挂点更靠外。代价是贴身队友手里的道具
                // 也会被隐藏，1.5 米内的地面道具本来也在视野里，可以接受。
                if (prop_hdist >= 1.5f) {
                    const char* propName = getprop(data[i].class_name);
                    if (propName) {
                        std::string s = propName;
                        s += std::to_string((int) dist);
                        s += " 米";
                        auto textSize = ImGui::CalcTextSize(s.c_str(), 0, 25);
                        Draw->AddText({r_x-(textSize.x/2),r_y}, ImColor(255,200,0,255), s.c_str());
                    }
                }
            }

            // 这里原本读一次 +0xaa(zy)，唯一用到它的是下面那段已停用的相机深度启发式，已删。
            if (!ShouldSkipEntity(data[i])){
                if (show_draw_Role&&strstr(data[i].class_name, "chr") != NULL){
                    std::string test;
                    test += " [";
                    test += std::to_string((int) dist);    
                    test += " 米]  0x";
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

                // 离自己 1 米以内的角色不画方框和附带数据(名字/距离/天赋/特质/射线)：
                // 贴身的基本是自己身上的其它模型(备用形态、残留模型)，画出来只会挡视线。
                // 角色原点都在脚底，三维距离即可(道具那边是挂点偏高，才只能算水平距离)。
                // 没有自身锚点时 Z 不可信，不做这层过滤。
                //bool too_close = (self_obj != 0) && dist < 1.0f;

                //if (!too_close && (data[i].camp==1||data[i].camp==2)){
                // [已删除] 原来 1 米内不绘制的过滤，用户要求删除
if (data[i].camp==1||data[i].camp==2){
                    // 模仿者模式：名字换成"N号 身份"，文字和方框按阵营着色(侦探团蓝/模仿者红/中立黄)。
                    // 身份进局约 2 秒后才赋值，之前只有号码、画白色；不是模仿者玩家本体的对象照旧画角色名
                    int cc_identity = 0, cc_seat = 0;
                    bool cc = copycat_mode && PySelf::copycat_identity(data[i].obj, cc_identity, cc_seat);
                    ImColor name_color = ImColor(255,200,0,255);
                    if (cc){
                        char cc_text[48];
                        const char *nm = PySelf::copycat_identity_name(cc_identity);
                        int len = cc_seat > 0 ? snprintf(cc_text, sizeof(cc_text), "%d号 ", cc_seat) : 0;
                        if (nm) snprintf(cc_text + len, sizeof(cc_text) - len, "%s", nm);
                        else if (cc_identity) snprintf(cc_text + len, sizeof(cc_text) - len, "身份%d", cc_identity);
                        else snprintf(cc_text + len, sizeof(cc_text) - len, "?");
                        s += cc_text;
                        name_color = copycat_color(cc_identity);
                    } else {
                        s+=data[i].str;
                    }
                    
                    ImFont* font = ImGui::GetFont();
float ns = g_esp.name_size;
ImVec2 ts = font->CalcTextSizeA(ns, FLT_MAX, 0.0f, s.c_str());
ImU32 nc = cc ? (ImU32)name_color : g_esp.name_color;
if (show_draw_Name) {
        ImFont* font = ImGui::GetFont();
        float ns = g_esp.name_size;
        ImVec2 ts = font->CalcTextSizeA(ns, FLT_MAX, 0.0f, s.c_str());
        ImU32 nc = cc ? (ImU32)name_color : g_esp.name_color;
        Draw->AddText(font, ns, {X1 + W/2 - ts.x/2, Y1 - 45}, nc, s.c_str());
    }

if (show_draw_Rect){
    ImU32 rc;
    if (cc) {
        switch (PySelf::copycat_camp(cc_identity)) {
            case 1:  rc = g_esp.rect_cc_good;    break;
            case 2:  rc = g_esp.rect_cc_bad;     break;
            case 3:  rc = g_esp.rect_cc_neutral; break;
            default: rc = g_esp.rect_ghost;      break;
        }
    }
    else if (IsGhostEntity(data[i]))  rc = g_esp.rect_ghost;
    else if (data[i].camp == 1)       rc = g_esp.rect_hunter;
    else                              rc = g_esp.rect_survivor;
    DrawEspRect(ImGui::GetForegroundDrawList(), X1, Y1, X2, Y2, W, H, rc);
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
    std::string dist_str = std::to_string((int) dist) + " 米";
    auto textSize = ImGui::CalcTextSize(dist_str.c_str(), 0, 25);
    Draw->AddText({X1 + W/2-(textSize.x/2),next_y}, ImColor(255,200,0,255), dist_str.c_str());
    next_y += dist_line_h;
}

if (show_draw_Genius){
    PyGenius::Info gi;
    if (PyGenius::lookup(data[i].obj, D.X, D.Y, gi)){
        PyGenius::GeniusLine seg;
        PyGenius::genius_segments(gi, seg);
        if (seg.pre[0] || seg.flywheel[0] || seg.post[0]){
            ImColor c;
            switch (PyGenius::desperate_state(gi)) {
                case PyGenius::DESPERATE_AVAILABLE: c = color_green; break;
                case PyGenius::DESPERATE_USED:      c = ImColor(140,140,140,255); break;
                case PyGenius::DESPERATE_UNKNOWN:
                default:                            c = ImColor(g_esp.genius_color); break;
            }
            ImColor flywheel_color = seg.flywheel_ready ? color_red : c;
            ImFont* font = ImGui::GetFont();
            float gs = g_esp.genius_size;
            float w_pre = font->CalcTextSizeA(gs, FLT_MAX, 0.0f, seg.pre).x;
            float w_fly = font->CalcTextSizeA(gs, FLT_MAX, 0.0f, seg.flywheel).x;
            float w_pos = font->CalcTextSizeA(gs, FLT_MAX, 0.0f, seg.post).x;
            float tx = X1 + W/2 - (w_pre + w_fly + w_pos)/2;
            if (seg.pre[0])      Draw->AddText(font, gs, {tx, next_y}, c, seg.pre);
            if (seg.flywheel[0]) Draw->AddText(font, gs, {tx + w_pre, next_y}, flywheel_color, seg.flywheel);
            if (seg.post[0])     Draw->AddText(font, gs, {tx + w_pre + w_fly, next_y}, c, seg.post);
            next_y += g_esp.genius_size + 4.0f;
        }
        if (show_draw_trait && (gi.camp == 1 || gi.camp == PyGenius::YIDHRA_PUPPET_UNIT_TYPE) && gi.support_trait != 0){
            char trait_line[48];
            PyGenius::trait_line_text(gi, PyGenius::trait_name(gi.support_trait), trait_line, sizeof(trait_line));
            if (trait_line[0]){
                ImColor cc2 = PyGenius::cd_ready(gi) ? color_green : ImColor(g_esp.trait_color);
                ImFont* font = ImGui::GetFont();
                float cs = g_esp.trait_size;
                float ts2 = font->CalcTextSizeA(cs, FLT_MAX, 0.0f, trait_line).x;
                Draw->AddText(font, cs, {X1 + W/2 - ts2/2, next_y}, cc2, trait_line);
                next_y += g_esp.trait_size + 4.0f;
            }
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
    FILE* f = fopen("/sdcard/umbrella_dump.txt", "w");
    if (f) { fprintf(f, "=== 宿伞快照 dump 开始 ===\n"); fclose(f); }
}
if (g_umbrella_dump_active) {
    double now = ImGui::GetTime();
    if (now - g_umbrella_dump_start > 5.0) {
        g_umbrella_dump_active = false;
        FILE* f = fopen("/sdcard/umbrella_dump.txt", "a");
        if (f) { fprintf(f, "=== dump 结束 ===\n"); fclose(f); }
    } else {
        int cur = PyAttack::cur_state();
        if (cur == 47) {
            // 只在 cur==47 期间 dump，每帧一次
            FILE* f = fopen("/sdcard/umbrella_dump.txt", "a");
            if (f) {
                fprintf(f, "\n--- frame %d t=%.2f ---\n",
                        g_umbrella_dump_frame++, now - g_umbrella_dump_start);
                for (int i = 0; i < data_count; i++) {
                    float xyz[3] = {0, 0, 0};
                    vm_readv(data[i].objcoor + 0xa0, xyz, 12);
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
            FILE* f = fopen("/sdcard/umbrella_dump.txt", "a");
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
            if (getFloat(data[i].obj+0x1a0)==450&&data[i].camp==2){
                std::string ss;
                // 镜线在 Draw_Main 开头算好(放下的镜子 / 准备阶段预览镜子二选一)，这里只做一次关于直线的对称
                float orig_x = D.X, orig_y = D.Y;
                calculate_line_reflection(mirror_line_x1, mirror_line_y1, mirror_line_x2, mirror_line_y2, orig_x, orig_y, &D.X, &D.Y);
                camera = matrix[3] * D.X + matrix[7] * D.Z + matrix[11] * D.Y + matrix[15];
                dist = sqrt(pow(D.X - Z.X, 2) + pow(D.Y - Z.Y, 2) + pow(D.Z - Z.Z, 2)) / dist_scale;
        		r_x = px + (matrix[0] * D.X + matrix[4] * D.Z + matrix[8] * D.Y + matrix[12]) / camera * px;
                r_y = py - (matrix[1] * D.X + matrix[5] * (D.Z+ 8.5) + matrix[9] * (D.Y) + matrix[13]) / camera * py;
                r_w = py - (matrix[1] * D.X + matrix[5] * (D.Z+ 28.5) + matrix[9] * (D.Y) + matrix[13]) / camera * py;
												
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
                        mirror_dist_str += " 米";
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
// ==================== 给 AutoAim 发布目标列表 ====================
{
    static AutoAim::TargetInfo tbuf[AutoAim::MAX_TARGETS];
    int n = 0;
    for (int i = 0; i < data_count && n < AutoAim::MAX_TARGETS; i++) {
        if (data[i].camp != 1 && data[i].camp != 2) continue;
        if (ShouldSkipEntity(data[i])) continue;

        float xyz[3] = {0, 0, 0};
        vm_readv(data[i].objcoor + 0xa0, xyz, 12);
        if (xyz[0] == 0 && xyz[2] == 0) continue;
        if (xyz[1] <= -300) continue;

        // 世界坐标：xyz[0]=X, xyz[2]=Y(水平), xyz[1]=Z(高度)
        float wx = xyz[0], wy = xyz[2], wz = xyz[1];

        // 投影
        float cam = matrix[3]*wx + matrix[7]*wz + matrix[11]*wy + matrix[15];
        if (cam <= 0.01f) continue;
        float sx = px + (matrix[0]*wx + matrix[4]*wz + matrix[8]*wy + matrix[12]) / cam * px;
        float sy = py - (matrix[1]*wx + matrix[5]*(wz+8.5f) + matrix[9]*wy + matrix[13]) / cam * py;

        // 朝向：coor + 0x90 / +0x98
        float fx = 0.f, fy = 0.f;
        uintptr_t coor = getPtr64(data[i].obj + 0x28);
        if (coor) {
            float a = 0.f, b = 0.f;
            vm_readv(coor + 0x90, &a, 4);
            vm_readv(coor + 0x98, &b, 4);
            float len = sqrtf(a*a + b*b);
            if (len > 0.01f) { fx = a / len; fy = b / len; }
        }

        // 世界距离
        float dmx = wx - Z.X, dmy = wy - Z.Y, dmz = wz - Z.Z;
        float dm = sqrtf(dmx*dmx + dmy*dmy + dmz*dmz) / dist_scale;

        // 预判后的屏幕坐标
        float pred = AutoAim::predict_m.load();
        float psx = sx, psy = sy;
        if (pred > 0.001f && (fx != 0.f || fy != 0.f)) {
            float wx2 = wx + fx * pred;
            float wy2 = wy + fy * pred;
            float c2 = matrix[3]*wx2 + matrix[7]*wz + matrix[11]*wy2 + matrix[15];
            if (c2 > 0.01f) {
                psx = px + (matrix[0]*wx2 + matrix[4]*wz + matrix[8]*wy2 + matrix[12]) / c2 * px;
                psy = py - (matrix[1]*wx2 + matrix[5]*(wz+8.5f) + matrix[9]*wy2 + matrix[13]) / c2 * py;
            }
        }

        AutoAim::TargetInfo& t = tbuf[n++];
        t.obj = data[i].obj;
        t.wx = wx; t.wy = wy; t.wz = wz;
        t.sx = sx; t.sy = sy;
        t.psx = psx; t.psy = psy;
        t.fx = fx; t.fy = fy;
        t.dist_m = dm;
        t.camp = data[i].camp;
    }
    AutoAim::publish_targets(tbuf, n);
}
AutoAim::DrawOverlay(Draw, native_window_screen_x, native_window_screen_y);
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
static char             g_boss_names[MAX_BOSSES][24];
static int              g_boss_n = 0;
static std::unordered_map<std::string, int> g_boss_name_idx;
static bool             g_hunter_picker_open = false;   // "选择监管"弹窗开关
static std::mutex g_hunter_name_mtx;
static std::string g_cached_hunter_name;
static const char*      kSkillCfgPath = "/sdcard/dwrgcfg.txt";

static void InitBossList() {
    if (g_boss_n > 0) return;
    auto add = [](const char* raw) {
        if (!raw || !raw[0]) return;
        std::string s = raw;
        if (s.size() >= 2 && s.front() == '[' && s.back() == ']')
            s = s.substr(1, s.size() - 2);
        if (g_boss_name_idx.count(s)) return;
        if (g_boss_n >= MAX_BOSSES) return;
        snprintf(g_boss_names[g_boss_n], sizeof(g_boss_names[0]), "%s", s.c_str());
        g_boss_name_idx[s] = g_boss_n;
        // 原来
//g_boss_delays[g_boss_n].store(0);

// 改成
g_boss_windup_normal[g_boss_n].store(0);
g_boss_windup_charge[g_boss_n].store(0);
        g_boss_n++;
    };
    for (size_t i = 0; i < sizeof(boss_table) / sizeof(boss_table[0]); i++)
        add(boss_table[i].display);
    // Name.h 里 getboss 特殊分支的显示名（不在这张表里）
    add("[白无常]"); add("[黑无常]"); add("[厂长]");
    add("[伊斯人]"); add("[时空之影]");
}

// 读场景对象的类名（跟读取循环里那 5 跳指针链一模一样）
static std::string read_scene_class_name(uint64_t obj) {
    if (!obj) return "";
    uint64_t chain_head = getPtr64(obj + 0xf8);
    if (!chain_head) return "";
    uint64_t class_name_obj = getPtr64(getPtr64(getPtr64(getPtr64(chain_head) + 0x8) + 0x20) + 0x20);
    if (!class_name_obj) return "";
    int len = getDword(class_name_obj + 0x10);
    if (len <= 0 || len >= 256) return "";
    uint64_t str_ptr = getPtr64(class_name_obj + 0x8);
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
    std::string cls = read_scene_class_name(s);
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

static void LoadHunterDelays() {
    InitBossList();
    FILE* f = fopen(kSkillCfgPath, "r");
    if (!f) { printf("[前摇] %s 不存在\n", kSkillCfgPath); return; }
    char line[256];
    int loaded = 0;
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == 0) continue;
        char name[64] = {0};
        int ni = 0;
        while (*p && !isspace((unsigned char)*p) && ni < (int)sizeof(name) - 1)
            name[ni++] = *p++;
        name[ni] = 0;
        while (*p && isspace((unsigned char)*p)) p++;

        std::string nstr(name);
        size_t cut = nstr.find("（");
        if (cut != std::string::npos) nstr = nstr.substr(0, cut);
        cut = nstr.find('(');
        if (cut != std::string::npos) nstr = nstr.substr(0, cut);

        // 格式：监管 普通刀前摇(s) [蓄力刀前摇(s)]
        float f1 = 0.f, f2 = 0.f;
        int got = sscanf(p, "%f %f", &f1, &f2);
        if (got < 1) continue;

        int n1 = (int)(f1 * 1000.f + 0.5f);
        int n2 = (got >= 2) ? (int)(f2 * 1000.f + 0.5f) : 0;
        if (n1 < 0) n1 = 0;  if (n1 > 5000) n1 = 5000;
        if (n2 < 0) n2 = 0;  if (n2 > 5000) n2 = 5000;

        auto it = g_boss_name_idx.find(nstr);
        if (it != g_boss_name_idx.end()) {
            g_boss_windup_normal[it->second].store(n1);
            g_boss_windup_charge[it->second].store(n2);
            loaded++;
        }
    }
    fclose(f);
    printf("[前摇] 已从 %s 加载 %d 条监管前摇\n", kSkillCfgPath, loaded);
    fflush(stdout);
}

static void SaveHunterDelays() {
    InitBossList();
    FILE* f = fopen(kSkillCfgPath, "w");
    if (!f) { printf("[前摇] 无法写 %s\n", kSkillCfgPath); return; }
    int saved = 0;
    for (int i = 0; i < g_boss_n; i++) {
        int wn = g_boss_windup_normal[i].load();
        int wc = g_boss_windup_charge[i].load();
        if (wn <= 0 && wc <= 0) continue;
        fprintf(f, "%s %.3f %.3f\n", g_boss_names[i], wn / 1000.0, wc / 1000.0);
        saved++;
    }
    fclose(f);
    printf("[前摇] 已保存 %d 条监管前摇到 %s\n", saved, kSkillCfgPath);
    fflush(stdout);
}

// 找主频最高的核（性能大核）
static int find_big_core() {
    int best_cpu = -1;
    long best_freq = 0;
    for (int i = 0; i < 16; i++) {
        char path[128];
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
        FILE* f = fopen(path, "r");
        if (!f) continue;
        long freq = 0;
        if (fscanf(f, "%ld", &freq) != 1) freq = 0;
        fclose(f);
        if (freq > best_freq) { best_freq = freq; best_cpu = i; }
    }
    return best_cpu;
}

// 把自己绑到大核 + SCHED_FIFO。失败不影响功能（普通用户没权限）
static void BoostCurrentThread() {
    int cpu = find_big_core();
    if (cpu >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(cpu, &set);
        sched_setaffinity(0, sizeof(set), &set);
    }
    struct sched_param param;
    param.sched_priority = 1;   // 最低实时优先级，别再高了会卡系统
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
}

// ==================== 触摸功能实现 ====================

static uint64_t TouchNowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// 画红框 + 处理拖动。在 Draw_Main 开头调用。
// 通用实现：一个红框 = 一组参数；调用两次分别处理技能/飞轮
static void DrawOneTouchRect(ImDrawList* Draw,
                              bool enabled,
                              float& rx, float& ry, float& rw, float& rh,
                              bool& inited, bool& dragging,
                              float& drag_dx, float& drag_dy,
                              ImU32 color, const char* label)
{
    if (!enabled) return;

    if (!inited && displayInfo.width > 0 && displayInfo.height > 0) {
        rw = 200.f; rh = 200.f;
        rx = displayInfo.width  * 0.7f;
        ry = displayInfo.height * 0.6f;
        inited = true;
    }

    ImVec2 p1(rx, ry);
    ImVec2 p2(rx + rw, ry + rh);
    ImVec2 center((p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f);

    Draw->AddRect(p1, p2, color, 0.0f, 0, 3.0f);
    Draw->AddLine(ImVec2(center.x - 12, center.y), ImVec2(center.x + 12, center.y),
                  color, 2.0f);
    Draw->AddLine(ImVec2(center.x, center.y - 12), ImVec2(center.x, center.y + 12),
                  color, 2.0f);
    char buf[96];
    snprintf(buf, sizeof(buf), "%s (%.0f, %.0f)", label, center.x, center.y);
    Draw->AddText(ImVec2(p1.x, p1.y > 24 ? p1.y - 20 : p1.y + 4), color, buf);

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse = io.MousePos;
    bool inside = mouse.x >= p1.x && mouse.x <= p2.x && mouse.y >= p1.y && mouse.y <= p2.y;

    if (io.MouseDown[0]) {
        if (!dragging && inside) {
            dragging = true;
            drag_dx = mouse.x - p1.x;
            drag_dy = mouse.y - p1.y;
        }
        if (dragging) {
            rx = mouse.x - drag_dx;
            ry = mouse.y - drag_dy;
            if (rx < 0.f) rx = 0.f;
            if (ry < 0.f) ry = 0.f;
            if (rx + rw > displayInfo.width)  rx = displayInfo.width  - rw;
            if (ry + rh > displayInfo.height) ry = displayInfo.height - rh;
        }
    } else {
        dragging = false;
    }
}

static void TouchRect_Draw(ImDrawList* Draw) {
    // 技能触摸区（红）
    DrawOneTouchRect(Draw, g_touch_adjust_enabled,
                     g_touch_rect_x, g_touch_rect_y, g_touch_rect_w, g_touch_rect_h,
                     g_touch_rect_inited, g_touch_rect_dragging,
                     g_touch_drag_dx, g_touch_drag_dy,
                     IM_COL32(255, 0, 0, 255), "技能");
    // 飞轮触摸区（绿）—— 只在"调整触摸区"打开时显示
DrawOneTouchRect(Draw, g_touch_adjust_enabled && g_auto_flywheel_enabled,
                 g_flywheel_rect_x, g_flywheel_rect_y, g_flywheel_rect_w, g_flywheel_rect_h,
                 g_flywheel_rect_inited, g_flywheel_rect_dragging,
                 g_flywheel_drag_dx, g_flywheel_drag_dy,
                 IM_COL32(0, 220, 120, 255), "飞轮");
}


// ==================== 自动技能：独立高频线程 ====================
static std::atomic<bool> g_autoskill_worker_started{false};

// ==================== 出刀上升沿回调 ====================
// 在 PyAttack 线程里执行：检测到出刀的那一瞬间立即 touch_down，零线程切换延迟
// 出刀后待触发的延迟状态
static std::atomic<uint64_t> g_auto_skill_attack_at{0};      // 出刀时刻(ms)，0 = 无待触发
static std::atomic<int> g_auto_skill_attack_windup{0};       // 那次出刀的前摇时长(ms)

// 在 PyAttack 线程里执行：只记时刻 + 前摇时长，实际 point 由 Worker 到点执行
static void OnAttackDetected()
{
    LOG("[攻击] enter en=%d t=%d p=%d taps=%d atk_at=%llu camp=%d\n",
        g_auto_skill_enabled ? 1 : 0, g_touch_inited ? 1 : 0,
        g_auto_skill_pending_up ? 1 : 0, g_auto_skill_remaining_taps,
        (unsigned long long)g_auto_skill_attack_at.load(), PySelf::camp());

    if (!g_auto_skill_enabled && !g_auto_flywheel_enabled) return;
    if (!g_touch_inited) return;
    if (g_auto_skill_pending_up) return;
    if (g_auto_skill_remaining_taps > 0) return;
    if (g_auto_skill_attack_at.load() != 0) return;
    if (PySelf::camp() == 1) return;
    if (displayInfo.width <= 0) return;

    int windup = 0;
    std::string hn;
    {
        std::lock_guard<std::mutex> lk(g_hunter_name_mtx);
        hn = g_cached_hunter_name;
    }
    if (!hn.empty()) {
        auto it = g_boss_name_idx.find(hn);
        if (it != g_boss_name_idx.end()) {
            if (PyAttack::attack_is_charge())
                windup = g_boss_windup_charge[it->second].load();
            else
                windup = g_boss_windup_normal[it->second].load();
        }
    }
    g_auto_skill_attack_windup.store(windup);
    g_auto_skill_attack_at.store(TouchNowMs());
}

// 读自己的飞轮剩余 CD（秒）。返回 false = 完全读不到（无论带没带都放行，避免误封）
static bool GetOwnFlywheelRemain(float &remain) {
    static uint64_t last_log = 0;
    uint64_t self_scene = 0;
    if (!PySelf::anchor(self_scene)) {
        if (TouchNowMs() - last_log > 2000) {
            last_log = TouchNowMs();
            LOG("[飞轮] anchor 失败\n");
        }
        return false;
    }
    PyGenius::Info gi;
    if (!PyGenius::lookup(self_scene, 0, 0, gi)) {
        if (TouchNowMs() - last_log > 2000) {
            last_log = TouchNowMs();
            LOG("[飞轮] lookup 失败 self_scene=0x%llX\n", (unsigned long long)self_scene);
        }
        return false;
    }
    remain = PyGenius::flywheel_remain_now(gi);
    if (TouchNowMs() - last_log > 2000) {
        last_log = TouchNowMs();
        LOG("[飞轮] has=%d remain=%.2f\n", gi.has_flywheel ? 1 : 0, remain);
    }
    return true;
}

// 读自己的技能 CD。目前没有可靠的"技能CD"数据源，默认总是可用（返回 true）。
// 待你确认要读哪一项之后，把这里替换成真实逻辑即可。
static bool GetOwnSkillReady() {
    return true;
}

static void AutoSkill_Worker()
{
    BoostCurrentThread();
    while (displayInfo.width <= 0 || displayInfo.height <= 0) usleep(20 * 1000);

    if (!g_touch_inited) {
        int short_side = std::min(displayInfo.width, displayInfo.height);
        int long_side  = std::max(displayInfo.width, displayInfo.height);
        bool ok = touch_init(short_side, long_side);
        LOG("[触摸] touch_init(%d,%d) -> %s\n", short_side, long_side, ok ? "OK" : "FAIL");
        g_touch_inited = ok;
    }

    // 通用发送：给某个 slot 在某个框的中心点发送 down
    auto send_down_at = [](int slot, float rx, float ry, float rw, float rh, uint64_t now_ms) {
    float cx = rx + rw * 0.5f;
    float cy = ry + rh * 0.5f;
    int tx = (int)cx, ty = (int)cy;

    // ★ Paradise 期望"短边在前、长边在后"，横屏时交换 x/y
    if (displayInfo.width > displayInfo.height) {
    // 横屏: 短边=1080(高), 长边=2400(宽)
    // Paradise 短边轴从底部往上数 → 用 高度-sy 翻转
    int sx = tx;      // 屏幕水平
    int sy = ty;      // 屏幕垂直
    tx = displayInfo.height - sy;   // 短边轴: 从底部数
    ty = sx;                        // 长边轴: 水平
}

// Paradise 直传屏幕坐标
    touch_down(slot, tx, ty);
        g_auto_skill_down_ms = now_ms;
        g_auto_skill_pending_up = true;
        g_auto_skill_remaining_taps = g_auto_skill_tap_count - 1;
        // 记一下用的是哪个 slot，抬指时用
        g_auto_skill_last_slot = slot;
    };

    // 记录本次 down 的 slot
    // (原来全局只有一个 slot=TOUCH_SLOT，现在两个复用)
    // g_auto_skill_last_slot 是新增的 static int

    while (true) {
        uint64_t now_ms = TouchNowMs();

        // ---- 出刀前摇计时 ----
        uint64_t atk_at = g_auto_skill_attack_at.load();
        if (atk_at != 0) {
            if (!g_auto_skill_enabled && !g_auto_flywheel_enabled) {
                g_auto_skill_attack_at.store(0);
            } else if (g_auto_skill_pending_up || g_auto_skill_remaining_taps > 0) {
                g_auto_skill_attack_at.store(0);
            } else {
                int windup = g_auto_skill_attack_windup.load();
                int delta  = windup - g_skill_lead_time_ms;
                if (delta < 0) delta = 0;
                uint64_t elapsed = now_ms - atk_at;
                if (elapsed >= (uint64_t)delta) {
    g_auto_skill_attack_at.store(0);
    // ★ 去掉 still_atk 检查：
    //   OnAttackDetected 靠 skill_id 变化触发，而 still_atk 靠 state_machine._state，
    //   两者有时序差。等 windup 时间后 raw_attacking 早就变 false 了。
    //   上升沿已经确认过是真出刀，这里不再重复验证。
    bool gate_ok = PyAttack::gate_pass_now();
    if (gate_ok) {
    
float fly_remain = 999.f;
bool fly_known   = GetOwnFlywheelRemain(fly_remain);
bool fly_ready   = fly_known && (fly_remain <= 0.1f);
bool skill_ready = GetOwnSkillReady();

bool use_flywheel = false;
bool do_action    = false;

if (g_auto_flywheel_enabled && g_auto_skill_enabled) {
    if (g_priority_flywheel) {
        // 优先飞轮：飞轮好了就飞轮，否则技能
        if (fly_ready)        { use_flywheel = true;  do_action = true; }
        else if (skill_ready) { use_flywheel = false; do_action = true; }
    } else {
        // 优先技能：技能好了就技能，否则飞轮
        if (skill_ready)      { use_flywheel = false; do_action = true; }
        else if (fly_ready)   { use_flywheel = true;  do_action = true; }
    }
} else if (g_auto_flywheel_enabled) {
    if (fly_ready) { use_flywheel = true; do_action = true; }
} else if (g_auto_skill_enabled) {
    use_flywheel = false; do_action = true;
}

LOG("[技能] 决策 skill_en=%d fly_en=%d skill_ready=%d fly_ready=%d fly_remain=%.2f use_fly=%d do=%d\n",
    g_auto_skill_enabled ? 1 : 0,
    g_auto_flywheel_enabled ? 1 : 0,
    skill_ready ? 1 : 0,
    fly_ready ? 1 : 0,
    fly_remain,
    use_flywheel ? 1 : 0,
    do_action ? 1 : 0);

if (do_action) {
    if (use_flywheel) {
        send_down_at(FLYWHEEL_SLOT,
                     g_flywheel_rect_x, g_flywheel_rect_y,
                     g_flywheel_rect_w, g_flywheel_rect_h,
                     now_ms);
    } else {
        send_down_at(TOUCH_SLOT,
                     g_touch_rect_x, g_touch_rect_y,
                     g_touch_rect_w, g_touch_rect_h,
                     now_ms);
    }
}
                    }
                }
            }
        }

        // ---- pending_up ----
        if (g_auto_skill_pending_up && now_ms - g_auto_skill_down_ms >= 80) {
            touch_up(g_auto_skill_last_slot);
            g_auto_skill_pending_up = false;
            if (g_auto_skill_remaining_taps > 0)
                g_auto_skill_next_tap_ms = now_ms + (uint64_t)g_auto_skill_tap_delay_ms;
        }

        // ---- 连点（沿用上一次的 slot）----
        if (!g_auto_skill_pending_up && g_auto_skill_remaining_taps > 0
            && now_ms >= g_auto_skill_next_tap_ms) {
            // 用 slot 对应框的中心发
            if (g_auto_skill_last_slot == FLYWHEEL_SLOT) {
                send_down_at(FLYWHEEL_SLOT,
                             g_flywheel_rect_x, g_flywheel_rect_y,
                             g_flywheel_rect_w, g_flywheel_rect_h,
                             now_ms);
            } else {
                send_down_at(TOUCH_SLOT,
                             g_touch_rect_x, g_touch_rect_y,
                             g_touch_rect_w, g_touch_rect_h,
                             now_ms);
            }
            g_auto_skill_remaining_taps--;
        }

        usleep(200);
    }
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

// 触摸屏监听线程
static void TouchTrigger_Worker() {
    int fd = find_touch_device();
    if (fd < 0) {
        printf("[触发] 找不到触摸屏设备\n");
        return;
    }

    int cur_x = 0, cur_y = 0;
    bool touching = false;
    input_event ev;

    while (true) {
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n != sizeof(ev)) {
            usleep(5000);
            continue;
        }

        if (ev.type == EV_ABS) {
            if (ev.code == ABS_MT_POSITION_X) cur_x = ev.value;
            else if (ev.code == ABS_MT_POSITION_Y) cur_y = ev.value;
            else if (ev.code == ABS_MT_TRACKING_ID) {
                touching = (ev.value >= 0);
            }
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            touching = (ev.value != 0);
        } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            // 一次完整帧：判断是否在触发区
            int x1 = g_trigger_zone_x1.load();
            int y1 = g_trigger_zone_y1.load();
            int x2 = g_trigger_zone_x2.load();
            int y2 = g_trigger_zone_y2.load();
            bool in_zone = touching && x1 >= 0 &&
                           cur_x >= x1 && cur_x <= x2 &&
                           cur_y >= y1 && cur_y <= y2;
            g_aim_trigger.store(in_zone);
        }
    }
}

void VolumeKeyHide() {
    int EventCount = GetInputDeviceCount();
    if (EventCount <= 0) return;

    int *fdArray = (int *)malloc(EventCount * sizeof(int));
    if (!fdArray) return;

    for (int i = 0; i < EventCount; i++) {
        char temp[128];
        sprintf(temp, "/dev/input/event%d", i);
        fdArray[i] = open(temp, O_RDONLY | O_NONBLOCK);   // 只读按键，不需要写权限
    }

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
static const char* kConfigPath = "/data/local/tmp/cg.config";

struct CfgKV {
    char key[64];
    char val[128];
    bool used;
};
static CfgKV g_cfg[128];
static int   g_cfg_n = 0;

static void CfgPut(const char* k, int v) {
    for (int i = 0; i < g_cfg_n; i++) if (strcmp(g_cfg[i].key, k) == 0) {
        snprintf(g_cfg[i].val, sizeof(g_cfg[i].val), "%d", v); return;
    }
    if (g_cfg_n >= 128) return;
    snprintf(g_cfg[g_cfg_n].key, sizeof(g_cfg[g_cfg_n].key), "%s", k);
    snprintf(g_cfg[g_cfg_n].val, sizeof(g_cfg[g_cfg_n].val), "%d", v);
    g_cfg_n++;
}
static void CfgPut(const char* k, float v) {
    for (int i = 0; i < g_cfg_n; i++) if (strcmp(g_cfg[i].key, k) == 0) {
        snprintf(g_cfg[i].val, sizeof(g_cfg[i].val), "%.4f", v); return;
    }
    if (g_cfg_n >= 128) return;
    snprintf(g_cfg[g_cfg_n].key, sizeof(g_cfg[g_cfg_n].key), "%s", k);
    snprintf(g_cfg[g_cfg_n].val, sizeof(g_cfg[g_cfg_n].val), "%.4f", v);
    g_cfg_n++;
}
static bool CfgGet(int idx, const char* k, int& out) {
    if (strcmp(g_cfg[idx].key, k) != 0) return false;
    out = atoi(g_cfg[idx].val); return true;
}
static bool CfgGet(int idx, const char* k, float& out) {
    if (strcmp(g_cfg[idx].key, k) != 0) return false;
    out = (float)atof(g_cfg[idx].val); return true;
}

static void CfgPut(const char* k, ImU32 v) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%08X", (unsigned)v);
    for (int i = 0; i < g_cfg_n; i++) if (strcmp(g_cfg[i].key, k) == 0) {
        snprintf(g_cfg[i].val, sizeof(g_cfg[i].val), "%s", buf); return;
    }
    if (g_cfg_n >= 128) return;
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
    CfgPut("show_draw_Rect",         show_draw_Rect ? 1 : 0);
    CfgPut("show_draw_Line",         show_draw_Line ? 1 : 0);
    CfgPut("show_draw_Camera",       show_draw_Camera ? 1 : 0);
    CfgPut("show_draw_Door",         show_draw_Door ? 1 : 0);
    CfgPut("show_draw_umbrella",     show_draw_umbrella ? 1 : 0);
    CfgPut("show_draw_Box",          show_draw_Box ? 1 : 0);
    CfgPut("show_draw_Name",         show_draw_Name ? 1 : 0);
    CfgPut("show_draw_Distance",     show_draw_Distance ? 1 : 0);
    CfgPut("show_draw_Cellar",       show_draw_Cellar ? 1 : 0);
    CfgPut("show_draw_Chair",        show_draw_Chair ? 1 : 0);
    CfgPut("show_draw_Prop",         show_draw_Prop ? 1 : 0);
    CfgPut("show_draw_Genius",       show_draw_Genius ? 1 : 0);
    CfgPut("show_draw_prophet",      show_draw_prophet ? 1 : 0);
    CfgPut("redqueenmod",            redqueenmod ? 1 : 0);
    CfgPut("copycat_mode",           copycat_mode ? 1 : 0);
    CfgPut("show_draw_secret_mechine", show_draw_secret_mechine ? 1 : 0);
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
CfgPut("esp.genius_size",        g_esp.genius_size);
CfgPut("esp.genius_color",       g_esp.genius_color);
CfgPut("esp.trait_size",         g_esp.trait_size);
CfgPut("esp.trait_color",        g_esp.trait_color);

    CfgPut("g_touch_adjust_enabled", g_touch_adjust_enabled ? 1 : 0);
    CfgPut("g_auto_skill_enabled",   g_auto_skill_enabled ? 1 : 0);
    CfgPut("g_touch_rect_x",         g_touch_rect_x);
    CfgPut("g_touch_rect_y",         g_touch_rect_y);
    CfgPut("g_touch_rect_w",         g_touch_rect_w);
    CfgPut("g_auto_flywheel_enabled",  g_auto_flywheel_enabled ? 1 : 0);
CfgPut("g_priority_flywheel",      g_priority_flywheel ? 1 : 0);
CfgPut("g_flywheel_rect_x",        g_flywheel_rect_x);
CfgPut("g_flywheel_rect_y",        g_flywheel_rect_y);
CfgPut("g_flywheel_rect_w",        g_flywheel_rect_w);

    CfgPut("PyAttack.max_distance",  PyAttack::max_distance());
    CfgPut("g_auto_skill_tap_count",    g_auto_skill_tap_count);
    CfgPut("g_auto_skill_tap_delay_ms", g_auto_skill_tap_delay_ms);
    CfgPut("PyAttack.require_facing",       PyAttack::require_facing() ? 1 : 0);
    CfgPut("PyAttack.facing_cos_threshold", PyAttack::facing_cos_threshold());
    CfgPut("PyAttack.skip_if_board_between", PyAttack::skip_if_board_between() ? 1 : 0);
    

    CfgPut("AutoPallet.enabled",     AutoPallet::enabled ? 1 : 0);
    CfgPut("AutoPallet.mode",        AutoPallet::mode);
    CfgPut("AutoPallet.touch_x",     AutoPallet::touch_x);
    CfgPut("AutoPallet.touch_y",     AutoPallet::touch_y);
    CfgPut("AutoPallet.hold_ms",     AutoPallet::hold_ms);
    CfgPut("AutoPallet.show_range",  AutoPallet::show_range ? 1 : 0);
    CfgPut("AutoPallet.show_touch_point", AutoPallet::show_touch_point ? 1 : 0);
    
    CfgPut("AutoAim.enabled",         AutoAim::enabled.load() ? 1 : 0);
CfgPut("AutoAim.touch_mode",      AutoAim::touch_mode.load());
CfgPut("AutoAim.lock_policy",     AutoAim::lock_policy.load());
CfgPut("AutoAim.predict_m",       AutoAim::predict_m.load());
CfgPut("AutoAim.show_circle",     AutoAim::show_circle.load() ? 1 : 0);
CfgPut("AutoAim.circle_r",        AutoAim::circle_r.load());
CfgPut("AutoAim.circle_thick",    AutoAim::circle_thick.load());
CfgPut("AutoAim.circle_color",    (ImU32)AutoAim::circle_color.load());
CfgPut("AutoAim.max_dist",        AutoAim::max_dist.load());
CfgPut("AutoAim.aim_rate",        AutoAim::aim_rate.load());
CfgPut("AutoAim.skill_x",         AutoAim::skill_x.load());
CfgPut("AutoAim.skill_y",         AutoAim::skill_y.load());
CfgPut("AutoAim.skill_w",         AutoAim::skill_w.load());
CfgPut("AutoAim.aim_x",           AutoAim::aim_x.load());
CfgPut("AutoAim.aim_y",           AutoAim::aim_y.load());
CfgPut("AutoAim.aim_w",           AutoAim::aim_w.load());

    FILE* f = fopen(kConfigPath, "w");
    if (!f) return;
    for (int i = 0; i < g_cfg_n; i++)
        fprintf(f, "%s=%s\n", g_cfg[i].key, g_cfg[i].val);
    fclose(f);
    SaveHunterDelays();
    printf("[配置] 已保存到 %s (%d 项)\n", kConfigPath, g_cfg_n);
    fflush(stdout);
}

// 直接 _exit() 绕过 atexit 和所有 detached 线程清理，只留 50ms 让 UI 刷一帧
static void ForceExit() {
    SaveConfig();
    std::thread([](){
        usleep(50 * 1000);
        _exit(0);   // 系统调用直接退，不清理
    }).detach();
}

static void LoadConfig() {
    FILE* f = fopen(kConfigPath, "r");
    if (!f) { printf("[配置] %s 不存在，使用默认值\n", kConfigPath); return; }
    char line[256];
    g_cfg_n = 0;
    while (fgets(line, sizeof(line), f) && g_cfg_n < 128) {
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
    fclose(f);

    int iv; float fv; ImU32 cv;
    for (int i = 0; i < g_cfg_n; i++) {
        if      (CfgGet(i, "show_draw_Rect", iv))           show_draw_Rect = iv != 0;
        else if (CfgGet(i, "show_draw_Line", iv))           show_draw_Line = iv != 0;
        else if (CfgGet(i, "show_draw_Camera", iv))         show_draw_Camera = iv != 0;
        else if (CfgGet(i, "show_draw_Door", iv))           show_draw_Door = iv != 0;
        else if (CfgGet(i, "show_draw_umbrella", iv))       show_draw_umbrella = iv != 0;
        else if (CfgGet(i, "show_draw_Box", iv))            show_draw_Box = iv != 0;
        else if (CfgGet(i, "show_draw_Name", iv))           show_draw_Name = iv != 0;
        else if (CfgGet(i, "show_draw_Distance", iv))       show_draw_Distance = iv != 0;
        else if (CfgGet(i, "show_draw_Cellar", iv))         show_draw_Cellar = iv != 0;
        else if (CfgGet(i, "show_draw_Chair", iv))          show_draw_Chair = iv != 0;
        else if (CfgGet(i, "show_draw_Prop", iv))           show_draw_Prop = iv != 0;
        else if (CfgGet(i, "show_draw_Genius", iv))         show_draw_Genius = iv != 0;
        else if (CfgGet(i, "show_draw_prophet", iv))        show_draw_prophet = iv != 0;
        else if (CfgGet(i, "redqueenmod", iv))              redqueenmod = iv != 0;
        else if (CfgGet(i, "copycat_mode", iv))             copycat_mode = iv != 0;
        else if (CfgGet(i, "show_draw_secret_mechine", iv)) show_draw_secret_mechine = iv != 0;
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
else if (CfgGet(i, "esp.trait_size", fv))         g_esp.trait_size = fv;
else if (CfgGet(i, "esp.trait_color", cv))        g_esp.trait_color = cv;
        else if (CfgGet(i, "g_touch_adjust_enabled", iv))   g_touch_adjust_enabled = iv != 0;
        else if (CfgGet(i, "g_auto_skill_enabled", iv))     g_auto_skill_enabled = iv != 0;
        else if (CfgGet(i, "g_touch_rect_x", fv))           g_touch_rect_x = fv;
        else if (CfgGet(i, "g_touch_rect_y", fv))           g_touch_rect_y = fv;
        else if (CfgGet(i, "g_touch_rect_w", fv))           g_touch_rect_w = fv;
        else if (CfgGet(i, "g_auto_flywheel_enabled", iv)) g_auto_flywheel_enabled = iv != 0;
else if (CfgGet(i, "g_priority_flywheel", iv))     g_priority_flywheel = iv != 0;
else if (CfgGet(i, "g_flywheel_rect_x", fv))       g_flywheel_rect_x = fv;
else if (CfgGet(i, "g_flywheel_rect_y", fv))       g_flywheel_rect_y = fv;
else if (CfgGet(i, "g_flywheel_rect_w", fv))       g_flywheel_rect_w = fv;
        else if (CfgGet(i, "PyAttack.max_distance", fv))    PyAttack::set_max_distance(fv);
        else if (CfgGet(i, "g_auto_skill_tap_count", iv))    g_auto_skill_tap_count = (iv < 1 ? 1 : (iv > 8 ? 8 : iv));
        else if (CfgGet(i, "g_auto_skill_tap_delay_ms", iv)) g_auto_skill_tap_delay_ms = (iv < 0 ? 0 : (iv > 1000 ? 1000 : iv));
        else if (CfgGet(i, "AutoPallet.enabled", iv))       AutoPallet::enabled = iv != 0;
        else if (CfgGet(i, "AutoPallet.mode", iv))          AutoPallet::mode = iv;
        else if (CfgGet(i, "AutoPallet.touch_x", fv))       AutoPallet::touch_x = fv;
        else if (CfgGet(i, "AutoPallet.touch_y", fv))       AutoPallet::touch_y = fv;
        else if (CfgGet(i, "AutoPallet.hold_ms", iv))       AutoPallet::hold_ms = iv;
        else if (CfgGet(i, "AutoPallet.show_range", iv))    AutoPallet::show_range = iv != 0;
        else if (CfgGet(i, "AutoPallet.show_touch_point", iv)) AutoPallet::show_touch_point = iv != 0;
        else if (CfgGet(i, "PyAttack.require_facing", iv))       PyAttack::set_require_facing(iv != 0);
        else if (CfgGet(i, "PyAttack.facing_cos_threshold", fv)) PyAttack::set_facing_cos_threshold(fv);
        else if (CfgGet(i, "PyAttack.skip_if_board_between", iv)) PyAttack::set_skip_if_board_between(iv != 0);
        
        else if (CfgGet(i, "AutoAim.enabled", iv))     AutoAim::enabled.store(iv != 0);
else if (CfgGet(i, "AutoAim.touch_mode", iv))  AutoAim::touch_mode.store(iv);
else if (CfgGet(i, "AutoAim.lock_policy", iv)) AutoAim::lock_policy.store(iv);
else if (CfgGet(i, "AutoAim.predict_m", fv))   AutoAim::predict_m.store(fv);
else if (CfgGet(i, "AutoAim.show_circle", iv)) AutoAim::show_circle.store(iv != 0);
else if (CfgGet(i, "AutoAim.circle_r", fv))    AutoAim::circle_r.store(fv);
else if (CfgGet(i, "AutoAim.circle_thick", fv)) AutoAim::circle_thick.store(fv);
else if (CfgGet(i, "AutoAim.circle_color", cv)) AutoAim::circle_color.store((uint32_t)cv);
else if (CfgGet(i, "AutoAim.max_dist", fv))    AutoAim::max_dist.store(fv);
else if (CfgGet(i, "AutoAim.aim_rate", iv))    AutoAim::aim_rate.store(iv);
else if (CfgGet(i, "AutoAim.skill_x", fv))     AutoAim::skill_x.store(fv);
else if (CfgGet(i, "AutoAim.skill_y", fv))     AutoAim::skill_y.store(fv);
else if (CfgGet(i, "AutoAim.skill_w", fv))     { AutoAim::skill_w.store(fv); AutoAim::skill_h.store(fv); }
else if (CfgGet(i, "AutoAim.aim_x", fv))       AutoAim::aim_x.store(fv);
else if (CfgGet(i, "AutoAim.aim_y", fv))       AutoAim::aim_y.store(fv);
else if (CfgGet(i, "AutoAim.aim_w", fv))       { AutoAim::aim_w.store(fv); AutoAim::aim_h.store(fv); }
    }
    // 如果配置里读到了有效的触摸区域坐标，标记为已初始化，
// 否则 TouchRect_Draw / AutoSkill_Worker 会把它重置回屏幕默认位置
if (g_touch_rect_x > 0.f || g_touch_rect_y > 0.f)         g_touch_rect_inited = true;
if (g_flywheel_rect_x > 0.f || g_flywheel_rect_y > 0.f)   g_flywheel_rect_inited = true;

if (AutoAim::skill_x.load() > 0.f || AutoAim::skill_y.load() > 0.f)
    AutoAim::skill_inited = true;
if (AutoAim::aim_x.load() > 0.f || AutoAim::aim_y.load() > 0.f)
    AutoAim::aim_inited = true;
    
    LoadHunterDelays();
printf("[配置] 已从 %s 加载 %d 项\n", kConfigPath, g_cfg_n);
    fflush(stdout);
}

// ==================== 新 UI 的四个页面（供 my_ui.cpp 调用） ====================
namespace UI_Pages {

// ---------- 页 0：绘制 ----------
void Draw(Theme::Palette& pal) {
    using namespace FangUI;

    SectionLabel("显示设置", pal);

    // ---- 方框 ----
    if (CollapseRowToggle("方框", &show_draw_Rect, pal)) {
        ImGui::Indent(20.0f);
        SectionLabel("样式", pal);
        static const char* rect_styles[] = {"全框", "四角"};
        int ns = SegmentedControl("rect_style", rect_styles, 2, g_esp.rect_style, pal);
        if (ns != g_esp.rect_style) g_esp.rect_style = ns;

        SectionLabel("粗细", pal);
        float th = g_esp.rect_thickness;
        if (FancySlider("rect_th", &th, 0.5f, 6.0f, "%.1f px", pal))
            g_esp.rect_thickness = th;

        ImGui::Dummy(ImVec2(0, 6));
        SectionLabel("颜色", pal);
        ColorPickerRow("监管者",  &g_esp.rect_hunter,     pal);
        ColorPickerRow("求生者",  &g_esp.rect_survivor,   pal);
        ColorPickerRow("鬼魂",    &g_esp.rect_ghost,      pal);
        ColorPickerRow("侦探团",  &g_esp.rect_cc_good,    pal);
        ColorPickerRow("模仿者",  &g_esp.rect_cc_bad,     pal);
        ColorPickerRow("中立",    &g_esp.rect_cc_neutral, pal);
        ImGui::Unindent(20.0f);
        ImGui::Dummy(ImVec2(0, 6));
    }

    // ---- 射线（新增独立开关） ----
    if (CollapseRowToggle("射线", &show_draw_Line, pal)) {
        ImGui::Indent(20.0f);
        ColorPickerRow("颜色", &g_esp.line_color, pal);
        SectionLabel("粗细", pal);
        float lt = g_esp.line_thickness;
        if (FancySlider("ln_th", &lt, 0.5f, 6.0f, "%.1f px", pal))
            g_esp.line_thickness = lt;
        ImGui::Unindent(20.0f);
        ImGui::Dummy(ImVec2(0, 6));
    }

    // ---- 名字 ----
    if (CollapseRowToggle("名字", &show_draw_Name, pal)) {
        ImGui::Indent(20.0f);
        ColorPickerRow("颜色", &g_esp.name_color, pal);
        SectionLabel("大小", pal);
        float nm = g_esp.name_size;
        if (FancySlider("nm_sz", &nm, 12.0f, 48.0f, "%.0f", pal))
            g_esp.name_size = nm;
        ImGui::Unindent(20.0f);
        ImGui::Dummy(ImVec2(0, 6));
    }

    // ---- 距离（仅开关） ----
    RowToggle("距离", &show_draw_Distance, pal);

    // ---- 天赋 ----
    if (CollapseRowToggle("天赋", &show_draw_Genius, pal)) {
        ImGui::Indent(20.0f);
        ColorPickerRow("颜色", &g_esp.genius_color, pal);
        SectionLabel("大小", pal);
        float gs = g_esp.genius_size;
        if (FancySlider("gn_sz", &gs, 12.0f, 48.0f, "%.0f", pal))
            g_esp.genius_size = gs;
        ImGui::Unindent(20.0f);
        ImGui::Dummy(ImVec2(0, 6));
    }

    // ---- 技能CD（新增独立开关） ----
    if (CollapseRowToggle("技能CD", &show_draw_trait, pal)) {
        ImGui::Indent(20.0f);
        ColorPickerRow("颜色", &g_esp.trait_color, pal);
        SectionLabel("大小", pal);
        float ts = g_esp.trait_size;
        if (FancySlider("tr_sz", &ts, 12.0f, 48.0f, "%.0f", pal))
            g_esp.trait_size = ts;
        ImGui::Unindent(20.0f);
        ImGui::Dummy(ImVec2(0, 6));
    }

    RowToggle("显示鬼魂",   &inform_ghost, pal);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("预知监管");
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 46.0f);
    if (ToggleSwitch("prophet", &show_draw_prophet, pal) && show_draw_prophet)
        copycat_mode = false;

    RowToggle("绘制道具",   &show_draw_Prop, pal);
    RowToggle("夫人模式",   &redqueenmod, pal);
    RowToggle("绘制调试",   &Debugging, pal);
    RowToggle("显示密码机", &show_draw_secret_mechine, pal);
    RowToggle("显示大门",   &show_draw_Door, pal);
    RowToggle("宿伞模式",   &show_draw_umbrella, pal);

    ImGui::Dummy(ImVec2(0, 10));
    SectionLabel("模仿者", pal);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("模仿者模式");
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 46.0f);
    if (ToggleSwitch("copycat", &copycat_mode, pal) && copycat_mode)
        show_draw_prophet = false;

    if (copycat_mode) {
        PySelf::CopycatBook book = PySelf::copycat_book();
        int known = 0, with_id = 0;
        for (int k = 0; k < PySelf::MAX_COPYCAT; ++k)
            if (book.seat[k].known) { known++; if (book.seat[k].identity) with_id++; }
        ImGui::TextDisabled("已记录 %d 人，身份已知 %d 人", known, with_id);
        for (int k = 0; k < PySelf::MAX_COPYCAT; ++k) {
            const PySelf::CopycatSeat &st = book.seat[k];
            if (!st.known) continue;
            const char *nm = PySelf::copycat_identity_name(st.identity);
            char row[64];
            int len = snprintf(row, sizeof(row), "%2d号 ", k + 1);
            if (nm) len += snprintf(row + len, sizeof(row) - len, "%s", nm);
            else if (st.identity) len += snprintf(row + len, sizeof(row) - len, "身份%d", st.identity);
            else len += snprintf(row + len, sizeof(row) - len, "?");
            if (book.self_seat == k + 1) len += snprintf(row + len, sizeof(row) - len, "  (我)");
            if (st.dead) snprintf(row + len, sizeof(row) - len, "  已死");
            ImColor col = st.dead ? ImColor(140,140,140,255) : copycat_color(st.identity);
            ImGui::TextColored(col.Value, "%s", row);
        }
    }

    ImGui::Dummy(ImVec2(0, 16));
    float avail = ImGui::GetContentRegionAvail().x;
    if (CenterButton("保存配置", ImVec2(avail * 0.48f, 44), pal, true)) {
        SaveConfig();
        PyDump::reset_status("配置已保存");
    }
    ImGui::SameLine(0, avail * 0.04f);
    if (CenterButton("结束进程", ImVec2(avail * 0.48f, 44), pal, false, true)) {
        ForceExit();
    }
}

// ---------- 页 1：触摸 ----------
void Touch(Theme::Palette& pal) {
    using namespace FangUI;

    static int g_touch_sub = 0;
const char* sub_items[] = { "自瞄", "自动技能", "自动盖板" };
float sw3 = (ImGui::GetContentRegionAvail().x - 4.0f) / 3.0f;
for (int i = 0; i < 3; i++) {
    if (i > 0) ImGui::SameLine(0, 2);
    if (CenterButton(sub_items[i], ImVec2(sw3, 36), pal, false, false, g_touch_sub == i ? 1 : 0))
        g_touch_sub = i;
}
ImGui::Dummy(ImVec2(0, 12));

if (g_touch_sub == 0) {
    // ==================== 自瞄 ====================
    SectionLabel("自瞄开关", pal);
{
    bool en = AutoAim::enabled.load();
    if (RowToggle("自瞄", &en, pal)) AutoAim::enabled.store(en);
}
ImGui::TextDisabled("状态: %s", AutoAim::status_text());

    ImGui::Dummy(ImVec2(0, 8));
    SectionLabel("触摸模式", pal);
    static const char* modes[] = { "双触摸", "单触摸(蜡像师)" };
    int tm = AutoAim::touch_mode.load();
    int nt = SegmentedControl("aa_mode", modes, 2, tm, pal);
    if (nt != tm) AutoAim::touch_mode.store(nt);
    ImGui::TextDisabled(AutoAim::touch_mode.load() == 0
        ? "A手指常驻技能键，B手指在自瞄区滑动"
        : "手指从技能键位置直接滑动");
        
    ImGui::Dummy(ImVec2(0, 8));
SectionLabel("触发区", pal);
RowToggle("显示触发区", &AutoAim::draw_trigger, pal);
RowToggle("显示技能键框", &AutoAim::draw_ranges, pal);

{
    int zx = g_trigger_zone_x1.load();
    int zy = g_trigger_zone_y1.load();
    int zw = g_trigger_zone_x2.load() - zx;
    int zh = g_trigger_zone_y2.load() - zy;

    ImGui::TextDisabled("触发区 (%d,%d) %dx%d", zx, zy, zw, zh);
}
float ar = AutoAim::aim_radius.load();
if (FancySlider("aim_radius", &ar, 100.0f, 800.0f, "%.0f px", pal))
    AutoAim::aim_radius.store(ar);
ImGui::TextDisabled("视野手指从屏幕中心偏移多少像素算满方向");

ImGui::Dummy(ImVec2(0, 8));
SectionLabel("触发区调整", pal);
RowToggle("显示触发区", &AutoAim::draw_trigger, pal);

{
    int zx = g_trigger_zone_x1.load();
    int zy = g_trigger_zone_y1.load();
    int zw = g_trigger_zone_x2.load() - zx;
    int zh = g_trigger_zone_y2.load() - zy;

    ImGui::TextDisabled("位置/大小 (屏幕像素)");

    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("X", &zx, 10, 100)) {
        if (zx < 0) zx = 0;
        g_trigger_zone_x1.store(zx);
        g_trigger_zone_x2.store(zx + zw);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("Y", &zy, 10, 100)) {
        if (zy < 0) zy = 0;
        g_trigger_zone_y1.store(zy);
        g_trigger_zone_y2.store(zy + zh);
    }

    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("宽", &zw, 10, 100)) {
        if (zw < 20) zw = 20;
        g_trigger_zone_x2.store(zx + zw);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("高", &zh, 10, 100)) {
        if (zh < 20) zh = 20;
        g_trigger_zone_y2.store(zy + zh);
    }

    ImGui::TextDisabled("当前: (%d,%d) ~ (%d,%d)",
        zx, zy, g_trigger_zone_x2.load(), g_trigger_zone_y2.load());
}

// 映射模式切换
ImGui::Dummy(ImVec2(0, 6));
SectionLabel("触摸映射模式(横屏)", pal);
ImGui::TextDisabled("若触发位置跟红框错位，切换模式试");
static const char* map_names[] = {
    "0:不交换", "1:交换", "2:交换+X反向", "3:交换+Y反向", "4:交换+都反向"
};
int mm = g_trigger_mapping.load();
ImGui::SetNextItemWidth(200);
if (ImGui::Combo("##map_mode", &mm, map_names, 5)) {
    g_trigger_mapping.store(mm);
}
ImGui::TextDisabled("当前: %s", map_names[mm]);

    ImGui::Dummy(ImVec2(0, 8));
    SectionLabel("触摸区调整", pal);
    RowToggle("显示调整框", &AutoAim::draw_ranges, pal);
    if (AutoAim::draw_ranges) {
        float sw2 = AutoAim::skill_w.load();
        if (FancySlider("sk_sz", &sw2, 60.0f, 500.0f, "%.0f px", pal))
            AutoAim::skill_w.store(sw2), AutoAim::skill_h.store(sw2);
        ImGui::TextDisabled("技能键: (%.0f,%.0f)",
            AutoAim::skill_x.load() + AutoAim::skill_w.load()*0.5f,
            AutoAim::skill_y.load() + AutoAim::skill_h.load()*0.5f);
        ImGui::TextColored(ImVec4(1,0.4f,0.4f,1), "红框内按住拖动改技能键位置");

        float aw2 = AutoAim::aim_w.load();
        if (FancySlider("am_sz", &aw2, 60.0f, 800.0f, "%.0f px", pal))
            AutoAim::aim_w.store(aw2), AutoAim::aim_h.store(aw2);
        ImGui::TextDisabled("自瞄区: (%.0f,%.0f)",
            AutoAim::aim_x.load() + AutoAim::aim_w.load()*0.5f,
            AutoAim::aim_y.load() + AutoAim::aim_h.load()*0.5f);
        ImGui::TextColored(ImVec4(0.4f,1,0.4f,1), "绿框内按住拖动改自瞄区位置");
    }

    ImGui::Dummy(ImVec2(0, 8));
    SectionLabel("锁定判定", pal);
    static const char* lock_items[] = { "优先距离最近", "优先屏幕视角最近" };
    int lp = AutoAim::lock_policy.load();
    int nl = SegmentedControl("aa_lock", lock_items, 2, lp, pal);
    if (nl != lp) AutoAim::lock_policy.store(nl);

    ImGui::Dummy(ImVec2(0, 8));
    SectionLabel("预判", pal);
    float pr = AutoAim::predict_m.load();
    if (FancySlider("aa_pred", &pr, 0.0f, 10.0f, "%.1f m", pal))
        AutoAim::predict_m.store(pr);
    ImGui::TextDisabled("沿敌方朝向推 %.1f 米", pr);

    ImGui::Dummy(ImVec2(0, 8));
SectionLabel("自瞄范围", pal);
{
    bool sc = AutoAim::show_circle.load();
    if (RowToggle("显示范围圆", &sc, pal)) AutoAim::show_circle.store(sc);
}
if (AutoAim::show_circle.load()) {
    float cr = AutoAim::circle_r.load();            // ← 这行
    if (FancySlider("aa_cr", &cr, 50.0f, 1200.0f, "%.0f px", pal))
        AutoAim::circle_r.store(cr);

    float ct = AutoAim::circle_thick.load();
    if (FancySlider("aa_ct", &ct, 0.5f, 8.0f, "%.1f px", pal))
        AutoAim::circle_thick.store(ct);

    ImU32 cc = AutoAim::circle_color.load();
    if (ColorPickerRow("颜色", &cc, pal))
        AutoAim::circle_color.store(cc);
}

    ImGui::Dummy(ImVec2(0, 8));
    SectionLabel("限制距离", pal);
    float md2 = AutoAim::max_dist.load();
    if (FancySlider("aa_md", &md2, 0.0f, 100.0f, "%.0f m", pal))
        AutoAim::max_dist.store(md2);
    ImGui::TextDisabled("超过该距离不触发；100=不限制");

    ImGui::Dummy(ImVec2(0, 8));
SectionLabel("瞄准速率", pal);
float mv = (float)AutoAim::aim_rate.load();
if (FancySlider("aa_rate", &mv, 100.0f, 5000.0f, "%.0f px/s", pal))
    AutoAim::aim_rate.store((int)mv);
ImGui::TextDisabled("每秒最多移动 %.0f 像素", mv);
} else if (g_touch_sub == 1) {
    
        // ==================== 自动技能 ====================
        SectionLabel("自动技能", pal);
        RowToggle("自动技能", &g_auto_skill_enabled, pal);

        if (g_auto_skill_enabled) {
            // ---- 技能触摸区域 ----
            ImGui::Dummy(ImVec2(0, 8));
            SectionLabel("技能触摸区", pal);
            RowToggle("调整触摸区（红=技能，绿=飞轮）", &g_touch_adjust_enabled, pal);
            if (g_touch_adjust_enabled) {
                float v = g_touch_rect_w;
                if (FancySlider("rect_size", &v, 50.0f, 800.0f, "%.0f px", pal))
                    g_touch_rect_w = g_touch_rect_h = v;
                ImGui::TextDisabled("位置 (%.0f, %.0f)", g_touch_rect_x, g_touch_rect_y);
                ImGui::TextColored(ImVec4(1,0,0,1), "红框内按住拖动可改位置");
            }

            // ---- 距离限制 ----
            ImGui::Dummy(ImVec2(0, 8));
            SectionLabel("触发距离", pal);
            float md = PyAttack::max_distance();
            if (FancySlider("md", &md, 0.0f, 50.0f, "%.0f m", pal))
                PyAttack::set_max_distance(md);
            ImGui::TextDisabled("50 = 不限制距离");

            // ---- 点技能延迟 ----
            ImGui::Dummy(ImVec2(0, 10));
            SectionLabel("点技能延迟", pal);
            std::string hn = current_hunter_name();
            if (hn.empty())
                ImGui::TextDisabled("当前监管: (未识别)");
            else
                ImGui::Text("当前监管: %s", hn.c_str());

            int cur_idx = -1;
            if (!hn.empty()) {
                auto it = g_boss_name_idx.find(hn);
                if (it != g_boss_name_idx.end()) cur_idx = it->second;
            }

            if (cur_idx >= 0) {
                int wn = g_boss_windup_normal[cur_idx].load();
                float fwn = wn / 1000.0f;
                if (FancySlider("windup_n", &fwn, 0.0f, 5.0f, "%.3f s", pal))
                    g_boss_windup_normal[cur_idx].store((int)(fwn * 1000.0f + 0.5f));
                ImGui::TextDisabled("普通刀前摇 %.3f s", fwn);

                int wc = g_boss_windup_charge[cur_idx].load();
                float fwc = wc / 1000.0f;
                if (FancySlider("windup_c", &fwc, 0.0f, 5.0f, "%.3f s", pal))
                    g_boss_windup_charge[cur_idx].store((int)(fwc * 1000.0f + 0.5f));
                ImGui::TextDisabled("蓄力刀前摇 %.3f s", fwc);
            } else {
                ImGui::TextDisabled("未识别到监管，无法单独设置");
            }

            {
                float lead = (float)g_skill_lead_time_ms;
                if (FancySlider("lead", &lead, 0.0f, 300.0f, "%.0f ms", pal))
                    g_skill_lead_time_ms = (int)(lead + 0.5f);
                ImGui::TextDisabled("前摇结束前 %.0f ms 按下", lead);
            }

            if (CenterButton("选择监管",
                    ImVec2(ImGui::GetContentRegionAvail().x, 38), pal, true))
                g_hunter_picker_open = true;
            ImGui::TextDisabled("每把监管可单独配置，0 = 不延迟");

            // ---- 触发条件 ----
            bool facing = PyAttack::require_facing();
            if (RowToggle("只在监管面朝自己时触发", &facing, pal))
                PyAttack::set_require_facing(facing);

            bool skipb = PyAttack::skip_if_board_between();
            if (RowToggle("隔着板子不放技能", &skipb, pal))
                PyAttack::set_skip_if_board_between(skipb);

            if (facing) {
                float cos_th = PyAttack::facing_cos_threshold();
                if (FancySlider("cos", &cos_th, -0.5f, 0.9f, "%.2f", pal))
                    PyAttack::set_facing_cos_threshold(cos_th);
                ImGui::TextDisabled("0.3 = 约 72°；越大越严格");
            }

            // ---- 连点 ----
            float tc = (float)g_auto_skill_tap_count;
            if (FancySlider("tc", &tc, 1.0f, 8.0f, "%.0f 次", pal))
                g_auto_skill_tap_count = (int)(tc + 0.5f);
            float td = (float)g_auto_skill_tap_delay_ms;
            if (FancySlider("td", &td, 0.0f, 1000.0f, "%.0f ms", pal))
                g_auto_skill_tap_delay_ms = (int)(td + 0.5f);
            ImGui::TextDisabled("延迟只影响第 2 次起的点击");
        }

        // ==================== 自动飞轮 ====================
        ImGui::Dummy(ImVec2(0, 14));
        SectionLabel("自动飞轮", pal);
        RowToggle("自动飞轮", &g_auto_flywheel_enabled, pal);

        if (g_auto_flywheel_enabled) {
            // ---- 飞轮触摸区域 ----
            ImGui::Dummy(ImVec2(0, 8));
            SectionLabel("飞轮触摸区", pal);
            ImGui::TextDisabled("位置 (%.0f, %.0f)  %.0fx%.0f",
                g_flywheel_rect_x, g_flywheel_rect_y,
                g_flywheel_rect_w, g_flywheel_rect_h);
            ImGui::TextColored(ImVec4(0,0.86f,0.47f,1), "绿框内按住拖动可改位置");
            float fw = g_flywheel_rect_w;
            if (FancySlider("flywheel_size", &fw, 50.0f, 800.0f, "%.0f px", pal))
                g_flywheel_rect_w = g_flywheel_rect_h = fw;

            ImGui::TextDisabled("开启「调整触摸区域」后拖动绿框");
        }

        // ---- 优先飞轮（两个都开时才有效） ----
        if (g_auto_flywheel_enabled && g_auto_skill_enabled) {
            ImGui::Dummy(ImVec2(0, 10));
            SectionLabel("优先级", pal);
            RowToggle("优先飞轮(出刀时)", &g_priority_flywheel, pal);
            ImGui::TextDisabled(g_priority_flywheel
                ? "飞轮就绪 → 飞轮；否则 → 技能"
                : "技能就绪 → 技能；否则 → 飞轮");
        }
    } else {
        SectionLabel("盖板开关", pal);
        RowToggle("开关盖板",     &AutoPallet::enabled, pal);
        RowToggle("显示判定范围", &AutoPallet::show_range, pal);
        RowToggle("显示触摸点",   &AutoPallet::show_touch_point, pal);
        ImGui::Spacing();
        if (CenterButton("测试点击", ImVec2(ImGui::GetContentRegionAvail().x, 38), pal, true))
            AutoPallet::request_test_tap();
        ImGui::Dummy(ImVec2(0, 10));
        SectionLabel("参数", pal);
        static const char *modes[] = {"暴力(宽12)", "演戏(宽8)", "随机(8~12)"};
        ImGui::Combo("模式", &AutoPallet::mode, modes, 3);
        float tx = AutoPallet::touch_x;
        if (FancySlider("tx", &tx, 0.0f, (float)displayInfo.width, "%.0f", pal))
            AutoPallet::touch_x = tx;
        float ty = AutoPallet::touch_y;
        if (FancySlider("ty", &ty, 0.0f, (float)displayInfo.height, "%.0f", pal))
            AutoPallet::touch_y = ty;
        float hm = (float)AutoPallet::hold_ms;
        if (FancySlider("hm", &hm, 10.0f, 120.0f, "%.0f ms", pal))
            AutoPallet::hold_ms = (int)(hm + 0.5f);

        ImGui::Spacing();
        ImGui::TextWrapped("状态: %s", AutoPallet::status_text());
        ImGui::TextDisabled("点击 成功%d 失败%d  确认放下%d",
            AutoPallet::g_tap_ok.load(), AutoPallet::g_tap_fail.load(),
            AutoPallet::g_confirmed.load());
        ImGui::TextDisabled("上次: %s", AutoPallet::last_result());
    }
}
// ---------- 页 2：设置 ----------
void Settings(Theme::Palette& pal) {
    using namespace FangUI;
SectionLabel("刷新率", pal);
    static float g_target_fps_f = 60.0f;
    if (FancySlider("fps", &g_target_fps_f, 60.0f, 144.0f, "%.0f Hz", pal))
        ::fps = g_target_fps_f;
    ImGui::TextDisabled("影响辅助自身渲染频率");
    ImGui::Text("当前帧率: %.1f FPS", ImGui::GetIO().Framerate);

    ImGui::Dummy(ImVec2(0, 10));
    SectionLabel("内存", pal);
    static MemUsage mem;
    static double   mem_time = -10.0;
    if (ImGui::GetTime() - mem_time >= 1.0) { mem = read_memory_usage(); mem_time = ImGui::GetTime(); }
    ImGui::Text("内存占用: %.1f MB", mem.total_kb() / 1024.0f);
    if (mem.gpu_ok)
        ImGui::TextDisabled("  进程%.1f + 换出%.1f + 显存%.1f + 缓冲%.1f MB",
            mem.pss_kb/1024.0f, mem.swap_kb/1024.0f, mem.gpu_kb/1024.0f, mem.buffer_kb/1024.0f);
    else
        ImGui::TextDisabled("  进程%.1f + 换出%.1f + 缓冲%.1f MB (显存未计)",
            mem.pss_kb/1024.0f, mem.swap_kb/1024.0f, mem.buffer_kb/1024.0f);

    ImGui::Spacing();
    if (read_state == 2)      ImGui::TextColored(ImVec4(0,0.78f,0.35f,1), "● 已获取到游戏数据");
    else if (read_state == 1) ImGui::TextColored(ImVec4(1,0.23f,0.19f,1), "● 正在获取游戏数据");
    else                      ImGui::TextDisabled("● 未连接");

    ImGui::Dummy(ImVec2(0, 10));
    SectionLabel("地址信息", pal);
    ImGui::TextDisabled("游戏进程: %d",   pid);
    ImGui::TextDisabled("模块入口: %lx",  libbase);
    ImGui::TextDisabled("游戏包名: %s",   extractedString);
    ImGui::TextDisabled("矩阵地址: %lx",  Matrix);
    ImGui::TextDisabled("数组地址: %lx",  Arrayaddr);
    ImGui::TextDisabled("矩阵偏移: %lx",  MatrixOffset);
    ImGui::TextDisabled("数组偏移: %lx",  ArrayaddrOffset);
    ImGui::TextDisabled("模块页数: %d",   c);
    ImGui::TextDisabled("监管者: %s",     prophet_text);

    ImGui::Dummy(ImVec2(0, 10));
    SectionLabel("模块状态", pal);
    ImGui::Dummy(ImVec2(0, 10));
SectionLabel("当前角色", pal);
{
    std::string self = current_self_name();
    int camp = PySelf::camp();
    if (self.empty())
        ImGui::TextDisabled("(未识别)");
    else
        ImGui::Text("我: %s", self.c_str());
    if (camp == 1) {
        std::string h = current_hunter_name();
        if (!h.empty()) ImGui::TextDisabled("当前监管: %s", h.c_str());
    }
}
    ImGui::TextWrapped("Py根: %s",              PyRoot::status_text());
    ImGui::TextWrapped("密码机: %s (已破译%d)", PyProgress::status_text(), PyProgress::decoded_count());
    ImGui::TextWrapped("天赋: %s",              PyGenius::status_text());
    ImGui::TextWrapped("自身: %s",              PySelf::status_text());

    ImGui::Spacing();
    if (PyAttack::ready()) {
        std::string nm = PyAttack::state_name();
        ImVec4 col = PyAttack::attacking() ? ImVec4(1,0,0,1) : Theme::U32ToVec4(pal.text);
        ImGui::TextColored(col, "监管状态: %s", nm.c_str());
        ImGui::TextDisabled("cur=%d idle=%d", PyAttack::cur_state(), PyAttack::idle_state());
    } else {
        ImGui::TextDisabled("监管状态: 等待数据");
    }
    if (PyAttack::skillmgr_ready()) {
        int sid = PyAttack::last_cast_skill_id();
        const char *tag = (sid == 2601) ? "普通刀"
                        : (sid == 2605) ? "蓄力刀"
                        : (sid == 2603) ? "未出刀" : "?";
        ImGui::TextDisabled("技能ID: %d (%s)", sid, tag);
    } else {
        ImGui::TextDisabled("技能ID: 读取中...");
    }
    if (PyAttack::ready() && PyAttack::cur_state() == 0)
        ImGui::TextDisabled("(state=0：该帧读到坏值，已丢弃)");

    ImGui::Dummy(ImVec2(0, 10));
    SectionLabel("监管技能 CD", pal);
    {
        uint64_t hu = PyAttack::hunter_unit_raw();
        if (!hu) {
            ImGui::TextDisabled("监管 unit 未就绪");
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
            } else {
                for (int i = 0; i < sk_n; i++) {
                    const auto &e = sk_list[i];
                    float r = PyGenius::skill_cd_remain_now(e);
                    if (e.charge_max > 0)
                        ImGui::Text("id=%lld  %d/%d  %.1fs / %.1fs",
                                    (long long)e.skill_id, e.charge_cur, e.charge_max, r, e.total);
                    else
                        ImGui::Text("id=%lld  %.1fs / %.1fs",
                                    (long long)e.skill_id, r, e.total);
                }
            }
        }
    }
}

// ---------- 页 3：Dump ----------
void Dump(Theme::Palette& pal) {
    using namespace FangUI;

    static int g_kb_target = 0;
    static char filter_buf[64] = "";
    SectionLabel("查询", pal);
    ImGui::InputText("过滤词", filter_buf, sizeof(filter_buf));
    if (ImGui::IsItemClicked()) g_kb_target = 0;
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
    if (CenterButton("Dump 监管[0]", ImVec2(avail * 0.48f, 38), pal, true))
        PyDump::dump_unit_by_type(1, 0);
    ImGui::SameLine(0, avail * 0.04f);
    if (CenterButton("保存 dump", ImVec2(avail * 0.48f, 38), pal, true))
        PyDump::save_buf("/sdcard/dwdump.txt", "/data/local/tmp/dwdump.txt");
    if (CenterButton("Dump 宿伞快照", ImVec2(ImGui::GetContentRegionAvail().x, 38), pal, true))
        g_umbrella_dump_request = true;
    if (CenterButton("Dump 宿伞字段(诊断)", ImVec2(-1, 38), pal, true))
        DumpUmbrellaCandidates();

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

    static char addr_buf[32] = "";
static bool g_hex_kb_open = false;
ImGui::InputText("对象地址 (0x...)", addr_buf, sizeof(addr_buf));
if (ImGui::IsItemClicked()) { g_kb_target = 1; g_hex_kb_open = true; }

// ---- 内置 hex 键盘 ----
if (g_hex_kb_open) {
    ImGui::Dummy(ImVec2(0, 4));
    auto kbtn = [&](const char* label, float w) -> bool {
        return ImGui::Button(label, ImVec2(w, 36));
    };
    auto append = [&](const char* s) {
        size_t cur = strlen(addr_buf);
        size_t add = strlen(s);
        if (cur + add < sizeof(addr_buf) - 1) {
            memcpy(addr_buf + cur, s, add);
            addr_buf[cur + add] = 0;
        }
    };

    float availKb = ImGui::GetContentRegionAvail().x;
    float bw = (availKb - 3 * 6) / 4.0f;   // 4 列，间距 6

    // 行 1
    if (kbtn("7", bw)) append("7");  ImGui::SameLine(0,6);
    if (kbtn("8", bw)) append("8");  ImGui::SameLine(0,6);
    if (kbtn("9", bw)) append("9");  ImGui::SameLine(0,6);
    if (kbtn("退格", bw)) { size_t n = strlen(addr_buf); if (n > 0) addr_buf[n-1] = 0; }

    // 行 2
    if (kbtn("4", bw)) append("4");  ImGui::SameLine(0,6);
    if (kbtn("5", bw)) append("5");  ImGui::SameLine(0,6);
    if (kbtn("6", bw)) append("6");  ImGui::SameLine(0,6);
    if (kbtn("清空", bw)) { addr_buf[0] = 0; }

    // 行 3
    if (kbtn("1", bw)) append("1");  ImGui::SameLine(0,6);
    if (kbtn("2", bw)) append("2");  ImGui::SameLine(0,6);
    if (kbtn("3", bw)) append("3");  ImGui::SameLine(0,6);
    if (kbtn("0x", bw)) append("0x");

    // 行 4
    if (kbtn("A", bw)) append("A");  ImGui::SameLine(0,6);
    if (kbtn("B", bw)) append("B");  ImGui::SameLine(0,6);
    if (kbtn("C", bw)) append("C");  ImGui::SameLine(0,6);
    if (kbtn("D", bw)) append("D");

    // 行 5
    if (kbtn("E", bw)) append("E");  ImGui::SameLine(0,6);
    if (kbtn("F", bw)) append("F");  ImGui::SameLine(0,6);
    if (kbtn("x", bw)) append("x");  ImGui::SameLine(0,6);
    if (kbtn("收起", bw)) g_hex_kb_open = false;
}
    if (CenterButton("Dump 指定地址", ImVec2(ImGui::GetContentRegionAvail().x, 38), pal, true)) {
        uint64_t a = strtoull(addr_buf, nullptr, 16);
        if (a) PyDump::dump_dict_at(a);
        else   PyDump::reset_status("地址无效");
    }


    ImGui::Dummy(ImVec2(0, 10));
    SectionLabel("输出", pal);
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
}

// ---------- 主窗口 End() 之后调用的额外窗口（弹窗类） ----------
void ExtraWindows(Theme::Palette& pal) {
    (void)pal;
    if (g_hunter_picker_open) {
        ImGui::SetNextWindowSize(ImVec2(720, 660), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(120, 120), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("选择监管 - 出刀前摇(s)##picker", &g_hunter_picker_open,
                         ImGuiWindowFlags_NoCollapse)) {
            ImGui::TextWrapped(
                "每把监管出刀速度不同。为每把监管单独设置\n"
                "「检测到出刀」→「点技能」之间的延迟。0 = 不延迟。");
            ImGui::Separator();

            std::string cur_hn = current_hunter_name();

            ImGui::BeginChild("##hunter_list", ImVec2(0, -90), false);
            for (int i = 0; i < g_boss_n; i++) {
                ImGui::PushID(i);
                bool is_current = (!cur_hn.empty() && cur_hn == g_boss_names[i]);
                ImGui::AlignTextToFramePadding();
                if (is_current)
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%s (当前)", g_boss_names[i]);
                else
                    ImGui::Text("%s", g_boss_names[i]);
                ImGui::SameLine(140);
                {
                    int wn = g_boss_windup_normal[i].load();
                    float fwn = wn / 1000.0f;
                    ImGui::SetNextItemWidth(110);
                    if (ImGui::InputFloat("##wn", &fwn, 0.01f, 0.1f, "%.3f"))
                        g_boss_windup_normal[i].store((int)(fwn * 1000.0f + 0.5f));
                    ImGui::SameLine();
                    int wc = g_boss_windup_charge[i].load();
                    float fwc = wc / 1000.0f;
                    ImGui::SetNextItemWidth(110);
                    if (ImGui::InputFloat("##wc", &fwc, 0.01f, 0.1f, "%.3f"))
                        g_boss_windup_charge[i].store((int)(fwc * 1000.0f + 0.5f));
                    ImGui::SameLine();
                    if (ImGui::SmallButton("清0")) {
                        g_boss_windup_normal[i].store(0);
                        g_boss_windup_charge[i].store(0);
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndChild();

            ImGui::Separator();
            float bw = (ImGui::GetContentRegionAvail().x - 12.0f) * 0.5f;
            if (ImGui::Button("保存到文件", ImVec2(bw, 40)))
                SaveHunterDelays();
            ImGui::SameLine();
            if (ImGui::Button("关闭", ImVec2(bw, 40)))
                g_hunter_picker_open = false;
        }
        ImGui::End();
    }
}

} // namespace UI_Pages

void Layout_tick_UI(bool *main_thread_flag) {
    static bool volume_thread_started = false;
    if (!volume_thread_started) {
        std::thread(VolumeKeyHide).detach();
        volume_thread_started = true;
    }

    static bool config_loaded = false;
    if (!config_loaded) {
        config_loaded = true;
        LoadConfig();
        atexit(SaveConfig);
    }

    px = static_cast<float>(native_window_screen_x) / 2;
py = static_cast<float>(native_window_screen_y) / 2;
AutoPallet::set_screen_size(native_window_screen_x, native_window_screen_y);

    if (!g_autoskill_worker_started.exchange(true)) {
    std::thread(AutoSkill_Worker).detach();
    PyAttack::set_attack_callback(OnAttackDetected);   // ★ 注册回调
}

static bool hunter_name_thread_started = false;
if (!hunter_name_thread_started) {
    hunter_name_thread_started = true;
    std::thread([](){
        while (true) {
            std::string hn = current_hunter_name();
            {
                std::lock_guard<std::mutex> lk(g_hunter_name_mtx);
                g_cached_hunter_name = hn;
            }
            usleep(500 * 1000);   // 500ms 一次
        }
    }).detach();
}

    Draw_Main(ImGui::GetForegroundDrawList());

    if (!show_window) return;

g_ui_state.dark = g_dark_theme;                 // 从持久化状态同步
MyUI::Draw(g_ui_state, ImGui::GetIO().DeltaTime);
g_dark_theme = g_ui_state.dark;                 // 用户可能在 UI 里切了
}
