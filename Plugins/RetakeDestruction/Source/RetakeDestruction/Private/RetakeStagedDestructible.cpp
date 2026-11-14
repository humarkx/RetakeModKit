// Copyright 2026 Codnyx. All Rights Reserved.
#include "RetakeStagedDestructible.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Misc/PackageName.h"
#include "RetakeAudioCueDefinition.h"
#include "RetakeDestructionSubsystem.h"
#include "RetakeMapElementSubsystem.h"
#include "TimerManager.h"
#include "UObject/Package.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#include "NiagaraSystem.h"
#include "PhysicsEngine/BodySetup.h"
#include "RetakeDestructionDefinition.h"
#include "RetakeStageImpactReceiver.h"
#endif

namespace
{
    enum class EStageMeshRole : uint8
    {
        Visual,
        PointQuery,
        Movement,
    };

    bool IsValidStageCue(const URetakeAudioCueDefinition& Cue, FName Root, bool bLoop)
    {
        FString Error;
        return Cue.bSpatial && Cue.bLooping == bLoop && Cue.Category == ERetakeAudioCategory::Gameplay
            && FPackageName::GetPackageMountPoint(Cue.GetPackage()->GetName(), false) == Root
            && Cue.Validate(Error);
    }
}

float FRetakeDestructibleExitBlast::DamageAtDistance(double DistanceCm) const
{
    if (!IsEnabled() || !FMath::IsFinite(DistanceCm) || DistanceCm < 0. || DistanceCm >= RadiusCm) return 0.f;
    return FMath::Lerp(CenterDamage, EdgeDamage, static_cast<float>(DistanceCm / RadiusCm));
}

bool URetakeStagedDestructionDefinition::HasAudio() const
{
    for (const FRetakeDestructiblePart& Part : Parts)
        if (Part.Stages.ContainsByPredicate([](const FRetakeDestructibleStage& Stage)
            { return Stage.LoopCue || !Stage.ExitCues.IsEmpty(); })) return true;
    return false;
}

bool URetakeStagedDestructionDefinition::HasStageEffects() const
{
    for (const FRetakeDestructiblePart& Part : Parts)
        if (Part.Stages.ContainsByPredicate([](const FRetakeDestructibleStage& Stage)
            { return !Stage.RepeatEffect.System.IsNull() || !Stage.EntryEffects.IsEmpty() || !Stage.ExitEffects.IsEmpty(); }))
            return true;
    return false;
}

