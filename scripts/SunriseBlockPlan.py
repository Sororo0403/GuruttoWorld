"""Shared architectural footprints: both rendered structure and physical walls use these."""
# x minimum, x maximum, z minimum, z maximum, height, spatial batch zone
BUILDINGS={
    'entry-right':(12.5,22.5,-4,10,9,0),
    'corner':(-11,1,28,42,9,1),
    'arcade':(14,24,24,42,9,1),
    'cafe':(-20,-10,44,62,9,2),
    'workshop':(19,29,44,62,6,2),
    'roof':(-9,9,80,94,5.4,3),
}

# A small off-centre garden has two ways around it. Geometry and collision
# deliberately share these dimensions; the open centre is not a marked lane.
PLAZA_GARDEN=(8.0,10.6,3.2,5.2,.45)
