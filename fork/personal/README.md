# SeeGram Personal

Personal macOS fork of SeeGram. The accepted scope is in
[the foundation spec](../../docs/personal/foundation.md).

## Fast module tests

```sh
cmake -S fork/personal/tests -B out/personal-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out/personal-tests
ctest --test-dir out/personal-tests --output-on-failure
```

These tests compile the production manager, launch real worker processes and
cover a killed process, self-check timeout, runtime timeout, interface mismatch,
preference restoration, sanitized reports and the built-in diagnostic worker.
They require a C++20 compiler, CMake, Ninja and Python, with no Telegram login.

## Development app

Prepare dependencies with the upstream macOS instructions first. Keep Libraries
next to this checkout. Then use the repository's development entry point:

```sh
python3 fork/build-dev-mac.py
```

The output is `out/personal/SeeGram Personal.app`. It uses a distinct bundle ID
and the `SeeGram Personal` application-support folder. It never copies release
profiles or registers itself as the Telegram link handler. Development updates
are disabled; changing a bundle identifier alone is insufficient profile isolation.
The local build command runs the fast module and update-candidate checks first.
A failing check stops the command before the installed personal app is replaced.

Supply your own Telegram API credentials through the existing ignored
`fork/private/api_credentials.cmake`, or the matching environment variables.
For development without normal account use, `TDESKTOP_API_TEST=1` enables the
limited upstream test credentials. Keep credentials out of commits and chat.

The module protocol and reports carry structured status only. Worker stderr is
discarded and arbitrary worker output is never copied into a diagnostic report.
The current diagnostic worker checks startup and process responsiveness; it does
not prove every future feature's behavior. Feature-specific self-checks and tests
must be added with that feature.

## Manual updates

Settings → Plugins → Check SeeGram updates queries the public GitHub release
endpoint only when clicked. It reports a newer base release or a check failure;
it never replaces the app with the official SeeGram binary.

After committing personal changes, prepare a candidate beside the checkout:

```sh
python3 fork/personal/update_candidate.py --release vX.Y-Z --worktree ../seegram-personal-candidate
```

The candidate replays only commits after the explicitly recorded SeeGram base.
The current branch and working files stay intact. Conflicts leave the candidate
and a structured report for inspection. Review and test a successful candidate
before building it; run the development builder from its checkout. Initialize
its submodules before building. A candidate is not an installed update.

Failure history is saved atomically next to module preferences, capped at 64
structured events and restored across app restarts. Reports include timestamps
and indicate when the local history could not be saved.

Full build automation, a background GitHub runner and release signing setup are
deferred by the user. No scheduled job or background agent is installed.
