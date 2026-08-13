// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraControlLibrary.h"
#include "UnityUnrealCoordinateConverter.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"

// === 좌표 변환 ===
// legacy Unity(x,y,z; m) -> UE(z,x,y; cm). 내부 JSON은 Unreal 미터로 정규화한다.

FVector UCameraControlLibrary::UnityPosToUE(const FCamVec3& UnityMeters, float MetersToUU)
{
	return UUnityUnrealCoordinateConverter::UnrealMetersToWorld(
		UUnityUnrealCoordinateConverter::UnityMetersToUnrealMeters(FVector(UnityMeters.x, UnityMeters.y, UnityMeters.z)), MetersToUU);
}

FCamVec3 UCameraControlLibrary::UEToUnityPos(const FVector& UECm, float MetersToUU)
{
	const float U = (FMath::IsNearlyZero(MetersToUU)) ? 1.f : MetersToUU;
	FCamVec3 Out;
	const FVector Unity = UUnityUnrealCoordinateConverter::UnrealMetersToUnityMeters(
		UUnityUnrealCoordinateConverter::WorldToUnrealMeters(UECm, U));
	Out.x = static_cast<float>(Unity.X);
	Out.y = static_cast<float>(Unity.Y);
	Out.z = static_cast<float>(Unity.Z);
	return Out;
}

FVector UCameraControlLibrary::UnrealMetersToWorld(const FCamVec3& UnrealMeters, float MetersToUU)
{
	return UUnityUnrealCoordinateConverter::UnrealMetersToWorld(FVector(UnrealMeters.x, UnrealMeters.y, UnrealMeters.z), MetersToUU);
}

FCamVec3 UCameraControlLibrary::WorldToUnrealMeters(const FVector& UECm, float MetersToUU)
{
	const FVector UnrealMeters = UUnityUnrealCoordinateConverter::WorldToUnrealMeters(UECm, MetersToUU);
	FCamVec3 Out;
	Out.x = static_cast<float>(UnrealMeters.X);
	Out.y = static_cast<float>(UnrealMeters.Y);
	Out.z = static_cast<float>(UnrealMeters.Z);
	return Out;
}

// === 줌 ↔ FOV (설계 §7.3 + 규격 정정 설계 §4) ===
// UE FOVAngle 은 이미 수평 화각 → aspect 변환 불필요.
// 실장비(휴컴스 HNR-2036LA: 광각 H 56.5° / V 33.63°, 광학 x36)의 렌즈는 화각이 배율에 반비례하지 않는다.
//  horizontalFov = 2·atan( tan(DefaultHFov/2) / clamp(zoom, 1, MaxZoom) )   ← 탄젠트 광학 모델
//  zoom<1(0 포함) → 1 선클램프(§12-C). MaxZoom 이 비정상(<=1)이면 1 로 보정(0나눗셈/역전 방지).

namespace
{
	/**
	 * 화각(도)의 반각 탄젠트. FMath::Tan 은 라디안이므로 반각을 먼저 취한 뒤 라디안화한다(규격 정정 설계 §4.1).
	 * FovDeg 를 (0,180) 열린구간으로 선클램프해 0나눗셈·부호반전·발산·NaN 을 일괄 차단한다(규격 정정 설계 §3.3).
	 *  하한 0.001° → tan≈8.727e-6 (항상 양수·비영), 상한 179.999° → tan≈1.146e5 (유한).
	 * 정의역 이탈은 실제로 도달 가능하다 — HFov 는 cam.setFOV 로 들어오는 외부 RPC 입력이고,
	 * DefaultHFov 는 APTZCameraActor 의 EditAnywhere UPROPERTY 다.
	 */
	float TanHalfFovDeg(float FovDeg)
	{
		const float Clamped = FMath::Clamp(FovDeg, 0.001f, 179.999f);
		return FMath::Tan(FMath::DegreesToRadians(Clamped * 0.5f));
	}
}

float UCameraControlLibrary::ZoomToHFov(float Zoom, float MaxZoom, float DefaultHFov)
{
	const float MaxZ = (MaxZoom < 1.f) ? 1.f : MaxZoom;
	const float ZoomClamped = FMath::Clamp(Zoom, 1.f, MaxZ);
	// Atan 반환은 라디안 → 2배한 뒤 도로 환산(규격 정정 설계 §4.1). zoom=1 이면 2·atan(tan(H/2))=H 항등.
	return FMath::RadiansToDegrees(2.f * FMath::Atan(TanHalfFovDeg(DefaultHFov) / ZoomClamped));
}

