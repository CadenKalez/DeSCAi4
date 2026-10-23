# Ledger provenance audit

Rows: **24787**. With a named verification: **24709**.
Without one: **78** across **43** classes.

## Classes whose rows name no verification at all

These predate the evidence rule: their functions are decompiled and ledgered, but no test or oracle
is named. Repair means writing or pointing at a test, not editing the ledger.

| Class | Rows without evidence | Registered test compiling its source |
|---|---:|---|
| `std::__1::__hash_table<std::__1::__hash_value_type<int, cSC4VertexHtConstraintSatisfier::tToSolveVertInfo>, std::__1::__unordered_map_hasher<int, std::__1::__hash_value_type<int, cSC4VertexHtConstraintSatisfier::tToSolveVertInfo>, std::__1::hash<int>, true>, std::__1::__unordered_map_equal<int, std::__1::__hash_value_type<int, cSC4VertexHtConstraintSatisfier::tToSolveVertInfo>, std::__1::equal_to<int>, true>, std::__1::allocator<std::__1::__hash_value_type<int, cSC4VertexHtConstraintSatisfier::tToSolveVertInfo>>>` | 7 | — |
| `std::__1::map<unsigned long, cSCPropertyView::tPropertyInfo, std::__1::less<unsigned long>, std::__1::allocator<std::__1::pair<unsigned long const, cSCPropertyView::tPropertyInfo>>>` | 3 | — |
| `std::__1::__hash_table<std::__1::__hash_value_type<int, float>, std::__1::__unordered_map_hasher<int, std::__1::__hash_value_type<int, float>, std::__1::hash<int>, true>, std::__1::__unordered_map_equal<int, std::__1::__hash_value_type<int, float>, std::__1::equal_to<int>, true>, std::__1::allocator<std::__1::__hash_value_type<int, float>>>` | 3 | — |
| `std::__1::map<unsigned int, cSCPropertyView::sResKeyTableInfo, std::__1::less<unsigned int>, std::__1::allocator<std::__1::pair<unsigned int const, cSCPropertyView::sResKeyTableInfo>>>` | 2 | — |
| `std::__1::__tree<std::__1::__value_type<unsigned long, cSCPropertyView::tPropertyInfo>, std::__1::__map_value_compare<unsigned long, std::__1::__value_type<unsigned long, cSCPropertyView::tPropertyInfo>, std::__1::less<unsigned long>, true>, std::__1::allocator<std::__1::__value_type<unsigned long, cSCPropertyView::tPropertyInfo>>>` | 2 | — |
| `std::__1::__tree<std::__1::__value_type<unsigned int, cSCPropertyView::sResKeyTableInfo>, std::__1::__map_value_compare<unsigned int, std::__1::__value_type<unsigned int, cSCPropertyView::sResKeyTableInfo>, std::__1::less<unsigned int>, true>, std::__1::allocator<std::__1::__value_type<unsigned int, cSCPropertyView::sResKeyTableInfo>>>` | 2 | — |
| `std::__1::vector<nSGLDX7::cGDriver::D3DLight, std::__1::allocator<nSGLDX7::cGDriver::D3DLight>>` | 2 | — |
| `std::__1::__split_buffer<cSCResourceCatalogView::tTypeNameAssoc, std::__1::allocator<cSCResourceCatalogView::tTypeNameAssoc>&>` | 2 | — |
| `void std::__1::random_shuffle<std::__1` | 2 | — |
| `std::__1::list<nGZLog::cService::DeferredOutputData, std::__1::allocator<nGZLog::cService::DeferredOutputData>>` | 2 | — |
| `AddFileKeyEnumerate` | 1 | — |
| `std::vector<double, std::allocator<double> >` | 1 | — |
| `clear` | 1 | — |
| `~__hash_table` | 1 | — |
| `cSCPropertyView::tPropertyInfo` | 1 | — |
| `void std::__1::vector<nSGLDX7::cGDriver::BufferRegionData, std::__1::allocator<nSGLDX7::cGDriver::BufferRegionData>>::__push_back_slow_path<nSGLDX7::cGDriver` | 1 | — |
| `std::__1::__split_buffer<nSGLDX7::cGDriver::BufferRegionData, std::__1::allocator<nSGLDX7::cGDriver::BufferRegionData>&>` | 1 | — |
| `std::__1::__split_buffer<nSGLDX7::cGDriver::D3DLight, std::__1::allocator<nSGLDX7::cGDriver::D3DLight>&>` | 1 | — |
| `std::__1::__vector_base<cSCResourceCatalogView::tTypeNameAssoc, std::__1::allocator<cSCResourceCatalogView::tTypeNameAssoc>>` | 1 | — |
| `void std::__1::vector<cSCResourceCatalogView::tTypeNameAssoc, std::__1::allocator<cSCResourceCatalogView::tTypeNameAssoc>>::__push_back_slow_path<cSCResourceCatalogView` | 1 | — |
| `std::__1::vector<cSCResourceCatalogView::tTypeNameAssoc, std::__1::allocator<cSCResourceCatalogView::tTypeNameAssoc>>` | 1 | — |
| `std::__1::__hash_const_iterator<std::__1::__hash_node<std::__1::__hash_value_type<int, cSC4VertexHtConstraintSatisfier::tToSolveVertInfo>, void*>*> std::__1::__hash_table<std::__1::__hash_value_type<int, cSC4VertexHtConstraintSatisfier::tToSolveVertInfo>, std::__1::__unordered_map_hasher<int, std::__1::__hash_value_type<int, cSC4VertexHtConstraintSatisfier::tToSolveVertInfo>, std::__1::hash<int>, true>, std::__1::__unordered_map_equal<int, std::__1::__hash_value_type<int, cSC4VertexHtConstraintSatisfier::tToSolveVertInfo>, std::__1::equal_to<int>, true>, std::__1::allocator<std::__1::__hash_value_type<int, cSC4VertexHtConstraintSatisfier::tToSolveVertInfo>>>` | 1 | — |
| `std::__1::unordered_map<int, cSC4VertexHtConstraintSatisfier::tToSolveVertInfo, std::__1::hash<int>, std::__1::equal_to<int>, std::__1::allocator<std::__1::pair<int const, cSC4VertexHtConstraintSatisfier::tToSolveVertInfo>>>` | 1 | — |
| `std::__1::__list_imp<cSC4VertexHtConstraintSatisfier::tSmoothConstrEndFixed, std::__1::allocator<cSC4VertexHtConstraintSatisfier::tSmoothConstrEndFixed>>` | 1 | — |
| `std::__1::__list_imp<cSC4VertexHtConstraintSatisfier::tSmoothConstrMidFixed, std::__1::allocator<cSC4VertexHtConstraintSatisfier::tSmoothConstrMidFixed>>` | 1 | — |
| `std::__1::__list_imp<cSC4VertexHtConstraintSatisfier::tSmoothnessConstraint, std::__1::allocator<cSC4VertexHtConstraintSatisfier::tSmoothnessConstraint>>` | 1 | — |
| `std::__1::__list_imp<cSC4VertexHtConstraintSatisfier::tSlopeConstraint, std::__1::allocator<cSC4VertexHtConstraintSatisfier::tSlopeConstraint>>` | 1 | — |
| `std::__1::__hash_iterator<std::__1::__hash_node<std::__1::__hash_value_type<int, cSC4VertexHtConstraintSatisfier::tToSolveVertInfo>, void*>*> std::__1::__hash_table<std::__1::__hash_value_type<int, cSC4VertexHtConstraintSatisfier::tToSolveVertInfo>, std::__1::__unordered_map_hasher<int, std::__1::__hash_value_type<int, cSC4VertexHtConstraintSatisfier::tToSolveVertInfo>, std::__1::hash<int>, true>, std::__1::__unordered_map_equal<int, std::__1::__hash_value_type<int, cSC4VertexHtConstraintSatisfier::tToSolveVertInfo>, std::__1::equal_to<int>, true>, std::__1::allocator<std::__1::__hash_value_type<int, cSC4VertexHtConstraintSatisfier::tToSolveVertInfo>>>` | 1 | — |
| `std::__1::__split_buffer<cSC4VertexHtConstraintSatisfier::tSolvedVert, std::__1::allocator<cSC4VertexHtConstraintSatisfier::tSolvedVert>&>` | 1 | — |
| `std::__1::__list_imp<std::__1::set<int, std::__1::less<int>, std::__1::allocator<int>>*, std::__1::allocator<std::__1::set<int, std::__1::less<int>, std::__1::allocator<int>>*>>` | 1 | — |
| `std::__1::list<std::__1::set<int, std::__1::less<int>, std::__1::allocator<int>>*, std::__1::allocator<std::__1::set<int, std::__1::less<int>, std::__1::allocator<int>>*>>` | 1 | — |
| `std::__1::__hash_const_iterator<std::__1::__hash_node<std::__1::__hash_value_type<int, float>, void*>*> std::__1::__hash_table<std::__1::__hash_value_type<int, float>, std::__1::__unordered_map_hasher<int, std::__1::__hash_value_type<int, float>, std::__1::hash<int>, true>, std::__1::__unordered_map_equal<int, std::__1::__hash_value_type<int, float>, std::__1::equal_to<int>, true>, std::__1::allocator<std::__1::__hash_value_type<int, float>>>` | 1 | — |
| `std::__1::__hash_iterator<std::__1::__hash_node<std::__1::__hash_value_type<int, float>, void*>*> std::__1::__hash_table<std::__1::__hash_value_type<int, float>, std::__1::__unordered_map_hasher<int, std::__1::__hash_value_type<int, float>, std::__1::hash<int>, true>, std::__1::__unordered_map_equal<int, std::__1::__hash_value_type<int, float>, std::__1::equal_to<int>, true>, std::__1::allocator<std::__1::__hash_value_type<int, float>>>` | 1 | — |
| `void std::__1::vector<cSC4VertexHtConstraintSatisfier::tSolvedVert, std::__1::allocator<cSC4VertexHtConstraintSatisfier::tSolvedVert>>::__push_back_slow_path<cSC4VertexHtConstraintSatisfier` | 1 | — |
| `std::__1::list<cRZString, std::__1::allocator<cRZString>>` | 1 | — |
| `std::__1::__list_imp<nGZLog::cService::DeferredOutputData, std::__1::allocator<nGZLog::cService::DeferredOutputData>>` | 1 | — |
| `cGZWinFolderRow` | 1 | — |
| `std::__1::list<tFolderTab, std::__1::allocator<tFolderTab>>` | 1 | — |
| `void std::__1::vector<cISC4CatalogItem*, std::__1::allocator<cISC4CatalogItem*>>` | 1 | — |
