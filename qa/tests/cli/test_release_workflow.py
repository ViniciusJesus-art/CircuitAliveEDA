"""Exercise the real Actions selection/publication guards without starting builds."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[3]
WORKFLOW = (ROOT / '.github/workflows/publish-release.yml').read_text()


@unittest.skipUnless(shutil.which('node'), 'Node is required to exercise Actions JavaScript')
class ReleaseWorkflowTests(unittest.TestCase):
    def prepare(self, version, target, publish=True):
        source = WORKFLOW.split('          script: |\n', 1)[1].split('\n  windows:', 1)[0]
        source = '\n'.join(line[12:] if line.startswith(' ' * 12) else line for line in source.splitlines())
        harness = '''
const outputs = {};
const context = {ref:'refs/heads/main', sha:'abc123', repo:{}};
const core = {setOutput:(key,value)=>outputs[key]=value,
 summary:{addHeading(){return this},addRaw(){return this},async write(){}}};
const github = new Proxy({}, {get(){throw new Error('Commit/release lookup must not gate selection')}});
(async()=>{
''' + source + '''
})().then(()=>process.stdout.write(JSON.stringify(outputs)))
.catch(error=>{process.stderr.write(error.message);process.exitCode=1});
'''
        return subprocess.run(['node', '-e', harness], text=True, capture_output=True,
                              env=dict(os.environ, RELEASE_VERSION=version, RELEASE_TARGET=target,
                                       PUBLISH_RELEASE=str(publish).lower()))

    def test_every_target_selects_only_requested_native_builds(self):
        for target, formats in [('all','rpm,appimage,deb,exe'), ('linux','rpm,appimage,deb'),
                                ('fedora','rpm,appimage'), ('windows','exe'), ('exe','exe'),
                                ('rpm','rpm'), ('appimage','appimage'), ('deb','deb')]:
            for publish in (False, True):
                with self.subTest(target=target, publish=publish):
                    result = self.prepare('10.0.7-alpha.4.2.0', target, publish)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    selected = json.loads(result.stdout)
                    self.assertEqual(selected['formats'], formats)
                    self.assertEqual(selected['version'], '10.0.7-alpha.4.2.0')
                    self.assertEqual(selected['tag'], 'v10.0.7-alpha.4.2.0')
                    self.assertEqual(selected['fedora_formats'], ','.join(
                        kind for kind in formats.split(',') if kind in ('rpm', 'appimage')))

    def test_invalid_versions_and_targets_fail_before_building(self):
        for version in ('10.0.7', 'v10.0.7-alpha.4.2', '10.0.7-alpha.65536',
                        '10.0.7-alpha.1.2.3.4.5', 'bad\nversion'):
            with self.subTest(version=version):
                self.assertNotEqual(self.prepare(version, 'all').returncode, 0)
        self.assertNotEqual(self.prepare('10.0.7-1', 'unknown').returncode, 0)

    def test_publication_requires_success_of_selected_jobs_and_explicit_checkbox(self):
        condition = WORKFLOW.split('  publish:',1)[1].split('    if: >-\n',1)[1].split('    runs-on:',1)[0]
        for outcomes, publish, cancelled, expected in [
            (('success','success','success'),True,False,True),
            (('success','skipped','skipped'),True,False,True),
            (('skipped','success','skipped'),True,False,True),
            (('success','failure','success'),True,False,False),
            (('success','success','success'),False,False,False),
            (('success','success','success'),True,True,False)]:
            needs = dict(prepare={'result':'success'}, **{
                key:{'result':result} for key,result in zip(('windows','linux','deb'),outcomes)})
            harness = 'const needs='+json.dumps(needs)+';const inputs='+json.dumps({'publish_release':publish})+';'
            harness += 'const always=()=>true;const cancelled=()=>'+str(cancelled).lower()+';'
            # Assembly also runs in artifact-only mode; remote upload is a separate guarded step.
            harness += 'process.stdout.write(String(!!(('+condition+') && inputs.publish_release)));'
            result = subprocess.run(['node','-e',harness], capture_output=True,text=True,check=True)
            self.assertEqual(result.stdout, str(expected).lower())


class ReleaseWiringTests(unittest.TestCase):
    def test_publication_uses_same_incremental_entry_point_as_local_builds(self):
        publish = WORKFLOW.split('  publish:',1)[1]
        self.assertIn('release/release.py "$RELEASE_VERSION"', publish)
        self.assertIn('--from-artifacts --formats "$RELEASE_FORMATS"', publish)
        self.assertIn('contents: write', publish)
        self.assertIn('GH_TOKEN: ${{ github.token }}', publish)
        self.assertNotIn('package_version:', WORKFLOW)
        self.assertIn('if: inputs.publish_release', publish)
        self.assertIn('path: dist/CircuitAliveEDA-${{ env.RELEASE_VERSION }}-auxiliares.zip', publish)

    def test_reusable_workflow_and_native_validation_workflows_exist(self):
        for source in ROOT.glob('.github/workflows/*.yml'):
            for local in re.findall(r'uses: (\./\S+)',source.read_text()):
                self.assertTrue((ROOT/local).exists(), local)
        for name in ('release','windows-audit','windows-dependencies','windows-validate'):
            self.assertTrue((ROOT/f'.github/workflows/{name}.yml').exists())

    def test_selected_version_reaches_all_native_builds_and_installer(self):
        self.assertIn('version: ${{ needs.prepare.outputs.version }}', WORKFLOW)
        for script in ('build-linux.sh','build-deb.sh'):
            self.assertIn('CIRCUITALIVE_RELEASE_VERSION=$RELEASE_VERSION',
                          (ROOT/'tools/circuitalive/release'/script).read_text())
        windows = (ROOT/'.github/workflows/release.yml').read_text()
        self.assertIn('-Phase configure -Version $env:RELEASE_VERSION', windows)
        self.assertIn('-Phase stage -Version $env:RELEASE_VERSION', windows)
        installer = (ROOT/'tools/circuitalive/release/package-windows.py').read_text()
        self.assertIn('AppVersion={version}', installer)
        self.assertIn('OutputBaseFilename=CircuitAliveEDA-{version}', installer)


if __name__ == '__main__':
    unittest.main()
