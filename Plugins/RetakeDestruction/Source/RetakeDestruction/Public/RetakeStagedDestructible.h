// Copyright 2026 Codnyx. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Actor.h"
#include "RetakeStagedDestructible.generated.h"

class UPrimitiveComponent;
class UNiagaraSystem;
class URetakeAudioCueDefinition;
class UStaticMesh;
class UStaticMeshComponent;
enum class ERetakeDestructionDamageCause : uint8;

/** A spatial one-shot emitted when damage advances past the authored stage. */
USTRUCT(BlueprintType)
struct RETAKEDESTRUCTION_API FRetakeDestructibleStageCue
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Audio")
    TObjectPtr<URetakeAudioCueDefinition> Cue;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Audio")
    FVector LocalOrigin = FVector::ZeroVector;

    /** Delay in seconds for each recording in cue order. Empty for immediate playback. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Audio", meta=(ClampMin="0", ClampMax="10"))
    TArray<float> RecordingDelays;
};

/** A finite effect spawned periodically while this stage is active. */
USTRUCT(BlueprintType)
struct RETAKEDESTRUCTION_API FRetakeDestructibleRepeatEffect
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Effects", meta=(AssetBundles="Client"))
    TSoftObjectPtr<UNiagaraSystem> System;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Effects")
    FVector LocalOrigin = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Effects")
    FRotator LocalRotation = FRotator::ZeroRotator;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Effects", meta=(ClampMin="0.05", Units="s"))
    float IntervalSeconds = 0.4f;

    /** Zero leaves distance culling to Niagara. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Effects", meta=(ClampMin="0", Units="cm"))
    float RangeCm = 0.f;

    bool operator==(const FRetakeDestructibleRepeatEffect&) const = default;
};

/** A finite effect emitted when damage advances past the authored stage. */
USTRUCT(BlueprintType)
struct RETAKEDESTRUCTION_API FRetakeDestructibleExitEffect
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Effects", meta=(AssetBundles="Client"))
    TSoftObjectPtr<UNiagaraSystem> System;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Effects")
    FVector LocalOrigin = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Effects")
    FRotator LocalRotation = FRotator::ZeroRotator;

    /** Align the system's forward axis with world up. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Effects")
    bool bWorldUp = false;

    /** Upper lifetime for this burst within the shared effects budget. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Effects", meta=(ClampMin="0.05", ClampMax="60", Units="s"))
    float MaximumLifetimeSeconds = 30.f;

    /** Client effects selected by the spark emitter's exported zero-based variant index. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Effects", meta=(AssetBundles="Client"))
    TArray<TSoftObjectPtr<UNiagaraSystem>> ImpactFireVariants;

    /** Bound on contacts handled per exit effect. Zero disables the receiver. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Effects", meta=(ClampMax="8"))
    uint8 MaximumImpactFires = 0;
};

/** Optional authoritative area damage emitted when leaving a stage. A zero radius disables it. */
USTRUCT(BlueprintType)
struct RETAKEDESTRUCTION_API FRetakeDestructibleExitBlast
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Blasts")
    FVector LocalOrigin = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Blasts", meta=(Units="cm"))
    float WorldUpOffsetCm = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Blasts", meta=(ClampMin="0", Units="cm"))
    float RadiusCm = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Blasts", meta=(ClampMin="0"))
    float CenterDamage = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Blasts", meta=(ClampMin="0"))
    float EdgeDamage = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Blasts", meta=(ClampMin="0", ClampMax="10", Units="s"))
    float DelaySeconds = 0.f;

    bool IsEnabled() const { return RadiusCm > 0.f; }
    float DamageAtDistance(double DistanceCm) const;
};

