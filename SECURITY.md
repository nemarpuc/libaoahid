# Security Policy

## Supported Versions

Only the [latest release](https://github.com/nemarpuc/libaoahid/releases/latest)
receives security fixes. Before 1.0.0 a fix may come with a change of the
minor number.

## Scope and Threat Model

- **Sensitive input payloads.** Input reports can contain keystrokes,
  credentials, precise touch locations, and medical or accessibility data.
  `libaoahid` never sends report payload bytes to its log sink. Do not add
  payloads to application logs, crash reports, fixtures, or public bug reports;
  reproduce issues with synthetic non-sensitive input instead.
- **Untrusted descriptors and host permissions.** Raw descriptors are untrusted
  input: `aoahid_spec_create_raw` reads item boundaries and works out wire lengths
  with checked arithmetic, but does not judge the descriptor: the phone's
  kernel does, when the device is registered. It makes no Android-support claim.
  USB device nodes allow arbitrary input injection into a connected Android
  device; `udev/51-aoahid.rules` is an example and must be reviewed against the
  host's user/group access policy before installation. Its active rule tags
  every USB device with `uaccess`, so any program of the user at the local
  seat can open any USB device; the file and
  [docs/QUICKSTART.md](docs/QUICKSTART.md#quickstart-linux) show the narrower per-vendor
  group rule.

## Reporting a Vulnerability

Report vulnerabilities privately through the repository's
[GitHub Security Advisories form](https://github.com/nemarpuc/libaoahid/security/advisories/new).
Do not put device serial numbers, USB traces containing accessory strings, or
undisclosed hardware identifiers in a public issue.
