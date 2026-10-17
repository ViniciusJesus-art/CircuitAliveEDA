#!/usr/bin/env bash
set -Eeuo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
export CIRCUITALIVE_CONFIG_HOME="$CIRCUITALIVE_ROOT/.circuitalive/test-config"
source "$CIRCUITALIVE_ROOT/tools/circuitalive/runtime-env.sh"
ca_demo_tests='MemoryResourceFieldsPersistAndNativeDemoLoadsBothImages,CircuitAliveDemoSchematicsAllRunInteractively,Native555PipelineDemoRegistersDataOnSeparateRisingEdges,NativeTitlesAndTextBoxesAreAnnotationsDuringInteractiveSimulation,NativeExcludedInstanceBypassesBrokenModelAndPersistsWithoutChangingSettings,LegacyRectifierDemoAssociatesNamedDiodeAndOriginalSineSource,LegacyAmplifierDemoAssociates2n2222TransistorsAndRuns'
if [[ ${1:-} == --audit ]]; then
    ca_demo_tests+=',AuditAllSimulationDemos'
    shift
fi
if (( $# )); then
    echo 'Uso: tools/circuitalive/test-demos.sh [--audit]' >&2
    exit 2
fi
ca_demo_binary="$CIRCUITALIVE_WORK/build/circuitalive/qa/tests/spice/qa_spice"
if [[ ! -x "$ca_demo_binary" ]]; then
    echo 'Compile primeiro: tools/circuitalive/build.sh --target qa_spice' >&2
    exit 1
fi
mkdir -p "$CIRCUITALIVE_ROOT/.circuitalive/logs"
export CIRCUITALIVE_DEMO_AUDIT_REPORT="$CIRCUITALIVE_ROOT/.circuitalive/logs/demo-audit.tsv"
"$ca_demo_binary" --run_test="$ca_demo_tests" --log_level=message --color_output=no 2>&1 \
    | tee "$CIRCUITALIVE_ROOT/.circuitalive/logs/test-demos.log"
