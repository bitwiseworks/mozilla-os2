// Copyright (c) 2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process_util.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <unistd.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>

#ifdef OS_OS2
#include <process.h>
#endif

#include "base/debug_util.h"
#include "base/eintr_wrapper.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/string_tokenizer.h"
#include "base/string_util.h"

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

#if defined(ANDROID) || defined(OS_OS2)
#include <pthread.h>
/*
 * Currently, PR_DuplicateEnvironment is implemented in
 * mozglue/build/BionicGlue.cpp
 */
#define HAVE_PR_DUPLICATE_ENVIRONMENT

#include "plstr.h"
#include "prenv.h"
#include "prmem.h"
#if !defined(OS_OS2)
/* Temporary until we have PR_DuplicateEnvironment in prenv.h */
extern "C" { NSPR_API(pthread_mutex_t *)PR_GetEnvLock(void); }
#endif
#endif

namespace {

enum ParsingState {
  KEY_NAME,
  KEY_VALUE
};

static mozilla::EnvironmentLog gProcessLog("MOZ_PROCESS_LOG");

}  // namespace

namespace base {

#ifdef HAVE_PR_DUPLICATE_ENVIRONMENT
#if !defined(OS_OS2)
/*
 * I tried to put PR_DuplicateEnvironment down in mozglue, but on android
 * this winds up failing because the strdup/free calls wind up mismatching.
 */

static char **
PR_DuplicateEnvironment(void)
{
    char **result = NULL;
    char **s;
    char **d;
    pthread_mutex_lock(PR_GetEnvLock());
    for (s = environ; *s != NULL; s++)
      ;
    if ((result = (char **)PR_Malloc(sizeof(char *) * (s - environ + 1))) != NULL) {
      for (s = environ, d = result; *s != NULL; s++, d++) {
        *d = PL_strdup(*s);
      }
      *d = NULL;
    }
    pthread_mutex_unlock(PR_GetEnvLock());
    return result;
}
#endif

class EnvironmentEnvp
{
public:
#if !defined(OS_OS2)
  EnvironmentEnvp()
    : mEnvp(PR_DuplicateEnvironment()) {}
#endif

