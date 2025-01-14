/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Mats Palmgren <matspal@gmail.com>
 *   Geoff Lankow <geoff@darktrojan.net>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "nsFileControlFrame.h"

#include "nsIContent.h"
#include "prtypes.h"
#include "nsIAtom.h"
#include "nsPresContext.h"
#include "nsGkAtoms.h"
#include "nsWidgetsCID.h"
#include "nsIComponentManager.h"
#include "nsHTMLParts.h"
#include "nsIDOMHTMLInputElement.h"
#include "nsIFormControl.h"
#include "nsINameSpaceManager.h"
#include "nsCOMPtr.h"
#include "nsIDOMElement.h"
#include "nsIDOMDocument.h"
#include "nsIDocument.h"
#include "nsIPresShell.h"
#include "nsXPCOM.h"
#include "nsISupportsPrimitives.h"
#include "nsIComponentManager.h"
#include "nsPIDOMWindow.h"
#include "nsIFilePicker.h"
#include "nsIDOMMouseEvent.h"
#include "nsINodeInfo.h"
#include "nsIDOMEventTarget.h"
#include "nsILocalFile.h"
#include "nsHTMLInputElement.h"
#include "nsNodeInfoManager.h"
#include "nsContentCreatorFunctions.h"
#include "nsContentUtils.h"
#include "nsDisplayList.h"
#include "nsIDOMNSEvent.h"
#include "nsIDOMHTMLInputElement.h"
#include "nsEventListenerManager.h"
#ifdef ACCESSIBILITY
#include "nsAccessibilityService.h"
#endif

#include "nsInterfaceHashtable.h"
#include "nsURIHashKey.h"
#include "nsILocalFile.h"
#include "nsNetCID.h"
#include "nsWeakReference.h"
#include "nsIVariant.h"
#include "mozilla/Services.h"
#include "nsDirectoryServiceDefs.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsHTMLInputElement.h"
#include "nsICapturePicker.h"
#include "nsIFileURL.h"
#include "nsDOMFile.h"
#include "nsEventStates.h"

#include "nsIDOMDOMStringList.h"
#include "nsIDOMDragEvent.h"

namespace dom = mozilla::dom;

#define SYNC_TEXT 0x1
#define SYNC_BUTTON 0x2

nsIFrame*
NS_NewFileControlFrame(nsIPresShell* aPresShell, nsStyleContext* aContext)
{
  return new (aPresShell) nsFileControlFrame(aContext);
}

NS_IMPL_FRAMEARENA_HELPERS(nsFileControlFrame)

nsFileControlFrame::nsFileControlFrame(nsStyleContext* aContext):
  nsBlockFrame(aContext),
  mTextFrame(nsnull)
{
  AddStateBits(NS_BLOCK_FLOAT_MGR);
}


NS_IMETHODIMP
nsFileControlFrame::Init(nsIContent* aContent,
                         nsIFrame*   aParent,
                         nsIFrame*   aPrevInFlow)
{
  nsresult rv = nsBlockFrame::Init(aContent, aParent, aPrevInFlow);
  NS_ENSURE_SUCCESS(rv, rv);

  mMouseListener = new BrowseMouseListener(this);
  NS_ENSURE_TRUE(mMouseListener, NS_ERROR_OUT_OF_MEMORY);
  mCaptureMouseListener = new CaptureMouseListener(this);
  NS_ENSURE_TRUE(mCaptureMouseListener, NS_ERROR_OUT_OF_MEMORY);

  return rv;
}

