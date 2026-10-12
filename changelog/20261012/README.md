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
