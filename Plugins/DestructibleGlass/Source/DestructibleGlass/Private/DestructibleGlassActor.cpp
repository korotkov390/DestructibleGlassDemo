// Fill out your copyright notice in the Description page of Project Settings.


#include "DestructibleGlassActor.h"
#include "Components/BoxComponent.h"
#include "GeometryScript/CollisionFunctions.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "Engine/World.h"
#include "TimerManager.h"




ADestructibleGlassActor::ADestructibleGlassActor()
{
	PrimaryActorTick.bCanEverTick = false;

	//bNetLoadOnClient = false;
	//bReplicates = true;
	
	// Glass dynamic mesh
	GlassMeshComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("GlassMesh"));
	RootComponent = GlassMeshComponent;
	GlassMeshComponent->SetCollisionEnabled(ECollisionEnabled::Type::NoCollision);
	GlassMeshComponent->SetCollisionObjectType(ECC_WorldStatic);
	GlassMeshComponent->SetComplexAsSimpleCollisionEnabled(false);
	GlassMeshComponent->SetIsReplicated(true);

	// Simple Collision
	CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionBox"));
	CollisionBox->SetupAttachment(RootComponent);
	CollisionBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionBox->SetCollisionObjectType(ECC_WorldStatic);
	CollisionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	CollisionBox->SetIsReplicated(true);

	// Niagara
	//NSComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("NiagaraComponent"));
	//NSComponent->SetupAttachment(RootComponent);
	//NSComponent->SetAutoActivate(false);
	//NSComponent->Deactivate();
}

void ADestructibleGlassActor::BeginPlay()
{
	Super::BeginPlay();
	GenerateInitialGlassMesh();
}

void ADestructibleGlassActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	GenerateInitialGlassMesh();
}

// Initial Mesh Generate
void ADestructibleGlassActor::GenerateInitialGlassMesh()
{
	if (!GlassMeshComponent) return;
	GlassMeshComponent->GetDynamicMesh()->Reset();
	GlassMeshComponent->SetMaterial(0, Material);

	//if (NSSystem)
	//{
	//	NSComponent->SetAsset(NSSystem);
	//	NSComponent->Deactivate();
	//}

	TArray<UActorComponent*> OldShardsList = K2_GetComponentsByClass(UDynamicMeshComponent::StaticClass());
	for (UActorComponent* Component : OldShardsList)
	{
		if (UDynamicMeshComponent* OldShard = Cast<UDynamicMeshComponent>(Component))
		{
			if (OldShard != GlassMeshComponent)
			{
				OldShard->DestroyComponent();
			}
		}
	}
	
	GlassState.Shards.Empty();
	FGlassShard WholeShard;
	WholeShard.Polygons = {{0, 0}, {Width, 0}, {Width, Height}, {0, Height}};
	WholeShard.Pivot = FVector::ZeroVector;
	WholeShard.Area = CalculateArea(WholeShard.Polygons);
	FBox2D Box(EForceInit::ForceInit);
	for (const FVector2D& V : WholeShard.Polygons)
	{
		Box += V;
	}
	WholeShard.Bound2D = Box;
	GlassState.Shards.Add(WholeShard);

	// Collision Box
	
	CollisionBox->SetBoxExtent(FVector(Thickness * 0.5f, Box.GetSize().X * 0.5f, Box.GetSize().Y * 0.5f));
	CollisionBox->SetRelativeLocation(FVector(Thickness * -0.5f, Width * -0.5f, Height * 0.5f));
	
	DrawGlass(FVector::ZeroVector, FVector::ZeroVector);
}

void ADestructibleGlassActor::Server_NotifyHit_Implementation(const FVector& HitLocation, const FVector& HitDirection)
{
	UE_LOG(LogTemp, Warning, TEXT("SERVER_HIT"))
	Multicast_PlayGlassHit(HitLocation, HitDirection);
}

