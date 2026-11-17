#pragma once

#include <string>

/**
    The passes, in the order a frame runs them (SPEC-bubble.md, The chain).

    - **Advect**, once per 1/720 s step of film age that is due and only while
      Swirl is on: the displacement grid (R32F, 512 columns) read at the cell's
      point traced back by midpoint RK2 through the six box modes; d_new =
      d_old(x_b) + (y_b - y). With no flow it is the resample a change of
      aspect uses.
    - **Composite**, to the host: per pixel the origin height from the grid,
      the thickness from the law and the shape table, the black film, the
      burst's arrival test, the tint from the colour table, the clip.

    Every texture is read texel for texel with `texelFetch`; the one kind of
    interpolation is bilinear by hand.
*/
namespace bubble::shaders
{
enum class Pass
{
	Advect,
	Composite
};

constexpr int kModes        = 6;
constexpr int kArrivalTaps  = 48;

std::string AssembleVertex( Pass pass );
std::string Assemble( Pass pass );

} // namespace bubble::shaders
