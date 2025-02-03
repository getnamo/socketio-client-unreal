// Copyright 2018-current Getnamo. All Rights Reserved


#include "SocketIOClientComponent.h"
#include "SIOJConvert.h"
#include "SIOMessageConvert.h"
#include "SIOJRequestJSON.h"
#include "SocketIOClient.h"
#include "Engine/Engine.h"
#include "sio_message.h"


USocketIOClientComponent::USocketIOClientComponent(const FObjectInitializer& init) : UActorComponent(init)
{
	bWantsInitializeComponent = true;
	bAutoActivate = true;

	bForceTLS = false;
	bUnbindEventsOnDisconnect = false;
	bShouldVerifyTLSCertificate = false;	//Until verification feature is implemented, this should default to false
	bShouldAutoConnect = true;
	NativeClient = nullptr;
	bLimitConnectionToGameWorld = true;
	SessionId = TEXT("Invalid");

	//Plugin scoped utilities
	bPluginScopedConnection = false;
	PluginScopedId = TEXT("Default");
	bVerboseConnectionLog = true;
	ReconnectionTimeout = 0.f;
	MaxReconnectionAttempts = -1.f;
	ReconnectionDelayInMs = 5000;

	bStaticallyInitialized = false;

	ClearCallbacks();
}

void USocketIOClientComponent::StaticInitialization(UObject* WorldContextObject, bool bValidOwnerWorld /*= false*/)
{
	bStaticallyInitialized = true;

	if (!bValidOwnerWorld)
	{
		//Need to allow connections to non-owner worlds.
		bLimitConnectionToGameWorld = false;

		//The auto-connect will never happen in this case, so disable for clarity
		bShouldAutoConnect = false;
	}

	//We statically initialize for all cases
	InitializeNative();
}

void USocketIOClientComponent::InitializeComponent()
{
	Super::InitializeComponent();

	if (!bStaticallyInitialized)
	{
		InitializeNative();
	}
}

void USocketIOClientComponent::InitializeNative()
{
	if (bPluginScopedConnection)
	{
		NativeClient = ISocketIOClientModule::Get().ValidSharedNativePointer(PluginScopedId, bForceTLS, bShouldVerifyTLSCertificate);

		//Enforcement: This is the default FSocketIONative option value, but this component depends on it being true.
		NativeClient->bCallbackOnGameThread = true;
	}
	else
	{
		NativeClient = ISocketIOClientModule::Get().NewValidNativePointer(bForceTLS, bShouldVerifyTLSCertificate);
	}

	SetupCallbacks();
}

void USocketIOClientComponent::BeginPlay()
{
	Super::BeginPlay();

	//Auto-connect to default address if supported and not already connected
	if (bShouldAutoConnect && !bIsConnected)
	{
		ConnectWithParams(URLParams);
	}
}

USocketIOClientComponent::~USocketIOClientComponent()
{
	ClearCallbacks();

	//If we're a regular connection we should close and release when we quit
	if (!bPluginScopedConnection && NativeClient.IsValid())
	{
		ISocketIOClientModule::Get().ReleaseNativePointer(NativeClient);
		NativeClient = nullptr;
	}
}

void USocketIOClientComponent::UninitializeComponent()
{
	//Because our connections can last longer than game world 
	//end, we let plugin-scoped structures manage our memory.
	//We must ensure we set our pointer to null however.

	ClearCallbacks();

	//If we're a regular connection we should close and release when we quit
	if (!bPluginScopedConnection)
	{
		ISocketIOClientModule::Get().ReleaseNativePointer(NativeClient);
		NativeClient = nullptr;
	}

	Super::UninitializeComponent();
}