FString URetakeStagedDestructionDefinition::GetConfigurationError() const
{
    if (Parts.IsEmpty() || Parts.Num() > 64) return TEXT("Author a body and at most 63 additional destruction parts.");
    const FName Root = FPackageName::GetPackageMountPoint(GetPackage()->GetName(), false);
    TSet<FName> Ids;
    for (int32 PartIndex = 0; PartIndex < Parts.Num(); ++PartIndex)
    {
        const FRetakeDestructiblePart& Part = Parts[PartIndex];
        if (Part.Id.IsNone() || Ids.Contains(Part.Id) || Part.Stages.IsEmpty() || Part.Stages.Num() > 32)
            return TEXT("Each destruction part needs a unique ID and between one and 32 stages.");
        Ids.Add(Part.Id);
        if (Part.TerminalVariantCount < 1 || Part.TerminalVariantCount > 4 || Part.FirstTerminalStage() < 0
            || (Part.TerminalVariantCount > 1 && Part.FirstTerminalStage() == 0))
            return FString::Printf(TEXT("Invalid terminal variant count on part %ls."), *Part.Id.ToString());
        if (Part.DamageMode == ERetakeDestructibleDamageMode::IgniteOnDamage)
        {
            if (PartIndex != 0 || Part.FirstTerminalStage() != 2
                || Part.Stages[0].Health != Part.Stages[1].Health
                || Part.Stages[0].DrainDamage != 0.f || Part.Stages[1].DrainDamage <= 0.f)
                return TEXT("Ignition needs intact and burning body stages with one health pool and drain only while burning.");
        }
        else if (Part.DamageMode != ERetakeDestructibleDamageMode::HealthStages)
            return TEXT("Choose a supported destruction damage mode.");
        if (!FMath::IsFinite(Part.DirectBodyDamageScale) || Part.DirectBodyDamageScale < 0.f
            || (PartIndex == 0 && Part.DirectBodyDamageScale != 0.f))
            return FString::Printf(TEXT("Invalid direct body damage transfer on part %ls."), *Part.Id.ToString());
        for (int32 StageIndex = 0; StageIndex < Part.Stages.Num(); ++StageIndex)
        {
            const FRetakeDestructibleStage& Stage = Part.Stages[StageIndex];
            const bool bTerminal = StageIndex >= Part.FirstTerminalStage();
            if (!FMath::IsFinite(Stage.Health) || (bTerminal ? Stage.Health != 0.f : Stage.Health <= 0.f)
                || !FMath::IsFinite(Stage.DrainDamage) || Stage.DrainDamage < 0.f
                || !FMath::IsFinite(Stage.DrainDamageVariation) || Stage.DrainDamageVariation < 0.f
                || !FMath::IsFinite(Stage.DrainDamage + Stage.DrainDamageVariation)
                || ((Stage.DrainDamageVariation > 0.f || Stage.bDrainOnEntry) && Stage.DrainDamage <= 0.f)
                || !FMath::IsFinite(Stage.DrainInterval) || Stage.DrainInterval < 0.f
                || (Stage.DrainDamage > 0.f) != (Stage.DrainInterval > 0.f)
                || (bTerminal && Stage.DrainDamage != 0.f) || (PartIndex != 0 && Stage.bReplaceWholeProp))
                return FString::Printf(TEXT("Invalid health, drain or replacement on part %ls stage %d."), *Part.Id.ToString(), StageIndex);
            if (Stage.ExitCues.Num() > 8 || (bTerminal && !Stage.ExitCues.IsEmpty())
                || Stage.LoopLocalOrigin.ContainsNaN()
                || (Stage.LoopCue && !IsValidStageCue(*Stage.LoopCue, Root, true)))
                return FString::Printf(TEXT("Invalid exit cue count or loop on part %ls stage %d."), *Part.Id.ToString(), StageIndex);
            for (const FRetakeDestructibleStageCue& Exit : Stage.ExitCues)
            {
                if (!Exit.Cue || !IsValidStageCue(*Exit.Cue, Root, false) || Exit.LocalOrigin.ContainsNaN()
                    || (!Exit.RecordingDelays.IsEmpty() && Exit.RecordingDelays.Num() != Exit.Cue->Recordings.Num())
                    || Exit.RecordingDelays.ContainsByPredicate([](float Delay) { return !FMath::IsFinite(Delay) || Delay < 0.f || Delay > 10.f; }))
                    return FString::Printf(TEXT("Invalid exit cue or recording delays on part %ls stage %d."), *Part.Id.ToString(), StageIndex);
            }
            const FRetakeDestructibleRepeatEffect& Effect = Stage.RepeatEffect;
            if (!Effect.System.IsNull()
                && (FPackageName::GetPackageMountPoint(Effect.System.ToSoftObjectPath().GetLongPackageName(), false) != Root
                    || Effect.LocalOrigin.ContainsNaN() || Effect.LocalRotation.ContainsNaN()
                    || !FMath::IsFinite(Effect.IntervalSeconds) || Effect.IntervalSeconds < 0.05f
                    || !FMath::IsFinite(Effect.RangeCm) || Effect.RangeCm < 0.f))
                return FString::Printf(TEXT("Invalid repeated effect on part %ls stage %d."), *Part.Id.ToString(), StageIndex);
            if (Stage.EntryEffects.Num() > 8 || Stage.ExitEffects.Num() > 8 || (bTerminal && !Stage.ExitEffects.IsEmpty()))
                return FString::Printf(TEXT("Invalid transition effect count on part %ls stage %d."), *Part.Id.ToString(), StageIndex);
            for (const auto* Effects : {&Stage.EntryEffects, &Stage.ExitEffects})
            for (const FRetakeDestructibleExitEffect& Exit : *Effects)
            {
                if (Exit.System.IsNull() || FPackageName::GetPackageMountPoint(Exit.System.ToSoftObjectPath().GetLongPackageName(), false) != Root
                    || Exit.LocalOrigin.ContainsNaN() || Exit.LocalRotation.ContainsNaN()
                    || !FMath::IsFinite(Exit.MaximumLifetimeSeconds) || Exit.MaximumLifetimeSeconds < 0.05f
                    || Exit.MaximumLifetimeSeconds > 60.f || Exit.ImpactFireVariants.Num() > 4 || Exit.MaximumImpactFires > 8
                    || Exit.ImpactFireVariants.IsEmpty() != (Exit.MaximumImpactFires == 0))
                    return FString::Printf(TEXT("Invalid transition effect on part %ls stage %d."), *Part.Id.ToString(), StageIndex);
                TSet<FSoftObjectPath> UniqueImpactFires;
                for (const TSoftObjectPtr<UNiagaraSystem>& Variant : Exit.ImpactFireVariants)
                {
                    const FSoftObjectPath Path = Variant.ToSoftObjectPath();
                    if (Path.IsNull() || Path == Exit.System.ToSoftObjectPath() || UniqueImpactFires.Contains(Path)
                        || FPackageName::GetPackageMountPoint(Path.GetLongPackageName(), false) != Root)
                        return FString::Printf(TEXT("Invalid impact fire on part %ls stage %d."), *Part.Id.ToString(), StageIndex);
                    UniqueImpactFires.Add(Path);
                }
            }
            const FRetakeDestructibleExitBlast& Blast = Stage.ExitBlast;
            if (Blast.LocalOrigin.ContainsNaN() || !FMath::IsFinite(Blast.WorldUpOffsetCm)
                || !FMath::IsFinite(Blast.RadiusCm) || Blast.RadiusCm < 0.f
                || !FMath::IsFinite(Blast.CenterDamage) || Blast.CenterDamage < 0.f
                || !FMath::IsFinite(Blast.EdgeDamage) || Blast.EdgeDamage < 0.f || Blast.EdgeDamage > Blast.CenterDamage
                || !FMath::IsFinite(Blast.DelaySeconds) || Blast.DelaySeconds < 0.f || Blast.DelaySeconds > 10.f
                || Blast.IsEnabled() != (Blast.CenterDamage > 0.f) || (bTerminal && Blast.IsEnabled()))
                return FString::Printf(TEXT("Invalid exit blast on part %ls stage %d."), *Part.Id.ToString(), StageIndex);
        }
    }
    return {};
}

