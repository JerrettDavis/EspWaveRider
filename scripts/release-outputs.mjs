export function releaseOutputs(result, tagsAtHead = []) {
  const existingTag = tagsAtHead.find(tag => /^v\d+\.\d+\.\d+(?:-[\w.-]+)?$/.test(tag));
  const tag = result ? result.nextRelease.gitTag : existingTag;
  return {
    new_release_published: String(Boolean(tag)),
    new_release_version: result ? result.nextRelease.version : (tag?.slice(1) ?? ''),
    new_release_git_tag: tag ?? '',
  };
}
