// Copyright 2018-current Getnamo. All Rights Reserved


#pragma once

#include "Components/ActorComponent.h"
#include "SocketIONative.h"
#include "Runtime/Engine/Classes/Engine/LatentActionManager.h"
#include "SocketIOClientComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSIOCEventSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSIOCSocketEventSignature, FString, Namespace);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FSIOCOpenEventSignature, FString, SocketId, FString, SessionId, bool, bIsReconnection);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSIOCCloseEventSignature, TEnumAsByte<ESIOConnectionCloseReason>, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSIOCEventJsonSignature, FString, EventName, class USIOJsonValue*, EventData);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FSIOConnectionProblemSignature, int32, Attempts, int32, NextAttemptInMs, float, TimeSinceConnected);

//For Direct Delegate Event Bind
DECLARE_DYNAMIC_DELEGATE_OneParam(FSIOJsonValueSignature, USIOJsonValue*, EventData);

UCLASS(BlueprintType, ClassGroup = "Networking", meta = (BlueprintSpawnableComponent))
class SOCKETIOCLIENT_API USocketIOClientComponent : public UActorComponent
{
	GENERATED_UCLASS_BODY()
public:

	//Async events

	/** On generic bound event received. Requires Bind Event to Generic Event to be called before. Will not receive Bind Event To Function events. */
	UPROPERTY(BlueprintAssignable, Category = "SocketIO Events")
	FSIOCEventJsonSignature OnGenericEvent;

	/** Received on socket.io connection established. */
	UPROPERTY(BlueprintAssignable, Category = "SocketIO Events")
	FSIOCOpenEventSignature OnConnected;

	/**
	* Received on socket.io connection disconnected. This may never get
	* called in default settings, see OnConnectionProblems event for details.
	*/
	UPROPERTY(BlueprintAssignable, Category = "SocketIO Events")
	FSIOCCloseEventSignature OnDisconnected;

	/**
	* Received when connection problems arise. In default settings the
	* connection will keep repeating trying to reconnect an infinite
	* amount of times and you may never get OnDisconnected callback
	* unless you call it.
	*/
	UPROPERTY(BlueprintAssignable, Category = "SocketIO Events")
	FSIOConnectionProblemSignature OnConnectionProblems;

	/** Received on having joined namespace. */
	UPROPERTY(BlueprintAssignable, Category = "SocketIO Events")
	FSIOCSocketEventSignature OnSocketNamespaceConnected;

	/** Received on having left namespace. */
	UPROPERTY(BlueprintAssignable, Category = "SocketIO Events")
	FSIOCSocketEventSignature OnSocketNamespaceDisconnected;

	/** Received on connection failure. */
	UPROPERTY(BlueprintAssignable, Category = "SocketIO Events")
	FSIOCEventSignature OnFail;


	/**
	* Default connection params used on e.g. on begin play. Can be updated and re-used on custom connection.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SocketIO Connection Properties")
	FSIOConnectParams URLParams;

	/**
	* Will force using TLS even if url doesn't have https:// prepend.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SocketIO Connection Properties")
	bool bForceTLS;

	/**
	* If true, all your bound events will unbind on disconnect.
	* Useful for cleanup if typically binding on connection and there
	* are no early event binds (before connection).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SocketIO Connection Properties")
	bool bUnbindEventsOnDisconnect;

	/**
	* If using a TLS url (or if forced) and setting this to false will not verify
	* the authenticity of the SSL certificate (i.e. asio::ssl::verify_none).
	*
	* NOTE: Certification verification is currently not implemented; setting to true will
	* always fail verification.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SocketIO Connection Properties")
	bool bShouldVerifyTLSCertificate;

	/** If true will auto-connect on begin play to address specified in AddressAndPort. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SocketIO Connection Properties")
	bool bShouldAutoConnect;

	/** Delay between reconnection attempts */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SocketIO Connection Properties")
	int32 ReconnectionDelayInMs;

