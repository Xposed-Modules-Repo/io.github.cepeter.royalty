# Security Policy

## Supported versions

| Version | Supported |
|---|---|
| 2.0.x | Yes, after stable release |
| 1.x | No |

Version 1.x is unsupported because its custom ART entry-point hook was not ABI-safe.

## Reporting

Use GitHub’s **Security → Report a vulnerability** workflow. Do not publish hidden-chat identifiers, Telegram data, device logs containing personal information, or proof-of-concept exploits in a public issue.

Include the module version, Vector/LSPosed version, Android version, Telegram version, hook-status panel, reproduction steps, and sanitized logs.

## Security boundaries

- Hook failures fail open so Telegram remains usable.
- The catalog Binder service validates that the calling UID owns `org.telegram.messenger`.
- XSharedPreferences are read through the framework safe zone.
- The module does not provide secrecy against root, framework compromise, malicious Telegram builds, or physical compromise of an unlocked device.