void USocketIOClientComponent::SetupCallbacks()
{
	//Sync current connected state
	bIsConnected = NativeClient->bIsConnected;
	if (bIsConnected)
	{
		SessionId = NativeClient->SessionId;
		URLParams = NativeClient->URLParams;
	}

	NativeClient->OnConnectedCallback = [this](const FString& InSocketId, const FString& InSessionId)
		{
			if (NativeClient.IsValid())
			{
				bIsConnected = true;
				SocketId = InSocketId;
				SessionId = InSessionId;
				bool bIsReconnection = bIsHavingConnectionProblems;
				bIsHavingConnectionProblems = false;
				OnConnected.Broadcast(SocketId, SessionId, bIsReconnection);

			}
		};

	const FSIOCCloseEventSignature OnDisconnectedSafe = OnDisconnected;

	NativeClient->OnDisconnectedCallback = [OnDisconnectedSafe, this](const ESIOConnectionCloseReason Reason)
		{
			if (NativeClient.IsValid())
			{
				bIsConnected = false;
				OnDisconnectedSafe.Broadcast(Reason);
			}
		};

	NativeClient->OnNamespaceConnectedCallback = [this](const FString& Namespace)
		{
			if (NativeClient.IsValid())
			{
				OnSocketNamespaceConnected.Broadcast(Namespace);
			}
		};

	const FSIOCSocketEventSignature OnSocketNamespaceDisconnectedSafe = OnSocketNamespaceDisconnected;

	NativeClient->OnNamespaceDisconnectedCallback = [this, OnSocketNamespaceDisconnectedSafe](const FString& Namespace)
		{

			if (NativeClient.IsValid())
			{
				OnSocketNamespaceDisconnectedSafe.Broadcast(Namespace);
			}
		};
	NativeClient->OnReconnectionCallback = [this](const uint32 AttemptCount, const uint32 DelayInMs)
		{
			if (NativeClient.IsValid())
			{
				//First time we know about this problem?
				if (!bIsHavingConnectionProblems)
				{
					TimeWhenConnectionProblemsStarted = FDateTime::Now();
					bIsHavingConnectionProblems = true;
				}

				FTimespan Difference = FDateTime::Now() - TimeWhenConnectionProblemsStarted;
				float ElapsedInSec = Difference.GetTotalSeconds();

				if (ReconnectionTimeout > 0 && ElapsedInSec > ReconnectionTimeout)
				{
					//Let's stop trying and disconnect if we're using timeouts
					Disconnect();
				}
				OnConnectionProblems.Broadcast(AttemptCount, DelayInMs, ElapsedInSec);
			}
		};

	NativeClient->OnFailCallback = [this]()
		{
			if (NativeClient.IsValid())
			{
				OnFail.Broadcast();
			};
		};
}

void USocketIOClientComponent::ClearCallbacks()
{
	if (NativeClient.IsValid())
	{
		NativeClient->ClearAllCallbacks();
	}
}


