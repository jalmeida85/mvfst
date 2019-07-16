/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 */

#pragma once

#include <quic/api/QuicSocket.h>

#include <folly/io/IOBufQueue.h>
#include <folly/io/async/EventBase.h>

namespace quic {
namespace samples {
class DemoHandler
	: public quic::QuicSocket::ConnectionCallback,
	  public quic::QuicSocket::ReadCallback,
	  public quic::QuicSocket::WriteCallback {
 public:
	using StreamData = std::pair<folly::IOBufQueue, bool>;

	explicit DemoHandler(folly::EventBase *evbIn, bool prEnabled = false) : evb(evbIn), prEnabled_(prEnabled) {}

	void setQuicSocket(std::shared_ptr <quic::QuicSocket> socket) {
		sock = socket;
	}

	void onNewBidirectionalStream(quic::StreamId id) noexcept override {
		LOG(INFO) << "[DemoHandler][onNewBidirectionalStream] Got bidirectional stream id=" << id;
		sock->setReadCallback(id, this);
	}

	void onNewUnidirectionalStream(quic::StreamId id) noexcept override {
		LOG(INFO) << "[DemoHandler][onNewUnidirectionalStream] Got unidirectional stream id=" << id;
		sock->setReadCallback(id, this);
	}

	void onStopSending(
		quic::StreamId id, quic::ApplicationErrorCode error) noexcept override {
		LOG(INFO) << "[DemoHandler][onStopSending] Got StopSending stream id=" << id << " error=" << error;
	}

	void onConnectionEnd() noexcept override {
		LOG(INFO) << "[DemoHandler][onConnectionEnd] Socket closed";
	}

	void onConnectionError(
		std::pair <quic::QuicErrorCode, std::string> error) noexcept override {
		LOG(ERROR) << "[DemoHandler][onConnectionError] Socket error=" << toString(error.first);
	}

	void readAvailable(quic::StreamId id) noexcept override {

		auto res = sock->read(id, 0);
		if (res.hasError()) {
			LOG(ERROR) << "[DemoHandler][readAvailable] Got error=" << toString(res.error());
			return;
		}

		if (input_.find(id) == input_.end()) {
			input_.emplace(id, std::make_pair(folly::IOBufQueue(folly::IOBufQueue::cacheChainLength()), false));
		}
		quic::Buf data = std::move(res.value().first);
		bool eof = res.value().second;
		auto dataLen = (data ? data->computeChainDataLength() : 0);
		LOG(INFO) << "[DemoHandler][readAvailable] Got len=" << dataLen << " eof=" << uint32_t(eof) << " total="
				  << input_[id].first.chainLength() + dataLen << " bytes="
				  << data->clone()->moveToFbString().toStdString();
		input_[id].first.append(std::move(data));
		input_[id].second = eof;
		if (eof) {
			sendBytes(id, input_[id]);
		}
	}

	void readError(
		quic::StreamId id,
		std::pair <quic::QuicErrorCode, folly::Optional<folly::StringPiece>> error) noexcept override {
		LOG(ERROR) << "[DemoHandler][readError] Got read error on stream=" << id << " error=" << toString(error);
		// A read error only terminates the ingress portion of the stream state.
		// Your application should probably terminate the egress portion via
		// resetStream
	}

	void sendBytes(quic::StreamId id, StreamData &data) {
		if (!data.second) {
			// only echo when eof is present
			return;
		}

		auto value = data.first.move();
		int bytes = std::stoi(value->clone()->moveToFbString().toStdString());

		int sent = 0;
		//int bufSize = kDefaultMaxUDPPayload;
		//int bufSize = kDefaultUDPSendPacketLen;
		int bufSize = 4 * kDefaultUDPReadBufferSize;
		//int bufSize = 4 * kDefaultUDPSendPacketLen;

		LOG(INFO) << "[Server] Will send " << bytes << " bytes";

		bytes *= 10;
		while (sent < bytes) {
			if (sent + bufSize > bytes) {
				int lastBytes = bytes - sent;
				auto buf = folly::IOBuf::create(lastBytes);
				buf->append(lastBytes);
				auto res = sock->writeChain(id, std::move(buf), true, false, nullptr);
				sent += lastBytes;

				if (res.hasError()) {
					LOG(ERROR) << "[DemoHandler][sendBytes] write error=" << toString(res.error());
				} else if (res.value()) {
					LOG(INFO) << "[DemoHandler][sendBytes] socket did not accept all data, buffering len="
							  << res.value()->computeChainDataLength();
					data.first.append(std::move(res.value()));
					sock->notifyPendingWriteOnStream(id, this);
				} else {
					LOG(INFO) << "[Sender] sent " << lastBytes << " bytes | total bytes sent: " << sent;
				}
			} else {
				auto buf = folly::IOBuf::create(bufSize);
				buf->append(bufSize);
				auto res = sock->writeChain(id, std::move(buf), false, true, nullptr);
				sent += bufSize;

				if (res.hasError()) {
					LOG(ERROR) << "[DemoHandler][echo] write error=" << toString(res.error());
				} else if (res.value()) {
					LOG(INFO) << "[DemoHandler][echo] socket did not accept all data, buffering len="
							  << res.value()->computeChainDataLength();
					data.first.append(std::move(res.value()));
					sock->notifyPendingWriteOnStream(id, this);
				} else {
					LOG(INFO) << "[Sender] sent " << bufSize << " bytes | total bytes sent: " << sent;
				}
			}
		}

		// echo is done, clear EOF
		data.second = false;
		LOG(INFO) << "[Server] finished sending | sent " << sent << " bytes";

	}

	void onStreamWriteReady(
		quic::StreamId id, uint64_t maxToSend) noexcept override {
		LOG(INFO) << "[DemoHandler][onStreamWriteReady] socket is write ready with maxToSend=" << maxToSend;
		sendBytes(id, input_[id]);
	}

	void onStreamWriteError(
		quic::StreamId id,
		std::pair <quic::QuicErrorCode, folly::Optional<folly::StringPiece>> error) noexcept override {
		LOG(ERROR) << "[DemoHandler][onStreamWriteError] write error with stream=" << id << " error="
				   << toString(error);
	}

	folly::EventBase *getEventBase() {
		return evb;
	}

	folly::EventBase *evb;
	std::shared_ptr <quic::QuicSocket> sock;

 private:
	std::map <quic::StreamId, StreamData> input_;
	bool prEnabled_;
};
} // namespace samples
} // namespace quic
