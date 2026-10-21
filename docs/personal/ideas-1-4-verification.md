# Ideas 1–4: native implementation verification

Date: 2026-10-21. Review base: `232f39432`.

## Scope

- Sidebar Serenade remains the existing switchable `slightly-redesigned` module.
  The user already verified its native behavior before this implementation.
- Session Suitcase adds common start/QR entry points, a native combined folder/ZIP
  picker, read-only bounded parsing, explicit preview selection and sequential
  account publication. Existing authorizations continue under the same keys.
- Gift Glance adds the ton–high badge and composite Value label, using one shared
  snapshot per session/slug and the existing Gift Details setting and API cache.
- Fork Footnote adds the requested bold personal-source link to SeeGram About.

The source specifications are the corresponding records in the `ideas` registry.
They describe planned acceptance coverage; their planning status does not certify
live session compatibility or rendered native results.

## Checks completed

`python3 fork/build-dev-mac.py --build-only` completed the final Debug build and
staged a separately identified app. Final changed source objects were checked
against source modification times. All ten changed/new integration translation
units also passed compiler syntax checking using the native build configuration.

The builder passed thirteen module/update checks and the native source/value
suite. The latter covers SQLite source immutability, mixed Telethon v7/Pyrogram v2,
missing identity fallback, WAL refusal, bot refusal, IPv6, authorization framing,
full-key/environment duplicate policy, exclusive import reservations, malformed
ZIP bounds, path traversal, multiple tdata roots, encrypted local passcodes,
restricted-profile selection, cached identity and rotated-file precedence.
Synthetic fixtures contain unusable all-zero authorization keys. Price checks
cover ton–high selection and invalid, missing or non-finite values.

The three supplied local examples were parsed without network connection. No
actual session credentials were added to fixtures, logs, source or reports.

A single final native startup check used a new empty working directory. The
Debug app stayed running for twenty seconds and exited normally through its
own `-quit` command; both processes returned zero. This exercised startup and
the new initial-screen construction without touching the personal account.

## Review

The Standards pass identified a translation-unit visibility error, excessive
upstream hooks, inline Reader definitions and multiline call formatting. These
were corrected, including moving new member implementations into fork files.

The Spec pass identified cancellation through generic box closure, durable test
configuration, concurrent bootstrap ownership, positive TTL extension, row click
routing and unknown confidence. These were corrected. The bounded follow-up
found no additional P1/P2 issue after fixing label event routing. Closing after
partial success also switches to a surviving first imported account.

Bootstrap destruction precedes persistent startup. The native
`MTP::Instance::Private::prepareToDestroy()` stops sessions and waits for its
threads; the import reservation remains held across this handoff and publication.
The new import path does not call server logout or auth-key destruction.
Authorization and encrypted environment configuration are saved synchronously
before registry publication; rollback removes only the newly allocated slot.

After the startup check, a final lifecycle review found that off-screen previews
could retain all snapshot-cache slots and react to enabling the setting. Gift
snapshots now follow the native heavy-view unload lifecycle; enabling only
requests repaint, and actual displayed drawing attaches the snapshot. The four
changed translation units passed the Debug compiler syntax check; the startup
check was not repeated for this isolated view-lifecycle change. The bounded
follow-up also added heavy-view unregistration on destruction and skips queued
requests whose previews have been released. Both fixes passed source review and
Debug compiler syntax checking, then compiled in the final Release build.

## Limits and user verification

Native visual checks are assigned to the user, as requested. No screenshots or
rendered-layout assertions were taken. The web-only UI auditor does not cover
these C++ widgets; its findings in old HTML examples and third-party documentation
are outside this implementation.

Live imported-session validity, real bootstrap cancellation and interrupted
storage publication have not been exercised with Telegram. Parser success and
source review do not substitute for those checks. The source client/script must
remain closed before Add and throughout use of the same authorization in SeeGram;
Telegram can revoke a key used concurrently, and local rollback cannot restore it.

Live authenticated see.tg responses, scrolling, MediaEditor, interface-scale/theme
combinations, keyboard behavior and the About link remain manual checks.

## Optimized build

Use `python3 fork/build-prod-mac.py` for the local Release app. It prepares missing
Release Qt/Crashpad/rnnoise/tde2e/tg_owt archives while retaining installed Debug
archives, checks the sources, builds, strips local/debug symbols, validates the Mach-O mapping and
signs/verifies the staged bundle. It preserves the existing personal portable
profile and refuses to replace a running app. Automatic replacement by official
SeeGram releases remains disabled; the manual update check and isolated update
candidate workflow remain the update path.

The local Release build completed successfully with `-O3` and `NDEBUG` for arm64.
The staged bundle passed deep, strict signature verification, contains the module
runner, has the personal bundle identifier and does not claim Telegram URLs.
All fourteen changed product translation units have fresh Release objects.
The signed executable is approximately 273 MiB and maps approximately 277 MiB,
below the arm64 mapping guard. The existing personal portable profile link remains
in place. No additional native startup or visual check was performed on Release.

Initial linking exposed missing Release rnnoise/tde2e/tg_owt archives and the
architecture-specific Crashpad helper path. The preparation script now handles
these before the app build; the final retry reused the application object cache.
Intermediate Debug, test and raw Release app bundles were removed after packaging
verification. Object caches, profiles and one previous personal app remain.

The user's subsequent launch exposed an enabled-module startup-order bug that
the empty-profile smoke check could not catch. The regression and repair are
recorded in [startup-enabled-modules.md](startup-enabled-modules.md). The repaired
Release passed the enabled-module native startup check and the ordinary main
window was observed; this supersedes the earlier lack of Release startup evidence.

The subsequent four screenshot corrections are source-only: the account expander
hit target follows the actual cover and centers its arrow, mouse clicks no longer
focus-outline the profile, the compact action row has no leading divider gap,
and QR login links use horizontal-only center alignment in the native layout.
No build or native verification of these changes was run, as requested by the user.
The installed Release still predates these four corrections.
