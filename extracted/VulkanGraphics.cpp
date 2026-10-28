#include <cstdlib>
#include <dlfcn.h>
#include "VulkanGraphics.h"
#include "imgui_impl_vulkan.h"
#include "McpBridge/mcp_bridge.h"   // ImGui-MCP 截图
#include <vulkan/vulkan_android.h>
#include <android/native_window.h>
#include <unistd.h>

#ifndef NDEBUG

static void check_vk_result(VkResult err) {
    if (err == 0)
        return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0)
        abort();
}

#else

static void check_vk_result(VkResult err) {

}

#endif

// ============================================================
// 交换链图像的 usage
//
// ★ ImGui 1.92.9b 起 ImGui_ImplVulkanH_CreateOrResizeWindow() 多了
//   `VkImageUsageFlags image_usage` 参数（传 0 时默认只有
//   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT）。
//
//   这里额外加上 VK_IMAGE_USAGE_TRANSFER_SRC_BIT，是为了给
//   ImGui-MCP 的截图功能留出「把交换链图像拷到 host 可见 buffer」的能力。
//   （Android 的交换链图像默认不一定带 TRANSFER_SRC，必须先申请。）
// ============================================================
static const VkImageUsageFlags kSwapchainImageUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

// 与 VkApplicationInfo::apiVersion 保持一致
static const uint32_t kVulkanApiVersion = VK_API_VERSION_1_1;

VkPhysicalDevice VulkanGraphics::SetupVulkan_SelectPhysicalDevice() {
    uint32_t gpu_count;
    VkResult err = vkEnumeratePhysicalDevices(m_Instance, &gpu_count, nullptr);
    check_vk_result(err);
    IM_ASSERT(gpu_count > 0);

    ImVector<VkPhysicalDevice> gpus;
    gpus.resize(gpu_count);
    err = vkEnumeratePhysicalDevices(m_Instance, &gpu_count, gpus.Data);
    check_vk_result(err);

    // If a number >1 of GPUs got reported, find discrete GPU if present, or use first one available. This covers
    // most common cases (multi-gpu/integrated+dedicated graphics). Handling more complicated setups (multiple
    // dedicated GPUs) is out of scope of this sample.
    for (VkPhysicalDevice &device: gpus) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device, &properties);
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            return device;
    }

    // Use first GPU (Integrated) is a Discrete one is not available.
    if (gpu_count > 0)
        return gpus[0];
    return VK_NULL_HANDLE;
}

