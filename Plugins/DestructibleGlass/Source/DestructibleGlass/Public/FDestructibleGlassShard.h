#pragma once

#include "CoreMinimal.h"
#include "FDestructibleGlassShard.generated.h"

USTRUCT()
struct FGlassSegment2D
{
	GENERATED_BODY()

	FVector2D A;
	FVector2D B;
};

USTRUCT(BlueprintType)
struct FGlassShard
{
	GENERATED_BODY()
	
	UPROPERTY()
	TArray<FVector2D> Polygons;

	UPROPERTY()
	TArray<FGlassSegment2D> Edges;

	UPROPERTY(BlueprintReadWrite, Category="DestructibleGlass")
	FBox2D Bound2D;

	UPROPERTY(BlueprintReadWrite, Category="DestructibleGlass")
	FVector Pivot = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category="DestructibleGlass")
	FTransform LocalTransform = FTransform::Identity;
	
	UPROPERTY(BlueprintReadWrite, Category="DestructibleGlass")
	bool bDetached = false;

	UPROPERTY(BlueprintReadWrite, Category="DestructibleGlass")
	float Area = 0.0f;
};