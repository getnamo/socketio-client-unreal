// Copyright 2018-current Getnamo. All Rights Reserved


#include "SocketIONative.h"
#include "SIOMessageConvert.h"
#include "CULambdaRunnable.h"
#include "SIOJConvert.h"
#include "sio_client.h"
#include "sio_message.h"
#include "sio_socket.h"

FSocketIONative::FSocketIONative(const bool bForceTLS, const bool bShouldVerifyTLSCertificate)
{
	PrivateClient = nullptr;
	SessionId = TEXT("Invalid");
	LastSessionId = TEXT("None");
	bIsConnected = false;
	MaxReconnectionAttempts = -1;
	ReconnectionDelay = 5000;
	bCallbackOnGameThread = true;
	bUnbindEventsOnDisconnect = false;
	bForceTLSUse = bForceTLS;
	InitPrivateClient(bForceTLS, bShouldVerifyTLSCertificate);

	ClearAllCallbacks();
}


void FSocketIONative::InitPrivateClient(const bool bShouldUseTlsLibraries /*= false*/, const bool bShouldVerifyTLSCertificate /*= false*/)
{
	bIsSetupForTLS = bShouldUseTlsLibraries;
	bUsingTLSCertVerification = bShouldVerifyTLSCertificate;
	PrivateClient = MakeShareable(new sio::client(bShouldUseTlsLibraries, bUsingTLSCertVerification));
}

void FSocketIONative::Connect(const FSIOConnectParams& InConnectParams)
{
	//Special case: reconnect on current settings if address is empty
	if (!InConnectParams.AddressAndPort.IsEmpty())
	{
		URLParams = InConnectParams;
	}

	SyncPrivateClientToTLSMode(URLParams.AddressAndPort);
	
	//Fill std types before going to background thread.

	std::string StdAddressString = USIOMessageConvert::StdString(URLParams.AddressAndPort);
	std::string StdPathString = USIOMessageConvert::StdString(URLParams.Path);
	std::map<std::string, std::string> QueryMap = {};
	std::map<std::string, std::string> HeadersMap = {};
	sio::message::ptr AuthMessage = sio::object_message::create();
	if (!URLParams.AuthToken.IsEmpty())
	{
		AuthMessage->get_map()["token"] = sio::string_message::create(USIOMessageConvert::StdString(URLParams.AuthToken));
	}
	if (URLParams.ExtraAuth.Num() > 0)
	{
		TArray<FString> Keys;
		URLParams.ExtraAuth.GetKeys(Keys);

		for (const FString& Key : Keys)
		{
			std::string StdKey = USIOMessageConvert::StdString(Key);
			std::string StdValue = USIOMessageConvert::StdString(URLParams.ExtraAuth[Key]);
			AuthMessage->get_map()[StdKey] = sio::string_message::create(StdValue);
		}
	}

	QueryMap = USIOMessageConvert::FStringMapToStdStringMap(URLParams.Query);
	HeadersMap = USIOMessageConvert::FStringMapToStdStringMap(URLParams.Headers);

	//Connect to the server on a background thread so it never blocks
	FCULambdaRunnable::RunLambdaOnBackGroundThread([&, StdAddressString, StdPathString, QueryMap, HeadersMap, AuthMessage]
	{
		PrivateClient->set_reconnect_attempts(MaxReconnectionAttempts);
		PrivateClient->set_reconnect_delay(ReconnectionDelay);
		PrivateClient->set_path(StdPathString);

		//close and reconnect if different url
		if(PrivateClient->opened())
		{
			if (PrivateClient->get_url() != StdAddressString)
			{
				//sync close to re-open
				PrivateClient->sync_close();
			}
			else
			{
				//we're already connected to the correct endpoint, ignore request
				UE_LOG(SocketIO, Warning, TEXT("Attempted to connect to %s when we're already connected. Request ignored."), UTF8_TO_TCHAR(StdAddressString.c_str()));
				return;
			}
		}
		PrivateClient->connect(StdAddressString, QueryMap, HeadersMap, AuthMessage);
	});
}