void ADestructibleGlassActor::Multicast_PlayGlassHit_Implementation(const FVector& HitLocation, const FVector& HitDirection)
{
	UE_LOG(LogTemp, Warning, TEXT("SEND_TO_CLIENTS"))
	ApplyHit(HitLocation, HitDirection);
}



// Apply Hit Func
void ADestructibleGlassActor::ApplyHit(const FVector& WorldHitPoint, const FVector& WorldDirection)
{
	UE_LOG(LogTemp, Warning, TEXT("GENERAL_SHARD_COUNT: %d"), GlassState.Shards.Num())
	int32 SizeLevel = 0;
	int32 TargetShard = FindShard(WorldHitPoint, SizeLevel);
	UE_LOG(LogTemp, Warning, TEXT("TARGET_SHARD: %d"), TargetShard)
	if (TargetShard >= 0)
	{
		FGlassShard& Shard = GlassState.Shards[TargetShard];
	
		FTransform ActorTransform = GetActorTransform();
		FTransform ShardLocalTransform;
		ShardLocalTransform.SetTranslation(Shard.Pivot);
		ShardLocalTransform.SetRotation(GlassRotation);
		ShardLocalTransform.SetScale3D(FVector(1,1,1));
		FTransform ShardWorldTransform = ShardLocalTransform * ActorTransform;
		FVector HitLocal = ShardWorldTransform.InverseTransformPosition(WorldHitPoint);
	
		FVector2D Hit2D(HitLocal.X, HitLocal.Y);
		TArray<FGlassSegment2D> CrackSegments;
		TArray<FGlassSegment2D> BoundarySegments;
	
		TArray<FVector2D> OutVertices = GenerateCracks(SizeLevel, TargetShard, Hit2D, ShardWorldTransform, CrackSegments, BoundarySegments);
		
		UE_LOG(LogTemp, Warning, TEXT("FINAL_OUT_CrackSegments: %d"), OutVertices.Num());
	
		TArray<FTargetEdge> Edges;
		BuildGraph(CrackSegments, BoundarySegments, OutVertices, Edges);
	
		TMap<int32, TArray<int32>> Adjacency;
		BuildAdjacency(Edges, Adjacency);
	
		TArray<FGlassShard> OutShards;
		ExtractPolygonsToGlassState(Hit2D, Edges, OutVertices, Adjacency, OutShards);
		GlassState.Shards.RemoveAt(TargetShard);
		UE_LOG(LogTemp, Warning, TEXT("NEW_SHARDS_NUMBER: %d"), OutShards.Num());
		GlassState.Shards.Append(OutShards);
		FindNeighbours();
		
		DrawGlass(WorldDirection, WorldHitPoint);

		// Phys timer
		if (GetWorld()->GetTimerManager().IsTimerActive(TimerHandle_DisablePhys))
		{
			GetWorld()->GetTimerManager().ClearTimer(TimerHandle_DisablePhys);
		}
		GetWorld()->GetTimerManager().SetTimer(TimerHandle_DisablePhys, this, &ADestructibleGlassActor::DisableShardPhysics, 3.0f, false);
	}
}

void ADestructibleGlassActor::DisableShardPhysics()
{
	TArray<UDynamicMeshComponent*> ShardComponents;
	GetComponents<UDynamicMeshComponent>(ShardComponents);
	for (UDynamicMeshComponent* Shard : ShardComponents)
	{
		if (!Shard) continue;

		Shard->SetSimulatePhysics(false);
		Shard->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Shard->SetEnableGravity(false);
		Shard->SetComponentTickEnabled(false);
	}
	GetWorldTimerManager().ClearTimer(TimerHandle_DisablePhys);
	UE_LOG(LogTemp, Warning, TEXT("Phys_CLEAR"));
}

