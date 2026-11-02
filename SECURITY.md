# Security

Bunbun sits between your coding agents and their permission prompts, so security reports matter to
us.

## Reporting a vulnerability

Please **don't open a public issue**. Use GitHub's
[private vulnerability reporting](https://docs.github.com/code-security/security-advisories/guidance-on-reporting-and-writing-information-about-vulnerabilities/privately-reporting-a-security-vulnerability)
on this repository, with steps to reproduce and the Bunbun version (`VERSION`, or the exe's
properties). We aim to reply within a week.

## What we consider in scope

- Anything that lets a request be approved without a person clicking **Allow**.
- Another user, process or remote machine reaching the agent pipe (`\\.\pipe\bunbun-<SID>`).
- The hook talking to a pipe server owned by someone else.
- Hook installers damaging or leaking agent configuration files.
- Code execution through settings, skin packs or the settings window.

## Design notes

- The pipe's DACL admits only the current user, and remote clients are rejected.
- The hook checks the pipe server's identity before sending anything.
- When unsure, the hook defers to the agent's own prompt; it never approves on its own.
- No telemetry. Network access is limited to weather, lyrics, dictionary lookups, searches you ask
  for, and a daily read-only update check against GitHub's releases API (it never downloads or runs
  anything; it can be turned off).
