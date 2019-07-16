/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 */

#include <glog/logging.h>

#include <fizz/crypto/Utils.h>
#include <folly/init/Init.h>
#include <folly/portability/GFlags.h>

#include "DemoClient.h"
#include "DemoServer.h"

DEFINE_string(host, "::1", "Server hostname/IP");
DEFINE_int32(port, 6668, "Server port");
DEFINE_string(mode, "server", "Mode to run in: 'client' or 'server'");
DEFINE_bool(pr, false, "Enable partially reliable mode");

using namespace quic::samples;

int main(int argc, char* argv[]) {
#if FOLLY_HAVE_LIBGFLAGS
	// Enable glog logging to stderr by default.
  gflags::SetCommandLineOptionWithMode(
      "logtostderr", "1", gflags::SET_FLAGS_DEFAULT);
#endif
	gflags::ParseCommandLineFlags(&argc, &argv, false);
	folly::Init init(&argc, &argv);
	fizz::CryptoUtils::init();

	if (FLAGS_mode == "server") {
		DemoServer server(FLAGS_host, FLAGS_port, FLAGS_pr);
		server.start();
	} else if (FLAGS_mode == "client") {
		if (FLAGS_host.empty() || FLAGS_port == 0) {
			LOG(ERROR) << "FileTransfer expected --host and --port";
			return -2;
		}
		DemoClient client(argv[1], argv[2], argv[3], FLAGS_host, FLAGS_port, FLAGS_pr);
		client.start();
	} else {
		LOG(ERROR) << "Unknown mode specified: " << FLAGS_mode;
		return -1;
	}
	return 0;
}
