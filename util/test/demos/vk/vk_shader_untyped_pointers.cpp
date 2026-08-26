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

RD_TEST(VK_Shader_Untyped_Pointers, VulkanGraphicsTest)
{
  static constexpr const char *Description =
      "Test VK_KHR_untyped_pointers references to resources.";

  bool testSecondary = false;

  const VkPipelineShaderStageCreateInfo CompileTemplatedFS(const char *namesAndDecorations,
                                                           const char *types, const char *body)
  {
    const char *pixelTemplate = R"EOSHADER(
               OpCapability Shader
               OpCapability UntypedPointersKHR
               OpExtension "SPV_KHR_untyped_pointers"
               OpExtension "SPV_KHR_storage_buffer_storage_class"
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %main "main" %gl_FragCoord %Color
               OpExecutionMode %main OriginUpperLeft
               OpSource GLSL 460
               OpName %main "main"
               OpName %uv "uv"
               OpName %gl_FragCoord "gl_FragCoord"
               OpName %PushData "PushData"
               OpMemberName %PushData 0 "data"
               OpName %push "push"
               OpName %uvi "uvi"
               OpName %Color "Color"
{}
               OpDecorate %gl_FragCoord BuiltIn FragCoord
               OpDecorate %_arr_v4float_uint_1 ArrayStride 16
               OpDecorate %PushData Block
               OpMemberDecorate %PushData 0 Offset 0
               OpDecorate %Color Location 0
               OpDecorate %Color Index 0
       %void = OpTypeVoid
          %3 = OpTypeFunction %void
      %float = OpTypeFloat 32
       %uint = OpTypeInt 32 0
        %int = OpTypeInt 32 1
    %v2float = OpTypeVector %float 2
    %v4float = OpTypeVector %float 4
      %v4int = OpTypeVector %int 4
%_ptr_Function_v2float = OpTypePointer Function %v2float
%_ptr_Input_v4float = OpTypePointer Input %v4float
%gl_FragCoord = OpVariable %_ptr_Input_v4float Input
     %uint_1 = OpConstant %uint 1
%_arr_v4float_uint_1 = OpTypeArray %v4float %uint_1
   %PushData = OpTypeStruct %_arr_v4float_uint_1
%_ptr_PushConstant_PushData = OpTypePointer PushConstant %PushData
       %push = OpVariable %_ptr_PushConstant_PushData PushConstant
      %int_0 = OpConstant %int 0
     %uint_0 = OpConstant %uint 0
     %uint_4 = OpConstant %uint 4
%_ptr_PushConstant_v4float = OpTypePointer PushConstant %v4float
      %v2int = OpTypeVector %int 2
%_ptr_Function_v2int = OpTypePointer Function %v2int
%_ptr_Output_v4float = OpTypePointer Output %v4float
{}
      %Color = OpVariable %_ptr_Output_v4float Output
       %main = OpFunction %void None %3
          %5 = OpLabel
         %uv = OpVariable %_ptr_Function_v2float Function
        %uvi = OpVariable %_ptr_Function_v2int Function
         %13 = OpLoad %v4float %gl_FragCoord
         %14 = OpVectorShuffle %v2float %13 %13 0 1
         %24 = OpAccessChain %_ptr_PushConstant_v4float %push %int_0 %int_0
         %25 = OpLoad %v4float %24
         %26 = OpVectorShuffle %v2float %25 %25 2 3
         %28 = OpConvertFToS %v2int %26
         %29 = OpConvertSToF %v2float %28
         %30 = OpFSub %v2float %14 %29
         %31 = OpAccessChain %_ptr_PushConstant_v4float %push %int_0 %int_0
         %32 = OpLoad %v4float %31
         %33 = OpVectorShuffle %v2float %32 %32 0 0
         %34 = OpFDiv %v2float %30 %33
               OpStore %uv %34
         %37 = OpLoad %v2float %uv
         %38 = OpAccessChain %_ptr_PushConstant_v4float %push %int_0 %int_0
         %39 = OpLoad %v4float %38
         %40 = OpVectorShuffle %v2float %39 %39 1 1
         %41 = OpFMul %v2float %37 %40
         %42 = OpConvertFToS %v2int %41
               OpStore %uvi %42
{}
               OpReturn
               OpFunctionEnd
)EOSHADER";

    std::string generatedShader = fmt::format(pixelTemplate, namesAndDecorations, types, body);

    return CompileShaderModule(generatedShader, ShaderLang::spvasm, ShaderStage::frag, "main", {},
                               SPIRVTarget::vulkan12);
  }

  void Prepare(int argc, char **argv)
  {
    devExts.push_back(VK_KHR_SHADER_UNTYPED_POINTERS_EXTENSION_NAME);

    VulkanGraphicsTest::Prepare(argc, argv);

    if(!Avail.empty())
      return;

    static VkPhysicalDeviceShaderUntypedPointersFeaturesKHR untypedFeats = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_UNTYPED_POINTERS_FEATURES_KHR,
    };
    untypedFeats.shaderUntypedPointers = VK_TRUE;
    untypedFeats.pNext = (void *)devInfoNext;
    devInfoNext = &untypedFeats;

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

  uint32_t nextResourceOffset = 0;
  uint32_t nextSamplerOffset = 0;

  Vec4f NextColor()
  {
    const Vec4f colors[] = {
        {1.0, 0.0, 0.0, 1.0}, {0.0, 1.0, 0.0, 1.0}, {0.0, 0.0, 1.0, 1.0},
        {1.0, 0.0, 0.5, 1.0}, {0.5, 1.0, 0.0, 1.0}, {0.0, 0.5, 1.0, 1.0},
    };

    static int i = 0;
    return colors[i++ % ARRAY_COUNT(colors)];
  }

  AllocatedBuffer MakeTestBuffer(const char *name, uint32_t offset, const Vec4f &data)
  {
    // use 256 aligned sizes for buffers so we can check this on all drivers, we don't care to test
    // aliasing caused by different sizes
    VkDeviceSize size = AlignUp(offset, 0x100U) + 0x2000;
    AllocatedBuffer ret(this,
                        vkh::BufferCreateInfo(size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
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
                        vkh::BufferCreateInfo(size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
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

  class TestDescriptorSet
  {
  public:
    VkDescriptorSetLayout setLayout;
    VkPipelineLayout layout;
    VkDescriptorSet set;
  };

  class TestSetup
  {
  public:
    TestSetup(const char *name, VkPipeline pipeline, TestDescriptorSet descSet,
              std::vector<PushData> pushData = {})
        : name(name), pipeline(pipeline), descSet(descSet), pushData(std::move(pushData))
    {
    }
    std::string name;
    std::vector<PushData> pushData;
    VkPipeline pipeline;
    TestDescriptorSet descSet;
  };

  int main()
  {
    // initialise, create window, create context, etc
    if(!Init())
      return 3;

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

    // graphics pipeline creation shared across many sub-tests.
    vkh::GraphicsPipelineCreateInfo pipeCreateInfo;
    pipeCreateInfo.renderPass = renderPass;
    pipeCreateInfo.inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    pipeCreateInfo.stages.resize(2);
    pipeCreateInfo.stages[0] =
        CompileShaderModule(VKFullscreenQuadVertex, ShaderLang::glsl, ShaderStage::vert, "main");

    std::vector<TestSetup> tests;

    const uint32_t binding = 2;

    auto createDescriptorSet = [&](VkDescriptorType type, uint32_t bindingCount) {
      VkDescriptorSetLayout setLayout = createDescriptorSetLayout(vkh::DescriptorSetLayoutCreateInfo({
          {
              binding,
              type,
              bindingCount,
              VK_SHADER_STAGE_FRAGMENT_BIT,
          },
      }));

      VkPipelineLayout layout = createPipelineLayout(vkh::PipelineLayoutCreateInfo(
          {setLayout}, {
                           vkh::PushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Vec4i) * 2),
                       }));

      VkDescriptorSet descset = allocateDescriptorSet(setLayout);

      return TestDescriptorSet{setLayout, layout, descset};
    };

    VkSampler linearSampler = createSampler(vkh::SamplerCreateInfo(VK_FILTER_LINEAR));

    auto writeBufferDesc = [&](VkDescriptorType type, TestDescriptorSet &descriptors,
                               uint32_t arrayElement, VkBuffer buffer) {
      vkh::updateDescriptorSets(
          device, {
                      vkh::WriteDescriptorSet(descriptors.set, binding, arrayElement, type,
                                              {
                                                  vkh::DescriptorBufferInfo(buffer),
                                              }),
                  });
    };

    {
      auto descriptors = createDescriptorSet(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
      writeBufferDesc(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptors, 0,
                      MakeTestBuffer("ssbo1", 0, NextColor()).buffer);

      pipeCreateInfo.stages[1] = CompileTemplatedFS(R"EOSHADER(
               OpName %variable "ssbo1"
               OpDecorate %SSBO Block
               OpMemberDecorate %SSBO 0 Offset 0
               OpDecorate %variable DescriptorSet 0
               OpDecorate %variable Binding 2
               OpDecorate %variable NonWritable
        )EOSHADER",
                                                    R"EOSHADER(
       %SSBO = OpTypeStruct %v4float
    %var_ptr = OpTypeUntypedPointerKHR StorageBuffer
   %variable = OpUntypedVariableKHR %var_ptr StorageBuffer %SSBO
        )EOSHADER",
                                                    R"EOSHADER(
       %read = OpLoad %v4float %variable
               OpStore %Color %read
        )EOSHADER");

      pipeCreateInfo.layout = descriptors.layout;
      tests.push_back(
          TestSetup("SSBO direct var load", createGraphicsPipeline(pipeCreateInfo), descriptors));
    }

    {
      auto descriptors = createDescriptorSet(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
      writeBufferDesc(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptors, 0,
                      MakeTestBuffer("ssbo2", 0, NextColor()).buffer);

      pipeCreateInfo.stages[1] = CompileTemplatedFS(R"EOSHADER(
               OpName %variable "ssbo2"
               OpDecorate %SSBOi Block
               OpMemberDecorate %SSBOi 0 Offset 0
               OpDecorate %SSBOf Block
               OpMemberDecorate %SSBOf 0 Offset 0
               OpDecorate %variable DescriptorSet 0
               OpDecorate %variable Binding 2
               OpDecorate %variable NonWritable
               OpDecorate %uint_arr4 ArrayStride 4
        )EOSHADER",
                                                    R"EOSHADER(
  %uint_arr4 = OpTypeArray %uint %uint_4
      %SSBOi = OpTypeStruct %uint_arr4
      %SSBOf = OpTypeStruct %v4float
    %var_ptr = OpTypeUntypedPointerKHR StorageBuffer
   %variable = OpUntypedVariableKHR %var_ptr StorageBuffer %SSBOi
        )EOSHADER",
                                                    R"EOSHADER(
         %ac = OpUntypedAccessChainKHR %var_ptr %SSBOf %variable %uint_0
       %read = OpLoad %v4float %ac
               OpStore %Color %read
        )EOSHADER");

      pipeCreateInfo.layout = descriptors.layout;
      tests.push_back(TestSetup("SSBO access chain var load",
                                createGraphicsPipeline(pipeCreateInfo), descriptors));
    }

    {
      auto descriptors = createDescriptorSet(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4);
      writeBufferDesc(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptors, 2,
                      MakeTestBuffer("ssboarray1_2", 0, NextColor()).buffer);
      VkBuffer unusedBuf = MakeTestBuffer("unusedbuf", 0, NextColor()).buffer;
      writeBufferDesc(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptors, 0, unusedBuf);
      writeBufferDesc(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptors, 1, unusedBuf);
      writeBufferDesc(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptors, 3, unusedBuf);

      pipeCreateInfo.stages[1] = CompileTemplatedFS(R"EOSHADER(
               OpName %variable "ssboarray1"
               OpDecorate %SSBO Block
               OpMemberDecorate %SSBO 0 Offset 0
               OpDecorate %variable DescriptorSet 0
               OpDecorate %variable Binding 2
               OpDecorate %variable NonWritable
        )EOSHADER",
                                                    R"EOSHADER(
       %SSBO = OpTypeStruct %v4float
  %SSBO_arr4 = OpTypeArray %SSBO %uint_4
    %var_ptr = OpTypeUntypedPointerKHR StorageBuffer
   %variable = OpUntypedVariableKHR %var_ptr StorageBuffer %SSBO_arr4
     %int_2  = OpConstant %int 2
        )EOSHADER",
                                                    R"EOSHADER(
         %ac = OpUntypedAccessChainKHR %var_ptr %SSBO_arr4 %variable %int_2 %uint_0
       %read = OpLoad %v4float %ac
               OpStore %Color %read
        )EOSHADER");

      pipeCreateInfo.layout = descriptors.layout;
      tests.push_back(TestSetup("SSBO array", createGraphicsPipeline(pipeCreateInfo), descriptors));
    }

    {
      auto descriptors = createDescriptorSet(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1);
      writeBufferDesc(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, descriptors, 0,
                      MakeTestBuffer("ubo1", 0, NextColor()).buffer);

      pipeCreateInfo.stages[1] = CompileTemplatedFS(R"EOSHADER(
               OpName %variable "ubo1"
               OpDecorate %UBO Block
               OpMemberDecorate %UBO 0 Offset 0
               OpDecorate %variable DescriptorSet 0
               OpDecorate %variable Binding 2
               OpDecorate %variable NonWritable
        )EOSHADER",
                                                    R"EOSHADER(
       %UBO = OpTypeStruct %v4float                   
    %var_ptr = OpTypeUntypedPointerKHR Uniform
   %variable = OpUntypedVariableKHR %var_ptr Uniform %UBO
        )EOSHADER",
                                                    R"EOSHADER(
       %read = OpLoad %v4float %variable
               OpStore %Color %read
        )EOSHADER");

      pipeCreateInfo.layout = descriptors.layout;
      tests.push_back(
          TestSetup("UBO direct var load", createGraphicsPipeline(pipeCreateInfo), descriptors));
    }

    while(Running())
    {
      VkCommandBuffer cmd = GetCommandBuffer();
      VkCommandBuffer primaryCmd = cmd;

      vkBeginCommandBuffer(cmd, vkh::CommandBufferBeginInfo());

      VkImage swapimg = StartUsingBackbuffer(cmd);

      vkCmdBeginRenderPass(cmd,
                           vkh::RenderPassBeginInfo(renderPass, framebuffer, mainWindow->scissor,
                                                    {vkh::ClearValue(0.2f, 0.2f, 0.2f, 1.0f)}),
                           VK_SUBPASS_CONTENTS_INLINE);
      mainWindow->setViewScissor(cmd);
      float sqSize = float(screenHeight) / ceilf(sqrtf((float)(tests.size())));

      float x = 0.0f, y = 0.0f;

      pushMarker(cmd, "Graphics Tests");
      for(TestSetup &test : tests)
      {
        pushMarker(cmd, test.name);
        VkViewport v = {x, y, sqSize, sqSize, 0.0f, 1.0f};
        Vec4f scale = Vec4f(sqSize, (float)texSize, x, y);
        vkCmdPushConstants(cmd, test.descSet.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(scale),
                           &scale);
        for(const PushData &p : test.pushData)
        {
          vkCmdPushConstants(cmd, test.descSet.layout, VK_SHADER_STAGE_FRAGMENT_BIT, p.offset,
                             p.size, p.data);
        }
        vkCmdSetViewport(cmd, 0, 1, &v);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, test.pipeline);
        vkh::cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, test.descSet.layout, 0,
                                   {test.descSet.set}, {});
        vkCmdDraw(cmd, 4, 1, 0, 0);

        popMarker(cmd);

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

      Submit(0, 1, {cmd});

      Present();
    }

    return 0;
  }
};

REGISTER_TEST();
