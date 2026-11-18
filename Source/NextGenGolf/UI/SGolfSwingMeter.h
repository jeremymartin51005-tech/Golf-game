// UNVERIFIED: written without an Unreal toolchain available; not yet compiled.
#pragma once
#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

/**
 * Swing gauge, drawn entirely in code (no texture assets): a vertical capsule whose fill is the
 * backswing (power), a glowing band for the perfect zone (power that reaches the pin, or a full swing),
 * a live carry readout riding the fill, a tempo strip with a dot, and a grade badge after the swing.
 * Same design as the browser build's gauge.
 */
class NEXTGENGOLF_API SGolfSwingMeter : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SGolfSwingMeter) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs) {}

	/** Call every frame. Power and zone are 0..1.1 (1 = full swing). */
	void SetLive(float InPower, float InZoneCenter, float InZoneHalfWidth, const FText& InCarry, const FText& InPrompt);
	/** Call once per stroke after impact. Timing < 0 = early/fast, > 0 = late/slow. */
	void SetResult(const FText& InGrade, const FLinearColor& InGradeColor, float InTiming);
	void ClearResult();

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(220.f, 420.f); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out,
	                      int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
	float TargetPower = 0.f;
	mutable float ShownPower = 0.f;           // eased toward TargetPower while painting
	mutable double LastPaintTime = 0.0;
	float ZoneCenter = 1.f, ZoneHalfWidth = 0.035f;
	FText Carry, Prompt, Grade;
	FLinearColor GradeColor = FLinearColor(0.44f, 0.94f, 0.6f);
	float Timing = 0.f;
	bool bHasResult = false;
	double ResultTime = 0.0;
};
