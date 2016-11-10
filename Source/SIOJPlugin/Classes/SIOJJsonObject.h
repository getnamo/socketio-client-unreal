// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.
// Copyright 2014 Vladimir Alyamkin. All Rights Reserved.

#pragma once

#include "SIOJJsonObject.generated.h"

class USIOJJsonValue;

/**
 * Blueprintable FJsonObject wrapper
 */
UCLASS(BlueprintType, Blueprintable)
class SIOJPLUGIN_API USIOJJsonObject : public UObject
{
	GENERATED_UCLASS_BODY()

	/** Create new Json object, cannot be pure  */
	UFUNCTION(BlueprintCallable , meta = (DisplayName = "Construct Json Object", HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject"), Category = "SIOJ|Json")
	static USIOJJsonObject* ConstructJsonObject(UObject* WorldContextObject);

	/** Reset all internal data */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	void Reset();

	/** Get the root Json object */
	TSharedPtr<FJsonObject>& GetRootObject();

	/** Set the root Json object */
	void SetRootObject(TSharedPtr<FJsonObject>& JsonObject);


	//////////////////////////////////////////////////////////////////////////
	// Serialization

	/** Serialize Json to string (formatted with line breaks) */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	FString EncodeJson() const;

	/** Serialize Json to string (single string without line breaks) */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	FString EncodeJsonToSingleString() const;

	/** Construct Json object from string */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	bool DecodeJson(const FString& JsonString);


	//////////////////////////////////////////////////////////////////////////
	// FJsonObject API

	/** Returns a list of field names that exist in the object */
	UFUNCTION(BlueprintPure, Category = "SIOJ|Json")
	TArray<FString> GetFieldNames();

	/** Checks to see if the FieldName exists in the object */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	bool HasField(const FString& FieldName) const;

	/** Remove field named FieldName */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	void RemoveField(const FString& FieldName);

	/** Get the field named FieldName as a JsonValue */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	USIOJJsonValue* GetField(const FString& FieldName) const;

	/** Add a field named FieldName with a Value */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	void SetField(const FString& FieldName, USIOJJsonValue* JsonValue);

	/** Get the field named FieldName as a Json Array */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	TArray<USIOJJsonValue*> GetArrayField(const FString& FieldName);

	/** Set an ObjectField named FieldName and value of Json Array */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	void SetArrayField(const FString& FieldName, const TArray<USIOJJsonValue*>& InArray);
	
	/** Adds all of the fields from one json object to this one */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	void MergeJsonObject(USIOJJsonObject* InJsonObject, bool Overwrite);


	//////////////////////////////////////////////////////////////////////////
	// FJsonObject API Helpers (easy to use with simple Json objects)

	/** Get the field named FieldName as a number. Ensures that the field is present and is of type Json number.
	 * Attn.!! float used instead of double to make the function blueprintable! */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	float GetNumberField(const FString& FieldName) const;

	/** Add a field named FieldName with Number as value
	 * Attn.!! float used instead of double to make the function blueprintable! */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	void SetNumberField(const FString& FieldName, float Number);

	/** Get the field named FieldName as a string. */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	FString GetStringField(const FString& FieldName) const;

	/** Add a field named FieldName with value of StringValue */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	void SetStringField(const FString& FieldName, const FString& StringValue);

	/** Get the field named FieldName as a boolean. */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	bool GetBoolField(const FString& FieldName) const;

	/** Set a boolean field named FieldName and value of InValue */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	void SetBoolField(const FString& FieldName, bool InValue);

	/** Get the field named FieldName as a Json object. */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	USIOJJsonObject* GetObjectField(const FString& FieldName) const;

	/** Set an ObjectField named FieldName and value of JsonObject */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	void SetObjectField(const FString& FieldName, USIOJJsonObject* JsonObject);


	//////////////////////////////////////////////////////////////////////////
	// Array fields helpers (uniform arrays)

	/** Get the field named FieldName as a Number Array. Use it only if you're sure that array is uniform!
	 * Attn.!! float used instead of double to make the function blueprintable! */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	TArray<float> GetNumberArrayField(const FString& FieldName);

	/** Set an ObjectField named FieldName and value of Number Array
	 * Attn.!! float used instead of double to make the function blueprintable! */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	void SetNumberArrayField(const FString& FieldName, const TArray<float>& NumberArray);

	/** Get the field named FieldName as a String Array. Use it only if you're sure that array is uniform! */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	TArray<FString> GetStringArrayField(const FString& FieldName);

	/** Set an ObjectField named FieldName and value of String Array */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	void SetStringArrayField(const FString& FieldName, const TArray<FString>& StringArray);

	/** Get the field named FieldName as a Bool Array. Use it only if you're sure that array is uniform! */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	TArray<bool> GetBoolArrayField(const FString& FieldName);

	/** Set an ObjectField named FieldName and value of Bool Array */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	void SetBoolArrayField(const FString& FieldName, const TArray<bool>& BoolArray);

	/** Get the field named FieldName as an Object Array. Use it only if you're sure that array is uniform! */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	TArray<USIOJJsonObject*> GetObjectArrayField(const FString& FieldName);

	/** Set an ObjectField named FieldName and value of Ob Array */
	UFUNCTION(BlueprintCallable, Category = "SIOJ|Json")
	void SetObjectArrayField(const FString& FieldName, const TArray<USIOJJsonObject*>& ObjectArray);


	//////////////////////////////////////////////////////////////////////////
	// Data

private:
	/** Internal JSON data */
	TSharedPtr<FJsonObject> JsonObj;

};