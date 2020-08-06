/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <errno.h>
#include <fcntl.h>
#include <mutex>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "base/eintr_wrapper.h"
#include "base/logging.h"
#include "base/message_loop.h"
#include "base/process_util.h"
#include "mozilla/DataMutex.h"
#include "mozilla/StaticPtr.h"
#include "nsTArray.h"

#include "chrome/common/process_watcher.h"

#ifdef MOZ_ENABLE_FORKSERVER
#  include "mozilla/ipc/ForkServiceChild.h"
#endif

// FIXME: reword this to acknowledge the hack
//
// If the processes aren't direct children then this can't be
// implemented correctly without help from their actual parent.
// In particular, we can't use `kill(pid, 0)` to test for process
// existence, because the pid could have been reused by an unrelated
// process.  The kill and waitpid operations will need to be remoted
// to the fork server first.

// The basic idea here is a minimal SIGCHLD handler which writes to a
// pipe and a libevent callback on the I/O thread which fires when the
// other end becomes readable.  When we start waiting for process
// termination we check if it's terminated immediately, and otherwise
// register it to be checked later when SIGCHLD fires.
//
// Making this more complicated is that we usually want to kill the
// process after a timeout, in case it hangs trying to exit, but not
// if it's already exited by that point.  But we also support waiting
// indefinitely, for debug/CI use cases like refcount logging, and in
// that case we want to block parent process shutdown until all
// children exit.

// Maximum amount of time (in milliseconds) to wait for the process to exit.
// XXX/cjones: fairly arbitrary, chosen to match process_watcher_win.cc
static const int kMaxWaitMs = 2000;

namespace {

// A runnable which handles killing child processes after a timeout.
class DelayedKill;

// Represents a child process being awaited (which is expected to exit
// soon, or already has).
//
// If `mForce` is null then we will wait indefinitely (and block
// parent shutdown; see above); otherwise it will be killed after a
// timeout (or during parent shutdown, if that happens first).
struct PendingChild {
  pid_t mPid;
  RefPtr<DelayedKill> mForce;
};

// `EnsureProcessTerminated` is called when a process is expected to
// be shutting down, so there should be relatively few `PendingChild`
// instances at any given time, meaning that using an array and doing
// O(n) operations should be fine.
static mozilla::StaticDataMutex<mozilla::StaticAutoPtr<nsTArray<PendingChild>>>
    gPendingChildren("ProcessWatcher::gPendingChildren");
static int gSignalPipe[2] = {-1, -1};

enum class BlockingWait { NO, YES };

#ifdef MOZ_ENABLE_FORKSERVER
// With the current design of the fork server we can't waitpid
// directly, and trying to simulate it by polling with `kill(pid, 0)`
// is unreliable because pids can be reused, so we could think the
// process is still running when it's exited.
//
// This waitpid substitute uses signal 0 for the nonblocking case
// (which risks SIGKILLing an unrelated process, but trying to "fix"
// that is more work than just fixing the fork server), and in the
// blocking case polls a limited number of times (so that at least the
// hang/jank is bounded).
static pid_t FakeWaitpid(pid_t pid, int* wstatus, int options) {
  static constexpr long kDelayMS = 500;
  static constexpr int kAttempts = 10;

  if (options & ~WNOHANG) {
    errno = EINVAL;
    return -1;
  }

  // We can't get the actual exit status, so pretend everything is fine.
  static constexpr int kZero = 0;  // Unfortunately, macros.
  static_assert(WIFEXITED(kZero));
  static_assert(WEXITSTATUS(kZero) == 0);
  if (wstatus) {
    *wstatus = 0;
  }

  for (int attempt = 0; attempt < kAttempts; ++attempt) {
    int rv = kill(pid, 0);
    if (rv == 0) {
      // Process is still running (or its pid was reassigned; oops).
      if (options & WNOHANG) {
        return 0;
      }
    } else {
      if (errno == ESRCH) {
        // Process presumably exited.
        return pid;
      }
      // Some other error (permissions, if it's the wrong process?).
      return rv;
    }

    // Wait and try again.
    struct timespec delay = {(kDelayMS / 1000),
                             (kDelayMS % 1000) * 1000 * 1000};
    HANDLE_EINTR(nanosleep(&delay, &delay));
  }

  errno = ETIME;  // "Timer expired"; close enough.
  return -1;
}
#endif

// A convenient wrapper for `waitpid`; returns true if the child
// process has exited.
static bool WaitForProcess(pid_t pid, BlockingWait aBlock) {
  int wstatus;
  int flags = aBlock == BlockingWait::NO ? WNOHANG : 0;

  pid_t (*waitpidImpl)(pid_t, int*, int) = waitpid;
#ifdef MOZ_ENABLE_FORKSERVER
  if (mozilla::ipc::ForkServiceChild::Get()) {
    waitpidImpl = FakeWaitpid;
  }
#endif

  pid_t rv = HANDLE_EINTR(waitpidImpl(pid, &wstatus, flags));
  if (rv < 0) {
    // Shouldn't happen, but maybe the pid was incorrect (not a child
    // of this process), or maybe some other code already waited for
    // it.  This can be caused by issues like bug 227246, but also
    // because of the fork server.
    CHROMIUM_LOG(ERROR) << "waitpid failed (pid " << pid
                        << "): " << strerror(errno);
    return true;
  }

  if (rv == 0) {
    MOZ_ASSERT(aBlock == BlockingWait::NO);
    return false;
  }

  if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) != 0) {
    CHROMIUM_LOG(WARNING) << "process " << pid << " exited with status "
                          << WEXITSTATUS(wstatus);
  } else if (WIFSIGNALED(wstatus)) {
    CHROMIUM_LOG(WARNING) << "process " << pid << " exited on signal "
                          << WTERMSIG(wstatus);
  }
  return true;
}