	/**
	* Number of times the connection should try before giving up.
	* Default: infinity, this means you never truly disconnect, just suffer connection problems
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SocketIO Connection Properties")
	int32 MaxReconnectionAttempts;

	/** Optional parameter to limit reconnections by elapsed time. Default: infinity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SocketIO Connection Properties")
	float ReconnectionTimeout;

	FDateTime TimeWhenConnectionProblemsStarted;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SocketIO Connection Properties")
	bool bVerboseConnectionLog;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SocketIO Scope Properties")
	bool bLimitConnectionToGameWorld;

	/**
	* Toggle which enables plugin scoped connections.
	* If you enable this the connection will remain until you manually call disconnect
	* or close the app. The latest connection with the same PluginScopedId will use the same connection
	* as the previous one and receive the same events.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SocketIO Scope Properties")
	bool bPluginScopedConnection;

	/** If you leave this as is all plugin scoped connection components will share same connection*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SocketIO Scope Properties")
	FString PluginScopedId;

	UPROPERTY(BlueprintReadOnly, Category = "SocketIO Connection Properties")
	bool bIsConnected;

	/** When connected this session id will be valid and contain a unique Id. */
	UPROPERTY(BlueprintReadOnly, Category = "SocketIO Connection Properties")
	FString SessionId;

	/** Each new connection is assigned a random 20-characters identifier. This identifier is synced with the value on the client-side. */
	UPROPERTY(BlueprintReadOnly, Category = "SocketIO Connection Properties")
	FString SocketId;

	UPROPERTY(BlueprintReadOnly, Category = "SocketIO Connection Properties")
	bool bIsHavingConnectionProblems;

	/** If this component has been statically initialized. Largely exposed for traceability. */
	UPROPERTY(BlueprintReadOnly, Category = "SocketIO Connection Properties")
	bool bStaticallyInitialized;

	/**
	* Connect to a socket.io server, optional method if auto-connect is set to true.
	* Query and headers are defined by a {'stringKey':'stringValue'} SIOJson Object
	*
	* @param AddressAndPort	the address in URL format with port, if left empty it will
	*						use current URLParams for all inputs.
	* @param Path optional ws:// trailing path for socket.io connection
	* @param Query http query as a SIOJsonObject with string keys and values
	* @param Headers http header as a SIOJsonObject with string keys and values
	* @param Auth socket.io authorization option as a SIOJsonObject with string keys and values
	*
	*/
	UFUNCTION(BlueprintCallable, Category = "SocketIO Functions")
	void Connect(const FString& InAddressAndPort = TEXT(""),
		const FString& InPath = TEXT("socket.io"),
		const FString& InAuthToken = TEXT(""),
		USIOJsonObject* Query = nullptr,
		USIOJsonObject* Headers = nullptr);

	/**
	* Connect to a socket.io server, optional method if auto-connect is set to true.
	*
	* @param InURLParams - A struct holding address&port, path, headers, query, and auth params
	*/
	UFUNCTION(BlueprintCallable, Category = "SocketIO Functions")
	void ConnectWithParams(const FSIOConnectParams& InURLParams);

	/**
	* Disconnect from current socket.io server. This is an asynchronous action,
	* subscribe to OnDisconnected to know when you can safely continue from a
	* disconnected state.
	*
	* @param AddressAndPort	the address in URL format with port
	*/
	UFUNCTION(BlueprintCallable, Category = "SocketIO Functions")
	void Disconnect();

	void SyncDisconnect();

	/**
	* Join a desired namespace. Keep in mind that emitting to a namespace will auto-join it
	*/
	UFUNCTION(BlueprintCallable, Category = "SocketIO Functions")
	void JoinNamespace(const FString& Namespace);

	/**
	* Leave a specified namespace. Should stop listening to events on given namespace.
	*/
	UFUNCTION(BlueprintCallable, Category = "SocketIO Functions")
	void LeaveNamespace(const FString& Namespace);

	//
	//Blueprint Functions
	//

	/**
	* Emit an event with a JsonValue message
	*
	* @param Name		Event name
	* @param Message	SIOJJsonValue
	* @param Namespace	Namespace within socket.io
	*/
	UFUNCTION(BlueprintCallable, Category = "SocketIO Functions")
	void Emit(const FString& EventName, USIOJsonValue* Message = nullptr, const FString& Namespace = TEXT("/"));

