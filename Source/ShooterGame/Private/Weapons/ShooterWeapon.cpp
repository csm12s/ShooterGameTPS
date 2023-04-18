// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterGame.h"
#include "Weapons/ShooterWeapon.h"
#include "Player/ShooterCharacter.h"
#include "Particles/ParticleSystemComponent.h"
#include "Bots/ShooterAIController.h"
#include "Online/ShooterPlayerState.h"
#include "UI/ShooterHUD.h"
#include "MatineeCameraShake.h"

AShooterWeapon::AShooterWeapon(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	Mesh1P = ObjectInitializer.CreateDefaultSubobject<USkeletalMeshComponent>(this, TEXT("WeaponMesh1P"));
	Mesh1P->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	Mesh1P->bReceivesDecals = false;
	Mesh1P->CastShadow = false;
	Mesh1P->SetCollisionObjectType(ECC_WorldDynamic);
	Mesh1P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh1P->SetCollisionResponseToAllChannels(ECR_Ignore);
	Mesh1P->SetupAttachment(RootComponent);

	Mesh3P = ObjectInitializer.CreateDefaultSubobject<USkeletalMeshComponent>(this, TEXT("WeaponMesh3P"));
	Mesh3P->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	Mesh3P->bReceivesDecals = false;
	Mesh3P->CastShadow = true;
	Mesh3P->SetCollisionObjectType(ECC_WorldDynamic);
	Mesh3P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh3P->SetCollisionResponseToAllChannels(ECR_Ignore);
	Mesh3P->SetCollisionResponseToChannel(COLLISION_WEAPON, ECR_Block);
	Mesh3P->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Mesh3P->SetCollisionResponseToChannel(COLLISION_PROJECTILE, ECR_Block);
	Mesh3P->SetupAttachment(RootComponent);

	bIsEquipped = false;
	CurrentState = EWeaponState::Idle;

	AmmoInClip = 0;
	AmmoCarry = 0;

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	SetRemoteRoleForBackwardsCompat(ROLE_SimulatedProxy);
	bReplicates = true;
	bNetUseOwnerRelevancy = true;
}

void AShooterWeapon::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (WeaponConfig.InitialClips > 0)
	{
		AmmoInClip = WeaponConfig.AmmoPerClip;
		AmmoCarry = WeaponConfig.AmmoPerClip * WeaponConfig.InitialClips;
	}

	DetachMeshFromPawn();
}

void AShooterWeapon::Destroyed()
{
	Super::Destroyed();
}

//////////////////////////////////////////////////////////////////////////
// Inventory

void AShooterWeapon::OnEquip(const AShooterWeapon* LastWeapon)
{
	SetWeaponState(EWeaponState::Drawing);

	AttachMeshToPawn();

	// Only play animation if last weapon is valid
	if (LastWeapon)
	{
		float Duration = PlayWeaponAnimation(EquipAnim);
		if (Duration <= 0.0f)
		{
			// failsafe
			Duration = 0.5f;
		}
		EquipStartedTime = GetWorld()->GetTimeSeconds();
		EquipDuration = Duration;

		GetWorldTimerManager().SetTimer(TimerHandle_OnEquipFinished, this, &AShooterWeapon::OnEquipFinished, Duration, false);
	}
	else
	{
		OnEquipFinished();
	}

	if (MyPawn && MyPawn->IsLocallyControlled())
	{
		PlayWeaponSound(EquipSound);
	}

	AShooterCharacter::NotifyEquipWeapon.Broadcast(MyPawn, this);
}

void AShooterWeapon::OnEquipFinished()
{
	SetWeaponState(EWeaponState::Idle);
	bIsEquipped = true;

	AttachMeshToPawn();

	if (MyPawn)
	{
		// try to reload empty clip
		if (MyPawn->IsLocallyControlled() &&
			AmmoInClip <= 0 &&
			CanReload())
		{
			StartReload();
		}
	}
}

