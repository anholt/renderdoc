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
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT,
  };
  VkPhysicalDeviceDescriptorHeapPropertiesEXT descHeapProps = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT,
  };

  void Prepare(int argc, char **argv)
  {
    devExts.push_back(VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME);
    devExts.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);

    VulkanGraphicsTest::Prepare(argc, argv);

    if(!Avail.empty())
      return;

    getPhysFeatures2(&descHeapFeatures);
    getPhysProperties2(&descHeapProps);

    if(!descHeapFeatures.descriptorHeap)
      Avail = "Feature 'descriptorBuffer' not available";

    static VkPhysicalDeviceBufferDeviceAddressFeaturesKHR bufaddrFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR,
    };

    getPhysFeatures2(&bufaddrFeatures);

    if(!bufaddrFeatures.bufferDeviceAddress)
      Avail = "feature 'bufferDeviceAddress' not available";

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

    static AllocatedBuffer uploadBuf(
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
  int main()
  {
    // initialise, create window, create context, etc
    if(!Init())
      return 3;

    VkImageViewCreateInfo red = MakeTestImage("red", Vec4f(1.0f, 0.0f, 0.0f, 1.0f));
    VkImageViewCreateInfo green = MakeTestImage("green", Vec4f(0.0f, 1.0f, 0.0f, 1.0f));

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

    vkh::GraphicsPipelineCreateInfo pipeCreateInfo;
    pipeCreateInfo.renderPass = renderPass;
    pipeCreateInfo.inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    pipeCreateInfo.pNext = &pipeCreateFlags;

    while(Running())
    {
      VkCommandBuffer cmd = GetCommandBuffer();

      vkBeginCommandBuffer(cmd, vkh::CommandBufferBeginInfo());

      VkImage swapimg = StartUsingBackbuffer(cmd);

      vkCmdBeginRenderPass(cmd,
                           vkh::RenderPassBeginInfo(renderPass, framebuffer, mainWindow->scissor,
                                                    {vkh::ClearValue(0.2f, 0.2f, 0.2f, 1.0f)}),
                           VK_SUBPASS_CONTENTS_INLINE);
      mainWindow->setViewScissor(cmd);

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
