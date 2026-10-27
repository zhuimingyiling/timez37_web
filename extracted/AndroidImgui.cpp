#include <algorithm>
#include <android/native_window.h>
#include "AndroidImgui.h"
#include "imgui.h"
#include "my_imgui_impl_android.h"
#include "stb_image.h"

bool AndroidImgui::Init_Render(ANativeWindow *window, float width, float height) {
    if (window == nullptr) {
        return false;
    }

    m_Window = window;
    m_Width = width;
    m_Height = height;

    ANativeWindow_acquire(window);
    Create();

    // 启动时由这里创建 Context；旋转重建时 drawBegin() 已创建 Context 并加载字体，
    // 因此不能无条件再次 CreateContext，否则会替换刚加载字体的 Context。
    IMGUI_CHECKVERSION();
    if (ImGui::GetCurrentContext() == nullptr) {
        ImGui::CreateContext();
    }
    ImGuiIO &io = ImGui::GetIO();

    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = {width, height};

    ImGui::StyleColorsLight();
    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(3);
    style.WindowRounding = 3.f;
    // ImGui 1.92 起 io.FontGlobalScale 被 style.FontScaleMain 取代
    style.FontScaleMain = 1.3f;
    My_ImGui_ImplAndroid_Init(window);

    Setup();
    return true;
}

void AndroidImgui::NewFrame(bool resize) {
    PrepareFrame(resize);
    My_ImGui_ImplAndroid_NewFrame(resize);
    ImGui::NewFrame();
}

// 由 my_ui.cpp 维护：UI 完全隐藏且静止时为 true（定义在 namespace MyUI 内）
namespace MyUI { extern bool g_ui_hidden_idle; }

#include <stdio.h>

void AndroidImgui::EndFrame() {
    ImGui::Render();

    ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data == nullptr) {
        return;  // 没有可渲染数据，直接返回
    }

    Render(draw_data);
}

void AndroidImgui::Shutdown() {
    for (auto &texture: m_Textures) {
        RemoveTexture(texture);
    }
    m_Textures.clear();
    PrepareShutdown();
    My_ImGui_ImplAndroid_Shutdown();

    // Shutdown 是 Context 的唯一销毁责任方；drawBegin() 不再重复销毁。
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui::DestroyContext();
    }

    Cleanup();
    if (m_Window != nullptr) {
        ANativeWindow_release(m_Window);
        m_Window = nullptr;
    }
}

BaseTexData *AndroidImgui::LoadTextureData(const std::function<unsigned char *(BaseTexData *)> &loadFunc) {
    BaseTexData tex_data{};

    tex_data.Channels = 4;
    unsigned char *image_data = loadFunc(&tex_data);
    if (image_data == nullptr)
        return nullptr;

    auto result = LoadTexture(&tex_data, image_data);

    stbi_image_free(image_data);

    if (result) {
        m_Textures.push_back(result);
    }
    return result;
}

BaseTexData *AndroidImgui::LoadTextureFromFile(const char *filepath) {
    return LoadTextureData([filepath](BaseTexData *tex_data) {
        return stbi_load(filepath, &tex_data->Width, &tex_data->Height, nullptr, tex_data->Channels);
    });
}

BaseTexData *AndroidImgui::LoadTextureFromMemory(void *data, int len) {
    return LoadTextureData([data, len](BaseTexData *tex_data) {
        return stbi_load_from_memory((const stbi_uc *) data, len, &tex_data->Width, &tex_data->Height, nullptr, tex_data->Channels);
    });
}

void AndroidImgui::DeleteTexture(BaseTexData *tex_data) {
    if (tex_data == nullptr) return;
    RemoveTexture(tex_data);
    auto it = std::find(m_Textures.begin(), m_Textures.end(), tex_data);
    if (it != m_Textures.end()) {
        m_Textures.erase(it);
    }
}
