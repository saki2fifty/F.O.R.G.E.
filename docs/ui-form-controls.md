# Native UI form access

UiPresenter::form_controls, form_options and form_control_action operate on the
displayed native document set through the same owner as text_fields. The private
ui_control_access helper supplies classification, IDs, visibility/admission and
one bounded walker. It preserves the text-only DOM scan/count policy and uses an
independent form scan including native non-DOM option children. There is no form
registry, copied widget implementation, second DOM, retained option token store or
alternate gameplay/authoring writer.

Forms include checkbox/radio input, select and read-only progress; other inputs
and password/text/textarea values are outside this snapshot. Option text reads
bounded public native text leaves, refuses nested controls and never serializes
source. Native value admission and secret handling remain explicit. See
[MCP form tools](mcp.md#native-ui-form-tools) for fields, budgets and failures.

At the pinned native revision, dropdown GetNumOptions/GetOption can normalize
pending DOM children and emit Change. Reads call those getters only after the
native control has no pending DOM children, otherwise returning readiness false.
Native Update remains the normalization owner. Read calls never move focus or
invoke native actions. Option state uses the existing asset content digest over
current scope/index/value/text/admission, excluding the mutable selected hint.

Actions share current document scope, presentation revision, unique-ID, held-input,
output/surface mapping and native focus gates with text actions. Call-local native
observers detect retirement. Identity, kind and option facts are checked again
after native blur/focus callbacks. Already-satisfied requests perform no focus or
activation. Native Click and SetSelection own events and radio-group semantics;
no extra Change event is dispatched. Results report delivery separately from
observed change, with null change if the activated target detaches, retires or changes kind.
Callbacks keep their ordinary native effects; there is no authored rollback.

RuntimeUiHost, the editor provider and the MCP workflow host call these presenter
methods. Automation metadata supplies the five MCP bindings and their schemas;
read and execute grants, Play scope, ordinary mutation receipts and stale guards
retain their owners. The existing gameplay input release runs before semantic
actions. Source creation, bindings, Scene history, Save, packaging and standalone
input retain their existing owners. MCP operates in editor Play; no server is
added to an exported game.

The nonvirtual PImpl presenter gains methods without data-layout changes. Its
header is not part of the installed native gameplay SDK. This feature does not
change SDK fingerprints, serialized schemas, dependency pins, or runtime editing
and shaping capability profiles.

Native extension callbacks must retain removed native owners until synchronous
dispatch returns, or defer removal. Observers do not keep widgets alive; immediate
dropdown destruction inside Change is a pinned native limitation. See
[the independent finding and boundary](dependency-known-issues.md#native-ui-dropdown-callback-lifetime).

Focus callbacks also have a native retained-pointer limit. Defer focused-element
removal; deliberate early detach requires the native caller to retain the owner,
restore attached native focus after dispatch and release the owner afterwards.
See [focus callback removal](dependency-known-issues.md#native-ui-focus-callback-removal).
