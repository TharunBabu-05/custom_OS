# README artwork

Original code-generated graphics for the GitHub project README:

| Asset | Purpose |
| --- | --- |
| `hero.svg` | Circuit-board identity graphic with moving trace dashes and status pulse |
| `boot-replay.gif` | 19-frame looping illustration of captured kernel output |
| `boot-replay-still.png` | Static alternative to the animated console |
| `boot-path.svg` | Implemented firmware-to-monitor boundaries with an animated trace |
| `footer.svg` | Planned memory-management milestone and moving trace |

The GIF replays selected commands from the real serial log, with some output
shortened for readability and illustrative timing. It is not a video recording
or an additional hardware test. Original proof screenshots are in `proofs/`.
SVG animations use declarative SMIL, with useful static layouts when animation
is unavailable. No scripts, embedded external resources or external fonts are
required by the artwork. The README's optional live badges use Shields.io.

Regenerate on Windows with System.Drawing and Node.js installed:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/readme-assets.ps1
```

Sources: `tools/readme/RenderBootReplay.cs` and
`tools/readme/generate-vectors.mjs`. These host-only documentation tools are
independent of kernel builds. The generator decodes every GIF frame to verify
the generated animation. No proof screenshot is read or altered by it.
