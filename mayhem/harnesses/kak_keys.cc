// kak_keys: `kak -n -q -f <keys> <file>` — kakoune's filter mode (run_filter in src/main.cc).
//
// Input: <keys> NUL <buffer bytes>. The keys are what `-f` takes on the command line (argv can't
// hold a NUL, hence the separator); the buffer bytes are written to a scratch file that
// run_filter opens, edits with the keys through the real InputHandler (normal/insert/prompt
// modes, selectors, regex, utf8 handling) and writes back. Without a NUL the whole input is
// the key string and the file is empty. fd 0 is /dev/null, so run_filter's stdin pass sees an
// empty buffer.
#include "kak_fuzz_common.hh"

using namespace Kakoune;

static std::string g_file;

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    kak_fuzz::init_env();
    g_file = kak_fuzz::scratch_dir() + "/buffer.txt";
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    kak_fuzz::reset_env();
    const uint8_t* nul = (const uint8_t*)memchr(data, 0, size);
    size_t key_len = nul ? (size_t)(nul - data) : size;
    const uint8_t* buf = nul ? nul + 1 : data + size;
    size_t buf_len = (size_t)(data + size - buf);

    kak_fuzz::write_file(g_file, buf, buf_len);
    kak_fuzz::set_stdin("");

    StringView keys{(const char*)data, (int)key_len};
    StringView files[] = { StringView{g_file.c_str()} };
    kak_fuzz::run_like_main([&] { run_filter(keys, files, true, {}); });
    return 0;
}
