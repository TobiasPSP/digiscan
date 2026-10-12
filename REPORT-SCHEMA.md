# Runtime Reports Schema

Optional top-level `reports` is a list of pages. Current built-in YAMLs use:

```yaml
reports:
  - header: All
    groups:
      - header: null
        items: [All]
  - header: Battery
    groups:
      - header: Overview
        items:
          - 0x50_1111111111111111
          - 0x6E_1111111111111111
          - 0x03_11111111
```

IP2368 uses `0x03_01111111` for its seven-bit charge limit instead. Masks are
actual property masks, not wildcards. Null/empty group headers are hidden.
`All` expands to all readable properties in register order, highest field first.
Explicit references preserve authored order within each group.

References have a two-digit hexadecimal starting register and an 8-, 16- or
24-bit nonzero binary mask. The key is `(starting register, mask)`, scoped to
the profile assigned to the selected address. There are no generated property
indexes or GUIDs. An unresolved, ambiguous or oversized page is unavailable
with an explicit error; it is never silently redirected to another property.

Limits: 16 pages/profile, 16 groups/page, 512 authored items/group, 512 resolved
rows/page, 80 characters/header. Runtime property capacity remains 512 shared
across assigned addresses. Write-only properties are excluded/rejected.

The PC generator validates syntax and emits literal references to
`generated_reports.h`. `reporting::Model::build` performs lookup and `All`
expansion on the tablet whenever a page or assignment changes. Built-in data is
only the current storage provider; external loading is not yet implemented.
The future binary loader must expose the same report/property contracts with
owned, validated lifetimes rather than prebinding report rows during conversion.

Read cache semantics are separate from Monitor change detection. Writes never
populate a report. Complete values must pass range/enum/conflict checks.
The acquisition timestamp is set only on the first successful complete read;
subsequent reads update the value without restarting its one-time orange fade.
Failure renders white with bold red text; recovery does not reset acquisition age. Clear or profile
replacement resets acquisition. This is a collection overview, not a live
change-highlight display.
Consecutive low-first component reads retain existing per-address assembly and
the one-second arrival limit. An interrupted pending group marks its report
failed without adding a new Monitor transaction. No inference joins arbitrary
nonconsecutive reads. Cache entries are cleared on Clear/profile replacement.

Colors represent observed acquisition/decoding results, not guaranteed DUT
health. An unknown transport loss cannot identify every affected property.
Active read policy, graphs, identification, hardware recommendations and writes
will extend the schema after their separate implementations are validated.
