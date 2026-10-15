# GitHub Actions in this fork

## Builds

`ci.yml` runs on pushes to every branch (including `master`), pull requests,
and manual dispatch. Version tags (`v*`) use the release workflow instead.
CI builds desktop Linux, macOS, and Windows packages, runs the Linux Xbox
unit suite, and builds arm64 Android debug APKs on Linux and Apple Silicon
macOS. APK artifacts are named `x1box-android-<host>-Debug` and retained for
14 days.

Run `Build for Android` manually to choose `Debug`, `Profile`, or `Release`.
Debug and Profile use development signing; Release requires the signing secrets
below and produces a signed APK and AAB. Packages are available under the run's
**Artifacts** section.
The Android toolchain matches `android/README.md`.
Linux packages link the pinned SDL3 statically because Ubuntu 22.04 does not
provide an SDL3 runtime package; macOS bundles it in the app.

## Releases and images

Publishing is intentional: `Pre-release` is manually dispatched, `Release
(Dispatched)` updates the Android version and creates a matching tag and draft,
and pushing a matching `v*` tag also creates a draft. Standard releases include desktop
packages, source, and signed Android release APKs and AABs built on Linux and
macOS. `Pre-release` continues to include Android debug APKs. Review a draft
before publishing it; an AAB is uploaded to Google Play separately.

### Create a versioned release

Open **Actions → Release (Dispatched) → Run workflow**, select `master`, and:

- Leave `version_name` and `version_code` empty to increment the current
  Android patch version and build number. From `1.2.9` / `30`, the next release
  becomes `versionName = "1.2.10"`, `versionCode = 31`, and Git tag `v1.2.10`.
- Enter explicit values for another release, such as `1.3.0` and `40`. Both
  values must increase; `version_name` must use `X.Y.Z`.
- Enable `dry_run` to preview the next values without committing, tagging,
  building, or creating a release. Preview runs may use a test branch.
- Run with `dry_run` disabled to commit both fields in
  `android/app/build.gradle.kts` on the default branch and push that commit and
  its annotated tag in one atomic operation. The tag message records
  `versionCode`; its name matches the user-facing `versionName`.

The release builds that exact version commit and creates a **draft** containing
signed APKs/AABs and desktop packages. Review the draft under **Releases** and
click **Publish release** when ready. Existing tags, decreasing versions,
missing signing secrets, and rejected branch updates stop the operation.

For a manual release instead, edit both Android version fields, commit them,
then push `v<versionName>` pointing to that commit. The release workflow checks
that the tag matches `versionName` before building. Do not run the automatic
version bump as well if you already prepared the version manually.

The workflow calls the release build directly because pushes made with
[`GITHUB_TOKEN`](https://docs.github.com/en/actions/concepts/security/github_token)
do not trigger another push workflow. No additional token or broader default
workflow permissions are needed. Run the release helper's local integration
tests with `python3 .github/scripts/test-android-release.py`; CI runs them too.
Android's [versioning documentation](https://developer.android.com/studio/publish/versioning)
explains the separate build number (`versionCode`) and display version
(`versionName`).

### Android release signing

Configure these repository Actions secrets in this fork:

| Secret | Value |
| --- | --- |
| `ANDROID_KEYSTORE_BASE64` | Base64-encoded release keystore (`.jks` or `.p12`) |
| `ANDROID_KEYSTORE_PASSWORD` | Keystore password |
| `ANDROID_KEY_ALIAS` | Alias of the signing key |
| `ANDROID_KEY_PASSWORD` | Signing key password |

Use the same key for subsequent versions and keep a secure backup of the
keystore and credentials. A new fork key cannot update an installation signed
with the upstream project's key. Never commit signing files or passwords.
GitHub's CLI accepts secret values on standard input, for example:

```bash
base64 < /path/to/release.jks | tr -d '\n' | \
  gh secret set ANDROID_KEYSTORE_BASE64 --repo niilo/ARMXBOX
gh secret set ANDROID_KEYSTORE_PASSWORD --repo niilo/ARMXBOX
gh secret set ANDROID_KEY_ALIAS --repo niilo/ARMXBOX
gh secret set ANDROID_KEY_PASSWORD --repo niilo/ARMXBOX
```

Only release entry points forward signing secrets through the reusable build
workflows; normal CI does not receive them. The Android job validates the key
before compiling, creates temporary signing files with restricted permissions,
and removes them even after failure. APK and AAB signatures must verify against
that key's certificate before upload; missing or invalid secrets fail the job.
Only package files are uploaded, and signing files are excluded from caches.
See [GitHub's reusable workflow secret documentation](https://docs.github.com/en/actions/how-tos/reuse-automations/reuse-workflows#passing-secrets-to-nested-workflows)
for the explicit forwarding used here.

To test signing without creating a tag or release, run `Build for Android`
manually with `variant=Release`. Artifacts contain
`x1box-android-release-<host>.apk` and `.aab`. This build-only workflow uses the
versions already committed in `android/app/build.gradle.kts`; it does not bump
versions or create a tag. Use `Release (Dispatched)` for the coordinated bump.

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
