/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkSurface.h"
#include "include/gpu/GrDirectContext.h"
#include "src/gpu/GrContextPriv.h"
#include "src/gpu/gl/GrGLDefines.h"
#include "src/gpu/gl/GrGLGpu.h"
#include "src/gpu/gl/GrGLUtil.h"
#include "tests/Test.h"

#ifdef SK_GL

DEF_GPUTEST_FOR_GL_RENDERING_CONTEXTS(TextureBindingsResetTest, reporter, ctxInfo) {
#define GL(F) GR_GL_CALL(ctxInfo.glContext()->gl(), F)

    auto context = ctxInfo.directContext();
    GrGpu* gpu = context->priv().getGpu();
    GrGLGpu* glGpu = static_cast<GrGLGpu*>(context->priv().getGpu());

    struct Target {
        GrGLenum fName;
        GrGLenum fQuery;
    };
    SkTDArray<Target> targets;
    targets.push_back({GR_GL_TEXTURE_2D, GR_GL_TEXTURE_BINDING_2D});
    bool supportExternal;
    if ((supportExternal = glGpu->glCaps().shaderCaps()->externalTextureSupport())) {
        targets.push_back({GR_GL_TEXTURE_EXTERNAL, GR_GL_TEXTURE_BINDING_EXTERNAL});
    }
    bool supportRectangle;
    if ((supportRectangle = glGpu->glCaps().rectangleTextureSupport())) {
        targets.push_back({GR_GL_TEXTURE_RECTANGLE, GR_GL_TEXTURE_BINDING_RECTANGLE});
    }
    GrGLint numUnits;
    GL(GetIntegerv(GR_GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &numUnits));
    SkTDArray<GrGLuint> claimedIDs;
    claimedIDs.setCount(numUnits * targets.count());
    GL(GenTextures(claimedIDs.count(), claimedIDs.begin()));

    auto resetBindings = [&] {
        int i = 0;
        for (int u = 0; u < numUnits; ++u) {
            GL(ActiveTexture(GR_GL_TEXTURE0 + u));
            for (auto target : targets) {
                GL(BindTexture(target.fName, claimedIDs[i++]));
            }
        }
    };
    auto checkBindings = [&] {
        int i = 0;
        for (int u = 0; u < numUnits; ++u) {
            GL(ActiveTexture(GR_GL_TEXTURE0 + u));
            for (auto target : targets) {
                GrGLuint boundID = ~0;
                GL(GetIntegerv(target.fQuery, reinterpret_cast<GrGLint*>(&boundID)));
                if (boundID != claimedIDs[i] && boundID != 0) {
                    ERRORF(reporter, "Unit %d, target 0x%04x has ID %d bound. Expected %d or 0.", u,
                           target.fName, boundID, claimedIDs[i]);
                    return;
                }
                ++i;
            }
        }
    };

    // Initialize texture unit/target combo bindings to 0.
    context->flushAndSubmit();
    resetBindings();
    context->resetContext();

    // Test creating a texture and then resetting bindings.
    static constexpr SkISize kDims = {10, 10};
    auto format = gpu->caps()->getDefaultBackendFormat(GrColorType::kRGBA_8888, GrRenderable::kNo);
    auto tex = gpu->createTexture(kDims, format, GrRenderable::kNo, 1, GrMipmapped::kNo,
                                  SkBudgeted::kNo, GrProtected::kNo);
    REPORTER_ASSERT(reporter, tex);
    context->resetGLTextureBindings();
    checkBindings();
    resetBindings();
    context->resetContext();

    // Test drawing and then resetting bindings. This should force a MIP regeneration if MIP
    // maps are supported as well.
    auto info = SkImageInfo::Make(10, 10, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    auto surf = SkSurface::MakeRenderTarget(context, SkBudgeted::kYes, info, 1, nullptr);
    surf->getCanvas()->clear(0x80FF0000);
    auto img = surf->makeImageSnapshot();
    surf->getCanvas()->clear(SK_ColorBLUE);
    surf->getCanvas()->save();
    surf->getCanvas()->scale(0.25, 0.25);
    SkPaint paint;
    paint.setFilterQuality(kHigh_SkFilterQuality);
    surf->getCanvas()->drawImage(img, 0, 0, &paint);
    surf->getCanvas()->restore();
    surf->flushAndSubmit();
    context->resetGLTextureBindings();
    checkBindings();
    resetBindings();
    context->resetContext();

    if (supportExternal) {
        GrBackendTexture texture2D = context->createBackendTexture(
                10, 10, kRGBA_8888_SkColorType,
                SkColors::kTransparent, GrMipmapped::kNo, GrRenderable::kNo, GrProtected::kNo);
        GrGLTextureInfo info2D;
        REPORTER_ASSERT(reporter, texture2D.getGLTextureInfo(&info2D));
        GrEGLImage eglImage = ctxInfo.glContext()->texture2DToEGLImage(info2D.fID);
        REPORTER_ASSERT(reporter, eglImage);
        GrGLTextureInfo infoExternal;
        infoExternal.fID = ctxInfo.glContext()->eglImageToExternalTexture(eglImage);
        infoExternal.fTarget = GR_GL_TEXTURE_EXTERNAL;
        infoExternal.fFormat = info2D.fFormat;
        REPORTER_ASSERT(reporter, infoExternal.fID);
        GrBackendTexture backendTexture(10, 10, GrMipmapped::kNo, infoExternal);
        // Above texture creation will have messed with GL state and bindings.
        resetBindings();
        context->resetContext();
        img = SkImage::MakeFromTexture(context, backendTexture, kTopLeft_GrSurfaceOrigin,
                                       kRGBA_8888_SkColorType, kPremul_SkAlphaType, nullptr);
        REPORTER_ASSERT(reporter, img);
        surf->getCanvas()->drawImage(img, 0, 0);
        img.reset();
        surf->flushAndSubmit();
        context->resetGLTextureBindings();
        checkBindings();
        resetBindings();
        GL(DeleteTextures(1, &infoExternal.fID));
        ctxInfo.glContext()->destroyEGLImage(eglImage);
        context->deleteBackendTexture(texture2D);
        context->resetContext();
    }

    if (supportRectangle) {
        auto format = GrBackendFormat::MakeGL(GR_GL_RGBA8, GR_GL_TEXTURE_RECTANGLE);
        GrBackendTexture rectangleTexture =
                context->createBackendTexture(10, 10, format, GrMipmapped::kNo, GrRenderable::kNo);
        if (rectangleTexture.isValid()) {
            img = SkImage::MakeFromTexture(context, rectangleTexture, kTopLeft_GrSurfaceOrigin,
                                           kRGBA_8888_SkColorType, kPremul_SkAlphaType, nullptr);
            REPORTER_ASSERT(reporter, img);
            surf->getCanvas()->drawImage(img, 0, 0);
            img.reset();
            surf->flushAndSubmit();
            context->resetGLTextureBindings();
            checkBindings();
            resetBindings();
            context->deleteBackendTexture(rectangleTexture);
        }
    }

    GL(DeleteTextures(claimedIDs.count(), claimedIDs.begin()));

#undef GL
}

#endif  // SK_GL
