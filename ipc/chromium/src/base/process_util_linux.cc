/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=2 autoindent cindent expandtab: */
// Copyright (c) 2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process_util.h"

#include <ctype.h>
#include <fcntl.h>
#include <memory>
#include <unistd.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>

#include "base/eintr_wrapper.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/string_tokenizer.h"
#include "base/string_util.h"
#include "nsLiteralString.h"
#include "mozilla/UniquePtr.h"

#include "prenv.h"
#include "prmem.h"

#if defined(MOZ_SANDBOX) && !defined(MOZ_WIDGET_GONK)
#define USE_PID_NAMESPACE
#include <linux/sched.h>
#include <sched.h>
#include <setjmp.h>
#include "mozilla/Sandbox.h"
#endif

#ifdef MOZ_B2G_LOADER
#include "ProcessUtils.h"

using namespace mozilla::ipc;
#endif	// MOZ_B2G_LOADER

#ifdef MOZ_WIDGET_GONK
/*
 * AID_APP is the first application UID used by Android. We're using
 * it as our unprivilegied UID.  This ensure the UID used is not
 * shared with any other processes than our own childs.
 */
# include <private/android_filesystem_config.h>
# define CHILD_UNPRIVILEGED_UID AID_APP
# define CHILD_UNPRIVILEGED_GID AID_APP
#else
/*
 * On platforms that are not gonk based, we fall back to an arbitrary
 * UID. This is generally the UID for user `nobody', albeit it is not
 * always the case.
 */
# define CHILD_UNPRIVILEGED_UID 65534
# define CHILD_UNPRIVILEGED_GID 65534
#endif

namespace {

enum ParsingState {
  KEY_NAME,
  KEY_VALUE
};

static mozilla::EnvironmentLog gProcessLog("MOZ_PROCESS_LOG");

}  // namespace

namespace base {

class EnvironmentEnvp
{
public:
  EnvironmentEnvp()
    : mEnvp(PR_DuplicateEnvironment()) {}

  explicit EnvironmentEnvp(const environment_map &em)
  {
    mEnvp = (char **)PR_Malloc(sizeof(char *) * (em.size() + 1));
    if (!mEnvp) {
      return;
    }
    char **e = mEnvp;
    for (environment_map::const_iterator it = em.begin();
         it != em.end(); ++it, ++e) {
      std::string str = it->first;
      str += "=";
      str += it->second;
      size_t len = str.length() + 1;
      *e = static_cast<char*>(PR_Malloc(len));
      memcpy(*e, str.c_str(), len);
    }
    *e = NULL;
  }

  ~EnvironmentEnvp()
  {
    if (!mEnvp) {
      return;
    }
    for (char **e = mEnvp; *e; ++e) {
      PR_Free(*e);
    }
    PR_Free(mEnvp);
  }

  char * const *AsEnvp() { return mEnvp; }

  void ToMap(environment_map &em)
  {
    if (!mEnvp) {
      return;
    }
    em.clear();
    for (char **e = mEnvp; *e; ++e) {
      const char *eq;
      if ((eq = strchr(*e, '=')) != NULL) {
        std::string varname(*e, eq - *e);
        em[varname.c_str()] = &eq[1];
      }
    }
  }

private:
  char **mEnvp;
};

class Environment : public environment_map
{
public:
  Environment()
  {
    EnvironmentEnvp envp;
    envp.ToMap(*this);
  }

  char * const *AsEnvp() {
    mEnvp.reset(new EnvironmentEnvp(*this));
    return mEnvp->AsEnvp();
  }