#if WITH_EDITOR
EDataValidationResult URetakeStagedDestructionDefinition::IsDataValid(FDataValidationContext& Context) const
{
    EDataValidationResult Result = Super::IsDataValid(Context);
    if (const FString Error = GetConfigurationError(); !Error.IsEmpty())
    {
        Context.AddError(FText::FromString(Error));
        return EDataValidationResult::Invalid;
    }
    const FName Root = FPackageName::GetPackageMountPoint(GetPackage()->GetName(), false);
    TSet<FSoftObjectPath> ValidatedEffects;
    const auto CheckEffect = [&](const TSoftObjectPtr<UNiagaraSystem>& Reference)
    {
        if (Reference.IsNull() || ValidatedEffects.Contains(Reference.ToSoftObjectPath())) return;
        ValidatedEffects.Add(Reference.ToSoftObjectPath());
        const UNiagaraSystem* Effect = Reference.LoadSynchronous();
        const FString Problem = Effect ? URetakeDestructionDefinition::GetEffectLifeCycleError(*Effect)
                                       : TEXT("The Niagara system could not be loaded");
        if (!Problem.IsEmpty())
        {
            Context.AddError(FText::FromString(FString::Printf(TEXT("Stage effect %ls must end on its own: %ls."),
                *Reference.ToString(), *Problem)));
            Result = EDataValidationResult::Invalid;
        }
    };
    for (const FRetakeDestructiblePart& Part : Parts)
    {
        for (const FRetakeDestructibleStage& Stage : Part.Stages)
        {
            auto CheckMeshes = [&](const TArray<TObjectPtr<UStaticMesh>>& Meshes, EStageMeshRole Role)
            {
                TSet<const UStaticMesh*> Seen;
                for (const UStaticMesh* Mesh : Meshes)
                {
                    const UBodySetup* Body = Mesh ? Mesh->GetBodySetup() : nullptr;
                    bool bValidCollision = true;
                    const TCHAR* Description = TEXT("visual");
                    if (Role == EStageMeshRole::PointQuery)
                    {
                        Description = TEXT("point-query collision");
                        bValidCollision = Body && Body->GetCollisionTraceFlag() == CTF_UseComplexAsSimple;
                    }
                    else if (Role == EStageMeshRole::Movement)
                    {
                        Description = TEXT("movement collision");
                        bValidCollision = Body && Body->GetCollisionTraceFlag() != CTF_UseComplexAsSimple
                            && Body->AggGeom.GetElementCount() > 0;
                    }
                    if (!Mesh || Seen.Contains(Mesh) || !bValidCollision
                        || FPackageName::GetPackageMountPoint(Mesh->GetPackage()->GetName(), false) != Root)
                    {
                        Context.AddError(FText::FromString(FString::Printf(TEXT("Invalid %ls mesh %ls on staged part %ls."),
                            Description, *GetNameSafe(Mesh), *Part.Id.ToString())));
                        Result = EDataValidationResult::Invalid;
                    }
                    Seen.Add(Mesh);
                }
            };
            CheckMeshes(Stage.VisualMeshes, EStageMeshRole::Visual);
            CheckMeshes(Stage.CollisionMeshes, EStageMeshRole::PointQuery);
            CheckMeshes(Stage.MovementCollisionMeshes, EStageMeshRole::Movement);
            CheckEffect(Stage.RepeatEffect.System);
            for (const auto* Effects : {&Stage.EntryEffects, &Stage.ExitEffects})
            for (const FRetakeDestructibleExitEffect& Exit : *Effects)
            {
                CheckEffect(Exit.System);
                if (Exit.ImpactFireVariants.IsEmpty()) continue;
                const UNiagaraSystem* Parent = Exit.System.Get();
                if (!Parent || !URetakeStageImpactReceiver::HasSinkParameter(*Parent))
                {
                    Context.AddError(FText::FromString(FString::Printf(TEXT("Stage effect %ls needs the native impact callback parameter."),
                        *Exit.System.ToString())));
                    Result = EDataValidationResult::Invalid;
                }
                for (const auto& Variant : Exit.ImpactFireVariants)
                    if (!Variant.LoadSynchronous())
                    {
                        Context.AddError(FText::FromString(FString::Printf(TEXT("Could not load stage impact fire %ls."), *Variant.ToString())));
                        Result = EDataValidationResult::Invalid;
                    }
            }
        }
    }
    return Result == EDataValidationResult::Invalid ? Result : EDataValidationResult::Valid;
}
#endif

