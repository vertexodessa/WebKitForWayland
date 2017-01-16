/*
 * Copyright (C) 2015 Igalia S.L.
 * Copyright (C) 2015 Metrological
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
#include "MediaPlayerPrivateHolePunch.h"

#include "MediaPlayer.h"
#include "NotImplemented.h"
#include "TimeRanges.h"

#if ENABLE(MEDIA_SOURCE)
#include "MediaSource.h"
#endif

using namespace std;

namespace WebCore {

static HashSet<String, ASCIICaseInsensitiveHash>& mimeTypeCache()
{
    static NeverDestroyed<HashSet<String, ASCIICaseInsensitiveHash>> cache;
    static bool typeListInitialized = false;

    if (typeListInitialized)
        return cache;

    const char* mimeTypes[] = {
        "video/ave"
    };

    for (unsigned i = 0; i < (sizeof(mimeTypes) / sizeof(*mimeTypes)); ++i)
        cache.get().add(String(mimeTypes[i]));

    typeListInitialized = true;
    return cache;
}

void MediaPlayerPrivateHolePunch::getSupportedTypes(HashSet<String, ASCIICaseInsensitiveHash>& types)
{
    types = mimeTypeCache();
}

MediaPlayer::SupportsType MediaPlayerPrivateHolePunch::supportsType(const MediaEngineSupportParameters& parameters)
{
    if (parameters.type.isNull() || parameters.type.isEmpty())
        return MediaPlayer::IsNotSupported;

    return mimeTypeCache().contains(parameters.type) ? MediaPlayer::IsSupported : MediaPlayer::IsNotSupported;
}

void MediaPlayerPrivateHolePunch::registerMediaEngine(MediaEngineRegistrar registrar)
{
    registrar([](MediaPlayer* player) { return std::make_unique<MediaPlayerPrivateHolePunch>(player); },
            getSupportedTypes, supportsType, 0, 0, 0,
              [](const String&, const String&) { return false; });
}

MediaPlayerPrivateHolePunch::MediaPlayerPrivateHolePunch(MediaPlayer* player)
    : MediaPlayerPrivateHolePunchBase(player)
{
}

MediaPlayerPrivateHolePunch::~MediaPlayerPrivateHolePunch()
{
}

}
