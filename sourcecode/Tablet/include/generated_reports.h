// Generated unresolved report descriptors. Resolution and All expansion happen on the tablet.
#pragma once
#include "report_model.h"
namespace builtin_reporting {
using reporting::Page;
static const char *const items_0_0[] = {"All"};
static const reporting::Group groups_0[] = {{"",items_0_0,1},};
static const char *const items_1_0[] = {"0x50_1111111111111111","0x6E_1111111111111111","0x03_11111111"};
static const reporting::Group groups_1[] = {{"Overview",items_1_0,3},};
static const char *const items_2_0[] = {"All"};
static const reporting::Group groups_2[] = {{"",items_2_0,1},};
static const char *const items_3_0[] = {"0x50_1111111111111111","0x6E_1111111111111111","0x03_01111111"};
static const reporting::Group groups_3[] = {{"Overview",items_3_0,3},};
static const char *const items_4_0[] = {"All"};
static const reporting::Group groups_4[] = {{"",items_4_0,1},};
static const char *const items_5_0[] = {"0x50_1111111111111111","0x6E_1111111111111111","0x03_11111111"};
static const reporting::Group groups_5[] = {{"Overview",items_5_0,3},};
static const Page pages[] = {
{"ip2366","All",groups_0,1},
{"ip2366","Battery",groups_1,1},
{"ip2368","All",groups_2,1},
{"ip2368","Battery",groups_3,1},
{"ip2369","All",groups_4,1},
{"ip2369","Battery",groups_5,1},
};
constexpr unsigned PageCount=sizeof(pages)/sizeof(pages[0]);
}
#include "runtime_reports.h"
