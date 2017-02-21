/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Peter Kelly (pmk@post.com)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 *           (C) 2007 David Smith (catfish.man@gmail.com)
 * Copyright (C) 2004-2016 Apple Inc. All rights reserved.
 *           (C) 2007 Eric Seidel (eric@webkit.org)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "Element.h"

#include "AXObjectCache.h"
#include "Attr.h"
#include "AttributeChangeInvalidation.h"
#include "CSSParser.h"
#include "Chrome.h"
#include "ChromeClient.h"
#include "ClassChangeInvalidation.h"
#include "ClientRect.h"
#include "ClientRectList.h"
#include "ComposedTreeAncestorIterator.h"
#include "ContainerNodeAlgorithms.h"
#include "CustomElementReactionQueue.h"
#include "CustomElementRegistry.h"
#include "DOMTokenList.h"
#include "Dictionary.h"
#include "DocumentAnimation.h"
#include "DocumentSharedObjectPool.h"
#include "ElementIterator.h"
#include "ElementRareData.h"
#include "EventDispatcher.h"
#include "EventHandler.h"
#include "EventNames.h"
#include "FlowThreadController.h"
#include "FocusController.h"
#include "FocusEvent.h"
#include "FrameSelection.h"
#include "FrameView.h"
#include "HTMLCanvasElement.h"
#include "HTMLCollection.h"
#include "HTMLDocument.h"
#include "HTMLEmbedElement.h"
#include "HTMLHtmlElement.h"
#include "HTMLIFrameElement.h"
#include "HTMLLabelElement.h"
#include "HTMLNameCollection.h"
#include "HTMLObjectElement.h"
#include "HTMLParserIdioms.h"
#include "HTMLSelectElement.h"
#include "HTMLTemplateElement.h"
#include "IdChangeInvalidation.h"
#include "IdTargetObserverRegistry.h"
#include "JSLazyEventListener.h"
#include "KeyboardEvent.h"
#include "KeyframeEffect.h"
#include "MainFrame.h"
#include "MutationObserverInterestGroup.h"
#include "MutationRecord.h"
#include "NoEventDispatchAssertion.h"
#include "NodeRenderStyle.h"
#include "PlatformWheelEvent.h"
#include "PointerLockController.h"
#include "RenderFlowThread.h"
#include "RenderLayer.h"
#include "RenderNamedFlowFragment.h"
#include "RenderRegion.h"
#include "RenderTheme.h"
#include "RenderTreeUpdater.h"
#include "RenderView.h"
#include "RenderWidget.h"
#include "SVGDocumentExtensions.h"
#include "SVGElement.h"
#include "SVGNames.h"
#include "SVGSVGElement.h"
#include "ScrollLatchingState.h"
#include "SelectorQuery.h"
#include "Settings.h"
#include "SimulatedClick.h"
#include "SlotAssignment.h"
#include "StyleProperties.h"
#include "StyleResolver.h"
#include "StyleTreeResolver.h"
#include "TextIterator.h"
#include "VoidCallback.h"
#include "WheelEvent.h"
#include "XLinkNames.h"
#include "XMLNSNames.h"
#include "XMLNames.h"
#include "htmlediting.h"
#include "markup.h"
#include <wtf/CurrentTime.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/text/CString.h>

#include <wtf/macros.h>

namespace WebCore {

using namespace HTMLNames;
using namespace XMLNames;

static HashMap<Element*, Vector<RefPtr<Attr>>>& attrNodeListMap()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    static NeverDestroyed<HashMap<Element*, Vector<RefPtr<Attr>>>> map;
    return map;
}

static Vector<RefPtr<Attr>>* attrNodeListForElement(Element& element)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!element.hasSyntheticAttrChildNodes())
        return nullptr;
    ASSERT(attrNodeListMap().contains(&element));
    return &attrNodeListMap().find(&element)->value;
}

static Vector<RefPtr<Attr>>& ensureAttrNodeListForElement(Element& element)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (element.hasSyntheticAttrChildNodes()) {
        ASSERT(attrNodeListMap().contains(&element));
        return attrNodeListMap().find(&element)->value;
    }
    ASSERT(!attrNodeListMap().contains(&element));
    element.setHasSyntheticAttrChildNodes(true);
    return attrNodeListMap().add(&element, Vector<RefPtr<Attr>>()).iterator->value;
}

static void removeAttrNodeListForElement(Element& element)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(element.hasSyntheticAttrChildNodes());
    ASSERT(attrNodeListMap().contains(&element));
    attrNodeListMap().remove(&element);
    element.setHasSyntheticAttrChildNodes(false);
}

static Attr* findAttrNodeInList(Vector<RefPtr<Attr>>& attrNodeList, const QualifiedName& name)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    for (auto& node : attrNodeList) {
        if (node->qualifiedName().matches(name))
            return node.get();
    }
    return nullptr;
}

static Attr* findAttrNodeInList(Vector<RefPtr<Attr>>& attrNodeList, const AtomicString& localName, bool shouldIgnoreAttributeCase)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    const AtomicString& caseAdjustedName = shouldIgnoreAttributeCase ? localName.convertToASCIILowercase() : localName;
    for (auto& node : attrNodeList) {
        if (node->qualifiedName().localName() == caseAdjustedName)
            return node.get();
    }
    return nullptr;
}

Ref<Element> Element::create(const QualifiedName& tagName, Document& document)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return adoptRef(*new Element(tagName, document, CreateElement));
}

Element::Element(const QualifiedName& tagName, Document& document, ConstructionType type)
    : ContainerNode(document, type)
    , m_tagName(tagName)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
}

Element::~Element()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
#ifndef NDEBUG
    if (document().hasLivingRenderTree()) {
        // When the document is not destroyed, an element that was part of a named flow
        // content nodes should have been removed from the content nodes collection
        // and the isNamedFlowContentElement flag reset.
        ASSERT_WITH_SECURITY_IMPLICATION(!isNamedFlowContentElement());
    }
#endif

    ASSERT(!beforePseudoElement());
    ASSERT(!afterPseudoElement());

    removeShadowRoot();

    if (hasSyntheticAttrChildNodes())
        detachAllAttrNodesFromElement();

    if (hasPendingResources()) {
        document().accessSVGExtensions().removeElementFromPendingResources(this);
        ASSERT(!hasPendingResources());
    }
}

inline ElementRareData* Element::elementRareData() const
{//     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT_WITH_SECURITY_IMPLICATION(hasRareData());
    return static_cast<ElementRareData*>(rareData());
}

inline ElementRareData& Element::ensureElementRareData()
{//     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return static_cast<ElementRareData&>(ensureRareData());
}

void Element::clearTabIndexExplicitlyIfNeeded()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (hasRareData())
        elementRareData()->clearTabIndexExplicitly();
}

void Element::setTabIndexExplicitly(int tabIndex)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ensureElementRareData().setTabIndexExplicitly(tabIndex);
}

bool Element::tabIndexSetExplicitly() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return hasRareData() && elementRareData()->tabIndexSetExplicitly();
}

bool Element::supportsFocus() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return tabIndexSetExplicitly();
}

Element* Element::focusDelegate()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return this;
}

int Element::tabIndex() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return hasRareData() ? elementRareData()->tabIndex() : 0;
}

void Element::setTabIndex(int value)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    setIntegralAttribute(tabindexAttr, value);
}

bool Element::isKeyboardFocusable(KeyboardEvent*) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return isFocusable() && tabIndex() >= 0;
}

bool Element::isMouseFocusable() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return isFocusable();
}

bool Element::shouldUseInputMethod()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return computeEditability(UserSelectAllIsAlwaysNonEditable, ShouldUpdateStyle::Update) != Editability::ReadOnly;
}

static bool isForceEvent(const PlatformMouseEvent& platformEvent)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return platformEvent.type() == PlatformEvent::MouseForceChanged || platformEvent.type() == PlatformEvent::MouseForceDown || platformEvent.type() == PlatformEvent::MouseForceUp;
}

bool Element::dispatchMouseEvent(const PlatformMouseEvent& platformEvent, const AtomicString& eventType, int detail, Element* relatedTarget)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (isDisabledFormControl())
        return false;

    if (isForceEvent(platformEvent) && !document().hasListenerTypeForEventType(platformEvent.type()))
        return false;

    Ref<MouseEvent> mouseEvent = MouseEvent::create(eventType, document().defaultView(), platformEvent, detail, relatedTarget);

    if (mouseEvent->type().isEmpty())
        return true; // Shouldn't happen.

    ASSERT(!mouseEvent->target() || mouseEvent->target() != relatedTarget);
    bool didNotSwallowEvent = dispatchEvent(mouseEvent) && !mouseEvent->defaultHandled();

    if (mouseEvent->type() == eventNames().clickEvent && mouseEvent->detail() == 2) {
        // Special case: If it's a double click event, we also send the dblclick event. This is not part
        // of the DOM specs, but is used for compatibility with the ondblclick="" attribute. This is treated
        // as a separate event in other DOM-compliant browsers like Firefox, and so we do the same.
        Ref<MouseEvent> doubleClickEvent = MouseEvent::create(eventNames().dblclickEvent,
            mouseEvent->bubbles(), mouseEvent->cancelable(), mouseEvent->view(), mouseEvent->detail(),
            mouseEvent->screenX(), mouseEvent->screenY(), mouseEvent->clientX(), mouseEvent->clientY(),
            mouseEvent->ctrlKey(), mouseEvent->altKey(), mouseEvent->shiftKey(), mouseEvent->metaKey(),
            mouseEvent->button(), mouseEvent->syntheticClickType(), relatedTarget);

        if (mouseEvent->defaultHandled())
            doubleClickEvent->setDefaultHandled();

        dispatchEvent(doubleClickEvent);
        if (doubleClickEvent->defaultHandled() || doubleClickEvent->defaultPrevented())
            return false;
    }
    return didNotSwallowEvent;
}


bool Element::dispatchWheelEvent(const PlatformWheelEvent& event)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    Ref<WheelEvent> wheelEvent = WheelEvent::create(event, document().defaultView());

    // Events with no deltas are important because they convey platform information about scroll gestures
    // and momentum beginning or ending. However, those events should not be sent to the DOM since some
    // websites will break. They need to be dispatched because dispatching them will call into the default
    // event handler, and our platform code will correctly handle the phase changes. Calling stopPropogation()
    // will prevent the event from being sent to the DOM, but will still call the default event handler.
    if (!event.deltaX() && !event.deltaY())
        wheelEvent->stopPropagation();

    return EventDispatcher::dispatchEvent(this, wheelEvent) && !wheelEvent->defaultHandled();
}

bool Element::dispatchKeyEvent(const PlatformKeyboardEvent& platformEvent)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    Ref<KeyboardEvent> event = KeyboardEvent::create(platformEvent, document().defaultView());
    if (Frame* frame = document().frame()) {
        if (frame->eventHandler().accessibilityPreventsEventPropogation(event))
            event->stopPropagation();
    }
    return EventDispatcher::dispatchEvent(this, event) && !event->defaultHandled();
}

void Element::dispatchSimulatedClick(Event* underlyingEvent, SimulatedClickMouseEventOptions eventOptions, SimulatedClickVisualOptions visualOptions)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    simulateClick(*this, underlyingEvent, eventOptions, visualOptions, SimulatedClickCreationOptions::FromUserAgent);
}

void Element::dispatchSimulatedClickForBindings(Event* underlyingEvent)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    simulateClick(*this, underlyingEvent, SendNoEvents, DoNotShowPressedLook, SimulatedClickCreationOptions::FromBindings);
}

Ref<Node> Element::cloneNodeInternal(Document& targetDocument, CloningOperation type)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    switch (type) {
    case CloningOperation::OnlySelf:
    case CloningOperation::SelfWithTemplateContent:
        return cloneElementWithoutChildren(targetDocument);
    case CloningOperation::Everything:
        break;
    }
    return cloneElementWithChildren(targetDocument);
}

Ref<Element> Element::cloneElementWithChildren(Document& targetDocument)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    Ref<Element> clone = cloneElementWithoutChildren(targetDocument);
    cloneChildNodes(clone);
    return clone;
}

Ref<Element> Element::cloneElementWithoutChildren(Document& targetDocument)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    Ref<Element> clone = cloneElementWithoutAttributesAndChildren(targetDocument);
    // This will catch HTML elements in the wrong namespace that are not correctly copied.
    // This is a sanity check as HTML overloads some of the DOM methods.
    ASSERT(isHTMLElement() == clone->isHTMLElement());

    clone->cloneDataFromElement(*this);
    return clone;
}

Ref<Element> Element::cloneElementWithoutAttributesAndChildren(Document& targetDocument)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return targetDocument.createElement(tagQName(), false);
}

Ref<Attr> Element::detachAttribute(unsigned index)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(elementData());

    const Attribute& attribute = elementData()->attributeAt(index);

    RefPtr<Attr> attrNode = attrIfExists(attribute.name());
    if (attrNode)
        detachAttrNodeFromElementWithValue(attrNode.get(), attribute.value());
    else
        attrNode = Attr::create(document(), attribute.name(), attribute.value());

    removeAttributeInternal(index, NotInSynchronizationOfLazyAttribute);
    return attrNode.releaseNonNull();
}

bool Element::removeAttribute(const QualifiedName& name)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!elementData())
        return false;

    unsigned index = elementData()->findAttributeIndexByName(name);
    if (index == ElementData::attributeNotFound)
        return false;

    removeAttributeInternal(index, NotInSynchronizationOfLazyAttribute);
    return true;
}

void Element::setBooleanAttribute(const QualifiedName& name, bool value)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (value)
        setAttribute(name, emptyAtom);
    else
        removeAttribute(name);
}

NamedNodeMap& Element::attributes() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ElementRareData& rareData = const_cast<Element*>(this)->ensureElementRareData();
    if (NamedNodeMap* attributeMap = rareData.attributeMap())
        return *attributeMap;

    rareData.setAttributeMap(std::make_unique<NamedNodeMap>(const_cast<Element&>(*this)));
    return *rareData.attributeMap();
}

Node::NodeType Element::nodeType() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return ELEMENT_NODE;
}

bool Element::hasAttribute(const QualifiedName& name) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return hasAttributeNS(name.namespaceURI(), name.localName());
}

void Element::synchronizeAllAttributes() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!elementData())
        return;
    if (elementData()->styleAttributeIsDirty()) {
        ASSERT(isStyledElement());
        static_cast<const StyledElement*>(this)->synchronizeStyleAttributeInternal();
    }

    if (elementData()->animatedSVGAttributesAreDirty()) {
        ASSERT(isSVGElement());
        downcast<SVGElement>(*this).synchronizeAnimatedSVGAttribute(anyQName());
    }
}

ALWAYS_INLINE void Element::synchronizeAttribute(const QualifiedName& name) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!elementData())
        return;
    if (UNLIKELY(name == styleAttr && elementData()->styleAttributeIsDirty())) {
        ASSERT_WITH_SECURITY_IMPLICATION(isStyledElement());
        static_cast<const StyledElement*>(this)->synchronizeStyleAttributeInternal();
        return;
    }

    if (UNLIKELY(elementData()->animatedSVGAttributesAreDirty())) {
        ASSERT(isSVGElement());
        downcast<SVGElement>(*this).synchronizeAnimatedSVGAttribute(name);
    }
}