void
nsFileControlFrame::DestroyFrom(nsIFrame* aDestructRoot)
{
  mTextFrame = nsnull;
  ENSURE_TRUE(mContent);

  // Remove the drag events
  nsCOMPtr<nsIDOMEventTarget> dragTarget = do_QueryInterface(mContent);
  if (dragTarget) {
    dragTarget->RemoveEventListener(NS_LITERAL_STRING("drop"),
                                    mMouseListener, PR_FALSE);
    dragTarget->RemoveEventListener(NS_LITERAL_STRING("dragover"),
                                    mMouseListener, PR_FALSE);
  }

  // remove mMouseListener as a mouse event listener (bug 40533, bug 355931)
  NS_NAMED_LITERAL_STRING(click, "click");

  nsContentUtils::DestroyAnonymousContent(&mCapture);

  nsEventListenerManager* elm = mBrowse->GetListenerManager(PR_FALSE);
  if (elm) {
    elm->RemoveEventListenerByType(mMouseListener, click,
                                   NS_EVENT_FLAG_BUBBLE |
                                   NS_EVENT_FLAG_SYSTEM_EVENT);
  }
  nsContentUtils::DestroyAnonymousContent(&mBrowse);

  elm = mTextContent->GetListenerManager(PR_FALSE);
  if (elm) {
    elm->RemoveEventListenerByType(mMouseListener, click,
                                   NS_EVENT_FLAG_BUBBLE |
                                   NS_EVENT_FLAG_SYSTEM_EVENT);
  }
  nsContentUtils::DestroyAnonymousContent(&mTextContent);

  mCaptureMouseListener->ForgetFrame();
  mMouseListener->ForgetFrame();
  nsBlockFrame::DestroyFrom(aDestructRoot);
}

struct CaptureCallbackData {
  nsICapturePicker* picker;
  PRUint32* mode;
};

typedef struct CaptureCallbackData CaptureCallbackData;

PRBool CapturePickerAcceptCallback(const nsAString& aAccept, void* aClosure)
{
  nsresult rv;
  PRBool captureEnabled;
  CaptureCallbackData* closure = (CaptureCallbackData*)aClosure;

  if (StringBeginsWith(aAccept,
                       NS_LITERAL_STRING("image/"))) {
    rv = closure->picker->ModeMayBeAvailable(nsICapturePicker::MODE_STILL,
                                             &captureEnabled);
    NS_ENSURE_SUCCESS(rv, PR_TRUE);
    if (captureEnabled) {
      *closure->mode = nsICapturePicker::MODE_STILL;
      return PR_FALSE;
    }
  } else if (StringBeginsWith(aAccept,
                              NS_LITERAL_STRING("audio/"))) {
    rv = closure->picker->ModeMayBeAvailable(nsICapturePicker::MODE_AUDIO_CLIP,
                                             &captureEnabled);
    NS_ENSURE_SUCCESS(rv, PR_TRUE);
    if (captureEnabled) {
      *closure->mode = nsICapturePicker::MODE_AUDIO_CLIP;
      return PR_FALSE;
    }
  } else if (StringBeginsWith(aAccept,
                              NS_LITERAL_STRING("video/"))) {
    rv = closure->picker->ModeMayBeAvailable(nsICapturePicker::MODE_VIDEO_CLIP,
                                             &captureEnabled);
    NS_ENSURE_SUCCESS(rv, PR_TRUE);
    if (captureEnabled) {
      *closure->mode = nsICapturePicker::MODE_VIDEO_CLIP;
      return PR_FALSE;
    }
    rv = closure->picker->ModeMayBeAvailable(nsICapturePicker::MODE_VIDEO_NO_SOUND_CLIP,
                                             &captureEnabled);
    NS_ENSURE_SUCCESS(rv, PR_TRUE);
    if (captureEnabled) {
      *closure->mode = nsICapturePicker::MODE_VIDEO_NO_SOUND_CLIP;
      return PR_FALSE;
    }
  }
  return PR_TRUE;
}

