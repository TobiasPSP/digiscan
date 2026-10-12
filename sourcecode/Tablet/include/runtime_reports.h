#pragma once
// Runtime report catalog permits external profiles to define their own pages.
// Report references remain text until resolved against the assigned property set;
// fallback tables and uploaded tables share this resolution path.
namespace reporting {
static decoding::Table<Page,64> pages;
static unsigned &PageCount=pages.count;
struct BuiltinReports { BuiltinReports(){for(const auto &p:builtin_reporting::pages)pages.entries[pages.count++]=p;} };
static BuiltinReports builtinReports;
}
