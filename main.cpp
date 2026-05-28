#include <vulkan/vulkan.h>
#include <vulkan/vulkan_gl.h>
#include <GLFW/glfw3.h>
#include <cmath>

// Vulkan + OpenGL context wrapper
class Vulkangl {
public:
    Vulkangl() : instance(nullptr), device(nullptr), swapchainKHR(nullptr), framebuffer(nullptr)
                 , renderPass(nullptr), depthFormat(VK_FORMAT_D32_SFLOAT), depthImage(nullptr), depthView(nullptr)
                 , vertexBuffer(nullptr), indexBuffer(nullptr), vertexBufferSize(0), indexBufferSize(0) {}

    ~Vulkangl() {
        if (vertexBuffer) vkDestroyBuffer(device, vertexBuffer, nullptr);
        if (indexBuffer) vkDestroyBuffer(device, indexBuffer, nullptr);
        if (depthImage) vkDestroyImage(device, depthImage, nullptr);
        if (framebuffer) vkDestroyFramebuffer(device, framebuffer, nullptr);
    }

    bool init(int width, int height) {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(width, height, "Vulkan Sphere", nullptr, nullptr);
        if (!window) return false;

        // Create Vulkan instance
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Sphere";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_2;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> layers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, layers.data());

        uint32_t extensionCount = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

        createInfo.enabledLayerCount = 0;
        createInfo.enabledExtensionCount = 0;

        VkResult err = vkCreateInstance(&createInfo, nullptr, &instance);
        if (err) return false;

        // Create physical device and logical device
        uint32_t gpuCount = 0;
        vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr);
        std::vector<VkPhysicalDevice> devices(gpuCount);
        vkEnumeratePhysicalDevices(instance, &gpuCount, devices.data());

        if (gpuCount == 0) return false;

        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(devices[0], &properties);
        printf("GPU: %s\n", properties.deviceName);

        VkPhysicalDeviceFeatures deviceFeatures{};
        vkGetPhysicalDeviceFeatures(devices[0], &deviceFeatures);

        VkSurfaceKHR surface = nullptr;
        glfwCreateWindowSurface(instance, window, nullptr, &surface);

        VkPhysicalDeviceVulkan12Properties vulkan12properties{};
        vkGetPhysicalDeviceVulkan12Properties(devices[0], &vulkan12properties);

        // Find suitable queue family
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[0], &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(devices[0], &queueFamilyCount, queueFamilies.data());