static ALWAYS_INLINE bool isStyleAttribute(const Element& element, const AtomicString& attributeLocalName)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (shouldIgnoreAttributeCase(element))
        return equalLettersIgnoringASCIICase(attributeLocalName, "style");
    return attributeLocalName == styleAttr.localName();
}

ALWAYS_INLINE void Element::synchronizeAttribute(const AtomicString& localName) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    // This version of synchronizeAttribute() is streamlined for the case where you don't have a full QualifiedName,
    // e.g when called from DOM API.
    if (!elementData())
        return;
    if (elementData()->styleAttributeIsDirty() && isStyleAttribute(*this, localName)) {
        ASSERT_WITH_SECURITY_IMPLICATION(isStyledElement());
        static_cast<const StyledElement*>(this)->synchronizeStyleAttributeInternal();
        return;
    }
    if (elementData()->animatedSVGAttributesAreDirty()) {
        // We're not passing a namespace argument on purpose. SVGNames::*Attr are defined w/o namespaces as well.
        ASSERT_WITH_SECURITY_IMPLICATION(isSVGElement());
        downcast<SVGElement>(*this).synchronizeAnimatedSVGAttribute(QualifiedName(nullAtom, localName, nullAtom));
    }
}

const AtomicString& Element::getAttribute(const QualifiedName& name) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!elementData())
        return nullAtom;
    synchronizeAttribute(name);
    if (const Attribute* attribute = findAttributeByName(name))
        return attribute->value();
    return nullAtom;
}

Vector<String> Element::getAttributeNames() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    Vector<String> attributesVector;
    if (!hasAttributes())
        return attributesVector;

    auto attributes = attributesIterator();
    attributesVector.reserveInitialCapacity(attributes.attributeCount());
    for (auto& attribute : attributes)
        attributesVector.uncheckedAppend(attribute.name().toString());
    return attributesVector;
}

bool Element::isFocusable() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!inDocument() || !supportsFocus())
        return false;

    if (!renderer()) {
        // If the node is in a display:none tree it might say it needs style recalc but
        // the whole document is actually up to date.
        ASSERT(!needsStyleRecalc() || !document().childNeedsStyleRecalc());

        // Elements in canvas fallback content are not rendered, but they are allowed to be
        // focusable as long as their canvas is displayed and visible.
        if (auto* canvas = ancestorsOfType<HTMLCanvasElement>(*this).first())
            return canvas->renderer() && canvas->renderer()->style().visibility() == VISIBLE;
    }

    // FIXME: Even if we are not visible, we might have a child that is visible.
    // Hyatt wants to fix that some day with a "has visible content" flag or the like.
    if (!renderer() || renderer()->style().visibility() != VISIBLE)
        return false;

    return true;
}

bool Element::isUserActionElementInActiveChain() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(isUserActionElement());
    return document().userActionElements().isInActiveChain(this);
}

bool Element::isUserActionElementActive() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(isUserActionElement());
    return document().userActionElements().isActive(this);
}

bool Element::isUserActionElementFocused() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(isUserActionElement());
    return document().userActionElements().isFocused(this);
}

bool Element::isUserActionElementHovered() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(isUserActionElement());
    return document().userActionElements().isHovered(this);
}

void Element::setActive(bool flag, bool pause)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (flag == active())
        return;

    document().userActionElements().setActive(this, flag);

    const RenderStyle* renderStyle = this->renderStyle();
    bool reactsToPress = (renderStyle && renderStyle->affectedByActive()) || styleAffectedByActive();
    if (reactsToPress)
        setNeedsStyleRecalc();

    if (!renderer())
        return;

    if (renderer()->style().hasAppearance() && renderer()->theme().stateChanged(*renderer(), ControlStates::PressedState))
        reactsToPress = true;

    // The rest of this function implements a feature that only works if the
    // platform supports immediate invalidations on the ChromeClient, so bail if
    // that isn't supported.
    if (!document().page()->chrome().client().supportsImmediateInvalidation())
        return;

    if (reactsToPress && pause) {
        // The delay here is subtle. It relies on an assumption, namely that the amount of time it takes
        // to repaint the "down" state of the control is about the same time as it would take to repaint the
        // "up" state. Once you assume this, you can just delay for 100ms - that time (assuming that after you
        // leave this method, it will be about that long before the flush of the up state happens again).
#ifdef HAVE_FUNC_USLEEP
        double startTime = monotonicallyIncreasingTime();
#endif

        document().updateStyleIfNeeded();

        // Do an immediate repaint.
        if (renderer())
            renderer()->repaint();

        // FIXME: Come up with a less ridiculous way of doing this.
#ifdef HAVE_FUNC_USLEEP
        // Now pause for a small amount of time (1/10th of a second from before we repainted in the pressed state)
        double remainingTime = 0.1 - (monotonicallyIncreasingTime() - startTime);
        if (remainingTime > 0)
            usleep(static_cast<useconds_t>(remainingTime * 1000000.0));
#endif
    }
}

void Element::setFocus(bool flag)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (flag == focused())
        return;

    document().userActionElements().setFocused(this, flag);
    setNeedsStyleRecalc();

    for (Element* element = this; element; element = element->parentOrShadowHostElement())
        element->setHasFocusWithin(flag);
}

void Element::setHovered(bool flag)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (flag == hovered())
        return;

    document().userActionElements().setHovered(this, flag);

    if (!renderer()) {
        // When setting hover to false, the style needs to be recalc'd even when
        // there's no renderer (imagine setting display:none in the :hover class,
        // if a nil renderer would prevent this element from recalculating its
        // style, it would never go back to its normal style and remain
        // stuck in its hovered style).
        if (!flag)
            setNeedsStyleRecalc();

        return;
    }

    if (renderer()->style().affectedByHover() || childrenAffectedByHover())
        setNeedsStyleRecalc();

    if (renderer()->style().hasAppearance())
        renderer()->theme().stateChanged(*renderer(), ControlStates::HoverState);
}

void Element::scrollIntoView(bool alignToTop) 
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().updateLayoutIgnorePendingStylesheets();

    if (!renderer())
        return;

    LayoutRect bounds = renderer()->anchorRect();
    // Align to the top / bottom and to the closest edge.
    if (alignToTop)
        renderer()->scrollRectToVisible(SelectionRevealMode::Reveal, bounds, ScrollAlignment::alignToEdgeIfNeeded, ScrollAlignment::alignTopAlways);
    else
        renderer()->scrollRectToVisible(SelectionRevealMode::Reveal, bounds, ScrollAlignment::alignToEdgeIfNeeded, ScrollAlignment::alignBottomAlways);
}

void Element::scrollIntoViewIfNeeded(bool centerIfNeeded)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().updateLayoutIgnorePendingStylesheets();

    if (!renderer())
        return;

    LayoutRect bounds = renderer()->anchorRect();
    if (centerIfNeeded)
        renderer()->scrollRectToVisible(SelectionRevealMode::Reveal, bounds, ScrollAlignment::alignCenterIfNeeded, ScrollAlignment::alignCenterIfNeeded);
    else
        renderer()->scrollRectToVisible(SelectionRevealMode::Reveal, bounds, ScrollAlignment::alignToEdgeIfNeeded, ScrollAlignment::alignToEdgeIfNeeded);
}

void Element::scrollIntoViewIfNotVisible(bool centerIfNotVisible)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().updateLayoutIgnorePendingStylesheets();
    
    if (!renderer())
        return;
    
    LayoutRect bounds = renderer()->anchorRect();
    if (centerIfNotVisible)
        renderer()->scrollRectToVisible(SelectionRevealMode::Reveal, bounds, ScrollAlignment::alignCenterIfNotVisible, ScrollAlignment::alignCenterIfNotVisible);
    else
        renderer()->scrollRectToVisible(SelectionRevealMode::Reveal, bounds, ScrollAlignment::alignToEdgeIfNotVisible, ScrollAlignment::alignToEdgeIfNotVisible);
}
    
void Element::scrollByUnits(int units, ScrollGranularity granularity)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().updateLayoutIgnorePendingStylesheets();

    auto* renderer = this->renderer();
    if (!renderer)
        return;

    if (!renderer->hasOverflowClip())
        return;

    ScrollDirection direction = ScrollDown;
    if (units < 0) {
        direction = ScrollUp;
        units = -units;
    }
    Element* stopElement = this;
    downcast<RenderBox>(*renderer).scroll(direction, granularity, units, &stopElement);
}

void Element::scrollByLines(int lines)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    scrollByUnits(lines, ScrollByLine);
}

void Element::scrollByPages(int pages)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    scrollByUnits(pages, ScrollByPage);
}

static double localZoomForRenderer(const RenderElement& renderer)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    // FIXME: This does the wrong thing if two opposing zooms are in effect and canceled each
    // other out, but the alternative is that we'd have to crawl up the whole render tree every
    // time (or store an additional bit in the RenderStyle to indicate that a zoom was specified).
    double zoomFactor = 1;
    if (renderer.style().effectiveZoom() != 1) {
        // Need to find the nearest enclosing RenderElement that set up
        // a differing zoom, and then we divide our result by it to eliminate the zoom.
        const RenderElement* prev = &renderer;
        for (RenderElement* curr = prev->parent(); curr; curr = curr->parent()) {
            if (curr->style().effectiveZoom() != prev->style().effectiveZoom()) {
                zoomFactor = prev->style().zoom();
                break;
            }
            prev = curr;
        }
        if (prev->isRenderView())
            zoomFactor = prev->style().zoom();
    }
    return zoomFactor;
}

static double adjustForLocalZoom(LayoutUnit value, const RenderElement& renderer, double& zoomFactor)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    zoomFactor = localZoomForRenderer(renderer);
    if (zoomFactor == 1)
        return value.toDouble();
    return value.toDouble() / zoomFactor;
}

enum LegacyCSSOMElementMetricsRoundingStrategy { Round, Floor };

static bool subpixelMetricsEnabled(const Document& document)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return document.settings() && document.settings()->subpixelCSSOMElementMetricsEnabled();
}

static double convertToNonSubpixelValueIfNeeded(double value, const Document& document, LegacyCSSOMElementMetricsRoundingStrategy roundStrategy = Round)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return subpixelMetricsEnabled(document) ? value : roundStrategy == Round ? round(value) : floor(value);
}

double Element::offsetLeft()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().updateLayoutIgnorePendingStylesheets();
    if (RenderBoxModelObject* renderer = renderBoxModelObject()) {
        LayoutUnit offsetLeft = subpixelMetricsEnabled(renderer->document()) ? renderer->offsetLeft() : LayoutUnit(roundToInt(renderer->offsetLeft()));
        double zoomFactor = 1;
        double offsetLeftAdjustedWithZoom = adjustForLocalZoom(offsetLeft, *renderer, zoomFactor);
        return convertToNonSubpixelValueIfNeeded(offsetLeftAdjustedWithZoom, renderer->document(), zoomFactor == 1 ? Floor : Round);
    }
    return 0;
}

double Element::offsetTop()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().updateLayoutIgnorePendingStylesheets();
    if (RenderBoxModelObject* renderer = renderBoxModelObject()) {
        LayoutUnit offsetTop = subpixelMetricsEnabled(renderer->document()) ? renderer->offsetTop() : LayoutUnit(roundToInt(renderer->offsetTop()));
        double zoomFactor = 1;
        double offsetTopAdjustedWithZoom = adjustForLocalZoom(offsetTop, *renderer, zoomFactor);
        return convertToNonSubpixelValueIfNeeded(offsetTopAdjustedWithZoom, renderer->document(), zoomFactor == 1 ? Floor : Round);
    }
    return 0;
}

double Element::offsetWidth()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().updateLayoutIfDimensionsOutOfDate(*this, WidthDimensionsCheck);
    if (RenderBoxModelObject* renderer = renderBoxModelObject()) {
        LayoutUnit offsetWidth = subpixelMetricsEnabled(renderer->document()) ? renderer->offsetWidth() : LayoutUnit(roundToInt(renderer->offsetWidth()));
        return convertToNonSubpixelValueIfNeeded(adjustLayoutUnitForAbsoluteZoom(offsetWidth, *renderer).toDouble(), renderer->document());
    }
    return 0;
}

double Element::offsetHeight()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().updateLayoutIfDimensionsOutOfDate(*this, HeightDimensionsCheck);
    if (RenderBoxModelObject* renderer = renderBoxModelObject()) {
        LayoutUnit offsetHeight = subpixelMetricsEnabled(renderer->document()) ? renderer->offsetHeight() : LayoutUnit(roundToInt(renderer->offsetHeight()));
        return convertToNonSubpixelValueIfNeeded(adjustLayoutUnitForAbsoluteZoom(offsetHeight, *renderer).toDouble(), renderer->document());
    }
    return 0;
}

Element* Element::bindingsOffsetParent()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    Element* element = offsetParent();
    if (!element || !element->isInShadowTree())
        return element;
    return element->containingShadowRoot()->mode() == ShadowRoot::Mode::UserAgent ? nullptr : element;
}

Element* Element::offsetParent()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().updateLayoutIgnorePendingStylesheets();
    auto renderer = this->renderer();
    if (!renderer)
        return nullptr;
    auto offsetParent = renderer->offsetParent();
    if (!offsetParent)
        return nullptr;
    return offsetParent->element();
}

double Element::clientLeft()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().updateLayoutIgnorePendingStylesheets();

    if (RenderBox* renderer = renderBox()) {
        LayoutUnit clientLeft = subpixelMetricsEnabled(renderer->document()) ? renderer->clientLeft() : LayoutUnit(roundToInt(renderer->clientLeft()));
        return convertToNonSubpixelValueIfNeeded(adjustLayoutUnitForAbsoluteZoom(clientLeft, *renderer).toDouble(), renderer->document());
    }
    return 0;
}

double Element::clientTop()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().updateLayoutIgnorePendingStylesheets();

    if (RenderBox* renderer = renderBox()) {
        LayoutUnit clientTop = subpixelMetricsEnabled(renderer->document()) ? renderer->clientTop() : LayoutUnit(roundToInt(renderer->clientTop()));
        return convertToNonSubpixelValueIfNeeded(adjustLayoutUnitForAbsoluteZoom(clientTop, *renderer).toDouble(), renderer->document());
    }
    return 0;
}

double Element::clientWidth()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().updateLayoutIfDimensionsOutOfDate(*this, WidthDimensionsCheck);

    if (!document().hasLivingRenderTree())
        return 0;
    RenderView& renderView = *document().renderView();

    // When in strict mode, clientWidth for the document element should return the width of the containing frame.
    // When in quirks mode, clientWidth for the body element should return the width of the containing frame.
    bool inQuirksMode = document().inQuirksMode();
    if ((!inQuirksMode && document().documentElement() == this) || (inQuirksMode && isHTMLElement() && document().bodyOrFrameset() == this))
        return adjustForAbsoluteZoom(renderView.frameView().layoutWidth(), renderView);
    
    if (RenderBox* renderer = renderBox()) {
        LayoutUnit clientWidth = subpixelMetricsEnabled(renderer->document()) ? renderer->clientWidth() : LayoutUnit(roundToInt(renderer->clientWidth()));
        return convertToNonSubpixelValueIfNeeded(adjustLayoutUnitForAbsoluteZoom(clientWidth, *renderer).toDouble(), renderer->document());
    }
    return 0;
}

