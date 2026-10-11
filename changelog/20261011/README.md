# 2026-10-11

## Transform timeline local validation

The transform animation bundle is implemented and locally validated. Six focused
runtime/regression suites pass under ASan/UBSan. Mouse/keyboard timeline checks
pass at 65/100/200% UI scale, and all eight Gallery scenes preserve their authored
content through the headless editor API. Held keys and nonseamless loops use the
existing presentation snap mechanism; seamless motion stays interpolated.

The Windows-target Gallery clip is published and included by packaging. The
manual, CI selections, exact SDK header fingerprint and content/export adapters
are reconciled. No new Windows ZIP, Windows execution or physical acceptance is
claimed for this bundle.

## Two-clip skeletal transitions

Animator now has an optional second clip, a blend target and transition duration.
Changing the target from C++ fades from the currently evaluated mix. The two clips
share normalized phase and use pinned Ozz local-transform blending; pause, Step,
resource admission, model-node application and validated recovery retain their
existing owners. Single-clip documents use additive defaults. Rebuild native
modules against the matching SDK after updating.

Animation Preview opens as a central document with temporary playback, scrubbing,
blend controls and one-step application of settings to Inspector's Animator.
Preview uses a copied world and stops on game Play, scene edits, publication or
close. Feature Gallery scene 09 uses an original skinned column with two clips;
normal export discovers both cooked clip references and needs no source glTF.
Two-clip morph curves are diagnosed as unsupported. This bundle adds no animation
graph, retargeting or new language.

Local validation passed: 11 affected runtime/regression suites under sanitizers,
two shared C++ SDK suites, actual preview controls at 65/100/200% scale, the
existing timeline consumer, and admission/persistence of all nine Gallery scenes.
Source-free relocated playback is included in the numerical blending gate.

Windows execution and physical acceptance remain pending a separately authorized
delivery. No new Windows ZIP or GitHub push is part of this local batch.


## Named skeletal animation states

Reusable state sets now name clips and assign stable numeric IDs, an entry,
loop/speed and incoming fade time. C++ selects IDs on the existing Animator.
Ozz advances independent clip clocks, blends local poses and freezes the current
pose when a fade is interrupted. Short nonlooping clips finish their fade before
stopping. Existing direct/two-clip playback remains available.

The central Animation States document supports source history, Save/Publish,
reordering and atomic scene attachment. Animation Preview adds temporary named
state selection. Typed set dependencies participate in source reference review,
copy/rename and ordinary source-free export. Rebuild native modules with the
matching SDK after updating.

Gallery scene 10 contains actual CC0 Quaternius Idle/Walk/Sprint character clips,
three independent skins, editable state source, cooked Windows-target bundles,
license and immutable provenance. Editor package selection includes its GLB and
license. Windows execution/physical acceptance remains pending; no new ZIP or
GitHub push is part of this local batch.