  void Merge(const environment_map &em)
  {
    for (const_iterator it = em.begin(); it != em.end(); ++it) {
      (*this)[it->first] = it->second;
    }
  }
private:
  std::auto_ptr<EnvironmentEnvp> mEnvp;
};

bool LaunchApp(const std::vector<std::string>& argv,
               const file_handle_mapping_vector& fds_to_remap,
               bool wait, ProcessHandle* process_handle) {
  return LaunchApp(argv, fds_to_remap, environment_map(),
                   wait, process_handle);
}

bool LaunchApp(const std::vector<std::string>& argv,
               const file_handle_mapping_vector& fds_to_remap,
               const environment_map& env_vars_to_set,
               bool wait, ProcessHandle* process_handle,
               ProcessArchitecture arch) {
  return LaunchApp(argv, fds_to_remap, env_vars_to_set,
                   PRIVILEGES_INHERIT,
                   wait, process_handle);
}

#ifdef MOZ_B2G_LOADER
/**
 * Launch an app using B2g Loader.
 */
static bool
LaunchAppProcLoader(const std::vector<std::string>& argv,
                    const file_handle_mapping_vector& fds_to_remap,
                    const environment_map& env_vars_to_set,
                    ChildPrivileges privs,
                    ProcessHandle* process_handle) {
  size_t i;
  mozilla::UniquePtr<char*[]> argv_cstr(new char*[argv.size() + 1]);
  for (i = 0; i < argv.size(); i++) {
    argv_cstr[i] = const_cast<char*>(argv[i].c_str());
  }
  argv_cstr[argv.size()] = nullptr;

  mozilla::UniquePtr<char*[]> env_cstr(new char*[env_vars_to_set.size() + 1]);
  i = 0;
  for (environment_map::const_iterator it = env_vars_to_set.begin();
       it != env_vars_to_set.end(); ++it) {
    env_cstr[i++] = strdup((it->first + "=" + it->second).c_str());
  }
  env_cstr[env_vars_to_set.size()] = nullptr;

  bool ok = ProcLoaderLoad((const char **)argv_cstr.get(),
                           (const char **)env_cstr.get(),
                           fds_to_remap, privs,
                           process_handle);
  MOZ_ASSERT(ok, "ProcLoaderLoad() failed");

  for (size_t i = 0; i < env_vars_to_set.size(); i++) {
    free(env_cstr[i]);
  }

  return ok;
}

static bool
IsLaunchingNuwa(const std::vector<std::string>& argv) {
  std::vector<std::string>::const_iterator it;
  for (it = argv.begin(); it != argv.end(); ++it) {
    if (*it == std::string("-nuwa")) {
      return true;
    }
  }
  return false;
}
#endif // MOZ_B2G_LOADER

#ifdef USE_PID_NAMESPACE
namespace {

#ifdef MOZ_VALGRIND
bool IsRunningOnValgrind() {
#ifdef RUNNING_ON_VALGRIND
  return RUNNING_ON_VALGRIND;
#else
  return false;
#endif // RUNNING_ON_VALGRIND
}
#endif // MOZ_VALGRIND

// This function runs on the stack specified on the clone call. It uses longjmp
// to switch back to the original stack so the child can return from sys_clone.
int
CloneHelper(void* arg) {
  jmp_buf* env_ptr = reinterpret_cast<jmp_buf*>(arg);
  longjmp(*env_ptr, 1);

  // Should not be reached.
  MOZ_CRASH("unreachable");
  return 1;
}

// This function is noinline to ensure that stack_buf is below the stack pointer
// that is saved when setjmp is called below. This is needed because when
// compiled with FORTIFY_SOURCE, glibc's longjmp checks that the stack is moved
// upwards. See https://crbug.com/442912 for more details.
//
// Disable AddressSanitizer instrumentation for this function to make sure
// |stack_buf| is allocated on thread stack instead of ASan's fake stack.
// Under ASan longjmp() will attempt to clean up the area between the old and
// new stack pointers and print a warning that may confuse the user.
MOZ_NEVER_INLINE MOZ_ASAN_BLACKLIST pid_t
CloneAndLongjmpInChild(unsigned long flags,
                       pid_t* ptid,
                       pid_t* ctid,
                       jmp_buf* env) {
  // We use the libc clone wrapper instead of making the syscall
  // directly because making the syscall may fail to update the libc's
  // internal pid cache. The libc interface unfortunately requires
  // specifying a new stack, so we use setjmp/longjmp to emulate
  // fork-like behavior.
  char stack_buf[PTHREAD_STACK_MIN];
#if defined(ARCH_CPU_X86_FAMILY) || defined(ARCH_CPU_ARM_FAMILY) || \
    defined(ARCH_CPU_MIPS64_FAMILY) || defined(ARCH_CPU_MIPS_FAMILY)
  // The stack grows downward.
  void* stack = stack_buf + sizeof(stack_buf);
#else
#error "Unsupported architecture"
#endif // ARCH_*
  return clone(&CloneHelper, stack, flags, env, ptid, nullptr, ctid);
}

}  // anonymous namespace

pid_t ForkWithFlags(unsigned long flags,
                    pid_t* ptid = nullptr,
                    pid_t* ctid = nullptr) {
  const bool clone_tls_used = flags & CLONE_SETTLS;
  const bool invalid_ctid =
      (flags & (CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID)) && !ctid;
  const bool invalid_ptid = (flags & CLONE_PARENT_SETTID) && !ptid;

  // We do not support CLONE_VM.
  const bool clone_vm_used = flags & CLONE_VM;

  if (clone_tls_used || invalid_ctid || invalid_ptid || clone_vm_used) {
    MOZ_CRASH("Invalid usage of ForkWithFlags");
  }

#ifdef MOZ_VALGRIND
  // Valgrind's clone implementation does not support specifiying a child_stack
  // without CLONE_VM, so we cannot use libc's clone wrapper when running under
  // Valgrind. As a result, the libc pid cache may be incorrect under Valgrind.
  // See https://crbug.com/442817 for more details.
  if (IsRunningOnValgrind()) {
    // See kernel/fork.c in Linux. There is different ordering of sys_clone
    // parameters depending on CONFIG_CLONE_BACKWARDS* configuration options.
#if defined(ARCH_CPU_X86_64)
    return syscall(__NR_clone, flags, nullptr, ptid, ctid, nullptr);
#elif defined(ARCH_CPU_X86) || defined(ARCH_CPU_ARM_FAMILY) || \
    defined(ARCH_CPU_MIPS_FAMILY) || defined(ARCH_CPU_MIPS64_FAMILY)
    // CONFIG_CLONE_BACKWARDS defined.
    return syscall(__NR_clone, flags, nullptr, ptid, nullptr, ctid);
#else
#error "Unsupported architecture"
#endif // ARCH_*
  }
#endif // MOZ_VALGRIND

  jmp_buf env;
  if (setjmp(env) == 0) {
    return CloneAndLongjmpInChild(flags, ptid, ctid, &env);
  }

  return 0;
}
#endif // USE_PID_NAMESPACE

bool LaunchApp(const std::vector<std::string>& argv,
               const file_handle_mapping_vector& fds_to_remap,
               const environment_map& env_vars_to_set,
               ChildPrivileges privs,
               bool wait, ProcessHandle* process_handle,
               ProcessArchitecture arch) {
#ifdef MOZ_B2G_LOADER
  static bool beforeFirstNuwaLaunch = true;
  if (!wait && beforeFirstNuwaLaunch && IsLaunchingNuwa(argv)) {
    beforeFirstNuwaLaunch = false;
    return LaunchAppProcLoader(argv, fds_to_remap, env_vars_to_set,
                               privs, process_handle);
  }
#endif // MOZ_B2G_LOADER

  mozilla::UniquePtr<char*[]> argv_cstr(new char*[argv.size() + 1]);
  // Illegal to allocate memory after fork and before execvp
  InjectiveMultimap fd_shuffle1, fd_shuffle2;
  fd_shuffle1.reserve(fds_to_remap.size());
  fd_shuffle2.reserve(fds_to_remap.size());

  Environment env;
  env.Merge(env_vars_to_set);
  char * const *envp = env.AsEnvp();
  if (!envp) {
    DLOG(ERROR) << "FAILED to duplicate environment for: " << argv_cstr[0];
    return false;
  }

  pid_t pid = -1;
  int cloneFlags;
#ifdef USE_PID_NAMESPACE
  uid_t uid = getuid();
  gid_t gid = getgid();
  if (privs == PRIVILEGES_UNPRIVILEGED) {
    cloneFlags = CLONE_NEWUSER | CLONE_NEWPID;
    pid = ForkWithFlags(SIGCHLD | cloneFlags);
    if (pid < 0) {
      gProcessLog.print("==> failed namespace fork: %s\n", strerror(errno));
    }
  }
#endif // USE_PID_NAMESPACE
  if (pid < 0) {
    cloneFlags = 0;
    pid = fork();
  }
  if (pid < 0) {
    return false;
  }

  if (pid == 0) {
#ifdef USE_PID_NAMESPACE
    mozilla::SandboxPostFork(cloneFlags, uid, gid);
#endif // USE_PID_NAMESPACE
    for (file_handle_mapping_vector::const_iterator
        it = fds_to_remap.begin(); it != fds_to_remap.end(); ++it) {
      fd_shuffle1.push_back(InjectionArc(it->first, it->second, false));
      fd_shuffle2.push_back(InjectionArc(it->first, it->second, false));
    }

    if (!ShuffleFileDescriptors(&fd_shuffle1))
      _exit(127);

    CloseSuperfluousFds(fd_shuffle2);

    for (size_t i = 0; i < argv.size(); i++)
      argv_cstr[i] = const_cast<char*>(argv[i].c_str());
    argv_cstr[argv.size()] = NULL;

    SetCurrentProcessPrivileges(privs);

    execve(argv_cstr[0], argv_cstr.get(), envp);
    // if we get here, we're in serious trouble and should complain loudly
    // NOTE: This is async signal unsafe; it could deadlock instead.  (But
    // only on debug builds; otherwise it's a signal-safe no-op.)
    DLOG(ERROR) << "FAILED TO exec() CHILD PROCESS, path: " << argv_cstr[0];
    _exit(127);
  } else {
    gProcessLog.print("==> process %d launched child process %d (privs %d flags %x)\n",
                      GetCurrentProcId(), pid, privs, (unsigned)cloneFlags);
    if (wait)
      HANDLE_EINTR(waitpid(pid, 0, 0));

    if (process_handle)
      *process_handle = pid;
  }

  return true;
}

bool LaunchApp(const CommandLine& cl,
               bool wait, bool start_hidden,
               ProcessHandle* process_handle) {
  file_handle_mapping_vector no_files;
  return LaunchApp(cl.argv(), no_files, wait, process_handle);
}

void SetCurrentProcessPrivileges(ChildPrivileges privs) {
#ifdef MOZ_WIDGET_GONK
  if (privs == PRIVILEGES_INHERIT) {
    return;
  }

  gid_t gid = CHILD_UNPRIVILEGED_GID;
  uid_t uid = CHILD_UNPRIVILEGED_UID;
  {
    static bool checked_pix_max, pix_max_ok;
    if (!checked_pix_max) {
      checked_pix_max = true;
      int fd = open("/proc/sys/kernel/pid_max", O_CLOEXEC | O_RDONLY);
      if (fd < 0) {
        DLOG(ERROR) << "Failed to open pid_max";
        _exit(127);
      }
      char buf[PATH_MAX];
      ssize_t len = read(fd, buf, sizeof(buf) - 1);
      close(fd);
      if (len < 0) {
        DLOG(ERROR) << "Failed to read pid_max";
        _exit(127);
      }
      buf[len] = '\0';
      int pid_max = atoi(buf);
      pix_max_ok =
        (pid_max + CHILD_UNPRIVILEGED_UID > CHILD_UNPRIVILEGED_UID);
    }
    if (!pix_max_ok) {
      DLOG(ERROR) << "Can't safely get unique uid/gid";
      _exit(127);
    }
    gid += getpid();
    uid += getpid();
  }
  if (setgid(gid) != 0) {
    DLOG(ERROR) << "FAILED TO setgid() CHILD PROCESS";
    _exit(127);
  }
  if (setuid(uid) != 0) {
    DLOG(ERROR) << "FAILED TO setuid() CHILD PROCESS";
    _exit(127);
  }
  if (chdir("/") != 0)
    gProcessLog.print("==> could not chdir()\n");
#endif
}

}  // namespace base
