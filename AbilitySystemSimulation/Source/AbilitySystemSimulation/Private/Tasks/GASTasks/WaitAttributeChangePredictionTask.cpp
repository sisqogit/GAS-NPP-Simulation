// Fill out your copyright notice in the Description page of Project Settings.


#include "Tasks/GASTasks/WaitAttributeChangePredictionTask.h"

#include "AbilitySystemGlobals.h"
#include "GameplayEffect.h"
#include "GameplayEffectExtension.h"
#include "GameplayEffectTypes.h"
#include "Abilities/NpAbilitySystemComponent.h"
#include "Tasks/GASTasks/CommonDataTypes.h"

#define LOCTEXT_NAMESPACE "WaitAttributeChangedTask"

#pragma region Wait Owner Atrribute Changed Task Class
UWaitOwnerAttributeChangePredictionTask::UWaitOwnerAttributeChangePredictionTask(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer)
{
	DataType = nullptr;
	StartTaskFunctionName = GET_FUNCTION_NAME_CHECKED(UWaitOwnerAttributeChangePredictionTask, ExecuteTask);
	ShouldTaskTick = false;
}

void UWaitOwnerAttributeChangePredictionTask::ExecuteTask()
{
	if (IsInRollback()) return;

	UAbilitySystemComponent* ASC = GetFocusedASC();
	if (!ASC || Attribute.Num() == 0) return;

	//AttributesToListenFor = Attribute; 

	for (const FGameplayAttribute& Attr : Attribute)
	{
		ASC->GetGameplayAttributeValueChangeDelegate(Attr)
			.AddUObject(this, &UWaitOwnerAttributeChangePredictionTask::OnAttributeChange);
	}

	ActivateTask();
}

void UWaitOwnerAttributeChangePredictionTask::OnAttributeChange(const FOnAttributeChangeData& CallbackData)
{
	if (!ShouldTriggerCallbacks())
	{
		return;
	}

	//if (!Attribute.Contains(CallbackData.Attribute))
	//{
	//	return;
	//}

	// -------------------------
	if (FMath::IsNearlyEqual(CallbackData.OldValue, CallbackData.NewValue))
	{
		return;
	}
	// -------------------------

	const float NewValue = CallbackData.NewValue;
	const float OldValue = CallbackData.OldValue;
	const FGameplayEffectModCallbackData* Data = CallbackData.GEModData;

	if (Data == nullptr)
	{
		// There may be no execution data associated with this change, for example a GE being removed. 
		// In this case, we auto fail any WithTag requirement and auto pass any WithoutTag requirement
		if (WithTag.IsValid())
		{
			return;
		}
	}
	else
	{
		if ((WithTag.IsValid() && !Data->EffectSpec.CapturedSourceTags.GetAggregatedTags()->HasTag(WithTag)) ||
			(WithoutTag.IsValid() && Data->EffectSpec.CapturedSourceTags.GetAggregatedTags()->HasTag(WithoutTag)))
		{
			// Failed tag check
			return;
		}
	}

	bool PassedComparison = true;
	switch (ComparisonType)
	{
	case ExactlyEqualTo:
		PassedComparison = (NewValue == ComparisonValue);
		break;
	case GreaterThan:
		PassedComparison = (NewValue > ComparisonValue);
		break;
	case GreaterThanOrEqualTo:
		PassedComparison = (NewValue >= ComparisonValue);
		break;
	case LessThan:
		PassedComparison = (NewValue < ComparisonValue);
		break;
	case LessThanOrEqualTo:
		PassedComparison = (NewValue <= ComparisonValue);
		break;
	case NotEqualTo:
		PassedComparison = (NewValue != ComparisonValue);
		break;
	default:
		break;
	}
	if (PassedComparison)
	{
		OnChange.Broadcast(CallbackData.Attribute, OldValue, NewValue); // Added CallbackData.Attribute
		if (bTriggerOnce)
		{
			DeactivateTask(false);  // or EndTask()

			// Optional safety cleanup 
			if (UAbilitySystemComponent* ASC = GetFocusedASC())
			{
				for (const FGameplayAttribute& Attr : Attribute)
				{
					ASC->GetGameplayAttributeValueChangeDelegate(Attr)
						.RemoveAll(this);   
				}
			}
		}
	}
}

UAbilitySystemComponent* UWaitOwnerAttributeChangePredictionTask::GetFocusedASC() const
{
	return GetAbilitySystemComponent();
}

void UWaitOwnerAttributeChangePredictionTask::OnPreDeactivate(const bool& bWasCancelled)
{
	Super::OnPreDeactivate(bWasCancelled);

	if (UAbilitySystemComponent* ASC = GetFocusedASC())
	{
		for (const FGameplayAttribute Attr : Attribute)
		{
			ASC->GetGameplayAttributeValueChangeDelegate(Attr).RemoveAll(this);
		}
	}
}