double Element::clientHeight()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().updateLayoutIfDimensionsOutOfDate(*this, HeightDimensionsCheck);
    if (!document().hasLivingRenderTree())
        return 0;
    RenderView& renderView = *document().renderView();

    // When in strict mode, clientHeight for the document element should return the height of the containing frame.
    // When in quirks mode, clientHeight for the body element should return the height of the containing frame.
    bool inQuirksMode = document().inQuirksMode();
    if ((!inQuirksMode && document().documentElement() == this) || (inQuirksMode && isHTMLElement() && document().bodyOrFrameset() == this))
        return adjustForAbsoluteZoom(renderView.frameView().layoutHeight(), renderView);

    if (RenderBox* renderer = renderBox()) {
        LayoutUnit clientHeight = subpixelMetricsEnabled(renderer->document()) ? renderer->clientHeight() : LayoutUnit(roundToInt(renderer->clientHeight()));
        return convertToNonSubpixelValueIfNeeded(adjustLayoutUnitForAbsoluteZoom(clientHeight, *renderer).toDouble(), renderer->document());
    }
    return 0;
}

int Element::scrollLeft()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().updateLayoutIgnorePendingStylesheets();

    if (RenderBox* rend = renderBox())
        return adjustForAbsoluteZoom(rend->scrollLeft(), *rend);
    return 0;
}

int Element::scrollTop()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().updateLayoutIgnorePendingStylesheets();

    if (RenderBox* rend = renderBox())
        return adjustForAbsoluteZoom(rend->scrollTop(), *rend);
    return 0;
}

void Element::setScrollLeft(int newLeft)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().updateLayoutIgnorePendingStylesheets();

    if (RenderBox* renderer = renderBox()) {
        renderer->setScrollLeft(static_cast<int>(newLeft * renderer->style().effectiveZoom()));
        if (auto* scrollableArea = renderer->layer())
            scrollableArea->setScrolledProgrammatically(true);
    }
}

void Element::setScrollTop(int newTop)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().updateLayoutIgnorePendingStylesheets();

    if (RenderBox* renderer = renderBox()) {
        renderer->setScrollTop(static_cast<int>(newTop * renderer->style().effectiveZoom()));
        if (auto* scrollableArea = renderer->layer())
            scrollableArea->setScrolledProgrammatically(true);
    }
}

int Element::scrollWidth()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().updateLayoutIfDimensionsOutOfDate(*this, WidthDimensionsCheck);
    if (RenderBox* rend = renderBox())
        return adjustForAbsoluteZoom(rend->scrollWidth(), *rend);
    return 0;
}

int Element::scrollHeight()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().updateLayoutIfDimensionsOutOfDate(*this, HeightDimensionsCheck);
    if (RenderBox* rend = renderBox())
        return adjustForAbsoluteZoom(rend->scrollHeight(), *rend);
    return 0;
}

IntRect Element::boundsInRootViewSpace()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().updateLayoutIgnorePendingStylesheets();

    FrameView* view = document().view();
    if (!view)
        return IntRect();

    Vector<FloatQuad> quads;

    if (isSVGElement() && renderer()) {
        // Get the bounding rectangle from the SVG model.
        SVGElement& svgElement = downcast<SVGElement>(*this);
        FloatRect localRect;
        if (svgElement.getBoundingBox(localRect))
            quads.append(renderer()->localToAbsoluteQuad(localRect));
    } else {
        // Get the bounding rectangle from the box model.
        if (renderBoxModelObject())
            renderBoxModelObject()->absoluteQuads(quads);
    }

    if (quads.isEmpty())
        return IntRect();

    IntRect result = quads[0].enclosingBoundingBox();
    for (size_t i = 1; i < quads.size(); ++i)
        result.unite(quads[i].enclosingBoundingBox());

    result = view->contentsToRootView(result);
    return result;
}

static bool layoutOverflowRectContainsAllDescendants(const RenderBox& renderBox)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (renderBox.isRenderView())
        return true;

    if (!renderBox.element())
        return false;

    // If there are any position:fixed inside of us, game over.
    if (auto* viewPositionedObjects = renderBox.view().positionedObjects()) {
        for (auto* positionedBox : *viewPositionedObjects) {
            if (positionedBox == &renderBox)
                continue;
            if (positionedBox->style().position() == FixedPosition && renderBox.element()->contains(positionedBox->element()))
                return false;
        }
    }

    if (renderBox.canContainAbsolutelyPositionedObjects()) {
        // Our layout overflow will include all descendant positioned elements.
        return true;
    }

    // This renderer may have positioned descendants whose containing block is some ancestor.
    if (auto* containingBlock = renderBox.containingBlockForAbsolutePosition()) {
        if (auto* positionedObjects = containingBlock->positionedObjects()) {
            for (auto* positionedBox : *positionedObjects) {
                if (positionedBox == &renderBox)
                    continue;
                if (renderBox.element()->contains(positionedBox->element()))
                    return false;
            }
        }
    }
    return false;
}

LayoutRect Element::absoluteEventBounds(bool& boundsIncludeAllDescendantElements, bool& includesFixedPositionElements)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    boundsIncludeAllDescendantElements = false;
    includesFixedPositionElements = false;

    if (!renderer())
        return LayoutRect();

    LayoutRect result;
    if (isSVGElement()) {
        // Get the bounding rectangle from the SVG model.
        SVGElement& svgElement = downcast<SVGElement>(*this);
        FloatRect localRect;
        if (svgElement.getBoundingBox(localRect, SVGLocatable::DisallowStyleUpdate))
            result = LayoutRect(renderer()->localToAbsoluteQuad(localRect, UseTransforms, &includesFixedPositionElements).boundingBox());
    } else {
        auto* renderer = this->renderer();
        if (is<RenderBox>(renderer)) {
            auto& box = downcast<RenderBox>(*renderer);

            bool computedBounds = false;
            
            if (RenderFlowThread* flowThread = box.flowThreadContainingBlock()) {
                bool wasFixed = false;
                Vector<FloatQuad> quads;
                FloatRect localRect(0, 0, box.width(), box.height());
                if (flowThread->absoluteQuadsForBox(quads, &wasFixed, &box, localRect.y(), localRect.maxY())) {
                    FloatRect quadBounds = quads[0].boundingBox();
                    for (size_t i = 1; i < quads.size(); ++i)
                        quadBounds.unite(quads[i].boundingBox());
                    
                    result = LayoutRect(quadBounds);
                    computedBounds = true;
                } else {
                    // Probably columns. Just return the bounds of the multicol block for now.
                    // FIXME: this doesn't handle nested columns.
                    RenderElement* multicolContainer = flowThread->parent();
                    if (multicolContainer && is<RenderBox>(multicolContainer)) {
                        auto overflowRect = downcast<RenderBox>(*multicolContainer).layoutOverflowRect();
                        result = LayoutRect(multicolContainer->localToAbsoluteQuad(FloatRect(overflowRect), UseTransforms, &includesFixedPositionElements).boundingBox());
                        computedBounds = true;
                    }
                }
            }

            if (!computedBounds) {
                LayoutRect overflowRect = box.layoutOverflowRect();
                result = LayoutRect(box.localToAbsoluteQuad(FloatRect(overflowRect), UseTransforms, &includesFixedPositionElements).boundingBox());
                boundsIncludeAllDescendantElements = layoutOverflowRectContainsAllDescendants(box);
            }
        } else
            result = LayoutRect(renderer->absoluteBoundingBoxRect(true /* useTransforms */, &includesFixedPositionElements));
    }

    return result;
}

LayoutRect Element::absoluteEventBoundsOfElementAndDescendants(bool& includesFixedPositionElements)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    bool boundsIncludeDescendants;
    LayoutRect result = absoluteEventBounds(boundsIncludeDescendants, includesFixedPositionElements);
    if (boundsIncludeDescendants)
        return result;

    for (auto& child : childrenOfType<Element>(*this)) {
        bool includesFixedPosition = false;
        LayoutRect childBounds = child.absoluteEventBoundsOfElementAndDescendants(includesFixedPosition);
        includesFixedPositionElements |= includesFixedPosition;
        result.unite(childBounds);
    }

    return result;
}

LayoutRect Element::absoluteEventHandlerBounds(bool& includesFixedPositionElements)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    // This is not web-exposed, so don't call the FOUC-inducing updateLayoutIgnorePendingStylesheets().
    FrameView* frameView = document().view();
    if (!frameView)
        return LayoutRect();

    if (frameView->needsLayout())
        frameView->layout();

    return absoluteEventBoundsOfElementAndDescendants(includesFixedPositionElements);
}

Ref<ClientRectList> Element::getClientRects()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().updateLayoutIgnorePendingStylesheets();

    RenderBoxModelObject* renderBoxModelObject = this->renderBoxModelObject();
    if (!renderBoxModelObject)
        return ClientRectList::create();

    // FIXME: Handle SVG elements.
    // FIXME: Handle table/inline-table with a caption.

    Vector<FloatQuad> quads;
    renderBoxModelObject->absoluteQuads(quads);
    document().adjustFloatQuadsForScrollAndAbsoluteZoomAndFrameScale(quads, renderBoxModelObject->style());
    return ClientRectList::create(quads);
}

Ref<ClientRect> Element::getBoundingClientRect()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().updateLayoutIgnorePendingStylesheets();

    Vector<FloatQuad> quads;
    if (isSVGElement() && renderer() && !renderer()->isSVGRoot()) {
        // Get the bounding rectangle from the SVG model.
        SVGElement& svgElement = downcast<SVGElement>(*this);
        FloatRect localRect;
        if (svgElement.getBoundingBox(localRect))
            quads.append(renderer()->localToAbsoluteQuad(localRect));
    } else {
        // Get the bounding rectangle from the box model.
        if (renderBoxModelObject())
            renderBoxModelObject()->absoluteQuads(quads);
    }

    if (quads.isEmpty())
        return ClientRect::create();

    FloatRect result = quads[0].boundingBox();
    for (size_t i = 1; i < quads.size(); ++i)
        result.unite(quads[i].boundingBox());

    document().adjustFloatRectForScrollAndAbsoluteZoomAndFrameScale(result, renderer()->style());
    return ClientRect::create(result);
}

IntRect Element::clientRect() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (RenderObject* renderer = this->renderer())
        return document().view()->contentsToRootView(renderer->absoluteBoundingBoxRect());
    return IntRect();
}
    
IntRect Element::screenRect() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (RenderObject* renderer = this->renderer())
        return document().view()->contentsToScreen(renderer->absoluteBoundingBoxRect());
    return IntRect();
}

const AtomicString& Element::getAttribute(const AtomicString& localName) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!elementData())
        return nullAtom;
    synchronizeAttribute(localName);
    if (const Attribute* attribute = elementData()->findAttributeByName(localName, shouldIgnoreAttributeCase(*this)))
        return attribute->value();
    return nullAtom;
}

const AtomicString& Element::getAttributeNS(const AtomicString& namespaceURI, const AtomicString& localName) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return getAttribute(QualifiedName(nullAtom, localName, namespaceURI));
}

void Element::setAttribute(const AtomicString& localName, const AtomicString& value, ExceptionCode& ec)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!Document::isValidName(localName)) {
        ec = INVALID_CHARACTER_ERR;
        return;
    }

    synchronizeAttribute(localName);
    const AtomicString& caseAdjustedLocalName = shouldIgnoreAttributeCase(*this) ? localName.convertToASCIILowercase() : localName;

    unsigned index = elementData() ? elementData()->findAttributeIndexByName(caseAdjustedLocalName, false) : ElementData::attributeNotFound;
    const QualifiedName& qName = index != ElementData::attributeNotFound ? attributeAt(index).name() : QualifiedName(nullAtom, caseAdjustedLocalName, nullAtom);
    setAttributeInternal(index, qName, value, NotInSynchronizationOfLazyAttribute);
}

void Element::setAttribute(const QualifiedName& name, const AtomicString& value)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    synchronizeAttribute(name);
    unsigned index = elementData() ? elementData()->findAttributeIndexByName(name) : ElementData::attributeNotFound;
    setAttributeInternal(index, name, value, NotInSynchronizationOfLazyAttribute);
}

void Element::setAttributeWithoutSynchronization(const QualifiedName& name, const AtomicString& value)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    unsigned index = elementData() ? elementData()->findAttributeIndexByName(name) : ElementData::attributeNotFound;
    setAttributeInternal(index, name, value, NotInSynchronizationOfLazyAttribute);
}

void Element::setSynchronizedLazyAttribute(const QualifiedName& name, const AtomicString& value)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    unsigned index = elementData() ? elementData()->findAttributeIndexByName(name) : ElementData::attributeNotFound;
    setAttributeInternal(index, name, value, InSynchronizationOfLazyAttribute);
}

inline void Element::setAttributeInternal(unsigned index, const QualifiedName& name, const AtomicString& newValue, SynchronizationOfLazyAttribute inSynchronizationOfLazyAttribute)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (newValue.isNull()) {
        if (index != ElementData::attributeNotFound)
            removeAttributeInternal(index, inSynchronizationOfLazyAttribute);
        return;
    }

    if (index == ElementData::attributeNotFound) {
        addAttributeInternal(name, newValue, inSynchronizationOfLazyAttribute);
        return;
    }

    if (inSynchronizationOfLazyAttribute) {
        ensureUniqueElementData().attributeAt(index).setValue(newValue);
        return;
    }

    const Attribute& attribute = attributeAt(index);
    QualifiedName attributeName = attribute.name();
    AtomicString oldValue = attribute.value();

    willModifyAttribute(attributeName, oldValue, newValue);

    if (newValue != oldValue) {
        // If there is an Attr node hooked to this attribute, the Attr::setValue() call below
        // will write into the ElementData.
        // FIXME: Refactor this so it makes some sense.
        if (RefPtr<Attr> attrNode = attrIfExists(attributeName))
            attrNode->setValue(newValue);
        else {
            Style::AttributeChangeInvalidation styleInvalidation(*this, name, oldValue, newValue);
            ensureUniqueElementData().attributeAt(index).setValue(newValue);
        }
    }

    didModifyAttribute(attributeName, oldValue, newValue);
}

static inline AtomicString makeIdForStyleResolution(const AtomicString& value, bool inQuirksMode)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (inQuirksMode)
        return value.convertToASCIILowercase();
    return value;
}

void Element::attributeChanged(const QualifiedName& name, const AtomicString& oldValue, const AtomicString& newValue, AttributeModificationReason)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    bool valueIsSameAsBefore = oldValue == newValue;

    if (!valueIsSameAsBefore) {
        if (name == HTMLNames::idAttr) {
            if (!oldValue.isEmpty())
                treeScope().idTargetObserverRegistry().notifyObservers(*oldValue.impl());
            if (!newValue.isEmpty())
                treeScope().idTargetObserverRegistry().notifyObservers(*newValue.impl());

            AtomicString oldId = elementData()->idForStyleResolution();
            AtomicString newId = makeIdForStyleResolution(newValue, document().inQuirksMode());
            if (newId != oldId) {
                Style::IdChangeInvalidation styleInvalidation(*this, oldId, newId);
                elementData()->setIdForStyleResolution(newId);
            }
        } else if (name == classAttr)
            classAttributeChanged(newValue);
        else if (name == HTMLNames::nameAttr)
            elementData()->setHasNameAttribute(!newValue.isNull());
        else if (name == HTMLNames::pseudoAttr) {
            if (needsStyleInvalidation() && isInShadowTree())
                setNeedsStyleRecalc(FullStyleChange);
        }
        else if (name == HTMLNames::slotAttr) {
            if (auto* parent = parentElement()) {
                if (auto* shadowRoot = parent->shadowRoot())
                    shadowRoot->hostChildElementDidChangeSlotAttribute(oldValue, newValue);
            }
        }
    }

    parseAttribute(name, newValue);

    document().incDOMTreeVersion();

