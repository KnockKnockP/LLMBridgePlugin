#include "LLMBlueprintBridgeMcpEndpoint.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HttpServerResponse.h"
#include "LLMBlueprintBridgeToolCatalog.h"
#include "LLMBlueprintBridgeToolInvoker.h"
#include "Misc/FileHelper.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogLLMBlueprintBridgeMcp, Log, All);

namespace
{
	const TCHAR* McpJsonRpcVersion = TEXT("2.0");
	const TCHAR* McpJsonContentType = TEXT("application/json");
	const TCHAR* McpServerName = TEXT("llm-blueprint-bridge");
	const TCHAR* McpServerTitle = TEXT("LLM Blueprint Bridge");
	const TCHAR* McpServerVersion = TEXT("0.2.0");

	// JSON-RPC 2.0 표준 오류 코드
	constexpr int32 RpcParseError = -32700;
	constexpr int32 RpcInvalidRequest = -32600;
	constexpr int32 RpcMethodNotFound = -32601;
	constexpr int32 RpcInvalidParams = -32602;

	FString SerializeMcpJson(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		Writer->Close();
		return Out;
	}

	FString ReadMcpRequestBody(const FHttpServerRequest& Request)
	{
		FString Body;
		if (!Request.Body.IsEmpty())
		{
			FFileHelper::BufferToString(Body, Request.Body.GetData(), Request.Body.Num());
		}
		return Body;
	}

	FString FindMcpHeader(const FHttpServerRequest& Request, const TCHAR* HeaderName)
	{
		for (const TPair<FString, TArray<FString>>& Header : Request.Headers)
		{
			if (Header.Key.Equals(HeaderName, ESearchCase::IgnoreCase) && Header.Value.Num() > 0)
			{
				return Header.Value[0];
			}
		}
		return FString();
	}

	/**
	 * DNS 리바인딩 방어. UE5.8 의 MCP 서버도 같은 방식으로 비루프백 Origin 을 거부한다.
	 * Origin 헤더가 아예 없는 요청(curl, MCP 클라이언트 대부분)은 통과시킨다.
	 */
	bool IsMcpAllowedOrigin(const FHttpServerRequest& Request)
	{
		const FString Origin = FindMcpHeader(Request, TEXT("Origin"));
		if (Origin.IsEmpty())
		{
			return true;
		}

		return Origin.StartsWith(TEXT("http://127.0.0.1"))
			|| Origin.StartsWith(TEXT("https://127.0.0.1"))
			|| Origin.StartsWith(TEXT("http://localhost"))
			|| Origin.StartsWith(TEXT("https://localhost"))
			|| Origin.StartsWith(TEXT("http://[::1]"))
			|| Origin.StartsWith(TEXT("https://[::1]"));
	}

