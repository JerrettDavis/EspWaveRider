import { appendFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import semanticRelease from 'semantic-release';
import { releaseOutputs } from './release-outputs.mjs';

// Use the project's locked dependency graph, including the compatible writer.
const result = await semanticRelease();
// A retry after publishing must still rebuild missing board assets for this tag.
const tagsAtHead = execFileSync('git', ['tag', '--points-at', 'HEAD', '--sort=-version:refname'], { encoding: 'utf8' })
  .trim().split(/\r?\n/);
if (process.env.GITHUB_OUTPUT) {
  appendFileSync(process.env.GITHUB_OUTPUT,
    Object.entries(releaseOutputs(result, tagsAtHead)).map(([key, value]) => `${key}=${value}\n`).join(''));
}
