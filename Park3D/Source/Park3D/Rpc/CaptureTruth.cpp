// Copyright Epic Games, Inc. All Rights Reserved.

#include "CaptureTruth.h"
#include "RpcModuleSupport.h"
#include "../CarActor.h"
#include "../CamLensDistortion.h"
#include "../PTZCameraActor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	/** 깊이 공간 렌더에 아무것도 안 그려진 픽셀(하늘·배경)을 거르는 상한(cm). 주차장 시야는 수백 m 안이다. */
	constexpr float TruthFarCm = 1.0e6f;

	/** 같은 표면으로 볼 깊이 차(cm). 두 렌더는 지터 없이 같은 격자라 자기 자신은 거의 정확히 겹친다 — 원거리 정밀도만 비례로 준다. */
	double TruthCarTolCm(double Depth) { return 1.0 + 0.001 * Depth; }
	/** 번호판은 판 메시 대신 바운즈의 평면으로 계산한다 — 테두리 모서리 굴림만큼 조금 더 준다. */
	double TruthPlateTolCm(double Depth) { return 1.5 + 0.002 * Depth; }

	bool TruthValidDepth(float D) { return D > 0.f && D < TruthFarCm && FMath::IsFinite(D); }

	/** 정수 픽셀 상자(포함 min, 포함 max)를 모은다. */
	struct FTruthPixBox
	{
		int32 X0 = MAX_int32, Y0 = MAX_int32, X1 = MIN_int32, Y1 = MIN_int32;
		int64 Count = 0;
		void Add(int32 X, int32 Y) { X0 = FMath::Min(X0, X); Y0 = FMath::Min(Y0, Y); X1 = FMath::Max(X1, X); Y1 = FMath::Max(Y1, Y); ++Count; }
		/** [x1,y1,x2,y2] 픽셀 가장자리 좌표(오른쪽·아래는 마지막 픽셀 + 1). 픽셀이 없으면 null. */
		TSharedPtr<FJsonValue> Json() const
		{
			if (Count == 0) { return MakeShared<FJsonValueNull>(); }
			TArray<TSharedPtr<FJsonValue>> A;
			A.Add(MakeShared<FJsonValueNumber>(X0));
			A.Add(MakeShared<FJsonValueNumber>(Y0));
			A.Add(MakeShared<FJsonValueNumber>(X1 + 1));
			A.Add(MakeShared<FJsonValueNumber>(Y1 + 1));
			return MakeShared<FJsonValueArray>(A);
		}
	};

	double TruthRound2(double V) { return FMath::RoundToDouble(V * 100.0) / 100.0; }
	double TruthRound4(double V) { return FMath::RoundToDouble(V * 10000.0) / 10000.0; }
}

namespace CaptureTruth
{
	bool FProjector::Project(const FVector& P, FVector2D& OutPx, double* OutDepth) const
	{
		const FVector D = P - C;
		const double X = FVector::DotProduct(D, Fwd);
		if (OutDepth) { *OutDepth = X; }
		if (X <= 1.0)
		{
			return false;
		}
		FVector2D Tan(FVector::DotProduct(D, Right) / X, -FVector::DotProduct(D, Up) / X);   // y 아래가 +
		if (!CamLens::IsPinhole(K1, K2)) { Tan = CamLens::DistortTan(Tan, K1, K2); }
		OutPx = FVector2D(W * 0.5 + F * Tan.X, H * 0.5 + F * Tan.Y);
		return true;
	}

	void BuildDepthIndexMap(const FProjector& Out, int32 Dw, int32 Dh, double DepthTan, TArray<int32>& OutMap)
	{
		OutMap.SetNumUninitialized(Out.W * Out.H);
		const double Fd = (Dw * 0.5) / DepthTan;
		for (int32 Y = 0; Y < Out.H; ++Y)
		{
			for (int32 X = 0; X < Out.W; ++X)
			{
				const FVector2D Xd((X + 0.5 - Out.W * 0.5) / Out.F, (Y + 0.5 - Out.H * 0.5) / Out.F);
				const FVector2D U = CamLens::UndistortTan(Xd, Out.K1, Out.K2);
				const int32 Rx = FMath::FloorToInt32(Dw * 0.5 + Fd * U.X);
				const int32 Ry = FMath::FloorToInt32(Dh * 0.5 + Fd * U.Y);
				OutMap[Y * Out.W + X] = (Rx >= 0 && Rx < Dw && Ry >= 0 && Ry < Dh) ? Ry * Dw + Rx : -1;
			}
		}
	}

