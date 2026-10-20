---
version: alpha
name: SeeGram Personal
description: Personal macOS settings built from SeeGram's existing Qt components.
omitted:
  - section: colors
    reason: Runtime palette and selected theme remain canonical in Telegram/lib_ui/ui/colors.palette.
  - section: typography
    reason: Existing Qt style fonts and user-selected scaling remain canonical.
  - section: spacing
    reason: Existing settings and layer style files own scaled dimensions.
  - section: rounded
    reason: Existing settings controls and GenericBox own shapes.
  - section: components
    reason: Existing C++ components are mapped in the Components section.
---

# SeeGram Personal interface

## Overview

The references are SeeGram's existing settings, sidebar and full profile. This personal macOS application
adds modules for one user; product familiarity takes precedence over a separate
visual identity. The Plugins page shows activation, health and recovery clearly.
Personal additions support Russian and English; other locales use English.
The accepted behavior is recorded in docs/personal/foundation.md.

Runtime Qt styles remain canonical. This document records their ownership;
it does not generate a second palette or duplicate numeric design tokens.

## Colors

Use existing semantic palette roles through settings styles. Respect the active
light or dark theme. Errors have a written reason and an off switch, so their
meaning does not depend on color.

## Typography

Use existing settings button and description text styles. Preserve Cyrillic
rendering and user-selected interface scale; do not add custom fonts.

## Layout

Use Ui::VerticalLayout, settings rows and existing spacing helpers. Long
descriptions wrap. Both the signed-in section and pre-login dialog call the
same content builder. The dialog uses the existing GenericBox scroll container.

## Elevation & Depth

Follow the existing settings layer and GenericBox modal. Add no extra shadows
or decorative surfaces. Escape and Close follow the standard layer behavior.

## Shapes

Inherit control, divider and box geometry from Qt style files and their generated
headers. Scale through the existing style system; avoid screen-local dimensions.

## Components

| Capability | Canonical owner | Source of truth | Allowed variants | Verification |
|---|---|---|---|---|
| Toast | Ui::Toast | Telegram/lib_ui/ui/toast | Existing save success/failure | Native export and saved JSON |
| Toggle | Fork::SettingsRows::AddToggle | fork/settings_rows.h | Module health with version and accessible description | Native activation and process failure |
| Dialog | Ui::GenericBox | Telegram/lib_ui/ui/layers | Plugins before login | Native Close and Escape |
| File export | FileDialog::GetWritePath | core/file_utilities.h | User-selected JSON path | Native save panel and report validation |
| Sidebar profile | Info::Profile::TopBar | info/profile/info_profile_top_bar | MainMenu interaction policy, original renderer and live data | Native profile, status and gift clicks |
| Sidebar actions | Ui::SettingsButton + Settings::AddButtonIcon | window/window_main_menu and settings/settings_common | Four icon-only buttons with the same callbacks, palette and glyphs | Native commands, context menus and keyboard focus |

Settings dimensions flow from Telegram/SourceFiles/settings/settings.style and
Telegram/lib_ui/ui/layers/layers.style into generated style headers, shared
settings controls and the personal module view. Palette changes remain upstream.
The slightly-redesigned module defines its compact geometry in
fork/personal/slightly_redesigned.style. Profile fonts, decorations and icon
assets stay owned by the existing profile and menu styles. These values flow
through generated Qt style headers; no web preview tokens enter the app.

Activation shows Checking, Running or a written failure reason. Failed modules
stay off. Update checking has Idle, Checking, Current, Available and Failed
states; duplicate clicks are disabled while the request is pending. Failure
offers retry. Status is available to accessibility APIs as well as visible text.

## Do's and Don'ts

- Reuse the shared content builder for both entry points.
- Keep module state and error visible beside its version.
- Keep the existing settings typography, themes, icons and keyboard behavior.
- Do not put implementation logs, secrets or chat data in the user flow.
- Do not add web components or redesign the surrounding SeeGram settings.
