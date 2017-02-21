/*
 * Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
 * Copyright (C) 2013 Company 100, Inc. All rights reserved.
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
#include "CompositingCoordinator.h"

#if USE(COORDINATED_GRAPHICS)

#include <WebCore/DOMWindow.h>
#include <WebCore/Document.h>
#include <WebCore/FrameView.h>
#include <WebCore/GraphicsContext.h>
#include <WebCore/InspectorController.h>
#include <WebCore/MainFrame.h>
#include <WebCore/Page.h>
#include <wtf/TemporaryChange.h>
#include "Extensions3DCache.h"

#include <wtf/macros.h>

using namespace WebCore;

namespace WebKit {

CompositingCoordinator::CompositingCoordinator(Page* page, CompositingCoordinator::Client& client)
    : m_page(page)
    , m_client(client)
    , m_releaseInactiveAtlasesTimer(*this, &CompositingCoordinator::releaseInactiveAtlasesTimerFired)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
}

CompositingCoordinator::~CompositingCoordinator()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_isDestructing = true;

    purgeBackingStores();

    for (auto& registeredLayer : m_registeredLayers.values())
        registeredLayer->setCoordinator(nullptr);
}

void CompositingCoordinator::invalidate()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_rootLayer = nullptr;
    purgeBackingStores();
}

void CompositingCoordinator::setRootCompositingLayer(GraphicsLayer* graphicsLayer)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (m_rootCompositingLayer == graphicsLayer)
        return;

    if (m_rootCompositingLayer)
        m_rootCompositingLayer->removeFromParent();

    m_rootCompositingLayer = graphicsLayer;
    if (m_rootCompositingLayer)
        m_rootLayer->addChildAtIndex(m_rootCompositingLayer, 0);
}

void CompositingCoordinator::setViewOverlayRootLayer(GraphicsLayer* graphicsLayer)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (m_overlayCompositingLayer == graphicsLayer)
        return;

    if (m_overlayCompositingLayer)
        m_overlayCompositingLayer->removeFromParent();

    m_overlayCompositingLayer = graphicsLayer;
    if (m_overlayCompositingLayer)
        m_rootLayer->addChild(m_overlayCompositingLayer);
}

void CompositingCoordinator::sizeDidChange(const IntSize& newSize)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_rootLayer->setSize(newSize);
    notifyFlushRequired(m_rootLayer.get());
}

bool CompositingCoordinator::flushPendingLayerChanges()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    TemporaryChange<bool> protector(m_isFlushingLayerChanges, true);

    initializeRootCompositingLayerIfNeeded();

    bool viewportIsStable = m_page->mainFrame().view()->viewportIsStable();
    m_rootLayer->flushCompositingStateForThisLayerOnly(viewportIsStable);
    m_client.didFlushRootLayer(m_visibleContentsRect);

    if (m_overlayCompositingLayer)
        m_overlayCompositingLayer->flushCompositingState(FloatRect(FloatPoint(), m_rootLayer->size()), viewportIsStable);

    bool didSync = m_page->mainFrame().view()->flushCompositingStateIncludingSubframes();

    auto& coordinatedLayer = downcast<CoordinatedGraphicsLayer>(*m_rootLayer);
    coordinatedLayer.updateContentBuffersIncludingSubLayers();
    coordinatedLayer.syncPendingStateChangesIncludingSubLayers();

    flushPendingImageBackingChanges();

    if (m_shouldSyncFrame) {
        didSync = true;

        if (m_rootCompositingLayer) {
            m_state.contentsSize = roundedIntSize(m_rootCompositingLayer->size());
            if (CoordinatedGraphicsLayer* contentsLayer = mainContentsLayer())
                m_state.coveredRect = contentsLayer->coverRect();
        }
        m_state.scrollPosition = m_visibleContentsRect.location();

        m_client.commitSceneState(m_state);

        clearPendingStateChanges();
        m_shouldSyncFrame = false;
    }

    return didSync;
}

double CompositingCoordinator::timestamp() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    auto* document = m_page->mainFrame().document();
    if (!document)
        return 0;
    return document->domWindow() ? document->domWindow()->nowTimestamp() : document->monotonicTimestamp();
}

void CompositingCoordinator::syncDisplayState()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
#if ENABLE(REQUEST_ANIMATION_FRAME) && !USE(REQUEST_ANIMATION_FRAME_TIMER) && !USE(REQUEST_ANIMATION_FRAME_DISPLAY_MONITOR)
    // Make sure that any previously registered animation callbacks are being executed before we flush the layers.
    m_lastAnimationServiceTime = timestamp();
    m_page->mainFrame().view()->serviceScriptedAnimations();
#endif
    m_page->mainFrame().view()->updateLayoutAndStyleIfNeededRecursive();
}

#if ENABLE(REQUEST_ANIMATION_FRAME)
double CompositingCoordinator::nextAnimationServiceTime() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    // According to the requestAnimationFrame spec, rAF callbacks should not be faster than 60FPS.
    static const double MinimalTimeoutForAnimations = 1. / 60.;
    return std::max<double>(0., MinimalTimeoutForAnimations - timestamp() + m_lastAnimationServiceTime);
}
#endif

void CompositingCoordinator::clearPendingStateChanges()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_state.layersToCreate.clear();
    m_state.layersToUpdate.clear();
    m_state.layersToRemove.clear();

    m_state.imagesToCreate.clear();
    m_state.imagesToRemove.clear();
    m_state.imagesToUpdate.clear();
    m_state.imagesToClear.clear();

    m_state.updateAtlasesToCreate.clear();
    m_state.updateAtlasesToRemove.clear();
}

void CompositingCoordinator::initializeRootCompositingLayerIfNeeded()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (m_didInitializeRootCompositingLayer)
        return;

    m_state.rootCompositingLayer = downcast<CoordinatedGraphicsLayer>(*m_rootLayer).id();
    m_didInitializeRootCompositingLayer = true;
    m_shouldSyncFrame = true;
}

void CompositingCoordinator::createRootLayer(const IntSize& size)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    ASSERT(!m_rootLayer);
    // Create a root layer.
    m_rootLayer = GraphicsLayer::create(this, *this);
#ifndef NDEBUG
    m_rootLayer->setName("CompositingCoordinator root layer");
#endif
    m_rootLayer->setDrawsContent(false);
    m_rootLayer->setSize(size);
}

void CompositingCoordinator::syncLayerState(CoordinatedLayerID id, CoordinatedGraphicsLayerState& state)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_shouldSyncFrame = true;
    m_state.layersToUpdate.append(std::make_pair(id, state));
}

Ref<CoordinatedImageBacking> CompositingCoordinator::createImageBackingIfNeeded(Image* image)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    CoordinatedImageBackingID imageID = CoordinatedImageBacking::getCoordinatedImageBackingID(image);
    auto addResult = m_imageBackings.ensure(imageID, [this, image] {
        return CoordinatedImageBacking::create(this, image);
    });
    return *addResult.iterator->value;
}

void CompositingCoordinator::createImageBacking(CoordinatedImageBackingID imageID)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_state.imagesToCreate.append(imageID);
}

void CompositingCoordinator::updateImageBacking(CoordinatedImageBackingID imageID, RefPtr<CoordinatedSurface>&& coordinatedSurface)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_shouldSyncFrame = true;
    m_state.imagesToUpdate.append(std::make_pair(imageID, WTFMove(coordinatedSurface)));
}

void CompositingCoordinator::clearImageBackingContents(CoordinatedImageBackingID imageID)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_shouldSyncFrame = true;
    m_state.imagesToClear.append(imageID);
}

void CompositingCoordinator::removeImageBacking(CoordinatedImageBackingID imageID)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (m_isPurging)
        return;

    ASSERT(m_imageBackings.contains(imageID));
    m_imageBackings.remove(imageID);

    m_state.imagesToRemove.append(imageID);

    size_t imageIDPosition = m_state.imagesToClear.find(imageID);
    if (imageIDPosition != notFound)
        m_state.imagesToClear.remove(imageIDPosition);
}

void CompositingCoordinator::flushPendingImageBackingChanges()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    for (auto& imageBacking : m_imageBackings.values())
        imageBacking->update();
}

void CompositingCoordinator::notifyAnimationStarted(const GraphicsLayer*, const String&, double /* time */)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
}

