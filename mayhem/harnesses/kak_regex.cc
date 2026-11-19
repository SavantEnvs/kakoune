// kak_regex: kakoune's own regex engine (src/regex_vm.cc parser, compiler, optimiser and threaded
// VM) through its public interface, the Regex class and regex_search/backward_regex_search
// (src/regex.hh), as `s`, `/`, `<a-/>` and the regex options use it.
//
// Input: <flag byte> <pattern> NUL <subject>.
//   flag bit 0: RegexCompileFlags::Optimize   bit 1: RegexCompileFlags::Backward
//   bit 2: RegexCompileFlags::NoForward (only with Backward; a regex needs at least one direction)
//   bit 3: RegexCompileFlags::NoSubs          bits 4-7: RegexExecFlags NotBeginOfLine,
//          NotEndOfLine, NotBeginOfWord, NotEndOfWord
// Forward search runs unless NoForward was compiled in; backward search runs iff Backward was
// compiled in (the VM is only generated for the directions requested, src/regex_vm.hh).
// Searches that return captures are skipped for NoSubs regexes, which upstream only uses for
// capture-less matching.
#include "kak_fuzz_common.hh"
#include "regex.hh"
#include "exception.hh"
#include "shared_string.hh"

using namespace Kakoune;

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    kak_fuzz::init_env();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1)
        return -1;
    uint8_t f = data[0];
    const char* rest = (const char*)data + 1;
    size_t rest_len = size - 1;
    const char* nul = (const char*)memchr(rest, 0, rest_len);
    size_t pat_len = nul ? (size_t)(nul - rest) : rest_len;
    const char* subj = nul ? nul + 1 : rest + rest_len;
    const char* subj_end = rest + rest_len;

    RegexCompileFlags cflags = RegexCompileFlags::None;
    bool backward = f & 2;
    bool forward = not (backward and (f & 4));
    if (f & 1) cflags |= RegexCompileFlags::Optimize;
    if (backward) cflags |= RegexCompileFlags::Backward;
    if (not forward) cflags |= RegexCompileFlags::NoForward;
    if (f & 8) cflags |= RegexCompileFlags::NoSubs;

    RegexExecFlags eflags = RegexExecFlags::None;
    if (f & 16) eflags |= RegexExecFlags::NotBeginOfLine;
    if (f & 32) eflags |= RegexExecFlags::NotEndOfLine;
    if (f & 64) eflags |= RegexExecFlags::NotBeginOfWord;
    if (f & 128) eflags |= RegexExecFlags::NotEndOfWord;

    StringRegistry string_registry;
    try
    {
        // Kakoune builds regexes from its own String, which is always NUL-terminated
        // (src/string.hh); the parser relies on that, so hand it one too.
        String pattern{rest, ByteCount{(int)pat_len}};
        Regex re{pattern, cflags};
        MatchResults<const char*> res;
        bool subs = not (f & 8);
        if (forward)
        {
            // the capture-less search is what NoSubs regexes are compiled for
            regex_search(subj, subj_end, subj, subj_end, re, eflags);
            if (subs)
                regex_search(subj, subj_end, subj, subj_end, res, re, eflags);
        }
        if (backward and subs)
            backward_regex_search(subj, subj_end, subj, subj_end, res, re, eflags);
    }
    catch (runtime_error&)
    {
        // invalid pattern: Regex's documented error path (regex_error is a runtime_error)
    }
    return 0;
}