// Find Target Shard
int32 ADestructibleGlassActor::FindShard(const FVector& HitWorldPos, int32& SizeLevel)
{
	SizeLevel = 0;
	const FTransform ActorTransform = GetActorTransform();
	for (int32 i = 0; i < GlassState.Shards.Num(); ++i)
	{
		const FGlassShard& Shard = GlassState.Shards[i];
		FTransform ShardLocalTransform;
		ShardLocalTransform.SetTranslation(Shard.Pivot);
		ShardLocalTransform.SetRotation(GlassRotation);
		ShardLocalTransform.SetScale3D(FVector::OneVector);
		const FTransform ShardWorldTransform = ShardLocalTransform * ActorTransform;
		const FVector HitLocal = ShardWorldTransform.InverseTransformPosition(HitWorldPos);
		const FVector2D Hit2D(HitLocal.X, HitLocal.Y);

		if (!Shard.Bound2D.IsInside(Hit2D)) continue;
		if (Shard.Polygons.Num() > 0)
		{
			if (IsPointInPolygon(Hit2D, Shard.Polygons))
			{
				if (Shard.Area < 2000) SizeLevel = 1;
				return i;
			}
		}
	}
	return INDEX_NONE;
}

// Check point in shard
bool ADestructibleGlassActor::IsPointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon)
{
	bool bInside = false;
	int32 Count = Polygon.Num();
	for (int32 i = 0, j = Count - 1; i < Count; j = i++)
	{
		const FVector2D& Pi = Polygon[i];
		const FVector2D& Pj = Polygon[j];

		const bool bIntersect = ((Pi.Y > Point.Y) != (Pj.Y > Point.Y)) && (Point.X < (Pj.X - Pi.X) * (Point.Y - Pi.Y) / (Pj.Y - Pi.Y + SMALL_NUMBER) + Pi.X);

		if (bIntersect) bInside = !bInside;
	}
	return bInside;
}


