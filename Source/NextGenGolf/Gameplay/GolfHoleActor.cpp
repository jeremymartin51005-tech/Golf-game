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

void AGolfHoleActor::BuildFromHole(const golf::HoleLayout& Hole)
{
	const int32 NX = Hole.nx, NY = Hole.ny;
	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	TArray<int32> Triangles;
	Vertices.Reserve(NX * NY); Normals.Reserve(NX * NY); UVs.Reserve(NX * NY); Colors.Reserve(NX * NY);
	Triangles.Reserve((NX - 1) * (NY - 1) * 6);

	for (int32 IY = 0; IY < NY; ++IY)
	{
		for (int32 IX = 0; IX < NX; ++IX)
		{
			const golf::Vec2 P{Hole.gridMin.x + IX * Hole.cell, Hole.gridMin.y + IY * Hole.cell};
			const golf::Vec3 N = Hole.normal(P);
			Vertices.Add(UGolfShotComponent::ToUnreal({P.x, P.y, Hole.height(P)}));
			Normals.Add(FVector(N.y, N.x, N.z));
			UVs.Add(FVector2D(double(IX) / (NX - 1), double(IY) / (NY - 1)));
			Colors.Add(SurfaceColor(Hole.surface(P)));
		}
	}
	for (int32 IY = 0; IY < NY - 1; ++IY)
	{
		for (int32 IX = 0; IX < NX - 1; ++IX)
		{
			const int32 A = IY * NX + IX, B = A + 1, C = A + NX, D = C + 1;
			// Unreal is left-handed with clockwise front faces when viewed from above.
			Triangles.Append({A, B, C, B, D, C});
		}
	}
	Terrain->ClearAllMeshSections();
	Terrain->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, /*bCreateCollision=*/false);
	if (TerrainMaterial) { Terrain->SetMaterial(0, TerrainMaterial); }
}