const FName ARetakeStagedDestructible::StageChannel(TEXT("DestructionStages"));

ARetakeStagedDestructible::ARetakeStagedDestructible()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;
    bAlwaysRelevant = true;
    SetReplicateMovement(false);
    SetNetUpdateFrequency(2.f);
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    Tags.Add(TEXT("Retake.Visibility.Ignore"));
#if WITH_EDITORONLY_DATA
    Preview = CreateEditorOnlyDefaultSubobject<UStaticMeshComponent>(TEXT("Preview"));
    if (Preview)
    {
        Preview->SetupAttachment(RootComponent);
        Preview->SetCollisionProfileName(TEXT("NoCollision"));
        Preview->SetCanEverAffectNavigation(false);
    }
#endif
}

void ARetakeStagedDestructible::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
#if WITH_EDITORONLY_DATA
    if (Preview)
    {
        const bool bHasPreview = Definition && !Definition->Parts.IsEmpty() && !Definition->Parts[0].Stages.IsEmpty()
            && !Definition->Parts[0].Stages[0].VisualMeshes.IsEmpty();
        Preview->SetStaticMesh(bHasPreview ? Definition->Parts[0].Stages[0].VisualMeshes[0].Get() : nullptr);
    }
#endif
}

#if WITH_EDITOR
EDataValidationResult ARetakeStagedDestructible::IsDataValid(FDataValidationContext& Context) const
{
    const EDataValidationResult Result = Super::IsDataValid(Context);
    if (!Definition || FPackageName::GetPackageMountPoint(GetPackage()->GetName(), false)
        != FPackageName::GetPackageMountPoint(Definition->GetPackage()->GetName(), false))
    {
        Context.AddError(FText::FromString(TEXT("Choose a staged destruction definition from the map's own package root.")));
        return EDataValidationResult::Invalid;
    }
    if ((Definition->HasAudio() || Definition->HasStageEffects()) && (!GetWorld() || GetLevel() != GetWorld()->PersistentLevel))
    {
        Context.AddError(FText::FromString(TEXT("Place staged props with audio or effects in the persistent level for preparation at admission.")));
        return EDataValidationResult::Invalid;
    }
    if (Definition->IsDataValid(Context) == EDataValidationResult::Invalid) return EDataValidationResult::Invalid;
    return Result == EDataValidationResult::Invalid ? Result : EDataValidationResult::Valid;
}
#endif