	/**
	* Emit an event with a JsonValue message
	*
	* @param Name		Event name
	* @param Messages	TArray of Message
	* @param Namespace	Namespace within socket.io
	*/
	UFUNCTION(BlueprintCallable, Category = "SocketIO Functions")
	void EmitMessages(const FString& EventName, const TArray <USIOJsonValue*>& Messages, const FString& Namespace = TEXT("/"));

	/**
	* Emit an event with a JsonValue message with a callback function defined by CallBackFunctionName
	*
	* @param Name					Event name
	* @param Message				SIOJsonValue
	* @param CallbackFunctionName	Name of the optional callback function with signature (String, SIOJsonValue)
	* @param Target					Optional, defaults to caller self. Change to delegate function callback to another class.
	* @param Namespace				Namespace within socket.io
	*/
	UFUNCTION(BlueprintCallable, Category = "SocketIO Functions", meta = (WorldContext = "WorldContextObject"))
	void EmitWithCallBack(const FString& EventName,
		USIOJsonValue* Message = nullptr,
		const FString& CallbackFunctionName = FString(""),
		UObject* Target = nullptr,
		const FString& Namespace = TEXT("/"),
		UObject* WorldContextObject = nullptr);


	/**
	* Emit an event with a JsonValue message with a result callback directly into the event graph. This cannot be called from within blueprint functions.
	*
	* @param Name					Event name
	* @param LatentInfo				Graph callback reference
	* @param Result					Graph callback result SIOJsonValue
	* @param Message				SIOJsonValue
	* @param Namespace				Namespace within socket.io
	*/
	UFUNCTION(BlueprintCallable, meta = (Latent, LatentInfo = "LatentInfo"), Category = "SocketIO Functions")
	void EmitWithGraphCallBack(const FString& EventName,
		struct FLatentActionInfo LatentInfo,
		USIOJsonValue*& Result,
		USIOJsonValue* Message = nullptr,
		const FString& Namespace = TEXT("/"));

	/**
	* Emit an event with a MessageList with a callback function defined by CallBackFunctionName
	*
	* @param Name					Event name
	* @param Message				SIOJsonValue
	* @param CallbackFunctionName	Name of the optional callback function with signature (String, SIOJsonValue)
	* @param Target					Optional, defaults to caller self. Change to delegate function callback to another class.
	* @param Namespace				Namespace within socket.io
	*/
	UFUNCTION(BlueprintCallable, Category = "SocketIO Functions", meta = (WorldContext = "WorldContextObject"))
	void EmitMessagesWithCallBack(const FString& EventName,
		const TArray <USIOJsonValue*>& Messages,
		const FString& CallbackFunctionName = FString(""),
		UObject* Target = nullptr,
		const FString& Namespace = TEXT("/"),
		UObject* WorldContextObject = nullptr);

	/**
	* Bind an event directly to a matching delegate. Drag off from red box or
	* use create event option.
	*
	* @param EventName	Event name
	* @param CallbackDelegate Delegate that needs to be bound
	* @param Namespace	Optional namespace, defaults to default namespace
	* @param ThreadOverride	Optional override to receive event on specified thread. Note NETWORK thread is lower latency but unsafe for a lot of blueprint use. Use with CAUTION.
	*/
	UFUNCTION(BlueprintCallable, Category = "SocketIO Functions")
	void BindEventToDelegate(const FString& EventName,
		const FSIOJsonValueSignature& CallbackDelegate,
		const FString& Namespace = TEXT("/"),
		ESIOThreadOverrideOption ThreadOverride = USE_DEFAULT);

	/**
	* Bind an event, then respond to it with 'OnGenericEvent' multi-cast delegate.
	* If you want functions or custom events to receive the event, use Bind Event To Function.
	*
	* @param EventName	Event name
	* @param Namespace	Optional namespace, defaults to default namespace
	*/
	UFUNCTION(BlueprintCallable, Category = "SocketIO Functions")
	void BindEventToGenericEvent(const FString& EventName, const FString& Namespace = TEXT("/"));