void AShooterWeapon::OnUnEquip()
{
	DetachMeshFromPawn();
	bIsEquipped = false;

	//if (bPendingReload)
	{
		StopWeaponAnimation(ReloadAnim);

		GetWorldTimerManager().ClearTimer(TimerHandle_StopReload);
		GetWorldTimerManager().ClearTimer(TimerHandle_ReloadWeapon);
	}

	//if (bPendingEquip)
	{
		StopWeaponAnimation(EquipAnim);

		GetWorldTimerManager().ClearTimer(TimerHandle_OnEquipFinished);
	}

	AShooterCharacter::NotifyUnEquipWeapon.Broadcast(MyPawn, this);
}

void AShooterWeapon::OnEnterInventory(AShooterCharacter* NewOwner)
{
	SetOwningPawn(NewOwner);
}

void AShooterWeapon::OnLeaveInventory()
{
	if (IsAttachedToPawn())
	{
		OnUnEquip();
	}

	if (GetLocalRole() == ROLE_Authority)
	{
		SetOwningPawn(NULL);
	}
}

void AShooterWeapon::AttachMeshToPawn()
{
	if (MyPawn)
	{
		// Remove and hide both first and third person meshes
		DetachMeshFromPawn();

		// For locally controller players we attach both weapons and let the bOnlyOwnerSee, bOwnerNoSee flags deal with visibility.
		FName AttachPoint = MyPawn->GetWeaponAttachPoint();
		if (MyPawn->IsLocallyControlled() == true)
		{
			USkeletalMeshComponent* PawnMesh1p = MyPawn->GetSpecifcPawnMesh(true);
			USkeletalMeshComponent* PawnMesh3p = MyPawn->GetSpecifcPawnMesh(false);
			Mesh1P->SetHiddenInGame(false);
			Mesh3P->SetHiddenInGame(false);
			Mesh1P->AttachToComponent(PawnMesh1p, FAttachmentTransformRules::KeepRelativeTransform, AttachPoint);
			Mesh3P->AttachToComponent(PawnMesh3p, FAttachmentTransformRules::KeepRelativeTransform, AttachPoint);
		}
		else
		{
			USkeletalMeshComponent* UseWeaponMesh = GetWeaponMesh();
			USkeletalMeshComponent* UsePawnMesh = MyPawn->GetPawnMesh();
			UseWeaponMesh->AttachToComponent(UsePawnMesh, FAttachmentTransformRules::KeepRelativeTransform, AttachPoint);
			UseWeaponMesh->SetHiddenInGame(false);
		}

		//todo ref
		UpdateMesh(MyPawn->IsFirstPerson());
	}
}

void AShooterWeapon::DetachMeshFromPawn()
{
	Mesh1P->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
	Mesh1P->SetHiddenInGame(true);

	Mesh3P->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
	Mesh3P->SetHiddenInGame(true);
}


//////////////////////////////////////////////////////////////////////////
// Input


void AShooterWeapon::StartReload(bool bFromReplication)
{
	if (!bFromReplication && GetLocalRole() < ROLE_Authority)
	{
		ServerStartReload();
	}

	if (bFromReplication || CanReload())
	{
		SetWeaponState(EWeaponState::Reloading);

		if (MyPawn && MyPawn->IsLocallyControlled()) {
			MyPawn->OnStopTargeting();
		}

		float AnimDuration = PlayWeaponAnimation(ReloadAnim);
		if (AnimDuration <= 0.0f)
		{
			AnimDuration = WeaponConfig.DefaultReloadDuration;
		}

		GetWorldTimerManager().SetTimer(TimerHandle_StopReload, this, &AShooterWeapon::StopReload, AnimDuration, false);
		if (GetLocalRole() == ROLE_Authority)
		{
			GetWorldTimerManager().SetTimer(TimerHandle_ReloadWeapon, this, &AShooterWeapon::ReloadWeapon, FMath::Max(0.1f, AnimDuration - 0.1f), false);
		}

		if (MyPawn && MyPawn->IsLocallyControlled())
		{
			PlayWeaponSound(ReloadSound);
		}
	}
}

