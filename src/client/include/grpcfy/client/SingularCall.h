#pragma once

#include <type_traits>
#include <variant>

#include <grpcpp/grpcpp.h>
//FIXME lfs: this is private header
#include <grpcpp/impl/codegen/async_unary_call.h>

#include <grpcfy/client/Common.h>

#include <boost/outcome.hpp>

namespace grpcfy::client {

template<typename Response>
using SingularReader = std::unique_ptr<grpc::ClientAsyncResponseReader<Response>>;

/**
 * @brief Represents Request-Response RPC, like function call
 * @details Does request and awaits response till deadline\n
 * Returns result or error via CompletionCallback\n
 * May have custom deadline option
 * @tparam OutboundRequest Client RPC initializing request
 * @tparam InboundResponse Server response
 * @tparam RpcStub Generated Stub type
 * @tparam MakeReader Stub factory function for corresponding reader creation
 */
template<typename OutboundRequest,
         typename InboundResponse,
         typename RpcStub,
         SingularReader<InboundResponse> (
             RpcStub::*MakeReader)(grpc::ClientContext *, const OutboundRequest &, grpc::CompletionQueue *)>
class SingularCall final
{
public:
	using Request = OutboundRequest;
	using Response = InboundResponse;
	using Result = boost::outcome_v2::result<Response, grpc::Status, boost::outcome_v2::policy::terminate>;
	using Stub = RpcStub;
	constexpr static auto MakeReaderFn = MakeReader;

	struct Summary final
	{
		Request request;
		Result result;

		explicit operator bool() const noexcept { return static_cast<bool>(result); }
		typename Result::value_type &operator*() noexcept { return result.value(); }
		typename Result::value_type *operator->() noexcept { return &result.value(); }
		typename Result::error_type &operator~() noexcept { return result.error(); }
	};
	using CompletionCallback = std::function<void(Summary &&summary)>;

public:
	SingularCall(Request &&request, CompletionCallback &&callback) noexcept
	    : request(std::move(request))
	    , callback(std::move(callback))
	{
	}

	explicit SingularCall(CompletionCallback &&callback) noexcept
	    : SingularCall({}, std::move(callback))
	{
	}

public:
	Request request;
	CompletionCallback callback;

public:
	//TODO lfs: provide getter|setter and verify
	std::optional<Duration> deadline;
	// more options
};

}  // namespace grpcfy::client