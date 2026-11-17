import assert from 'node:assert/strict';
import test from 'node:test';
import { generateNotes } from '@semantic-release/release-notes-generator';

// Exercise the real preset and writer together: preset 10 requires writer 9.
test('release notes render the configured conventional-commit preset', async () => {
  const config = JSON.parse(await (await import('node:fs/promises')).readFile(new URL('../.releaserc.json', import.meta.url)));
  const [, options] = config.plugins.find(plugin => Array.isArray(plugin) && plugin[0] === '@semantic-release/release-notes-generator');
  const notes = await generateNotes(options, {
    cwd: process.cwd(),
    commits: [{ hash: '1234567890abcdef', message: 'fix: preserve the release notes renderer' }],
    lastRelease: { version: '1.0.0', gitTag: 'v1.0.0', gitHead: 'abcdef1234567890' },
    nextRelease: { version: '1.0.1', gitTag: 'v1.0.1', gitHead: '1234567890abcdef' },
    options: { repositoryUrl: 'https://github.com/JerrettDavis/EspWaveRider.git' },
    logger: { log() {}, error() {} },
  });
  assert.match(notes, /Fixes/);
  assert.match(notes, /preserve the release notes renderer/);
  assert.match(notes, /1\.0\.1/);
});