	void ApplyMcpCorsHeaders(FHttpServerResponse& Response)
	{
		Response.Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("http://127.0.0.1"), TEXT("http://localhost") });
		Response.Headers.Add(TEXT("Access-Control-Allow-Headers"),
			{ TEXT("Content-Type, Accept, Authorization, X-LLM-Bridge-Token, Mcp-Protocol-Version, Mcp-Session-Id") });
		Response.Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("GET, POST, OPTIONS") });
	}

	TUniquePtr<FHttpServerResponse> MakeMcpJsonResponse(
		const TSharedRef<FJsonObject>& Object,
		EHttpServerResponseCodes Code = EHttpServerResponseCodes::Ok)
	{
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(SerializeMcpJson(Object), McpJsonContentType);
		Response->Code = Code;
		ApplyMcpCorsHeaders(*Response);
		return Response;
	}

	/** 통지(notification)에 대한 응답: 본문 없음. */
	TUniquePtr<FHttpServerResponse> MakeMcpEmptyResponse(EHttpServerResponseCodes Code)
	{
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(FString(), McpJsonContentType);
		Response->Code = Code;
		ApplyMcpCorsHeaders(*Response);
		return Response;
	}

	TSharedRef<FJsonObject> MakeRpcEnvelope(const TSharedPtr<FJsonValue>& Id)
	{
		TSharedRef<FJsonObject> Envelope = MakeShared<FJsonObject>();
		Envelope->SetStringField(TEXT("jsonrpc"), McpJsonRpcVersion);
		if (Id.IsValid())
		{
			Envelope->SetField(TEXT("id"), Id);
		}
		else
		{
			Envelope->SetField(TEXT("id"), MakeShared<FJsonValueNull>());
		}
		return Envelope;
	}

	TSharedRef<FJsonObject> MakeRpcResult(const TSharedPtr<FJsonValue>& Id, const TSharedRef<FJsonObject>& Result)
	{
		TSharedRef<FJsonObject> Envelope = MakeRpcEnvelope(Id);
		Envelope->SetObjectField(TEXT("result"), Result);
		return Envelope;
	}

	TSharedRef<FJsonObject> MakeRpcError(const TSharedPtr<FJsonValue>& Id, const int32 Code, const FString& Message)
	{
		TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetNumberField(TEXT("code"), Code);
		Error->SetStringField(TEXT("message"), Message);

		TSharedRef<FJsonObject> Envelope = MakeRpcEnvelope(Id);
		Envelope->SetObjectField(TEXT("error"), Error);
		return Envelope;
	}

	TSharedRef<FJsonObject> MakeMcpTextContent(const FString& Text)
	{
		TSharedRef<FJsonObject> Content = MakeShared<FJsonObject>();
		Content->SetStringField(TEXT("type"), TEXT("text"));
		Content->SetStringField(TEXT("text"), Text);
		return Content;
	}

	/**
	 * 브리지의 {"ok":...} 봉투를 MCP 툴 결과로 변환한다.
	 *
	 * 성공: content[0] 에 데이터의 압축 JSON 텍스트, structuredContent 에 같은 객체.
	 * 실패: isError=true 와 "code: message" 텍스트. JSON-RPC 오류가 아니라 정상 결과로 돌려준다
	 *       (MCP 규격상 툴 실행 실패는 프로토콜 오류가 아니다).
	 */
	TSharedRef<FJsonObject> ToolEnvelopeToMcpResult(const TSharedRef<FJsonObject>& Envelope)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		FString ContentText;

		bool bOk = false;
		Envelope->TryGetBoolField(TEXT("ok"), bOk);

		if (bOk)
		{
			const TSharedPtr<FJsonObject>* Data = nullptr;
			if (Envelope->TryGetObjectField(TEXT("data"), Data) && Data != nullptr && Data->IsValid())
			{
				Result->SetObjectField(TEXT("structuredContent"), *Data);
				ContentText = SerializeMcpJson(Data->ToSharedRef());
			}
			else
			{
				ContentText = TEXT("{}");
			}
		}
		else
		{
			FString Code;
			FString Message;
			Envelope->TryGetStringField(TEXT("code"), Code);
			Envelope->TryGetStringField(TEXT("message"), Message);

			ContentText = Message;
			if (!Code.IsEmpty())
			{
				ContentText = Message.IsEmpty() ? Code : FString::Printf(TEXT("%s: %s"), *Code, *Message);
			}
			if (ContentText.IsEmpty())
			{
				ContentText = TEXT("Tool call failed.");
			}

			Result->SetBoolField(TEXT("isError"), true);
		}

		// content 항목은 {"type":"text","text":...} 객체여야 한다.
		TArray<TSharedPtr<FJsonValue>> Content;
		Content.Add(MakeShared<FJsonValueObject>(MakeMcpTextContent(ContentText)));
		Result->SetArrayField(TEXT("content"), Content);

		return Result;
	}
}

FLLMBlueprintBridgeMcpEndpoint::FLLMBlueprintBridgeMcpEndpoint(
	ILLMBlueprintBridgeToolInvoker& InInvoker,
	FAuthorizePredicate InIsAuthorized)
	: Invoker(InInvoker)
	, IsAuthorized(MoveTemp(InIsAuthorized))
{
}

