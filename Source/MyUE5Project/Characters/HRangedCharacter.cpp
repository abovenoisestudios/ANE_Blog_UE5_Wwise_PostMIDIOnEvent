


#include "HRangedCharacter.h"

#include "AkAudioEvent.h"
#include "AkGameplayStatics.h"
#include "EnhancedInputSubsystems.h"
#include "Kismet/GameplayStatics.h"
#include "Wwise/API/WwiseSoundEngineAPI.h"

AHRangedCharacter::AHRangedCharacter()
{
	AkWeaponAudioComponent = CreateDefaultSubobject<UAkComponent>("Weapon Audio Component");
	AkWeaponAudioComponent->SetupAttachment(GetMesh(), FireSocketName);

	GetMesh()->SetRelativeLocation(FVector(0, 0, -90));
	GetMesh()->SetRelativeRotation(FRotator(0, -90, 0));

	PrimaryActorTick.bCanEverTick = true;
}

void AHRangedCharacter::FireBurstOnMIDI()
{
	auto* SoundEngine = IWwiseSoundEngineAPI::Get(); // Get the Wwise Sound Engine

	if (!SoundEngine || !SoundEngine->IsInitialized()) { return; } // Safety check

	AkAudioSettings AudioSettings;
	SoundEngine->GetAudioSettings(AudioSettings); // Gets the current settings in the audio device

	const AkUInt32 SamplesPerSecond = AudioSettings.uNumSamplesPerSecond; // Gets how many audio samples per second in the audio device (Usually 48000)
	const AkUInt64 SamplesPerMillisecond = SamplesPerSecond / 1000; // Calculates how many samples per millisecond
	const AkUInt64 OffsetSamples = FireRateMilliseconds * SamplesPerMillisecond; // Calculate how many samples will separate each MIDI Post (Trigger Rate)

	const AkUInt32 EventUniqueID = GunshotOnMidiAudioEvent->GetShortID();
	const AkGameObjectID AkGameObjectID = AkWeaponAudioComponent->GetAkGameObjectID();

	// Allocate [n] number of AkMidiPost in an array. Using Unreal TSharedPtr<> for memory management
	const TSharedPtr<AkMIDIPost> MidiPosts(new AkMIDIPost[NumberOfInstancesPerBurst]);
	AkUInt64 InstanceOffset = 0; // The first instance will be posted as soon as the function is called

	// Prepare all the Midi Posts allocated in the MidiPosts array
	for (size_t i = 0; i < NumberOfInstancesPerBurst; i++)
	{
		const AkUInt8 LAST_SHOT_MIDI_VELOCITY = 30;
		const AkUInt8 SHOT_MIDI_VELOCITY = 127;

		// If this is the last shot, set its MIDI velocity to 30 so the callback triggers the tail sound
		MidiPosts.Get()[i].NoteOnOff.byVelocity = i == (NumberOfInstancesPerBurst - 1) ? LAST_SHOT_MIDI_VELOCITY : SHOT_MIDI_VELOCITY;
		MidiPosts.Get()[i].byType = AK_MIDI_EVENT_TYPE_NOTE_ON;
		MidiPosts.Get()[i].byChan = 0;
		MidiPosts.Get()[i].uOffset = InstanceOffset;
		InstanceOffset += OffsetSamples;
	}

	/*
	 * 1. Post the Midi sequence using the EventUniqueID on the AudioComponent
	 * 2. Bind the OnPostMidiCallback callback.
	 * 3. Send the OnMidiEvent delegate as a pCookie
	 * 4. Saves the current PlayingID in case it need to be stopped
	 */
	CurrentPlayingID = SoundEngine->PostMIDIOnEvent(EventUniqueID, AkGameObjectID, MidiPosts.Get(),
		NumberOfInstancesPerBurst, false, AK_MIDIEvent | AK_EndOfEvent, &OnPostMidiCallback, &OnFireWeapon);
}

