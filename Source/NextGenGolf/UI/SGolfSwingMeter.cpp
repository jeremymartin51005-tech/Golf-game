// UNVERIFIED: written without an Unreal toolchain available; not yet compiled.
#include "UI/SGolfSwingMeter.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

void SGolfSwingMeter::SetLive(float InPower, float InZoneCenter, float InZoneHalfWidth, const FText& InCarry, const FText& InPrompt)
{
	TargetPower = FMath::Clamp(InPower, 0.f, 1.15f);
	ZoneCenter = InZoneCenter;
	ZoneHalfWidth = InZoneHalfWidth;
	Carry = InCarry;
	Prompt = InPrompt;
}

void SGolfSwingMeter::SetResult(const FText& InGrade, const FLinearColor& InGradeColor, float InTiming)
{
	Grade = InGrade;
	GradeColor = InGradeColor;
	Timing = InTiming;
	bHasResult = true;
	ResultTime = FSlateApplication::Get().GetCurrentTime();
}

void SGolfSwingMeter::ClearResult() { bHasResult = false; }

int32 SGolfSwingMeter::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out,
                               int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const double Now = FSlateApplication::Get().GetCurrentTime();
	const float Dt = LastPaintTime > 0.0 ? float(FMath::Min(Now - LastPaintTime, 0.1)) : 0.f;
	LastPaintTime = Now;
	ShownPower += (TargetPower - ShownPower) * (1.f - FMath::Exp(-Dt * 26.f));

	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const FSlateBrush* Round = FCoreStyle::Get().GetBrush("Border");
	const FSlateFontInfo Big = FCoreStyle::GetDefaultFontStyle("Bold", 16);
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 10);

	const FVector2D Size = Geometry.GetLocalSize();
	const float CapW = 30.f, CapH = 280.f, CapX = Size.X - CapW, CapY = 70.f, Travel = CapH - 8.f;
	auto Box = [&](float X, float Y, float W, float H, const FLinearColor& C, const FSlateBrush* B)
	{
		FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y))), B, ESlateDrawEffect::None, C);
	};
	auto Text = [&](const FText& T, float X, float Y, const FSlateFontInfo& F, const FLinearColor& C)
	{
		FSlateDrawElement::MakeText(Out, Layer + 2, Geometry.ToPaintGeometry(FVector2f(200.f, 24.f), FSlateLayoutTransform(FVector2f(X, Y))), T, F, ESlateDrawEffect::None, C);
	};
	auto PowerY = [&](float P) { return CapY + CapH - 4.f - FMath::Clamp(P, 0.f, 1.1f) / 1.1f * Travel; };

	// Capsule, overswing band, ticks.
	Box(CapX, CapY, CapW, CapH, FLinearColor(0.03f, 0.06f, 0.04f, 0.65f), White);
	Box(CapX, CapY, CapW, CapH * (0.1f / 1.1f), FLinearColor(0.94f, 0.36f, 0.28f, 0.45f), White);
	for (float T : {0.25f, 0.5f, 0.75f}) { Box(CapX, PowerY(T), 8.f, 2.f, FLinearColor(1, 1, 1, 0.4f), White); }

	// Fill.
	const bool bOver = ShownPower > 1.04f;
	const float FillTop = PowerY(ShownPower);
	Box(CapX + 4.f, FillTop, CapW - 8.f, CapY + CapH - 4.f - FillTop,
	    bOver ? FLinearColor(0.94f, 0.36f, 0.28f) : FLinearColor(1.f, 0.86f, 0.45f), White);

	// Perfect zone band.
	const float ZTop = PowerY(ZoneCenter + ZoneHalfWidth), ZBot = PowerY(ZoneCenter - ZoneHalfWidth);
	const bool bInZone = FMath::Abs(ShownPower - ZoneCenter) <= ZoneHalfWidth;
	Box(CapX - 2.f, ZTop, CapW + 4.f, FMath::Max(10.f, ZBot - ZTop), FLinearColor(0.44f, 0.94f, 0.6f, bInZone ? 0.55f : 0.25f), Round);

	// Carry readout riding the fill, and the coaching prompt.
	if (ShownPower > 0.03f) { Text(Carry, CapX - 110.f, FillTop - 10.f, Big, FLinearColor::White); }
	Text(Prompt, CapX - 170.f, CapY - 34.f, Big, bInZone ? FLinearColor(0.44f, 0.94f, 0.6f) : FLinearColor::White);

	// Tempo strip (fast | tempo | slow) and the result.
	const float StripY = CapY + CapH + 14.f, StripX = Size.X - 150.f;
	Box(StripX, StripY, 150.f, 8.f, FLinearColor(1.f, 0.82f, 0.4f, 0.75f), White);
	Box(StripX + 63.f, StripY, 24.f, 8.f, FLinearColor(0.44f, 0.94f, 0.6f, 0.95f), White);
	Text(FText::FromString(TEXT("FAST        TEMPO        SLOW")), StripX, StripY + 10.f, Small, FLinearColor(1, 1, 1, 0.7f));
	if (bHasResult)
	{
		const float DotX = StripX + 75.f + FMath::Clamp(Timing * 110.f, -70.f, 70.f);
		Box(DotX - 7.f, StripY - 3.f, 14.f, 14.f, FLinearColor::White, Round);
		const float Pop = FMath::Clamp(float(Now - ResultTime) / 0.25f, 0.f, 1.f);
		const float W = 130.f * Pop, H = 28.f * Pop;
		Box(Size.X - 75.f - W * 0.5f, StripY + 40.f - H * 0.5f + 14.f, W, H, GradeColor, White);
		if (Pop >= 1.f) { Text(Grade, Size.X - 120.f, StripY + 44.f, Big, FLinearColor(0.06f, 0.09f, 0.06f)); }
	}
	return Layer + 3;
}
