Ok. I plan to do these changes:
The main goal is move the heavy works into the builder itself, so that the emitter code is cleaner and simpler.
1. move the builder into the main project instead of a seperate lib
	- relocate `llvmbuilder` sources under `src/mir/codegen/` so they build with the primary target
	- collapse the dedicated CMake target and point existing includes to the new in-tree headers
	- ensure the builder gains access to semantic headers without circular deps by wiring it through existing MIR codegen libs

2. make it aware of our type system and handle type printing in itself
	- expose the semantic `Type` APIs to the builder and add a lightweight printer helper (e.g. `TypeFormatter`)
	- replace ad-hoc stringification hooks with builder-owned helpers that render LLVM/MIR text directly from `TypeId`
	- add regression tests that show the builder can emit correct type annotations without outside assistance

3. add string global support & seperate string handling
	- add a string table inside the builder that interns literal data and emits the needed global declarations
	- provide an explicit `emit_string_literal` helper returning the temp that references the global
	- restructure string-related instructions so runtime temps vs global constants stay distinct (e.g. dedicated structs/helpers)

4. abandon temp hint, since builder now is aware of our temp system, just generate from tempid. Use a pure function for name, ensure a deterministic mapping from TempId to name. Do pure temp_name(temp) for all MIR temps. Kill temp_names_. Stop returning names from builder for instructions whose result is a TempId. Keep builder returning names for anonymous intermediates.
	- implement a pure `temp_name(TempId)` utility with deterministic formatting and call it from every MIR printer
	- delete `temp_names_` and the hint-plumbing from the builder; instructions now only traffic in `TempId`
	- audit instruction builders so any operation producing a temp returns the `TempId`, letting callers name it via `temp_name`
	- keep builder-returned raw strings exclusively for ephemeral/anonymous intermediates (e.g. offsets) to avoid churn
    - Only place will need to return names for places, since places are not temps, and will generate temps.