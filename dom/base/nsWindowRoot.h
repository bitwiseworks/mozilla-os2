/* -*- Mode: C++; tab-width: 3; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsWindowRoot_h__
#define nsWindowRoot_h__

class nsPIDOMWindow;
class nsIDOMEventListener;
class nsEventListenerManager;
class nsIDOMEvent;
class nsEventChainPreVisitor;
class nsEventChainPostVisitor;

#include "nsIDOMEventTarget.h"
#include "nsEventListenerManager.h"
#include "nsPIWindowRoot.h"
#include "nsCycleCollectionParticipant.h"

class nsWindowRoot : public nsPIWindowRoot
{
public:
  nsWindowRoot(nsPIDOMWindow* aWindow);
  virtual ~nsWindowRoot();

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_NSIDOMEVENTTARGET

  // nsPIWindowRoot

  virtual nsPIDOMWindow* GetWindow();

  virtual nsresult GetControllers(nsIControllers** aResult);
  virtual nsresult GetControllerForCommand(const char * aCommand,
                                           nsIController** _retval);

  virtual nsIDOMNode* GetPopupNode();
  virtual void SetPopupNode(nsIDOMNode* aNode);

  virtual void SetParentTarget(nsIDOMEventTarget* aTarget)
  {
    mParent = aTarget;
  }
  virtual nsIDOMEventTarget* GetParentTarget() { return mParent; }

  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(nsWindowRoot, nsIDOMEventTarget)

protected:
  // Members
  nsCOMPtr<nsPIDOMWindow> mWindow;
  nsRefPtr<nsEventListenerManager> mListenerManager; // [Strong]. We own the manager, which owns event listeners attached
                                                      // to us.

  nsCOMPtr<nsIDOMNode> mPopupNode; // [OWNER]

  nsCOMPtr<nsIDOMEventTarget> mParent;
};

extern nsresult
NS_NewWindowRoot(nsPIDOMWindow* aWindow,
                 nsIDOMEventTarget** aResult);

#endif
