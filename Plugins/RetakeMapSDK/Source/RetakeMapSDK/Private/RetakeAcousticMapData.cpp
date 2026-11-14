// Copyright 2026 Codnyx. All Rights Reserved.
#include "RetakeAcousticMapData.h"
#include "Misc/DataValidation.h"

bool FRetakeAcousticMaterial::IsValid() const
{
    return FMath::IsFinite(Transmission) && Transmission >= 0 && Transmission <= 1
        && FMath::IsFinite(LowPassHz) && LowPassHz >= 20 && LowPassHz <= 20000;
}

FRetakeAcousticMaterial FRetakeAcousticMaterial::ForProfile(const FRetakeAudioPropagationProfile& Profile) const
{
    if (Profile.ObstructionStrength == 1.f) return *this;
    FRetakeAcousticMaterial Result;
    Result.Transmission = FMath::Pow(Transmission, Profile.ObstructionStrength);
    Result.LowPassHz = FMath::Clamp(20000.f * FMath::Pow(LowPassHz / 20000.f, Profile.ObstructionStrength), 20.f, 20000.f);
    return Result;
}

bool FRetakeAcousticRegionData::Contains(const FVector& Position) const
{
    const FVector Local = Transform.InverseTransformPosition(Position);
    return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}

int32 URetakeAcousticMapData::FindRegion(const FVector& Position) const
{
    if (Position.ContainsNaN()) return INDEX_NONE;
    int32 Found = INDEX_NONE;
    double Volume = TNumericLimits<double>::Max();
    for (int32 Index = 0; Index < Regions.Num(); ++Index)
    {
        const auto& Region = Regions[Index];
        if (!Region.Contains(Position)) continue;
        const double CandidateVolume = Region.Extent.X * Region.Extent.Y * Region.Extent.Z * FMath::Abs(Region.Transform.GetDeterminant());
        if (Found == INDEX_NONE || Region.Priority > Regions[Found].Priority
            || (Region.Priority == Regions[Found].Priority && CandidateVolume < Volume))
        { Found = Index; Volume = CandidateVolume; }
    }
    return Found;
}

bool URetakeAcousticMapData::Validate(FString& Error) const
{
    if (Version != FormatVersion || MapPackage.IsEmpty() || SourceSignature.Len() != 40
        || Regions.IsEmpty() || Regions.Num() > MaximumRegions || Connections.Num() > MaximumConnections)
    { Error = TEXT("Missing, stale, unsupported or oversized acoustic map bake."); return false; }
    TSet<FName> Ids;
    for (int32 RegionIndex = 0; RegionIndex < Regions.Num(); ++RegionIndex)
    {
        const auto& Region = Regions[RegionIndex];
        const FVector Scale = Region.Transform.GetScale3D();
        if (Region.Id.IsNone() || Ids.Contains(Region.Id) || Region.Transform.ContainsNaN() || Region.Extent.ContainsNaN()
            || Region.Extent.GetMin() <= 0 || Scale.GetAbs().GetMin() <= UE_SMALL_NUMBER || !Region.InternalTransmission.IsValid()
            || !FMath::IsFinite(Region.ReverbDecaySeconds) || Region.ReverbDecaySeconds < .1f || Region.ReverbDecaySeconds > 5.f)
        { Error = TEXT("Invalid or duplicate acoustic region."); return false; }
        Ids.Add(Region.Id);
        TSet<int32> Links;
        for (int32 Index : Region.Connections)
        {
            if (!Connections.IsValidIndex(Index) || Links.Contains(Index)
                || (Connections[Index].RegionA != RegionIndex && Connections[Index].RegionB != RegionIndex))
            { Error = TEXT("Invalid acoustic region adjacency index."); return false; }
            Links.Add(Index);
        }
    }
    Ids.Reset();
    for (int32 ConnectionIndex = 0; ConnectionIndex < Connections.Num(); ++ConnectionIndex)
    {
        const auto& Connection = Connections[ConnectionIndex];
        if (Connection.Id.IsNone() || Ids.Contains(Connection.Id) || Connection.RegionA == Connection.RegionB
            || !Regions.IsValidIndex(Connection.RegionA) || !Regions.IsValidIndex(Connection.RegionB)
            || Connection.Position.ContainsNaN() || !Connection.OpenMaterial.IsValid() || !Connection.ClosedMaterial.IsValid())
        { Error = TEXT("Invalid or duplicate acoustic connection."); return false; }
        Ids.Add(Connection.Id);
        if (!Regions[Connection.RegionA].Connections.Contains(ConnectionIndex) || !Regions[Connection.RegionB].Connections.Contains(ConnectionIndex))
        { Error = TEXT("Acoustic connection is missing from its region adjacency."); return false; }
    }
    return true;
}

