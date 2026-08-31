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

  static const uint32_t aSet = 0;
  const uint32_t aBinding = 1;
  static const uint32_t fSet = 0;
  static const uint32_t fBinding = 22;
  static const uint32_t iSet = 0;
  static const uint32_t iBinding = 41;
  std::vector<AllocatedBuffer> tempBufs;

  std::string header = R"EOSHADER(

#version 460 core

#extension GL_EXT_samplerless_texture_functions : require

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

  std::string pixel = R"EOSHADER(

layout(push_constant) uniform PushData {
  vec4 data;
} push;

layout(location = 0, index = 0) out vec4 Color;

layout(set = 0, binding = 1, std140) uniform aa
{
  vec4 data[90];
} a;

layout(set = 0, binding = 2, std140) buffer bb
{
  vec4 data[90];
} b;

layout(set = 0, binding = 11) uniform samplerBuffer c;
layout(set = 0, binding = 12, rgba32f) uniform imageBuffer d;

layout(set = 0, binding = 21) uniform texture2D e;
layout(set = 0, binding = 22, rgba8) uniform image2D f;
layout(set = 0, binding = 23, input_attachment_index = 0) uniform subpassInput g;

layout(set = 0, binding = 31) uniform sampler h;

layout(set = 0, binding = 41) uniform sampler2D i;

#ifdef RAYS
layout(set = 0, binding = 51) uniform accelerationStructureEXT j;
#endif

layout(set = 0, binding = 61, std140) uniform descbuff
{
  vec4 data[3];
} descbuf;

layout(set = 1, binding = 0) uniform sampler l;

layout(set = 3, binding = 1, std140) uniform mm
{
  vec4 data[90];
} m[100];

layout(set = 3, binding = 2) uniform sampler2D n[100];

#ifdef RAYS
layout(set = 3, binding = 3) uniform accelerationStructureEXT u[100];
#endif

layout(set = 3, binding = 4, std140) uniform oo
{
  vec4 data[90];
} o[];

layout(set = 4, binding = 1, std140) uniform pp
{
  vec4 data[90];
} p;

layout(set = 4, binding = 2, std140) buffer qq
{
  vec4 data[90];
} q;

layout(set = 5, binding = 0) uniform sampler r;

layout(set = 2, binding = 0) uniform sampler t_samp[100];
layout(set = 2, binding = 0) uniform texture2D t_tex[100];
layout(set = 2, binding = 0) uniform sampler2D t_comb[100];
layout(set = 2, binding = 0, std140) uniform tt_ubo
{
  vec4 data[90];
} t_ubo[];
layout(set = 2, binding = 0, std140) buffer tt_ssbo
{
  vec4 data[90];
} t_ssbo[];
#ifdef RAYS
layout(set = 2, binding = 0) uniform accelerationStructureEXT t_as[100];
#endif

