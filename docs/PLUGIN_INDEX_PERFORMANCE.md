# Plugin dispatch and administration indexes (#57)

The host indexes command targets, shortcut owners, case-insensitive module names
and actual pretranslators as each plugin is published. References use plugin/item
indices, so later vector growth does not invalidate them. Normal and FastView
targets retain their own first eligible handler; disabled commands still stop
dispatch. Module messages retain their first matching plugin and shortcut lookup
retains the first matching shortcut, including noncommand entries. Pretranslators
retain load order. Initialization and reverse shutdown order are unchanged, and
indexes are cleared before unloading DLLs.

Plugin Admin builds catalog and loaded-module indexes for each Load. Enabled and
disabled installations have separate row slots even for the same folder; a
second-tree installation receives the same metadata fallback behavior as before.
The join preserves the original last matching loaded-module name.

## Local paired measurements

Windows x64, MSVC 14.44 RelWithDebInfo (/O2), SSE4.2, seven alternating pairs in
one executable, warm caches, fixed synthetic callback tables. Raw samples are in
[plugin-index-pairs.csv](benchmarks/plugin-index-pairs.csv).

| Workload | Linear median ms | Indexed median ms | Linear max ms | Indexed max ms |
|---|---:|---:|---:|---:|
| 20,000 command lookups, 128 plugins × 4 entries | 2.6867 | 0.1176 | 2.8803 | 0.1739 |
| 20,000 pretranslations, two actual callbacks | 1.1620 | 0.1557 | 1.3300 | 0.1696 |
| 512 installed-folder joins, 4096 catalog entries | 12.9273 | 0.2563 | 13.3442 | 0.2762 |

The command benchmark isolates the lookup from unchanged callback and frame
refresh work. The pretranslation after path is the actual host PreTranslate;
the before path visits the original plugin array. Both visit the same callbacks
and verify identical counts. The catalog benchmark isolates the old linear join
and indexed equivalent, with identical checksums. It excludes index construction
and filesystem/version-resource I/O. Actual PluginAdmin::Load with one catalog
folder installed in both trees took 5.4331 ms, including native DLL metadata
reads; this is a single after observation, not a paired total-dialog speedup.

The index adds O(commands + plugins) retained references; per-message temporary
document copying and temporary-file I/O are absent in these paths. CPU time,
process peak private bytes and first-paint latency were not instrumented. Seven
samples support only these local measurements, not p95/p99 latency guarantees.

The plugin_index CTest checks ordinary/FastView duplicate-command precedence,
disabled commands, late shortcuts, missing IDs, case-insensitive module messages,
pretranslator order, and independent enabled/disabled catalog rows. The fixture
reads the real bundled Base64 DLL without loading either fixture copy. No visual
layout was changed; physical IME and mixed-DPI interactions were not exercised.

The native DLL fixture is conditional on MULTIPAD4_TEST_BASE64, matching the
optional bundled plugin. Builds without Base64 retain all synthetic host/index
checks and omit only that filesystem fixture.
