// SPDX-License-Identifier: GPL-2.0-or-later
// Offscreen owned-packet consumer. Software Vulkan is not Pocket S proof.
#include "output_packet.h"
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
constexpr unsigned Width = 64, Height = 64;
using Pixels = std::array<uint8_t, Width * Height * 4>;
void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
void check(VkResult result, const char *operation)
{
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "%s: VkResult=%d\n", operation, int(result));
        throw std::runtime_error(operation);
    }
}
std::atomic<unsigned> validationErrors{};
VKAPI_ATTR VkBool32 VKAPI_CALL
debugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
             VkDebugUtilsMessageTypeFlagsEXT,
             const VkDebugUtilsMessengerCallbackDataEXT *data, void *)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        ++validationErrors;
        std::fprintf(stderr, "Vulkan validation: %s\n", data->pMessage);
    }
    return VK_FALSE;
}
struct Buffer {
    VkBuffer handle{};
    VkDeviceMemory memory{};
    void *mapped{};
};
struct Image {
    VkImage handle{};
    VkDeviceMemory memory{};
    VkImageView view{};
};
struct Consumer {
    VkInstance instance{};
    VkDebugUtilsMessengerEXT debug{};
    VkPhysicalDevice physical{};
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkDevice device{};
    VkQueue queue{};
    uint32_t family{};
    Buffer upload, readback;
    Image texture, target;
    VkSampler sampler{};
    VkDescriptorSetLayout setLayout{};
    VkDescriptorPool descriptorPool{};
    VkDescriptorSet descriptor{};
    VkPipelineLayout pipelineLayout{};
    VkRenderPass renderPass{};
    VkFramebuffer framebuffer{};
    VkPipeline pipeline{};
    VkCommandPool commandPool{};
    VkCommandBuffer command{};
    VkFence fence{};
    bool uploaded = false;
    // All objects and mapped storage are created once. render() allocates no
    // Vulkan objects and waits for this slot's fence, never device-wide idle.
    ~Consumer()
    {
        if (device) {
            // Final teardown only. No bounded recovery is claimed for device
            // loss.
            vkDeviceWaitIdle(device);
            vkDestroyFence(device, fence, nullptr);
            vkDestroyCommandPool(device, commandPool, nullptr);
            vkDestroyPipeline(device, pipeline, nullptr);
            vkDestroyFramebuffer(device, framebuffer, nullptr);
            vkDestroyRenderPass(device, renderPass, nullptr);
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
            vkDestroySampler(device, sampler, nullptr);
            for (auto *image : { &texture, &target }) {
                vkDestroyImageView(device, image->view, nullptr);
                vkDestroyImage(device, image->handle, nullptr);
                vkFreeMemory(device, image->memory, nullptr);
            }
            for (auto *buffer : { &upload, &readback }) {
                if (buffer->mapped)
                    vkUnmapMemory(device, buffer->memory);
                vkDestroyBuffer(device, buffer->handle, nullptr);
                vkFreeMemory(device, buffer->memory, nullptr);
            }
            vkDestroyDevice(device, nullptr);
        }
        if (debug) {
            auto destroy =
                reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(instance,
                                          "vkDestroyDebugUtilsMessengerEXT"));
            if (destroy)
                destroy(instance, debug, nullptr);
        }
        if (instance)
            vkDestroyInstance(instance, nullptr);
    }
    uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags required)
    {
        for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
            if ((bits & (1u << i)) &&
                (memoryProperties.memoryTypes[i].propertyFlags & required) ==
                    required)
                return i;
        throw std::runtime_error("required memory type unavailable");
    }
    void makeBuffer(Buffer &buffer, VkDeviceSize size, VkBufferUsageFlags usage)
    {
        VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        info.size = size;
        info.usage = usage;
        check(vkCreateBuffer(device, &info, nullptr, &buffer.handle),
              "create buffer");
        VkMemoryRequirements need;
        vkGetBufferMemoryRequirements(device, buffer.handle, &need);
        VkMemoryAllocateInfo allocation{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
        };
        allocation.allocationSize = need.size;
        allocation.memoryTypeIndex = memoryType(
            need.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vkAllocateMemory(device, &allocation, nullptr, &buffer.memory),
              "buffer memory");
        check(vkBindBufferMemory(device, buffer.handle, buffer.memory, 0),
              "bind buffer");
        check(vkMapMemory(device, buffer.memory, 0, VK_WHOLE_SIZE, 0,
                          &buffer.mapped),
              "map buffer");
    }
    void makeImage(Image &image, unsigned width, unsigned height,
                   VkImageUsageFlags usage)
    {
        VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = VK_FORMAT_R8G8B8A8_UNORM;
        info.extent = { width, height, 1 };
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        check(vkCreateImage(device, &info, nullptr, &image.handle),
              "create image");
        VkMemoryRequirements need;
        vkGetImageMemoryRequirements(device, image.handle, &need);
        VkMemoryAllocateInfo allocation{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
        };
        allocation.allocationSize = need.size;
        allocation.memoryTypeIndex = memoryType(
            need.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device, &allocation, nullptr, &image.memory),
              "image memory");
        check(vkBindImageMemory(device, image.handle, image.memory, 0),
              "bind image");
        VkImageViewCreateInfo view{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        view.image = image.handle;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = info.format;
        view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        check(vkCreateImageView(device, &view, nullptr, &image.view),
              "image view");
    }
    VkShaderModule shader(const std::string &path)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        require(bool(input), "open SPIR-V");
        const auto bytes = input.tellg();
        require(bytes > 0 && bytes < 65536 && bytes % 4 == 0, "SPIR-V length");
        std::vector<uint32_t> words(size_t(bytes) / 4);
        input.seekg(0);
        input.read(reinterpret_cast<char *>(words.data()), bytes);
        require(bool(input) && words[0] == 0x07230203, "SPIR-V content");
        VkShaderModuleCreateInfo info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO
        };
        info.codeSize = size_t(bytes);
        info.pCode = words.data();
        VkShaderModule module{};
        check(vkCreateShaderModule(device, &info, nullptr, &module),
              "shader module");
        return module;
    }
    void init(const std::string &shaders)
    {
        const char *layer = "VK_LAYER_KHRONOS_validation";
        const char *extension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
        app.pApplicationName = "ARMXBOX owned output offscreen test";
        app.apiVersion = VK_API_VERSION_1_0;
        VkDebugUtilsMessengerCreateInfoEXT debugInfo{
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT
        };
        debugInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugInfo.pfnUserCallback = debugMessage;
        VkValidationFeatureEnableEXT synchronization =
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        VkValidationFeaturesEXT validation{
            VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT
        };
        validation.enabledValidationFeatureCount = 1;
        validation.pEnabledValidationFeatures = &synchronization;
        validation.pNext = &debugInfo;
        VkInstanceCreateInfo create{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        create.pApplicationInfo = &app;
        create.enabledLayerCount = 1;
        create.ppEnabledLayerNames = &layer;
        create.enabledExtensionCount = 1;
        create.ppEnabledExtensionNames = &extension;
        create.pNext = &validation;
        check(vkCreateInstance(&create, nullptr, &instance),
              "instance with validation");
        auto makeDebug = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        require(makeDebug, "debug extension");
        check(makeDebug(instance, &debugInfo, nullptr, &debug),
              "debug messenger");
        uint32_t count = 0;
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr),
              "enumerate devices");
        require(count && count <= 64, "device count");
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance, &count, devices.data()),
              "device list");
        for (auto candidate : devices) {
            uint32_t queues = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queues,
                                                     nullptr);
            std::vector<VkQueueFamilyProperties> properties(queues);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queues,
                                                     properties.data());
            for (uint32_t i = 0; i < queues; ++i)
                if (properties[i].queueCount &&
                    (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                    physical = candidate;
                    family = i;
                    break;
                }
            if (physical)
                break;
        }
        require(physical, "graphics queue");
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physical, &properties);
        std::printf("Vulkan device=%s vendor=%x device=%x driver=%u "
                    "API=%u.%u.%u queue=%u\n",
                    properties.deviceName, properties.vendorID,
                    properties.deviceID, properties.driverVersion,
                    VK_VERSION_MAJOR(properties.apiVersion),
                    VK_VERSION_MINOR(properties.apiVersion),
                    VK_VERSION_PATCH(properties.apiVersion), family);
        VkFormatProperties format;
        vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R8G8B8A8_UNORM,
                                            &format);
        constexpr auto features = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                  VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
        require((format.optimalTilingFeatures & features) == features,
                "RGBA8 capabilities");
        std::printf("RGBA8 optimalFeatures=%x; 64x64; nearest sampling; "
                    "validation+sync enabled\n",
                    format.optimalTilingFeatures);
        vkGetPhysicalDeviceMemoryProperties(physical, &memoryProperties);
        float priority = 1;
        VkDeviceQueueCreateInfo queueInfo{
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO
        };
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        check(vkCreateDevice(physical, &deviceInfo, nullptr, &device),
              "device");
        vkGetDeviceQueue(device, family, 0, &queue);
        makeBuffer(upload, 128,
                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        makeBuffer(readback, Pixels{}.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        makeImage(texture, 4, 4,
                  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        makeImage(target, Width, Height,
                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        VkSamplerCreateInfo sample{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        sample.magFilter = sample.minFilter = VK_FILTER_NEAREST;
        sample.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sample.addressModeU = sample.addressModeV = sample.addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        check(vkCreateSampler(device, &sample, nullptr, &sampler), "sampler");
        VkDescriptorSetLayoutBinding binding{
            0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
            VK_SHADER_STAGE_FRAGMENT_BIT, nullptr
        };
        VkDescriptorSetLayoutCreateInfo setInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
        };
        setInfo.bindingCount = 1;
        setInfo.pBindings = &binding;
        check(
            vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &setLayout),
            "set layout");
        VkDescriptorPoolSize poolSize{
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1
        };
        VkDescriptorPoolCreateInfo poolInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
        };
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        check(
            vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool),
            "descriptor pool");
        VkDescriptorSetAllocateInfo setAllocate{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
        };
        setAllocate.descriptorPool = descriptorPool;
        setAllocate.descriptorSetCount = 1;
        setAllocate.pSetLayouts = &setLayout;
        check(vkAllocateDescriptorSets(device, &setAllocate, &descriptor),
              "descriptor");
        VkDescriptorImageInfo descriptorImage{
            sampler, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
        VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = descriptor;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &descriptorImage;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        VkPipelineLayoutCreateInfo layout{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
        };
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &setLayout;
        check(vkCreatePipelineLayout(device, &layout, nullptr, &pipelineLayout),
              "pipeline layout");
        VkAttachmentDescription attachment{};
        attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkAttachmentReference color{ 0,
                                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color;
        std::array<VkSubpassDependency, 2> deps{};
        deps[0] = { VK_SUBPASS_EXTERNAL,
                    0,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    0,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    0 };
        deps[1] = { 0,
                    VK_SUBPASS_EXTERNAL,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    0 };
        VkRenderPassCreateInfo pass{
            VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO
        };
        pass.attachmentCount = 1;
        pass.pAttachments = &attachment;
        pass.subpassCount = 1;
        pass.pSubpasses = &subpass;
        pass.dependencyCount = deps.size();
        pass.pDependencies = deps.data();
        check(vkCreateRenderPass(device, &pass, nullptr, &renderPass),
              "render pass");
        VkFramebufferCreateInfo fb{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fb.renderPass = renderPass;
        fb.attachmentCount = 1;
        fb.pAttachments = &target.view;
        fb.width = Width;
        fb.height = Height;
        fb.layers = 1;
        check(vkCreateFramebuffer(device, &fb, nullptr, &framebuffer),
              "framebuffer");
        // Modules are temporary; retain their handles even if pipeline creation
        // fails.
        const VkShaderModule vert = shader(shaders + "/output.vert.spv");
        VkShaderModule frag{};
        try {
            frag = shader(shaders + "/output.frag.spv");
            std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
            stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                          nullptr,
                          0,
                          VK_SHADER_STAGE_VERTEX_BIT,
                          vert,
                          "main",
                          nullptr };
            stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                          nullptr,
                          0,
                          VK_SHADER_STAGE_FRAGMENT_BIT,
                          frag,
                          "main",
                          nullptr };
            VkVertexInputBindingDescription vb{ 0, 8,
                                                VK_VERTEX_INPUT_RATE_VERTEX };
            VkVertexInputAttributeDescription va{ 0, 0, VK_FORMAT_R32G32_SFLOAT,
                                                  0 };
            VkPipelineVertexInputStateCreateInfo vertex{
                VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
            };
            vertex.vertexBindingDescriptionCount = 1;
            vertex.pVertexBindingDescriptions = &vb;
            vertex.vertexAttributeDescriptionCount = 1;
            vertex.pVertexAttributeDescriptions = &va;
            VkPipelineInputAssemblyStateCreateInfo assembly{
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
            };
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkViewport viewport{ 0, 0, float(Width), float(Height), 0, 1 };
            VkRect2D scissor{ { 0, 0 }, { Width, Height } };
            VkPipelineViewportStateCreateInfo view{
                VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
            };
            view.viewportCount = 1;
            view.pViewports = &viewport;
            view.scissorCount = 1;
            view.pScissors = &scissor;
            VkPipelineRasterizationStateCreateInfo raster{
                VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
            };
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.cullMode = VK_CULL_MODE_NONE;
            raster.lineWidth = 1;
            VkPipelineMultisampleStateCreateInfo multi{
                VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
            };
            multi.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineColorBlendAttachmentState blend{};
            blend.colorWriteMask = 15;
            VkPipelineColorBlendStateCreateInfo blending{
                VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
            };
            blending.attachmentCount = 1;
            blending.pAttachments = &blend;
            VkGraphicsPipelineCreateInfo graphics{
                VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO
            };
            graphics.stageCount = stages.size();
            graphics.pStages = stages.data();
            graphics.pVertexInputState = &vertex;
            graphics.pInputAssemblyState = &assembly;
            graphics.pViewportState = &view;
            graphics.pRasterizationState = &raster;
            graphics.pMultisampleState = &multi;
            graphics.pColorBlendState = &blending;
            graphics.layout = pipelineLayout;
            graphics.renderPass = renderPass;
            check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                            &graphics, nullptr, &pipeline),
                  "graphics pipeline");
        } catch (...) {
            vkDestroyShaderModule(device, frag, nullptr);
            vkDestroyShaderModule(device, vert, nullptr);
            throw;
        }
        vkDestroyShaderModule(device, frag, nullptr);
        vkDestroyShaderModule(device, vert, nullptr);
        VkCommandPoolCreateInfo commands{
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO
        };
        commands.queueFamilyIndex = family;
        commands.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        check(vkCreateCommandPool(device, &commands, nullptr, &commandPool),
              "command pool");
        VkCommandBufferAllocateInfo allocate{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
        };
        allocate.commandPool = commandPool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device, &allocate, &command),
              "command buffer");
        VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(device, &fenceInfo, nullptr, &fence), "fence");
    }
    Pixels render(const armxbox::OutputPacket &packet)
    {
        constexpr uint64_t Deadline = 3'000'000'000;
        check(vkWaitForFences(device, 1, &fence, VK_TRUE, Deadline),
              "slot fence before reuse");
        std::memcpy(upload.mapped, packet.vertices().data(), 24);
        std::memcpy(static_cast<uint8_t *>(upload.mapped) + 64,
                    packet.texture().data(), 64);
        std::memset(readback.mapped, 0x5d, Pixels{}.size());
        check(vkResetCommandBuffer(command, 0), "reset commands");
        VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command, &begin), "begin commands");
        VkImageMemoryBarrier textureBarrier{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER
        };
        textureBarrier.srcAccessMask = uploaded ? VK_ACCESS_SHADER_READ_BIT : 0;
        textureBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        textureBarrier.oldLayout =
            uploaded ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL :
                       VK_IMAGE_LAYOUT_UNDEFINED;
        textureBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        textureBarrier.srcQueueFamilyIndex =
            textureBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        textureBarrier.image = texture.handle;
        textureBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0,
                                            1 };
        vkCmdPipelineBarrier(command,
                             uploaded ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT :
                                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &textureBarrier);
        VkBufferImageCopy texCopy{};
        texCopy.bufferOffset = 64;
        texCopy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        texCopy.imageExtent = { 4, 4, 1 };
        vkCmdCopyBufferToImage(command, upload.handle, texture.handle,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &texCopy);
        textureBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        textureBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        textureBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        textureBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &textureBarrier);
        VkClearValue clear{};
        clear.color.float32[3] = 1;
        VkRenderPassBeginInfo pass{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        pass.renderPass = renderPass;
        pass.framebuffer = framebuffer;
        pass.renderArea = { { 0, 0 }, { Width, Height } };
        pass.clearValueCount = 1;
        pass.pClearValues = &clear;
        vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout, 0, 1, &descriptor, 0, nullptr);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(command, 0, 1, &upload.handle, &offset);
        vkCmdDraw(command, 3, 1, 0, 0);
        vkCmdEndRenderPass(command);
        VkBufferImageCopy copy{};
        copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copy.imageExtent = { Width, Height, 1 };
        vkCmdCopyImageToBuffer(command, target.handle,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readback.handle, 1, &copy);
        VkBufferMemoryBarrier host{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        host.srcQueueFamilyIndex = host.dstQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED;
        host.buffer = readback.handle;
        host.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                             &host, 0, nullptr);
        check(vkEndCommandBuffer(command), "end commands");
        check(vkResetFences(device, 1, &fence), "reset slot fence");
        VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        check(vkQueueSubmit(queue, 1, &submit, fence), "submit packet");
        check(vkWaitForFences(device, 1, &fence, VK_TRUE, Deadline),
              "readback fence");
        uploaded = true;
        Pixels pixels;
        std::memcpy(pixels.data(), readback.mapped, pixels.size());
        return pixels;
    }
};
// Separate scalar raster/nearest oracle: no shader invocation or shared output
// bytes. Ignore only pixels within one pixel of an edge to allow fill rules.
unsigned mismatches(const Pixels &pixels, const armxbox::OutputPacket &packet)
{
    std::array<double, 6> v;
    for (unsigned i = 0; i < 6; ++i) {
        uint32_t bits = 0;
        for (unsigned b = 0; b < 4; ++b)
            bits |= uint32_t(packet.vertices()[4 * i + b]) << (8 * b);
        v[i] = (double(std::bit_cast<float>(bits)) + 1) *
               (i % 2 ? Height : Width) / 2;
    }
    unsigned wrong = 0, compared = 0;
    for (unsigned y = 0; y < Height; ++y)
        for (unsigned x = 0; x < Width; ++x) {
            std::array<double, 3> edges;
            bool near = false;
            for (unsigned i = 0; i < 3; ++i) {
                const unsigned j = (i + 1) % 3;
                const double dx = v[2 * j] - v[2 * i],
                             dy = v[2 * j + 1] - v[2 * i + 1];
                edges[i] =
                    dx * (y + .5 - v[2 * i + 1]) - dy * (x + .5 - v[2 * i]);
                near |= std::abs(edges[i]) <= std::abs(dx) + std::abs(dy);
            }
            if (near)
                continue;
            const bool inside = std::all_of(edges.begin(), edges.end(),
                                            [](double e) { return e > 0; }) ||
                                std::all_of(edges.begin(), edges.end(),
                                            [](double e) { return e < 0; });
            const size_t texel = 4 * ((4 * y / Height) * 4 + (4 * x / Width));
            for (unsigned c = 0; c < 4; ++c) {
                const uint8_t expected =
                    inside ? packet.texture()[texel + c] : (c == 3 ? 255 : 0);
                wrong += pixels[4 * (y * Width + x) + c] != expected;
            }
            ++compared;
        }
    require(compared > 3000, "oracle coverage");
    return wrong;
}
} // namespace
int main(int argc, char **argv)
{
    try {
        require(argc == 2,
                "usage: armxbox_output_vulkan_test shader-directory");
        validationErrors = 0;
        {
            Consumer gpu;
            gpu.init(argv[1]);
            armxbox::OutputQueue queue;
            Pixels first{};
            for (unsigned iteration = 1; iteration <= 4; ++iteration) {
                auto source = armxbox::outputFixture(0x13000, iteration);
                // Asymmetry catches transpose/orientation/channel mistakes
                // hidden by a symmetric checker. Inset geometry catches ignored
                // vertices.
                if (iteration % 2 == 0) {
                    for (unsigned i = 0; i < 16; ++i) {
                        source[0xa0 + 4 * i] = 11 + 9 * i;
                        source[0xa1 + 4 * i] = 197 - 7 * i;
                        source[0xa2 + 4 * i] = 31 + 5 * i;
                        source[0xa3 + 4 * i] = 83 + 11 * i;
                    }
                    const std::array<float, 6> inset{ -.75f, -.5f, -.75f,
                                                      .75f,  .5f,  -.5f };
                    for (unsigned i = 0; i < 6; ++i) {
                        auto bits = std::bit_cast<uint32_t>(inset[i]);
                        for (unsigned b = 0; b < 4; ++b)
                            source[0x80 + 4 * i + b] = bits >> (8 * b);
                    }
                }
                require(queue.submit({ 0x13000, source, 1, true, false },
                                     0x13000) == armxbox::OutputError::None,
                        "capture owned test packet");
                auto lease = queue.acquire();
                auto *packet = queue.get(lease);
                require(packet, "packet lease");
                source.fill(0x39); // Host consumer must use its owned snapshot.
                const auto pixels = gpu.render(*packet);
                require(!mismatches(pixels, *packet), "pixel oracle mismatch");
                std::printf(
                    "PASS Vulkan owned packet %u: scalar triangle/nearest RGBA "
                    "oracle; source overwritten\n",
                    iteration);
                if (iteration == 1 || iteration == 2) {
                    const auto stem = std::string(argv[1]) + "/offscreen-" +
                                      std::to_string(iteration);
                    std::ofstream raw(stem + ".rgba", std::ios::binary);
                    raw.write(reinterpret_cast<const char *>(pixels.data()),
                              pixels.size());
                    require(bool(raw), "write RGBA evidence");
                    std::ofstream ppm(stem + ".ppm", std::ios::binary);
                    ppm << "P6\n" << Width << " " << Height << "\n255\n";
                    for (size_t i = 0; i < pixels.size(); i += 4)
                        ppm.write(
                            reinterpret_cast<const char *>(pixels.data() + i),
                            3);
                    require(bool(ppm), "write PPM evidence");
                }
                if (iteration == 1)
                    first = pixels;
                if (iteration == 3)
                    require(first == pixels, "warm canonical frame differs");
                if (iteration == 2) {
                    auto corrupt = pixels;
                    for (size_t i = 0; i < corrupt.size(); i += 4)
                        std::swap(corrupt[i], corrupt[i + 2]);
                    require(mismatches(corrupt, *packet),
                            "channel negative control");
                    for (unsigned y = 0; y < Height; ++y)
                        std::memcpy(corrupt.data() + 4 * y * Width,
                                    pixels.data() +
                                        4 * (Height - 1 - y) * Width,
                                    4 * Width);
                    require(mismatches(corrupt, *packet),
                            "orientation negative control");
                    corrupt.fill(0x5d);
                    require(mismatches(corrupt, *packet),
                            "missing copy negative control");
                    corrupt.fill(0);
                    for (size_t i = 3; i < corrupt.size(); i += 4)
                        corrupt[i] = 255;
                    require(mismatches(corrupt, *packet),
                            "missing draw negative control");
                    std::puts("PASS pixel oracle rejects channel swap, "
                              "vertical flip, missing copy and missing draw");
                }
                require(queue.retire(lease) && !queue.get(lease),
                        "retire completed GPU packet");
            }
        }
        require(validationErrors == 0,
                "Vulkan validation errors including cleanup");
        std::puts("PASS Vulkan validation and synchronization validation: zero "
                  "errors");
        std::puts("Result: cloud offscreen consumer PASS; not Android/Turnip, "
                  "PCM playback or game FPS proof");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL offscreen consumer: %s\n", error.what());
        return 1;
    }
}