void ARetakeStagedDestructible::BeginPlay()
{
    Super::BeginPlay();
#if WITH_EDITORONLY_DATA
    if (Preview) Preview->DestroyComponent();
#endif
    if (!Definition || !Definition->GetConfigurationError().IsEmpty()) return;
    Runtime.SetNum(Definition->Parts.Num());
    if (HasServerAuthority()) ResetForRound();
    else ApplyStages();
    URetakeMapElementSubsystem::Register(*this);
}

void ARetakeStagedDestructible::EndPlay(const EEndPlayReason::Type Reason)
{
    URetakeMapElementSubsystem::Unregister(*this);
    for (FPartRuntime& Part : Runtime) GetWorldTimerManager().ClearTimer(Part.DrainTimer);
    Super::EndPlay(Reason);
}

bool ARetakeStagedDestructible::HasServerAuthority() const
{
    return HasAuthority() && !IsNetMode(NM_Client);
}

const FRetakeDestructibleStage& ARetakeStagedDestructible::GetStage(int32 Part) const
{
    const TArray<FRetakeDestructibleStage>& Authored = Definition->Parts[Part].Stages;
    const int32 Stage = Stages.IsValidIndex(Part) ? Stages[Part] : 0;
    return Authored[Authored.IsValidIndex(Stage) ? Stage : 0];
}

bool ARetakeStagedDestructible::IsPartVisible(int32 Part) const
{
    return Part == BodyPart || !GetStage(BodyPart).bReplaceWholeProp;
}

void ARetakeStagedDestructible::ReplacePartComponents(int32 Part, int32 Stage)
{
    FPartRuntime& Current = Runtime[Part];
    for (const TWeakObjectPtr<UStaticMeshComponent>& Weak : Current.Components)
    {
        if (UStaticMeshComponent* Component = Weak.Get())
        {
            RemoveInstanceComponent(Component);
            Component->DestroyComponent();
        }
    }
    Current.Components.Reset();
    Current.Collision.Reset();
    Current.AppliedStage = Stage;
    if (Stage == INDEX_NONE) return;
    const FRetakeDestructibleStage& Authored = GetStage(Part);
    auto CreateComponents = [&](const TArray<TObjectPtr<UStaticMesh>>& Meshes, EStageMeshRole MeshRole)
    {
        for (UStaticMesh* Mesh : Meshes)
        {
            if (!Mesh) continue;
            UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
            AddInstanceComponent(Component);
            Component->SetupAttachment(RootComponent);
            Component->SetStaticMesh(Mesh);
            Component->SetMobility(EComponentMobility::Movable);
            Component->SetCollisionProfileName(TEXT("NoCollision"));
            Component->SetGenerateOverlapEvents(false);
            Component->SetCanEverAffectNavigation(false);
            if (MeshRole != EStageMeshRole::Visual)
            {
                Component->SetVisibility(false);
                Component->SetCastShadow(false);
                Component->SetCollisionObjectType(ECC_WorldStatic);
                Component->SetCollisionResponseToAllChannels(ECR_Ignore);
                if (MeshRole == EStageMeshRole::PointQuery)
                {
                    Component->SetCollisionResponseToChannel(ECC_GameTraceChannel4, ECR_Block); // Weapon
                    Component->SetCollisionResponseToChannel(ECC_GameTraceChannel5, ECR_Block); // Explosion
                    Component->SetCollisionResponseToChannel(ECC_GameTraceChannel6, ECR_Block); // RetakeGrenade
                    Current.Collision.Add(Component);
                }
                else
                {
                    Component->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
                    Component->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Block);
                }
                Component->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            }
            Component->RegisterComponent();
            Current.Components.Add(Component);
        }
    };
    if (!IsNetMode(NM_DedicatedServer)) CreateComponents(Authored.VisualMeshes, EStageMeshRole::Visual);
    CreateComponents(Authored.CollisionMeshes, EStageMeshRole::PointQuery);
    CreateComponents(Authored.MovementCollisionMeshes, EStageMeshRole::Movement);
}

