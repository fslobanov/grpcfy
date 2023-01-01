#pragma once

#include <cstdint>

#include <grpcfy/core/PointerTagging.h>

namespace grpcfy::server::detail {

/**
 * @brief Type erased base for each type of gRPC methods
 * @details Each supported method type should inherit this base, tags from CompletionQueue being casted to this type
 */
class MethodContext : public core::TagThisPointer<MethodContext>
{
public:
	virtual ~MethodContext() noexcept = default;

	/**
	 * @brief Runs a method
	 */
	virtual void run() noexcept = 0;

	/**
	 * @brief Processes event received from completion queue, toggles internal method FSM
	 * @param ok Status got from grpc::CompletionQueue::Next
	 * @param flags Flags attached via pointer tagging
	 */
	virtual void on_event(bool ok, Flags flags) noexcept = 0;

protected:
	/**
	 * @brief Self destroying an object.
	 * @details This needed due to internal method lifecycle implementation, each method is a FSM,\n
	 * which should delete itself on completion, removing this obligation from external owner
	 */
	void suicide() noexcept { delete this; }
};

}  // namespace grpcfy::server::detail