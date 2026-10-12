#requires -Version 5.1
# Shared report schema boundary for YAML conversion and fallback generation.
# References encode starting-register + binary mask, not a generated index.
# Validate syntactic bounds here, but retain unresolved references in firmware
# descriptors so packages installed later can resolve against their own catalog.
function Get-ReportSchema($Source) {
    $pages=@($Source.reports)
    if (!$Source.Contains('reports')) { return @() }
    if (!$pages.Count -or $pages.Count -gt 16) { throw 'reports requires 1..16 pages.' }
    foreach ($page in $pages) {
        if ($page -isnot [Collections.IDictionary] -or @($page.Keys | Where-Object {$_ -notin @('header','groups')}).Count -or !$page.Contains('header') -or !$page.Contains('groups') -or $page.header -isnot [string] -or !$page.header.Trim() -or $page.header.Length -gt 80 -or $page.header -match '[\r\n\x00-\x1F]') { throw 'Invalid report page.' }
        $groups=@($page.groups)
        if (!$groups.Count -or $groups.Count -gt 16) { throw 'Report requires 1..16 groups.' }
        foreach ($group in $groups) {
            if ($group -isnot [Collections.IDictionary] -or @($group.Keys | Where-Object {$_ -notin @('header','items')}).Count -or !$group.Contains('header') -or !$group.Contains('items') -or ($null -ne $group.header -and ($group.header -isnot [string] -or $group.header.Length -gt 80 -or $group.header -match '[\r\n\x00-\x1F]'))) { throw 'Invalid report group.' }
            $items=@($group.items)
            if (!$items.Count -or $items.Count -gt 512) { throw 'Report requires 1..512 items.' }
            foreach ($item in $items) {
                if ($item -isnot [string] -or ($item -cne 'All' -and $item -cnotmatch '^0x[0-9A-Fa-f]{2}_([01]{8}|[01]{16}|[01]{24})$') -or $item -match '_0+$') { throw "Invalid report reference: $item" }
            }
        }
    }
    return $pages
}
