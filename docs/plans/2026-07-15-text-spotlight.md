# Text and Spotlight Implementation Plan

**Goal:** Ship the approved Release 2 text-editing and single-region Spotlight workflow on Eddy's existing scene, undo, contextual-bar, and export paths.

## 1. Retained text state and edit transactions

- Extend `TextItem` with plain/filled-label state, semantic annotation colour, alignment, and snapshot/apply helpers.
- Add one undo command for an entire existing-text edit.
- Keep new text live until commit; `Ctrl+Enter` or focus loss commits one add command, while `Esc` restores/removes without adding undo history.
- Double-click re-enters editing; `Enter` remains a newline.

## 2. Text width and contextual controls

- Add one right-side text-width handle; dragging changes auto-width to fixed wrapping width in one undo command.
- Add a compact `TextBar` for size, bold, alignment, and plain/filled-label style.
- Route changes through one state command per control action and keep the bar anchored like `RedactBar`.

## 3. Single retained Spotlight

- Add `SpotlightItem` with rounded-rect/ellipse holes and three fixed dim levels.
- Add the toolbar tool, contextual `SpotlightBar`, selection/resize support, clone contract, and compositing tests.
- Replace an existing Spotlight with a newly drawn one through one undo command; cancellation leaves the prior region untouched.

## 4. Verification

- Run focused RED/GREEN tests for text, handles, controller, bars, rendering, and editor integration.
- Run the full build and CTest suite, then inspect dark/light preview renders.
