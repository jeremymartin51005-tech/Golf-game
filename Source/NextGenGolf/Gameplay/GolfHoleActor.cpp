// UNVERIFIED: written without an Unreal toolchain available; not yet compiled.
#include "Gameplay/GolfHoleActor.h"
#include "Gameplay/GolfShotComponent.h"
#include "ProceduralMeshComponent.h"

AGolfHoleActor::AGolfHoleActor()
{
	Terrain = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Terrain"));
	RootComponent = Terrain;
	Terrain->bUseAsyncCooking = true;
}

FLinearColor AGolfHoleActor::SurfaceColor(golf::Surface Surface)
{
	switch (Surface)
	{
		case golf::Surface::Tee:        return FLinearColor(0.10f, 0.31f, 0.08f);
		case golf::Surface::Fairway:    return FLinearColor(0.12f, 0.36f, 0.08f);
		case golf::Surface::FirstCut:   return FLinearColor(0.09f, 0.28f, 0.06f);
		case golf::Surface::Rough:      return FLinearColor(0.06f, 0.20f, 0.05f);
		case golf::Surface::DeepRough:  return FLinearColor(0.04f, 0.14f, 0.04f);
		case golf::Surface::Sand:       return FLinearColor(0.78f, 0.66f, 0.38f);
		case golf::Surface::Green:      return FLinearColor(0.18f, 0.50f, 0.13f);
		case golf::Surface::Fringe:     return FLinearColor(0.14f, 0.40f, 0.09f);
		case golf::Surface::HardGround: return FLinearColor(0.30f, 0.27f, 0.20f);
		case golf::Surface::Water:      return FLinearColor(0.03f, 0.14f, 0.30f);
		default:                        return FLinearColor(0.04f, 0.11f, 0.03f);
	}
}

namespace
{
	// One grid of terrain: positions, analytic normals and surface colours sampled from the core.
	void AppendGrid(const golf::HoleLayout& Hole, int32 Cols, int32 Rows, double X0, double Y0, double Step,
	                TFunctionRef<bool(int32, int32)> SkipQuad, TArray<FVector>& V, TArray<FVector>& N, TArray<FVector2D>& UV,
	                TArray<FLinearColor>& C, TArray<int32>& T)
	{
		const double W = (Hole.nx - 1) * Hole.cell, H = (Hole.ny - 1) * Hole.cell;
		const int32 Base = V.Num();
		for (int32 IY = 0; IY < Rows; ++IY)
		{
			for (int32 IX = 0; IX < Cols; ++IX)
			{
				const golf::Vec2 P{X0 + IX * Step, Y0 + IY * Step};
				const golf::Vec3 Nn = Hole.normal(P);
				V.Add(UGolfShotComponent::ToUnreal({P.x, P.y, Hole.height(P)}));
				N.Add(FVector(Nn.y, Nn.x, Nn.z));
				UV.Add(FVector2D((P.x - Hole.gridMin.x) / W, (P.y - Hole.gridMin.y) / H));
				C.Add(AGolfHoleActor::SurfaceColor(Hole.surface(P)));
			}
		}
		for (int32 IY = 0; IY < Rows - 1; ++IY)
		{
			for (int32 IX = 0; IX < Cols - 1; ++IX)
			{
				if (SkipQuad(IX, IY)) { continue; }
				const int32 A = Base + IY * Cols + IX, B = A + 1, Cc = A + Cols, D = Cc + 1;
				T.Append({A, B, Cc, B, D, Cc});
			}
		}
	}
}

void AGolfHoleActor::BuildFromHole(const golf::HoleLayout& Hole)
{
	// The 2 m grid covers the hole except a rectangle around the green, which gets a 0.5 m mesh so every
	// putting contour is visible. Both sample the same core height function, so the seam has no gap.
	const int32 NX = Hole.nx, NY = Hole.ny;
	const double Cell = Hole.cell;
	int32 IX0 = 0, IY0 = 0, IX1 = 0, IY1 = 0;
	const bool bFine = Hole.fnx > 1;
	if (bFine)
	{
		IX0 = FMath::Max(0, int32(FMath::FloorToDouble((Hole.fineMin.x - Hole.gridMin.x) / Cell)) - 1);
		IY0 = FMath::Max(0, int32(FMath::FloorToDouble((Hole.fineMin.y - Hole.gridMin.y) / Cell)) - 1);
		IX1 = FMath::Min(NX - 1, int32(FMath::CeilToDouble((Hole.fineMin.x + (Hole.fnx - 1) * Hole.fineCell - Hole.gridMin.x) / Cell)) + 1);
		IY1 = FMath::Min(NY - 1, int32(FMath::CeilToDouble((Hole.fineMin.y + (Hole.fny - 1) * Hole.fineCell - Hole.gridMin.y) / Cell)) + 1);
	}

	TArray<FVector> Vertices, Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<int32> Triangles;
	TArray<FProcMeshTangent> Tangents;
	AppendGrid(Hole, NX, NY, Hole.gridMin.x, Hole.gridMin.y, Cell,
	           [&](int32 IX, int32 IY) { return bFine && IX >= IX0 && IX < IX1 && IY >= IY0 && IY < IY1; },
	           Vertices, Normals, UVs, Colors, Triangles);
	Terrain->ClearAllMeshSections();
	Terrain->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, /*bCreateCollision=*/false);

	if (bFine)
	{
		const int32 Sub = FMath::Max(1, FMath::RoundToInt32(Cell / Hole.fineCell));
		TArray<FVector> FV, FN;
		TArray<FVector2D> FUV;
		TArray<FLinearColor> FC;
		TArray<int32> FT;
		AppendGrid(Hole, (IX1 - IX0) * Sub + 1, (IY1 - IY0) * Sub + 1, Hole.gridMin.x + IX0 * Cell, Hole.gridMin.y + IY0 * Cell,
		           Cell / Sub, [](int32, int32) { return false; }, FV, FN, FUV, FC, FT);
		Terrain->CreateMeshSection_LinearColor(1, FV, FT, FN, FUV, FC, Tangents, /*bCreateCollision=*/false);
	}
	if (TerrainMaterial)
	{
		Terrain->SetMaterial(0, TerrainMaterial);
		Terrain->SetMaterial(1, TerrainMaterial);
	}
}