	/**
	* Bind an event to a function with the given name.
	* Expects a String message signature which can be decoded from JSON into SIOJsonObject
	*
	* @param EventName		Event name
	* @param FunctionName	The function that gets called when the event is received
	* @param Target			Optional, defaults to caller self. Change to delegate to another class.
	* @param Namespace		Optional namespace, defaults to default namespace
	* @param ThreadOverride	Optional override to receive event on specified thread. Note NETWORK thread is lower latency but unsafe for a lot of blueprint use. Use with CAUTION.
	*/
	UFUNCTION(BlueprintCallable, Category = "SocketIO Functions", meta = (WorldContext = "WorldContextObject"))
	void BindEventToFunction(const FString& EventName,
		const FString& FunctionName,
		UObject* Target,
		const FString& Namespace = TEXT("/"),
		ESIOThreadOverrideOption ThreadOverride = USE_DEFAULT,
		UObject* WorldContextObject = nullptr);

	/**
	* Unbind an event from whatever it was bound to (safe to call if not already bound)
	*
	* @param EventName	Event name
	* @param Namespace	Optional namespace, defaults to default namespace
	*/
	UFUNCTION(BlueprintCallable, Category = "SocketIO Functions")
	void UnbindEvent(const FString& EventName, const FString& Namespace = TEXT("/"));


	//
	//C++ functions
	//

	/**
	* Connect to a socket.io server, optional method if auto-connect is set to true.
	* Query and headers are defined by a {'stringKey':'stringValue'} FJsonObjects
	*
	* @param AddressAndPort	the address in URL format with port
	* @param Path the path of the socketio server (ex: "socket.io"
	* @param Query http query as a FJsonObject with string keys and values
	* @param Headers http header as a FJsonObject with string keys and values
	*
	*/
	void ConnectNative(const FString& InAddressAndPort,
		const FString& InPath = TEXT("socket.io"),
		const FString& InAuthToken = TEXT(""),
		const TSharedPtr<FJsonObject>& Query = nullptr,
		const TSharedPtr<FJsonObject>& Headers = nullptr);


	/**
	* Emit an event with a JsonValue message
	*
	* @param EventName				Event name
	* @param Message				FJsonValue
	* @param CallbackFunction		Optional callback TFunction
	* @param Namespace				Optional Namespace within socket.io
	*/
	void EmitNative(const FString& EventName,
		const TSharedPtr<FJsonValue>& Message = nullptr,
		TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction = nullptr,
		const FString& Namespace = TEXT("/"));

	/**
	* (Overloaded) Emit an event with a Json Object message
	*
	* @param EventName				Event name
	* @param ObjectMessage			FJsonObject
	* @param CallbackFunction		Optional callback TFunction
	* @param Namespace				Optional Namespace within socket.io
	*/
	void EmitNative(const FString& EventName,
		const TSharedPtr<FJsonObject>& ObjectMessage = nullptr,
		TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction = nullptr,
		const FString& Namespace = TEXT("/"));

	/**
	* (Overloaded) Emit an event with a string message
	*
	* @param EventName				Event name
	* @param StringMessage			Message in string format
	* @param CallbackFunction		Optional callback TFunction
	* @param Namespace				Optional Namespace within socket.io
	*/
	void EmitNative(const FString& EventName,
		const FString& StringMessage = FString(),
		TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction = nullptr,
		const FString& Namespace = TEXT("/"));

	/**
	* (Overloaded) Emit an event with a string message
	*
	* @param EventName				Event name
	* @param StringMessage			Message in string format
	* @param CallbackFunction		Optional callback TFunction
	* @param Namespace				Optional Namespace within socket.io
	*/
	void EmitNative(const FString& EventName,
		const SIO_TEXT_TYPE StringMessage = TEXT(""),
		TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction = nullptr,
		const FString& Namespace = TEXT("/"));