nsresult
nsFileControlFrame::CreateAnonymousContent(nsTArray<ContentInfo>& aElements)
{
  // Get the NodeInfoManager and tag necessary to create input elements
  nsCOMPtr<nsIDocument> doc = mContent->GetDocument();

  nsCOMPtr<nsINodeInfo> nodeInfo;
  nodeInfo = doc->NodeInfoManager()->GetNodeInfo(nsGkAtoms::input, nsnull,
                                                 kNameSpaceID_XHTML,
                                                 nsIDOMNode::ELEMENT_NODE);

  // Create the text content
  NS_NewHTMLElement(getter_AddRefs(mTextContent), nodeInfo.forget(),
                    dom::NOT_FROM_PARSER);
  if (!mTextContent)
    return NS_ERROR_OUT_OF_MEMORY;

  // Mark the element to be native anonymous before setting any attributes.
  mTextContent->SetNativeAnonymous();

  mTextContent->SetAttr(kNameSpaceID_None, nsGkAtoms::type,
                        NS_LITERAL_STRING("text"), PR_FALSE);

  nsHTMLInputElement* inputElement =
    nsHTMLInputElement::FromContent(mContent);
  NS_ASSERTION(inputElement, "Why is our content not a <input>?");

  // Initialize value when we create the content in case the value was set
  // before we got here
  nsAutoString value;
  inputElement->GetDisplayFileName(value);

  nsCOMPtr<nsIDOMHTMLInputElement> textControl = do_QueryInterface(mTextContent);
  NS_ASSERTION(textControl, "Why is the <input> we created not a <input>?");
  textControl->SetValue(value);

  textControl->SetTabIndex(-1);
  textControl->SetReadOnly(PR_TRUE);

  if (!aElements.AppendElement(mTextContent))
    return NS_ERROR_OUT_OF_MEMORY;

  // Register the whole frame as an event listener of drag events
  nsCOMPtr<nsIDOMEventTarget> dragTarget = do_QueryInterface(mContent);
  NS_ENSURE_STATE(dragTarget);
  dragTarget->AddEventListener(NS_LITERAL_STRING("drop"),
                               mMouseListener, PR_FALSE);
  dragTarget->AddEventListener(NS_LITERAL_STRING("dragover"),
                               mMouseListener, PR_FALSE);

  NS_NAMED_LITERAL_STRING(click, "click");
  nsEventListenerManager* manager = mTextContent->GetListenerManager(PR_TRUE);
  NS_ENSURE_STATE(manager);
  // Register as an event listener of the textbox
  // to open file dialog on mouse click
  manager->AddEventListenerByType(mMouseListener, click,
                                  NS_EVENT_FLAG_BUBBLE |
                                  NS_EVENT_FLAG_SYSTEM_EVENT);

  // Create the browse button
  nodeInfo = doc->NodeInfoManager()->GetNodeInfo(nsGkAtoms::input, nsnull,
                                                 kNameSpaceID_XHTML,
                                                 nsIDOMNode::ELEMENT_NODE);
  NS_NewHTMLElement(getter_AddRefs(mBrowse), nodeInfo.forget(),
                    dom::NOT_FROM_PARSER);
  if (!mBrowse)
    return NS_ERROR_OUT_OF_MEMORY;

  // Mark the element to be native anonymous before setting any attributes.
  mBrowse->SetNativeAnonymous();

  mBrowse->SetAttr(kNameSpaceID_None, nsGkAtoms::type,
                   NS_LITERAL_STRING("button"), PR_FALSE);

  // Create the capture button
  nsCOMPtr<nsICapturePicker> capturePicker;
  capturePicker = do_GetService("@mozilla.org/capturepicker;1");
  if (capturePicker) {
    PRUint32 mode = 0;

    CaptureCallbackData data;
    data.picker = capturePicker;
    data.mode = &mode;
    ParseAcceptAttribute(&CapturePickerAcceptCallback, (void*)&data);

    if (mode != 0) {
      mCaptureMouseListener->mMode = mode;
      nodeInfo = doc->NodeInfoManager()->GetNodeInfo(nsGkAtoms::input, nsnull,
                                                     kNameSpaceID_XHTML,
                                                     nsIDOMNode::ELEMENT_NODE);
      NS_NewHTMLElement(getter_AddRefs(mCapture), nodeInfo.forget(),
                        dom::NOT_FROM_PARSER);
      if (!mCapture)
        return NS_ERROR_OUT_OF_MEMORY;

      // Mark the element to be native anonymous before setting any attributes.
      mCapture->SetNativeAnonymous();

      mCapture->SetAttr(kNameSpaceID_None, nsGkAtoms::type,
                        NS_LITERAL_STRING("button"), PR_FALSE);

      mCapture->SetAttr(kNameSpaceID_None, nsGkAtoms::value,
                        NS_LITERAL_STRING("capture"), PR_FALSE);

      nsCOMPtr<nsIDOMEventTarget> captureEventTarget =
        do_QueryInterface(mCapture);
      captureEventTarget->AddEventListener(click, mCaptureMouseListener, PR_FALSE);
    }
  }
  nsCOMPtr<nsIDOMHTMLInputElement> fileContent = do_QueryInterface(mContent);
  nsCOMPtr<nsIDOMHTMLInputElement> browseControl = do_QueryInterface(mBrowse);
  if (fileContent && browseControl) {
    PRInt32 tabIndex;
    nsAutoString accessKey;

    fileContent->GetAccessKey(accessKey);
    browseControl->SetAccessKey(accessKey);
    fileContent->GetTabIndex(&tabIndex);
    browseControl->SetTabIndex(tabIndex);
  }

  if (!aElements.AppendElement(mBrowse))
    return NS_ERROR_OUT_OF_MEMORY;

  if (mCapture && !aElements.AppendElement(mCapture))
    return NS_ERROR_OUT_OF_MEMORY;

  nsCOMPtr<nsIDOMEventTarget> target = do_QueryInterface(mBrowse);
  NS_ENSURE_STATE(target);
  manager = target->GetListenerManager(PR_TRUE);
  NS_ENSURE_STATE(manager);
  // Register as an event listener of the button
  // to open file dialog on mouse click
  manager->AddEventListenerByType(mMouseListener, click,
                                  NS_EVENT_FLAG_BUBBLE |
                                  NS_EVENT_FLAG_SYSTEM_EVENT);

  SyncAttr(kNameSpaceID_None, nsGkAtoms::size,     SYNC_TEXT);
  SyncDisabledState();

  return NS_OK;
}

