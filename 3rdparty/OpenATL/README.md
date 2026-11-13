# OpenATL

A clean-room, minimal reimplementation of Microsoft's Active Template Library (ATL) for the MinGW compiler toolchain.

## Purpose

[WTL](https://wtl.sourceforge.io) (Windows Template Library) depends on ATL headers that are only available in Microsoft's MSVC toolchain. OpenATL provides the ATL subset required by WTL, making it possible to compile WTL applications with [MinGW](https://www.mingw-w64.org/)-based compilers. This allows building Windows WTL applications on non-Windows platforms (macOS, Linux) using cross-compilers like [llvm-mingw](https://github.com/mstorsjo/llvm-mingw).

## Features

- Header-only library, no build step required
- C++17 standard
- Supports WTL 10.0
- Clean-room implementation (no Microsoft or ReactOS code)
- Provides: `atlbase.h`, `atlwin.h`, `atlcom.h`, `atlstr.h`, `atltypes.h`, and supporting headers

## Implemented ATL Components

| Header | Contents |
|--------|----------|
| `atldef.h` | Base macros (`ATLASSERT`, `ATLVERIFY`, `AtlThrow`, `ATL_NO_VTABLE`, etc.) |
| `atlcore.h` | `CComCriticalSection`, `CAtlBaseModule`, string resource helpers |
| `atlalloc.h` | `CCRTAllocator`, `CHeapPtr`, `CTempBuffer` |
| `atlsimpcoll.h` | `CSimpleArray`, `CSimpleMap` |
| `atlcomcli.h` | `CComPtr`, `CComQIPtr`, `CComBSTR`, `CComVariant` |
| `atltrace.h` | `ATLTRACE`, `ATLTRACE2`, trace categories |
| `atlbase.h` | `CComModule`, `CAtlModule`, `CRegKey`, `CHandle`, threading models, `ATL::Checked` namespace |
| `atlwin.h` | `CWindow`, `CWindowImpl`, `CDialogImpl`, `CContainedWindow`, message map macros, thunks (x86, x86_64, AArch64) |
| `atlcom.h` | `CComObjectRootEx`, `CComObject`, COM map macros |
| `atlstr.h` | `CString` (based on `CSimpleStringT`) |
| `atltypes.h` | `CPoint`, `CSize`, `CRect` |

## Verified Compiler

Tested and verified with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (Clang/MinGW cross-compiler toolchain).

## Usage

Add the `include` directory to your include path:

```cmake
target_include_directories(your_target PRIVATE path/to/openatl/include)
```

Then include ATL headers as usual:

```cpp
#include <atlbase.h>
#include <atlwin.h>
```

## Demo

See [wtltest](https://github.com/kkHAIKE/wtltest) for a comprehensive WTL 10.0 test application built with OpenATL.

## Author

Created by Claude Opus (Anthropic).

## Support

If you find this project useful, consider [sponsoring my open-source work](https://github.com/sponsors/kkHAIKE).

## License

MIT License. See [LICENSE](LICENSE) for details.