bool VulkanGraphics::Create() {
    if (InitVulkan() != 1) {
        fprintf(stderr, "Vulkan is not supported %s\n", dlerror());
        abort();
    }

    wd = std::make_unique<ImGui_ImplVulkanH_Window>();

    //为imgui加载vulkan函数
    // ★ ImGui 1.92.9b：LoadFunctions() 新增第一个参数 api_version
    void *libvulkan = dlopen("libvulkan.so", RTLD_NOW);
    ImGui_ImplVulkan_LoadFunctions(kVulkanApiVersion,
                                   [](const char *function_name, void *handle) -> PFN_vkVoidFunction {
                                       return reinterpret_cast<PFN_vkVoidFunction>(dlsym(handle, function_name));
                                   }, libvulkan);

    VkResult err;
    // Create Vulkan Instance
    {
        const char *instance_extensions[] = {
                "VK_KHR_surface",
                "VK_KHR_android_surface",
        };
        VkApplicationInfo appInfo = {
                .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                .pNext = nullptr,
                .pApplicationName = "pApplicationName",
                .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
                .pEngineName = "pEngineName",
                .engineVersion = VK_MAKE_VERSION(1, 0, 0),
                .apiVersion = VK_MAKE_VERSION(1, 1, 0),
        };
        VkInstanceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create_info.pApplicationInfo = &appInfo;
        create_info.enabledExtensionCount = sizeof(instance_extensions) / sizeof(instance_extensions[0]);
        create_info.ppEnabledExtensionNames = instance_extensions;
#ifdef IMGUI_VULKAN_DEBUG_REPORT
        // Enabling validation layers
            const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
            create_info.enabledLayerCount = 1;
            create_info.ppEnabledLayerNames = layers;

            // Enable debug report extension (we need additional storage, so we duplicate the user array to add our new extension to it)
            const char** extensions_ext = (const char**)malloc(sizeof(const char*) * (extensions_count + 1));
            memcpy(extensions_ext, extensions, extensions_count * sizeof(const char*));
            extensions_ext[extensions_count] = "VK_EXT_debug_report";
            create_info.enabledExtensionCount = extensions_count + 1;
            create_info.ppEnabledExtensionNames = extensions_ext;

            // Create Vulkan Instance
            err = vkCreateInstance(&create_info, g_Allocator, &g_Instance);
            check_vk_result(err);
            free(extensions_ext);

            // Get the function pointer (required for any extensions)
            auto vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(g_Instance, "vkCreateDebugReportCallbackEXT");
            IM_ASSERT(vkCreateDebugReportCallbackEXT != NULL);

            // Setup the debug report callback
            VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
            debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
            debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
            debug_report_ci.pfnCallback = debug_report;
            debug_report_ci.pUserData = NULL;
            err = vkCreateDebugReportCallbackEXT(g_Instance, &debug_report_ci, g_Allocator, &g_DebugReport);
            check_vk_result(err);
#else
        // Create Vulkan Instance without any debug feature
        err = vkCreateInstance(&create_info, m_Allocator, &m_Instance);
        check_vk_result(err);
        IM_UNUSED(m_DebugReport);
#endif
    }

    // Select Physical Device (GPU)
    m_PhysicalDevice = SetupVulkan_SelectPhysicalDevice();

    // Select graphics queue family
    {
        uint32_t count;
        vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &count, nullptr);
        VkQueueFamilyProperties *queues = (VkQueueFamilyProperties *) malloc(sizeof(VkQueueFamilyProperties) * count);
        vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &count, queues);
        for (uint32_t i = 0; i < count; i++)
            if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                m_QueueFamily = i;
                break;
            }
        free(queues);
        IM_ASSERT(m_QueueFamily != (uint32_t) -1);
    }

    // Create Logical Device (with 1 queue)
    {
        ImVector<const char *> device_extensions;
        device_extensions.push_back("VK_KHR_swapchain");

        // Enumerate physical device extension
        uint32_t properties_count;
        ImVector<VkExtensionProperties> properties;
        vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &properties_count, nullptr);
        properties.resize(properties_count);
        vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &properties_count, properties.Data);
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
        if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
            device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

        const float queue_priority[] = {1.0f};
        VkDeviceQueueCreateInfo queue_info[1] = {};
        queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info[0].queueFamilyIndex = m_QueueFamily;
        queue_info[0].queueCount = 1;
        queue_info[0].pQueuePriorities = queue_priority;
        VkDeviceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
        create_info.pQueueCreateInfos = queue_info;
        create_info.enabledExtensionCount = (uint32_t) device_extensions.Size;
        create_info.ppEnabledExtensionNames = device_extensions.Data;
        err = vkCreateDevice(m_PhysicalDevice, &create_info, m_Allocator, &m_Device);
        check_vk_result(err);
        vkGetDeviceQueue(m_Device, m_QueueFamily, 0, &m_Queue);
    }
    // Create Descriptor Pool
    {
        VkDescriptorPoolSize pool_sizes[] =
                {
                        {VK_DESCRIPTOR_TYPE_SAMPLER,                1000},
                        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
                        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          1000},
                        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1000},
                        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,   1000},
                        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,   1000},
                        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1000},
                        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1000},
                        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
                        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
                        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1000}
                };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
        pool_info.poolSizeCount = (uint32_t) IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        err = vkCreateDescriptorPool(m_Device, &pool_info, m_Allocator, &m_DescriptorPool);
        check_vk_result(err);
    }
    {
        // Create Window Surface
        VkSurfaceKHR surface;
        VkAndroidSurfaceCreateInfoKHR createInfo{
                .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
                .pNext = nullptr,
                .flags = 0,
                .window = m_Window};

        err = vkCreateAndroidSurfaceKHR(m_Instance, &createInfo, m_Allocator,
                                        &surface);
        check_vk_result(err);
        wd->Surface = surface;

        // Check for WSI support
        VkBool32 res;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_PhysicalDevice, m_QueueFamily, wd->Surface, &res);
        if (res != VK_TRUE) {
            fprintf(stderr, "Error no WSI support on physical device 0\n");
            exit(-1);
        }
        // Select Surface Format
        const VkFormat requestSurfaceImageFormat[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
                                                      VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM};
        const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
        wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(m_PhysicalDevice, wd->Surface,
                                                                  requestSurfaceImageFormat,
                                                                  (size_t) IM_ARRAYSIZE(requestSurfaceImageFormat),
                                                                  requestSurfaceColorSpace);

        // Select Present Mode
