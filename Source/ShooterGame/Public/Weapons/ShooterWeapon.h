// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "Engine/Canvas.h" // for FCanvasIcon
#include "ShooterWeapon.generated.h"

class UAnimMontage;
class AShooterCharacter;
class UAudioComponent;
class UParticleSystemComponent;
class UForceFeedbackEffect;
class USoundCue;
class UMatineeCameraShake;

namespace EWeaponState
{
	enum Type
	{
		Idle,
		Firing,
		Reloading,
		Drawing,
	};
}

UENUM(BlueprintType)
enum class EWeaponAnimType : uint8 {
	RifleAim       UMETA(DisplayName = "Rifle_Aim"),
	RifleHip        UMETA(DisplayName = "Rifle_Hip"),
};

USTRUCT()
struct FWeaponData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditDefaultsOnly, Category=Ammo)
	bool bInfiniteAmmo;

	UPROPERTY(EditDefaultsOnly, Category=Ammo)
	bool bInfiniteClip;

	UPROPERTY(EditDefaultsOnly, Category=Ammo)
	int32 MaxAmmo;

	UPROPERTY(EditDefaultsOnly, Category=Ammo)
	int32 AmmoPerClip;

	UPROPERTY(EditDefaultsOnly, Category=Ammo)
	int32 InitialClips;

	UPROPERTY(EditDefaultsOnly, Category=WeaponStat)
	float TimeBetweenShots;

	UPROPERTY(EditDefaultsOnly, Category=WeaponStat)
	float DefaultReloadDuration;



	FWeaponData()
	{
		bInfiniteAmmo = false;
		bInfiniteClip = false;
		MaxAmmo = 180;
		AmmoPerClip = 30;
		InitialClips = 3;
		TimeBetweenShots = 0.07f;
		DefaultReloadDuration = 1.0f;
	}
};

USTRUCT()
struct FWeaponAnim
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditDefaultsOnly, Category=Animation)
	UAnimMontage* Pawn1P;

	UPROPERTY(EditDefaultsOnly, Category=Animation)
	UAnimMontage* Pawn3P;

	FWeaponAnim()
		: Pawn1P(nullptr)
		, Pawn3P(nullptr)
	{
	}
};

UCLASS(Abstract, Blueprintable)
class AShooterWeapon : public AActor
{
	GENERATED_UCLASS_BODY()

	/** perform initial setup */
	virtual void PostInitializeComponents() override;

	virtual void Destroyed() override;

	//////////////////////////////////////////////////////////////////////////
	// Ammo
	
	enum class EAmmoType
	{
		EBullet,
		ERocket,
		EMax,
	};

	/** [server] add ammo */
	void GiveAmmo(int AddAmount);

	void UseAmmo();

	virtual EAmmoType GetAmmoType() const
	{
		return EAmmoType::EBullet;
	}

	//////////////////////////////////////////////////////////////////////////
	// Inventory

	virtual void OnEquip(const AShooterWeapon* LastWeapon);

	virtual void OnEquipFinished();

	virtual void OnUnEquip();

	/** [server] weapon was added to pawn's inventory */
	virtual void OnEnterInventory(AShooterCharacter* NewOwner);

	/** [server] weapon was removed from pawn's inventory */
	virtual void OnLeaveInventory();

	bool IsEquipped() const;

	bool IsAttachedToPawn() const;

	void UpdateMesh(bool isFirstPerson);

	//////////////////////////////////////////////////////////////////////////
	// Input

	/** [local + server] start weapon fire */
	virtual void TryShoot();

	/** [all] start weapon reload */
	virtual void StartReload(bool bFromReplication = false);

	/** [local + server] interrupt weapon reload */
	virtual void StopReload();

	/** [server] performs actual reload */
	virtual void ReloadWeapon();

	/** trigger reload from server */
	UFUNCTION(reliable, client)
	void ClientStartReload();


	//////////////////////////////////////////////////////////////////////////
	// Control

