# Physical character traversal through jump links

Status: accepted for implementation; extends decision 052.

The 3D Character movement profile may traverse an explicit NavigationLink with
positive arc height using the existing native character. Direct movement keeps
its prescribed geometric arc. Floating XY movement and zero-height character link
traversal remain unsupported. No new source selector or asset identity is needed.

Physics owns position, collision, support, velocity, gravity and jump acceptance.
Navigation reads the adopted character and the physics owner's configured gravity.
It derives a bounded takeoff request from the oriented link chord and authored
lift; it never writes a flight pose or requires the actual pose to lie on the
prescribed geometric arc. The initial supported profile requires upright world-Y
characters, downward vertical gravity and static takeoff/landing support. Range,
step resolution and support are checked before requesting takeoff. A physically
obstructed or missed landing cannot be reported as route arrival.

The existing character command FIFO owns a combined automatic planar movement
and jump request. It is consumed once at a positive physics step, including when
armed at a zero-time synchronization. Manual movement, jump or placement wins in
either queue order and cancels an armed automatic request. Its origin is retained
by the existing CharacterRecord, including shape recreation and private recovery;
there is no second command queue or durable gameplay state. Ordinary manual jump
requests retain their existing lifetime and acceptance rules.

Private physics recovery uses version 13 only when an armed or queued automatic
jump is present. Versions 10, 11 and 12 retain their existing canonical profiles.
Malformed, empty, downgraded and cross-domain automatic jump extensions reject
before native publication. The host-only service extension does not add an SDK C
callback. Any installed contract hash change must follow the existing fingerprint
inputs rather than a separate compatibility identity.

An active flight retains its admitted oriented link, not a navigation-owned
trajectory. Actual takeoff and landing feedback determines continuation. Cancelling
navigation releases steering; setting speed to zero cannot freeze gravity.
Changing destination during flight takes effect after landing. Character collision
filters remain authoritative even if optional direct-link query checks are off.
Moving-platform routing, grounded XY platformer links and navigation streaming
remain separate unsupported capabilities.
