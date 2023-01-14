#pragma once

#include <grpcfy/client/detail/CallContext.h>
#include <grpcfy/client/SingularCall.h>

namespace grpcfy::client {

/**
 * Internal SingularCall runtime
 * @tparam Call SingularCall<T> specialization
 */
template<typename Call, typename CompletionCallback>
class SingularCallContext final : public CallContext
{
public:
	using Stub = typename Call::Stub;
	using Request = typename Call::Request;
	using Response = typename Call::Response;
	using Result = typename Call::Result;
	using Summary = typename Call::Summary;

public:
	SingularCallContext(grpc::CompletionQueue &queue,
	                    Stub &stub,
	                    Request &&request,
	                    std::chrono::milliseconds deadline,
	                    CompletionCallback &&callback) noexcept
	    : request(std::move(request))
	    , callback(std::forward<CompletionCallback>(callback))
	    , context(setup_context(deadline))
	    , reader((stub.*Call::MakeReaderFn)(&*context, SingularCallContext::request, &queue))
	{
		check_flags_fit<SingularCallContext>();

		assert(SingularCallContext::reader);
		assert(SingularCallContext::context);
	}

public:
	void run() noexcept final override
	{
		reader->StartCall();
		reader->Finish(&response, &status, tagify());
	}

	Aliveness on_event(bool ok, ClientState client_state, Flags flags) noexcept final override
	{
		(void)client_state;
		(void)flags;
		auto result = (!ok || !status.ok()) ? Result{std::move(status)} : Result{std::move(response)};
		callback(Summary{std::move(request), std::move(result)});
		return Aliveness::Dead;
	}

	[[nodiscard]] static std::unique_ptr<grpc::ClientContext> setup_context(Duration duration) noexcept
	{
		auto context = std::make_unique<grpc::ClientContext>();
		context->set_fail_fast(true);
		context->set_deadline(to_grpc_timespec(duration));
		return context;
	}

private:
	Request request;
	Response response;
	CompletionCallback callback;

	std::unique_ptr<grpc::ClientContext> context;
	grpc::Status status;
	SingularReader<Response> reader;
};

}  // namespace grpcfy::client