bool USocketIOClientComponent::CallBPFunctionWithResponse(UObject* Target, const FString& FunctionName, TArray<TSharedPtr<FJsonValue>> Response)
{
	if (!Target->IsValidLowLevel())
	{
		UE_LOG(SocketIO, Warning, TEXT("CallFunctionByNameWithArguments: Target not found for '%s'"), *FunctionName);
		return false;
	}
	UWorld* World = GEngine->GetWorldFromContextObject(Target, EGetWorldErrorMode::LogAndReturnNull);
	if (!World)
	{
		UE_LOG(SocketIO, Log, TEXT("World is invalid, %s BP function call ignored."), *FunctionName);
		return false;
	}
	if (World && World->bIsTearingDown)
	{
		UE_LOG(SocketIO, Log, TEXT("World tearing down, %s BP function call ignored."), *FunctionName);
		return false;
	}

	UFunction* Function = Target->FindFunction(FName(*FunctionName));
	if (nullptr == Function)
	{
		UE_LOG(SocketIO, Warning, TEXT("CallFunctionByNameWithArguments: Function not found '%s'"), *FunctionName);
		return false;
	}

	//Check function signature
	TFieldIterator<FProperty> Iterator(Function);

	TArray<FProperty*> Properties;
	while (Iterator && (Iterator->PropertyFlags & CPF_Parm))
	{
		FProperty* Prop = *Iterator;
		Properties.Add(Prop);
		++Iterator;
	}

	auto ResponseJsonValue = USIOJConvert::ToSIOJsonValue(Response);

	bool bResponseNumNotZero = Response.Num() > 0;
	bool bNoFunctionParams = Properties.Num() == 0;
	bool bNullResponse = ResponseJsonValue->IsNull();

	if (bNullResponse && bNoFunctionParams)
	{
		Target->ProcessEvent(Function, nullptr);
		return true;
	}
	else if (bResponseNumNotZero)
	{
		//function has too few params
		if (bNoFunctionParams)
		{
			UE_LOG(SocketIO, Warning, TEXT("CallFunctionByNameWithArguments: Function '%s' has too few parameters, callback parameters ignored : <%s>"), *FunctionName, *ResponseJsonValue->EncodeJson());
			Target->ProcessEvent(Function, nullptr);
			return true;
		}
		struct FDynamicArgs
		{
			void* Arg01 = nullptr;
			USIOJsonValue* Arg02 = nullptr;
		};
		//create the container
		FDynamicArgs Args = FDynamicArgs();

		//add the full response array as second param
		Args.Arg02 = ResponseJsonValue;
		const FString& FirstParam = Properties[0]->GetCPPType();
		auto FirstFJsonValue = Response[0];

		//Is first param...
		//SIOJsonValue?
		if (FirstParam.Equals("USIOJsonValue*"))
		{
			//convenience wrapper, response is a single object
			USIOJsonValue* Value = NewObject<USIOJsonValue>();
			Value->SetRootValue(FirstFJsonValue);
			Args.Arg01 = Value;
			Target->ProcessEvent(Function, &Args);
			return true;
		}
		//SIOJsonObject?
		else if (FirstParam.Equals("USIOJsonObject*"))
		{
			//convenience wrapper, response is a single object
			USIOJsonObject* ObjectValue = NewObject<USIOJsonObject>();
			ObjectValue->SetRootObject(FirstFJsonValue->AsObject());
			Args.Arg01 = ObjectValue;
			Target->ProcessEvent(Function, &Args);
			return true;
		}
		//String?
		else if (FirstParam.Equals("FString"))
		{
			FString	StringValue = USIOJConvert::ToJsonString(FirstFJsonValue);

			Target->ProcessEvent(Function, &StringValue);
			return true;
		}
		//Float?
		else if (FirstParam.Equals("float"))
		{
			float NumberValue = (float)FirstFJsonValue->AsNumber();
			Target->ProcessEvent(Function, &NumberValue);
			return true;
		}
		//Int?
		else if (FirstParam.Equals("int32"))
		{
			int NumberValue = (int)FirstFJsonValue->AsNumber();
			Target->ProcessEvent(Function, &NumberValue);
			return true;
		}
		//bool?
		else if (FirstParam.Equals("bool"))
		{
			bool BoolValue = FirstFJsonValue->AsBool();
			Target->ProcessEvent(Function, &BoolValue);
			return true;
		}
		//array?
		else if (FirstParam.Equals("TArray"))
		{
			FArrayProperty* ArrayProp = CastField<FArrayProperty>(Properties[0]);

			FString Inner;
			ArrayProp->GetCPPMacroType(Inner);

			//byte array is the only supported version
			if (Inner.Equals("uint8"))
			{
				TArray<uint8> Bytes = ResponseJsonValue->AsArray()[0]->AsBinary();
				Target->ProcessEvent(Function, &Bytes);
				return true;
			}
		}
	}

	UE_LOG(SocketIO, Warning, TEXT("CallFunctionByNameWithArguments: Function '%s' signature not supported expected <%s>"), *FunctionName, *ResponseJsonValue->EncodeJson());
	return false;
}