void AHRangedCharacter::OnPostMidiCallback(AkCallbackType CallbackType, AkCallbackInfo* CallbackInfo)
{
	if (CallbackType != AK_MIDIEvent) { return; }

	const AkMIDIEventCallbackInfo* MidiCallbackInfo = static_cast<AkMIDIEventCallbackInfo*>(CallbackInfo);
	if (MidiCallbackInfo->midiEvent.byType == AK_MIDI_EVENT_TYPE_NOTE_ON)
	{
		// MidiCallbackInfo is not a UOBJECT so it can't use Cast<>() to cast to a FOnFireWeapon type. Native C++ static_cast<>() is required here.
		// It is important to know which type of data is sent as a pCookie from the AK::SoundEngine::PostMIDIOnEvent()
		if (const auto* OnFireWeaponDelegate = static_cast<FOnFireWeapon*>(MidiCallbackInfo->pCookie))
		{
			// Check if its the last shot
			bool bLastShot = MidiCallbackInfo->midiEvent.NoteOnOff.byVelocity < 127;

			/*
			 * IMPORTANT!
			 * This callback runs on the Audio Thread. It uses a Lambda expression.
			 * Using this AsyncTask makes sure that any function that binds to this delegate (OnFireWeaponDelegate) runs on the Game Thread.
			 * Animations and Particle Effects will crash the game if they are called from outside the Game Thread.
			 */
			AsyncTask(ENamedThreads::GameThread, [OnFireWeaponDelegate = MoveTempIfPossible(OnFireWeaponDelegate), bLastShot]() mutable
			{
				OnFireWeaponDelegate->Broadcast(bLastShot);
			});
		}
	}
}

void AHRangedCharacter::HandleOnFireWeapon(bool bLastShot)
{
	if (FireSocketName.IsNone()) { return; }
	if (bLastShot) { FireShotTail(); }

	FHitResult OutHit;
	constexpr float MaxBulletSpread = 5.f;

	// Create a random fire direction
	FRotator WeaponRotation = GetMesh()->GetSocketRotation(FireSocketName);
	WeaponRotation.Pitch += FMath::RandRange(-MaxBulletSpread, MaxBulletSpread);
	WeaponRotation.Yaw += FMath::RandRange(-MaxBulletSpread, MaxBulletSpread);

	// Trace from the weapon socket and apply the random rotation to the end
	const FVector LineStart = GetMesh()->GetSocketLocation(FireSocketName);
	const FVector LineEnd = LineStart + WeaponRotation.Vector() * 900.f;
	const FLinearColor DebugColor = FLinearColor::MakeRandomColor();

	// Fire the line trace
	bool bHit = UKismetSystemLibrary::LineTraceSingle(this, LineStart, LineEnd,
		UEngineTypes::ConvertToTraceType(ECC_Visibility),false,
		TArray<AActor*>(),EDrawDebugTrace::ForDuration,OutHit, true, DebugColor, DebugColor,0.75f);

	// Spawn the VFX and Impact Audio Event
	if (PrimaryAttackMuzzleFlash)
	{
		UGameplayStatics::SpawnEmitterAtLocation(this, PrimaryAttackMuzzleFlash, LineStart);
	}
	if (bHit && PrimaryAttackHitWorld && BulletHitAudioEvent)
	{
		UGameplayStatics::SpawnEmitterAtLocation(this, PrimaryAttackHitWorld, OutHit.ImpactPoint);
		UAkGameplayStatics::PostEventAtLocation(BulletHitAudioEvent, OutHit.ImpactPoint, FRotator::ZeroRotator, this);
	}
}

void AHRangedCharacter::FireShotTail() const
{
	if (!GunshotTailAudioEvent) { return; }

	AkWeaponAudioComponent->PostAkEvent(GunshotTailAudioEvent);
}

void AHRangedCharacter::SetupInputMappingContext() const
{
	const auto* PlayerController = Cast<APlayerController>(Controller);
	if (!IsValid(PlayerController)) { return; }

	auto* EnhancedInputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer());
	EnhancedInputSubsystem->AddMappingContext(InputMappingContext, 0);
}

void AHRangedCharacter::BeginPlay()
{
	Super::BeginPlay();
	SetupInputMappingContext();
}

void AHRangedCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Binds the OnFireWeapon Delegate to HRangedCharacter::HandleOnFireWeapon to perform line traces, and play particle FX
	OnFireWeapon.AddUniqueDynamic(this, &AHRangedCharacter::HandleOnFireWeapon);
}

void AHRangedCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (auto* SoundEngine = IWwiseSoundEngineAPI::Get())
	{
		// Stops all the Midi sequences immediately
		SoundEngine->StopMIDIOnEvent(AK_INVALID_UNIQUE_ID, AkWeaponAudioComponent->GetAkGameObjectID());
	}

	// Sanity Check: Removes all bounded functions in case a MIDI call is still staged on the audio thread
	OnFireWeapon.Clear();

	Super::EndPlay(EndPlayReason);
}

