# Security Policy

## Supported Versions

| Version | Supported |
| :--- | :--- |
| 0.1.x | Yes |

## Scope and Threat Model

- **Sensitive input payloads.** Input reports can contain keystrokes,
  credentials, precise touch locations, and medical or accessibility data.
  `libaoahid` never sends report payload bytes to its log sink. Do not add
  payloads to application logs, crash reports, fixtures, or public bug reports;
  reproduce issues with synthetic non-sensitive input instead.
- **Untrusted descriptors and host permissions.** Raw descriptors are untrusted
  input: `aoahid_spec_create_raw` validates HID 1.11 short-item structure,
  target parser bounds, and wire lengths, but makes no Android-support claim.
  USB device nodes allow arbitrary input injection into a connected Android
  device; `udev/51-aoahid.rules` is an example and must be reviewed against the
  host's user/group access policy before installation.

## Reporting a Vulnerability

Report vulnerabilities privately through the repository's
[GitHub Security Advisories form](https://github.com/nemarpuc/libaoahid/security/advisories/new).
Do not put device serial numbers, USB traces containing accessory strings, or
undisclosed hardware identifiers in a public issue.