        uint32_t graphicsFamily = 0;
        for (uint32_t i = 0; i < queueFamilyCount; ++i) {
            if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                graphicsFamily = i;
                break;
            }
        }

        float queuePriorities[] = {1.0f};
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = graphicsFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = queuePriorities;

        std::vector<VkDeviceQueueCreateInfo> queueInfos(1, queueInfo);

        VkPhysicalDeviceProperties2 properties2{};
        properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties2.properties = properties;

        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.features = deviceFeatures;

        VkPhysicalDeviceVulkan12Features vulkan12features{};
        vulkan12features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        vulkan12features.protectedMemory = true;

        std::vector<VkPushDescriptorSetFeatureEXT> pushDescriptors(1);
        pushDescriptors[0].sType = VK_STRUCTURE_TYPE_PUSH_DESCRIPTOR_SET_FEATURE_EXT;
        pushDescriptors[0].pushDescriptor = true;

        VkPhysicalDevicePushDescriptorPropertiesKHR pushDescriptorProperties{};
        pushDescriptorProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR;

        std::vector<VkPushDescriptorSetPropertyEXT> pushDescriptorFeatures(1);
        pushDescriptorFeatures[0].sType = VK_STRUCTURE_TYPE_PUSH_DESCRIPTOR_SET_PROPERTY_EXT;
        pushDescriptorFeatures[0].pushDescriptor = true;

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = queueInfos.size();
        createInfo.pQueueCreateInfos = queueInfos.data();
        createInfo.pushDescriptorSetCount = pushDescriptors.size();
        createInfo.pPushDescriptorSetInfos = pushDescriptorFeatures.data();
        createInfo.pEnabledExtensionProperties = nullptr;

        err = vkCreateDevice(devices[0], &createInfo, nullptr, &device);
        if (err) return false;

        vkGetPhysicalDevicePushDescriptorPropertiesKHR(devices[0], &pushDescriptorProperties);

        VkQueue graphicsQueue = VK_NULL_HANDLE;
        vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
        this->queue = graphicsQueue;

        // Create swapchain
        VkSurfaceCapabilities2KHR capabilities{};
        capabilities.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR;
        vkGetPhysicalDeviceSurfaceCapabilities2KHR(devices[0], &surface, &capabilities);

        VkSurfaceFormat2KHR format{};
        format.sType = VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR;
        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormats2KHR(devices[0], &surface, nullptr, &formatCount, nullptr);
        std::vector<VkSurfaceFormat2KHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormats2KHR(devices[0], &surface, nullptr, &formatCount, formats.data());

        VkSurfacePresentMode2KHR present{};
        present.sType = VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_2_KHR;
        uint32_t presentCount = 0;
        vkGetPhysicalDeviceSurfacePresentModes2KHR(devices[0], &surface, nullptr, &presentCount, nullptr);
        std::vector<VkSurfacePresentMode2KHR> presents(presentCount);
        vkGetPhysicalDeviceSurfacePresentModes2KHR(devices[0], &surface, nullptr, &presentCount, presents.data());

        VkSwapchainCreateInfoKHR swapchainInfo{};
        swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainInfo.surface = surface;
        swapchainInfo.imageFormat = formats[0].format;
        swapchainInfo.imageColorSpace = formats[0].colorSpace;
        swapchainInfo.imageExtent = {capabilities.currentExtent.width, capabilities.currentExtent.height};
        swapchainInfo.imageArrayLayers = 1;
        swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchainInfo.minImageAlignment = VK_MAX(VkPhysicalDeviceProperties2::maxMemoryAllocationCount, 1);
        swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchainInfo.presentMode = presents[0].presentMode;
        swapchainInfo.clipped = true;
        swapchainInfo.oldSwapchain = VK_NULL_HANDLE;

        uint32_t imageCount = capabilities.minImageCount;
        if (imageCount == 0) imageCount = 1;
        VkResult err = vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &swapchainKHR);
        if (err) return false;

        uint32_t swapchainImageCount = 0;
        vkGetSwapchainImagesKHR(device, swapchainKHR, &swapchainImageCount, nullptr);
        std::vector<VkImage> images(swapchainImageCount);
        vkGetSwapchainImagesKHR(device, swapchainKHR, &swapchainImageCount, images.data());

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = formats[0].format;
        imageInfo.extent.width = capabilities.currentExtent.width;
        imageInfo.extent.height = capabilities.currentExtent.height;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        if (vulkan12properties.properties.protectedMemory) {
            depthFormat = VK_FORMAT_X8_D24_UNORM_PACK8;
        }

        // Create depth image
        imageInfo.format = depthFormat;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        vkCreateImage(device, &imageInfo, nullptr, &depthImage);
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device, depthImage, &memReqs);

        VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo{};
        memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        memoryAllocateFlagsInfo.memoryAllocateFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.requiredSizeBytes;
        allocInfo.image = depthImage;
        allocInfo.memoryHeapIndex = 0;
        allocInfo.memoryTypeIndex = findMemoryType(0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        err = vkAllocateMemory(device, &allocInfo, nullptr, &depthAllocation);
        if (err) return false;

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.image = depthImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkImageMemoryBarrier2 depthBarrier{};
        depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        depthBarrier.srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        depthBarrier.dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        depthBarrier.srcAccessMask = 0;
        depthBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthBarrier.image = depthImage;
        depthBarrier.subresourceRanges[0].aspectMask = VK_IMAGE_ASPECT_DEPTH;
        depthBarrier.subresourceRanges[0].baseMipLevel = 0;
        depthBarrier.subresourceRanges[0].levelCount = 1;
        depthBarrier.subresourceRanges[0].baseArrayLayer = 0;
        depthBarrier.subresourceRanges[0].layerCount = 1;

        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.pNext = &depthBarrier;

        vkCmdPipelineBarrier2(device, &dependencyInfo, nullptr);

        // Create depth view
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = depthImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = depthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        err = vkCreateImageView(device, &viewInfo, nullptr, &depthView);
        if (err) return false;

        // Create render pass
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = formats[0].format;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = depthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachments[0].attachment = VK_NULL_HANDLE;
        subpass.inputAttachments[0] = VK_NULL_HANDLE;
        subpass.viewMask = 1;

        VkSubpassDescription2 subpass2{};
        subpass2.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
        subpass2.pNext = nullptr;
        subpass2.flags = 0;
        subpass2.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass2.colorAttachments[0].attachment = VK_NULL_HANDLE;
        subpass2.inputAttachments[0] = VK_NULL_HANDLE;
        subpass2.viewMask = 1;

        VkSubpassDependency2 dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.dependencyFlags = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &depthAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass2;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        err = vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass);
        if (err) return false;

        // Create framebuffer
        VkImageView depthView{};
        VkImageViewCreateInfo depthViewInfo{};
        depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        depthViewInfo.image = depthImage;
        depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depthViewInfo.format = depthFormat;
        depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH;
        depthViewInfo.subresourceRange.baseMipLevel = 0;
        depthViewInfo.subresourceRange.levelCount = 1;
        depthViewInfo.subresourceRange.baseArrayLayer = 0;
        depthViewInfo.subresourceRange.layerCount = 1;

        err = vkCreateImageView(device, &depthViewInfo, nullptr, &depthView);
        if (err) return false;

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &depthView;
        framebufferInfo.width = capabilities.currentExtent.width;
        framebufferInfo.height = capabilities.currentExtent.height;
        framebufferInfo.layers = 1;

        err = vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffer);
        if (err) return false;

        // Create vertex and index buffers for sphere
        createSphere();

        return true;
    }

    void draw() {
        VkRenderPassBeginInfo renderPassBegin{};
        renderPassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBegin.renderPass = renderPass;
        renderPassBegin.framebuffer = framebuffer;
        renderPassBegin.renderArea.offset.x = 0;
        renderPassBegin.renderArea.offset.y = 0;
        renderPassBegin.renderArea.extent.width = glfwGetWindowFramebuffersize(window);
        renderPassBegin.renderArea.extent.height = glfwGetWindowFramebufferheight(window);

        VkClearValue clearColor{};
        clearColor.color.float32[0] = 0.1f;
        clearColor.color.float32[1] = 0.1f;
        clearColor.color.float32[2] = 0.1f;
        clearColor.color.float32[3] = 1.0f;

        VkClearValue depthClear{};
        depthClear.depthStencil.depth = 1.0f;

        std::vector<VkClearValue> clears(2);
        clears[0] = clearColor;
        clears[1] = depthClear;

        renderPassBegin.clearValueCount = clears.size();
        renderPassBegin.pClearValues = clears.data();

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkCmdBeginRenderPass(commandBuffer, &renderPassBegin, VK_SUBPASS_CONTENT_INLINE);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)glfwGetWindowFramebufferwidth(window);
        viewport.height = (float)glfwGetWindowFramebufferheight(window);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset.x = 0;
        scissor.offset.y = 0;
        scissor.extent.width = (uint32_t)viewport.width;
        scissor.extent.height = (uint32_t)viewport.height;

        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        VkPipelineLayout pipelineLayout{};
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = 1;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &poolSize;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 1;

        VkDescriptorSetLayout layout;
        vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout);

        VkDescriptorPool pool;
        vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);

        VkDescriptorSet descriptorSet;
        vkUpdateDescriptorSets(device, 0, nullptr, 1, &descriptorSet);

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 0;
        vertexInput.vertexAttributeDescriptionCount = 0;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.depthBiasEnable = VK_FALSE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicopEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        depthStencil.stencilTestEnable = VK_FALSE;

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 0;
        pipelineLayoutInfo.pSetLayouts = nullptr;

        vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout);

        VkGraphicsPipelineCreateInfo graphicsPipeline{};
        graphicsPipeline.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        graphicsPipeline.vertexBindingDescriptionCount = 0;
        graphicsPipeline.vertexAttributeDescriptionCount = 0;
        graphicsPipeline.pVertexInputState = &vertexInput;
        graphicsPipeline.pInputAssemblyState = &inputAssembly;
        graphicsPipeline.pViewportState = &viewportState;
        graphicsPipeline.pRasterizationState = &rasterizer;
        graphicsPipeline.pMultisampleState = &multisampling;
        graphicsPipeline.pColorBlendState = &colorBlending;
        graphicsPipeline.pDepthStencilState = &depthStencil;
        graphicsPipeline.layout = pipelineLayout;
        graphicsPipeline.renderPass = renderPass;

        VkShaderModuleCreateInfo vertexShaderInfo{};
        vertexShaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vertexShaderInfo.codeSize = sizeof(vertexShader);
        vertexShaderInfo.pCode = vertexShader;

        VkShaderModuleCreateInfo fragmentShaderInfo{};
        fragmentShaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        fragmentShaderInfo.codeSize = sizeof(fragmentShader);
        fragmentShaderInfo.pCode = fragmentShader;

        VkPipelineShaderStageCreateInfo vertexShaderStage{};
        vertexShaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertexShaderStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertexShaderStage.module = vkCreateShaderModule(device, &vertexShaderInfo, nullptr);

        VkPipelineShaderStageCreateInfo fragmentShaderStage{};
        fragmentShaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragmentShaderStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragmentShaderStage.module = vkCreateShaderModule(device, &fragmentShaderInfo, nullptr);

        graphicsPipeline.pVertexShaderStage = &vertexShaderStage;
        graphicsPipeline.pFragmentShaderStage = &fragmentShaderStage;

        VkResult err = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &graphicsPipeline, nullptr, &pipeline);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &vertexOffset);
        vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);

        vkEndRenderPass(commandBuffer);

        vkCmdPipelineBarrier2(device, &dependencyInfo, nullptr);

        vkCmdEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

        vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);

        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }

private:
    GLFWwindow* window = nullptr;
    VkInstance instance = nullptr;
    VkPhysicalDevice physicalDevice = devices[0];
    VkDevice device = nullptr;
    VkQueue queue = VK_NULL_HANDLE;
    VkSwapchainKHR swapchainKHR = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkImage depthImage = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexAllocation = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexAllocation = VK_NULL_HANDLE;

    uint32_t vertexBufferSize = 0;
    uint32_t indexBufferSize = 0;

    void createSphere() {
        // Generate sphere vertices and indices
        int segments = 32;
        int rings = 16;

        std::vector<float> vertices;
        std::vector<uint32_t> indices;

        for (int i = 0; i <= segments; ++i) {
            float theta = (float)i / segments * M_PI_F * 2.0f;
            float sinTheta = sinf(theta);
            float cosTheta = cosf(theta);

            for (int j = 0; j <= rings; ++j) {
                float phi = (float)j / rings * M_PI_F;
                float sinPhi = sinf(phi);
                float cosPhi = cosf(phi);

                // Position
                vertices.push_back(cosTheta * sinPhi);
                vertices.push_back(sinTheta * sinPhi);
                vertices.push_back(cosPhi);

                // Normal (same as position for unit sphere)
                vertices.push_back(cosTheta * sinPhi);
                vertices.push_back(sinTheta * sinPhi);
                vertices.push_back(cosPhi);

                // UV coordinates
                float u = (float)i / segments;
                float v = 1.0f - (float)j / rings;
                vertices.push_back(u);
                vertices.push_back(v);

                // Color (gradient based on position)
                float r = 0.5f + 0.5f * cosTheta;
                float g = 0.5f + 0.5f * sinTheta;
                float b = 0.5f + 0.5f * cosPhi;
                vertices.push_back(r);
                vertices.push_back(g);
                vertices.push_back(b);
            }
        }

        // Generate indices for triangle fan per ring
        int stride = (3 + 3 + 2 + 3) * sizeof(float); // pos, normal, uv, color
        uint32_t indexOffset = 0;

        for (int i = 0; i < segments; ++i) {
            for (int j = 1; j <= rings; ++j) {
                indices.push_back(i * (rings + 1) + (j - 1));
                indices.push_back(i * (rings + 1) + j);
                indices.push_back((i + 1) * (rings + 1) + j);

                indices.push_back(i * (rings + 1) + (j - 1));
                indices.push_back((i + 1) * (rings + 1) + j);
                indices.push_back((i + 1) * (rings + 1) + (j - 1));
            }
        }

        // Create vertex buffer
        VkBufferCreateInfo vertexBufferInfo{};
        vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        vertexBufferInfo.size = vertices.size() * sizeof(float);
        vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        vertexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        err = vkCreateBuffer(device, &vertexBufferInfo, nullptr, &vertexBuffer);
        if (err) return;

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(device, vertexBuffer, &memReqs);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.requiredSizeBytes;
        allocInfo.memoryHeapIndex = 0;
        allocInfo.memoryTypeIndex = findMemoryType(1, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        err = vkAllocateMemory(device, &allocInfo, nullptr, &vertexAllocation);
        if (err) return;

        vkBindBufferMemory(device, vertexBuffer, vertexAllocation, 0);

        // Copy vertices to buffer
        std::vector<uint8_t> data(vertices.size() * sizeof(float));
        for (size_t i = 0; i < vertices.size(); ++i) {
            data[i] = ((uint8_t*)vertices.data())[i];
        }

        vkCmdCopyBuffer(commandBuffer, VK_NULL_HANDLE, vertexBuffer, 0, VK_NULL_HANDLE, 0, vertices.size() * sizeof(float));

        // Create index buffer
        VkBufferCreateInfo indexBufferInfo{};
        indexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        indexBufferInfo.size = indices.size() * sizeof(uint32_t);
        indexBufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        indexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        err = vkCreateBuffer(device, &indexBufferInfo, nullptr, &indexBuffer);
        if (err) return;

        VkMemoryRequirements indexMemReqs;
        vkGetBufferMemoryRequirements(device, indexBuffer, &indexMemReqs);

        VkMemoryAllocateInfo indexAllocInfo{};
        indexAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        indexAllocInfo.allocationSize = indexMemReqs.requiredSizeBytes;
        indexAllocInfo.memoryHeapIndex = 0;
        indexAllocInfo.memoryTypeIndex = findMemoryType(1, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        err = vkAllocateMemory(device, &indexAllocInfo, nullptr, &indexAllocation);
        if (err) return;

        vkBindBufferMemory(device, indexBuffer, indexAllocation, 0);

        // Copy indices to buffer
        std::vector<uint8_t> indexData(indices.size() * sizeof(uint32_t));
        for (size_t i = 0; i < indices.size(); ++i) {
            indexData[i] = ((uint8_t*)indices.data())[i];
        }

        vkCmdCopyBuffer(commandBuffer, VK_NULL_HANDLE, indexBuffer, 0, VK_NULL_HANDLE, 0, indices.size() * sizeof(uint32_t));
    }

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((typeFilter & (1 << i)) &&
                (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        return UINT32_MAX;
    }
};

// Vertex shader
const char vertexShader[] = R"(
#version 450
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aColor;

out gl_FragColor;

void main() {
    gl_Position = vec4(aPos, 1.0);
    gl_FragColor = vec4(aColor, 1.0);
}
" R);

// Fragment shader
const char fragmentShader[] = R"(
#version 450
layout(location = 0) out vec4 fragColor;
in vec3 aPos;
in vec3 aNormal;
in vec2 aUV;
in vec3 aColor;

void main() {
    // Simple lighting: diffuse + ambient
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    vec3 normal = normalize(aNormal);
    float diff = max(dot(normal, lightDir), 0.0);
    
    // Add some shading based on position
    vec3 ambient = vec3(0.2, 0.2, 0.2);
    vec3 diffuse = aColor * diff;
    
    fragColor = vec4(ambient + diffuse, 1.0);
}
" R);

int main() {
    Vulkangl vulkan;
    if (!vulkan.init(800, 600)) {
        printf("Failed to initialize Vulkan\n");
        return -1;
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        vulkan.draw();
    }

    vkDeviceWaitIdle(vulkan.device);
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
