// Copyright 2026 Codnyx. All Rights Reserved.
#include "RetakeStageImpactReceiver.h"

#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "RetakeDestructionSubsystem.h"
#include "RetakeMapElementSubsystem.h"
#include "RetakeStagedDestructible.h"

namespace
{
    const FName ImpactSinkParameter(TEXT("User.RetakeImpactSink"));
}

bool URetakeStageImpactReceiver::HasSinkParameter(const UNiagaraSystem& System)
{
    return System.GetExposedParameters().IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetUObjectDef(), ImpactSinkParameter)) != INDEX_NONE;
}

bool URetakeStageImpactReceiver::Initialize(ARetakeStagedDestructible& InOwner, UNiagaraComponent& InParent,
    const FRetakeDestructibleExitEffect& Exit)
{
    if (Exit.MaximumImpactFires == 0 || Exit.MaximumImpactFires > 8 || Exit.ImpactFireVariants.IsEmpty()
        || Exit.ImpactFireVariants.Num() > 4) return false;
    const UNiagaraSystem* Asset = InParent.GetAsset();
    if (!Asset || !HasSinkParameter(*Asset)) return false;
    for (const TSoftObjectPtr<UNiagaraSystem>& Reference : Exit.ImpactFireVariants)
    {
        UNiagaraSystem* Variant = Reference.Get();
        if (!Variant || !Variant->IsValid() || !Variant->IsReadyToRun()) return false;
        Variants.Add(Variant);
    }
    Owner = &InOwner;
    Parent = &InParent;
    const auto Stages = InOwner.GetStageIndices();
    SourceStages.Append(Stages.GetData(), Stages.Num());
    MaximumContacts = Exit.MaximumImpactFires;
    InParent.SetVariableObject(ImpactSinkParameter, this);
    return true;
}

bool URetakeStageImpactReceiver::BelongsTo(const ARetakeStagedDestructible& Candidate) const
{
    return Owner.Get() == &Candidate;
}

bool URetakeStageImpactReceiver::HasLiveEffects() const
{
    return Parent.IsValid() || ActiveFires.ContainsByPredicate([](const auto& Reference) { return Reference.IsValid(); });
}

bool URetakeStageImpactReceiver::HasStageRollback() const
{
    const auto* Source = Owner.Get();
    if (!Source) return true;
    const auto Current = Source->GetStageIndices();
    if (Current.Num() != SourceStages.Num()) return true;
    for (int32 Index = 0; Index < Current.Num(); ++Index)
        if (Current[Index] < SourceStages[Index]) return true;
    return false;
}

void URetakeStageImpactReceiver::Clear()
{
    Owner.Reset();
    Parent.Reset();
    for (const auto& Reference : ActiveFires)
        if (auto* Fire = Reference.Get()) Fire->DestroyComponent();
    ActiveFires.Reset();
}

void URetakeStageImpactReceiver::ReceiveParticleData_Implementation(const TArray<FBasicParticleData>& Data,
    UNiagaraSystem* NiagaraSystem, const FVector& SimulationPositionOffset)
{
    const auto* Source = Owner.Get();
    const auto* Burst = Parent.Get();
    UWorld* World = Source ? Source->GetWorld() : nullptr;
    if (!World || World->bIsTearingDown || !Burst || Burst->GetAsset() != NiagaraSystem || SimulationPositionOffset.ContainsNaN()
        || URetakeMapElementSubsystem::IsReconstructingReplay(World)) return;
    auto* Destruction = GetTypedOuter<URetakeDestructionSubsystem>();
    if (!Destruction) return;
    if (HasStageRollback()) { Clear(); return; }
    for (const FBasicParticleData& Packet : Data)
    {
        if (ContactCount >= MaximumContacts) break;
        if (!FMath::IsFinite(Packet.Size) || Packet.Size < 0.f || Packet.Size >= Variants.Num()
            || Packet.Position.ContainsNaN() || Packet.Velocity.ContainsNaN()) continue;
        const int32 Index = FMath::TruncToInt(Packet.Size);
        const FVector Position = Packet.Position + SimulationPositionOffset;
        const FVector Normal = Packet.Velocity.GetSafeNormal();
        if (Packet.Size != static_cast<float>(Index) || Position.ContainsNaN() || Normal.IsNearlyZero()) continue;
        ++ContactCount;
        if (!Destruction->AcquireEffectBudget()) continue;
        if (auto* Fire = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Variants[Index], Position, Normal.Rotation(),
            FVector::OneVector, true, true, ENCPoolMethod::None))
        {
            ActiveFires.Add(Fire);
            // The existing destruction budget counts the component until it finishes or the prop's round reset destroys it.
            Destruction->ActiveEffects.Add({Fire, TNumericLimits<double>::Max()});
        }
    }
}
