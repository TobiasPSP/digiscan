<#
.SYNOPSIS
Generate the monitor font from the pinned LVGL UNSCII source.
.DESCRIPTION
Reads the pinned LVGL UNSCII 8 source, validates its glyph table, and doubles
glyph height while retaining fixed advance. Normal builds use the committed
header; this maintainer command does not load YAML or contact hardware.
.EXAMPLE
New-MonitorFont -Source .\lv_font_unscii_8.c -Output .\mono_font.h
.PARAMETER Source
LVGL 9.2.2 lv_font_unscii_8.c path. Defaults to the local PlatformIO dependency.
.PARAMETER Output
Generated header path; defaults to include/mono_font.h in the firmware checkout.
.NOTES
Supply explicit paths when using the module outside the firmware checkout.
#>
function New-MonitorFont {
    [CmdletBinding(PositionalBinding = $false)]
    param([string]$Source, [string]$Output)
    $ErrorActionPreference = 'Stop'
    $project = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (!$Source) { $Source = Join-Path $project '.pio/libdeps/tablet/lvgl/src/font/lv_font_unscii_8.c' }
    if (!$Output) { $Output = Join-Path $project 'include/mono_font.h' }
    $text = [IO.File]::ReadAllText($Source)
    $bitmap = [regex]::Match($text,'(?s)glyph_bitmap\[\] = \{(.*?)\};').Groups[1].Value
    $bitmap = [regex]::Replace($bitmap,'(?s)/\*.*?\*/','')
    $bytes = @([regex]::Matches($bitmap,'0x[0-9a-fA-F]+') | ForEach-Object { [Convert]::ToByte($_.Value.Substring(2),16) })
    $table = [regex]::Match($text,'(?s)glyph_dsc\[\] = \{(.*?)\};').Groups[1].Value
    $glyphs = [regex]::Matches($table,'\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}')
    if ($glyphs.Count -ne 97 -or !$bytes.Count) { throw 'Unexpected pinned LVGL UNSCII font structure.' }
    $result = [Collections.Generic.List[byte]]::new(); $descriptors = [Collections.Generic.List[string]]::new()
    foreach ($g in $glyphs) {
        $start=[int]$g.Groups[1].Value; $advance=[int]$g.Groups[2].Value
        $w=[int]$g.Groups[3].Value; $h=[int]$g.Groups[4].Value
        $x=[int]$g.Groups[5].Value; $y=[int]$g.Groups[6].Value
        $descriptors.Add(('{{{0},{1},{2},{3},{4},{5}}},' -f $result.Count,$advance,$w,($h*2),$x,($y*2)))
        $acc=0; $bits=0
        for ($row=0;$row -lt $h;$row++) {
            for ($repeat=0;$repeat -lt 2;$repeat++) {
                for ($col=0;$col -lt $w;$col++) {
                    $bit=$row*$w+$col
                    $value=($bytes[$start+[int][Math]::Floor($bit/8)] -shr (7-($bit%8))) -band 1
                    $acc=($acc -shl 1) -bor $value; $bits++
                    if ($bits -eq 8) { $result.Add([byte]$acc); $acc=0; $bits=0 }
                }
            }
        }
        if ($bits) { $result.Add([byte]($acc -shl (8-$bits))) }
    }
    $out=@('// Generated from LVGL 9.2.2 UNSCII 8, vertically doubled; 8 px fixed advance.',
     '#pragma once','#include <lvgl.h>','namespace mono {',
     ('static const uint8_t bitmap[] = {' + (($result | ForEach-Object { '0x{0:X2}' -f $_ }) -join ',') + '};'),
     'static const lv_font_fmt_txt_glyph_dsc_t glyphs[] = {') + $descriptors.ToArray() + @(
     '};','static const lv_font_fmt_txt_cmap_t cmap = {32,96,1,nullptr,nullptr,0,LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY};',
     'inline lv_font_t make() {',
     'static lv_font_fmt_txt_dsc_t data={}; data.glyph_bitmap=bitmap; data.glyph_dsc=glyphs; data.cmaps=&cmap; data.cmap_num=1; data.bpp=1;',
     'lv_font_t font={}; font.get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt; font.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt; font.line_height=18; font.dsc=&data; return font;',
     '}','static const lv_font_t font=make();','}')
    [IO.File]::WriteAllLines($Output,$out,(New-Object Text.UTF8Encoding($false)))
    Write-Host "Generated fixed-width 8x18 display font: $Output"
    
}
