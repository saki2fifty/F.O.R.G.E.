# Maintaining the user manual

The user manual lives in `manual/`. Its audience is someone creating a game with
FORGE. Write instructions for the current product: prerequisites, exact menu and
control names, steps, expected results, saving/export behavior, troubleshooting
and supported limits. Explain unfamiliar terms when they first matter.

Keep issue histories, implementation rationale, milestones and validation reports
in technical documentation or the dated changelog. A limitation belongs in the
manual when it affects what a user can do; describe that limitation directly.
Do not describe planned behavior as available.

## Page structure and navigation

Start each page with one `# Title`. Organize the workflow with `##` and `###`
headings. Link related tasks instead of repeating their complete instructions.
Review both the feature overview and its limitations when updating a workflow.
Instructions involving Play must account for a game Camera, rendering/lighting
needs and authored versus runtime values.

`manual/README.md` owns the task groups and navigation order. Add a new page there
as a plain bullet link beneath the appropriate second-level heading. The offline
renderer uses the linked page's title for navigation and marks the current page.
Unlisted pages remain discoverable under **More guides** during development;
reconcile the contents before delivery.

The dependency-free renderer supports headings, paragraphs, flat numbered/bullet
lists, fenced code, inline code, bold text, relative `.md` links and local `.png`
images. Avoid tables, nested lists, raw HTML, remote links/resources and anchors
in this deliberately small subset. Use technical documentation for material that
needs a different presentation. Unsupported block forms fail validation.

## Screenshots

Keep canonical screenshots in `manual/images/` and reference those same assets
from the repository README. Use an actual FORGE capture with legible controls,
useful alternative text and a short caption describing the task:

```markdown
![Inspector showing an object's Transform and Components](../images/component-inspector.png)

Select an object in Hierarchy to inspect its components.
```

Images scale to the article width and link to their original full-size file. Use
PNG; the renderer verifies containment and the PNG signature, then copies bytes
unchanged. It does not decode or optimize images. Validate the source image and
visually review the page before delivery. Do not fabricate or retouch controls.

Keep capture provenance in [the screenshot record](images/readme/README.md):
source revision, test/build/artifact, original path, relevant rendering backend
and limitations. Preserve original dimensions and pixels for validation captures.
Reuse an attributable capture while its visible workflow remains current; replace
it when controls or behavior change. Test-fixture captures and software GPU
rendering do not establish physical desktop or hardware acceptance.

## Offline preview and checks

From the source checkout:

```text
python tools/build_manual.py --output /path/outside/source/manual-preview --build-id unassigned
python tests/manual_test.py
python tests/windows_example_packaging_test.py
```

Source and output must be separate folders. Open the generated `index.html` in a
browser, including a narrow window; follow page links and open screenshots. Check
long labels and code blocks. All files must work without a network connection
after the output folder is moved. The tests cover navigation, escaping,
containment, image bytes, relocation and the real ZIP writer's image/manifest
inclusion. Packaging tests use fixture executables; they are not Windows execution.

The existing `tools/package_windows.py` renders the manual once from current
sources, includes HTML, Markdown and referenced images, and records their hashes
in the package manifest. Do not create a second manual packaging path. Numbered
delivery and editor **Help → User Manual** integration follow [CI and packaging](ci.md).
