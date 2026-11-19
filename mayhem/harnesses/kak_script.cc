// kak_script: `kak -n -ui json -e 'source <script>'` — a kakoune command script (kakrc, `source`)
// run in a server's client context (run_server in src/main.cc).
//
// Input: a kakoune script. It is written to a scratch file and sourced as the client's init
// command, exercising the command parser and expansions (src/command_manager.cc), every
// builtin command (src/commands.cc), option/highlighter/hook/keymap/face parsing and `exec`
// into normal mode; the client is then redrawn. stdin is /dev/null, so the JSON UI hits EOF
// right after and run_server returns. FS-writing / process-global builtins are disabled, see
// kak_fuzz_common.hh.
#include "kak_fuzz_common.hh"

using namespace Kakoune;

static std::string g_script;
static std::string g_client_init;

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    kak_fuzz::init_env();
    g_script = kak_fuzz::scratch_dir() + "/input.kak";
    g_client_init = "source " + g_script;
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    kak_fuzz::reset_env();
    kak_fuzz::write_file(g_script, data, size);
    kak_fuzz::set_stdin("");
    StringView client_init{g_client_init.c_str()};
    kak_fuzz::run_like_main([&] { run_server({}, kak_fuzz::server_init(), client_init, {}, {}, ServerFlags::IgnoreKakrc, UIType::Json, DebugFlags::None, {}); });
    kak_fuzz::set_stdin("");
    return 0;
}
