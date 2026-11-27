# Test instrumentation

These `.inc` files contain the test-only declarations, fault injection, counters,
and benchmark alternatives extracted from `src`. Each filename identifies its
source file and the order of its extracted branches. They are included at the
original scope, so tests can still exercise private implementation details without
duplicating the product implementation. They intentionally have no include guard.

Only the test variants of the application, file, plugin maintenance, and Scintilla
libraries enable this instrumentation. `multipad4` links the product variants even
when `BUILD_TESTING=ON`; `BUILD_TESTING=OFF` does not require these files.

Keep production branches in `src` and test implementations here. Settings isolation
and document load statistics remain product facilities: session persistence uses
isolated settings, and file commands consume load statistics.