// Crack Generator
TArray<FVector2D> ADestructibleGlassActor::GenerateCracks(const int32 SizeLevel, int32 ShardIndex, const FVector2D& Hit2D, const FTransform& ShardWorldTransform, TArray<FGlassSegment2D>& CrackSegments, TArray<FGlassSegment2D>& BoundarySegments)
{
	TArray<FVector2D> OutCracksVerts;
	const FGlassShard& Shard = GlassState.Shards[ShardIndex];
	TArray<FVector2D> BoundaryVerts = Shard.Polygons;
	
	const int32 RayCount = FMath::RandRange(RayCountMin, RayCountMax);
	const float AngleStep = 2.f * PI / RayCount;
	TArray<TArray<FGlassSegment2D>> RaysSegments;

	for (int32 Ray = 0; Ray < RayCount; Ray++)
	{
		TArray<FGlassSegment2D> CrackSeparateSegments;
		float Angle = Ray * AngleStep + FMath::RandRange(-AngleJitter, AngleJitter);
		TArray<FVector2D> Crack;
		Crack.Add(Hit2D);
		FVector2D Prev = Hit2D;

		for (int32 Step = 0; Step < StepsPerRay; Step++)
		{
			Angle += FMath::RandRange(-AngleDrift, AngleDrift);
			const float Len = StepLen + FMath::RandRange(-StepJitter, StepJitter);

			FVector2D Dir(FMath::Cos(Angle), FMath::Sin(Angle));
			FVector2D Curr = Prev + Dir * Len;

			bool bHitBoundary = false;
			FVector2D HitPoint;
			int32 HitEdge = -1;
			
			for (int32 i = 0; i < BoundaryVerts.Num(); i++)
			{
				const FVector2D& A = BoundaryVerts[i];
				const FVector2D& B = BoundaryVerts[(i + 1) % BoundaryVerts.Num()];

				if (SegmentIntersect(Prev, Curr, A, B, HitPoint))
				{
					bHitBoundary = true;
					HitEdge = i;
					break;
				}
			}
			if (bHitBoundary)
			{
				Crack.Add(HitPoint);
				const int32 InsertIndex = HitEdge + 1;
				BoundaryVerts.Insert(HitPoint, InsertIndex);
				break;
			}
			else
			{
				Crack.Add(Curr);
				Prev = Curr;
			}
		}
		for (int32 i = 0; i < Crack.Num()-1; ++i)
		{
			CrackSeparateSegments.Add(FGlassSegment2D(Crack[i], Crack[i+1]));
		}
		if (Crack.Num() > 1)
		{
			OutCracksVerts.Append(Crack);
			CrackSegments.Append(CrackSeparateSegments);
			RaysSegments.Add(CrackSeparateSegments);

			// Cracks Debug
			if (bDebug)
			{
				for (int32 i = 0; i < Crack.Num() - 1; ++i)
				{
					FVector Start = FVector(Crack[i].X, Crack[i].Y, 25) + GetActorLocation();
					FVector End = FVector(Crack[i+1].X, Crack[i+1].Y, 25) + GetActorLocation();

					DrawDebugLine(
						GetWorld(),
						Start,
						End,
						FColor::Green,
						false,
						25.0f,
						0,
						1.f
					);
					DrawDebugSphere(
						GetWorld(),
						End,
						5.0f,
						5,
						FColor::Green,
						false,
						25.0f,
						0,
						0.5f
						);
				}
			}
		}
	}
	
	for (int32 i = 0; i < BoundaryVerts.Num(); ++i)
	{
		FVector2D Start2D = BoundaryVerts[i];
		FVector2D End2D = BoundaryVerts[(i+1)%BoundaryVerts.Num()];
		BoundarySegments.Add(FGlassSegment2D(Start2D, End2D));

		// Boundary Debug
		if (bDebug)
		{
			FVector Start = FVector(BoundaryVerts[i].X, BoundaryVerts[i].Y, 25) + GetActorLocation();
			FVector End = FVector(BoundaryVerts[(i+1)%BoundaryVerts.Num()].X, BoundaryVerts[(i+1)%BoundaryVerts.Num()].Y, 25) + GetActorLocation();
			DrawDebugLine(
				GetWorld(),
				Start,
				End,
				FColor::Blue,
				false,
				25.0f,
				0,
				1.f
				);
			DrawDebugSphere(
				GetWorld(),
				Start,
				10.0f,
				5,
				FColor::Blue,
				false,
				25.0f,
				0,
				0.5f
				);
		}
	}
	
	// Arc Segments/////////////////////////////////////////
	if (SizeLevel == 0)
	{
		TArray<FGlassSegment2D> CrackArk;
		for (int32 k = 0; k < RaysSegments.Num(); ++k)
		{
			const TArray<FGlassSegment2D>& RayA = RaysSegments[k];
			const TArray<FGlassSegment2D>& RayB = RaysSegments[(k + 1) % RaysSegments.Num()];
			int32 MaxRange = FMath::Min(RaysSegments[k].Num() - 2, RaysSegments[(k + 1) % RaysSegments.Num()].Num() - 2);
		
			if (MaxRange > 1)
			{
				int32 RandSegment = FMath::RandRange(1, MaxRange);
				AddArcSegments(Shard, RandSegment, RayA, RayB, Hit2D, OutCracksVerts, CrackArk);
			}
			// Hole
			AddArcSegments(Shard, 0, RayA, RayB, Hit2D, OutCracksVerts, CrackArk);
		}
		CrackSegments.Append(CrackArk);
		if (bDebug)
		{
			// Arks Debug
			for (int32 i = 0; i < CrackArk.Num(); ++i)
			{
				FVector Start = FVector(CrackArk[i].A.X, CrackArk[i].A.Y, 25) + GetActorLocation();
				FVector End = FVector(CrackArk[i].B.X, CrackArk[i].B.Y, 25) + GetActorLocation();

				DrawDebugLine(
					GetWorld(),
					Start,
					End,
					FColor::Red,
					false,
					25.0f,
					0,
					1.f
				);
			}
		}
	}
	// Arc Segments/////////////////////////////////////////
	return OutCracksVerts;
}

