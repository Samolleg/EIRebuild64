# Inventory owner membership

`CBaseManager` keeps a lookup hash, an enumeration list, and the list positions
indexed by numeric object ID. All three must identify the same current object.

`RemoveAll` can retire registrations while a caller still holds an item reference.
It also resets the free-ID watermark. After the next item is created with that
ID, releasing the old item used to remove the **new** item's enumeration entry.
`MoveToTrash` already compared pointers before removing the hash entry, but the
earlier removal from `list`/`respmap` did not. The resulting inventory had one
object in lookup and zero in enumeration. `StoreMembers` wrote zero members;
the wagon reference still pulled the new item into the serialized graph.
`LoadMembers` then rejected that graph with `owner membership count mismatch`.

The release path now removes list membership only if lookup still points to the
released object. Replacing an occupied ID also updates the enumeration entry,
so lookup and iteration cannot retain different objects. Save validation and
the wire format are unchanged.

The native regression uses production managers and the production save reader
and writer. It covers ordinary item/spell round-trips, delayed item release
after party refresh, delayed spell release, and replacement at an occupied ID
followed by ordinary release. It does not initialize or run the game.

After preparing a matching Release build:

```powershell
powershell -NoProfile -File tools/check-owner-membership.ps1 -OutputDirectory <task>/checks
```

`-PreparedRoot` can select another prepared build tree. The checker verifies its
core sources, holds the build-cache lock, recompiles the current `BaseManager`,
and links to the prepared engine objects. The engine DLLs come from the accepted
`builds/rebuild64/current` directory; user saves and game settings are not used.