void AShooterWeapon::StopReload()
{
	if (CurrentState == EWeaponState::Reloading)
	{
		StopWeaponAnimation(ReloadAnim);
		SetWeaponState(EWeaponState::Idle);
	}
}

#pragma region StartFire
/// <summary>
/// TryShoot
/// todo change name
/// </summary>
void AShooterWeapon::TryShoot()
{
	if (GetLocalRole() < ROLE_Authority)
	{
		ServerTryShoot();
	}

	HandleShoot();
}

bool AShooterWeapon::ServerTryShoot_Validate()
{
	return true;
}

void AShooterWeapon::ServerTryShoot_Implementation()
{
	TryShoot();
}
#pragma endregion

#pragma region fire
/// <summary>
/// HandleShoot
/// </summary>
void AShooterWeapon::HandleShoot()
{
	if ((AmmoInClip > 0 || HasInfiniteClip() || HasInfiniteAmmo())
		&& CanFire())
	{
		StartFireTimer();

		CurrentState = EWeaponState::Firing;

		if (GetNetMode() != NM_DedicatedServer)
		{
			SimulateWeaponFire();
		}

		if (MyPawn && MyPawn->IsLocallyControlled())
		{
			float AnimDuration = PlayWeaponAnimation(FireAnim);

			FireWeapon();

			UseAmmo();
		}
	}

	if (MyPawn && MyPawn->IsLocallyControlled())
	{
		// local client will notify server
		if (GetLocalRole() < ROLE_Authority)
		{
			ServerHandleShoot();
		}

		// auto reload
		if (AmmoInClip <= 0 && CanReload())
		{
			StartReload();
		}
	}
}

bool AShooterWeapon::ServerHandleShoot_Validate()
{
	return true;
}

void AShooterWeapon::ServerHandleShoot_Implementation()
{
	const bool bShouldUpdateAmmo = (AmmoInClip > 0 && CanFire());

	HandleShoot();

	if (bShouldUpdateAmmo)
	{
		// update ammo
		UseAmmo();

	}
}

#pragma endregion


bool AShooterWeapon::ServerStartReload_Validate()
{
	return true;
}

void AShooterWeapon::ServerStartReload_Implementation()
{
	StartReload();
}

bool AShooterWeapon::ServerStopReload_Validate()
{
	return true;
}

void AShooterWeapon::ServerStopReload_Implementation()
{
	StopReload();
}

void AShooterWeapon::ClientStartReload_Implementation()
{
	StartReload();
}

//////////////////////////////////////////////////////////////////////////
// Control

void AShooterWeapon::StartFireTimer()
{
	bIsFiring = true;
	//CurrentState = EWeaponState::Firing;

	GetWorldTimerManager().SetTimer(
		AutoFireTimer,
		this,
		&AShooterWeapon::EndFireTimer,
		WeaponConfig.TimeBetweenShots,
		false);
}

void AShooterWeapon::EndFireTimer()
{
	bIsFiring = false;
	CurrentState = EWeaponState::Idle;
}

bool AShooterWeapon::CanFire() const
{
	bool bPawnCanFire = MyPawn && MyPawn->CanFire();
	bool bStateOKToFire = CurrentState == EWeaponState::Idle;//|| CurrentState == EWeaponState.FireReady);

	return bPawnCanFire == true
		&& bStateOKToFire == true
		&& !bIsFiring
		&& AmmoInClip > 0;
}

bool AShooterWeapon::CanReload() const
{
	bool bPawnCanReload = (!MyPawn || MyPawn->CanReload());
	bool bGotAmmo = (AmmoInClip < WeaponConfig.AmmoPerClip)
		&& (AmmoCarry > 0 || HasInfiniteClip());
	
	bool bStateOKToReload = CurrentState == EWeaponState::Idle
		|| CurrentState == EWeaponState::Firing;

	return ((bPawnCanReload == true)
		&& (bGotAmmo == true)
		&& (bStateOKToReload == true));
}


//////////////////////////////////////////////////////////////////////////
// Weapon usage

