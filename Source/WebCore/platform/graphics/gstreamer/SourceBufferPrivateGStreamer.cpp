/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 * Copyright (C) 2013 Orange
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
 * Copyright (C) 2015, 2016 Metrological Group B.V.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "SourceBufferPrivateGStreamer.h"

#if ENABLE(MEDIA_SOURCE) && USE(GSTREAMER)

#include "ContentType.h"
#include "GStreamerUtilities.h"
#include "MediaPlayerPrivateGStreamerMSE.h"
#include "MediaSample.h"
#include "MediaSourceGStreamer.h"
#include "NotImplemented.h"
#include "WebKitMediaSourceGStreamer.h"

namespace WebCore {

Ref<SourceBufferPrivateGStreamer> SourceBufferPrivateGStreamer::create(MediaSourceGStreamer* mediaSource, PassRefPtr<MediaSourceClientGStreamerMSE> client, const ContentType& contentType)
{
    return adoptRef(*new SourceBufferPrivateGStreamer(mediaSource, client, contentType));
}

SourceBufferPrivateGStreamer::SourceBufferPrivateGStreamer(MediaSourceGStreamer* mediaSource, PassRefPtr<MediaSourceClientGStreamerMSE> client, const ContentType& contentType)
    : SourceBufferPrivate()
    , m_mediaSource(mediaSource)
    , m_type(contentType)
    , m_client(client)
    , m_isReadyForMoreSamples(true)
    , m_notifyWhenReadyForMoreSamples(false)
{
}

SourceBufferPrivateGStreamer::~SourceBufferPrivateGStreamer()
{
}

void SourceBufferPrivateGStreamer::setClient(SourceBufferPrivateClient* client)
{
    m_sourceBufferPrivateClient = client;
}

void SourceBufferPrivateGStreamer::append(const unsigned char* data, unsigned length)
{
    ASSERT(m_mediaSource);

    if (!m_sourceBufferPrivateClient)
        return;

    if (m_client->append(this, data, length))
        return;

    m_sourceBufferPrivateClient->sourceBufferPrivateAppendComplete(this, SourceBufferPrivateClient::ReadStreamFailed);
}

void SourceBufferPrivateGStreamer::abort()
{
    if (m_client)
        m_client->abort(this);
}

void SourceBufferPrivateGStreamer::removedFromMediaSource()
{
    if (m_mediaSource)
        m_mediaSource->removeSourceBuffer(this);
    if (m_client)
        m_client->removedFromMediaSource(this);
}

MediaPlayer::ReadyState SourceBufferPrivateGStreamer::readyState() const
{
    return m_mediaSource->readyState();
}

void SourceBufferPrivateGStreamer::setReadyState(MediaPlayer::ReadyState state)
{
    m_mediaSource->setReadyState(state);
}

void SourceBufferPrivateGStreamer::flushAndEnqueueNonDisplayingSamples(Vector<RefPtr<MediaSample>> samples, AtomicString)
{
    if (m_client)
        m_client->flushAndEnqueueNonDisplayingSamples(samples);
}

void SourceBufferPrivateGStreamer::enqueueSample(PassRefPtr<MediaSample> prpSample, AtomicString)
{
    if(m_notifyWhenReadyForMoreSamples != false)
        LOG(Media, "%p GStreamer Disabling notification about sample readiness!", this);
    m_notifyWhenReadyForMoreSamples = false;

    RefPtr<MediaSample> sample = prpSample;
    if (m_client)
        m_client->enqueueSample(sample);
}

bool SourceBufferPrivateGStreamer::isReadyForMoreSamples(AtomicString)
{
    return m_isReadyForMoreSamples;
}

void SourceBufferPrivateGStreamer::setReadyForMoreSamples(bool isReady)
{
    ASSERT(WTF::isMainThread());
    if (m_isReadyForMoreSamples != isReady)
        LOG(Media, "%p GStreamer is now %s for more samples!", this, isReady ? "READY" : "NOT READY");
    m_isReadyForMoreSamples = isReady;
}

void SourceBufferPrivateGStreamer::notifyReadyForMoreSamples()
{
    ASSERT(WTF::isMainThread());
    setReadyForMoreSamples(true);
    if (m_notifyWhenReadyForMoreSamples)
        m_sourceBufferPrivateClient->sourceBufferPrivateDidBecomeReadyForMoreSamples(this, m_trackId);
}

void SourceBufferPrivateGStreamer::setActive(bool isActive)
{
    if (m_mediaSource)
        m_mediaSource->sourceBufferPrivateDidChangeActiveState(this, isActive);
}

void SourceBufferPrivateGStreamer::stopAskingForMoreSamples(AtomicString)
{
    notImplemented();
}

void SourceBufferPrivateGStreamer::notifyClientWhenReadyForMoreSamples(AtomicString trackId)
{
    ASSERT(WTF::isMainThread());
    if (m_notifyWhenReadyForMoreSamples != true)
        LOG(Media, "%p GStreamer enabling notification about sample readiness!", this);

    m_notifyWhenReadyForMoreSamples = true;
    m_trackId = trackId;
}

#if ENABLE(VIDEO_TRACK)
void SourceBufferPrivateGStreamer::didReceiveInitializationSegment(const SourceBufferPrivateClient::InitializationSegment& initializationSegment)
{
    if (m_sourceBufferPrivateClient)
        m_sourceBufferPrivateClient->sourceBufferPrivateDidReceiveInitializationSegment(this, initializationSegment);
}

void SourceBufferPrivateGStreamer::didReceiveSample(PassRefPtr<MediaSample> prpSample)
{
    RefPtr<MediaSample> sample = prpSample;
    if (m_sourceBufferPrivateClient)
        m_sourceBufferPrivateClient->sourceBufferPrivateDidReceiveSample(this, *sample);
}

void SourceBufferPrivateGStreamer::didReceiveAllPendingSamples()
{
    if (m_sourceBufferPrivateClient)
        m_sourceBufferPrivateClient->sourceBufferPrivateAppendComplete(this, SourceBufferPrivateClient::AppendSucceeded);
}
#endif

double SourceBufferPrivateGStreamer::timestampOffset() const
{
    if (!m_sourceBufferPrivateClient)
        return 0;

    return m_sourceBufferPrivateClient->timestampOffset();
}

}
#endif
