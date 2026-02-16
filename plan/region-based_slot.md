# Core idea: “Region tree” per slot

Instead of tracking one fact per `SlotId`, track a **tree of regions** keyed by `Place` projections.

- The **root** represents the whole slot (`@x`)
- Children represent subregions (`@x.f`, `@x.f.g`, `@x[3]`, …)

Each region stores two different concepts:

1. **Exact fact**: “If you load *exactly this region*, what is it?”
2. **Base mapping**: “If you load a *subregion* not explicitly overridden here, where does it come from by default?”

This separation is what preserves useful info after partial writes:
- After `x = memcpy(y)`, we know all subfields map to `y.*`
- After later `x.a = 1`, we no longer know the whole `x`, but we still know `x.b` maps to `y.b`