#ifdef APP_USE_UNLIMITED_FRAME_RATE
        VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR };
#else
        VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_FIFO_KHR};
#endif
        wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(m_PhysicalDevice, wd->Surface, &present_modes[0],
                                                              IM_ARRAYSIZE(present_modes));
        //printf("[vulkan] Selected PresentMode = %d\n", wd->PresentMode);

        // Create SwapChain, RenderPass, Framebuffer, etc.
        IM_ASSERT(m_MinImageCount >= 2);
        ImGui_ImplVulkanH_CreateOrResizeWindow(m_Instance, m_PhysicalDevice, m_Device, wd.get(), m_QueueFamily,
                                               m_Allocator,
                                               (int) m_Width, (int) m_Height, m_MinImageCount,
                                               kSwapchainImageUsage);
    }

return true;
}

void VulkanGraphics::Setup() {
    // ★ ImGui 1.92.9b 的 ImGui_ImplVulkan_InitInfo 变化：
    //   - 新增 ApiVersion（必须填，否则后端无法正确加载函数）
    //   - RenderPass / Subpass / MSAASamples / PipelineRenderingCreateInfo
    //     已被收进 PipelineInfoMain（ImGui_ImplVulkan_PipelineInfo）
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.ApiVersion = kVulkanApiVersion;
    init_info.Instance = m_Instance;
    init_info.PhysicalDevice = m_PhysicalDevice;
    init_info.Device = m_Device;
    init_info.QueueFamily = m_QueueFamily;
    init_info.Queue = m_Queue;
    init_info.PipelineCache = m_PipelineCache;
    init_info.DescriptorPool = m_DescriptorPool;
    init_info.MinImageCount = m_MinImageCount;
    init_info.ImageCount = wd->ImageCount;
    init_info.Allocator = m_Allocator;
    init_info.CheckVkResultFn = check_vk_result;

    init_info.PipelineInfoMain.RenderPass = wd->RenderPass;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&init_info);
}

void VulkanGraphics::PrepareFrame(bool resize) {
    int width  = ANativeWindow_getWidth(m_Window);
    int height = ANativeWindow_getHeight(m_Window);

    // ★ 首次也走重建分支（m_LastWidth==0 视为"尺寸变了"）
    bool size_changed = (width > 0 && height > 0) &&
                    ((int)wd->Width != width || (int)wd->Height != height);

    if (m_SwapChainRebuild || resize || size_changed) {
    if (width > 0 && height > 0 && size_changed) {
        if (m_LastWidth != 0) {
            usleep(300000);
        }
        //   2026-10-27：重建前先让 device 空闲。旋转/异形屏/折叠屏
        //   切换时，上一帧可能刚提交完还没执行，某些驱动在 image
        //   in-flight 时回收 swapchain 会直接 SIGSEGV。
        vkDeviceWaitIdle(m_Device);
        m_LastWidth = width;
        m_LastHeight = height;
        
            ImGui_ImplVulkan_SetMinImageCount(m_MinImageCount);
            ImGui_ImplVulkanH_CreateOrResizeWindow(m_Instance, m_PhysicalDevice, m_Device, wd.get(),
                                                   m_QueueFamily, m_Allocator,
                                                   width, height, m_MinImageCount,
                                                   kSwapchainImageUsage);
            wd->FrameIndex = 0;
            m_SwapChainRebuild = false;
            printf("[VK] swapchain 重建 %dx%d\n", width, height);
            fflush(stdout);
        }
    }
    ImGui_ImplVulkan_NewFrame();
}


