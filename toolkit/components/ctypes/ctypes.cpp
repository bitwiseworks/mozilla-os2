/* -*-  Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ctypes.h"
#include "jsapi.h"
#include "mozilla/ModuleUtils.h"
#include "nsMemory.h"
#include "nsString.h"
#include "nsNativeCharsetUtils.h"
#include "mozilla/Preferences.h"
#include "mozJSComponentLoader.h"
#include "nsZipArchive.h"
#include "xpc_make_class.h"

#define JSCTYPES_CONTRACTID \
  "@mozilla.org/jsctypes;1"


#define JSCTYPES_CID \
{ 0xc797702, 0x1c60, 0x4051, { 0x9d, 0xd7, 0x4d, 0x74, 0x5, 0x60, 0x56, 0x42 } }

namespace mozilla {
namespace ctypes {

// copy of what's in dom/workers/ChromeWorkerScope.cpp
static char*
UnicodeToNative(JSContext* aCx, const char16_t* aSource, size_t aSourceLen)
{
  nsDependentString unicode(aSource, aSourceLen);

  nsAutoCString native;
  if (NS_FAILED(NS_CopyUnicodeToNative(unicode, native))) {
    JS_ReportErrorASCII(aCx, "Could not convert string to native charset!");
    return nullptr;
  }

  char* result = static_cast<char*>(JS_malloc(aCx, native.Length() + 1));
  if (!result) {
    return nullptr;
  }

  memcpy(result, native.get(), native.Length());
  result[native.Length()] = 0;
  return result;
}

// copy of what's in dom/workers/ChromeWorkerScope.cpp
static char16_t*
NativeToUnicode(JSContext* aCx, const char* aSource, size_t aSourceLen)
{
  nsDependentCString native(aSource, aSourceLen);

  nsAutoString unicode;
  if (NS_FAILED(NS_CopyNativeToUnicode(native, unicode))) {
    JS_ReportError(aCx, "Could not convert string to unicode charset!");
    return nullptr;
  }

  char16_t* result = static_cast<char16_t*>(JS_malloc(aCx, (unicode.Length() + 1) * sizeof(char16_t)));
  if (!result) {
    return nullptr;
  }

  memcpy(result, unicode.get(), unicode.Length() * sizeof(char16_t));
  result[unicode.Length()] = 0;
  return result;
}

static JSCTypesCallbacks sCallbacks = {
  UnicodeToNative, NativeToUnicode
};

NS_GENERIC_FACTORY_CONSTRUCTOR(Module)

NS_IMPL_ISUPPORTS(Module, nsIXPCScriptable)

Module::Module()
{
}

Module::~Module()
{
}

#define XPC_MAP_CLASSNAME Module
#define XPC_MAP_QUOTED_CLASSNAME "Module"
#define XPC_MAP_WANT_CALL
#define XPC_MAP_FLAGS nsIXPCScriptable::WANT_CALL
#include "xpc_map_end.h"

static bool
SealObjectAndPrototype(JSContext* cx, JS::Handle<JSObject *> parent, const char* name)
{
  JS::Rooted<JS::Value> prop(cx);
  if (!JS_GetProperty(cx, parent, name, &prop))
    return false;

  if (prop.isUndefined()) {
    // Pretend we sealed the object.
    return true;
  }

  JS::Rooted<JSObject*> obj(cx, prop.toObjectOrNull());
  if (!JS_GetProperty(cx, obj, "prototype", &prop))
    return false;

  JS::Rooted<JSObject*> prototype(cx, prop.toObjectOrNull());
  return JS_FreezeObject(cx, obj) && JS_FreezeObject(cx, prototype);
}

static bool
InitAndSealCTypesClass(JSContext* cx, JS::Handle<JSObject*> global)
{
  // Init the ctypes object.
  if (!JS_InitCTypesClass(cx, global))
    return false;

  // Set callbacks for charset conversion and such.
  JS::Rooted<JS::Value> ctypes(cx);
  if (!JS_GetProperty(cx, global, "ctypes", &ctypes))
    return false;

  JS_SetCTypesCallbacks(ctypes.toObjectOrNull(), &sCallbacks);

  // Seal up Object, Function, Array and Error and their prototypes.  (This
  // single object instance is shared amongst everyone who imports the ctypes
  // module.)
  if (!SealObjectAndPrototype(cx, global, "Object") ||
      !SealObjectAndPrototype(cx, global, "Function") ||
      !SealObjectAndPrototype(cx, global, "Array") ||
      !SealObjectAndPrototype(cx, global, "Error"))
    return false;

  return true;
}

NS_IMETHODIMP
Module::Call(nsIXPConnectWrappedNative* wrapper,
             JSContext* cx,
             JSObject* obj,
             const JS::CallArgs& args,
             bool* _retval)
{
  mozJSComponentLoader* loader = mozJSComponentLoader::Get();
  JS::Rooted<JSObject*> targetObj(cx);
  nsresult rv = loader->FindTargetObject(cx, &targetObj);
  NS_ENSURE_SUCCESS(rv, rv);

  *_retval = InitAndSealCTypesClass(cx, targetObj);
  return NS_OK;
}

} // namespace ctypes
} // namespace mozilla

NS_DEFINE_NAMED_CID(JSCTYPES_CID);

static const mozilla::Module::CIDEntry kCTypesCIDs[] = {
  { &kJSCTYPES_CID, false, nullptr, mozilla::ctypes::ModuleConstructor },
  { nullptr }
};

static const mozilla::Module::ContractIDEntry kCTypesContracts[] = {
  { JSCTYPES_CONTRACTID, &kJSCTYPES_CID },
  { nullptr }
};

static const mozilla::Module kCTypesModule = {
  mozilla::Module::kVersion,
  kCTypesCIDs,
  kCTypesContracts
};

NSMODULE_DEFN(jsctypes) = &kCTypesModule;
