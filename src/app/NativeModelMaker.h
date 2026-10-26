// The city's model pipeline on the native Metal application: the S3D model resource (type 0x5ad0e817) the resource
// manager hands cSC4ModelMaker, the texture bindings its materials point at, and the hosts that connect the recovered
// model maker, placeable objects and model instances to the city and the 3D renderer.
#pragma once

namespace gd { class cIGZGDriver; }
namespace sc4 { class cSC43DRender; class cSC4City; }

namespace native {

// Registers the S3D model factory with the resource manager (once) and makes the driver the texture target.
void InstallModelResources(gd::cIGZGDriver* driver);
// The driver texture of an FSH resource {0x7ab50e44, group, instance} (0 when it is missing).
unsigned int FshTextureId(unsigned int group, unsigned int instance);
// Builds and initialises the model maker for the loaded city; its models go into `render`.
bool AttachModelMaker(sc4::cSC43DRender& render, sc4::cSC4City& city);

}  // namespace native
