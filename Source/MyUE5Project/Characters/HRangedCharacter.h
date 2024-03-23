
#pragma once

#include "AkComponent.h"
#include "AkGameplayTypes.h"
#include "CoreMinimal.h"
#include "GameFramework/Character.h"

#include "HRangedCharacter.generated.h"

class UInputMappingContext;
class UParticleSystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFireWeapon, bool, bLastShot);

UCLASS()
class MYUE5PROJECT_API AHRangedCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AHRangedCharacter();

protected:

	// COMPONENTS

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	UAkComponent* AkWeaponAudioComponent;

	// PRIMARY ATTACK

	// The separation between each bullet instance in Milliseconds
	UPROPERTY(EditAnywhere, Category = "PrimaryAttack")
	float FireRateMilliseconds = 1000;
	// How many bullets to fire per burst
	UPROPERTY(EditAnywhere, Category = "PrimaryAttack")
	int32 NumberOfInstancesPerBurst = 1;
	// The name of the Weapon Socket
	UPROPERTY(EditDefaultsOnly, Category = "PrimaryAttack")
	FName FireSocketName = "MuzzleStationary";

	// Wwise Events

	UPROPERTY(EditAnywhere, Category = "PrimaryAttack")
	UAkAudioEvent* GunshotOnMidiAudioEvent;
	UPROPERTY(EditAnywhere, Category = "PrimaryAttack")
	UAkAudioEvent* GunshotTailAudioEvent;
	UPROPERTY(EditAnywhere, Category = "PrimaryAttack")
	UAkAudioEvent* BulletHitAudioEvent;
    // The current playing ID
	UPROPERTY(VisibleAnywhere, Category = "PrimaryAttack")
	int32 CurrentPlayingID = 0;

	// FX

	UPROPERTY(EditDefaultsOnly, Category = "PrimaryAttack|FX")
	UParticleSystem* PrimaryAttackHitWorld;
	UPROPERTY(EditDefaultsOnly, Category = "PrimaryAttack|FX")
	UParticleSystem* PrimaryAttackMuzzleFlash;

	// PRIMARY ATTACK - FUNCTIONS

	// Start the firing action
	UFUNCTION(BlueprintCallable)
	void FireBurstOnMIDI(); // [1]

	/**
	 * Required AkCallback Static Function. Passed as reference to AK::SoundEngine::PostMIDIOnEvent()
	 * This function can't have the UFUNCTION macro because its parameters are not UOBJECTS
	 * C++ Only
	 * @param CallbackType Callback Type
	 * @param CallbackInfo Callback Info Structure
	 */
	static void OnPostMidiCallback(AkCallbackType CallbackType, AkCallbackInfo* CallbackInfo); // [2]

	/**
	 * Bounded to OnFireWeapon in AHRangedCharacter::PostInitializeComponents()
	 * @param bLastShot Call AHRangedCharacter::FireShotTail() if true
	 */
	UFUNCTION()
	void HandleOnFireWeapon(bool bLastShot); // [3]

	// Posts the GunshotTailAudioEvent
	UFUNCTION()
	void FireShotTail() const; // [4]

public:

	/**
	 * Delegate called from AHRangedCharacter::OnPostMidiCallback()
	 * Invoked on the Audio Thread, but runs on the Game Thread
	 */
	UPROPERTY()
	FOnFireWeapon OnFireWeapon;

protected:

	// INPUT

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	UInputMappingContext* InputMappingContext;

	void SetupInputMappingContext() const;

	// GAMEPLAY - LIFETIME

	virtual void BeginPlay() override;
	virtual void PostInitializeComponents() override;

	// This function is used to do some cleanup when this actor is destroyed
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
