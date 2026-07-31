# Security

## Reporting

Report a vulnerability privately through GitHub's
[security advisories](https://github.com/JPHutchins/salix/security/advisories/new).
Please do not open a public issue for one.

## Scope

salix is a C extension. A crash reachable from ordinary Python — a segfault, a
use-after-free, a read past the end of a field table — is in scope whether or
not it is exploitable, because the memory unsafety is the bug and the
exploitability is a detail. Report it here rather than as an issue if you are
unsure which it is.

Out of scope: passing deliberately hostile arguments to `ctypes`, to
`object.__setattr__`, or to anything else that reaches an instance's memory
without going through the API. `salix/__init__.pyi` is the API.

## Supported versions

Nothing has been released yet. Once it has, fixes land on the latest minor
version.
