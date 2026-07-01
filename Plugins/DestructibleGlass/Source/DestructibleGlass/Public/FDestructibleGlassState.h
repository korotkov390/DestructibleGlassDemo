#pragma once

#include "CoreMinimal.h"
#include "FDestructibleGlassShard.h"
#include "FDestructibleGlassState.generated.h"

USTRUCT(BlueprintType)
struct FDestructibleGlassState
{
	GENERATED_BODY()
	
	UPROPERTY()
	TArray<FGlassShard> Shards;
};