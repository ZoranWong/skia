/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkImage.h"
#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/vk/GrVkTypes.h"
#include "src/gpu/GrContextPriv.h"
#include "src/gpu/GrTexture.h"
#include "src/gpu/GrTextureProxy.h"
#include "src/image/SkImage_Base.h"
#include "tests/Test.h"

#ifdef SK_VULKAN
#include "src/gpu/vk/GrVkGpu.h"
#include "src/gpu/vk/GrVkTexture.h"

DEF_GPUTEST_FOR_VULKAN_CONTEXT(VkBackendSurfaceMutableStateTest, reporter, ctxInfo) {
    auto context = ctxInfo.directContext();

    GrBackendFormat format = GrBackendFormat::MakeVk(VK_FORMAT_R8G8B8A8_UNORM);
    GrBackendTexture backendTex = context->createBackendTexture(
            32, 32, format, GrMipmapped::kNo, GrRenderable::kNo, GrProtected::kNo);

    REPORTER_ASSERT(reporter, backendTex.isValid());

    GrVkImageInfo info;
    REPORTER_ASSERT(reporter, backendTex.getVkImageInfo(&info));
    VkImageLayout initLayout = info.fImageLayout;
    uint32_t initQueue = info.fCurrentQueueFamily;
    GrBackendSurfaceMutableState initState(initLayout, initQueue);

    // Verify that setting that state via a copy of a backendTexture is reflected in all the
    // backendTextures.
    GrBackendTexture backendTexCopy = backendTex;
    REPORTER_ASSERT(reporter, backendTexCopy.getVkImageInfo(&info));
    REPORTER_ASSERT(reporter, initLayout == info.fImageLayout);
    REPORTER_ASSERT(reporter, initQueue == info.fCurrentQueueFamily);

    GrBackendSurfaceMutableState newState(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                          VK_QUEUE_FAMILY_IGNORED);
    backendTexCopy.setMutableState(newState);

    REPORTER_ASSERT(reporter, backendTex.getVkImageInfo(&info));
    REPORTER_ASSERT(reporter, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL == info.fImageLayout);
    REPORTER_ASSERT(reporter, VK_QUEUE_FAMILY_IGNORED == info.fCurrentQueueFamily);

    REPORTER_ASSERT(reporter, backendTexCopy.getVkImageInfo(&info));
    REPORTER_ASSERT(reporter, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL == info.fImageLayout);
    REPORTER_ASSERT(reporter, VK_QUEUE_FAMILY_IGNORED == info.fCurrentQueueFamily);

    // Setting back to the init state since we didn't actually change it
    backendTex.setMutableState(initState);

    sk_sp<SkImage> wrappedImage = SkImage::MakeFromTexture(context, backendTex,
                                                           kTopLeft_GrSurfaceOrigin,
                                                           kRGBA_8888_SkColorType,
                                                           kPremul_SkAlphaType, nullptr);

    const GrSurfaceProxyView* view = as_IB(wrappedImage)->view(context);
    REPORTER_ASSERT(reporter, view);
    REPORTER_ASSERT(reporter, view->proxy()->isInstantiated());
    GrTexture* texture = view->proxy()->peekTexture();
    REPORTER_ASSERT(reporter, texture);

    // Verify that modifying the layout via the GrVkTexture is reflected in the GrBackendTexture
    GrVkTexture* vkTexture = static_cast<GrVkTexture*>(texture);
    REPORTER_ASSERT(reporter, initLayout == vkTexture->currentLayout());
    REPORTER_ASSERT(reporter, initQueue == vkTexture->currentQueueFamilyIndex());
    vkTexture->updateImageLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    REPORTER_ASSERT(reporter, backendTex.getVkImageInfo(&info));
    REPORTER_ASSERT(reporter, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL == info.fImageLayout);
    REPORTER_ASSERT(reporter, initQueue == info.fCurrentQueueFamily);

    GrBackendTexture backendTexImage = wrappedImage->getBackendTexture(false);
    REPORTER_ASSERT(reporter, backendTexImage.getVkImageInfo(&info));
    REPORTER_ASSERT(reporter, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL == info.fImageLayout);
    REPORTER_ASSERT(reporter, initQueue == info.fCurrentQueueFamily);

    // Verify that modifying the layout via the GrBackendTexutre is reflected in the GrVkTexture
    backendTexImage.setMutableState(newState);
    REPORTER_ASSERT(reporter,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL == vkTexture->currentLayout());
    REPORTER_ASSERT(reporter, VK_QUEUE_FAMILY_IGNORED == info.fCurrentQueueFamily);

    vkTexture->setQueueFamilyIndex(initQueue);
    vkTexture->updateImageLayout(initLayout);

    REPORTER_ASSERT(reporter, backendTex.getVkImageInfo(&info));
    REPORTER_ASSERT(reporter, initLayout == info.fImageLayout);
    REPORTER_ASSERT(reporter, initQueue == info.fCurrentQueueFamily);

    REPORTER_ASSERT(reporter, backendTexCopy.getVkImageInfo(&info));
    REPORTER_ASSERT(reporter, initLayout == info.fImageLayout);
    REPORTER_ASSERT(reporter, initQueue == info.fCurrentQueueFamily);

    REPORTER_ASSERT(reporter, backendTexImage.getVkImageInfo(&info));
    REPORTER_ASSERT(reporter, initLayout == info.fImageLayout);
    REPORTER_ASSERT(reporter, initQueue == info.fCurrentQueueFamily);

    // Test using the setBackendTextureStateAPI. Unlike the previous test this will actually add
    // real transitions to the image so we need to be careful about doing actual valid transitions.
    GrVkGpu* gpu = static_cast<GrVkGpu*>(context->priv().getGpu());

    context->setBackendTextureState(backendTex, newState);

    REPORTER_ASSERT(reporter, backendTex.getVkImageInfo(&info));
    REPORTER_ASSERT(reporter, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL == info.fImageLayout);
    REPORTER_ASSERT(reporter, gpu->queueIndex() == info.fCurrentQueueFamily);

    // To test queue transitions, we don't have any other valid queue available so instead we try
    // to transition to external queue.
    if (gpu->vkCaps().supportsExternalMemory()) {
        GrBackendSurfaceMutableState externalState(VK_IMAGE_LAYOUT_GENERAL,
                                                   VK_QUEUE_FAMILY_EXTERNAL);

        context->setBackendTextureState(backendTex, externalState);

        REPORTER_ASSERT(reporter, backendTex.getVkImageInfo(&info));
        REPORTER_ASSERT(reporter, VK_IMAGE_LAYOUT_GENERAL == info.fImageLayout);
        REPORTER_ASSERT(reporter, VK_QUEUE_FAMILY_EXTERNAL == info.fCurrentQueueFamily);

        context->submit();

        GrBackendSurfaceMutableState externalState2(VK_IMAGE_LAYOUT_GENERAL, initQueue);
        context->setBackendTextureState(backendTex, externalState2);

        REPORTER_ASSERT(reporter, backendTex.getVkImageInfo(&info));
        REPORTER_ASSERT(reporter, VK_IMAGE_LAYOUT_GENERAL == info.fImageLayout);
        REPORTER_ASSERT(reporter, gpu->queueIndex() == info.fCurrentQueueFamily);
    }

    // We must submit this work before we try to delete the backend texture.
    context->submit(true);

    context->deleteBackendTexture(backendTex);
}

#endif
