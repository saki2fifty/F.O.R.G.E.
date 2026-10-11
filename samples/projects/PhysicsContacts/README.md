# C++ physics contacts

Open `forge.project.json` in FORGE. Play: the red cube moves toward the blue floor
marker, reacts to the sensor's contact-cache Begin, then moves back to the left.
The marker shows a taller invisible sensor volume; it does not block the cube.
Console diagnostics report the actual Begin/End tick.

Select **Contact actor**, then edit **Contact Reaction** in the Inspector. `speed`
sets its initial speed in metres/second. Disable `reverse_on_begin` before Play
to watch it pass straight through. The live `begin_count`/`end_count` fields show
raw native cache observations for this single collider pair. Stop restores the
authored values. Change C++ in Content > Code, save, and build while stopped.

The source ships with an exact matching prebuilt module in numbered Windows ZIPs.
Export uses the matching shared runtime kit and compiled module. Players need no
source or compiler. This is an ordinary editable project, not engine behavior.

Cache End can occur when a body sleeps or retires; it does not prove geometric
separation. Complex shapes can yield multiple subshape events. This example does
not build an overlap tracker. Reads are copied, nonconsuming, fixed-tick and
owner-thread only. The counters and one-shot reaction state are reflected so a
fresh recovered worker continues without launching or reacting twice.

Physics Body speed limits still apply. The sample disables gravity and damping.
Use moderate speeds: a fast body can cross a sensor between discrete physics
samples; sensor bodies do not gain continuous response from the CCD checkbox.
