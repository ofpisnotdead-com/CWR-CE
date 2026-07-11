# Contributing

Thank you for your interest in CWR-CE, the community continuation of the
Poseidon engine (Arma: Cold War Assault Remastered) source release. This
repository now also carries the Apple Silicon / iOS port (native Metal
renderer, macOS/iOS build support) contributed from the CWR-arm64 fork.

## Repository Model

This is a community fork, not the original Bohemia Interactive release, and
is not affiliated with or endorsed by Bohemia Interactive:

- Pull requests are welcome for fixes, portability work, tooling, tests, and
  documentation — including Apple Silicon/Metal/iOS-specific work.
- Ideas and feature proposals are welcome in issues or discussions.
- This is a community project, not a formal one — response times and review
  turnaround aren't guaranteed.
- Changes must respect the license terms in `LICENSE`, including the
  additional Section 7 terms.
- For bugs in official Bohemia Interactive builds distributed on Steam, use
  the upstream repository instead: <https://github.com/BohemiaInteractive/CWR>.

## Issues

Use GitHub issues for bugs, build problems, ideas, feature requests, and
coordination work. Use the issue template when opening a report.

- Include your platform (macOS/iOS/Linux/Windows), build preset or game
  version/commit, CMake/Clang versions where relevant, reproduction steps,
  expected behavior, and actual behavior.
- Do not report security vulnerabilities in public issues.

## Pull Requests

Before opening one:

- keep the change focused, and describe the problem being solved,
- include tests or manual verification notes when practical,
- follow the existing code style and testing conventions — see
  [`tests/README.md`](tests/README.md) and the CI workflow
  (`.github/workflows/build.yml`) for what's expected to pass,
- avoid committing game data, proprietary assets, generated dependency trees,
  or local build outputs — retail/demo game data questions are out of scope
  here; see the [README](README.md#getting-game-data-to-run-what-you-build)
  for where to get game data,
- make sure modified versions remain clearly marked as modified and are not
  presented as an official Bohemia Interactive product.

## Community Development

Ports, experiments, tooling, source cleanup, bug fixes, and long-running
feature work can be coordinated directly in this repository. Larger ideas are
easiest to review when they start as an issue or discussion before a large
pull request. You can also share ideas and community development work with
the community release authors at <https://github.com/ofpisnotdead-com/CWR-CE>.

## Licensing Of Contributions

The project source is licensed under the GNU General Public License v3.0 or
later, with additional Section 7 terms in `LICENSE`.

By contributing source changes here, you agree that your contribution is
provided under the same GPL-3.0-or-later license terms, including the
additional Section 7 terms in `LICENSE`.

When working with forks or modified versions:

- source changes are governed by the same GPL-3.0-or-later terms, including
  the Section 7 additional terms,
- you do not grant or imply any right to use "ARMA" or any other Bohemia
  Interactive trademarks,
- modified versions must be marked as modified and must not be presented as
  the original program.
