/*
 * Copyright (C) 2011 Apple Inc. All rights reserved.
 * Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
 * Copyright (C) 2012 Company 100, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#if USE(COORDINATED_GRAPHICS)
#include "CoordinatedLayerTreeHost.h"

#include "DrawingArea.h"
#include "WebCoordinatedSurface.h"
#include "WebPage.h"
#include "WebPageProxyMessages.h"
#include <WebCore/FrameView.h>
#include <WebCore/MainFrame.h>
#include <WebCore/PageOverlayController.h>

#if USE(COORDINATED_GRAPHICS_THREADED)
#include "ThreadSafeCoordinatedSurface.h"
#elif USE(COORDINATED_GRAPHICS_MULTIPROCESS)
#include "CoordinatedGraphicsArgumentCoders.h"
#include "CoordinatedLayerTreeHostProxyMessages.h"
#include "WebCoreArgumentCoders.h"
#endif

#include <wtf/macros.h>

using namespace WebCore;

namespace WebKit {

Ref<CoordinatedLayerTreeHost> CoordinatedLayerTreeHost::create(WebPage& webPage)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return adoptRef(*new CoordinatedLayerTreeHost(webPage));
}

CoordinatedLayerTreeHost::~CoordinatedLayerTreeHost()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
}

CoordinatedLayerTreeHost::CoordinatedLayerTreeHost(WebPage& webPage)
    : LayerTreeHost(webPage)
    , m_coordinator(webPage.corePage(), *this)
    , m_layerFlushTimer(RunLoop::main(), this, &CoordinatedLayerTreeHost::layerFlushTimerFired)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
#if PLATFORM(WPE)
    m_layerFlushTimer.setPriority(G_PRIORITY_HIGH + 30);
#endif

    m_coordinator.createRootLayer(m_webPage.size());
#if USE(COORDINATED_GRAPHICS_MULTIPROCESS)
    m_layerTreeContext.contextID = downcast<CoordinatedGraphicsLayer>(*m_coordinator.rootLayer()).id();
#endif

    CoordinatedSurface::setFactory(createCoordinatedSurface);
    scheduleLayerFlush();
}

void CoordinatedLayerTreeHost::scheduleLayerFlush()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!m_layerFlushSchedulingEnabled)
        return;

    if (m_isWaitingForRenderer) {
        m_scheduledWhileWaitingForRenderer = true;
        return;
    }

    if (!m_layerFlushTimer.isActive())
        m_layerFlushTimer.startOneShot(0);
}

void CoordinatedLayerTreeHost::cancelPendingLayerFlush()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_layerFlushTimer.stop();
}

void CoordinatedLayerTreeHost::setViewOverlayRootLayer(GraphicsLayer* viewOverlayRootLayer)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    LayerTreeHost::setViewOverlayRootLayer(viewOverlayRootLayer);
    m_coordinator.setViewOverlayRootLayer(viewOverlayRootLayer);
}

void CoordinatedLayerTreeHost::setRootCompositingLayer(GraphicsLayer* graphicsLayer)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_coordinator.setRootCompositingLayer(graphicsLayer);
}

void CoordinatedLayerTreeHost::invalidate()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    cancelPendingLayerFlush();

    m_coordinator.invalidate();
    LayerTreeHost::invalidate();
}

void CoordinatedLayerTreeHost::forceRepaint()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    // This is necessary for running layout tests. Since in this case we are not waiting for a UIProcess to reply nicely.
    // Instead we are just triggering forceRepaint. But we still want to have the scripted animation callbacks being executed.
    m_coordinator.syncDisplayState();

    // We need to schedule another flush, otherwise the forced paint might cancel a later expected flush.
    // This is aligned with LayerTreeHostCA.
    scheduleLayerFlush();

    if (m_isWaitingForRenderer)
        return;

    m_coordinator.flushPendingLayerChanges();
}

bool CoordinatedLayerTreeHost::forceRepaintAsync(uint64_t callbackID)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    // We expect the UI process to not require a new repaint until the previous one has finished.
    ASSERT(!m_forceRepaintAsyncCallbackID);
    m_forceRepaintAsyncCallbackID = callbackID;
    scheduleLayerFlush();
    return true;
}

void CoordinatedLayerTreeHost::sizeDidChange(const IntSize& newSize)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_coordinator.sizeDidChange(newSize);
    scheduleLayerFlush();
}

void CoordinatedLayerTreeHost::setVisibleContentsRect(const FloatRect& rect, const FloatPoint& trajectoryVector)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_coordinator.setVisibleContentsRect(rect, trajectoryVector);
    scheduleLayerFlush();
}

void CoordinatedLayerTreeHost::renderNextFrame()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_isWaitingForRenderer = false;
    bool scheduledWhileWaitingForRenderer = std::exchange(m_scheduledWhileWaitingForRenderer, false);
    m_coordinator.renderNextFrame();

    if (scheduledWhileWaitingForRenderer || m_layerFlushTimer.isActive()) {
        m_layerFlushTimer.stop();
        layerFlushTimerFired();
    }
}

void CoordinatedLayerTreeHost::didFlushRootLayer(const FloatRect& visibleContentRect)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    // Because our view-relative overlay root layer is not attached to the FrameView's GraphicsLayer tree, we need to flush it manually.
    if (m_viewOverlayRootLayer)
        m_viewOverlayRootLayer->flushCompositingState(visibleContentRect,  m_webPage.mainFrame()->view()->viewportIsStable());
}

void CoordinatedLayerTreeHost::layerFlushTimerFired()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (m_isSuspended || m_isWaitingForRenderer)
        return;

    m_coordinator.syncDisplayState();

    if (!m_isValid)
        return;

    bool didSync = m_coordinator.flushPendingLayerChanges();

    if (m_forceRepaintAsyncCallbackID) {
        m_webPage.send(Messages::WebPageProxy::VoidCallback(m_forceRepaintAsyncCallbackID));
        m_forceRepaintAsyncCallbackID = 0;
    }

    if (m_notifyAfterScheduledLayerFlush && didSync) {
        m_webPage.drawingArea()->layerHostDidFlushLayers();
        m_notifyAfterScheduledLayerFlush = false;
    }
}

void CoordinatedLayerTreeHost::paintLayerContents(const GraphicsLayer*, GraphicsContext&, const IntRect&)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
}

void CoordinatedLayerTreeHost::commitSceneState(const CoordinatedGraphicsState& state)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
#if USE(COORDINATED_GRAPHICS_MULTIPROCESS)
    m_webPage.send(Messages::CoordinatedLayerTreeHostProxy::CommitCoordinatedGraphicsState(state));
#endif
    m_isWaitingForRenderer = true;
}

RefPtr<CoordinatedSurface> CoordinatedLayerTreeHost::createCoordinatedSurface(const IntSize& size, CoordinatedSurface::Flags flags)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
#if USE(COORDINATED_GRAPHICS_THREADED)
    return ThreadSafeCoordinatedSurface::create(size, flags);
#elif USE(COORDINATED_GRAPHICS_MULTIPROCESS)
    return WebCoordinatedSurface::create(size, flags);
#else
    UNUSED_PARAM(size);
    UNUSED_PARAM(flags);
    return nullptr;
#endif
}

void CoordinatedLayerTreeHost::deviceOrPageScaleFactorChanged()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_coordinator.deviceOrPageScaleFactorChanged();
    m_webPage.mainFrame()->pageOverlayController().didChangeDeviceScaleFactor();
}

void CoordinatedLayerTreeHost::pageBackgroundTransparencyChanged()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
}

GraphicsLayerFactory* CoordinatedLayerTreeHost::graphicsLayerFactory()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return &m_coordinator;
}

#if ENABLE(REQUEST_ANIMATION_FRAME)
void CoordinatedLayerTreeHost::scheduleAnimation()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (m_isWaitingForRenderer)
        return;

    if (m_layerFlushTimer.isActive())
        return;

    scheduleLayerFlush();
    m_layerFlushTimer.startOneShot(m_coordinator.nextAnimationServiceTime());
}
#endif

void CoordinatedLayerTreeHost::commitScrollOffset(uint32_t layerID, const WebCore::IntSize& offset)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_coordinator.commitScrollOffset(layerID, offset);
}

} // namespace WebKit
#endif // USE(COORDINATED_GRAPHICS)