	bool Build(APTZCameraActor* Cam, int32 CamId, int32 W, int32 H, bool bWithMask,
		TSharedPtr<FJsonObject>& OutTruth, TArray<uint8>& OutMask, FString& OutError)
	{
		const double T0 = FPlatformTime::Seconds();
		UWorld* World = Cam ? Cam->GetWorld() : nullptr;
		if (!Cam || !Cam->Capture || !World || W <= 0 || H <= 0)
		{
			OutError = TEXT("truth: 카메라·월드 없음");
			return false;
		}

		// ---- 투영 모델(출력 그림) ----
		const FTransform CamXf = Cam->Capture->GetComponentTransform();
		FProjector Pj;
		Pj.C = CamXf.GetLocation();
		Pj.Fwd = CamXf.GetUnitAxis(EAxis::X);
		Pj.Right = CamXf.GetUnitAxis(EAxis::Y);
		Pj.Up = CamXf.GetUnitAxis(EAxis::Z);
		Pj.W = W; Pj.H = H;
		const double HFov = Cam->Capture->FOVAngle;
		const double OutTan = FMath::Tan(FMath::DegreesToRadians(HFov * 0.5));
		Pj.F = (W * 0.5) / OutTan;
		Pj.K1 = Cam->GetLensK1(); Pj.K2 = Cam->GetLensK2();
		const bool bDistorted = !CamLens::IsPinhole(Pj.K1, Pj.K2);

		// ---- 깊이 공간(핀홀이면 출력과 같은 격자) ----
		const APTZCameraActor::FTruthDepthSpace Space = Cam->GetTruthDepthSpace(W, H);
		const int32 Dw = Space.W, Dh = Space.H;
		const double Fd = (Dw * 0.5) / Space.Tan;
		// 표는 출력 크기·화각·k 가 같으면 같다 — 픽셀마다 역왜곡(이분법)이라 매번 만들면 1280×720 에서 수 초가 든다. 게임 스레드 전용.
		static TArray<int32> Map;
		static FString MapKey;
		if (bDistorted)
		{
			const FString Key = FString::Printf(TEXT("%d|%d|%.9g|%.9g|%.9g|%d|%d|%.9g"), W, H, Pj.F, Pj.K1, Pj.K2, Dw, Dh, Space.Tan);
			if (Key != MapKey || Map.Num() != W * H)
			{
				BuildDepthIndexMap(Pj, Dw, Dh, Space.Tan, Map);
				MapKey = Key;
			}
		}
		auto DepthIndexOf = [&](int32 Q) -> int32 { return bDistorted ? Map[Q] : Q; };
		auto DepthRay = [&](int32 R) -> FVector
		{
			const double Tx = ((R % Dw) + 0.5 - Dw * 0.5) / Fd;
			const double Ty = ((R / Dw) + 0.5 - Dh * 0.5) / Fd;
			return Pj.Fwd + Pj.Right * Tx - Pj.Up * Ty;   // 전방 성분 1 → 평면 교점 t 가 곧 장면 깊이
		};

		int32 Passes = 0;
		TArray<float> Full;
		if (!Cam->CaptureTruthDepth(Space, nullptr, FIntRect(0, 0, Dw, Dh), Full))
		{
			OutError = TEXT("truth: 깊이 렌더 실패 — 실RHI 필요(-nullrhi 불가)");
			return false;
		}
		++Passes;

		// ---- 후보 차량: 보이는 차, 카메라와 가까운 순(마스크 겹침은 가까운 쪽이 가진다) ----
		struct FCand { ACarActor* Car; double Dist; };
		TArray<FCand> Cands;
		for (TActorIterator<ACarActor> It(World); It; ++It)
		{
			ACarActor* Car = *It;
			if (!Car || Car->IsHidden() || !Car->MeshComp || !Car->MeshComp->GetStaticMesh()) continue;
			Cands.Add({ Car, FVector::Dist(Car->MeshComp->Bounds.Origin, Pj.C) });
		}
		Cands.Sort([](const FCand& A, const FCand& B) { return A.Dist < B.Dist; });

		TArray<uint8> Owner;
		Owner.SetNumZeroed(W * H);
		TArray<TSharedPtr<FJsonValue>> CarsJson;
		int32 NextIndex = 1;

		for (const FCand& Cand : Cands)
		{
			if (NextIndex > 255) break;   // 마스크가 8비트다
			ACarActor* Car = Cand.Car;

			// 바운즈 8 꼭짓점을 깊이 공간에 투영해 읽을 영역을 줄인다. 하나라도 카메라 뒤면 전체.
			const FBox Box = Car->MeshComp->Bounds.GetBox().ExpandBy(Car->MeshComp->Bounds.BoxExtent.Size() * 0.05 + 20.0);   // 번호판·거울 여유
			bool bAnyBehind = false, bAnyFront = false;
			double U0 = DBL_MAX, V0 = DBL_MAX, U1 = -DBL_MAX, V1 = -DBL_MAX;
			for (int32 i = 0; i < 8; ++i)
			{
				const FVector P((i & 1) ? Box.Max.X : Box.Min.X, (i & 2) ? Box.Max.Y : Box.Min.Y, (i & 4) ? Box.Max.Z : Box.Min.Z);
				const FVector D = P - Pj.C;
				const double X = FVector::DotProduct(D, Pj.Fwd);
				if (X <= 1.0) { bAnyBehind = true; continue; }
				bAnyFront = true;
				const double U = Dw * 0.5 + Fd * FVector::DotProduct(D, Pj.Right) / X;
				const double V = Dh * 0.5 - Fd * FVector::DotProduct(D, Pj.Up) / X;
				U0 = FMath::Min(U0, U); V0 = FMath::Min(V0, V); U1 = FMath::Max(U1, U); V1 = FMath::Max(V1, V);
			}
			if (!bAnyFront) continue;
			FIntRect Rect(0, 0, Dw, Dh);
			if (!bAnyBehind)
			{
				Rect = FIntRect(FMath::Max(0, FMath::FloorToInt32(U0) - 2), FMath::Max(0, FMath::FloorToInt32(V0) - 2),
					FMath::Min(Dw, FMath::CeilToInt32(U1) + 2), FMath::Min(Dh, FMath::CeilToInt32(V1) + 2));
			}
			if (Rect.Width() <= 0 || Rect.Height() <= 0) continue;

			TArray<float> Solo;
			if (!Cam->CaptureTruthDepth(Space, Car, Rect, Solo)) continue;
			++Passes;
			const int32 Rw = Rect.Width();

			const int32 Index = NextIndex;
			FTruthPixBox Amodal, Visible;
			auto Visit = [&](int32 Q, int32 R)
			{
				const int32 Rx = R % Dw, Ry = R / Dw;
				if (Rx < Rect.Min.X || Rx >= Rect.Max.X || Ry < Rect.Min.Y || Ry >= Rect.Max.Y) return;
				const float S = Solo[(Ry - Rect.Min.Y) * Rw + (Rx - Rect.Min.X)];
				if (!TruthValidDepth(S)) return;
				const int32 Qx = Q % W, Qy = Q / W;
				Amodal.Add(Qx, Qy);
				if (FMath::Abs(Full[R] - S) <= TruthCarTolCm(S) && Owner[Q] == 0)
				{
					Owner[Q] = static_cast<uint8>(Index);
					Visible.Add(Qx, Qy);
				}
			};
			if (!bDistorted)
			{
				for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
					for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X) { Visit(Y * W + X, Y * W + X); }
			}
			else
			{
				for (int32 Q = 0; Q < W * H; ++Q) { const int32 R = Map[Q]; if (R >= 0) Visit(Q, R); }
			}
			if (Amodal.Count == 0) continue;   // 출력 그림 밖
			++NextIndex;

			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("index"), Index);
			O->SetStringField(TEXT("carNameId"), Car->CarData.id);
			O->SetNumberField(TEXT("prefabId"), Car->CarData.prefabId);
			O->SetStringField(TEXT("prefabName"), Car->CarData.prefabName);
			O->SetStringField(TEXT("plate"), Car->GetPlateNumber());
			O->SetStringField(TEXT("plateText"), Car->GetPlateDisplayText());
			O->SetStringField(TEXT("plateKind"), Car->GetPlateKind());
			O->SetNumberField(TEXT("distanceM"), TruthRound2(Cand.Dist / 100.0));
			O->SetField(TEXT("bboxAmodal"), Amodal.Json());
			O->SetField(TEXT("bboxVisible"), Visible.Json());
			O->SetNumberField(TEXT("amodalPixels"), static_cast<double>(Amodal.Count));
			O->SetNumberField(TEXT("visiblePixels"), static_cast<double>(Visible.Count));
			O->SetNumberField(TEXT("visibleFraction"), TruthRound4(static_cast<double>(Visible.Count) / Amodal.Count));
			O->SetBoolField(TEXT("truncated"), Amodal.X0 == 0 || Amodal.Y0 == 0 || Amodal.X1 == W - 1 || Amodal.Y1 == H - 1);