bool USocketIOClientComponent::CallBPFunctionWithMessage(UObject* Target, const FString& FunctionName, TSharedPtr<FJsonValue> Message)
{
	TArray<TSharedPtr<FJsonValue>> Response;
	Response.Add(Message);

	return CallBPFunctionWithResponse(Target, FunctionName, Response);
}

#if PLATFORM_WINDOWS
#pragma region Connect
#endif

void USocketIOClientComponent::Connect(const FString& InAddressAndPort, const FString& InPath, const FString& InAuthToken, USIOJsonObject* Query /*= nullptr*/, USIOJsonObject* Headers /*= nullptr*/)
{
	//Check if we're limiting this component
	if (bLimitConnectionToGameWorld)
	{
		UWorld* World = GEngine->GetWorldFromContextObject(this, EGetWorldErrorMode::LogAndReturnNull);
		if (World)
		{
			bool bIsGameWorld = (World->IsGameWorld() || World->IsPreviewWorld());
			if (!bIsGameWorld)
			{
				UE_LOG(SocketIO, Log, TEXT("USocketIOClientComponent::Connect attempt in non-game world blocked by bLimitConnectionToGameWorld."));
				return;
			}
		}
		else
		{
			UE_LOG(SocketIO, Warning, TEXT("USocketIOClientComponent::Connect attempt while in invalid world."));
			return;
		}
	}

	//NB: Only if the address and port is not empty do we use any of the other parameters
	if (!InAddressAndPort.IsEmpty())
	{
		URLParams.AddressAndPort = InAddressAndPort;

		TSharedPtr<FJsonObject> QueryFJson;
		TSharedPtr<FJsonObject> HeadersFJson;

		if (Query != nullptr)
		{
			QueryFJson = Query->GetRootObject();
		}

		if (Headers != nullptr)
		{
			HeadersFJson = Headers->GetRootObject();
		}

		URLParams.Query = USIOMessageConvert::JsonObjectToFStringMap(QueryFJson);
		URLParams.Headers = USIOMessageConvert::JsonObjectToFStringMap(HeadersFJson);
		URLParams.Path = InPath;
	}

	if (!InAuthToken.IsEmpty())
	{
		URLParams.AuthToken = InAuthToken;
	}

	//Sync all params to native client before connecting
	NativeClient->MaxReconnectionAttempts = MaxReconnectionAttempts;
	NativeClient->ReconnectionDelay = ReconnectionDelayInMs;
	NativeClient->VerboseLog = bVerboseConnectionLog;
	NativeClient->bUnbindEventsOnDisconnect = bUnbindEventsOnDisconnect;
	NativeClient->bForceTLSUse = bForceTLS;

	ConnectWithParams(URLParams);
}

void USocketIOClientComponent::ConnectWithParams(const FSIOConnectParams& InURLParams)
{
	NativeClient->Connect(InURLParams);
}

void USocketIOClientComponent::ConnectNative(const FString& InAddressAndPort,
	const FString& InPath,
	const FString& InAuthToken,
	const TSharedPtr<FJsonObject>& Query /*= nullptr*/,
	const TSharedPtr<FJsonObject>& Headers /*= nullptr*/)
{
	FSIOConnectParams Params;
	Params.AddressAndPort = InAddressAndPort;
	Params.Path = InPath;
	Params.AuthToken = InAuthToken;

	Params.Query = USIOMessageConvert::JsonObjectToFStringMap(Query);
	Params.Headers = USIOMessageConvert::JsonObjectToFStringMap(Headers);

	ConnectWithParams(Params);
}

void USocketIOClientComponent::Disconnect()
{
	NativeClient->Disconnect();
}

void USocketIOClientComponent::SyncDisconnect()
{
	NativeClient->SyncDisconnect();
}

void USocketIOClientComponent::JoinNamespace(const FString& Namespace)
{
	NativeClient->JoinNamespace(Namespace);
}

void USocketIOClientComponent::LeaveNamespace(const FString& Namespace)
{
	NativeClient->LeaveNamespace(Namespace);
}

