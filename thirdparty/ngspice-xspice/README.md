XSPICE interface headers from ngspice 47, maintained upstream mirror tag `ngspice-47`, commit a80f6e3e95d51534905b1f23410a951802666656 (https://github.com/imr/ngspice/tree/ngspice-47).

Only the dependency closure needed by our code models is included. Original notices and upstream COPYING are retained. config.h was generated for the local Linux shared library with XSPICE. KLU, CIDER and PREDICTOR are detected from the linked shared library by CMake. PREDICTOR changes the circuit structure layout used by the native expression and GPIO loaders. This module must be rebuilt and its ABI verified when changing the ngspice major version or build configuration; it is loaded only by the isolated worker.

`eeschema/sim/memory_cm/ifspec.c` and `cfunc.c` were generated from the adjacent .ifs/.mod with ngspice 47 cmpp. The checked-in C files make normal builds independent of cmpp.

For MSVC, CMake uses the ngspice 46 headers/configuration installed by the pinned
vcpkg port. That port omits `src/maths/dense/dense.h`, which `cktdefs.h` requires
with RFSPICE enabled. The existing `maths/dense/dense.h` is byte-identical to the
file in the port's upstream commit `ebdaf58ec76a06ffaac7e0f138360dd1cf5ee4b6`
([source](https://github.com/imr/ngspice/blob/ebdaf58ec76a06ffaac7e0f138360dd1cf5ee4b6/src/maths/dense/dense.h)).
Its SHA-256 with LF line endings is
`4c56586a20128f36967441abe428e4611fb29a7c7a1cc6e5399a5e10b35e2a29`.
CMake checks the version and checksum and stages only this file in a separate
include directory; the bundled ngspice 47 ABI headers are not on the MSVC include
path. The port's RFSPICE, KLU and CIDER settings remain intact.
