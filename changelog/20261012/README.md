# 2026-10-12

## Bounded hinge and slider velocity motors

PhysicsJoint supports optional velocity drives for Hinge and Slider. Hinge speed
uses degrees/s with a maximum torque; Slider speed uses m/s with a maximum force.
Zero target speed brakes within that budget, while zero budget allows coasting.
Inspector controls, Undo/save/reopen and selected Scene guide status use the
existing joint owner. C++ can change the same typed fields before Physics without
replacing the native constraint. Legacy documents default to disabled motors.

Original Gallery25 contains a force-limited slider and torque-limited hinge for
Play and source-free export. Matching SDK modules must be rebuilt because the
component layout changes. Authored formats, ABI1 and private checkpoint4 remain
unchanged. Windows delivery and physical review are pending; this entry does not
claim a new delivered ZIP.

## Bounded joint position drives

Hinge and Slider can return to an angle or signed displacement using bounded
force/torque, spring frequency and damping. Inspector mode selection retains hidden
values, supports Undo/save/reopen and adds selected Scene target markers. Active
targets outside joint limits are rejected before native mutation. The existing
owner retains native constraint geometry/order when controls change; C++ edits use
the same typed component before Physics. Legacy controls default to velocity mode.

Original Gallery26 returns a slider to-.75m and hinge to-45degrees and supports
source-free export. Fixed-step implicit integration adds damping even at authored
ratio zero. Matching SDK modules require rebuilding; authored formats, ABI1 and
private checkpoint4 remain unchanged. Windows delivery and physical review are
pending; no new delivered ZIP is claimed.
