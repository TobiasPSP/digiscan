#pragma once
// Generated fallback experiment data; edit YAML and use New-ProfileHeaders.
// Runtime profiles use the same descriptors without needing a firmware rebuild.
#include "experiment.h"
namespace builtin_experiment {
using experiment::Rule; using experiment::GraphLine; using experiment::Graph; using experiment::FilterList;
static const uint8_t filter_0[]={11,49,51,52,56};
static const GraphLine graph_0[]={{"0x50_1111111111111111",0,30000,6345955,false},{"0x6E_1111111111111111",0,10000,16764272,false},};
static const uint8_t filter_1[]={11,49,51,52,56};
static const GraphLine graph_1[]={{"0x50_1111111111111111",0,30000,6345955,false},{"0x6E_1111111111111111",0,10000,16764272,false},};
static const uint8_t filter_2[]={11,49,51,52,56};
static const GraphLine graph_2[]={{"0x50_1111111111111111",0,30000,6345955,false},{"0x6E_1111111111111111",0,10000,16764272,false},};
static const Rule rules[]={{"ip2366","Direct cell-count range, DUT verified, shared with IP2369",4,7,2,6,identification::Class::C},{"ip2368","Capacity code; all seven-bit codes compatible, not distinguishing",4,127,0,127,identification::Class::C},{"ip2369","Documented IC revision 1=B, 2=C, 3=D and later, manual p20, not exclusive",57,255,1,255,identification::Class::C},{"ip2369","Direct cell-count range, shared with IP2366",4,7,2,6,identification::Class::C},};
constexpr unsigned RuleCount=sizeof(rules)/sizeof(rules[0]);
static const Graph graphs[]={{"ip2366","Battery",graph_0,2},{"ip2368","Battery",graph_1,2},{"ip2369","Battery",graph_2,2},};
constexpr unsigned GraphCount=sizeof(graphs)/sizeof(graphs[0]);
static const FilterList filterLists[]={{"ip2366","USB Direction",filter_0,5},{"ip2368","USB Direction",filter_1,5},{"ip2369","USB Direction",filter_2,5},};
constexpr unsigned FilterListCount=sizeof(filterLists)/sizeof(filterLists[0]);
}
#include "runtime_experiments.h"