#if PLATFORM_WINDOWS
#pragma endregion Connect
#pragma region Emit
#endif

void USocketIOClientComponent::Emit(const FString& EventName, USIOJsonValue* Message, const FString& Namespace /*= FString(TEXT("/"))*/)
{
	//Set the message is not null
	TSharedPtr<FJsonValue> JsonMessage = nullptr;
	if (Message != nullptr)
	{
		JsonMessage = Message->GetRootValue();
	}
	else
	{
		JsonMessage = MakeShareable(new FJsonValueNull);
	}

	NativeClient->Emit(EventName, JsonMessage, nullptr, Namespace);
}

void USocketIOClientComponent::EmitMessages(const FString& EventName, const TArray<USIOJsonValue*>& Messages, const FString& Namespace)
{
	if (Messages.Num() <= 0)
	{
		NativeClient->Emit(EventName, MakeShareable(new FJsonValueNull), nullptr, Namespace);
		return;
	}

	sio::message::list* MessageList = new sio::message::list();

	for (int i = 0; i < Messages.Num(); i++)
	{
		auto Message = Messages[i];
		TSharedPtr<FJsonValue> JsonMessage = nullptr;

		if (Message != nullptr)
		{
			JsonMessage = Message->GetRootValue();
		}
		else
		{
			JsonMessage = MakeShareable(new FJsonValueNull);
		}

		MessageList->push(USIOMessageConvert::ToSIOMessage(JsonMessage));
	}

	NativeClient->EmitRaw(EventName, *MessageList, nullptr, Namespace);

	delete MessageList;
}

void USocketIOClientComponent::EmitWithCallBack(const FString& EventName, USIOJsonValue* Message /*= nullptr*/, const FString& CallbackFunctionName /*= FString(TEXT(""))*/, UObject* Target /*= nullptr*/, const FString& Namespace /*= FString(TEXT("/"))*/, UObject* WorldContextObject /*= nullptr*/)
{
	if (!CallbackFunctionName.IsEmpty())
	{
		if (Target == nullptr)
		{
			Target = WorldContextObject;
		}

		//Set the message is not null
		TSharedPtr<FJsonValue> JsonMessage = nullptr;
		if (Message != nullptr)
		{
			JsonMessage = Message->GetRootValue();
		}
		else
		{
			JsonMessage = MakeShareable(new FJsonValueNull);
		}

		EmitNative(EventName, JsonMessage, [&, Target, CallbackFunctionName, this](const TArray<TSharedPtr<FJsonValue>>& Response)
			{
				CallBPFunctionWithResponse(Target, CallbackFunctionName, Response);
			}, Namespace);
	}
	else
	{
		EmitNative(EventName, Message->GetRootValue(), nullptr, Namespace);
	}
}

void USocketIOClientComponent::EmitWithGraphCallBack(const FString& EventName, struct FLatentActionInfo LatentInfo, USIOJsonValue*& Result, USIOJsonValue* Message /*= nullptr*/, const FString& Namespace /*= FString(TEXT("/"))*/)
{
	//Set the message is not null
	TSharedPtr<FJsonValue> JsonMessage = nullptr;
	if (Message != nullptr)
	{
		JsonMessage = Message->GetRootValue();
	}
	else
	{
		JsonMessage = MakeShareable(new FJsonValueNull);
	}
	FCULatentAction* LatentAction = FCULatentAction::CreateLatentAction(LatentInfo, this);

	if (LatentAction)
	{
		//emit the message and pass the LatentAction, we also pass the result reference through lambda capture
		NativeClient->Emit(EventName, JsonMessage, [this, LatentAction, &Result](const TArray<TSharedPtr<FJsonValue>>& Response)
			{
				// Finish the latent action
				if (LatentAction)
				{
					TSharedPtr<FJsonValue> FirstResponseValue = Response[0];
					USIOJsonValue* ResultObj = NewObject<USIOJsonValue>();
					ResultObj->SetRootValue(FirstResponseValue);
					Result = ResultObj;		//update the output value
					LatentAction->Call();	//resume the latent action
				}
			}, Namespace);
	}
}

