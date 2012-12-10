/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com
#include <iostream>

#include "prio.h"

#include "nsCOMPtr.h"
#include "nsNetCID.h"
#include "nsXPCOM.h"
#include "nsXPCOMGlue.h"

#include "nsIComponentManager.h"
#include "nsIComponentRegistrar.h"
#include "nsIIOService.h"
#include "nsIServiceManager.h"
#include "nsISocketTransportService.h"

#include "nsASocketHandler.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"

#include "mtransport_test_utils.h"

#define GTEST_HAS_RTTI 0
#include "gtest/gtest.h"
#include "gtest_utils.h"

MtransportTestUtils *test_utils;

namespace {
class SocketTransportServiceTest : public ::testing::Test {
 public:
  SocketTransportServiceTest() : received_(0),
                                 readpipe_(nullptr),
                                 writepipe_(nullptr),
                                 registered_(false) {
  }

  ~SocketTransportServiceTest() {
    if (readpipe_)
      PR_Close(readpipe_);
    if (writepipe_)
      PR_Close(writepipe_);
  }

  void SetUp();
  void RegisterHandler();
  void SendEvent();
  void SendPacket();

  void ReceivePacket() {
    ++received_;
  }

  void ReceiveEvent() {
    ++received_;
  }

  size_t Received() {
    return received_;
  }

 private:
  nsCOMPtr<nsISocketTransportService> stservice_;
  nsCOMPtr<nsIEventTarget> target_;
  size_t received_;
  PRFileDesc *readpipe_;
  PRFileDesc *writepipe_;
  bool registered_;
};


// Received an event.
class EventReceived : public nsRunnable {
public:
  EventReceived(SocketTransportServiceTest *test) :
      test_(test) {}

  NS_IMETHOD Run() {
    test_->ReceiveEvent();
    return NS_OK;
  }

  SocketTransportServiceTest *test_;
};


// Register our listener on the socket
class RegisterEvent : public nsRunnable {
public:
  RegisterEvent(SocketTransportServiceTest *test) :
      test_(test) {}

  NS_IMETHOD Run() {
    test_->RegisterHandler();
    return NS_OK;
  }

  SocketTransportServiceTest *test_;
};


class SocketHandler : public nsASocketHandler {
 public:
  SocketHandler(SocketTransportServiceTest *test) : test_(test) {
  }
  virtual ~SocketHandler() {}

  void OnSocketReady(PRFileDesc *fd, int16_t outflags) {
    unsigned char buf[1600];

    int32_t rv;
    rv = PR_Recv(fd, buf, sizeof(buf), 0, PR_INTERVAL_NO_WAIT);
    if (rv > 0) {
      std::cerr << "Read " << rv << " bytes" << std::endl;
      test_->ReceivePacket();
    }
  }

  void OnSocketDetached(PRFileDesc *fd) {}

  void IsLocal(bool *aIsLocal) {
    // TODO(jesup): better check? Does it matter? (likely no)
    *aIsLocal = false;
  }

  NS_DECL_ISUPPORTS

 private:
  SocketTransportServiceTest *test_;
};

NS_IMPL_ISUPPORTS0(SocketHandler)

void SocketTransportServiceTest::SetUp() {
  // Get the transport service as a dispatch target
  nsresult rv;
  target_ = do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &rv);
  ASSERT_TRUE(NS_SUCCEEDED(rv));

  // Get the transport service as a transport service
  stservice_ = do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &rv);
  ASSERT_TRUE(NS_SUCCEEDED(rv));

  // Create a loopback pipe
  PRStatus status = PR_CreatePipe(&readpipe_, &writepipe_);
  ASSERT_EQ(status, PR_SUCCESS);

  // Register ourselves as a listener for the read side of the
  // socket. The registration has to happen on the STS thread,
  // hence this event stuff.
  rv = target_->Dispatch(new RegisterEvent(this), 0);
  ASSERT_TRUE(NS_SUCCEEDED(rv));
  ASSERT_TRUE_WAIT(registered_, 10000);

}

void SocketTransportServiceTest::RegisterHandler() {
  nsresult rv;

  rv = stservice_->AttachSocket(readpipe_, new SocketHandler(this));
  ASSERT_TRUE(NS_SUCCEEDED(rv));

  registered_ = true;
}

void SocketTransportServiceTest::SendEvent() {
  nsresult rv;

  rv = target_->Dispatch(new EventReceived(this), 0);
  ASSERT_TRUE(NS_SUCCEEDED(rv));
  ASSERT_TRUE_WAIT(Received() == 1, 10000);
}

void SocketTransportServiceTest::SendPacket() {
  unsigned char buffer[1024];
  memset(buffer, 0, sizeof(buffer));

  int32_t status = PR_Write(writepipe_, buffer, sizeof(buffer));
  uint32_t size = status & 0xffff;
  ASSERT_EQ(sizeof(buffer), size);
}



// The unit tests themselves
TEST_F(SocketTransportServiceTest, SendEvent) {
  SendEvent();
}

TEST_F(SocketTransportServiceTest, SendPacket) {
  SendPacket();
}


}  // end namespace


int main(int argc, char **argv) {
  test_utils = new MtransportTestUtils();

  // Start the tests
  ::testing::InitGoogleTest(&argc, argv);

  int rv = RUN_ALL_TESTS();
  delete test_utils;
  return rv;
}
