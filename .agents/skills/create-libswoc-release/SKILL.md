---
name: create-libswoc-release
description: Create and publish an Apache trafficserver-libswoc release by porting reviewed lib/swoc changes from apache/trafficserver. Use when asked to catch the standalone library up to ATS and release it.
---

# Create a libswoc release

Create a release of [apache/trafficserver-libswoc](https://github.com/apache/trafficserver-libswoc) from changes that were first committed and reviewed in [apache/trafficserver](https://github.com/apache/trafficserver/tree/master/lib/swoc). The ATS `lib/swoc` tree is the canonical source; the standalone repository packages that code for independent use.

Publishing updates Apache `master` and creates a public tag. Do so only when the user explicitly requests a release. If the user asks only to prepare or inspect a release, stop before pushing either one.

## Required outcome

- Port every relevant ATS `lib/swoc` commit since the source baseline, preserving one standalone commit per ATS commit and the original authorship.
- Update all standalone version metadata for the new version in a separate release-preparation commit.
- Make every newly affected production source and test file identical to its ATS counterpart after applying the repository's path mapping.
- Explain every remaining full-tree difference as either release metadata, standalone packaging, or a pre-existing ATS test-harness adaptation. Do not silently accept a new difference.
- Pass the standalone SCons builds in the current ATS Fedora CI image.
- Publish an annotated version tag on the same commit as Apache `master`, then verify both remote references.

## Preflight

Work from clean clones of both repositories. Fetch and prune their canonical remotes, identify those remotes from their URLs rather than assuming names, and verify that both checkouts use `master`. The standalone `master` must be fast-forwarded to the canonical Apache branch before making changes. A maintainer fork is optional and must also be current if it will be updated.

Stop for clarification if either worktree has unrelated changes, histories have diverged, the intended version is ambiguous, or the runner lacks permission to update the Apache repository. Never force-push a release.

Inspect the latest tags, untagged standalone commits, current version metadata, and recent ATS `lib/swoc` history. Use the next version already prepared in the standalone repository when one exists; do not create a duplicate metadata commit. Otherwise, choose the next version according to the versioning policy in `README.md`, run:

```bash
tools/update-version.sh <major> <minor> <point>
```

Review every changed occurrence with `rg`, run `git diff --check`, and commit only the version metadata. Follow the repository's recent release-preparation commit naming convention.

## Find the ATS source baseline

The previous standalone tag is not an ATS revision. Locate the newest ATS `master` commit whose `lib/swoc` content is represented by the previous standalone release. Version-update commits and the previous release's ported commit messages are useful anchors, but verify the choice by comparing content. Record the intentional differences between the previous standalone tag and that ATS baseline; this is the baseline difference set for later verification.

List candidates only from the canonical first-parent history, oldest first:

```bash
git log --first-parent --reverse --format='%H %s' \
  <ats-baseline>..<ats-canonical>/master -- lib/swoc
```

Do not use `--all`: it includes duplicate commits from topic and maintenance branches. Inspect each candidate and exclude an ATS-only version update only when its source changes are already represented by the standalone release metadata. Include test-only follow-ups and commits that change files requiring standalone path adaptation.

## Port commits

For each selected ATS commit, in the displayed oldest-first order:

1. Create a one-commit patch limited to `lib/swoc` with `git format-patch -1`.
2. Rewrite patch paths from `lib/swoc` to `code`, then from `code/unit_tests` to `unit_tests`.
3. Apply it immediately to the standalone checkout with `git am` so author and message metadata are preserved.
4. Inspect the resulting commit before continuing.

For example, after setting the checkout, temporary-directory, and commit
variables:

```bash
git -C "$ATS_REPO" format-patch -1 --stdout "$ATS_COMMIT" -- lib/swoc |
  sed -e 's:lib/swoc:code:g' -e 's:code/unit_tests:unit_tests:g' \
    > "$PATCH_DIR/$ATS_COMMIT.patch"
git -C "$LIBSWOC_REPO" am "$PATCH_DIR/$ATS_COMMIT.patch"
```

Use a temporary directory created with `mktemp -d` and remove it after verification. Do not generate every patch with the default `0001-*.patch` name and later apply a glob: sorting by subject can reorder dependent commits. If a patch needs an intentional standalone adaptation, preserve the ATS commit as a single commit and document why its content cannot be identical.

Abort `git am` and investigate rather than guessing through a conflict. A conflict often indicates a wrong baseline, missing commit, or incorrect order.

## Verify the port

Compare the mapped standalone trees against ATS `master`:

- `code/include` against `lib/swoc/include`
- `code/src` against `lib/swoc/src`
- `unit_tests` against `lib/swoc/unit_tests`

Use recursive byte-for-byte comparisons such as `diff -qr`, then inspect every reported difference. Require exact equality for every file touched by the selected ATS commits unless a standalone adaptation is necessary. Compare the final difference set with the baseline difference set recorded earlier; investigate every new unexplained difference. Version headers, standalone build files, bundled dependencies, and ATS-specific test fixtures may legitimately differ, but their presence alone is not proof that a difference is intentional.

Also verify commit order and repository hygiene:

```bash
git log --reverse --format='%h %an <%ae>%n    %s' <previous-tag>..HEAD
git diff --check <previous-tag>..HEAD
git status --short --branch
```

## Build

Use the Fedora container version currently used by ATS CI or documented in the current ATS checkout; do not rely on a stale hardcoded tag. Mount the standalone checkout read-only and clone it inside the container so generated files cannot dirty the host checkout. After setting `LIBSWOC_REPO` and `FEDORA_VERSION`, run:

```bash
docker run --rm \
  -v "$LIBSWOC_REPO:/source:ro" \
  "ci.trafficserver.apache.org/ats/fedora:$FEDORA_VERSION" \
  bash -lc 'git clone --quiet /source /work/trafficserver-libswoc &&
    cd /work/trafficserver-libswoc &&
    pipenv install &&
    pipenv run scons -j"$(nproc)" libswoc &&
    pipenv run scons -j"$(nproc)" libswoc.static &&
    pipenv run scons -j"$(nproc)" libswoc.shared'
```

All three targets must pass. Treat dependency setup warnings separately from build failures, but report material warnings to the user.

## Publish

Immediately before publishing, fetch the canonical standalone remote again. Confirm that its `master` still equals the preflight revision, the worktree is clean, the new tag does not exist locally or remotely, and the tested `HEAD` is the commit to release. If upstream moved, reconcile the history and repeat affected comparisons and builds.

Construct annotated tag notes in the established format, using the release manager's identity and the current date rather than a hardcoded maintainer:

```text
Version <version> (<MM/DD/YYYY>, <release-manager>)

* <oldest commit hash> <subject>
* ...
* <newest commit hash> <subject>
```

Generate the entries from `<previous-tag>..HEAD` with `git log --reverse`. Inspect the notes, create the annotated `<version>` tag at `HEAD`, and push `master` plus the tag atomically to the canonical Apache remote when supported. If an atomic push is unavailable, push `master` first and the tag only after verifying the remote branch. Optionally synchronize a configured maintainer fork; never assume its owner or remote name.

This project's established release artifact is the annotated tag. Do not create a separate GitHub Release unless the user requests it or the repository's convention changes.

Finally, verify with `git ls-remote` that canonical `master` and the peeled annotated tag both resolve to the tested `HEAD`. Confirm the tag annotation and a clean local worktree, then report the version, commit, source-comparison result, build results, and published tag URL.
