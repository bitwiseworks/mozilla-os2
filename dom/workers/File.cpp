/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "File.h"

#include "nsIDOMFile.h"
#include "nsDOMBlobBuilder.h"
#include "nsError.h"

#include "jsapi.h"
#include "jsatom.h"
#include "jsfriendapi.h"
#include "nsCOMPtr.h"
#include "nsJSUtils.h"
#include "nsStringGlue.h"

#include "Exceptions.h"
#include "WorkerInlines.h"
#include "WorkerPrivate.h"

USING_WORKERS_NAMESPACE

using mozilla::dom::workers::exceptions::ThrowDOMExceptionForNSResult;

namespace {

class Blob
{
  // Blob should never be instantiated.
  Blob();
  ~Blob();

  static JSClass sClass;
  static JSPropertySpec sProperties[];
  static JSFunctionSpec sFunctions[];

public:
  static JSObject*
  InitClass(JSContext* aCx, JSObject* aObj)
  {
    return JS_InitClass(aCx, aObj, NULL, &sClass, Construct, 0, sProperties,
                        sFunctions, NULL, NULL);
  }

  static JSObject*
  Create(JSContext* aCx, nsIDOMBlob* aBlob)
  {
    JS_ASSERT(SameCOMIdentity(static_cast<nsISupports*>(aBlob), aBlob));

    JSObject* obj = JS_NewObject(aCx, &sClass, NULL, NULL);
    if (obj) {
      JS_SetPrivate(obj, aBlob);
      NS_ADDREF(aBlob);
    }
    return obj;
  }

  static nsIDOMBlob*
  GetPrivate(JSObject* aObj);

private:
  static nsIDOMBlob*
  GetInstancePrivate(JSContext* aCx, JSObject* aObj, const char* aFunctionName)
  {
    nsIDOMBlob* blob = GetPrivate(aObj);
    if (blob) {
      return blob;
    }

    JS_ReportErrorNumber(aCx, js_GetErrorMessage, NULL,
                         JSMSG_INCOMPATIBLE_PROTO, sClass.name, aFunctionName,
                         JS_GetClass(aObj)->name);
    return NULL;
  }

  static nsIDOMBlob*
  Unwrap(JSContext* aCx, JSObject* aObj)
  {
    return GetPrivate(aObj);
  }

  static JSBool
  Construct(JSContext* aCx, unsigned aArgc, jsval* aVp)
  {
    nsRefPtr<nsDOMMultipartFile> file = new nsDOMMultipartFile();
    nsresult rv = file->InitInternal(aCx, aArgc, JS_ARGV(aCx, aVp),
                                     Unwrap);
    if (NS_FAILED(rv)) {
      ThrowDOMExceptionForNSResult(aCx, rv);
      return false;
    }

    JSObject* obj = file::CreateBlob(aCx, file);
    if (!obj) {
      return false;
    }

    JS_SET_RVAL(aCx, aVp, OBJECT_TO_JSVAL(obj));
    return true;
  }

  static void
  Finalize(JSFreeOp* aFop, JSObject* aObj)
  {
    JS_ASSERT(JS_GetClass(aObj) == &sClass);

    nsIDOMBlob* blob = GetPrivate(aObj);
    NS_IF_RELEASE(blob);
  }

  static bool
  IsBlob(const JS::Value& v)
  {
    return v.isObject() && GetPrivate(&v.toObject()) != nullptr;
  }

  static bool
  GetSizeImpl(JSContext* aCx, JS::CallArgs aArgs)
  {
    JS::Rooted<JSObject*> obj(aCx, &aArgs.thisv().toObject());
    nsIDOMBlob* blob = GetInstancePrivate(aCx, obj, "size");
    MOZ_ASSERT(blob);

    uint64_t size;
    if (NS_FAILED(blob->GetSize(&size))) {
      ThrowDOMExceptionForNSResult(aCx, NS_ERROR_DOM_FILE_NOT_READABLE_ERR);
      return false;
    }

    aArgs.rval().setNumber(double(size));
    return true;
  }

  static JSBool
  GetSize(JSContext* aCx, unsigned aArgc, JS::Value* aVp)
  {
    JS::CallArgs args = JS::CallArgsFromVp(aArgc, aVp);
    return JS::CallNonGenericMethod<IsBlob, GetSizeImpl>(aCx, args);
  }

  static bool
  GetTypeImpl(JSContext* aCx, JS::CallArgs aArgs)
  {
    JS::Rooted<JSObject*> obj(aCx, &aArgs.thisv().toObject());
    nsIDOMBlob* blob = GetInstancePrivate(aCx, obj, "type");
    if (!blob) {
      return false;
    }

    nsString type;
    if (NS_FAILED(blob->GetType(type))) {
      ThrowDOMExceptionForNSResult(aCx, NS_ERROR_DOM_FILE_NOT_READABLE_ERR);
      return false;
    }

    JSString* jsType = JS_NewUCStringCopyN(aCx, type.get(), type.Length());
    if (!jsType) {
      return false;
    }

    aArgs.rval().setString(jsType);
    return true;
  }

