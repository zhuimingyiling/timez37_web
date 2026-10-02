//微验网络验证//
//如果是AIDE编译jni，请将原main.cpp删除，将此注入好的文件改成main.cpp
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>
#include <fstream>
#include <string.h>
#include <time.h>
#include <malloc.h>
#include <iostream>
#include <fstream>
#include <sys/prctl.h>

#include<iostream>
#include<ctime>
using namespace std;
#include "Android_draw/WenxinVerify.h"
#include "Android_draw/kerneldriver-kma.hpp"
#include <linux/input.h>
#include <sys/ioctl.h>

    // ... 后面原有的初始化代码不变
#include "draw.h"    //绘制套
#include "AndroidImgui.h"     //创建绘制套
#include "GraphicsManager.h" //获取 当前渲染模式
#include "Android_draw/timer.h"
#include "SoHookIntegration.h"
#include "build_entropy.h"

extern int native_window_screen_x;
extern int native_window_screen_y;

timer DrawFPS;
float fps = 60;
long int value1,value2,value3;

// 编译期随机熵实际被读取一次，防止链接器/优化器把整段常量数组当死数据丢掉，
// 顺带让每次编译产物字节内容不同（防哈希黑名单），本身不影响任何逻辑。
static volatile unsigned g_entropy_sink = 0;
static void touch_build_entropy() {
    for (unsigned char b : g_build_entropy) {
        g_entropy_sink += b;
    }
    g_entropy_sink += static_cast<unsigned>(g_build_tag[0]);
}

// 运行时把 /proc/<pid>/comm 改成常见系统/内核线程名之一，
// 躲避"进程启动后再扫描进程名"这类动态检测；跟编译期改 LOCAL_MODULE 是两道独立的防线。
static void spoof_process_name() {
    static const char *kDisguiseNames[] = {
        "kworker/u8:3",
        "kworker/0:2",
        "logd.auditd",
        "mdnsd",
        "vndservicemgr",
        "hwservicemanager",
        "wifi_forward",
        "statsd",
    };
    srand(static_cast<unsigned>(time(nullptr)) ^ static_cast<unsigned>(getpid()));
    const char *name = kDisguiseNames[rand() % (sizeof(kDisguiseNames) / sizeof(kDisguiseNames[0]))];
    prctl(PR_SET_NAME, name);
}




void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        exit(1);
    }
    if (pid > 0) {
        exit(0); // 父进程退出，子进程继续
    }

    if (setsid() < 0) {
        exit(1);
    }

    spoof_process_name();

    if (chdir("/") < 0) {
        exit(1);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // stdin → /dev/null
    open("/dev/null", O_RDONLY);

    // stdout → 日志文件（追加写入）
    int fd1 = open("/data/local/tmp/hack_stdout.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
int fd2 = open("/data/local/tmp/hack_stderr.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
// fd1 应该 == 1，fd2 应该 == 2；如果不等就 dup2
if (fd1 != 1 && fd1 >= 0) { dup2(fd1, 1); close(fd1); }
if (fd2 != 2 && fd2 >= 0) { dup2(fd2, 2); close(fd2); }
}


int main(int argc, char *argv[]) {
    Wenxin::RunVerifyOrExit();
    daemonize();          // ← fork 先做完

    InitKmaDriver();      // ← 这一步才真正碰 KMA
    touch_build_entropy();
    // 发布版本: 注入功能已停用
    // SoHook::StartListeners();

	   
    value1 = 970061201;
    value2 = 16384;
    value3 = 257;
    
    ::graphics = GraphicsManager::getGraphicsInterface(GraphicsManager::VULKAN);//绘图方式

    //获取屏幕信息    
    ::screen_config(); 

// ↓↓↓ 用这个替换原来的 4 行 native_window_screen_x/y/abs_ScreenX/Y 赋值
// displayInfo.width/height 一般是自然方向尺寸(竖屏手机=1080x1920，平板可能反过来)
// 用 min/max 拆短长边，再按 orientation 决定当前屏幕是竖是横
{
    int a = ::displayInfo.width, b = ::displayInfo.height;
    int short_side = a < b ? a : b;
    int long_side  = a < b ? b : a;
    // Android 标准：ROTATION_0=0(自然方向), 1(旋转90°), 2(180°), 3(270°)
    // 手机自然方向=竖屏，所以偶数=竖屏、奇数=横屏
    bool is_portrait = ((::displayInfo.orientation % 2) == 0);
    ::native_window_screen_x = is_portrait ? short_side : long_side;
    ::native_window_screen_y = is_portrait ? long_side : short_side;
    ::abs_ScreenX = ::native_window_screen_x;
    ::abs_ScreenY = ::native_window_screen_y;

    printf("[init] disp=%dx%d orient=%d -> win %dx%d (portrait=%d)\n",
           ::displayInfo.width, ::displayInfo.height, ::displayInfo.orientation,
           ::native_window_screen_x, ::native_window_screen_y, is_portrait);
    fflush(stdout);
}
// ↑↑↑ 替换结束

   // GetPKG();
    
    ::window = android::ANativeWindowCreator::Create("new_edition", native_window_screen_x, native_window_screen_y, permeate_record);
    graphics->Init_Render(::window, native_window_screen_x, native_window_screen_y);
    
    Touch::Init({(float)::abs_ScreenX, (float)::abs_ScreenY}, false);
    Touch::setOrientation(displayInfo.orientation);
    
    std::thread(read_thread, value1, value2, value3).detach();
    
	DrawFPS.SetFps(fps);
	DrawFPS.AotuFPS_init();
	DrawFPS.setAffinity();
    
    ::init_My_drawdata(); //初始化绘制数据
    
    static bool flag = true;
    while (flag) {
        drawBegin();
        graphics->NewFrame();        
        Layout_tick_UI(&flag);
        graphics->EndFrame();
        // 字体纹理上传后 CPU 侧的图集副本(Alpha8 4MB + RGBA32 16MB)就没用了。
        // 必须在 EndFrame 之后(NewFrame~EndFrame 期间图集是锁住的)；
        // 按状态判断而不是只做一次：ImGui 上下文重建(录屏穿透那条路径)后会重新生成一份
        ImFontAtlas *atlas = ImGui::GetIO().Fonts;
        if (atlas->TexID && (atlas->TexPixelsAlpha8 || atlas->TexPixelsRGBA32))
            atlas->ClearTexData();
        DrawFPS.SetFps(fps);
	    DrawFPS.AotuFPS();
    }
    
    graphics->Shutdown();
    // 发布版本: 注入功能已停用
    // SoHook::StopListeners();
    android::ANativeWindowCreator::Destroy(::window);
    return 0;
}
