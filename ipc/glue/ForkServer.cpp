/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ipc/ForkServer.h"

#include "chrome/common/chrome_switches.h"
#include "ipc/IPCMessageUtilsSpecializations.h"
#include "mozilla/BlockingResourceBase.h"
#include "mozilla/Logging.h"
#include "mozilla/Omnijar.h"
#include "mozilla/ipc/FileDescriptor.h"
#include "mozilla/ipc/IPDLParamTraits.h"
#include "mozilla/ipc/ProtocolMessageUtils.h"
#include "mozilla/ipc/SetProcessTitle.h"
#include "nsTraceRefcnt.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
#  include "mozilla/SandboxLaunch.h"
#endif

#include <algorithm>

namespace mozilla {
namespace ipc {

LazyLogModule gForkServiceLog("ForkService");

ForkServer::ForkServer() {}

/**
 * Prepare an environment for running a fork server.
 */
void ForkServer::InitProcess(int* aArgc, char*** aArgv) {
  base::InitForkServerProcess();

  mTcver = MakeUnique<MiniTransceiver>(kClientPipeFd, SOCK_SEQPACKET);
}

/**
 * Preload any resources that the forked child processes might need,
 * and which might change incompatibly or become unavailable by the
 * time they're started.  For example: the omnijar files, or certain
 * shared libraries.
 */
static void ForkServerPreload(int& aArgc, char** aArgv) {
  Omnijar::ChildProcessInit(aArgc, aArgv);
}

/**
 * Start providing the service at the IPC channel.
 */
bool ForkServer::HandleMessages() {
  while (true) {
    UniquePtr<IPC::Message> msg;
    if (!mTcver->Recv(msg)) {
      break;
    }

    OnMessageReceived(std::move(msg));

    if (mAppProcBuilder) {
      // New process - child
      return false;
    }
  }
  // Stop the server
  return true;
}

inline void PrepareArguments(std::vector<std::string>* aArgv,
                             nsTArray<nsCString>& aArgvArray) {
  for (auto& elt : aArgvArray) {
    aArgv->push_back(elt.get());
  }
}

// Prepare aOptions->env_map
inline void PrepareEnv(base::environment_map* aEnvOut,
                       nsTArray<EnvVar>& aEnvMap) {
  for (auto& elt : aEnvMap) {
    nsCString& var = std::get<0>(elt);
    nsCString& val = std::get<1>(elt);
    (*aEnvOut)[var.get()] = val.get();
  }
}

// Prepare aOptions->fds_to_remap
inline void PrepareFdsRemap(base::LaunchOptions* aOptions,
                            nsTArray<FdMapping>& aFdsRemap) {
  MOZ_LOG(gForkServiceLog, LogLevel::Verbose, ("fds mapping:"));
  for (auto& elt : aFdsRemap) {
    // FDs are duplicated here.
    int fd = std::get<0>(elt).ClonePlatformHandle().release();
    std::pair<int, int> fdmap(fd, std::get<1>(elt));
    aOptions->fds_to_remap.push_back(fdmap);
    MOZ_LOG(gForkServiceLog, LogLevel::Verbose,
            ("\t%d => %d", fdmap.first, fdmap.second));
  }
}

template <class P>
static void ReadParamInfallible(IPC::MessageReader* aReader, P* aResult,
                                const char* aCrashMessage) {
  if (!IPC::ReadParam(aReader, aResult)) {
    MOZ_CRASH_UNSAFE(aCrashMessage);
  }
}

/**
 * Parse a Message to obtain a `LaunchOptions` and the attached fd
 * that the child will use to receive its `SubprocessExecInfo`.
 */
inline bool ParseForkNewSubprocess(IPC::Message& aMsg,
                                   UniqueFileHandle* aExecFd,
                                   base::LaunchOptions* aOptions) {
  if (aMsg.type() != Msg_ForkNewSubprocess__ID) {
    MOZ_LOG(gForkServiceLog, LogLevel::Verbose,
            ("unknown message type %d (!= %d)\n", aMsg.type(),
             Msg_ForkNewSubprocess__ID));
    return false;
  }

  IPC::MessageReader reader(aMsg);
  nsTArray<FdMapping> fds_remap;

  // FIXME(jld): This should all be fallible, but that will have to
  // wait until bug 1752638 before it makes sense.
#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
  ReadParamInfallible(&reader, &aOptions->fork_flags,
                      "Error deserializing 'int'");
  ReadParamInfallible(&reader, &aOptions->sandbox_chroot,
                      "Error deserializing 'bool'");
#endif
  ReadParamInfallible(&reader, aExecFd,
                      "Error deserializing 'UniqueFileHandle'");
  ReadParamInfallible(&reader, &fds_remap, "Error deserializing 'FdMapping[]'");
  reader.EndRead();

  PrepareFdsRemap(aOptions, fds_remap);

  return true;
}

/**
 * Parse a `Message`, in the forked child process, to get the argument
 * and environment strings.
 */
inline bool ParseSubprocessExecInfo(IPC::Message& aMsg,
                                    std::vector<std::string>* aArgv,
                                    base::environment_map* aEnv) {
  if (aMsg.type() != Msg_SubprocessExecInfo__ID) {
    MOZ_LOG(gForkServiceLog, LogLevel::Verbose,
            ("unexpected message type %d (!= %d)\n", aMsg.type(),
             Msg_SubprocessExecInfo__ID));
    return false;
  }

  IPC::MessageReader reader(aMsg);
  nsTArray<nsCString> argv_array;
  nsTArray<EnvVar> env_map;

  // FIXME(jld): We may want to do something nicer than crashing,
  // given that this process doesn't have crash reporting set up yet.
  ReadParamInfallible(&reader, &argv_array,
                      "Error deserializing 'nsCString[]'");
  ReadParamInfallible(&reader, &env_map, "Error deserializing 'EnvVar[]'");
  reader.EndRead();

  PrepareArguments(aArgv, argv_array);
  PrepareEnv(aEnv, env_map);

  return true;
}

/**
 * Extract parameters from the |Message| to create a
 * |base::AppProcessBuilder| as |mAppProcBuilder|.
 *
 * It will return in both the fork server process and the new content
 * process.  |mAppProcBuilder| is null for the fork server.
 */
void ForkServer::OnMessageReceived(UniquePtr<IPC::Message> message) {
  UniqueFileHandle execFd;
  base::LaunchOptions options;
  if (!ParseForkNewSubprocess(*message, &execFd, &options)) {
    return;
  }

  MiniTransceiver execTcver(execFd.get());
  IPC::Message reply(MSG_ROUTING_CONTROL, Reply_ForkNewSubprocess__ID);
  IPC::MessageWriter writer(reply);
  auto send_reply = [&](pid_t aPid) {
    mAppProcBuilder = nullptr;
    WriteIPDLParam(&writer, nullptr, pid_t(aPid));
    execTcver.SendInfallible(reply, "failed to send a reply message");
  };

  base::ProcessHandle child_pid = -1;
  mAppProcBuilder = MakeUnique<base::AppProcessBuilder>();
  if (!mAppProcBuilder->ForkProcess(std::move(options), &child_pid)) {
    send_reply(-1);
    return;
  }
  MOZ_ASSERT(child_pid >= 0);

  if (child_pid == 0) {
    // Content process
    UniquePtr<IPC::Message> execMsg;
    if (!execTcver.Recv(execMsg)) {
      // Crashing here isn't great, because the crash reporter isn't
      // set up, but we don't have a lot of options currently.  Also,
      // receive probably won't fail unless the parent also crashes.
      printf_stderr("ForkServer: SubprocessExecInfo receive error\n");
      MOZ_CRASH();
    }

    std::vector<std::string> argv;
    base::environment_map env;
    if (!ParseSubprocessExecInfo(*execMsg, &argv, &env)) {
      printf_stderr("ForkServer: SubprocessExecInfo parse error\n");
      MOZ_CRASH();
    }
    mAppProcBuilder->SetExecInfo(std::move(argv), std::move(env));
    return;
  }

  // Fork server process
  send_reply(child_pid);
}

/**
 * Setup and run a fork server at the main thread.
 *
 * This function returns for two reasons:
 *  - the fork server is stopped normally, or
 *  - a new process is forked from the fork server and this function
 *    returned in the child, the new process.
 *
 * For the later case, aArgc and aArgv are modified to pass the
 * arguments from the chrome process.
 */
bool ForkServer::RunForkServer(int* aArgc, char*** aArgv) {
#ifdef DEBUG
  if (getenv("MOZ_FORKSERVER_WAIT_GDB")) {
    printf(
        "Waiting for 30 seconds."
        "  Attach the fork server with gdb %s %d\n",
        (*aArgv)[0], base::GetCurrentProcId());
    sleep(30);
  }
  bool sleep_newproc = !!getenv("MOZ_FORKSERVER_WAIT_GDB_NEWPROC");
#endif

  SetProcessTitleInit(*aArgv);

  // Do this before NS_LogInit() to avoid log files taking lower
  // FDs.
  ForkServer forkserver;
  forkserver.InitProcess(aArgc, aArgv);

  XRE_SetProcessType("forkserver");
  NS_LogInit();
  mozilla::LogModule::Init(0, nullptr);
  ForkServerPreload(*aArgc, *aArgv);
  MOZ_LOG(gForkServiceLog, LogLevel::Verbose, ("Start a fork server"));
  {
    DebugOnly<base::ProcessHandle> forkserver_pid = base::GetCurrentProcId();
    if (forkserver.HandleMessages()) {
      // In the fork server process
      // The server has stopped.
      MOZ_LOG(gForkServiceLog, LogLevel::Verbose,
              ("Terminate the fork server"));
      Omnijar::CleanUp();
      NS_LogTerm();
      return true;
    }
    // Now, we are running in a content process just forked from
    // the fork server process.
    MOZ_ASSERT(base::GetCurrentProcId() != forkserver_pid);
    MOZ_LOG(gForkServiceLog, LogLevel::Verbose, ("Fork a new content process"));
  }
#ifdef DEBUG
  if (sleep_newproc) {
    printf(
        "Waiting for 30 seconds."
        "  Attach the new process with gdb %s %d\n",
        (*aArgv)[0], base::GetCurrentProcId());
    sleep(30);
  }
#endif
  NS_LogTerm();

  MOZ_ASSERT(forkserver.mAppProcBuilder);
  // |messageloop| has been destroyed.  So, we can intialized the
  // process safely.  Message loops may allocates some file
  // descriptors.  If it is destroyed later, it may mess up this
  // content process by closing wrong file descriptors.
  forkserver.mAppProcBuilder->InitAppProcess(aArgc, aArgv);
  forkserver.mAppProcBuilder.reset();

  // Open log files again with right names and the new PID.
  nsTraceRefcnt::ResetLogFiles((*aArgv)[*aArgc - 1]);

  return false;
}

}  // namespace ipc
}  // namespace mozilla