void FSocketIONative::Connect(const FString& InAddressAndPort)
{
	if (!InAddressAndPort.IsEmpty())
	{
		URLParams.AddressAndPort = InAddressAndPort;
	}

	Connect(URLParams);
}

void FSocketIONative::JoinNamespace(const FString& Namespace)
{
	//just referencing the namespace will join it
	PrivateClient->socket(USIOMessageConvert::StdString(Namespace));
}

void FSocketIONative::LeaveNamespace(const FString& Namespace)
{
	PrivateClient->socket(USIOMessageConvert::StdString(Namespace))->close();
}

void FSocketIONative::Disconnect()
{	
	//Ensure disconnected callback is called first because we 
	//clear all callbacks for stability.
	if (OnDisconnectedCallback)
	{
		OnDisconnectedCallback(ESIOConnectionCloseReason::CLOSE_REASON_NORMAL);
	}
	bIsConnected = false;

	if (bUnbindEventsOnDisconnect)
	{
		ClearAllCallbacks();
		PrivateClient->close();
	}
	else
	{
		//Clear and re-init map
		ClearInternalCallbacks();
		PrivateClient->close();
		RebindCurrentEventMap();
	}
}

void FSocketIONative::SyncDisconnect()
{
	//Ensure disconnected callback is called first because we 
	//clear all callbacks for stability.
	if (OnDisconnectedCallback)
	{
		OnDisconnectedCallback(ESIOConnectionCloseReason::CLOSE_REASON_NORMAL);
	}
	bIsConnected = false;

	if (bUnbindEventsOnDisconnect)
	{
		ClearAllCallbacks();
		PrivateClient->sync_close();
	}
	else
	{
		//Clear and re-init map
		ClearInternalCallbacks();
		PrivateClient->sync_close();
		RebindCurrentEventMap();
	}
}

void FSocketIONative::ClearAllCallbacks()
{
	PrivateClient->clear_socket_listeners();
	SetupInternalCallbacks();					//if clear socket listeners cleared our internal callbacks. reset them
	EventFunctionMap.Empty();

	OnConnectedCallback = nullptr;
	OnDisconnectedCallback = nullptr;
	OnNamespaceConnectedCallback = nullptr;
	OnNamespaceDisconnectedCallback = nullptr;
	OnReconnectionCallback = nullptr;
	OnFailCallback = nullptr;
}

void FSocketIONative::Emit(const FString& EventName, const TSharedPtr<FJsonValue>& Message /*= nullptr*/, TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction /*= nullptr*/, const FString& Namespace /*= FString(TEXT("/"))*/)
{
	TFunction<void(const sio::message::list&)> RawCallback = nullptr;

	//Only bind the raw callback if we pass in a callback ourselves;
	if (CallbackFunction)
	{
		RawCallback = [&, CallbackFunction](const sio::message::list& MessageList)
		{
			TArray<TSharedPtr<FJsonValue>> ValueArray;

			for (uint32 i = 0; i < MessageList.size(); i++)
			{
				auto ItemMessagePtr = MessageList[i];
				ValueArray.Add(USIOMessageConvert::ToJsonValue(ItemMessagePtr));
			}
			if (CallbackFunction)
			{
				CallbackFunction(ValueArray);
			}
		};
	}

	EmitRaw(
		EventName,
		USIOMessageConvert::ToSIOMessage(Message),
		RawCallback,
		Namespace);
}

void FSocketIONative::Emit(const FString& EventName, const TSharedPtr<FJsonObject>& ObjectMessage /*= nullptr*/, TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction /*= nullptr*/, const FString& Namespace /*= FString(TEXT("/"))*/)
{
	Emit(EventName, MakeShareable(new FJsonValueObject(ObjectMessage)), CallbackFunction, Namespace);
}

