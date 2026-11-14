// Copyright 2026 Codnyx. All Rights Reserved.
#include "RetakeAudioCueDefinition.h"
#include "Sound/SoundWave.h"
#include "Misc/DataValidation.h"

bool FRetakeAudioPropagationProfile::Validate(FString& Error) const
{
    if (!FMath::IsFinite(RangeCm) || RangeCm <= 0 || RangeCm > 655350.f
        || !FMath::IsFinite(FullGainRadiusCm) || FullGainRadiusCm < 0 || FullGainRadiusCm >= RangeCm
        || !FMath::IsFinite(FalloffExponent) || FalloffExponent < 1 || FalloffExponent > 4
        || !FMath::IsFinite(ReverbSend) || ReverbSend < 0 || ReverbSend > 1
        || !FMath::IsFinite(ObstructionStrength) || ObstructionStrength < .1f || ObstructionStrength > 4.f)
    { Error = TEXT("Invalid acoustic range, full-gain radius, falloff, reverb send or obstruction strength."); return false; }
    if (!FalloffPoints.IsEmpty())
    {
        if (FalloffPoints.Num() < 2 || FalloffPoints.Num() > 16
            || FalloffPoints[0].DistanceFraction != 0.f || FalloffPoints[0].Gain != 1.f
            || FalloffPoints.Last().DistanceFraction != 1.f || FalloffPoints.Last().Gain != 0.f)
        { Error = TEXT("An authored falloff needs 2 to 16 points from (0,1) to (1,0)."); return false; }
        for (int32 Index = 1; Index < FalloffPoints.Num(); ++Index)
        {
            const auto& Previous = FalloffPoints[Index - 1];
            const auto& Point = FalloffPoints[Index];
            if (!FMath::IsFinite(Point.DistanceFraction) || !FMath::IsFinite(Point.Gain)
                || Point.DistanceFraction <= Previous.DistanceFraction || Point.Gain < 0.f || Point.Gain > Previous.Gain)
            { Error = TEXT("Falloff distances must strictly increase and finite gains must not increase."); return false; }
        }
    }
    return true;
}

float FRetakeAudioPropagationProfile::GainAtDistance(float Distance) const
{
    if (!FMath::IsFinite(Distance) || Distance < 0 || Distance >= RangeCm || RangeCm <= FullGainRadiusCm) return 0.f;
    if (Distance <= FullGainRadiusCm) return 1.f;
    const float Fraction = (Distance - FullGainRadiusCm) / (RangeCm - FullGainRadiusCm);
    if (FalloffPoints.IsEmpty()) return FMath::Pow(1.f - Fraction, FalloffExponent);
    for (int32 Index = 1; Index < FalloffPoints.Num(); ++Index)
    {
        const auto& Point = FalloffPoints[Index];
        if (Fraction <= Point.DistanceFraction)
        {
            const auto& Previous = FalloffPoints[Index - 1];
            return FMath::Lerp(Previous.Gain, Point.Gain,
                (Fraction - Previous.DistanceFraction) / (Point.DistanceFraction - Previous.DistanceFraction));
        }
    }
    return 0.f;
}

bool FRetakeAudioRecording::HasValidVariation() const
{
    return FMath::IsFinite(VolumeMinimum) && FMath::IsFinite(VolumeMaximum)
        && VolumeMinimum>=0.f && VolumeMaximum>=VolumeMinimum && VolumeMaximum<=4.f
        && FMath::IsFinite(PitchMinimum) && FMath::IsFinite(PitchMaximum)
        && PitchMinimum>=.125f && PitchMaximum>=PitchMinimum && PitchMaximum<=4.f;
}

void FRetakeAudioRecording::ResolveVariation(FRandomStream& Random,float& OutVolume,float& OutPitch) const
{
    OutVolume=VolumeMinimum==VolumeMaximum ? VolumeMinimum : Random.FRandRange(VolumeMinimum,VolumeMaximum);
    OutPitch=PitchMinimum==PitchMaximum ? PitchMinimum : Random.FRandRange(PitchMinimum,PitchMaximum);
}

