# Modulation

Each part has four **mod slots** and a **modulation matrix** of twelve routes. A mod slot holds a modulation engine (an envelope, an LFO, a random source or a function generator) or is empty. A route takes a **source** (a mod slot's output, or one of the fixed sources such as velocity or the mod wheel), scales it by an **amount** and sends it to a **destination**: any control of the Source, Filter or Shaper, the pitch, the Amp, the pan, the part level, or a control of another mod slot.

In the host, each part has the groups *Part A/Mod 1* … *Part A/Mod 4* (each with *Engine*, *Scope* and P1–P16) and *Part A/Matrix* (*Route 1* … *Route 12*, the amounts). Which source and destination each route uses is part of the patch: it is saved with the project, and the editor arrives with EG13's own interface. A route with no source or no destination does nothing and its amount shows "-".
