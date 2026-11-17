import assert from 'node:assert/strict';
import test from 'node:test';
import { releaseOutputs } from './release-outputs.mjs';

test('new releases keep the binary workflow output contract', () => {
  assert.deepEqual(releaseOutputs({ nextRelease: { version: '1.2.3', gitTag: 'v1.2.3' } }), {
    new_release_published: 'true', new_release_version: '1.2.3', new_release_git_tag: 'v1.2.3',
  });
});
test('retries resume board artifacts for the release at HEAD', () => {
  assert.deepEqual(releaseOutputs(false, ['v1.2.3']), {
    new_release_published: 'true', new_release_version: '1.2.3', new_release_git_tag: 'v1.2.3',
  });
});
test('commits with no new release or release tag skip publishing', () => {
  assert.equal(releaseOutputs(false, ['unrelated']).new_release_published, 'false');
});