void ARetakeStagedDestructible::ApplyStages()
{
    bool bChanged = false;
    for (int32 Part = 0; Part < Runtime.Num(); ++Part)
    {
        const int32 Index = Stages.IsValidIndex(Part) ? Stages[Part] : 0;
        const int32 Stage = IsPartVisible(Part) ? Index : INDEX_NONE;
        FPartRuntime& Current = Runtime[Part];
        if (Current.AppliedStage == Stage) continue;
        const TArray<FRetakeDestructibleStage>& Authored = Definition->Parts[Part].Stages;
        if (Authored.IsValidIndex(Current.AppliedStage) && Authored.IsValidIndex(Stage)
            && Authored[Current.AppliedStage].VisualMeshes == Authored[Stage].VisualMeshes
            && Authored[Current.AppliedStage].CollisionMeshes == Authored[Stage].CollisionMeshes
            && Authored[Current.AppliedStage].MovementCollisionMeshes == Authored[Stage].MovementCollisionMeshes)
            Current.AppliedStage = Stage;
        else
            ReplacePartComponents(Part, Stage);
        if (HasServerAuthority()) StartDrain(Part);
        bChanged = true;
    }
    if (bChanged)
    {
        if (URetakeDestructionSubsystem* Destruction = GetWorld()->GetSubsystem<URetakeDestructionSubsystem>())
            Destruction->NotifyStructureChanged();
        URetakeMapElementSubsystem::NotifyStateChanged(*this, StageChannel);
    }
}

void ARetakeStagedDestructible::StartDrain(int32 Part)
{
    FTimerManager& Timers = GetWorldTimerManager();
    FTimerHandle& Timer = Runtime[Part].DrainTimer;
    Timers.ClearTimer(Timer);
    if (!IsPartVisible(Part)) return;
    const FRetakeDestructibleStage& Stage = GetStage(Part);
    if (Stage.DrainDamage <= 0.f) return;
    const float Damage = Stage.DrainDamage;
    const float Variation = Stage.DrainDamageVariation;
    Timers.SetTimer(Timer, FTimerDelegate::CreateWeakLambda(this, [this, Part, Damage, Variation]()
    {
        const float TickDamage = Variation > 0.f ? Damage + Variation * FMath::FRand() : Damage;
        ApplyAcceptedDamage(Part, TickDamage, false, true, ERetakeDestructionDamageCause::Normal);
    }), Stage.DrainInterval, true, Stage.bDrainOnEntry ? 0.f : -1.f);
}

int32 ARetakeStagedDestructible::FindHitPart(const UPrimitiveComponent* Component) const
{
    if (!HasServerAuthority() || !Component) return INDEX_NONE;
    for (int32 Part = 0; Part < Runtime.Num(); ++Part)
        for (const TWeakObjectPtr<UStaticMeshComponent>& Mesh : Runtime[Part].Collision)
            if (Mesh.Get() == Component) return Part;
    return INDEX_NONE;
}

FBox ARetakeStagedDestructible::GetDamageablePartBounds(int32 Part) const
{
    FBox Bounds(ForceInit);
    if (!Runtime.IsValidIndex(Part) || Runtime[Part].RemainingHealth <= 0.f) return Bounds;
    for (const TWeakObjectPtr<UStaticMeshComponent>& Weak : Runtime[Part].Collision)
        if (const UStaticMeshComponent* Mesh = Weak.Get()) Bounds += Mesh->Bounds.GetBox();
    return Bounds;
}

void ARetakeStagedDestructible::AdvanceToTerminalStage(int32 Part)
{
    const FRetakeDestructiblePart& Authored = Definition->Parts[Part];
    const int32 Variant = Authored.TerminalVariantCount > 1 ? FMath::RandHelper(Authored.TerminalVariantCount) : 0;
    Stages[Part] = static_cast<uint8>(Authored.FirstTerminalStage() + Variant);
    Runtime[Part].RemainingHealth = 0.f;
}