void
nsFileControlFrame::AppendAnonymousContentTo(nsBaseContentList& aElements,
                                             PRUint32 aFilter)
{
  aElements.MaybeAppendElement(mTextContent);
  aElements.MaybeAppendElement(mBrowse);
  aElements.MaybeAppendElement(mCapture);
}

NS_QUERYFRAME_HEAD(nsFileControlFrame)
  NS_QUERYFRAME_ENTRY(nsIAnonymousContentCreator)
  NS_QUERYFRAME_ENTRY(nsIFormControlFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsBlockFrame)

void 
nsFileControlFrame::SetFocus(PRBool aOn, PRBool aRepaint)
{
}

PRBool ShouldProcessMouseClick(nsIDOMEvent* aMouseEvent)
{
  // only allow the left button
  nsCOMPtr<nsIDOMMouseEvent> mouseEvent = do_QueryInterface(aMouseEvent);
  nsCOMPtr<nsIDOMNSEvent> domNSEvent = do_QueryInterface(aMouseEvent);
  NS_ENSURE_TRUE(mouseEvent && domNSEvent, PR_FALSE);
  PRBool defaultPrevented = PR_FALSE;
  domNSEvent->GetPreventDefault(&defaultPrevented);
  if (defaultPrevented) {
    return PR_FALSE;
  }

  PRUint16 whichButton;
  if (NS_FAILED(mouseEvent->GetButton(&whichButton)) || whichButton != 0) {
    return PR_FALSE;
  }

  PRInt32 clickCount;
  if (NS_FAILED(mouseEvent->GetDetail(&clickCount)) || clickCount > 1) {
    return PR_FALSE;
  }

  return PR_TRUE;
}

