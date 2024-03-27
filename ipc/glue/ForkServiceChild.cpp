/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ForkServiceChild.h"
#include "ForkServer.h"
#include "mozilla/Atomics.h"
#include "mozilla/Logging.h"
#include "mozilla/ipc/GeckoChildProcessHost.h"
#include "mozilla/ipc/IPDLParamTraits.h"
#include "mozilla/ipc/ProtocolMessageUtils.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/Services.h"
#include "ipc/IPCMessageUtilsSpecializations.h"
#include "nsIObserverService.h"

#include <unistd.h>
#include <fcntl.h>

namespace mozilla {
namespace ipc {

extern LazyLogModule gForkServiceLog;

mozilla::UniquePtr<ForkServiceChild> ForkServiceChild::sForkServiceChild;
Atomic<bool> ForkServiceChild::sForkServiceUsed;

#ifndef SOCK_CLOEXEC
static bool ConfigurePipeFd(int aFd) {
  int flags = fcntl(aFd, F_GETFD, 0);
  return flags != -1 && fcntl(aFd, F_SETFD, flags | FD_CLOEXEC) != -1;
}
#endif

// Create a socketpair with both ends marked as close-on-exec
static Result<Ok, LaunchError> CreateSocketPair(UniqueFileHandle& aFD0,
                                                UniqueFileHandle& aFD1,
                                                int aType = SOCK_STREAM) {
  int fds[2];
#ifdef SOCK_CLOEXEC
  const int type = aType | SOCK_CLOEXEC;
#else
  const int type = aType;
#endif

  if (socketpair(AF_UNIX, type, 0, fds) < 0) {
    return Err(LaunchError("FSC::CSP::sp", errno));
  }

#ifndef SOCK_CLOEXEC
  if (!ConfigurePipeFd(server.get()) || !ConfigurePipeFd(client.get())) {
    return Err(LaunchError("FSC::CSP::cfg", errno));
  }
#endif

  aFD0.reset(fds[0]);
  aFD1.reset(fds[1]);

  return Ok();
}

void ForkServiceChild::StartForkServer() {
  UniqueFileHandle server;
  UniqueFileHandle client;
  if (CreateSocketPair(server, client, SOCK_SEQPACKET).isErr()) {
    MOZ_LOG(gForkServiceLog, LogLevel::Error,
            ("failed to create fork server socket"));
    return;
  }

  GeckoChildProcessHost* subprocess =
      new GeckoChildProcessHost(GeckoProcessType_ForkServer, false);
  subprocess->AddFdToRemap(client.get(), ForkServer::kClientPipeFd);
  if (!subprocess->LaunchAndWaitForProcessHandle(std::vector<std::string>{})) {
    MOZ_LOG(gForkServiceLog, LogLevel::Error, ("failed to launch fork server"));
    return;
  }

  sForkServiceUsed = true;
  sForkServiceChild =
      mozilla::MakeUnique<ForkServiceChild>(server.release(), subprocess);
}

void ForkServiceChild::StopForkServer() { sForkServiceChild = nullptr; }

ForkServiceChild::ForkServiceChild(int aFd, GeckoChildProcessHost* aProcess)
    : mFailed(false), mProcess(aProcess) {
  mTcver = MakeUnique<MiniTransceiver>(aFd, SOCK_SEQPACKET);
}

ForkServiceChild::~ForkServiceChild() {
  mProcess->Destroy();
  close(mTcver->GetFD());
}

Result<Ok, LaunchError> ForkServiceChild::SendForkNewSubprocess(
    const Args& aArgs, pid_t* aPid) {
  mRecvPid = -1;

  UniqueFileHandle execParent;
  {
    UniqueFileHandle execChild;
    IPC::Message msg(MSG_ROUTING_CONTROL, Msg_ForkNewSubprocess__ID);

    MOZ_TRY(CreateSocketPair(execParent, execChild));

    IPC::MessageWriter writer(msg);
#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
    WriteIPDLParam(&writer, nullptr, aArgs.mForkFlags);
    WriteIPDLParam(&writer, nullptr, aArgs.mChroot);
#endif
    WriteIPDLParam(&writer, nullptr, std::move(execChild));
    WriteIPDLParam(&writer, nullptr, aArgs.mFdsRemap);
    if (!mTcver->Send(msg)) {
      MOZ_LOG(gForkServiceLog, LogLevel::Error,
              ("SendForkNewSubprocess: Send error"));
      OnError();
      return Err(LaunchError("FSC::SFNS::Send"));
    }
  }

  MiniTransceiver execTcver(execParent.get());
  {
    IPC::Message execMsg(MSG_ROUTING_CONTROL, Msg_SubprocessExecInfo__ID);
    IPC::MessageWriter execWriter(execMsg);
    WriteIPDLParam(&execWriter, nullptr, aArgs.mArgv);
    WriteIPDLParam(&execWriter, nullptr, aArgs.mEnv);
    if (!execTcver.Send(execMsg)) {
      // FIXME why are all of these verbose; they're actual errors.
      // (And maybe they should MOZ_ASSERT too.)
      MOZ_LOG(gForkServiceLog, LogLevel::Error,
              ("SendForkNewSubprocess: ExecInfo send error"));
      OnError();
      return Err(LaunchError("FSC::SFNS::Send2"));
    }
  }

  UniquePtr<IPC::Message> reply;
  if (!execTcver.Recv(reply)) {
    MOZ_LOG(gForkServiceLog, LogLevel::Error,
            ("SendForkNewSubprocess: Recv error"));
    OnError();
    return Err(LaunchError("FSC::SFNS::Recv"));
  }
  OnMessageReceived(std::move(reply));

  // FIXME mRecvPid can be a local variable and
  // `OnMessageReceived` should be inlined
  if (mRecvPid < 0) {
    return Err(LaunchError("FS::clone"));
  }
  MOZ_ASSERT(mRecvPid != -1);
  *aPid = mRecvPid;
  return Ok();
}

void ForkServiceChild::OnMessageReceived(UniquePtr<IPC::Message> message) {
  if (message->type() != Reply_ForkNewSubprocess__ID) {
    MOZ_LOG(gForkServiceLog, LogLevel::Verbose,
            ("unknown reply type %d", message->type()));
    return;
  }
  IPC::MessageReader reader(*message);

  if (!ReadIPDLParam(&reader, nullptr, &mRecvPid)) {
    MOZ_CRASH("Error deserializing 'pid_t'");
  }
  reader.EndRead();
}

void ForkServiceChild::OnError() {
  mFailed = true;
  ForkServerLauncher::RestartForkServer();
}

NS_IMPL_ISUPPORTS(ForkServerLauncher, nsIObserver)

bool ForkServerLauncher::mHaveStartedClient = false;
StaticRefPtr<ForkServerLauncher> ForkServerLauncher::mSingleton;

ForkServerLauncher::ForkServerLauncher() {}

ForkServerLauncher::~ForkServerLauncher() {}

already_AddRefed<ForkServerLauncher> ForkServerLauncher::Create() {
  if (mSingleton == nullptr) {
    mSingleton = new ForkServerLauncher();
  }
  RefPtr<ForkServerLauncher> launcher = mSingleton;
  return launcher.forget();
}

NS_IMETHODIMP
ForkServerLauncher::Observe(nsISupports* aSubject, const char* aTopic,
                            const char16_t* aData) {
  if (strcmp(aTopic, NS_XPCOM_STARTUP_CATEGORY) == 0) {
    nsCOMPtr<nsIObserverService> obsSvc =
        mozilla::services::GetObserverService();
    MOZ_ASSERT(obsSvc != nullptr);
    // preferences are not available until final-ui-startup
    obsSvc->AddObserver(this, "final-ui-startup", false);
  } else if (!mHaveStartedClient && strcmp(aTopic, "final-ui-startup") == 0) {
    if (StaticPrefs::dom_ipc_forkserver_enable_AtStartup()) {
      mHaveStartedClient = true;
      ForkServiceChild::StartForkServer();

      nsCOMPtr<nsIObserverService> obsSvc =
          mozilla::services::GetObserverService();
      MOZ_ASSERT(obsSvc != nullptr);
      obsSvc->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, false);
    } else {
      mSingleton = nullptr;
    }
  }

  if (strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID) == 0) {
    if (mHaveStartedClient) {
      mHaveStartedClient = false;
      ForkServiceChild::StopForkServer();
    }

    // To make leak checker happy!
    mSingleton = nullptr;
  }
  return NS_OK;
}

void ForkServerLauncher::RestartForkServer() {
  // Restart fork server
  NS_SUCCEEDED(NS_DispatchToMainThreadQueue(
      NS_NewRunnableFunction("OnForkServerError",
                             [] {
                               if (mSingleton) {
                                 ForkServiceChild::StopForkServer();
                                 ForkServiceChild::StartForkServer();
                               }
                             }),
      EventQueuePriority::Idle));
}

}  // namespace ipc
}  // namespace mozilla
