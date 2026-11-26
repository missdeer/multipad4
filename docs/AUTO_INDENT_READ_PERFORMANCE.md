# Auto-indent line reads (#44)

Auto-indent reads the preceding line's indentation and a short keyword prefix,
plus the suffix needed for punctuation/comment rules. It no longer copies and
reserves twice the entire preceding line. The output allocation follows actual
indentation and padding. A bounded borrowed-range scan finds whitespace; each
span is consumed before the next editor call, and no borrowed pointer survives
an edit. The x64 whitespace scan checks sixteen bytes at a time.

SQL BEGIN/START rules still search the complete logical remainder for
`transaction`, case-insensitively, stopping at an embedded NUL as before. That
rare rule uses bounded chunks with overlap, retaining matches across chunk
boundaries rather than introducing a prefix cutoff.

## Reproduce

Use the normal MSVC wrappers with BUILD_TESTING enabled, then run:

```powershell
ctest --test-dir cmake-msvc-build -R indent_read --output-on-failure
Start-Process ./cmake-msvc-build/multipad4-tests.exe --indent-read-benchmark -Wait
```

`CompletionIndentBaseline.h` retains the f19a330 implementation and notices for
the test/benchmark only. It is excluded from product builds. The dedicated test
compares bytes, caret, bookmark and undo results for LF/CRLF/CR, whitespace,
braces, comments and SQL transaction words beyond 64 KiB, including a mixed-case
word crossing a read boundary.

## Local paired measurements

Windows x64, MSVC 19.44, RelWithDebInfo `/O2`, SSE4.2. One ordinary UTF-8 tab,
plain-text lexer, warm memory, eleven runs per variant, same binary/document.
Document setup is outside the measurement. Newline is already inserted; the
measurement covers the actual auto-indent function and then a synchronous
application redraw, not physical key input or compositor presentation. Other
worktree builds were active, so absolute times include scheduling noise.

| Preceding line | Variant | Callback median ms | p95/p99/max ms | Paint-complete median ms |
|---|---|---:|---:|---:|
| 8 MiB text, four spaces of indentation | Before | 8.1202 | 9.8422 | 8.1270 |
| Same | After | 4.0241 | 4.3539 | 4.0278 |
| 2 MiB actual indentation, one text byte | Before | 7.3728 | 8.6650 | 7.3759 |
| Same | After | 4.6268 | 5.0600 | 4.6316 |

Eleven samples are insufficient for strong tail-latency claims; nearest-rank
p95/p99 select the maximum. Both variants' output bytes are asserted identical.
Very long actual indentation necessarily still requires scanning, output
storage and insertion proportional to that indentation.

Validation: optimized build and indent_read (0.86 s) passed. Full editor
integration is reported separately after the final build. Isolated CPU/private
memory peaks, exact copy counters, IME, dragging and mixed-DPI checks were not
measured; no visual behavior change is intended.

After integrating master fb8344d on 2026-11-26, the complete optimized build,
file_search, fileio_roundtrip, indent_read (0.63 s), and editor_integration
(28.35 s) passed. Test and benchmark entry points now use multipad4-tests.exe;
the production executable excludes the retained baseline and regression code.