/**
 * This is called when our capture button is clicked
 */
NS_IMETHODIMP
nsFileControlFrame::CaptureMouseListener::HandleEvent(nsIDOMEvent* aMouseEvent)
{
  nsresult rv;

  NS_ASSERTION(mFrame, "We should have been unregistered");
  if (!ShouldProcessMouseClick(aMouseEvent))
    return NS_OK;

  // Get parent nsPIDOMWindow object.
  nsIContent* content = mFrame->GetContent();
  if (!content)
    return NS_ERROR_FAILURE;

  nsHTMLInputElement* inputElement = nsHTMLInputElement::FromContent(content);
  if (!inputElement)
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIDocument> doc = content->GetDocument();
  if (!doc)
    return NS_ERROR_FAILURE;

  // Get Loc title
  nsXPIDLString title;
  nsContentUtils::GetLocalizedString(nsContentUtils::eFORMS_PROPERTIES,
                                     "FileUpload", title);

  nsPIDOMWindow* win = doc->GetWindow();
  if (!win) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsICapturePicker> capturePicker;
  capturePicker = do_CreateInstance("@mozilla.org/capturepicker;1");
  if (!capturePicker)
    return NS_ERROR_FAILURE;

  rv = capturePicker->Init(win, title, mMode);
  NS_ENSURE_SUCCESS(rv, rv);

  // Tell our textframe to remember the currently focused value
  mFrame->mTextFrame->InitFocusedValue();

  // Show dialog
  PRUint32 result;
  rv = capturePicker->Show(&result);
  NS_ENSURE_SUCCESS(rv, rv);
  if (result == nsICapturePicker::RETURN_CANCEL)
    return NS_OK;

  if (!mFrame) {
    // The frame got destroyed while the filepicker was up.  Don't do
    // anything here.
    // (This listener itself can't be destroyed because the event listener
    // manager holds a strong reference to us while it fires the event.)
    return NS_OK;
  }

  nsCOMPtr<nsIDOMFile> domFile;
  rv = capturePicker->GetFile(getter_AddRefs(domFile));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMArray<nsIDOMFile> newFiles;
  if (domFile) {
    newFiles.AppendObject(domFile);
  } else {
    return NS_ERROR_FAILURE;
  }

  // XXXkhuey we really should have a better UI story than the tired old
  // uneditable text box with the file name inside.
  // Set new selected files
  if (newFiles.Count()) {
    // Tell mTextFrame that this update of the value is a user initiated
    // change. Otherwise it'll think that the value is being set by a script
    // and not fire onchange when it should.
    PRBool oldState = mFrame->mTextFrame->GetFireChangeEventState();
    mFrame->mTextFrame->SetFireChangeEventState(PR_TRUE);
    inputElement->SetFiles(newFiles, true);

    mFrame->mTextFrame->SetFireChangeEventState(oldState);
    // May need to fire an onchange here
    mFrame->mTextFrame->CheckFireOnChange();
  }

  return NS_OK;
}

/**
 * This is called when we receive any registered events on the control.
 * We've only registered for drop, dragover and click events.
 */
