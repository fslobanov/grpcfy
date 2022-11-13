#pragma once

#include <random>

#include <boost/asio.hpp>

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <fmt/format.h>
#include <fmt/chrono.h>
#include <fmt/std.h>

#include <grpcfy/server/ServiceEngine.h>
#include <FooBar.grpc.pb.h>

namespace foobar {
using namespace std::chrono_literals;
using FoobarEngine = grpcfy::server::ServiceEngine<FooBar::AsyncService>;
}  // namespace foobar