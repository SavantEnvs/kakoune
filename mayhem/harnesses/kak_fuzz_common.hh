// Shared setup for the kakoune fuzz harnesses.
//
// Kakoune ships no library: its entry points (run_filter for `kak -f`, run_server for a
// server + local client) live in src/main.cc, in an anonymous namespace next to main(). Each
// harness therefore compiles src/main.cc into its own translation unit (included below, with
// main renamed to kak_main so it does not clash with libFuzzer's main) and links every other
// upstream object; build.sh leaves main.o out. The harnesses call those real CLI code paths.
// (Renaming the `main` token is consistent within this TU: elsewhere it only names inline
// SelectionList::main() and function parameters, none of which have external out-of-line
// definitions.)
#pragma once

#define main kak_main
#include "main.cc"
#undef main

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unistd.h>

namespace kak_fuzz
{

// Per-process scratch directory under $TMPDIR (Mayhem's cwd and /tmp are writable, the image
// is not). Also used as HOME and XDG_RUNTIME_DIR, so the session socket kakoune's Server
// creates lands here and nothing outside it is touched.
inline const std::string& scratch_dir()
{
    static std::string dir = [] {
        const char* tmp = getenv("TMPDIR");
        std::string templ = std::string(tmp && *tmp ? tmp : "/tmp") + "/kakfuzz.XXXXXX";
        if (!mkdtemp(templ.data()))
            abort();
        return templ;
    }();
    return dir;
}

inline std::string& saved_path()
{
    static std::string path;
    return path;
}

// Per-input reset of process-global state kakoune changes: the ShellManager constructor
// (src/shell_manager.cc) prepends kak's libexec dir to PATH every time a server/filter starts,
// so without this PATH (and the environment glibc keeps for it) grows with every input.
inline void reset_env()
{
    setenv("PATH", saved_path().c_str(), 1);
}

// One-time environment, mirroring how upstream's test/run drives kak:
//  * KAKOUNE_POSIX_SHELL=/bin/false: fuzzed `|`, `!`, `$`, `%sh{}` spawn /bin/false instead of
//    a real shell, so an input cannot run arbitrary commands (kakoune reads this variable in
//    src/shell_manager.cc as its POSIX shell).
//  * stdin and stdout on /dev/null: run_filter reads its *stdin* buffer from fd 0 and writes
//    results to fd 1, the JSON UI writes its draw RPCs to fd 1. libFuzzer reports on stderr.
inline void init_env()
{
    const std::string& dir = scratch_dir();
    setenv("KAKOUNE_POSIX_SHELL", "/bin/false", 1);
    setenv("HOME", dir.c_str(), 1);
    setenv("XDG_RUNTIME_DIR", dir.c_str(), 1);
    setenv("LC_ALL", "C.UTF-8", 1);
    unsetenv("KAKOUNE_RUNTIME");
    const char* path = getenv("PATH");
    saved_path() = path ? path : "/usr/bin:/bin";
    int null = open("/dev/null", O_RDWR);
    if (null < 0)
        abort();
    dup2(null, 0);
    dup2(null, 1);
    close(null);
}

inline void write_file(const std::string& path, const uint8_t* data, size_t size)
{
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        abort();
    while (size > 0)
    {
        ssize_t n = write(fd, data, size);
        if (n <= 0)
            abort();
        data += n;
        size -= n;
    }
    close(fd);
}

// Point fd 0 at `path` (or /dev/null when empty).
inline void set_stdin(const std::string& path)
{
    int fd = open(path.empty() ? "/dev/null" : path.c_str(), O_RDONLY);
    if (fd < 0)
        abort();
    dup2(fd, 0);
    if (fd != 0)
        close(fd);
}

// Exceptions that escape run_filter/run_server end the process through main()'s handlers in
// src/main.cc ("Fatal error: ..." / "uncaught exception", exit status -1): a clean error exit,
// not a crash. Mirror those handlers so such an input is a rejected run here too.
template<typename F>
inline void run_like_main(F&& f)
{
    try { f(); }
    catch (Kakoune::exception&) {}
    catch (std::exception&) {}
    catch (...) {}
}

// Server-init commands for the targets that run a kakoune server. The command language is an
// interpreter, so the harness allow-lists builtins: every builtin stays as upstream defines it
// EXCEPT the ones below, which are replaced (via kakoune's own `define-command -override`) by
// a command that fails. They are the builtins that write files, change process-global state
// that would outlive one input, or turn the session into a daemon that never exits:
//   write write! write-all write-quit write-quit! write-all-quit  -- write arbitrary paths
//   echo                         -- `echo -to-file` / `-to-shell-script` write arbitrary paths
//   change-directory             -- chdir() is process-global, would leak into the next input
//   daemonize-session            -- the server would wait for remote clients forever
//   rename-session               -- renames the session socket outside the per-input lifetime
// An input cannot get the originals back: overridden builtins are not reachable by any name.
inline const char* server_init()
{
    return
        "define-command -override -hidden -params .. write %{ fail 'disabled by fuzz harness' }\n"
        "define-command -override -hidden -params .. write! %{ fail 'disabled by fuzz harness' }\n"
        "define-command -override -hidden -params .. write-all %{ fail 'disabled by fuzz harness' }\n"
        "define-command -override -hidden -params .. write-quit %{ fail 'disabled by fuzz harness' }\n"
        "define-command -override -hidden -params .. write-quit! %{ fail 'disabled by fuzz harness' }\n"
        "define-command -override -hidden -params .. write-all-quit %{ fail 'disabled by fuzz harness' }\n"
        "define-command -override -hidden -params .. echo %{ fail 'disabled by fuzz harness' }\n"
        "define-command -override -hidden -params .. change-directory %{ fail 'disabled by fuzz harness' }\n"
        "define-command -override -hidden -params .. daemonize-session %{ fail 'disabled by fuzz harness' }\n"
        "define-command -override -hidden -params .. rename-session %{ fail 'disabled by fuzz harness' }\n"
        // same settings upstream's test/run uses so runs don't depend on timers
        "set-option global autoinfo ''\n"
        "set-option global autocomplete ''\n";
}

}