#if ENABLE(CUSTOM_ELEMENTS)
    if (UNLIKELY(isCustomElement()))
        CustomElementReactionQueue::enqueueAttributeChangedCallbackIfNeeded(*this, name, oldValue, newValue);
#endif

    if (valueIsSameAsBefore)
        return;

    invalidateNodeListAndCollectionCachesInAncestors(&name, this);

    if (AXObjectCache* cache = document().existingAXObjectCache())
        cache->handleAttributeChanged(name, this);
}

template <typename CharacterType>
static inline bool classStringHasClassName(const CharacterType* characters, unsigned length)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(length > 0);

    unsigned i = 0;
    do {
        if (isNotHTMLSpace(characters[i]))
            break;
        ++i;
    } while (i < length);

    return i < length;
}

static inline bool classStringHasClassName(const AtomicString& newClassString)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    unsigned length = newClassString.length();

    if (!length)
        return false;

    if (newClassString.is8Bit())
        return classStringHasClassName(newClassString.characters8(), length);
    return classStringHasClassName(newClassString.characters16(), length);
}

void Element::classAttributeChanged(const AtomicString& newClassString)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    // Note: We'll need ElementData, but it doesn't have to be UniqueElementData.
    if (!elementData())
        ensureUniqueElementData();

    bool shouldFoldCase = document().inQuirksMode();
    bool newStringHasClasses = classStringHasClassName(newClassString);

    auto oldClassNames = elementData()->classNames();
    auto newClassNames = newStringHasClasses ? SpaceSplitString(newClassString, shouldFoldCase) : SpaceSplitString();
    {
        Style::ClassChangeInvalidation styleInvalidation(*this, oldClassNames, newClassNames);
        elementData()->setClassNames(newClassNames);
    }

    if (hasRareData()) {
        if (auto* classList = elementRareData()->classList())
            classList->associatedAttributeValueChanged(newClassString);
    }
}

URL Element::absoluteLinkURL() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!isLink())
        return URL();

    AtomicString linkAttribute;
    if (hasTagName(SVGNames::aTag))
        linkAttribute = getAttribute(XLinkNames::hrefAttr);
    else
        linkAttribute = getAttribute(HTMLNames::hrefAttr);

    if (linkAttribute.isEmpty())
        return URL();

    return document().completeURL(stripLeadingAndTrailingHTMLSpaces(linkAttribute));
}

#if ENABLE(TOUCH_EVENTS)
bool Element::allowsDoubleTapGesture() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (renderStyle() && renderStyle()->touchAction() != TouchAction::Auto)
        return false;

    Element* parent = parentElement();
    return !parent || parent->allowsDoubleTapGesture();
}
#endif

StyleResolver& Element::styleResolver()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (auto* shadowRoot = containingShadowRoot())
        return shadowRoot->styleResolver();

    return document().ensureStyleResolver();
}

ElementStyle Element::resolveStyle(const RenderStyle* parentStyle)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return styleResolver().styleForElement(*this, parentStyle);
}

#if ENABLE(WEB_ANIMATIONS)
WebAnimationVector Element::getAnimations()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    auto checkTarget = [this](AnimationEffect const& effect)
    {
        return (static_cast<KeyframeEffect const&>(effect).target() == this);
    };

    auto* document = DocumentAnimation::from(&this->document());
    if (document)
        return document->getAnimations(checkTarget);
    return WebAnimationVector();
}
#endif

bool Element::hasDisplayContents() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return hasRareData() && elementRareData()->hasDisplayContents();
}

void Element::setHasDisplayContents(bool value)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (hasDisplayContents() == value)
        return;
    ensureElementRareData().setHasDisplayContents(value);
}

// Returns true is the given attribute is an event handler.
// We consider an event handler any attribute that begins with "on".
// It is a simple solution that has the advantage of not requiring any
// code or configuration change if a new event handler is defined.

static inline bool isEventHandlerAttribute(const Attribute& attribute)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return attribute.name().namespaceURI().isNull() && attribute.name().localName().startsWith("on");
}

bool Element::isJavaScriptURLAttribute(const Attribute& attribute) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return isURLAttribute(attribute) && protocolIsJavaScript(stripLeadingAndTrailingHTMLSpaces(attribute.value()));
}

void Element::stripScriptingAttributes(Vector<Attribute>& attributeVector) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    size_t destination = 0;
    for (size_t source = 0; source < attributeVector.size(); ++source) {
        if (isEventHandlerAttribute(attributeVector[source])
            || isJavaScriptURLAttribute(attributeVector[source])
            || isHTMLContentAttribute(attributeVector[source]))
            continue;

        if (source != destination)
            attributeVector[destination] = attributeVector[source];

        ++destination;
    }
    attributeVector.shrink(destination);
}

void Element::parserSetAttributes(const Vector<Attribute>& attributeVector)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(!inDocument());
    ASSERT(!parentNode());
    ASSERT(!m_elementData);

    if (!attributeVector.isEmpty()) {
        if (document().sharedObjectPool())
            m_elementData = document().sharedObjectPool()->cachedShareableElementDataWithAttributes(attributeVector);
        else
            m_elementData = ShareableElementData::createWithAttributes(attributeVector);

    }

    parserDidSetAttributes();

    // Use attributeVector instead of m_elementData because attributeChanged might modify m_elementData.
    for (const auto& attribute : attributeVector)
        attributeChanged(attribute.name(), nullAtom, attribute.value(), ModifiedDirectly);
}

void Element::parserDidSetAttributes()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
}

void Element::didMoveToNewDocument(Document* oldDocument)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    Node::didMoveToNewDocument(oldDocument);

    if (oldDocument->inQuirksMode() != document().inQuirksMode()) {
        // ElementData::m_classNames or ElementData::m_idForStyleResolution need to be updated with the right case.
        if (hasID())
            attributeChanged(idAttr, nullAtom, getIdAttribute());
        if (hasClass())
            attributeChanged(classAttr, nullAtom, getAttribute(classAttr));
    }
}

bool Element::hasAttributes() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    synchronizeAllAttributes();
    return elementData() && elementData()->length();
}

bool Element::hasEquivalentAttributes(const Element* other) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    synchronizeAllAttributes();
    other->synchronizeAllAttributes();
    if (elementData() == other->elementData())
        return true;
    if (elementData())
        return elementData()->isEquivalent(other->elementData());
    if (other->elementData())
        return other->elementData()->isEquivalent(elementData());
    return true;
}

String Element::nodeName() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_tagName.toString();
}

String Element::nodeNamePreservingCase() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_tagName.toString();
}

void Element::setPrefix(const AtomicString& prefix, ExceptionCode& ec)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ec = 0;
    checkSetPrefix(prefix, ec);
    if (ec)
        return;

    m_tagName.setPrefix(prefix.isEmpty() ? nullAtom : prefix);
}

const AtomicString& Element::imageSourceURL() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return attributeWithoutSynchronization(srcAttr);
}

bool Element::rendererIsNeeded(const RenderStyle& style)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return style.display() != NONE && style.display() != CONTENTS;
}

RenderPtr<RenderElement> Element::createElementRenderer(RenderStyle&& style, const RenderTreePosition&)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return RenderElement::createFor(*this, WTFMove(style));
}

Node::InsertionNotificationRequest Element::insertedInto(ContainerNode& insertionPoint)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    bool wasInDocument = inDocument();
    // need to do superclass processing first so inDocument() is true
    // by the time we reach updateId
    ContainerNode::insertedInto(insertionPoint);
    ASSERT(!wasInDocument || inDocument());

#if ENABLE(FULLSCREEN_API)
    if (containsFullScreenElement() && parentElement() && !parentElement()->containsFullScreenElement())
        setContainsFullScreenElementOnAncestorsCrossingFrameBoundaries(true);
#endif

    if (parentNode() == &insertionPoint) {
        if (auto* shadowRoot = parentNode()->shadowRoot())
            shadowRoot->hostChildElementDidChange(*this);
    }

    if (!insertionPoint.isInTreeScope())
        return InsertionDone;

    // This function could be called when this element's shadow root's host or its ancestor is inserted.
    // This element is new to the shadow tree (and its tree scope) only if the parent into which this element
    // or its ancestor is inserted belongs to the same tree scope as this element's.
    TreeScope* newScope = &insertionPoint.treeScope();
    HTMLDocument* newDocument = !wasInDocument && inDocument() && is<HTMLDocument>(newScope->documentScope()) ? &downcast<HTMLDocument>(newScope->documentScope()) : nullptr;
    if (newScope != &treeScope())
        newScope = nullptr;

    const AtomicString& idValue = getIdAttribute();
    if (!idValue.isNull()) {
        if (newScope)
            updateIdForTreeScope(*newScope, nullAtom, idValue);
        if (newDocument)
            updateIdForDocument(*newDocument, nullAtom, idValue, AlwaysUpdateHTMLDocumentNamedItemMaps);
    }

    const AtomicString& nameValue = getNameAttribute();
    if (!nameValue.isNull()) {
        if (newScope)
            updateNameForTreeScope(*newScope, nullAtom, nameValue);
        if (newDocument)
            updateNameForDocument(*newDocument, nullAtom, nameValue);
    }

    if (newScope && hasTagName(labelTag)) {
        if (newScope->shouldCacheLabelsByForAttribute())
            updateLabel(*newScope, nullAtom, attributeWithoutSynchronization(forAttr));
    }

#if ENABLE(CUSTOM_ELEMENTS)
    if (newDocument && UNLIKELY(isCustomElement()))
        CustomElementReactionQueue::enqueueConnectedCallbackIfNeeded(*this);
#endif

    return InsertionDone;
}

void Element::removedFrom(ContainerNode& insertionPoint)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
#if ENABLE(FULLSCREEN_API)
    if (containsFullScreenElement())
        setContainsFullScreenElementOnAncestorsCrossingFrameBoundaries(false);
#endif
#if ENABLE(POINTER_LOCK)
    if (document().page())
        document().page()->pointerLockController().elementRemoved(this);
#endif

    setSavedLayerScrollPosition(ScrollPosition());

    if (insertionPoint.isInTreeScope()) {
        TreeScope* oldScope = &insertionPoint.treeScope();
        HTMLDocument* oldDocument = inDocument() && is<HTMLDocument>(oldScope->documentScope()) ? &downcast<HTMLDocument>(oldScope->documentScope()) : nullptr;

        // ContainerNode::removeBetween always sets the removed chid's tree scope to Document's but InTreeScope flag is unset in Node::removedFrom.
        // So this element has been removed from the old tree scope only if InTreeScope flag is set and this element's tree scope is Document's.
        if (!isInTreeScope() || &treeScope() != &document())
            oldScope = nullptr;

        const AtomicString& idValue = getIdAttribute();
        if (!idValue.isNull()) {
            if (oldScope)
                updateIdForTreeScope(*oldScope, idValue, nullAtom);
            if (oldDocument)
                updateIdForDocument(*oldDocument, idValue, nullAtom, AlwaysUpdateHTMLDocumentNamedItemMaps);
        }

        const AtomicString& nameValue = getNameAttribute();
        if (!nameValue.isNull()) {
            if (oldScope)
                updateNameForTreeScope(*oldScope, nameValue, nullAtom);
            if (oldDocument)
                updateNameForDocument(*oldDocument, nameValue, nullAtom);
        }

        if (oldScope && hasTagName(labelTag)) {
            if (oldScope->shouldCacheLabelsByForAttribute())
                updateLabel(*oldScope, attributeWithoutSynchronization(forAttr), nullAtom);
        }

#if ENABLE(CUSTOM_ELEMENTS)
        if (oldDocument && UNLIKELY(isCustomElement()))
            CustomElementReactionQueue::enqueueDisconnectedCallbackIfNeeded(*this);
#endif
    }

    if (!parentNode()) {
        if (auto* shadowRoot = insertionPoint.shadowRoot())
            shadowRoot->hostChildElementDidChange(*this);
    }

    ContainerNode::removedFrom(insertionPoint);

    if (hasPendingResources())
        document().accessSVGExtensions().removeElementFromPendingResources(this);


#if PLATFORM(MAC)
    if (Frame* frame = document().frame())
        frame->mainFrame().removeLatchingStateForTarget(*this);
#endif
}

void Element::unregisterNamedFlowContentElement()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (isNamedFlowContentElement() && document().renderView())
        document().renderView()->flowThreadController().unregisterNamedFlowContentElement(*this);
}

ShadowRoot* Element::shadowRoot() const
{//     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return hasRareData() ? elementRareData()->shadowRoot() : nullptr;
}


void Element::addShadowRoot(Ref<ShadowRoot>&& newShadowRoot)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(!shadowRoot());

    ShadowRoot& shadowRoot = newShadowRoot.get();
    ensureElementRareData().setShadowRoot(WTFMove(newShadowRoot));

    shadowRoot.setHost(this);
    shadowRoot.setParentTreeScope(&treeScope());

    NodeVector postInsertionNotificationTargets;
    notifyChildNodeInserted(*this, shadowRoot, postInsertionNotificationTargets);
    for (auto& target : postInsertionNotificationTargets)
        target->finishedInsertingSubtree();

    setNeedsStyleRecalc(ReconstructRenderTree);

    InspectorInstrumentation::didPushShadowRoot(*this, shadowRoot);

    if (shadowRoot.mode() == ShadowRoot::Mode::UserAgent)
        didAddUserAgentShadowRoot(&shadowRoot);
}

void Element::removeShadowRoot()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    RefPtr<ShadowRoot> oldRoot = shadowRoot();
    if (!oldRoot)
        return;
    InspectorInstrumentation::willPopShadowRoot(*this, *oldRoot);
    document().removeFocusedNodeOfSubtree(oldRoot.get());

    ASSERT(!oldRoot->renderer());

    elementRareData()->clearShadowRoot();

    oldRoot->setHost(nullptr);
    oldRoot->setParentTreeScope(&document());

    notifyChildNodeRemoved(*this, *oldRoot);
}

RefPtr<ShadowRoot> Element::createShadowRoot(ExceptionCode& ec)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (alwaysCreateUserAgentShadowRoot())
        ensureUserAgentShadowRoot();

    ec = HIERARCHY_REQUEST_ERR;
    return nullptr;
}


static bool canAttachAuthorShadowRoot(const Element& element)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    static NeverDestroyed<HashSet<AtomicString>> tagNames = [] {
        static const HTMLQualifiedName* const tagList[] = {
            &articleTag,
            &asideTag,
            &blockquoteTag,
            &bodyTag,
            &divTag,
            &footerTag,
            &h1Tag,
            &h2Tag,
            &h3Tag,
            &h4Tag,
            &h5Tag,
            &h6Tag,
            &headerTag,
            &navTag,
            &pTag,
            &sectionTag,
            &spanTag
        };
        HashSet<AtomicString> set;
        for (auto& name : tagList)
            set.add(name->localName());
        return set;
    }();

    if (!is<HTMLElement>(element))
        return false;

    const auto& localName = element.localName();
    return tagNames.get().contains(localName) || Document::validateCustomElementName(localName) == CustomElementNameValidationStatus::Valid;
}