void FSocketIONative::Emit(const FString& EventName, const FString& StringMessage /*= FString()*/, TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction /*= nullptr*/, const FString& Namespace /*= FString(TEXT("/"))*/)
{
	Emit(EventName, MakeShareable(new FJsonValueString(StringMessage)), CallbackFunction, Namespace);
}

void FSocketIONative::Emit(const FString& EventName, double NumberMessage, TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction /*= nullptr*/, const FString& Namespace /*= FString(TEXT("/"))*/)
{
	Emit(EventName, MakeShareable(new FJsonValueNumber(NumberMessage)), CallbackFunction, Namespace);
}

void FSocketIONative::Emit(const FString& EventName, bool BooleanMessage, TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction /*= nullptr*/, const FString& Namespace /*= FString(TEXT("/"))*/)
{
	Emit(EventName, MakeShareable(new FJsonValueBoolean(BooleanMessage)), CallbackFunction, Namespace);
}

void FSocketIONative::Emit(const FString& EventName, const TArray<uint8>& BinaryMessage, TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction /*= nullptr*/, const FString& Namespace /*= FString(TEXT("/"))*/)
{
	Emit(EventName, MakeShareable(new FJsonValueBinary(BinaryMessage)), CallbackFunction, Namespace);
}

void FSocketIONative::Emit(const FString& EventName, const TArray<TSharedPtr<FJsonValue>>& ArrayMessage, TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction /*= nullptr*/, const FString& Namespace /*= FString(TEXT("/"))*/)
{
	Emit(EventName, MakeShareable(new FJsonValueArray(ArrayMessage)), CallbackFunction, Namespace);
}

void FSocketIONative::Emit(const FString& EventName, UStruct* Struct, const void* StructPtr, TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction /*= nullptr*/, const FString& Namespace /*= FString(TEXT("/"))*/)
{
	Emit(EventName, USIOJConvert::ToJsonObject(Struct, (void*)StructPtr), CallbackFunction, Namespace);
}

void FSocketIONative::Emit(const FString& EventName, TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction /*= nullptr*/, const FString& Namespace /*= TEXT("/")*/)
{
	TSharedPtr<FJsonValue> NoneValue;
	Emit(EventName, NoneValue, CallbackFunction, Namespace);
}

void FSocketIONative::Emit(const FString& EventName, const SIO_TEXT_TYPE StringMessage /*= TEXT("")*/, TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction /*= nullptr*/, const FString& Namespace /*= TEXT("/")*/)
{
	Emit(EventName, MakeShareable(new FJsonValueString(FString(StringMessage))), CallbackFunction, Namespace);
}

void FSocketIONative::EmitRaw(const FString& EventName, const sio::message::list& MessageList /*= nullptr*/, TFunction<void(const sio::message::list&)> CallbackFunction /*= nullptr*/, const FString& Namespace /*= FString(TEXT("/"))*/)
{
	std::function<void(sio::message::list const&)> RawCallback = nullptr;

	//Only have non-null raw callback if we pass in a callback function
	if (CallbackFunction)
	{
		RawCallback = [&, CallbackFunction](const sio::message::list& response)
		{
			if (CallbackFunction != nullptr)
			{
				//Callback on game thread
				if (bCallbackOnGameThread)
				{
					FCULambdaRunnable::RunShortLambdaOnGameThread([&, CallbackFunction, response]
					{
						if (CallbackFunction)
						{
							CallbackFunction(response);
						}
					});
				}
				else
				{
					CallbackFunction(response);
				}
			}
		};
	}

	PrivateClient->socket(USIOMessageConvert::StdString(Namespace))->emit(
		USIOMessageConvert::StdString(EventName),
		MessageList,
		RawCallback);
}

void FSocketIONative::EmitRawBinary(const FString& EventName, uint8* Data, int32 DataLength, const FString& Namespace /*= FString(TEXT("/"))*/)
{
	PrivateClient->socket(USIOMessageConvert::StdString(Namespace))->emit(USIOMessageConvert::StdString(EventName), std::make_shared<std::string>((char*)Data, DataLength));
}

