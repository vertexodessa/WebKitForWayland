/*
 * Copyright (C) 2010-2016 Apple Inc. All rights reserved.
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
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
#include "WebChromeClient.h"

#include "APIArray.h"
#include "APISecurityOrigin.h"
#include "DrawingArea.h"
#include "HangDetectionDisabler.h"
#include "InjectedBundleNavigationAction.h"
#include "InjectedBundleNodeHandle.h"
#include "NavigationActionData.h"
#include "PageBanner.h"
#include "UserData.h"
#include "WebColorChooser.h"
#include "WebCoreArgumentCoders.h"
#include "WebFrame.h"
#include "WebFrameLoaderClient.h"
#include "WebFullScreenManager.h"
#include "WebHitTestResultData.h"
#include "WebImage.h"
#include "WebOpenPanelResultListener.h"
#include "WebPage.h"
#include "WebPageCreationParameters.h"
#include "WebPageProxyMessages.h"
#include "WebPopupMenu.h"
#include "WebProcess.h"
#include "WebProcessProxyMessages.h"
#include "WebSearchPopupMenu.h"
#include <WebCore/ApplicationCacheStorage.h>
#include <WebCore/AXObjectCache.h>
#include <WebCore/ColorChooser.h>
#include <WebCore/DatabaseManager.h>
#include <WebCore/DocumentLoader.h>
#include <WebCore/FileChooser.h>
#include <WebCore/FileIconLoader.h>
#include <WebCore/FrameLoadRequest.h>
#include <WebCore/FrameLoader.h>
#include <WebCore/FrameView.h>
#include <WebCore/HTMLInputElement.h>
#include <WebCore/HTMLNames.h>
#include <WebCore/HTMLParserIdioms.h>
#include <WebCore/HTMLPlugInImageElement.h>
#include <WebCore/Icon.h>
#include <WebCore/MainFrame.h>
#include <WebCore/NotImplemented.h>
#include <WebCore/Page.h>
#include <WebCore/ScriptController.h>
#include <WebCore/SecurityOrigin.h>
#include <WebCore/SecurityOriginData.h>
#include <WebCore/Settings.h>
#include <JavaScriptCore/ConsoleTypes.h>

#if PLATFORM(IOS) || (PLATFORM(MAC) && ENABLE(VIDEO_PRESENTATION_MODE))
#include "WebPlaybackSessionManager.h"
#include "WebVideoFullscreenManager.h"
#endif

#if ENABLE(ASYNC_SCROLLING)
#include "RemoteScrollingCoordinator.h"
#endif

#if USE(COORDINATED_GRAPHICS)
#include "LayerTreeHost.h"
#endif

#if PLATFORM(GTK)
#include "PrinterListGtk.h"
#endif

#include <wtf/macros.h>



using namespace WebCore;
using namespace HTMLNames;

namespace WebKit {

static double area(WebFrame* frame)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    IntSize size = frame->visibleContentBoundsExcludingScrollbars().size();
    return static_cast<double>(size.height()) * size.width();
}


static WebFrame* findLargestFrameInFrameSet(WebPage* page)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    // Approximate what a user could consider a default target frame for application menu operations.

    WebFrame* mainFrame = page->mainWebFrame();
    if (!mainFrame || !mainFrame->isFrameSet())
        return 0;

    WebFrame* largestSoFar = 0;

    Ref<API::Array> frameChildren = mainFrame->childFrames();
    size_t count = frameChildren->size();
    for (size_t i = 0; i < count; ++i) {
        WebFrame* childFrame = frameChildren->at<WebFrame>(i);
        if (!largestSoFar || area(childFrame) > area(largestSoFar))
            largestSoFar = childFrame;
    }

    return largestSoFar;
}

void WebChromeClient::chromeDestroyed()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    delete this;
}

void WebChromeClient::setWindowRect(const FloatRect& windowFrame)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->sendSetWindowFrame(windowFrame);
}

FloatRect WebChromeClient::windowRect()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
#if PLATFORM(IOS)
    return FloatRect();
#else
#if PLATFORM(MAC)
    if (m_page->hasCachedWindowFrame())
        return m_page->windowFrameInUnflippedScreenCoordinates();
#endif

    FloatRect newWindowFrame;

    if (!WebProcess::singleton().parentProcessConnection()->sendSync(Messages::WebPageProxy::GetWindowFrame(), Messages::WebPageProxy::GetWindowFrame::Reply(newWindowFrame), m_page->pageID()))
        return FloatRect();

    return newWindowFrame;
#endif
}

FloatRect WebChromeClient::pageRect()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return FloatRect(FloatPoint(), m_page->size());
}

void WebChromeClient::focus()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->send(Messages::WebPageProxy::SetFocus(true));
}

void WebChromeClient::unfocus()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->send(Messages::WebPageProxy::SetFocus(false));
}

#if PLATFORM(COCOA)
void WebChromeClient::elementDidFocus(const WebCore::Node* node)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->elementDidFocus(const_cast<WebCore::Node*>(node));
}

void WebChromeClient::elementDidBlur(const WebCore::Node* node)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->elementDidBlur(const_cast<WebCore::Node*>(node));
}

void WebChromeClient::makeFirstResponder()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->send(Messages::WebPageProxy::MakeFirstResponder());
}    
#endif    

bool WebChromeClient::canTakeFocus(FocusDirection)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    notImplemented();
    return true;
}

void WebChromeClient::takeFocus(FocusDirection direction)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->send(Messages::WebPageProxy::TakeFocus(direction));
}

void WebChromeClient::focusedElementChanged(Element* element)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!is<HTMLInputElement>(element))
        return;

    HTMLInputElement& inputElement = downcast<HTMLInputElement>(*element);
    if (!inputElement.isText())
        return;

    WebFrame* webFrame = WebFrame::fromCoreFrame(*element->document().frame());
    ASSERT(webFrame);
    m_page->injectedBundleFormClient().didFocusTextField(m_page, &inputElement, webFrame);
}

void WebChromeClient::focusedFrameChanged(Frame* frame)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    WebFrame* webFrame = frame ? WebFrame::fromCoreFrame(*frame) : nullptr;

    WebProcess::singleton().parentProcessConnection()->send(Messages::WebPageProxy::FocusedFrameChanged(webFrame ? webFrame->frameID() : 0), m_page->pageID());
}

Page* WebChromeClient::createWindow(Frame* frame, const FrameLoadRequest& request, const WindowFeatures& windowFeatures, const NavigationAction& navigationAction)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
#if ENABLE(FULLSCREEN_API)
    if (frame->document() && frame->document()->webkitCurrentFullScreenElement())
        frame->document()->webkitCancelFullScreen();
#endif

    auto& webProcess = WebProcess::singleton();

    WebFrame* webFrame = WebFrame::fromCoreFrame(*frame);

    NavigationActionData navigationActionData;
    navigationActionData.navigationType = navigationAction.type();
    navigationActionData.modifiers = InjectedBundleNavigationAction::modifiersForNavigationAction(navigationAction);
    navigationActionData.mouseButton = InjectedBundleNavigationAction::mouseButtonForNavigationAction(navigationAction);
    navigationActionData.syntheticClickType = InjectedBundleNavigationAction::syntheticClickTypeForNavigationAction(navigationAction);
    navigationActionData.userGestureTokenIdentifier = webProcess.userGestureTokenIdentifier(navigationAction.userGestureToken());
    navigationActionData.canHandleRequest = m_page->canHandleRequest(request.resourceRequest());
    navigationActionData.shouldOpenExternalURLsPolicy = navigationAction.shouldOpenExternalURLsPolicy();
    navigationActionData.downloadAttribute = navigationAction.downloadAttribute();

    uint64_t newPageID = 0;
    WebPageCreationParameters parameters;
    if (!webProcess.parentProcessConnection()->sendSync(Messages::WebPageProxy::CreateNewPage(webFrame->frameID(), SecurityOriginData::fromFrame(frame), request.resourceRequest(), windowFeatures, navigationActionData), Messages::WebPageProxy::CreateNewPage::Reply(newPageID, parameters), m_page->pageID()))
        return nullptr;

    if (!newPageID)
        return nullptr;

    webProcess.createWebPage(newPageID, parameters);
    return webProcess.webPage(newPageID)->corePage();
}

void WebChromeClient::show()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->show();
}

bool WebChromeClient::canRunModal()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->canRunModal();
}

void WebChromeClient::runModal()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->runModal();
}

void WebChromeClient::setToolbarsVisible(bool toolbarsAreVisible)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->send(Messages::WebPageProxy::SetToolbarsAreVisible(toolbarsAreVisible));
}

bool WebChromeClient::toolbarsVisible()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    API::InjectedBundle::PageUIClient::UIElementVisibility toolbarsVisibility = m_page->injectedBundleUIClient().toolbarsAreVisible(m_page);
    if (toolbarsVisibility != API::InjectedBundle::PageUIClient::UIElementVisibility::Unknown)
        return toolbarsVisibility == API::InjectedBundle::PageUIClient::UIElementVisibility::Visible;
    
    bool toolbarsAreVisible = true;
    if (!WebProcess::singleton().parentProcessConnection()->sendSync(Messages::WebPageProxy::GetToolbarsAreVisible(), Messages::WebPageProxy::GetToolbarsAreVisible::Reply(toolbarsAreVisible), m_page->pageID()))
        return true;

    return toolbarsAreVisible;
}

void WebChromeClient::setStatusbarVisible(bool statusBarIsVisible)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->send(Messages::WebPageProxy::SetStatusBarIsVisible(statusBarIsVisible));
}

bool WebChromeClient::statusbarVisible()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    API::InjectedBundle::PageUIClient::UIElementVisibility statusbarVisibility = m_page->injectedBundleUIClient().statusBarIsVisible(m_page);
    if (statusbarVisibility != API::InjectedBundle::PageUIClient::UIElementVisibility::Unknown)
        return statusbarVisibility == API::InjectedBundle::PageUIClient::UIElementVisibility::Visible;

    bool statusBarIsVisible = true;
    if (!WebProcess::singleton().parentProcessConnection()->sendSync(Messages::WebPageProxy::GetStatusBarIsVisible(), Messages::WebPageProxy::GetStatusBarIsVisible::Reply(statusBarIsVisible), m_page->pageID()))
        return true;

    return statusBarIsVisible;
}

void WebChromeClient::setScrollbarsVisible(bool)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    notImplemented();
}

bool WebChromeClient::scrollbarsVisible()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    notImplemented();
    return true;
}

void WebChromeClient::setMenubarVisible(bool menuBarVisible)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->send(Messages::WebPageProxy::SetMenuBarIsVisible(menuBarVisible));
}

bool WebChromeClient::menubarVisible()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    API::InjectedBundle::PageUIClient::UIElementVisibility menubarVisibility = m_page->injectedBundleUIClient().menuBarIsVisible(m_page);
    if (menubarVisibility != API::InjectedBundle::PageUIClient::UIElementVisibility::Unknown)
        return menubarVisibility == API::InjectedBundle::PageUIClient::UIElementVisibility::Visible;
    
    bool menuBarIsVisible = true;
    if (!WebProcess::singleton().parentProcessConnection()->sendSync(Messages::WebPageProxy::GetMenuBarIsVisible(), Messages::WebPageProxy::GetMenuBarIsVisible::Reply(menuBarIsVisible), m_page->pageID()))
        return true;

    return menuBarIsVisible;
}

void WebChromeClient::setResizable(bool resizable)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->send(Messages::WebPageProxy::SetIsResizable(resizable));
}

void WebChromeClient::addMessageToConsole(MessageSource source, MessageLevel level, const String& message, unsigned lineNumber, unsigned columnNumber, const String& sourceID)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    // Notify the bundle client.
    m_page->injectedBundleUIClient().willAddMessageToConsole(m_page, source, level, message, lineNumber, columnNumber, sourceID);

    String src;
    switch(source) {
        case JSC::MessageSource::XML:
            src = "XML"; break;
        case JSC::MessageSource::JS:
            src = "JS"; break;
        case JSC::MessageSource::Network:
            src = "Network"; break;
        case JSC::MessageSource::ConsoleAPI:
            src = "ConsoleAPI"; break;
        case JSC::MessageSource::Storage:
            src = "Storage"; break;
        case JSC::MessageSource::AppCache:
            src = "AppCache"; break;
        case JSC::MessageSource::Rendering:
            src = "Rendering"; break;
        case JSC::MessageSource::CSS:
            src = "CSS"; break;
        case JSC::MessageSource::Security:
            src = "Security"; break;
        case JSC::MessageSource::ContentBlocker:
            src = "ContentBlocker"; break;
        case JSC::MessageSource::Other:
            src = "Other"; break;
    }

    String lvl;
    switch (level) {
        case JSC::MessageLevel::Log:
            lvl = "Log"; break;
        case JSC::MessageLevel::Warning:
            lvl = "Warning"; break;
        case JSC::MessageLevel::Error:
            lvl = "Error"; break;
        case JSC::MessageLevel::Debug:
            lvl = "Debug"; break;
        case JSC::MessageLevel::Info:
            lvl = "Info"; break;
    }

    m_page->send(Messages::WebPageProxy::WillAddDetailedMessageToConsole(src, lvl, lineNumber, columnNumber, message, sourceID));
}

bool WebChromeClient::canRunBeforeUnloadConfirmPanel()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->canRunBeforeUnloadConfirmPanel();
}

bool WebChromeClient::runBeforeUnloadConfirmPanel(const String& message, Frame* frame)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    WebFrame* webFrame = WebFrame::fromCoreFrame(*frame);

    bool shouldClose = false;

    HangDetectionDisabler hangDetectionDisabler;

    if (!WebProcess::singleton().parentProcessConnection()->sendSync(Messages::WebPageProxy::RunBeforeUnloadConfirmPanel(message, webFrame->frameID()), Messages::WebPageProxy::RunBeforeUnloadConfirmPanel::Reply(shouldClose), m_page->pageID(), std::chrono::milliseconds::max(), IPC::InformPlatformProcessWillSuspend))
        return false;

    return shouldClose;
}

void WebChromeClient::closeWindowSoon()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    // FIXME: This code assumes that the client will respond to a close page
    // message by actually closing the page. Safari does this, but there is
    // no guarantee that other applications will, which will leave this page
    // half detached. This approach is an inherent limitation making parts of
    // a close execute synchronously as part of window.close, but other parts
    // later on.

    m_page->corePage()->setGroupName(String());

    if (WebFrame* frame = m_page->mainWebFrame()) {
        if (Frame* coreFrame = frame->coreFrame())
            coreFrame->loader().stopForUserCancel();
    }

    m_page->sendClose();
}

void WebChromeClient::runJavaScriptAlert(Frame* frame, const String& alertText)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    WebFrame* webFrame = WebFrame::fromCoreFrame(*frame);
    ASSERT(webFrame);

    // Notify the bundle client.
    m_page->injectedBundleUIClient().willRunJavaScriptAlert(m_page, alertText, webFrame);

    HangDetectionDisabler hangDetectionDisabler;

    WebProcess::singleton().parentProcessConnection()->sendSync(Messages::WebPageProxy::RunJavaScriptAlert(webFrame->frameID(), SecurityOriginData::fromFrame(frame), alertText), Messages::WebPageProxy::RunJavaScriptAlert::Reply(), m_page->pageID(), std::chrono::milliseconds::max(), IPC::InformPlatformProcessWillSuspend);
}

bool WebChromeClient::runJavaScriptConfirm(Frame* frame, const String& message)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    WebFrame* webFrame = WebFrame::fromCoreFrame(*frame);
    ASSERT(webFrame);

    // Notify the bundle client.
    m_page->injectedBundleUIClient().willRunJavaScriptConfirm(m_page, message, webFrame);

    HangDetectionDisabler hangDetectionDisabler;

    bool result = false;
    if (!WebProcess::singleton().parentProcessConnection()->sendSync(Messages::WebPageProxy::RunJavaScriptConfirm(webFrame->frameID(), SecurityOriginData::fromFrame(frame), message), Messages::WebPageProxy::RunJavaScriptConfirm::Reply(result), m_page->pageID(), std::chrono::milliseconds::max(), IPC::InformPlatformProcessWillSuspend))
        return false;

    return result;
}

bool WebChromeClient::runJavaScriptPrompt(Frame* frame, const String& message, const String& defaultValue, String& result)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    WebFrame* webFrame = WebFrame::fromCoreFrame(*frame);
    ASSERT(webFrame);

    // Notify the bundle client.
    m_page->injectedBundleUIClient().willRunJavaScriptPrompt(m_page, message, defaultValue, webFrame);

    HangDetectionDisabler hangDetectionDisabler;

    if (!WebProcess::singleton().parentProcessConnection()->sendSync(Messages::WebPageProxy::RunJavaScriptPrompt(webFrame->frameID(), SecurityOriginData::fromFrame(frame), message, defaultValue), Messages::WebPageProxy::RunJavaScriptPrompt::Reply(result), m_page->pageID(), std::chrono::milliseconds::max(), IPC::InformPlatformProcessWillSuspend))
        return false;

    return !result.isNull();
}

void WebChromeClient::setStatusbarText(const String& statusbarText)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    // Notify the bundle client.
    m_page->injectedBundleUIClient().willSetStatusbarText(m_page, statusbarText);

    m_page->send(Messages::WebPageProxy::SetStatusText(statusbarText));
}

KeyboardUIMode WebChromeClient::keyboardUIMode()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->keyboardUIMode();
}

void WebChromeClient::invalidateRootView(const IntRect&)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    // Do nothing here, there's no concept of invalidating the window in the web process.
}

void WebChromeClient::invalidateContentsAndRootView(const IntRect& rect)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (Document* document = m_page->corePage()->mainFrame().document()) {
        if (document->printing())
            return;
    }

    m_page->drawingArea()->setNeedsDisplayInRect(rect);
}

void WebChromeClient::invalidateContentsForSlowScroll(const IntRect& rect)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (Document* document = m_page->corePage()->mainFrame().document()) {
        if (document->printing())
            return;
    }

    m_page->pageDidScroll();
#if USE(COORDINATED_GRAPHICS)
    m_page->drawingArea()->scroll(rect, IntSize());
#else
    m_page->drawingArea()->setNeedsDisplayInRect(rect);
#endif
}

void WebChromeClient::scroll(const IntSize& scrollDelta, const IntRect& scrollRect, const IntRect& clipRect)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->pageDidScroll();
    m_page->drawingArea()->scroll(intersection(scrollRect, clipRect), scrollDelta);
}

#if USE(COORDINATED_GRAPHICS)
void WebChromeClient::delegatedScrollRequested(const IntPoint& scrollOffset)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->pageDidRequestScroll(scrollOffset);
}
#endif

IntPoint WebChromeClient::screenToRootView(const IntPoint& point) const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->screenToRootView(point);
}

IntRect WebChromeClient::rootViewToScreen(const IntRect& rect) const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->rootViewToScreen(rect);
}
    
#if PLATFORM(IOS)
IntPoint WebChromeClient::accessibilityScreenToRootView(const IntPoint& point) const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->accessibilityScreenToRootView(point);
}

IntRect WebChromeClient::rootViewToAccessibilityScreen(const IntRect& rect) const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->rootViewToAccessibilityScreen(rect);
}
#endif

PlatformPageClient WebChromeClient::platformPageClient() const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    notImplemented();
    return 0;
}

void WebChromeClient::contentsSizeChanged(Frame* frame, const IntSize& size) const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!m_page->corePage()->settings().frameFlatteningEnabled()) {
        WebFrame* largestFrame = findLargestFrameInFrameSet(m_page);
        if (largestFrame != m_cachedFrameSetLargestFrame.get()) {
            m_cachedFrameSetLargestFrame = largestFrame;
            m_page->send(Messages::WebPageProxy::FrameSetLargestFrameChanged(largestFrame ? largestFrame->frameID() : 0));
        }
    }

    if (&frame->page()->mainFrame() != frame)
        return;

    m_page->send(Messages::WebPageProxy::DidChangeContentSize(size));

    m_page->drawingArea()->mainFrameContentSizeChanged(size);

    FrameView* frameView = frame->view();
    if (frameView && !frameView->delegatesScrolling())  {
        bool hasHorizontalScrollbar = frameView->horizontalScrollbar();
        bool hasVerticalScrollbar = frameView->verticalScrollbar();

        if (hasHorizontalScrollbar != m_cachedMainFrameHasHorizontalScrollbar || hasVerticalScrollbar != m_cachedMainFrameHasVerticalScrollbar) {
            m_page->send(Messages::WebPageProxy::DidChangeScrollbarsForMainFrame(hasHorizontalScrollbar, hasVerticalScrollbar));

            m_cachedMainFrameHasHorizontalScrollbar = hasHorizontalScrollbar;
            m_cachedMainFrameHasVerticalScrollbar = hasVerticalScrollbar;
        }
    }
}

void WebChromeClient::scrollRectIntoView(const IntRect&) const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    notImplemented();
}

bool WebChromeClient::shouldUnavailablePluginMessageBeButton(RenderEmbeddedObject::PluginUnavailabilityReason pluginUnavailabilityReason) const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    switch (pluginUnavailabilityReason) {
    case RenderEmbeddedObject::PluginMissing:
        // FIXME: <rdar://problem/8794397> We should only return true when there is a
        // missingPluginButtonClicked callback defined on the Page UI client.
    case RenderEmbeddedObject::InsecurePluginVersion:
        return true;


    case RenderEmbeddedObject::PluginCrashed:
    case RenderEmbeddedObject::PluginBlockedByContentSecurityPolicy:
        return false;
    }

    ASSERT_NOT_REACHED();
    return false;
}
    
void WebChromeClient::unavailablePluginButtonClicked(Element* element, RenderEmbeddedObject::PluginUnavailabilityReason pluginUnavailabilityReason) const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
#if ENABLE(NETSCAPE_PLUGIN_API)
    ASSERT(element->hasTagName(objectTag) || element->hasTagName(embedTag) || element->hasTagName(appletTag));
    ASSERT(pluginUnavailabilityReason == RenderEmbeddedObject::PluginMissing || pluginUnavailabilityReason == RenderEmbeddedObject::InsecurePluginVersion || pluginUnavailabilityReason);

    HTMLPlugInImageElement* pluginElement = static_cast<HTMLPlugInImageElement*>(element);

    String frameURLString = pluginElement->document().frame()->loader().documentLoader()->responseURL().string();
    String pageURLString = m_page->mainFrame()->loader().documentLoader()->responseURL().string();
    String pluginURLString = pluginElement->document().completeURL(pluginElement->url()).string();
    URL pluginspageAttributeURL = element->document().completeURL(stripLeadingAndTrailingHTMLSpaces(pluginElement->getAttribute(pluginspageAttr)));
    if (!pluginspageAttributeURL.protocolIsInHTTPFamily())
        pluginspageAttributeURL = URL();
    m_page->send(Messages::WebPageProxy::UnavailablePluginButtonClicked(pluginUnavailabilityReason, pluginElement->serviceType(), pluginURLString, pluginspageAttributeURL.string(), frameURLString, pageURLString));
#else
    UNUSED_PARAM(element);
    UNUSED_PARAM(pluginUnavailabilityReason);
#endif // ENABLE(NETSCAPE_PLUGIN_API)
}

void WebChromeClient::scrollbarsModeDidChange() const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    notImplemented();
}

void WebChromeClient::mouseDidMoveOverElement(const HitTestResult& hitTestResult, unsigned modifierFlags)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    RefPtr<API::Object> userData;

    // Notify the bundle client.
    m_page->injectedBundleUIClient().mouseDidMoveOverElement(m_page, hitTestResult, static_cast<WebEvent::Modifiers>(modifierFlags), userData);

    // Notify the UIProcess.
    WebHitTestResultData webHitTestResultData(hitTestResult);
    m_page->send(Messages::WebPageProxy::MouseDidMoveOverElement(webHitTestResultData, modifierFlags, UserData(WebProcess::singleton().transformObjectsToHandles(userData.get()).get())));
}

void WebChromeClient::setToolTip(const String& toolTip, TextDirection)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    // Only send a tool tip to the WebProcess if it has changed since the last time this function was called.

    if (toolTip == m_cachedToolTip)
        return;
    m_cachedToolTip = toolTip;

    m_page->send(Messages::WebPageProxy::SetToolTip(m_cachedToolTip));
}

void WebChromeClient::print(Frame* frame)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    WebFrame* webFrame = WebFrame::fromCoreFrame(*frame);
    ASSERT(webFrame);

#if PLATFORM(GTK) && HAVE(GTK_UNIX_PRINTING)
    // When printing synchronously in GTK+ we need to make sure that we have a list of Printers before starting the print operation.
    // Getting the list of printers is done synchronously by GTK+, but using a nested main loop that might process IPC messages
    // comming from the UI process like EndPrinting. When the EndPriting message is received while the printer list is being populated,
    // the print operation is finished unexpectely and the web process crashes, see https://bugs.webkit.org/show_bug.cgi?id=126979.
    // The PrinterListGtk class gets the list of printers in the constructor so we just need to ensure there's an instance alive
    // during the synchronous print operation.
    RefPtr<PrinterListGtk> printerList = PrinterListGtk::getOrCreate();
    if (!printerList) {
        // PrinterListGtk::getOrCreate() returns nullptr when called while a printers enumeration is ongoing.
        // This can happen if a synchronous print is started by a JavaScript and another one is inmeditaley started
        // from a JavaScript event listener. The second print operation is handled by the nested main loop used by GTK+
        // to enumerate the printers, and we end up here trying to get a reference of an object that is being constructed.
        // It's very unlikely that the user wants to print twice in a row, and other browsers don't do two print operations
        // in this particular case either. So, the safest solution is to return early here and ignore the second print.
        // See https://bugs.webkit.org/show_bug.cgi?id=141035
        return;
    }
#endif

    m_page->sendSync(Messages::WebPageProxy::PrintFrame(webFrame->frameID()), Messages::WebPageProxy::PrintFrame::Reply(), std::chrono::milliseconds::max(), IPC::InformPlatformProcessWillSuspend);
}

void WebChromeClient::exceededDatabaseQuota(Frame* frame, const String& databaseName, DatabaseDetails details)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    WebFrame* webFrame = WebFrame::fromCoreFrame(*frame);
    ASSERT(webFrame);
    
    SecurityOrigin* origin = frame->document()->securityOrigin();

    DatabaseManager& dbManager = DatabaseManager::singleton();
    uint64_t currentQuota = dbManager.quotaForOrigin(origin);
    uint64_t currentOriginUsage = dbManager.usageForOrigin(origin);
    uint64_t newQuota = 0;
    RefPtr<API::SecurityOrigin> securityOrigin = API::SecurityOrigin::create(WebCore::SecurityOrigin::createFromDatabaseIdentifier(origin->databaseIdentifier()));
    newQuota = m_page->injectedBundleUIClient().didExceedDatabaseQuota(m_page, securityOrigin.get(), databaseName, details.displayName(), currentQuota, currentOriginUsage, details.currentUsage(), details.expectedUsage());

    if (!newQuota) {
        WebProcess::singleton().parentProcessConnection()->sendSync(
            Messages::WebPageProxy::ExceededDatabaseQuota(webFrame->frameID(), origin->databaseIdentifier(), databaseName, details.displayName(), currentQuota, currentOriginUsage, details.currentUsage(), details.expectedUsage()),
            Messages::WebPageProxy::ExceededDatabaseQuota::Reply(newQuota), m_page->pageID(), std::chrono::milliseconds::max(), IPC::InformPlatformProcessWillSuspend);
    }

    dbManager.setQuota(origin, newQuota);
}

void WebChromeClient::reachedMaxAppCacheSize(int64_t)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    notImplemented();
}

void WebChromeClient::reachedApplicationCacheOriginQuota(SecurityOrigin* origin, int64_t totalBytesNeeded)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    RefPtr<API::SecurityOrigin> securityOrigin = API::SecurityOrigin::createFromString(origin->toString());
    if (m_page->injectedBundleUIClient().didReachApplicationCacheOriginQuota(m_page, securityOrigin.get(), totalBytesNeeded))
        return;

    auto& cacheStorage = m_page->corePage()->applicationCacheStorage();
    int64_t currentQuota = 0;
    if (!cacheStorage.calculateQuotaForOrigin(origin, currentQuota))
        return;

    uint64_t newQuota = 0;
    WebProcess::singleton().parentProcessConnection()->sendSync(
        Messages::WebPageProxy::ReachedApplicationCacheOriginQuota(origin->databaseIdentifier(), currentQuota, totalBytesNeeded),
        Messages::WebPageProxy::ReachedApplicationCacheOriginQuota::Reply(newQuota), m_page->pageID(), std::chrono::milliseconds::max(), IPC::InformPlatformProcessWillSuspend);

    cacheStorage.storeUpdatedQuotaForOrigin(origin, newQuota);
}

#if ENABLE(DASHBOARD_SUPPORT)
void WebChromeClient::annotatedRegionsChanged()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    notImplemented();
}
#endif

bool WebChromeClient::shouldReplaceWithGeneratedFileForUpload(const String& path, String& generatedFilename)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    generatedFilename = m_page->injectedBundleUIClient().shouldGenerateFileForUpload(m_page, path);
    return !generatedFilename.isNull();
}

String WebChromeClient::generateReplacementFile(const String& path)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->injectedBundleUIClient().generateFileForUpload(m_page, path);
}

#if ENABLE(INPUT_TYPE_COLOR)
std::unique_ptr<ColorChooser> WebChromeClient::createColorChooser(ColorChooserClient* client, const Color& initialColor)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return std::make_unique<WebColorChooser>(m_page, client, initialColor);
}
#endif

void WebChromeClient::runOpenPanel(Frame* frame, PassRefPtr<FileChooser> prpFileChooser)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (m_page->activeOpenPanelResultListener())
        return;

    RefPtr<FileChooser> fileChooser = prpFileChooser;

    m_page->setActiveOpenPanelResultListener(WebOpenPanelResultListener::create(m_page, fileChooser.get()));

    WebFrame* webFrame = WebFrame::fromCoreFrame(*frame);
    ASSERT(webFrame);

    m_page->send(Messages::WebPageProxy::RunOpenPanel(webFrame->frameID(), SecurityOriginData::fromFrame(frame), fileChooser->settings()));
}

void WebChromeClient::loadIconForFiles(const Vector<String>& filenames, FileIconLoader* loader)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    loader->notifyFinished(Icon::createIconForFiles(filenames));
}

#if !PLATFORM(IOS)
void WebChromeClient::setCursor(const WebCore::Cursor& cursor)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->send(Messages::WebPageProxy::SetCursor(cursor));
}

void WebChromeClient::setCursorHiddenUntilMouseMoves(bool hiddenUntilMouseMoves)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->send(Messages::WebPageProxy::SetCursorHiddenUntilMouseMoves(hiddenUntilMouseMoves));
}
#endif

#if ENABLE(REQUEST_ANIMATION_FRAME) && !USE(REQUEST_ANIMATION_FRAME_TIMER)
void WebChromeClient::scheduleAnimation()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
#if USE(COORDINATED_GRAPHICS)
    m_page->drawingArea()->layerTreeHost()->scheduleAnimation();
#endif
}
#endif

void WebChromeClient::didAssociateFormControls(const Vector<RefPtr<WebCore::Element>>& elements)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->injectedBundleFormClient().didAssociateFormControls(m_page, elements);
}

bool WebChromeClient::shouldNotifyOnFormChanges()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->injectedBundleFormClient().shouldNotifyOnFormChanges(m_page);
}

bool WebChromeClient::selectItemWritingDirectionIsNatural()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
#if PLATFORM(EFL)
    return true;
#else
    return false;
#endif
}

bool WebChromeClient::selectItemAlignmentFollowsMenuWritingDirection()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return true;
}

bool WebChromeClient::hasOpenedPopup() const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    notImplemented();
    return false;
}

RefPtr<WebCore::PopupMenu> WebChromeClient::createPopupMenu(WebCore::PopupMenuClient* client) const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return WebPopupMenu::create(m_page, client);
}

RefPtr<WebCore::SearchPopupMenu> WebChromeClient::createSearchPopupMenu(WebCore::PopupMenuClient* client) const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return WebSearchPopupMenu::create(m_page, client);
}

GraphicsLayerFactory* WebChromeClient::graphicsLayerFactory() const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (auto drawingArea = m_page->drawingArea())
        return drawingArea->graphicsLayerFactory();
    return nullptr;
}

#if USE(REQUEST_ANIMATION_FRAME_DISPLAY_MONITOR)
RefPtr<WebCore::DisplayRefreshMonitor> WebChromeClient::createDisplayRefreshMonitor(PlatformDisplayID displayID) const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->drawingArea()->createDisplayRefreshMonitor(displayID);
}
#endif

void WebChromeClient::attachRootGraphicsLayer(Frame*, GraphicsLayer* layer)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (layer)
        m_page->enterAcceleratedCompositingMode(layer);
    else
        m_page->exitAcceleratedCompositingMode();
}

void WebChromeClient::attachViewOverlayGraphicsLayer(Frame* frame, GraphicsLayer* graphicsLayer)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (auto drawingArea = m_page->drawingArea())
        drawingArea->attachViewOverlayGraphicsLayer(frame, graphicsLayer);
}

void WebChromeClient::setNeedsOneShotDrawingSynchronization()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    notImplemented();
}

void WebChromeClient::scheduleCompositingLayerFlush()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (m_page->drawingArea())
        m_page->drawingArea()->scheduleCompositingLayerFlush();
}

bool WebChromeClient::adjustLayerFlushThrottling(WebCore::LayerFlushThrottleState::Flags flags)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->drawingArea() && m_page->drawingArea()->adjustLayerFlushThrottling(flags);
}

bool WebChromeClient::layerTreeStateIsFrozen() const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (m_page->drawingArea())
        return m_page->drawingArea()->layerTreeStateIsFrozen();

    return false;
}

#if ENABLE(ASYNC_SCROLLING)
PassRefPtr<ScrollingCoordinator> WebChromeClient::createScrollingCoordinator(Page* page) const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(m_page->corePage() == page);
    if (m_page->drawingArea()->type() == DrawingAreaTypeRemoteLayerTree)
        return RemoteScrollingCoordinator::create(m_page);

    return 0;
}
#endif

#if (PLATFORM(IOS) && HAVE(AVKIT)) || (PLATFORM(MAC) && ENABLE(VIDEO_PRESENTATION_MODE))
bool WebChromeClient::supportsVideoFullscreen(WebCore::HTMLMediaElementEnums::VideoFullscreenMode mode)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->videoFullscreenManager().supportsVideoFullscreen(mode);
}

void WebChromeClient::setUpPlaybackControlsManager(WebCore::HTMLMediaElement& mediaElement)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->playbackSessionManager().setUpPlaybackControlsManager(mediaElement);
}

void WebChromeClient::clearPlaybackControlsManager()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->playbackSessionManager().clearPlaybackControlsManager();
}

void WebChromeClient::enterVideoFullscreenForVideoElement(WebCore::HTMLVideoElement& videoElement, WebCore::HTMLMediaElementEnums::VideoFullscreenMode mode)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(mode != HTMLMediaElementEnums::VideoFullscreenModeNone);
    m_page->videoFullscreenManager().enterVideoFullscreenForVideoElement(videoElement, mode);
}

void WebChromeClient::exitVideoFullscreenForVideoElement(WebCore::HTMLVideoElement& videoElement)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->videoFullscreenManager().exitVideoFullscreenForVideoElement(videoElement);
}

#if PLATFORM(MAC) && ENABLE(VIDEO_PRESENTATION_MODE)
void WebChromeClient::exitVideoFullscreenToModeWithoutAnimation(WebCore::HTMLVideoElement& videoElement, HTMLMediaElementEnums::VideoFullscreenMode targetMode)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->videoFullscreenManager().exitVideoFullscreenToModeWithoutAnimation(videoElement, targetMode);
}
#endif

#endif
    
#if ENABLE(FULLSCREEN_API)
bool WebChromeClient::supportsFullScreenForElement(const WebCore::Element*, bool withKeyboard)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->fullScreenManager()->supportsFullScreen(withKeyboard);
}

void WebChromeClient::enterFullScreenForElement(WebCore::Element* element)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->fullScreenManager()->enterFullScreenForElement(element);
}

void WebChromeClient::exitFullScreenForElement(WebCore::Element* element)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->fullScreenManager()->exitFullScreenForElement(element);
}
#endif

#if PLATFORM(IOS)
FloatSize WebChromeClient::screenSize() const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->screenSize();
}

FloatSize WebChromeClient::availableScreenSize() const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->availableScreenSize();
}
#endif

void WebChromeClient::dispatchViewportPropertiesDidChange(const ViewportArguments& viewportArguments) const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    UNUSED_PARAM(viewportArguments);
#if PLATFORM(IOS)
    m_page->viewportPropertiesDidChange(viewportArguments);
#endif
#if USE(COORDINATED_GRAPHICS)
    if (!m_page->useFixedLayout())
        return;

    m_page->sendViewportAttributesChanged();
#endif
}

void WebChromeClient::notifyScrollerThumbIsVisibleInRect(const IntRect& scrollerThumb)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->send(Messages::WebPageProxy::NotifyScrollerThumbIsVisibleInRect(scrollerThumb));
}

void WebChromeClient::recommendedScrollbarStyleDidChange(ScrollbarStyle newStyle)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->send(Messages::WebPageProxy::RecommendedScrollbarStyleDidChange(static_cast<int32_t>(newStyle)));
}

WTF::Optional<ScrollbarOverlayStyle> WebChromeClient::preferredScrollbarOverlayStyle()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->scrollbarOverlayStyle(); 
}

Color WebChromeClient::underlayColor() const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->underlayColor();
}

void WebChromeClient::pageExtendedBackgroundColorDidChange(Color backgroundColor) const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
#if PLATFORM(MAC) || PLATFORM(EFL)
    m_page->send(Messages::WebPageProxy::PageExtendedBackgroundColorDidChange(backgroundColor));
#else
    UNUSED_PARAM(backgroundColor);
#endif
}

void WebChromeClient::wheelEventHandlersChanged(bool hasHandlers)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->wheelEventHandlersChanged(hasHandlers);
}

String WebChromeClient::plugInStartLabelTitle(const String& mimeType) const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->injectedBundleUIClient().plugInStartLabelTitle(mimeType);
}

String WebChromeClient::plugInStartLabelSubtitle(const String& mimeType) const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->injectedBundleUIClient().plugInStartLabelSubtitle(mimeType);
}

String WebChromeClient::plugInExtraStyleSheet() const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->injectedBundleUIClient().plugInExtraStyleSheet();
}

String WebChromeClient::plugInExtraScript() const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->injectedBundleUIClient().plugInExtraScript();
}

void WebChromeClient::enableSuddenTermination()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->send(Messages::WebProcessProxy::EnableSuddenTermination());
}

void WebChromeClient::disableSuddenTermination()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->send(Messages::WebProcessProxy::DisableSuddenTermination());
}

void WebChromeClient::didAddHeaderLayer(GraphicsLayer* headerParent)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
#if ENABLE(RUBBER_BANDING)
    if (PageBanner* banner = m_page->headerPageBanner())
        banner->didAddParentLayer(headerParent);
#else
    UNUSED_PARAM(headerParent);
#endif
}

void WebChromeClient::didAddFooterLayer(GraphicsLayer* footerParent)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
#if ENABLE(RUBBER_BANDING)
    if (PageBanner* banner = m_page->footerPageBanner())
        banner->didAddParentLayer(footerParent);
#else
    UNUSED_PARAM(footerParent);
#endif
}

bool WebChromeClient::shouldUseTiledBackingForFrameView(const FrameView* frameView) const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->drawingArea()->shouldUseTiledBackingForFrameView(frameView);
}

void WebChromeClient::isPlayingMediaDidChange(WebCore::MediaProducer::MediaStateFlags state, uint64_t sourceElementID)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->send(Messages::WebPageProxy::IsPlayingMediaDidChange(state, sourceElementID));
}

#if ENABLE(MEDIA_SESSION)
void WebChromeClient::hasMediaSessionWithActiveMediaElementsDidChange(bool state)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->send(Messages::WebPageProxy::HasMediaSessionWithActiveMediaElementsDidChange(state));
}

void WebChromeClient::mediaSessionMetadataDidChange(const WebCore::MediaSessionMetadata& metadata)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->send(Messages::WebPageProxy::MediaSessionMetadataDidChange(metadata));
}

void WebChromeClient::focusedContentMediaElementDidChange(uint64_t elementID)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->send(Messages::WebPageProxy::FocusedContentMediaElementDidChange(elementID));
}
#endif

void WebChromeClient::setPageActivityState(PageActivityState::Flags activityState)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->setPageActivityState(activityState);
}

#if ENABLE(SUBTLE_CRYPTO)
bool WebChromeClient::wrapCryptoKey(const Vector<uint8_t>& key, Vector<uint8_t>& wrappedKey) const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    bool succeeded;
    if (!WebProcess::singleton().parentProcessConnection()->sendSync(Messages::WebPageProxy::WrapCryptoKey(key), Messages::WebPageProxy::WrapCryptoKey::Reply(succeeded, wrappedKey), m_page->pageID()))
        return false;
    return succeeded;
}

bool WebChromeClient::unwrapCryptoKey(const Vector<uint8_t>& wrappedKey, Vector<uint8_t>& key) const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    bool succeeded;
    if (!WebProcess::singleton().parentProcessConnection()->sendSync(Messages::WebPageProxy::UnwrapCryptoKey(wrappedKey), Messages::WebPageProxy::UnwrapCryptoKey::Reply(succeeded, key), m_page->pageID()))
        return false;
    return succeeded;
}
#endif

#if ENABLE(TELEPHONE_NUMBER_DETECTION) && PLATFORM(MAC)
void WebChromeClient::handleTelephoneNumberClick(const String& number, const WebCore::IntPoint& point)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->handleTelephoneNumberClick(number, point);
}
#endif

#if ENABLE(SERVICE_CONTROLS)
void WebChromeClient::handleSelectionServiceClick(WebCore::FrameSelection& selection, const Vector<String>& telephoneNumbers, const WebCore::IntPoint& point)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->handleSelectionServiceClick(selection, telephoneNumbers, point);
}

bool WebChromeClient::hasRelevantSelectionServices(bool isTextOnly) const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return (isTextOnly && WebProcess::singleton().hasSelectionServices()) || WebProcess::singleton().hasRichContentServices();
}
#endif

bool WebChromeClient::shouldDispatchFakeMouseMoveEvents() const
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->shouldDispatchFakeMouseMoveEvents();
}

void WebChromeClient::handleAutoFillButtonClick(HTMLInputElement& inputElement)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    RefPtr<API::Object> userData;

    // Notify the bundle client.
    auto nodeHandle = InjectedBundleNodeHandle::getOrCreate(inputElement);
    m_page->injectedBundleUIClient().didClickAutoFillButton(*m_page, nodeHandle.get(), userData);

    // Notify the UIProcess.
    m_page->send(Messages::WebPageProxy::HandleAutoFillButtonClick(UserData(WebProcess::singleton().transformObjectsToHandles(userData.get()).get())));
}

#if ENABLE(WIRELESS_PLAYBACK_TARGET) && !PLATFORM(IOS)
void WebChromeClient::addPlaybackTargetPickerClient(uint64_t contextId)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->send(Messages::WebPageProxy::AddPlaybackTargetPickerClient(contextId));
}

void WebChromeClient::removePlaybackTargetPickerClient(uint64_t contextId)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->send(Messages::WebPageProxy::RemovePlaybackTargetPickerClient(contextId));
}


void WebChromeClient::showPlaybackTargetPicker(uint64_t contextId, const WebCore::IntPoint& position, bool isVideo)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    FrameView* frameView = m_page->mainFrame()->view();
    FloatRect rect(frameView->contentsToRootView(frameView->windowToContents(position)), FloatSize());
    m_page->send(Messages::WebPageProxy::ShowPlaybackTargetPicker(contextId, rect, isVideo));
}

void WebChromeClient::playbackTargetPickerClientStateDidChange(uint64_t contextId, WebCore::MediaProducer::MediaStateFlags state)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->send(Messages::WebPageProxy::PlaybackTargetPickerClientStateDidChange(contextId, state));
}

void WebChromeClient::setMockMediaPlaybackTargetPickerEnabled(bool enabled)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->send(Messages::WebPageProxy::SetMockMediaPlaybackTargetPickerEnabled(enabled));
}

void WebChromeClient::setMockMediaPlaybackTargetPickerState(const String& name, WebCore::MediaPlaybackTargetContext::State state)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->send(Messages::WebPageProxy::SetMockMediaPlaybackTargetPickerState(name, state));
}
#endif

void WebChromeClient::imageOrMediaDocumentSizeChanged(const WebCore::IntSize& newSize)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->imageOrMediaDocumentSizeChanged(newSize);
}

#if ENABLE(VIDEO)
#if USE(GSTREAMER)
void WebChromeClient::requestInstallMissingMediaPlugins(const String& details, const String& description, WebCore::MediaPlayerRequestInstallMissingPluginsCallback& callback)
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->requestInstallMissingMediaPlugins(details, description, callback);
}
#endif
#endif // ENABLE(VIDEO)

void WebChromeClient::didInvalidateDocumentMarkerRects()
{       AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_page->findController().didInvalidateDocumentMarkerRects();
}

} // namespace WebKit