NS_IMETHODIMP
nsFileControlFrame::BrowseMouseListener::HandleEvent(nsIDOMEvent* aEvent)
{
  NS_ASSERTION(mFrame, "We should have been unregistered");

  nsAutoString eventType;
  aEvent->GetType(eventType);
  if (eventType.EqualsLiteral("click")) {
    if (!ShouldProcessMouseClick(aEvent))
      return NS_OK;
    
    nsHTMLInputElement* input =
      nsHTMLInputElement::FromContent(mFrame->GetContent());
    return input ? input->FireAsyncClickHandler() : NS_OK;
  }

  nsCOMPtr<nsIDOMNSEvent> domNSEvent = do_QueryInterface(aEvent);
  NS_ENSURE_STATE(domNSEvent);
  PRBool defaultPrevented = PR_FALSE;
  domNSEvent->GetPreventDefault(&defaultPrevented);
  if (defaultPrevented) {
    return NS_OK;
  }
  
  nsCOMPtr<nsIDOMDragEvent> dragEvent = do_QueryInterface(aEvent);
  if (!dragEvent || !IsValidDropData(dragEvent)) {
    return NS_OK;
  }

  if (eventType.EqualsLiteral("dragover")) {
    // Prevent default if we can accept this drag data
    aEvent->PreventDefault();
    return NS_OK;
  }

  if (eventType.EqualsLiteral("drop")) {
    aEvent->StopPropagation();
    aEvent->PreventDefault();

    nsIContent* content = mFrame->GetContent();
    NS_ASSERTION(content, "The frame has no content???");

    nsHTMLInputElement* inputElement = nsHTMLInputElement::FromContent(content);
    NS_ASSERTION(inputElement, "No input element for this file upload control frame!");

    nsCOMPtr<nsIDOMDataTransfer> dataTransfer;
    dragEvent->GetDataTransfer(getter_AddRefs(dataTransfer));

    nsCOMPtr<nsIDOMFileList> fileList;
    dataTransfer->GetFiles(getter_AddRefs(fileList));

    PRBool oldState = mFrame->mTextFrame->GetFireChangeEventState();
    mFrame->mTextFrame->SetFireChangeEventState(PR_TRUE);
    inputElement->SetFiles(fileList, true);
    mFrame->mTextFrame->SetFireChangeEventState(oldState);
    mFrame->mTextFrame->CheckFireOnChange();
  }

  return NS_OK;
}

/* static */ PRBool
nsFileControlFrame::BrowseMouseListener::IsValidDropData(nsIDOMDragEvent* aEvent)
{
  nsCOMPtr<nsIDOMDataTransfer> dataTransfer;
  aEvent->GetDataTransfer(getter_AddRefs(dataTransfer));
  NS_ENSURE_TRUE(dataTransfer, PR_FALSE);

  nsCOMPtr<nsIDOMDOMStringList> types;
  dataTransfer->GetTypes(getter_AddRefs(types));
  NS_ENSURE_TRUE(types, PR_FALSE);

  // We only support dropping files onto a file upload control
  PRBool typeSupported;
  types->Contains(NS_LITERAL_STRING("Files"), &typeSupported);
  return typeSupported;
}

nscoord
nsFileControlFrame::GetMinWidth(nsRenderingContext *aRenderingContext)
{
  nscoord result;
  DISPLAY_MIN_WIDTH(this, result);

  // Our min width is our pref width
  result = GetPrefWidth(aRenderingContext);
  return result;
}

NS_IMETHODIMP nsFileControlFrame::Reflow(nsPresContext*          aPresContext, 
                                         nsHTMLReflowMetrics&     aDesiredSize,
                                         const nsHTMLReflowState& aReflowState, 
                                         nsReflowStatus&          aStatus)
{
  DO_GLOBAL_REFLOW_COUNT("nsFileControlFrame");
  DISPLAY_REFLOW(aPresContext, this, aReflowState, aDesiredSize, aStatus);

  aStatus = NS_FRAME_COMPLETE;

  if (mState & NS_FRAME_FIRST_REFLOW) {
    mTextFrame = GetTextControlFrame(aPresContext, this);
    NS_ENSURE_TRUE(mTextFrame, NS_ERROR_UNEXPECTED);
  }

  // nsBlockFrame takes care of all our reflow
  return nsBlockFrame::Reflow(aPresContext, aDesiredSize, aReflowState,
                             aStatus);
}

