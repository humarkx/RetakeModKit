// Copyright 2026 Codnyx. All Rights Reserved.
#include "RetakeAcousticWorldSubsystem.h"
#include "RetakeAcousticMap.h"
#include "RetakeDoor.h"
#include "RetakeMapElementSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
    // Resolve runs on the game thread only (its cache and the scene query), so plain counters are enough.
    URetakeAcousticWorldSubsystem::FQueryTotals GQueryTotals;
}

URetakeAcousticWorldSubsystem::FQueryTotals URetakeAcousticWorldSubsystem::GetQueryTotals() { return GQueryTotals; }

bool URetakeAcousticWorldSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{ return Type == EWorldType::Game || Type == EWorldType::PIE; }

void URetakeAcousticWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    auto* Registry = Collection.InitializeDependency<URetakeMapElementSubsystem>();
    AddedHandle = Registry->OnElementAdded.AddWeakLambda(this, [this](AActor& Actor)
        { if (Actor.IsA<ARetakeAcousticConnection>() || Actor.IsA<ARetakeDoor>()) RefreshTopology(); });
    RemovedHandle = Registry->OnElementRemoved.AddWeakLambda(this, [this](AActor& Actor)
        { if (Actor.IsA<ARetakeAcousticConnection>() || Actor.IsA<ARetakeDoor>()) { Cache.Reset(); ++TopologyRevision; OnTopologyChanged.Broadcast(); } });
    StateHandle = Registry->OnElementStateChanged.AddWeakLambda(this, [this](AActor&, FName Channel)
        { if (Channel == ARetakeAcousticConnection::StateChannel || Channel == ARetakeDoor::StateChannel) RefreshTopology(); });
}

void URetakeAcousticWorldSubsystem::Deinitialize()
{
    if (auto* Registry = GetWorld()->GetSubsystem<URetakeMapElementSubsystem>())
    {
        Registry->OnElementAdded.Remove(AddedHandle); Registry->OnElementRemoved.Remove(RemovedHandle);
        Registry->OnElementStateChanged.Remove(StateHandle);
    }
    bReady = false; Cache.Reset(); ConnectionActors.Reset(); OpenStates.Reset(); Data = nullptr;
    OnTopologyChanged.Clear();
    Super::Deinitialize();
}

bool URetakeAcousticWorldSubsystem::Prepare(FString& Error)
{
    if (bReady) return true;
    ARetakeAcousticMap* Authoring = nullptr;
    for (TActorIterator<ARetakeAcousticMap> It(GetWorld()); It; ++It)
    {
        if (Authoring) { Error = TEXT("Gameplay map contains multiple Retake Acoustic Map actors."); return false; }
        Authoring = *It;
    }
    if (!Authoring) { Error = TEXT("Gameplay map requires a Retake Acoustic Map and a current bake."); return false; }
    if (!Authoring->ValidateBake(Error)) return false;
    Data = Authoring->Data;
    TMap<FName, ARetakeAcousticConnection*> ById;
    for (TActorIterator<ARetakeAcousticConnection> It(GetWorld()); It; ++It) ById.Add(It->ConnectionId, *It);
    ConnectionActors.Reset(); OpenStates.Reset();
    for (const auto& Edge : Data->Connections)
    {
        auto* Actor = ById.FindRef(Edge.Id);
        if (!Actor) { Error = TEXT("Baked acoustic connection is absent from the world."); Data = nullptr; return false; }
        ConnectionActors.Add(Actor); OpenStates.Add(Actor->HasActorBegunPlay() ? Actor->IsOpen() : Edge.bInitiallyOpen);
    }
    bReady = true; ++TopologyRevision; Cache.Reset();
    RefreshTopology();
    return true;
}

void URetakeAcousticWorldSubsystem::RefreshTopology()
{
    if (!bReady) return;
    bool Changed = false;
    for (int32 Index = 0; Index < ConnectionActors.Num(); ++Index)
    {
        const auto* Actor = ConnectionActors[Index].Get();
        const bool Open = Actor && Actor->HasActorBegunPlay() ? Actor->IsOpen() : Data->Connections[Index].bInitiallyOpen;
        if (OpenStates[Index] != uint8(Open)) { OpenStates[Index] = Open; Changed = true; }
    }
    if (Changed) { ++TopologyRevision; Cache.Reset(); OnTopologyChanged.Broadcast(); }
}