// ★★ 呈现管线分段计时（2026-10-10）
//   主循环 EndFrame 实测 6.1~6.3ms/帧 ≈ 单核 37%，是当前最大的可测项。
//   这里把它拆成五段：申请图像 / 等栅栏 / 记录绘制数据 / 提交 / 呈现，
//   看 6.2ms 究竟花在驱动的哪一步 —— 不然又只能猜。
static inline long long vk_cpu_ns() {
    struct timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
static long long g_vk_acc[5] = {0,0,0,0,0};
static long long g_vk_prev = 0;
static int       g_vk_n = 0;

void VulkanGraphics::Render(ImDrawData *drawData) {
    long long _v0 = vk_cpu_ns();
    VkResult err;

    VkSemaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    err = vkAcquireNextImageKHR(m_Device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE,
                                &wd->FrameIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR /*|| err == VK_SUBOPTIMAL_KHR*/) {
        m_SwapChainRebuild = true;
        return;
    }
    //check_vk_result(err);

    ImGui_ImplVulkanH_Frame *fd = &wd->Frames[wd->FrameIndex];
    {
        err = vkWaitForFences(m_Device, 1, &fd->Fence, VK_TRUE,
                              UINT64_MAX);    // wait indefinitely instead of periodically checking
        check_vk_result(err);

        err = vkResetFences(m_Device, 1, &fd->Fence);
        check_vk_result(err);
    }
    {
        err = vkResetCommandPool(m_Device, fd->CommandPool, 0);
        check_vk_result(err);
        VkCommandBufferBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
        check_vk_result(err);
    }
    {
        //透明 默认已经是0了
        //memset(wd->ClearValue.color.float32, 0, sizeof(wd->ClearValue.color.float32));
        VkRenderPassBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = wd->RenderPass;
        info.framebuffer = fd->Framebuffer;
        info.renderArea.extent.width = wd->Width;
        info.renderArea.extent.height = wd->Height;
        info.clearValueCount = 1;
        info.pClearValues = &wd->ClearValue;
        vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
    }

    // Record dear imgui primitives into command buffer
    long long _v1 = vk_cpu_ns();   // 等栅栏结束，开始记录绘制数据
    ImGui_ImplVulkan_RenderDrawData(drawData, fd->CommandBuffer);

    // ========================================================
    // ★ ImGui-MCP 截图：在 render pass 结束后把交换链图像拷到 host 可见 buffer。
    //   只在 AI 请求截图的那一帧做（McpBridge::WantsCapture()），平时零开销。
    //   交换链创建时已经带了 VK_IMAGE_USAGE_TRANSFER_SRC_BIT（kSwapchainImageUsage）。
    // ========================================================
    long long _v2 = vk_cpu_ns();   // 绘制数据记录完毕
    const bool do_capture = McpBridge::WantsCapture() &&
                            EnsureCaptureBuffer((size_t)wd->Width * wd->Height * 4);

    // Submit command buffer
    vkCmdEndRenderPass(fd->CommandBuffer);

    if (do_capture) {
        // 呈现布局 -> 传输源布局
        VkImageMemoryBarrier to_src = {};
        to_src.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_src.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        to_src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        to_src.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        to_src.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        to_src.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_src.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_src.image = fd->Backbuffer;
        to_src.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_src.subresourceRange.levelCount = 1;
        to_src.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(fd->CommandBuffer,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &to_src);

        VkBufferImageCopy region = {};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;        // 0 = 紧凑排列
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {(uint32_t)wd->Width, (uint32_t)wd->Height, 1};
        vkCmdCopyImageToBuffer(fd->CommandBuffer, fd->Backbuffer,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               m_CapBuffer, 1, &region);

        // 传回呈现布局，否则下一帧/呈现会挂
        VkImageMemoryBarrier to_present = to_src;
        to_present.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        to_present.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(fd->CommandBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &to_present);
    }

    {
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &image_acquired_semaphore;
        info.pWaitDstStageMask = &wait_stage;
        info.commandBufferCount = 1;
        info.pCommandBuffers = &fd->CommandBuffer;
        info.signalSemaphoreCount = 1;
        info.pSignalSemaphores = &render_complete_semaphore;


err = vkEndCommandBuffer(fd->CommandBuffer);
        check_vk_result(err);
    long long _v3a = vk_cpu_ns();  // 命令缓冲结束，准备提交
        err = vkQueueSubmit(m_Queue, 1, &info, fd->Fence);
        check_vk_result(err);
    }

    long long _v3 = vk_cpu_ns();   // 提交完成
    // 截图：等这一帧真的执行完，再把 buffer 映射出来交给桥去编码
    if (do_capture) {
        err = vkWaitForFences(m_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
        check_vk_result(err);

        void *mapped = nullptr;
        VkResult merr = vkMapMemory(m_Device, m_CapMemory, 0, (VkDeviceSize)m_CapSize, 0, &mapped);
        if (merr == VK_SUCCESS && mapped) {
            VkMappedMemoryRange range = {};
            range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory = m_CapMemory;
            range.offset = 0;
            range.size = VK_WHOLE_SIZE;
            vkInvalidateMappedMemoryRanges(m_Device, 1, &range);

            McpBridge::SubmitCapture(mapped, wd->Width, wd->Height,
                                     (int)wd->SurfaceFormat.format,
                                     wd->Width * 4);

            vkUnmapMemory(m_Device, m_CapMemory);
        } else {
            printf("[MCP] vkMapMemory 失败: %d\n", (int)merr);
            fflush(stdout);
            McpBridge::SubmitCapture(nullptr, 0, 0, 0, 0);
        }
    }


    {
        if (m_SwapChainRebuild)
            return;
        VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
        VkPresentInfoKHR info = {};
        info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &render_complete_semaphore;
        info.swapchainCount = 1;
        info.pSwapchains = &wd->Swapchain;
        info.pImageIndices = &wd->FrameIndex;
        VkResult err = vkQueuePresentKHR(m_Queue, &info);
        const long long _v4 = vk_cpu_ns();   // 呈现完成
        g_vk_acc[0] += _v1 - _v0;      // 申请图像 + 等栅栏
        g_vk_acc[1] += _v2 - _v1;      // 记录绘制数据
        g_vk_acc[2] += 0;              // （收尾并入提交段）
        g_vk_acc[3] += _v3 - _v2;      // 结束命令缓冲 + 提交
        g_vk_acc[4] += _v4 - _v3;      // 呈现
        if (++g_vk_n >= 60) {
            printf("[呈现] 每帧: 申请+等待=%.2f 记录=%.2f 提交=%.2f 呈现=%.2f ms | 合计=%.2f\n",
                   g_vk_acc[0]/60.0/1e6, g_vk_acc[1]/60.0/1e6,
                   g_vk_acc[3]/60.0/1e6, g_vk_acc[4]/60.0/1e6,
                   (g_vk_acc[0]+g_vk_acc[1]+g_vk_acc[3]+g_vk_acc[4])/60.0/1e6);
            fflush(stdout);
            for (int k = 0; k < 5; k++) g_vk_acc[k] = 0;
            g_vk_n = 0;
        }
        if (err == VK_ERROR_OUT_OF_DATE_KHR /*|| err == VK_SUBOPTIMAL_KHR*/) {
            m_SwapChainRebuild = true;
            return;
        }
        //check_vk_result(err);
        wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount; // Now we can use the next set of semaphores
    }
}


void VulkanGraphics::PrepareShutdown() {
    printf("[VK-SD] P1 vkDeviceWaitIdle 开始\n");
    fflush(stdout);

    VkResult err = vkDeviceWaitIdle(m_Device);

    printf("[VK-SD] P2 vkDeviceWaitIdle 返回: %d\n", (int)err);
    fflush(stdout);

    check_vk_result(err);

    printf("[VK-SD] P3 ImGui_ImplVulkan_Shutdown 开始\n");
    fflush(stdout);

    ImGui_ImplVulkan_Shutdown();

    printf("[VK-SD] P4 ImGui_ImplVulkan_Shutdown 完成\n");
    fflush(stdout);
}

void VulkanGraphics::Cleanup() {
    printf("[VK-SD] C1 DestroyCaptureBuffer 开始\n");
    fflush(stdout);

    DestroyCaptureBuffer();

    printf("[VK-SD] C2 DestroyCaptureBuffer 完成\n");
    fflush(stdout);


    printf("[VK-SD] C3 DestroyWindow 开始\n");
    fflush(stdout);

    ImGui_ImplVulkanH_DestroyWindow(
        m_Instance, m_Device, wd.get(), m_Allocator
    );

    printf("[VK-SD] C4 DestroyWindow 完成\n");
    fflush(stdout);


    printf("[VK-SD] C5 DestroyDescriptorPool 开始\n");
    fflush(stdout);

    vkDestroyDescriptorPool(
        m_Device, m_DescriptorPool, m_Allocator
    );

    printf("[VK-SD] C6 DestroyDescriptorPool 完成\n");
    fflush(stdout);


#ifdef APP_USE_VULKAN_DEBUG_REPORT
    printf("[VK-SD] C7 销毁 DebugReport 开始\n");
    fflush(stdout);

    auto vkDestroyDebugReportCallbackEXT =
        (PFN_vkDestroyDebugReportCallbackEXT)
        vkGetInstanceProcAddr(
            g_Instance,
            "vkDestroyDebugReportCallbackEXT"
        );

    vkDestroyDebugReportCallbackEXT(
        g_Instance, g_DebugReport, g_Allocator
    );

    printf("[VK-SD] C8 销毁 DebugReport 完成\n");
    fflush(stdout);
#endif

    printf("[VK-SD] C9 vkDestroyDevice 开始\n");
    fflush(stdout);

    vkDestroyDevice(m_Device, m_Allocator);

    printf("[VK-SD] C10 vkDestroyDevice 完成\n");
    fflush(stdout);

    printf("[VK-SD] C11 vkDestroyInstance 开始\n");
    fflush(stdout);

    vkDestroyInstance(m_Instance, m_Allocator);

    printf("[VK-SD] C12 vkDestroyInstance 完成\n");
    fflush(stdout);

    m_SwapChainRebuild = false;
}

// ============================================================
// ★ ImGui-MCP 截图读回缓冲
// ============================================================
bool VulkanGraphics::EnsureCaptureBuffer(size_t bytes) {
    if (bytes == 0) return false;
    if (m_CapBuffer != VK_NULL_HANDLE && m_CapSize >= bytes) return true;

    DestroyCaptureBuffer();

    VkBufferCreateInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = (VkDeviceSize)bytes;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult err = vkCreateBuffer(m_Device, &bi, m_Allocator, &m_CapBuffer);
    if (err != VK_SUCCESS) { m_CapBuffer = VK_NULL_HANDLE; return false; }

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(m_Device, m_CapBuffer, &req);

    VkMemoryAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == 0xFFFFFFFFu) {
        // 退一步：只要 host 可见（后面会手动 invalidate）
        ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    }
    err = vkAllocateMemory(m_Device, &ai, m_Allocator, &m_CapMemory);
    if (err != VK_SUCCESS) { DestroyCaptureBuffer(); return false; }

    err = vkBindBufferMemory(m_Device, m_CapBuffer, m_CapMemory, 0);
    if (err != VK_SUCCESS) { DestroyCaptureBuffer(); return false; }

    m_CapSize = bytes;
    printf("[MCP] 截图缓冲已创建 %zu 字节\n", bytes);
    fflush(stdout);
    return true;
}

void VulkanGraphics::DestroyCaptureBuffer() {
    if (m_CapBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_Device, m_CapBuffer, m_Allocator);
        m_CapBuffer = VK_NULL_HANDLE;
    }
    if (m_CapMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_Device, m_CapMemory, m_Allocator);
        m_CapMemory = VK_NULL_HANDLE;
    }
    m_CapSize = 0;
}

