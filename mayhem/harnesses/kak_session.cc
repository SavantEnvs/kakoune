// kak_session: `kak -n -ui json` — a kakoune server with one local client driven by the JSON-RPC
// stream on stdin (run_server in src/main.cc, JsonUI in src/json_ui.cc).
//
// Input: the bytes kakoune reads from stdin, i.e. JSON-RPC requests (`keys`, `resize`,
// `mouse_*`, `scroll`, `menu_select`, ...), one per line, like upstream's test/*/script ui_in
// lines. The client renders into a window (highlighters, wrap, replace-ranges), and `:` keys
// reach the full command language. When the stream hits EOF the JSON UI closes, the client
// goes away and run_server returns. FS-writing / process-global builtins are disabled, see
// kak_fuzz_common.hh.
#include "kak_fuzz_common.hh"

using namespace Kakoune;

static std::string g_in;

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    kak_fuzz::init_env();
    g_in = kak_fuzz::scratch_dir() + "/ui-in.jsonrpc";
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    kak_fuzz::reset_env();
    kak_fuzz::write_file(g_in, data, size);
    kak_fuzz::set_stdin(g_in);
    kak_fuzz::run_like_main([&] { run_server({}, kak_fuzz::server_init(), {}, {}, {}, ServerFlags::IgnoreKakrc, UIType::Json, DebugFlags::None, {}); });
    kak_fuzz::set_stdin("");
    return 0;
}
