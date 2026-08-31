/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2025-2026 Baldur Karlsson
 * Copyright (c) 2026 Igalia, S.L.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "3rdparty/fmt/core.h"
#include "vk_test.h"

RD_TEST(VK_Descriptor_Heap, VulkanGraphicsTest)
{
  static constexpr const char *Description =
      "Test of EXT_descriptor_heap based bindings and different edge cases.";

  VkPhysicalDeviceDescriptorHeapFeaturesEXT descHeapFeatures = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT,
  };
  VkPhysicalDeviceDescriptorHeapPropertiesEXT descHeapProps = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT,
  };

  bool testSecondary = false;
  uint32_t imgDescSize = 0;
  uint32_t bufDescSize = 0;
  uint32_t sampDescSize = 0;

  std::string header = R"EOSHADER(

#version 460 core

#extension GL_EXT_samplerless_texture_functions : require
#extension GL_EXT_descriptor_heap : require
#extension GL_EXT_nonuniform_qualifier : require

)EOSHADER";

  const std::string passthroughGeom = header + R"EOSHADER(

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

void main()
{
  for(int i = 0; i < 3; i++)
  {
    gl_Position = gl_in[i].gl_Position;

    EmitVertex();
  }
  EndPrimitive();
}

)EOSHADER";

  const VkPipelineShaderStageCreateInfo CompileTemplatedFS(const char *descs, const char *body)
  {
    const char *pixelTemplate = R"EOSHADER(
{}

layout(push_constant) uniform PushData {{
  vec4 data[];
}} push;

layout(location = 0, index = 0) out vec4 Color;

{};

void main()
{{
  vec2 uv = vec2(gl_FragCoord.xy - ivec2(push.data[0].zw))/push.data[0].xx;
  ivec2 uvi = ivec2(uv*push.data[0].yy);

  Color = vec4(uv.xy, 0.0f, 1.0f);

  {};
}}

)EOSHADER";

    return CompileShaderModule(fmt::format(pixelTemplate, header, descs, body), ShaderLang::glsl,
                               ShaderStage::frag, "main", {}, SPIRVTarget::vulkan12);
  }

  void Prepare(int argc, char **argv)
  {
    devExts.push_back(VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME);
    devExts.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    devExts.push_back(VK_KHR_SHADER_UNTYPED_POINTERS_EXTENSION_NAME);
    optDevExts.push_back(VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME);
    optDevExts.push_back(VK_KHR_MAINTENANCE_7_EXTENSION_NAME);

    features.fragmentStoresAndAtomics = VK_TRUE;
    features.geometryShader = VK_TRUE;

    VulkanGraphicsTest::Prepare(argc, argv);

    if(!Avail.empty())
      return;

    static VkPhysicalDeviceShaderUntypedPointersFeaturesKHR untypedFeats = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_UNTYPED_POINTERS_FEATURES_KHR,
    };
    untypedFeats.shaderUntypedPointers = VK_TRUE;
    untypedFeats.pNext = (void *)devInfoNext;
    devInfoNext = &untypedFeats;

    static VkPhysicalDeviceCustomBorderColorFeaturesEXT borderFeats = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT,
    };

    if(hasExt(VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME))
    {
      // No need to check the properties, extension presence implies min 32
      // custom border color samplers.
      borderFeats.customBorderColors = VK_TRUE;
      borderFeats.pNext = (void *)devInfoNext;
      devInfoNext = &borderFeats;
    }

    // Enable this for the secondary test.
    static VkPhysicalDeviceMaintenance7FeaturesKHR maint7Feats = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_7_FEATURES_KHR,
    };
    if(hasExt(VK_KHR_MAINTENANCE_7_EXTENSION_NAME))
    {
      maint7Feats.maintenance7 = VK_TRUE;
      maint7Feats.pNext = (void *)devInfoNext;
      devInfoNext = &maint7Feats;
      testSecondary = true;
    }

    getPhysFeatures2(&descHeapFeatures);
    getPhysProperties2(&descHeapProps);

    imgDescSize = (uint32_t)descHeapProps.imageDescriptorSize;
    bufDescSize = (uint32_t)descHeapProps.bufferDescriptorSize;
    sampDescSize = (uint32_t)descHeapProps.samplerDescriptorSize;

    if(!descHeapFeatures.descriptorHeap)
      Avail = "Feature 'descriptorHeap' not available";

    descHeapFeatures.pNext = (void *)devInfoNext;
    devInfoNext = &descHeapFeatures;

    static VkPhysicalDeviceBufferDeviceAddressFeaturesKHR bufaddrFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR,
    };

    getPhysFeatures2(&bufaddrFeatures);

    if(!bufaddrFeatures.bufferDeviceAddress)
      Avail = "feature 'bufferDeviceAddress' not available";

    bufaddrFeatures.pNext = (void *)devInfoNext;
    devInfoNext = &bufaddrFeatures;

    static VkPhysicalDeviceDescriptorIndexingFeaturesEXT descIndexingEnable = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT,
    };

    descIndexingEnable.runtimeDescriptorArray = VK_TRUE;
    descIndexingEnable.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
    descIndexingEnable.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    descIndexingEnable.descriptorBindingVariableDescriptorCount = VK_TRUE;

    descIndexingEnable.pNext = (void *)devInfoNext;
    devInfoNext = &descIndexingEnable;
  }

  static const uint32_t texSize = 4;

  uint32_t maxDescriptorAlign;

  struct HeapRange
  {
    uint32_t offset;
    uint32_t count;
    uint32_t stride;

    uint32_t index(uint32_t i) const
    {
      TEST_ASSERT(i < count, "overflowed allocated range");
      return offset + i * stride;
    }
  };

  uint32_t nextResourceOffset = 0;
  uint32_t nextSamplerOffset = 0;
  byte *heapMap = NULL;
  byte *samplerHeapMap = NULL;
  uint32_t heapAllocationLimit, samplerHeapAllocationLimit;

  HeapRange AllocResources(uint32_t count, uint32_t stride, uint32_t align = 0)
  {
    if(align == 0)
      align = maxDescriptorAlign;
    nextResourceOffset = AlignUp(nextResourceOffset, align);
    uint32_t offset = nextResourceOffset;
    nextResourceOffset += count * stride;
    TEST_ASSERT(nextResourceOffset <= heapAllocationLimit, "overflowed resource heap allocation");
    return HeapRange{offset, count, stride};
  }

  HeapRange AllocSamplers(uint32_t count, uint32_t stride)
  {
    nextSamplerOffset =
        AlignUp(nextSamplerOffset, (uint32_t)descHeapProps.samplerDescriptorAlignment);
    uint32_t offset = nextSamplerOffset;
    nextSamplerOffset += count * stride;
    TEST_ASSERT(nextSamplerOffset <= samplerHeapAllocationLimit,
                "overflowed sampler heap allocation");
    return HeapRange{offset, count, stride};
  }

  VkImageViewCreateInfo MakeTestImage(const char *name, const Vec4f &col)
  {
    // make images half one colour half black, so we can test samplers that are linear vs point
    Vec4f pixels[texSize * texSize] = {};

    AllocatedBuffer uploadBuf(
        this,
        vkh::BufferCreateInfo(texSize * texSize * sizeof(Vec4f), VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
        VmaAllocationCreateInfo({0, VMA_MEMORY_USAGE_CPU_TO_GPU}));

    AllocatedImage tex(
        this,
        vkh::ImageCreateInfo(texSize, texSize, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                             VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                 VK_IMAGE_USAGE_STORAGE_BIT),
        VmaAllocationCreateInfo(
            {VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, VMA_MEMORY_USAGE_GPU_ONLY}));
    setName(tex.image, name);

    for(int i = 0; i < texSize * texSize / 2; i++)
      pixels[i] = col;
    uploadBuf.upload(pixels);
    uploadBufferToImage(tex.image, {texSize, texSize, 1}, uploadBuf.buffer, VK_IMAGE_LAYOUT_GENERAL);

    return vkh::ImageViewCreateInfo(tex.image, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT);
  }

  VkImageViewCreateInfo MakeNextTestImage(const char *name)
  {
    const Vec4f colors[] = {
        {1.0, 0.0, 0.0, 1.0}, {0.0, 1.0, 0.0, 1.0}, {0.0, 0.0, 1.0, 1.0},
        {1.0, 0.0, 0.5, 1.0}, {0.5, 1.0, 0.0, 1.0}, {0.0, 0.5, 1.0, 1.0},
    };

    static int i = 0;
    return MakeTestImage(name, colors[i++ % ARRAY_COUNT(colors)]);
  }

  AllocatedBuffer MakeTestBuffer(const char *name, uint32_t offset, const Vec4f &data)
  {
    // use 256 aligned sizes for buffers so we can check this on all drivers, we don't care to test
    // aliasing caused by different sizes
    VkDeviceSize size = AlignUp(offset, 0x100U) + 0x2000;
    AllocatedBuffer ret(this,
                        vkh::BufferCreateInfo(size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR |
                                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                                                        VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
                                                        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT),
                        VmaAllocationCreateInfo({0, VMA_MEMORY_USAGE_CPU_TO_GPU}));
    setName(ret.buffer, name);
    byte *ptr = ret.map();
    // fill with garbage (that will be a relatively normal float value)
    memset(ptr, 0x3f, size);
    memcpy(ptr + offset, &data, sizeof(data));
    ret.unmap();

    return ret;
  }

  AllocatedBuffer MakeTestBuffer(const char *name, uint32_t offset, uint64_t data)
  {
    VkDeviceSize size = 0x1000;
    AllocatedBuffer ret(this,
                        vkh::BufferCreateInfo(size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR |
                                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                                                        VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
                                                        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT),
                        VmaAllocationCreateInfo({0, VMA_MEMORY_USAGE_CPU_TO_GPU}));
    setName(ret.buffer, name);
    byte *ptr = ret.map();
    // fill with garbage (that will be a relatively normal float value)
    memset(ptr, 0x3f, size);
    memcpy(ptr + offset, &data, sizeof(data));
    ret.unmap();

    return ret;
  }
  void WriteBufferDescriptor(uint32_t heapOffset, VkDescriptorType type, AllocatedBuffer & buf,
                             VkDeviceSize offset, VkDeviceSize size)
  {
    VkDeviceAddressRangeEXT addressRange = {buf.address + offset, size};

    VkResourceDescriptorInfoEXT info = {VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT};
    info.type = type;
    info.data.pAddressRange = &addressRange;

    VkHostAddressRangeEXT range = {heapMap + heapOffset, descHeapProps.bufferDescriptorSize};

    vkWriteResourceDescriptorsEXT(device, 1, &info, &range);
  }

  void WriteImageDescriptor(uint32_t offset, VkDescriptorType type, VkImageViewCreateInfo & imageView)
  {
    VkImageDescriptorInfoEXT imageInfo = {VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT};
    imageInfo.pView = &imageView;
    imageInfo.layout = VK_IMAGE_LAYOUT_GENERAL;

    VkResourceDescriptorInfoEXT info = {VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT};
    info.type = type;
    info.data.pImage = &imageInfo;

    VkHostAddressRangeEXT range = {heapMap + offset, descHeapProps.imageDescriptorSize};

    vkWriteResourceDescriptorsEXT(device, 1, &info, &range);
  }

  HeapRange NextSingleImageDescriptor(const char *name, VkDescriptorType type, uint32_t align = 0)
  {
    VkImageViewCreateInfo image = MakeNextTestImage(name);
    HeapRange resourceRange = AllocResources(1, imgDescSize, align);
    WriteImageDescriptor(resourceRange.offset, type, image);
    return resourceRange;
  }

  HeapRange NextSingleSampler(const char *name, VkFilter filter)
  {
    vkh::SamplerCreateInfo sampler = vkh::SamplerCreateInfo(filter);

    // We could do verification of which sampler gets returned from the unit
    // test, if serialization and handling of
    // VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT was added.
#if 0
    VkDebugUtilsObjectNameInfoEXT nameInfo = {VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    nameInfo.objectType = VK_OBJECT_TYPE_UNKNOWN;
    nameInfo.pObjectName = "textureSeparateSampler_samp";
    sampler.pNext = &nameInfo;
#endif

    HeapRange samplerRange = AllocSamplers(1, sampDescSize);
    WriteSamplerDescriptor(samplerRange.offset, sampler);
    return samplerRange;
  }

  void WriteSamplerDescriptor(uint32_t offset, VkSamplerCreateInfo & sampler)
  {
    VkHostAddressRangeEXT range = {samplerHeapMap + offset, descHeapProps.samplerDescriptorSize};

    vkWriteSamplerDescriptorsEXT(device, 1, &sampler, &range);
  }

  class PushData
  {
  public:
    template <typename T>
    PushData(uint32_t offset, const T &val) : offset(offset), size(sizeof(T))
    {
      static_assert(sizeof(T) <= sizeof(data), "overflowed static push data size");
      memcpy(data, &val, sizeof(T));
    }
    uint32_t offset;
    uint32_t size;
    byte data[64];
  };

  enum TestExec
  {
    GRAPHICS_PRIMARY,
    GRAPHICS_SECONDARY,
  };

  class TestSetup
  {
  public:
    TestSetup(const char *name, VkPipeline pipeline, std::vector<PushData> pushData = {},
              TestExec testExec = GRAPHICS_PRIMARY)
        : name(name), pipeline(pipeline), pushData(std::move(pushData)), testExec(testExec)
    {
    }
    std::string name;
    std::vector<PushData> pushData;
    VkPipeline pipeline;
    TestExec testExec;
  };

  // Creates the fullscreen-quad VS we'll use for most of the test drawing.
  // This uses a UBO for the per-vertex positions, to ensure that postVS
  // successfully retains heap mappings.
  VkPipelineShaderStageCreateInfo CreateFullscreenVS()
  {
    const std::string fullscreenHeapVertex = R"EOSHADER(

#version 460 core

layout(set = 1, binding = 2, std140) uniform ubo { vec4 data[4]; } positions;

void main()
{
	gl_Position = positions.data[gl_VertexIndex];
}

)EOSHADER";

    VkDeviceSize size = 4 * sizeof(float) * 4;
    AllocatedBuffer buf(this,
                        vkh::BufferCreateInfo(size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR |
                                                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT),
                        VmaAllocationCreateInfo({0, VMA_MEMORY_USAGE_CPU_TO_GPU}));
    setName(buf.buffer, "positions");
    float *ptr = (float *)buf.map();

    for(int i = 0; i < 4; i++)
    {
      ptr[i * 4 + 0] = ((i & 1) != 0) ? 1.0f : -1.0f;
      ptr[i * 4 + 1] = ((i & 2) != 0) ? -1.0f : 1.0f;
      ptr[i * 4 + 2] = 0.0f;
      ptr[i * 4 + 3] = 1.0f;
    }
    buf.unmap();

    HeapRange vsPositionRange = AllocResources(1, bufDescSize);
    WriteBufferDescriptor(vsPositionRange.offset, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, buf, 0, size);
    // We use set/binding 1/2 just like many of the FS mappings, to encourage
    // confusion.
    static vkh::DescriptorSetAndBindingMapping map =
        vkh::DescriptorSetAndBindingMapping::constOffset(1, 2, vsPositionRange.offset, 0);
    static vkh::ShaderDescriptorSetAndBindingMappingInfo info(map);

    VkPipelineShaderStageCreateInfo ret =
        CompileShaderModule(fullscreenHeapVertex, ShaderLang::glsl, ShaderStage::vert, "main");
    ret.pNext = info;

    return ret;
  }

  int main()
  {
    vmaBDA = true;

    // initialise, create window, create context, etc
    if(!Init())
      return 3;

    // Common resource heap that we'll do most of our tests in.
    uint32_t heapSize = std::min(1024u * 1024u, (uint32_t)descHeapProps.maxResourceHeapSize);
    maxDescriptorAlign = (uint32_t)std::max(descHeapProps.imageDescriptorAlignment,
                                            descHeapProps.bufferDescriptorAlignment);
    AllocatedBuffer heap(
        this,
        vkh::BufferCreateInfo(heapSize, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR |
                                            VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT),
        VmaAllocationCreateInfo({0, VMA_MEMORY_USAGE_CPU_TO_GPU}));
    heapMap = heap.map();
    memset(heapMap, 0x3f, heapSize);
    uint32_t heapBindOffset = AlignUp(1024u, (uint32_t)descHeapProps.resourceHeapAlignment);
    heapMap += heapBindOffset;
    VkBindHeapInfoEXT heapInfo = {VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT};
    heapInfo.heapRange = {heap.address + heapBindOffset, heapSize - heapBindOffset};
    heapInfo.reservedRangeSize = descHeapProps.minResourceHeapReservedRange;
    heapInfo.reservedRangeOffset = heapInfo.heapRange.size - heapInfo.reservedRangeSize;
    heapInfo.reservedRangeOffset &= ~((VkDeviceSize)maxDescriptorAlign - 1);
    setName(heap.buffer, "resource heap");
    heapAllocationLimit = (uint32_t)heapInfo.reservedRangeOffset;

    // Common sampler heap that we'll do most of our tests in.
    uint32_t samplerHeapSize = std::min(1024u * 1024u, (uint32_t)descHeapProps.maxSamplerHeapSize);
    AllocatedBuffer samplerHeap(
        this,
        vkh::BufferCreateInfo(samplerHeapSize, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR |
                                                   VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT),
        VmaAllocationCreateInfo({0, VMA_MEMORY_USAGE_CPU_TO_GPU}));
    samplerHeapMap = samplerHeap.map();
    memset(samplerHeapMap, 0x3f, samplerHeapSize);
    uint32_t samplerHeapBindOffset = AlignUp(1024u, (uint32_t)descHeapProps.samplerHeapAlignment) * 2;
    samplerHeapMap += samplerHeapBindOffset;
    VkBindHeapInfoEXT samplerHeapInfo = {VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT};
    samplerHeapInfo.heapRange = {samplerHeap.address + samplerHeapBindOffset,
                                 samplerHeapSize - samplerHeapBindOffset};
    samplerHeapInfo.reservedRangeSize = descHeapProps.minSamplerHeapReservedRangeWithEmbedded;
    samplerHeapInfo.reservedRangeOffset =
        samplerHeapInfo.heapRange.size - samplerHeapInfo.reservedRangeSize;
    samplerHeapInfo.reservedRangeOffset &= ~(descHeapProps.samplerDescriptorAlignment - 1);
    setName(samplerHeap.buffer, "sampler heap");
    samplerHeapAllocationLimit = (uint32_t)samplerHeapInfo.reservedRangeOffset;

    AllocatedImage colatt(
        this,
        vkh::ImageCreateInfo(screenWidth, screenHeight, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT),
        VmaAllocationCreateInfo(
            {VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, VMA_MEMORY_USAGE_GPU_ONLY}));
    setName(colatt.image, "colatt");

    VkImageView colview = createImageView(vkh::ImageViewCreateInfo(
        colatt.image, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT));

    vkh::RenderPassCreator renderPassCreateInfo;

    renderPassCreateInfo.attachments.push_back(
        vkh::AttachmentDescription(VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED,
                                   VK_IMAGE_LAYOUT_GENERAL, VK_ATTACHMENT_LOAD_OP_CLEAR));

    renderPassCreateInfo.addSubpass({VkAttachmentReference({0, VK_IMAGE_LAYOUT_GENERAL})},
                                    VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED, {});

    VkRenderPass renderPass = createRenderPass(renderPassCreateInfo);

    VkFramebuffer framebuffer = createFramebuffer(
        vkh::FramebufferCreateInfo(renderPass, {colview}, mainWindow->scissor.extent));

    vkh::PipelineCreateFlags2CreateInfo pipeCreateFlags =
        vkh::PipelineCreateFlags2CreateInfo(VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT);

    // graphics pipeline creation shared across many sub-tests.
    vkh::GraphicsPipelineCreateInfo pipeCreateInfo;
    pipeCreateInfo.renderPass = renderPass;
    pipeCreateInfo.inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    pipeCreateInfo.pNext = &pipeCreateFlags;

    pipeCreateInfo.stages.resize(2);
    pipeCreateInfo.stages[0] = CreateFullscreenVS();

    std::vector<TestSetup> tests;

    // Start the resource and sampler heap allocations from some nonzero offset
    // in the buffer, to ensure that those offsets don't get misplaced in the
    // first test.
    (void)AllocResources(1, maxDescriptorAlign);
    (void)AllocSamplers(2, (uint32_t)descHeapProps.samplerDescriptorAlignment);

    {
      HeapRange resourceRange =
          NextSingleImageDescriptor("storageImage", VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
      pipeCreateInfo.stages[1] =
          CompileTemplatedFS("layout(set = 1, binding = 2, rgba32f) uniform image2D storageImage",
                             "Color = imageLoad(storageImage, uvi)");
      vkh::DescriptorSetAndBindingMapping map =
          vkh::DescriptorSetAndBindingMapping::constOffset(1, 2, resourceRange.offset, 0);
      vkh::ShaderDescriptorSetAndBindingMappingInfo info(map);
      pipeCreateInfo.stages[1].pNext = info;
      tests.push_back(
          TestSetup("storage image at const offset", createGraphicsPipeline(pipeCreateInfo)));
    }

    VkSamplerCreateInfo linearSampler = vkh::SamplerCreateInfo(VK_FILTER_LINEAR);
    VkSamplerCreateInfo nearestSampler = vkh::SamplerCreateInfo(VK_FILTER_NEAREST);

    {
      HeapRange resourceRange =
          NextSingleImageDescriptor("sampledImage", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
      HeapRange samplerRange = AllocSamplers(1, sampDescSize);
      WriteSamplerDescriptor(samplerRange.offset, linearSampler);
      pipeCreateInfo.stages[1] =
          CompileTemplatedFS("layout(set = 1, binding = 2) uniform sampler2D sampledImage",
                             "Color = texture(sampledImage, uv)");
      vkh::DescriptorSetAndBindingMapping map = vkh::DescriptorSetAndBindingMapping::constOffset(
          1, 2, resourceRange.offset, 0, samplerRange.offset, 0);
      vkh::ShaderDescriptorSetAndBindingMappingInfo info(map);
      pipeCreateInfo.stages[1].pNext = info;
      tests.push_back(
          TestSetup("sampled image at const offset", createGraphicsPipeline(pipeCreateInfo)));
    }

    {
      HeapRange resourceRange =
          NextSingleImageDescriptor("textureEmbeddedSampler", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
      vkh::SamplerCreateInfo embeddedSampler = vkh::SamplerCreateInfo(VK_FILTER_LINEAR);
      pipeCreateInfo.stages[1] = CompileTemplatedFS(
          "layout(set = 1, binding = 2) uniform sampler2D textureEmbeddedSampler",
          "Color = texture(textureEmbeddedSampler, uv)");
      vkh::DescriptorSetAndBindingMapping map = vkh::DescriptorSetAndBindingMapping::constOffset(
          1, 2, resourceRange.offset, 0, 2 * samplerHeapSize /* overflowing, but unused. */, 0);
      map.sourceData.constantOffset.pEmbeddedSampler = &embeddedSampler;
      vkh::ShaderDescriptorSetAndBindingMappingInfo info(map);
      pipeCreateInfo.stages[1].pNext = info;
      tests.push_back(TestSetup("sampled image with embedded sampler at const offset",
                                createGraphicsPipeline(pipeCreateInfo)));
    }

    {
      HeapRange resourceRange =
          NextSingleImageDescriptor("texSeparateSampler_tex", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
      HeapRange samplerRange = NextSingleSampler("texSeparateSampler_samp", VK_FILTER_LINEAR);

      pipeCreateInfo.stages[1] = CompileTemplatedFS(
          R"EOSHADER(layout(set = 1, binding = 2) uniform texture2D texSeparateSampler_tex;
                     layout(set = 1, binding = 3) uniform sampler texSeparateSampler_samp)EOSHADER",
          "Color = textureLod(sampler2D(texSeparateSampler_tex, texSeparateSampler_samp), "
          "uv, 0.0)");
      vkh::DescriptorSetAndBindingMapping mappings[2] = {
          vkh::DescriptorSetAndBindingMapping::constOffset(1, 2, resourceRange.offset, 0),
          vkh::DescriptorSetAndBindingMapping::constOffset(1, 3, samplerRange.offset, 0),
      };
      vkh::ShaderDescriptorSetAndBindingMappingInfo info(mappings, 2);
      pipeCreateInfo.stages[1].pNext = info;
      tests.push_back(
          TestSetup("separate sampler at const offsets", createGraphicsPipeline(pipeCreateInfo)));
    }

    {
      HeapRange resourceRange = AllocResources(5, imgDescSize * 2);
      auto image1 = MakeNextTestImage("texSepArray_tex_1");
      auto image2 = MakeNextTestImage("texSepArray_tex_2_unused");
      WriteImageDescriptor(resourceRange.index(1), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, image1);
      WriteImageDescriptor(resourceRange.index(2), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, image2);
      HeapRange samplerRange = AllocSamplers(4, sampDescSize);
      WriteSamplerDescriptor(samplerRange.index(2), linearSampler);

      pipeCreateInfo.stages[1] = CompileTemplatedFS(
          R"EOSHADER(layout(set = 1, binding = 2) uniform texture2D texSepArray_tex[3];
                     layout(set = 1, binding = 3) uniform sampler texSepArray_samp[4])EOSHADER",
          "Color = textureLod(sampler2D(texSepArray_tex[1], texSepArray_samp[2]), "
          "uv, 0.0)");
      vkh::DescriptorSetAndBindingMapping mappings[2] = {
          vkh::DescriptorSetAndBindingMapping::constOffset(1, 2, resourceRange.offset,
                                                           resourceRange.stride),
          vkh::DescriptorSetAndBindingMapping::constOffset(1, 3, samplerRange.offset,
                                                           samplerRange.stride),
      };
      vkh::ShaderDescriptorSetAndBindingMappingInfo info(mappings, 2);
      pipeCreateInfo.stages[1].pNext = info;
      tests.push_back(TestSetup("separate sampler array at const offsets",
                                createGraphicsPipeline(pipeCreateInfo)));
    }

    {
      HeapRange resourceRange = AllocResources(5, imgDescSize * 2);
      auto image1 = MakeNextTestImage("texSepArray_tex_1");
      auto image2 = MakeNextTestImage("texSepArray_tex_2_unused");
      WriteImageDescriptor(resourceRange.index(1), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, image1);
      WriteImageDescriptor(resourceRange.index(2), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, image2);
      HeapRange samplerRange = AllocSamplers(4, sampDescSize);
      WriteSamplerDescriptor(samplerRange.index(2), linearSampler);

      pipeCreateInfo.stages[1] = CompileTemplatedFS(
          R"EOSHADER(layout(set = 1, binding = 2) uniform texture2D texSepArray_tex[];
                     layout(set = 1, binding = 3) uniform sampler texSepArray_samp[])EOSHADER",
          "Color = textureLod(sampler2D(texSepArray_tex[1], texSepArray_samp[2]), "
          "uv, 0.0)");
      vkh::DescriptorSetAndBindingMapping mappings[2] = {
          vkh::DescriptorSetAndBindingMapping::constOffset(1, 2, resourceRange.offset,
                                                           resourceRange.stride),
          vkh::DescriptorSetAndBindingMapping::constOffset(1, 3, samplerRange.offset,
                                                           samplerRange.stride),
      };
      vkh::ShaderDescriptorSetAndBindingMappingInfo info(mappings, 2);
      pipeCreateInfo.stages[1].pNext = info;
      tests.push_back(TestSetup("separate sampler unsized array at const offsets",
                                createGraphicsPipeline(pipeCreateInfo)));
    }

    {
      HeapRange resourceRange = NextSingleImageDescriptor("texSeparateEmbeddedSampler_tex",
                                                          VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
      vkh::SamplerCreateInfo embeddedSampler = vkh::SamplerCreateInfo(VK_FILTER_LINEAR);

      pipeCreateInfo.stages[1] = CompileTemplatedFS(
          R"EOSHADER(layout(set = 1, binding = 2) uniform texture2D texSeparateEmbeddedSampler_tex;
                     layout(set = 1, binding = 3) uniform sampler texSeparateEmbeddedSampler_samp)EOSHADER",
          "Color = textureLod(sampler2D(texSeparateEmbeddedSampler_tex, "
          "texSeparateEmbeddedSampler_samp), uv, 0.0)");
      vkh::DescriptorSetAndBindingMapping mappings[2] = {
          vkh::DescriptorSetAndBindingMapping::constOffset(1, 2, resourceRange.offset, 0),
          vkh::DescriptorSetAndBindingMapping::constOffset(
              1, 3, 2 * samplerHeapSize /* overflowing, but unused. */, 0),
      };

      mappings[1].sourceData.constantOffset.pEmbeddedSampler = embeddedSampler;
      vkh::ShaderDescriptorSetAndBindingMappingInfo info(mappings, 2);
      pipeCreateInfo.stages[1].pNext = info;
      tests.push_back(TestSetup("separate pEmbeddedSampler at const offsets",
                                createGraphicsPipeline(pipeCreateInfo)));
    }

    {
      HeapRange resourceRange = AllocResources(5, imgDescSize * 2);
      auto image1 = MakeNextTestImage("siArray_1");
      auto image3 = MakeNextTestImage("siArray_3");
      auto image4 = MakeNextTestImage("siArrayUnused");
      WriteImageDescriptor(resourceRange.index(1), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, image1);
      WriteImageDescriptor(resourceRange.index(3), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, image3);
      WriteImageDescriptor(resourceRange.index(4), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, image4);
      HeapRange samplerRange = AllocSamplers(5, sampDescSize);
      WriteSamplerDescriptor(samplerRange.index(1), linearSampler);
      WriteSamplerDescriptor(samplerRange.index(3), nearestSampler);
      WriteSamplerDescriptor(samplerRange.index(4), linearSampler);
      pipeCreateInfo.stages[1] =
          CompileTemplatedFS("layout(set = 1, binding = 2) uniform sampler2D siArray[5]",
                             "Color = texture(siArray[1], uv) + texture(siArray[3], uv.yx)");
      vkh::DescriptorSetAndBindingMapping map = vkh::DescriptorSetAndBindingMapping::constOffset(
          1, 2, resourceRange.offset, resourceRange.stride, samplerRange.offset, samplerRange.stride);
      vkh::ShaderDescriptorSetAndBindingMappingInfo info(map);
      pipeCreateInfo.stages[1].pNext = info;
      tests.push_back(
          TestSetup("sampled image array at const offset", createGraphicsPipeline(pipeCreateInfo)));
    }

    AllocatedBuffer indirectIndexArrayBuffer(
        this, vkh::BufferCreateInfo(0x1000, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR),
        VmaAllocationCreateInfo({0, VMA_MEMORY_USAGE_CPU_TO_GPU}));
    setName(indirectIndexArrayBuffer.buffer, "indirect index array buffer");
    {
      byte *ptr = indirectIndexArrayBuffer.map();
      // fill with garbage (that will be a relatively normal float value)
      memset(ptr, 0x3f, 0x1000);
      // Indices into the heap
      ((uint32_t *)ptr)[10 + 1] = 1;
      ((uint32_t *)ptr)[10 + 3] = 2;
      ((uint32_t *)ptr)[10 + 4] = 0;

      // Indices into the sampler heap.
      ((uint32_t *)ptr)[100 + 1] = 3;
      ((uint32_t *)ptr)[100 + 3] = 4;

      // Combined image/samplers indices
      ((uint32_t *)ptr)[200 + 1] = (3 << 20) | 1;
      ((uint32_t *)ptr)[200 + 3] = (4 << 20) | 2;
      ((uint32_t *)ptr)[200 + 4] = (3 << 20) | 0;

      indirectIndexArrayBuffer.unmap();
    }

    {
      // Challenging index array case: needs feedback, needs the index array
      // access at feedback time, and uses a heap array stride that isn't just
      // the desciptor size.
      HeapRange resourceRange = AllocResources(3, 2 * imgDescSize);
      auto image1 = MakeNextTestImage("siIndirectIndexArray_1");
      auto image3 = MakeNextTestImage("siIndirectIndexArray_3");
      auto image4 = MakeNextTestImage("siIndirectIndexArrayUnused");
      WriteImageDescriptor(resourceRange.index(1), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, image1);
      WriteImageDescriptor(resourceRange.index(2), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, image3);
      WriteImageDescriptor(resourceRange.index(0), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, image4);

      HeapRange samplerRange = AllocSamplers(5, sampDescSize);
      WriteSamplerDescriptor(samplerRange.index(3), linearSampler);
      WriteSamplerDescriptor(samplerRange.index(4), nearestSampler);
      pipeCreateInfo.stages[1] = CompileTemplatedFS(
          "layout(set = 1, binding = 2) uniform sampler2D siIndirectIndexArray[5]",
          "Color = texture(siIndirectIndexArray[1], uv) + texture(siIndirectIndexArray[3], uv.yx)");
      VkDescriptorMappingSourceIndirectIndexArrayEXT mapData = {0};
      mapData.pushOffset = 24;
      mapData.addressOffset = 10 * 4;
      mapData.heapOffset = resourceRange.offset;
      mapData.heapIndexStride = resourceRange.stride;
      mapData.samplerAddressOffset = 100 * 4;
      mapData.samplerPushOffset = 32;
      mapData.samplerHeapOffset = samplerRange.offset;
      mapData.samplerHeapIndexStride = samplerRange.stride;

      vkh::DescriptorSetAndBindingMapping map =
          vkh::DescriptorSetAndBindingMapping::indirectIndexArray(1, 2, mapData);

      vkh::ShaderDescriptorSetAndBindingMappingInfo info(map);
      pipeCreateInfo.stages[1].pNext = info;
      tests.push_back(TestSetup("sampled image array at indirect index array offsets",
                                createGraphicsPipeline(pipeCreateInfo),
                                {{mapData.pushOffset, indirectIndexArrayBuffer.address},
                                 {mapData.samplerPushOffset, indirectIndexArrayBuffer.address}}));
    }

    {
      // Single-element index array case, handled by the static access path.
      HeapRange resourceRange = AllocResources(3, 2 * imgDescSize);
      auto image = MakeNextTestImage("siIndirectIndexArraySingle");
      WriteImageDescriptor(resourceRange.index(1), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, image);

      HeapRange samplerRange = AllocSamplers(3, sampDescSize);
      WriteSamplerDescriptor(samplerRange.index(2), linearSampler);
      pipeCreateInfo.stages[1] = CompileTemplatedFS(
          "layout(set = 1, binding = 2) uniform sampler2D siIndirectIndexArraySingle[1]",
          "Color = texture(siIndirectIndexArraySingle[0], uv)");
      VkDescriptorMappingSourceIndirectIndexArrayEXT mapData = {0};
      mapData.pushOffset = 24;
      mapData.addressOffset = 11 * 4;
      mapData.heapOffset = resourceRange.offset;
      mapData.heapIndexStride = resourceRange.stride;
      mapData.samplerAddressOffset = 13 * 4;
      mapData.samplerPushOffset = 32;
      mapData.samplerHeapOffset = samplerRange.offset;
      mapData.samplerHeapIndexStride = samplerRange.stride;

      vkh::DescriptorSetAndBindingMapping map =
          vkh::DescriptorSetAndBindingMapping::indirectIndexArray(1, 2, mapData);

      vkh::ShaderDescriptorSetAndBindingMappingInfo info(map);
      pipeCreateInfo.stages[1].pNext = info;
      tests.push_back(
          TestSetup("single-element sampled image array at indirect index array offsets",
                    createGraphicsPipeline(pipeCreateInfo),
                    {{mapData.pushOffset, indirectIndexArrayBuffer.address},
                     {mapData.samplerPushOffset, indirectIndexArrayBuffer.address}}));
    }

    {
      // Test useCombinedImageSamplerIndex
      HeapRange resourceRange = AllocResources(3, 2 * imgDescSize);
      auto image1 = MakeNextTestImage("siIndirectCombIndexArray_1");
      auto image3 = MakeNextTestImage("siIndirectCombIndexArray_3");
      auto image4 = MakeNextTestImage("siIndirectCombIndexArrayUnused");
      WriteImageDescriptor(resourceRange.index(1), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, image1);
      WriteImageDescriptor(resourceRange.index(2), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, image3);
      WriteImageDescriptor(resourceRange.index(0), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, image4);
      HeapRange samplerRange = AllocSamplers(5, sampDescSize);
      WriteSamplerDescriptor(samplerRange.index(3), linearSampler);
      WriteSamplerDescriptor(samplerRange.index(4), nearestSampler);
      pipeCreateInfo.stages[1] = CompileTemplatedFS(
          "layout(set = 1, binding = 2) uniform sampler2D siIndirectCombIndexArray[5]",
          "Color = texture(siIndirectCombIndexArray[1], uv) + texture(siIndirectCombIndexArray[3], "
          "uv.yx)");

      VkDescriptorMappingSourceIndirectIndexArrayEXT mapData = {0};

      mapData.useCombinedImageSamplerIndex = true;

      mapData.pushOffset = 24;
      mapData.addressOffset = 200 * 4;
      mapData.heapOffset = resourceRange.offset;
      mapData.heapIndexStride = resourceRange.stride;
      mapData.samplerHeapOffset = samplerRange.offset;
      mapData.samplerHeapIndexStride = samplerRange.stride;

      // These fields are unused with combined.
      mapData.samplerAddressOffset = 300 * 4;
      mapData.samplerPushOffset = 64;

      vkh::DescriptorSetAndBindingMapping map =
          vkh::DescriptorSetAndBindingMapping::indirectIndexArray(1, 2, mapData);

      vkh::ShaderDescriptorSetAndBindingMappingInfo info(map);
      pipeCreateInfo.stages[1].pNext = info;
      tests.push_back(TestSetup("useCombinedImageSamplerIndex with indirect index array offsets",
                                createGraphicsPipeline(pipeCreateInfo),
                                {{mapData.pushOffset, indirectIndexArrayBuffer.address}}));
    }

    {
      // Test useCombinedImageSamplerIndex for the single-element indirect index case.
      HeapRange resourceRange = AllocResources(3, 2 * imgDescSize);
      auto image = MakeNextTestImage("siIndirectCombIndexArraySingle");
      WriteImageDescriptor(resourceRange.index(1), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, image);
      HeapRange samplerRange = AllocSamplers(4, sampDescSize);
      WriteSamplerDescriptor(samplerRange.index(3), linearSampler);
      pipeCreateInfo.stages[1] = CompileTemplatedFS(
          "layout(set = 1, binding = 2) uniform sampler2D siIndirectCombIndexArraySingle[1]",
          "Color = texture(siIndirectCombIndexArraySingle[0], uv)");

      VkDescriptorMappingSourceIndirectIndexArrayEXT mapData = {0};

      mapData.useCombinedImageSamplerIndex = true;

      mapData.pushOffset = 24;
      mapData.addressOffset = 201 * 4;
      mapData.heapOffset = resourceRange.offset;
      mapData.heapIndexStride = resourceRange.stride;
      mapData.samplerHeapOffset = samplerRange.offset;
      mapData.samplerHeapIndexStride = samplerRange.stride;

      // These fields are unused with combined.
      mapData.samplerAddressOffset = 300 * 4;
      mapData.samplerPushOffset = 64;

      vkh::DescriptorSetAndBindingMapping map =
          vkh::DescriptorSetAndBindingMapping::indirectIndexArray(1, 2, mapData);

      vkh::ShaderDescriptorSetAndBindingMappingInfo info(map);
      pipeCreateInfo.stages[1].pNext = info;
      tests.push_back(TestSetup("useCombinedImageSamplerIndex with indirect index array offsets",
                                createGraphicsPipeline(pipeCreateInfo),
                                {{mapData.pushOffset, indirectIndexArrayBuffer.address}}));
    }

    {
      uint32_t pushIndex = 3;
      uint32_t pushOffset = 16;
      HeapRange resourceRange = AllocResources(pushIndex + 1, imgDescSize * 2);
      auto image = MakeNextTestImage("storageImagePushIndex");
      WriteImageDescriptor(resourceRange.index(pushIndex), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, image);
      pipeCreateInfo.stages[1] = CompileTemplatedFS(
          "layout(set = 1, binding = 2, rgba32f) uniform image2D storageImagePushIndex",
          "Color = imageLoad(storageImagePushIndex, uvi)");
      vkh::DescriptorSetAndBindingMapping map = vkh::DescriptorSetAndBindingMapping::pushIndex(
          1, 2, resourceRange.offset, pushOffset, resourceRange.stride, 0);
      vkh::ShaderDescriptorSetAndBindingMappingInfo info(map);
      pipeCreateInfo.stages[1].pNext = info;

      tests.push_back(TestSetup("storage image at push index",
                                createGraphicsPipeline(pipeCreateInfo), {{pushOffset, pushIndex}}));
    }

    {
      uint32_t pushIndex = 2;
      uint32_t pushOffset = 16;
      HeapRange resourceRange = AllocResources(pushIndex + 1, bufDescSize * 2);
      AllocatedBuffer buf = MakeTestBuffer("uboAtPushIndex", 0x310, Vec4f(1.0f, 2.0f, 3.0f, 4.0f));
      WriteBufferDescriptor(resourceRange.index(pushIndex), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, buf,
                            0x300, 256);
      pipeCreateInfo.stages[1] = CompileTemplatedFS(
          "layout(set = 1, binding = 2, std140) uniform ubo { vec4 data[10]; } uboAtPushIndex",
          "Color = uboAtPushIndex.data[1] + uboAtPushIndex.data[3]");
      vkh::DescriptorSetAndBindingMapping map = vkh::DescriptorSetAndBindingMapping::pushIndex(
          1, 2, resourceRange.offset, pushOffset, resourceRange.stride, 0);
      vkh::ShaderDescriptorSetAndBindingMappingInfo info(map);
      pipeCreateInfo.stages[1].pNext = info;

      tests.push_back(TestSetup("uniform buffer at push index",
                                createGraphicsPipeline(pipeCreateInfo), {{pushOffset, pushIndex}}));
    }

    {
      HeapRange resourceRange =
          NextSingleImageDescriptor("storageImageGS", VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
      pipeCreateInfo.stages[1] =
          CompileTemplatedFS("layout(set = 1, binding = 2, rgba32f) uniform image2D storageImageGS",
                             "Color = imageLoad(storageImageGS, uvi)");
      vkh::DescriptorSetAndBindingMapping map =
          vkh::DescriptorSetAndBindingMapping::constOffset(1, 2, resourceRange.offset, 0);
      vkh::ShaderDescriptorSetAndBindingMappingInfo info(map);
      pipeCreateInfo.stages[1].pNext = info;
      pipeCreateInfo.stages.resize(3);
      pipeCreateInfo.stages[2] =
          CompileShaderModule(passthroughGeom, ShaderLang::glsl, ShaderStage::geom, "main");
      tests.push_back(TestSetup("storage image at const offset with GS position",
                                createGraphicsPipeline(pipeCreateInfo)));

      // Restore default setup to not use a GS
      pipeCreateInfo.stages.pop_back();
    }

    {
      pipeCreateInfo.stages[1] = CompileTemplatedFS(
          "layout(set = 1, binding = 2, std140) uniform descbuff { vec4 data[3]; } descbuf",
          "Color = descbuf.data[1]");
      vkh::DescriptorSetAndBindingMapping map =
          vkh::DescriptorSetAndBindingMapping::pushData(1, 2, 16);
      vkh::ShaderDescriptorSetAndBindingMappingInfo info(map);
      pipeCreateInfo.stages[1].pNext = info;

      float push_data[12] = {.5, .5, .5, .5, 0, 1, 0, 0, .25, .25, .25, .25};

      tests.push_back(TestSetup("uniform buffer in push data",
                                createGraphicsPipeline(pipeCreateInfo), {{16, push_data}}));
    }

    {
      uint32_t pushAddressOffset = 16;
      AllocatedBuffer buf = MakeTestBuffer("uboAtPushAddr", 0x310, Vec4f(2.0f, 3.0f, 4.0f, 5.0f));
      pipeCreateInfo.stages[1] = CompileTemplatedFS(
          "layout(set = 1, binding = 2, std140) uniform ubo { vec4 data[90]; } uboAtPushAddr",
          "Color = uboAtPushAddr.data[1] + uboAtPushAddr.data[79]");
      vkh::DescriptorSetAndBindingMapping map =
          vkh::DescriptorSetAndBindingMapping::pushAddress(1, 2, pushAddressOffset);
      vkh::ShaderDescriptorSetAndBindingMappingInfo info(map);
      pipeCreateInfo.stages[1].pNext = info;

      tests.push_back(TestSetup("uniform buffer at push address",
                                createGraphicsPipeline(pipeCreateInfo),
                                {{pushAddressOffset, buf.address + 0x300}}));
    }

    {
      uint32_t pushAddressOffset = 16;
      AllocatedBuffer buf = MakeTestBuffer("uboAtIndirectAddr", 0x310, Vec4f(3.0f, 4.0f, 5.0f, 6.0f));
      AllocatedBuffer indirect = MakeTestBuffer("indirectAddressBuf", 0x10, buf.address + 0x300);
      pipeCreateInfo.stages[1] = CompileTemplatedFS(
          "layout(set = 1, binding = 2, std140) uniform ubo { vec4 data[90]; } uboAtIndirectAddr",
          "Color = uboAtIndirectAddr.data[1] + uboAtIndirectAddr.data[79]");
      vkh::DescriptorSetAndBindingMapping map =
          vkh::DescriptorSetAndBindingMapping::indirectAddress(1, 2, pushAddressOffset, 0x10);
      vkh::ShaderDescriptorSetAndBindingMappingInfo info(map);
      pipeCreateInfo.stages[1].pNext = info;

      tests.push_back(TestSetup("uniform buffer at indirect address",
                                createGraphicsPipeline(pipeCreateInfo),
                                {{pushAddressOffset, indirect.address}}));
    }

    {
      HeapRange resourceRange =
          NextSingleImageDescriptor("resourceMaskImage", VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
      pipeCreateInfo.stages[1] = CompileTemplatedFS(
          "layout(set = 1, binding = 2, rgba32f) uniform image2D resourceMaskImage",
          "Color = imageLoad(resourceMaskImage, uvi)");

      vkh::DescriptorSetAndBindingMapping mappings[4] = {
          vkh::DescriptorSetAndBindingMapping::constOffset(1, 2, 0, 0),
          vkh::DescriptorSetAndBindingMapping::constOffset(1, 2, 0, 0),
          vkh::DescriptorSetAndBindingMapping::constOffset(1, 2, 0, 0),
          vkh::DescriptorSetAndBindingMapping::constOffset(1, 2, resourceRange.offset, 0),
      };
      mappings[0].resourceMask = VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT;
      mappings[1].resourceMask = VK_SPIRV_RESOURCE_TYPE_READ_ONLY_IMAGE_BIT_EXT;
      mappings[2].resourceMask = VK_SPIRV_RESOURCE_TYPE_COMBINED_SAMPLED_IMAGE_BIT_EXT |
                                 VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT;
      mappings[3].resourceMask = VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT;

      vkh::ShaderDescriptorSetAndBindingMappingInfo info(mappings, 4);
      pipeCreateInfo.stages[1].pNext = info;
      tests.push_back(TestSetup("mappings with non-matching resourceMasks",
                                createGraphicsPipeline(pipeCreateInfo)));
    }

    if(hasExt(VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME))
    {
      VkResult ret;
      uint32_t borderColorIndex = ~0u;
      VkSamplerCustomBorderColorCreateInfoEXT borderColor = {
          VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT};
      /* register an unused red custom border color.  This makes sure that we
       * capture the index of the second custom border color that we actually
       * use.
       */
      borderColor.format = VK_FORMAT_R32G32B32A32_SFLOAT;
      borderColor.customBorderColor.float32[0] = 1.0;
      borderColor.customBorderColor.float32[1] = 0.0;
      borderColor.customBorderColor.float32[2] = 0.0;
      borderColor.customBorderColor.float32[3] = 1.0;
      ret = vkRegisterCustomBorderColorEXT(device, &borderColor, VK_FALSE, &borderColorIndex);
      TEST_ASSERT(ret == VK_SUCCESS, "failed to register custom border color");

      /* register the magenta custom border color we'll use. */
      borderColor.customBorderColor.float32[0] = 1.0;
      borderColor.customBorderColor.float32[1] = 0.0;
      borderColor.customBorderColor.float32[2] = 1.0;
      borderColor.customBorderColor.float32[3] = 1.0;
      ret = vkRegisterCustomBorderColorEXT(device, &borderColor, VK_FALSE, &borderColorIndex);
      TEST_ASSERT(ret == VK_SUCCESS, "failed to register custom border color");

      VkSamplerCreateInfo borderSampler =
          vkh::SamplerCreateInfo(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
      borderSampler.borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;
      borderColor.pNext = borderSampler.pNext;
      borderSampler.pNext = &borderColor;

      VkSamplerCustomBorderColorIndexCreateInfoEXT borderSamplerIndex = {
          VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_INDEX_CREATE_INFO_EXT};
      borderSamplerIndex.index = borderColorIndex;
      borderSamplerIndex.pNext = borderSampler.pNext;
      borderSampler.pNext = &borderSamplerIndex;

      HeapRange resourceRange =
          NextSingleImageDescriptor("texWithBorder", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
      HeapRange samplerRange = AllocSamplers(1, sampDescSize);
      WriteSamplerDescriptor(samplerRange.offset, borderSampler);
      pipeCreateInfo.stages[1] =
          CompileTemplatedFS("layout(set = 1, binding = 2) uniform sampler2D texWithBorder",
                             "Color = texture(texWithBorder, uv)");

      vkh::DescriptorSetAndBindingMapping map = vkh::DescriptorSetAndBindingMapping::constOffset(
          1, 2, resourceRange.offset, 0, samplerRange.offset, 0);
      vkh::ShaderDescriptorSetAndBindingMappingInfo info(map);
      pipeCreateInfo.stages[1].pNext = info;
      tests.push_back(TestSetup("sampled image with constant border color",
                                createGraphicsPipeline(pipeCreateInfo)));
    }

    {
      HeapRange resourceRange = AllocResources(1, imgDescSize, imgDescSize);
      float offset = resourceRange.offset / imgDescSize;
      TEST_ASSERT(
          ((uint32_t)offset) * imgDescSize == resourceRange.offset,
          "Failed to accurately represent direct addressed resource offset (%d) as a float (%f)",
          resourceRange.offset, offset);
      // XXX: the unit test looks for the resource_heap name, because that's
      // what glslc generates for its untyped var for the heap.
      std::string name = fmt::format("resource_heap_{}", (uint32_t)offset);
      auto image = MakeNextTestImage(name.c_str());
      WriteImageDescriptor(resourceRange.offset, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, image);
      pipeCreateInfo.stages[1] =
          CompileTemplatedFS("layout(descriptor_heap, rgba32f) uniform image2D directImage[]",
                             "Color = imageLoad(directImage[uint(push.data[1].x)], uvi)");
      tests.push_back(TestSetup("direct-addressed storage image",
                                createGraphicsPipeline(pipeCreateInfo), {{16, offset}}));
    }

    if(testSecondary)
    {
      HeapRange resourceRange =
          NextSingleImageDescriptor("secondaryImage", VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
      pipeCreateInfo.stages[1] =
          CompileTemplatedFS("layout(set = 1, binding = 2, rgba32f) uniform image2D secondaryImage",
                             "Color = imageLoad(secondaryImage, uvi)");
      vkh::DescriptorSetAndBindingMapping map =
          vkh::DescriptorSetAndBindingMapping::constOffset(1, 2, resourceRange.offset, 0);
      vkh::ShaderDescriptorSetAndBindingMappingInfo info(map);
      pipeCreateInfo.stages[1].pNext = info;
      tests.push_back(TestSetup("storage image in with inherited heaps in secondary",
                                createGraphicsPipeline(pipeCreateInfo), {}, GRAPHICS_SECONDARY));
    }

    heap.unmap();
    samplerHeap.unmap();

    while(Running())
    {
      VkCommandBuffer cmd = GetCommandBuffer();
      VkCommandBuffer primaryCmd = cmd;

      vkBeginCommandBuffer(cmd, vkh::CommandBufferBeginInfo());

      VkImage swapimg = StartUsingBackbuffer(cmd);

      vkCmdBindResourceHeapEXT(cmd, &heapInfo);
      vkCmdBindSamplerHeapEXT(cmd, &samplerHeapInfo);

      VkCommandBuffer secondaryCmd = VK_NULL_HANDLE;
      vkCmdBeginRenderPass(cmd,
                           vkh::RenderPassBeginInfo(renderPass, framebuffer, mainWindow->scissor,
                                                    {vkh::ClearValue(0.2f, 0.2f, 0.2f, 1.0f)}),
                           testSecondary ? VK_SUBPASS_CONTENTS_INLINE_AND_SECONDARY_COMMAND_BUFFERS_KHR
                                         : VK_SUBPASS_CONTENTS_INLINE);
      mainWindow->setViewScissor(cmd);
      float sqSize = float(screenHeight) / ceilf(sqrtf((float)(tests.size())));

      float x = 0.0f, y = 0.0f;

      pushMarker(cmd, "Heap Tests");
      for(TestSetup &test : tests)
      {
        if(test.testExec == GRAPHICS_SECONDARY)
        {
          cmd = secondaryCmd = GetCommandBuffer(VK_COMMAND_BUFFER_LEVEL_SECONDARY);
          VkCommandBufferInheritanceInfo inheritanceInfo =
              vkh::CommandBufferInheritanceInfo(renderPass, 0, framebuffer);
          VkCommandBufferInheritanceDescriptorHeapInfoEXT inheritanceHeapInfo = {
              VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_DESCRIPTOR_HEAP_INFO_EXT};
          inheritanceHeapInfo.pResourceHeapBindInfo = &heapInfo;
          inheritanceHeapInfo.pSamplerHeapBindInfo = &samplerHeapInfo;
          inheritanceInfo.pNext = &inheritanceHeapInfo;
          vkBeginCommandBuffer(
              cmd, vkh::CommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
                                               &inheritanceInfo));
          mainWindow->setViewScissor(cmd);
        }

        pushMarker(cmd, test.name);
        VkViewport v = {x, y, sqSize, sqSize, 0.0f, 1.0f};
        vkh::cmdPushData(cmd, 0, Vec4f(sqSize, (float)texSize, x, y));
        for(const PushData &p : test.pushData)
        {
          VkPushDataInfoEXT info = {
              VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT, NULL, p.offset, {p.data, p.size}};
          vkCmdPushDataEXT(cmd, &info);
        }
        vkCmdSetViewport(cmd, 0, 1, &v);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, test.pipeline);
        vkCmdDraw(cmd, 4, 1, 0, 0);

        popMarker(cmd);
        if(test.testExec == GRAPHICS_SECONDARY)
        {
          vkEndCommandBuffer(cmd);
          vkCmdExecuteCommands(primaryCmd, 1, &cmd);
          cmd = primaryCmd;
        }

        x += sqSize;

        if(x + sqSize >= (float)screenWidth)
        {
          x = 0.0f;
          y += sqSize;
        }
      }
      popMarker(cmd);

      vkCmdEndRenderPass(cmd);

      vkh::cmdPipelineBarrier(
          cmd, {
                   vkh::ImageMemoryBarrier(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                           VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                                           VK_IMAGE_LAYOUT_GENERAL, colatt.image),
               });

      blitToSwap(cmd, colatt.image, VK_IMAGE_LAYOUT_GENERAL, swapimg, VK_IMAGE_LAYOUT_GENERAL);

      FinishUsingBackbuffer(cmd, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);

      vkEndCommandBuffer(cmd);

      Submit(0, 1, {cmd}, {secondaryCmd});

      Present();
    }

    return 0;
  }
};

REGISTER_TEST();
