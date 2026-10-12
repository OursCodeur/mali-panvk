#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define CHECK(call) do { VkResult r = (call); if (r != VK_SUCCESS) { \
    fprintf(stderr, "%s: %d, line %d\n", #call, r, __LINE__); exit(1); } } while (0)

static VkPhysicalDevice physical;
static VkDevice device;
static VkQueue queue;
static VkPhysicalDeviceMemoryProperties memory;

static VkDeviceMemory allocate(VkMemoryRequirements req, VkMemoryPropertyFlags flags)
{
    uint32_t i;
    for (i=0; i<memory.memoryTypeCount; ++i)
        if ((req.memoryTypeBits & (1u<<i)) &&
            (memory.memoryTypes[i].propertyFlags & flags)==flags) break;
    if (i==memory.memoryTypeCount) { fprintf(stderr,"No memory type\n"); exit(1); }
    VkMemoryAllocateInfo info={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize=req.size,.memoryTypeIndex=i};
    VkDeviceMemory result;
    CHECK(vkAllocateMemory(device,&info,NULL,&result));
    return result;
}

static VkImage make_image(VkDeviceMemory *mem, VkImageView *view)
{
    VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,
        .extent={1,1,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,
        .tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    VkImage image;
    CHECK(vkCreateImage(device,&ci,NULL,&image));
    VkMemoryRequirements req;vkGetImageMemoryRequirements(device,image,&req);
    *mem=allocate(req,0);CHECK(vkBindImageMemory(device,image,*mem,0));
    VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image=image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=ci.format,
        .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    CHECK(vkCreateImageView(device,&vi,NULL,view));return image;
}

static VkShaderModule load_shader(const char *path)
{
    FILE *f=fopen(path,"rb");if(!f){perror(path);exit(1);}
    fseek(f,0,SEEK_END);size_t size=ftell(f);rewind(f);
    uint32_t *code=malloc(size);
    if(fread(code,1,size,f)!=size) exit(1);
    fclose(f);
    VkShaderModuleCreateInfo ci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize=size,.pCode=code};
    VkShaderModule shader;CHECK(vkCreateShaderModule(device,&ci,NULL,&shader));
    free(code);return shader;
}

int main(int argc,char **argv)
{
    setbuf(stdout,NULL);
    unsigned width=argc>1?atoi(argv[1]):640, height=argc>2?atoi(argv[2]):480;
    VkApplicationInfo ai={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName="Clockwork driver check",.apiVersion=VK_API_VERSION_1_3};
    VkInstanceCreateInfo ii={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&ai};
    VkInstance instance;CHECK(vkCreateInstance(&ii,NULL,&instance));
    uint32_t n=1;CHECK(vkEnumeratePhysicalDevices(instance,&n,&physical));
    VkPhysicalDeviceProperties properties;vkGetPhysicalDeviceProperties(physical,&properties);
    printf("Device: %s\n",properties.deviceName);
    if(properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU){fprintf(stderr,"CPU renderer rejected\n");return 1;}
    vkGetPhysicalDeviceMemoryProperties(physical,&memory);
    uint32_t families=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&families,NULL);
    VkQueueFamilyProperties *qs=calloc(families,sizeof(*qs));
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&families,qs);
    uint32_t family=0;while(family<families && !(qs[family].queueFlags&VK_QUEUE_COMPUTE_BIT))family++;
    free(qs);if(family==families)return 1;
    float priority=1;
    VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkPhysicalDeviceVulkan13Features f13={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2=VK_TRUE,.dynamicRendering=VK_TRUE,.maintenance4=VK_TRUE};
    VkPhysicalDeviceVulkan12Features f12={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext=&f13,.timelineSemaphore=VK_TRUE,.runtimeDescriptorArray=VK_TRUE,
        .shaderSampledImageArrayNonUniformIndexing=VK_TRUE,
        .descriptorBindingPartiallyBound=VK_TRUE,.descriptorBindingVariableDescriptorCount=VK_TRUE,
        .descriptorBindingSampledImageUpdateAfterBind=VK_TRUE,
        .descriptorBindingUpdateUnusedWhilePending=VK_TRUE};
    VkPhysicalDeviceFeatures features={.shaderSampledImageArrayDynamicIndexing=VK_TRUE};
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&f12,
        .queueCreateInfoCount=1,.pQueueCreateInfos=&qi,.pEnabledFeatures=&features};
    CHECK(vkCreateDevice(physical,&di,NULL,&device));vkGetDeviceQueue(device,family,0,&queue);

    VkImageCreateInfo ici={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,
        .extent={width,height,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,
        .tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
    VkImage target;CHECK(vkCreateImage(device,&ici,NULL,&target));
    VkMemoryRequirements req;vkGetImageMemoryRequirements(device,target,&req);
    VkDeviceMemory targetmem=allocate(req,0);CHECK(vkBindImageMemory(device,target,targetmem,0));
    VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image=target,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=ici.format,.subresourceRange=range};
    VkImageView view;CHECK(vkCreateImageView(device,&vi,NULL,&view));
    VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size=width*height*4,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VkBuffer buffer;CHECK(vkCreateBuffer(device,&bi,NULL,&buffer));
    vkGetBufferMemoryRequirements(device,buffer,&req);
    VkDeviceMemory output=allocate(req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    CHECK(vkBindBufferMemory(device,buffer,output,0));
    uint32_t *mapped;CHECK(vkMapMemory(device,output,0,VK_WHOLE_SIZE,0,(void**)&mapped));
    memset(mapped,0,width*height*4);
    VkCommandPoolCreateInfo cpi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};
    VkCommandPool commands;CHECK(vkCreateCommandPool(device,&cpi,NULL,&commands));
    VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool=commands,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer command;CHECK(vkAllocateCommandBuffers(device,&cai,&command));
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkBeginCommandBuffer(command,&begin));
    VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=target,.subresourceRange=range};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,NULL,0,NULL,1,&barrier);
    VkRenderingAttachmentInfo att={.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView=view,.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue={.color={.float32={1,0,0,1}}}};
    VkRenderingInfo ri={.sType=VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea={{0,0},{width,height}},.layerCount=1,.colorAttachmentCount=1,.pColorAttachments=&att};
    vkCmdBeginRendering(command,&ri);
    vkCmdEndRendering(command);
    barrier.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
    VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={width,height,1}};
    vkCmdCopyImageToBuffer(command,target,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
    VkMemoryBarrier hostbarrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&hostbarrier,0,NULL,0,NULL);
    CHECK(vkEndCommandBuffer(command));
    VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;CHECK(vkCreateFence(device,&fi,NULL,&fence));
    VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
    CHECK(vkQueueSubmit(queue,1,&si,fence));
    CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));
    unsigned errors=0;
    for(unsigned i=0;i<width*height;i++)if(mapped[i]!=0xff0000ffu){
        if(errors<8)printf("Pixel %u: %08x\n",i,mapped[i]);errors++;
    }
    printf("Clear %ux%u: %u errors\n",width,height,errors);
    vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,commands,NULL);
    vkDestroyImageView(device,view,NULL);vkDestroyImage(device,target,NULL);vkFreeMemory(device,targetmem,NULL);
    vkUnmapMemory(device,output);vkDestroyBuffer(device,buffer,NULL);vkFreeMemory(device,output,NULL);
    vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);
    return errors?2:0;
}
