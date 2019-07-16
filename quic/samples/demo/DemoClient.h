/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 */

#pragma once

#include <iostream>
#include <string>
#include <thread>

#include <glog/logging.h>

#include <folly/io/async/ScopedEventBaseThread.h>

#include <quic/api/QuicSocket.h>
#include <quic/client/QuicClientTransport.h>
#include <quic/common/test/TestUtils.h>

#include <chrono>
#include <cstdint>

std::unordered_map<quic::StreamId, int> _bytes_read;
std::unordered_map <quic::StreamId, int64_t> _start_time;
bool connection_ended = false;
bool connection_failed = false;
bool completed = false;

int64_t getTime() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

int sum = 0;

namespace quic {
namespace samples {
class DemoClient
	: public quic::QuicSocket::ConnectionCallback,
	  public quic::QuicSocket::ReadCallback,
	  public quic::QuicSocket::WriteCallback,
	  public quic::QuicSocket::DataExpiredCallback {
 public:
	DemoClient(
		const std::string &lat, const std::string &plr, const std::string &bytes, const std::string &host,
		uint16_t port, bool prEnabled = false)
		: host_(host), port_(port), prEnabled_(prEnabled), lat_(lat), plr_(plr), bytes_(bytes) {

		bytes_val_ = std::stoi(bytes_);
		LOG(INFO) << "Client(" << lat_ << "," << plr_ << "," << bytes_ << ") | host: " << host_ << " | port: " << port_
				  << " | pr: " << prEnabled_;
	}

	void readAvailable(quic::StreamId streamId) noexcept override {
		auto readData = quicClient_->read(streamId, 0);
		if (readData.hasError()) {
			LOG(ERROR) << "[readAvailable] failed read from stream=" << streamId << ", error="
					   << (uint32_t) readData.error();
		}

		auto copy = readData->first->clone();
		if (recvOffsets_.find(streamId) == recvOffsets_.end()) {
			recvOffsets_[streamId] = copy->length();
		} else {
			recvOffsets_[streamId] += copy->length();
		}

		int total_bytes = 0;
		sum += copy->length();
		if (_bytes_read.count(streamId) > 0) {
			total_bytes = _bytes_read[streamId];
		} else {
			_bytes_read[streamId] = 0;
		}

		total_bytes += copy->length();
		_bytes_read[streamId] = total_bytes;

//		LOG(INFO) << "[Client][" << streamId << "] read " << read_bytes << "bytes | total_bytes: " << total_bytes
//				  << " | first_read: " << first_read;

		if (total_bytes >= bytes_val_) {
			auto current_time = getTime();
			auto elapsed = current_time - _start_time[streamId];
			float rate = total_bytes * 8.0f * 1000.0f / (1024.0f * 1024.0f * elapsed);
			completed = true;
			LOG(INFO) << "latency: " << lat_ << "\tloss_percentage: " << plr_ << "\t start: " << _start_time[streamId]
					  << "\t stop: " << current_time << "\t bytes: " << total_bytes << "\t rate: " << rate;
			_bytes_read.erase(streamId);
			_start_time.erase(streamId);
		}

	}

	void readError(
		quic::StreamId streamId,
		std::pair <quic::QuicErrorCode, folly::Optional<folly::StringPiece>> error) noexcept override {
		LOG(ERROR) << "[readError] failed read from stream=" << streamId << ", error=" << toString(error);
		// A read error only terminates the ingress portion of the stream state.
		// Your application should probably terminate the egress portion via
		// resetStream
	}

	void onNewBidirectionalStream(quic::StreamId id) noexcept override {
		LOG(INFO) << "[onNewBidirectionalStream] new bidirectional stream=" << id;
		quicClient_->setReadCallback(id, this);
	}

	void onNewUnidirectionalStream(quic::StreamId id) noexcept override {
		LOG(INFO) << "[onNewUnidirectionalStream] new unidirectional stream=" << id;
		quicClient_->setReadCallback(id, this);
	}

	void onStopSending(
		quic::StreamId id, quic::ApplicationErrorCode /*error*/) noexcept override {
		VLOG(10) << "[onStopSending] got StopSending stream id=" << id;
	}

	void onConnectionEnd() noexcept override {
		LOG(INFO) << "[onConnectionEnd] connection end";
		connection_ended = true;
		if (!completed) {
			LOG(INFO) << "latency: " << lat_ << "\tloss_percentage: " << plr_ << "\t start: -1" << "\t stop: -1"
					  << "\t bytes: -1" << "\t rate: -1";
		}
	}

	void onConnectionError(
		std::pair <quic::QuicErrorCode, std::string> error) noexcept override {
		LOG(ERROR) << "[onConnectionError] " << toString(error.first);
		connection_failed = true;
		if (!completed) {
			LOG(INFO) << "latency: " << lat_ << "\tloss_percentage: " << plr_ << "\t start: -1" << "\t stop: -1"
					  << "\t bytes: -1" << "\t rate: -1";
		}
	}

	void onStreamWriteReady(
		quic::StreamId id, uint64_t maxToSend) noexcept override {
		LOG(INFO) << "[onStreamWriteReady] socket is write ready with maxToSend=" << maxToSend;
		sendMessage(id, pendingOutput_[id]);
	}

	void onStreamWriteError(
		quic::StreamId id,
		std::pair <quic::QuicErrorCode, folly::Optional<folly::StringPiece>> error) noexcept override {
		LOG(ERROR) << "[onStreamWriteError] write error with stream=" << id << " error=" << toString(error);
	}

	void onDataExpired(StreamId streamId, uint64_t newOffset) noexcept override {
		LOG(INFO) << "[onDataExpired] received skipData; " << newOffset - recvOffsets_[streamId]
				  << " bytes skipped on stream=" << streamId;
	}

	void start() {
		folly::ScopedEventBaseThread networkThread("DemoClientThread");
		auto evb = networkThread.getEventBase();
		folly::SocketAddress addr(host_.c_str(), port_);

		evb->runInEventBaseThreadAndWait(
			[&] {
				auto sock = std::make_unique<folly::AsyncUDPSocket>(evb);
				quicClient_ = std::make_shared<quic::QuicClientTransport>(evb, std::move(sock));
				quicClient_->setHostname("188.166.117.17");
				quicClient_->setCertificateVerifier(
					test::createTestCertificateVerifier());
				quicClient_->addNewPeerAddress(addr);
				TransportSettings settings;
				settings.partialReliabilityEnabled = false;
				quicClient_->setTransportSettings(settings);
				LOG(INFO) << "Connecting to " << addr.describe();
				quicClient_->start(this);
			});

		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		auto client = quicClient_;

		evb->runInEventBaseThreadAndWait(
			[=] {

				// create new stream for each message
				auto streamId = client->createBidirectionalStream().value();
				_start_time[streamId] = getTime();
				client->setReadCallback(streamId, this);
				pendingOutput_[streamId].append(folly::IOBuf::copyBuffer(bytes_));
				sendMessage(streamId, pendingOutput_[streamId]);

			});

		LOG(INFO) << "[Client] waiting for " << bytes_val_ << " bytes...";

		while (sum < bytes_val_ && !connection_ended && !connection_failed) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		client->closeTransport();
		LOG(INFO) << "[Client] received " << sum << " bytes... stopping client... | connection_ended: "
				  << connection_ended << " | connection_failed: " << connection_failed;
	}

	~DemoClient() override = default;

 private:
	void sendMessage(quic::StreamId id, folly::IOBufQueue &data) {

		auto message = data.move();
		auto res = quicClient_->writeChain(id, message->clone(), true, false);
		if (res.hasError()) {
			LOG(ERROR) << "[sendMessage] writeChain error=" << uint32_t(res.error());
		} else if (res.value()) {
			LOG(INFO) << "[sendMessage] socket did not accept all data, buffering len="
					  << res.value()->computeChainDataLength();
			data.append(std::move(res.value()));
			quicClient_->notifyPendingWriteOnStream(id, this);
		} else {
			// sent whole message
			pendingOutput_.erase(id);
		}
	}

	std::string host_;
	uint16_t port_;
	bool prEnabled_;
	std::shared_ptr <quic::QuicClientTransport> quicClient_;
	std::map <quic::StreamId, folly::IOBufQueue> pendingOutput_;
	std::map <quic::StreamId, uint64_t> recvOffsets_;
	std::string lat_;
	std::string plr_;
	std::string bytes_;
	int bytes_val_;

};
} // namespace samples
} // namespace quic