const TArray<FString>& FLLMBlueprintBridgeMcpEndpoint::GetSupportedProtocolVersions()
{
	// UE5.8 의 ModelContextProtocol 플러그인과 같은 목록을 쓴다.
	static const TArray<FString> Versions = {
		TEXT("2025-11-25"),
		TEXT("2025-06-18"),
		TEXT("2024-11-05")
	};
	return Versions;
}

FString FLLMBlueprintBridgeMcpEndpoint::NegotiateProtocolVersion(const FString& ClientRequestedVersion)
{
	for (const FString& Version : GetSupportedProtocolVersions())
	{
		if (Version == ClientRequestedVersion)
		{
			return ClientRequestedVersion;
		}
	}
	return GetSupportedProtocolVersions()[0];
}

bool FLLMBlueprintBridgeMcpEndpoint::HandleRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	switch (Request.Verb)
	{
	case EHttpServerRequestVerbs::VERB_POST:
		return HandlePost(Request, OnComplete);
	case EHttpServerRequestVerbs::VERB_GET:
		return HandleGet(Request, OnComplete);
	case EHttpServerRequestVerbs::VERB_OPTIONS:
		return HandleOptions(Request, OnComplete);
	default:
		break;
	}

	TUniquePtr<FHttpServerResponse> Response = MakeMcpEmptyResponse(EHttpServerResponseCodes::BadMethod);
	Response->Headers.Add(TEXT("Allow"), { TEXT("POST, OPTIONS") });
	OnComplete(MoveTemp(Response));
	return true;
}

bool FLLMBlueprintBridgeMcpEndpoint::HandleOptions(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	OnComplete(MakeMcpEmptyResponse(EHttpServerResponseCodes::NoContent));
	return true;
}

bool FLLMBlueprintBridgeMcpEndpoint::HandleGet(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// Streamable HTTP 의 GET 은 서버→클라이언트 SSE 스트림용이다. 이 브리지는 푸시가 없다.
	TUniquePtr<FHttpServerResponse> Response = MakeMcpEmptyResponse(EHttpServerResponseCodes::BadMethod);
	Response->Headers.Add(TEXT("Allow"), { TEXT("POST, OPTIONS") });
	OnComplete(MoveTemp(Response));
	return true;
}

