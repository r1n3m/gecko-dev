/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef CDMProxy_h_
#define CDMProxy_h_

#include "nsString.h"
#include "nsAutoPtr.h"
#include "mozilla/dom/MediaKeys.h"
#include "mozilla/dom/TypedArray.h"
#include "mozilla/Monitor.h"
#include "nsIThread.h"
#include "GMPDecryptorProxy.h"
#include "mozilla/CDMCaps.h"
#include "mp4_demuxer/DecoderData.h"

namespace mozilla {

class CDMCallbackProxy;

namespace dom {
class MediaKeySession;
}

class DecryptionClient {
public:
  virtual ~DecryptionClient() {}
  virtual void Decrypted(nsresult aResult,
                         mp4_demuxer::MP4Sample* aSample) = 0;
};

// Proxies calls GMP/CDM, and proxies calls back.
// Note: Promises are passed in via a PromiseId, so that the ID can be
// passed via IPC to the CDM, which can then signal when to reject or
// resolve the promise using its PromiseId.
class CDMProxy {
  typedef dom::PromiseId PromiseId;
  typedef dom::SessionType SessionType;
  typedef dom::Uint8Array Uint8Array;
public:

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(CDMProxy)

  // Main thread only.
  CDMProxy(dom::MediaKeys* aKeys, const nsAString& aKeySystem);

  // Main thread only.
  // Loads the CDM corresponding to mKeySystem.
  // Calls MediaKeys::OnCDMCreated() when the CDM is created.
  void Init(PromiseId aPromiseId);

  // Main thread only.
  // Uses the CDM to create a key session.
  // Caller is responsible for calling aInitData.ComputeLengthAndData().
  // Calls MediaKeys::OnSessionActivated() when session is created.
  void CreateSession(dom::SessionType aSessionType,
                     PromiseId aPromiseId,
                     const nsAString& aInitDataType,
                     const Uint8Array& aInitData);

  // Main thread only.
  // Uses the CDM to load a presistent session stored on disk.
  // Calls MediaKeys::OnSessionActivated() when session is loaded.
  void LoadSession(PromiseId aPromiseId,
                   const nsAString& aSessionId);

  // Main thread only.
  // Sends a new certificate to the CDM.
  // Caller is responsible for calling aCert.ComputeLengthAndData().
  // Calls MediaKeys->ResolvePromise(aPromiseId) after the CDM has
  // processed the request.
  void SetServerCertificate(PromiseId aPromiseId,
                            const Uint8Array& aCert);

  // Main thread only.
  // Sends an update to the CDM.
  // Caller is responsible for calling aResponse.ComputeLengthAndData().
  // Calls MediaKeys->ResolvePromise(aPromiseId) after the CDM has
  // processed the request.
  void UpdateSession(const nsAString& aSessionId,
                     PromiseId aPromiseId,
                     const Uint8Array& aResponse);

  // Main thread only.
  // Calls MediaKeys->ResolvePromise(aPromiseId) after the CDM has
  // processed the request.
  // If processing this operation results in the session actually closing,
  // we also call MediaKeySession::OnClosed(), which in turn calls
  // MediaKeys::OnSessionClosed().
  void CloseSession(const nsAString& aSessionId,
                    PromiseId aPromiseId);

  // Main thread only.
  // Removes all data for a persisent session.
  // Calls MediaKeys->ResolvePromise(aPromiseId) after the CDM has
  // processed the request.
  void RemoveSession(const nsAString& aSessionId,
                     PromiseId aPromiseId);

  // Main thread only.
  void Shutdown();

  // Threadsafe.
  const nsAString& GetOrigin() const;

  // Main thread only.
  void OnResolveNewSessionPromise(uint32_t aPromiseId,
                                  const nsAString& aSessionId);

  // Main thread only.
  void OnSessionMessage(const nsAString& aSessionId,
                        nsTArray<uint8_t>& aMessage,
                        const nsAString& aDestinationURL);

  // Main thread only.
  void OnExpirationChange(const nsAString& aSessionId,
                          GMPTimestamp aExpiryTime);

  // Main thread only.
  void OnSessionClosed(const nsAString& aSessionId);

  // Main thread only.
  void OnSessionError(const nsAString& aSessionId,
                      nsresult aException,
                      uint32_t aSystemCode,
                      const nsAString& aMsg);

  // Main thread only.
  void OnRejectPromise(uint32_t aPromiseId,
                       nsresult aDOMException,
                       const nsAString& aMsg);

  // Threadsafe.
  void Decrypt(mp4_demuxer::MP4Sample* aSample,
               DecryptionClient* aSink);

  // Reject promise with DOMException corresponding to aExceptionCode.
  // Can be called from any thread.
  void RejectPromise(PromiseId aId, nsresult aExceptionCode);

  // Resolves promise with "undefined".
  // Can be called from any thread.
  void ResolvePromise(PromiseId aId);

