#pragma once

#include <bitset>
#include <grpcfy/core/PointerTagging.h>

namespace grpcfy::client {

/**
 * @brief Base class for Call context
 * @details Represents one RPC call, singular or stream. Dispatches on CompletionQueue
 *
 */
class CallContext : public core::TagThisPointer<CallContext>
{
public:
	/**
	 * @brief Determine, when context work is done, and it should be destroyed
	 */
	enum class [[nodiscard]] Aliveness : bool
	{
		Alive,
		Dead
	};

public:
	virtual ~CallContext() noexcept = default;

	/**
	 * @brief Start an RPC call, pending call events will occur in CompletionQueue
	 */
	virtual void run() noexcept = 0;
	/**
	 * @brief RPC call event handling, concrete call need to implement internal state machine
	 * @param ok Status got from grpc::CompletionQueue::Next
	 * @param flags Flags passed on call action start
	 * @return Call aliveness
	 */
	virtual Aliveness on_event(bool ok, ClientState client_state, Flags flags) noexcept = 0;
};

}  // namespace grpcfy::client