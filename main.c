#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// We want the Vulkan Validation Layers to scream at us if we make a mistake!
const char* validationLayers[] = {
    "VK_LAYER_KHRONOS_validation"
};
// ========================================================
// THE C-SIDE MEMORY ALIGNMENT
// ========================================================
// This MUST perfectly match the memory layout of our GLSL shader!
// 3 floats (12B) + 1 float (4B) + 3 floats (12B) + 1 pad (4B) = 32 Bytes
typedef struct {
    float pos[3];
    float seed;
    float vel[3];
    float pad;
} Particle;

// ========================================================
// VULKAN HELPER: FIND VRAM MEMORY TYPE
// ========================================================
// GPUs have different types of memory. We need to find memory that the
// GPU can read super fast, but the CPU is still allowed to write to.
uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    printf("FATAL: Failed to find suitable VRAM memory type!\n");
    exit(-1);
}
// ========================================================
// HELPER: READ COMPILED SHADER FILE
// ========================================================
char* readShaderFile(const char* filename, size_t* outSize) {
    FILE* file = fopen(filename, "rb");
    if (!file) { printf("FATAL: Failed to open %s\n", filename); exit(-1); }
    fseek(file, 0, SEEK_END);
    *outSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    char* buffer = malloc(*outSize);
    fread(buffer, 1, *outSize, file);
    fclose(file);
    return buffer;
}