void CompositingCoordinator::notifyFlushRequired(const GraphicsLayer*)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!m_isDestructing && !isFlushingLayerChanges())
        m_client.notifyFlushRequired();
}

void CompositingCoordinator::paintContents(const GraphicsLayer* graphicsLayer, GraphicsContext& graphicsContext, GraphicsLayerPaintingPhase, const FloatRect& clipRect)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_client.paintLayerContents(graphicsLayer, graphicsContext, enclosingIntRect(clipRect));
}

std::unique_ptr<GraphicsLayer> CompositingCoordinator::createGraphicsLayer(GraphicsLayer::Type layerType, GraphicsLayerClient& client)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    CoordinatedGraphicsLayer* layer = new CoordinatedGraphicsLayer(layerType, client);
    layer->setCoordinator(this);
    m_registeredLayers.add(layer->id(), layer);
    m_state.layersToCreate.append(layer->id());
    layer->setNeedsVisibleRectAdjustment();
    notifyFlushRequired(layer);
    return std::unique_ptr<GraphicsLayer>(layer);
}

float CompositingCoordinator::deviceScaleFactor() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->deviceScaleFactor();
}

float CompositingCoordinator::pageScaleFactor() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_page->pageScaleFactor();
}

void CompositingCoordinator::createUpdateAtlas(uint32_t atlasID, RefPtr<CoordinatedSurface>&& coordinatedSurface)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_state.updateAtlasesToCreate.append(std::make_pair(atlasID, WTFMove(coordinatedSurface)));
}

