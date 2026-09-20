# Stable subasset mapping

`SubassetIdentityDocument` records a model/container AssetId, project-relative source
locator, source digest, versioned evidence schema and durable member entries. Format
`forge.subasset-identity` version1 is separate from the catalog format. Entries retain
AssetId, owner-scoped mapping key, logical type, display name, semantic evidence,
removed state and unknown fields. Serialization orders entries by durable key.

## Matching and conflicts

Importers supply candidate-local output addresses, such as `/meshes/3`, separately
from evidence. These addresses only connect a candidate's outputs to the returned
assignments. They never become durable keys. Names are display data, never matching
evidence. An importer must define and version its canonical content and semantic-role
digests without making array order or names alone authoritative.

Reconciliation first accepts explicit reviewed same-type remaps/create-new choices,
then unique exporter identities, then unique combined content/role evidence. Unique
individual content or role evidence can establish correspondence only when their
proposals do not conflict or compete for one old entry. Different nonempty exporter
IDs never fall back to matching identical content. A schema change requires an
explicit migration; old evidence is not reinterpreted under new rules.

Unresolved correspondence reports `subasset.identity-ambiguous`, candidate addresses
and previous same-type IDs. The result contains no publishable document or partial
assignments. The previous map remains unchanged. This prevents a guessed match from
silently retargeting scene references. The caller must retain selected artifacts
until an entire candidate is validated and published.

## Removed members and duplication

Removed members retain their ID/key/evidence/unknown fields as tombstones. Automatic
matching does not resurrect them; an explicit same-type choice can restore a member.
An explicit create-new choice allocates a new UUID and retains the old tombstone.
Duplicating a logical source container allocates new container/member IDs and keys,
and returns an old-to-new map for understood importer-owned references. Unknown
plugin payloads are preserved byte-value-equivalently as JSON and never scanned for
UUID substitutions. This identity helper does not duplicate authored Scene EntityIds;
the scene duplication operation retains that separate responsibility.

## Limits and current integration boundary

Admission bounds include100k retained entries,64MiB serialized data,64 nesting levels,
4million parse events,64KiB opaque data per entry and16MiB aggregate opaque data.
Duplicate JSON keys, duplicate member IDs/keys, wrong types, conflicting decisions,
nonfinite metadata and unsupported versions are rejected. Matching uses indexed
groups, with a10k reorder fixture; no all-pairs name matching is used.

This is a tested candidate identity service, not an exposed model import workflow.
Concrete glTF semantic evidence, recoverable sidecar/catalog publication, explicit
mapping UI and cooked-resource adoption are still being integrated in Phase7.
