# QuickJS-NG

Unmodified interpreter sources from **v0.17.0**, commit
`6d46d07` (release tag), https://github.com/quickjs-ng/quickjs/tree/v0.17.0.
Downloaded from the official GitHub release source tarball on 2026-10-21.
The MIT license is reproduced in `LICENSE` and source headers.

CircuitAliveEDA builds only `quickjs.c`, `dtoa.c`, `libregexp.c`, `libunicode.c`.
The local CMakeLists.txt does not expose upstream command-line programs, native
module loading, filesystem or networking APIs. Core sources and generated headers
are kept unchanged. Runtime scripts have memory, stack and execution limits.
Shared memory atomics are disabled through the upstream feature macro because
the synchronous simulation host does not expose shared memory to scripts. This
also avoids requiring MSVC's experimental C11 atomics option.

Release source archive SHA-256:
`559bc4c420475e55c7ab4510adbc562f55d7524d75e8e89d79ce4bb02f5687d9`.
