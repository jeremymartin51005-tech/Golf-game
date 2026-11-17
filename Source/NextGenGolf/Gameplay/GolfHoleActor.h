// UNVERIFIED: written without an Unreal toolchain available; not yet compiled.
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "golf/Course.h"
#include "GolfHoleActor.generated.h"

class UProceduralMeshComponent;

/**
 * Renders one generated hole: a terrain mesh from the core's heightfield, vertex-coloured by surface.
 * Assign a material that feeds Vertex Color into Base Color (TerrainMaterial); trees, water and
 * instanced grass are later passes on top of this.
 */
UCLASS()
class NEXTGENGOLF_API AGolfHoleActor : public AActor
{
	GENERATED_BODY()
public:
	AGolfHoleActor();
	void BuildFromHole(const golf::HoleLayout& Hole);

	UPROPERTY(VisibleAnywhere, Category = "Golf") TObjectPtr<UProceduralMeshComponent> Terrain;
	UPROPERTY(EditAnywhere, Category = "Golf") TObjectPtr<UMaterialInterface> TerrainMaterial;

	static FLinearColor SurfaceColor(golf::Surface Surface);
};