  static JSBool
  GetType(JSContext* aCx, unsigned aArgc, JS::Value* aVp)
  {
    JS::CallArgs args = JS::CallArgsFromVp(aArgc, aVp);
    return JS::CallNonGenericMethod<IsBlob, GetTypeImpl>(aCx, args);
  }

  static JSBool
  Slice(JSContext* aCx, unsigned aArgc, jsval* aVp)
  {
    JSObject* obj = JS_THIS_OBJECT(aCx, aVp);
    if (!obj) {
      return false;
    }

    nsIDOMBlob* blob = GetInstancePrivate(aCx, obj, "slice");
    if (!blob) {
      return false;
    }

    double start = 0, end = 0;
    JSString* jsContentType = JS_GetEmptyString(JS_GetRuntime(aCx));
    if (!JS_ConvertArguments(aCx, aArgc, JS_ARGV(aCx, aVp), "/IIS", &start,
                             &end, &jsContentType)) {
      return false;
    }

    nsDependentJSString contentType;
    if (!contentType.init(aCx, jsContentType)) {
      return false;
    }

    uint8_t optionalArgc = aArgc;
    nsCOMPtr<nsIDOMBlob> rtnBlob;
    if (NS_FAILED(blob->Slice(static_cast<uint64_t>(start),
                              static_cast<uint64_t>(end),
                              contentType, optionalArgc,
                              getter_AddRefs(rtnBlob)))) {
      ThrowDOMExceptionForNSResult(aCx, NS_ERROR_DOM_FILE_NOT_READABLE_ERR);
      return false;
    }

    JSObject* rtnObj = file::CreateBlob(aCx, rtnBlob);
    if (!rtnObj) {
      return false;
    }

    JS_SET_RVAL(aCx, aVp, OBJECT_TO_JSVAL(rtnObj));
    return true;
  }
};

JSClass Blob::sClass = {
  "Blob",
  JSCLASS_HAS_PRIVATE,
  JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_StrictPropertyStub,
  JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, Finalize
};

JSPropertySpec Blob::sProperties[] = {
  JS_PSGS("size", GetSize, GetterOnlyJSNative, JSPROP_ENUMERATE),
  JS_PSGS("type", GetType, GetterOnlyJSNative, JSPROP_ENUMERATE),
  JS_PS_END
};

JSFunctionSpec Blob::sFunctions[] = {
  JS_FN("slice", Slice, 1, JSPROP_ENUMERATE),
  JS_FS_END
};

class File : public Blob
{
  // File should never be instantiated.
  File();
  ~File();

  static JSClass sClass;
  static JSPropertySpec sProperties[];

public:
  static JSObject*
  InitClass(JSContext* aCx, JSObject* aObj, JSObject* aParentProto)
  {
    return JS_InitClass(aCx, aObj, aParentProto, &sClass, Construct, 0,
                        sProperties, NULL, NULL, NULL);
  }

  static JSObject*
  Create(JSContext* aCx, nsIDOMFile* aFile)
  {
    JS_ASSERT(SameCOMIdentity(static_cast<nsISupports*>(aFile), aFile));

    JSObject* obj = JS_NewObject(aCx, &sClass, NULL, NULL);
    if (obj) {
      JS_SetPrivate(obj, aFile);
      NS_ADDREF(aFile);
    }
    return obj;
  }

  static nsIDOMFile*
  GetPrivate(JSObject* aObj)
  {
    if (aObj) {
      JSClass* classPtr = JS_GetClass(aObj);
      if (classPtr == &sClass) {
        nsISupports* priv = static_cast<nsISupports*>(JS_GetPrivate(aObj));
        nsCOMPtr<nsIDOMFile> file = do_QueryInterface(priv);
        JS_ASSERT_IF(priv, file);
        return file;
      }
    }
    return NULL;
  }

  static JSClass*
  Class()
  {
    return &sClass;
  }

private:
  static nsIDOMFile*
  GetInstancePrivate(JSContext* aCx, JSObject* aObj, const char* aFunctionName)
  {
    nsIDOMFile* file = GetPrivate(aObj);
    if (file) {
      return file;
    }

    JS_ReportErrorNumber(aCx, js_GetErrorMessage, NULL,
                         JSMSG_INCOMPATIBLE_PROTO, sClass.name, aFunctionName,
                         JS_GetClass(aObj)->name);
    return NULL;
  }