	/** check if weapon can fire */
	bool CanFire() const;

	/** check if weapon can be reloaded */
	bool CanReload() const;


	//////////////////////////////////////////////////////////////////////////
	// Reading data

	EWeaponState::Type GetCurrentState() const;

	int32 GetCurrentAmmo() const;

	int32 GetCurrentAmmoInClip() const;

	int32 GetAmmoPerClip() const;

	int32 GetMaxAmmo() const;

	/** get weapon mesh (needs pawn owner to determine variant) */
	USkeletalMeshComponent* GetWeaponMesh() const;

	UFUNCTION(BlueprintCallable, Category="Game|Weapon")
	class AShooterCharacter* GetPawnOwner() const;

	UPROPERTY(EditDefaultsOnly, Category=HUD)
	FCanvasIcon PrimaryIcon;

	UPROPERTY(EditDefaultsOnly, Category=HUD)
	FCanvasIcon SecondaryIcon;

	/** bullet icon used to draw current clip (left side) */
	UPROPERTY(EditDefaultsOnly, Category=HUD)
	FCanvasIcon PrimaryClipIcon;

	/** bullet icon used to draw secondary clip (left side) */
	UPROPERTY(EditDefaultsOnly, Category=HUD)
	FCanvasIcon SecondaryClipIcon;

	/** how many icons to draw per clip */
	UPROPERTY(EditDefaultsOnly, Category=HUD)
	float AmmoIconsCount;

	/** defines spacing between primary ammo icons (left side) */
	UPROPERTY(EditDefaultsOnly, Category=HUD)
	int32 PrimaryClipIconOffset;

	/** defines spacing between secondary ammo icons (left side) */
	UPROPERTY(EditDefaultsOnly, Category=HUD)
	int32 SecondaryClipIconOffset;

	UPROPERTY(EditDefaultsOnly, Category=HUD)
	bool UseLaserDot;

	UPROPERTY(EditDefaultsOnly, Category=HUD)
	bool UseCustomAimingCrosshair;

	bool HasInfiniteAmmo() const;
	bool HasInfiniteClip() const;

	void SetOwningPawn(AShooterCharacter* AShooterCharacter);

	float GetEquipStartedTime() const;

	float GetEquipDuration() const;

protected:

	UPROPERTY(Transient, ReplicatedUsing=OnRep_MyPawn)
	class AShooterCharacter* MyPawn;

	UPROPERTY(EditDefaultsOnly, Category=Config)
	FWeaponData WeaponConfig;

private:
	UPROPERTY(VisibleDefaultsOnly, Category=Mesh)
	USkeletalMeshComponent* Mesh1P;

	UPROPERTY(VisibleDefaultsOnly, Category=Mesh)
	USkeletalMeshComponent* Mesh3P;

protected:

	UPROPERTY(Transient)
	UAudioComponent* FireAC;

	UPROPERTY(EditDefaultsOnly, Category=Effects)
	FName MuzzleAttachPoint;

	UPROPERTY(EditDefaultsOnly, Category=Effects)
	UParticleSystem* MuzzleFX;

	// todo?
	UPROPERTY(Transient)
	UParticleSystemComponent* MuzzlePSC;

	UPROPERTY(EditDefaultsOnly, Category=Effects)
	TSubclassOf<UMatineeCameraShake> FireCameraShake;

	UPROPERTY(EditDefaultsOnly, Category=Sound)
	USoundCue* FireSound;


	UPROPERTY(EditDefaultsOnly, Category=Sound)
	USoundCue* ReloadSound;

	UPROPERTY(EditDefaultsOnly, Category=Animation)
	FWeaponAnim ReloadAnim;

	UPROPERTY(EditDefaultsOnly, Category=Sound)
	USoundCue* EquipSound;

	UPROPERTY(EditDefaultsOnly, Category=Animation)
	FWeaponAnim EquipAnim;