  EnvironmentEnvp(const environment_map &em)
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
      *e = PL_strdup(str.c_str());
    }
    *e = NULL;
  }

  ~EnvironmentEnvp()
  {
    if (!mEnvp) {
      return;
    }
    for (char **e = mEnvp; *e; ++e) {
      PL_strfree(*e);
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
#if !defined(OS_OS2)
  Environment()
  {
    EnvironmentEnvp envp;
    envp.ToMap(*this);
  }
#endif

  Environment(const environment_map &m) : environment_map(m)
  {
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
#endif // HAVE_PR_DUPLICATE_ENVIRONMENT

bool LaunchApp(const std::vector<std::string>& argv,
               const file_handle_mapping_vector& fds_to_remap,
               bool wait, ProcessHandle* process_handle, base::ProcessArchitecture arch) {
  return LaunchApp(argv, fds_to_remap, environment_map(),
                   wait, process_handle, arch);
}

bool LaunchApp(const std::vector<std::string>& argv,
               const file_handle_mapping_vector& fds_to_remap,
               const environment_map& env_vars_to_set,
               bool wait, ProcessHandle* process_handle,
               ProcessArchitecture arch) {
  return LaunchApp(argv, fds_to_remap, env_vars_to_set,
                   PRIVILEGES_INHERIT,
                   wait, process_handle, arch);
}

bool LaunchApp(const std::vector<std::string>& argv,
               const file_handle_mapping_vector& fds_to_remap,
               const environment_map& env_vars_to_set,
               ChildPrivileges privs,
               bool wait, ProcessHandle* process_handle,
               ProcessArchitecture arch) {
  scoped_array<char*> argv_cstr(new char*[argv.size() + 1]);
  for (size_t i = 0; i < argv.size(); i++)
    argv_cstr[i] = const_cast<char*>(argv[i].c_str());
  argv_cstr[argv.size()] = NULL;

  // Illegal to allocate memory after fork and before execvp
  InjectiveMultimap fd_shuffle1, fd_shuffle2;
  fd_shuffle1.reserve(fds_to_remap.size());
  fd_shuffle2.reserve(fds_to_remap.size());

#ifdef HAVE_PR_DUPLICATE_ENVIRONMENT
#ifdef OS_OS2
  Environment env (env_vars_to_set);
#else
  Environment env;
  env.Merge(env_vars_to_set);
#endif
  char * const *envp = env.AsEnvp();
  if (!envp) {
    DLOG(ERROR) << "FAILED to duplicate environment for: " << argv_cstr[0];
    return false;
  }
#endif

#if !defined(OS_OS2)
  pid_t pid = fork();
  if (pid < 0)
    return false;

  if (pid == 0) {
    for (file_handle_mapping_vector::const_iterator
        it = fds_to_remap.begin(); it != fds_to_remap.end(); ++it) {
      fd_shuffle1.push_back(InjectionArc(it->first, it->second, false));
      fd_shuffle2.push_back(InjectionArc(it->first, it->second, false));
    }

    if (!ShuffleFileDescriptors(&fd_shuffle1))
      _exit(127);

    CloseSuperfluousFds(fd_shuffle2);

    SetCurrentProcessPrivileges(privs);

#ifdef HAVE_PR_DUPLICATE_ENVIRONMENT
    execve(argv_cstr[0], argv_cstr.get(), envp);
#else
    for (environment_map::const_iterator it = env_vars_to_set.begin();
         it != env_vars_to_set.end(); ++it) {
      if (setenv(it->first.c_str(), it->second.c_str(), 1/*overwrite*/))
        _exit(127);
    }
    execv(argv_cstr[0], argv_cstr.get());
#endif
    // if we get here, we're in serious trouble and should complain loudly
    DLOG(ERROR) << "FAILED TO exec() CHILD PROCESS, path: " << argv_cstr[0];
    _exit(127);
  } else
#else // !defined(OS_OS2)

  // Create requested file descriptor duplicates for the child
  for (file_handle_mapping_vector::const_iterator
      it = fds_to_remap.begin(); it != fds_to_remap.end(); ++it) {
    fd_shuffle1.push_back(InjectionArc(it->first, it->second, false));
    fd_shuffle2.push_back(InjectionArc(it->first, it->second, false));
  }
  if (!ShuffleFileDescriptors(&fd_shuffle1))
    return false;

  // Prohibit inheriting all handles but the created ones
  LONG delta = 0;
  ULONG max_fds = 0;
  DosSetRelMaxFH(&delta, &max_fds);
  for (int fd = 0; fd < (LONG)max_fds; ++fd) {
    if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO)
      continue;
    InjectiveMultimap::const_iterator j;
    for (j = fd_shuffle2.begin(); j != fd_shuffle2.end(); j++) {
      if (fd == j->dest)
        break;
    }
    if (j != fd_shuffle2.end())
      continue;

    fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
  }

  pid_t pid = spawnve(P_NOWAIT, argv_cstr[0], argv_cstr.get(), envp);

  // Undo the above: reenable inheritance and close created duplicates
  for (int fd = 0; fd < (LONG)max_fds; ++fd) {
    if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO)
      continue;
    InjectiveMultimap::const_iterator j;
    for (j = fd_shuffle2.begin(); j != fd_shuffle2.end(); j++) {
      if (fd == j->dest)
        break;
    }
    if (j != fd_shuffle2.end())
      continue;

    fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) & ~FD_CLOEXEC);
  }
  for (InjectiveMultimap::const_iterator i = fd_shuffle2.begin(); i != fd_shuffle2.end(); i++) {
    HANDLE_EINTR(close(i->dest));
  }
  
  if (pid < 0) {
    return false;
  } else
