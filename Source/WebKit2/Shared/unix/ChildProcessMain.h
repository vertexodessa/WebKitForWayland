/*
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

#ifndef ChildProcessMain_h
#define ChildProcessMain_h

#include "ChildProcess.h"
#include "WebKit2Initialize.h"
#include <wtf/RunLoop.h>


#include <string>
#include <sys/types.h>
#include <sys/syscall.h>
#include <fstream>
#define WTF_ENABLE
#include <wtf/macros.h>
#include <unistd.h>

#include <condition_variable>
#include <mutex>

namespace WebKit {

class ChildProcessMainBase {
public:
    virtual bool platformInitialize() { return true; }
    virtual bool parseCommandLine(int argc, char** argv);
    virtual void platformFinalize() { }

    const ChildProcessInitializationParameters& initializationParameters() const { return m_parameters; }

protected:
    ChildProcessInitializationParameters m_parameters;
};


extern std::mutex m;
extern std::condition_variable cv;

void dumpTraces(int);
void watchThread();

template<typename ChildProcessType, typename ChildProcessMainType>
int ChildProcessMain(int argc, char** argv)
{

    EASY_PROFILER_ENABLE;
    EASY_MAIN_THREAD;
    profiler::startListen();


    AUTO_EASY_THREAD(); EASY_FUNCTION();
    ChildProcessMainType childMain;

    InitializeWebKit2();

    if (!childMain.platformInitialize())
        return EXIT_FAILURE;

    if (!childMain.parseCommandLine(argc, argv))
        return EXIT_FAILURE;

    ChildProcessType::singleton().initialize(childMain.initializationParameters());

    int sig = SIGUSR2;
    fprintf(stderr, "III: installing signal handler (%d)!\n", sig);
    signal(sig, dumpTraces);

    std::thread t(watchThread);
    t.detach();

    RunLoop::run();
    childMain.platformFinalize();

    return EXIT_SUCCESS;
}

} // namespace WebKit

#endif // ChildProcessMain_h
