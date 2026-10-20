# slightly-redesigned

Built-in opt-in sidebar presentation based on local idea #1, Sidebar Serenade.
The source specification and user-provided visual references remain in the
local ideas registry. It changes the active account's ordinary sidebar only.

## Behavior

- Use the live profile renderer, including profile colors, pattern, avatar,
  pinned gifts, name, badges, rating and status. Profile decorations are not
  replaced with screenshots or fixed sample values.
- The card opens My Profile. Its separate emoji/star action uses the existing
  status picker or Premium offer. The full profile keeps its current actions.
- Music appears only when available and opens the existing music list.
- Keep the separate animated account expander, unread count and account list.
- Place New Group, New Channel, Contacts and Calls in one row. Use the original
  menu icons, renderers and handlers, including the first two context menus.
- Remove the redundant My Profile and textual emoji-status rows. Keep the
  divider's spacing before the compact actions, with no separator line.
- Preserve the remaining menu commands, visibility rules, themes and scrolling.
  Support mode keeps its existing presentation.

## Activation and recovery

The module defaults to off. Its process must finish the manager's self-check
before the interface uses it. A host integration check validates the available
profile height and the four original icon dimensions on every activation,
including restart. A failed check keeps this module off with a structured reason.

Each menu captures the current presentation when opened. If activation changes
or the worker fails while it is open, that menu closes; reopening uses the
current healthy presentation. Other modules and the session remain independent.
Disabling restores the existing sidebar and keeps the module preference history.

The Qt renderer and its narrow interaction adapter run in the app process.
The worker checks startup and responsiveness; it cannot prove that every native
rendering or Telegram action works. Native verification is required for changes
to this integration and after source updates.

## Verification

The module tests exercise the real worker's activation and disable. The host
check test rejects one module, preserves another and reruns the check on retry.
Debug compilation, package signing and all 13 standalone checks passed.
The native pre-login Plugins panel shows the module off by default and Running
after activation; the diagnostic module continues running.

Native checks on the signed personal app passed: live avatar, gifts, pattern,
music and online-status updates; separate profile and emoji-picker navigation;
playlist opening without playback; account expansion; the first screen of all
four commands; Tab focus and Return activation. No group, channel or call was
created. Stopping only this module's worker closed the redesigned menu and
restored the standard menu, while diagnostics stayed Running. The Plugins panel
reported the failed process, and retry restored the module.

The user confirmed the feature works and requested finishing. The full theme,
scale, minimum-height, multiple-account and non-Premium matrix was not completed.
Right-click checks through native automation were inconclusive; the existing
context-menu handler is retained. Final long-name geometry reserves the account
control width when positioning the name and badge.

Two independent review agents were requested for Standards and Spec; both
failed before reviewing because the service usage limit was reached. Local
source review was completed; no independent-review pass is claimed.
