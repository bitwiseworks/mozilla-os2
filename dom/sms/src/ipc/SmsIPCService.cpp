/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef MOZ_IPC
#include "mozilla/dom/ContentChild.h"
#else
#include "base/basictypes.h"
#endif
#include "SmsIPCService.h"
#include "nsXULAppAPI.h"
#include "jsapi.h"
#include "mozilla/dom/sms/SmsChild.h"
#include "mozilla/dom/sms/SmsMessage.h"
#include "SmsFilter.h"

namespace mozilla {
namespace dom {
namespace sms {

PSmsChild* SmsIPCService::sSmsChild = nullptr;

NS_IMPL_ISUPPORTS2(SmsIPCService, nsISmsService, nsISmsDatabaseService)

/* static */ PSmsChild*
SmsIPCService::GetSmsChild()
{
#ifdef MOZ_IPC
  if (!sSmsChild) {
    sSmsChild = ContentChild::GetSingleton()->SendPSmsConstructor();
  }
#endif

  return sSmsChild;
}

/*
 * Implementation of nsISmsService.
 */
NS_IMETHODIMP
SmsIPCService::HasSupport(bool* aHasSupport)
{
#ifdef MOZ_IPC
  GetSmsChild()->SendHasSupport(aHasSupport);

  return NS_OK;
#else
  return NS_ERROR_NOT_IMPLEMENTED;
#endif
}

NS_IMETHODIMP
SmsIPCService::GetNumberOfMessagesForText(const nsAString& aText, uint16_t* aResult)
{
#ifdef MOZ_IPC
  GetSmsChild()->SendGetNumberOfMessagesForText(nsString(aText), aResult);

  return NS_OK;
#else
  return NS_ERROR_NOT_IMPLEMENTED;
#endif
}

NS_IMETHODIMP
SmsIPCService::Send(const nsAString& aNumber, const nsAString& aMessage,
                    int32_t aRequestId, uint64_t aProcessId)
{
#ifdef MOZ_IPC
  GetSmsChild()->SendSendMessage(nsString(aNumber), nsString(aMessage),
                                 aRequestId, ContentChild::GetSingleton()->GetID());

  return NS_OK;
#else
  return NS_ERROR_NOT_IMPLEMENTED;
#endif
}

NS_IMETHODIMP
SmsIPCService::CreateSmsMessage(int32_t aId,
                                const nsAString& aDelivery,
                                const nsAString& aSender,
                                const nsAString& aReceiver,
                                const nsAString& aBody,
                                const jsval& aTimestamp,
                                const bool aRead,
                                JSContext* aCx,
                                nsIDOMMozSmsMessage** aMessage)
{
  return SmsMessage::Create(aId, aDelivery, aSender, aReceiver, aBody,
                            aTimestamp, aRead, aCx, aMessage);
}

/*
 * Implementation of nsISmsDatabaseService.
 */
NS_IMETHODIMP
SmsIPCService::SaveReceivedMessage(const nsAString& aSender,
                                   const nsAString& aBody,
                                   uint64_t aDate, int32_t* aId)
{
#ifdef MOZ_IPC
  GetSmsChild()->SendSaveReceivedMessage(nsString(aSender), nsString(aBody),
                                         aDate, aId);

  return NS_OK;
#else
  return NS_ERROR_NOT_IMPLEMENTED;
#endif
}

NS_IMETHODIMP
SmsIPCService::SaveSentMessage(const nsAString& aReceiver,
                               const nsAString& aBody,
                               uint64_t aDate, int32_t* aId)
{
#ifdef MOZ_IPC
  GetSmsChild()->SendSaveSentMessage(nsString(aReceiver), nsString(aBody),
                                     aDate, aId);

  return NS_OK;
#else
  return NS_ERROR_NOT_IMPLEMENTED;
#endif
}

NS_IMETHODIMP
SmsIPCService::GetMessageMoz(int32_t aMessageId, int32_t aRequestId,
                             uint64_t aProcessId)
{
#ifdef MOZ_IPC
  GetSmsChild()->SendGetMessage(aMessageId, aRequestId,
                                ContentChild::GetSingleton()->GetID());
  return NS_OK;
#else
  return NS_ERROR_NOT_IMPLEMENTED;
#endif
}

NS_IMETHODIMP
SmsIPCService::DeleteMessage(int32_t aMessageId, int32_t aRequestId,
                             uint64_t aProcessId)
{
#ifdef MOZ_IPC
  GetSmsChild()->SendDeleteMessage(aMessageId, aRequestId,
                                   ContentChild::GetSingleton()->GetID());
  return NS_OK;
#else
  return NS_ERROR_NOT_IMPLEMENTED;
#endif
}

NS_IMETHODIMP
SmsIPCService::CreateMessageList(nsIDOMMozSmsFilter* aFilter, bool aReverse,
                                 int32_t aRequestId, uint64_t aProcessId)
{
#ifdef MOZ_IPC
  SmsFilter* filter = static_cast<SmsFilter*>(aFilter);
  GetSmsChild()->SendCreateMessageList(filter->GetData(), aReverse, aRequestId,
                                       ContentChild::GetSingleton()->GetID());

  return NS_OK;
#else
  return NS_ERROR_NOT_IMPLEMENTED;
#endif
}

NS_IMETHODIMP
SmsIPCService::GetNextMessageInList(int32_t aListId, int32_t aRequestId,
                                    uint64_t aProcessId)
{
#ifdef MOZ_IPC
  GetSmsChild()->SendGetNextMessageInList(aListId, aRequestId,
                                          ContentChild::GetSingleton()->GetID());
  return NS_OK;
#else
  return NS_ERROR_NOT_IMPLEMENTED;
#endif
}

NS_IMETHODIMP
SmsIPCService::ClearMessageList(int32_t aListId)
{
#ifdef MOZ_IPC
  GetSmsChild()->SendClearMessageList(aListId);
  return NS_OK;
#else
  return NS_ERROR_NOT_IMPLEMENTED;
#endif
}

NS_IMETHODIMP
SmsIPCService::MarkMessageRead(int32_t aMessageId, bool aValue,
                               int32_t aRequestId, uint64_t aProcessId)
{
#ifdef MOZ_IPC
  GetSmsChild()->SendMarkMessageRead(aMessageId, aValue, aRequestId,
                                     ContentChild::GetSingleton()->GetID());
  return NS_OK;
#else
  return NS_ERROR_NOT_IMPLEMENTED;
#endif
}

} // namespace sms
} // namespace dom
} // namespace mozilla
