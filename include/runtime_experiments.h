#pragma once
// Merge immutable fallback and installed experiment descriptors by chip identity.
// Runtime resolution is necessary: uploaded properties cannot rely on compiled
// indices. Catalog capacity failures must remain visible instead of truncating.
namespace experiment {
static decoding::Table<Rule,256> rules;
static decoding::Table<Graph,64> graphs;
static decoding::Table<FilterList,64> filterLists;
static unsigned &RuleCount=rules.count, &GraphCount=graphs.count, &FilterListCount=filterLists.count;
struct BuiltinExtras { BuiltinExtras(){
    for(const auto &v:builtin_experiment::rules)rules.entries[rules.count++]=v;
    for(const auto &v:builtin_experiment::graphs)graphs.entries[graphs.count++]=v;
    for(const auto &v:builtin_experiment::filterLists)filterLists.entries[filterLists.count++]=v;
} };
static BuiltinExtras builtinExtras;
}
