#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Failed: %s\n", #x); exit(1); } } while (0)
#define VK(x) do { VkResult r = (x); if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) { fprintf(stderr, "%s: %d\n", #x, r); exit(1); } } while (0)
#define LIMIT 8
#define SLOTS 3

struct swap {
    VkSwapchainKHR handle;
    uint32_t count;
    VkImage images[LIMIT];
    VkSemaphore complete[LIMIT];
    int used[LIMIT];
};

static VkDevice device;
static VkPhysicalDevice physical;
static VkSurfaceKHR surface;
static VkQueue queue;

static struct swap make_swap(VkSwapchainKHR old, VkPresentModeKHR mode)
{
    VkSurfaceCapabilitiesKHR caps;
    VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps));
    CHECK(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    uint32_t n = 0;
    VK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &n, NULL));
    VkSurfaceFormatKHR *formats = calloc(n, sizeof(*formats));
    CHECK(formats);
    VK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &n, formats));
    CHECK(n > 0);
    VkSurfaceFormatKHR format = formats[0];
    for (uint32_t i = 0; i < n; ++i)
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) format = formats[i];
    free(formats);
    uint32_t count = caps.minImageCount + 1;
    if (caps.maxImageCount && count > caps.maxImageCount) count = caps.maxImageCount;
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) extent = (VkExtent2D){640, 480};
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & alpha))
        alpha = caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha;
    VkSwapchainCreateInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface, .minImageCount = count,
        .imageFormat = format.format, .imageColorSpace = format.colorSpace,
        .imageExtent = extent, .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform, .compositeAlpha = alpha,
        .presentMode = mode, .clipped = VK_TRUE, .oldSwapchain = old,
    };
    struct swap result = {0};
    VK(vkCreateSwapchainKHR(device, &info, NULL, &result.handle));
    result.count = LIMIT;
    VK(vkGetSwapchainImagesKHR(device, result.handle, &result.count, result.images));
    VkSemaphoreCreateInfo sem = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (uint32_t i = 0; i < result.count; ++i)
        VK(vkCreateSemaphore(device, &sem, NULL, &result.complete[i]));
    printf("Created mode=%d images=%u extent=%ux%u\n", mode, result.count, extent.width, extent.height);
    return result;
}

static void destroy_swap(struct swap *swap)
{
    vkDestroySwapchainKHR(device, swap->handle, NULL);
    for (uint32_t i = 0; i < swap->count; ++i)
        vkDestroySemaphore(device, swap->complete[i], NULL);
}

static void check_display(unsigned color)
{
    for (int i = 0; i < 100; ++i) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }
    int fd = open("/dev/fb0", O_RDONLY);
    CHECK(fd >= 0);
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    CHECK(ioctl(fd, FBIOGET_VSCREENINFO, &var) == 0);
    CHECK(ioctl(fd, FBIOGET_FSCREENINFO, &fix) == 0);
    CHECK(var.bits_per_pixel == 32 && var.xres == 640 && var.yres == 480);
    const struct fb_bitfield channels[] = {var.red, var.green, var.blue};
    unsigned good = 0;
    for (int y = 1; y <= 5; ++y) {
        for (int x = 1; x <= 5; ++x) {
            uint32_t pixel;
            off_t offset = (var.yoffset + y * var.yres / 6) * fix.line_length
                + (var.xoffset + x * var.xres / 6) * 4;
            CHECK(pread(fd, &pixel, sizeof(pixel), offset) == sizeof(pixel));
            int matches = 1;
            for (unsigned c = 0; c < 3; ++c) {
                unsigned max = (1u << channels[c].length) - 1;
                unsigned value = (pixel >> channels[c].offset) & max;
                if (c == color ? value < max * 9 / 10 : value > max / 10)
                    matches = 0;
            }
            good += matches;
        }
    }
    close(fd);
    printf("Final frame color=%u physical matches=%u/25\n", color, good);
    CHECK(good == 25);
}

