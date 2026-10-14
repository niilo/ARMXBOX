# GitHub Actions in this fork

## Builds

`ci.yml` runs on pushes to every branch (including `master`), pull requests,
and manual dispatch. Version tags (`v*`) use the release workflow instead.
CI builds desktop Linux, macOS, and Windows packages, runs the Linux Xbox
unit suite, and builds arm64 Android debug APKs on Linux and Apple Silicon
macOS. APK artifacts are named `x1box-android-<host>-Debug` and retained for
14 days.

Run `Build for Android` manually to choose `Debug` or `Profile`. Both use
development signing; production signing keys and Play Store publishing are
not required. APKs are available under the run's **Artifacts** section.
The Android toolchain matches `android/README.md`.
Linux packages link the pinned SDL3 statically because Ubuntu 22.04 does not
provide an SDL3 runtime package; macOS bundles it in the app.

## Releases and images

Publishing is intentional: `Pre-release` is manually dispatched, `Release
(Dispatched)` increments the highest `vX.Y.Z` tag and creates a draft, and
pushing a `v*` tag also creates a draft. Releases include desktop packages,
source, and Android debug APKs. Review a draft before publishing it.

`Build xemu-win64-toolchain` validates changed Dockerfiles. To publish,
dispatch it with `publish=true`; images go to
`ghcr.io/<owner>/<repository>-win64-toolchain-{gcc,llvm}` (lowercase).
Windows CI initially uses pinned public upstream compiler images and installs
this fork's SDL3/Vulkan dependencies. To use your published images, grant
this repository package access, make them public, and set repository variables
`WIN64_GCC_IMAGE` and `WIN64_LLVM_IMAGE` to their complete image references
(prefer immutable digests).

## Optional integrations

- Website: set `WEBSITE_REPOSITORY`, optionally `WEBSITE_REF` and
  `WEBSITE_WORKFLOW` (defaults: `master`, `build.yml`), and a `WEBSITE_TOKEN`
  secret with Actions write access to that website repository.
- PPA source snapshots: set `ENABLE_PPA_SNAPSHOT=true`. Publishing a stable
  release then replaces this fork's `ppa-snapshot` branch with vendored source.
  Connecting that branch to Launchpad remains a separate setup step.

Both integrations are disabled until configured. No workflow writes to the
upstream project, its website, or its container registry by default.

## Permissions and maintenance

Keep the repository's default workflow permission at **read**. Individual
release, package, and maintenance jobs request the write permissions they use.
The weekly Meson dependency updater uploads `subproject-updates.patch` and
`updated.json` as an artifact by default. Review and apply the patch with
`git apply subproject-updates.patch`.

To enable automatic update PRs, first allow GitHub Actions to create and
approve pull requests in repository Actions settings (GitHub combines these
capabilities), then set `ENABLE_DEPENDENCY_PRS=true`. The updater uses
`GITHUB_TOKEN`, creates one PR per batch, and explicitly dispatches CI because
token-created PRs do not trigger `pull_request` runs. It does not approve or
merge PRs.

Action revisions are pinned. Validate workflow edits with `actionlint` and
`git diff --check`; test build changes with the relevant local commands in
`android/README.md` and `CLAUDE.md`.
