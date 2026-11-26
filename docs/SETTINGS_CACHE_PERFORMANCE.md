# Settings cache and MRU (#55)

Persistent profile reads reuse a 256-wide-character buffer and retain growth up
to the previous 1 MiB limit. Nonpersistent reads allocate no profile buffer.
Missing values are distinguished from existing empty values using two distinct
defaults, including when either sentinel is itself a stored value. The original
profile escaping and fallback handling remain in use.

Only changed strings/options and missing or invalid numeric defaults are written.
Successful writes clear their dirty entries; failed writes remain retryable.
Read-only cache entries no longer overwrite independent INI writers. SaveOption
continues to persist only its requested command. Recent file ordering, duplicate
removal, slot changes and the 32–4096 limit now live in AppSettings; the dialog and
system MRU policy continue to use their existing settings.

## Paired local measurement

Windows x64, MSVC 14.44 RelWithDebInfo (/O2), SSE4.2, local temporary storage,
Unicode INI files, warm OS cache, fresh AppSettings instances, five alternating
pairs in one process. The baseline is the original implementation retained only
in the test executable. No document encoding or line shape applies to this
settings-only workload. Raw results: [settings-pairs.csv](benchmarks/settings-pairs.csv).

| Workload | Before median ms | After median ms | Before maximum ms | After maximum ms |
|---|---:|---:|---:|---:|
| 512 nonpersistent cache misses | 0.8673 | 0.3795 | 1.0790 | 0.4121 |
| 16 opens, 4096 MRU slots, exit Save | 1116.070 | 141.196 | 1129.170 | 169.312 |

Each old cold miss constructs 32,768 wide characters; persistent after reads
reuse 256 initially, while nonpersistent reads construct no scratch buffer.
For the empty-profile MRU workload, exit profile writes fall from 4096 slot
writes to 16 populated slot writes, plus the existing profile flush. Initial
empty-slot native reads increase from 4096 to 12,288 because exact missing-value
and fallback handling requires additional queries; subsequent opens use the
AppSettings-owned MRU/cache. This tradeoff is included in the paired timing.
No write is issued for an unchanged cached read on a later Save.

Five samples do not support meaningful tail-latency claims; the CSV contains all
samples. UI-thread CPU, peak private bytes, and first-paint latency were not
instrumented. The workload measures synchronous settings work and completion.

Run the optional benchmark with MULTIPAD4_SETTINGS_BENCHMARK=1 and
`ctest --test-dir cmake-msvc-build -R settings_profile --output-on-failure`.
Normal smoke runs exercise only the correctness checks:
Unicode/escaped values, missing versus empty, a 20,000-character value, external
string and numeric updates, default materialization, failed-save retry, MRU
duplicates/clearing/limit changes, and existing SaveOption behavior. Native INI
APIs themselves truncate a 70,000-character write on this machine (readback
4464); this change preserves the existing format and does not fix that platform
limit. Portable-path selection is unchanged; physical portable deployment was
not exercised.

The initial full editor smoke including all five benchmark pairs passed in
55.84 s. A later full smoke stopped before settings checks at the Project plugin
selector-count assertion (ProjectPluginTests.h:61); the dedicated settings test
allows these checks to run independently. Do not read the focused result as a
claim that the final full editor suite passed.