void ADestructibleGlassActor::AddArcSegments(const FGlassShard& Shard, const int32 Segment, const TArray<FGlassSegment2D>& RayA, const TArray<FGlassSegment2D>& RayB, const FVector2D& Hit2D, TArray<FVector2D>& OutCracksVerts, TArray<FGlassSegment2D>& CrackArk)
{
	float Dist = (RayA[Segment].B - RayB[Segment].B).Size();

	int32 NumSteps = FMath::Floor(Dist/ArcSegmentLength);
	FVector2D FirstArcVert = RayA[Segment].B;
	OutCracksVerts.Add(FirstArcVert);

	if (NumSteps > 0)
	{
		FVector2D Dir0 = (FirstArcVert - Hit2D).GetSafeNormal();
		FVector2D Dir1 = (RayB[Segment].B - Hit2D).GetSafeNormal();
		float ArcAngleBetween = FMath::Acos(FVector2D::DotProduct(Dir0, Dir1));
		float Radius = FVector2D::Distance(Hit2D, FirstArcVert);
		FVector2D RadiusVector = FirstArcVert - Hit2D;
		float ArcAngleStep = ArcAngleBetween/NumSteps;
				
		for (int32 D = 0; D < NumSteps - 1; ++D)
		{
			float CosA = FMath::Cos(ArcAngleStep * (D + 1));
			float SinA = FMath::Sin(ArcAngleStep * (D + 1));
			FVector2D Rotated;
			Rotated.X = RadiusVector.X * CosA - RadiusVector.Y * SinA;
			Rotated.Y = RadiusVector.X * SinA + RadiusVector.Y * CosA;
			float RadialOffset = FMath::FRandRange(ArcRadialOffset * -1, ArcRadialOffset);
			Rotated = Hit2D + Rotated.GetSafeNormal() * (Radius + RadialOffset);

			if (IsPointInPolygon(Rotated, Shard.Polygons))
			{
				FVector2D LastArcVert = Rotated;
				OutCracksVerts.Add(LastArcVert);
				CrackArk.Add(FGlassSegment2D(FirstArcVert, LastArcVert));
				FirstArcVert = LastArcVert;
			}
		}
	}
	FVector2D LastArcVert = RayB[Segment].B;
	OutCracksVerts.Add(LastArcVert);
	CrackArk.Add(FGlassSegment2D(FirstArcVert, LastArcVert));
}

float ADestructibleGlassActor::CalculateArea(const TArray<FVector2D>& Poly)
{
	float Sum = 0.f;
	for (int32 i = 0; i < Poly.Num(); ++i)
	{
		const FVector2D& Current = Poly[i];
		const FVector2D& Next = Poly[(i + 1) % Poly.Num()];
		Sum += (Current.X * Next.Y) - (Next.X * Current.Y);
	}
	Sum = FMath::Abs(Sum) * 0.5f;
	return Sum;
}


// Find Intersection
bool ADestructibleGlassActor::SegmentIntersect(const FVector2D& ALine, const FVector2D& BLine, const FVector2D& ABound, const FVector2D& BBound, FVector2D& NewPoint)
{
	NewPoint = FVector2d::ZeroVector;
	const FVector2D R = BLine - ALine;
	const FVector2D S = BBound - ABound;

	const float Den = FVector2D::CrossProduct(R, S);
	if (FMath::Abs(Den) < KINDA_SMALL_NUMBER) return false;

	const FVector2D AC = ABound - ALine;
	const float T = FVector2D::CrossProduct(AC, S) / Den;
	const float U = FVector2D::CrossProduct(AC, R) / Den;

	if (T >= 0.f && T <= 1.f && U >= 0.f && U <= 1.f)
	{
		NewPoint = ALine + T * R;
		return true;
	}
	return false;
}

