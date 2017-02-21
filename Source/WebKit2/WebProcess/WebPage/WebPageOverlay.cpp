/*
 * Copyright (C) 2014 Apple Inc. All rights reserved.
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
#include "WebPageOverlay.h"

#include "WebFrame.h"
#include "WebPage.h"
#include <WebCore/GraphicsLayer.h>
#include <WebCore/PageOverlay.h>
#include <wtf/NeverDestroyed.h>

#include <wtf/macros.h>

using namespace WebCore;

namespace WebKit {

static HashMap<PageOverlay*, WebPageOverlay*>& overlayMap()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    static NeverDestroyed<HashMap<PageOverlay*, WebPageOverlay*>> map;
    return map;
}

Ref<WebPageOverlay> WebPageOverlay::create(std::unique_ptr<WebPageOverlay::Client> client, PageOverlay::OverlayType overlayType)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return adoptRef(*new WebPageOverlay(WTFMove(client), overlayType));
}

WebPageOverlay::WebPageOverlay(std::unique_ptr<WebPageOverlay::Client> client, PageOverlay::OverlayType overlayType)
    : m_overlay(PageOverlay::create(*this, overlayType))
    , m_client(WTFMove(client))
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(m_client);
    overlayMap().add(m_overlay.get(), this);
}

WebPageOverlay::~WebPageOverlay()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!m_overlay)
        return;

    overlayMap().remove(m_overlay.get());
    m_overlay = nullptr;
}

WebPageOverlay* WebPageOverlay::fromCoreOverlay(PageOverlay& overlay)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return overlayMap().get(&overlay);
}

void WebPageOverlay::setNeedsDisplay(const IntRect& dirtyRect)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_overlay->setNeedsDisplay(dirtyRect);
}

void WebPageOverlay::setNeedsDisplay()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_overlay->setNeedsDisplay();
}

void WebPageOverlay::clear()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_overlay->clear();
}

void WebPageOverlay::willMoveToPage(PageOverlay&, Page* page)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_client->willMoveToPage(*this, page ? WebPage::fromCorePage(page) : nullptr);
}

void WebPageOverlay::didMoveToPage(PageOverlay&, Page* page)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_client->didMoveToPage(*this, page ? WebPage::fromCorePage(page) : nullptr);
}

void WebPageOverlay::drawRect(PageOverlay&, GraphicsContext& context, const IntRect& dirtyRect)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_client->drawRect(*this, context, dirtyRect);
}

bool WebPageOverlay::mouseEvent(PageOverlay&, const PlatformMouseEvent& event)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_client->mouseEvent(*this, event);
}

void WebPageOverlay::didScrollFrame(PageOverlay&, Frame& frame)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_client->didScrollFrame(*this, WebFrame::fromCoreFrame(frame));
}

#if PLATFORM(MAC)
DDActionContext *WebPageOverlay::actionContextForResultAtPoint(FloatPoint location, RefPtr<WebCore::Range>& rangeHandle)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_client->actionContextForResultAtPoint(*this, location, rangeHandle);
}

void WebPageOverlay::dataDetectorsDidPresentUI()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_client->dataDetectorsDidPresentUI(*this);
}

void WebPageOverlay::dataDetectorsDidChangeUI()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_client->dataDetectorsDidChangeUI(*this);
}

void WebPageOverlay::dataDetectorsDidHideUI()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_client->dataDetectorsDidHideUI(*this);
}
#endif // PLATFORM(MAC)

bool WebPageOverlay::copyAccessibilityAttributeStringValueForPoint(PageOverlay&, String attribute, FloatPoint parameter, String& value)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_client->copyAccessibilityAttributeStringValueForPoint(*this, attribute, parameter, value);
}

bool WebPageOverlay::copyAccessibilityAttributeBoolValueForPoint(PageOverlay&, String attribute, FloatPoint parameter, bool& value)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_client->copyAccessibilityAttributeBoolValueForPoint(*this, attribute, parameter, value);
}

Vector<String> WebPageOverlay::copyAccessibilityAttributeNames(PageOverlay&, bool parameterizedNames)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_client->copyAccessibilityAttributeNames(*this, parameterizedNames);
}

} // namespace WebKit
