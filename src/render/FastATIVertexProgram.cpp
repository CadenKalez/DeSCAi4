#include "render/FastATIVertexProgram.h"

#include <string>

#include "render/GLApi.h"

namespace sgl {

std::uint32_t FastATIVertexProgram::aVertexPrograms[2][2] = {};
std::uint32_t FastATIVertexProgram::pBoundProgram = 0;
std::int32_t FastATIVertexProgram::bEnabled = 0;

// Program text is assembled line by line (no separators) exactly as the original appends literals; the outer index picks
// where unit 0's coordinates come from (0: transformed position, 1: the vertex texcoord) and the inner index does the
// same for unit 1.
void FastATIVertexProgram::Init() {
    pBoundProgram = 0;
    bEnabled = 0;
    for (std::uint32_t unit0 = 0; unit0 < 2; ++unit0) {
        for (std::uint32_t unit1 = 0; unit1 < 2; ++unit1) {
            std::string text;
            GL()->GenProgramsARB(1, &aVertexPrograms[unit0][unit1]);
            text += "!!ARBvp1.0\n";
            text += "TEMP   transformedPosition;";
            text += "TEMP   transformedTexCoord0;";
            text += "TEMP   transformedTexCoord1;";
            text += "DP4 transformedPosition.x, state.matrix.mvp.row[0], vertex.position;";
            text += "DP4 transformedPosition.y, state.matrix.mvp.row[1], vertex.position;";
            text += "DP4 transformedPosition.z, state.matrix.mvp.row[2], vertex.position;";
            text += "DP4 transformedPosition.w, state.matrix.mvp.row[3], vertex.position;";
            if (unit0 == 0) {
                text += "DP4 transformedTexCoord0.x, state.matrix.texture[0].row[0], vertex.position;";
                text += "DP4 transformedTexCoord0.y, state.matrix.texture[0].row[1], vertex.position;";
                text += "DP4 transformedTexCoord0.z, state.matrix.texture[0].row[2], vertex.position;";
                text += "DP4 transformedTexCoord0.w, state.matrix.texture[0].row[3], vertex.position;";
                text += "MOV result.texcoord, transformedTexCoord0;";
            } else {
                text += "DP4 transformedTexCoord0.x, state.matrix.texture[0].row[0], vertex.texcoord;";
                text += "DP4 transformedTexCoord0.y, state.matrix.texture[0].row[1], vertex.texcoord;";
                text += "DP4 transformedTexCoord0.z, state.matrix.texture[0].row[2], vertex.texcoord;";
                text += "DP4 transformedTexCoord0.w, state.matrix.texture[0].row[3], vertex.texcoord;";
                text += "MOV result.texcoord, transformedTexCoord0;";
            }
            if (unit1 == 0) {
                text += "DP4 transformedTexCoord1.x, state.matrix.texture[1].row[0], vertex.position;";
                text += "DP4 transformedTexCoord1.y, state.matrix.texture[1].row[1], vertex.position;";
                text += "DP4 transformedTexCoord1.z, state.matrix.texture[1].row[2], vertex.position;";
                text += "DP4 transformedTexCoord1.w, state.matrix.texture[1].row[3], vertex.position;";
                text += "MOV result.texcoord[1], transformedTexCoord1;";
            } else {
                text += "DP4 transformedTexCoord1.x, state.matrix.texture[1].row[0], vertex.texcoord[1];";
                text += "DP4 transformedTexCoord1.y, state.matrix.texture[1].row[1], vertex.texcoord[1];";
                text += "DP4 transformedTexCoord1.z, state.matrix.texture[1].row[2], vertex.texcoord[1];";
                text += "DP4 transformedTexCoord1.w, state.matrix.texture[1].row[3], vertex.texcoord[1];";
                text += "MOV result.texcoord[1], transformedTexCoord1;";
            }
            text += "MOV result.color, state.lightmodel.scenecolor;";
            text += "MOV result.position, transformedPosition;";
            text += "END";
            GL()->BindProgramARB(gl::VERTEX_PROGRAM_ARB, aVertexPrograms[unit0][unit1]);
            GL()->ProgramStringARB(gl::VERTEX_PROGRAM_ARB, 0x8875, static_cast<GLsizei>(text.size()), text.data());
        }
    }
}

void FastATIVertexProgram::Shutdown() {
    if (bEnabled != 0) {
        bEnabled = 0;
        GL()->Disable(gl::VERTEX_PROGRAM_ARB);
    }
    pBoundProgram = 0;
    for (std::uint32_t a = 0; a < 2; ++a)
        for (std::uint32_t b = 0; b < 2; ++b) GL()->DeleteProgramsARB(1, &aVertexPrograms[a][b]);
}

}  // namespace sgl
