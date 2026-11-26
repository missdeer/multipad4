# Encoding alias lookup (#60)

The normalized fixed alias table is indexed once using thread-safe function-local
static initialization. try_emplace retains the original first-table-match
precedence. Explicit UTF-16 and UTF-8-signature aliases still execute first and
the installed-code-page numeric fallback remains unchanged. Input normalization,
UTF-16 validation and an unsuccessful call's output argument are unchanged.

The file I/O tests compare every listed alias, decorated uppercase/punctuation
variants, explicit overrides, unknown/empty/numeric names and overflow names
against the original parser. Eight threads exercise first publication and check
results; invalid UTF-16 remains rejected. A test-only thread-local parser override
also measures the real files::Read path against the original parser, preserving
the same modeline extraction, decoding and file I/O on both sides.

Windows x64, MSVC 14.44 RelWithDebInfo (/O2), SSE4.2, seven alternating pairs,
warm cache, local temporary storage. The batch contains 32 ISO-8859-1 files, each
with an iso-ir-100 coding modeline, an 8192-byte ASCII body and a final é. Both
paths check encoding identity and decoded output. Raw results:
[encoding-alias-pairs.csv](benchmarks/encoding-alias-pairs.csv).

| Workload | Linear median / max ms | Indexed median / max ms |
|---|---:|---:|
| All alias/decorated queries × 8 | 56.7687 / 61.4281 | 0.8981 / 1.0028 |
| Late/numeric/miss queries × 2000 | 49.4756 / 52.1359 | 1.2224 / 1.7193 |
| Actual 32 modeline-tagged file reads | 1.2930 / 2.4726 | 1.3970 / 2.6583 |

The parser reduction is substantial; the small file batch is dominated by other
work and did not improve in these samples. No total-open speedup is claimed.
The index is retained for the measured repeated parser cost, including miss
paths. Initialization allocates one normalized key per unique alias; subsequent
queries no longer copy/split/normalize every preceding alias entry. No production
temporary-file I/O is added. CPU time, peak private bytes and first-paint latency
were not instrumented; seven samples do not establish p95/p99 guarantees. No
visual behavior changed, and physical IME or mixed-DPI interaction was not tested.