bool FLLMBlueprintBridgeMcpEndpoint::HandlePost(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	if (!IsMcpAllowedOrigin(Request))
	{
		OnComplete(MakeMcpJsonResponse(
			MakeRpcError(nullptr, RpcInvalidRequest, TEXT("Origin is not a loopback address.")),
			EHttpServerResponseCodes::Denied));
		return true;
	}

	// 기존 REST 경로와 동일한 규칙(bLocalRequestsOnly / AuthToken)을 적용한다.
	if (IsAuthorized && !IsAuthorized(Request))
	{
		OnComplete(MakeMcpJsonResponse(
			MakeRpcError(nullptr, RpcInvalidRequest, TEXT("Request is not authorized.")),
			EHttpServerResponseCodes::Denied));
		return true;
	}

	const FString Body = ReadMcpRequestBody(Request);

	TSharedPtr<FJsonValue> RootValue;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, RootValue) || !RootValue.IsValid())
	{
		OnComplete(MakeMcpJsonResponse(
			MakeRpcError(nullptr, RpcParseError, TEXT("Request body is not valid JSON.")),
			EHttpServerResponseCodes::BadRequest));
		return true;
	}

	// JSON-RPC 배치는 MCP 2025-06-18 에서 제거되었다. 배열이 오면 거부한다.
	if (RootValue->Type == EJson::Array)
	{
		OnComplete(MakeMcpJsonResponse(
			MakeRpcError(nullptr, RpcInvalidRequest, TEXT("JSON-RPC batching is not supported.")),
			EHttpServerResponseCodes::BadRequest));
		return true;
	}

	const TSharedPtr<FJsonObject>* RootObjectPtr = nullptr;
	if (!RootValue->TryGetObject(RootObjectPtr) || RootObjectPtr == nullptr || !RootObjectPtr->IsValid())
	{
		OnComplete(MakeMcpJsonResponse(
			MakeRpcError(nullptr, RpcInvalidRequest, TEXT("Expected a JSON-RPC request object.")),
			EHttpServerResponseCodes::BadRequest));
		return true;
	}

	const TSharedPtr<FJsonObject> RootObject = *RootObjectPtr;

	FString Method;
	if (!RootObject->TryGetStringField(TEXT("method"), Method) || Method.IsEmpty())
	{
		OnComplete(MakeMcpJsonResponse(
			MakeRpcError(nullptr, RpcInvalidRequest, TEXT("Expected a non-empty 'method' string.")),
			EHttpServerResponseCodes::BadRequest));
		return true;
	}

	// id 가 없으면 통지다. 응답 본문을 만들지 않는다.
	const TSharedPtr<FJsonValue> Id = RootObject->HasField(TEXT("id")) ? RootObject->TryGetField(TEXT("id")) : nullptr;
	const bool bIsNotification = !Id.IsValid() || Id->Type == EJson::Null;

	TSharedPtr<FJsonObject> Params;
	const TSharedPtr<FJsonObject>* ParamsPtr = nullptr;
	if (RootObject->TryGetObjectField(TEXT("params"), ParamsPtr) && ParamsPtr != nullptr && ParamsPtr->IsValid())
	{
		Params = *ParamsPtr;
	}
	else
	{
		Params = MakeShared<FJsonObject>();
	}

	if (bIsNotification)
	{
		UE_LOG(LogLLMBlueprintBridgeMcp, Verbose, TEXT("MCP notification: %s"), *Method);
		OnComplete(MakeMcpEmptyResponse(EHttpServerResponseCodes::Accepted));
		return true;
	}

	DispatchMethod(Method, Params, Id, OnComplete);
	return true;
}