void AShooterWeapon::GiveAmmo(int AddAmount)
{
	const int32 MissingAmmo = FMath::Max(0, WeaponConfig.MaxAmmo - AmmoCarry);
	AddAmount = FMath::Min(AddAmount, MissingAmmo);
	AmmoCarry += AddAmount;

	AShooterAIController* BotAI = MyPawn ? Cast<AShooterAIController>(MyPawn->GetController()) : NULL;
	if (BotAI)
	{
		BotAI->CheckAmmo(this);
	}

	// start reload if clip was empty
	if (GetCurrentAmmoInClip() <= 0 &&
		CanReload() &&
		MyPawn && (MyPawn->GetWeapon() == this))
	{
		ClientStartReload();
	}
}

void AShooterWeapon::UseAmmo()
{
	if (!HasInfiniteAmmo())
	{
		AmmoInClip--;
	}

	if (!HasInfiniteAmmo() && !HasInfiniteClip())
	{
		AmmoCarry--;
	}

	AShooterAIController* BotAI = MyPawn ? Cast<AShooterAIController>(MyPawn->GetController()) : NULL;
	AShooterPlayerController* PlayerController = MyPawn ? Cast<AShooterPlayerController>(MyPawn->GetController()) : NULL;
	if (BotAI)
	{
		BotAI->CheckAmmo(this);
	}
	else if (PlayerController)
	{
		if (AShooterPlayerState* PlayerState = Cast<AShooterPlayerState>(PlayerController->PlayerState))
		{
			switch (GetAmmoType())
			{
			case EAmmoType::ERocket:
				PlayerState->AddRocketsFired(1);
				break;
			case EAmmoType::EBullet:
			default:
				PlayerState->AddBulletsFired(1);
				break;
			}
		}
	}
}


void AShooterWeapon::ReloadWeapon()
{
	int32 ClipDelta = FMath::Min(WeaponConfig.AmmoPerClip - AmmoInClip, AmmoCarry - AmmoInClip);

	if (HasInfiniteClip())
	{
		ClipDelta = WeaponConfig.AmmoPerClip - AmmoInClip;
	}

	if (ClipDelta > 0)
	{
		AmmoInClip += ClipDelta;
	}

	if (HasInfiniteClip())
	{
		AmmoCarry = FMath::Max(AmmoInClip, AmmoCarry);
	}
}

void AShooterWeapon::SetWeaponState(EWeaponState::Type NewState)
{
	const EWeaponState::Type PrevState = CurrentState;

	CurrentState = NewState;

}

//////////////////////////////////////////////////////////////////////////
// Weapon usage helpers

UAudioComponent* AShooterWeapon::PlayWeaponSound(USoundCue* Sound)
{
	UAudioComponent* AC = NULL;
	if (Sound && MyPawn)
	{
		AC = UGameplayStatics::SpawnSoundAttached(Sound, MyPawn->GetRootComponent());
	}

	return AC;
}

float AShooterWeapon::PlayWeaponAnimation(const FWeaponAnim& Animation)
{
	float Duration = 0.0f;
	if (MyPawn)
	{
		UAnimMontage* UseAnim = MyPawn->IsFirstPerson() ? Animation.Pawn1P : Animation.Pawn3P;
		if (UseAnim)
		{
			Duration = MyPawn->PlayAnimMontage(UseAnim);
		}
	}

	return Duration;
}

void AShooterWeapon::StopWeaponAnimation(const FWeaponAnim& Animation)
{
	if (MyPawn)
	{
		UAnimMontage* UseAnim = MyPawn->IsFirstPerson() ? Animation.Pawn1P : Animation.Pawn3P;
		if (UseAnim)
		{
			MyPawn->StopAnimMontage(UseAnim);
		}
	}
}

FVector AShooterWeapon::GetCameraAim() const
{
	AShooterPlayerController* const PlayerController = GetInstigatorController<AShooterPlayerController>();
	FVector FinalAim = FVector::ZeroVector;

	if (PlayerController)
	{
		FVector CamLoc;
		FRotator CamRot;
		PlayerController->GetPlayerViewPoint(CamLoc, CamRot);
		FinalAim = CamRot.Vector();
	}
	else if (GetInstigator())
	{
		FinalAim = GetInstigator()->GetBaseAimRotation().Vector();
	}

	return FinalAim;
}

