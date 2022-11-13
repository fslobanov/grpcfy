#pragma once

#include <bitset>

namespace grpcfy::core {

template<typename T>
class EnablePointerTagThis
{
public:
	/**
	 * @brief 2 less significant bits mask
	 *
	 * @details Because gRPC call context always being allocated on heap, and it has some memory alignment,
	 * it's address less significant bits is unused, which can be used for pointer tagging\n
	 * It means we can set some of this bits, when asking gRPC runtime to do something,
	 * and mask and reset that bits, when event pulled from CompletionQueue,
	 * and pass this flags to event handling method\n
	 * Now we can use 2 LSB of address
	 *
	 * @details Motivation: Setting additional flags to pointer tag
	 * allows to check event and call state compatibility\n
	 * Additionally, but server side only, it can be used to tag cancellation event
	 * via grpc::ServerContext::AsyncNotifyWhenDone()
	 *
	 * @link https://en.wikipedia.org/wiki/Tagged_pointer
	 * @link https://graphics.stanford.edu/~seander/bithacks.html
	 */
	using Flags = std::bitset<2>;
	using Pointer = uintptr_t;
	static constexpr Pointer kFlagsMask = 0b11UL;

	/**
	 * @brief Check that derived type has enough alignment to fit flags	in pointer
	 */
	template<typename Derived>
	static constexpr void checkFlagsFit()
	{
		static_assert(alignof(T) >= kFlagsMask);
		static_assert(alignof(Derived) >= kFlagsMask);
	}

protected:
	/**
	 * @brief Represents object This pointer as Tag with additional Flags
	 * @param flags Flags applied to tag
	 */
	[[nodiscard]] void *tagify(Flags flags) noexcept
	{
		static_assert(sizeof(Pointer) >= sizeof(std::declval<Flags>().to_ulong()));

		const auto tag = reinterpret_cast<Pointer>(this);
		const auto flags_mask = static_cast<Pointer>(flags.to_ulong());
		return reinterpret_cast<void *>(tag | flags_mask);
	}

	/**
	 * @brief Zero flags overload, actually This casted to void pointer
	 */
	[[nodiscard]] void *tagify() noexcept { return tagify(0); };
};

}  // namespace grpcfy::core