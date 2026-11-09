# In-app update announcements

The update dialog reads the body of the latest GitHub Release in
`mohamedbassem612/Velune`. Write the user-facing features there when publishing
an update; no app code change is needed to change the announcement.

Example release description (replace the sample features with actual changes):

```markdown
![Velune update](https://example.com/velune-update-banner.jpg)

## Top Features
- Describe the first new feature here.
- Describe the second new feature here.

## Fixes
- Describe the fixes included in this release.
```

The first image is optional. Use a direct HTTPS image URL and put it at the very
start of the description. It appears as a wide banner; without it, the dialog
shows a Velune banner. The remaining description renders as Markdown. An empty
description displays a fallback message instead of invented release notes.

Attach the installable APK to the release and use a version tag higher than the
installed version, such as `v1.3.7`. The existing automatic check runs in release
builds at most once every 12 hours. “Remind Me Later” dismisses the dialog for the
current session; the normal check interval determines when it can appear again.

“Download Now” saves the APK in private persistent app storage. Interrupted
transfers keep their partial file; Retry resumes with HTTP Range and If-Range
when the server supplies a validator. A server that ignores ranges restarts the
transfer safely. Without a validator, Retry restarts to avoid mixing versions.
An invalid range is rejected; HTTP 416 clears the stale partial for the next retry.

Completed downloads show Install and remain saved when the dialog closes, the
installer is dismissed, or the app restarts. Install permission is requested only
when installing. Open Settings → Updates → Open available update to return to
the saved package, or reopen the app (cached newer releases bypass the network
check interval). On the first launch after successful installation, APKs for this app whose
version code is no higher than the installed version are deleted. Dismissing or
failing installation keeps the package. Uninstalling Velune or clearing app
data also removes saved downloads. Files are scoped to the release asset
URL, so a different release does not reuse an older APK.

Manual device checks:

- Publish a higher-version release with an APK, notes, and optionally a banner;
  launch an eligible release build and verify the dialog and scrolling.
- Try a narrow screen, landscape, large fonts, and Arabic.
- Download an APK and confirm progress and the Android installer prompt.
- Cancel mid-download and verify no installer opens; download again.
- Disable the network during download and verify the error and retry action.
- Verify a release without notes or an APK displays the appropriate message.