#endif // !defined(OS_OS2)
  {
    gProcessLog.print("==> process %d launched child process %d\n",
                      GetCurrentProcId(), pid);
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

#if !defined(OS_OS2)
void SetCurrentProcessPrivileges(ChildPrivileges privs) {
  if (privs == PRIVILEGES_INHERIT) {
    return;
  }

  gid_t gid = CHILD_UNPRIVILEGED_GID;
  uid_t uid = CHILD_UNPRIVILEGED_UID;
#ifdef MOZ_WIDGET_GONK
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
  if (privs == PRIVILEGES_CAMERA) {
    gid_t groups[] = { AID_SDCARD_RW };
    if (setgroups(sizeof(groups) / sizeof(groups[0]), groups) != 0) {
      DLOG(ERROR) << "FAILED TO setgroups() CHILD PROCESS";
      _exit(127);
    }
  }
#endif
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
}

NamedProcessIterator::NamedProcessIterator(const std::wstring& executable_name,
                                           const ProcessFilter* filter)
    : executable_name_(executable_name), filter_(filter) {
  procfs_dir_ = opendir("/proc");
}

NamedProcessIterator::~NamedProcessIterator() {
  if (procfs_dir_) {
    closedir(procfs_dir_);
    procfs_dir_ = NULL;
  }
}

const ProcessEntry* NamedProcessIterator::NextProcessEntry() {
  bool result = false;
  do {
    result = CheckForNextProcess();
  } while (result && !IncludeEntry());

  if (result)
    return &entry_;

  return NULL;
}

bool NamedProcessIterator::CheckForNextProcess() {
  // TODO(port): skip processes owned by different UID

  dirent* slot = 0;
  const char* openparen;
  const char* closeparen;

  // Arbitrarily guess that there will never be more than 200 non-process
  // files in /proc.  Hardy has 53.
  int skipped = 0;
  const int kSkipLimit = 200;
  while (skipped < kSkipLimit) {
    slot = readdir(procfs_dir_);
    // all done looking through /proc?
    if (!slot)
      return false;

    // If not a process, keep looking for one.
    bool notprocess = false;
    int i;
    for (i = 0; i < NAME_MAX && slot->d_name[i]; ++i) {
       if (!isdigit(slot->d_name[i])) {
         notprocess = true;
         break;
       }
    }
    if (i == NAME_MAX || notprocess) {
      skipped++;
      continue;
    }

    // Read the process's status.
    char buf[NAME_MAX + 12];
    sprintf(buf, "/proc/%s/stat", slot->d_name);
    FILE *fp = fopen(buf, "r");
    if (!fp)
      return false;
    const char* result = fgets(buf, sizeof(buf), fp);
    fclose(fp);
    if (!result)
      return false;

    // Parse the status.  It is formatted like this:
    // %d (%s) %c %d ...
    // pid (name) runstate ppid
    // To avoid being fooled by names containing a closing paren, scan
    // backwards.
    openparen = strchr(buf, '(');
    closeparen = strrchr(buf, ')');
    if (!openparen || !closeparen)
      return false;
    char runstate = closeparen[2];

    // Is the process in 'Zombie' state, i.e. dead but waiting to be reaped?
    // Allowed values: D R S T Z
    if (runstate != 'Z')
      break;

    // Nope, it's a zombie; somebody isn't cleaning up after their children.
    // (e.g. WaitForProcessesToExit doesn't clean up after dead children yet.)
    // There could be a lot of zombies, can't really decrement i here.
  }
  if (skipped >= kSkipLimit) {
    NOTREACHED();
    return false;
  }

  entry_.pid = atoi(slot->d_name);
  entry_.ppid = atoi(closeparen + 3);

  // TODO(port): read pid's commandline's $0, like killall does.  Using the
  // short name between openparen and closeparen won't work for long names!
  int len = closeparen - openparen - 1;
  if (len > NAME_MAX)
    len = NAME_MAX;
  memcpy(entry_.szExeFile, openparen + 1, len);
  entry_.szExeFile[len] = 0;

  return true;
}

bool NamedProcessIterator::IncludeEntry() {
  // TODO(port): make this also work for non-ASCII filenames
  if (WideToASCII(executable_name_) != entry_.szExeFile)
    return false;
  if (!filter_)
    return true;
  return filter_->Includes(entry_.pid, entry_.ppid);
}

// To have /proc/self/io file you must enable CONFIG_TASK_IO_ACCOUNTING
// in your kernel configuration.
bool ProcessMetrics::GetIOCounters(IoCounters* io_counters) const {
  std::string proc_io_contents;
  if (!file_util::ReadFileToString(L"/proc/self/io", &proc_io_contents))
    return false;

  (*io_counters).OtherOperationCount = 0;
  (*io_counters).OtherTransferCount = 0;

  StringTokenizer tokenizer(proc_io_contents, ": \n");
  ParsingState state = KEY_NAME;
  std::string last_key_name;
  while (tokenizer.GetNext()) {
    switch (state) {
      case KEY_NAME:
        last_key_name = tokenizer.token();
        state = KEY_VALUE;
        break;
      case KEY_VALUE:
        DCHECK(!last_key_name.empty());
        if (last_key_name == "syscr") {
          (*io_counters).ReadOperationCount = StringToInt64(tokenizer.token());
        } else if (last_key_name == "syscw") {
          (*io_counters).WriteOperationCount = StringToInt64(tokenizer.token());
        } else if (last_key_name == "rchar") {
          (*io_counters).ReadTransferCount = StringToInt64(tokenizer.token());
        } else if (last_key_name == "wchar") {
          (*io_counters).WriteTransferCount = StringToInt64(tokenizer.token());
        }
        state = KEY_NAME;
        break;
    }
  }
  return true;
}
#endif // !defined(OS_OS2)

}  // namespace base