  // Threadsafe.
  const nsString& KeySystem() const;

  // GMP thread only.
  void gmp_Decrypted(uint32_t aId,
                     GMPErr aResult,
                     const nsTArray<uint8_t>& aDecryptedData);

  // GMP thread only.
  void gmp_Terminated();

  CDMCaps& Capabilites();

#ifdef DEBUG
  bool IsOnGMPThread();
#endif

private:

  // GMP thread only.
  void gmp_Init(uint32_t aPromiseId);

  // GMP thread only.
  void gmp_Shutdown();

  // Main thread only.
  void OnCDMCreated(uint32_t aPromiseId);

  struct CreateSessionData {
    dom::SessionType mSessionType;
    PromiseId mPromiseId;
    nsAutoCString mInitDataType;
    nsTArray<uint8_t> mInitData;
  };
  // GMP thread only.
  void gmp_CreateSession(nsAutoPtr<CreateSessionData> aData);

  struct SessionOpData {
    PromiseId mPromiseId;
    nsAutoCString mSessionId;
  };
  // GMP thread only.
  void gmp_LoadSession(nsAutoPtr<SessionOpData> aData);

  struct SetServerCertificateData {
    PromiseId mPromiseId;
    nsTArray<uint8_t> mCert;
  };
  // GMP thread only.
  void gmp_SetServerCertificate(nsAutoPtr<SetServerCertificateData> aData);

  struct UpdateSessionData {
    PromiseId mPromiseId;
    nsAutoCString mSessionId;
    nsTArray<uint8_t> mResponse;
  };
  // GMP thread only.
  void gmp_UpdateSession(nsAutoPtr<UpdateSessionData> aData);

  // GMP thread only.
  void gmp_CloseSession(nsAutoPtr<SessionOpData> aData);

  // GMP thread only.
  void gmp_RemoveSession(nsAutoPtr<SessionOpData> aData);

  struct DecryptJob {
    DecryptJob(mp4_demuxer::MP4Sample* aSample,
               DecryptionClient* aClient)
      : mId(0)
      , mSample(aSample)
      , mClient(aClient)
    {}
    uint32_t mId;
    nsAutoPtr<mp4_demuxer::MP4Sample> mSample;
    nsAutoPtr<DecryptionClient> mClient;
  };
  // GMP thread only.
  void gmp_Decrypt(nsAutoPtr<DecryptJob> aJob);

  class RejectPromiseTask : public nsRunnable {
  public:
    RejectPromiseTask(CDMProxy* aProxy,
                      PromiseId aId,
                      nsresult aCode)
      : mProxy(aProxy)
      , mId(aId)
      , mCode(aCode)
    {
    }
    NS_METHOD Run() {
      mProxy->RejectPromise(mId, mCode);
      return NS_OK;
    }
  private:
    nsRefPtr<CDMProxy> mProxy;
    PromiseId mId;
    nsresult mCode;
  };

  ~CDMProxy();

  // Helper to enforce that a raw pointer is only accessed on the main thread.
  template<class Type>
  class MainThreadOnlyRawPtr {
  public:
    MainThreadOnlyRawPtr(Type* aPtr)
      : mPtr(aPtr)
    {
      MOZ_ASSERT(NS_IsMainThread());
    }

    bool IsNull() const {
      MOZ_ASSERT(NS_IsMainThread());
      return !mPtr;
    }

    void Clear() {
      MOZ_ASSERT(NS_IsMainThread());
      mPtr = nullptr;
    }

    Type* operator->() const {
      MOZ_ASSERT(NS_IsMainThread());
      return mPtr;
    }
  private:
    Type* mPtr;
  };

  // Our reference back to the MediaKeys object.
  // WARNING: This is a non-owning reference that is cleared by MediaKeys
  // destructor. only use on main thread, and always nullcheck before using!
  MainThreadOnlyRawPtr<dom::MediaKeys> mKeys;

  const nsAutoString mKeySystem;

  // Gecko Media Plugin thread. All interactions with the out-of-process
  // EME plugin must come from this thread.
  nsRefPtr<nsIThread> mGMPThread;

  nsAutoString mOrigin;

  GMPDecryptorProxy* mCDM;
  CDMCaps mCapabilites;
  nsAutoPtr<CDMCallbackProxy> mCallback;

  // Decryption jobs sent to CDM, awaiting result.
  // GMP thread only.
  nsTArray<nsAutoPtr<DecryptJob>> mDecryptionJobs;

  // Number of buffers we've decrypted. Used to uniquely identify
  // decryption jobs sent to CDM. Note we can't just use the length of
  // mDecryptionJobs as that shrinks as jobs are completed and removed
  // from it.
  // GMP thread only.
  uint32_t mDecryptionJobCount;
};


} // namespace mozilla

#endif // CDMProxy_h_
