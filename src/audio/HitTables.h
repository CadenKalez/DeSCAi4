// Default property tables cHitMan::Init writes into its property-set resources, read byte-exact
// from the binary: kTrackPropertyDefaults @ 0x100a222e0 (54 entries, the last nine are zero
// padding that the original also stores), kChannelPropertyDefaults @ 0x100a22490 (29 entries).
// Ids are the property ids of the Hit register file (see HitRegisters.h).
#pragma once
#include <array>
#include <cstdint>

#include "audio/HitManServices.h"

namespace Hit {

inline constexpr std::array<DefaultProperty, 54> kTrackPropertyDefaults = {{
    {0xff4c97be, 0},          {0xff1cdf6a, -356090},    {0xff5f15e1, 1226901227}, {0xffaec905, 0},
    {0xff4dcd0d, 0},          {0xff050a94, 32},         {0xffb0333d, 0},          {0xff706616, 0},
    {0xff303b1b, 0},          {0xffcc156f, 0},          {0xffcc3b29, 4},          {0xff891eaa, 2110647597},
    {0xffae9ddb, 32},         {0xfff7b8df, 0},          {0xffb4335c, -1163022114},{0xff90aac8, 1024},
    {0xff53dbf3, 1024},       {0xff3c2160, 0},          {0xffd7ee4d, 0},          {0xff59fdd8, 0},
    {0xff2d7cd1, 0},          {0xff0246fe, 0},          {0xff8dcfd8, 0},          {0xff69510b, 0},
    {0xff3ec880, 512},        {0xffff4e79, 0},          {0xff595ffd, 0},          {0xff47f685, -11281421},
    {0xff2d16f2, 0},          {0xffd1c82f, 10},         {0xff0ad592, 0},          {0xffcaa344, 0},
    {0xffcc156f, 0},          {0xffa6a614, 1000000},    {0xffaab623, 1000000},    {0xffcc4d2d, 0},
    {0xff4a01d6, 0},          {0xff469820, 0},          {0xff444c9a, 0},          {0xffc20061, 0},
    {0xffce9997, 0},          {0xffc6b36d, 0},          {0xff40ff96, 0},          {0xff4c6660, 0},
    {0xffc6e666, 0},          {0, 0},                   {0, 0},                   {0, 0},
    {0, 0},                   {0, 0},                   {0, 0},                   {0, 0},
    {0, 0},                   {0, 0},
}};

// cHitMan::Init first sets 0xff4c97be = 0 on this resource, then applies these.
inline constexpr std::array<DefaultProperty, 29> kChannelPropertyDefaults = {{
    {0xffaab623, 100},  {0xffaab623, 10000}, {0xff050a94, 32},    {0xff53dbf3, 1024},
    {0xff90aac8, 1024}, {0xff3ec880, 512},   {0xffb36659, 0},     {0xffae9ddb, 0},
    {0xffcc156f, 0},    {0xffcaa344, 0},     {0xffcc4d2d, 0},     {0xff4a01d6, 0},
    {0xff469820, 0},    {0xff706616, 0},     {0xff2d16f2, 0},     {0xff47f685, -11281421},
    {0xffd1c82f, 0},    {0xfff7b8df, 0},     {0xff0ad592, 0},     {0xffff4e79, 0},
    {0xff595ffd, 0},    {0xff3c2160, 0},     {0xff444c9a, 0},     {0xffc20061, 0},
    {0xffce9997, 0},    {0xffc6b36d, 0},     {0xff40ff96, 0},     {0xff4c6660, 0},
    {0xffaec905, 0},
}};

}  // namespace Hit