// ========================================================
// THE PUSH CONSTANTS (Matches the GLSL struct exactly)
// ========================================================
typedef struct {
    float center[3];
    float time;
    float dt;
    float noise_blend;
    uint32_t particleCount;
} PushConstants;
// 64 Bytes perfectly aligned for Vulkan Push Constants
typedef struct {
    float pos[3]; float pad1;
    float fwd[3]; float pad2;
    float right[3]; float pad3;
    float up[3]; float fov;
    float w; float h;
} RenderPushConstants;
int main() {
    // ========================================================
    // 1. INITIALIZE THE OS WINDOW (GLFW)
    // ========================================================
    if (!glfwInit()) {
        printf("FATAL: Failed to initialize GLFW!\n");
        return -1;
    }

    // Tell GLFW absolutely NOT to create an OpenGL context. We are a Vulkan house now.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    // --- [NEW] FULLSCREEN SETUP ---
    GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);

    // Pass the monitor's exact width/height, and hand it the monitor pointer to go fullscreen
    GLFWwindow* window = glfwCreateWindow(mode->width, mode->height, "Vulkan Swarm Engine", primaryMonitor, NULL);

    if (!window) {
        printf("FATAL: Failed to create GLFW window!\n");
        glfwTerminate();
        return -1;
    }
    // ========================================================
    // 2. INITIALIZE VULKAN 1.3 INSTANCE
    // ========================================================
    VkApplicationInfo appInfo = {0}; // ZII - Zero is Initialization!
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Swarm Compute Bootstrapper";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3; // Demanding modern Vulkan

    // Ask GLFW what extensions the OS needs to draw to a window
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    VkInstanceCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = glfwExtensionCount;
    createInfo.ppEnabledExtensionNames = glfwExtensions;

    // Enable the safety net (Validation Layers)
    createInfo.enabledLayerCount = 1;
    createInfo.ppEnabledLayerNames = validationLayers;

    VkInstance instance;
    if (vkCreateInstance(&createInfo, NULL, &instance) != VK_SUCCESS) {
        printf("FATAL: Failed to create Vulkan instance! (Do you have vulkan-validation-layers installed?)\n");
        return -1;
    }
    printf("[SYSTEM] Vulkan 1.3 Instance Created Successfully.\n");

    // ========================================================
    // 3. INTERROGATE THE HARDWARE (Find the GPU)
    // ========================================================
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);

    if (deviceCount == 0) {
        printf("FATAL: Failed to find GPUs with Vulkan support!\n");
        return -1;
    }

    // Allocate an array on the stack to hold the GPUs we found
    VkPhysicalDevice* devices = malloc(deviceCount * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices);

    printf("[SYSTEM] Found %d Vulkan-compatible GPU(s):\n", deviceCount);

    VkPhysicalDevice chosenGPU = devices[0]; // Just grab the first one for now

    for (uint32_t i = 0; i < deviceCount; i++) {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(devices[i], &deviceProperties);
        printf("  -> [%d] %s (API: %d.%d.%d)\n",
            i,
            deviceProperties.deviceName,
            VK_VERSION_MAJOR(deviceProperties.apiVersion),
            VK_VERSION_MINOR(deviceProperties.apiVersion),
            VK_VERSION_PATCH(deviceProperties.apiVersion)
        );

        // If it's a discrete GPU (like a dedicated AMD/NVIDIA card), prioritize it!
        if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            chosenGPU = devices[i];
        }
    }

    VkPhysicalDeviceProperties chosenProps;
    vkGetPhysicalDeviceProperties(chosenGPU, &chosenProps);
    printf("[SYSTEM] Selected GPU: %s\n", chosenProps.deviceName);

    free(devices);
    // ========================================================
    // 3.5. ESTABLISH THE LOGICAL DEVICE & COMMAND QUEUE
    // ========================================================
    // GPUs have different "Queues" for Graphics, Compute, and Transfer.
    // We need to find the index of a Queue Family that supports our needs.
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(chosenGPU, &queueFamilyCount, NULL);
    VkQueueFamilyProperties* queueFamilies = malloc(queueFamilyCount * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(chosenGPU, &queueFamilyCount, queueFamilies);

    uint32_t graphicsComputeQueueIndex = 0;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        // We want a queue that can do both Graphics (later) and Compute (now)
        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
            graphicsComputeQueueIndex = i;
            break;
        }
    }
    free(queueFamilies);
    printf("[SYSTEM] Using Queue Family Index: %d\n", graphicsComputeQueueIndex);

    // Now we tell Vulkan to open a connection to this specific queue
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {0};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = graphicsComputeQueueIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    // --- [NEW] ENABLE SWAPCHAIN AND DYNAMIC RENDERING ---
    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkPhysicalDeviceDynamicRenderingFeatures dynamicRendering = {0};
    dynamicRendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    dynamicRendering.dynamicRendering = VK_TRUE;

    // Create the Logical Device
    VkDeviceCreateInfo deviceCreateInfo = {0};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.pNext = &dynamicRendering; // Hook in Dynamic Rendering!
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.enabledExtensionCount = 1;
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions;
    deviceCreateInfo.enabledLayerCount = 1;
    deviceCreateInfo.ppEnabledLayerNames = validationLayers;

    VkDevice device;
    if (vkCreateDevice(chosenGPU, &deviceCreateInfo, NULL, &device) != VK_SUCCESS) {
        printf("FATAL: Failed to create Logical Device!\n");
        return -1;
    }
    printf("[SYSTEM] Logical Device Created Successfully.\n");

    // Grab the actual handle to the Queue so we can submit commands later
    VkQueue computeQueue;
    vkGetDeviceQueue(device, graphicsComputeQueueIndex, 0, &computeQueue);
    // ========================================================
    // 3.6. ALLOCATE VRAM (The Particle Buffer)
    // ========================================================
    uint32_t particleCount = 1000000;
    VkDeviceSize bufferSize = sizeof(Particle) * particleCount; // Exactly 32 Megabytes

    // Step A: Describe the Buffer to Vulkan (We will use this as a Storage Buffer in the Shader)
    VkBufferCreateInfo bufferInfo = {0};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer particleBuffer;
    if (vkCreateBuffer(device, &bufferInfo, NULL, &particleBuffer) != VK_SUCCESS) {
        printf("FATAL: Failed to create particle VkBuffer!\n");
        return -1;
    }

    // Step B: Ask Vulkan exactly how much silicon this buffer requires
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, particleBuffer, &memReqs);

    // Step C: Allocate the physical VRAM
    VkMemoryAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    // We request memory that the CPU can see (HOST_VISIBLE) and doesn't need flushing (HOST_COHERENT)
    allocInfo.memoryTypeIndex = findMemoryType(chosenGPU, memReqs.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkDeviceMemory particleMemory;
    if (vkAllocateMemory(device, &allocInfo, NULL, &particleMemory) != VK_SUCCESS) {
        printf("FATAL: Failed to allocate GPU VRAM!\n");
        return -1;
    }

    // Step D: Marry the Buffer to the Memory
    vkBindBufferMemory(device, particleBuffer, particleMemory, 0);

    // Step E: SEED THE PARTICLES (Write from CPU directly into GPU VRAM)
    Particle* mappedData;
    vkMapMemory(device, particleMemory, 0, bufferSize, 0, (void**)&mappedData);

    for (uint32_t i = 0; i < particleCount; i++) {
        mappedData[i].pos[0] = 0.0f; mappedData[i].pos[1] = 0.0f; mappedData[i].pos[2] = 0.0f;
        mappedData[i].vel[0] = 0.0f; mappedData[i].vel[1] = 0.0f; mappedData[i].vel[2] = 0.0f;
        mappedData[i].seed = (float)i / (float)(particleCount - 1);
    }

    // We unmap it so the CPU lets go, leaving the data sitting purely on the GPU.
    vkUnmapMemory(device, particleMemory);

    printf("[SYSTEM] Successfully allocated %zu MB of VRAM for 1,000,000 Particles.\n", bufferSize / (1024 * 1024));
    // ========================================================
    // 3.7. LOAD THE SPIR-V SHADER
    // ========================================================
    size_t shaderSize;
    char* shaderCode = readShaderFile("swarm.spv", &shaderSize);

    VkShaderModuleCreateInfo shaderInfo = {0};
    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.codeSize = shaderSize;
    shaderInfo.pCode = (uint32_t*)shaderCode;

    VkShaderModule computeShaderModule;
    if (vkCreateShaderModule(device, &shaderInfo, NULL, &computeShaderModule) != VK_SUCCESS) {
        printf("FATAL: Failed to create shader module!\n"); return -1;
    }
    free(shaderCode);

    // ========================================================
    // 3.8. DESCRIPTOR SETS (Giving the Shader our VRAM pointer)
    // ========================================================
    // A. The Layout (Telling Vulkan "Expect 1 Storage Buffer at Binding 0")
    VkDescriptorSetLayoutBinding layoutBinding = {0};
    layoutBinding.binding = 0;
    layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    layoutBinding.descriptorCount = 1;
    // Let both the Physics cores and the Graphics cores access the memory!
    layoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {0};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &layoutBinding;

    VkDescriptorSetLayout descriptorSetLayout;
    vkCreateDescriptorSetLayout(device, &layoutInfo, NULL, &descriptorSetLayout);

    // B. The Pool (Where descriptors are allocated from)
    VkDescriptorPoolSize poolSize = {0};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo = {0};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    VkDescriptorPool descriptorPool;
    vkCreateDescriptorPool(device, &poolInfo, NULL, &descriptorPool);

    // C. Allocate the actual Descriptor Set
    VkDescriptorSetAllocateInfo allocSetInfo = {0};
    allocSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocSetInfo.descriptorPool = descriptorPool;
    allocSetInfo.descriptorSetCount = 1;
    allocSetInfo.pSetLayouts = &descriptorSetLayout;

    VkDescriptorSet descriptorSet;
    vkAllocateDescriptorSets(device, &allocSetInfo, &descriptorSet);

    // D. Connect the Descriptor Set to our particleBuffer!
    VkDescriptorBufferInfo dBufferInfo = {0};
    dBufferInfo.buffer = particleBuffer; // This is the VRAM buffer we made in 3.6!
    dBufferInfo.offset = 0;
    dBufferInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet descriptorWrite = {0};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &dBufferInfo;

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, NULL);

    // ========================================================
    // 3.9. CREATE THE COMPUTE PIPELINE
    // ========================================================
    // Tell the pipeline about our PushConstants
    VkPushConstantRange pushConstantRange = {0};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VkPipelineLayout pipelineLayout;
    vkCreatePipelineLayout(device, &pipelineLayoutInfo, NULL, &pipelineLayout);

    VkComputePipelineCreateInfo pipelineInfo = {0};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = computeShaderModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipelineLayout;

    VkPipeline computePipeline;
    vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &computePipeline);

    // ========================================================
    // 3.10. DISPATCH THE MATH TO THE GPU!
    // ========================================================
    // 1. Create a Command Pool & Command Buffer
    VkCommandPoolCreateInfo cmdPoolInfo = {0};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.queueFamilyIndex = graphicsComputeQueueIndex;
    // --- [THE FIX] Tell Vulkan we intend to recycle these buffers every frame! ---
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool commandPool;
    vkCreateCommandPool(device, &cmdPoolInfo, NULL, &commandPool);

    VkCommandBufferAllocateInfo cmdAllocInfo = {0};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &cmdAllocInfo, &commandBuffer);

    // 2. Start Recording Commands
    VkCommandBufferBeginInfo beginInfo = {0};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

    // Send our Push Constants (Time, DT, Center)
    PushConstants pc = {0};
    pc.center[0] = 0.0f; pc.center[1] = 5000.0f; pc.center[2] = 0.0f;
    pc.time = 1.0f;
    pc.dt = 0.016f;
    pc.noise_blend = 1.0f; // Metal Blend
    pc.particleCount = particleCount;

    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);

    // THE MAGIC TRIGGER: Dispatch 3907 thread groups of 256 cores (Covering 1,000,000 particles)
    uint32_t groupCountX = (particleCount + 255) / 256;
    vkCmdDispatch(commandBuffer, groupCountX, 1, 1);

    vkEndCommandBuffer(commandBuffer);

    // 3. Submit to the RTX 3050 and Wait!
    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    printf("[SYSTEM] Pushing 1,000,000 physics calculations to RTX 3050...\n");
    vkQueueSubmit(computeQueue, 1, &submitInfo, VK_NULL_HANDLE);

    // We force the CPU to wait here until the GPU finishes the math
    vkQueueWaitIdle(computeQueue);
    printf("[SYSTEM] GPU Compute Finished!\n");

    // ========================================================
    // 3.11. READ THE RESULTS BACK TO THE CPU
    // ========================================================
    Particle* outData;
    vkMapMemory(device, particleMemory, 0, bufferSize, 0, (void**)&outData);

    printf("\n--- PARTICLE DATA AFTER 1 FRAME OF GPU PHYSICS ---\n");
    for (int i = 0; i < 3; i++) {
        printf("Particle %d | POS: [%.2f, %.2f, %.2f] | VEL: [%.2f, %.2f, %.2f]\n",
            i,
            outData[i].pos[0], outData[i].pos[1], outData[i].pos[2],
            outData[i].vel[0], outData[i].vel[1], outData[i].vel[2]);
    }
    printf("--------------------------------------------------\n\n");

    vkUnmapMemory(device, particleMemory);
    // ========================================================
    // 3.12. THE SWAPCHAIN (The Bridge to the Monitor)
    // ========================================================
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(instance, window, NULL, &surface) != VK_SUCCESS) {
        printf("FATAL: Failed to create window surface!\n"); return -1;
    }

    // Get Surface Capabilities (to find the actual window size)
    VkSurfaceCapabilitiesKHR surfaceCaps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(chosenGPU, surface, &surfaceCaps);
    VkExtent2D swapchainExtent = surfaceCaps.currentExtent;

    // Create the Swapchain
    VkSwapchainCreateInfoKHR swapchainInfo = {0};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = surface;
    swapchainInfo.minImageCount = surfaceCaps.minImageCount + 1; // Double/Triple buffering
    swapchainInfo.imageFormat = VK_FORMAT_B8G8R8A8_SRGB;
    swapchainInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchainInfo.imageExtent = swapchainExtent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.preTransform = surfaceCaps.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR; // V-Sync enabled
    swapchainInfo.clipped = VK_TRUE;

    VkSwapchainKHR swapchain;
    if (vkCreateSwapchainKHR(device, &swapchainInfo, NULL, &swapchain) != VK_SUCCESS) {
        printf("FATAL: Failed to create Swapchain!\n"); return -1;
    }

    // Extract the Images from the Swapchain
    uint32_t imageCount;
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, NULL);
    VkImage* swapchainImages = malloc(imageCount * sizeof(VkImage));
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages);

    // Create Image Views (Vulkan needs these to actually draw into the images)
    VkImageView* swapchainImageViews = malloc(imageCount * sizeof(VkImageView));
    for (uint32_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo viewInfo = {0};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_B8G8R8A8_SRGB;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        vkCreateImageView(device, &viewInfo, NULL, &swapchainImageViews[i]);
    }
    printf("[SYSTEM] Swapchain created with %d images.\n", imageCount);
    // ========================================================
    // 3.13. THE GRAPHICS PIPELINE (Additive Blending)
    // ========================================================
    size_t vertSize, fragSize;
    char* vertCode = readShaderFile("render_vert.spv", &vertSize);
    char* fragCode = readShaderFile("render_frag.spv", &fragSize);

    VkShaderModuleCreateInfo vertInfo = {0}; vertInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO; vertInfo.codeSize = vertSize; vertInfo.pCode = (uint32_t*)vertCode;
    VkShaderModuleCreateInfo fragInfo = {0}; fragInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO; fragInfo.codeSize = fragSize; fragInfo.pCode = (uint32_t*)fragCode;
    VkShaderModule vertModule, fragModule;
    vkCreateShaderModule(device, &vertInfo, NULL, &vertModule); vkCreateShaderModule(device, &fragInfo, NULL, &fragModule);
    free(vertCode); free(fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[2] = {{0}};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; shaderStages[0].module = vertModule; shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; shaderStages[1].module = fragModule; shaderStages[1].pName = "main";

    // NO VERTEX BUFFER REQUIRED! We generate it in the shader.
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {0}; vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {0}; inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState = {0}; viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; viewportState.viewportCount = 1; viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterizer = {0}; rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rasterizer.polygonMode = VK_POLYGON_MODE_FILL; rasterizer.lineWidth = 1.0f; rasterizer.cullMode = VK_CULL_MODE_NONE;
    VkPipelineMultisampleStateCreateInfo multisampling = {0}; multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // ADDITIVE BLENDING (Looks like glowing energy)
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {0};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending = {0}; colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; colorBlending.attachmentCount = 1; colorBlending.pAttachments = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicStateInfo = {0}; dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; dynamicStateInfo.dynamicStateCount = 2; dynamicStateInfo.pDynamicStates = dynamicStates;

    VkPushConstantRange gfxPushRange = {0}; gfxPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; gfxPushRange.offset = 0; gfxPushRange.size = sizeof(RenderPushConstants);
    VkPipelineLayoutCreateInfo gfxLayoutInfo = {0}; gfxLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; gfxLayoutInfo.setLayoutCount = 1; gfxLayoutInfo.pSetLayouts = &descriptorSetLayout; gfxLayoutInfo.pushConstantRangeCount = 1; gfxLayoutInfo.pPushConstantRanges = &gfxPushRange;
    VkPipelineLayout graphicsPipelineLayout; vkCreatePipelineLayout(device, &gfxLayoutInfo, NULL, &graphicsPipelineLayout);

    // VULKAN 1.3 DYNAMIC RENDERING (No Render Passes!)
    VkPipelineRenderingCreateInfo renderingCreateInfo = {0}; renderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO; renderingCreateInfo.colorAttachmentCount = 1; renderingCreateInfo.pColorAttachmentFormats = &swapchainInfo.imageFormat;

    VkGraphicsPipelineCreateInfo gfxPipelineInfo = {0}; gfxPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; gfxPipelineInfo.pNext = &renderingCreateInfo;
    gfxPipelineInfo.stageCount = 2; gfxPipelineInfo.pStages = shaderStages; gfxPipelineInfo.pVertexInputState = &vertexInputInfo; gfxPipelineInfo.pInputAssemblyState = &inputAssembly; gfxPipelineInfo.pViewportState = &viewportState; gfxPipelineInfo.pRasterizationState = &rasterizer; gfxPipelineInfo.pMultisampleState = &multisampling; gfxPipelineInfo.pColorBlendState = &colorBlending; gfxPipelineInfo.pDynamicState = &dynamicStateInfo; gfxPipelineInfo.layout = graphicsPipelineLayout;

    VkPipeline graphicsPipeline; vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gfxPipelineInfo, NULL, &graphicsPipeline);
    // ========================================================
    // 4. THE MAIN LOOP (PHYSICS + GRAPHICS)
    // ========================================================
    printf("[SYSTEM] Entering Main Loop. Close the window to exit.\n");

    VkSemaphore imageAvailableSemaphore;
    VkSemaphoreCreateInfo semaInfo = {0}; semaInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCreateSemaphore(device, &semaInfo, NULL, &imageAvailableSemaphore);

    float engine_time = 0.0f;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        // --- [NEW] GRACEFUL FULLSCREEN EXIT ---
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        engine_time += 0.016f; // Advance time by 16ms every frame

        uint32_t imageIndex;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

        vkResetCommandBuffer(commandBuffer, 0);
        VkCommandBufferBeginInfo beginInfo = {0}; beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        // --------------------------------------------------------
        // STEP A: THE COMPUTE PASS (PHYSICS)
        // --------------------------------------------------------
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

        PushConstants pc = {0};
        pc.center[0] = 0.0f; pc.center[1] = 5000.0f; pc.center[2] = 0.0f;
        pc.time = engine_time;
        pc.dt = 0.016f;
        pc.noise_blend = 1.0f;
        pc.particleCount = particleCount;
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);

        // Dispatch 1,000,000 threads!
        uint32_t groupCountX = (particleCount + 255) / 256;
        vkCmdDispatch(commandBuffer, groupCountX, 1, 1);

        // --------------------------------------------------------
        // STEP B: THE MEMORY BARRIER
        // Force the GPU to finish Compute writes before Vertex reads!
        // --------------------------------------------------------
        VkMemoryBarrier compBarrier = {0};
        compBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        compBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        compBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            0, 1, &compBarrier, 0, NULL, 0, NULL);

        // --------------------------------------------------------
        // STEP C: THE GRAPHICS PASS (RENDERING)
        // --------------------------------------------------------
        VkImageMemoryBarrier imgBarrier = {0};
        imgBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imgBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imgBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imgBarrier.image = swapchainImages[imageIndex];
        imgBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imgBarrier.subresourceRange.levelCount = 1; imgBarrier.subresourceRange.layerCount = 1;
        imgBarrier.srcAccessMask = 0; imgBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1, &imgBarrier);

        VkRenderingAttachmentInfo colorAttachment = {0};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = swapchainImageViews[imageIndex];
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color.float32[0] = 0.05f; colorAttachment.clearValue.color.float32[1] = 0.05f;
        colorAttachment.clearValue.color.float32[2] = 0.15f; colorAttachment.clearValue.color.float32[3] = 1.0f;

        VkRenderingInfo renderInfo = {0}; renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderInfo.renderArea.extent = swapchainExtent; renderInfo.layerCount = 1;
        renderInfo.colorAttachmentCount = 1; renderInfo.pColorAttachments = &colorAttachment;

        vkCmdBeginRendering(commandBuffer, &renderInfo);

        VkViewport viewport = {0.0f, 0.0f, (float)swapchainExtent.width, (float)swapchainExtent.height, 0.0f, 1.0f};
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        VkRect2D scissor = {{0, 0}, swapchainExtent};
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipelineLayout, 0, 1, &descriptorSet, 0, NULL);

        RenderPushConstants rpc = {0};
        rpc.pos[0] = 0.0f; rpc.pos[1] = 5000.0f; rpc.pos[2] = -12000.0f;
        rpc.fwd[0] = 0.0f; rpc.fwd[1] = 0.0f; rpc.fwd[2] = 1.0f;
        rpc.right[0] = 1.0f; rpc.right[1] = 0.0f; rpc.right[2] = 0.0f;
        rpc.up[0] = 0.0f; rpc.up[1] = 1.0f; rpc.up[2] = 0.0f;
        rpc.fov = 120.0f; rpc.w = (float)swapchainExtent.width; rpc.h = (float)swapchainExtent.height;
        vkCmdPushConstants(commandBuffer, graphicsPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(RenderPushConstants), &rpc);

        vkCmdDraw(commandBuffer, 6, particleCount, 0, 0);

        vkCmdEndRendering(commandBuffer);

        imgBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        imgBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        imgBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; imgBarrier.dstAccessMask = 0;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &imgBarrier);

        vkEndCommandBuffer(commandBuffer);

        // --------------------------------------------------------
        // STEP D: SUBMIT AND PRESENT
        // --------------------------------------------------------
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSubmitInfo submitInfo = {0}; submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1; submitInfo.pWaitSemaphores = &imageAvailableSemaphore;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1; submitInfo.pCommandBuffers = &commandBuffer;
        vkQueueSubmit(computeQueue, 1, &submitInfo, VK_NULL_HANDLE);

        VkPresentInfoKHR presentInfo = {0}; presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.swapchainCount = 1; presentInfo.pSwapchains = &swapchain;
        presentInfo.pImageIndices = &imageIndex;
        vkQueuePresentKHR(computeQueue, &presentInfo);

        vkQueueWaitIdle(computeQueue);
    }

    // ========================================================
    // 5. GRACEFUL TEARDOWN
    // ========================================================
    printf("[SYSTEM] Shutting down...\n");

    // CRITICAL: Wait for the GPU to finish rendering its final frame before we start deleting memory!
    vkDeviceWaitIdle(device);

    // 1. Destroy Synchronization Objects
    vkDestroySemaphore(device, imageAvailableSemaphore, NULL);

    // 2. Destroy Pipelines & Layouts
    vkDestroyPipeline(device, graphicsPipeline, NULL);
    // vkCreateShaderModule /* Wait, we already destroyed the shader modules! */
    vkDestroyPipelineLayout(device, graphicsPipelineLayout, NULL);
    vkDestroyPipeline(device, computePipeline, NULL);
    vkDestroyPipelineLayout(device, pipelineLayout, NULL);

    // --- [THE FIX] Actually destroying the shader modules! ---
    vkDestroyShaderModule(device, computeShaderModule, NULL);
    vkDestroyShaderModule(device, vertModule, NULL);
    vkDestroyShaderModule(device, fragModule, NULL);

    // 3. Destroy Descriptors
    vkDestroyDescriptorPool(device, descriptorPool, NULL);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, NULL);

    // 4. Destroy Command Pool (This automatically destroys all command buffers inside it)
    vkDestroyCommandPool(device, commandPool, NULL);

    // 5. Destroy Swapchain & Image Views
    for (uint32_t i = 0; i < imageCount; i++) {
        vkDestroyImageView(device, swapchainImageViews[i], NULL);
    }
    free(swapchainImageViews);
    free(swapchainImages);
    vkDestroySwapchainKHR(device, swapchain, NULL);

    // 6. Free the Particle VRAM
    vkDestroyBuffer(device, particleBuffer, NULL);
    vkFreeMemory(device, particleMemory, NULL);

    // 7. Destroy the Logical Device
    vkDestroyDevice(device, NULL);

    // 8. Destroy the Window Surface & Instance
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);

    // 9. Shutdown OS Window
    glfwDestroyWindow(window);
    glfwTerminate();

    printf("[SYSTEM] Clean shutdown complete. Goodbye!\n");
    return 0;
}
