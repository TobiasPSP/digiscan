#pragma once
// Bounded mutable catalog over immutable profile descriptors. Installed definitions
// replace the same stable chip ID; unrelated chips retain their own property sets.
// Catalog pointers borrow package memory, so replacement is published at boot.
namespace decoding {
constexpr unsigned PropertyCount=512, MaxProfiles=16;
static Property properties[PropertyCount];
static Table<Profile,MaxProfiles> profiles;
static unsigned &ProfileCount=profiles.count;
static unsigned DefinitionCount=builtin_decoding::DefinitionCount;
static const auto &definitions=builtin_decoding::definitions;
inline const Property &definition(const Profile &p,unsigned i){return p.table?p.table[i]:definitions[p.first+i];}
struct BuiltinProfiles { BuiltinProfiles(){for(const auto &p:builtin_decoding::profiles)profiles.entries[profiles.count++]=p;} };
static BuiltinProfiles builtinProfiles;
}
