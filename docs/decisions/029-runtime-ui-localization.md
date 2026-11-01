# ADR029 — Runtime UI localization

Date: 2026-11-01. Accepted.

## Ownership

An optional Scene UI Localization controller contains a typed catalog reference
and Locale. Its runtime-copy component is the locale authority. UI commands, live
tuning and project C++ change that same value. UiRuntime produces copied settings;
UiPresenter owns their native bindings and UiResources owns admitted immutable
catalog data for each candidate. There is no independent locale service, cache,
thread, clock or player-preference database.

The first profile admits one controller and one catalog per Scene UI context.
This matches the existing shared presentation context and its context-wide native
transform registration. Independent Worlds retain independent locale values.
Ordinary Scene/Prefab reference, history, Stop, recovery and export rules apply.
Stop restores authored settings; private recovery uses the runtime Scene snapshot.
It does not introduce automatic durable player-language preferences.

## Source and access

Typed `.localization.json` tables use the existing source-document, history,
import/publication, identity and immutable artifact owners. Stable string keys
identify literal messages. The default locale owns the keys; other locales may
omit translations and fall back. Existing source/editor operations also serve
automation/MCP. A key rename requires updating its calling UI expressions.

The bounded profile has eight locales, 128 default message keys, 256 KiB source
and 1 KiB per message. Locale tags use an explicit ASCII hyphenated subset with
case-insensitive selection. Selection tries the exact tag, primary language,
then default. A missing translation uses the default message; an unknown key
produces a bounded visible marker. No plural or parameter-format language is added.

UiResources admits the selected typed artifact inside its existing candidate
count/byte limits. Whole-context preparation and retained last-good presentation
remain authoritative. No editable source is read while drawing or ticking.
The Scene's typed catalog reference participates in the existing package closure.

## Literal text and controls

RML expressions use a pure translation transform and a copied Locale binding.
Expression text follows the native literal text path. The translation hook that
can precede markup interpretation is not used. Catalog messages cannot introduce
DOM or new expressions. Locale-only updates dirty bindings without replacing
the context or its focus; controller/catalog changes use normal candidate loading.

The namespaced `forge.locale` command uses the existing command gate, document
incarnation and visibility checks, then the same runtime property owner as the
Inspector. It works while paused without advancing simulation. A localized Scene
rejects a conflicting custom action; unlocalized projects retain that capability.
Existing UI documents, UiDocument value layout and C host-table layout retain
their meaning. New component values require the exact matching SDK.

This is game UI message localization. Editor translation, full shaping, arbitrary
international formatting and complete international input remain separate work.
Dependency pins and the existing font/text-rendering path are unchanged. Actual
glyphs, literal behavior, controls, lifetimes and export require consumer evidence;
this decision is not an implementation or platform-acceptance claim.