void FSocketIONative::OnEvent(const FString& EventName, 
	TFunction< void(const FString&, const TSharedPtr<FJsonValue>&)> CallbackFunction, 
	const FString& Namespace /*= FString(TEXT("/"))*/,
	ESIOThreadOverrideOption CallbackThread /*= USE_DEFAULT*/)
{
	//Keep track of all the bound native JsonValue functions
	FSIOBoundEvent BoundEvent;
	BoundEvent.Function = CallbackFunction;
	BoundEvent.Namespace = Namespace;
	BoundEvent.ThreadOption = CallbackThread;
	EventFunctionMap.Add(EventName, BoundEvent);

	OnRawEvent(EventName, [&, CallbackFunction](const FString& Event, const sio::message::ptr& RawMessage) {
		CallbackFunction(Event, USIOMessageConvert::ToJsonValue(RawMessage));
	}, Namespace, CallbackThread);
}

void FSocketIONative::OnRawEvent(const FString& EventName, 
	TFunction< void(const FString&, const sio::message::ptr&)> CallbackFunction, 
	const FString& Namespace /*= FString(TEXT("/"))*/,
	ESIOThreadOverrideOption CallbackThread /*= USE_DEFAULT*/)
{
	if (CallbackFunction == nullptr)
	{
		PrivateClient->socket(USIOMessageConvert::StdString(Namespace))->off(USIOMessageConvert::StdString(EventName));
	}
	else
	{
		//determine thread override option
		bool bCallbackThisEventOnGameThread = bCallbackOnGameThread;
		switch (CallbackThread)
		{
		case USE_DEFAULT:
			break;
		case USE_GAME_THREAD:
			bCallbackThisEventOnGameThread = true;
			break;
		case USE_NETWORK_THREAD:
			bCallbackThisEventOnGameThread = false;
			break;
		default:
			break;
		}

		const TFunction< void(const FString&, const sio::message::ptr&)> SafeFunction = CallbackFunction;	//copy the function so it remains in context

		PrivateClient->socket(USIOMessageConvert::StdString(Namespace))->on(
			USIOMessageConvert::StdString(EventName),
			sio::socket::event_listener_aux(
			[&, SafeFunction, bCallbackThisEventOnGameThread](std::string const& name, sio::message::ptr const& data, bool isAck, sio::message::list &ack_resp)
			{
				if (SafeFunction != nullptr)
				{
					const FString SafeName = USIOMessageConvert::FStringFromStd(name);

					if (bCallbackThisEventOnGameThread)
					{
						FCULambdaRunnable::RunShortLambdaOnGameThread([&, SafeFunction, SafeName, data]
							{
								SafeFunction(SafeName, data);
							});
					}
					else
					{
						SafeFunction(SafeName, data);
					}
				}
			}));
	}
}

void FSocketIONative::OnRawBinaryEvent(const FString& EventName, TFunction< void(const FString&, const TArray<uint8>&)> CallbackFunction, const FString& Namespace /*= FString(TEXT("/"))*/)
{
	const TFunction< void(const FString&, const TArray<uint8>&)> SafeFunction = CallbackFunction;	//copy the function so it remains in context

	PrivateClient->socket(USIOMessageConvert::StdString(Namespace))->on(
		USIOMessageConvert::StdString(EventName),
		sio::socket::event_listener_aux(
			[&, SafeFunction](std::string const& name, sio::message::ptr const& data, bool isAck, sio::message::list &ack_resp)
	{
		const FString SafeName = USIOMessageConvert::FStringFromStd(name);

		//Construct raw buffer
		if (data->get_flag() == sio::message::flag_binary)
		{
			TArray<uint8> Buffer;
			int32 BufferSize = data->get_binary()->size();
			auto MessageBuffer = data->get_binary();
			Buffer.Append((uint8*)(MessageBuffer->data()), BufferSize);

			if (bCallbackOnGameThread)
			{
				FCULambdaRunnable::RunShortLambdaOnGameThread([&, SafeFunction, SafeName, Buffer]
				{
					SafeFunction(SafeName, Buffer);
				});
			}
			else
			{
				SafeFunction(SafeName, Buffer);
			}
		}
		else
		{
			UE_LOG(SocketIO, Warning, TEXT("Non-binary message received to binary message lambda, check server message data!"));
		}
	}));
}