void main()
{
  vec2 uv = vec2(gl_FragCoord.xy - ivec2(push.data.zw))/push.data.xx;
  ivec2 uvi = ivec2(uv*push.data.yy);

  Color = vec4(uv.xy, 0.0f, 1.0f);

#if defined(RAYS)
  const vec3 light_origin = vec3(0,0,0);

  const vec3  pos = vec3((uv.xy - 0.5f)*10*vec2(1,-1), 5.0f);

  const float tmin = 0.01, tmax = 1000;
  const vec3  direction = light_origin - pos;

  rayQueryEXT query;
  float blue = 0.0f;
#if RAYS == 1
  rayQueryInitializeEXT(query, j, gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, pos, tmin, direction.xyz, 1.0);
#elif RAYS == 2
  blue = 1.0f;
  rayQueryInitializeEXT(query, t_as[60], gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, pos, tmin, direction.xyz, 1.0);
#elif RAYS == 3
  blue = 0.2f;
  rayQueryInitializeEXT(query, u[20], gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, pos, tmin, direction.xyz, 1.0);
#elif RAYS == 4
  blue = 0.0f;
  rayQueryInitializeEXT(query, u[31], gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, pos, tmin, direction.xyz, 1.0);
#endif
  rayQueryProceedEXT(query);

  if(rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionNoneEXT)
    Color = vec4(0, 1, blue, 1);
  else
    Color = vec4(1, 0, blue, 1);
#elif TEST == 0
  Color = a.data[1] + a.data[79];
#elif TEST == 1
  Color = b.data[1] + b.data[79];
#elif TEST == 2
  Color = texelFetch(c, 1);
#elif TEST == 3
  Color = imageLoad(d, 1);
#elif TEST == 4
  Color = texelFetch(e, uvi, 0);
#elif TEST == 5
  Color = imageLoad(f, uvi);
#elif TEST == 6
  Color = subpassLoad(g);
#elif TEST == 7
  Color = textureLod(sampler2D(e, h), uv, 0.0);
#elif TEST == 8
  Color = texture(i, uv);
#elif TEST == 9
  // j - rays
#elif TEST == 10
  // inline UBO, named 'descbuf' instead of k to match resource name
  // we don't do a robustness check because inline UBOs don't provide bounds checking
  Color = descbuf.data[1];
#elif TEST == 11
  Color = textureLod(sampler2D(e, l), uv, 0.0);
#elif TEST == 12
  Color = m[20].data[1] + m[20].data[79];
#elif TEST == 13
  Color = m[31].data[1] + m[31].data[79];
#elif TEST == 14
  Color = textureLod(n[20], uv, 0.0);
#elif TEST == 15
  Color = textureLod(n[31], uv, 0.0);
#elif TEST == 16
  Color = textureLod(n[41], uv, 0.0);
#elif TEST == 17
  Color = o[40].data[1] + o[40].data[79];
#elif TEST == 18
  Color = o[51].data[1] + o[51].data[79];
#elif TEST == 19
  Color = p.data[1] + p.data[79];
#elif TEST == 20
  Color = q.data[1] + q.data[79];
#elif TEST == 21
  Color = textureLod(sampler2D(e, r), uv, 0.0);
#elif TEST == 22

#if defined(MUTABLE_SAMP)
  Color = textureLod(sampler2D(t_tex[20], t_samp[10]), uv, 0.0);
#else
  Color = textureLod(sampler2D(t_tex[20], r), uv, 0.0);
#endif

#elif TEST == 23 && defined(MUTABLE_COMB)
  Color = texture(t_comb[30], uv);
#elif TEST == 24
  Color = t_ubo[40].data[1] + t_ubo[40].data[79];
#elif TEST == 25
  Color = t_ssbo[50].data[1] + t_ssbo[50].data[79];
#endif
}

)EOSHADER";

  void Prepare(int argc, char **argv)
  {
    devExts.push_back(VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME);
    devExts.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);

    features.fragmentStoresAndAtomics = VK_TRUE;
    features.geometryShader = VK_TRUE;

    VulkanGraphicsTest::Prepare(argc, argv);

    if(!Avail.empty())
      return;

    getPhysFeatures2(&descHeapFeatures);
    getPhysProperties2(&descHeapProps);

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

    tempBufs.push_back(std::move(uploadBuf));

    return vkh::ImageViewCreateInfo(tex.image, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT);
  }

  VkDeviceAddress dataAddress;

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
    dataAddress = ret.address;
    byte *ptr = ret.map();
    // fill with garbage (that will be a relatively normal float value)
    memset(ptr, 0x3f, size);
    memcpy(ptr + offset, &data, sizeof(data));
    ret.unmap();

    return ret;
  }

  void WriteBufferDescriptor(byte * heapMap, VkDeviceSize heapOffset, VkDescriptorType type,
                             AllocatedBuffer & buf, VkDeviceSize offset, VkDeviceSize size)
  {
    VkDeviceAddressRangeEXT addressRange = {buf.address + offset, size};

    VkResourceDescriptorInfoEXT info = {VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT};
    info.type = type;
    info.data.pAddressRange = &addressRange;

    VkHostAddressRangeEXT range = {heapMap + heapOffset, descHeapProps.bufferDescriptorSize};

    vkWriteResourceDescriptorsEXT(device, 1, &info, &range);
  }

  void WriteImageDescriptor(byte * heapMap, VkDeviceSize offset, VkDescriptorType type,
                            VkImageViewCreateInfo & imageView)
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

  void WriteSamplerDescriptor(byte * heapMap, VkDeviceSize offset, VkSamplerCreateInfo & sampler)
  {
    VkHostAddressRangeEXT range = {heapMap + offset, descHeapProps.samplerDescriptorSize};

    vkWriteSamplerDescriptorsEXT(device, 1, &sampler, &range);
  }

  class PushData
  {
  public:
    template <typename T>
    PushData(uint32_t offset, const T &val) : offset(offset), size(sizeof(T))
    {
      memcpy(data, &val, sizeof(T));
    }
    uint32_t offset;
    uint32_t size;
    byte data[8];
  };

  class TestSetup
  {
  public:
    TestSetup(const char *name, VkPipeline pipeline, std::vector<PushData> pushData = {})
        : name(name), pipeline(pipeline), pushData(std::move(pushData))
    {
    }
    std::string name;
    std::vector<PushData> pushData;
    VkPipeline pipeline;
  };

  int main()
  {
    vmaBDA = true;

    // initialise, create window, create context, etc
    if(!Init())
      return 3;

    // Common resource heap that we'll do most of our tests in.
    uint32_t heapSize = std::min(1024u * 1024u, (uint32_t)descHeapProps.maxResourceHeapSize);
    uint32_t maxDescriptorAlign = (uint32_t)std::max(descHeapProps.imageDescriptorAlignment,
                                                     descHeapProps.bufferDescriptorAlignment);
    AllocatedBuffer heap(
        this,
        vkh::BufferCreateInfo(heapSize, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR |
                                            VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT),
        VmaAllocationCreateInfo({0, VMA_MEMORY_USAGE_CPU_TO_GPU}));
    byte *heapMap = heap.map();
    memset(heapMap, 0x3f, heapSize);
    uint32_t heapBindOffset = AlignUp(1024u, (uint32_t)descHeapProps.resourceHeapAlignment);
    heapMap += heapBindOffset;
    VkBindHeapInfoEXT heapInfo = {VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT};
    heapInfo.heapRange = {heap.address + heapBindOffset, heapSize - heapBindOffset};
    heapInfo.reservedRangeSize = descHeapProps.minResourceHeapReservedRange;
    heapInfo.reservedRangeOffset = heapInfo.heapRange.size - heapInfo.reservedRangeSize;
    heapInfo.reservedRangeOffset &= ~((VkDeviceSize)maxDescriptorAlign - 1);
    setName(heap.buffer, "resource heap");

    // Common sampler heap that we'll do most of our tests in.
    uint32_t samplerHeapSize = std::min(1024u * 1024u, (uint32_t)descHeapProps.maxSamplerHeapSize);
    AllocatedBuffer samplerHeap(
        this,
        vkh::BufferCreateInfo(samplerHeapSize, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR |
                                                   VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT),
        VmaAllocationCreateInfo({0, VMA_MEMORY_USAGE_CPU_TO_GPU}));
    byte *samplerHeapMap = samplerHeap.map();
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

    VkImageViewCreateInfo red = MakeTestImage("red", Vec4f(1.0f, 0.0f, 0.0f, 1.0f));
    VkImageViewCreateInfo green = MakeTestImage("green", Vec4f(0.0f, 1.0f, 0.0f, 1.0f));
    AllocatedBuffer a = MakeTestBuffer("a", 0x310, Vec4f(1.0f, 2.0f, 3.0f, 4.0f));

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
    pipeCreateInfo.stages[0] =
        CompileShaderModule(VKFullscreenQuadVertex, ShaderLang::glsl, ShaderStage::vert, "main");

    std::vector<TestSetup> tests;

    uint32_t nextResourceStep = (uint32_t)AlignUp(descHeapProps.imageDescriptorSize * 10,
                                                  descHeapProps.imageDescriptorAlignment);
    uint32_t nextResourceOffset = nextResourceStep;

    uint32_t nextSamplerStep = (uint32_t)AlignUp(descHeapProps.samplerDescriptorSize * 10,
                                                 descHeapProps.samplerDescriptorAlignment);
    uint32_t nextSamplerOffset = nextSamplerStep;

    {
      WriteImageDescriptor(heapMap, nextResourceOffset + descHeapProps.imageDescriptorSize,
                           VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, red);
      pipeCreateInfo.stages[1] = CompileShaderModule(header + "#define TEST 5" + pixel,
                                                     ShaderLang::glsl, ShaderStage::frag, "main");
      vkh::DescriptorSetAndBindingMapping map = vkh::DescriptorSetAndBindingMapping::constOffset(
          fSet, fBinding, nextResourceOffset + (uint32_t)descHeapProps.imageDescriptorSize);
      pipeCreateInfo.stages[1].pNext = vkh::ShaderDescriptorSetAndBindingMappingInfo(map);
      tests.push_back(
          TestSetup("storage image at const offset", createGraphicsPipeline(pipeCreateInfo)));
      nextResourceOffset += nextResourceStep;
    }

    VkSamplerCreateInfo linearSampler = vkh::SamplerCreateInfo(VK_FILTER_LINEAR);

    {
      WriteImageDescriptor(heapMap, nextResourceOffset + descHeapProps.imageDescriptorSize,
                           VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, green);
      WriteSamplerDescriptor(
          samplerHeapMap, nextSamplerOffset + 2 * descHeapProps.samplerDescriptorSize, linearSampler);
      pipeCreateInfo.stages[1] = CompileShaderModule(header + "#define TEST 8" + pixel,
                                                     ShaderLang::glsl, ShaderStage::frag, "main");
      vkh::DescriptorSetAndBindingMapping map = vkh::DescriptorSetAndBindingMapping::constOffset(
          iSet, iBinding, nextResourceOffset + (uint32_t)descHeapProps.imageDescriptorSize,
          nextSamplerOffset + 2 * (uint32_t)descHeapProps.samplerDescriptorSize);
      pipeCreateInfo.stages[1].pNext = vkh::ShaderDescriptorSetAndBindingMappingInfo(map);
      tests.push_back(
          TestSetup("sampled image at const offset", createGraphicsPipeline(pipeCreateInfo)));
      nextResourceOffset += nextResourceStep;
      nextSamplerOffset += nextSamplerStep;
    }

    {
      uint32_t pushIndex = 3;
      WriteImageDescriptor(heapMap,
                           nextResourceOffset + pushIndex * descHeapProps.imageDescriptorSize,
                           VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, green);
      pipeCreateInfo.stages[1] = CompileShaderModule(header + "#define TEST 5" + pixel,
                                                     ShaderLang::glsl, ShaderStage::frag, "main");
      vkh::DescriptorSetAndBindingMapping map = vkh::DescriptorSetAndBindingMapping::pushIndex(
          fSet, fBinding, nextResourceOffset, 16, (uint32_t)descHeapProps.imageDescriptorSize, 0);
      pipeCreateInfo.stages[1].pNext = vkh::ShaderDescriptorSetAndBindingMappingInfo(map);

      tests.push_back(TestSetup("storage image at push index",
                                createGraphicsPipeline(pipeCreateInfo), {{16, pushIndex}}));
      nextResourceOffset += nextResourceStep;
    }

    {
      uint32_t pushIndex = 2;
      WriteBufferDescriptor(heapMap,
                            nextResourceOffset + pushIndex * descHeapProps.bufferDescriptorSize,
                            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, a, 0x300, 256);
      pipeCreateInfo.stages[1] = CompileShaderModule(header + "#define TEST 0" + pixel,
                                                     ShaderLang::glsl, ShaderStage::frag, "main");
      vkh::DescriptorSetAndBindingMapping map = vkh::DescriptorSetAndBindingMapping::pushIndex(
          aSet, aBinding, nextResourceOffset, 16, (uint32_t)descHeapProps.bufferDescriptorSize, 0);
      pipeCreateInfo.stages[1].pNext = vkh::ShaderDescriptorSetAndBindingMappingInfo(map);

      tests.push_back(TestSetup("uniform buffer at push index",
                                createGraphicsPipeline(pipeCreateInfo), {{16, pushIndex}}));
      nextResourceOffset += nextResourceStep;
    }

    {
      WriteImageDescriptor(heapMap, nextResourceOffset + 2 * descHeapProps.imageDescriptorSize,
                           VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, red);
      pipeCreateInfo.stages[1] = CompileShaderModule(header + "#define TEST 5" + pixel,
                                                     ShaderLang::glsl, ShaderStage::frag, "main");
      vkh::DescriptorSetAndBindingMapping map = vkh::DescriptorSetAndBindingMapping::constOffset(
          fSet, fBinding, nextResourceOffset + 2 * (uint32_t)descHeapProps.imageDescriptorSize);
      pipeCreateInfo.stages[1].pNext = vkh::ShaderDescriptorSetAndBindingMappingInfo(map);
      pipeCreateInfo.stages.resize(3);
      pipeCreateInfo.stages[2] =
          CompileShaderModule(passthroughGeom, ShaderLang::glsl, ShaderStage::geom, "main");
      tests.push_back(TestSetup("storage image at const offset with GS position",
                                createGraphicsPipeline(pipeCreateInfo)));
      nextResourceOffset += nextResourceStep;

      // Restore default setup to not use a GS
      pipeCreateInfo.stages.pop_back();
    }

    heap.unmap();
    samplerHeap.unmap();

    TEST_ASSERT(nextResourceOffset <= heapInfo.reservedRangeOffset,
                "overflowed heap reserved range");
    TEST_ASSERT(nextSamplerOffset <= samplerHeapInfo.reservedRangeOffset,
                "overflowed sampler reserved range");

    while(Running())
    {
      VkCommandBuffer cmd = GetCommandBuffer();

      vkBeginCommandBuffer(cmd, vkh::CommandBufferBeginInfo());

      VkImage swapimg = StartUsingBackbuffer(cmd);

      vkCmdBindResourceHeapEXT(cmd, &heapInfo);
      vkCmdBindSamplerHeapEXT(cmd, &samplerHeapInfo);

      vkCmdBeginRenderPass(cmd,
                           vkh::RenderPassBeginInfo(renderPass, framebuffer, mainWindow->scissor,
                                                    {vkh::ClearValue(0.2f, 0.2f, 0.2f, 1.0f)}),
                           VK_SUBPASS_CONTENTS_INLINE);
      mainWindow->setViewScissor(cmd);
      float sqSize = float(screenHeight) / ceilf(sqrtf((float)tests.size()));

      float x = 0.0f, y = 0.0f;

      for(size_t t = 0; t < tests.size(); t++)
      {
        setMarker(cmd, tests[t].name);
        VkViewport v = {x, y, sqSize, sqSize, 0.0f, 1.0f};
        vkh::cmdPushData(cmd, 0, Vec4f(sqSize, (float)texSize, x, y));
        for(const PushData &p : tests[t].pushData)
        {
          VkPushDataInfoEXT info = {
              VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT, NULL, p.offset, {p.data, p.size}};
          vkCmdPushDataEXT(cmd, &info);
        }
        vkCmdSetViewport(cmd, 0, 1, &v);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, tests[t].pipeline);
        vkCmdDraw(cmd, 4, 1, 0, 0);

        x += sqSize;

        if(x + sqSize >= (float)screenWidth)
        {
          x = 0.0f;
          y += sqSize;
        }
      }

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

      Submit(0, 1, {cmd});

      Present();
    }

    return 0;
  }
};

REGISTER_TEST();