RefPtr<ShadowRoot> Element::attachShadow(const ShadowRootInit& init, ExceptionCode& ec)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!canAttachAuthorShadowRoot(*this)) {
        ec = NOT_SUPPORTED_ERR;
        return nullptr;
    }

    if (shadowRoot()) {
        ec = INVALID_STATE_ERR;
        return nullptr;
    }

    auto shadow = ShadowRoot::create(document(), init.mode == ShadowRootMode::Open ? ShadowRoot::Mode::Open : ShadowRoot::Mode::Closed);
    addShadowRoot(shadow.copyRef());
    return WTFMove(shadow);
}

ShadowRoot* Element::shadowRootForBindings(JSC::ExecState& state) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ShadowRoot* root = shadowRoot();
    if (!root)
        return nullptr;

    if (root->mode() != ShadowRoot::Mode::Open) {
        if (!JSC::jsCast<JSDOMGlobalObject*>(state.lexicalGlobalObject())->world().shadowRootIsAlwaysOpen())
            return nullptr;
    }
    return root;
}


ShadowRoot* Element::userAgentShadowRoot() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (ShadowRoot* shadowRoot = this->shadowRoot()) {
        ASSERT(shadowRoot->mode() == ShadowRoot::Mode::UserAgent);
        return shadowRoot;
    }
    return nullptr;
}

ShadowRoot& Element::ensureUserAgentShadowRoot()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ShadowRoot* shadowRoot = userAgentShadowRoot();
    if (!shadowRoot) {
        addShadowRoot(ShadowRoot::create(document(), ShadowRoot::Mode::UserAgent));
        shadowRoot = userAgentShadowRoot();
    }
    return *shadowRoot;
}

const AtomicString& Element::shadowPseudoId() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return pseudo();
}

bool Element::childTypeAllowed(NodeType type) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    switch (type) {
    case ELEMENT_NODE:
    case TEXT_NODE:
    case COMMENT_NODE:
    case PROCESSING_INSTRUCTION_NODE:
    case CDATA_SECTION_NODE:
        return true;
    default:
        break;
    }
    return false;
}

static void checkForEmptyStyleChange(Element& element)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (element.styleAffectedByEmpty()) {
        auto* style = element.renderStyle();
        if (!style || (!style->emptyState() || element.hasChildNodes()))
            element.setNeedsStyleRecalc();
    }
}

enum SiblingCheckType { FinishedParsingChildren, SiblingElementRemoved, Other };

static void checkForSiblingStyleChanges(Element& parent, SiblingCheckType checkType, Element* elementBeforeChange, Element* elementAfterChange)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    // :empty selector.
    checkForEmptyStyleChange(parent);

    if (parent.styleChangeType() >= FullStyleChange)
        return;

    // :first-child.  In the parser callback case, we don't have to check anything, since we were right the first time.
    // In the DOM case, we only need to do something if |afterChange| is not 0.
    // |afterChange| is 0 in the parser case, so it works out that we'll skip this block.
    if (parent.childrenAffectedByFirstChildRules() && elementAfterChange) {
        // Find our new first child.
        Element* newFirstElement = ElementTraversal::firstChild(parent);
        // Find the first element node following |afterChange|

        // This is the insert/append case.
        if (newFirstElement != elementAfterChange) {
            auto* style = elementAfterChange->renderStyle();
            if (!style || style->firstChildState())
                elementAfterChange->setNeedsStyleRecalc();
        }

        // We also have to handle node removal.
        if (checkType == SiblingElementRemoved && newFirstElement == elementAfterChange && newFirstElement) {
            auto* style = newFirstElement->renderStyle();
            if (!style || !style->firstChildState())
                newFirstElement->setNeedsStyleRecalc();
        }
    }

    // :last-child.  In the parser callback case, we don't have to check anything, since we were right the first time.
    // In the DOM case, we only need to do something if |afterChange| is not 0.
    if (parent.childrenAffectedByLastChildRules() && elementBeforeChange) {
        // Find our new last child.
        Element* newLastElement = ElementTraversal::lastChild(parent);

        if (newLastElement != elementBeforeChange) {
            auto* style = elementBeforeChange->renderStyle();
            if (!style || style->lastChildState())
                elementBeforeChange->setNeedsStyleRecalc();
        }

        // We also have to handle node removal.  The parser callback case is similar to node removal as well in that we need to change the last child
        // to match now.
        if ((checkType == SiblingElementRemoved || checkType == FinishedParsingChildren) && newLastElement == elementBeforeChange && newLastElement) {
            auto* style = newLastElement->renderStyle();
            if (!style || !style->lastChildState())
                newLastElement->setNeedsStyleRecalc();
        }
    }

    if (elementAfterChange) {
        if (elementAfterChange->styleIsAffectedByPreviousSibling())
            elementAfterChange->setNeedsStyleRecalc();
        else if (elementAfterChange->affectsNextSiblingElementStyle()) {
            Element* elementToInvalidate = elementAfterChange;
            do {
                elementToInvalidate = elementToInvalidate->nextElementSibling();
            } while (elementToInvalidate && !elementToInvalidate->styleIsAffectedByPreviousSibling());

            if (elementToInvalidate)
                elementToInvalidate->setNeedsStyleRecalc();
        }
    }

    // Backward positional selectors include nth-last-child, nth-last-of-type, last-of-type and only-of-type.
    // We have to invalidate everything following the insertion point in the forward case, and everything before the insertion point in the
    // backward case.
    // |afterChange| is 0 in the parser callback case, so we won't do any work for the forward case if we don't have to.
    // For performance reasons we just mark the parent node as changed, since we don't want to make childrenChanged O(n^2) by crawling all our kids
    // here.  recalcStyle will then force a walk of the children when it sees that this has happened.
    if (parent.childrenAffectedByBackwardPositionalRules() && elementBeforeChange)
        parent.setNeedsStyleRecalc();
}

void Element::childrenChanged(const ChildChange& change)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ContainerNode::childrenChanged(change);
    if (change.source == ChildChangeSourceParser)
        checkForEmptyStyleChange(*this);
    else {
        SiblingCheckType checkType = change.type == ElementRemoved ? SiblingElementRemoved : Other;
        checkForSiblingStyleChanges(*this, checkType, change.previousSiblingElement, change.nextSiblingElement);
    }

    if (ShadowRoot* shadowRoot = this->shadowRoot()) {
        switch (change.type) {
        case ElementInserted:
        case ElementRemoved:
            // For elements, we notify shadowRoot in Element::insertedInto and Element::removedFrom.
            break;
        case AllChildrenRemoved:
            shadowRoot->didRemoveAllChildrenOfShadowHost();
            break;
        case TextInserted:
        case TextRemoved:
        case TextChanged:
            shadowRoot->didChangeDefaultSlot();
            break;
        case NonContentsChildChanged:
            break;
        }
    }
}

void Element::setAttributeEventListener(const AtomicString& eventType, const QualifiedName& attributeName, const AtomicString& attributeValue)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    setAttributeEventListener(eventType, JSLazyEventListener::create(*this, attributeName, attributeValue));
}

void Element::setIsNamedFlowContentElement()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ensureElementRareData().setIsNamedFlowContentElement(true);
}

void Element::clearIsNamedFlowContentElement()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ensureElementRareData().setIsNamedFlowContentElement(false);
}

void Element::removeAllEventListeners()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ContainerNode::removeAllEventListeners();
    if (ShadowRoot* shadowRoot = this->shadowRoot())
        shadowRoot->removeAllEventListeners();
}

void Element::beginParsingChildren()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    clearIsParsingChildrenFinished();
}

void Element::finishParsingChildren()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ContainerNode::finishParsingChildren();
    setIsParsingChildrenFinished();
    checkForSiblingStyleChanges(*this, FinishedParsingChildren, ElementTraversal::lastChild(*this), nullptr);
}

#if ENABLE(TREE_DEBUGGING)
void Element::formatForDebugger(char* buffer, unsigned length) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    StringBuilder result;
    String s;

    result.append(nodeName());

    s = getIdAttribute();
    if (s.length() > 0) {
        if (result.length() > 0)
            result.appendLiteral("; ");
        result.appendLiteral("id=");
        result.append(s);
    }

    s = getAttribute(classAttr);
    if (s.length() > 0) {
        if (result.length() > 0)
            result.appendLiteral("; ");
        result.appendLiteral("class=");
        result.append(s);
    }

    strncpy(buffer, result.toString().utf8().data(), length - 1);
}
#endif

const Vector<RefPtr<Attr>>& Element::attrNodeList()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(hasSyntheticAttrChildNodes());
    return *attrNodeListForElement(*this);
}

RefPtr<Attr> Element::setAttributeNode(Attr& attrNode, ExceptionCode& ec)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    RefPtr<Attr> oldAttrNode = attrIfExists(attrNode.qualifiedName().localName(), shouldIgnoreAttributeCase(*this));
    if (oldAttrNode.get() == &attrNode)
        return &attrNode; // This Attr is already attached to the element.

    // INUSE_ATTRIBUTE_ERR: Raised if node is an Attr that is already an attribute of another Element object.
    // The DOM user must explicitly clone Attr nodes to re-use them in other elements.
    if (attrNode.ownerElement() && attrNode.ownerElement() != this) {
        ec = INUSE_ATTRIBUTE_ERR;
        return nullptr;
    }

    synchronizeAllAttributes();
    UniqueElementData& elementData = ensureUniqueElementData();

    unsigned existingAttributeIndex = elementData.findAttributeIndexByName(attrNode.qualifiedName().localName(), shouldIgnoreAttributeCase(*this));
    if (existingAttributeIndex != ElementData::attributeNotFound) {
        const Attribute& attribute = attributeAt(existingAttributeIndex);
        if (oldAttrNode)
            detachAttrNodeFromElementWithValue(oldAttrNode.get(), attribute.value());
        else
            oldAttrNode = Attr::create(document(), attrNode.qualifiedName(), attribute.value());

        if (attribute.name().matches(attrNode.qualifiedName()))
            setAttributeInternal(existingAttributeIndex, attrNode.qualifiedName(), attrNode.value(), NotInSynchronizationOfLazyAttribute);
        else {
            removeAttributeInternal(existingAttributeIndex, NotInSynchronizationOfLazyAttribute);
            unsigned existingAttributeIndexForFullQualifiedName = elementData.findAttributeIndexByName(attrNode.qualifiedName());
            setAttributeInternal(existingAttributeIndexForFullQualifiedName, attrNode.qualifiedName(), attrNode.value(), NotInSynchronizationOfLazyAttribute);
        }
    } else {
        unsigned existingAttributeIndexForFullQualifiedName = elementData.findAttributeIndexByName(attrNode.qualifiedName());
        setAttributeInternal(existingAttributeIndexForFullQualifiedName, attrNode.qualifiedName(), attrNode.value(), NotInSynchronizationOfLazyAttribute);
    }
    if (attrNode.ownerElement() != this) {
        attrNode.attachToElement(this);
        treeScope().adoptIfNeeded(&attrNode);
        ensureAttrNodeListForElement(*this).append(&attrNode);
    }
    return oldAttrNode;
}

RefPtr<Attr> Element::setAttributeNodeNS(Attr& attrNode, ExceptionCode& ec)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    RefPtr<Attr> oldAttrNode = attrIfExists(attrNode.qualifiedName());
    if (oldAttrNode.get() == &attrNode)
        return &attrNode; // This Attr is already attached to the element.

    // INUSE_ATTRIBUTE_ERR: Raised if node is an Attr that is already an attribute of another Element object.
    // The DOM user must explicitly clone Attr nodes to re-use them in other elements.
    if (attrNode.ownerElement() && attrNode.ownerElement() != this) {
        ec = INUSE_ATTRIBUTE_ERR;
        return nullptr;
    }

    synchronizeAllAttributes();
    UniqueElementData& elementData = ensureUniqueElementData();

    unsigned index = elementData.findAttributeIndexByName(attrNode.qualifiedName());
    if (index != ElementData::attributeNotFound) {
        if (oldAttrNode)
            detachAttrNodeFromElementWithValue(oldAttrNode.get(), elementData.attributeAt(index).value());
        else
            oldAttrNode = Attr::create(document(), attrNode.qualifiedName(), elementData.attributeAt(index).value());
    }

    setAttributeInternal(index, attrNode.qualifiedName(), attrNode.value(), NotInSynchronizationOfLazyAttribute);

    attrNode.attachToElement(this);
    treeScope().adoptIfNeeded(&attrNode);
    ensureAttrNodeListForElement(*this).append(&attrNode);

    return oldAttrNode;
}

RefPtr<Attr> Element::removeAttributeNode(Attr& attr, ExceptionCode& ec)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (attr.ownerElement() != this) {
        ec = NOT_FOUND_ERR;
        return nullptr;
    }

    ASSERT(&document() == &attr.document());

    synchronizeAllAttributes();

    if (!m_elementData) {
        ec = NOT_FOUND_ERR;
        return nullptr;
    }

    unsigned existingAttributeIndex = m_elementData->findAttributeIndexByName(attr.qualifiedName());

    if (existingAttributeIndex == ElementData::attributeNotFound) {
        ec = NOT_FOUND_ERR;
        return nullptr;
    }

    RefPtr<Attr> attrNode = &attr;
    detachAttrNodeFromElementWithValue(&attr, m_elementData->attributeAt(existingAttributeIndex).value());
    removeAttributeInternal(existingAttributeIndex, NotInSynchronizationOfLazyAttribute);
    return attrNode;
}

bool Element::parseAttributeName(QualifiedName& out, const AtomicString& namespaceURI, const AtomicString& qualifiedName, ExceptionCode& ec)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    String prefix, localName;
    if (!Document::parseQualifiedName(qualifiedName, prefix, localName, ec))
        return false;
    ASSERT(!ec);

    QualifiedName qName(prefix, localName, namespaceURI);

    if (!Document::hasValidNamespaceForAttributes(qName)) {
        ec = NAMESPACE_ERR;
        return false;
    }

    out = qName;
    return true;
}

void Element::setAttributeNS(const AtomicString& namespaceURI, const AtomicString& qualifiedName, const AtomicString& value, ExceptionCode& ec)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    QualifiedName parsedName = anyName;
    if (!parseAttributeName(parsedName, namespaceURI, qualifiedName, ec))
        return;
    setAttribute(parsedName, value);
}

void Element::removeAttributeInternal(unsigned index, SynchronizationOfLazyAttribute inSynchronizationOfLazyAttribute)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT_WITH_SECURITY_IMPLICATION(index < attributeCount());

    UniqueElementData& elementData = ensureUniqueElementData();

    QualifiedName name = elementData.attributeAt(index).name();
    AtomicString valueBeingRemoved = elementData.attributeAt(index).value();

    if (RefPtr<Attr> attrNode = attrIfExists(name))
        detachAttrNodeFromElementWithValue(attrNode.get(), elementData.attributeAt(index).value());

    if (inSynchronizationOfLazyAttribute) {
        elementData.removeAttribute(index);
        return;
    }

    if (!valueBeingRemoved.isNull())
        willModifyAttribute(name, valueBeingRemoved, nullAtom);

    {
        Style::AttributeChangeInvalidation styleInvalidation(*this, name, valueBeingRemoved, nullAtom);
        elementData.removeAttribute(index);
    }

    didRemoveAttribute(name, valueBeingRemoved);
}

void Element::addAttributeInternal(const QualifiedName& name, const AtomicString& value, SynchronizationOfLazyAttribute inSynchronizationOfLazyAttribute)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (inSynchronizationOfLazyAttribute) {
        ensureUniqueElementData().addAttribute(name, value);
        return;
    }

    willModifyAttribute(name, nullAtom, value);
    {
        Style::AttributeChangeInvalidation styleInvalidation(*this, name, nullAtom, value);
        ensureUniqueElementData().addAttribute(name, value);
    }
    didAddAttribute(name, value);
}

