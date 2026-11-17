// UNVERIFIED: written without an Unreal toolchain available; not yet compiled.
#pragma once
#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"
#include "golf/Swing.h"

/**
 * Captures mouse movement at the rate the OS delivers it (up to 1000 Hz), not once per rendered frame.
 * Each event is timestamped on arrival, so swing tempo is measured independently of frame rate and a
 * dropped frame cannot distort a swing. Register with FSlateApplication::RegisterInputPreProcessor.
 */
class NEXTGENGOLF_API FGolfMouseSampler : public IInputProcessor
{
public:
	TFunction<void(const golf::MouseSample&)> OnSample;
	bool bCapturing = false;

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual const TCHAR* GetDebugName() const override { return TEXT("GolfMouseSampler"); }
};