class DelayedKill final : public mozilla::Runnable {
 public:
  explicit DelayedKill(pid_t aPid)
      : mozilla::Runnable("ProcessWatcher::DelayedKill"), mPid(aPid) {}

  virtual ~DelayedKill() = default;

  NS_IMETHOD Run() override {
    KillProcess();
    return NS_OK;
  }

  friend class ProcessCleaner;

 private:
  // Protected by gPendingChildren.Lock; probably unnecessary because
  // this is posted to the I/O thread and Disarm() is called from
  // ProcessCleaner also on the I/O thread, but locking is easier than
  // asserting the current thread and this isn't a fast path.
  pid_t mPid;

  void KillProcess() {
    auto lock = gPendingChildren.Lock();
    if (mPid == 0) {
      return;
    }
    if (kill(mPid, SIGKILL) != 0) {
      CHROMIUM_LOG(ERROR) << "failed to send SIGKILL to process " << mPid;
    }
    Disarm();
  }

  void Disarm() {
    // Rather than adding complexity to cancel the runnable, just
    // modify it so it does nothing.
    mPid = 0;
  }
};

// Most of the logic is here.  Reponds to SIGCHLD via the self-pipe,
// and handles shutdown behavior in `WillDestroyCurrentMessageLoop`.
// There is one instance of this class; it's created the first time
// it's used and destroys itself during IPC shutdown.
class ProcessCleaner final : public MessageLoopForIO::Watcher,
                             public MessageLoop::DestructionObserver {
 public:
  void Register(MessageLoopForIO* loop) {
    loop->AddDestructionObserver(this);
    loop->WatchFileDescriptor(gSignalPipe[0], /* persistent= */ true,
                              MessageLoopForIO::WATCH_READ, &mWatcher, this);
  }

  void OnFileCanReadWithoutBlocking(int fd) override {
    DCHECK(fd == gSignalPipe[0]);
    ssize_t rv;
    // Drain the pipe and prune dead processes.
    do {
      char msg;
      rv = HANDLE_EINTR(read(gSignalPipe[0], &msg, 1));
      CHECK(rv != 0);
      if (rv < 0) {
        DCHECK(errno == EAGAIN || errno == EWOULDBLOCK);
      } else {
        DCHECK(msg == 0);
      }
    } while (rv > 0);
    PruneDeadProcesses();
  }

  void OnFileCanWriteWithoutBlocking(int fd) override {
    CHROMIUM_LOG(FATAL) << "unreachable";
  }

  void WillDestroyCurrentMessageLoop() override {
    mWatcher.StopWatchingFileDescriptor();
    auto lock = gPendingChildren.Lock();
    auto& children = lock.ref();
    if (children) {
      for (const auto& child : *children) {
        // If the child still has force-termination pending, do that now.
        if (child.mForce) {
          if (kill(child.mPid, SIGKILL) != 0) {
            CHROMIUM_LOG(ERROR)
                << "failed to send SIGKILL to process " << child.mPid;
            continue;
          }
        } else {
          CHROMIUM_LOG(WARNING)
              << "Waiting in WillDestroyCurrentMessageLoop for pid "
              << child.mPid;
        }
        // If the process was just killed, it should exit immediately;
        // otherwise, block until it exits on its own.
        WaitForProcess(child.mPid, BlockingWait::YES);
      }
      children = nullptr;
    }
    delete this;
  }

 private:
  MessageLoopForIO::FileDescriptorWatcher mWatcher;

  static void PruneDeadProcesses() {
    auto lock = gPendingChildren.Lock();
    auto& children = lock.ref();
    if (!children || children->IsEmpty()) {
      return;
    }
    nsTArray<PendingChild> live;
    for (const auto& child : *children) {
      if (WaitForProcess(child.mPid, BlockingWait::NO)) {
        if (child.mForce) {
          child.mForce->Disarm();
        }
      } else {
        live.AppendElement(child);
      }
    }
    *children = std::move(live);
  }
};

