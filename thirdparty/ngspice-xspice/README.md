XSPICE interface headers from ngspice 47, maintained upstream mirror tag `ngspice-47`, commit a80f6e3e95d51534905b1f23410a951802666656 (https://github.com/imr/ngspice/tree/ngspice-47).

Only the dependency closure needed by our memory code model is included. Original notices and upstream COPYING are retained. config.h was generated for the local Linux shared library with XSPICE. KLU and CIDER are detected from the linked shared library by CMake. This module must be rebuilt and its ABI verified when changing the ngspice major version or build configuration; it is loaded only by the isolated worker.

`eeschema/sim/memory_cm/ifspec.c` and `cfunc.c` were generated from the adjacent .ifs/.mod with ngspice 47 cmpp. The checked-in C files make normal builds independent of cmpp.