FRetakeAcousticPathResult RetakeAcousticGraph::Resolve(const URetakeAcousticMapData& Map, TConstArrayView<uint8> OpenStates,
    const FVector& Source, const FVector& Listener, const FRetakeAudioPropagationProfile& Profile)
{
    return Resolve(Map, OpenStates, Source, Listener, Profile, [](int32, int32) { return true; });
}

FRetakeAcousticPathResult RetakeAcousticGraph::Resolve(const URetakeAcousticMapData& Map, TConstArrayView<uint8> OpenStates,
    const FVector& Source, const FVector& Listener, const FRetakeAudioPropagationProfile& Profile,
    TFunctionRef<bool(int32, int32)> IsSegmentClear)
{
    return ResolveWithTransmission(Map, OpenStates, Source, Listener, Profile,
        [&](int32, int32 From, int32 To)
        {
            FRetakeAcousticMaterial Segment;
            Segment.Transmission = IsSegmentClear(From, To) ? 1.f : 0.f;
            Segment.LowPassHz = 20000.f;
            return Segment;
        });
}

FRetakeAcousticPathResult RetakeAcousticGraph::ResolveWithTransmission(const URetakeAcousticMapData& Map,
    TConstArrayView<uint8> OpenStates, const FVector& Source, const FVector& Listener,
    const FRetakeAudioPropagationProfile& Profile,
    TFunctionRef<FRetakeAcousticMaterial(int32, int32, int32)> ResolveSegment, float MinimumGain)
{
    FRetakeAcousticPathResult Result;
    const int32 Start = Map.FindRegion(Source), Finish = Map.FindRegion(Listener);
    if (Start == INDEX_NONE || Finish == INDEX_NONE || OpenStates.Num() != Map.Connections.Num()
        || FVector::DistSquared(Source, Listener) >= FMath::Square(Profile.RangeCm)) return Result;
    struct FNode
    {
        int32 Region = INDEX_NONE;
        int32 Arrival = INDEX_NONE;
        int32 Previous = INDEX_NONE;
        float Distance = 0, Transmission = 1, LowPass = 20000, UpperGain = 1;
        int32 Hops = 0;
        int32 Visited[MaximumHops + 1]{};
    };
    TArray<FNode, TInlineAllocator<MaximumSearchNodes>> Pending;
    FNode Root; Root.Region = Start; Root.Visited[0] = Start;
    Pending.Add(Root);
    int32 Expanded = 0, Alternatives = 0;
    // Check the best candidates first. Shared segments can be reused by several routes.
    struct FCheckedSegment { int32 Region, From, To; FRetakeAcousticMaterial Material; };
    TArray<FCheckedSegment, TInlineAllocator<MaximumSegmentChecks>> CheckedSegments;
    const auto CheckSegment = [&](int32 Region, int32 From, int32 To)
    {
        for (const auto& Segment : CheckedSegments)
        {
            if (Segment.Region == Region && Segment.From == From && Segment.To == To)
                return Segment.Material;
        }
        if (CheckedSegments.Num() == MaximumSegmentChecks)
        {
            Result.bSearchLimitReached = true;
            FRetakeAcousticMaterial Blocked;
            Blocked.Transmission = 0.f;
            return Blocked;
        }
        const auto Material = ResolveSegment(Region, From, To);
        CheckedSegments.Add({Region, From, To, Material});
        return Material;
    };
    const auto BestGain = [&]() { return FMath::Max3(.001f, MinimumGain, Result.Gain); };
    while (!Pending.IsEmpty() && Expanded < MaximumSearchNodes && Alternatives < MaximumAlternatives)
    {
        int32 Best = 0;
        for (int32 Index = 1; Index < Pending.Num(); ++Index)
            if (Pending[Index].UpperGain > Pending[Best].UpperGain) Best = Index;
        FNode Node = Pending[Best]; Pending.RemoveAtSwap(Best, EAllowShrinking::No);
        ++Expanded;
        if (Node.UpperGain <= BestGain()) break;
        if (Node.Hops > 0)
        {
            const auto Segment = CheckSegment(Node.Visited[Node.Hops - 1], Node.Previous, Node.Arrival);
            Node.Transmission *= Segment.Transmission;
            Node.LowPass = FMath::Min(Node.LowPass, Segment.LowPassHz);
        }
        const FVector& Position = Node.Arrival == INDEX_NONE ? Source : Map.Connections[Node.Arrival].Position;
        Node.UpperGain = Profile.GainAtDistance(Node.Distance + FVector::Distance(Position, Listener)) * Node.Transmission;
        if (Node.UpperGain <= BestGain()) continue;
        if (Node.Region == Finish && Node.Hops > 0)
        {
            const auto Segment = CheckSegment(Finish, Node.Arrival, INDEX_NONE);
            const float Distance = Node.Distance + FVector::Distance(Position, Listener);
            const float Gain = Profile.GainAtDistance(Distance) * Node.Transmission * Segment.Transmission;
            if (Gain > BestGain())
            {
                ++Alternatives;
                Result.Gain = Gain; Result.LowPassHz = FMath::Min(Node.LowPass, Segment.LowPassHz);
                Result.DistanceCm = Distance; Result.Connections = Node.Hops;
            }
            continue;
        }
        if (Node.Hops == MaximumHops) continue;
        for (int32 Index : Map.Regions[Node.Region].Connections)
        {
            const auto& Edge = Map.Connections[Index];
            const int32 Next = Edge.RegionA == Node.Region ? Edge.RegionB : Edge.RegionB == Node.Region ? Edge.RegionA : INDEX_NONE;
            if (Next == INDEX_NONE || Index == Node.Arrival) continue;
            bool Seen = false;
            for (int32 Hop = 0; Hop <= Node.Hops; ++Hop) Seen |= Node.Visited[Hop] == Next;
            // A route between two points in one region can leave and return via different openings.
            if (Seen && Next != Finish) continue;
            const auto Material = (OpenStates[Index] ? Edge.OpenMaterial : Edge.ClosedMaterial).ForProfile(Profile);
            FNode Candidate = Node;
            Candidate.Region = Next;
            Candidate.Previous = Node.Arrival;
            Candidate.Arrival = Index;
            Candidate.Distance += FVector::Distance(Position, Edge.Position);
            Candidate.Transmission *= Material.Transmission;
            Candidate.LowPass = FMath::Min(Candidate.LowPass, Material.LowPassHz);
            Candidate.Visited[++Candidate.Hops] = Next;
            Candidate.UpperGain = Profile.GainAtDistance(Candidate.Distance + FVector::Distance(Edge.Position, Listener)) * Candidate.Transmission;
            if (Candidate.UpperGain <= BestGain()) continue;
            if (Pending.Num() == MaximumSearchNodes) { Result.bSearchLimitReached = true; continue; }
            Pending.Add(Candidate);
        }
    }
    Result.bSearchLimitReached |= Expanded == MaximumSearchNodes && !Pending.IsEmpty();
    return Result;
}

#if WITH_EDITOR
EDataValidationResult URetakeAcousticMapData::IsDataValid(FDataValidationContext& Context) const
{
    FString Error;
    if (!Validate(Error)) { Context.AddError(FText::FromString(Error)); return EDataValidationResult::Invalid; }
    return EDataValidationResult::Valid;
}
#endif