float UCameraControlLibrary::HFovToZoom(float HFov, float MaxZoom, float DefaultHFov)
{
	const float MaxZ = (MaxZoom < 1.f) ? 1.f : MaxZoom;
	if (HFov <= 0.f)
	{
		// 화각 0 이하 → 무한대 줌 → 최대 배율로 클램프.
		return MaxZ;
	}
	// 분자·분모 모두 헬퍼가 라디안 변환을 마친 무차원 비율 → 추가 도↔라디안 변환 금지(규격 정정 설계 §4.1).
	const float Zoom = TanHalfFovDeg(DefaultHFov) / TanHalfFovDeg(HFov);
	return FMath::Clamp(Zoom, 1.f, MaxZ);
}

// === PTZ 회전 (설계 §7.2 — 부호 §11 가정) ===
FRotator UCameraControlLibrary::PanTiltToRotator(float Pan, float Tilt)
{
	// FRotator(Pitch, Yaw, Roll). 1차 가정: Yaw=Pan, Pitch=-Tilt, Roll=0.
	return FRotator(-Tilt, Pan, 0.f);
}

void UCameraControlLibrary::RotatorToPanTilt(const FRotator& Rot, float& OutPan, float& OutTilt)
{
	OutPan  = Rot.Yaw;
	OutTilt = -Rot.Pitch;
}

// === 슬라이더 범위 매핑 ===
float UCameraControlLibrary::SliderToValue(float Slider01, float Min, float Max)
{
	return FMath::Lerp(Min, Max, Slider01);
}

float UCameraControlLibrary::ValueToSlider(float Value, float Min, float Max)
{
	const float Range = Max - Min;
	// Min==Max(및 근접) 시 0나눗셈 방어. 정확 0 비교를 먼저 두어 상수 인라인(테스트)
	// 시 MSVC가 IsNearlyZero를 폴딩 못해 발생하는 C4723(0나눗셈) 오탐을 제거한다.
	if (Range == 0.f || FMath::IsNearlyZero(Range))
	{
		return 0.f;
	}
	return (Value - Min) / Range;
}

// === 거리 / 각도 (설계 §7.4, 축 XZ→UE X/Y평면·높이=Z 제거) ===
float UCameraControlLibrary::DistanceXZ(const FVector& A, const FVector& B)
{
	FVector D = A - B;
	D.Z = 0.f; // UE Z(높이) 제거 → X/Y 수평 평면 거리.
	return static_cast<float>(D.Size());
}

float UCameraControlLibrary::Distance3D(const FVector& A, const FVector& B)
{
	return static_cast<float>((A - B).Size());
}

float UCameraControlLibrary::WorldCentimetersToMeters(float WorldCentimeters, float MetersToUU)
{
	return WorldCentimeters / ((MetersToUU > 0.f) ? MetersToUU : 100.f);
}

float UCameraControlLibrary::SignedAngleAroundUp(const FVector& From, const FVector& To)
{
	const FVector F = From.GetSafeNormal();
	const FVector T = To.GetSafeNormal();
	if (F.IsNearlyZero() || T.IsNearlyZero())
	{
		return 0.f;
	}
	const float Dot = FMath::Clamp(static_cast<float>(FVector::DotProduct(F, T)), -1.f, 1.f);
	const float Ang = FMath::RadiansToDegrees(FMath::Acos(Dot));
	// UE +Z(Up) 기준 cross.Z 부호로 좌/우 판별: 우(+Y)=(+), 좌=(-). (Unity Vector3.SignedAngle up 축 대응)
	const float CrossZ = static_cast<float>(FVector::CrossProduct(F, T).Z);
	return (CrossZ < 0.f) ? -Ang : Ang;
}

void UCameraControlLibrary::VertHorzAngleToTarget(const FVector& Cam, const FVector& Target,
	const FVector& RefDirBase, float& OutVertDeg, float& OutHorzDeg)
{
	// 수평 벡터(카메라 → 타겟, 높이 Z 제거).
	const FVector ToTargetHorz(Target.X - Cam.X, Target.Y - Cam.Y, 0.f);
	const float   HDist = static_cast<float>(ToTargetHorz.Size());

	// ── 수직각 ── 높이차 = 카메라 Z - 타겟 Z (양수 = 카메라가 위 → 내려다봄 = +).
	const float DeltaHeight = static_cast<float>(Cam.Z - Target.Z);
	if (HDist > 0.001f)
	{
		OutVertDeg = FMath::RadiansToDegrees(FMath::Atan2(DeltaHeight, HDist));
	}
	else
	{
		OutVertDeg = (DeltaHeight >= 0.f) ? 90.f : -90.f; // 수평거리≈0 폴백.
	}

	// ── 수평각 ── 기준방향(카메라 → RefDirBase, 높이 제거) 대비 signed angle.
	OutHorzDeg = 0.f;
	if (HDist > 0.001f)
	{
		FVector BaseDir(RefDirBase.X - Cam.X, RefDirBase.Y - Cam.Y, 0.f);
		if (BaseDir.SizeSquared() > 1e-5f)
		{
			OutHorzDeg = SignedAngleAroundUp(BaseDir, ToTargetHorz);
		}
	}
}

