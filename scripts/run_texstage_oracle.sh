#!/bin/bash
# Compares src/render/TextureStageData.cpp against the Ghidra decompilation of nSGLDX7::cTextureStageData.
set -e
cd "$(dirname "$0")/.."
mkdir -p work/texstage
python3 scripts/gen_block_oracle.py work/gz/nSGLDX7_cTextureStageData.txt work/texstage/oracle.c \
    0x10060444c 0x10060898c 0x1006089f2 0x100608c50 0x100608eb4 0x10060902c \
    --table vLookupCombineSource:0x100a268e0:16 --table DAT_100a26760:0x100a26760:16 \
    --sub '\bApplyTextureState\s*\(=>oracle_1006089f2(' --sub '\bApplyEnvModeState\s*\(=>oracle_100608c50(' \
    --sub '\bApplyColorCombinerState\s*\(=>oracle_100608eb4(' --sub '\bApplyAlphaCombinerState\s*\(=>oracle_10060902c(' \
    --sub 'cTextureManager::TextureHandle\s*\(=>mock_TextureHandle(' --sub '\bcTextureStageData\(this\);=>oracle_10060898c(this);'
cat > work/texstage/proto.h <<'H'
typedef unsigned char cTextureStageData, IDirect3DDevice7, cTextureManager;
unsigned long oracle_1006089f2(cTextureStageData*, int, IDirect3DDevice7*, cTextureManager*);
void oracle_100608c50(cTextureStageData*, int, IDirect3DDevice7*);
void oracle_100608eb4(cTextureStageData*, int, IDirect3DDevice7*);
void oracle_10060902c(cTextureStageData*, int, IDirect3DDevice7*);
void oracle_10060898c(cTextureStageData*);
long mock_TextureHandle(void*, unsigned);
H
clang -std=gnu11 -include work/texstage/proto.h -c work/texstage/oracle.c -o /tmp/texstage_oracle.o -Wno-everything
clang++ -std=c++20 -fsanitize=address,undefined -Isrc scripts/tests/render_texturestage_oracle_driver.cpp src/render/TextureStageData.cpp /tmp/texstage_oracle.o -o /tmp/texstage_test
/tmp/texstage_test