bool Element::removeAttribute(const AtomicString& name)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!elementData())
        return false;

    AtomicString localName = shouldIgnoreAttributeCase(*this) ? name.convertToASCIILowercase() : name;
    unsigned index = elementData()->findAttributeIndexByName(localName, false);
    if (index == ElementData::attributeNotFound) {
        if (UNLIKELY(localName == styleAttr) && elementData()->styleAttributeIsDirty() && is<StyledElement>(*this))
            downcast<StyledElement>(*this).removeAllInlineStyleProperties();
        return false;
    }

    removeAttributeInternal(index, NotInSynchronizationOfLazyAttribute);
    return true;
}

bool Element::removeAttributeNS(const AtomicString& namespaceURI, const AtomicString& localName)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return removeAttribute(QualifiedName(nullAtom, localName, namespaceURI));
}

RefPtr<Attr> Element::getAttributeNode(const AtomicString& localName)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!elementData())
        return nullptr;
    synchronizeAttribute(localName);
    const Attribute* attribute = elementData()->findAttributeByName(localName, shouldIgnoreAttributeCase(*this));
    if (!attribute)
        return nullptr;
    return ensureAttr(attribute->name());
}

RefPtr<Attr> Element::getAttributeNodeNS(const AtomicString& namespaceURI, const AtomicString& localName)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!elementData())
        return 0;
    QualifiedName qName(nullAtom, localName, namespaceURI);
    synchronizeAttribute(qName);
    const Attribute* attribute = elementData()->findAttributeByName(qName);
    if (!attribute)
        return 0;
    return ensureAttr(attribute->name());
}

bool Element::hasAttribute(const AtomicString& localName) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!elementData())
        return false;
    synchronizeAttribute(localName);
    return elementData()->findAttributeByName(localName, shouldIgnoreAttributeCase(*this));
}

bool Element::hasAttributeNS(const AtomicString& namespaceURI, const AtomicString& localName) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!elementData())
        return false;
    QualifiedName qName(nullAtom, localName, namespaceURI);
    synchronizeAttribute(qName);
    return elementData()->findAttributeByName(qName);
}

CSSStyleDeclaration* Element::cssomStyle()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return nullptr;
}

void Element::focus(bool restorePreviousSelection, FocusDirection direction)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!inDocument())
        return;

    if (document().focusedElement() == this) {
        if (document().page())
            document().page()->chrome().client().elementDidRefocus(this);

        return;
    }

    // If the stylesheets have already been loaded we can reliably check isFocusable.
    // If not, we continue and set the focused node on the focus controller below so
    // that it can be updated soon after attach. 
    if (document().haveStylesheetsLoaded()) {
        document().updateLayoutIgnorePendingStylesheets();
        if (!isFocusable())
            return;
    }

    if (!supportsFocus())
        return;

    RefPtr<Node> protect;
    if (Page* page = document().page()) {
        // Focus and change event handlers can cause us to lose our last ref.
        // If a focus event handler changes the focus to a different node it
        // does not make sense to continue and update appearence.
        protect = this;
        if (!page->focusController().setFocusedElement(this, document().frame(), direction))
            return;
    }

    // Setting the focused node above might have invalidated the layout due to scripts.
    document().updateLayoutIgnorePendingStylesheets();

    if (!isFocusable()) {
        ensureElementRareData().setNeedsFocusAppearanceUpdateSoonAfterAttach(true);
        return;
    }
        
    cancelFocusAppearanceUpdate();

    SelectionRevealMode revealMode = SelectionRevealMode::Reveal;
#if PLATFORM(IOS)
    // Focusing a form element triggers animation in UIKit to scroll to the right position.
    // Calling updateFocusAppearance() would generate an unnecessary call to ScrollView::setScrollPosition(),
    // which would jump us around during this animation. See <rdar://problem/6699741>.
    bool isFormControl = is<HTMLFormControlElement>(*this);
    if (isFormControl)
        revealMode = SelectionRevealMode::RevealUpToMainFrame;
#endif

    updateFocusAppearance(restorePreviousSelection ? SelectionRestorationMode::Restore : SelectionRestorationMode::SetDefault, revealMode);
}

void Element::updateFocusAppearanceAfterAttachIfNeeded()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!hasRareData())
        return;
    ElementRareData* data = elementRareData();
    if (!data->needsFocusAppearanceUpdateSoonAfterAttach())
        return;
    if (isFocusable() && document().focusedElement() == this)
        document().updateFocusAppearanceSoon(SelectionRestorationMode::SetDefault);
    data->setNeedsFocusAppearanceUpdateSoonAfterAttach(false);
}

void Element::updateFocusAppearance(SelectionRestorationMode, SelectionRevealMode revealMode)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (isRootEditableElement()) {
        // Keep frame alive in this method, since setSelection() may release the last reference to |frame|.
        RefPtr<Frame> frame = document().frame();
        if (!frame)
            return;
        
        // When focusing an editable element in an iframe, don't reset the selection if it already contains a selection.
        if (this == frame->selection().selection().rootEditableElement())
            return;

        // FIXME: We should restore the previous selection if there is one.
        VisibleSelection newSelection = VisibleSelection(firstPositionInOrBeforeNode(this), DOWNSTREAM);
        
        if (frame->selection().shouldChangeSelection(newSelection)) {
            frame->selection().setSelection(newSelection, FrameSelection::defaultSetSelectionOptions(), Element::defaultFocusTextStateChangeIntent());
            frame->selection().revealSelection(revealMode);
        }
    } else if (renderer() && !renderer()->isWidget())
        renderer()->scrollRectToVisible(revealMode, renderer()->anchorRect());
}

void Element::blur()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    cancelFocusAppearanceUpdate();
    if (treeScope().focusedElement() == this) {
        if (Frame* frame = document().frame())
            frame->page()->focusController().setFocusedElement(0, frame);
        else
            document().setFocusedElement(nullptr);
    }
}

void Element::dispatchFocusInEvent(const AtomicString& eventType, RefPtr<Element>&& oldFocusedElement)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT_WITH_SECURITY_IMPLICATION(!NoEventDispatchAssertion::isEventDispatchForbidden());
    ASSERT(eventType == eventNames().focusinEvent || eventType == eventNames().DOMFocusInEvent);
    dispatchScopedEvent(FocusEvent::create(eventType, true, false, document().defaultView(), 0, WTFMove(oldFocusedElement)));
}

void Element::dispatchFocusOutEvent(const AtomicString& eventType, RefPtr<Element>&& newFocusedElement)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT_WITH_SECURITY_IMPLICATION(!NoEventDispatchAssertion::isEventDispatchForbidden());
    ASSERT(eventType == eventNames().focusoutEvent || eventType == eventNames().DOMFocusOutEvent);
    dispatchScopedEvent(FocusEvent::create(eventType, true, false, document().defaultView(), 0, WTFMove(newFocusedElement)));
}

void Element::dispatchFocusEvent(RefPtr<Element>&& oldFocusedElement, FocusDirection)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (document().page())
        document().page()->chrome().client().elementDidFocus(this);

    EventDispatcher::dispatchEvent(this, FocusEvent::create(eventNames().focusEvent, false, false, document().defaultView(), 0, WTFMove(oldFocusedElement)));
}

void Element::dispatchBlurEvent(RefPtr<Element>&& newFocusedElement)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (document().page())
        document().page()->chrome().client().elementDidBlur(this);

    EventDispatcher::dispatchEvent(this, FocusEvent::create(eventNames().blurEvent, false, false, document().defaultView(), 0, WTFMove(newFocusedElement)));
}

#if ENABLE(MOUSE_FORCE_EVENTS)
bool Element::dispatchMouseForceWillBegin()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!document().hasListenerType(Document::FORCEWILLBEGIN_LISTENER))
        return false;

    Frame* frame = document().frame();
    if (!frame)
        return false;

    PlatformMouseEvent platformMouseEvent(frame->eventHandler().lastKnownMousePosition(), frame->eventHandler().lastKnownMouseGlobalPosition(), NoButton, PlatformEvent::NoType, 1, false, false, false, false, WTF::currentTime(), ForceAtClick, NoTap);
    Ref<MouseEvent> mouseForceWillBeginEvent =  MouseEvent::create(eventNames().webkitmouseforcewillbeginEvent, document().defaultView(), platformMouseEvent, 0, nullptr);
    mouseForceWillBeginEvent->setTarget(this);
    dispatchEvent(mouseForceWillBeginEvent);

    if (mouseForceWillBeginEvent->defaultHandled() || mouseForceWillBeginEvent->defaultPrevented())
        return true;
    return false;
}
#else
bool Element::dispatchMouseForceWillBegin()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return false;
}
#endif // #if ENABLE(MOUSE_FORCE_EVENTS)

void Element::mergeWithNextTextNode(Text& node, ExceptionCode& ec)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    Node* next = node.nextSibling();
    if (!is<Text>(next))
        return;

    Ref<Text> textNode(node);
    Ref<Text> textNext(downcast<Text>(*next));
    textNode->appendData(textNext->data());
    textNext->remove(ec);
}

String Element::innerHTML() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return createMarkup(*this, ChildrenOnly);
}

String Element::outerHTML() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return createMarkup(*this);
}

void Element::setOuterHTML(const String& html, ExceptionCode& ec)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    Element* p = parentElement();
    if (!is<HTMLElement>(p)) {
        ec = NO_MODIFICATION_ALLOWED_ERR;
        return;
    }
    RefPtr<HTMLElement> parent = downcast<HTMLElement>(p);
    RefPtr<Node> prev = previousSibling();
    RefPtr<Node> next = nextSibling();

    RefPtr<DocumentFragment> fragment = createFragmentForInnerOuterHTML(*parent, html, AllowScriptingContent, ec);
    if (ec)
        return;
    
    parent->replaceChild(*fragment, *this, ec);
    RefPtr<Node> node = next ? next->previousSibling() : nullptr;
    if (!ec && is<Text>(node.get()))
        mergeWithNextTextNode(downcast<Text>(*node), ec);
    if (!ec && is<Text>(prev.get()))
        mergeWithNextTextNode(downcast<Text>(*prev), ec);
}


void Element::setInnerHTML(const String& html, ExceptionCode& ec)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (RefPtr<DocumentFragment> fragment = createFragmentForInnerOuterHTML(*this, html, AllowScriptingContent, ec)) {
        ContainerNode* container = this;

        if (is<HTMLTemplateElement>(*this))
            container = &downcast<HTMLTemplateElement>(*this).content();

        replaceChildrenWithFragment(*container, fragment.releaseNonNull(), ec);
    }
}

String Element::innerText()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    // We need to update layout, since plainText uses line boxes in the render tree.
    document().updateLayoutIgnorePendingStylesheets();

    if (!renderer())
        return textContent(true);

    return plainText(rangeOfContents(*this).ptr());
}

String Element::outerText()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    // Getting outerText is the same as getting innerText, only
    // setting is different. You would think this should get the plain
    // text for the outer range, but this is wrong, <br> for instance
    // would return different values for inner and outer text by such
    // a rule, but it doesn't in WinIE, and we want to match that.
    return innerText();
}

String Element::title() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return String();
}

const AtomicString& Element::pseudo() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return attributeWithoutSynchronization(pseudoAttr);
}

void Element::setPseudo(const AtomicString& value)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    setAttributeWithoutSynchronization(pseudoAttr, value);
}

LayoutSize Element::minimumSizeForResizing() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return hasRareData() ? elementRareData()->minimumSizeForResizing() : defaultMinimumSizeForResizing();
}

void Element::setMinimumSizeForResizing(const LayoutSize& size)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!hasRareData() && size == defaultMinimumSizeForResizing())
        return;
    ensureElementRareData().setMinimumSizeForResizing(size);
}

void Element::willBecomeFullscreenElement()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    for (auto& child : descendantsOfType<Element>(*this))
        child.ancestorWillEnterFullscreen();
}

static PseudoElement* beforeOrAfterPseudoElement(Element& host, PseudoId pseudoElementSpecifier)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    switch (pseudoElementSpecifier) {
    case BEFORE:
        return host.beforePseudoElement();
    case AFTER:
        return host.afterPseudoElement();
    default:
        return nullptr;
    }
}

const RenderStyle* Element::existingComputedStyle()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (auto* renderTreeStyle = renderStyle())
        return renderTreeStyle;

    if (hasRareData())
        return elementRareData()->computedStyle();

    return nullptr;
}

const RenderStyle& Element::resolveComputedStyle()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(inDocument());
    ASSERT(!existingComputedStyle());

    Deque<Element*, 32> elementsRequiringComputedStyle({ this });
    const RenderStyle* computedStyle = nullptr;

    // Collect ancestors until we find one that has style.
    auto composedAncestors = composedTreeAncestors(*this);
    for (auto& ancestor : composedAncestors) {
        elementsRequiringComputedStyle.prepend(&ancestor);
        if (auto* existingStyle = ancestor.existingComputedStyle()) {
            computedStyle = existingStyle;
            break;
        }
    }

    // Resolve and cache styles starting from the most distant ancestor.
    for (auto* element : elementsRequiringComputedStyle) {
        auto style = document().styleForElementIgnoringPendingStylesheets(*element, computedStyle);
        computedStyle = style.get();
        ElementRareData& rareData = element->ensureElementRareData();
        rareData.setComputedStyle(WTFMove(style));
    }

    return *computedStyle;
}

const RenderStyle* Element::computedStyle(PseudoId pseudoElementSpecifier)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (PseudoElement* pseudoElement = beforeOrAfterPseudoElement(*this, pseudoElementSpecifier))
        return pseudoElement->computedStyle();

    auto* style = existingComputedStyle();
    if (!style) {
        if (!inDocument())
            return nullptr;
        style = &resolveComputedStyle();
    }

    if (pseudoElementSpecifier) {
        if (auto* cachedPseudoStyle = style->getCachedPseudoStyle(pseudoElementSpecifier))
            return cachedPseudoStyle;
    }

    return style;
}

bool Element::needsStyleInvalidation() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!inRenderedDocument())
        return false;
    if (styleChangeType() >= FullStyleChange)
        return false;
    if (document().hasPendingForcedStyleRecalc())
        return false;

    return true;
}

void Element::setStyleAffectedByEmpty()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ensureElementRareData().setStyleAffectedByEmpty(true);
}

void Element::setStyleAffectedByFocusWithin()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ensureElementRareData().setStyleAffectedByFocusWithin(true);
}

void Element::setStyleAffectedByActive()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ensureElementRareData().setStyleAffectedByActive(true);
}

void Element::setChildrenAffectedByDrag()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ensureElementRareData().setChildrenAffectedByDrag(true);
}

void Element::setChildrenAffectedByBackwardPositionalRules()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ensureElementRareData().setChildrenAffectedByBackwardPositionalRules(true);
}

void Element::setChildrenAffectedByPropertyBasedBackwardPositionalRules()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ensureElementRareData().setChildrenAffectedByPropertyBasedBackwardPositionalRules(true);
}

void Element::setChildIndex(unsigned index)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ElementRareData& rareData = ensureElementRareData();
    rareData.setChildIndex(index);
}