// Create Polygons //////////////////////////////////////////////////////////////////////////////////////////
int32 ADestructibleGlassActor::FindOrAddVertex(TArray<FVector2D>& Vertices, const FVector2D& P, float Eps = 0.01f)
{
	const float EpsSqr = Eps * Eps;
	for (int32 i = 0; i < Vertices.Num(); ++i)
	{
		if (FVector2D::DistSquared(Vertices[i], P) <= EpsSqr)
		{
			return i;
		}
	}
	return Vertices.Add(P);
}
void ADestructibleGlassActor::BuildGraph(const TArray<FGlassSegment2D>& Cracks, const TArray<FGlassSegment2D>& Boundaryes, TArray<FVector2D>& OutVertices, TArray<FTargetEdge>& OutEdges)
{
	for (const FGlassSegment2D& S : Cracks)
	{
		const int32 A = FindOrAddVertex(OutVertices, S.A);
		const int32 B = FindOrAddVertex(OutVertices, S.B);

		// Two directed edges
		OutEdges.Add({ A, B });
		OutEdges.Add({ B, A });
	}
	for (const FGlassSegment2D& Bo : Boundaryes)
	{
		const int32 A = FindOrAddVertex(OutVertices, Bo.A);
		const int32 B = FindOrAddVertex(OutVertices, Bo.B);

		// One directed edges
		OutEdges.Add({ A, B });
	}
}
void ADestructibleGlassActor::BuildAdjacency(const TArray<FTargetEdge>& Edges, TMap<int32, TArray<int32>>& OutAdjacency)
{
	OutAdjacency.Empty();

	for (int32 EdgeIdx = 0; EdgeIdx < Edges.Num(); ++EdgeIdx)
	{
		const int32 From = Edges[EdgeIdx].From;
		OutAdjacency.FindOrAdd(From).Add(EdgeIdx);
	}
}
int32 ADestructibleGlassActor::FindNextEdgeCCW(int32 CurrentEdge, const TArray<FTargetEdge>& Edges, const TArray<FVector2D>& Vertices, const TMap<int32, TArray<int32>>& Adjacency)
{
	const FTargetEdge& Curr = Edges[CurrentEdge];
	const FVector2D P0 = Vertices[Curr.From];
	const FVector2D P1 = Vertices[Curr.To];
	const FVector2D InDir = (P1 - P0).GetSafeNormal();

	float BestAngle = -BIG_NUMBER;
	int32 BestEdge = INDEX_NONE;

	const TArray<int32>* Candidates = Adjacency.Find(Curr.To);
	if (!Candidates)
	{
		return INDEX_NONE;
	}

	for (int32 EdgeIdx : *Candidates)
	{
		const FTargetEdge& Next = Edges[EdgeIdx];
		if (Next.To == Curr.From)
		{
			continue;
		}

		const FVector2D OutDir = (Vertices[Next.To] - P1).GetSafeNormal();
		float Angle = FMath::Atan2(FVector2D::CrossProduct(InDir, OutDir),FVector2D::DotProduct(InDir, OutDir));
		if (Angle > BestAngle)
		{
			BestAngle = Angle;
			BestEdge = EdgeIdx;
		}
	}
	return BestEdge;
}
void ADestructibleGlassActor::ExtractPolygons(const TArray<FTargetEdge>& Edges, const TArray<FVector2D>& Vertices, const TMap<int32, TArray<int32>>& Adjacency, TArray<TArray<FVector2D>>& OutPolygons)
{
	TSet<int32> UsedEdges;
	for (int32 StartEdge = 0; StartEdge < Edges.Num(); ++StartEdge)
	{
		if (UsedEdges.Contains(StartEdge))
		{
			continue;
		}
		TArray<FVector2D> Polygon;
		int32 CurrentEdge = StartEdge;
		while (true)
		{
			if (UsedEdges.Contains(CurrentEdge))
			{
				break;
			}

			UsedEdges.Add(CurrentEdge);
			Polygon.Add(Vertices[Edges[CurrentEdge].From]);

			const int32 NextEdge = FindNextEdgeCCW(CurrentEdge, Edges, Vertices, Adjacency);
			if (NextEdge == INDEX_NONE || NextEdge == StartEdge)
			{
				break;
			}
			CurrentEdge = NextEdge;
		}
		if (Polygon.Num() >= 3)
		{
			OutPolygons.Add(Polygon);
		}
	}
}
void ADestructibleGlassActor::ExtractPolygonsToGlassState(const FVector2D& Hit, const TArray<FTargetEdge>& Edges, const TArray<FVector2D>& Vertices, const TMap<int32, TArray<int32>>& Adjacency, TArray<FGlassShard>& InOutGlassShards)
{
	TArray<TArray<FVector2D>> Polygons;
	ExtractPolygons(Edges, Vertices, Adjacency, Polygons);
	
	for (int32 i = 0; i < Polygons.Num(); ++i)
	{
		const TArray<FVector2D>& Poly = Polygons[i];
		if (Poly.Num() < 3)
		{
			continue;
		}
		
		FVector2D Pivot2D = FVector2D::ZeroVector;
		for (const FVector2D& V : Poly)
		{
			Pivot2D += V;
		}
		Pivot2D /= Poly.Num();
		float Distance = FVector2D::Distance(Pivot2D, Hit);
		
		FGlassShard NewShard;
		NewShard.Polygons = Poly;
		NewShard.Pivot = FVector::ZeroVector;
		NewShard.LocalTransform = FTransform(FVector::ZeroVector);
		NewShard.Area = CalculateArea(Poly);
		NewShard.bDetached = (NewShard.Area <= MinDetachedArea) || (Distance <= DestroyRadius);
		FBox2D Box(EForceInit::ForceInit);
		for (const FVector2D& V : Poly)
		{
			Box += V;
		}
		NewShard.Bound2D = Box;

		InOutGlassShards.Add(NewShard);
	}
}
void ADestructibleGlassActor::FindNeighbours()
{
	for (int32 i = 0; i < GlassState.Shards.Num(); ++i)
	{
		if (GlassState.Shards[i].bDetached) continue;
		const TArray<FVector2D>& V1 = GlassState.Shards[i].Polygons;
		int32 NeighbourCount = 0;
		
		for (int32 j = 0; j < GlassState.Shards.Num(); ++j)
		{
			if (NeighbourCount >= 2) break;
			
			if (i == j || GlassState.Shards[j].bDetached) continue;
			const TArray<FVector2D>& V2 = GlassState.Shards[j].Polygons;
			int32 VertsCount = 0;
			for (const FVector2D& V : V1)
			{
				if (VertsCount >= 2)
				{
					NeighbourCount++;
					break;
				}
				for (const FVector2D& P : V2)
				{
					if (V.Equals(P, 0.01))
					{
						VertsCount++;
						break;
					}
				}
			}
		}
		if (NeighbourCount < 2)
		{
			GlassState.Shards[i].bDetached = true;
		}
	}
}
// Create Polygons //////////////////////////////////////////////////////////////////////////////////////////

