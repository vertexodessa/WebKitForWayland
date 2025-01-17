#include "config.h"
#include "PeerConnectionBackendQt5WebRTC.h"

#include "DOMError.h"
#include "EventNames.h"
#include "JSDOMError.h"
#include "JSRTCSessionDescription.h"
#include "JSRTCStatsResponse.h"
#include "MediaStream.h"
#include "MediaStreamCreationClient.h"
#include "MediaStreamEvent.h"
#include "MediaStreamPrivate.h"
#include "MediaStreamTrack.h"
#include "MediaStreamTrackSourcesRequestClient.h"
#include "RTCConfiguration.h"
#include "RTCDataChannelEvent.h"
#include "RTCDataChannelHandler.h"
#include "RTCDataChannelHandlerClient.h"
#include "RTCIceCandidate.h"
#include "RTCIceCandidateEvent.h"
#include "RTCOfferAnswerOptions.h"
#include "RTCRtpReceiver.h"
#include "RTCRtpSender.h"
#include "RTCRtpTransceiver.h"
#include "RTCSessionDescription.h"
#include "RTCStatsResponse.h"
#include "ScriptExecutionContext.h"
#include "UUID.h"

namespace WebCore {

using namespace PeerConnection;

static bool parseSdpTypeString(const std::string& string, RTCSessionDescription::SdpType& outType)
{
    if (string == "offer")
        outType = RTCSessionDescription::SdpType::Offer;
    else if (string == "pranswer")
        outType = RTCSessionDescription::SdpType::Pranswer;
    else if (string == "answer")
        outType = RTCSessionDescription::SdpType::Answer;
    else if (string == "rollback")
        outType = RTCSessionDescription::SdpType::Rollback;
    else
        return false;

    return true;
}

static std::string sdpTypeToString(const RTCSessionDescription::SdpType& type)
{
    switch (type) {
    case RTCSessionDescription::SdpType::Offer:
        return "offer";
    case RTCSessionDescription::SdpType::Pranswer:
        return "pranswer";
    case RTCSessionDescription::SdpType::Answer:
        return "answer";
    case RTCSessionDescription::SdpType::Rollback:
        return "rollback";
    }
    ASSERT_NOT_REACHED();
    return "";
}

static std::unique_ptr<PeerConnectionBackend> createPeerConnectionBackendQt5WebRTC(PeerConnectionBackendClient* client)
{
    WRTCInt::init();
    return std::unique_ptr<PeerConnectionBackend>(new PeerConnectionBackendQt5WebRTC(client));
}

CreatePeerConnectionBackend PeerConnectionBackend::create = createPeerConnectionBackendQt5WebRTC;

void enableQt5WebRTCPeerConnectionBackend()
{
    PeerConnectionBackend::create = createPeerConnectionBackendQt5WebRTC;
}

PeerConnectionBackendQt5WebRTC::PeerConnectionBackendQt5WebRTC(PeerConnectionBackendClient* client)
    : m_client(client)
{
    m_rtcConnection.reset(getRTCMediaSourceCenter().createPeerConnection(this));
}

void PeerConnectionBackendQt5WebRTC::createOffer(RTCOfferOptions& options, PeerConnection::SessionDescriptionPromise&& promise)
{
    ASSERT(WRTCInt::InvalidRequestId == m_sessionDescriptionRequestId);
    ASSERT(!m_sessionDescriptionPromise);

    WRTCInt::RTCOfferAnswerOptions rtcOptions;
    rtcOptions[WRTCInt::kOfferToReceiveAudio] = !!options.offerToReceiveAudio();
    rtcOptions[WRTCInt::kOfferToReceiveVideo] = !!options.offerToReceiveVideo();
    rtcOptions[WRTCInt::kIceRestart] = options.iceRestart();
    rtcOptions[WRTCInt::kVoiceActivityDetection] = options.voiceActivityDetection();

    int id = m_rtcConnection->createOffer(rtcOptions);
    if (WRTCInt::InvalidRequestId != id) {
        m_sessionDescriptionRequestId = id;
        m_sessionDescriptionPromise = WTFMove(promise);
    } else {
        promise.reject(DOMError::create("Failed to create offer"));
    }
}

void PeerConnectionBackendQt5WebRTC::createAnswer(RTCAnswerOptions& options, PeerConnection::SessionDescriptionPromise&& promise)
{
    ASSERT(WRTCInt::InvalidRequestId == m_sessionDescriptionRequestId);
    ASSERT(!m_sessionDescriptionPromise);

    WRTCInt::RTCOfferAnswerOptions rtcOptions;
    rtcOptions[WRTCInt::kVoiceActivityDetection] = options.voiceActivityDetection();

    int id = m_rtcConnection->createAnswer(rtcOptions);
    if (WRTCInt::InvalidRequestId != id) {
        m_sessionDescriptionRequestId = id;
        m_sessionDescriptionPromise = WTFMove(promise);
    } else {
        promise.reject(DOMError::create("Failed to create answer"));
    }
}

void PeerConnectionBackendQt5WebRTC::setLocalDescription(RTCSessionDescription& desc, PeerConnection::VoidPromise&& promise)
{
    ASSERT(WRTCInt::InvalidRequestId == m_voidRequestId);
    ASSERT(!m_voidPromise);

    WRTCInt::RTCSessionDescription localDesc;
    localDesc.type = sdpTypeToString(desc.type());
    localDesc.sdp = desc.sdp().utf8().data();

    int id = m_rtcConnection->setLocalDescription(localDesc);
    if (WRTCInt::InvalidRequestId != id) {
        m_voidRequestId = id;
        m_voidPromise = WTFMove(promise);
    } else {
        promise.reject(DOMError::create("Failed to parse local description"));
    }
}
RefPtr<RTCSessionDescription> PeerConnectionBackendQt5WebRTC::localDescription() const
{
    // TODO: pendingLocalDescription/currentLocalDescription
    WRTCInt::RTCSessionDescription localDesc;
    m_rtcConnection->localDescription(localDesc);

    RTCSessionDescription::SdpType sdpType;
    if (parseSdpTypeString(localDesc.type, sdpType)) {
        String sdp = localDesc.sdp.c_str();
        return RTCSessionDescription::create(sdpType, sdp);
    }
    return nullptr;
}
RefPtr<RTCSessionDescription> PeerConnectionBackendQt5WebRTC::currentLocalDescription() const
{
    return localDescription();
}
RefPtr<RTCSessionDescription> PeerConnectionBackendQt5WebRTC::pendingLocalDescription() const
{
    return RefPtr<RTCSessionDescription>();
}

void PeerConnectionBackendQt5WebRTC::setRemoteDescription(RTCSessionDescription& desc, PeerConnection::VoidPromise&& promise)
{
    ASSERT(WRTCInt::InvalidRequestId == m_voidRequestId);
    ASSERT(!m_voidPromise);

    WRTCInt::RTCSessionDescription remoteDesc;
    remoteDesc.type = sdpTypeToString(desc.type());
    remoteDesc.sdp = desc.sdp().utf8().data();

    int id = m_rtcConnection->setRemoteDescription(remoteDesc);
    if (WRTCInt::InvalidRequestId != id) {
        m_voidRequestId = id;
        m_voidPromise = WTFMove(promise);
    } else {
        promise.reject(DOMError::create("Failed to parse remote description"));
    }
}
RefPtr<RTCSessionDescription> PeerConnectionBackendQt5WebRTC::remoteDescription() const
{
    // TODO: pendingRemoteDescription/currentRemoteDescription
    WRTCInt::RTCSessionDescription remoteDesc;
    m_rtcConnection->remoteDescription(remoteDesc);

    RTCSessionDescription::SdpType sdpType;
    if (parseSdpTypeString(remoteDesc.type, sdpType)) {
        String sdp = remoteDesc.sdp.c_str();
        return RTCSessionDescription::create(sdpType, sdp);
    }
    return nullptr;
}
RefPtr<RTCSessionDescription> PeerConnectionBackendQt5WebRTC::currentRemoteDescription() const
{
    return remoteDescription();
}
RefPtr<RTCSessionDescription> PeerConnectionBackendQt5WebRTC::pendingRemoteDescription() const
{
    return RefPtr<RTCSessionDescription>();
}

void PeerConnectionBackendQt5WebRTC::setConfiguration(RTCConfiguration& config, const MediaConstraints& constraints)
{
    WRTCInt::RTCConfiguration wrtcConfig;
    for(auto& server : config.iceServers()) {
        WRTCInt::RTCIceServer wrtcICEServer;
        wrtcICEServer.credential = server->credential().utf8().data();
        wrtcICEServer.username = server->username().utf8().data();
        for(auto& url : server->urls()) {
            wrtcICEServer.urls.push_back(url.utf8().data());
        }
        wrtcConfig.iceServers.push_back(wrtcICEServer);
    }

    WRTCInt::RTCMediaConstraints wrtcConstraints;
    MediaTrackConstraintSetMap mediaConstraints = constraints.mandatoryConstraints();
    for (auto& key : mediaConstraints.keys()) {
        std::string name = key.utf8().data();
        MediaConstraintType type(mediaConstraints.get(key)->type());
        std::string value = RealtimeMediaSourceSupportedConstraints::nameForConstraint(type).string().utf8().data();
        wrtcConstraints[name] = value;
    }

    Vector<MediaTrackConstraintSetMap> advancedConstraints = constraints.advancedConstraints();
    for (const auto& constraintsVector : advancedConstraints) {
        for (auto& key : constraintsVector.keys()) {
            std::string name = key.utf8().data();
            MediaConstraintType type(mediaConstraints.get(key)->type());
            std::string value = RealtimeMediaSourceSupportedConstraints::nameForConstraint(type).string().utf8().data();
            wrtcConstraints[name] = value;
        }
    }

    m_rtcConnection->setConfiguration(wrtcConfig, wrtcConstraints);
}

void PeerConnectionBackendQt5WebRTC::addIceCandidate(RTCIceCandidate& candidate, PeerConnection::VoidPromise&& promise)
{
    WRTCInt::RTCIceCandidate iceCandidate;
    iceCandidate.sdp = candidate.candidate().utf8().data();
    iceCandidate.sdpMid = candidate.sdpMid().utf8().data();
    iceCandidate.sdpMLineIndex = candidate.sdpMLineIndex().valueOr(0);
    bool rc = m_rtcConnection->addIceCandidate(iceCandidate);
    if (rc) {
        promise.resolve(nullptr);
    } else {
        promise.reject(DOMError::create("Failed to add ICECandidate"));
    }
}

void PeerConnectionBackendQt5WebRTC::getStats(MediaStreamTrack*, PeerConnection::StatsPromise&& promise)
{
    int id = m_rtcConnection->getStats();
    if (WRTCInt::InvalidRequestId != id) {
        m_statsPromises.add(id, WTFMove(promise));
    } else {
        promise.reject(DOMError::create("Failed to get stats"));
    }
}

Vector<RefPtr<MediaStream>> PeerConnectionBackendQt5WebRTC::getRemoteStreams() const
{
    return m_remoteStreams;
}

RefPtr<RTCRtpReceiver> PeerConnectionBackendQt5WebRTC::createReceiver(const String&, const String&, const String&)
{
    return nullptr;
}

void PeerConnectionBackendQt5WebRTC::replaceTrack(RTCRtpSender&, RefPtr<MediaStreamTrack>&&, PeerConnection::VoidPromise&& promise)
{
    notImplemented();
    promise.reject(DOMError::create("NotSupportedError"));
}

void PeerConnectionBackendQt5WebRTC::stop()
{
    m_rtcConnection->stop();
}

bool PeerConnectionBackendQt5WebRTC::isNegotiationNeeded() const
{
    return m_isNegotiationNeeded;
}

void PeerConnectionBackendQt5WebRTC::markAsNeedingNegotiation()
{
    const Vector<RefPtr<RTCRtpTransceiver>>& transceivers = m_client->getTransceivers();
    for(auto &transceiver : transceivers) {
        if (!transceiver)
            continue;
        RTCRtpSender* sender = transceiver->sender();
        if (!sender || !sender->track())
            continue;
        RealtimeMediaSource& source = sender->track()->source();
        WRTCInt::RTCMediaStream* stream = static_cast<RealtimeMediaSourceQt5WebRTC&>(source).rtcStream();
        if (stream) {
            m_rtcConnection->addStream(stream);
            break;
        }
    }
}

void PeerConnectionBackendQt5WebRTC::clearNegotiationNeededState()
{
    m_isNegotiationNeeded = false;
}

std::unique_ptr<RTCDataChannelHandler> PeerConnectionBackendQt5WebRTC::createDataChannel(const String& label, const Dictionary& options)
{
    WRTCInt::DataChannelInit initData;
    String maxRetransmitsStr;
    String maxRetransmitTimeStr;
    String protocolStr;
    options.get("ordered", initData.ordered);
    options.get("negotiated", initData.negotiated);
    options.get("id", initData.id);
    options.get("maxRetransmits", maxRetransmitsStr);
    options.get("maxRetransmitTime", maxRetransmitTimeStr);
    options.get("protocol", protocolStr);
    initData.protocol = protocolStr.utf8().data();
    bool maxRetransmitsConversion;
    bool maxRetransmitTimeConversion;
    initData.maxRetransmits = maxRetransmitsStr.toUIntStrict(&maxRetransmitsConversion);
    initData.maxRetransmitTime = maxRetransmitTimeStr.toUIntStrict(&maxRetransmitTimeConversion);
    if (maxRetransmitsConversion && maxRetransmitTimeConversion) {
        return nullptr;
    }
    WRTCInt::RTCDataChannel* channel = m_rtcConnection->createDataChannel(label.utf8().data(), initData);
    return channel
        ? std::make_unique<RTCDataChannelHandlerQt5WebRTC>(channel)
        : nullptr;
}

// ===========  WRTCInt::RTCPeerConnectionClient ==========

void PeerConnectionBackendQt5WebRTC::requestSucceeded(int id, const WRTCInt::RTCSessionDescription& desc)
{
    ASSERT(id == m_sessionDescriptionRequestId);
    ASSERT(m_sessionDescriptionPromise);

    // printf("%p:%s: %d, type=%s sdp=\n%s\n", this, __func__, id, desc.type.c_str(), desc.sdp.c_str());

    RTCSessionDescription::SdpType sdpType;
    if (parseSdpTypeString(desc.type, sdpType)) {
        String sdp = desc.sdp.c_str();
        RefPtr<RTCSessionDescription> sessionDesc(RTCSessionDescription::create(sdpType, sdp));
        m_sessionDescriptionPromise->resolve(*sessionDesc);
    } else {
        m_sessionDescriptionPromise->reject(DOMError::create("Failed to parse sdp type"));
    }

    m_sessionDescriptionRequestId = WRTCInt::InvalidRequestId;
    m_sessionDescriptionPromise = WTF::Nullopt;
}

void PeerConnectionBackendQt5WebRTC::requestSucceeded(int id, const std::vector<std::unique_ptr<WRTCInt::RTCStatsReport>>& reports)
{
    Optional<PeerConnection::StatsPromise> statsPromise = m_statsPromises.take(id);
    if (!statsPromise) {
        printf("***Error: couldn't find promise for stats request: %d\n", id);
        return;
    }

    Ref<RTCStatsResponse> response = RTCStatsResponse::create();
    for(auto& r : reports)
    {
        String id = r->id().c_str();
        String type = r->type().c_str();
        double timestamp = r->timestamp();
        size_t idx = response->addReport(id, type, timestamp);
        for(auto& v : r->values())
        {
            response->addStatistic(idx, v.first.c_str(), v.second.c_str());
        }
    }

    statsPromise->resolve(WTFMove(response));
}

void PeerConnectionBackendQt5WebRTC::requestSucceeded(int id)
{
    ASSERT(id == m_voidRequestId);
    ASSERT(m_voidPromise);

    m_voidPromise->resolve(nullptr);

    m_voidRequestId = WRTCInt::InvalidRequestId;
    m_voidPromise = WTF::Nullopt;
}

void PeerConnectionBackendQt5WebRTC::requestFailed(int id, const std::string& error)
{
    if (id == m_voidRequestId) {
        ASSERT(m_voidPromise);
        ASSERT(!m_sessionDescriptionPromise);
        m_voidPromise->reject(DOMError::create(error.c_str()));
        m_voidPromise = WTF::Nullopt;
        m_voidRequestId = WRTCInt::InvalidRequestId;
    } else if (id == m_sessionDescriptionRequestId) {
        ASSERT(m_sessionDescriptionPromise);
        ASSERT(!m_voidPromise);
        m_sessionDescriptionPromise->reject(DOMError::create(error.c_str()));
        m_sessionDescriptionPromise = WTF::Nullopt;
        m_sessionDescriptionRequestId = WRTCInt::InvalidRequestId;
    } else {
        ASSERT_NOT_REACHED();
    }
}

void PeerConnectionBackendQt5WebRTC::negotiationNeeded()
{
    m_isNegotiationNeeded = true;
    m_client->scheduleNegotiationNeededEvent();
}

void PeerConnectionBackendQt5WebRTC::didAddRemoteStream(
    WRTCInt::RTCMediaStream *stream,
    const std::vector<std::string> &audioDevices,
    const std::vector<std::string> &videoDevices)
{
    ASSERT(m_client);

    std::shared_ptr<WRTCInt::RTCMediaStream> rtcStream;
    rtcStream.reset(stream);

    Vector<RefPtr<RealtimeMediaSource>> audioSources;
    Vector<RefPtr<RealtimeMediaSource>> videoSources;

    for (auto& device : audioDevices) {
        String name(device.c_str());
        String id(createCanonicalUUIDString());
        RefPtr<RealtimeMediaSourceQt5WebRTC> audioSource = adoptRef(new RealtimeAudioSourceQt5WebRTC(id, name));
        audioSource->setRTCStream(rtcStream);
        audioSources.append(audioSource.release());
    }
    for (auto& device : videoDevices) {
        String name(device.c_str());
        String id(createCanonicalUUIDString());
        RefPtr<RealtimeMediaSourceQt5WebRTC> videoSource = adoptRef(new RealtimeVideoSourceQt5WebRTC(id, name));
        videoSource->setRTCStream(rtcStream);
        videoSources.append(videoSource.release());
    }
    String id = rtcStream->id().c_str();
    RefPtr<MediaStreamPrivate> privateStream = MediaStreamPrivate::create(id, audioSources, videoSources);
    RefPtr<MediaStream> mediaStream = MediaStream::create(*m_client->scriptExecutionContext(), privateStream.copyRef());
    privateStream->startProducingData();

    m_remoteStreams.append(mediaStream);
    m_client->scriptExecutionContext()->postTask([=](ScriptExecutionContext&) {
        m_client->fireEvent(MediaStreamEvent::create(eventNames().addstreamEvent, false, false, mediaStream.copyRef()));
    });
}

void PeerConnectionBackendQt5WebRTC::didGenerateIceCandidate(const WRTCInt::RTCIceCandidate& iceCandidate)
{
    ASSERT(m_client);
    String sdp = iceCandidate.sdp.c_str();
    String sdpMid = iceCandidate.sdpMid.c_str();
    Optional<unsigned short> sdpMLineIndex = iceCandidate.sdpMLineIndex;
    RefPtr<RTCIceCandidate> candidate = RTCIceCandidate::create(sdp, sdpMid, sdpMLineIndex);
    m_client->scriptExecutionContext()->postTask([this, candidate] (ScriptExecutionContext&) {
        m_client->fireEvent(RTCIceCandidateEvent::create(false, false, candidate.copyRef()));
    });
}

void PeerConnectionBackendQt5WebRTC::didChangeSignalingState(WRTCInt::SignalingState state)
{
    ASSERT(m_client);
    PeerConnectionStates::SignalingState signalingState = PeerConnectionStates::SignalingState::Stable;
    switch(state)
    {
        case WRTCInt::Stable:
            signalingState = PeerConnectionStates::SignalingState::Stable;
            break;
        case WRTCInt::HaveLocalOffer:
            signalingState = PeerConnectionStates::SignalingState::HaveLocalOffer;
            break;
        case WRTCInt::HaveRemoteOffer:
            signalingState = PeerConnectionStates::SignalingState::HaveRemoteOffer;
            break;
        case WRTCInt::HaveLocalPrAnswer:
            signalingState = PeerConnectionStates::SignalingState::HaveLocalPrAnswer;
            break;
        case WRTCInt::HaveRemotePrAnswer:
            signalingState = PeerConnectionStates::SignalingState::HaveRemotePrAnswer;
            break;
        case WRTCInt::Closed:
            signalingState = PeerConnectionStates::SignalingState::Closed;
            break;
        default:
            return;
    }
    m_client->setSignalingState(signalingState);
}

void PeerConnectionBackendQt5WebRTC::didChangeIceGatheringState(WRTCInt::IceGatheringState state)
{
    ASSERT(m_client);
    PeerConnectionStates::IceGatheringState iceGatheringState = PeerConnectionStates::IceGatheringState::New;
    switch(state)
    {
        case WRTCInt::IceGatheringNew:
            iceGatheringState = PeerConnectionStates::IceGatheringState::New;
            break;
        case WRTCInt::IceGatheringGathering:
            iceGatheringState = PeerConnectionStates::IceGatheringState::Gathering;
            break;
        case WRTCInt::IceGatheringComplete:
            iceGatheringState = PeerConnectionStates::IceGatheringState::Complete;
            break;
        default:
            return;
    }
    m_client->updateIceGatheringState(iceGatheringState);
}

void PeerConnectionBackendQt5WebRTC::didChangeIceConnectionState(WRTCInt::IceConnectionState state)
{
    ASSERT(m_client);
    PeerConnectionStates::IceConnectionState iceConnectionState = PeerConnectionStates::IceConnectionState::New;
    switch(state)
    {
        case WRTCInt::IceConnectionNew:
            iceConnectionState = PeerConnectionStates::IceConnectionState::New;
            break;
        case WRTCInt::IceConnectionChecking:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Checking;
            break;
        case WRTCInt::IceConnectionConnected:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Connected;
            break;
        case WRTCInt::IceConnectionCompleted:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Completed;
            break;
        case WRTCInt::IceConnectionFailed:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Failed;
            break;
        case WRTCInt::IceConnectionDisconnected:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Disconnected;
            break;
        case WRTCInt::IceConnectionClosed:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Closed;
            break;
        default:
            return;
    }
    m_client->updateIceConnectionState(iceConnectionState);
}

void PeerConnectionBackendQt5WebRTC::didAddRemoteDataChannel(WRTCInt::RTCDataChannel* channel)
{
    std::unique_ptr<RTCDataChannelHandler> handler = std::make_unique<RTCDataChannelHandlerQt5WebRTC>(channel);
    RefPtr<RTCDataChannel> dataChannel = RTCDataChannel::create(m_client->scriptExecutionContext(), WTFMove(handler));
    if (!dataChannel)
        return;
    m_remoteDataChannels.append(dataChannel);
    m_client->scriptExecutionContext()->postTask([=](ScriptExecutionContext&) {
        m_client->fireEvent(RTCDataChannelEvent::create(eventNames().datachannelEvent, false, false, *dataChannel.get()));
    });
}

RTCDataChannelHandlerQt5WebRTC::RTCDataChannelHandlerQt5WebRTC(WRTCInt::RTCDataChannel* dataChannel)
    : m_rtcDataChannel(dataChannel)
    , m_client(nullptr)
{
}

void RTCDataChannelHandlerQt5WebRTC::setClient(RTCDataChannelHandlerClient* client)
{
    if (m_client == client)
        return;

    m_client = client;

    if (m_client)
        m_rtcDataChannel->setClient(this);
}

String RTCDataChannelHandlerQt5WebRTC::label()
{
    return m_rtcDataChannel->label().c_str();
}

bool RTCDataChannelHandlerQt5WebRTC::ordered()
{
    return m_rtcDataChannel->ordered();
}

unsigned short RTCDataChannelHandlerQt5WebRTC::maxRetransmitTime()
{
    return m_rtcDataChannel->maxRetransmitTime();
}

unsigned short RTCDataChannelHandlerQt5WebRTC::maxRetransmits()
{
    return m_rtcDataChannel->maxRetransmits();
}

String RTCDataChannelHandlerQt5WebRTC::protocol()
{
    return m_rtcDataChannel->protocol().c_str();
}

bool RTCDataChannelHandlerQt5WebRTC::negotiated()
{
    return m_rtcDataChannel->negotiated();
}

unsigned short RTCDataChannelHandlerQt5WebRTC::id()
{
    return m_rtcDataChannel->id();
}

unsigned long RTCDataChannelHandlerQt5WebRTC::bufferedAmount()
{
    return m_rtcDataChannel->bufferedAmount();
}

bool RTCDataChannelHandlerQt5WebRTC::sendStringData(const String& str)
{
    return m_rtcDataChannel->sendStringData(str.utf8().data());
}

bool RTCDataChannelHandlerQt5WebRTC::sendRawData(const char* data, size_t size)
{
    return m_rtcDataChannel->sendRawData(data, size);
}

void RTCDataChannelHandlerQt5WebRTC::close()
{
    m_rtcDataChannel->close();
}

void RTCDataChannelHandlerQt5WebRTC::didChangeReadyState(WRTCInt::DataChannelState state)
{
    RTCDataChannelHandlerClient::ReadyState readyState = RTCDataChannelHandlerClient::ReadyStateConnecting;
    switch(state) {
        case WRTCInt::DataChannelConnecting:
            readyState = RTCDataChannelHandlerClient::ReadyStateConnecting;
            break;
        case WRTCInt::DataChannelOpen:
            readyState = RTCDataChannelHandlerClient::ReadyStateOpen;
            break;
        case WRTCInt::DataChannelClosing:
            readyState = RTCDataChannelHandlerClient::ReadyStateClosing;
            break;
        case WRTCInt::DataChannelClosed:
            readyState = RTCDataChannelHandlerClient::ReadyStateClosed;
            break;
        default:
            break;
    };
    m_client->didChangeReadyState(readyState);
}

void RTCDataChannelHandlerQt5WebRTC::didReceiveStringData(const std::string& str)
{
    m_client->didReceiveStringData(str.c_str());
}

void RTCDataChannelHandlerQt5WebRTC::didReceiveRawData(const char* data, size_t sz)
{
    m_client->didReceiveRawData(data, sz);
}

}