// Helper function to find Vulkan memory type bits. See ImGui_ImplVulkan_MemoryType() in imgui_impl_vulkan.cpp
uint32_t VulkanGraphics::findMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties) {    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &mem_properties);

    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++)
        if ((type_filter & (1 << i)) && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;

    return 0xFFFFFFFF; // Unable to find memoryType
}

BaseTexData *VulkanGraphics::LoadTexture(BaseTexData *tex, void *pixel_data) {
    auto *tex_data = new VulkanTextureData();
    tex_data->Width = tex->Width;
    tex_data->Height = tex->Height;
    tex_data->Channels = tex->Channels;

    // tex_data->UserData=
    // Calculate allocation size (in number of bytes)
    size_t image_size = tex_data->Width * tex_data->Height * tex_data->Channels;

    VkResult err;
    // Create the Vulkan image.
    {
        VkImageCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = VK_FORMAT_R8G8B8A8_UNORM;
        info.extent.width = tex_data->Width;
        info.extent.height = tex_data->Height;
        info.extent.depth = 1;
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        err = vkCreateImage(m_Device, &info, m_Allocator, &tex_data->Image);
        check_vk_result(err);
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(m_Device, tex_data->Image, &req);
        VkMemoryAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = req.size;
        alloc_info.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        err = vkAllocateMemory(m_Device, &alloc_info, m_Allocator, &tex_data->ImageMemory);
        check_vk_result(err);
        err = vkBindImageMemory(m_Device, tex_data->Image, tex_data->ImageMemory, 0);
        check_vk_result(err);
    }

    // Create the Image View
    {
        VkImageViewCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = tex_data->Image;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = VK_FORMAT_R8G8B8A8_UNORM;
        info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.layerCount = 1;
        err = vkCreateImageView(m_Device, &info, m_Allocator, &tex_data->ImageView);
        check_vk_result(err);
    }

    // Create Sampler
    // ★ ImGui 1.92.9b：ImGui_ImplVulkan_AddTexture() 去掉了 VkSampler 参数
    //   （后端内部统一用 linear + nearest 两个自带 sampler）。
    //   所以这里不再需要创建 sampler，也就不再需要销毁它。
    //   VulkanTextureData::Sampler 字段保留但恒为 VK_NULL_HANDLE。

    // Create Descriptor Set using ImGUI's implementation
    tex_data->DS = (void *) ImGui_ImplVulkan_AddTexture(tex_data->ImageView,
                                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Create Upload Buffer
    {
        VkBufferCreateInfo buffer_info = {};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = image_size;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        err = vkCreateBuffer(m_Device, &buffer_info, m_Allocator, &tex_data->UploadBuffer);
        check_vk_result(err);
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(m_Device, tex_data->UploadBuffer, &req);
        VkMemoryAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = req.size;
        alloc_info.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        err = vkAllocateMemory(m_Device, &alloc_info, m_Allocator, &tex_data->UploadBufferMemory);
        check_vk_result(err);
        err = vkBindBufferMemory(m_Device, tex_data->UploadBuffer, tex_data->UploadBufferMemory, 0);
        check_vk_result(err);
    }

    // Upload to Buffer:
    {
        void *map = NULL;
        err = vkMapMemory(m_Device, tex_data->UploadBufferMemory, 0, image_size, 0, &map);
        check_vk_result(err);
        memcpy(map, pixel_data, image_size);
        VkMappedMemoryRange range[1] = {};
        range[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range[0].memory = tex_data->UploadBufferMemory;
        range[0].size = image_size;
        err = vkFlushMappedMemoryRanges(m_Device, 1, range);
        check_vk_result(err);
        vkUnmapMemory(m_Device, tex_data->UploadBufferMemory);
    }

    // Create a command buffer that will perform following steps when hit in the command queue.
    // TODO: this works in the example, but may need input if this is an acceptable way to access the pool/create the command buffer.
    VkCommandPool command_pool = wd->Frames[wd->FrameIndex].CommandPool;
    VkCommandBuffer command_buffer;
    {
        VkCommandBufferAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandPool = command_pool;
        alloc_info.commandBufferCount = 1;

        err = vkAllocateCommandBuffers(m_Device, &alloc_info, &command_buffer);
        check_vk_result(err);

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        err = vkBeginCommandBuffer(command_buffer, &begin_info);
        check_vk_result(err);
    }

    // Copy to Image
    {
        VkImageMemoryBarrier copy_barrier[1] = {};
        copy_barrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        copy_barrier[0].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        copy_barrier[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        copy_barrier[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        copy_barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        copy_barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        copy_barrier[0].image = tex_data->Image;
        copy_barrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy_barrier[0].subresourceRange.levelCount = 1;
        copy_barrier[0].subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL,
                             0,
                             NULL, 1, copy_barrier);

        VkBufferImageCopy region = {};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent.width = tex_data->Width;
        region.imageExtent.height = tex_data->Height;
        region.imageExtent.depth = 1;
        vkCmdCopyBufferToImage(command_buffer, tex_data->UploadBuffer, tex_data->Image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier use_barrier[1] = {};
        use_barrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        use_barrier[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        use_barrier[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        use_barrier[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        use_barrier[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        use_barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        use_barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        use_barrier[0].image = tex_data->Image;
        use_barrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        use_barrier[0].subresourceRange.levelCount = 1;
        use_barrier[0].subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0,
                             0, NULL, 0, NULL, 1, use_barrier);
    }

    // End command buffer
    {
        VkSubmitInfo end_info = {};
        end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        end_info.commandBufferCount = 1;
        end_info.pCommandBuffers = &command_buffer;
        err = vkEndCommandBuffer(command_buffer);
        check_vk_result(err);
        err = vkQueueSubmit(m_Queue, 1, &end_info, VK_NULL_HANDLE);
        check_vk_result(err);
        err = vkDeviceWaitIdle(m_Device);
        check_vk_result(err);
    }

    return tex_data;
}

void VulkanGraphics::RemoveTexture(BaseTexData *tex) {
    auto *tex_data = (VulkanTextureData *) (tex);
    vkFreeMemory(m_Device, tex_data->UploadBufferMemory, nullptr);
    vkDestroyBuffer(m_Device, tex_data->UploadBuffer, nullptr);
    // 1.92.9b 起后端不再保留用户传入的 sampler，这里也不再创建
    if (tex_data->Sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_Device, tex_data->Sampler, nullptr);
        tex_data->Sampler = VK_NULL_HANDLE;
    }
    vkDestroyImageView(m_Device, tex_data->ImageView, nullptr);
    vkDestroyImage(m_Device, tex_data->Image, nullptr);
    vkFreeMemory(m_Device, tex_data->ImageMemory, nullptr);
    ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet) tex_data->DS);
    delete tex_data;
}