// Draw Polygons
void ADestructibleGlassActor::DrawGlass(const FVector& ShootDirection, const FVector& HitWorld)
{
	if (!GlassMeshComponent) return;
	UDynamicMesh* Mesh = GlassMeshComponent->GetDynamicMesh();
	Mesh->Reset();

	TArray<int32> IndexForRemove;
	//TArray<FVector> NSPos;
	int32 NSCount = 0;
	for (int32 i = 0; i < GlassState.Shards.Num(); ++i)
	{
		const FGlassShard& Shard = GlassState.Shards[i];
		if (!Shard.bDetached)
		{
			if (Shard.Polygons.Num() > 0)	
			{
				const TArray<FVector2D>& PolygonVerts2D = Shard.Polygons;
				FGeometryScriptPrimitiveOptions CreateOpts;
				CreateOpts.bFlipOrientation = false;

				FTransform ShardTransform;
				ShardTransform.SetTranslation(Shard.Pivot);
				ShardTransform.SetRotation(GlassRotation);
				ShardTransform.SetScale3D(FVector(1,1,1));

				UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSimpleExtrudePolygon(
				Mesh,
				CreateOpts,
				ShardTransform,
				PolygonVerts2D,
				Thickness,
				1,
				true,
				EGeometryScriptPrimitiveOriginMode::Base
				);
			}
		}
		else
		{
			if (Shard.Polygons.Num() > 0)
			{
				//if (Shard.Area > 500)
				//{
					UDynamicMeshComponent* ShardMeshComp = NewObject<UDynamicMeshComponent>(this);
					ShardMeshComp->RegisterComponent();
					ShardMeshComp->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
					ShardMeshComp->SetWorldLocation(GetActorLocation(), true, nullptr, ETeleportType::None);
					UDynamicMesh* ShardMesh = ShardMeshComp->GetDynamicMesh();
					ShardMeshComp->SetMaterial(0, Material);
				
					const TArray<FVector2D>& PolygonVerts2D = Shard.Polygons;
					FGeometryScriptPrimitiveOptions CreateOpts;
					CreateOpts.bFlipOrientation = false;

					FTransform ShardTransform;
					ShardTransform.SetTranslation(Shard.Pivot);
					ShardTransform.SetRotation(GlassRotation);
					ShardTransform.SetScale3D(FVector(1,1,1));

					UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSimpleExtrudePolygon(
					ShardMesh,
					CreateOpts,
					ShardTransform,
					PolygonVerts2D,
					Thickness,
					1,
					true,
					EGeometryScriptPrimitiveOriginMode::Base
					);

					ShardMeshComp->NotifyMeshUpdated();
					ShardMeshComp->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
					ShardMeshComp->SetCollisionObjectType(ECC_PhysicsBody);
					ShardMeshComp->SetCollisionResponseToAllChannels(ECR_Block);
				
					ShardMeshComp->SetComplexAsSimpleCollisionEnabled(false, true);

					ShardMeshComp->UpdateCollision(false);
					ShardMeshComp->RecreatePhysicsState();
				
					FGeometryScriptCollisionFromMeshOptions CollisionOptions;
					CollisionOptions.MaxConvexHullsPerMesh = 1;
					CollisionOptions.MaxShapeCount = 1;
					CollisionOptions.Method = EGeometryScriptCollisionGenerationMethod::AlignedBoxes;
				
					UGeometryScriptLibrary_CollisionFunctions::SetDynamicMeshCollisionFromMesh(ShardMeshComp->GetDynamicMesh(),ShardMeshComp,CollisionOptions);
			
					ShardMeshComp->SetSimulatePhysics(true);
					ShardMeshComp->SetEnableGravity(true);

					ShardMeshComp->BodyInstance.MassScale = 0.2f;
					ShardMeshComp->SetLinearDamping(0.1f);
					ShardMeshComp->SetAngularDamping(0.2f);

					if (Shard.Area < 1000)
					{
						FVector Direction = ShootDirection + FMath::FRandRange(-0.5f, 0.5f);
						ShardMeshComp->AddImpulse(Direction * 300.f, NAME_None, true);
					}
				//}
				//else NSCount++;
			}
			IndexForRemove.Add(i);
		}
	}
	//if (NSCount > 0 && NSSystem->IsValid())
	//{
		//UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(NSComponent, TEXT("User.ShardPositions"),NSPos);
	//	NSComponent->SetIntParameter(TEXT("User.ShardCount"), NSCount);
	//	NSComponent->SetVariablePosition(TEXT("User.HitPosition"), HitWorld);
	//	NSComponent->SetVectorParameter(TEXT("User.Direction"), ShootDirection);
	//	NSComponent->Activate();
	//}
	
	for (int32 i = IndexForRemove.Num() - 1; i >= 0; i--)
	{
		GlassState.Shards.RemoveAt(IndexForRemove[i]);
	}
}