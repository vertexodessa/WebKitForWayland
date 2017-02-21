/*
 * Copyright (C) 2011 Apple Inc. All rights reserved.
 * Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
 * Copyright (C) 2012 Company 100, Inc.
 * Copyright (C) 2014 Igalia S.L.
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
#include "ThreadedCoordinatedLayerTreeHost.h"

#if USE(COORDINATED_GRAPHICS_THREADED)
#include "WebPage.h"
#include <WebCore/FrameView.h>
#include <WebCore/MainFrame.h>

#if PLATFORM(WPE)
#include "DrawingAreaWPE.h"
#endif

#if USE(REDIRECTED_XCOMPOSITE_WINDOW)
#include "RedirectedXCompositeWindow.h"
#endif

#include <wtf/macros.h>

using namespace WebCore;

namespace WebKit {

Ref<ThreadedCoordinatedLayerTreeHost> ThreadedCoordinatedLayerTreeHost::create(WebPage& webPage)
{  WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    return adoptRef(*new ThreadedCoordinatedLayerTreeHost(webPage));
}

ThreadedCoordinatedLayerTreeHost::~ThreadedCoordinatedLayerTreeHost()
{  WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
}

ThreadedCoordinatedLayerTreeHost::ThreadedCoordinatedLayerTreeHost(WebPage& webPage)
    : CoordinatedLayerTreeHost(webPage)
    , m_compositorClient(*this)
#if USE(REDIRECTED_XCOMPOSITE_WINDOW)
    , m_redirectedWindow(RedirectedXCompositeWindow::create(webPage))
    , m_compositor(ThreadedCompositor::create(&m_compositorClient, webPage, m_redirectedWindow ? m_redirectedWindow->window() : 0))
#else
    , m_compositor(ThreadedCompositor::create(&m_compositorClient, webPage))
#endif
{  WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
#if USE(REDIRECTED_XCOMPOSITE_WINDOW)
    if (m_redirectedWindow)
        m_layerTreeContext.contextID = m_redirectedWindow->pixmap();
#endif
}

void ThreadedCoordinatedLayerTreeHost::invalidate()
{  WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    m_compositor->invalidate();
    CoordinatedLayerTreeHost::invalidate();
#if USE(REDIRECTED_XCOMPOSITE_WINDOW)
    m_redirectedWindow = nullptr;
#endif
}

void ThreadedCoordinatedLayerTreeHost::forceRepaint()
{  WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    CoordinatedLayerTreeHost::forceRepaint();
    m_compositor->forceRepaint();
}

void ThreadedCoordinatedLayerTreeHost::scrollNonCompositedContents(const WebCore::IntRect& rect)
{  WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    m_compositor->scrollTo(rect.location());
    scheduleLayerFlush();
}

void ThreadedCoordinatedLayerTreeHost::contentsSizeChanged(const WebCore::IntSize& newSize)
{  WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    m_compositor->didChangeContentsSize(newSize);
}

void ThreadedCoordinatedLayerTreeHost::deviceOrPageScaleFactorChanged()
{  WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
#if USE(REDIRECTED_XCOMPOSITE_WINDOW)
    if (m_redirectedWindow) {
        m_redirectedWindow->resize(m_webPage.size());
        m_layerTreeContext.contextID = m_redirectedWindow->pixmap();
    }
#endif

    CoordinatedLayerTreeHost::deviceOrPageScaleFactorChanged();
    m_compositor->setDeviceScaleFactor(m_webPage.deviceScaleFactor());
}

void ThreadedCoordinatedLayerTreeHost::pageBackgroundTransparencyChanged()
{  WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    CoordinatedLayerTreeHost::pageBackgroundTransparencyChanged();
    m_compositor->setDrawsBackground(m_webPage.drawsBackground());
}

void ThreadedCoordinatedLayerTreeHost::sizeDidChange(const IntSize& size)
{  WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
#if USE(REDIRECTED_XCOMPOSITE_WINDOW)
    if (m_redirectedWindow) {
        m_redirectedWindow->resize(size);
        m_layerTreeContext.contextID = m_redirectedWindow->pixmap();
    }
#endif
    CoordinatedLayerTreeHost::sizeDidChange(size);
    m_compositor->didChangeViewportSize(size);
}

void ThreadedCoordinatedLayerTreeHost::didChangeViewportProperties(const WebCore::ViewportAttributes& attr)
{  WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    m_compositor->didChangeViewportAttribute(attr);
}

void ThreadedCoordinatedLayerTreeHost::didScaleFactorChanged(float scale, const IntPoint& origin)
{  WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    m_webPage.scalePage(scale, origin);
}

#if PLATFORM(GTK) && !USE(REDIRECTED_XCOMPOSITE_WINDOW)
void ThreadedCoordinatedLayerTreeHost::setNativeSurfaceHandleForCompositing(uint64_t handle)
{  WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    m_layerTreeContext.contextID = handle;
    m_compositor->setNativeSurfaceHandleForCompositing(handle);
    scheduleLayerFlush();
}
#endif

void ThreadedCoordinatedLayerTreeHost::setVisibleContentsRect(const FloatRect& rect, const FloatPoint& trajectoryVector, float scale)
{  WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    CoordinatedLayerTreeHost::setVisibleContentsRect(rect, trajectoryVector);
    if (m_lastScrollPosition != roundedIntPoint(rect.location())) {
        m_lastScrollPosition = roundedIntPoint(rect.location());

        if (!m_webPage.corePage()->mainFrame().view()->useFixedLayout())
            m_webPage.corePage()->mainFrame().view()->notifyScrollPositionChanged(m_lastScrollPosition);
    }

    if (m_lastScaleFactor != scale) {
        m_lastScaleFactor = scale;
        didScaleFactorChanged(m_lastScaleFactor, m_lastScrollPosition);
    }
}

void ThreadedCoordinatedLayerTreeHost::commitSceneState(const CoordinatedGraphicsState& state)
{  WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    CoordinatedLayerTreeHost::commitSceneState(state);
    m_compositor->updateSceneState(state);
}

#if USE(REQUEST_ANIMATION_FRAME_DISPLAY_MONITOR)
RefPtr<WebCore::DisplayRefreshMonitor> ThreadedCoordinatedLayerTreeHost::createDisplayRefreshMonitor(PlatformDisplayID displayID)
{  WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    return m_compositor->createDisplayRefreshMonitor(displayID);
}
#endif

} // namespace WebKit

#endif // USE(COORDINATED_GRAPHICS)