bool Element::hasFlagsSetDuringStylingOfChildren() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (childrenAffectedByHover() || childrenAffectedByFirstChildRules() || childrenAffectedByLastChildRules())
        return true;

    if (!hasRareData())
        return false;
    return rareDataStyleAffectedByActive()
        || rareDataChildrenAffectedByDrag()
        || rareDataChildrenAffectedByBackwardPositionalRules()
        || rareDataChildrenAffectedByPropertyBasedBackwardPositionalRules();
}

bool Element::rareDataStyleAffectedByEmpty() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(hasRareData());
    return elementRareData()->styleAffectedByEmpty();
}

bool Element::rareDataStyleAffectedByFocusWithin() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(hasRareData());
    return elementRareData()->styleAffectedByFocusWithin();
}

bool Element::rareDataIsNamedFlowContentElement() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(hasRareData());
    return elementRareData()->isNamedFlowContentElement();
}

bool Element::rareDataStyleAffectedByActive() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(hasRareData());
    return elementRareData()->styleAffectedByActive();
}

bool Element::rareDataChildrenAffectedByDrag() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(hasRareData());
    return elementRareData()->childrenAffectedByDrag();
}

bool Element::rareDataChildrenAffectedByBackwardPositionalRules() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(hasRareData());
    return elementRareData()->childrenAffectedByBackwardPositionalRules();
}

bool Element::rareDataChildrenAffectedByPropertyBasedBackwardPositionalRules() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(hasRareData());
    return elementRareData()->childrenAffectedByPropertyBasedBackwardPositionalRules();
}

unsigned Element::rareDataChildIndex() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(hasRareData());
    return elementRareData()->childIndex();
}

void Element::setRegionOversetState(RegionOversetState state)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ensureElementRareData().setRegionOversetState(state);
}

RegionOversetState Element::regionOversetState() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return hasRareData() ? elementRareData()->regionOversetState() : RegionUndefined;
}

AtomicString Element::computeInheritedLanguage() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (const ElementData* elementData = this->elementData()) {
        if (const Attribute* attribute = elementData->findLanguageAttribute())
            return attribute->value();
    }

    // The language property is inherited, so we iterate over the parents to find the first language.
    const Node* currentNode = this;
    while ((currentNode = currentNode->parentNode())) {
        if (is<Element>(*currentNode)) {
            if (const ElementData* elementData = downcast<Element>(*currentNode).elementData()) {
                if (const Attribute* attribute = elementData->findLanguageAttribute())
                    return attribute->value();
            }
        } else if (is<Document>(*currentNode)) {
            // checking the MIME content-language
            return downcast<Document>(*currentNode).contentLanguage();
        }
    }

    return nullAtom;
}

Locale& Element::locale() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return document().getCachedLocale(computeInheritedLanguage());
}

void Element::cancelFocusAppearanceUpdate()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (hasRareData())
        elementRareData()->setNeedsFocusAppearanceUpdateSoonAfterAttach(false);
    if (document().focusedElement() == this)
        document().cancelFocusAppearanceUpdate();
}

void Element::normalizeAttributes()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!hasAttributes())
        return;

    auto* attrNodeList = attrNodeListForElement(*this);
    if (!attrNodeList)
        return;

    // Copy the Attr Vector because Node::normalize() can fire synchronous JS
    // events (e.g. DOMSubtreeModified) and a JS listener could add / remove
    // attributes while we are iterating.
    auto copyOfAttrNodeList = *attrNodeList;
    for (auto& attrNode : copyOfAttrNodeList)
        attrNode->normalize();
}

PseudoElement* Element::beforePseudoElement() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return hasRareData() ? elementRareData()->beforePseudoElement() : nullptr;
}

PseudoElement* Element::afterPseudoElement() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return hasRareData() ? elementRareData()->afterPseudoElement() : nullptr;
}

void Element::setBeforePseudoElement(Ref<PseudoElement>&& element)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ensureElementRareData().setBeforePseudoElement(WTFMove(element));
}

void Element::setAfterPseudoElement(Ref<PseudoElement>&& element)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ensureElementRareData().setAfterPseudoElement(WTFMove(element));
}

static void disconnectPseudoElement(PseudoElement* pseudoElement)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!pseudoElement)
        return;
    if (pseudoElement->renderer())
        RenderTreeUpdater::tearDownRenderers(*pseudoElement);
    ASSERT(pseudoElement->hostElement());
    pseudoElement->clearHostElement();
}

void Element::clearBeforePseudoElement()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!hasRareData())
        return;
    disconnectPseudoElement(elementRareData()->beforePseudoElement());
    elementRareData()->setBeforePseudoElement(nullptr);
}

void Element::clearAfterPseudoElement()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!hasRareData())
        return;
    disconnectPseudoElement(elementRareData()->afterPseudoElement());
    elementRareData()->setAfterPseudoElement(nullptr);
}

bool Element::matchesValidPseudoClass() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return false;
}

bool Element::matchesInvalidPseudoClass() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return false;
}

bool Element::matchesReadWritePseudoClass() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return false;
}

bool Element::matchesIndeterminatePseudoClass() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return shouldAppearIndeterminate();
}

bool Element::matchesDefaultPseudoClass() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return false;
}

bool Element::matches(const String& selector, ExceptionCode& ec)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    SelectorQuery* selectorQuery = document().selectorQueryForString(selector, ec);
    return selectorQuery && selectorQuery->matches(*this);
}

Element* Element::closest(const String& selector, ExceptionCode& ec)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    SelectorQuery* selectorQuery = document().selectorQueryForString(selector, ec);
    if (selectorQuery)
        return selectorQuery->closest(*this);
    return nullptr;
}

bool Element::shouldAppearIndeterminate() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return false;
}

bool Element::mayCauseRepaintInsideViewport(const IntRect* visibleRect) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return renderer() && renderer()->mayCauseRepaintInsideViewport(visibleRect);
}

DOMTokenList& Element::classList()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ElementRareData& data = ensureElementRareData();
    if (!data.classList())
        data.setClassList(std::make_unique<DOMTokenList>(*this, HTMLNames::classAttr));
    return *data.classList();
}

DatasetDOMStringMap& Element::dataset()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ElementRareData& data = ensureElementRareData();
    if (!data.dataset())
        data.setDataset(std::make_unique<DatasetDOMStringMap>(*this));
    return *data.dataset();
}

URL Element::getURLAttribute(const QualifiedName& name) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
#if !ASSERT_DISABLED
    if (elementData()) {
        if (const Attribute* attribute = findAttributeByName(name))
            ASSERT(isURLAttribute(*attribute));
    }
#endif
    return document().completeURL(stripLeadingAndTrailingHTMLSpaces(getAttribute(name)));
}

URL Element::getNonEmptyURLAttribute(const QualifiedName& name) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
#if !ASSERT_DISABLED
    if (elementData()) {
        if (const Attribute* attribute = findAttributeByName(name))
            ASSERT(isURLAttribute(*attribute));
    }
#endif
    String value = stripLeadingAndTrailingHTMLSpaces(getAttribute(name));
    if (value.isEmpty())
        return URL();
    return document().completeURL(value);
}

int Element::getIntegralAttribute(const QualifiedName& attributeName) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return parseHTMLInteger(getAttribute(attributeName)).valueOr(0);
}

void Element::setIntegralAttribute(const QualifiedName& attributeName, int value)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    setAttribute(attributeName, AtomicString::number(value));
}

unsigned Element::getUnsignedIntegralAttribute(const QualifiedName& attributeName) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return parseHTMLNonNegativeInteger(getAttribute(attributeName)).valueOr(0);
}

void Element::setUnsignedIntegralAttribute(const QualifiedName& attributeName, unsigned value)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    setAttribute(attributeName, AtomicString::number(limitToOnlyHTMLNonNegative(value)));
}

#if ENABLE(INDIE_UI)
void Element::setUIActions(const AtomicString& actions)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    setAttribute(uiactionsAttr, actions);
}

const AtomicString& Element::UIActions() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return getAttribute(uiactionsAttr);
}
#endif

bool Element::childShouldCreateRenderer(const Node& child) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    // Only create renderers for SVG elements whose parents are SVG elements, or for proper <svg xmlns="svgNS"> subdocuments.
    if (child.isSVGElement()) {
        ASSERT(!isSVGElement());
        const SVGElement& childElement = downcast<SVGElement>(child);
        return is<SVGSVGElement>(childElement) && childElement.isValid();
    }
    return true;
}

#if ENABLE(FULLSCREEN_API)
void Element::webkitRequestFullscreen()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().requestFullScreenForElement(this, ALLOW_KEYBOARD_INPUT, Document::EnforceIFrameAllowFullScreenRequirement);
}

void Element::webkitRequestFullScreen(unsigned short flags)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().requestFullScreenForElement(this, (flags | LEGACY_MOZILLA_REQUEST), Document::EnforceIFrameAllowFullScreenRequirement);
}

bool Element::containsFullScreenElement() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return hasRareData() && elementRareData()->containsFullScreenElement();
}

void Element::setContainsFullScreenElement(bool flag)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ensureElementRareData().setContainsFullScreenElement(flag);
    setNeedsStyleRecalc(SyntheticStyleChange);
}

static Element* parentCrossingFrameBoundaries(Element* element)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(element);
    return element->parentElement() ? element->parentElement() : element->document().ownerElement();
}

void Element::setContainsFullScreenElementOnAncestorsCrossingFrameBoundaries(bool flag)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    Element* element = this;
    while ((element = parentCrossingFrameBoundaries(element)))
        element->setContainsFullScreenElement(flag);
}
#endif

#if ENABLE(POINTER_LOCK)
void Element::requestPointerLock()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (document().page())
        document().page()->pointerLockController().requestPointerLock(this);
}
#endif

SpellcheckAttributeState Element::spellcheckAttributeState() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    const AtomicString& value = attributeWithoutSynchronization(HTMLNames::spellcheckAttr);
    if (value.isNull())
        return SpellcheckAttributeDefault;
    if (value.isEmpty() || equalLettersIgnoringASCIICase(value, "true"))
        return SpellcheckAttributeTrue;
    if (equalLettersIgnoringASCIICase(value, "false"))
        return SpellcheckAttributeFalse;
    return SpellcheckAttributeDefault;
}

bool Element::isSpellCheckingEnabled() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    for (const Element* element = this; element; element = element->parentOrShadowHostElement()) {
        switch (element->spellcheckAttributeState()) {
        case SpellcheckAttributeTrue:
            return true;
        case SpellcheckAttributeFalse:
            return false;
        case SpellcheckAttributeDefault:
            break;
        }
    }

    return true;
}

RenderNamedFlowFragment* Element::renderNamedFlowFragment() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (renderer() && renderer()->isRenderNamedFlowFragmentContainer())
        return downcast<RenderBlockFlow>(*renderer()).renderNamedFlowFragment();

    return nullptr;
}

#if ENABLE(CSS_REGIONS)

bool Element::shouldMoveToFlowThread(const RenderStyle& styleToUse) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
#if ENABLE(FULLSCREEN_API)
    if (document().webkitIsFullScreen() && document().webkitCurrentFullScreenElement() == this)
        return false;
#endif

    if (isInShadowTree())
        return false;

    if (!styleToUse.hasFlowInto())
        return false;

    return true;
}

const AtomicString& Element::webkitRegionOverset() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    document().updateLayoutIgnorePendingStylesheets();

    static NeverDestroyed<AtomicString> undefinedState("undefined", AtomicString::ConstructFromLiteral);
    if (!renderNamedFlowFragment())
        return undefinedState;

    switch (regionOversetState()) {
    case RegionFit: {
        static NeverDestroyed<AtomicString> fitState("fit", AtomicString::ConstructFromLiteral);
        return fitState;
    }
    case RegionEmpty: {
        static NeverDestroyed<AtomicString> emptyState("empty", AtomicString::ConstructFromLiteral);
        return emptyState;
    }
    case RegionOverset: {
        static NeverDestroyed<AtomicString> overflowState("overset", AtomicString::ConstructFromLiteral);
        return overflowState;
    }
    case RegionUndefined:
        return undefinedState;
    }

    ASSERT_NOT_REACHED();
    return undefinedState;
}

Vector<RefPtr<Range>> Element::webkitGetRegionFlowRanges() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    Vector<RefPtr<Range>> rangeObjects;
    document().updateLayoutIgnorePendingStylesheets();
    auto* renderer = this->renderer();
    if (renderer && renderer->isRenderNamedFlowFragmentContainer()) {
        auto& namedFlowFragment = *downcast<RenderBlockFlow>(*renderer).renderNamedFlowFragment();
        if (namedFlowFragment.isValid())
            namedFlowFragment.getRanges(rangeObjects);
    }
    return rangeObjects;
}

#endif

#ifndef NDEBUG
bool Element::fastAttributeLookupAllowed(const QualifiedName& name) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (name == HTMLNames::styleAttr)
        return false;

    if (isSVGElement())
        return !downcast<SVGElement>(*this).isAnimatableAttribute(name);

    return true;
}
#endif

#ifdef DUMP_NODE_STATISTICS
bool Element::hasNamedNodeMap() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return hasRareData() && elementRareData()->attributeMap();
}
#endif

inline void Element::updateName(const AtomicString& oldName, const AtomicString& newName)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!isInTreeScope())
        return;

    if (oldName == newName)
        return;

    updateNameForTreeScope(treeScope(), oldName, newName);

    if (!inDocument())
        return;
    if (!is<HTMLDocument>(document()))
        return;
    updateNameForDocument(downcast<HTMLDocument>(document()), oldName, newName);
}

void Element::updateNameForTreeScope(TreeScope& scope, const AtomicString& oldName, const AtomicString& newName)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(oldName != newName);

    if (!oldName.isEmpty())
        scope.removeElementByName(*oldName.impl(), *this);
    if (!newName.isEmpty())
        scope.addElementByName(*newName.impl(), *this);
}

void Element::updateNameForDocument(HTMLDocument& document, const AtomicString& oldName, const AtomicString& newName)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(oldName != newName);

    if (WindowNameCollection::elementMatchesIfNameAttributeMatch(*this)) {
        const AtomicString& id = WindowNameCollection::elementMatchesIfIdAttributeMatch(*this) ? getIdAttribute() : nullAtom;
        if (!oldName.isEmpty() && oldName != id)
            document.removeWindowNamedItem(*oldName.impl(), *this);
        if (!newName.isEmpty() && newName != id)
            document.addWindowNamedItem(*newName.impl(), *this);
    }

    if (DocumentNameCollection::elementMatchesIfNameAttributeMatch(*this)) {
        const AtomicString& id = DocumentNameCollection::elementMatchesIfIdAttributeMatch(*this) ? getIdAttribute() : nullAtom;
        if (!oldName.isEmpty() && oldName != id)
            document.removeDocumentNamedItem(*oldName.impl(), *this);
        if (!newName.isEmpty() && newName != id)
            document.addDocumentNamedItem(*newName.impl(), *this);
    }
}

inline void Element::updateId(const AtomicString& oldId, const AtomicString& newId, NotifyObservers notifyObservers)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!isInTreeScope())
        return;

    if (oldId == newId)
        return;

    updateIdForTreeScope(treeScope(), oldId, newId, notifyObservers);

    if (!inDocument())
        return;
    if (!is<HTMLDocument>(document()))
        return;
    updateIdForDocument(downcast<HTMLDocument>(document()), oldId, newId, UpdateHTMLDocumentNamedItemMapsOnlyIfDiffersFromNameAttribute);
}

