# Contributing to Velocity

We would love for you to contribute to Velocity!

Velocity is a speed testing tool that can run tests against both
[Ookla](https://www.speedtest.net/) and [LibreSpeed](https://librespeed.org/)
servers.

## Filing a bug

Please file bugs on the
[issue tracker](https://github.com/vani-tty1/velocity/issues)
after checking that the bug has not already been reported or fixed in the
latest `preview` branch.

A good bug report includes:

 * What you expected to happen and what happened instead
 * Steps to reproduce the problem
 * Your distribution and version
 * Which server type you were testing against (Ookla or LibreSpeed)
 * Any relevant output or error messages

__Please do not file issues for feature requests.__
Open a thread in
[Discussions](https://github.com/vani-tty1/velocity/discussions) first so the
idea can be talked through before any code is written.
Once there is agreement on the approach, open a Pull Request and I'd be happy
to review it and help you get it merged.

## Asking for Help

If you have questions, or would like to start on a new feature and want to get
oriented first, start a thread in
[Discussions](https://github.com/vani-tty1/velocity/discussions).


## Branches

Development happens on the `preview` branch. Please base your work on it and
target your Pull Request at it.

## Portability

Velocity is primarily focused on **GNU/Linux**.

However, we do accept patches for other platforms. Please help me by keeping
things working for yours, as we cannot test every possible configuration.

## Testing

Before opening a Pull Request, please make sure the project builds and that
nothing was broken by your changes.

If your change touches server selection or the test flow, please try it with
**both** Ookla and LibreSpeed servers, since they behave differently.

## Licensing

Contributions should be licensed under the same license as the project. See
the `COPYING` file for details. By submitting a Pull Request you agree to
license your contribution under those terms.

## Commit Messages

We follow a simple convention of `type: short summary`, with an optional body
explaining the why.

Common types are `feat`, `fix`, `build`, `docs`, `refactor` and `chore`.

```
build: specify which server type to use in testing

alternatively, for testing purposes, you set it to use Ookla servers
too not just librespeed servers
```

Please keep each commit focused. Check what is staged with `git status` and
`git show --stat HEAD` before pushing, so unrelated files do not slip in.

## Coding Style

Please match the style of the surrounding code. In general:

 * Keep functions small and focused
 * Use descriptive names
 * Do not mix unrelated formatting changes with functional changes


## Documentation

Most code should be obvious from its names and structure. Add comments when
something is particularly tricky, or when you are being clever.

If your change alters behaviour or options, please update `README.md` as well.

## Submitting a Pull Request

 1. Fork the repository and create a branch from `preview`
 2. Make your changes in small, focused commits
 3. Make sure it builds and the tests pass
 4. Push to your fork and open a Pull Request against `preview`
 5. Describe what the change does and why, and link any related issue