void UWaitOwnerAttributeChangePredictionTask::StartTaskRollback(const FAbilityTaskDataContainer& AuthoritySyncData)
{
	Super::StartTaskRollback(AuthoritySyncData);

	const FExternalTargetTaskData* AttributeChangedAuthorityData = static_cast<FExternalTargetTaskData*>(AuthoritySyncData.TaskDataPointer.Get());
	UAbilitySystemComponent* TargetAsc = AttributeChangedAuthorityData && AttributeChangedAuthorityData->ExternalTarget
		? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(AttributeChangedAuthorityData->ExternalTarget)
		: GetAbilitySystemComponent();

	if (!TargetAsc || Attribute.Num() == 0)
		return;

	if (AuthoritySyncData.IsActive)
	{
		// Bind all attributes in the array instead of just one
		for (const FGameplayAttribute& Attr : Attribute)
		{
			TargetAsc->GetGameplayAttributeValueChangeDelegate(Attr)
				.AddUObject(this, &UWaitOwnerAttributeChangePredictionTask::OnAttributeChange);
		}
	}
	else
	{
		// Unbind all attributes in the array
		for (const FGameplayAttribute& Attr : Attribute)
		{
			TargetAsc->GetGameplayAttributeValueChangeDelegate(Attr)
				.RemoveAll(this);
		}
	}
}

FText UWaitOwnerAttributeChangePredictionTask::GetNodeTitle() const
{
	return LOCTEXT("WaitOwnerAttributeChangedTask", "Wait Owner Attribute Changed Prediction Task");
}

FLinearColor UWaitOwnerAttributeChangePredictionTask::GetNodeTitleColor() const
{
	return FLinearColor(0.243f, 0.902f, 0.62f, 1.0f);
}
#pragma endregion

#pragma region Wait Target Atrribute Changed Task Class
UWaitTargetAttributeChangePredictionTask::UWaitTargetAttributeChangePredictionTask(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer)
{
	DataType = FExternalTargetTaskData::StaticStruct();
	StartTaskFunctionName = GET_FUNCTION_NAME_CHECKED(UWaitTargetAttributeChangePredictionTask, ExecuteTaskWithTarget);
	ShouldTaskTick = false;
}

void UWaitTargetAttributeChangePredictionTask::ExecuteTaskWithTarget(AActor* TargetActor)
{
	if (IsInRollback())
	{
		return;
	}

	CachedExternalTarget = TargetActor;

	UAbilitySystemComponent* TargetAsc = CachedExternalTarget
		? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(CachedExternalTarget)
		: GetAbilitySystemComponent();

	if (TargetAsc && Attribute.Num() > 0)
	{
		for (const FGameplayAttribute& Attr : Attribute)
		{
			TargetAsc->GetGameplayAttributeValueChangeDelegate(Attr)
				.AddUObject(this, &UWaitTargetAttributeChangePredictionTask::OnAttributeChange);
		}

		ActivateTask();
	}
}

UAbilitySystemComponent* UWaitTargetAttributeChangePredictionTask::GetFocusedASC() const
{
	if (CachedExternalTarget)
	{
		return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(CachedExternalTarget);
	}
	return GetAbilitySystemComponent();
}

void UWaitTargetAttributeChangePredictionTask::StartTaskRollback(const FAbilityTaskDataContainer& AuthoritySyncData)
{
	// this override the super to ensure we have the same target and bind to its ASC.
	if (AuthoritySyncData.IsActive)
	{
		const FExternalTargetTaskData* AttributeChangedAuthorityData = static_cast<FExternalTargetTaskData*>(AuthoritySyncData.TaskDataPointer.Get());
		UAbilitySystemComponent* TargetAsc = AttributeChangedAuthorityData && AttributeChangedAuthorityData->ExternalTarget
			? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(AttributeChangedAuthorityData->ExternalTarget)
			: GetAbilitySystemComponent();

		if (!TargetAsc || Attribute.Num() == 0)
			return;

		// Always unbind from current ASC first (safety)
		if (UAbilitySystemComponent* CurrentASC = GetFocusedASC())
		{
			for (const FGameplayAttribute& Attr : Attribute)
			{
				CurrentASC->GetGameplayAttributeValueChangeDelegate(Attr).RemoveAll(this);
			}
		}

		// Then bind to correct target ASC
		for (const FGameplayAttribute& Attr : Attribute)
		{
			TargetAsc->GetGameplayAttributeValueChangeDelegate(Attr)
				.AddUObject(this, &UWaitTargetAttributeChangePredictionTask::OnAttributeChange);
		}
	}
	else // we shouldn't be active
	{
		// Unbind from whatever we're currently listening to
		if (UAbilitySystemComponent* CurrentASC = GetFocusedASC())
		{
			for (const FGameplayAttribute& Attr : Attribute)
			{
				CurrentASC->GetGameplayAttributeValueChangeDelegate(Attr).RemoveAll(this);
			}
		}
	}
}
void UWaitTargetAttributeChangePredictionTask::ReadFromSyncedData(TSharedPtr<const FAbilityTaskDataBase> DataToRead)
{
	const FExternalTargetTaskData* ExternalTargetData = static_cast<const FExternalTargetTaskData*>(DataToRead.Get());
	CachedExternalTarget = ExternalTargetData->ExternalTarget;
}

void UWaitTargetAttributeChangePredictionTask::WriteToSyncedData(TSharedPtr<FAbilityTaskDataBase> DataToWrite)
{
	FExternalTargetTaskData* ExternalTargetData = static_cast<FExternalTargetTaskData*>(DataToWrite.Get());
	ExternalTargetData->ExternalTarget = CachedExternalTarget;

}

FText UWaitTargetAttributeChangePredictionTask::GetNodeTitle() const
{
	return LOCTEXT("WaitTargetAttributeChangedTask", "Wait Target Attribute Changed Prediction Task");
}

FLinearColor UWaitTargetAttributeChangePredictionTask::GetNodeTitleColor() const
{
	return FLinearColor(0.243f, 0.902f, 0.62f, 1.0f);
}
#pragma endregion

#undef LOCTEXT_NAMESPACE