	/**
	* (Overloaded) Emit an event with a number (double) message
	*
	* @param EventName				Event name
	* @param NumberMessage			Message in double format
	* @param CallbackFunction		Optional callback TFunction
	* @param Namespace				Optional Namespace within socket.io
	*/
	void EmitNative(const FString& EventName,
		double NumberMessage,
		TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction = nullptr,
		const FString& Namespace = TEXT("/"));

	/**
	* (Overloaded) Emit an event with a bool message
	*
	* @param EventName				Event name
	* @param BooleanMessage			Message in bool format
	* @param CallbackFunction		Optional callback TFunction
	* @param Namespace				Optional Namespace within socket.io
	*/
	void EmitNative(const FString& EventName,
		bool BooleanMessage,
		TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction = nullptr,
		const FString& Namespace = TEXT("/"));

	/**
	* (Overloaded) Emit an event with a binary message
	*
	* @param EventName				Event name
	* @param BinaryMessage			Message in an TArray of uint8
	* @param CallbackFunction		Optional callback TFunction
	* @param Namespace				Optional Namespace within socket.io
	*/
	void EmitNative(const FString& EventName,
		const TArray<uint8>& BinaryMessage,
		TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction = nullptr,
		const FString& Namespace = TEXT("/"));

	/**
	* (Overloaded) Emit an event with an array message
	*
	* @param EventName				Event name
	* @param ArrayMessage			Message in an TArray of FJsonValues
	* @param CallbackFunction		Optional callback TFunction
	* @param Namespace				Optional Namespace within socket.io
	*/
	void EmitNative(const FString& EventName,
		const TArray<TSharedPtr<FJsonValue>>& ArrayMessage,
		TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction = nullptr,
		const FString& Namespace = TEXT("/"));

	/**
	* (Overloaded) Emit an event with an UStruct message
	*
	* @param EventName				Event name
	* @param Struct					UStruct type usually obtained via e.g. FMyStructType::StaticStruct()
	* @param StructPtr				Pointer to the actual struct memory e.g. &MyStruct
	* @param CallbackFunction		Optional callback TFunction
	* @param Namespace				Optional Namespace within socket.io
	*/
	void EmitNative(const FString& EventName,
		UStruct* Struct,
		const void* StructPtr,
		TFunction< void(const TArray<TSharedPtr<FJsonValue>>&)> CallbackFunction = nullptr,
		const FString& Namespace = TEXT("/"));

	/**
	* Call function callback on receiving socket event. C++ only.
	*
	* @param EventName	Event name
	* @param TFunction	Lambda callback, JSONValue
	* @param Namespace	Optional namespace, defaults to default namespace
	* @param ThreadOverride	Optional override to receive event on specified thread. Note NETWORK thread is lower latency but unsafe for a lot of blueprint use. Use with CAUTION.
	*/
	void OnNativeEvent(const FString& EventName,
		TFunction< void(const FString&, const TSharedPtr<FJsonValue>&)> CallbackFunction,
		const FString& Namespace = TEXT("/"),
		ESIOThreadOverrideOption ThreadOverride = USE_DEFAULT);

	/**
	* Call function callback on receiving binary event. C++ only.
	*
	* @param EventName	Event name
	* @param TFunction	Lambda callback, raw flavor
	* @param Namespace	Optional namespace, defaults to default namespace
	*/
	void OnBinaryEvent(const FString& EventName,
		TFunction< void(const FString&, const TArray<uint8>&)> CallbackFunction,
		const FString& Namespace = TEXT("/"));


	/** Called by SocketIOFunctionLibrary to initialize statically constructed components. */
	void StaticInitialization(UObject* WorldContextObject, bool bValidOwnerWorld);

	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void BeginPlay() override;

	~USocketIOClientComponent();

protected:
	void SetupCallbacks();
	void ClearCallbacks();
	void InitializeNative();

	bool CallBPFunctionWithResponse(UObject* Target, const FString& FunctionName, TArray<TSharedPtr<FJsonValue>> Response);
	bool CallBPFunctionWithMessage(UObject* Target, const FString& FunctionName, TSharedPtr<FJsonValue> Message);

	FCriticalSection AllocationSection;
	TSharedPtr<FSocketIONative> NativeClient;
};