  static JSBool
  Construct(JSContext* aCx, unsigned aArgc, jsval* aVp)
  {
    JS_ReportErrorNumber(aCx, js_GetErrorMessage, NULL, JSMSG_WRONG_CONSTRUCTOR,
                         sClass.name);
    return false;
  }

  static void
  Finalize(JSFreeOp* aFop, JSObject* aObj)
  {
    JS_ASSERT(JS_GetClass(aObj) == &sClass);

    nsIDOMFile* file = GetPrivate(aObj);
    NS_IF_RELEASE(file);
  }

  static bool
  IsFile(const JS::Value& v)
  {
    return v.isObject() && GetPrivate(&v.toObject()) != nullptr;
  }

  static bool
  GetMozFullPathImpl(JSContext* aCx, JS::CallArgs aArgs)
  {
    JS::Rooted<JSObject*> obj(aCx, &aArgs.thisv().toObject());
    nsIDOMFile* file = GetInstancePrivate(aCx, obj, "mozFullPath");
    MOZ_ASSERT(file);

    nsString fullPath;

    if (GetWorkerPrivateFromContext(aCx)->UsesSystemPrincipal() &&
        NS_FAILED(file->GetMozFullPathInternal(fullPath))) {
      ThrowDOMExceptionForNSResult(aCx, NS_ERROR_DOM_FILE_NOT_READABLE_ERR);
      return false;
    }

    JSString* jsFullPath = JS_NewUCStringCopyN(aCx, fullPath.get(),
                                               fullPath.Length());
    if (!jsFullPath) {
      return false;
    }

    aArgs.rval().setString(jsFullPath);
    return true;
  }

  static JSBool
  GetMozFullPath(JSContext* aCx, unsigned aArgc, JS::Value* aVp)
  {
    JS::CallArgs args = JS::CallArgsFromVp(aArgc, aVp);
    return JS::CallNonGenericMethod<IsFile, GetMozFullPathImpl>(aCx, args);
  }

  static bool
  GetNameImpl(JSContext* aCx, JS::CallArgs aArgs)
  {
    JS::Rooted<JSObject*> obj(aCx, &aArgs.thisv().toObject());
    nsIDOMFile* file = GetInstancePrivate(aCx, obj, "name");
    MOZ_ASSERT(file);

    nsString name;
    if (NS_FAILED(file->GetName(name))) {
      name.Truncate();
    }

    JSString* jsName = JS_NewUCStringCopyN(aCx, name.get(), name.Length());
    if (!jsName) {
      return false;
    }

    aArgs.rval().setString(jsName);
    return true;
  }

  static JSBool
  GetName(JSContext* aCx, unsigned aArgc, JS::Value* aVp)
  {
    JS::CallArgs args = JS::CallArgsFromVp(aArgc, aVp);
    return JS::CallNonGenericMethod<IsFile, GetNameImpl>(aCx, args);
  }
};

JSClass File::sClass = {
  "File",
  JSCLASS_HAS_PRIVATE,
  JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_StrictPropertyStub,
  JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, Finalize
};

JSPropertySpec File::sProperties[] = {
  JS_PSGS("name", GetName, GetterOnlyJSNative, JSPROP_ENUMERATE),
  JS_PSGS("mozFullPath", GetMozFullPath, GetterOnlyJSNative, JSPROP_ENUMERATE),
  JS_PS_END
};

nsIDOMBlob*
Blob::GetPrivate(JSObject* aObj)
{
  if (aObj) {
    JSClass* classPtr = JS_GetClass(aObj);
    if (classPtr == &sClass || classPtr == File::Class()) {
      nsISupports* priv = static_cast<nsISupports*>(JS_GetPrivate(aObj));
      nsCOMPtr<nsIDOMBlob> blob = do_QueryInterface(priv);
      JS_ASSERT_IF(priv, blob);
      return blob;
    }
  }
  return NULL;
}

} // anonymous namespace

BEGIN_WORKERS_NAMESPACE

namespace file {

JSObject*
CreateBlob(JSContext* aCx, nsIDOMBlob* aBlob)
{
  return Blob::Create(aCx, aBlob);
}

bool
InitClasses(JSContext* aCx, JSObject* aGlobal)
{
  JSObject* blobProto = Blob::InitClass(aCx, aGlobal);
  return blobProto && File::InitClass(aCx, aGlobal, blobProto);
}

nsIDOMBlob*
GetDOMBlobFromJSObject(JSObject* aObj)
{
  return Blob::GetPrivate(aObj);
}

JSObject*
CreateFile(JSContext* aCx, nsIDOMFile* aFile)
{
  return File::Create(aCx, aFile);
}

nsIDOMFile*
GetDOMFileFromJSObject(JSObject* aObj)
{
  return File::GetPrivate(aObj);
}

} // namespace file

END_WORKERS_NAMESPACE
