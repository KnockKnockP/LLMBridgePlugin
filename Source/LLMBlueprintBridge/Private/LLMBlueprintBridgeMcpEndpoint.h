#pragma once

#include "CoreMinimal.h"
#include "HttpResultCallback.h"
#include "HttpServerRequest.h"
#include "Templates/SharedPointer.h"

class FJsonObject;
class FJsonValue;
class ILLMBlueprintBridgeToolInvoker;

/**
 * Model Context Protocol 엔드포인트.
 *
 * JSON-RPC 2.0 over HTTP 로 initialize / tools/list / tools/call / ping 을 처리한다.
 * 툴 실행 자체는 ILLMBlueprintBridgeToolInvoker 뒤로 위임하므로, 이 클래스는 브리지의
 * 165개 툴 구현을 전혀 모른다.
 *
 * 프로토콜 형태를 UE5.8 의 Experimental/ModelContextProtocol 플러그인에 맞춰 두었다.
 * 5.8 로 올라가면 이 클래스와 자체 HTTP 서버를 버리고, 툴을 IModelContextProtocolTool 로
 * 감싸 IModelContextProtocolModule::AddTool 에 등록하면 된다.
 *
 * @see https://modelcontextprotocol.io/specification/2025-06-18/server/tools
 */
class FLLMBlueprintBridgeMcpEndpoint
{
public:
	/** 요청이 허용되는지 판정한다. 기존 REST 경로와 같은 규칙을 쓰기 위해 서버가 넘겨준다. */
	using FAuthorizePredicate = TFunction<bool(const FHttpServerRequest&)>;

	FLLMBlueprintBridgeMcpEndpoint(ILLMBlueprintBridgeToolInvoker& InInvoker, FAuthorizePredicate InIsAuthorized);

	/** 라우터에 물리는 단일 진입점. Verb 로 갈라 아래 핸들러들로 보낸다. */
	bool HandleRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** JSON-RPC 본문을 받는 주 경로. */
	bool HandlePost(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** SSE 스트림 요청. 이 브리지는 서버→클라이언트 푸시를 쓰지 않으므로 405 를 돌려준다. */
	bool HandleGet(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** CORS 프리플라이트. */
	bool HandleOptions(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** 클라이언트가 요청한 버전을 지원하면 그대로, 아니면 서버 최신 버전을 돌려준다. */
	static FString NegotiateProtocolVersion(const FString& ClientRequestedVersion);

	/** 서버가 지원하는 프로토콜 버전 목록(최신 순). */
	static const TArray<FString>& GetSupportedProtocolVersions();

private:
	void DispatchMethod(
		const FString& Method,
		const TSharedPtr<FJsonObject>& Params,
		const TSharedPtr<FJsonValue>& Id,
		const FHttpResultCallback& OnComplete);

	TSharedRef<FJsonObject> MakeInitializeResult(const TSharedPtr<FJsonObject>& Params) const;
	TSharedRef<FJsonObject> MakeToolsListResult() const;

	void RunToolCall(
		const TSharedPtr<FJsonObject>& Params,
		const TSharedPtr<FJsonValue>& Id,
		const FHttpResultCallback& OnComplete);

	ILLMBlueprintBridgeToolInvoker& Invoker;
	FAuthorizePredicate IsAuthorized;
};