void Element::updateIdForTreeScope(TreeScope& scope, const AtomicString& oldId, const AtomicString& newId, NotifyObservers notifyObservers)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(isInTreeScope());
    ASSERT(oldId != newId);

    if (!oldId.isEmpty())
        scope.removeElementById(*oldId.impl(), *this, notifyObservers == NotifyObservers::Yes);
    if (!newId.isEmpty())
        scope.addElementById(*newId.impl(), *this, notifyObservers == NotifyObservers::Yes);
}

void Element::updateIdForDocument(HTMLDocument& document, const AtomicString& oldId, const AtomicString& newId, HTMLDocumentNamedItemMapsUpdatingCondition condition)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(inDocument());
    ASSERT(oldId != newId);

    if (WindowNameCollection::elementMatchesIfIdAttributeMatch(*this)) {
        const AtomicString& name = condition == UpdateHTMLDocumentNamedItemMapsOnlyIfDiffersFromNameAttribute && WindowNameCollection::elementMatchesIfNameAttributeMatch(*this) ? getNameAttribute() : nullAtom;
        if (!oldId.isEmpty() && oldId != name)
            document.removeWindowNamedItem(*oldId.impl(), *this);
        if (!newId.isEmpty() && newId != name)
            document.addWindowNamedItem(*newId.impl(), *this);
    }

    if (DocumentNameCollection::elementMatchesIfIdAttributeMatch(*this)) {
        const AtomicString& name = condition == UpdateHTMLDocumentNamedItemMapsOnlyIfDiffersFromNameAttribute && DocumentNameCollection::elementMatchesIfNameAttributeMatch(*this) ? getNameAttribute() : nullAtom;
        if (!oldId.isEmpty() && oldId != name)
            document.removeDocumentNamedItem(*oldId.impl(), *this);
        if (!newId.isEmpty() && newId != name)
            document.addDocumentNamedItem(*newId.impl(), *this);
    }
}

void Element::updateLabel(TreeScope& scope, const AtomicString& oldForAttributeValue, const AtomicString& newForAttributeValue)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(hasTagName(labelTag));

    if (!inDocument())
        return;

    if (oldForAttributeValue == newForAttributeValue)
        return;

    if (!oldForAttributeValue.isEmpty())
        scope.removeLabel(*oldForAttributeValue.impl(), downcast<HTMLLabelElement>(*this));
    if (!newForAttributeValue.isEmpty())
        scope.addLabel(*newForAttributeValue.impl(), downcast<HTMLLabelElement>(*this));
}

void Element::willModifyAttribute(const QualifiedName& name, const AtomicString& oldValue, const AtomicString& newValue)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (name == HTMLNames::idAttr)
        updateId(oldValue, newValue, NotifyObservers::No); // Will notify observers after the attribute is actually changed.
    else if (name == HTMLNames::nameAttr)
        updateName(oldValue, newValue);
    else if (name == HTMLNames::forAttr && hasTagName(labelTag)) {
        if (treeScope().shouldCacheLabelsByForAttribute())
            updateLabel(treeScope(), oldValue, newValue);
    }

    if (std::unique_ptr<MutationObserverInterestGroup> recipients = MutationObserverInterestGroup::createForAttributesMutation(*this, name))
        recipients->enqueueMutationRecord(MutationRecord::createAttributes(*this, name, oldValue));

    InspectorInstrumentation::willModifyDOMAttr(document(), *this, oldValue, newValue);
}

void Element::didAddAttribute(const QualifiedName& name, const AtomicString& value)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    attributeChanged(name, nullAtom, value);
    InspectorInstrumentation::didModifyDOMAttr(document(), *this, name.localName(), value);
    dispatchSubtreeModifiedEvent();
}

void Element::didModifyAttribute(const QualifiedName& name, const AtomicString& oldValue, const AtomicString& newValue)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    attributeChanged(name, oldValue, newValue);
    InspectorInstrumentation::didModifyDOMAttr(document(), *this, name.localName(), newValue);
    // Do not dispatch a DOMSubtreeModified event here; see bug 81141.
}

void Element::didRemoveAttribute(const QualifiedName& name, const AtomicString& oldValue)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    attributeChanged(name, oldValue, nullAtom);
    InspectorInstrumentation::didRemoveDOMAttr(document(), *this, name.localName());
    dispatchSubtreeModifiedEvent();
}

IntPoint Element::savedLayerScrollPosition() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return hasRareData() ? elementRareData()->savedLayerScrollPosition() : IntPoint();
}

void Element::setSavedLayerScrollPosition(const IntPoint& position)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (position.isZero() && !hasRareData())
        return;
    ensureElementRareData().setSavedLayerScrollPosition(position);
}

RefPtr<Attr> Element::attrIfExists(const AtomicString& localName, bool shouldIgnoreAttributeCase)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (auto* attrNodeList = attrNodeListForElement(*this))
        return findAttrNodeInList(*attrNodeList, localName, shouldIgnoreAttributeCase);
    return nullptr;
}

RefPtr<Attr> Element::attrIfExists(const QualifiedName& name)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (auto* attrNodeList = attrNodeListForElement(*this))
        return findAttrNodeInList(*attrNodeList, name);
    return nullptr;
}

Ref<Attr> Element::ensureAttr(const QualifiedName& name)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    auto& attrNodeList = ensureAttrNodeListForElement(*this);
    RefPtr<Attr> attrNode = findAttrNodeInList(attrNodeList, name);
    if (!attrNode) {
        attrNode = Attr::create(this, name);
        treeScope().adoptIfNeeded(attrNode.get());
        attrNodeList.append(attrNode);
    }
    return attrNode.releaseNonNull();
}

void Element::detachAttrNodeFromElementWithValue(Attr* attrNode, const AtomicString& value)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(hasSyntheticAttrChildNodes());
    attrNode->detachFromElementWithValue(value);

    auto& attrNodeList = *attrNodeListForElement(*this);
    bool found = attrNodeList.removeFirstMatching([attrNode](auto& attribute) {
        return attribute->qualifiedName() == attrNode->qualifiedName();
    });
    ASSERT_UNUSED(found, found);
    if (attrNodeList.isEmpty())
        removeAttrNodeListForElement(*this);
}

void Element::detachAllAttrNodesFromElement()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    auto* attrNodeList = attrNodeListForElement(*this);
    ASSERT(attrNodeList);

    for (const Attribute& attribute : attributesIterator()) {
        if (RefPtr<Attr> attrNode = findAttrNodeInList(*attrNodeList, attribute.name()))
            attrNode->detachFromElementWithValue(attribute.value());
    }

    removeAttrNodeListForElement(*this);
}

void Element::resetComputedStyle()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!hasRareData() || !elementRareData()->computedStyle())
        return;

    auto reset = [](Element& element) {
        if (!element.hasRareData() || !element.elementRareData()->computedStyle())
            return;
        if (element.hasCustomStyleResolveCallbacks())
            element.willResetComputedStyle();
        element.elementRareData()->resetComputedStyle();
    };
    reset(*this);
    for (auto& child : descendantsOfType<Element>(*this))
        reset(child);
}

void Element::clearStyleDerivedDataBeforeDetachingRenderer()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    unregisterNamedFlowContentElement();
    cancelFocusAppearanceUpdate();
    clearBeforePseudoElement();
    clearAfterPseudoElement();
    if (!hasRareData())
        return;
    ElementRareData* data = elementRareData();
    data->resetComputedStyle();
    data->resetDynamicRestyleObservations();
}

void Element::clearHoverAndActiveStatusBeforeDetachingRenderer()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!isUserActionElement())
        return;
    if (hovered())
        document().hoveredElementDidDetach(this);
    if (inActiveChain())
        document().elementInActiveChainDidDetach(this);
    document().userActionElements().didDetach(this);
}

bool Element::willRecalcStyle(Style::Change)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(hasCustomStyleResolveCallbacks());
    return true;
}

void Element::didRecalcStyle(Style::Change)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(hasCustomStyleResolveCallbacks());
}

void Element::willResetComputedStyle()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(hasCustomStyleResolveCallbacks());
}

void Element::willAttachRenderers()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(hasCustomStyleResolveCallbacks());
}

void Element::didAttachRenderers()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(hasCustomStyleResolveCallbacks());
}

void Element::willDetachRenderers()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(hasCustomStyleResolveCallbacks());
}

void Element::didDetachRenderers()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(hasCustomStyleResolveCallbacks());
}

Optional<ElementStyle> Element::resolveCustomStyle(const RenderStyle&, const RenderStyle*)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(hasCustomStyleResolveCallbacks());
    return Nullopt;
}

void Element::cloneAttributesFromElement(const Element& other)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (hasSyntheticAttrChildNodes())
        detachAllAttrNodesFromElement();

    other.synchronizeAllAttributes();
    if (!other.m_elementData) {
        m_elementData = nullptr;
        return;
    }

    // We can't update window and document's named item maps since the presence of image and object elements depend on other attributes and children.
    // Fortunately, those named item maps are only updated when this element is in the document, which should never be the case.
    ASSERT(!inDocument());

    const AtomicString& oldID = getIdAttribute();
    const AtomicString& newID = other.getIdAttribute();

    if (!oldID.isNull() || !newID.isNull())
        updateId(oldID, newID, NotifyObservers::No); // Will notify observers after the attribute is actually changed.

    const AtomicString& oldName = getNameAttribute();
    const AtomicString& newName = other.getNameAttribute();

    if (!oldName.isNull() || !newName.isNull())
        updateName(oldName, newName);

    // If 'other' has a mutable ElementData, convert it to an immutable one so we can share it between both elements.
    // We can only do this if there is no CSSOM wrapper for other's inline style, and there are no presentation attributes.
    if (is<UniqueElementData>(*other.m_elementData)
        && !other.m_elementData->presentationAttributeStyle()
        && (!other.m_elementData->inlineStyle() || !other.m_elementData->inlineStyle()->hasCSSOMWrapper()))
        const_cast<Element&>(other).m_elementData = downcast<UniqueElementData>(*other.m_elementData).makeShareableCopy();

    if (!other.m_elementData->isUnique())
        m_elementData = other.m_elementData;
    else
        m_elementData = other.m_elementData->makeUniqueCopy();

    for (const Attribute& attribute : attributesIterator())
        attributeChanged(attribute.name(), nullAtom, attribute.value(), ModifiedByCloning);
}

void Element::cloneDataFromElement(const Element& other)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    cloneAttributesFromElement(other);
    copyNonAttributePropertiesFromElement(other);
}

void Element::createUniqueElementData()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!m_elementData)
        m_elementData = UniqueElementData::create();
    else
        m_elementData = downcast<ShareableElementData>(*m_elementData).makeUniqueCopy();
}

bool Element::hasPendingResources() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return hasRareData() && elementRareData()->hasPendingResources();
}

void Element::setHasPendingResources()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ensureElementRareData().setHasPendingResources(true);
}

void Element::clearHasPendingResources()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ensureElementRareData().setHasPendingResources(false);
}

bool Element::canContainRangeEndPoint() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return !equalLettersIgnoringASCIICase(attributeWithoutSynchronization(roleAttr), "img");
}

String Element::completeURLsInAttributeValue(const URL& base, const Attribute& attribute) const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return URL(base, attribute.value()).string();
}

bool Element::ieForbidsInsertHTML() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    // FIXME: Supposedly IE disallows setting innerHTML, outerHTML
    // and createContextualFragment on these tags. We have no tests to
    // verify this however, so this list could be totally wrong.
    // This list was moved from the previous endTagRequirement() implementation.
    // This is also called from editing and assumed to be the list of tags
    // for which no end tag should be serialized. It's unclear if the list for
    // IE compat and the list for serialization sanity are the same.
    if (hasTagName(areaTag)
        || hasTagName(baseTag)
        || hasTagName(basefontTag)
        || hasTagName(brTag)
        || hasTagName(colTag)
        || hasTagName(embedTag)
        || hasTagName(frameTag)
        || hasTagName(hrTag)
        || hasTagName(imageTag)
        || hasTagName(imgTag)
        || hasTagName(inputTag)
        || hasTagName(isindexTag)
        || hasTagName(linkTag)
        || hasTagName(metaTag)
        || hasTagName(paramTag)
        || hasTagName(sourceTag)
        || hasTagName(wbrTag))
        return true;
    // FIXME: I'm not sure why dashboard mode would want to change the
    // serialization of <canvas>, that seems like a bad idea.
#if ENABLE(DASHBOARD_SUPPORT)
    if (hasTagName(canvasTag)) {
        Settings* settings = document().settings();
        if (settings && settings->usesDashboardBackwardCompatibilityMode())
            return true;
    }
#endif
    return false;
}

Node* Element::insertAdjacent(const String& where, Ref<Node>&& newChild, ExceptionCode& ec)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    // In Internet Explorer if the element has no parent and where is "beforeBegin" or "afterEnd",
    // a document fragment is created and the elements appended in the correct order. This document
    // fragment isn't returned anywhere.
    //
    // This is impossible for us to implement as the DOM tree does not allow for such structures,
    // Opera also appears to disallow such usage.

    if (equalLettersIgnoringASCIICase(where, "beforebegin")) {
        ContainerNode* parent = this->parentNode();
        return (parent && parent->insertBefore(newChild, this, ec)) ? newChild.ptr() : nullptr;
    }

    if (equalLettersIgnoringASCIICase(where, "afterbegin"))
        return insertBefore(newChild, firstChild(), ec) ? newChild.ptr() : nullptr;

    if (equalLettersIgnoringASCIICase(where, "beforeend"))
        return appendChild(newChild, ec) ? newChild.ptr() : nullptr;

    if (equalLettersIgnoringASCIICase(where, "afterend")) {
        ContainerNode* parent = this->parentNode();
        return (parent && parent->insertBefore(newChild, nextSibling(), ec)) ? newChild.ptr() : nullptr;
    }

    ec = SYNTAX_ERR;
    return nullptr;
}

Element* Element::insertAdjacentElement(const String& where, Element& newChild, ExceptionCode& ec)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    Node* returnValue = insertAdjacent(where, newChild, ec);
    ASSERT_WITH_SECURITY_IMPLICATION(!returnValue || is<Element>(*returnValue));
    return downcast<Element>(returnValue);
}

// Step 1 of https://w3c.github.io/DOM-Parsing/#dom-element-insertadjacenthtml.
static Element* contextElementForInsertion(const String& where, Element* element, ExceptionCode& ec)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (equalLettersIgnoringASCIICase(where, "beforebegin") || equalLettersIgnoringASCIICase(where, "afterend")) {
        auto* parent = element->parentElement();
        if (!parent) {
            ec = NO_MODIFICATION_ALLOWED_ERR;
            return nullptr;
        }
        return parent;
    }
    if (equalLettersIgnoringASCIICase(where, "afterbegin") || equalLettersIgnoringASCIICase(where, "beforeend"))
        return element;
    ec =  SYNTAX_ERR;
    return nullptr;
}

void Element::insertAdjacentHTML(const String& where, const String& markup, ExceptionCode& ec)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    Element* contextElement = contextElementForInsertion(where, this, ec);
    if (!contextElement)
        return;
    RefPtr<DocumentFragment> fragment = createFragmentForInnerOuterHTML(*contextElement, markup, AllowScriptingContent, ec);
    if (!fragment)
        return;
    insertAdjacent(where, fragment.releaseNonNull(), ec);
}

void Element::insertAdjacentText(const String& where, const String& text, ExceptionCode& ec)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    insertAdjacent(where, document().createTextNode(text), ec);
}

} // namespace WebCore
