# AVR8js

Unmodified CommonJS JavaScript distribution of **avr8js 0.21.1**, downloaded
from https://registry.npmjs.org/avr8js/-/avr8js-0.21.1.tgz on 2026-10-21.
Upstream: https://github.com/wokwi/avr8js. MIT license: `LICENSE`.

`cjs` contains the published runtime modules and type declarations. The script
`simulation/runtime/build-avr8js.py` bundles these modules with the CircuitAliveEDA
adapter into `simulation/runtime/avr8js.js`. This uses Python only at development
time; installed applications execute the bundled JavaScript in QuickJS-NG and
do not require Node.js, npm, Python or a firmware compiler.

The generator adds an explicit unsupported-opcode exception to the bundled
instruction decoder's otherwise silent fallback. Vendored modules remain unchanged.
It also resolves the decoder's 99 constant opcode masks into a generated lookup
table, preserving instruction bodies and branch priority. The differential check
in `qa/tests/cli/test_avr_decoder.cjs` covers every opcode with two register/flag
states against the original vendored decoder.

Published npm archive SHA-256:
`febfe96e73a840cdb6cc6e583433af2badb8c85421f38545cf5d802802ed35e8`.