nsNewFrame*
nsFileControlFrame::GetTextControlFrame(nsPresContext* aPresContext, nsIFrame* aStart)
{
  nsNewFrame* result = nsnull;
#ifndef DEBUG_NEWFRAME
  // find the text control frame.
  nsIFrame* childFrame = aStart->GetFirstPrincipalChild();

  while (childFrame) {
    // see if the child is a text control
    nsCOMPtr<nsIFormControl> formCtrl =
      do_QueryInterface(childFrame->GetContent());

    if (formCtrl && formCtrl->GetType() == NS_FORM_INPUT_TEXT) {
      result = (nsNewFrame*)childFrame;
    }

    // if not continue looking
    nsNewFrame* frame = GetTextControlFrame(aPresContext, childFrame);
    if (frame)
       result = frame;
     
    childFrame = childFrame->GetNextSibling();
  }

  return result;
#else
  return nsnull;
#endif
}

PRIntn
nsFileControlFrame::GetSkipSides() const
{
  return 0;
}

void
nsFileControlFrame::SyncAttr(PRInt32 aNameSpaceID, nsIAtom* aAttribute,
                             PRInt32 aWhichControls)
{
  nsAutoString value;
  if (mContent->GetAttr(aNameSpaceID, aAttribute, value)) {
    if (aWhichControls & SYNC_TEXT && mTextContent) {
      mTextContent->SetAttr(aNameSpaceID, aAttribute, value, PR_TRUE);
    }
    if (aWhichControls & SYNC_BUTTON && mBrowse) {
      mBrowse->SetAttr(aNameSpaceID, aAttribute, value, PR_TRUE);
    }
  } else {
    if (aWhichControls & SYNC_TEXT && mTextContent) {
      mTextContent->UnsetAttr(aNameSpaceID, aAttribute, PR_TRUE);
    }
    if (aWhichControls & SYNC_BUTTON && mBrowse) {
      mBrowse->UnsetAttr(aNameSpaceID, aAttribute, PR_TRUE);
    }
  }
}

void
nsFileControlFrame::SyncDisabledState()
{
  nsEventStates eventStates = mContent->AsElement()->State();
  if (eventStates.HasState(NS_EVENT_STATE_DISABLED)) {
    mTextContent->SetAttr(kNameSpaceID_None, nsGkAtoms::disabled, EmptyString(),
                          PR_TRUE);
    mBrowse->SetAttr(kNameSpaceID_None, nsGkAtoms::disabled, EmptyString(),
                     PR_TRUE);
  } else {
    mTextContent->UnsetAttr(kNameSpaceID_None, nsGkAtoms::disabled, PR_TRUE);
    mBrowse->UnsetAttr(kNameSpaceID_None, nsGkAtoms::disabled, PR_TRUE);
  }
}

NS_IMETHODIMP
nsFileControlFrame::AttributeChanged(PRInt32         aNameSpaceID,
                                     nsIAtom*        aAttribute,
                                     PRInt32         aModType)
{
  if (aNameSpaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::size) {
      SyncAttr(aNameSpaceID, aAttribute, SYNC_TEXT);
    } else if (aAttribute == nsGkAtoms::tabindex) {
      SyncAttr(aNameSpaceID, aAttribute, SYNC_BUTTON);
    }
  }

  return nsBlockFrame::AttributeChanged(aNameSpaceID, aAttribute, aModType);
}

void
nsFileControlFrame::ContentStatesChanged(nsEventStates aStates)
{
  if (aStates.HasState(NS_EVENT_STATE_DISABLED)) {
    nsContentUtils::AddScriptRunner(new SyncDisabledStateEvent(this));
  }
}

PRBool
nsFileControlFrame::IsLeaf() const
{
  return PR_TRUE;
}

#ifdef NS_DEBUG
NS_IMETHODIMP
nsFileControlFrame::GetFrameName(nsAString& aResult) const
{
  return MakeFrameName(NS_LITERAL_STRING("FileControl"), aResult);
}
#endif