int main(void)
{
    setbuf(stdout, NULL);
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_Window *window = SDL_CreateWindow("Presentation check", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, 640, 480, SDL_WINDOW_VULKAN | SDL_WINDOW_FULLSCREEN);
    CHECK(window);
    SDL_ShowCursor(SDL_DISABLE);
    unsigned extension_count = LIMIT;
    const char *extensions[LIMIT];
    CHECK(SDL_Vulkan_GetInstanceExtensions(window, &extension_count, extensions));
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo instance_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app, .enabledExtensionCount = extension_count, .ppEnabledExtensionNames = extensions};
    VkInstance instance;
    VK(vkCreateInstance(&instance_info, NULL, &instance));
    CHECK(SDL_Vulkan_CreateSurface(window, instance, &surface));
    uint32_t count = 1;
    VK(vkEnumeratePhysicalDevices(instance, &count, &physical));
    count = LIMIT;
    VkQueueFamilyProperties families[LIMIT];
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families);
    uint32_t family = UINT32_MAX;
    for (uint32_t i = 0; i < count; ++i) {
        VkBool32 present = 0;
        VK(vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, surface, &present));
        if (present && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { family = i; break; }
    }
    CHECK(family != UINT32_MAX);
    float priority = 1;
    VkDeviceQueueCreateInfo qinfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
    const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dinfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qinfo,
        .enabledExtensionCount = 1, .ppEnabledExtensionNames = device_extensions};
    VK(vkCreateDevice(physical, &dinfo, NULL, &device));
    vkGetDeviceQueue(device, family, 0, &queue);
    count = LIMIT;
    VkPresentModeKHR modes[LIMIT];
    VK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, modes));
    int mailbox = 0;
    for (uint32_t i = 0; i < count; ++i) mailbox |= modes[i] == VK_PRESENT_MODE_MAILBOX_KHR;
    VkCommandPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = family};
    VkCommandPool pool;
    VK(vkCreateCommandPool(device, &pool_info, NULL, &pool));
    VkCommandBuffer commands[SLOTS];
    VkCommandBufferAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = SLOTS};
    VK(vkAllocateCommandBuffers(device, &allocate, commands));
    VkSemaphore acquire[SLOTS];
    VkFence fences[SLOTS];
    VkSemaphoreCreateInfo sem = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    for (int i = 0; i < SLOTS; ++i) {
        VK(vkCreateSemaphore(device, &sem, NULL, &acquire[i]));
        VK(vkCreateFence(device, &fence_info, NULL, &fences[i]));
    }
    struct swap swap = {0};
    unsigned serial = 0;
    for (unsigned generation = 0; generation < 4; ++generation) {
        VkPresentModeKHR mode = mailbox && generation % 2 ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_FIFO_KHR;
        struct swap next = make_swap(swap.handle, mode);
        if (swap.handle) destroy_swap(&swap);
        swap = next;
        unsigned frames = generation < 3 ? 28 : 25;
        for (unsigned frame = 0; frame < frames; ++frame, ++serial) {
            unsigned slot = serial % SLOTS;
            VK(vkWaitForFences(device, 1, &fences[slot], VK_TRUE, 5000000000ull));
            uint32_t index;
            VK(vkAcquireNextImageKHR(device, swap.handle, 5000000000ull, acquire[slot], VK_NULL_HANDLE, &index));
            VK(vkResetFences(device, 1, &fences[slot]));
            VK(vkResetCommandBuffer(commands[slot], 0));
            VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
            VK(vkBeginCommandBuffer(commands[slot], &begin));
            VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = swap.used[index] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = swap.images[index], .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
            vkCmdPipelineBarrier(commands[slot], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
            VkClearColorValue color = {.float32 = {0, 0, 0, 1}};
            color.float32[frame == 24 ? generation % 3 : frame % 3] = 1;
            vkCmdClearColorImage(commands[slot], swap.images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                &color, 1, &barrier.subresourceRange);
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = 0;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            vkCmdPipelineBarrier(commands[slot], VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
            VK(vkEndCommandBuffer(commands[slot]));
            VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1,
                .pWaitSemaphores = &acquire[slot], .pWaitDstStageMask = &stage,
                .commandBufferCount = 1, .pCommandBuffers = &commands[slot],
                .signalSemaphoreCount = 1, .pSignalSemaphores = &swap.complete[index]};
            VK(vkQueueSubmit(queue, 1, &submit, fences[slot]));
            VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                .waitSemaphoreCount = 1, .pWaitSemaphores = &swap.complete[index],
                .swapchainCount = 1, .pSwapchains = &swap.handle, .pImageIndices = &index};
            VK(vkQueuePresentKHR(queue, &present));
            swap.used[index] = 1;
            SDL_PumpEvents();
            if (frame == 24) check_display(generation % 3);
        }
        VK(vkDeviceWaitIdle(device));
    }
    destroy_swap(&swap);
    for (int i = 0; i < SLOTS; ++i) {
        vkDestroySemaphore(device, acquire[i], NULL);
        vkDestroyFence(device, fences[i], NULL);
    }
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);
    SDL_DestroyWindow(window);
    SDL_Quit();
    printf("PASS: %u presents, four swapchains, final frames, clean shutdown\n", serial);
    return 0;
}