void CompositingCoordinator::removeUpdateAtlas(uint32_t atlasID)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (m_isPurging)
        return;
    m_state.updateAtlasesToRemove.append(atlasID);
}

FloatRect CompositingCoordinator::visibleContentsRect() const
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    return m_visibleContentsRect;
}

CoordinatedGraphicsLayer* CompositingCoordinator::mainContentsLayer()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!is<CoordinatedGraphicsLayer>(m_rootCompositingLayer))
        return nullptr;

    return downcast<CoordinatedGraphicsLayer>(*m_rootCompositingLayer).findFirstDescendantWithContentsRecursively();
}

void CompositingCoordinator::setVisibleContentsRect(const FloatRect& rect, const FloatPoint& trajectoryVector)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    // A zero trajectoryVector indicates that tiles all around the viewport are requested.
    if (CoordinatedGraphicsLayer* contentsLayer = mainContentsLayer())
        contentsLayer->setVisibleContentRectTrajectoryVector(trajectoryVector);

    bool contentsRectDidChange = rect != m_visibleContentsRect;
    if (contentsRectDidChange) {
        m_visibleContentsRect = rect;

        for (auto& registeredLayer : m_registeredLayers.values())
            registeredLayer->setNeedsVisibleRectAdjustment();
    }

    FrameView* view = m_page->mainFrame().view();
    if (view->useFixedLayout() && contentsRectDidChange) {
        // Round the rect instead of enclosing it to make sure that its size stays
        // the same while panning. This can have nasty effects on layout.
        view->setFixedVisibleContentRect(roundedIntRect(rect));
    }
}

void CompositingCoordinator::deviceOrPageScaleFactorChanged()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    m_rootLayer->deviceOrPageScaleFactorChanged();
}