nsresult
nsFileControlFrame::SetFormProperty(nsIAtom* aName,
                                    const nsAString& aValue)
{
  if (nsGkAtoms::value == aName) {
    nsCOMPtr<nsIDOMHTMLInputElement> textControl =
      do_QueryInterface(mTextContent);
    NS_ASSERTION(textControl,
                 "The text control should exist and be an input element");
    textControl->SetValue(aValue);
  }
  return NS_OK;
}      

nsresult
nsFileControlFrame::GetFormProperty(nsIAtom* aName, nsAString& aValue) const
{
  aValue.Truncate();  // initialize out param

  if (nsGkAtoms::value == aName) {
    nsHTMLInputElement* inputElement =
      nsHTMLInputElement::FromContent(mContent);

    if (inputElement) {
      inputElement->GetDisplayFileName(aValue);
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsFileControlFrame::BuildDisplayList(nsDisplayListBuilder*   aBuilder,
                                     const nsRect&           aDirtyRect,
                                     const nsDisplayListSet& aLists)
{
  // box-shadow
  if (GetStyleBorder()->mBoxShadow) {
    nsresult rv = aLists.BorderBackground()->AppendNewToTop(new (aBuilder)
        nsDisplayBoxShadowOuter(aBuilder, this));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Our background is inherited to the text input, and we don't really want to
  // paint it or out padding and borders (which we never have anyway, per
  // styles in forms.css) -- doing it just makes us look ugly in some cases and
  // has no effect in others.
  nsDisplayListCollection tempList;
  nsresult rv = nsBlockFrame::BuildDisplayList(aBuilder, aDirtyRect, tempList);
  if (NS_FAILED(rv))
    return rv;

  tempList.BorderBackground()->DeleteAll();

  // Clip height only
  nsRect clipRect(aBuilder->ToReferenceFrame(this), GetSize());
  clipRect.width = GetVisualOverflowRect().XMost();
  nscoord radii[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  rv = OverflowClip(aBuilder, tempList, aLists, clipRect, radii);
  NS_ENSURE_SUCCESS(rv, rv);

  // Disabled file controls don't pass mouse events to their children, so we
  // put an invisible item in the display list above the children
  // just to catch events
  nsEventStates eventStates = mContent->AsElement()->State();
  if (eventStates.HasState(NS_EVENT_STATE_DISABLED) && IsVisibleForPainting(aBuilder)) {
    rv = aLists.Content()->AppendNewToTop(
        new (aBuilder) nsDisplayEventReceiver(aBuilder, this));
    if (NS_FAILED(rv))
      return rv;
  }

  return DisplaySelectionOverlay(aBuilder, aLists.Content());
}

#ifdef ACCESSIBILITY
already_AddRefed<nsAccessible>
nsFileControlFrame::CreateAccessible()
{
  // Accessible object exists just to hold onto its children, for later shutdown
  nsAccessibilityService* accService = nsIPresShell::AccService();
  if (!accService)
    return nsnull;

  return accService->CreateHyperTextAccessible(mContent,
                                               PresContext()->PresShell());
}
#endif

void 
nsFileControlFrame::ParseAcceptAttribute(AcceptAttrCallback aCallback,
                                         void* aClosure) const
{
  nsAutoString accept;
  mContent->GetAttr(kNameSpaceID_None, nsGkAtoms::accept, accept);

  nsCharSeparatedTokenizerTemplate<nsContentUtils::IsHTMLWhitespace>
    tokenizer(accept, ',');
  // Empty loop body because aCallback is doing the work
  while (tokenizer.hasMoreTokens() &&
         (*aCallback)(tokenizer.nextToken(), aClosure));
}

////////////////////////////////////////////////////////////
// Mouse listener implementation

NS_IMPL_ISUPPORTS1(nsFileControlFrame::MouseListener,
                   nsIDOMEventListener)