static void HandleSigChld(int signum) {
  DCHECK(signum == SIGCHLD);
  char msg = 0;
  HANDLE_EINTR(write(gSignalPipe[1], &msg, 1));
  // Can't log here if this fails (at least not normally; SafeSPrintf
  // from security/sandbox/chromium could be used).
}

static void ProcessWatcherInit() {
  int rv;

  rv = pipe(gSignalPipe);
  CHECK(rv == 0)
  << "pipe() failed";
  rv = fcntl(gSignalPipe[0], F_SETFL, O_NONBLOCK);
  CHECK(rv == 0)
  << "O_NONBLOCK failed";

  // Currently there are no other SIGCHLD handlers; this is debug
  // asserted.  If the situation changes, it should be relatively
  // simple to delegate; note that this ProcessWatcher doesn't
  // interfere with child processes it hasn't been asked to handle.
  auto oldHandler = signal(SIGCHLD, HandleSigChld);
  CHECK(oldHandler != SIG_ERR);
  DCHECK(oldHandler == SIG_DFL);

  ProcessCleaner* pc = new ProcessCleaner();
  pc->Register(MessageLoopForIO::current());
}

}  // namespace

/**
 * Do everything possible to ensure that |process| has been reaped
 * before this process exits.
 *
 * |force| decides how strict to be with the child's shutdown.
 *
 *                | child exit timeout | upon parent shutdown:
 *                +--------------------+----------------------------------
 *   force=true   | 2 seconds          | kill(child, SIGKILL)
 *   force=false  | infinite           | waitpid(child)
 *
 * If a child process doesn't shut down properly, and |force=false|
 * used, then the parent will wait on the child forever.  So,
 * |force=false| is expected to be used when an external entity can be
 * responsible for terminating hung processes, e.g. automated test
 * harnesses.
 */
void ProcessWatcher::EnsureProcessTerminated(base::ProcessHandle process,
                                             bool force) {
  DCHECK(process != base::GetCurrentProcId());
  DCHECK(process > 0);

  static std::once_flag sInited;
  std::call_once(sInited, ProcessWatcherInit);

  auto lock = gPendingChildren.Lock();
  auto& children = lock.ref();

  // Check if the process already exited.  This needs to happen under
  // the `gPendingChildren` lock to prevent this sequence:
  //
  // A1. this non-blocking wait fails
  // B1. the process exits
  // B2. SIGCHLD is handled
  // B3. the ProcessCleaner wakes up and drains the signal pipe
  // A2. the process is added to `gPendingChildren`
  //
  // Holding the lock prevents B3 from occurring between A1 and A2.
  if (WaitForProcess(process, BlockingWait::NO)) {
    return;
  }

  if (!children) {
    children = new nsTArray<PendingChild>();
  }
  // Check for duplicate pids.  This is safe even in corner cases with
  // pid reuse: the pid can't be reused by the OS until the zombie
  // process has been waited, and both the `waitpid` and the following
  // removal of the `PendingChild` object occur while continually
  // holding the lock, which is also held here.
  for (const auto& child : *children) {
    if (child.mPid == process) {
      MOZ_ASSERT(false,
                 "EnsureProcessTerminated must be called at most once for a "
                 "given process");
      return;
    }
  }

  PendingChild child{};
  child.mPid = process;
  if (force) {
    MessageLoopForIO* loop = MessageLoopForIO::current();
    auto reaper = mozilla::MakeRefPtr<DelayedKill>(process);
    child.mForce = reaper;
    loop->PostDelayedTask(reaper.forget(), kMaxWaitMs);
  }
  children->AppendElement(std::move(child));
}
