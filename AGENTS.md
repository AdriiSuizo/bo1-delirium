# BO1 Delirium porting standards

The user supplied "PORTING-STANDARDS to learn from for other projects.md".
Apply its standards when maintaining this port.

- Compare the working BO1 development implementation before replacing a system.
  Transfer the existing fix and its required data together.
- Read original code and data. Distinguish a verified port from BO1 integration
  code. Cite TU8 addresses only after checking those addresses yourself.
- Check the current upstream implementation in a reference checkout owned by
  this project. Treat the user's other projects as read-only references.
- Preserve existing `ponytail:` lines verbatim until every gap they name is
  actually closed. Do not invent constants, empty callbacks or fallback
  behavior and describe them as original game logic.
- Make separate commits for separate fixes. Preserve line endings and working
  functionality. Keep diagnostic logging out of production changes.
- Add a regression test for ported logic and run release workspace tests.
  Verify the packaged mod with the original Steam installation through the
  supported Plutonium LAN launch path.
- Preserve source provenance and licenses. Keep game assets, executable images,
  keys, decompiler output and personal information out of commits and releases.
- Never bypass commit hooks. Do not add AI attribution lines to commits.
- The user tests visually. Do not record videos, capture screenshots or create
  handoff documents.
