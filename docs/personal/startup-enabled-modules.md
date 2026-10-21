# Startup with enabled personal modules

The first native startup smoke check used an empty profile, where modules are
disabled. A subsequent user launch restored `slightly-redesigned` and crashed.
The minidump resolved to `CheckSidebarIntegration()`, called by the module manager
from `Application::startLocalStorage()` before fonts and styles were initialized.
The normal font pointer was unavailable. The cached language log was simply the
last startup message before this separate failure.

Personal modules now start in `Application::run()` after the native style,
accessibility, text, corner and emoji initialization, before windows are created.
Other fork startup hooks keep their original storage-phase ordering. The fix
does not disable the module or alter its saved preferences or the account data.

The regression command exercises the actual packaged executable:

```sh
python3 fork/personal/tests/startup_mac_test.py 'out/personal/SeeGram Personal.app'
```

It creates a disposable profile with diagnostics and slightly-redesigned enabled,
requires both workers to reach Running, checks that the app remains alive, then
quits through the native IPC command. The original package failed with SIGSEGV;
the fixed Release passed. The incremental Release compiled only application.cpp,
and the staged app passed strict signature verification. The user's ordinary
main window was then observed after normal Finder activation and left open.

No startup instrumentation was added to production code. Crash dumps and
debugger captures remain local and are not included in Git or shared reports.
