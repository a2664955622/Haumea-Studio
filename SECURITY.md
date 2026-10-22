# Security policy

## Reporting a vulnerability

Please don't report security problems in public issues, pull requests or on Discord.

Report them privately through GitHub: open the repository's **Security** tab and choose
**Report a vulnerability** ([direct link](https://github.com/itsneufox/Haumea-Studio/security/advisories/new)).
Only the maintainer sees the report. Say what is affected, how to reproduce it, and which version
and operating system you used.

You'll get an answer as soon as the maintainer can look at it. When a fix is released, the
advisory is published, with credit to you if you want it.

Examples of what to report: a map, prefab or catalog file that makes the editor run code or write
outside the folders it should, a problem in the updater's download checks, or a way for another
program to use the Namaka (AI assistant) connection without its key.

## Supported versions

Fixes go into the latest release of each channel:

| Version | Supported |
| --- | --- |
| The latest **Production** release | Yes |
| The latest **Beta** release | Yes |
| Older releases, Dev and pull request builds | No: update first |

The editor's updater (Help > Check for updates) installs the latest release of your channel.
