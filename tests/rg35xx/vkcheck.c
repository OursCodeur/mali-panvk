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
    if(argc<4){fprintf(stderr,"usage: vkcheck shader group_x group_y [arithmetic]\n");return 1;}
    unsigned gx=atoi(argv[2]),gy=atoi(argv[3]),count=gx*gy,total=2*count;
    VkApplicationInfo ai={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName="Clockwork driver check",.apiVersion=VK_API_VERSION_1_3};
    VkInstanceCreateInfo ii={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&ai};
    VkInstance instance;CHECK(vkCreateInstance(&ii,NULL,&instance));
    uint32_t n=1;CHECK(vkEnumeratePhysicalDevices(instance,&n,&physical));
    VkPhysicalDeviceProperties properties;vkGetPhysicalDeviceProperties(physical,&properties);
    printf("Device: %s; group %u x %u\n",properties.deviceName,gx,gy);
    if(properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU){fprintf(stderr,"CPU renderer rejected\n");return 1;}
    if(properties.limits.maxComputeWorkGroupInvocations<count){fprintf(stderr,"Workgroup unsupported\n");return 1;}
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
    VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=total*16,
        .usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
    VkBuffer buffer;CHECK(vkCreateBuffer(device,&bi,NULL,&buffer));
    VkMemoryRequirements req;vkGetBufferMemoryRequirements(device,buffer,&req);
    VkDeviceMemory output=allocate(req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    CHECK(vkBindBufferMemory(device,buffer,output,0));
    uint32_t *mapped;CHECK(vkMapMemory(device,output,0,VK_WHOLE_SIZE,0,(void**)&mapped));
    memset(mapped,0,total*16);
    VkImage images[2];VkImageView views[2];VkDeviceMemory image_memory[2];
    for(unsigned i=0;i<2;i++) images[i]=make_image(&image_memory[i],&views[i]);
    VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,
        .mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    VkSampler sampler;CHECK(vkCreateSampler(device,&si,NULL,&sampler));
    VkDescriptorSetLayoutBinding bindings[]={
        {.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.descriptorCount=16384,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT}};
    VkDescriptorBindingFlags flags[]={0,VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT|
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT|VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT|
        VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT};
    VkDescriptorSetLayoutBindingFlagsCreateInfo fi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount=2,.pBindingFlags=flags};
    VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext=&fi,.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount=2,.pBindings=bindings};
    VkDescriptorSetLayout layout;CHECK(vkCreateDescriptorSetLayout(device,&li,NULL,&layout));
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,16384}};
    VkDescriptorPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags=VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,.maxSets=1,.poolSizeCount=2,.pPoolSizes=sizes};
    VkDescriptorPool pool;CHECK(vkCreateDescriptorPool(device,&pi,NULL,&pool));
    uint32_t descriptors=16384;
    VkDescriptorSetVariableDescriptorCountAllocateInfo variable={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .descriptorSetCount=1,.pDescriptorCounts=&descriptors};
    VkDescriptorSetAllocateInfo dai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext=&variable,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&layout};
    VkDescriptorSet set;CHECK(vkAllocateDescriptorSets(device,&dai,&set));
    VkDescriptorBufferInfo buffer_info={buffer,0,VK_WHOLE_SIZE};
    VkDescriptorImageInfo image_info[2]={{sampler,views[0],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                       {sampler,views[0],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkWriteDescriptorSet writes[]={
        {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,.descriptorCount=1,
         .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&buffer_info},
        {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=1,.dstArrayElement=16382,
         .descriptorCount=2,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=image_info}};
    vkUpdateDescriptorSets(device,2,writes,0,NULL);
    VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&layout};
    VkPipelineLayout pipeline_layout;CHECK(vkCreatePipelineLayout(device,&pli,NULL,&pipeline_layout));
    VkShaderModule shader=load_shader(argv[1]);
    VkComputePipelineCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,
                .module=shader,.pName="main"},.layout=pipeline_layout};
    VkPipeline pipeline;CHECK(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pci,NULL,&pipeline));
    puts("Pipeline compiled");
    VkCommandPoolCreateInfo cpi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};
    VkCommandPool commands;CHECK(vkCreateCommandPool(device,&cpi,NULL,&commands));
    VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool=commands,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer command;CHECK(vkAllocateCommandBuffers(device,&cai,&command));
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkBeginCommandBuffer(command,&begin));
    VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    for(unsigned i=0;i<2;i++){
        VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=images[i],.subresourceRange=range};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
        VkClearColorValue color={.float32={i==0,i==1,0,1}};
        vkCmdClearColorImage(command,images[i],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&range);
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,NULL,0,NULL,1,&barrier);
    }
    vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
    vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_layout,0,1,&set,0,NULL);
    vkCmdDispatch(command,2,1,1);
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
    CHECK(vkEndCommandBuffer(command));
    VkFenceCreateInfo fence_info={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;CHECK(vkCreateFence(device,&fence_info,NULL,&fence));
    unsigned errors=0;
    for(unsigned pass=0;pass<2;pass++){
        image_info[0].imageView=views[pass==0];
        writes[1].descriptorCount=1;vkUpdateDescriptorSets(device,1,&writes[1],0,NULL);
        VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
        CHECK(vkQueueSubmit(queue,1,&submit,fence));
        CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));
        for(unsigned i=0;i<total;i++){
            uint32_t expected[]={((i&1)||pass||argc>4)?0xff0000ffu:0xff00ff00u,
                count*(count-1)/2+100*count*(i/count),gx,gx*(i/count)+(i%gx)};
            if(memcmp(mapped+4*i,expected,sizeof(expected))){
                if(errors<8)printf("Mismatch pass %u slot %u: %08x %u %u %u; expected %08x %u %u %u\n",pass,i,
                    mapped[4*i],mapped[4*i+1],mapped[4*i+2],mapped[4*i+3],
                    expected[0],expected[1],expected[2],expected[3]);
                errors++;
            }
        }
        printf("Pass %u: %u invocations checked, %u cumulative errors\n",pass,total,errors);
        CHECK(vkResetFences(device,1,&fence));
    }
    CHECK(vkDeviceWaitIdle(device));
    vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,commands,NULL);
    vkDestroyPipeline(device,pipeline,NULL);vkDestroyShaderModule(device,shader,NULL);
    vkDestroyPipelineLayout(device,pipeline_layout,NULL);vkDestroyDescriptorPool(device,pool,NULL);
    vkDestroyDescriptorSetLayout(device,layout,NULL);vkDestroySampler(device,sampler,NULL);
    for(unsigned i=0;i<2;i++){vkDestroyImageView(device,views[i],NULL);vkDestroyImage(device,images[i],NULL);vkFreeMemory(device,image_memory[i],NULL);}
    vkUnmapMemory(device,output);vkDestroyBuffer(device,buffer,NULL);vkFreeMemory(device,output,NULL);
    vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);
    return errors?2:0;
}
