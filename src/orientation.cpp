#include "ankink/orientation.hpp"

#include <cerrno>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

namespace ankink {
namespace {

bool run_orientation_command(const std::vector<std::string> &arguments,
                             std::string &error) {
  std::vector<char *> argv;
  argv.reserve(arguments.size() + 1);
  for (const auto &argument : arguments)
    argv.push_back(const_cast<char *>(argument.c_str()));
  argv.push_back(nullptr);

  const pid_t child = ::fork();
  if (child < 0) {
    error = std::strerror(errno);
    return false;
  }
  if (child == 0) {
    ::execv(argv[0], argv.data());
    ::_exit(127);
  }

  int status = 0;
  while (::waitpid(child, &status, 0) < 0) {
    if (errno == EINTR)
      continue;
    error = std::strerror(errno);
    return false;
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    error = "The Kindle window manager rejected the rotation change";
    return false;
  }
  return true;
}

} // namespace

bool apply_kindle_rotation(const std::string &mode,
                           const OrientationCommandRunner &runner,
                           std::string &error) {
  if (mode != "auto" && mode != "locked") {
    error = "Invalid rotation mode";
    return false;
  }
  return runner({"/usr/bin/lipc-set-prop", "com.lab126.winmgr",
                 "orientationLock", mode == "auto" ? "off" : "current"},
                error);
}

bool apply_kindle_rotation(const std::string &mode, std::string &error) {
  return apply_kindle_rotation(mode, run_orientation_command, error);
}

} // namespace ankink
