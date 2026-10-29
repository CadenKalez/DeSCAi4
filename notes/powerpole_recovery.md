# cSC4PowerPoleOccupant recovery notes (in progress, 2026-10-28)

Read fully from work/sim/cSC4PowerPoleOccupant.txt plus disassembly. Nothing written to src yet. The existing
src/sc4/PowerPoleOccupant.{h,cpp} (city-load partial) has wrong guesses: its destructor releases connection poles and
lines (the original does not), and +0x108 is the model instance, not a "preview pole". Rewrite it following
cSC4PylonOccupant's pattern (occupant at +0x10, cS3DModelInstanceBasic at +0x70, host seam for SL::sp*).

## Layout
+0x00 cISC4PowerPoleOccupant, +0x08 msg target, +0x10 cSC4Occupant (type 0x2890d4de), +0x70 cS3DModelInstanceBasic
(+0x90 draw attributes: byte0 bit0 = highlighted, byte1 (+0x91) = highlight index; +0xa0 float 1.0), +0xa4 transform,
+0xdc position, +0xe8 vector<tConnection>, +0x100 style/variant, +0x108 model, +0x110 rotation, +0x114 zoom,
+0x118/+0x11c cell, +0x120 bounds dirty, +0x124/+0x128/+0x12c/+0x130 min x/min z/max x/max z cells,
+0x138 pointer into the shared cardinal-point table, +0x140 floor verts, +0x158/+0x170 wall verts (sVF_V3F_C4UB_T2F),
+0x188 floor height (min of the cell's 4 corner heights), +0x18c bool, +0x18d subscribed, +0x18e distressed,
+0x190 "powerpolezap" effect, +0x198 preview.
tConnection (0x60): target (ref), list<line*> (refs), +0x20 inserted, +0x21 owner (draws), +0x24/+0x28 target cell,
+0x30 wires (4 Bezier polylines), +0x48 buoys (3-point polylines, only when either cell has water).

## Static data
Cardinal tables (BSS 0x100b24b40 / 0x100b24c00, filled by __GLOBAL__sub_I_SC4PowerLineOccupant.cpp from 0x100a1d340 /
0x100a1d400): types 1,2 -> table A, 3,4 -> table B; index = (instance byte1 - 1) & 0xf if < 4. 4 dirs x 4 points:
A: x=0, y 31.7/38.7, z ±4.8 (dir0), ±6.5 (dir1); dirs 2/3 same on x. B: diagonal ±3.3 / ±4.5 (see dump in session).
Read/Write stream these 4 points through the shared pointer (quirk: overwrites the shared table).
Floor: corners (-1,-1),(-1,1),(1,1),(1,-1) * 10 * 0.5; u (0,0,1,1) v (0,1,1,0); rotate 45 deg (c 0.70711255,
s 0.70710105) when model key instance & 0xffff0000 is 0x05040000/0x05030000. Vertex
X = (m6*fz + (m3*0 + m0*fx)) + pos.x, Z = (fz*m8 + (m5*0 + fx*m2)) + pos.z; colour 0xffffffff.
Walls: edges (0,1),(3,0),(2,3),(1,2); per rotation r use edges table[2r], table[2r+1] from {2,3,3,0,0,1,1,2};
quad: a, (a.x, floorY, a.z) v=(a.y-floorY)*0.0625, (b.x,floorY,b.z) u=1, b u=1 v=0; colour = (c & 0xff000000)|0xffffff.
Textures: top 0x0912220e, wall 0x08080004, line 0xaa9da78e (also GetSortValue), buoy 0xc9af0fcd; key
{0x7ab50e44, 0x1abe787d, id}; zoom change retries the identical key once.
Wire quad (format 2, 0x14 bytes), w = widthFactors[zoom] (5 x 0.3, exemplar 0xe9ac43e7), u = inv*dz, x = dx*inv over
len = sqrt(dz^2 + dx^2): v0 (x0 - w*uz, y0, w*ux + z0) uv(0,0); v1 (w*uz + x0, y0, z0 - w*ux) uv(0,1);
v2 (w*uz + x1, y1, z1 - w*ux) uv(len*0.125,1); v3 (x1 - uz*w, y1, ux*w + z1) uv(len*0.125,0).
Buoy quad at each buoy point p (not the last), k = w*4: (x-k,y,z-k)(0,0), (x-k,y,k+z)(0,1), (k+x,y,k+z)(1,1),
(k+x,y,z-k)(1,0).
Tuning exemplar {0x6534284a,0xe7e2c2db,0xc911e35b}: control-point vars (PowerLineTunables), 0xe9ac43e7, 0xa2eced8
(int64 demolition cost). Packed render state (true,true,true,true,false,1,false,3,4,5,4,0.1,1,-1).

## Behaviour notes
AddConnection: skip if target cell already connected or equals own cell; owner = target z > own z || (== && own x <
target x); 4 wires: GetControlPoints(A[i],B[i]) -> n; wire[0]=A[i], Bezier(..., &wire[1], n-1); buoys Bezier(...,3);
lines from tLineInfo (+8 is the line occupant pointer) AddRef'd push_back; post 0x99ef1145; bounds dirty.
UpdateConnection: rebuilds wires only (buoys appended again, never cleared: quirk). RemoveConnection(pole) is a plain
erase. BreakAll leaves emptied entries in the vector. LoadModel: key from 0x27812821 (default {0x5ad0e817,0xbadb57f1}),
instance = GetModelInstanceID(occ, zoom, rot, variant==8||1), then yaw rotation by -viewYaw with cosf/sinf:
r = (c, -(c*0), s, s*0+0, 1-(s*0)*0, -(c*0), 0-s, s*0+0, c). SetModelPosition adds exemplar 0x4835c6a4 offset
(original crashes if missing). Effect transform for the zap: counter 1, flags 1, identity, translation = pos.
GetWireCells(int): non-owner returns target->GetWireCells(this); owner uses GetPowerWireCells, returns true.
Write: vertex vector = count, then x,y,z,u,v floats and 4 colour bytes each.