bool URetakeAudioCueDefinition::Validate(FString& Error) const
{
    if (CueId < 1 || CueId > 65535 || SemanticName.IsNone() || uint8(Category) >= uint8(ERetakeAudioCategory::Count))
    { Error = TEXT("An audio cue requires a stable ID (1..65535), semantic name and valid category."); return false; }
    if (Recordings.IsEmpty()) { Error = TEXT("Audio cue has no replacement recordings."); return false; }
    if (bSpatial && !Propagation.Validate(Error)) return false;
    if (!FMath::IsFinite(Priority) || Priority < 1 || Priority > 100 || MaximumInstances < 1 || MaximumInstances > 64
        || !FMath::IsFinite(GainDb) || GainDb < -60 || GainDb > 12
        || !FMath::IsFinite(PitchVariationSemitones) || PitchVariationSemitones < 0 || PitchVariationSemitones > 2)
    { Error = TEXT("Invalid cue priority, concurrency, gain or pitch variation."); return false; }
    for (const auto& Recording : Recordings)
    {
        if (Recording.Wave.IsNull() || Recording.SourceId.IsNone() || !FMath::IsFinite(Recording.GainDb)
            || Recording.GainDb < -60 || Recording.GainDb > 12 || !Recording.HasValidVariation())
        { Error = TEXT("Every recording needs a wave, source-manifest identity and valid gain/volume/pitch ranges."); return false; }
        if (const USoundWave* Wave = Recording.Wave.Get())
        {
            if (Wave->GetClass()!=USoundWave::StaticClass())
            { Error = TEXT("Recording slots require recorded SoundWave assets; procedural sources belong in Generator."); return false; }
            if (bSpatial && Wave->NumChannels != 1)
            { Error = TEXT("Positional recordings must be authored mono; runtime downmix is not supported."); return false; }
            if (Wave->bLooping != bLooping)
            { Error = TEXT("Recording loop state does not match the cue."); return false; }
        }
    }
    return true;
}

bool URetakeAudioLibrary::Validate(FString& Error) const
{
    if (Revision < 1 || Cues.IsEmpty()) { Error = TEXT("Audio catalogue is empty or unversioned."); return false; }
    TSet<int32> Ids; TSet<FName> Names;
    for (const URetakeAudioCueDefinition* Cue : Cues)
    {
        if (!Cue || !Cue->Validate(Error)) { if (!Cue) Error = TEXT("Audio catalogue contains a null cue."); return false; }
        if (Ids.Contains(Cue->CueId) || Names.Contains(Cue->SemanticName))
        { Error = TEXT("Audio catalogue contains duplicate IDs or semantic names."); return false; }
        Ids.Add(Cue->CueId); Names.Add(Cue->SemanticName);
    }
    return true;
}

#if WITH_EDITOR
EDataValidationResult URetakeAudioCueDefinition::IsDataValid(FDataValidationContext& Context) const
{
    FString Error;
    if (!Validate(Error)) { Context.AddError(FText::FromString(Error)); return EDataValidationResult::Invalid; }
    // Validation intentionally loads the recordings; runtime playback must prepare them asynchronously.
    for (const auto& Recording : Recordings)
    {
        const USoundWave* Wave = Recording.Wave.LoadSynchronous();
        if (!Wave || Wave->GetClass()!=USoundWave::StaticClass() || (bSpatial && Wave->NumChannels != 1) || Wave->bLooping != bLooping)
        { Context.AddError(FText::FromString(TEXT("Missing recording, invalid channel count or mismatched loop state."))); return EDataValidationResult::Invalid; }
    }
    return EDataValidationResult::Valid;
}
EDataValidationResult URetakeAudioLibrary::IsDataValid(FDataValidationContext& Context) const
{
    FString Error;
    if (!Validate(Error)) { Context.AddError(FText::FromString(Error)); return EDataValidationResult::Invalid; }
    return EDataValidationResult::Valid;
}
#endif