float ARetakeStagedDestructible::DamagePart(int32 Part, float Damage, bool bMelee, ERetakeDestructionDamageCause Cause)
{
    FPartRuntime& Current = Runtime[Part];
    const FRetakeDestructiblePart& Authored = Definition->Parts[Part];
    if (Authored.DamageMode == ERetakeDestructibleDamageMode::IgniteOnDamage)
    {
        if (bMelee || Cause == ERetakeDestructionDamageCause::ProjectileImpact) return 0.f;
        if (Stages[Part] == 0)
        {
            if (Cause == ERetakeDestructionDamageCause::GrenadeDetonation)
            {
                const float Accepted = FMath::Min(Damage, Current.RemainingHealth);
                AdvanceToTerminalStage(Part);
                return Accepted;
            }
            Stages[Part] = 1;
        }
    }
    float Accepted = 0.f;
    while (Damage > 0.f && Current.RemainingHealth > 0.f && (!bMelee || !GetStage(Part).bRejectMelee))
    {
        const float Used = FMath::Min(Damage, Current.RemainingHealth);
        Current.RemainingHealth -= Used;
        Damage -= Used;
        Accepted += Used;
        if (Current.RemainingHealth == 0.f)
        {
            if (Stages[Part] + 1 == Authored.FirstTerminalStage())
                AdvanceToTerminalStage(Part);
            else
            {
                ++Stages[Part];
                Current.RemainingHealth = GetStage(Part).Health;
            }
        }
    }
    return Accepted;
}

float ARetakeStagedDestructible::ApplyAcceptedDamage(int32 Part, float Damage, bool bMelee, bool bSplash,
    ERetakeDestructionDamageCause Cause, const FGuid& DamageOwner)
{
    if (!HasServerAuthority() || !Runtime.IsValidIndex(Part) || !IsPartVisible(Part)
        || !FMath::IsFinite(Damage) || Damage <= 0.f) return 0.f;
    const uint8 OldStage = Stages[Part];
    const uint8 OldBodyStage = Stages[BodyPart];
    const bool bTransfer = Part != BodyPart && !bSplash && (!bMelee || !GetStage(Part).bRejectMelee);
    const float PartDamage = DamagePart(Part, Damage, bMelee, Cause);
    float BodyDamage = 0.f;
    if (bTransfer)
    {
        const float Transferred = Damage * Definition->Parts[Part].DirectBodyDamageScale;
        if (FMath::IsFinite(Transferred) && Transferred > 0.f)
            BodyDamage = DamagePart(BodyPart, Transferred, bMelee, Cause);
    }
    const float Accepted = FMath::Min(Damage, PartDamage + BodyDamage);
    if (Accepted > 0.f && DamageOwner.IsValid()) LastDamageOwner = DamageOwner;
    if (Stages[Part] != OldStage || Stages[BodyPart] != OldBodyStage)
    {
        ApplyStages();
        ForceNetUpdate();
        NotifyExitBlasts(Part, OldStage);
        if (Part != BodyPart) NotifyExitBlasts(BodyPart, OldBodyStage);
    }
    return Accepted;
}

void ARetakeStagedDestructible::NotifyExitBlasts(int32 Part, int32 PreviousStage)
{
    if (PreviousStage == Stages[Part]) return;
    URetakeDestructionSubsystem* Destruction = GetWorld()->GetSubsystem<URetakeDestructionSubsystem>();
    if (!Destruction || !Destruction->OnStagedBlast.IsBound()) return;
    for (int32 Stage = PreviousStage; Stage < Stages[Part]; ++Stage)
        if (Definition->Parts[Part].Stages[Stage].ExitBlast.IsEnabled())
            Destruction->OnStagedBlast.Broadcast(*this, Part, Stage, LastDamageOwner);
}

void ARetakeStagedDestructible::ResetForRound()
{
    if (!HasServerAuthority() || Runtime.IsEmpty()) return;
    LastDamageOwner.Invalidate();
    Stages.Init(0, Runtime.Num());
    for (int32 Part = 0; Part < Runtime.Num(); ++Part)
    {
        Runtime[Part].RemainingHealth = GetStage(Part).Health;
        StartDrain(Part);
    }
    ApplyStages();
    ForceNetUpdate();
}

void ARetakeStagedDestructible::OnRep_Stages()
{
    ApplyStages();
}

void ARetakeStagedDestructible::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ARetakeStagedDestructible, Stages);
}