FRetakeAcousticPathResult URetakeAcousticWorldSubsystem::Resolve(const FVector& Source, const FVector& Listener,
    const FRetakeAudioPropagationProfile& Profile, const AActor* SourceActor, const AActor* ListenerActor)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(RetakeAudio_PropagationGraph);
    FRetakeAcousticPathResult Result;
    if (!bReady || Source.ContainsNaN() || Listener.ContainsNaN()) return Result;
    ++GQueryTotals.Resolves;
    const float Distance = FVector::Distance(Source, Listener);
    if (Profile.GainAtDistance(Distance) <= .001f) return Result;
    const auto Quantize = [](const FVector& V) { return FIntVector(FMath::FloorToInt(V.X / 50.), FMath::FloorToInt(V.Y / 50.), FMath::FloorToInt(V.Z / 50.)); };
    FCacheKey Key{Quantize(Source), Quantize(Listener), Data->FindRegion(Source), Data->FindRegion(Listener),
        Profile.RangeCm, Profile.FullGainRadiusCm, Profile.FalloffExponent, Profile.ObstructionStrength,
        FObjectKey(SourceActor), FObjectKey(ListenerActor)};
    for (const auto& Point : Profile.FalloffPoints)
    {
        Key.FalloffPoints.Add(Point.DistanceFraction);
        Key.FalloffPoints.Add(Point.Gain);
    }
    if (Key.SourceRegion == INDEX_NONE || Key.ListenerRegion == INDEX_NONE) return Result;
    const double Now = GetWorld()->GetTimeSeconds();
    if (const auto* Entry = Cache.Find(Key); Entry && Now < Entry->Expires && Now >= Entry->Expires - .1)
    { ++GQueryTotals.CacheHits; return Entry->Result; }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(RetakeAcousticStructure), false, SourceActor);
    if (ListenerActor) Query.AddIgnoredActor(ListenerActor);
    int32 TraceChecks = 0;
    bool bTraceLimitReached = false;
    const auto ResolveStructure = [&](const FVector& From, const FVector& To, const FRetakeAcousticMaterial& Material)
    {
        const auto EffectiveMaterial = Material.ForProfile(Profile);
        FRetakeAcousticMaterial Transmission;
        Transmission.Transmission = 1.f;
        Transmission.LowPassHz = 20000.f;
        FCollisionQueryParams SegmentQuery = Query;
        // Each distinct structural component is one bounded transmission layer.
        // Continue across it rather than treating the first obstruction as absolute silence.
        for (int32 Layer = 0; Layer <= RetakeAcousticGraph::MaximumObstructionLayers; ++Layer)
        {
            if (TraceChecks == RetakeAcousticGraph::MaximumTraceChecks)
            {
                bTraceLimitReached = true;
                Transmission.Transmission = 0.f;
                return Transmission;
            }
            ++TraceChecks;
            ++GQueryTotals.Traces;
            FHitResult Hit;
            if (!GetWorld()->LineTraceSingleByChannel(Hit, From, To, ECC_GameTraceChannel7, SegmentQuery))
                return Transmission;
            const UPrimitiveComponent* Blocker = Hit.GetComponent();
            if (!Blocker || Layer == RetakeAcousticGraph::MaximumObstructionLayers)
            {
                Transmission.Transmission = 0.f;
                return Transmission;
            }
            Transmission.Transmission *= EffectiveMaterial.Transmission;
            Transmission.LowPassHz = FMath::Min(Transmission.LowPassHz, EffectiveMaterial.LowPassHz);
            if (Transmission.Transmission <= .001f) return Transmission;
            SegmentQuery.AddIgnoredComponent(Blocker);
        }
        return Transmission;
    };
    const auto DoorForConnection = [this](int32 Index) -> const ARetakeDoor*
    {
        const auto* Connection = ConnectionActors.IsValidIndex(Index) ? ConnectionActors[Index].Get() : nullptr;
        return Connection ? Connection->Door.Get() : nullptr;
    };
    struct FClosedDoor
    {
        const ARetakeDoor* Actor;
        const UStaticMeshComponent* Mesh;
        int32 Connection;
    };
    TArray<FClosedDoor, TInlineAllocator<32>> ClosedDoors;
    for (int32 Index = 0; Index < ConnectionActors.Num(); ++Index)
    {
        if (OpenStates[Index]) continue;
        const auto* Door = DoorForConnection(Index);
        const auto* Mesh = Door ? Door->DoorMesh.Get() : nullptr;
        if (Mesh && Mesh->GetStaticMesh() && Door != SourceActor && Door != ListenerActor
            && !ClosedDoors.ContainsByPredicate([Door](const FClosedDoor& Entry) { return Entry.Actor == Door; }))
            ClosedDoors.Add({Door, Mesh, Index});
    }
    // Door leaves stay out of structural collision. Check the oriented leaf, including
    // when the door has handed its collision over to a destructible.
    const auto CrossesDoor = [](const FClosedDoor& Door, const FVector& From, const FVector& To)
    {
        const FTransform& Transform = Door.Mesh->GetComponentTransform();
        const FVector Start = Transform.InverseTransformPosition(From);
        const FVector End = Transform.InverseTransformPosition(To);
        return FMath::LineBoxIntersection(Door.Mesh->GetStaticMesh()->GetBoundingBox(), Start, End, End - Start);
    };

    // Resolve a physical direct path first, including transmission. A coarse room graph
    // or an exhausted optional route search cannot discard this already measured path.
    FRetakeAcousticMaterial DirectMaterial = Data->Regions[Key.SourceRegion].InternalTransmission;
    const auto& ListenerMaterial = Data->Regions[Key.ListenerRegion].InternalTransmission;
    DirectMaterial.Transmission = FMath::Min(DirectMaterial.Transmission, ListenerMaterial.Transmission);
    DirectMaterial.LowPassHz = FMath::Min(DirectMaterial.LowPassHz, ListenerMaterial.LowPassHz);
    const auto Direct = ResolveStructure(Source, Listener, DirectMaterial);
    Result.Gain = Profile.GainAtDistance(Distance) * Direct.Transmission;
    Result.LowPassHz = Direct.LowPassHz;
    Result.DistanceCm = Distance;
    for (const FClosedDoor& Door : ClosedDoors)
    {
        if (!CrossesDoor(Door, Source, Listener)) continue;
        const auto Material = Data->Connections[Door.Connection].ClosedMaterial.ForProfile(Profile);
        Result.Gain *= Material.Transmission;
        Result.LowPassHz = FMath::Min(Result.LowPassHz, Material.LowPassHz);
    }
    if (Result.Gain < Profile.GainAtDistance(Distance))
    {
        const auto Indirect = RetakeAcousticGraph::ResolveWithTransmission(*Data, OpenStates, Source, Listener, Profile,
            [&](int32 Region, int32 FromConnection, int32 ToConnection)
            {
                const FVector& From = FromConnection == INDEX_NONE ? Source : Data->Connections[FromConnection].Position;
                const FVector& To = ToConnection == INDEX_NONE ? Listener : Data->Connections[ToConnection].Position;
                auto Segment = ResolveStructure(From, To, Data->Regions[Region].InternalTransmission);
                const auto* FromDoor = DoorForConnection(FromConnection);
                const auto* ToDoor = DoorForConnection(ToConnection);
                // A traversed door already contributes its transmission at the graph edge.
                for (const FClosedDoor& Door : ClosedDoors)
                {
                    if (Door.Actor == FromDoor || Door.Actor == ToDoor || !CrossesDoor(Door, From, To)) continue;
                    const auto Material = Data->Connections[Door.Connection].ClosedMaterial.ForProfile(Profile);
                    Segment.Transmission *= Material.Transmission;
                    Segment.LowPassHz = FMath::Min(Segment.LowPassHz, Material.LowPassHz);
                }
                return Segment;
            }, Result.Gain);
        if (Indirect.Gain > Result.Gain) Result = Indirect;
        else Result.bSearchLimitReached |= Indirect.bSearchLimitReached;
    }
    Result.bSearchLimitReached |= bTraceLimitReached;
    // Bounded cache storage, including denied paths. No scene search or allocation runs on an audio thread.
    if (Cache.Num() >= 4096) Cache.Reset();
    Cache.Add(Key, FCachedPath{Now + .1, Result});
    return Result;
}
