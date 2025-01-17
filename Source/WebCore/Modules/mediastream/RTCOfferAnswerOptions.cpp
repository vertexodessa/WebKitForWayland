/*
 * Copyright (C) 2014 Apple Inc. All rights reserved.
 * Copyright (C) 2015 Ericsson AB. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#if ENABLE(WEB_RTC)
#include "RTCOfferAnswerOptions.h"

#include <wtf/text/WTFString.h>

namespace WebCore {

RTCOfferAnswerOptions::RTCOfferAnswerOptions()
    : m_voiceActivityDetection(true)
{
}

bool RTCOfferAnswerOptions::initialize(const Dictionary& options)
{
    bool voiceActivityDetection;
    if (options.get("voiceActivityDetection", voiceActivityDetection))
        m_voiceActivityDetection = voiceActivityDetection;

    return true;
}

RefPtr<RTCOfferOptions> RTCOfferOptions::create(const Dictionary& options, ExceptionCode& ec)
{
    RefPtr<RTCOfferOptions> offerOptions = adoptRef(new RTCOfferOptions());
    if (!offerOptions->initialize(options)) {
        // FIXME: https://webkit.org/b/129800
        // According to the spec, the error is going to be defined yet, so let's use TYPE_MISMATCH_ERR for now.
        ec = TYPE_MISMATCH_ERR;
        return nullptr;
    }

    return offerOptions;
}

RTCOfferOptions::RTCOfferOptions()
    : m_offerToReceiveVideo(0)
    , m_offerToReceiveAudio(0)
    , m_iceRestart(false)
{
}

bool RTCOfferOptions::initialize(const Dictionary& options)
{
    if (options.isUndefinedOrNull())
        return true;

    String stringValue;
    int64_t intConversionResult;
    bool numberConversionSuccess;

    if (options.get("offerToReceiveVideo", stringValue)) {
        intConversionResult = stringValue.toInt64Strict(&numberConversionSuccess);
        if (!numberConversionSuccess)
            return false;

        m_offerToReceiveVideo = intConversionResult;
    }

    if (options.get("offerToReceiveAudio", stringValue)) {
        intConversionResult = stringValue.toInt64Strict(&numberConversionSuccess);
        if (!numberConversionSuccess)
            return false;

        m_offerToReceiveAudio = intConversionResult;
    }

    bool iceRestart;
    if (options.get("iceRestart", iceRestart))
        m_iceRestart = iceRestart;

    // Legacy mode
    Dictionary mandatoryOptions;
    if (options.get("mandatory", mandatoryOptions)) {
        bool boolValue;
        if (mandatoryOptions.get("OfferToReceiveAudio", boolValue) && boolValue)
            m_offerToReceiveAudio = 1;
        if (mandatoryOptions.get("OfferToReceiveVideo", boolValue) && boolValue)
            m_offerToReceiveVideo = 1;
    }

    return RTCOfferAnswerOptions::initialize(options);
}

RefPtr<RTCAnswerOptions> RTCAnswerOptions::create(const Dictionary& options, ExceptionCode& ec)
{
    RefPtr<RTCAnswerOptions> offerOptions = adoptRef(new RTCAnswerOptions());
    if (!offerOptions->initialize(options)) {
        // FIXME: https://webkit.org/b/129800
        // According to the spec, the error is going to be defined yet, so let's use TYPE_MISMATCH_ERR for now.
        ec = TYPE_MISMATCH_ERR;
        return nullptr;
    }

    return offerOptions;
}

bool RTCAnswerOptions::initialize(const Dictionary& options)
{
    if (options.isUndefinedOrNull())
        return true;

    return RTCOfferAnswerOptions::initialize(options);
}

} // namespace WebCore

#endif // ENABLE(WEB_RTC)
