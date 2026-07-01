// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FDestructibleGlassState.h"
#include "Components/BoxComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "DestructibleGlassActor.generated.h"


UCLASS()
class DESTRUCTIBLEGLASS_API ADestructibleGlassActor : public AActor
{
	GENERATED_BODY()
public:

ADestructibleGlassActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="DestructibleGlass")
	UDynamicMeshComponent* GlassMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="DestructibleGlass")
	UBoxComponent* CollisionBox;

	//UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	//UNiagaraComponent* NSComponent;
	
	UPROPERTY()
	FDestructibleGlassState GlassState;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DestructibleGlass")
	bool bDebug = false;
	
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Glass")
	//UNiagaraSystem* NSSystem;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Glass")
	float Width = 200.f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Glass")
	float Height = 300.f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Glass")
	float Thickness = 2.f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Glass")
	UMaterialInterface* Material;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GlassCracks")
	float MinDetachedArea = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GlassCracks")
	float DestroyRadius = 50.f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GlassCracks")
	int32 RayCountMin = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GlassCracks")
	int32 RayCountMax = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GlassCracks")
	int32 StepsPerRay = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GlassCracks")
	float AngleJitter = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GlassCracks")
	float AngleDrift = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GlassCracks")
	float StepLen = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GlassCracks")
	float StepJitter = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GlassCracks")
	float ArcSegmentLength = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GlassCracks")
	float ArcRadialOffset = 1.f;

	UFUNCTION(BlueprintCallable, Category="DestructibleGlass")
	void ApplyHit(const FVector& WorldHitPoint, const FVector& WorldDirection);
	
	// Server
	UFUNCTION(BlueprintCallable, Server, Reliable, Category="DestructibleGlass")
	void Server_NotifyHit(const FVector& HitLocation, const FVector& HitDirection);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayGlassHit(const FVector& HitLocation, const FVector& HitDirection);

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	void GenerateInitialGlassMesh();

	struct FTargetEdge
	{
		int32 From = INDEX_NONE;
		int32 To = INDEX_NONE;
	};

	FQuat GlassRotation =  FQuat(FVector(1,0,0), PI*0.5f) * FQuat(FVector(0,1,0), -PI*0.5f);
	FTimerHandle TimerHandle_DisablePhys;
	void DisableShardPhysics();

	TArray<FVector2D> GenerateCracks(const int32 SizeLevel, int32 ShardIndex, const FVector2D& Hit2D, const FTransform& ShardWorldTransform, TArray<FGlassSegment2D>& CrackSegments, TArray<FGlassSegment2D>& BoundarySegments);
	void DrawGlass(const FVector& ShootDirection, const FVector& HitWorld);
	int32 FindShard(const FVector& HitWorldPos, int32& SizeLevel);
	static bool SegmentIntersect(const FVector2D& ALine, const FVector2D& BLine,const FVector2D& ABound, const FVector2D& BBound, FVector2D& NewPoint);
	void AddArcSegments(const FGlassShard& Shard, const int32 Segment, const TArray<FGlassSegment2D>& RayA, const TArray<FGlassSegment2D>& RayB, const FVector2D& Hit2D, TArray<FVector2D>& OutCracksVerts, TArray<FGlassSegment2D>& CrackArk);
	static float CalculateArea(const TArray<FVector2D>& Poly);
	void FindNeighbours();
	static bool IsPointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon);
	
	// Generate new polygons
	static int32 FindOrAddVertex(TArray<FVector2D>& Vertices, const FVector2D& P, float Eps);
	static void BuildGraph(const TArray<FGlassSegment2D>& Cracks, const TArray<FGlassSegment2D>& Boundaryes, TArray<FVector2D>& OutVertices, TArray<FTargetEdge>& OutEdges);
	static void BuildAdjacency(const TArray<FTargetEdge>& Edges, TMap<int32, TArray<int32>>& OutAdjacency);
	static int32 FindNextEdgeCCW(int32 CurrentEdge, const TArray<FTargetEdge>& Edges, const TArray<FVector2D>& Vertices, const TMap<int32, TArray<int32>>& Adjacency);
	static void ExtractPolygons(const TArray<FTargetEdge>& Edges, const TArray<FVector2D>& Vertices, const TMap<int32, TArray<int32>>& Adjacency, TArray<TArray<FVector2D>>& OutPolygons);
	void ExtractPolygonsToGlassState(const FVector2D& Hit, const TArray<FTargetEdge>& Edges, const TArray<FVector2D>& Vertices, const TMap<int32, TArray<int32>>& Adjacency, TArray<FGlassShard>& InOutGlassShards);
};