FVector AShooterWeapon::GetAdjustedAim() const
{
	AShooterPlayerController* const PlayerController = GetInstigatorController<AShooterPlayerController>();
	FVector FinalAim = FVector::ZeroVector;
	// If we have a player controller use it for the aim
	if (PlayerController)
	{
		FVector CamLoc;
		FRotator CamRot;
		PlayerController->GetPlayerViewPoint(CamLoc, CamRot);
		FinalAim = CamRot.Vector();
	}
	else if (GetInstigator())
	{
		// Now see if we have an AI controller - we will want to get the aim from there if we do
		AShooterAIController* AIController = MyPawn ? Cast<AShooterAIController>(MyPawn->Controller) : NULL;
		if (AIController != NULL)
		{
			FinalAim = AIController->GetControlRotation().Vector();
		}
		else
		{
			FinalAim = GetInstigator()->GetBaseAimRotation().Vector();
		}
	}

	return FinalAim;
}

FVector AShooterWeapon::GetCameraDamageStartLocation(const FVector& AimDir) const
{
	AShooterPlayerController* PC = MyPawn ? Cast<AShooterPlayerController>(MyPawn->Controller) : NULL;
	AShooterAIController* AIPC = MyPawn ? Cast<AShooterAIController>(MyPawn->Controller) : NULL;
	FVector OutStartTrace = FVector::ZeroVector;

	if (PC)
	{
		// use player's camera
		FRotator UnusedRot;
		PC->GetPlayerViewPoint(OutStartTrace, UnusedRot);

		// Adjust trace so there is nothing blocking the ray between the camera and the pawn, and calculate distance from adjusted start
		OutStartTrace = OutStartTrace + AimDir * ((GetInstigator()->GetActorLocation() - OutStartTrace) | AimDir);
	}
	else if (AIPC)
	{
		OutStartTrace = GetMuzzleLocation();
	}

	return OutStartTrace;
}

FVector AShooterWeapon::GetMuzzleLocation() const
{
	USkeletalMeshComponent* UseMesh = GetWeaponMesh();
	return UseMesh->GetSocketLocation(MuzzleAttachPoint);
}

FVector AShooterWeapon::GetMuzzleDirection() const
{
	USkeletalMeshComponent* UseMesh = GetWeaponMesh();
	return UseMesh->GetSocketRotation(MuzzleAttachPoint).Vector();
}

FHitResult AShooterWeapon::WeaponTrace(const FVector& StartTrace, const FVector& EndTrace) const
{

	// Perform trace to retrieve hit info
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(WeaponTrace), true, GetInstigator());
	TraceParams.bReturnPhysicalMaterial = true;

	FHitResult Hit(ForceInit);
	GetWorld()->LineTraceSingleByChannel(Hit, StartTrace, EndTrace, COLLISION_WEAPON, TraceParams);

	return Hit;
}

void AShooterWeapon::SetOwningPawn(AShooterCharacter* NewOwner)
{
	if (MyPawn != NewOwner)
	{
		SetInstigator(NewOwner);
		MyPawn = NewOwner;
		// net owner for RPC calls
		SetOwner(NewOwner);
	}
}

//////////////////////////////////////////////////////////////////////////
// Replication & effects

void AShooterWeapon::OnRep_MyPawn()
{
	if (MyPawn)
	{
		OnEnterInventory(MyPawn);
	}
	else
	{
		OnLeaveInventory();
	}
}


