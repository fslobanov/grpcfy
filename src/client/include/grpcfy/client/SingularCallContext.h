#pragma once

#include <grpcfy/client/CallContext.h>
#include <grpcfy/client/SingularCall.h>

namespace grpcfy::client {

/**
 * Internal SingularCall runtime
 * @tparam Call SingularCall<T> specialization
 */
template<typename Call>
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
	                    typename Call::CompletionCallback &&callback) noexcept
	    : request(std::move(request))
	    , callback(std::move(callback))
	    , context(setup_context(deadline))
	    , reader((stub.*Call::MakeReaderFn)(&*context, SingularCallContext::request, &queue))
	{
		checkFlagsFit<SingularCallContext>();

		assert(SingularCallContext::reader);
		assert(SingularCallContext::context);
		assert(SingularCallContext::callback);
	}

public:
	[[nodiscard]] Type getType() const noexcept final { return Type::SingularCall; }

	void run() noexcept final
	{
		reader->StartCall();
		reader->Finish(&response, &status, tagify());
	}

	Aliveness onEvent(bool ok, ClientState client_state, Flags flags) noexcept final
	{
		(void)client_state;
		(void)flags;
		//const SuicideGuard guard{this};
		auto result = (!ok || !status.ok()) ? Result{std::move(status)} : Result{std::move(response)};
		callback(Summary{std::move(request), std::move(result)});
		return Aliveness::Dead;
	}

	[[nodiscard]] static std::unique_ptr<grpc::ClientContext> setup_context(Duration d) noexcept
	{
		auto c = std::make_unique<grpc::ClientContext>();
		c->set_fail_fast(true);
		c->set_deadline(toGrpcTimespec(d));
		return c;
	}

private:
	Request request;
	Response response;
	typename Call::CompletionCallback callback;

	std::unique_ptr<grpc::ClientContext> context;
	grpc::Status status;
	SingularReader<Response> reader;
};

}  // namespace grpcfy::client