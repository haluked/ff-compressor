# Security policy

## Supported versions

Only the latest 1.x release receives security fixes.

## Reporting a vulnerability

Please report security issues privately through GitHub's
**Security → Report a vulnerability** flow rather than opening a public issue.
Include the affected version, the steps to reproduce, and any relevant logs.

You can expect an initial response within seven days.

## Scope

FF Compressor is a local Win32 application: it makes no network calls and
stores its settings under `HKCU\Software\ff-compressor`. Bugs in the bundled
FFmpeg build should be reported to the
[FFmpeg project](https://ffmpeg.org/security.html) as well.
