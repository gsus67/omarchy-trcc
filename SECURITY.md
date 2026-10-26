# Security

Please report vulnerabilities privately through
[GitHub security advisories](https://github.com/gsus67/omarchy-trcc/security/advisories/new)
instead of a public issue.

Every change runs, in CI:

- the Python tests, including static QML checks: every text element renders
  plain text and the panel QML references no remote URL;
- the native engine build and self-tests (theme validation, plain-text theme
  names, path boundaries, power sources);
- CodeQL `security-extended` analysis for C++, Python and the workflows.
