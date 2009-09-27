/*  Berkelium Implementation
 *  Root.cpp
 *
 *  Copyright (c) 2009, Patrick Reiter Horn
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of Sirikata nor the names of its contributors may
 *    be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// Berkelium headers
#include "berkelium/Platform.hpp"
#include "Root.hpp"
#include "MemoryRenderViewHost.hpp"

// Chromium headers
#include "base/message_loop.h"
#include "base/at_exit.h"
#include "base/path_service.h"
#include "base/thread.h"
#include "base/command_line.h"
#include "base/icu_util.h"
#include "net/base/cookie_monster.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/browser/browser_prefs.h"
#include "chrome/browser/browser_process_impl.h"
#include "chrome/browser/process_singleton.h"
#include "chrome/browser/profile_manager.h"
#include "chrome/browser/plugin_service.h"
#include "chrome/browser/renderer_host/browser_render_process_host.h"
#include "chrome/browser/chrome_thread.h"
#include "chrome/browser/browser_url_handler.h"
#include "app/resource_bundle.h"
#include "app/app_paths.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/logging_chrome.h"
#include "base/logging.h"
#include <signal.h>
#include <sys/stat.h>

//icu_util::Initialize()

extern "C"
void handleINT(int sig) {
    FilePath homedirpath;
    PathService::Get(chrome::DIR_USER_DATA,&homedirpath);
    FilePath child = homedirpath.AppendASCII("SingletonLock");
    unlink(child.value().c_str());
    exit(-sig);
}

AUTO_SINGLETON_INSTANCE(Berkelium::Root);
namespace Berkelium {

Root::Root (){

    {
        const char* argv[] = { "berkelium", "--browser-subprocess-path=./berkelium" };
        CommandLine::Init(2, argv);
    }

    new base::AtExitManager();

/// Temporary SingletonLock fix:
// Do not do this for child processes--they should only be initialized.
// Children should never delete the lock.
    if (signal(SIGINT, handleINT) == SIG_IGN) {
        signal(SIGINT, SIG_IGN);
    }
    if (signal(SIGHUP, handleINT) == SIG_IGN) {
        signal(SIGHUP, SIG_IGN);
    }
    if (signal(SIGTERM, handleINT) == SIG_IGN) {
        signal(SIGTERM, SIG_IGN);
    }


    chrome::RegisterPathProvider();
    app::RegisterPathProvider();
    FilePath homedirpath;
    PathService::Get(chrome::DIR_USER_DATA,&homedirpath);

    //RenderProcessHost::set_run_renderer_in_process(true);

    mProcessSingleton= new ProcessSingleton(homedirpath);
    BrowserProcess *browser_process;
    browser_process=new BrowserProcessImpl(*CommandLine::ForCurrentProcess());
    browser_process->local_state()->RegisterStringPref(prefs::kApplicationLocale, L"");
    browser_process->local_state()->RegisterBooleanPref(prefs::kMetricsReportingEnabled, false);

    assert(g_browser_process);

#ifdef OS_WIN
    logging::InitLogging(
        L"chrome.log",
        logging::LOG_TO_BOTH_FILE_AND_SYSTEM_DEBUG_LOG,
        logging::DONT_LOCK_LOG_FILE,
        logging::DELETE_OLD_LOG_FILE
        );
#else
    logging::InitLogging(
        "chrome.log",
        logging::LOG_ONLY_TO_SYSTEM_DEBUG_LOG,
        logging::DONT_LOCK_LOG_FILE,
        logging::DELETE_OLD_LOG_FILE
        );
#endif
    logging::InitChromeLogging(
        *CommandLine::ForCurrentProcess(),
        logging::DELETE_OLD_LOG_FILE);
    //APPEND_TO_OLD_LOG_FILE

    mRenderViewHostFactory = new MemoryRenderViewHostFactory;

	mMessageLoop = new MessageLoop(MessageLoop::TYPE_UI);
    mUIThread = new ChromeThread();
    
//    mNotificationService=new NotificationService();
//    ChildProcess* coreProcess=new ChildProcess;
//    coreProcess->set_main_thread(new ChildThread);

    ResourceBundle::InitSharedInstance(L"en-US");// FIXME: lookup locale
    // We only load the theme dll in the browser process.
    ResourceBundle::GetSharedInstance().LoadThemeResources();
    net::CookieMonster::EnableFileScheme();
    ProfileManager* profile_manager = browser_process->profile_manager();
    mProf = profile_manager->GetDefaultProfile(homedirpath);
    mProf->InitExtensions();
    PrefService* user_prefs = mProf->GetPrefs();
    DCHECK(user_prefs);
    
    // Now that local state and user prefs have been loaded, make the two pref
    // services aware of all our preferences.
    browser::RegisterAllPrefs(user_prefs, browser_process->local_state());

//    browser_process->local_state()->SetString(prefs::kApplicationLocale,std::wstring());
    mProcessSingleton->Create();

    BrowserURLHandler::InitURLHandlers();

    ResourceDispatcherHost *resDispatcher =
        new ResourceDispatcherHost(ChromeThread::GetMessageLoop(ChromeThread::IO));
    resDispatcher->Initialize();
    {
        char dir[L_tmpnam+1];
        tmpnam(dir);
        mkdir(dir
#ifndef OS_WIN
              ,0777
#endif
            );
        FilePath path(dir);
        PluginService::GetInstance()->SetChromePluginDataDir(path);
    }
    PluginService::GetInstance()->LoadChromePlugins(resDispatcher);

    PathService::Override(base::FILE_EXE, FilePath("./berkelium"));
}

/*
void Root::runUntilStopped() {
    MessageLoopForUI::current()->Run();
}

void Root::stopRunning() {
    MessageLoopForUI::current()->Quit();
}
*/

void Root::update() {
    MessageLoopForUI::current()->RunAllPending();
}

Root::~Root(){
    //  
    g_browser_process->profile_manager()->RemoveProfile(mProf);

    g_browser_process->EndSession();

    delete mRenderViewHostFactory;
    
    delete mProf;
    delete mUIThread;
//    delete mNotificationService;
    delete mMessageLoop;
    delete g_browser_process;
    delete mProcessSingleton;
}


}