void CompositingCoordinator::detachLayer(CoordinatedGraphicsLayer* layer)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (m_isPurging)
        return;

    m_registeredLayers.remove(layer->id());

    size_t index = m_state.layersToCreate.find(layer->id());
    if (index != notFound) {
        m_state.layersToCreate.remove(index);
        return;
    }

    m_state.layersToRemove.append(layer->id());
    notifyFlushRequired(layer);
}

void CompositingCoordinator::commitScrollOffset(uint32_t layerID, const WebCore::IntSize& offset)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (auto* layer = m_registeredLayers.get(layerID))
        layer->commitScrollOffset(offset);
}

void CompositingCoordinator::renderNextFrame()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    for (auto& atlas : m_updateAtlases)
        atlas->didSwapBuffers();
}

void CompositingCoordinator::purgeBackingStores()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    TemporaryChange<bool> purgingToggle(m_isPurging, true);

    for (auto& registeredLayer : m_registeredLayers.values())
        registeredLayer->purgeBackingStores();

    m_imageBackings.clear();
    m_updateAtlases.clear();
}

bool CompositingCoordinator::paintToSurface(const IntSize& size, CoordinatedSurface::Flags flags, uint32_t& atlasID, IntPoint& offset, CoordinatedSurface::Client& client)
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (Extensions3DCache::singleton().GL_EXT_unpack_subimage()) {
        for (auto& updateAtlas : m_updateAtlases) {
            UpdateAtlas* atlas = updateAtlas.get();
            if (atlas->supportsAlpha() == (flags & CoordinatedSurface::SupportsAlpha)) {
                // This will be false if there is no available buffer space.
                if (atlas->paintOnAvailableBuffer(size, atlasID, offset, client))
                    return true;
            }
        }

        static const int ScratchBufferDimension = 1024; // Must be a power of two.
        m_updateAtlases.append(std::make_unique<UpdateAtlas>(*this, IntSize(ScratchBufferDimension, ScratchBufferDimension), flags));
    } else {
        m_updateAtlases.append(std::make_unique<UpdateAtlas>(*this, size, flags));
    }

    scheduleReleaseInactiveAtlases();
    return m_updateAtlases.last()->paintOnAvailableBuffer(size, atlasID, offset, client);
}

const double ReleaseInactiveAtlasesTimerInterval = 0.5;

void CompositingCoordinator::scheduleReleaseInactiveAtlases()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    if (!m_releaseInactiveAtlasesTimer.isActive())
        m_releaseInactiveAtlasesTimer.startRepeating(ReleaseInactiveAtlasesTimerInterval);
}

void CompositingCoordinator::releaseInactiveAtlasesTimerFired()
{     AUTO_EASY_THREAD(); EASY_FUNCTION();
    // We always want to keep one atlas for root contents layer.
    std::unique_ptr<UpdateAtlas> atlasToKeepAnyway;
    bool foundActiveAtlasForRootContentsLayer = false;
    for (int i = m_updateAtlases.size() - 1;  i >= 0; --i) {
        UpdateAtlas* atlas = m_updateAtlases[i].get();
        if (!atlas->isInUse())
            atlas->addTimeInactive(ReleaseInactiveAtlasesTimerInterval);
        bool usableForRootContentsLayer = !atlas->supportsAlpha();
        if (atlas->isInactive()) {
            if (!foundActiveAtlasForRootContentsLayer && !atlasToKeepAnyway && usableForRootContentsLayer)
                atlasToKeepAnyway = WTFMove(m_updateAtlases[i]);
            m_updateAtlases.remove(i);
        } else if (usableForRootContentsLayer)
            foundActiveAtlasForRootContentsLayer = true;
    }

    if (!foundActiveAtlasForRootContentsLayer && atlasToKeepAnyway)
        m_updateAtlases.append(atlasToKeepAnyway.release());

    m_updateAtlases.shrinkToFit();

    if (m_updateAtlases.size() <= 1)
        m_releaseInactiveAtlasesTimer.stop();
}

} // namespace WebKit

#endif // USE(COORDINATED_GRAPHICS)