void FLLMBlueprintBridgeMcpEndpoint::DispatchMethod(
	const FString& Method,
	const TSharedPtr<FJsonObject>& Params,
	const TSharedPtr<FJsonValue>& Id,
	const FHttpResultCallback& OnComplete)
{
	if (Method == TEXT("initialize"))
	{
		OnComplete(MakeMcpJsonResponse(MakeRpcResult(Id, MakeInitializeResult(Params))));
		return;
	}

	if (Method == TEXT("ping"))
	{
		OnComplete(MakeMcpJsonResponse(MakeRpcResult(Id, MakeShared<FJsonObject>())));
		return;
	}

	if (Method == TEXT("tools/list"))
	{
		OnComplete(MakeMcpJsonResponse(MakeRpcResult(Id, MakeToolsListResult())));
		return;
	}

	if (Method == TEXT("tools/call"))
	{
		RunToolCall(Params, Id, OnComplete);
		return;
	}

	OnComplete(MakeMcpJsonResponse(
		MakeRpcError(Id, RpcMethodNotFound, FString::Printf(TEXT("Unknown method '%s'."), *Method)),
		EHttpServerResponseCodes::Ok));
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeMcpEndpoint::MakeInitializeResult(const TSharedPtr<FJsonObject>& Params) const
{
	FString RequestedVersion;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("protocolVersion"), RequestedVersion);
	}

	TSharedRef<FJsonObject> Tools = MakeShared<FJsonObject>();
	Tools->SetBoolField(TEXT("listChanged"), false);

	TSharedRef<FJsonObject> Capabilities = MakeShared<FJsonObject>();
	Capabilities->SetObjectField(TEXT("tools"), Tools);

	TSharedRef<FJsonObject> ServerInfo = MakeShared<FJsonObject>();
	ServerInfo->SetStringField(TEXT("name"), McpServerName);
	ServerInfo->SetStringField(TEXT("title"), McpServerTitle);
	ServerInfo->SetStringField(TEXT("version"), McpServerVersion);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("protocolVersion"), NegotiateProtocolVersion(RequestedVersion));
	Result->SetObjectField(TEXT("capabilities"), Capabilities);
	Result->SetObjectField(TEXT("serverInfo"), ServerInfo);
	Result->SetStringField(TEXT("instructions"),
		TEXT("Unreal Editor bridge for Blueprint, StateTree, GAS, material, animation and asset work. ")
		TEXT("Inspect before mutating, keep edits small, then compile or validate and save only if the result is acceptable."));

	return Result;
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeMcpEndpoint::MakeToolsListResult() const
{
	const TArray<FLLMBlueprintBridgeToolDescriptor>& Catalog = FLLMBlueprintBridgeToolCatalog::Get();

	TArray<TSharedPtr<FJsonValue>> Tools;
	Tools.Reserve(Catalog.Num());

	for (const FLLMBlueprintBridgeToolDescriptor& Descriptor : Catalog)
	{
		TSharedRef<FJsonObject> Tool = MakeShared<FJsonObject>();
		Tool->SetStringField(TEXT("name"), Descriptor.Name);
		Tool->SetStringField(TEXT("description"), Descriptor.Description);
		Tool->SetObjectField(TEXT("inputSchema"), Descriptor.InputSchema);
		if (Descriptor.Name == TEXT("format_blueprint_graph"))
		{
			TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
			Metadata->SetBoolField(TEXT("deprecated"), true);
			Metadata->SetStringField(TEXT("replacement"), TEXT("set_property with manually chosen NodePosX/NodePosY"));
			Tool->SetObjectField(TEXT("_meta"), Metadata);
		}
		Tools.Add(MakeShared<FJsonValueObject>(Tool));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("tools"), Tools);
	return Result;
}

void FLLMBlueprintBridgeMcpEndpoint::RunToolCall(
	const TSharedPtr<FJsonObject>& Params,
	const TSharedPtr<FJsonValue>& Id,
	const FHttpResultCallback& OnComplete)
{
	FString ToolName;
	if (!Params.IsValid() || !Params->TryGetStringField(TEXT("name"), ToolName) || ToolName.IsEmpty())
	{
		OnComplete(MakeMcpJsonResponse(
			MakeRpcError(Id, RpcInvalidParams, TEXT("Expected a non-empty 'name' string.")),
			EHttpServerResponseCodes::Ok));
		return;
	}

	// 알 수 없는 툴은 MCP 규격상 프로토콜 오류로 돌려준다.
	if (FLLMBlueprintBridgeToolCatalog::Find(ToolName) == nullptr)
	{
		OnComplete(MakeMcpJsonResponse(
			MakeRpcError(Id, RpcInvalidParams, FString::Printf(TEXT("Unknown tool '%s'."), *ToolName)),
			EHttpServerResponseCodes::Ok));
		return;
	}

	TSharedPtr<FJsonObject> Arguments;
	const TSharedPtr<FJsonObject>* ArgumentsPtr = nullptr;
	if (Params->TryGetObjectField(TEXT("arguments"), ArgumentsPtr) && ArgumentsPtr != nullptr && ArgumentsPtr->IsValid())
	{
		Arguments = *ArgumentsPtr;
	}
	else
	{
		Arguments = MakeShared<FJsonObject>();
	}

	ILLMBlueprintBridgeToolInvoker* InvokerPtr = &Invoker;
	AsyncTask(ENamedThreads::GameThread, [InvokerPtr, ToolName, Arguments, Id, OnComplete]()
	{
		const TSharedRef<FJsonObject> Envelope = InvokerPtr->InvokeToolByName(ToolName, Arguments);
		OnComplete(MakeMcpJsonResponse(MakeRpcResult(Id, ToolEnvelopeToMcpResult(Envelope))));
	});
}