void FSocketIONative::UnbindEvent(const FString& EventName, const FString& Namespace /*= TEXT("/")*/)
{
	OnRawEvent(EventName, nullptr, Namespace);
	EventFunctionMap.Remove(EventName);
}

void FSocketIONative::ClearInternalCallbacks()
{
	PrivateClient->clear_socket_listeners();
}

void FSocketIONative::SetupInternalCallbacks()
{
	PrivateClient->set_open_listener(sio::client::con_listener([&]() 
	{
		//too early to get session id here so we defer the connection event until we connect to a namespace
	}));

	PrivateClient->set_close_listener(sio::client::close_listener([&](sio::client::close_reason const& reason)
	{
		bIsConnected = false;

		ESIOConnectionCloseReason DisconnectReason = (ESIOConnectionCloseReason)reason;
		if (reason == NULL) {
			DisconnectReason = ESIOConnectionCloseReason::CLOSE_REASON_NORMAL;
		}

		if (VerboseLog)
		{
			FString DisconnectReasonString = UEnum::GetValueAsString<ESIOConnectionCloseReason>(DisconnectReason);
			UE_LOG(SocketIO, Log, TEXT("SocketIO Disconnected %s reason: %s"), *SessionId, *DisconnectReasonString);
		}
		
		LastSessionId = SessionId;
		SessionId = TEXT("Invalid");

		if (OnDisconnectedCallback)
		{
			if (bCallbackOnGameThread)
			{
				FCULambdaRunnable::RunShortLambdaOnGameThread([&, DisconnectReason]
				{
					if (OnDisconnectedCallback)
					{
						OnDisconnectedCallback(DisconnectReason);
					}
				});
			}
			else
			{
				OnDisconnectedCallback(DisconnectReason);
			}
		}
	}));

	PrivateClient->set_socket_open_listener(sio::client::socket_listener([&](std::string const& nsp)
	{
		//Special case, we have a latent connection after already having been disconnected
		if (!PrivateClient.IsValid())
		{
			return;
		}
		if (!bIsConnected)
		{
			bIsConnected = true;
			SessionId = USIOMessageConvert::FStringFromStd(PrivateClient->get_sessionid());
			SocketId = USIOMessageConvert::FStringFromStd(PrivateClient->socket(nsp)->get_socket_id());

			if (VerboseLog)
			{
				UE_LOG(SocketIO, Log, TEXT("SocketIO Connected with session: %s"), *SessionId);
			}
			if (OnConnectedCallback)
			{
				if (bCallbackOnGameThread)
				{
					FCULambdaRunnable::RunShortLambdaOnGameThread([&]
					{
						if (OnConnectedCallback)
						{
							OnConnectedCallback(SocketId, SessionId);
						}
					});
				}
				else
				{
					OnConnectedCallback(SocketId, SessionId);
				}
			}
		}

		const FString Namespace = USIOMessageConvert::FStringFromStd(nsp);

		if (VerboseLog)
		{
			UE_LOG(SocketIO, Log, TEXT("SocketIO %s connected to namespace: %s"), *SessionId, *Namespace);
		}
		if (OnNamespaceConnectedCallback)
		{
			if (bCallbackOnGameThread)
			{
				FCULambdaRunnable::RunShortLambdaOnGameThread([&, Namespace]
				{
					if (OnNamespaceConnectedCallback)
					{
						OnNamespaceConnectedCallback(Namespace);
					}
				});
			}
			else
			{
				OnNamespaceConnectedCallback(Namespace);
			}
		}
	}));

	PrivateClient->set_socket_close_listener(sio::client::socket_listener([&](std::string const& nsp)
	{
		const FString Namespace = USIOMessageConvert::FStringFromStd(nsp);
		FString NamespaceSession = SessionId;
		if (NamespaceSession.Equals(TEXT("Invalid")))
		{
			NamespaceSession = LastSessionId;
		}
		if (VerboseLog)
		{
			UE_LOG(SocketIO, Log, TEXT("SocketIO %s disconnected from namespace: %s"), *NamespaceSession, *Namespace);
		}
		if (OnNamespaceDisconnectedCallback)
		{
			if (bCallbackOnGameThread)
			{
				FCULambdaRunnable::RunShortLambdaOnGameThread([&, Namespace]
				{
					if (OnNamespaceDisconnectedCallback)
					{
						OnNamespaceDisconnectedCallback(Namespace);
					}
				});
			}
			else
			{
				OnNamespaceDisconnectedCallback(Namespace);
			}
		}
	}));

	PrivateClient->set_fail_listener(sio::client::con_listener([&]()
	{
		if (VerboseLog)
		{
			UE_LOG(SocketIO, Log, TEXT("SocketIO failed to connect."));
		}
		if (OnFailCallback)
		{
			if (bCallbackOnGameThread)
			{
				FCULambdaRunnable::RunShortLambdaOnGameThread([&]
				{
					if (OnFailCallback)
					{
						OnFailCallback();
					}
				});
			}
			else
			{
				OnFailCallback();
			}
		}
	}));

	PrivateClient->set_reconnect_listener(sio::client::reconnect_listener([&](unsigned num, unsigned delay)
	{
		bIsConnected = false;

		if (VerboseLog)
		{
			UE_LOG(SocketIO, Log, TEXT("SocketIO %s appears to have lost connection, reconnecting attempt %d with delay %d"), *SessionId, num, delay);
		}
		if (OnReconnectionCallback)
		{
			if (bCallbackOnGameThread)
			{
				FCULambdaRunnable::RunShortLambdaOnGameThread([&, num, delay]
				{
					if (OnReconnectionCallback)
					{
						OnReconnectionCallback(num, delay);
					}
				});
			}
			else
			{
				OnReconnectionCallback(num, delay);
			}
		}
	}));
}

