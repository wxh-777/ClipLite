# Security Reports

中文：[SECURITY.md](SECURITY.md)

Do not publish an unpatched vulnerability, exploit details, clipboard contents, history files, or sensitive logs in a public issue.

There is currently no dedicated security email address for this repository. Contact the maintainer through a private channel and include `Security report` in the subject. A report should include:

- Affected ClipLite and Windows versions.
- Minimal reproduction steps or a safe verification method.
- Impact, prerequisites, and possible mitigations.
- Whether the issue has been disclosed elsewhere and a preferred disclosure timeline.

Do not attach real clipboard history. Use an isolated test directory and synthetic content when reproducing storage issues.

The maintainer will confirm the issue, assess its impact, and update the changelog after a fix. ClipLite is source-available and there is no guaranteed remediation timeline. Do not use it as the only protection in a high-security environment.