void AShooterWeapon::SimulateWeaponFire()
{
	if (GetLocalRole() == ROLE_Authority && CurrentState != EWeaponState::Firing)
	{
		return;
	}

	if (MuzzleFX)
	{
		USkeletalMeshComponent* UseWeaponMesh = GetWeaponMesh();
		if (MuzzlePSC == NULL)
		{
			// Split screen requires we create 2 effects. One that we see and one that the other player sees.
			if ((MyPawn != NULL) && (MyPawn->IsLocallyControlled() == true))
			{
				AController* PlayerCon = MyPawn->GetController();
				if (PlayerCon != NULL)
				{
					Mesh1P->GetSocketLocation(MuzzleAttachPoint);
					MuzzlePSC = UGameplayStatics::SpawnEmitterAttached(MuzzleFX, Mesh1P, MuzzleAttachPoint);
					MuzzlePSC->bOwnerNoSee = false;
					MuzzlePSC->bOnlyOwnerSee = true;

					Mesh3P->GetSocketLocation(MuzzleAttachPoint);
				}
			}
			else
			{
				MuzzlePSC = UGameplayStatics::SpawnEmitterAttached(MuzzleFX, UseWeaponMesh, MuzzleAttachPoint);
			}
		}
	}

	PlayWeaponAnimation(FireAnim);
	PlayWeaponSound(FireSound);

	AShooterPlayerController* PC = (MyPawn != NULL) ? Cast<AShooterPlayerController>(MyPawn->Controller) : NULL;
	if (PC != NULL && PC->IsLocalController())
	{
		if (FireCameraShake != NULL)
		{
			PC->ClientStartCameraShake(FireCameraShake, 1);
		}

	}
}

void AShooterWeapon::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShooterWeapon, MyPawn);

	DOREPLIFETIME_CONDITION(AShooterWeapon, AmmoCarry, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AShooterWeapon, AmmoInClip, COND_OwnerOnly);
}

USkeletalMeshComponent* AShooterWeapon::GetWeaponMesh() const
{
	return (MyPawn != NULL && MyPawn->IsFirstPerson()) ? Mesh1P : Mesh3P;
}

class AShooterCharacter* AShooterWeapon::GetPawnOwner() const
{
	return MyPawn;
}

bool AShooterWeapon::IsEquipped() const
{
	return bIsEquipped;
}

bool AShooterWeapon::IsAttachedToPawn() const
{
	return bIsEquipped;
}

void AShooterWeapon::UpdateMesh(bool bFirstPerson)
{
	//Mesh1P->SetVisibility(bFirstPerson, true);

	Mesh1P->VisibilityBasedAnimTickOption = !bFirstPerson ? EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered : EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh1P->SetOwnerNoSee(!bFirstPerson);

	Mesh3P->VisibilityBasedAnimTickOption = bFirstPerson ? EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered : EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh3P->SetOwnerNoSee(bFirstPerson);
}

EWeaponState::Type AShooterWeapon::GetCurrentState() const
{
	return CurrentState;
}

int32 AShooterWeapon::GetCurrentAmmo() const
{
	return AmmoCarry;
}

int32 AShooterWeapon::GetCurrentAmmoInClip() const
{
	return AmmoInClip;
}

int32 AShooterWeapon::GetAmmoPerClip() const
{
	return WeaponConfig.AmmoPerClip;
}

int32 AShooterWeapon::GetMaxAmmo() const
{
	return WeaponConfig.MaxAmmo;
}

bool AShooterWeapon::HasInfiniteAmmo() const
{
	const AShooterPlayerController* MyPC = (MyPawn != NULL) ? Cast<const AShooterPlayerController>(MyPawn->Controller) : NULL;
	return WeaponConfig.bInfiniteAmmo || (MyPC && MyPC->HasInfiniteAmmo());
}

bool AShooterWeapon::HasInfiniteClip() const
{
	const AShooterPlayerController* MyPC = (MyPawn != NULL) ? Cast<const AShooterPlayerController>(MyPawn->Controller) : NULL;
	return WeaponConfig.bInfiniteClip || (MyPC && MyPC->HasInfiniteClip());
}

float AShooterWeapon::GetEquipStartedTime() const
{
	return EquipStartedTime;
}

float AShooterWeapon::GetEquipDuration() const
{
	return EquipDuration;
}
