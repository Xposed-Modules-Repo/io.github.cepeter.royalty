# Spec: Royalty modern interface

## Objective
Modernize the existing configuration dashboard without changing catalog, selection, notification, or persistence behavior.

## Constraints
- Keep Java and platform Android views; add no UI framework dependency.
- Support API 27–35, system light/dark mode, and accessible 48dp+ touch targets.
- Preserve every existing control and lifecycle behavior.

## Interface
- Branded Royalty header and concise privacy subtitle.
- Rounded status, notification, and dialog-selection surfaces.
- Clear section hierarchy, modern typography, muted supporting text, and purple accent actions.
- Persistent primary Save action and secondary Refresh action.
- Modern multiple-choice rows with comfortable spacing.

## Success criteria
- All existing UI contracts still pass.
- Light and dark color resources exist.
- Refresh, save, notification suppression, catalog selection, timeout guidance, and status output remain functional.
- Debug unit tests, lint, and APK build pass.
