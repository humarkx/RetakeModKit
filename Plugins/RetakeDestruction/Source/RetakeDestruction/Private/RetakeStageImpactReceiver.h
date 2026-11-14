// Copyright 2026 Codnyx. All Rights Reserved.
#pragma once

#include "NiagaraDataInterfaceExport.h"
#include "RetakeStageImpactReceiver.generated.h"

class ARetakeStagedDestructible;
class UNiagaraComponent;
struct FRetakeDestructibleExitEffect;

/** Receives contact data from one CPU exit effect and owns its client-only children until the prop resets. */
UCLASS()
class URetakeStageImpactReceiver : public UObject, public INiagaraParticleCallbackHandler
{
    GENERATED_BODY()

public:
    static bool HasSinkParameter(const UNiagaraSystem& System);
    bool Initialize(ARetakeStagedDestructible& InOwner, UNiagaraComponent& InParent, const FRetakeDestructibleExitEffect& Exit);
    bool BelongsTo(const ARetakeStagedDestructible& Candidate) const;
    bool HasStageRollback() const;
    bool HasLiveEffects() const;
    void Clear();

    virtual void ReceiveParticleData_Implementation(const TArray<FBasicParticleData>& Data,
        UNiagaraSystem* NiagaraSystem, const FVector& SimulationPositionOffset) override;

private:
    TWeakObjectPtr<ARetakeStagedDestructible> Owner;
    TWeakObjectPtr<UNiagaraComponent> Parent;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UNiagaraSystem>> Variants;
    TArray<TWeakObjectPtr<UNiagaraComponent>> ActiveFires;
    TArray<uint8> SourceStages;
    uint8 MaximumContacts = 0;
    uint8 ContactCount = 0;
};
