// UNVERIFIED: written without an Unreal toolchain available; not yet compiled.
#include "Input/GolfMouseSampler.h"
#include "HAL/PlatformTime.h"
#include "Input/Events.h"

bool FGolfMouseSampler::HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (bCapturing && OnSample)
	{
		const FVector2D Delta = MouseEvent.GetCursorDelta();
		// Screen +Y is toward the player, which is the backswing direction the analyser expects.
		OnSample(golf::MouseSample{FPlatformTime::Seconds(), Delta.X, Delta.Y});
	}
	return false; // never swallow input; UI and camera still see it
}
