# Contributing

Thank you for your interest in CWR-arm64, the Apple Silicon / iOS community
fork of the Poseidon engine (Arma: Cold War Assault Remastered) source
release.

## Repository Model

This is a community fork, not the original Bohemia Interactive release, and
is not affiliated with or endorsed by Bohemia Interactive:

- Pull requests are welcome. Fork the repo, branch off `main`, and open a PR.
- This is a small community project, not a formal one — response times and
  review turnaround aren't guaranteed.
- Issues are open for bugs, build problems, and feature discussion specific
  to this fork (Apple Silicon/Metal/iOS support, CMake/build tooling, etc.).
  For bugs in official Bohemia Interactive builds distributed on Steam, use
  the upstream repository instead: <https://github.com/BohemiaInteractive/CWR>.

## Issues

Use GitHub issues for bugs, build problems, or feature requests specific to
this fork.

- Include your platform (macOS/iOS/Linux/Windows), build preset, CMake/Clang
  versions, reproduction steps, expected behavior, and actual behavior.
- Do not report security vulnerabilities in public issues.

## Pull Requests

- Keep changes focused and scoped to the fork's own concerns (build
  portability, native Metal rendering, iOS support, and related tooling).
- Follow the existing code style and testing conventions — see
  [`tests/README.md`](tests/README.md) and the CI workflow
  (`.github/workflows/build.yml`) for what's expected to pass.
- Retail/Demo game data questions, asset questions, and content requests are
  out of scope for this repository — see the [README](README.md#getting-game-data-to-run-what-you-build)
  for where to get game data.

## Community Development

If you are building a port, mod, experiment, or long-running fork of your
own, you're welcome to do so under the license terms in `LICENSE`. You can
also share ideas, improvements, and community development work with the
community release authors at <https://github.com/ofpisnotdead-com/CWR-CE>.

## Licensing Of Contributions

The project source is licensed under the GNU General Public License v3.0 or
later, with additional Section 7 terms in `LICENSE`. By submitting a pull
request, you agree your contribution is licensed under the same terms.

- source changes are governed by the same GPL-3.0-or-later terms, including
  the Section 7 additional terms,
- you do not grant or imply any right to use "ARMA" or any other Bohemia
  Interactive trademarks,
- modified versions must be marked as modified and must not be presented as
  the original program.