	UPROPERTY(EditDefaultsOnly, Category=Animation)
	FWeaponAnim FireAnim;

	uint32 bIsEquipped : 1;

	/** current weapon state */
	EWeaponState::Type CurrentState;

	/** last time when this weapon was switched to */
	float EquipStartedTime;

	/** how much time weapon needs to be equipped */
	float EquipDuration;

	/** current total ammo */
	UPROPERTY(Transient, Replicated)
	int32 AmmoCarry;

	/** current ammo - inside clip */
	UPROPERTY(Transient, Replicated)
	int32 AmmoInClip;

	/** Handle for efficient management of OnEquipFinished timer */
	FTimerHandle TimerHandle_OnEquipFinished;

	/** Handle for efficient management of StopReload timer */
	FTimerHandle TimerHandle_StopReload;

	/** Handle for efficient management of ReloadWeapon timer */
	FTimerHandle TimerHandle_ReloadWeapon;

	//////////////////////////////////////////////////////////////////////////
	// Input - server side

	bool bIsFiring = false;
	FTimerHandle AutoFireTimer;
	void StartFireTimer();
	UFUNCTION()
		void EndFireTimer();

	UFUNCTION(reliable, server, WithValidation)
	void ServerTryShoot();

	UFUNCTION(reliable, server, WithValidation)
	void ServerStartReload();

	UFUNCTION(reliable, server, WithValidation)
	void ServerStopReload();


	//////////////////////////////////////////////////////////////////////////
	// Replication & effects

	UFUNCTION()
	void OnRep_MyPawn();


	/** Called in network play to do the cosmetic fx for firing */
	virtual void SimulateWeaponFire();


	//////////////////////////////////////////////////////////////////////////
	// Weapon usage

	/** [local] weapon specific fire implementation */
	virtual void FireWeapon() PURE_VIRTUAL(AShooterWeapon::FireWeapon,);

	/** [server] fire & update ammo */
	UFUNCTION(reliable, server, WithValidation)
	void ServerHandleShoot();

	/** [local + server] handle weapon fire */
	void HandleShoot();

	/** update weapon state */
	void SetWeaponState(EWeaponState::Type NewState);

	//////////////////////////////////////////////////////////////////////////
	// Inventory

	/** attaches weapon mesh to pawn's mesh */
	void AttachMeshToPawn();

	/** detaches weapon mesh from pawn */
	void DetachMeshFromPawn();


	//////////////////////////////////////////////////////////////////////////
	// Weapon usage helpers

	/** play weapon sounds */
    UAudioComponent* PlayWeaponSound(USoundCue* Sound);

	/** play weapon animations */
	float PlayWeaponAnimation(const FWeaponAnim& Animation);

	/** stop playing weapon animations */
	void StopWeaponAnimation(const FWeaponAnim& Animation);

	/** Get the aim of the weapon, allowing for adjustments to be made by the weapon */
	virtual FVector GetAdjustedAim() const;

	/** Get the aim of the camera */
	FVector GetCameraAim() const;

	/** get the originating location for camera damage */
	FVector GetCameraDamageStartLocation(const FVector& AimDir) const;

	/** get the muzzle location of the weapon */
	FVector GetMuzzleLocation() const;

	/** get direction of weapon's muzzle */
	FVector GetMuzzleDirection() const;

	/** find hit */
	FHitResult WeaponTrace(const FVector& TraceFrom, const FVector& TraceTo) const;

protected:
	/** Returns Mesh1P subobject **/
	FORCEINLINE USkeletalMeshComponent* GetMesh1P() const { return Mesh1P; }
	/** Returns Mesh3P subobject **/
	FORCEINLINE USkeletalMeshComponent* GetMesh3P() const { return Mesh3P; }


	public:
		UPROPERTY(EditDefaultsOnly, Category = Sound)
			USoundCue* OutOfAmmoSound;
		UPROPERTY(EditAnywhere, BlueprintReadWrite)
			EWeaponAnimType WeaponAnimType;
};

