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

“Download Now” asks for Android's install permission when needed. The dialog
keeps the release notes visible during download, shows a percentage when the
server provides a content length (otherwise an indeterminate progress bar), and
opens Android's installer only after a complete download. Cancel stops the
download at the next network read, removes the partial file, and returns to the
announcement. A blocked read is bounded by the existing 30-second read timeout.
Failures remain in the dialog with a Retry action.

Manual device checks:

- Publish a higher-version release with an APK, notes, and optionally a banner;
  launch an eligible release build and verify the dialog and scrolling.
- Try a narrow screen, landscape, large fonts, and Arabic.
- Download an APK and confirm progress and the Android installer prompt.
- Cancel mid-download and verify no installer opens; download again.
- Disable the network during download and verify the error and retry action.
- Verify a release without notes or an APK displays the appropriate message.

## Local 1 MB demo

Open Settings → Updates → Try a demo update (1 MB) in a build containing this
change. It opens the same dialog without requiring a newer GitHub release or
waiting for the automatic check. Tap Download Now to rehearse progress for about
five seconds, Cancel to stop, or Remind Me Later to close it. Completion stays
visible until OK is tapped.

This is explicitly labelled a demo: it generates exactly 1,048,576 bytes locally,
uses no network, never opens the installer or asks for install permission, and
deletes its temporary file after success, cancellation, or failure. The real
update path is unchanged. Verify completion and cancellation, then reopen the
demo to repeat it.