			// ---- 번호판: 판 평면과 광선의 교점 깊이를 장면 깊이와 비교한다(렌더 추가 없음). 카메라를 향한 판만. ----
			TArray<TSharedPtr<FJsonValue>> PlatesJson;
			TArray<FCarPlateWorld> Plates;
			Car->GetPlateWorldInfos(Plates);
			for (const FCarPlateWorld& Pl : Plates)
			{
				const FVector ToCam = Pj.C - Pl.FaceCenter;
				if (FVector::DotProduct(Pl.Normal, ToCam) <= 0.0) continue;
				const double HalfW = Pl.WidthCm * 0.5, HalfH = Pl.HeightCm * 0.5;

				// 꼭짓점(판 앞에서 본 왼쪽 위 → 오른쪽 위 → 오른쪽 아래 → 왼쪽 아래)과 5×3 표본으로 영역을 잡는다(왜곡이면 변이 휜다).
				TArray<TSharedPtr<FJsonValue>> Quad;
				bool bQuadOk = true;
				const FVector2D CornerSign[4] = { {-1, 1}, {1, 1}, {1, -1}, {-1, -1} };
				for (const FVector2D& Sg : CornerSign)
				{
					FVector2D Px;
					if (!Pj.Project(Pl.FaceCenter + Pl.Right * (Sg.X * HalfW) + Pl.Up * (Sg.Y * HalfH), Px)) { bQuadOk = false; break; }
					TArray<TSharedPtr<FJsonValue>> Pt;
					Pt.Add(MakeShared<FJsonValueNumber>(TruthRound2(Px.X)));
					Pt.Add(MakeShared<FJsonValueNumber>(TruthRound2(Px.Y)));
					Quad.Add(MakeShared<FJsonValueArray>(Pt));
				}
				if (!bQuadOk) continue;
				double A0 = DBL_MAX, B0 = DBL_MAX, A1 = -DBL_MAX, B1 = -DBL_MAX;
				for (int32 i = 0; i <= 4; ++i)
				{
					for (int32 j = 0; j <= 2; ++j)
					{
						FVector2D Px;
						if (!Pj.Project(Pl.FaceCenter + Pl.Right * ((i / 2.0 - 1.0) * HalfW) + Pl.Up * ((j - 1.0) * HalfH), Px)) continue;
						A0 = FMath::Min(A0, Px.X); B0 = FMath::Min(B0, Px.Y); A1 = FMath::Max(A1, Px.X); B1 = FMath::Max(B1, Px.Y);
					}
				}
				const int32 X0 = FMath::Max(0, FMath::FloorToInt32(A0) - 2), Y0 = FMath::Max(0, FMath::FloorToInt32(B0) - 2);
				const int32 X1 = FMath::Min(W, FMath::CeilToInt32(A1) + 2), Y1 = FMath::Min(H, FMath::CeilToInt32(B1) + 2);

				FTruthPixBox PlA, PlV;
				const double PlaneD = FVector::DotProduct(Pl.Normal, Pl.FaceCenter - Pj.C);
				for (int32 Y = Y0; Y < Y1; ++Y)
				{
					for (int32 X = X0; X < X1; ++X)
					{
						const int32 Q = Y * W + X;
						const int32 R = DepthIndexOf(Q);
						if (R < 0) continue;
						const FVector Dir = DepthRay(R);
						const double Den = FVector::DotProduct(Pl.Normal, Dir);
						if (FMath::Abs(Den) < 1e-9) continue;
						const double Tt = PlaneD / Den;
						if (Tt <= 0.0) continue;
						const FVector Hit = Pj.C + Dir * Tt - Pl.FaceCenter;
						if (FMath::Abs(FVector::DotProduct(Hit, Pl.Right)) > HalfW || FMath::Abs(FVector::DotProduct(Hit, Pl.Up)) > HalfH) continue;
						PlA.Add(X, Y);
						if (TruthValidDepth(Full[R]) && FMath::Abs(Full[R] - Tt) <= TruthPlateTolCm(Tt)) { PlV.Add(X, Y); }
					}
				}
				if (PlA.Count == 0) continue;

				TSharedPtr<FJsonObject> PO = MakeShared<FJsonObject>();
				PO->SetStringField(TEXT("side"), Pl.bFrontSide ? TEXT("front") : TEXT("rear"));
				PO->SetStringField(TEXT("plateText"), Car->GetPlateDisplayText());
				PO->SetArrayField(TEXT("quad"), Quad);
				PO->SetField(TEXT("bboxAmodal"), PlA.Json());
				PO->SetField(TEXT("bboxVisible"), PlV.Json());
				PO->SetNumberField(TEXT("amodalPixels"), static_cast<double>(PlA.Count));
				PO->SetNumberField(TEXT("visiblePixels"), static_cast<double>(PlV.Count));
				PO->SetNumberField(TEXT("visibleFraction"), TruthRound4(static_cast<double>(PlV.Count) / PlA.Count));
				PO->SetNumberField(TEXT("distanceM"), TruthRound2(ToCam.Size() / 100.0));
				// 판 법선과 카메라 방향 사이 각 — 0° = 정면. 판독 가능성 판단용.
				PO->SetNumberField(TEXT("viewAngleDeg"), TruthRound2(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Pl.Normal, ToCam.GetSafeNormal()), -1.0, 1.0)))));
				PO->SetBoolField(TEXT("truncated"), PlA.X0 == 0 || PlA.Y0 == 0 || PlA.X1 == W - 1 || PlA.Y1 == H - 1);
				PlatesJson.Add(MakeShared<FJsonValueObject>(PO));
			}
			O->SetArrayField(TEXT("plates"), PlatesJson);
			CarsJson.Add(MakeShared<FJsonValueObject>(O));
		}

		// ---- 자세(실제로 그린 값) ----
		TSharedPtr<FJsonObject> Pose = RpcDto::CamToDto(Cam, CamId, 100.f);
		Pose->RemoveField(TEXT("name"));
		Pose->SetNumberField(TEXT("hfov"), TruthRound4(HFov));
		Pose->SetNumberField(TEXT("vfov"), TruthRound4(2.0 * FMath::RadiansToDegrees(FMath::Atan(OutTan * H / W))));
		Pose->SetNumberField(TEXT("k1"), Pj.K1);
		Pose->SetNumberField(TEXT("k2"), Pj.K2);
		TSharedPtr<FJsonObject> K = MakeShared<FJsonObject>();
		K->SetNumberField(TEXT("fx"), TruthRound4(Pj.F));
		K->SetNumberField(TEXT("fy"), TruthRound4(Pj.F));
		K->SetNumberField(TEXT("cx"), W * 0.5);
		K->SetNumberField(TEXT("cy"), H * 0.5);
		Pose->SetObjectField(TEXT("intrinsics"), K);
		Pose->SetObjectField(TEXT("forward"), RpcDto::Vec3(Pj.Fwd.X, Pj.Fwd.Y, Pj.Fwd.Z));

		TSharedPtr<FJsonObject> Img = MakeShared<FJsonObject>();
		Img->SetNumberField(TEXT("width"), W);
		Img->SetNumberField(TEXT("height"), H);
		Img->SetStringField(TEXT("origin"), TEXT("top-left, x right, y down, pixel-edge coords (pixel i spans [i, i+1))"));

		OutTruth = MakeShared<FJsonObject>();
		OutTruth->SetObjectField(TEXT("image"), Img);
		OutTruth->SetObjectField(TEXT("pose"), Pose);
		OutTruth->SetArrayField(TEXT("cars"), CarsJson);
		OutTruth->SetNumberField(TEXT("carCount"), CarsJson.Num());
		OutTruth->SetNumberField(TEXT("depthPasses"), Passes);
		OutTruth->SetNumberField(TEXT("truthMs"), FMath::RoundToDouble((FPlatformTime::Seconds() - T0) * 10000.0) / 10.0);

		if (bWithMask) { OutMask = MoveTemp(Owner); }
		else { OutMask.Reset(); }
		return true;
	}
}
