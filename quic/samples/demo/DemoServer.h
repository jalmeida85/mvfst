/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 */

#pragma once

#include <glog/logging.h>

#include <quic/common/test/TestUtils.h>
#include <quic/samples/demo/DemoHandler.h>
#include <quic/server/QuicServer.h>
#include <quic/server/QuicServerTransport.h>
#include <quic/server/QuicSharedUDPSocketFactory.h>

namespace quic {
namespace samples {

class DemoServerTransportFactory : public quic::QuicServerTransportFactory {
 public:
	~DemoServerTransportFactory() override {
		while (!_transferHandlers.empty()) {
			auto &handler = _transferHandlers.back();
			handler->getEventBase()->runImmediatelyOrRunInEventBaseThreadAndWait(
				[this] {
					// The evb should be performing a sequential consistency atomic
					// operation already, so we can bank on that to make sure the writes
					// propagate to all threads.
					_transferHandlers.pop_back();
				});
		}
	}

	DemoServerTransportFactory(bool prEnabled = false) : prEnabled_(prEnabled) {}

	quic::QuicServerTransport::Ptr make(
		folly::EventBase *evb, std::unique_ptr <folly::AsyncUDPSocket> sock, const folly::SocketAddress &,
		std::shared_ptr<const fizz::server::FizzServerContext> ctx) noexcept override {
		CHECK_EQ(evb, sock->getEventBase());
		auto transferHandler = std::make_unique<DemoHandler>(evb, prEnabled_);
		auto transport = quic::QuicServerTransport::make(
			evb, std::move(sock), *transferHandler, ctx);
		transferHandler->setQuicSocket(transport);
		_transferHandlers.push_back(std::move(transferHandler));
		return transport;
	}

	std::vector <std::unique_ptr<DemoHandler>> _transferHandlers;

 private:
	bool prEnabled_;
};

class DemoServer {
 public:
	explicit DemoServer(
		const std::string &host = "::1", uint16_t port = 6666, bool prEnabled = false)
		: host_(host), port_(port), prEnabled_(prEnabled), server_(QuicServer::createQuicServer()) {
		server_->setQuicServerTransportFactory(
			std::make_unique<DemoServerTransportFactory>(prEnabled_));
		server_->setFizzContext(quic::test::createServerCtx());
		if (prEnabled_) {
			TransportSettings settings;
			settings.partialReliabilityEnabled = true;
			server_->setTransportSettings(settings);
		}
	}

	void start() {
		// Create a SocketAddress and the default or passed in host.
		folly::SocketAddress addr1(host_.c_str(), port_);
		addr1.setFromHostPort(host_, port_);
		server_->start(addr1, 0);
		LOG(INFO) << "Server started at: " << addr1.describe();
		eventbase_.loopForever();
	}

 private:
	std::string host_;
	uint16_t port_;
	bool prEnabled_;
	folly::EventBase eventbase_;
	std::shared_ptr <quic::QuicServer> server_;
};
} // namespace samples
} // namespace quic