void FSocketIONative::RebindCurrentEventMap()
{
	ClearInternalCallbacks();

	for (auto& EventPair : EventFunctionMap)
	{
		const FString& EventName = EventPair.Key;
		const FSIOBoundEvent EventBind = EventPair.Value;

		OnRawEvent(EventName, [&, EventBind](const FString& Event, const sio::message::ptr& RawMessage) {
			EventBind.Function(Event, USIOMessageConvert::ToJsonValue(RawMessage));
		}, EventBind.Namespace, EventBind.ThreadOption);
	}

	SetupInternalCallbacks();
}

bool FSocketIONative::IsTLSURL(const FString& URL)
{
	return URL.StartsWith(TEXT("https://")) || URL.StartsWith(TEXT("wss://"));
}

void FSocketIONative::SyncPrivateClientToTLSMode(const FString& URL)
{
	//Should be in TLS mode?
	if (IsTLSURL(URL) || bForceTLSUse)
	{
		//needs to swap to TLS
		if (!bIsSetupForTLS)
		{
			if (PrivateClient->opened())
			{
				PrivateClient->sync_close();
			}
			ClearInternalCallbacks();
			InitPrivateClient(true, bUsingTLSCertVerification);
			RebindCurrentEventMap();
		}
	}
	//Should not be in TLS mode
	else
	{
		//URL is not TLS, but we are setup for it
		if (bIsSetupForTLS)
		{
			if (PrivateClient->opened())
			{
				PrivateClient->sync_close();
			}
			ClearInternalCallbacks();
			InitPrivateClient(false, bUsingTLSCertVerification);
			RebindCurrentEventMap();
		}
	}
}