/** A part's authored visuals and independent query collision, all in placement space. */
USTRUCT(BlueprintType)
struct RETAKEDESTRUCTION_API FRetakeDestructibleStage
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Stages")
    TArray<TObjectPtr<UStaticMesh>> VisualMeshes;

    /** Point-query meshes for weapon hits, projectile contacts and blast cover. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Stages")
    TArray<TObjectPtr<UStaticMesh>> CollisionMeshes;

    /** Optional authored simple hulls for pawn and vehicle movement while this stage is active. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Stages")
    TArray<TObjectPtr<UStaticMesh>> MovementCollisionMeshes;

    /** Damage required to advance to the next stage. Terminal stages have zero health and remain in place. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Stages", meta=(ClampMin="0"))
    float Health = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Stages")
    bool bRejectMelee = false;

    /** A body stage may replace the whole prop with a single authored wreck. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Stages")
    bool bReplaceWholeProp = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Stages", meta=(ClampMin="0"))
    float DrainDamage = 0.f;

    /** Optional positive range added to the base damage on each drain tick. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Stages", meta=(ClampMin="0"))
    float DrainDamageVariation = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Stages", meta=(ClampMin="0", Units="s"))
    float DrainInterval = 0.f;

    /** Schedule the first tick as soon as this stage enters, then use DrainInterval. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Stages")
    bool bDrainOnEntry = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Audio")
    TArray<FRetakeDestructibleStageCue> ExitCues;

    /** Persistent spatial loop while this stage is active. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Audio")
    TObjectPtr<URetakeAudioCueDefinition> LoopCue;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Audio")
    FVector LoopLocalOrigin = FVector::ZeroVector;

    /** Identical settings on adjacent stages preserve the current cadence. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Effects")
    FRetakeDestructibleRepeatEffect RepeatEffect;

    /** One-shots for a live transition into this stage, including the first visible ignition. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Effects")
    TArray<FRetakeDestructibleExitEffect> EntryEffects;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Effects")
    TArray<FRetakeDestructibleExitEffect> ExitEffects;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Blasts")
    FRetakeDestructibleExitBlast ExitBlast;
};

UENUM(BlueprintType)
enum class ERetakeDestructibleDamageMode : uint8
{
    HealthStages,
    IgniteOnDamage,
};

USTRUCT(BlueprintType)
struct RETAKEDESTRUCTION_API FRetakeDestructiblePart
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Parts")
    FName Id;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Parts")
    TArray<FRetakeDestructibleStage> Stages;

    /** Ignition uses intact and burning stages with one health pool, then the terminal variants. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Parts")
    ERetakeDestructibleDamageMode DamageMode = ERetakeDestructibleDamageMode::HealthStages;

    /** The server chooses one of the last stages when this part is destroyed. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Parts", meta=(ClampMin="1", ClampMax="4"))
    int32 TerminalVariantCount = 1;

    int32 FirstTerminalStage() const { return Stages.Num() - TerminalVariantCount; }

    /** Direct damage on this part can also damage the body. Splash already evaluates each part separately. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Parts", meta=(ClampMin="0"))
    float DirectBodyDamageScale = 0.f;
};

/** Immutable state machine for props with authored damage states. Part zero is the body. */
UCLASS(BlueprintType)
class RETAKEDESTRUCTION_API URetakeStagedDestructionDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Definitions")
    TArray<FRetakeDestructiblePart> Parts;

    bool HasAudio() const;
    bool HasStageEffects() const;
    FString GetConfigurationError() const;
#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

/** Server-owned damage, stable authored placement, and state-derived client presentation. */
UCLASS(NotBlueprintable)
class RETAKEDESTRUCTION_API ARetakeStagedDestructible : public AActor
{
    GENERATED_BODY()
public:
    ARetakeStagedDestructible();
    static const FName StageChannel;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retake|Destruction|Definitions")
    TObjectPtr<URetakeStagedDestructionDefinition> Definition;

    int32 FindHitPart(const UPrimitiveComponent* Component) const;
    FBox GetDamageablePartBounds(int32 Part) const;
    float ApplyAcceptedDamage(int32 Part, float Damage, bool bMelee, bool bSplash,
        ERetakeDestructionDamageCause Cause, const FGuid& DamageOwner = {});
    void ResetForRound();
    TConstArrayView<uint8> GetStageIndices() const { return Stages; }
    int32 GetAppliedStageIndex(int32 Part) const { return Runtime.IsValidIndex(Part) ? Runtime[Part].AppliedStage : INDEX_NONE; }
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void OnConstruction(const FTransform& Transform) override;
#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    static constexpr int32 BodyPart = 0;
    struct FPartRuntime
    {
        int32 AppliedStage = INDEX_NONE;
        float RemainingHealth = 0.f;
        FTimerHandle DrainTimer;
        TArray<TWeakObjectPtr<UStaticMeshComponent>> Components;
        TArray<TWeakObjectPtr<UStaticMeshComponent>> Collision;
    };

    UPROPERTY(ReplicatedUsing=OnRep_Stages)
    TArray<uint8> Stages;

#if WITH_EDITORONLY_DATA
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Preview;
#endif

    TArray<FPartRuntime> Runtime;
    FGuid LastDamageOwner;

    UFUNCTION()
    void OnRep_Stages();

    bool HasServerAuthority() const;
    bool IsPartVisible(int32 Part) const;
    const FRetakeDestructibleStage& GetStage(int32 Part) const;
    void ApplyStages();
    void ReplacePartComponents(int32 Part, int32 Stage);
    void StartDrain(int32 Part);
    float DamagePart(int32 Part, float Damage, bool bMelee, ERetakeDestructionDamageCause Cause);
    void AdvanceToTerminalStage(int32 Part);
    void NotifyExitBlasts(int32 Part, int32 PreviousStage);
};
