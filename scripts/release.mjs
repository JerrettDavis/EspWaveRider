import { appendFileSync } from 'node:fs';
import semanticRelease from 'semantic-release';

// Use the project's locked dependency graph, including the compatible writer.
const result = await semanticRelease();
if (process.env.GITHUB_OUTPUT) {
  const outputs = {
    new_release_published: String(Boolean(result)),
    new_release_version: result ? result.nextRelease.version : '',
    new_release_git_tag: result ? result.nextRelease.gitTag : '',
  };
  appendFileSync(process.env.GITHUB_OUTPUT,
    Object.entries(outputs).map(([key, value]) => `${key}=${value}\n`).join(''));
}