void USocketIOClientComponent::EmitMessagesWithCallBack(const FString& EventName, const TArray<USIOJsonValue*>& Messages, const FString& CallbackFunctionName, UObject* Target, const FString& Namespace, UObject* WorldContextObject)
{
	sio::message::list* MessageList = new sio::message::list();

	for (int i = 0; i < Messages.Num(); i++)
	{
		auto Message = Messages[i];
		TSharedPtr<FJsonValue> JsonMessage = nullptr;

		if (Message != nullptr)
		{
			JsonMessage = Message->GetRootValue();
		}
		else
		{
			JsonMessage = MakeShareable(new FJsonValueNull);
		}

		MessageList->push(USIOMessageConvert::ToSIOMessage(JsonMessage));
	}

	if (CallbackFunctionName.IsEmpty()) {
		NativeClient->EmitRaw(EventName, *MessageList, nullptr, Namespace);
	}
	else {
		// TODO: Update CallBPFunctionWithResponse to support NativeClient->EmitRaw.
		//NativeClient->EmitRaw(EventName, *MessageList, [&, Target, CallbackFunctionName, this](const sio::message::list& Response)
		//	{
		//		CallBPFunctionWithResponse(Target, CallbackFunctionName, Response);
		//	}, Namespace);
	}

	delete MessageList;
}

void USocketIOClientComponent::EmitNative(const FString& EventName, const TSharedPtr<FJsonValue>& Message /*= nullptr*/, TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction /*= nullptr*/, const FString& Namespace /*= FString(TEXT("/"))*/)
{
	NativeClient->Emit(EventName, Message, CallbackFunction, Namespace);
}

void USocketIOClientComponent::EmitNative(const FString& EventName, const TSharedPtr<FJsonObject>& ObjectMessage /*= nullptr*/, TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction /*= nullptr*/, const FString& Namespace /*= FString(TEXT("/"))*/)
{
	EmitNative(EventName, MakeShareable(new FJsonValueObject(ObjectMessage)), CallbackFunction, Namespace);
}

void USocketIOClientComponent::EmitNative(const FString& EventName, const FString& StringMessage /*= FString()*/, TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction /*= nullptr*/, const FString& Namespace /*= FString(TEXT("/"))*/)
{
	EmitNative(EventName, MakeShareable(new FJsonValueString(StringMessage)), CallbackFunction, Namespace);
}

void USocketIOClientComponent::EmitNative(const FString& EventName, double NumberMessage, TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction /*= nullptr*/, const FString& Namespace /*= FString(TEXT("/"))*/)
{
	EmitNative(EventName, MakeShareable(new FJsonValueNumber(NumberMessage)), CallbackFunction, Namespace);
}

void USocketIOClientComponent::EmitNative(const FString& EventName, const TArray<uint8>& BinaryMessage, TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction /*= nullptr*/, const FString& Namespace /*= FString(TEXT("/"))*/)
{
	EmitNative(EventName, MakeShareable(new FJsonValueBinary(BinaryMessage)), CallbackFunction, Namespace);
}

void USocketIOClientComponent::EmitNative(const FString& EventName, const TArray<TSharedPtr<FJsonValue>>& ArrayMessage, TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction /*= nullptr*/, const FString& Namespace /*= FString(TEXT("/"))*/)
{
	EmitNative(EventName, MakeShareable(new FJsonValueArray(ArrayMessage)), CallbackFunction, Namespace);
}

void USocketIOClientComponent::EmitNative(const FString& EventName, bool BooleanMessage, TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction /*= nullptr*/, const FString& Namespace /*= FString(TEXT("/"))*/)
{
	EmitNative(EventName, MakeShareable(new FJsonValueBoolean(BooleanMessage)), CallbackFunction, Namespace);
}