void UCameraControlLibrary::TargetLineAngles(const FVector& Cam, const FVector& LineStart, const FVector& LineEnd,
	FVector& OutRefPoint, float& OutStartDeg, float& OutEndDeg)
{
	// 모든 계산을 X/Y 평면(높이 Z 제거)에서 수행.
	const FVector Cam2(Cam.X, Cam.Y, 0.f);
	const FVector S2(LineStart.X, LineStart.Y, 0.f);
	const FVector E2(LineEnd.X, LineEnd.Y, 0.f);

	OutStartDeg = 0.f;
	OutEndDeg   = 0.f;

	FVector LineDir = E2 - S2;
	if (!LineDir.Normalize())
	{
		OutRefPoint = S2; // 라인 길이≈0 → 직교점을 시작점으로 폴백.
		return;
	}

	// 수선의 발(직교점): 카메라 → 라인 위 최근접점.
	const float T = static_cast<float>(FVector::DotProduct(Cam2 - S2, LineDir));
	const FVector RefPt = S2 + T * LineDir;
	OutRefPoint = RefPt;

	// 0° 기준방향: 카메라 → 직교점.
	const FVector RefDir = RefPt - Cam2;
	if (RefDir.SizeSquared() < 1e-3f)
	{
		return; // 카메라가 직교점 위 → 각도 계산 불가(0 유지).
	}

	OutStartDeg = SignedAngleAroundUp(RefDir, S2 - Cam2);
	OutEndDeg   = SignedAngleAroundUp(RefDir, E2 - Cam2);
}

// === JSON 입출력 (UCarPlacementLibrary::Save/LoadCarDatasFromJson 와 동일 패턴 + §12-C 보정) ===
bool UCameraControlLibrary::SaveToJson(const FString& Path, const FCameraPosList& Data)
{
	FCameraPosList UnrealData = Data;
	UnrealData.isUnreal = true;
	FString Json;
	if (!FJsonObjectConverter::UStructToJsonObjectString(UnrealData, Json))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CameraControl] JSON 직렬화 실패"));
		return false;
	}

	// 인코딩을 명시하지 않으면 AutoDetect 가 비-ASCII 한 글자에도 UTF-16 으로 쓴다.
	if (!FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CameraControl] 파일 쓰기 실패: %s"), *Path);
		return false;
	}
	UE_LOG(LogTemp, Log, TEXT("[CameraControl] 저장(UE 좌표) 카메라 %d대 → %s"), UnrealData.datas.Num(), *Path);
	return true;
}

bool UCameraControlLibrary::LoadFromJson(const FString& Path, FCameraPosList& Out)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CameraControl] 파일 읽기 실패: %s"), *Path);
		return false;
	}

	Out = FCameraPosList();
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Out, 0, 0))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CameraControl] JSON 역직렬화 실패"));
		return false;
	}

	const bool bSourceIsUnreal = Out.isUnreal;
	NormalizeLoaded(Out, bSourceIsUnreal); // §12-C 보정 + 좌표 정규화.
	Out.isUnreal = true;
	UE_LOG(LogTemp, Log, TEXT("[CameraControl] 로드 카메라 %d대 ← %s"), Out.datas.Num(), *Path);
	return true;
}

void UCameraControlLibrary::NormalizeLoaded(FCameraPosList& Data, bool bSourceIsUnreal)
{
	for (FCameraPos& CamPos : Data.datas)
	{
		for (FCamDir& Dir : CamPos.datas)
		{
			if (!bSourceIsUnreal)
			{
				const FVector UnrealMeters = UUnityUnrealCoordinateConverter::UnityMetersToUnrealMeters(FVector(Dir.pos.x, Dir.pos.y, Dir.pos.z));
				Dir.pos = { static_cast<float>(UnrealMeters.X), static_cast<float>(UnrealMeters.Y), static_cast<float>(UnrealMeters.Z) };
			}
			// ptzmax.z: 구버전 파일 360 오저장 또는 0이하 → 36 클램프.
			if (Dir.ptzmax.z > 36.f || Dir.ptzmax.z <= 0.f)
			{
				Dir.ptzmax.z = 36.f;
			}
			// preset_id 0 → 1 (1부터 시작).
			if (Dir.preset_id == 0)
			{
				Dir.preset_id = 1;
			}
			// zoom < 1(0 포함) → 1 (58/zoom 0나눗셈 방지).
			if (Dir.zoom < 1.f)
			{
				Dir.zoom = 1.f;
			}
			// rot 와 pan/tilt 중복 저장 → 로드 직후 동기화(원본 SCamDir.Rot() setter 대응).
			Dir.pan  = Dir.rot.y;
			Dir.tilt = Dir.rot.x;
		}
	}
}
