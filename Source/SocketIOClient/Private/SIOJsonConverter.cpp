// Fill out your copyright notice in the Description page of Project Settings.

#include "SocketIOClientPrivatePCH.h"
#include "SIOJsonConverter.h"

TSharedPtr<FJsonValue> USIOJsonConverter::ToJsonValue(const sio::message::ptr& Message)
{
	auto flag = Message->get_flag();

	if (flag == sio::message::flag_integer)
	{
		return MakeShareable(new FJsonValueNumber(Message->get_int()));
	}
	else if (flag == sio::message::flag_double)
	{
		return MakeShareable(new FJsonValueNumber(Message->get_double()));
	}
	else if (flag == sio::message::flag_string)
	{
		return MakeShareable(new FJsonValueString(FStringFromStd(Message->get_string())));
	}
	else if (flag == sio::message::flag_binary)
	{
		//Todo: add support for this
		return MakeShareable(new FJsonValueString(FString("<binary not supported in FJsonValue>")));
	}
	else if (flag == sio::message::flag_array)
	{
		auto MessageVector = Message->get_vector();
		TArray< TSharedPtr<FJsonValue> > InArray;

		InArray.Reset(MessageVector.size());

		for (auto ItemMessage : MessageVector)
		{
			InArray.Add(ToJsonValue(ItemMessage));
		}
		
		return MakeShareable(new FJsonValueArray(InArray));
	}
	else if (flag == sio::message::flag_object)
	{
		auto  MessageMap = Message->get_map();
		TSharedPtr<FJsonObject> InObject = MakeShareable(new FJsonObject());

		for (auto MapPair : MessageMap)
		{
			InObject->SetField(FStringFromStd(MapPair.first), ToJsonValue(MapPair.second));
		}

		return MakeShareable(new FJsonValueObject(InObject));
	}
	else if (flag == sio::message::flag_boolean)
	{
		bool InBoolean = false;

		return MakeShareable(new FJsonValueBoolean(InBoolean));
	}
	else if (flag == sio::message::flag_null)
	{
		return MakeShareable(new FJsonValueNull());
	}
	else 
	{
		return MakeShareable(new FJsonValueNull());
	}
}

sio::message::ptr USIOJsonConverter::ToSIOMessage(const TSharedPtr<FJsonValue>& JsonValue)
{
	if (JsonValue->Type == EJson::None)
	{
		return sio::null_message::create();
	}
	else if (JsonValue->Type == EJson::Null)
	{
		return sio::null_message::create();
	}
	else if (JsonValue->Type == EJson::String)
	{
		return sio::string_message::create(StdString(JsonValue->AsString()));
	}
	else if (JsonValue->Type == EJson::Number)
	{
		return sio::double_message::create(JsonValue->AsNumber());
	}
	else if (JsonValue->Type == EJson::Boolean)
	{
		return sio::bool_message::create(JsonValue->AsBool());
	}
	else if (JsonValue->Type == EJson::Array)
	{
		auto ValueArray = JsonValue->AsArray();
		auto ArrayMessage = sio::array_message::create();
		auto ArrayVector = ArrayMessage->get_vector();

		for (auto ItemValue : ValueArray)
		{
			ArrayVector.push_back(ToSIOMessage(ItemValue));
		}

		return ArrayMessage;
	}
	else if (JsonValue->Type == EJson::Object)
	{
		auto ValueTmap = JsonValue->AsObject()->Values;

		auto ObjectMessage = sio::object_message::create();
		auto ObjectMap = ObjectMessage->get_map();

		for (auto ItemPair : ValueTmap)
		{
			ObjectMap.at(StdString(ItemPair.Key)) = ToSIOMessage(ItemPair.Value);
		}

		return ObjectMessage;
	}
	else
	{
		return sio::null_message::create();
	}
}

std::string USIOJsonConverter::StdString(FString UEString)
{
	return std::string(TCHAR_TO_UTF8(*UEString));	//TCHAR_TO_ANSI try this string instead?
}

FString USIOJsonConverter::FStringFromStd(std::string StdString)
{
	return FString(StdString.c_str());
}