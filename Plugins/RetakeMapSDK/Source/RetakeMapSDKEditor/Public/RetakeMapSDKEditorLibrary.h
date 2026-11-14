// Copyright 2026 Codnyx. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RetakeMapSDKEditorLibrary.generated.h"

class AActor;
class UActorComponent;
class USceneComponent;
class UNiagaraSystem;
class UAnimSequence;
class UTexture2D;
class URetakeWeaponPoseBasis;
class UWorld;
struct FRetakeModMap;

/** Editor scripting for Map Kit tooling: what a creator does by hand in the editor, for scripts that run without one. */
UCLASS()
class RETAKEMAPSDKEDITOR_API URetakeMapSDKEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Builds metadata for the currently loaded source map using the exporter's mode/presentation checks.
     * Does not load another world, modify assets, or manufacture an installed/exported revision. */
    static bool BuildMapPreviewMetadata(const UWorld& World, FRetakeModMap& OutMap, FString& OutError);

    /** Replace optional Interchange metadata with native import records on an owned texture.
     * Preserves source filenames and texture data. Returns an error or empty string. Caller saves the asset. */
    UFUNCTION(BlueprintCallable, Category="Retake|Mods")
    static FString NormalizeTextureImportMetadata(UTexture2D* Texture, const FString& OwnerModId);

    /** Exact native identity of the declared shared body; empty if invalid. Does not modify assets. */
    UFUNCTION(BlueprintCallable, Category="Retake|Mods|Weapons")
    static FString GetWeaponAuthoringBodyIdentity(URetakeWeaponPoseBasis* Basis);

    /** Restore the published mesh/skeleton reference after a lossy source-file import. Only mod-owned,
     * single-LOD authoring meshes within a strict import-rounding envelope are accepted; no topology changes.
     * Updates editable mesh-description poses and the separate animation reference. Does not save or relax bake checks. */
    UFUNCTION(BlueprintCallable, Category="Retake|Mods|Weapons")
    static FString NormalizeWeaponAuthoringReference(URetakeWeaponPoseBasis* Basis, const FString& OwnerModId);

    /**
     * Adds a component of Class named Name to a placed actor, as the details panel's Add Component does: an instance component,
     * saved with the level. A scene component is attached under Parent, or under the root when Parent is null. Null when the
     * actor is not in an editor world, the class is abstract or the name is taken (traversal ledges are found by name).
     */
    UFUNCTION(BlueprintCallable, Category="Retake|Mods", meta=(DeterminesOutputType="Class"))
    static UActorComponent* AddInstanceComponent(AActor* Actor, TSubclassOf<UActorComponent> Class, FName Name, USceneComponent* Parent);

    /**
     * Gives every pawn-blocking static mesh of a traversal obstacle actor a simple box fitted to the mesh bounds and
     * Use Simple and Complex, so pawn sweeps land on a flat top at the ledge line while shots keep hitting the triangles.
     * Only meshes owned by OwnerModId without simple collision change; the result lists each mesh and the obstacle's
     * remaining configuration error, or the reason nothing changed.
     */
    UFUNCTION(BlueprintCallable, Category="Retake|Mods")
    static FString RepairTraversalCollision(AActor* Actor, const FString& OwnerModId);

    /** Raw frame-index pose hash and timing diagnostics. Does not evaluate gameplay, modify or save assets. */
    UFUNCTION(BlueprintCallable, Category="Retake|Mods")
    static FString InspectAnimationFrames(UAnimSequence* Animation);

    /** Finish any queued compilation for an owned animation before editing its source keys or sample rate. Does not save. */
    UFUNCTION(BlueprintCallable, Category="Retake|Mods")
    static FString FinishAnimationCompilation(UAnimSequence* Animation, const FString& OwnerModId);

    /** Change sample timing on a saved mod-owned raw clip without moving frame keys. Rejects timed events/curves/attributes/additive clips; does not save. */
    UFUNCTION(BlueprintCallable, Category="Retake|Mods")
    static bool RetimeAnimationFrames(UAnimSequence* Animation, const FString& OwnerModId, int32 RateNumerator, int32 RateDenominator, FString& OutError);

    /** Write one rotation per authored key on an existing mod-owned FK track, using matrix-derived Euler channels.
     * Preserves positions/scales, verifies every key and restores channels on failure. Empty string means success; does not save. */
    UFUNCTION(BlueprintCallable, Category="Retake|Mods")
    static FString SetAnimationBoneRotationKeys(UAnimSequence* Animation, const FString& OwnerModId, FName BoneName, const TArray<FQuat>& Rotations);

    /** Safe retirement of an unreferenced owned preparation asset. Uses normal editor deletion and verifies the package file is gone.
     * Empty string means success. Refuses other asset classes, incoming references and PIE; never force-replaces references. */
    UFUNCTION(BlueprintCallable, Category="Retake|Mods|Weapons")
    static FString RetireWeaponPoseContribution(const FString& OwnerModId, const FString& ObjectPath);

    /** Native compile barrier and diagnostic JSON. Does not simulate or save; editor readiness is not packaged/runtime acceptance. */
    UFUNCTION(BlueprintCallable, Category="Retake|Mods|Effects")
    static FString InspectNiagaraSystem(UNiagaraSystem* System);

    /** Install the typed emission-history parameters on an owned system. Returns an error or empty string; does not save.
     * Graph authoring still connects the array through Niagara's built-in indexed selector. */
    UFUNCTION(BlueprintCallable, Category="Retake|Mods|Effects")
    static FString ConfigureSmokeEmissionInterface(UNiagaraSystem* System, const FString& OwnerModId);

    /** Explicitly detach emitter parents in a mod-owned system, preserving its merged graphs. Does not save. */
    UFUNCTION(BlueprintCallable, Category="Retake|Mods|Effects")
    static bool DetachNiagaraEmitterParents(UNiagaraSystem* System, const FString& OwnerModId, FString& OutError);
};
