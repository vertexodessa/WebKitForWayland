/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
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
#include "ChildProcess.h"

#include "SandboxInitializationParameters.h"
#include <unistd.h>

#include <wtf/macros.h>



namespace WebKit {

ChildProcess::ChildProcess()
    : m_terminationTimeout(0)
    , m_terminationCounter(0)
    , m_terminationTimer(RunLoop::main(), this, &ChildProcess::terminationTimerFired)
    , m_processSuppressionDisabled("Process Suppression Disabled by UIProcess")
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
}

ChildProcess::~ChildProcess()
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
}

static void didCloseOnConnectionWorkQueue(IPC::Connection*)
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    // If the connection has been closed and we haven't responded in the main thread for 10 seconds
    // the process will exit forcibly.
    auto watchdogDelay = std::chrono::seconds(10);

    WorkQueue::create("com.apple.WebKit.ChildProcess.WatchDogQueue")->dispatchAfter(watchdogDelay, [] {
        // We use _exit here since the watchdog callback is called from another thread and we don't want
        // global destructors or atexit handlers to be called from this thread while the main thread is busy
        // doing its thing.
        RELEASE_LOG_ERROR("Exiting process early due to unacknowledged closed-connection");
        _exit(EXIT_FAILURE);
    });
}

void ChildProcess::initialize(const ChildProcessInitializationParameters& parameters)
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    platformInitialize();

#if PLATFORM(COCOA)
    m_priorityBoostMessage = parameters.priorityBoostMessage;
#endif

    initializeProcess(parameters);
    initializeProcessName(parameters);

    SandboxInitializationParameters sandboxParameters;
    initializeSandbox(parameters, sandboxParameters);
    
    m_connection = IPC::Connection::createClientConnection(parameters.connectionIdentifier, *this);
    m_connection->setDidCloseOnConnectionWorkQueueCallback(didCloseOnConnectionWorkQueue);
    initializeConnection(m_connection.get());
    m_connection->open();
}

void ChildProcess::setProcessSuppressionEnabled(bool enabled)
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    if (enabled)
        m_processSuppressionDisabled.stop();
    else
        m_processSuppressionDisabled.start();
}

void ChildProcess::initializeProcess(const ChildProcessInitializationParameters&)
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
}

void ChildProcess::initializeProcessName(const ChildProcessInitializationParameters&)
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
}

void ChildProcess::initializeConnection(IPC::Connection*)
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
}

void ChildProcess::addMessageReceiver(IPC::StringReference messageReceiverName, IPC::MessageReceiver& messageReceiver)
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    m_messageReceiverMap.addMessageReceiver(messageReceiverName, messageReceiver);
}

void ChildProcess::addMessageReceiver(IPC::StringReference messageReceiverName, uint64_t destinationID, IPC::MessageReceiver& messageReceiver)
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    m_messageReceiverMap.addMessageReceiver(messageReceiverName, destinationID, messageReceiver);
}

void ChildProcess::removeMessageReceiver(IPC::StringReference messageReceiverName, uint64_t destinationID)
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    m_messageReceiverMap.removeMessageReceiver(messageReceiverName, destinationID);
}

void ChildProcess::removeMessageReceiver(IPC::StringReference messageReceiverName)
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    m_messageReceiverMap.removeMessageReceiver(messageReceiverName);
}

void ChildProcess::removeMessageReceiver(IPC::MessageReceiver& messageReceiver)
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    m_messageReceiverMap.removeMessageReceiver(messageReceiver);
}

void ChildProcess::disableTermination()
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    m_terminationCounter++;
    m_terminationTimer.stop();
}

void ChildProcess::enableTermination()
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    ASSERT(m_terminationCounter > 0);
    m_terminationCounter--;

    if (m_terminationCounter)
        return;

    if (!m_terminationTimeout) {
        terminationTimerFired();
        return;
    }

    m_terminationTimer.startOneShot(m_terminationTimeout);
}

IPC::Connection* ChildProcess::messageSenderConnection()
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    return m_connection.get();
}

uint64_t ChildProcess::messageSenderDestinationID()
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    return 0;
}

void ChildProcess::terminationTimerFired()
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    if (!shouldTerminate())
        return;

    terminate();
}

void ChildProcess::stopRunLoop()
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    platformStopRunLoop();
}

#if !PLATFORM(IOS)
void ChildProcess::platformStopRunLoop()
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    RunLoop::main().stop();
}
#endif

void ChildProcess::terminate()
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    m_connection->invalidate();

    stopRunLoop();
}

void ChildProcess::shutDown()
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
    terminate();
}

#if !PLATFORM(COCOA)
void ChildProcess::platformInitialize()
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
}

void ChildProcess::initializeSandbox(const ChildProcessInitializationParameters&, SandboxInitializationParameters&)
{    WTF_AUTO_SCOPE0(__PRETTY_FUNCTION__);
}
#endif

} // namespace WebKit
