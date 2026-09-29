# Group-search core: native adapter pending

Exact source transfer from addon commit ac9778d32f65254e99d5a77efc33b4e33bcd837b,
whose diagnostic DLL matches the installed PZ/Dejavu artifact on 2026-09-28.
The seven headers are unchanged from that source. They implement shared
awareness, roster/return tickets, movement ownership, expedition phases and
base/body/witness serialization. They contain no retail offsets or hooks.

Status after R123: portable core plus native duty/path/reset boundary.
Guard/Patrol/Sentry call CommandDuty after their selection; package/load reset
clears leases. There is NO automatic intent producer yet: SetActive/Request
must be connected to the complete sensing/GS coordinator. Do not count this as
working NPC group search. Existing corpse AI remains the current owner.

Required integration boundaries:

1. Port GroupSearchRuntime.inl sensing: only legitimately observed allies and
   bodies, active-side batch, simulation-time advancement, sleep/script guards.
2. Native.h/Native.inl now provide selected-duty leases, revocation, feedback,
   low-level stop/turn and native path probes. Finish exact owned-pickup handling
   with the coordinator: animations currently yield conservatively. Preserve
   combat, scripted movement and corpse-pickup ownership.
3. Port GroupSearchPath.inl plus route preparation using native AI-map types,
   endpoint validation and shared path budget. Do not substitute straight-line
   arrival or an omniscient scan of hidden NPC positions.
4. Bind base/body/witness GS keys to current party vars. Load replaces state on
   rollback; reset leases on world/zone changes. Keep base v1 representation.
5. Reconcile existing AddonUpdateCorpseInvestigation/AddonUpdateCorpseHaul with
   group witness/pickup paths before enabling the new owner. No parallel command
   writers for the same NPC.

Private provenance, checks and precise next work: investigation R122.md.