void USocketIOClientComponent::EmitNative(const FString& EventName, UStruct* Struct, const void* StructPtr, TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction /*= nullptr*/, const FString& Namespace /*= FString(TEXT("/"))*/)
{
	EmitNative(EventName, USIOJConvert::ToJsonObject(Struct, (void*)StructPtr), CallbackFunction, Namespace);
}

void USocketIOClientComponent::EmitNative(const FString& EventName, const SIO_TEXT_TYPE StringMessage /*= TEXT("")*/, TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction /*= nullptr*/, const FString& Namespace /*= FString(TEXT("/"))*/)
{
	EmitNative(EventName, MakeShareable(new FJsonValueString(FString(StringMessage))), CallbackFunction, Namespace);
}

#if PLATFORM_WINDOWS
#pragma endregion Emit
#pragma region OnEvents
#endif

void USocketIOClientComponent::BindEventToGenericEvent(const FString& EventName, const FString& Namespace)
{
	NativeClient->OnEvent(EventName, [&](const FString& Event, const TSharedPtr<FJsonValue>& EventValue)
		{
			USIOJsonValue* NewValue = NewObject<USIOJsonValue>();
			TSharedPtr<FJsonValue> NonConstValue = EventValue;
			NewValue->SetRootValue(NonConstValue);
			OnGenericEvent.Broadcast(Event, NewValue);
		}, Namespace);
}

void USocketIOClientComponent::BindEventToDelegate(const FString& EventName,
	const FSIOJsonValueSignature& CallbackDelegate,
	const FString& Namespace /*= TEXT("/")*/,
	ESIOThreadOverrideOption ThreadOverride /*= USE_DEFAULT*/)
{
	const FSIOJsonValueSignature SafeCallback = CallbackDelegate;	//copy for lambda ref
	OnNativeEvent(EventName, [&, SafeCallback](const FString& Event, const TSharedPtr<FJsonValue>& Message)
		{
			USIOJsonValue* Value = NewObject<USIOJsonValue>();
			TSharedPtr<FJsonValue> NonConstValue = Message;
			Value->SetRootValue(NonConstValue);

			SafeCallback.ExecuteIfBound(Value);
		}, Namespace, ThreadOverride);
}



void USocketIOClientComponent::BindEventToFunction(const FString& EventName,
	const FString& FunctionName,
	UObject* Target,
	const FString& Namespace /*= FString(TEXT("/"))*/,
	ESIOThreadOverrideOption ThreadOverride /*= USE_DEFAULT*/,
	UObject* WorldContextObject /*= nullptr*/)
{
	if (!FunctionName.IsEmpty())
	{
		if (Target == nullptr)
		{
			Target = WorldContextObject;
		}
		OnNativeEvent(EventName, [&, FunctionName, Target](const FString& Event, const TSharedPtr<FJsonValue>& Message)
			{
				CallBPFunctionWithMessage(Target, FunctionName, Message);
			}, Namespace, ThreadOverride);
	}
	else
	{
		//if we forgot our function name, fallback to regular bind event
		BindEventToGenericEvent(EventName, Namespace);
	}
}

void USocketIOClientComponent::UnbindEvent(const FString& EventName, const FString& Namespace/* = TEXT("/")*/)
{
	NativeClient->UnbindEvent(EventName, Namespace);
}

void USocketIOClientComponent::OnNativeEvent(const FString& EventName,
	TFunction< void(const FString&, const TSharedPtr<FJsonValue>&)> CallbackFunction,
	const FString& Namespace /*= FString(TEXT("/"))*/,
	ESIOThreadOverrideOption ThreadOverride /*= USE_DEFAULT*/)
{
	NativeClient->OnEvent(EventName, CallbackFunction, Namespace, ThreadOverride);
}

#if PLATFORM_WINDOWS
#pragma endregion OnEvents
#endif
